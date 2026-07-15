#include "physics/gpu/gpu_broad_phase.hpp"

#include "core/log.hpp"
#include "gpu/resources.hpp"
#include "physics/physics_types.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace voxy::physics {
namespace {

constexpr uint32_t kTelemetryWords = GpuBroadPhase::kTelemetryWordCount;

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

WGPUComputePipeline makePipeline(WGPUDevice device,
                                 WGPUPipelineLayout layout,
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

template <size_t N>
WGPUPipelineLayout pipelineLayout(
    WGPUDevice device, const std::array<WGPUBindGroupLayout, N>& layouts,
    const char* label) {
    return gpu::createPipelineLayout(device, layouts, label);
}

} // namespace

class GpuBroadPhase::Impl {
public:
    struct alignas(16) Params {
        std::array<uint32_t, 4> counts{};
        std::array<uint32_t, 4> capacities{};
        std::array<float, 4> grid{};
    };

    ~Impl() { shutdown(); }

    bool initialize(WGPUDevice device, WGPUQueue queue, const Config& config) {
        if (device_ || !device || !queue || config.bodyCapacity == 0
            || config.candidatePairCapacity == 0 || config.pairCapacity == 0
            || config.contactCapacity == 0
            || !validSectorCellSize(config.cellSize)
            || (config.workgroupSize != 64 && config.workgroupSize != 128
                && config.workgroupSize != 256)) return false;
        const uint64_t entryCapacity = config.bodyCapacity;
        const uint64_t ownerCapacity = uint64_t{config.bodyCapacity} * 2u;
        if (ownerCapacity > std::numeric_limits<uint32_t>::max()) return false;

        device_ = device;
        queue_ = queue;
        config_ = config;
        entryCapacity_ = static_cast<uint32_t>(entryCapacity);
        ownerCapacity_ = static_cast<uint32_t>(ownerCapacity);
        const uint32_t primitiveCapacity = std::max({
            config.bodyCapacity, entryCapacity_, ownerCapacity_,
            config.candidatePairCapacity, config.pairCapacity,
            config.contactCapacity});
        DeterministicGpuPrimitives::Config primitiveConfig;
        primitiveConfig.capacity = primitiveCapacity;
        primitiveConfig.workgroupSize = config.workgroupSize;
        primitiveConfig.shaderPath = config.primitivesShaderPath;
        if (!primitives_.initialize(device_, queue_, primitiveConfig)) {
            shutdown();
            return false;
        }

        parameterBuffer_ = makeBuffer(sizeof(Params), "broad_phase_params",
                                      WGPUBufferUsage_Uniform
                                      | WGPUBufferUsage_CopyDst);
        bodyEntryCounts_ = makeStorage(config.bodyCapacity,
                                       sizeof(uint32_t), "grid_entry_counts");
        bodyEntryOffsets_ = makeStorage(config.bodyCapacity,
                                        sizeof(uint32_t), "grid_entry_offsets");
        gridEntries_ = makeStorage(entryCapacity_, sizeof(GpuKeyValue),
                                   "grid_entries");
        sortedGridEntries_ = makeStorage(entryCapacity_, sizeof(GpuKeyValue),
                                         "sorted_grid_entries");
        cellRanges_ = makeStorage(uint64_t{entryCapacity_} + 1u,
                                  sizeof(GpuCellRange),
                                  "occupied_cell_ranges");
        rangePredicates_ = makeStorage(entryCapacity_, sizeof(uint32_t),
                                       "cell_range_predicates");
        entryRangeIndices_ = makeStorage(entryCapacity_, sizeof(uint32_t),
                                         "cell_range_indices");
        oversizedFlags_ = makeStorage(uint64_t{config.bodyCapacity} + 2u,
                                      sizeof(uint32_t),
                                      "oversized_body_flags");
        dispatchArgs_ = makeBuffer(
            18u * sizeof(uint32_t), "broad_phase_dispatch_args",
            WGPUBufferUsage_Storage | WGPUBufferUsage_CopyDst
                | WGPUBufferUsage_Indirect);
        ownerPairCounts_ = makeStorage(ownerCapacity_, sizeof(uint32_t),
                                       "owner_pair_counts");
        ownerPairOffsets_ = makeStorage(ownerCapacity_, sizeof(uint32_t),
                                        "owner_pair_offsets");
        pairCandidates_ = makeStorage(config.candidatePairCapacity,
                                      sizeof(GpuKeyValue), "pair_candidates");
        sortedPairCandidates_ = makeStorage(
            config.candidatePairCapacity, sizeof(GpuKeyValue),
            "sorted_pair_candidates");
        uniquePairs_ = makeStorage(config.pairCapacity, sizeof(GpuKeyValue),
                                   "unique_body_pairs");
        contactsA_ = makeStorage(config.contactCapacity,
                                 sizeof(GpuPersistentContact), "contacts_a");
        contactsB_ = makeStorage(config.contactCapacity,
                                 sizeof(GpuPersistentContact), "contacts_b");
        contactOccupancy_ = makeStorage(config.contactCapacity,
                                        sizeof(uint32_t), "contact_occupancy");
        contactEvents_ = makeStorage(uint64_t{config.contactCapacity} * 2u,
                                     sizeof(GpuContactEvent), "contact_events");
        lifecycleState_ = makeStorage(4, sizeof(uint32_t), "lifecycle_state");
        lifecycleNewPredicates_ = makeStorage(
            config.contactCapacity, sizeof(uint32_t),
            "lifecycle_new_predicates");
        lifecycleNewOffsets_ = makeStorage(
            config.contactCapacity, sizeof(uint32_t),
            "lifecycle_new_offsets");
        lifecycleEndPredicates_ = makeStorage(
            config.contactCapacity, sizeof(uint32_t),
            "lifecycle_end_predicates");
        lifecycleEndOffsets_ = makeStorage(
            config.contactCapacity, sizeof(uint32_t),
            "lifecycle_end_offsets");
        lifecycleFreePredicates_ = makeStorage(
            config.contactCapacity, sizeof(uint32_t),
            "lifecycle_free_predicates");
        lifecycleFreeOffsets_ = makeStorage(
            config.contactCapacity, sizeof(uint32_t),
            "lifecycle_free_offsets");
        lifecycleFreeIds_ = makeStorage(
            config.contactCapacity, sizeof(uint32_t), "lifecycle_free_ids");
        telemetry_ = makeBuffer(
            uint64_t{kTelemetryWords} * sizeof(uint32_t),
            "broad_phase_telemetry",
            WGPUBufferUsage_Storage | WGPUBufferUsage_CopyDst
                | WGPUBufferUsage_CopySrc | WGPUBufferUsage_Indirect);
        if (!parameterBuffer_ || !bodyEntryCounts_ || !bodyEntryOffsets_
            || !gridEntries_ || !sortedGridEntries_ || !cellRanges_
            || !rangePredicates_ || !entryRangeIndices_ || !oversizedFlags_
            || !dispatchArgs_
            || !ownerPairCounts_ || !ownerPairOffsets_
            || !pairCandidates_ || !sortedPairCandidates_ || !uniquePairs_
            || !contactsA_ || !contactsB_ || !contactOccupancy_
            || !contactEvents_ || !lifecycleState_
            || !lifecycleNewPredicates_ || !lifecycleNewOffsets_
            || !lifecycleEndPredicates_ || !lifecycleEndOffsets_
            || !lifecycleFreePredicates_ || !lifecycleFreeOffsets_
            || !lifecycleFreeIds_ || !telemetry_) {
            shutdown();
            return false;
        }
        scratchBytes_ = gpu::saturatingSize(
            primitives_.scratchBytes()
            + (uint64_t{config.bodyCapacity} * 3u + 2u + 18u)
                * sizeof(uint32_t)
            + uint64_t{entryCapacity_}
                * (2u * sizeof(GpuKeyValue) + sizeof(GpuCellRange)
                   + 2u * sizeof(uint32_t))
            + sizeof(GpuCellRange)
            + uint64_t{ownerCapacity_} * 2u * sizeof(uint32_t)
            + uint64_t{config.candidatePairCapacity} * 2u * sizeof(GpuKeyValue)
            + uint64_t{config.pairCapacity} * sizeof(GpuKeyValue)
            + uint64_t{config.contactCapacity}
                * (2u * sizeof(GpuPersistentContact) + sizeof(uint32_t)
                   + 2u * sizeof(GpuContactEvent)
                   + 7u * sizeof(uint32_t))
            + sizeof(Params) + (kTelemetryWords + 4u) * sizeof(uint32_t));

        shaderModule_ = gpu::loadShaderModule(
            device_, config.shaderPath, "physics_broad_phase.wgsl");
        if (!shaderModule_ || !createPipelines()) {
            shutdown();
            return false;
        }
        return true;
    }

    WGPUBuffer makeBuffer(uint64_t bytes, const char* label,
                          gpu::WGPUBufferUsageFlags usage) {
        return gpu::createBuffer(device_, gpu::BufferDesc{
            .label = label, .size = bytes, .usage = usage});
    }

    WGPUBuffer makeStorage(uint64_t count, uint64_t stride, const char* label) {
        return makeBuffer(count * stride, label,
                          WGPUBufferUsage_Storage | WGPUBufferUsage_CopyDst
                          | WGPUBufferUsage_CopySrc);
    }

    bool createPipelines() {
        using LE = gpu::BindGroupLayoutEntry;
        auto storage = [](std::vector<LE>& entries, uint32_t binding,
                          bool readOnly) {
            entries.emplace_back(binding).computeVisible().storageBuffer(readOnly);
        };
        auto uniform = [](std::vector<LE>& entries) {
            entries.emplace_back(7).computeVisible().uniformBuffer(false,
                                                                    sizeof(Params));
        };

        std::vector<LE> gridEntries;
        for (uint32_t binding : {0u, 1u, 2u}) storage(gridEntries, binding, true);
        storage(gridEntries, 3, false);
        storage(gridEntries, 4, true);
        storage(gridEntries, 5, false);
        storage(gridEntries, 6, false);
        storage(gridEntries, 10, false);
        uniform(gridEntries);
        gridLayout_ = gpu::createBindGroupLayout(
            device_, gridEntries, "broad_phase_grid_layout");

        std::vector<LE> rangeEntries;
        storage(rangeEntries, 6, false);
        storage(rangeEntries, 8, true);
        storage(rangeEntries, 9, false);
        storage(rangeEntries, 21, false);
        storage(rangeEntries, 22, true);
        uniform(rangeEntries);
        rangeLayout_ = gpu::createBindGroupLayout(
            device_, rangeEntries, "broad_phase_range_layout");

        std::vector<LE> finalizeEntries;
        storage(finalizeEntries, 3, false);
        storage(finalizeEntries, 4, true);
        storage(finalizeEntries, 6, false);
        storage(finalizeEntries, 9, false);
        storage(finalizeEntries, 10, false);
        storage(finalizeEntries, 21, false);
        storage(finalizeEntries, 22, true);
        storage(finalizeEntries, 23, false);
        uniform(finalizeEntries);
        finalizeLayout_ = gpu::createBindGroupLayout(
            device_, finalizeEntries, "broad_phase_finalize_layout");

        std::vector<LE> pairCountEntries;
        for (uint32_t binding : {0u, 1u, 2u, 8u})
            storage(pairCountEntries, binding, true);
        storage(pairCountEntries, 9, false);
        storage(pairCountEntries, 11, false);
        uniform(pairCountEntries);
        pairCountLayout_ = gpu::createBindGroupLayout(
            device_, pairCountEntries, "broad_phase_pair_count_layout");

        std::vector<LE> pairScatterEntries;
        for (uint32_t binding : {0u, 1u, 2u, 8u})
            storage(pairScatterEntries, binding, true);
        storage(pairScatterEntries, 9, false);
        storage(pairScatterEntries, 11, false);
        storage(pairScatterEntries, 12, true);
        storage(pairScatterEntries, 13, false);
        uniform(pairScatterEntries);
        pairScatterLayout_ = gpu::createBindGroupLayout(
            device_, pairScatterEntries, "broad_phase_pair_scatter_layout");

        std::vector<LE> candidateClearEntries;
        storage(candidateClearEntries, 13, false);
        storage(candidateClearEntries, 11, false);
        storage(candidateClearEntries, 12, true);
        storage(candidateClearEntries, 6, false);
        storage(candidateClearEntries, 10, false);
        storage(candidateClearEntries, 23, false);
        uniform(candidateClearEntries);
        candidateClearLayout_ = gpu::createBindGroupLayout(
            device_, candidateClearEntries, "broad_phase_candidate_clear_layout");

        std::vector<LE> uniqueEntries;
        storage(uniqueEntries, 14, true);
        storage(uniqueEntries, 15, false);
        storage(uniqueEntries, 11, false);
        storage(uniqueEntries, 12, true);
        storage(uniqueEntries, 6, false);
        uniform(uniqueEntries);
        uniqueLayout_ = gpu::createBindGroupLayout(
            device_, uniqueEntries, "broad_phase_unique_layout");

        std::vector<LE> lifecyclePrepareEntries;
        for (uint32_t binding : {6u, 15u})
            storage(lifecyclePrepareEntries, binding, false);
        storage(lifecyclePrepareEntries, 16, true);
        for (uint32_t binding : {17u, 18u, 20u, 24u, 26u})
            storage(lifecyclePrepareEntries, binding, false);
        uniform(lifecyclePrepareEntries);
        lifecyclePrepareLayout_ = gpu::createBindGroupLayout(
            device_, lifecyclePrepareEntries,
            "broad_phase_lifecycle_prepare_layout");

        std::vector<LE> lifecycleFreeEntries;
        for (uint32_t binding : {18u, 20u, 28u, 29u, 30u})
            storage(lifecycleFreeEntries, binding, false);
        uniform(lifecycleFreeEntries);
        lifecycleFreeLayout_ = gpu::createBindGroupLayout(
            device_, lifecycleFreeEntries,
            "broad_phase_lifecycle_free_layout");

        std::vector<LE> lifecycleBeginEntries;
        for (uint32_t binding : {
                 17u, 18u, 19u, 20u, 24u, 25u, 30u})
            storage(lifecycleBeginEntries, binding, false);
        uniform(lifecycleBeginEntries);
        lifecycleBeginLayout_ = gpu::createBindGroupLayout(
            device_, lifecycleBeginEntries,
            "broad_phase_lifecycle_begin_layout");

        std::vector<LE> lifecycleEndEntries;
        storage(lifecycleEndEntries, 16, true);
        for (uint32_t binding : {19u, 20u, 26u, 27u})
            storage(lifecycleEndEntries, binding, false);
        uniform(lifecycleEndEntries);
        lifecycleEndLayout_ = gpu::createBindGroupLayout(
            device_, lifecycleEndEntries,
            "broad_phase_lifecycle_end_layout");

        std::vector<LE> lifecycleFinalizeEntries;
        for (uint32_t binding : {
                 6u, 20u, 24u, 25u, 26u, 27u, 28u, 29u})
            storage(lifecycleFinalizeEntries, binding, false);
        uniform(lifecycleFinalizeEntries);
        lifecycleFinalizeLayout_ = gpu::createBindGroupLayout(
            device_, lifecycleFinalizeEntries,
            "broad_phase_lifecycle_finalize_layout");
        if (!gridLayout_ || !rangeLayout_ || !finalizeLayout_
            || !pairCountLayout_
            || !pairScatterLayout_ || !candidateClearLayout_ || !uniqueLayout_
            || !lifecyclePrepareLayout_ || !lifecycleFreeLayout_
            || !lifecycleBeginLayout_ || !lifecycleEndLayout_
            || !lifecycleFinalizeLayout_) return false;

        gridPipelineLayout_ = pipelineLayout(
            device_, std::array{gridLayout_}, "broad_phase_grid_pipeline_layout");
        rangePipelineLayout_ = pipelineLayout(
            device_, std::array{rangeLayout_}, "broad_phase_range_pipeline_layout");
        finalizePipelineLayout_ = pipelineLayout(
            device_, std::array{finalizeLayout_},
            "broad_phase_finalize_pipeline_layout");
        pairCountPipelineLayout_ = pipelineLayout(
            device_, std::array{pairCountLayout_},
            "broad_phase_pair_count_pipeline_layout");
        pairScatterPipelineLayout_ = pipelineLayout(
            device_, std::array{pairScatterLayout_},
            "broad_phase_pair_scatter_pipeline_layout");
        candidateClearPipelineLayout_ = pipelineLayout(
            device_, std::array{candidateClearLayout_},
            "broad_phase_candidate_clear_pipeline_layout");
        uniquePipelineLayout_ = pipelineLayout(
            device_, std::array{uniqueLayout_}, "broad_phase_unique_pipeline_layout");
        lifecyclePreparePipelineLayout_ = pipelineLayout(
            device_, std::array{lifecyclePrepareLayout_},
            "broad_phase_lifecycle_prepare_pipeline_layout");
        lifecycleFreePipelineLayout_ = pipelineLayout(
            device_, std::array{lifecycleFreeLayout_},
            "broad_phase_lifecycle_free_pipeline_layout");
        lifecycleBeginPipelineLayout_ = pipelineLayout(
            device_, std::array{lifecycleBeginLayout_},
            "broad_phase_lifecycle_begin_pipeline_layout");
        lifecycleEndPipelineLayout_ = pipelineLayout(
            device_, std::array{lifecycleEndLayout_},
            "broad_phase_lifecycle_end_pipeline_layout");
        lifecycleFinalizePipelineLayout_ = pipelineLayout(
            device_, std::array{lifecycleFinalizeLayout_},
            "broad_phase_lifecycle_finalize_pipeline_layout");
        if (!gridPipelineLayout_ || !rangePipelineLayout_
            || !finalizePipelineLayout_
            || !pairCountPipelineLayout_ || !pairScatterPipelineLayout_
            || !candidateClearPipelineLayout_ || !uniquePipelineLayout_
            || !lifecyclePreparePipelineLayout_ || !lifecycleFreePipelineLayout_
            || !lifecycleBeginPipelineLayout_ || !lifecycleEndPipelineLayout_
            || !lifecycleFinalizePipelineLayout_) return false;

        const std::string suffix = std::to_string(config_.workgroupSize);
        resetPipeline_ = makePipeline(device_, gridPipelineLayout_, shaderModule_,
                                      "reset_telemetry", "broad_phase_reset");
        clearEntriesPipeline_ = makePipeline(
            device_, gridPipelineLayout_, shaderModule_,
            "clear_grid_entries_" + suffix, "broad_phase_clear_entries");
        countEntriesPipeline_ = makePipeline(
            device_, gridPipelineLayout_, shaderModule_,
            "count_grid_entries_" + suffix, "broad_phase_count_entries");
        scatterEntriesPipeline_ = makePipeline(
            device_, gridPipelineLayout_, shaderModule_,
            "scatter_grid_entries_" + suffix, "broad_phase_scatter_entries");
        finalizeEntryCountPipeline_ = makePipeline(
            device_, finalizePipelineLayout_, shaderModule_,
            "finalize_grid_entry_count", "broad_phase_finalize_entry_count");
        markRangeStartsPipeline_ = makePipeline(
            device_, rangePipelineLayout_, shaderModule_,
            "mark_cell_range_starts_" + suffix,
            "broad_phase_mark_range_starts");
        finalizeRangeCountPipeline_ = makePipeline(
            device_, finalizePipelineLayout_, shaderModule_,
            "finalize_cell_range_count", "broad_phase_finalize_range_count");
        scatterRangeStartsPipeline_ = makePipeline(
            device_, rangePipelineLayout_, shaderModule_,
            "scatter_cell_range_starts_" + suffix,
            "broad_phase_scatter_range_starts");
        scatterRangeEndsPipeline_ = makePipeline(
            device_, rangePipelineLayout_, shaderModule_,
            "scatter_cell_range_ends_" + suffix,
            "broad_phase_scatter_range_ends");
        countPairsPipeline_ = makePipeline(
            device_, pairCountPipelineLayout_, shaderModule_,
            "count_pairs_" + suffix, "broad_phase_count_pairs");
        clearCandidatesPipeline_ = makePipeline(
            device_, candidateClearPipelineLayout_, shaderModule_,
            "clear_pair_candidates_" + suffix,
            "broad_phase_clear_candidates");
        scatterPairsPipeline_ = makePipeline(
            device_, pairScatterPipelineLayout_, shaderModule_,
            "scatter_pairs_" + suffix, "broad_phase_scatter_pairs");
        uniquePairsPipeline_ = makePipeline(
            device_, uniquePipelineLayout_, shaderModule_,
            "unique_pairs_" + suffix,
            "broad_phase_unique_pairs");
        lifecycleResetPipeline_ = makePipeline(
            device_, lifecyclePreparePipelineLayout_, shaderModule_,
            "lifecycle_reset_" + suffix,
            "broad_phase_lifecycle_reset");
        lifecyclePreparePipeline_ = makePipeline(
            device_, lifecyclePreparePipelineLayout_, shaderModule_,
            "lifecycle_prepare_" + suffix,
            "broad_phase_lifecycle_prepare");
        lifecycleMarkFreePipeline_ = makePipeline(
            device_, lifecycleFreePipelineLayout_, shaderModule_,
            "lifecycle_mark_free_" + suffix,
            "broad_phase_lifecycle_mark_free");
        lifecycleScatterFreePipeline_ = makePipeline(
            device_, lifecycleFreePipelineLayout_, shaderModule_,
            "lifecycle_scatter_free_" + suffix,
            "broad_phase_lifecycle_scatter_free");
        lifecycleAssignBeginPipeline_ = makePipeline(
            device_, lifecycleBeginPipelineLayout_, shaderModule_,
            "lifecycle_assign_begin_" + suffix,
            "broad_phase_lifecycle_assign_begin");
        lifecycleScatterEndPipeline_ = makePipeline(
            device_, lifecycleEndPipelineLayout_, shaderModule_,
            "lifecycle_scatter_end_" + suffix,
            "broad_phase_lifecycle_scatter_end");
        lifecycleFinalizePipeline_ = makePipeline(
            device_, lifecycleFinalizePipelineLayout_, shaderModule_,
            "lifecycle_finalize", "broad_phase_lifecycle_finalize");
        return resetPipeline_ && clearEntriesPipeline_ && countEntriesPipeline_
            && scatterEntriesPipeline_ && finalizeEntryCountPipeline_
            && markRangeStartsPipeline_ && finalizeRangeCountPipeline_
            && scatterRangeStartsPipeline_ && scatterRangeEndsPipeline_
            && countPairsPipeline_ && clearCandidatesPipeline_
            && scatterPairsPipeline_ && uniquePairsPipeline_
            && lifecycleResetPipeline_ && lifecyclePreparePipeline_
            && lifecycleMarkFreePipeline_ && lifecycleScatterFreePipeline_
            && lifecycleAssignBeginPipeline_ && lifecycleScatterEndPipeline_
            && lifecycleFinalizePipeline_;
    }

    void setBodyView(const BroadPhaseBodyView& view) {
        bodyView_ = view.bodyCapacity <= config_.bodyCapacity
            ? view : BroadPhaseBodyView{};
    }

    WGPUBindGroup bindGroup(WGPUBindGroupLayout layout,
                            std::span<const gpu::BindGroupEntry> entries,
                            const char* label) {
        return gpu::createBindGroup(device_, layout, entries, label);
    }

    bool encode(WGPUCommandEncoder encoder) {
        if (!encoder || !bodyView_.valid()) return false;
        const uint32_t bodyCount = bodyView_.bodyCapacity;
        const uint32_t ownerCount = bodyCount * 2u;
        const uint32_t bodyGroups =
            (bodyCount + config_.workgroupSize - 1u) / config_.workgroupSize;
        const float cellsPerSectorValue = kWorldSectorSize / config_.cellSize;
        const float roundedCellsPerSector = std::round(cellsPerSectorValue);
        const float cellsPerSector = std::abs(
            cellsPerSectorValue - roundedCellsPerSector) <= 1e-5f
            ? roundedCellsPerSector : 0.0f;
        const Params params{
            .counts = {bodyCount, entryCapacity_, ownerCount,
                       config_.candidatePairCapacity},
            .capacities = {config_.pairCapacity, config_.contactCapacity,
                           1u, config_.workgroupSize},
            .grid = {config_.cellSize, config_.speculativeMargin,
                     cellsPerSector, 0.0f},
        };
        gpu::writeBuffer(queue_, parameterBuffer_, 0, params);

        const std::array<gpu::BindGroupEntry, 9> gridEntries = {
            gpu::BindGroupEntry(0).buffer(bodyView_.poseBuffer),
            gpu::BindGroupEntry(1).buffer(bodyView_.shapeBuffer),
            gpu::BindGroupEntry(2).buffer(bodyView_.metadataBuffer),
            gpu::BindGroupEntry(3).buffer(bodyEntryCounts_),
            gpu::BindGroupEntry(4).buffer(bodyEntryOffsets_),
            gpu::BindGroupEntry(5).buffer(gridEntries_),
            gpu::BindGroupEntry(6).buffer(telemetry_),
            gpu::BindGroupEntry(10).buffer(oversizedFlags_),
            gpu::BindGroupEntry(7).buffer(parameterBuffer_),
        };
        WGPUBindGroup gridGroup = bindGroup(
            gridLayout_, gridEntries, "broad_phase_grid_bind_group");
        if (!gridGroup) return false;
        WGPUComputePassDescriptor passDesc{};
        WGPUComputePassEncoder pass =
            wgpuCommandEncoderBeginComputePass(encoder, &passDesc);
        wgpuComputePassEncoderSetBindGroup(pass, 0, gridGroup, 0, nullptr);
        wgpuComputePassEncoderSetPipeline(pass, resetPipeline_);
        wgpuComputePassEncoderDispatchWorkgroups(pass, 1, 1, 1);
        wgpuComputePassEncoderSetPipeline(pass, countEntriesPipeline_);
        wgpuComputePassEncoderDispatchWorkgroups(pass, bodyGroups, 1, 1);
        wgpuComputePassEncoderEnd(pass);
        wgpuComputePassEncoderRelease(pass);

        if (!primitives_.encodeScanU32(
                encoder, bodyEntryCounts_, bodyEntryOffsets_, bodyCount, 0u)) {
            wgpuBindGroupRelease(gridGroup);
            return false;
        }
        pass = wgpuCommandEncoderBeginComputePass(encoder, &passDesc);
        wgpuComputePassEncoderSetBindGroup(pass, 0, gridGroup, 0, nullptr);
        wgpuComputePassEncoderSetPipeline(pass, scatterEntriesPipeline_);
        wgpuComputePassEncoderDispatchWorkgroups(pass, bodyGroups, 1, 1);
        wgpuComputePassEncoderEnd(pass);
        wgpuComputePassEncoderRelease(pass);
        wgpuBindGroupRelease(gridGroup);

        const std::array<gpu::BindGroupEntry, 6> rangeEntries = {
            gpu::BindGroupEntry(6).buffer(telemetry_),
            gpu::BindGroupEntry(8).buffer(sortedGridEntries_),
            gpu::BindGroupEntry(9).buffer(cellRanges_),
            gpu::BindGroupEntry(21).buffer(rangePredicates_),
            gpu::BindGroupEntry(22).buffer(entryRangeIndices_),
            gpu::BindGroupEntry(7).buffer(parameterBuffer_),
        };
        const std::array<gpu::BindGroupEntry, 9> finalizeEntries = {
            gpu::BindGroupEntry(3).buffer(bodyEntryCounts_),
            gpu::BindGroupEntry(4).buffer(bodyEntryOffsets_),
            gpu::BindGroupEntry(6).buffer(telemetry_),
            gpu::BindGroupEntry(9).buffer(cellRanges_),
            gpu::BindGroupEntry(10).buffer(oversizedFlags_),
            gpu::BindGroupEntry(21).buffer(rangePredicates_),
            gpu::BindGroupEntry(22).buffer(entryRangeIndices_),
            gpu::BindGroupEntry(23).buffer(dispatchArgs_),
            gpu::BindGroupEntry(7).buffer(parameterBuffer_),
        };
        WGPUBindGroup rangeGroup = bindGroup(
            rangeLayout_, rangeEntries, "broad_phase_range_bind_group");
        WGPUBindGroup finalizeGroup = bindGroup(
            finalizeLayout_, finalizeEntries, "broad_phase_finalize_bind_group");
        if (!rangeGroup || !finalizeGroup) {
            if (rangeGroup) wgpuBindGroupRelease(rangeGroup);
            if (finalizeGroup) wgpuBindGroupRelease(finalizeGroup);
            return false;
        }
        pass = wgpuCommandEncoderBeginComputePass(encoder, &passDesc);
        wgpuComputePassEncoderSetBindGroup(pass, 0, finalizeGroup, 0, nullptr);
        wgpuComputePassEncoderSetPipeline(pass, finalizeEntryCountPipeline_);
        wgpuComputePassEncoderDispatchWorkgroups(pass, 1, 1, 1);
        wgpuComputePassEncoderEnd(pass);
        wgpuComputePassEncoderRelease(pass);

        if (!primitives_.encodeRadixSort(
                encoder, gridEntries_, sortedGridEntries_, entryCapacity_, 2u,
                8u, dispatchArgs_, 0u, 3u * sizeof(uint32_t), telemetry_, 0u)) {
            wgpuBindGroupRelease(rangeGroup);
            wgpuBindGroupRelease(finalizeGroup);
            return false;
        }

        pass = wgpuCommandEncoderBeginComputePass(encoder, &passDesc);
        wgpuComputePassEncoderSetBindGroup(pass, 0, rangeGroup, 0, nullptr);
        wgpuComputePassEncoderSetPipeline(pass, markRangeStartsPipeline_);
        wgpuComputePassEncoderDispatchWorkgroupsIndirect(
            pass, dispatchArgs_, 0u);
        wgpuComputePassEncoderEnd(pass);
        wgpuComputePassEncoderRelease(pass);

        if (!primitives_.encodeScanU32(
                encoder, rangePredicates_, entryRangeIndices_,
                entryCapacity_, 2u, dispatchArgs_, 0u,
                3u * sizeof(uint32_t), telemetry_, 0u)) {
            wgpuBindGroupRelease(rangeGroup);
            wgpuBindGroupRelease(finalizeGroup);
            return false;
        }

        pass = wgpuCommandEncoderBeginComputePass(encoder, &passDesc);
        wgpuComputePassEncoderSetBindGroup(pass, 0, finalizeGroup, 0, nullptr);
        wgpuComputePassEncoderSetPipeline(pass, finalizeRangeCountPipeline_);
        wgpuComputePassEncoderDispatchWorkgroups(pass, 1, 1, 1);
        wgpuComputePassEncoderEnd(pass);
        wgpuComputePassEncoderRelease(pass);

        pass = wgpuCommandEncoderBeginComputePass(encoder, &passDesc);
        wgpuComputePassEncoderSetBindGroup(pass, 0, rangeGroup, 0, nullptr);
        wgpuComputePassEncoderSetPipeline(pass, scatterRangeStartsPipeline_);
        wgpuComputePassEncoderDispatchWorkgroupsIndirect(
            pass, dispatchArgs_, 0u);
        wgpuComputePassEncoderSetPipeline(pass, scatterRangeEndsPipeline_);
        wgpuComputePassEncoderDispatchWorkgroupsIndirect(
            pass, dispatchArgs_, 0u);
        wgpuComputePassEncoderEnd(pass);
        wgpuComputePassEncoderRelease(pass);
        wgpuBindGroupRelease(rangeGroup);
        wgpuBindGroupRelease(finalizeGroup);

        const std::array<gpu::BindGroupEntry, 7> pairCountEntries = {
            gpu::BindGroupEntry(0).buffer(bodyView_.poseBuffer),
            gpu::BindGroupEntry(1).buffer(bodyView_.shapeBuffer),
            gpu::BindGroupEntry(2).buffer(bodyView_.metadataBuffer),
            gpu::BindGroupEntry(8).buffer(sortedGridEntries_),
            gpu::BindGroupEntry(9).buffer(cellRanges_),
            gpu::BindGroupEntry(11).buffer(ownerPairCounts_),
            gpu::BindGroupEntry(7).buffer(parameterBuffer_),
        };
        WGPUBindGroup pairCountGroup = bindGroup(
            pairCountLayout_, pairCountEntries,
            "broad_phase_pair_count_bind_group");
        if (!pairCountGroup) return false;
        pass = wgpuCommandEncoderBeginComputePass(encoder, &passDesc);
        wgpuComputePassEncoderSetBindGroup(pass, 0, pairCountGroup, 0, nullptr);
        wgpuComputePassEncoderSetPipeline(pass, countPairsPipeline_);
        wgpuComputePassEncoderDispatchWorkgroupsIndirect(
            pass, dispatchArgs_, 6u * sizeof(uint32_t));
        wgpuComputePassEncoderEnd(pass);
        wgpuComputePassEncoderRelease(pass);
        wgpuBindGroupRelease(pairCountGroup);

        if (!primitives_.encodeScanU32(
                encoder, ownerPairCounts_, ownerPairOffsets_, ownerCount, 1u,
                dispatchArgs_, 6u * sizeof(uint32_t),
                9u * sizeof(uint32_t), oversizedFlags_,
                bodyCount + 1u))
            return false;

        const std::array<gpu::BindGroupEntry, 7> clearEntries = {
            gpu::BindGroupEntry(13).buffer(pairCandidates_),
            gpu::BindGroupEntry(11).buffer(ownerPairCounts_),
            gpu::BindGroupEntry(12).buffer(ownerPairOffsets_),
            gpu::BindGroupEntry(6).buffer(telemetry_),
            gpu::BindGroupEntry(10).buffer(oversizedFlags_),
            gpu::BindGroupEntry(23).buffer(dispatchArgs_),
            gpu::BindGroupEntry(7).buffer(parameterBuffer_),
        };
        WGPUBindGroup clearGroup = bindGroup(
            candidateClearLayout_, clearEntries,
            "broad_phase_candidate_clear_bind_group");
        const std::array<gpu::BindGroupEntry, 9> scatterEntries = {
            gpu::BindGroupEntry(0).buffer(bodyView_.poseBuffer),
            gpu::BindGroupEntry(1).buffer(bodyView_.shapeBuffer),
            gpu::BindGroupEntry(2).buffer(bodyView_.metadataBuffer),
            gpu::BindGroupEntry(8).buffer(sortedGridEntries_),
            gpu::BindGroupEntry(9).buffer(cellRanges_),
            gpu::BindGroupEntry(11).buffer(ownerPairCounts_),
            gpu::BindGroupEntry(12).buffer(ownerPairOffsets_),
            gpu::BindGroupEntry(13).buffer(pairCandidates_),
            gpu::BindGroupEntry(7).buffer(parameterBuffer_),
        };
        WGPUBindGroup scatterGroup = bindGroup(
            pairScatterLayout_, scatterEntries,
            "broad_phase_pair_scatter_bind_group");
        if (!clearGroup || !scatterGroup) {
            if (clearGroup) wgpuBindGroupRelease(clearGroup);
            if (scatterGroup) wgpuBindGroupRelease(scatterGroup);
            return false;
        }
        pass = wgpuCommandEncoderBeginComputePass(encoder, &passDesc);
        wgpuComputePassEncoderSetBindGroup(pass, 0, clearGroup, 0, nullptr);
        wgpuComputePassEncoderSetPipeline(pass, clearCandidatesPipeline_);
        // Pair owners fill a contiguous output range. The finalize kernel only
        // publishes its dynamic length; clearing the full reserved capacity
        // would make sparse worlds pay for all 262k candidate slots.
        wgpuComputePassEncoderDispatchWorkgroups(pass, 1, 1, 1);
        wgpuComputePassEncoderEnd(pass);
        wgpuComputePassEncoderRelease(pass);

        pass = wgpuCommandEncoderBeginComputePass(encoder, &passDesc);
        wgpuComputePassEncoderSetBindGroup(pass, 0, scatterGroup, 0, nullptr);
        wgpuComputePassEncoderSetPipeline(pass, scatterPairsPipeline_);
        wgpuComputePassEncoderDispatchWorkgroupsIndirect(
            pass, dispatchArgs_, 6u * sizeof(uint32_t));
        wgpuComputePassEncoderEnd(pass);
        wgpuComputePassEncoderRelease(pass);
        wgpuBindGroupRelease(clearGroup);
        wgpuBindGroupRelease(scatterGroup);

        if (!primitives_.encodeRadixSort(
                encoder, pairCandidates_, sortedPairCandidates_,
                config_.candidatePairCapacity, 2u, 24u, dispatchArgs_,
                12u * sizeof(uint32_t), 15u * sizeof(uint32_t), telemetry_,
                2u)) return false;

        const std::array<gpu::BindGroupEntry, 6> uniqueEntries = {
            gpu::BindGroupEntry(14).buffer(sortedPairCandidates_),
            gpu::BindGroupEntry(15).buffer(uniquePairs_),
            gpu::BindGroupEntry(11).buffer(ownerPairCounts_),
            gpu::BindGroupEntry(12).buffer(ownerPairOffsets_),
            gpu::BindGroupEntry(6).buffer(telemetry_),
            gpu::BindGroupEntry(7).buffer(parameterBuffer_),
        };
        WGPUBindGroup uniqueGroup = bindGroup(
            uniqueLayout_, uniqueEntries, "broad_phase_unique_bind_group");
        if (!uniqueGroup) return false;
        pass = wgpuCommandEncoderBeginComputePass(encoder, &passDesc);
        wgpuComputePassEncoderSetBindGroup(pass, 0, uniqueGroup, 0, nullptr);
        wgpuComputePassEncoderSetPipeline(pass, uniquePairsPipeline_);
        wgpuComputePassEncoderDispatchWorkgroupsIndirect(
            pass, dispatchArgs_, 12u * sizeof(uint32_t));
        wgpuComputePassEncoderEnd(pass);
        wgpuComputePassEncoderRelease(pass);
        wgpuBindGroupRelease(uniqueGroup);

        WGPUBuffer previous = contactsAreB_ ? contactsB_ : contactsA_;
        WGPUBuffer next = contactsAreB_ ? contactsA_ : contactsB_;
        const std::array<gpu::BindGroupEntry, 9> lifecyclePrepareEntries = {
            gpu::BindGroupEntry(6).buffer(telemetry_),
            gpu::BindGroupEntry(15).buffer(uniquePairs_),
            gpu::BindGroupEntry(16).buffer(previous),
            gpu::BindGroupEntry(17).buffer(next),
            gpu::BindGroupEntry(18).buffer(contactOccupancy_),
            gpu::BindGroupEntry(20).buffer(lifecycleState_),
            gpu::BindGroupEntry(24).buffer(lifecycleNewPredicates_),
            gpu::BindGroupEntry(26).buffer(lifecycleEndPredicates_),
            gpu::BindGroupEntry(7).buffer(parameterBuffer_),
        };
        const std::array<gpu::BindGroupEntry, 6> lifecycleFreeEntries = {
            gpu::BindGroupEntry(18).buffer(contactOccupancy_),
            gpu::BindGroupEntry(20).buffer(lifecycleState_),
            gpu::BindGroupEntry(28).buffer(lifecycleFreePredicates_),
            gpu::BindGroupEntry(29).buffer(lifecycleFreeOffsets_),
            gpu::BindGroupEntry(30).buffer(lifecycleFreeIds_),
            gpu::BindGroupEntry(7).buffer(parameterBuffer_),
        };
        const std::array<gpu::BindGroupEntry, 8> lifecycleBeginEntries = {
            gpu::BindGroupEntry(17).buffer(next),
            gpu::BindGroupEntry(18).buffer(contactOccupancy_),
            gpu::BindGroupEntry(19).buffer(contactEvents_),
            gpu::BindGroupEntry(20).buffer(lifecycleState_),
            gpu::BindGroupEntry(24).buffer(lifecycleNewPredicates_),
            gpu::BindGroupEntry(25).buffer(lifecycleNewOffsets_),
            gpu::BindGroupEntry(30).buffer(lifecycleFreeIds_),
            gpu::BindGroupEntry(7).buffer(parameterBuffer_),
        };
        const std::array<gpu::BindGroupEntry, 6> lifecycleEndEntries = {
            gpu::BindGroupEntry(16).buffer(previous),
            gpu::BindGroupEntry(19).buffer(contactEvents_),
            gpu::BindGroupEntry(20).buffer(lifecycleState_),
            gpu::BindGroupEntry(26).buffer(lifecycleEndPredicates_),
            gpu::BindGroupEntry(27).buffer(lifecycleEndOffsets_),
            gpu::BindGroupEntry(7).buffer(parameterBuffer_),
        };
        const std::array<gpu::BindGroupEntry, 9> lifecycleFinalizeEntries = {
            gpu::BindGroupEntry(6).buffer(telemetry_),
            gpu::BindGroupEntry(20).buffer(lifecycleState_),
            gpu::BindGroupEntry(24).buffer(lifecycleNewPredicates_),
            gpu::BindGroupEntry(25).buffer(lifecycleNewOffsets_),
            gpu::BindGroupEntry(26).buffer(lifecycleEndPredicates_),
            gpu::BindGroupEntry(27).buffer(lifecycleEndOffsets_),
            gpu::BindGroupEntry(28).buffer(lifecycleFreePredicates_),
            gpu::BindGroupEntry(29).buffer(lifecycleFreeOffsets_),
            gpu::BindGroupEntry(7).buffer(parameterBuffer_),
        };
        const std::array<WGPUBindGroup, 5> lifecycleGroups = {
            bindGroup(lifecyclePrepareLayout_, lifecyclePrepareEntries,
                      "broad_phase_lifecycle_prepare_group"),
            bindGroup(lifecycleFreeLayout_, lifecycleFreeEntries,
                      "broad_phase_lifecycle_free_group"),
            bindGroup(lifecycleBeginLayout_, lifecycleBeginEntries,
                      "broad_phase_lifecycle_begin_group"),
            bindGroup(lifecycleEndLayout_, lifecycleEndEntries,
                      "broad_phase_lifecycle_end_group"),
            bindGroup(lifecycleFinalizeLayout_, lifecycleFinalizeEntries,
                      "broad_phase_lifecycle_finalize_group"),
        };
        const auto releaseLifecycleGroups = [&] {
            for (WGPUBindGroup group : lifecycleGroups) {
                if (group) wgpuBindGroupRelease(group);
            }
        };
        if (std::ranges::any_of(lifecycleGroups,
                                [](WGPUBindGroup group) { return !group; })) {
            releaseLifecycleGroups();
            return false;
        }
        const uint32_t lifecycleWorkgroups =
            (config_.contactCapacity + config_.workgroupSize - 1u)
            / config_.workgroupSize;
        pass = wgpuCommandEncoderBeginComputePass(encoder, &passDesc);
        wgpuComputePassEncoderSetBindGroup(
            pass, 0, lifecycleGroups[0], 0, nullptr);
        wgpuComputePassEncoderSetPipeline(pass, lifecycleResetPipeline_);
        wgpuComputePassEncoderDispatchWorkgroups(
            pass, lifecycleWorkgroups, 1, 1);
        wgpuComputePassEncoderSetPipeline(pass, lifecyclePreparePipeline_);
        wgpuComputePassEncoderDispatchWorkgroups(
            pass, lifecycleWorkgroups, 1, 1);
        wgpuComputePassEncoderEnd(pass);
        wgpuComputePassEncoderRelease(pass);

        if (!primitives_.encodeScanU32(
                encoder, lifecycleNewPredicates_, lifecycleNewOffsets_,
                config_.contactCapacity, 3u)
            || !primitives_.encodeScanU32(
                encoder, lifecycleEndPredicates_, lifecycleEndOffsets_,
                config_.contactCapacity, 4u)) {
            releaseLifecycleGroups();
            return false;
        }

        pass = wgpuCommandEncoderBeginComputePass(encoder, &passDesc);
        wgpuComputePassEncoderSetBindGroup(
            pass, 0, lifecycleGroups[1], 0, nullptr);
        wgpuComputePassEncoderSetPipeline(pass, lifecycleMarkFreePipeline_);
        wgpuComputePassEncoderDispatchWorkgroups(
            pass, lifecycleWorkgroups, 1, 1);
        wgpuComputePassEncoderEnd(pass);
        wgpuComputePassEncoderRelease(pass);
        if (!primitives_.encodeScanU32(
                encoder, lifecycleFreePredicates_, lifecycleFreeOffsets_,
                config_.contactCapacity, 5u)) {
            releaseLifecycleGroups();
            return false;
        }

        pass = wgpuCommandEncoderBeginComputePass(encoder, &passDesc);
        wgpuComputePassEncoderSetBindGroup(
            pass, 0, lifecycleGroups[1], 0, nullptr);
        wgpuComputePassEncoderSetPipeline(
            pass, lifecycleScatterFreePipeline_);
        wgpuComputePassEncoderDispatchWorkgroups(
            pass, lifecycleWorkgroups, 1, 1);
        wgpuComputePassEncoderSetBindGroup(
            pass, 0, lifecycleGroups[2], 0, nullptr);
        wgpuComputePassEncoderSetPipeline(
            pass, lifecycleAssignBeginPipeline_);
        wgpuComputePassEncoderDispatchWorkgroups(
            pass, lifecycleWorkgroups, 1, 1);
        wgpuComputePassEncoderSetBindGroup(
            pass, 0, lifecycleGroups[3], 0, nullptr);
        wgpuComputePassEncoderSetPipeline(
            pass, lifecycleScatterEndPipeline_);
        wgpuComputePassEncoderDispatchWorkgroups(
            pass, lifecycleWorkgroups, 1, 1);
        wgpuComputePassEncoderSetBindGroup(
            pass, 0, lifecycleGroups[4], 0, nullptr);
        wgpuComputePassEncoderSetPipeline(pass, lifecycleFinalizePipeline_);
        wgpuComputePassEncoderDispatchWorkgroups(pass, 1, 1, 1);
        wgpuComputePassEncoderEnd(pass);
        wgpuComputePassEncoderRelease(pass);
        releaseLifecycleGroups();
        contactsAreB_ = !contactsAreB_;
        return true;
    }

    void shutdown() {
        releaseHandle(resetPipeline_, wgpuComputePipelineRelease);
        releaseHandle(clearEntriesPipeline_, wgpuComputePipelineRelease);
        releaseHandle(countEntriesPipeline_, wgpuComputePipelineRelease);
        releaseHandle(scatterEntriesPipeline_, wgpuComputePipelineRelease);
        releaseHandle(finalizeEntryCountPipeline_, wgpuComputePipelineRelease);
        releaseHandle(markRangeStartsPipeline_, wgpuComputePipelineRelease);
        releaseHandle(finalizeRangeCountPipeline_, wgpuComputePipelineRelease);
        releaseHandle(scatterRangeStartsPipeline_, wgpuComputePipelineRelease);
        releaseHandle(scatterRangeEndsPipeline_, wgpuComputePipelineRelease);
        releaseHandle(countPairsPipeline_, wgpuComputePipelineRelease);
        releaseHandle(clearCandidatesPipeline_, wgpuComputePipelineRelease);
        releaseHandle(scatterPairsPipeline_, wgpuComputePipelineRelease);
        releaseHandle(uniquePairsPipeline_, wgpuComputePipelineRelease);
        for (WGPUComputePipeline* pipeline : {
                 &lifecycleResetPipeline_, &lifecyclePreparePipeline_,
                 &lifecycleMarkFreePipeline_, &lifecycleScatterFreePipeline_,
                 &lifecycleAssignBeginPipeline_, &lifecycleScatterEndPipeline_,
                 &lifecycleFinalizePipeline_}) {
            releaseHandle(*pipeline, wgpuComputePipelineRelease);
        }
        releaseHandle(gridPipelineLayout_, wgpuPipelineLayoutRelease);
        releaseHandle(rangePipelineLayout_, wgpuPipelineLayoutRelease);
        releaseHandle(finalizePipelineLayout_, wgpuPipelineLayoutRelease);
        releaseHandle(pairCountPipelineLayout_, wgpuPipelineLayoutRelease);
        releaseHandle(pairScatterPipelineLayout_, wgpuPipelineLayoutRelease);
        releaseHandle(candidateClearPipelineLayout_, wgpuPipelineLayoutRelease);
        releaseHandle(uniquePipelineLayout_, wgpuPipelineLayoutRelease);
        for (WGPUPipelineLayout* layout : {
                 &lifecyclePreparePipelineLayout_,
                 &lifecycleFreePipelineLayout_,
                 &lifecycleBeginPipelineLayout_,
                 &lifecycleEndPipelineLayout_,
                 &lifecycleFinalizePipelineLayout_}) {
            releaseHandle(*layout, wgpuPipelineLayoutRelease);
        }
        releaseHandle(gridLayout_, wgpuBindGroupLayoutRelease);
        releaseHandle(rangeLayout_, wgpuBindGroupLayoutRelease);
        releaseHandle(finalizeLayout_, wgpuBindGroupLayoutRelease);
        releaseHandle(pairCountLayout_, wgpuBindGroupLayoutRelease);
        releaseHandle(pairScatterLayout_, wgpuBindGroupLayoutRelease);
        releaseHandle(candidateClearLayout_, wgpuBindGroupLayoutRelease);
        releaseHandle(uniqueLayout_, wgpuBindGroupLayoutRelease);
        for (WGPUBindGroupLayout* layout : {
                 &lifecyclePrepareLayout_, &lifecycleFreeLayout_,
                 &lifecycleBeginLayout_, &lifecycleEndLayout_,
                 &lifecycleFinalizeLayout_}) {
            releaseHandle(*layout, wgpuBindGroupLayoutRelease);
        }
        releaseHandle(shaderModule_, wgpuShaderModuleRelease);
        primitives_.shutdown();
        for (WGPUBuffer* buffer : {
                 &parameterBuffer_, &bodyEntryCounts_, &bodyEntryOffsets_,
                 &gridEntries_, &sortedGridEntries_, &cellRanges_,
                 &rangePredicates_, &entryRangeIndices_,
                 &oversizedFlags_, &dispatchArgs_, &ownerPairCounts_,
                 &ownerPairOffsets_,
                 &pairCandidates_, &sortedPairCandidates_, &uniquePairs_,
                 &contactsA_, &contactsB_, &contactOccupancy_, &contactEvents_,
                 &lifecycleState_, &lifecycleNewPredicates_,
                 &lifecycleNewOffsets_, &lifecycleEndPredicates_,
                 &lifecycleEndOffsets_, &lifecycleFreePredicates_,
                 &lifecycleFreeOffsets_, &lifecycleFreeIds_, &telemetry_}) {
            releaseBuffer(*buffer);
        }
        device_ = nullptr;
        queue_ = nullptr;
        bodyView_ = {};
        config_ = {};
        entryCapacity_ = 0;
        ownerCapacity_ = 0;
        scratchBytes_ = 0;
        contactsAreB_ = false;
    }

    WGPUDevice device_ = nullptr;
    WGPUQueue queue_ = nullptr;
    Config config_{};
    BroadPhaseBodyView bodyView_{};
    uint32_t entryCapacity_ = 0;
    uint32_t ownerCapacity_ = 0;
    size_t scratchBytes_ = 0;
    bool contactsAreB_ = false;
    DeterministicGpuPrimitives primitives_;

    WGPUBuffer parameterBuffer_ = nullptr;
    WGPUBuffer bodyEntryCounts_ = nullptr;
    WGPUBuffer bodyEntryOffsets_ = nullptr;
    WGPUBuffer gridEntries_ = nullptr;
    WGPUBuffer sortedGridEntries_ = nullptr;
    WGPUBuffer cellRanges_ = nullptr;
    WGPUBuffer rangePredicates_ = nullptr;
    WGPUBuffer entryRangeIndices_ = nullptr;
    WGPUBuffer oversizedFlags_ = nullptr;
    WGPUBuffer dispatchArgs_ = nullptr;
    WGPUBuffer ownerPairCounts_ = nullptr;
    WGPUBuffer ownerPairOffsets_ = nullptr;
    WGPUBuffer pairCandidates_ = nullptr;
    WGPUBuffer sortedPairCandidates_ = nullptr;
    WGPUBuffer uniquePairs_ = nullptr;
    WGPUBuffer contactsA_ = nullptr;
    WGPUBuffer contactsB_ = nullptr;
    WGPUBuffer contactOccupancy_ = nullptr;
    WGPUBuffer contactEvents_ = nullptr;
    WGPUBuffer lifecycleState_ = nullptr;
    WGPUBuffer lifecycleNewPredicates_ = nullptr;
    WGPUBuffer lifecycleNewOffsets_ = nullptr;
    WGPUBuffer lifecycleEndPredicates_ = nullptr;
    WGPUBuffer lifecycleEndOffsets_ = nullptr;
    WGPUBuffer lifecycleFreePredicates_ = nullptr;
    WGPUBuffer lifecycleFreeOffsets_ = nullptr;
    WGPUBuffer lifecycleFreeIds_ = nullptr;
    WGPUBuffer telemetry_ = nullptr;
    WGPUShaderModule shaderModule_ = nullptr;
    WGPUBindGroupLayout gridLayout_ = nullptr;
    WGPUBindGroupLayout rangeLayout_ = nullptr;
    WGPUBindGroupLayout finalizeLayout_ = nullptr;
    WGPUBindGroupLayout pairCountLayout_ = nullptr;
    WGPUBindGroupLayout pairScatterLayout_ = nullptr;
    WGPUBindGroupLayout candidateClearLayout_ = nullptr;
    WGPUBindGroupLayout uniqueLayout_ = nullptr;
    WGPUBindGroupLayout lifecyclePrepareLayout_ = nullptr;
    WGPUBindGroupLayout lifecycleFreeLayout_ = nullptr;
    WGPUBindGroupLayout lifecycleBeginLayout_ = nullptr;
    WGPUBindGroupLayout lifecycleEndLayout_ = nullptr;
    WGPUBindGroupLayout lifecycleFinalizeLayout_ = nullptr;
    WGPUPipelineLayout gridPipelineLayout_ = nullptr;
    WGPUPipelineLayout rangePipelineLayout_ = nullptr;
    WGPUPipelineLayout finalizePipelineLayout_ = nullptr;
    WGPUPipelineLayout pairCountPipelineLayout_ = nullptr;
    WGPUPipelineLayout pairScatterPipelineLayout_ = nullptr;
    WGPUPipelineLayout candidateClearPipelineLayout_ = nullptr;
    WGPUPipelineLayout uniquePipelineLayout_ = nullptr;
    WGPUPipelineLayout lifecyclePreparePipelineLayout_ = nullptr;
    WGPUPipelineLayout lifecycleFreePipelineLayout_ = nullptr;
    WGPUPipelineLayout lifecycleBeginPipelineLayout_ = nullptr;
    WGPUPipelineLayout lifecycleEndPipelineLayout_ = nullptr;
    WGPUPipelineLayout lifecycleFinalizePipelineLayout_ = nullptr;
    WGPUComputePipeline resetPipeline_ = nullptr;
    WGPUComputePipeline clearEntriesPipeline_ = nullptr;
    WGPUComputePipeline countEntriesPipeline_ = nullptr;
    WGPUComputePipeline scatterEntriesPipeline_ = nullptr;
    WGPUComputePipeline finalizeEntryCountPipeline_ = nullptr;
    WGPUComputePipeline markRangeStartsPipeline_ = nullptr;
    WGPUComputePipeline finalizeRangeCountPipeline_ = nullptr;
    WGPUComputePipeline scatterRangeStartsPipeline_ = nullptr;
    WGPUComputePipeline scatterRangeEndsPipeline_ = nullptr;
    WGPUComputePipeline countPairsPipeline_ = nullptr;
    WGPUComputePipeline clearCandidatesPipeline_ = nullptr;
    WGPUComputePipeline scatterPairsPipeline_ = nullptr;
    WGPUComputePipeline uniquePairsPipeline_ = nullptr;
    WGPUComputePipeline lifecycleResetPipeline_ = nullptr;
    WGPUComputePipeline lifecyclePreparePipeline_ = nullptr;
    WGPUComputePipeline lifecycleMarkFreePipeline_ = nullptr;
    WGPUComputePipeline lifecycleScatterFreePipeline_ = nullptr;
    WGPUComputePipeline lifecycleAssignBeginPipeline_ = nullptr;
    WGPUComputePipeline lifecycleScatterEndPipeline_ = nullptr;
    WGPUComputePipeline lifecycleFinalizePipeline_ = nullptr;
};

GpuBroadPhase::GpuBroadPhase() : impl_(std::make_unique<Impl>()) {}
GpuBroadPhase::~GpuBroadPhase() = default;
GpuBroadPhase::GpuBroadPhase(GpuBroadPhase&&) noexcept = default;
GpuBroadPhase& GpuBroadPhase::operator=(GpuBroadPhase&&) noexcept = default;

bool GpuBroadPhase::initialize(WGPUDevice device, WGPUQueue queue,
                               const Config& config) {
    return impl_->initialize(device, queue, config);
}
void GpuBroadPhase::shutdown() { impl_->shutdown(); }
void GpuBroadPhase::setBodyView(const BroadPhaseBodyView& view) {
    impl_->setBodyView(view);
}
bool GpuBroadPhase::encode(WGPUCommandEncoder encoder) {
    return impl_->encode(encoder);
}
WGPUBuffer GpuBroadPhase::sortedGridEntries() const noexcept {
    return impl_->sortedGridEntries_;
}
WGPUBuffer GpuBroadPhase::occupiedCellRanges() const noexcept {
    return impl_->cellRanges_;
}
WGPUBuffer GpuBroadPhase::uniquePairs() const noexcept { return impl_->uniquePairs_; }
WGPUBuffer GpuBroadPhase::persistentContacts() const noexcept {
    return impl_->contactsAreB_ ? impl_->contactsB_ : impl_->contactsA_;
}
WGPUBuffer GpuBroadPhase::contactEvents() const noexcept {
    return impl_->contactEvents_;
}
WGPUBuffer GpuBroadPhase::telemetryBuffer() const noexcept {
    return impl_->telemetry_;
}
uint32_t GpuBroadPhase::gridEntryCapacity() const noexcept {
    return impl_->entryCapacity_;
}
uint32_t GpuBroadPhase::candidatePairCapacity() const noexcept {
    return impl_->config_.candidatePairCapacity;
}
uint32_t GpuBroadPhase::pairCapacity() const noexcept {
    return impl_->config_.pairCapacity;
}
uint32_t GpuBroadPhase::contactCapacity() const noexcept {
    return impl_->config_.contactCapacity;
}
size_t GpuBroadPhase::scratchBytes() const noexcept { return impl_->scratchBytes_; }

GpuBroadPhaseTelemetry GpuBroadPhase::decodeTelemetry(
    std::span<const uint32_t> words) noexcept {
    if (words.size() < 21) return {};
    return {
        .gridEntries = words[0],
        .occupiedCells = words[1],
        .candidatePairs = words[2],
        .uniquePairs = words[3],
        .activeSleepingPairs = words[4],
        .oversizedBodies = words[5],
        .persistentContacts = words[6],
        .beginEvents = words[7],
        .endEvents = words[8],
        .candidateOverflow = words[9] != 0,
        .pairOverflow = words[10] != 0,
        .contactOverflow = words[11] != 0,
        .eventOverflow = words[12] != 0,
        .highGridEntries = words[14],
        .highOccupiedCells = words[15],
        .highCandidatePairs = words[16],
        .highUniquePairs = words[17],
        .highContacts = words[18],
        .highEvents = words[20],
        .tick = words[19],
    };
}

} // namespace voxy::physics
