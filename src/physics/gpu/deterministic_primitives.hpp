#pragma once

#include "gpu/shader_source.hpp"
#include "gpu/webgpu_compat.hpp"

#include <array>
#include <cstddef>
#include <compare>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string_view>
#include <vector>

namespace voxy::gpu {
class BindGroupEntry;
}

namespace voxy::physics {

// Sort records retain an explicit payload and source ordinal. Equal keys are
// stable, so callers can use either field without relying on invocation order.
struct alignas(16) GpuKeyValue {
    uint32_t keyLow = 0;
    uint32_t keyHigh = 0;
    uint32_t value = 0;
    uint32_t ordinal = 0;

    [[nodiscard]] constexpr auto operator<=>(const GpuKeyValue&) const noexcept = default;
};

enum class GpuMergeTag : uint32_t {
    CurrentOnly = 1,
    PreviousOnly = 2,
    Matched = 3,
};

class DeterministicGpuPrimitives {
public:
    struct Config {
        uint32_t capacity = 1'048'576;
        uint32_t workgroupSize = 256;
        std::filesystem::path shaderPath =
            "shaders/physics_deterministic_primitives.wgsl";
        std::span<const gpu::ShaderSource> shaderSources{};
    };

    DeterministicGpuPrimitives() = default;
    ~DeterministicGpuPrimitives();

    DeterministicGpuPrimitives(const DeterministicGpuPrimitives&) = delete;
    DeterministicGpuPrimitives& operator=(const DeterministicGpuPrimitives&) = delete;
    DeterministicGpuPrimitives(DeterministicGpuPrimitives&&) = delete;
    DeterministicGpuPrimitives& operator=(
        DeterministicGpuPrimitives&&) = delete;

    [[nodiscard]] bool initialize(WGPUDevice device, WGPUQueue queue,
                                  const Config& config);
    void shutdown();

    [[nodiscard]] bool encodeScanU32(WGPUCommandEncoder encoder,
                                     WGPUBuffer input,
                                     WGPUBuffer exclusiveOutput,
                                     uint32_t count,
                                     uint32_t parameterSlot = 0,
                                     WGPUBuffer indirectDispatchBuffer = nullptr,
                                     uint64_t indirectWorkOffset = 0,
                                     uint64_t indirectScalarOffset = 0,
                                     WGPUBuffer dynamicCountBuffer = nullptr,
                                     uint32_t dynamicCountWord = 0,
                                     uint32_t dynamicCountScale = 1);
    // Exclusive scan with every intermediate and output value saturated at
    // inclusiveLimit. This is used for capacity-bounded work queues: once the
    // limit is reached, later producers can be skipped without allowing a
    // u32 prefix sum to wrap back into the valid output range.
    [[nodiscard]] bool encodeScanU32Clamped(
        WGPUCommandEncoder encoder, WGPUBuffer input,
        WGPUBuffer exclusiveOutput, uint32_t count, uint32_t inclusiveLimit,
        uint32_t parameterSlot = 0,
        WGPUBuffer indirectDispatchBuffer = nullptr,
        uint64_t indirectWorkOffset = 0,
        uint64_t indirectScalarOffset = 0,
        WGPUBuffer dynamicCountBuffer = nullptr,
        uint32_t dynamicCountWord = 0,
        uint32_t dynamicCountScale = 1);
    [[nodiscard]] bool encodeStableCompactU32(WGPUCommandEncoder encoder,
                                              WGPUBuffer values,
                                              WGPUBuffer predicates,
                                              WGPUBuffer output,
                                              uint32_t count);
    [[nodiscard]] bool encodeRadixSort(WGPUCommandEncoder encoder,
                                       WGPUBuffer input,
                                       WGPUBuffer output,
                                       uint32_t count,
                                       uint32_t logicalKeyWords = 1,
                                       uint32_t parameterBaseSlot = 8,
                                       WGPUBuffer indirectDispatchBuffer = nullptr,
                                       uint64_t indirectWorkOffset = 0,
                                       uint64_t indirectScalarOffset = 0,
                                       WGPUBuffer dynamicCountBuffer = nullptr,
                                       uint32_t dynamicCountWord = 0,
                                       uint32_t dynamicCountScale = 1);
    // Stable two-word sort when every real key component is below the given
    // exclusive bound. The all-ones sentinel is also supported and remains
    // after every real key. Constant high-byte passes are omitted while the
    // exact order of the full eight-pass sort is preserved.
    [[nodiscard]] bool encodeRadixSortBoundedU32x2(
        WGPUCommandEncoder encoder, WGPUBuffer input, WGPUBuffer output,
        uint32_t count, uint32_t exclusiveKeyBound,
        uint32_t parameterBaseSlot = 8,
        WGPUBuffer indirectDispatchBuffer = nullptr,
        uint64_t indirectWorkOffset = 0,
        uint64_t indirectScalarOffset = 0,
        WGPUBuffer dynamicCountBuffer = nullptr,
        uint32_t dynamicCountWord = 0,
        uint32_t dynamicCountScale = 1);
    // Stable sort by one selected key word when every real key is below a
    // 16-bit exclusive bound. Equal keys retain their input order, and the
    // all-ones sentinel remains after every real key.
    [[nodiscard]] bool encodeRadixSortBoundedU16Word(
        WGPUCommandEncoder encoder, WGPUBuffer input, WGPUBuffer output,
        uint32_t count, uint32_t keyWord, uint32_t exclusiveKeyBound,
        uint32_t parameterBaseSlot = 8,
        WGPUBuffer indirectDispatchBuffer = nullptr,
        uint64_t indirectWorkOffset = 0,
        uint64_t indirectScalarOffset = 0,
        WGPUBuffer dynamicCountBuffer = nullptr,
        uint32_t dynamicCountWord = 0,
        uint32_t dynamicCountScale = 1);
    [[nodiscard]] bool encodeAdjacentUnique(WGPUCommandEncoder encoder,
                                            WGPUBuffer sortedInput,
                                            WGPUBuffer output,
                                            uint32_t count,
                                            uint32_t logicalKeyWords = 2);
    [[nodiscard]] bool encodeSortedMerge(WGPUCommandEncoder encoder,
                                         WGPUBuffer previous,
                                         uint32_t previousCount,
                                         WGPUBuffer current,
                                         uint32_t currentCount,
                                         WGPUBuffer output,
                                         WGPUBuffer outputTags);
    [[nodiscard]] bool encodeAssignFreeIds(WGPUCommandEncoder encoder,
                                           WGPUBuffer requestPredicates,
                                           uint32_t requestItemCount,
                                           WGPUBuffer sortedFreeIds,
                                           uint32_t freeIdCount,
                                           WGPUBuffer assignedIds);

    // Operation result words are deterministic and asynchronously copyable:
    // [0] output/request count, [1] assigned count, [2] overflow count.
    [[nodiscard]] WGPUBuffer resultBuffer() const noexcept { return resultBuffer_; }
    [[nodiscard]] uint32_t capacity() const noexcept { return capacity_; }
    [[nodiscard]] uint32_t workgroupSize() const noexcept { return workgroupSize_; }
    [[nodiscard]] size_t scratchBytes() const noexcept { return scratchBytes_; }
    [[nodiscard]] bool usesSubgroups() const noexcept { return false; }
    [[nodiscard]] size_t cachedBindGroupCount() const noexcept {
        return bindGroupCache_.size();
    }

private:
    struct Params;
    struct CachedBufferBinding {
        uint32_t binding = 0;
        WGPUBuffer buffer = nullptr;
        uint64_t offset = 0;
        uint64_t size = 0;
    };
    struct CachedBindGroup {
        WGPUBindGroupLayout layout = nullptr;
        std::array<CachedBufferBinding, 7> bindings{};
        uint32_t bindingCount = 0;
        WGPUBindGroup group = nullptr;
    };

    [[nodiscard]] bool createLayoutsAndPipelines();
    [[nodiscard]] bool encodeScanAt(WGPUCommandEncoder encoder,
                                    WGPUBuffer input,
                                    WGPUBuffer exclusiveOutput,
                                    uint32_t count,
                                    uint32_t parameterSlot,
                                    WGPUBuffer indirectDispatchBuffer = nullptr,
                                    uint64_t indirectWorkOffset = 0,
                                    uint64_t indirectScalarOffset = 0,
                                    WGPUBuffer dynamicCountBuffer = nullptr,
                                    uint32_t dynamicCountWord = 0,
                                    uint32_t dynamicCountScale = 1,
                                    uint32_t inclusiveLimit = UINT32_MAX);
    [[nodiscard]] bool encodeRadixSortImpl(
        WGPUCommandEncoder encoder, WGPUBuffer input, WGPUBuffer output,
        uint32_t count, uint32_t firstKeyWord, uint32_t logicalKeyWords,
        uint32_t significantBytesPerWord, uint32_t parameterBaseSlot,
        WGPUBuffer indirectDispatchBuffer, uint64_t indirectWorkOffset,
        uint64_t indirectScalarOffset, WGPUBuffer dynamicCountBuffer,
        uint32_t dynamicCountWord, uint32_t dynamicCountScale);
    void writeParams(uint32_t slot, const Params& params);
    [[nodiscard]] bool flushParams(uint32_t firstSlot, uint32_t slotCount);
    void prepareBindGroupCache(size_t requiredEntries);
    void releaseCachedBindGroups();
    [[nodiscard]] WGPUBindGroup cachedBindGroup(
        WGPUBindGroupLayout layout,
        std::span<const gpu::BindGroupEntry> entries,
        std::string_view label);

    WGPUDevice device_ = nullptr;
    WGPUQueue queue_ = nullptr;
    uint32_t capacity_ = 0;
    uint32_t workgroupSize_ = 0;
    uint32_t blockCapacity_ = 0;
    size_t scratchBytes_ = 0;
    std::filesystem::path shaderPath_;
    std::span<const gpu::ShaderSource> shaderSources_{};

    WGPUBuffer parameterBuffer_ = nullptr;
    WGPUBuffer blockSumsBuffer_ = nullptr;
    WGPUBuffer blockPrefixBuffer_ = nullptr;
    WGPUBuffer offsetsBuffer_ = nullptr;
    WGPUBuffer predicatesBuffer_ = nullptr;
    WGPUBuffer radixScratchBuffer_ = nullptr;
    WGPUBuffer radixHistogramBuffer_ = nullptr;
    WGPUBuffer radixOffsetsBuffer_ = nullptr;
    WGPUBuffer radixDigitBasesBuffer_ = nullptr;
    WGPUBuffer resultBuffer_ = nullptr;
    std::vector<std::byte> parameterUpload_;
    std::vector<CachedBindGroup> bindGroupCache_;

    WGPUShaderModule shaderModule_ = nullptr;
    WGPUBindGroupLayout scanLayout_ = nullptr;
    WGPUBindGroupLayout compactLayout_ = nullptr;
    WGPUBindGroupLayout radixLayout_ = nullptr;
    WGPUBindGroupLayout uniqueMarkLayout_ = nullptr;
    WGPUBindGroupLayout uniqueScatterLayout_ = nullptr;
    WGPUBindGroupLayout mergeLayout_ = nullptr;
    WGPUBindGroupLayout assignLayout_ = nullptr;
    WGPUPipelineLayout scanPipelineLayout_ = nullptr;
    WGPUPipelineLayout compactPipelineLayout_ = nullptr;
    WGPUPipelineLayout radixPipelineLayout_ = nullptr;
    WGPUPipelineLayout uniqueMarkPipelineLayout_ = nullptr;
    WGPUPipelineLayout uniqueScatterPipelineLayout_ = nullptr;
    WGPUPipelineLayout mergePipelineLayout_ = nullptr;
    WGPUPipelineLayout assignPipelineLayout_ = nullptr;
    WGPUComputePipeline scanBlocksPipeline_ = nullptr;
    WGPUComputePipeline scanPrefixPipeline_ = nullptr;
    WGPUComputePipeline scanAddPipeline_ = nullptr;
    WGPUComputePipeline compactScatterPipeline_ = nullptr;
    WGPUComputePipeline compactFinalizePipeline_ = nullptr;
    WGPUComputePipeline radixHistogramPipeline_ = nullptr;
    WGPUComputePipeline radixBlockPrefixPipeline_ = nullptr;
    WGPUComputePipeline radixPrefixPipeline_ = nullptr;
    WGPUComputePipeline radixScatterPipeline_ = nullptr;
    WGPUComputePipeline uniqueMarkPipeline_ = nullptr;
    WGPUComputePipeline uniqueScatterPipeline_ = nullptr;
    WGPUComputePipeline uniqueFinalizePipeline_ = nullptr;
    WGPUComputePipeline mergePipeline_ = nullptr;
    WGPUComputePipeline assignScatterPipeline_ = nullptr;
    WGPUComputePipeline assignFinalizePipeline_ = nullptr;
};

static_assert(sizeof(GpuKeyValue) == 16);

} // namespace voxy::physics
