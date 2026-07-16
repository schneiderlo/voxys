#include "physics/gpu/gpu_dynamic_solver.hpp"

#include "gpu/resources.hpp"
#include "physics/gpu/deterministic_primitives.hpp"

#include <algorithm>
#include <array>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace voxy::physics {
namespace {

constexpr uint32_t kParameterStride = 256;
constexpr uint32_t kParameterSlots = 512;
constexpr uint32_t kTelemetryWords = GpuDynamicSolver::kTelemetryWordCount;
constexpr uint32_t kUnconditionalColorRounds = 8;

template <typename T>
void releaseHandle(T& handle, void (*release)(T)) {
    if (handle) {
        release(handle);
        handle = nullptr;
    }
}

void releaseBuffer(WGPUBuffer& buffer) {
    if (buffer) {
        wgpuBufferDestroy(buffer);
        wgpuBufferRelease(buffer);
        buffer = nullptr;
    }
}

WGPUComputePipeline makePipeline(WGPUDevice device, WGPUPipelineLayout layout,
                                 WGPUShaderModule shader,
                                 const std::string& entryPoint,
                                 const char* label) {
    WGPUComputePipelineDescriptor desc{};
    WGPU_SET_LABEL(desc, label);
    desc.layout = layout;
    desc.compute.module = shader;
    WGPU_SET_ENTRY_POINT(desc.compute, entryPoint.c_str());
    return wgpuDeviceCreateComputePipeline(device, &desc);
}

} // namespace

class GpuDynamicSolver::Impl {
public:
    static constexpr size_t kInputGroupCount = 10;

    struct alignas(16) Params {
        std::array<uint32_t, 4> capacities{};
        std::array<uint32_t, 4> control{};
        std::array<float, 4> gravityDt{};
        std::array<float, 4> dampingSlop{};
        std::array<float, 4> solver{};
        std::array<float, 4> material{};
    };

    struct alignas(kParameterStride) ParameterUploadSlot {
        Params params{};
        std::array<std::byte, kParameterStride - sizeof(Params)> padding{};
    };

    static_assert(sizeof(ParameterUploadSlot) == kParameterStride);

    struct CachedInputGroups {
        WGPUBuffer manifoldBuffer = nullptr;
        std::array<WGPUBindGroup, kInputGroupCount> groups{};
    };

    ~Impl() { shutdown(); }

    bool initialize(WGPUDevice device, WGPUQueue queue, const Config& config) {
        if (device_ || !device || !queue || config.bodyCapacity == 0
            || config.contactCapacity == 0 || config.colorCount == 0
            || config.colorCount > kGpuSolverMaximumColors
            || config.substeps == 0 || config.overflowIterations == 0
            || config.tickSeconds <= 0.0f || config.linearSlop <= 0.0f
            || config.speculativeDistance < 0.0f
            || (config.workgroupSize != 64 && config.workgroupSize != 128
                && config.workgroupSize != 256)) {
            return false;
        }
        const uint64_t claims = uint64_t{config.bodyCapacity}
                              * config.colorCount;
        const uint64_t endpoints = uint64_t{config.contactCapacity} * 2u;
        if (claims > std::numeric_limits<uint32_t>::max()
            || endpoints > std::numeric_limits<uint32_t>::max()) return false;
        device_ = device;
        queue_ = queue;
        config_ = config;
        claimCapacity_ = static_cast<uint32_t>(claims);
        endpointCapacity_ = static_cast<uint32_t>(endpoints);

        DeterministicGpuPrimitives::Config primitiveConfig;
        primitiveConfig.capacity = endpointCapacity_;
        primitiveConfig.workgroupSize = config.workgroupSize;
        primitiveConfig.shaderPath = config.primitivesShaderPath;
        if (!primitives_.initialize(device_, queue_, primitiveConfig)) {
            shutdown();
            return false;
        }

        parameterBuffer_ = gpu::createBuffer(device_, gpu::BufferDesc{
            .label = "dynamic_solver_params",
            .size = uint64_t{kParameterSlots} * kParameterStride,
            .usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst,
        });
        const auto makeStorage = [this](uint64_t bytes, const char* label) {
            return gpu::createBuffer(device_, gpu::BufferDesc{
                .label = label,
                .size = bytes,
                .usage = WGPUBufferUsage_Storage | WGPUBufferUsage_CopyDst
                       | WGPUBufferUsage_CopySrc,
            });
        };
        colors_ = makeStorage(uint64_t{config.contactCapacity} * sizeof(uint32_t),
                              "solver_contact_colors");
        acceptedMasks_ = makeStorage(
            uint64_t{config.bodyCapacity} * sizeof(uint32_t),
            "solver_accepted_color_masks");
        claims_ = makeStorage(uint64_t{claimCapacity_} * sizeof(uint32_t),
                              "solver_color_claims");
        candidates_ = makeStorage(
            uint64_t{config.contactCapacity} * sizeof(uint32_t),
            "solver_candidate_colors");
        colorRecords_ = makeStorage(
            uint64_t{config.contactCapacity} * sizeof(GpuKeyValue),
            "solver_color_records");
        sortedColorRecords_ = makeStorage(
            uint64_t{config.contactCapacity} * sizeof(GpuKeyValue),
            "solver_sorted_color_records");
        colorRanges_ = makeStorage(
            uint64_t{config.colorCount + 1u} * 2u * sizeof(uint32_t),
            "solver_color_ranges");
        telemetry_ = makeStorage(kTelemetryWords * sizeof(uint32_t),
                                 "solver_telemetry");
        caches_ = makeStorage(
            uint64_t{config.contactCapacity} * sizeof(GpuConstraintCache),
            "solver_constraint_cache");
        adjacencyRecords_ = makeStorage(
            uint64_t{endpointCapacity_} * sizeof(GpuKeyValue),
            "solver_adjacency_records");
        sortedAdjacency_ = makeStorage(
            uint64_t{endpointCapacity_} * sizeof(GpuKeyValue),
            "solver_sorted_adjacency");
        bodyRanges_ = makeStorage(
            uint64_t{config.bodyCapacity} * 2u * sizeof(uint32_t),
            "solver_body_ranges");
        bodyDegrees_ = makeStorage(
            uint64_t{config.bodyCapacity} * sizeof(uint32_t),
            "solver_body_degrees");
        dispatchArgs_ = gpu::createBuffer(device_, gpu::BufferDesc{
            .label = "solver_color_dispatch_args",
            .size = uint64_t{config.colorCount + 7u} * 4u * sizeof(uint32_t),
            .usage = WGPUBufferUsage_Storage | WGPUBufferUsage_Indirect
                   | WGPUBufferUsage_CopyDst | WGPUBufferUsage_CopySrc,
        });
        endpointDeltas_ = makeStorage(
            uint64_t{endpointCapacity_} * sizeof(GpuEndpointDelta),
            "solver_endpoint_deltas");
        if (!parameterBuffer_ || !colors_ || !acceptedMasks_ || !claims_
            || !candidates_ || !colorRecords_ || !sortedColorRecords_
            || !colorRanges_ || !telemetry_ || !caches_
            || !adjacencyRecords_ || !sortedAdjacency_ || !bodyRanges_
            || !bodyDegrees_ || !dispatchArgs_ || !endpointDeltas_) {
            shutdown();
            return false;
        }
        const std::array<uint32_t, kTelemetryWords> zeroTelemetry{};
        gpu::writeBuffer(queue_, telemetry_, 0, zeroTelemetry);
        scratchBytes_ = gpu::saturatingSize(
            primitives_.scratchBytes()
            + uint64_t{kParameterSlots} * kParameterStride
            + uint64_t{config.contactCapacity}
                * (3u * sizeof(uint32_t) + 2u * sizeof(GpuKeyValue)
                   + sizeof(GpuConstraintCache))
            + uint64_t{config.bodyCapacity} * 4u * sizeof(uint32_t)
            + uint64_t{claimCapacity_} * sizeof(uint32_t)
            + uint64_t{endpointCapacity_}
                * (2u * sizeof(GpuKeyValue) + sizeof(GpuEndpointDelta))
            + uint64_t{config.colorCount + 1u} * 2u * sizeof(uint32_t)
            + uint64_t{config.colorCount + 7u} * 4u * sizeof(uint32_t)
            + kTelemetryWords * sizeof(uint32_t));

        shaderModule_ = gpu::loadShaderModule(
            device_, config.shaderPath, "physics_dynamic_solver.wgsl");
        if (!shaderModule_ || !createPipelines()) {
            shutdown();
            return false;
        }
        if (!createCachedStaticGroups()) {
            shutdown();
            return false;
        }
        return true;
    }

    bool createPipelines() {
        using LE = gpu::BindGroupLayoutEntry;
        const auto storage = [](std::vector<LE>& entries, uint32_t binding,
                                bool readOnly) {
            entries.emplace_back(binding).computeVisible().storageBuffer(readOnly);
        };
        const auto uniform = [](std::vector<LE>& entries) {
            entries.emplace_back(14).computeVisible().uniformBuffer(
                true, sizeof(Params));
        };
        const auto makeLayout = [this](std::span<const LE> entries,
                                       const char* label) {
            return gpu::createBindGroupLayout(device_, entries, label);
        };

        std::vector<LE> coloringEntries;
        storage(coloringEntries, 4, false);
        storage(coloringEntries, 5, true);
        for (uint32_t binding : {6u, 7u, 8u, 9u, 10u})
            storage(coloringEntries, binding, false);
        storage(coloringEntries, 13, false);
        uniform(coloringEntries);
        coloringLayout_ = makeLayout(coloringEntries, "solver_coloring_layout");

        std::vector<LE> classificationEntries;
        storage(classificationEntries, 4, false);
        storage(classificationEntries, 5, true);
        storage(classificationEntries, 6, false);
        storage(classificationEntries, 13, false);
        storage(classificationEntries, 20, false);
        storage(classificationEntries, 21, false);
        uniform(classificationEntries);
        classificationLayout_ = makeLayout(
            classificationEntries, "solver_island_classification_layout");

        std::vector<LE> rangeEntries;
        storage(rangeEntries, 5, true);
        storage(rangeEntries, 11, false);
        storage(rangeEntries, 12, false);
        storage(rangeEntries, 13, false);
        storage(rangeEntries, 21, false);
        uniform(rangeEntries);
        rangeLayout_ = makeLayout(rangeEntries, "solver_range_layout");

        std::vector<LE> adjacencyEntries;
        storage(adjacencyEntries, 4, false);
        storage(adjacencyEntries, 11, false);
        storage(adjacencyEntries, 12, false);
        storage(adjacencyEntries, 13, false);
        storage(adjacencyEntries, 16, false);
        storage(adjacencyEntries, 18, false);
        uniform(adjacencyEntries);
        adjacencyLayout_ = makeLayout(adjacencyEntries, "solver_adjacency_layout");

        std::vector<LE> bodyRangeEntries;
        storage(bodyRangeEntries, 12, false);
        storage(bodyRangeEntries, 13, false);
        storage(bodyRangeEntries, 17, false);
        storage(bodyRangeEntries, 18, false);
        uniform(bodyRangeEntries);
        bodyRangeLayout_ = makeLayout(bodyRangeEntries, "solver_body_range_layout");

        std::vector<LE> prepareEntries;
        storage(prepareEntries, 0, false);
        storage(prepareEntries, 1, false);
        storage(prepareEntries, 2, true);
        storage(prepareEntries, 3, false);
        storage(prepareEntries, 4, false);
        storage(prepareEntries, 5, true);
        storage(prepareEntries, 15, false);
        uniform(prepareEntries);
        prepareLayout_ = makeLayout(prepareEntries, "solver_prepare_layout");

        std::vector<LE> solveEntries;
        storage(solveEntries, 0, false);
        storage(solveEntries, 1, false);
        storage(solveEntries, 2, true);
        storage(solveEntries, 4, false);
        storage(solveEntries, 11, false);
        storage(solveEntries, 12, false);
        storage(solveEntries, 15, false);
        storage(solveEntries, 19, false);
        uniform(solveEntries);
        solveLayout_ = makeLayout(solveEntries, "solver_constraint_layout");

        std::vector<LE> gatherEntries;
        storage(gatherEntries, 1, false);
        storage(gatherEntries, 17, false);
        storage(gatherEntries, 18, false);
        storage(gatherEntries, 19, false);
        uniform(gatherEntries);
        gatherLayout_ = makeLayout(gatherEntries, "solver_gather_layout");

        std::vector<LE> integrateEntries;
        storage(integrateEntries, 0, false);
        storage(integrateEntries, 1, false);
        storage(integrateEntries, 2, true);
        storage(integrateEntries, 3, false);
        storage(integrateEntries, 20, false);
        uniform(integrateEntries);
        integrateLayout_ = makeLayout(integrateEntries, "solver_integrate_layout");

        std::vector<LE> smallIslandEntries;
        storage(smallIslandEntries, 0, false);
        storage(smallIslandEntries, 1, false);
        storage(smallIslandEntries, 2, true);
        storage(smallIslandEntries, 3, false);
        storage(smallIslandEntries, 4, false);
        storage(smallIslandEntries, 5, true);
        storage(smallIslandEntries, 15, false);
        storage(smallIslandEntries, 20, false);
        uniform(smallIslandEntries);
        smallIslandLayout_ = makeLayout(
            smallIslandEntries, "solver_small_island_layout");

        std::vector<LE> serialEntries;
        storage(serialEntries, 0, false);
        storage(serialEntries, 1, false);
        storage(serialEntries, 2, true);
        storage(serialEntries, 3, false);
        storage(serialEntries, 4, false);
        storage(serialEntries, 5, true);
        storage(serialEntries, 13, false);
        storage(serialEntries, 15, false);
        uniform(serialEntries);
        serialLayout_ = makeLayout(serialEntries, "solver_serial_layout");

        std::vector<LE> finishEntries;
        storage(finishEntries, 13, false);
        uniform(finishEntries);
        finishLayout_ = makeLayout(finishEntries, "solver_finish_layout");
        if (!coloringLayout_ || !classificationLayout_ || !rangeLayout_
            || !adjacencyLayout_
            || !bodyRangeLayout_ || !prepareLayout_ || !solveLayout_
            || !gatherLayout_ || !integrateLayout_ || !smallIslandLayout_
            || !serialLayout_
            || !finishLayout_) {
            return false;
        }

        const auto pipelineLayout = [this](WGPUBindGroupLayout layout,
                                            const char* label) {
            return gpu::createPipelineLayout(device_, std::array{layout}, label);
        };
        coloringPipelineLayout_ = pipelineLayout(
            coloringLayout_, "solver_coloring_pipeline_layout");
        classificationPipelineLayout_ = pipelineLayout(
            classificationLayout_, "solver_island_classification_pipeline_layout");
        rangePipelineLayout_ = pipelineLayout(
            rangeLayout_, "solver_range_pipeline_layout");
        adjacencyPipelineLayout_ = pipelineLayout(
            adjacencyLayout_, "solver_adjacency_pipeline_layout");
        bodyRangePipelineLayout_ = pipelineLayout(
            bodyRangeLayout_, "solver_body_range_pipeline_layout");
        preparePipelineLayout_ = pipelineLayout(
            prepareLayout_, "solver_prepare_pipeline_layout");
        solvePipelineLayout_ = pipelineLayout(
            solveLayout_, "solver_constraint_pipeline_layout");
        gatherPipelineLayout_ = pipelineLayout(
            gatherLayout_, "solver_gather_pipeline_layout");
        integratePipelineLayout_ = pipelineLayout(
            integrateLayout_, "solver_integrate_pipeline_layout");
        smallIslandPipelineLayout_ = pipelineLayout(
            smallIslandLayout_, "solver_small_island_pipeline_layout");
        serialPipelineLayout_ = pipelineLayout(
            serialLayout_, "solver_serial_pipeline_layout");
        finishPipelineLayout_ = pipelineLayout(
            finishLayout_, "solver_finish_pipeline_layout");
        if (!coloringPipelineLayout_ || !classificationPipelineLayout_
            || !rangePipelineLayout_
            || !adjacencyPipelineLayout_ || !bodyRangePipelineLayout_
            || !preparePipelineLayout_ || !solvePipelineLayout_
            || !gatherPipelineLayout_ || !integratePipelineLayout_
            || !smallIslandPipelineLayout_ || !serialPipelineLayout_
            || !finishPipelineLayout_) {
            return false;
        }

        const std::string suffix = std::to_string(config_.workgroupSize);
        resetPipeline_ = makePipeline(device_, coloringPipelineLayout_,
            shaderModule_, "reset_coloring_" + suffix, "solver_reset");
        resetDegreesPipeline_ = makePipeline(
            device_, classificationPipelineLayout_, shaderModule_,
            "reset_body_degrees_" + suffix, "solver_reset_body_degrees");
        countDegreesPipeline_ = makePipeline(
            device_, classificationPipelineLayout_, shaderModule_,
            "count_body_degrees_" + suffix, "solver_count_body_degrees");
        markSmallIslandsPipeline_ = makePipeline(
            device_, classificationPipelineLayout_, shaderModule_,
            "mark_small_islands_" + suffix, "solver_mark_small_islands");
        clearClaimsPipeline_ = makePipeline(device_, coloringPipelineLayout_,
            shaderModule_, "clear_claims_" + suffix, "solver_clear_claims");
        clearRoundClaimsPipeline_ = makePipeline(
            device_, coloringPipelineLayout_, shaderModule_,
            "clear_round_claims_" + suffix,
            "solver_clear_round_claims");
        resetColorContinuationPipeline_ = makePipeline(
            device_, classificationPipelineLayout_, shaderModule_,
            "reset_coloring_continuation",
            "solver_reset_coloring_continuation");
        markColorContinuationPipeline_ = makePipeline(
            device_, classificationPipelineLayout_, shaderModule_,
            "mark_coloring_continuation_" + suffix,
            "solver_mark_coloring_continuation");
        claimPipeline_ = makePipeline(device_, coloringPipelineLayout_,
            shaderModule_, "claim_colors_" + suffix, "solver_claim_colors");
        commitPipeline_ = makePipeline(device_, coloringPipelineLayout_,
            shaderModule_, "commit_colors_" + suffix, "solver_commit_colors");
        validateClaimPipeline_ = makePipeline(device_, coloringPipelineLayout_,
            shaderModule_, "validate_claim_" + suffix,
            "solver_validate_claims");
        buildRecordsPipeline_ = makePipeline(device_, coloringPipelineLayout_,
            shaderModule_, "build_color_records_" + suffix,
            "solver_build_color_records");
        buildRangesPipeline_ = makePipeline(device_, rangePipelineLayout_,
            shaderModule_, "build_color_ranges_" + suffix,
            "solver_build_color_ranges");
        clearAdjacencyPipeline_ = makePipeline(device_, adjacencyPipelineLayout_,
            shaderModule_, "clear_adjacency_" + suffix,
            "solver_clear_adjacency");
        emitAdjacencyPipeline_ = makePipeline(device_, adjacencyPipelineLayout_,
            shaderModule_, "emit_adjacency_" + suffix,
            "solver_emit_adjacency");
        buildBodyRangesPipeline_ = makePipeline(device_, bodyRangePipelineLayout_,
            shaderModule_, "build_body_ranges_" + suffix,
            "solver_build_body_ranges");
        preparePipeline_ = makePipeline(device_, preparePipelineLayout_,
            shaderModule_, "prepare_constraints_" + suffix,
            "solver_prepare_constraints");
        solveColoredPipeline_ = makePipeline(device_, solvePipelineLayout_,
            shaderModule_, "solve_colored_" + suffix,
            "solver_solve_colored");
        solveCompactColorsPipeline_ = makePipeline(
            device_, solvePipelineLayout_, shaderModule_,
            "solve_compact_colors_" + suffix,
            "solver_solve_compact_colors");
        solveOverflowPipeline_ = makePipeline(device_, solvePipelineLayout_,
            shaderModule_, "solve_overflow_" + suffix,
            "solver_solve_overflow");
        gatherPipeline_ = makePipeline(device_, gatherPipelineLayout_,
            shaderModule_, "gather_overflow_" + suffix,
            "solver_gather_overflow");
        integrateVelocityPipeline_ = makePipeline(device_, integratePipelineLayout_,
            shaderModule_, "integrate_velocities_" + suffix,
            "solver_integrate_velocities");
        integratePositionPipeline_ = makePipeline(device_, integratePipelineLayout_,
            shaderModule_, "integrate_positions_" + suffix,
            "solver_integrate_positions");
        solveSmallIslandsPipeline_ = makePipeline(
            device_, smallIslandPipelineLayout_, shaderModule_,
            "solve_small_islands_" + suffix, "solver_solve_small_islands");
        serialPipeline_ = makePipeline(
            device_, serialPipelineLayout_, shaderModule_,
            "solve_serial_world", "solver_solve_serial_world");
        finishPipeline_ = makePipeline(device_, finishPipelineLayout_,
            shaderModule_, "finish_solver_tick", "solver_finish_tick");
        return resetPipeline_ && resetDegreesPipeline_ && countDegreesPipeline_
            && markSmallIslandsPipeline_ && clearClaimsPipeline_
            && clearRoundClaimsPipeline_ && resetColorContinuationPipeline_
            && markColorContinuationPipeline_ && claimPipeline_
            && commitPipeline_ && validateClaimPipeline_
            && buildRecordsPipeline_ && buildRangesPipeline_
            && clearAdjacencyPipeline_ && emitAdjacencyPipeline_
            && buildBodyRangesPipeline_ && preparePipeline_
            && solveColoredPipeline_ && solveCompactColorsPipeline_
            && solveOverflowPipeline_
            && gatherPipeline_ && integrateVelocityPipeline_
            && integratePositionPipeline_ && solveSmallIslandsPipeline_
            && serialPipeline_
            && finishPipeline_;
    }

    static bool sameBaseBindings(const GpuDynamicSolverInput& lhs,
                                 const GpuDynamicSolverInput& rhs) {
        return lhs.poseBuffer == rhs.poseBuffer
            && lhs.motionBuffer == rhs.motionBuffer
            && lhs.shapeBuffer == rhs.shapeBuffer
            && lhs.metadataBuffer == rhs.metadataBuffer
            && lhs.narrowPhaseTelemetryBuffer
                == rhs.narrowPhaseTelemetryBuffer;
    }

    static void releaseCachedInputGroups(CachedInputGroups& cache) {
        for (WGPUBindGroup& group : cache.groups) {
            releaseHandle(group, wgpuBindGroupRelease);
        }
        cache.manifoldBuffer = nullptr;
    }

    void releaseAllCachedInputGroups() {
        for (CachedInputGroups& cache : cachedInputGroups_) {
            releaseCachedInputGroups(cache);
        }
        nextInputGroupReplacement_ = 0u;
    }

    void setInput(const GpuDynamicSolverInput& input) {
        const GpuDynamicSolverInput next =
            input.bodyCapacity <= config_.bodyCapacity
              && input.contactCapacity <= config_.contactCapacity
            ? input : GpuDynamicSolverInput{};
        if (!next.valid() || !sameBaseBindings(input_, next)) {
            releaseAllCachedInputGroups();
        }
        input_ = next;
    }

    CachedInputGroups& inputGroups() {
        for (CachedInputGroups& cache : cachedInputGroups_) {
            if (cache.manifoldBuffer == input_.manifoldBuffer) return cache;
        }
        for (CachedInputGroups& cache : cachedInputGroups_) {
            if (!cache.manifoldBuffer) {
                cache.manifoldBuffer = input_.manifoldBuffer;
                return cache;
            }
        }
        CachedInputGroups& cache =
            cachedInputGroups_[nextInputGroupReplacement_];
        nextInputGroupReplacement_ =
            (nextInputGroupReplacement_ + 1u)
            % static_cast<uint32_t>(cachedInputGroups_.size());
        releaseCachedInputGroups(cache);
        cache.manifoldBuffer = input_.manifoldBuffer;
        return cache;
    }

    WGPUBindGroup cachedInputGroup(
        CachedInputGroups& cache, size_t index, WGPUBindGroupLayout layout,
        std::span<const gpu::BindGroupEntry> entries, const char* label) {
        WGPUBindGroup& group = cache.groups[index];
        if (!group) {
            group = makeGroup(layout, entries, label);
            ++inputBindGroupCacheMisses_;
        }
        return group;
    }

    Params makeParams(uint32_t first, uint32_t second,
                      uint32_t third = 0u, uint32_t fourth = 0u) const {
        const float substep = config_.tickSeconds
                            / static_cast<float>(config_.substeps);
        return {
            .capacities = {input_.bodyCapacity, input_.contactCapacity,
                           config_.colorCount,
                           (config_.workgroupSize << 8u)
                               | (config_.enableSmallIslandFastPath ? 1u : 0u)},
            .control = {first, second, third, fourth},
            .gravityDt = {config_.gravity[0], config_.gravity[1],
                          config_.gravity[2], substep},
            .dampingSlop = {config_.linearDamping, config_.angularDamping,
                            config_.linearSlop, config_.speculativeDistance},
            .solver = {config_.speculativeDistance, config_.biasRate,
                       config_.maximumPushSpeed, 0.0f},
            .material = {config_.friction, config_.restitution,
                         config_.rollingResistance,
                         config_.restitutionThreshold},
        };
    }

    uint32_t writeParams(uint32_t& slot, const Params& params) {
        const uint32_t used = slot++;
        if (used < parameterUpload_.size()) {
            parameterUpload_[used].params = params;
        }
        return used * kParameterStride;
    }

    WGPUBindGroup makeGroup(WGPUBindGroupLayout layout,
                            std::span<const gpu::BindGroupEntry> entries,
                            const char* label) {
        return gpu::createBindGroup(device_, layout, entries, label);
    }

    bool createCachedStaticGroups() {
        const auto parameterEntry = [this] {
            return gpu::BindGroupEntry(14).buffer(
                parameterBuffer_, 0, sizeof(Params));
        };
        cachedStaticGroups_[0] = makeGroup(
            bodyRangeLayout_, std::array{
                gpu::BindGroupEntry(12).buffer(colorRanges_),
                gpu::BindGroupEntry(13).buffer(telemetry_),
                gpu::BindGroupEntry(17).buffer(sortedAdjacency_),
                gpu::BindGroupEntry(18).buffer(bodyRanges_), parameterEntry()},
            "solver_body_range_bind_group");
        cachedStaticGroups_[1] = makeGroup(
            finishLayout_, std::array{
                gpu::BindGroupEntry(13).buffer(telemetry_), parameterEntry()},
            "solver_finish_bind_group");
        for (WGPUBindGroup group : cachedStaticGroups_) {
            if (!group) return false;
        }
        return true;
    }

    bool encode(
        WGPUCommandEncoder encoder, bool serialWorldSolve,
        const GpuDynamicSolver::ProfilingBoundary& profilingBoundary) {
        if (!encoder || !input_.valid()) return false;
        const auto writeProfilingBoundary = [&] {
            if (profilingBoundary.callback) {
                profilingBoundary.callback(profilingBoundary.userData);
            }
        };
        CachedInputGroups& inputGroupCache = inputGroups();
        uint32_t slot = 0u;
        const auto parameterEntry = [this] {
            return gpu::BindGroupEntry(14).buffer(
                parameterBuffer_, 0, sizeof(Params));
        };
        if (serialWorldSolve) {
            // Serial worlds perform coloring, graph construction, and solving
            // in one kernel. Attribute the complete kernel to the solve stage.
            writeProfilingBoundary();
            writeProfilingBoundary();
            const std::array<gpu::BindGroupEntry, 9> serialEntries = {
                gpu::BindGroupEntry(0).buffer(input_.poseBuffer),
                gpu::BindGroupEntry(1).buffer(input_.motionBuffer),
                gpu::BindGroupEntry(2).buffer(input_.shapeBuffer),
                gpu::BindGroupEntry(3).buffer(input_.metadataBuffer),
                gpu::BindGroupEntry(4).buffer(input_.manifoldBuffer),
                gpu::BindGroupEntry(5).buffer(
                    input_.narrowPhaseTelemetryBuffer),
                gpu::BindGroupEntry(13).buffer(telemetry_),
                gpu::BindGroupEntry(15).buffer(caches_), parameterEntry()};
            WGPUBindGroup serialGroup = cachedInputGroup(
                inputGroupCache, 9u, serialLayout_, serialEntries,
                "solver_serial_bind_group");
            if (!serialGroup) return false;
            const uint32_t offset = writeParams(
                slot, makeParams(0u, config_.overflowIterations,
                                 config_.substeps, config_.substeps));
            WGPUComputePassDescriptor passDesc{};
            WGPUComputePassEncoder pass =
                wgpuCommandEncoderBeginComputePass(encoder, &passDesc);
            wgpuComputePassEncoderSetBindGroup(
                pass, 0, serialGroup, 1, &offset);
            wgpuComputePassEncoderSetPipeline(pass, serialPipeline_);
            wgpuComputePassEncoderDispatchWorkgroups(pass, 1u, 1u, 1u);
            wgpuComputePassEncoderEnd(pass);
            wgpuComputePassEncoderRelease(pass);
            gpu::writeBuffer(
                queue_, parameterBuffer_, 0u,
                std::span<const ParameterUploadSlot>(
                    parameterUpload_.data(), slot));
            return true;
        }
        const std::array<gpu::BindGroupEntry, 9> coloringEntries = {
            gpu::BindGroupEntry(4).buffer(input_.manifoldBuffer),
            gpu::BindGroupEntry(5).buffer(input_.narrowPhaseTelemetryBuffer),
            gpu::BindGroupEntry(6).buffer(colors_),
            gpu::BindGroupEntry(7).buffer(acceptedMasks_),
            gpu::BindGroupEntry(8).buffer(claims_),
            gpu::BindGroupEntry(9).buffer(candidates_),
            gpu::BindGroupEntry(10).buffer(colorRecords_),
            gpu::BindGroupEntry(13).buffer(telemetry_), parameterEntry()};
        const std::array<gpu::BindGroupEntry, 7> classificationEntries = {
            gpu::BindGroupEntry(4).buffer(input_.manifoldBuffer),
            gpu::BindGroupEntry(5).buffer(input_.narrowPhaseTelemetryBuffer),
            gpu::BindGroupEntry(6).buffer(colors_),
            gpu::BindGroupEntry(13).buffer(telemetry_),
            gpu::BindGroupEntry(20).buffer(bodyDegrees_),
            gpu::BindGroupEntry(21).buffer(dispatchArgs_), parameterEntry()};
        WGPUBindGroup coloringGroup = cachedInputGroup(
            inputGroupCache, 0u, coloringLayout_, coloringEntries,
            "solver_coloring_bind_group");
        WGPUBindGroup classificationGroup = cachedInputGroup(
            inputGroupCache, 1u, classificationLayout_, classificationEntries,
            "solver_island_classification_bind_group");
        if (!coloringGroup || !classificationGroup) return false;
        WGPUComputePassDescriptor passDesc{};
        WGPUComputePassEncoder pass =
            wgpuCommandEncoderBeginComputePass(encoder, &passDesc);
        const uint32_t bodyGroups = (input_.bodyCapacity + config_.workgroupSize - 1u)
                                  / config_.workgroupSize;
        const uint32_t contactGroups =
            (input_.contactCapacity + config_.workgroupSize - 1u)
            / config_.workgroupSize;
        const auto dispatchOffset = [](uint32_t dispatchSlot) {
            return uint64_t{dispatchSlot} * 4u * sizeof(uint32_t);
        };
        auto dispatchContacts = [&](WGPUComputePipeline pipeline) {
            wgpuComputePassEncoderSetPipeline(pass, pipeline);
            if (input_.activeContactDispatchBuffer) {
                wgpuComputePassEncoderDispatchWorkgroupsIndirect(
                    pass, input_.activeContactDispatchBuffer,
                    input_.activeContactDispatchOffset);
            } else {
                wgpuComputePassEncoderDispatchWorkgroups(
                    pass, contactGroups, 1, 1);
            }
        };
        auto bind = [&](WGPUBindGroup group, uint32_t offset) {
            wgpuComputePassEncoderSetBindGroup(pass, 0, group, 1, &offset);
        };
        uint32_t offset = writeParams(slot,
            makeParams(0u, config_.overflowIterations));
        bind(coloringGroup, offset);
        wgpuComputePassEncoderSetPipeline(pass, resetPipeline_);
        wgpuComputePassEncoderDispatchWorkgroups(
            pass, std::max(bodyGroups, contactGroups), 1, 1);
        bind(classificationGroup, offset);
        wgpuComputePassEncoderSetPipeline(pass, resetDegreesPipeline_);
        wgpuComputePassEncoderDispatchWorkgroups(pass, bodyGroups, 1, 1);
        dispatchContacts(countDegreesPipeline_);
        dispatchContacts(markSmallIslandsPipeline_);
        wgpuComputePassEncoderEnd(pass);
        wgpuComputePassEncoderRelease(pass);

        pass = wgpuCommandEncoderBeginComputePass(encoder, &passDesc);
        bind(coloringGroup, offset);
        const uint64_t globalWorkOffset = dispatchOffset(
            config_.colorCount + 1u);
        const uint64_t globalClaimOffset = dispatchOffset(
            config_.colorCount + 5u);
        const uint64_t continuationWorkOffset = dispatchOffset(
            config_.colorCount + 6u);
        const uint32_t unconditionalRounds = std::min(
            config_.colorCount, kUnconditionalColorRounds);
        const auto encodeColorRound = [&](uint32_t round,
                                          uint64_t workOffset) {
            offset = writeParams(slot, makeParams(round,
                config_.overflowIterations));
            bind(coloringGroup, offset);
            wgpuComputePassEncoderSetPipeline(
                pass, round == 0u ? clearClaimsPipeline_
                                  : clearRoundClaimsPipeline_);
            wgpuComputePassEncoderDispatchWorkgroupsIndirect(
                pass, dispatchArgs_,
                round == 0u ? globalClaimOffset : workOffset);
            wgpuComputePassEncoderSetPipeline(pass, claimPipeline_);
            wgpuComputePassEncoderDispatchWorkgroupsIndirect(
                pass, dispatchArgs_, workOffset);
            wgpuComputePassEncoderSetPipeline(pass, commitPipeline_);
            wgpuComputePassEncoderDispatchWorkgroupsIndirect(
                pass, dispatchArgs_, workOffset);
        };
        for (uint32_t round = 0; round < unconditionalRounds; ++round) {
            encodeColorRound(round, globalWorkOffset);
        }
        if (unconditionalRounds < config_.colorCount) {
            wgpuComputePassEncoderEnd(pass);
            wgpuComputePassEncoderRelease(pass);

            pass = wgpuCommandEncoderBeginComputePass(encoder, &passDesc);
            offset = writeParams(slot,
                makeParams(0u, config_.overflowIterations));
            bind(classificationGroup, offset);
            wgpuComputePassEncoderSetPipeline(
                pass, resetColorContinuationPipeline_);
            wgpuComputePassEncoderDispatchWorkgroups(pass, 1u, 1u, 1u);
            dispatchContacts(markColorContinuationPipeline_);
            wgpuComputePassEncoderEnd(pass);
            wgpuComputePassEncoderRelease(pass);

            pass = wgpuCommandEncoderBeginComputePass(encoder, &passDesc);
            for (uint32_t round = unconditionalRounds;
                 round < config_.colorCount; ++round) {
                encodeColorRound(round, continuationWorkOffset);
            }
        }
        offset = writeParams(slot,
            makeParams(0u, config_.overflowIterations));
        bind(coloringGroup, offset);
        wgpuComputePassEncoderSetPipeline(pass, clearRoundClaimsPipeline_);
        wgpuComputePassEncoderDispatchWorkgroupsIndirect(
            pass, dispatchArgs_, globalWorkOffset);
        wgpuComputePassEncoderSetPipeline(pass, validateClaimPipeline_);
        wgpuComputePassEncoderDispatchWorkgroupsIndirect(
            pass, dispatchArgs_, globalWorkOffset);
        wgpuComputePassEncoderSetPipeline(pass, buildRecordsPipeline_);
        wgpuComputePassEncoderDispatchWorkgroupsIndirect(
            pass, dispatchArgs_, globalWorkOffset);
        wgpuComputePassEncoderEnd(pass);
        wgpuComputePassEncoderRelease(pass);

        if (!primitives_.encodeRadixSortBoundedU16Word(
                encoder, colorRecords_, sortedColorRecords_,
                input_.contactCapacity, 1u, config_.colorCount + 1u, 8u,
                dispatchArgs_, dispatchOffset(config_.colorCount + 1u),
                dispatchOffset(config_.colorCount + 2u),
                input_.narrowPhaseTelemetryBuffer, 10u)) return false;
        writeProfilingBoundary();

        const std::array<gpu::BindGroupEntry, 6> rangeEntries = {
            gpu::BindGroupEntry(5).buffer(input_.narrowPhaseTelemetryBuffer),
            gpu::BindGroupEntry(11).buffer(sortedColorRecords_),
            gpu::BindGroupEntry(12).buffer(colorRanges_),
            gpu::BindGroupEntry(13).buffer(telemetry_),
            gpu::BindGroupEntry(21).buffer(dispatchArgs_), parameterEntry()};
        const std::array<gpu::BindGroupEntry, 7> adjacencyEntries = {
            gpu::BindGroupEntry(4).buffer(input_.manifoldBuffer),
            gpu::BindGroupEntry(11).buffer(sortedColorRecords_),
            gpu::BindGroupEntry(12).buffer(colorRanges_),
            gpu::BindGroupEntry(13).buffer(telemetry_),
            gpu::BindGroupEntry(16).buffer(adjacencyRecords_),
            gpu::BindGroupEntry(18).buffer(bodyRanges_), parameterEntry()};
        WGPUBindGroup rangeGroup = cachedInputGroup(
            inputGroupCache, 2u, rangeLayout_, rangeEntries,
            "solver_range_bind_group");
        WGPUBindGroup adjacencyGroup = cachedInputGroup(
            inputGroupCache, 3u, adjacencyLayout_, adjacencyEntries,
            "solver_adjacency_bind_group");
        if (!rangeGroup || !adjacencyGroup) return false;
        pass = wgpuCommandEncoderBeginComputePass(encoder, &passDesc);
        offset = writeParams(slot,
            makeParams(0u, config_.overflowIterations));
        bind(rangeGroup, offset);
        wgpuComputePassEncoderSetPipeline(pass, buildRangesPipeline_);
        wgpuComputePassEncoderDispatchWorkgroups(pass, 1, 1, 1);
        bind(adjacencyGroup, offset);
        wgpuComputePassEncoderSetPipeline(pass, clearAdjacencyPipeline_);
        wgpuComputePassEncoderDispatchWorkgroups(pass, bodyGroups, 1, 1);
        wgpuComputePassEncoderSetPipeline(pass, emitAdjacencyPipeline_);
        wgpuComputePassEncoderDispatchWorkgroupsIndirect(
            pass, dispatchArgs_,
            dispatchOffset(config_.colorCount + 3u));
        wgpuComputePassEncoderEnd(pass);
        wgpuComputePassEncoderRelease(pass);

        const bool sortedAdjacency = input_.bodyCapacity <= 0xffffu
            ? primitives_.encodeRadixSortBoundedU16Word(
                encoder, adjacencyRecords_, sortedAdjacency_,
                endpointCapacity_, 1u, input_.bodyCapacity, 24u,
                dispatchArgs_, dispatchOffset(config_.colorCount + 3u),
                dispatchOffset(config_.colorCount + 4u), colorRanges_,
                config_.colorCount * 2u + 1u, 2u)
            : primitives_.encodeRadixSortBoundedU32x2(
                encoder, adjacencyRecords_, sortedAdjacency_,
                endpointCapacity_,
                std::max(input_.contactCapacity, input_.bodyCapacity), 24u,
                dispatchArgs_, dispatchOffset(config_.colorCount + 3u),
                dispatchOffset(config_.colorCount + 4u), colorRanges_,
                config_.colorCount * 2u + 1u, 2u);
        if (!sortedAdjacency) return false;
        writeProfilingBoundary();

        const std::array<gpu::BindGroupEntry, 8> prepareEntries = {
            gpu::BindGroupEntry(0).buffer(input_.poseBuffer),
            gpu::BindGroupEntry(1).buffer(input_.motionBuffer),
            gpu::BindGroupEntry(2).buffer(input_.shapeBuffer),
            gpu::BindGroupEntry(3).buffer(input_.metadataBuffer),
            gpu::BindGroupEntry(4).buffer(input_.manifoldBuffer),
            gpu::BindGroupEntry(5).buffer(input_.narrowPhaseTelemetryBuffer),
            gpu::BindGroupEntry(15).buffer(caches_), parameterEntry()};
        const std::array<gpu::BindGroupEntry, 9> solveEntries = {
            gpu::BindGroupEntry(0).buffer(input_.poseBuffer),
            gpu::BindGroupEntry(1).buffer(input_.motionBuffer),
            gpu::BindGroupEntry(2).buffer(input_.shapeBuffer),
            gpu::BindGroupEntry(4).buffer(input_.manifoldBuffer),
            gpu::BindGroupEntry(11).buffer(sortedColorRecords_),
            gpu::BindGroupEntry(12).buffer(colorRanges_),
            gpu::BindGroupEntry(15).buffer(caches_),
            gpu::BindGroupEntry(19).buffer(endpointDeltas_), parameterEntry()};
        const std::array<gpu::BindGroupEntry, 5> gatherEntries = {
            gpu::BindGroupEntry(1).buffer(input_.motionBuffer),
            gpu::BindGroupEntry(17).buffer(sortedAdjacency_),
            gpu::BindGroupEntry(18).buffer(bodyRanges_),
            gpu::BindGroupEntry(19).buffer(endpointDeltas_), parameterEntry()};
        const std::array<gpu::BindGroupEntry, 6> integrateEntries = {
            gpu::BindGroupEntry(0).buffer(input_.poseBuffer),
            gpu::BindGroupEntry(1).buffer(input_.motionBuffer),
            gpu::BindGroupEntry(2).buffer(input_.shapeBuffer),
            gpu::BindGroupEntry(3).buffer(input_.metadataBuffer),
            gpu::BindGroupEntry(20).buffer(bodyDegrees_), parameterEntry()};
        const std::array<gpu::BindGroupEntry, 9> smallIslandEntries = {
            gpu::BindGroupEntry(0).buffer(input_.poseBuffer),
            gpu::BindGroupEntry(1).buffer(input_.motionBuffer),
            gpu::BindGroupEntry(2).buffer(input_.shapeBuffer),
            gpu::BindGroupEntry(3).buffer(input_.metadataBuffer),
            gpu::BindGroupEntry(4).buffer(input_.manifoldBuffer),
            gpu::BindGroupEntry(5).buffer(input_.narrowPhaseTelemetryBuffer),
            gpu::BindGroupEntry(15).buffer(caches_),
            gpu::BindGroupEntry(20).buffer(bodyDegrees_), parameterEntry()};
        WGPUBindGroup bodyRangeGroup = cachedStaticGroups_[0];
        WGPUBindGroup prepareGroup = cachedInputGroup(
            inputGroupCache, 4u, prepareLayout_, prepareEntries,
            "solver_prepare_bind_group");
        WGPUBindGroup solveGroup = cachedInputGroup(
            inputGroupCache, 5u, solveLayout_, solveEntries,
            "solver_solve_bind_group");
        WGPUBindGroup gatherGroup = cachedInputGroup(
            inputGroupCache, 6u, gatherLayout_, gatherEntries,
            "solver_gather_bind_group");
        WGPUBindGroup integrateGroup = cachedInputGroup(
            inputGroupCache, 7u, integrateLayout_, integrateEntries,
            "solver_integrate_bind_group");
        WGPUBindGroup smallIslandGroup = cachedInputGroup(
            inputGroupCache, 8u, smallIslandLayout_, smallIslandEntries,
            "solver_small_island_bind_group");
        WGPUBindGroup finishGroup = cachedStaticGroups_[1];
        if (!bodyRangeGroup || !prepareGroup || !solveGroup || !gatherGroup
            || !integrateGroup || !smallIslandGroup || !finishGroup) {
            return false;
        }

        pass = wgpuCommandEncoderBeginComputePass(encoder, &passDesc);
        offset = writeParams(slot,
            makeParams(0u, config_.overflowIterations));
        bind(bodyRangeGroup, offset);
        wgpuComputePassEncoderSetPipeline(pass, buildBodyRangesPipeline_);
        wgpuComputePassEncoderDispatchWorkgroupsIndirect(
            pass, dispatchArgs_, dispatchOffset(config_.colorCount + 3u));
        bind(prepareGroup, offset);
        dispatchContacts(preparePipeline_);
        const uint32_t smallIslandOffset = writeParams(
            slot, makeParams(0u, 0u, config_.substeps, 0u));
        bind(smallIslandGroup, smallIslandOffset);
        dispatchContacts(solveSmallIslandsPipeline_);

        auto solveColors = [&](uint32_t stage, uint32_t substep) {
            // One workgroup preserves the exact color order while avoiding
            // 32 host-side indirect dispatches at latency-sensitive sizes.
            // Contacts within a color have disjoint bodies; the shader places
            // a storage barrier between consecutive colors.
            constexpr uint32_t kCompactColorDispatchBodyLimit = 4'096u;
            if (input_.bodyCapacity <= kCompactColorDispatchBodyLimit) {
                const uint32_t colorOffset = writeParams(
                    slot, makeParams(0u, stage, substep, 0u));
                bind(solveGroup, colorOffset);
                wgpuComputePassEncoderSetPipeline(
                    pass, solveCompactColorsPipeline_);
                wgpuComputePassEncoderDispatchWorkgroups(pass, 1u, 1u, 1u);
                return;
            }
            constexpr uint32_t kParallelColorCount = 8u;
            const uint32_t parallelColors = std::min(
                config_.colorCount, kParallelColorCount);
            for (uint32_t color = 0; color < parallelColors; ++color) {
                const uint32_t colorOffset = writeParams(
                    slot, makeParams(color, stage, substep, 0u));
                bind(solveGroup, colorOffset);
                wgpuComputePassEncoderSetPipeline(pass, solveColoredPipeline_);
                wgpuComputePassEncoderDispatchWorkgroupsIndirect(
                    pass, dispatchArgs_, uint64_t{color} * 4u * sizeof(uint32_t));
            }
            if (parallelColors < config_.colorCount) {
                const uint32_t colorOffset = writeParams(
                    slot, makeParams(parallelColors, stage, substep, 0u));
                bind(solveGroup, colorOffset);
                wgpuComputePassEncoderSetPipeline(
                    pass, solveCompactColorsPipeline_);
                wgpuComputePassEncoderDispatchWorkgroups(pass, 1u, 1u, 1u);
            }
        };
        auto solveOverflow = [&](uint32_t stage, uint32_t substep,
                                 uint32_t iterations) {
            for (uint32_t iteration = 0; iteration < iterations; ++iteration) {
                const uint32_t solveOffset = writeParams(
                    slot, makeParams(config_.colorCount, stage,
                                     substep, iteration));
                bind(solveGroup, solveOffset);
                wgpuComputePassEncoderSetPipeline(pass, solveOverflowPipeline_);
                wgpuComputePassEncoderDispatchWorkgroupsIndirect(
                    pass, dispatchArgs_, uint64_t{config_.colorCount}
                        * 4u * sizeof(uint32_t));
                bind(gatherGroup, solveOffset);
                wgpuComputePassEncoderSetPipeline(pass, gatherPipeline_);
                wgpuComputePassEncoderDispatchWorkgroups(pass, bodyGroups, 1, 1);
            }
        };

        for (uint32_t substep = 0; substep < config_.substeps; ++substep) {
            const uint32_t integrateOffset = writeParams(
                slot, makeParams(0u, 0u, substep, config_.substeps));
            bind(integrateGroup, integrateOffset);
            wgpuComputePassEncoderSetPipeline(pass, integrateVelocityPipeline_);
            wgpuComputePassEncoderDispatchWorkgroups(pass, bodyGroups, 1, 1);
            if (substep == 0u) {
                solveColors(0u, substep);
                solveOverflow(0u, substep, 1u);
            }
            solveColors(1u, substep);
            solveOverflow(1u, substep, config_.overflowIterations);
            bind(integrateGroup, integrateOffset);
            wgpuComputePassEncoderSetPipeline(pass, integratePositionPipeline_);
            wgpuComputePassEncoderDispatchWorkgroups(pass, bodyGroups, 1, 1);
            solveColors(2u, substep);
            solveOverflow(2u, substep, config_.overflowIterations);
        }
        solveColors(3u, config_.substeps);
        solveOverflow(3u, config_.substeps, 1u);
        const uint32_t finishOffset = writeParams(
            slot, makeParams(0u, config_.overflowIterations));
        bind(finishGroup, finishOffset);
        wgpuComputePassEncoderSetPipeline(pass, finishPipeline_);
        wgpuComputePassEncoderDispatchWorkgroups(pass, 1, 1, 1);
        wgpuComputePassEncoderEnd(pass);
        wgpuComputePassEncoderRelease(pass);
        if (slot > kParameterSlots) return false;
        gpu::writeBuffer(
            queue_, parameterBuffer_, 0u,
            std::span<const ParameterUploadSlot>(
                parameterUpload_.data(), slot));
        return true;
    }

    void shutdown() {
        releaseAllCachedInputGroups();
        for (WGPUBindGroup& group : cachedStaticGroups_) {
            releaseHandle(group, wgpuBindGroupRelease);
        }
        for (WGPUComputePipeline* pipeline : {
                 &resetPipeline_, &resetDegreesPipeline_, &countDegreesPipeline_,
                 &markSmallIslandsPipeline_, &clearClaimsPipeline_,
                 &clearRoundClaimsPipeline_,
                 &resetColorContinuationPipeline_,
                 &markColorContinuationPipeline_, &claimPipeline_,
                 &commitPipeline_, &validateClaimPipeline_,
                 &buildRecordsPipeline_, &buildRangesPipeline_,
                 &clearAdjacencyPipeline_, &emitAdjacencyPipeline_,
                 &buildBodyRangesPipeline_, &preparePipeline_,
                 &solveColoredPipeline_, &solveCompactColorsPipeline_,
                 &solveOverflowPipeline_,
                 &gatherPipeline_, &integrateVelocityPipeline_,
                 &integratePositionPipeline_, &solveSmallIslandsPipeline_,
                 &serialPipeline_,
                 &finishPipeline_}) {
            releaseHandle(*pipeline, wgpuComputePipelineRelease);
        }
        for (WGPUPipelineLayout* layout : {
                 &coloringPipelineLayout_, &classificationPipelineLayout_,
                 &rangePipelineLayout_,
                 &adjacencyPipelineLayout_, &bodyRangePipelineLayout_,
                 &preparePipelineLayout_, &solvePipelineLayout_,
                 &gatherPipelineLayout_, &integratePipelineLayout_,
                 &smallIslandPipelineLayout_, &serialPipelineLayout_,
                 &finishPipelineLayout_}) {
            releaseHandle(*layout, wgpuPipelineLayoutRelease);
        }
        for (WGPUBindGroupLayout* layout : {
                 &coloringLayout_, &classificationLayout_, &rangeLayout_,
                 &adjacencyLayout_,
                 &bodyRangeLayout_, &prepareLayout_, &solveLayout_,
                 &gatherLayout_, &integrateLayout_, &smallIslandLayout_,
                 &serialLayout_,
                 &finishLayout_}) {
            releaseHandle(*layout, wgpuBindGroupLayoutRelease);
        }
        releaseHandle(shaderModule_, wgpuShaderModuleRelease);
        primitives_.shutdown();
        for (WGPUBuffer* buffer : {
                 &parameterBuffer_, &colors_, &acceptedMasks_, &claims_,
                 &candidates_, &colorRecords_, &sortedColorRecords_,
                 &colorRanges_, &telemetry_, &caches_, &adjacencyRecords_,
                 &sortedAdjacency_, &bodyRanges_, &endpointDeltas_}) {
            releaseBuffer(*buffer);
        }
        releaseBuffer(bodyDegrees_);
        releaseBuffer(dispatchArgs_);
        device_ = nullptr;
        queue_ = nullptr;
        config_ = {};
        input_ = {};
        claimCapacity_ = 0;
        endpointCapacity_ = 0;
        inputBindGroupCacheMisses_ = 0;
        scratchBytes_ = 0;
    }

    WGPUDevice device_ = nullptr;
    WGPUQueue queue_ = nullptr;
    Config config_{};
    GpuDynamicSolverInput input_{};
    uint32_t claimCapacity_ = 0;
    uint32_t endpointCapacity_ = 0;
    size_t scratchBytes_ = 0;
    size_t inputBindGroupCacheMisses_ = 0;
    DeterministicGpuPrimitives primitives_;
    std::array<ParameterUploadSlot, kParameterSlots> parameterUpload_{};
    std::array<WGPUBindGroup, 2> cachedStaticGroups_{};
    std::array<CachedInputGroups, 2> cachedInputGroups_{};
    uint32_t nextInputGroupReplacement_ = 0u;
    WGPUBuffer parameterBuffer_ = nullptr;
    WGPUBuffer colors_ = nullptr;
    WGPUBuffer acceptedMasks_ = nullptr;
    WGPUBuffer claims_ = nullptr;
    WGPUBuffer candidates_ = nullptr;
    WGPUBuffer colorRecords_ = nullptr;
    WGPUBuffer sortedColorRecords_ = nullptr;
    WGPUBuffer colorRanges_ = nullptr;
    WGPUBuffer telemetry_ = nullptr;
    WGPUBuffer caches_ = nullptr;
    WGPUBuffer adjacencyRecords_ = nullptr;
    WGPUBuffer sortedAdjacency_ = nullptr;
    WGPUBuffer bodyRanges_ = nullptr;
    WGPUBuffer bodyDegrees_ = nullptr;
    WGPUBuffer dispatchArgs_ = nullptr;
    WGPUBuffer endpointDeltas_ = nullptr;
    WGPUShaderModule shaderModule_ = nullptr;
    WGPUBindGroupLayout coloringLayout_ = nullptr;
    WGPUBindGroupLayout classificationLayout_ = nullptr;
    WGPUBindGroupLayout rangeLayout_ = nullptr;
    WGPUBindGroupLayout adjacencyLayout_ = nullptr;
    WGPUBindGroupLayout bodyRangeLayout_ = nullptr;
    WGPUBindGroupLayout prepareLayout_ = nullptr;
    WGPUBindGroupLayout solveLayout_ = nullptr;
    WGPUBindGroupLayout gatherLayout_ = nullptr;
    WGPUBindGroupLayout integrateLayout_ = nullptr;
    WGPUBindGroupLayout smallIslandLayout_ = nullptr;
    WGPUBindGroupLayout serialLayout_ = nullptr;
    WGPUBindGroupLayout finishLayout_ = nullptr;
    WGPUPipelineLayout coloringPipelineLayout_ = nullptr;
    WGPUPipelineLayout classificationPipelineLayout_ = nullptr;
    WGPUPipelineLayout rangePipelineLayout_ = nullptr;
    WGPUPipelineLayout adjacencyPipelineLayout_ = nullptr;
    WGPUPipelineLayout bodyRangePipelineLayout_ = nullptr;
    WGPUPipelineLayout preparePipelineLayout_ = nullptr;
    WGPUPipelineLayout solvePipelineLayout_ = nullptr;
    WGPUPipelineLayout gatherPipelineLayout_ = nullptr;
    WGPUPipelineLayout integratePipelineLayout_ = nullptr;
    WGPUPipelineLayout smallIslandPipelineLayout_ = nullptr;
    WGPUPipelineLayout serialPipelineLayout_ = nullptr;
    WGPUPipelineLayout finishPipelineLayout_ = nullptr;
    WGPUComputePipeline resetPipeline_ = nullptr;
    WGPUComputePipeline resetDegreesPipeline_ = nullptr;
    WGPUComputePipeline countDegreesPipeline_ = nullptr;
    WGPUComputePipeline markSmallIslandsPipeline_ = nullptr;
    WGPUComputePipeline clearClaimsPipeline_ = nullptr;
    WGPUComputePipeline clearRoundClaimsPipeline_ = nullptr;
    WGPUComputePipeline resetColorContinuationPipeline_ = nullptr;
    WGPUComputePipeline markColorContinuationPipeline_ = nullptr;
    WGPUComputePipeline claimPipeline_ = nullptr;
    WGPUComputePipeline commitPipeline_ = nullptr;
    WGPUComputePipeline validateClaimPipeline_ = nullptr;
    WGPUComputePipeline buildRecordsPipeline_ = nullptr;
    WGPUComputePipeline buildRangesPipeline_ = nullptr;
    WGPUComputePipeline clearAdjacencyPipeline_ = nullptr;
    WGPUComputePipeline emitAdjacencyPipeline_ = nullptr;
    WGPUComputePipeline buildBodyRangesPipeline_ = nullptr;
    WGPUComputePipeline preparePipeline_ = nullptr;
    WGPUComputePipeline solveColoredPipeline_ = nullptr;
    WGPUComputePipeline solveCompactColorsPipeline_ = nullptr;
    WGPUComputePipeline solveOverflowPipeline_ = nullptr;
    WGPUComputePipeline gatherPipeline_ = nullptr;
    WGPUComputePipeline integrateVelocityPipeline_ = nullptr;
    WGPUComputePipeline integratePositionPipeline_ = nullptr;
    WGPUComputePipeline solveSmallIslandsPipeline_ = nullptr;
    WGPUComputePipeline serialPipeline_ = nullptr;
    WGPUComputePipeline finishPipeline_ = nullptr;
};

GpuDynamicSolver::GpuDynamicSolver() : impl_(std::make_unique<Impl>()) {}
GpuDynamicSolver::~GpuDynamicSolver() = default;
GpuDynamicSolver::GpuDynamicSolver(GpuDynamicSolver&&) noexcept = default;
GpuDynamicSolver& GpuDynamicSolver::operator=(GpuDynamicSolver&&) noexcept = default;

bool GpuDynamicSolver::initialize(WGPUDevice device, WGPUQueue queue,
                                  const Config& config) {
    return impl_->initialize(device, queue, config);
}
void GpuDynamicSolver::shutdown() { impl_->shutdown(); }
void GpuDynamicSolver::setInput(const GpuDynamicSolverInput& input) {
    impl_->setInput(input);
}
bool GpuDynamicSolver::encode(WGPUCommandEncoder encoder,
                              bool serialWorldSolve) {
    return impl_->encode(encoder, serialWorldSolve, ProfilingBoundary{});
}
bool GpuDynamicSolver::encode(
    WGPUCommandEncoder encoder, bool serialWorldSolve,
    const ProfilingBoundary& profilingBoundary) {
    return impl_->encode(encoder, serialWorldSolve, profilingBoundary);
}
WGPUBuffer GpuDynamicSolver::colors() const noexcept { return impl_->colors_; }
WGPUBuffer GpuDynamicSolver::sortedColorRecords() const noexcept {
    return impl_->sortedColorRecords_;
}
WGPUBuffer GpuDynamicSolver::colorRanges() const noexcept {
    return impl_->colorRanges_;
}
WGPUBuffer GpuDynamicSolver::constraintCache() const noexcept {
    return impl_->caches_;
}
WGPUBuffer GpuDynamicSolver::telemetryBuffer() const noexcept {
    return impl_->telemetry_;
}
uint32_t GpuDynamicSolver::colorCount() const noexcept {
    return impl_->config_.colorCount;
}
size_t GpuDynamicSolver::scratchBytes() const noexcept {
    return impl_->scratchBytes_;
}
size_t GpuDynamicSolver::inputBindGroupCacheMisses() const noexcept {
    return impl_->inputBindGroupCacheMisses_;
}

GpuDynamicSolverTelemetry GpuDynamicSolver::decodeTelemetry(
    std::span<const uint32_t> words) noexcept {
    GpuDynamicSolverTelemetry result;
    if (words.size() < 46) return result;
    for (uint32_t color = 0; color < result.colorCounts.size(); ++color) {
        result.colorCounts[color] = words[color];
    }
    result.contactCount = words[32];
    result.coloredContacts = words[33];
    result.overflowContacts = words[34];
    result.persistentColorsRetained = words[35];
    result.maximumBodyDegree = words[36];
    result.conflictErrors = words[37];
    result.overflowIterations = words[38];
    result.highContacts = words[39];
    result.highOverflow = words[40];
    result.tick = words[41];
    result.contactOverflow = words[42] != 0u;
    result.smallIslandContacts = words[43];
    result.smallIslandBodies = words[44];
    result.serialWorld = words[45] != 0u;
    return result;
}

} // namespace voxy::physics
