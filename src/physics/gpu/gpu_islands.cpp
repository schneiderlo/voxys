#include "physics/gpu/gpu_islands.hpp"

#include "gpu/resources.hpp"
#include "physics/gpu/deterministic_primitives.hpp"
#include "physics/physics_types.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <string>
#include <utility>
#include <vector>

namespace voxy::physics {
namespace {

constexpr uint32_t kTelemetryWords = GpuIslandManager::kTelemetryWordCount;

bool validSectorCellSize(float cellSize) noexcept {
    if (!std::isfinite(cellSize) || cellSize <= 0.0f) return false;
    const float cellsPerSector = kWorldSectorSize / cellSize;
    const float rounded = std::round(cellsPerSector);
    return rounded >= 1.0f && rounded <= 2'097'152.0f
        && std::abs(cellsPerSector - rounded) <= 1e-5f;
}

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

class GpuIslandManager::Impl {
public:
    static constexpr size_t kInputGroupCount = 11;

    struct alignas(16) Params {
        std::array<uint32_t, 4> capacities{};
        std::array<uint32_t, 4> control{};
        std::array<float, 4> thresholds{};
    };

    struct CachedInputGroups {
        WGPUBuffer manifoldBuffer = nullptr;
        std::array<WGPUBindGroup, kInputGroupCount> groups{};
    };

    ~Impl() { shutdown(); }

    bool initialize(WGPUDevice device, WGPUQueue queue, const Config& config) {
        if (device_ || !device || !queue || config.bodyCapacity == 0
            || config.contactCapacity == 0 || config.eventCapacity == 0
            || config.unionRounds == 0 || config.sleepTicks == 0
            || config.linearSleepThreshold < 0.0f
            || config.angularSleepThreshold < 0.0f
            || !validSectorCellSize(config.sleepingCellSize)
            || (config.workgroupSize != 64 && config.workgroupSize != 128
                && config.workgroupSize != 256)) {
            return false;
        }
        device_ = device;
        queue_ = queue;
        config_ = config;

        DeterministicGpuPrimitives::Config primitiveConfig;
        primitiveConfig.capacity = config.bodyCapacity;
        primitiveConfig.workgroupSize = config.workgroupSize;
        primitiveConfig.shaderPath = config.primitivesShaderPath;
        if (!primitives_.initialize(device_, queue_, primitiveConfig)) {
            shutdown();
            return false;
        }

        const auto makeStorage = [this](uint64_t bytes, const char* label) {
            return gpu::createBuffer(device_, gpu::BufferDesc{
                .label = label,
                .size = bytes,
                .usage = WGPUBufferUsage_Storage | WGPUBufferUsage_CopyDst
                       | WGPUBufferUsage_CopySrc,
            });
        };
        parameterBuffer_ = gpu::createBuffer(device_, gpu::BufferDesc{
            .label = "island_params",
            .size = gpu::alignUniformBufferSize(sizeof(Params)),
            .usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst,
        });
        roots_ = makeStorage(uint64_t{config.bodyCapacity} * sizeof(uint32_t),
                             "island_body_roots");
        bodyRecords_ = makeStorage(
            uint64_t{config.bodyCapacity} * sizeof(GpuKeyValue),
            "island_body_records");
        sortedBodyRecords_ = makeStorage(
            uint64_t{config.bodyCapacity} * sizeof(GpuKeyValue),
            "island_sorted_body_records");
        islands_ = makeStorage(
            uint64_t{config.bodyCapacity} * sizeof(GpuIslandRecord),
            "island_records");
        scratch_ = makeStorage(uint64_t{config.bodyCapacity} * 16u,
                               "island_accumulators");
        islandPersistent_ = makeStorage(uint64_t{config.bodyCapacity} * 16u,
                                        "island_persistent_state");
        bodyPersistent_ = makeStorage(uint64_t{config.bodyCapacity} * 16u,
                                      "island_body_persistent_state");
        sleepingGrid_ = makeStorage(
            uint64_t{config.bodyCapacity} * sizeof(GpuKeyValue),
            "island_sleeping_grid");
        sortedSleepingGrid_ = makeStorage(
            uint64_t{config.bodyCapacity} * sizeof(GpuKeyValue),
            "island_sorted_sleeping_grid");
        compactedSleepingGrid_ = makeStorage(
            uint64_t{config.bodyCapacity} * sizeof(GpuKeyValue),
            "island_compacted_sleeping_grid");
        sleepingRanges_ = makeStorage(
            uint64_t{config.bodyCapacity} * sizeof(GpuSleepingCellRange),
            "island_sleeping_cell_ranges");
        rangePredicates_ = makeStorage(
            uint64_t{config.bodyCapacity} * sizeof(uint32_t),
            "island_range_predicates");
        rangeIndices_ = makeStorage(
            uint64_t{config.bodyCapacity} * sizeof(uint32_t),
            "island_range_indices");
        pendingEvents_ = makeStorage(
            uint64_t{config.bodyCapacity} * sizeof(GpuIslandEvent),
            "island_pending_events");
        events_ = makeStorage(
            uint64_t{config.eventCapacity} * sizeof(GpuIslandEvent),
            "island_events");
        telemetry_ = gpu::createBuffer(device_, gpu::BufferDesc{
            .label = "island_telemetry",
            .size = kTelemetryWords * sizeof(uint32_t),
            .usage = WGPUBufferUsage_Storage | WGPUBufferUsage_CopyDst
                   | WGPUBufferUsage_CopySrc | WGPUBufferUsage_Indirect,
        });
        if (!parameterBuffer_ || !roots_ || !bodyRecords_
            || !sortedBodyRecords_ || !islands_ || !scratch_
            || !islandPersistent_ || !bodyPersistent_ || !sleepingGrid_
            || !sortedSleepingGrid_ || !compactedSleepingGrid_
            || !sleepingRanges_ || !rangePredicates_
            || !rangeIndices_ || !pendingEvents_ || !events_ || !telemetry_) {
            shutdown();
            return false;
        }
        const std::array<uint32_t, kTelemetryWords> zeroTelemetry{};
        gpu::writeBuffer(queue_, telemetry_, 0, zeroTelemetry);
        scratchBytes_ = gpu::saturatingSize(
            primitives_.scratchBytes()
            + gpu::alignUniformBufferSize(sizeof(Params))
            + uint64_t{config.bodyCapacity}
                * (3u * sizeof(uint32_t) + 5u * sizeof(GpuKeyValue)
                   + sizeof(GpuIslandRecord) + sizeof(GpuSleepingCellRange)
                   + sizeof(GpuIslandEvent) + 3u * 16u)
            + uint64_t{config.eventCapacity} * sizeof(GpuIslandEvent)
            + kTelemetryWords * sizeof(uint32_t));

        shaderModule_ = gpu::loadShaderModule(
            device_, config.shaderPath, "physics_islands.wgsl");
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
            entries.emplace_back(10).computeVisible().uniformBuffer(
                false, sizeof(Params));
        };
        const auto makeLayout = [this](std::span<const LE> entries,
                                       const char* label) {
            return gpu::createBindGroupLayout(device_, entries, label);
        };

        std::vector<LE> entries;
        for (uint32_t binding : {3u, 6u, 7u, 9u, 12u, 13u, 14u, 18u})
            storage(entries, binding, false);
        uniform(entries);
        resetLayout_ = makeLayout(entries, "island_reset_layout");

        entries.clear();
        storage(entries, 5, true);
        storage(entries, 9, false);
        uniform(entries);
        prepareUnionLayout_ = makeLayout(
            entries, "island_prepare_union_layout");

        entries.clear();
        storage(entries, 3, false);
        storage(entries, 4, true);
        storage(entries, 5, true);
        storage(entries, 6, false);
        uniform(entries);
        unionLayout_ = makeLayout(entries, "island_union_layout");

        entries.clear();
        storage(entries, 3, false);
        storage(entries, 6, false);
        storage(entries, 7, false);
        storage(entries, 8, false);
        storage(entries, 9, false);
        uniform(entries);
        recordLayout_ = makeLayout(entries, "island_record_layout");

        entries.clear();
        storage(entries, 8, false);
        storage(entries, 9, false);
        storage(entries, 11, false);
        storage(entries, 19, false);
        storage(entries, 20, true);
        uniform(entries);
        rangeLayout_ = makeLayout(entries, "island_range_layout");

        entries.clear();
        storage(entries, 1, false);
        storage(entries, 3, false);
        storage(entries, 6, false);
        storage(entries, 12, false);
        storage(entries, 14, false);
        uniform(entries);
        classifyLayout_ = makeLayout(entries, "island_classify_layout");

        entries.clear();
        for (uint32_t binding : {9u, 11u, 12u, 13u, 18u, 19u, 21u})
            storage(entries, binding, false);
        uniform(entries);
        decideLayout_ = makeLayout(entries, "island_decide_layout");

        entries.clear();
        storage(entries, 9, false);
        storage(entries, 18, false);
        storage(entries, 19, false);
        storage(entries, 20, true);
        storage(entries, 21, false);
        uniform(entries);
        eventLayout_ = makeLayout(entries, "island_event_layout");

        entries.clear();
        storage(entries, 0, true);
        for (uint32_t binding : {1u, 3u, 6u, 9u, 13u, 14u, 15u})
            storage(entries, binding, false);
        uniform(entries);
        applyLayout_ = makeLayout(entries, "island_apply_layout");

        entries.clear();
        storage(entries, 9, false);
        storage(entries, 15, false);
        storage(entries, 16, false);
        storage(entries, 17, false);
        storage(entries, 19, false);
        storage(entries, 20, true);
        storage(entries, 22, false);
        uniform(entries);
        sleepingRangeLayout_ = makeLayout(
            entries, "island_sleeping_range_layout");

        entries.clear();
        storage(entries, 3, false);
        storage(entries, 4, true);
        storage(entries, 5, true);
        storage(entries, 6, false);
        storage(entries, 7, false);
        storage(entries, 8, false);
        storage(entries, 9, false);
        storage(entries, 14, false);
        uniform(entries);
        smallBuildLayout_ = makeLayout(entries, "island_small_build_layout");

        entries.clear();
        for (uint32_t binding : {1u, 3u, 8u, 9u, 11u, 13u, 14u, 18u})
            storage(entries, binding, false);
        uniform(entries);
        smallDecideLayout_ = makeLayout(entries, "island_small_decide_layout");

        entries.clear();
        storage(entries, 0, true);
        for (uint32_t binding : {3u, 9u, 15u, 16u, 17u})
            storage(entries, binding, false);
        uniform(entries);
        smallGridLayout_ = makeLayout(entries, "island_small_grid_layout");
        if (!resetLayout_ || !prepareUnionLayout_ || !unionLayout_
            || !recordLayout_ || !rangeLayout_
            || !classifyLayout_ || !decideLayout_ || !eventLayout_
            || !applyLayout_
            || !sleepingRangeLayout_ || !smallBuildLayout_
            || !smallDecideLayout_ || !smallGridLayout_) return false;

        const auto pipelineLayout = [this](WGPUBindGroupLayout layout,
                                            const char* label) {
            return gpu::createPipelineLayout(device_, std::array{layout}, label);
        };
        resetPipelineLayout_ = pipelineLayout(resetLayout_, "island_reset_pl");
        prepareUnionPipelineLayout_ = pipelineLayout(
            prepareUnionLayout_, "island_prepare_union_pl");
        unionPipelineLayout_ = pipelineLayout(unionLayout_, "island_union_pl");
        recordPipelineLayout_ = pipelineLayout(recordLayout_, "island_record_pl");
        rangePipelineLayout_ = pipelineLayout(rangeLayout_, "island_range_pl");
        classifyPipelineLayout_ = pipelineLayout(
            classifyLayout_, "island_classify_pl");
        decidePipelineLayout_ = pipelineLayout(decideLayout_, "island_decide_pl");
        eventPipelineLayout_ = pipelineLayout(eventLayout_, "island_event_pl");
        applyPipelineLayout_ = pipelineLayout(applyLayout_, "island_apply_pl");
        sleepingRangePipelineLayout_ = pipelineLayout(
            sleepingRangeLayout_, "island_sleeping_range_pl");
        smallBuildPipelineLayout_ = pipelineLayout(
            smallBuildLayout_, "island_small_build_pl");
        smallDecidePipelineLayout_ = pipelineLayout(
            smallDecideLayout_, "island_small_decide_pl");
        smallGridPipelineLayout_ = pipelineLayout(
            smallGridLayout_, "island_small_grid_pl");
        if (!resetPipelineLayout_ || !prepareUnionPipelineLayout_
            || !unionPipelineLayout_
            || !recordPipelineLayout_ || !rangePipelineLayout_
            || !classifyPipelineLayout_ || !decidePipelineLayout_
            || !eventPipelineLayout_ || !applyPipelineLayout_
            || !sleepingRangePipelineLayout_ || !smallBuildPipelineLayout_
            || !smallDecidePipelineLayout_ || !smallGridPipelineLayout_) {
            return false;
        }

        const std::string suffix = std::to_string(config_.workgroupSize);
        resetPipeline_ = makePipeline(device_, resetPipelineLayout_, shaderModule_,
            "reset_islands_" + suffix, "island_reset");
        prepareUnionPipeline_ = makePipeline(
            device_, prepareUnionPipelineLayout_, shaderModule_,
            "prepare_union_dispatch", "island_prepare_union_dispatch");
        unionPipeline_ = makePipeline(device_, unionPipelineLayout_, shaderModule_,
            "union_contacts_" + suffix, "island_union_contacts");
        compressPipeline_ = makePipeline(
            device_, unionPipelineLayout_, shaderModule_,
            "compress_roots_" + suffix, "island_compress_roots");
        recordPipeline_ = makePipeline(device_, recordPipelineLayout_, shaderModule_,
            "build_body_records_" + suffix, "island_build_body_records");
        markRangePipeline_ = makePipeline(
            device_, rangePipelineLayout_, shaderModule_,
            "mark_island_range_starts_" + suffix, "island_mark_ranges");
        finalizeRangePipeline_ = makePipeline(
            device_, rangePipelineLayout_, shaderModule_,
            "finalize_island_count", "island_finalize_range_count");
        scatterRangeStartsPipeline_ = makePipeline(
            device_, rangePipelineLayout_, shaderModule_,
            "scatter_island_range_starts_" + suffix,
            "island_scatter_range_starts");
        scatterRangeEndsPipeline_ = makePipeline(
            device_, rangePipelineLayout_, shaderModule_,
            "scatter_island_range_ends_" + suffix,
            "island_scatter_range_ends");
        classifyPipeline_ = makePipeline(
            device_, classifyPipelineLayout_, shaderModule_,
            "classify_bodies_" + suffix, "island_classify_bodies");
        decidePipeline_ = makePipeline(device_, decidePipelineLayout_, shaderModule_,
            "decide_islands_" + suffix, "island_decide");
        finalizeEventsPipeline_ = makePipeline(
            device_, eventPipelineLayout_, shaderModule_,
            "finalize_island_events", "island_finalize_events");
        scatterEventsPipeline_ = makePipeline(
            device_, eventPipelineLayout_, shaderModule_,
            "scatter_island_events_" + suffix, "island_scatter_events");
        applyPipeline_ = makePipeline(device_, applyPipelineLayout_, shaderModule_,
            "apply_states_" + suffix, "island_apply_states");
        markSleepingEntriesPipeline_ = makePipeline(
            device_, sleepingRangePipelineLayout_, shaderModule_,
            "mark_sleeping_entries_" + suffix, "island_mark_sleeping_entries");
        compactSleepingEntriesPipeline_ = makePipeline(
            device_, sleepingRangePipelineLayout_, shaderModule_,
            "compact_sleeping_entries_" + suffix,
            "island_compact_sleeping_entries");
        finalizeSleepingEntriesPipeline_ = makePipeline(
            device_, sleepingRangePipelineLayout_, shaderModule_,
            "finalize_sleeping_entry_count",
            "island_finalize_sleeping_entry_count");
        markSleepingRangePipeline_ = makePipeline(
            device_, sleepingRangePipelineLayout_, shaderModule_,
            "mark_sleeping_range_starts_" + suffix,
            "island_mark_sleeping_ranges");
        finalizeSleepingRangePipeline_ = makePipeline(
            device_, sleepingRangePipelineLayout_, shaderModule_,
            "finalize_sleeping_ranges", "island_finalize_sleeping_ranges");
        scatterSleepingRangeStartsPipeline_ = makePipeline(
            device_, sleepingRangePipelineLayout_, shaderModule_,
            "scatter_sleeping_range_starts_" + suffix,
            "island_scatter_sleeping_range_starts");
        scatterSleepingRangeEndsPipeline_ = makePipeline(
            device_, sleepingRangePipelineLayout_, shaderModule_,
            "scatter_sleeping_range_ends_" + suffix,
            "island_scatter_sleeping_range_ends");
        smallBuildPipeline_ = makePipeline(
            device_, smallBuildPipelineLayout_, shaderModule_,
            "small_world_build", "island_small_world_build");
        smallDecidePipeline_ = makePipeline(
            device_, smallDecidePipelineLayout_, shaderModule_,
            "small_world_decide", "island_small_world_decide");
        smallGridPipeline_ = makePipeline(
            device_, smallGridPipelineLayout_, shaderModule_,
            "small_world_sleeping_grid", "island_small_world_grid");
        return resetPipeline_ && prepareUnionPipeline_ && unionPipeline_
            && compressPipeline_
            && recordPipeline_ && markRangePipeline_ && finalizeRangePipeline_
            && scatterRangeStartsPipeline_ && scatterRangeEndsPipeline_
            && classifyPipeline_ && decidePipeline_ && finalizeEventsPipeline_
            && scatterEventsPipeline_ && applyPipeline_
            && markSleepingEntriesPipeline_ && compactSleepingEntriesPipeline_
            && finalizeSleepingEntriesPipeline_ && markSleepingRangePipeline_
            && finalizeSleepingRangePipeline_
            && scatterSleepingRangeStartsPipeline_
            && scatterSleepingRangeEndsPipeline_ && smallBuildPipeline_
            && smallDecidePipeline_ && smallGridPipeline_;
    }

    static bool sameBaseBindings(const GpuIslandInput& lhs,
                                 const GpuIslandInput& rhs) {
        return lhs.poseBuffer == rhs.poseBuffer
            && lhs.motionBuffer == rhs.motionBuffer
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

    void setInput(const GpuIslandInput& input) {
        const GpuIslandInput next =
            input.bodyCapacity <= config_.bodyCapacity
            && input.contactCapacity <= config_.contactCapacity
            ? input : GpuIslandInput{};
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
            group = gpu::createBindGroup(device_, layout, entries, label);
            ++inputBindGroupCacheMisses_;
        }
        return group;
    }

    bool createCachedStaticGroups() {
        const auto parameterEntry = [this] {
            return gpu::BindGroupEntry(10).buffer(
                parameterBuffer_, 0, sizeof(Params));
        };
        const std::array<gpu::BindGroupEntry, 6> rangeEntries = {
            gpu::BindGroupEntry(8).buffer(sortedBodyRecords_),
            gpu::BindGroupEntry(9).buffer(telemetry_),
            gpu::BindGroupEntry(11).buffer(islands_),
            gpu::BindGroupEntry(19).buffer(rangePredicates_),
            gpu::BindGroupEntry(20).buffer(rangeIndices_), parameterEntry()};
        const std::array<gpu::BindGroupEntry, 8> sleepingRangeEntries = {
            gpu::BindGroupEntry(9).buffer(telemetry_),
            gpu::BindGroupEntry(15).buffer(sleepingGrid_),
            gpu::BindGroupEntry(16).buffer(sortedSleepingGrid_),
            gpu::BindGroupEntry(17).buffer(sleepingRanges_),
            gpu::BindGroupEntry(19).buffer(rangePredicates_),
            gpu::BindGroupEntry(20).buffer(rangeIndices_),
            gpu::BindGroupEntry(22).buffer(compactedSleepingGrid_),
            parameterEntry()};
        cachedStaticGroups_[0] = gpu::createBindGroup(
            device_, rangeLayout_, rangeEntries, "island_range_group");
        cachedStaticGroups_[1] = gpu::createBindGroup(
            device_, sleepingRangeLayout_, sleepingRangeEntries,
            "island_sleeping_range_group");
        return cachedStaticGroups_[0] && cachedStaticGroups_[1];
    }

    bool encode(WGPUCommandEncoder encoder, bool compactSmallWorld) {
        if (!encoder || !input_.valid()) return false;
        CachedInputGroups& inputGroupCache = inputGroups();
        // At most one sleep/wake event can be emitted per body in a tick.
        // Keep the allocated event buffer at the configured maximum, but only
        // clear and expose the portion reachable by this sparse body view.
        const uint32_t eventCapacity = std::min(
            config_.eventCapacity, input_.bodyCapacity);
        Params params;
        params.capacities = {input_.bodyCapacity, input_.contactCapacity,
                             eventCapacity, config_.workgroupSize};
        params.control = {config_.unionRounds, config_.sleepTicks, 0u, 0u};
        const float cellsPerSectorValue =
            kWorldSectorSize / config_.sleepingCellSize;
        const float roundedCellsPerSector = std::round(cellsPerSectorValue);
        const float cellsPerSector = std::abs(
            cellsPerSectorValue - roundedCellsPerSector) <= 1e-5f
            ? roundedCellsPerSector : 0.0f;
        params.thresholds = {
            config_.linearSleepThreshold * config_.linearSleepThreshold,
            config_.angularSleepThreshold * config_.angularSleepThreshold,
            config_.sleepingCellSize, cellsPerSector};
        gpu::writeBuffer(queue_, parameterBuffer_, 0, params);
        const auto parameterEntry = [this] {
            return gpu::BindGroupEntry(10).buffer(
                parameterBuffer_, 0, sizeof(Params));
        };
        if (compactSmallWorld) {
            const std::array<gpu::BindGroupEntry, 9> buildEntries = {
                gpu::BindGroupEntry(3).buffer(input_.metadataBuffer),
                gpu::BindGroupEntry(4).buffer(input_.manifoldBuffer),
                gpu::BindGroupEntry(5).buffer(
                    input_.narrowPhaseTelemetryBuffer),
                gpu::BindGroupEntry(6).buffer(roots_),
                gpu::BindGroupEntry(7).buffer(bodyRecords_),
                gpu::BindGroupEntry(8).buffer(sortedBodyRecords_),
                gpu::BindGroupEntry(9).buffer(telemetry_),
                gpu::BindGroupEntry(14).buffer(bodyPersistent_),
                parameterEntry()};
            const std::array<gpu::BindGroupEntry, 9> decideEntries = {
                gpu::BindGroupEntry(1).buffer(input_.motionBuffer),
                gpu::BindGroupEntry(3).buffer(input_.metadataBuffer),
                gpu::BindGroupEntry(8).buffer(sortedBodyRecords_),
                gpu::BindGroupEntry(9).buffer(telemetry_),
                gpu::BindGroupEntry(11).buffer(islands_),
                gpu::BindGroupEntry(13).buffer(islandPersistent_),
                gpu::BindGroupEntry(14).buffer(bodyPersistent_),
                gpu::BindGroupEntry(18).buffer(events_), parameterEntry()};
            const std::array<gpu::BindGroupEntry, 7> gridEntries = {
                gpu::BindGroupEntry(0).buffer(input_.poseBuffer),
                gpu::BindGroupEntry(3).buffer(input_.metadataBuffer),
                gpu::BindGroupEntry(9).buffer(telemetry_),
                gpu::BindGroupEntry(15).buffer(sleepingGrid_),
                gpu::BindGroupEntry(16).buffer(sortedSleepingGrid_),
                gpu::BindGroupEntry(17).buffer(sleepingRanges_),
                parameterEntry()};
            WGPUBindGroup buildGroup = cachedInputGroup(
                inputGroupCache, 8u, smallBuildLayout_, buildEntries,
                "island_small_build_group");
            WGPUBindGroup decideGroup = cachedInputGroup(
                inputGroupCache, 9u, smallDecideLayout_, decideEntries,
                "island_small_decide_group");
            WGPUBindGroup gridGroup = cachedInputGroup(
                inputGroupCache, 10u, smallGridLayout_, gridEntries,
                "island_small_grid_group");
            if (!buildGroup || !decideGroup || !gridGroup) return false;
            WGPUComputePassDescriptor passDesc{};
            WGPUComputePassEncoder pass =
                wgpuCommandEncoderBeginComputePass(encoder, &passDesc);
            wgpuComputePassEncoderSetBindGroup(
                pass, 0, buildGroup, 0, nullptr);
            wgpuComputePassEncoderSetPipeline(pass, smallBuildPipeline_);
            wgpuComputePassEncoderDispatchWorkgroups(pass, 1u, 1u, 1u);
            wgpuComputePassEncoderSetBindGroup(
                pass, 0, decideGroup, 0, nullptr);
            wgpuComputePassEncoderSetPipeline(pass, smallDecidePipeline_);
            wgpuComputePassEncoderDispatchWorkgroups(pass, 1u, 1u, 1u);
            wgpuComputePassEncoderSetBindGroup(
                pass, 0, gridGroup, 0, nullptr);
            wgpuComputePassEncoderSetPipeline(pass, smallGridPipeline_);
            wgpuComputePassEncoderDispatchWorkgroups(pass, 1u, 1u, 1u);
            wgpuComputePassEncoderEnd(pass);
            wgpuComputePassEncoderRelease(pass);
            return true;
        }
        const std::array<gpu::BindGroupEntry, 9> resetEntries = {
            gpu::BindGroupEntry(3).buffer(input_.metadataBuffer),
            gpu::BindGroupEntry(6).buffer(roots_),
            gpu::BindGroupEntry(7).buffer(bodyRecords_),
            gpu::BindGroupEntry(9).buffer(telemetry_),
            gpu::BindGroupEntry(12).buffer(scratch_),
            gpu::BindGroupEntry(13).buffer(islandPersistent_),
            gpu::BindGroupEntry(14).buffer(bodyPersistent_),
            gpu::BindGroupEntry(18).buffer(events_), parameterEntry()};
        const std::array<gpu::BindGroupEntry, 5> unionEntries = {
            gpu::BindGroupEntry(3).buffer(input_.metadataBuffer),
            gpu::BindGroupEntry(4).buffer(input_.manifoldBuffer),
            gpu::BindGroupEntry(5).buffer(input_.narrowPhaseTelemetryBuffer),
            gpu::BindGroupEntry(6).buffer(roots_), parameterEntry()};
        const std::array<gpu::BindGroupEntry, 3> prepareUnionEntries = {
            gpu::BindGroupEntry(5).buffer(input_.narrowPhaseTelemetryBuffer),
            gpu::BindGroupEntry(9).buffer(telemetry_), parameterEntry()};
        const std::array<gpu::BindGroupEntry, 6> recordEntries = {
            gpu::BindGroupEntry(3).buffer(input_.metadataBuffer),
            gpu::BindGroupEntry(6).buffer(roots_),
            gpu::BindGroupEntry(7).buffer(bodyRecords_),
            gpu::BindGroupEntry(8).buffer(sortedBodyRecords_),
            gpu::BindGroupEntry(9).buffer(telemetry_), parameterEntry()};
        WGPUBindGroup resetGroup = cachedInputGroup(
            inputGroupCache, 0u, resetLayout_, resetEntries,
            "island_reset_group");
        WGPUBindGroup unionGroup = cachedInputGroup(
            inputGroupCache, 1u, unionLayout_, unionEntries,
            "island_union_group");
        WGPUBindGroup prepareUnionGroup = cachedInputGroup(
            inputGroupCache, 2u, prepareUnionLayout_, prepareUnionEntries,
            "island_prepare_union_group");
        WGPUBindGroup recordGroup = cachedInputGroup(
            inputGroupCache, 3u, recordLayout_, recordEntries,
            "island_record_group");
        if (!resetGroup || !prepareUnionGroup || !unionGroup || !recordGroup)
            return false;
        const uint32_t bodyGroups =
            (input_.bodyCapacity + config_.workgroupSize - 1u)
            / config_.workgroupSize;
        const uint32_t eventGroups =
            (eventCapacity + config_.workgroupSize - 1u)
            / config_.workgroupSize;
        WGPUComputePassDescriptor passDesc{};
        WGPUComputePassEncoder pass =
            wgpuCommandEncoderBeginComputePass(encoder, &passDesc);
        wgpuComputePassEncoderSetBindGroup(pass, 0, resetGroup, 0, nullptr);
        wgpuComputePassEncoderSetPipeline(pass, resetPipeline_);
        wgpuComputePassEncoderDispatchWorkgroups(
            pass, std::max(bodyGroups, eventGroups), 1, 1);
        wgpuComputePassEncoderSetBindGroup(
            pass, 0, prepareUnionGroup, 0, nullptr);
        wgpuComputePassEncoderSetPipeline(pass, prepareUnionPipeline_);
        wgpuComputePassEncoderDispatchWorkgroups(pass, 1, 1, 1);
        wgpuComputePassEncoderEnd(pass);
        wgpuComputePassEncoderRelease(pass);

        pass = wgpuCommandEncoderBeginComputePass(encoder, &passDesc);
        wgpuComputePassEncoderSetBindGroup(pass, 0, unionGroup, 0, nullptr);
        // Hooking propagates the minimum component label across one edge,
        // then pointer jumping doubles the covered distance. Therefore
        // ceil(log2(bodyCapacity)) rounds reach every vertex in any component.
        // Keep a smaller explicit configuration intact for callers that chose
        // an approximate budget, and preserve configured-round telemetry.
        const uint32_t convergenceRounds = std::min(
            config_.unionRounds,
            std::max(
                static_cast<uint32_t>(
                    std::bit_width(input_.bodyCapacity - 1u)),
                1u));
        for (uint32_t round = 0; round < convergenceRounds; ++round) {
            wgpuComputePassEncoderSetPipeline(pass, unionPipeline_);
            wgpuComputePassEncoderDispatchWorkgroupsIndirect(
                pass, telemetry_, 25u * sizeof(uint32_t));
            wgpuComputePassEncoderSetPipeline(pass, compressPipeline_);
            wgpuComputePassEncoderDispatchWorkgroupsIndirect(
                pass, telemetry_, 28u * sizeof(uint32_t));
        }
        wgpuComputePassEncoderEnd(pass);
        wgpuComputePassEncoderRelease(pass);

        pass = wgpuCommandEncoderBeginComputePass(encoder, &passDesc);
        wgpuComputePassEncoderSetBindGroup(pass, 0, recordGroup, 0, nullptr);
        wgpuComputePassEncoderSetPipeline(pass, recordPipeline_);
        wgpuComputePassEncoderDispatchWorkgroups(pass, bodyGroups, 1, 1);
        wgpuComputePassEncoderEnd(pass);
        wgpuComputePassEncoderRelease(pass);

        if (!primitives_.encodeRadixSortBoundedU32x2(
                encoder, bodyRecords_, sortedBodyRecords_,
                input_.bodyCapacity, input_.bodyCapacity, 8u, telemetry_,
                28u * sizeof(uint32_t), 31u * sizeof(uint32_t), telemetry_,
                34u)) return false;

        const std::array<gpu::BindGroupEntry, 6> classifyEntries = {
            gpu::BindGroupEntry(1).buffer(input_.motionBuffer),
            gpu::BindGroupEntry(3).buffer(input_.metadataBuffer),
            gpu::BindGroupEntry(6).buffer(roots_),
            gpu::BindGroupEntry(12).buffer(scratch_),
            gpu::BindGroupEntry(14).buffer(bodyPersistent_), parameterEntry()};
        const std::array<gpu::BindGroupEntry, 8> decideEntries = {
            gpu::BindGroupEntry(9).buffer(telemetry_),
            gpu::BindGroupEntry(11).buffer(islands_),
            gpu::BindGroupEntry(12).buffer(scratch_),
            gpu::BindGroupEntry(13).buffer(islandPersistent_),
            gpu::BindGroupEntry(18).buffer(events_),
            gpu::BindGroupEntry(19).buffer(rangePredicates_),
            gpu::BindGroupEntry(21).buffer(pendingEvents_), parameterEntry()};
        const std::array<gpu::BindGroupEntry, 6> eventEntries = {
            gpu::BindGroupEntry(9).buffer(telemetry_),
            gpu::BindGroupEntry(18).buffer(events_),
            gpu::BindGroupEntry(19).buffer(rangePredicates_),
            gpu::BindGroupEntry(20).buffer(rangeIndices_),
            gpu::BindGroupEntry(21).buffer(pendingEvents_), parameterEntry()};
        const std::array<gpu::BindGroupEntry, 9> applyEntries = {
            gpu::BindGroupEntry(0).buffer(input_.poseBuffer),
            gpu::BindGroupEntry(1).buffer(input_.motionBuffer),
            gpu::BindGroupEntry(3).buffer(input_.metadataBuffer),
            gpu::BindGroupEntry(6).buffer(roots_),
            gpu::BindGroupEntry(9).buffer(telemetry_),
            gpu::BindGroupEntry(13).buffer(islandPersistent_),
            gpu::BindGroupEntry(14).buffer(bodyPersistent_),
            gpu::BindGroupEntry(15).buffer(sleepingGrid_), parameterEntry()};
        WGPUBindGroup rangeGroup = cachedStaticGroups_[0];
        WGPUBindGroup classifyGroup = cachedInputGroup(
            inputGroupCache, 4u, classifyLayout_, classifyEntries,
            "island_classify_group");
        WGPUBindGroup decideGroup = cachedInputGroup(
            inputGroupCache, 5u, decideLayout_, decideEntries,
            "island_decide_group");
        WGPUBindGroup eventGroup = cachedInputGroup(
            inputGroupCache, 6u, eventLayout_, eventEntries,
            "island_event_group");
        WGPUBindGroup applyGroup = cachedInputGroup(
            inputGroupCache, 7u, applyLayout_, applyEntries,
            "island_apply_group");
        if (!rangeGroup || !classifyGroup || !decideGroup || !eventGroup
            || !applyGroup) return false;
        pass = wgpuCommandEncoderBeginComputePass(encoder, &passDesc);
        wgpuComputePassEncoderSetBindGroup(pass, 0, rangeGroup, 0, nullptr);
        wgpuComputePassEncoderSetPipeline(pass, markRangePipeline_);
        wgpuComputePassEncoderDispatchWorkgroups(pass, bodyGroups, 1, 1);
        wgpuComputePassEncoderEnd(pass);
        wgpuComputePassEncoderRelease(pass);

        if (!primitives_.encodeScanU32(
                encoder, rangePredicates_, rangeIndices_,
                input_.bodyCapacity, 2u)) return false;

        pass = wgpuCommandEncoderBeginComputePass(encoder, &passDesc);
        wgpuComputePassEncoderSetBindGroup(pass, 0, rangeGroup, 0, nullptr);
        wgpuComputePassEncoderSetPipeline(pass, finalizeRangePipeline_);
        wgpuComputePassEncoderDispatchWorkgroups(pass, 1, 1, 1);
        wgpuComputePassEncoderSetPipeline(pass, scatterRangeStartsPipeline_);
        wgpuComputePassEncoderDispatchWorkgroups(pass, bodyGroups, 1, 1);
        wgpuComputePassEncoderSetPipeline(pass, scatterRangeEndsPipeline_);
        wgpuComputePassEncoderDispatchWorkgroups(pass, bodyGroups, 1, 1);
        wgpuComputePassEncoderSetBindGroup(pass, 0, classifyGroup, 0, nullptr);
        wgpuComputePassEncoderSetPipeline(pass, classifyPipeline_);
        wgpuComputePassEncoderDispatchWorkgroups(pass, bodyGroups, 1, 1);
        wgpuComputePassEncoderSetBindGroup(pass, 0, decideGroup, 0, nullptr);
        wgpuComputePassEncoderSetPipeline(pass, decidePipeline_);
        wgpuComputePassEncoderDispatchWorkgroups(pass, bodyGroups, 1, 1);
        wgpuComputePassEncoderEnd(pass);
        wgpuComputePassEncoderRelease(pass);

        if (!primitives_.encodeScanU32(
                encoder, rangePredicates_, rangeIndices_,
                input_.bodyCapacity, 3u)) return false;

        pass = wgpuCommandEncoderBeginComputePass(encoder, &passDesc);
        wgpuComputePassEncoderSetBindGroup(pass, 0, eventGroup, 0, nullptr);
        wgpuComputePassEncoderSetPipeline(pass, finalizeEventsPipeline_);
        wgpuComputePassEncoderDispatchWorkgroups(pass, 1, 1, 1);
        wgpuComputePassEncoderSetPipeline(pass, scatterEventsPipeline_);
        wgpuComputePassEncoderDispatchWorkgroups(pass, bodyGroups, 1, 1);
        wgpuComputePassEncoderSetBindGroup(pass, 0, applyGroup, 0, nullptr);
        wgpuComputePassEncoderSetPipeline(pass, applyPipeline_);
        wgpuComputePassEncoderDispatchWorkgroups(pass, bodyGroups, 1, 1);
        wgpuComputePassEncoderEnd(pass);
        wgpuComputePassEncoderRelease(pass);

        WGPUBindGroup sleepingRangeGroup = cachedStaticGroups_[1];
        pass = wgpuCommandEncoderBeginComputePass(encoder, &passDesc);
        wgpuComputePassEncoderSetBindGroup(
            pass, 0, sleepingRangeGroup, 0, nullptr);
        wgpuComputePassEncoderSetPipeline(pass, markSleepingEntriesPipeline_);
        wgpuComputePassEncoderDispatchWorkgroups(pass, bodyGroups, 1, 1);
        wgpuComputePassEncoderEnd(pass);
        wgpuComputePassEncoderRelease(pass);

        if (!primitives_.encodeScanU32(
                encoder, rangePredicates_, rangeIndices_,
                input_.bodyCapacity, 4u)) {
            return false;
        }

        pass = wgpuCommandEncoderBeginComputePass(encoder, &passDesc);
        wgpuComputePassEncoderSetBindGroup(
            pass, 0, sleepingRangeGroup, 0, nullptr);
        wgpuComputePassEncoderSetPipeline(pass, compactSleepingEntriesPipeline_);
        wgpuComputePassEncoderDispatchWorkgroups(pass, bodyGroups, 1, 1);
        wgpuComputePassEncoderSetPipeline(pass, finalizeSleepingEntriesPipeline_);
        wgpuComputePassEncoderDispatchWorkgroups(pass, 1, 1, 1);
        wgpuComputePassEncoderEnd(pass);
        wgpuComputePassEncoderRelease(pass);

        if (!primitives_.encodeRadixSort(
                encoder, compactedSleepingGrid_, sortedSleepingGrid_,
                input_.bodyCapacity, 2u, 24u, telemetry_,
                19u * sizeof(uint32_t), 22u * sizeof(uint32_t), telemetry_,
                9u)) {
            return false;
        }

        pass = wgpuCommandEncoderBeginComputePass(encoder, &passDesc);
        wgpuComputePassEncoderSetBindGroup(
            pass, 0, sleepingRangeGroup, 0, nullptr);
        wgpuComputePassEncoderSetPipeline(pass, markSleepingRangePipeline_);
        wgpuComputePassEncoderDispatchWorkgroups(pass, bodyGroups, 1, 1);
        wgpuComputePassEncoderEnd(pass);
        wgpuComputePassEncoderRelease(pass);

        if (!primitives_.encodeScanU32(
                encoder, rangePredicates_, rangeIndices_,
                input_.bodyCapacity, 5u, telemetry_,
                19u * sizeof(uint32_t), 22u * sizeof(uint32_t), telemetry_,
                9u)) {
            return false;
        }

        pass = wgpuCommandEncoderBeginComputePass(encoder, &passDesc);
        wgpuComputePassEncoderSetBindGroup(
            pass, 0, sleepingRangeGroup, 0, nullptr);
        wgpuComputePassEncoderSetPipeline(pass, finalizeSleepingRangePipeline_);
        wgpuComputePassEncoderDispatchWorkgroups(pass, 1, 1, 1);
        wgpuComputePassEncoderSetPipeline(
            pass, scatterSleepingRangeStartsPipeline_);
        wgpuComputePassEncoderDispatchWorkgroups(pass, bodyGroups, 1, 1);
        wgpuComputePassEncoderSetPipeline(
            pass, scatterSleepingRangeEndsPipeline_);
        wgpuComputePassEncoderDispatchWorkgroups(pass, bodyGroups, 1, 1);
        wgpuComputePassEncoderEnd(pass);
        wgpuComputePassEncoderRelease(pass);
        return true;
    }

    void shutdown() {
        releaseAllCachedInputGroups();
        for (WGPUBindGroup& group : cachedStaticGroups_) {
            releaseHandle(group, wgpuBindGroupRelease);
        }
        for (WGPUComputePipeline* pipeline : {
                 &resetPipeline_, &prepareUnionPipeline_, &unionPipeline_,
                 &compressPipeline_,
                 &recordPipeline_, &markRangePipeline_,
                 &finalizeRangePipeline_, &scatterRangeStartsPipeline_,
                 &scatterRangeEndsPipeline_, &classifyPipeline_,
                 &decidePipeline_, &finalizeEventsPipeline_,
                 &scatterEventsPipeline_, &applyPipeline_,
                 &markSleepingEntriesPipeline_,
                 &compactSleepingEntriesPipeline_,
                 &finalizeSleepingEntriesPipeline_, &markSleepingRangePipeline_,
                 &finalizeSleepingRangePipeline_,
                 &scatterSleepingRangeStartsPipeline_,
                 &scatterSleepingRangeEndsPipeline_, &smallBuildPipeline_,
                 &smallDecidePipeline_, &smallGridPipeline_})
            releaseHandle(*pipeline, wgpuComputePipelineRelease);
        for (WGPUPipelineLayout* layout : {
                 &resetPipelineLayout_, &prepareUnionPipelineLayout_,
                 &unionPipelineLayout_,
                 &recordPipelineLayout_, &rangePipelineLayout_,
                 &classifyPipelineLayout_, &decidePipelineLayout_,
                 &eventPipelineLayout_, &applyPipelineLayout_,
                 &sleepingRangePipelineLayout_, &smallBuildPipelineLayout_,
                 &smallDecidePipelineLayout_, &smallGridPipelineLayout_})
            releaseHandle(*layout, wgpuPipelineLayoutRelease);
        for (WGPUBindGroupLayout* layout : {
                 &resetLayout_, &prepareUnionLayout_, &unionLayout_,
                 &recordLayout_, &rangeLayout_,
                 &classifyLayout_, &decideLayout_, &eventLayout_, &applyLayout_,
                 &sleepingRangeLayout_, &smallBuildLayout_, &smallDecideLayout_,
                 &smallGridLayout_})
            releaseHandle(*layout, wgpuBindGroupLayoutRelease);
        releaseHandle(shaderModule_, wgpuShaderModuleRelease);
        primitives_.shutdown();
        for (WGPUBuffer* buffer : {
                 &parameterBuffer_, &roots_, &bodyRecords_, &sortedBodyRecords_,
                 &islands_, &scratch_, &islandPersistent_, &bodyPersistent_,
                 &sleepingGrid_, &sortedSleepingGrid_, &compactedSleepingGrid_,
                 &sleepingRanges_,
                 &rangePredicates_, &rangeIndices_, &pendingEvents_, &events_,
                 &telemetry_})
            releaseBuffer(*buffer);
        device_ = nullptr;
        queue_ = nullptr;
        config_ = {};
        input_ = {};
        inputBindGroupCacheMisses_ = 0;
        scratchBytes_ = 0;
    }

    WGPUDevice device_ = nullptr;
    WGPUQueue queue_ = nullptr;
    Config config_{};
    GpuIslandInput input_{};
    DeterministicGpuPrimitives primitives_;
    std::array<WGPUBindGroup, 2> cachedStaticGroups_{};
    std::array<CachedInputGroups, 2> cachedInputGroups_{};
    uint32_t nextInputGroupReplacement_ = 0u;
    size_t scratchBytes_ = 0;
    size_t inputBindGroupCacheMisses_ = 0;
    WGPUBuffer parameterBuffer_ = nullptr;
    WGPUBuffer roots_ = nullptr;
    WGPUBuffer bodyRecords_ = nullptr;
    WGPUBuffer sortedBodyRecords_ = nullptr;
    WGPUBuffer islands_ = nullptr;
    WGPUBuffer scratch_ = nullptr;
    WGPUBuffer islandPersistent_ = nullptr;
    WGPUBuffer bodyPersistent_ = nullptr;
    WGPUBuffer sleepingGrid_ = nullptr;
    WGPUBuffer sortedSleepingGrid_ = nullptr;
    WGPUBuffer compactedSleepingGrid_ = nullptr;
    WGPUBuffer sleepingRanges_ = nullptr;
    WGPUBuffer rangePredicates_ = nullptr;
    WGPUBuffer rangeIndices_ = nullptr;
    WGPUBuffer pendingEvents_ = nullptr;
    WGPUBuffer events_ = nullptr;
    WGPUBuffer telemetry_ = nullptr;
    WGPUShaderModule shaderModule_ = nullptr;
    WGPUBindGroupLayout resetLayout_ = nullptr;
    WGPUBindGroupLayout prepareUnionLayout_ = nullptr;
    WGPUBindGroupLayout unionLayout_ = nullptr;
    WGPUBindGroupLayout recordLayout_ = nullptr;
    WGPUBindGroupLayout rangeLayout_ = nullptr;
    WGPUBindGroupLayout classifyLayout_ = nullptr;
    WGPUBindGroupLayout decideLayout_ = nullptr;
    WGPUBindGroupLayout eventLayout_ = nullptr;
    WGPUBindGroupLayout applyLayout_ = nullptr;
    WGPUBindGroupLayout sleepingRangeLayout_ = nullptr;
    WGPUBindGroupLayout smallBuildLayout_ = nullptr;
    WGPUBindGroupLayout smallDecideLayout_ = nullptr;
    WGPUBindGroupLayout smallGridLayout_ = nullptr;
    WGPUPipelineLayout resetPipelineLayout_ = nullptr;
    WGPUPipelineLayout prepareUnionPipelineLayout_ = nullptr;
    WGPUPipelineLayout unionPipelineLayout_ = nullptr;
    WGPUPipelineLayout recordPipelineLayout_ = nullptr;
    WGPUPipelineLayout rangePipelineLayout_ = nullptr;
    WGPUPipelineLayout classifyPipelineLayout_ = nullptr;
    WGPUPipelineLayout decidePipelineLayout_ = nullptr;
    WGPUPipelineLayout eventPipelineLayout_ = nullptr;
    WGPUPipelineLayout applyPipelineLayout_ = nullptr;
    WGPUPipelineLayout sleepingRangePipelineLayout_ = nullptr;
    WGPUPipelineLayout smallBuildPipelineLayout_ = nullptr;
    WGPUPipelineLayout smallDecidePipelineLayout_ = nullptr;
    WGPUPipelineLayout smallGridPipelineLayout_ = nullptr;
    WGPUComputePipeline resetPipeline_ = nullptr;
    WGPUComputePipeline prepareUnionPipeline_ = nullptr;
    WGPUComputePipeline unionPipeline_ = nullptr;
    WGPUComputePipeline compressPipeline_ = nullptr;
    WGPUComputePipeline recordPipeline_ = nullptr;
    WGPUComputePipeline markRangePipeline_ = nullptr;
    WGPUComputePipeline finalizeRangePipeline_ = nullptr;
    WGPUComputePipeline scatterRangeStartsPipeline_ = nullptr;
    WGPUComputePipeline scatterRangeEndsPipeline_ = nullptr;
    WGPUComputePipeline classifyPipeline_ = nullptr;
    WGPUComputePipeline decidePipeline_ = nullptr;
    WGPUComputePipeline finalizeEventsPipeline_ = nullptr;
    WGPUComputePipeline scatterEventsPipeline_ = nullptr;
    WGPUComputePipeline applyPipeline_ = nullptr;
    WGPUComputePipeline markSleepingEntriesPipeline_ = nullptr;
    WGPUComputePipeline compactSleepingEntriesPipeline_ = nullptr;
    WGPUComputePipeline finalizeSleepingEntriesPipeline_ = nullptr;
    WGPUComputePipeline markSleepingRangePipeline_ = nullptr;
    WGPUComputePipeline finalizeSleepingRangePipeline_ = nullptr;
    WGPUComputePipeline scatterSleepingRangeStartsPipeline_ = nullptr;
    WGPUComputePipeline scatterSleepingRangeEndsPipeline_ = nullptr;
    WGPUComputePipeline smallBuildPipeline_ = nullptr;
    WGPUComputePipeline smallDecidePipeline_ = nullptr;
    WGPUComputePipeline smallGridPipeline_ = nullptr;
};

GpuIslandManager::GpuIslandManager() : impl_(std::make_unique<Impl>()) {}
GpuIslandManager::~GpuIslandManager() = default;
GpuIslandManager::GpuIslandManager(GpuIslandManager&&) noexcept = default;
GpuIslandManager& GpuIslandManager::operator=(GpuIslandManager&&) noexcept = default;

bool GpuIslandManager::initialize(WGPUDevice device, WGPUQueue queue,
                                  const Config& config) {
    return impl_->initialize(device, queue, config);
}
void GpuIslandManager::shutdown() { impl_->shutdown(); }
void GpuIslandManager::setInput(const GpuIslandInput& input) {
    impl_->setInput(input);
}
bool GpuIslandManager::encode(WGPUCommandEncoder encoder,
                              bool compactSmallWorld) {
    return impl_->encode(encoder, compactSmallWorld);
}
WGPUBuffer GpuIslandManager::bodyRoots() const noexcept { return impl_->roots_; }
WGPUBuffer GpuIslandManager::sortedBodyRecords() const noexcept {
    return impl_->sortedBodyRecords_;
}
WGPUBuffer GpuIslandManager::islands() const noexcept { return impl_->islands_; }
WGPUBuffer GpuIslandManager::sleepingGridEntries() const noexcept {
    return impl_->sortedSleepingGrid_;
}
WGPUBuffer GpuIslandManager::sleepingCellRanges() const noexcept {
    return impl_->sleepingRanges_;
}
WGPUBuffer GpuIslandManager::events() const noexcept { return impl_->events_; }
WGPUBuffer GpuIslandManager::telemetryBuffer() const noexcept {
    return impl_->telemetry_;
}
size_t GpuIslandManager::scratchBytes() const noexcept {
    return impl_->scratchBytes_;
}
size_t GpuIslandManager::inputBindGroupCacheMisses() const noexcept {
    return impl_->inputBindGroupCacheMisses_;
}

GpuIslandTelemetry GpuIslandManager::decodeTelemetry(
    std::span<const uint32_t> words) noexcept {
    GpuIslandTelemetry result;
    if (words.size() < 20) return result;
    result.islandCount = words[0];
    result.awakeIslands = words[1];
    result.sleepingIslands = words[2];
    result.awakeBodies = words[3];
    result.sleepingBodies = words[4];
    result.maximumIslandBodies = words[5];
    result.sleepTransitions = words[6];
    result.wakeTransitions = words[7];
    result.events = words[8];
    result.sleepingGridEntries = words[9];
    result.sleepingGridCells = words[10];
    result.rootErrors = words[11];
    result.unionRounds = words[12];
    result.tick = words[13];
    result.highIslands = words[14];
    result.highSleepingBodies = words[15];
    result.highSleepingGridEntries = words[16];
    result.highEvents = words[19];
    result.eventOverflow = words[17] != 0u;
    result.gridOverflow = words[18] != 0u;
    return result;
}

} // namespace voxy::physics
