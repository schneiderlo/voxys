#include "physics/gpu/deterministic_primitives.hpp"

#include "core/log.hpp"
#include "gpu/resources.hpp"

#include <array>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

namespace voxy::physics {
namespace {

constexpr uint32_t kParameterStride = 256;
constexpr uint32_t kParameterSlots = 32;
constexpr uint32_t kRadix = 256;
constexpr size_t kMaximumCachedBindGroups = 256;

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
                                 WGPUShaderModule module,
                                 const std::string& entryPoint,
                                 const char* label) {
    WGPUComputePipelineDescriptor desc{};
    WGPU_SET_LABEL(desc, label);
    desc.layout = layout;
    desc.compute.module = module;
    WGPU_SET_ENTRY_POINT(desc.compute, entryPoint.c_str());
    return wgpuDeviceCreateComputePipeline(device, &desc);
}

template <size_t N>
WGPUPipelineLayout makeLayout(
    WGPUDevice device, const std::array<WGPUBindGroupLayout, N>& layouts,
    const char* label) {
    return gpu::createPipelineLayout(device, layouts, label);
}

} // namespace

struct alignas(16) DeterministicGpuPrimitives::Params {
    std::array<uint32_t, 4> counts{};
};

DeterministicGpuPrimitives::~DeterministicGpuPrimitives() { shutdown(); }

bool DeterministicGpuPrimitives::initialize(WGPUDevice device, WGPUQueue queue,
                                            const Config& config) {
    if (device_ || !device || !queue || config.capacity == 0
        || (config.workgroupSize != 64 && config.workgroupSize != 128
            && config.workgroupSize != 256)) {
        return false;
    }
    device_ = device;
    queue_ = queue;
    capacity_ = config.capacity;
    workgroupSize_ = config.workgroupSize;
    blockCapacity_ = (capacity_ + workgroupSize_ - 1u) / workgroupSize_;
    shaderPath_ = config.shaderPath;

    parameterBuffer_ = gpu::createBuffer(
        device_, gpu::BufferDesc::uniform(
            uint64_t{kParameterSlots} * kParameterStride,
            "deterministic_primitive_params"));
    parameterUpload_.assign(
        uint64_t{kParameterSlots} * kParameterStride, std::byte{});
    blockSumsBuffer_ = gpu::createBuffer(
        device_, gpu::BufferDesc::storage(
            uint64_t{blockCapacity_} * sizeof(uint32_t), false,
            "deterministic_scan_block_sums"));
    blockPrefixBuffer_ = gpu::createBuffer(
        device_, gpu::BufferDesc::storage(
            uint64_t{blockCapacity_} * sizeof(uint32_t), false,
            "deterministic_scan_block_prefix"));
    offsetsBuffer_ = gpu::createBuffer(
        device_, gpu::BufferDesc::storage(
            uint64_t{capacity_} * sizeof(uint32_t), false,
            "deterministic_offsets"));
    predicatesBuffer_ = gpu::createBuffer(
        device_, gpu::BufferDesc::storage(
            uint64_t{capacity_} * sizeof(uint32_t), false,
            "deterministic_predicates"));
    radixScratchBuffer_ = gpu::createBuffer(
        device_, gpu::BufferDesc::storage(
            uint64_t{capacity_} * sizeof(GpuKeyValue), false,
            "deterministic_radix_scratch"));
    radixHistogramBuffer_ = gpu::createBuffer(
        device_, gpu::BufferDesc::storage(
            uint64_t{blockCapacity_} * kRadix * sizeof(uint32_t), false,
            "deterministic_radix_histograms"));
    radixOffsetsBuffer_ = gpu::createBuffer(
        device_, gpu::BufferDesc::storage(
            uint64_t{blockCapacity_} * kRadix * sizeof(uint32_t), false,
            "deterministic_radix_offsets"));
    radixDigitBasesBuffer_ = gpu::createBuffer(
        device_, gpu::BufferDesc::storage(
            kRadix * sizeof(uint32_t), false,
            "deterministic_radix_digit_bases"));
    resultBuffer_ = gpu::createBuffer(
        device_, gpu::BufferDesc::storage(
            4u * sizeof(uint32_t), false,
            "deterministic_operation_result"));
    if (!parameterBuffer_ || !blockSumsBuffer_ || !blockPrefixBuffer_
        || !offsetsBuffer_ || !predicatesBuffer_ || !radixScratchBuffer_
        || !radixHistogramBuffer_ || !radixOffsetsBuffer_
        || !radixDigitBasesBuffer_ || !resultBuffer_) {
        shutdown();
        return false;
    }
    scratchBytes_ = gpu::saturatingSize(
        uint64_t{kParameterSlots} * kParameterStride
        + uint64_t{blockCapacity_} * 2u * sizeof(uint32_t)
        + uint64_t{capacity_} * 2u * sizeof(uint32_t)
        + uint64_t{capacity_} * sizeof(GpuKeyValue)
        + uint64_t{blockCapacity_} * kRadix * 2u * sizeof(uint32_t)
        + kRadix * sizeof(uint32_t)
        + 4u * sizeof(uint32_t));

    shaderModule_ = gpu::loadShaderModule(
        device_, shaderPath_, "physics_deterministic_primitives.wgsl");
    if (!shaderModule_ || !createLayoutsAndPipelines()) {
        shutdown();
        return false;
    }
    return true;
}

bool DeterministicGpuPrimitives::createLayoutsAndPipelines() {
    using LE = gpu::BindGroupLayoutEntry;
    const auto paramsEntry = [] {
        LE entry(4);
        entry.computeVisible().uniformBuffer(true, sizeof(Params));
        return entry;
    };

    std::vector<LE> scanEntries;
    scanEntries.emplace_back(0).computeVisible().storageBuffer(true);
    for (uint32_t binding : {1u, 2u, 3u}) {
        scanEntries.emplace_back(binding).computeVisible().storageBuffer(false);
    }
    scanEntries.emplace_back(5).computeVisible().storageBuffer(true);
    scanEntries.push_back(paramsEntry());
    scanLayout_ = gpu::createBindGroupLayout(
        device_, scanEntries, "deterministic_scan_layout");

    std::vector<LE> compactEntries;
    compactEntries.emplace_back(10).computeVisible().storageBuffer(true);
    compactEntries.emplace_back(11).computeVisible().storageBuffer(true);
    compactEntries.emplace_back(12).computeVisible().storageBuffer(true);
    compactEntries.emplace_back(13).computeVisible().storageBuffer(false);
    compactEntries.emplace_back(14).computeVisible().storageBuffer(false);
    compactEntries.push_back(paramsEntry());
    compactLayout_ = gpu::createBindGroupLayout(
        device_, compactEntries, "deterministic_compact_layout");

    std::vector<LE> radixEntries;
    radixEntries.emplace_back(20).computeVisible().storageBuffer(true);
    for (uint32_t binding : {21u, 22u, 23u, 24u}) {
        radixEntries.emplace_back(binding).computeVisible().storageBuffer(false);
    }
    radixEntries.emplace_back(25).computeVisible().storageBuffer(true);
    radixEntries.push_back(paramsEntry());
    radixLayout_ = gpu::createBindGroupLayout(
        device_, radixEntries, "deterministic_radix_layout");

    std::vector<LE> uniqueMarkEntries;
    uniqueMarkEntries.emplace_back(30).computeVisible().storageBuffer(true);
    uniqueMarkEntries.emplace_back(32).computeVisible().storageBuffer(false);
    uniqueMarkEntries.push_back(paramsEntry());
    uniqueMarkLayout_ = gpu::createBindGroupLayout(
        device_, uniqueMarkEntries, "deterministic_unique_mark_layout");

    std::vector<LE> uniqueScatterEntries;
    uniqueScatterEntries.emplace_back(30).computeVisible().storageBuffer(true);
    uniqueScatterEntries.emplace_back(31).computeVisible().storageBuffer(false);
    uniqueScatterEntries.emplace_back(32).computeVisible().storageBuffer(false);
    uniqueScatterEntries.emplace_back(33).computeVisible().storageBuffer(true);
    uniqueScatterEntries.emplace_back(14).computeVisible().storageBuffer(false);
    uniqueScatterEntries.push_back(paramsEntry());
    uniqueScatterLayout_ = gpu::createBindGroupLayout(
        device_, uniqueScatterEntries, "deterministic_unique_scatter_layout");

    std::vector<LE> mergeEntries;
    mergeEntries.emplace_back(40).computeVisible().storageBuffer(true);
    mergeEntries.emplace_back(41).computeVisible().storageBuffer(true);
    mergeEntries.emplace_back(42).computeVisible().storageBuffer(false);
    mergeEntries.emplace_back(43).computeVisible().storageBuffer(false);
    mergeEntries.emplace_back(14).computeVisible().storageBuffer(false);
    mergeEntries.push_back(paramsEntry());
    mergeLayout_ = gpu::createBindGroupLayout(
        device_, mergeEntries, "deterministic_merge_layout");

    std::vector<LE> assignEntries;
    for (uint32_t binding : {50u, 51u, 52u}) {
        assignEntries.emplace_back(binding).computeVisible().storageBuffer(true);
    }
    assignEntries.emplace_back(53).computeVisible().storageBuffer(false);
    assignEntries.emplace_back(14).computeVisible().storageBuffer(false);
    assignEntries.push_back(paramsEntry());
    assignLayout_ = gpu::createBindGroupLayout(
        device_, assignEntries, "deterministic_assign_layout");

    if (!scanLayout_ || !compactLayout_ || !radixLayout_
        || !uniqueMarkLayout_ || !uniqueScatterLayout_ || !mergeLayout_
        || !assignLayout_) return false;

    scanPipelineLayout_ = makeLayout(
        device_, std::array{scanLayout_}, "deterministic_scan_pipeline_layout");
    compactPipelineLayout_ = makeLayout(
        device_, std::array{compactLayout_},
        "deterministic_compact_pipeline_layout");
    radixPipelineLayout_ = makeLayout(
        device_, std::array{radixLayout_}, "deterministic_radix_pipeline_layout");
    uniqueMarkPipelineLayout_ = makeLayout(
        device_, std::array{uniqueMarkLayout_},
        "deterministic_unique_mark_pipeline_layout");
    uniqueScatterPipelineLayout_ = makeLayout(
        device_, std::array{uniqueScatterLayout_},
        "deterministic_unique_scatter_pipeline_layout");
    mergePipelineLayout_ = makeLayout(
        device_, std::array{mergeLayout_}, "deterministic_merge_pipeline_layout");
    assignPipelineLayout_ = makeLayout(
        device_, std::array{assignLayout_}, "deterministic_assign_pipeline_layout");
    if (!scanPipelineLayout_ || !compactPipelineLayout_ || !radixPipelineLayout_
        || !uniqueMarkPipelineLayout_ || !uniqueScatterPipelineLayout_
        || !mergePipelineLayout_ || !assignPipelineLayout_) return false;

    const std::string suffix = std::to_string(workgroupSize_);
    scanBlocksPipeline_ = makePipeline(
        device_, scanPipelineLayout_, shaderModule_, "scan_blocks_" + suffix,
        "deterministic_scan_blocks");
    scanPrefixPipeline_ = makePipeline(
        device_, scanPipelineLayout_, shaderModule_, "scan_prefix",
        "deterministic_scan_prefix");
    scanAddPipeline_ = makePipeline(
        device_, scanPipelineLayout_, shaderModule_, "scan_add_" + suffix,
        "deterministic_scan_add");
    compactScatterPipeline_ = makePipeline(
        device_, compactPipelineLayout_, shaderModule_,
        "compact_scatter_" + suffix, "deterministic_compact_scatter");
    compactFinalizePipeline_ = makePipeline(
        device_, compactPipelineLayout_, shaderModule_, "compact_finalize",
        "deterministic_compact_finalize");
    radixHistogramPipeline_ = makePipeline(
        device_, radixPipelineLayout_, shaderModule_,
        "radix_histogram_" + suffix, "deterministic_radix_histogram");
    radixPrefixPipeline_ = makePipeline(
        device_, radixPipelineLayout_, shaderModule_, "radix_prefix",
        "deterministic_radix_prefix");
    radixScatterPipeline_ = makePipeline(
        device_, radixPipelineLayout_, shaderModule_,
        "radix_scatter_" + suffix, "deterministic_radix_scatter");
    uniqueMarkPipeline_ = makePipeline(
        device_, uniqueMarkPipelineLayout_, shaderModule_,
        "unique_mark_" + suffix, "deterministic_unique_mark");
    uniqueScatterPipeline_ = makePipeline(
        device_, uniqueScatterPipelineLayout_, shaderModule_,
        "unique_scatter_" + suffix, "deterministic_unique_scatter");
    uniqueFinalizePipeline_ = makePipeline(
        device_, uniqueScatterPipelineLayout_, shaderModule_, "unique_finalize",
        "deterministic_unique_finalize");
    mergePipeline_ = makePipeline(
        device_, mergePipelineLayout_, shaderModule_, "sorted_merge",
        "deterministic_sorted_merge");
    assignScatterPipeline_ = makePipeline(
        device_, assignPipelineLayout_, shaderModule_,
        "assign_scatter_" + suffix, "deterministic_assign_scatter");
    assignFinalizePipeline_ = makePipeline(
        device_, assignPipelineLayout_, shaderModule_, "assign_finalize",
        "deterministic_assign_finalize");
    return scanBlocksPipeline_ && scanPrefixPipeline_ && scanAddPipeline_
        && compactScatterPipeline_ && compactFinalizePipeline_
        && radixHistogramPipeline_ && radixPrefixPipeline_
        && radixScatterPipeline_ && uniqueMarkPipeline_
        && uniqueScatterPipeline_ && uniqueFinalizePipeline_
        && mergePipeline_ && assignScatterPipeline_
        && assignFinalizePipeline_;
}

void DeterministicGpuPrimitives::writeParams(uint32_t slot,
                                              const Params& params) {
    std::memcpy(parameterUpload_.data() + uint64_t{slot} * kParameterStride,
                &params, sizeof(params));
}

void DeterministicGpuPrimitives::flushParams(uint32_t firstSlot,
                                              uint32_t slotCount) {
    if (slotCount == 0u) return;
    const size_t offset = size_t{firstSlot} * kParameterStride;
    const size_t bytes = size_t{slotCount} * kParameterStride;
    gpu::writeBuffer(
        queue_, parameterBuffer_, offset,
        std::span<const std::byte>(parameterUpload_.data() + offset, bytes));
}

void DeterministicGpuPrimitives::prepareBindGroupCache(
    size_t requiredEntries) {
    if (requiredEntries > kMaximumCachedBindGroups
        || bindGroupCache_.size()
            > kMaximumCachedBindGroups - requiredEntries) {
        releaseCachedBindGroups();
    }
}

void DeterministicGpuPrimitives::releaseCachedBindGroups() {
    for (auto& cached : bindGroupCache_) {
        releaseHandle(cached.group, wgpuBindGroupRelease);
    }
    bindGroupCache_.clear();
}

WGPUBindGroup DeterministicGpuPrimitives::cachedBindGroup(
    WGPUBindGroupLayout layout,
    std::span<const gpu::BindGroupEntry> entries,
    std::string_view label) {
    if (!layout || entries.size() > CachedBindGroup{}.bindings.size()) {
        return nullptr;
    }
    const auto matches = [&](const CachedBindGroup& cached) {
        if (cached.layout != layout || cached.bindingCount != entries.size()) {
            return false;
        }
        for (size_t index = 0; index < entries.size(); ++index) {
            const WGPUBindGroupEntry& entry = entries[index].get();
            const CachedBufferBinding& binding = cached.bindings[index];
            if (binding.binding != entry.binding
                || binding.buffer != entry.buffer
                || binding.offset != entry.offset
                || binding.size != entry.size) {
                return false;
            }
        }
        return true;
    };
    for (const auto& cached : bindGroupCache_) {
        if (matches(cached)) return cached.group;
    }

    prepareBindGroupCache(1u);
    WGPUBindGroup group = gpu::createBindGroup(
        device_, layout, entries, label);
    if (!group) return nullptr;
    CachedBindGroup cached;
    cached.layout = layout;
    cached.bindingCount = static_cast<uint32_t>(entries.size());
    cached.group = group;
    for (size_t index = 0; index < entries.size(); ++index) {
        const WGPUBindGroupEntry& entry = entries[index].get();
        cached.bindings[index] = {
            entry.binding, entry.buffer, entry.offset, entry.size};
    }
    bindGroupCache_.push_back(cached);
    return group;
}

bool DeterministicGpuPrimitives::encodeScanAt(
    WGPUCommandEncoder encoder, WGPUBuffer input, WGPUBuffer output,
    uint32_t count, uint32_t parameterSlot,
    WGPUBuffer indirectDispatchBuffer, uint64_t indirectWorkOffset,
    uint64_t indirectScalarOffset, WGPUBuffer dynamicCountBuffer,
    uint32_t dynamicCountWord, uint32_t dynamicCountScale) {
    if (!encoder || !input || !output || count > capacity_
        || parameterSlot >= kParameterSlots
        || (dynamicCountScale != 1u && dynamicCountScale != 2u)
        || (dynamicCountBuffer && dynamicCountWord >= (1u << 24u))) {
        return false;
    }
    const uint32_t blocks = (count + workgroupSize_ - 1u) / workgroupSize_;
    const uint32_t countDescriptor = dynamicCountBuffer
        ? dynamicCountWord
            | (dynamicCountScale == 2u ? (1u << 24u) : 0u)
        : std::numeric_limits<uint32_t>::max();
    writeParams(parameterSlot, Params{{count, countDescriptor,
                                       workgroupSize_, 0u}});
    const std::array<gpu::BindGroupEntry, 6> entries = {
        gpu::BindGroupEntry(0).buffer(input),
        gpu::BindGroupEntry(1).buffer(output),
        gpu::BindGroupEntry(2).buffer(blockSumsBuffer_),
        gpu::BindGroupEntry(3).buffer(blockPrefixBuffer_),
        gpu::BindGroupEntry(5).buffer(
            dynamicCountBuffer ? dynamicCountBuffer : resultBuffer_),
        gpu::BindGroupEntry(4).buffer(parameterBuffer_, 0, sizeof(Params)),
    };
    WGPUBindGroup bindGroup = cachedBindGroup(
        scanLayout_, entries, "deterministic_scan_bind_group");
    if (!bindGroup) return false;
    const uint32_t dynamicOffset = parameterSlot * kParameterStride;
    WGPUComputePassDescriptor passDesc{};
    WGPU_SET_LABEL(passDesc, "deterministic_scan");
    WGPUComputePassEncoder pass =
        wgpuCommandEncoderBeginComputePass(encoder, &passDesc);
    wgpuComputePassEncoderSetBindGroup(pass, 0, bindGroup, 1, &dynamicOffset);
    if (blocks != 0u) {
        wgpuComputePassEncoderSetPipeline(pass, scanBlocksPipeline_);
        if (indirectDispatchBuffer) {
            wgpuComputePassEncoderDispatchWorkgroupsIndirect(
                pass, indirectDispatchBuffer, indirectWorkOffset);
        } else {
            wgpuComputePassEncoderDispatchWorkgroups(pass, blocks, 1, 1);
        }
    }
    wgpuComputePassEncoderSetPipeline(pass, scanPrefixPipeline_);
    if (indirectDispatchBuffer) {
        wgpuComputePassEncoderDispatchWorkgroupsIndirect(
            pass, indirectDispatchBuffer, indirectScalarOffset);
    } else {
        wgpuComputePassEncoderDispatchWorkgroups(pass, 1, 1, 1);
    }
    if (blocks != 0u) {
        wgpuComputePassEncoderSetPipeline(pass, scanAddPipeline_);
        if (indirectDispatchBuffer) {
            wgpuComputePassEncoderDispatchWorkgroupsIndirect(
                pass, indirectDispatchBuffer, indirectWorkOffset);
        } else {
            wgpuComputePassEncoderDispatchWorkgroups(pass, blocks, 1, 1);
        }
    }
    wgpuComputePassEncoderEnd(pass);
    wgpuComputePassEncoderRelease(pass);
    flushParams(parameterSlot, 1u);
    return true;
}

bool DeterministicGpuPrimitives::encodeScanU32(
    WGPUCommandEncoder encoder, WGPUBuffer input, WGPUBuffer output,
    uint32_t count, uint32_t parameterSlot,
    WGPUBuffer indirectDispatchBuffer, uint64_t indirectWorkOffset,
    uint64_t indirectScalarOffset, WGPUBuffer dynamicCountBuffer,
    uint32_t dynamicCountWord, uint32_t dynamicCountScale) {
    return encodeScanAt(encoder, input, output, count, parameterSlot,
                        indirectDispatchBuffer, indirectWorkOffset,
                        indirectScalarOffset, dynamicCountBuffer,
                        dynamicCountWord, dynamicCountScale);
}

bool DeterministicGpuPrimitives::encodeStableCompactU32(
    WGPUCommandEncoder encoder, WGPUBuffer values, WGPUBuffer predicates,
    WGPUBuffer output, uint32_t count) {
    if (!encodeScanAt(encoder, predicates, offsetsBuffer_, count, 5u))
        return false;
    const uint32_t blocks = (count + workgroupSize_ - 1u) / workgroupSize_;
    writeParams(1u, Params{{count, 0u, blocks, 0u}});
    const std::array<gpu::BindGroupEntry, 6> entries = {
        gpu::BindGroupEntry(10).buffer(values),
        gpu::BindGroupEntry(11).buffer(predicates),
        gpu::BindGroupEntry(12).buffer(offsetsBuffer_),
        gpu::BindGroupEntry(13).buffer(output),
        gpu::BindGroupEntry(14).buffer(resultBuffer_),
        gpu::BindGroupEntry(4).buffer(parameterBuffer_, 0, sizeof(Params)),
    };
    WGPUBindGroup bindGroup = cachedBindGroup(
        compactLayout_, entries, "deterministic_compact_bind_group");
    if (!bindGroup) return false;
    constexpr uint32_t slot = 1u;
    const uint32_t dynamicOffset = slot * kParameterStride;
    WGPUComputePassDescriptor passDesc{};
    WGPUComputePassEncoder pass =
        wgpuCommandEncoderBeginComputePass(encoder, &passDesc);
    wgpuComputePassEncoderSetBindGroup(pass, 0, bindGroup, 1, &dynamicOffset);
    if (blocks != 0u) {
        wgpuComputePassEncoderSetPipeline(pass, compactScatterPipeline_);
        wgpuComputePassEncoderDispatchWorkgroups(pass, blocks, 1, 1);
    }
    wgpuComputePassEncoderSetPipeline(pass, compactFinalizePipeline_);
    wgpuComputePassEncoderDispatchWorkgroups(pass, 1, 1, 1);
    wgpuComputePassEncoderEnd(pass);
    wgpuComputePassEncoderRelease(pass);
    flushParams(1u, 1u);
    return true;
}

bool DeterministicGpuPrimitives::encodeRadixSort(
    WGPUCommandEncoder encoder, WGPUBuffer input, WGPUBuffer output,
    uint32_t count, uint32_t logicalKeyWords, uint32_t parameterBaseSlot,
    WGPUBuffer indirectDispatchBuffer, uint64_t indirectWorkOffset,
    uint64_t indirectScalarOffset, WGPUBuffer dynamicCountBuffer,
    uint32_t dynamicCountWord, uint32_t dynamicCountScale) {
    return encodeRadixSortImpl(
        encoder, input, output, count, logicalKeyWords, 4u,
        parameterBaseSlot, indirectDispatchBuffer, indirectWorkOffset,
        indirectScalarOffset, dynamicCountBuffer, dynamicCountWord,
        dynamicCountScale);
}

bool DeterministicGpuPrimitives::encodeRadixSortBoundedU32x2(
    WGPUCommandEncoder encoder, WGPUBuffer input, WGPUBuffer output,
    uint32_t count, uint32_t exclusiveKeyBound,
    uint32_t parameterBaseSlot,
    WGPUBuffer indirectDispatchBuffer, uint64_t indirectWorkOffset,
    uint64_t indirectScalarOffset, WGPUBuffer dynamicCountBuffer,
    uint32_t dynamicCountWord, uint32_t dynamicCountScale) {
    if (exclusiveKeyBound == 0u) return false;
    const uint32_t significantBytesPerWord = exclusiveKeyBound <= 0xffffu
        ? 2u : exclusiveKeyBound <= 0x00ffffffu ? 3u : 4u;
    return encodeRadixSortImpl(
        encoder, input, output, count, 2u, significantBytesPerWord,
        parameterBaseSlot, indirectDispatchBuffer, indirectWorkOffset,
        indirectScalarOffset, dynamicCountBuffer, dynamicCountWord,
        dynamicCountScale);
}

bool DeterministicGpuPrimitives::encodeRadixSortImpl(
    WGPUCommandEncoder encoder, WGPUBuffer input, WGPUBuffer output,
    uint32_t count, uint32_t logicalKeyWords,
    uint32_t significantBytesPerWord, uint32_t parameterBaseSlot,
    WGPUBuffer indirectDispatchBuffer, uint64_t indirectWorkOffset,
    uint64_t indirectScalarOffset, WGPUBuffer dynamicCountBuffer,
    uint32_t dynamicCountWord, uint32_t dynamicCountScale) {
    if (!encoder || !input || !output || count > capacity_
        || (logicalKeyWords != 1u && logicalKeyWords != 2u)
        || significantBytesPerWord < 2u || significantBytesPerWord > 4u
        || parameterBaseSlot
            > kParameterSlots - logicalKeyWords * significantBytesPerWord
        || (dynamicCountScale != 1u && dynamicCountScale != 2u)
        || (dynamicCountBuffer && dynamicCountWord >= (1u << 24u))) {
        return false;
    }
    const uint32_t blocks = (count + workgroupSize_ - 1u) / workgroupSize_;
    const uint32_t passCount = logicalKeyWords * significantBytesPerWord;
    prepareBindGroupCache(passCount);
    std::array<WGPUBindGroup, 8> bindGroups{};
    for (uint32_t passIndex = 0; passIndex < passCount; ++passIndex) {
        WGPUBuffer passInput = passIndex == 0u
            ? input : (passIndex & 1u ? radixScratchBuffer_ : output);
        WGPUBuffer passOutput = passIndex & 1u ? output : radixScratchBuffer_;
        const uint32_t slot = parameterBaseSlot + passIndex;
        const uint32_t countDescriptor = dynamicCountBuffer
            ? dynamicCountWord
                | (dynamicCountScale == 2u ? (1u << 24u) : 0u)
            : std::numeric_limits<uint32_t>::max();
        const uint32_t keyByte =
            (passIndex / significantBytesPerWord) * 4u
            + passIndex % significantBytesPerWord;
        writeParams(slot, Params{{count, countDescriptor,
                                  workgroupSize_, keyByte}});
        const std::array<gpu::BindGroupEntry, 7> entries = {
            gpu::BindGroupEntry(20).buffer(passInput),
            gpu::BindGroupEntry(21).buffer(passOutput),
            gpu::BindGroupEntry(22).buffer(radixHistogramBuffer_),
            gpu::BindGroupEntry(23).buffer(radixOffsetsBuffer_),
            gpu::BindGroupEntry(24).buffer(radixDigitBasesBuffer_),
            gpu::BindGroupEntry(25).buffer(
                dynamicCountBuffer ? dynamicCountBuffer : resultBuffer_),
            gpu::BindGroupEntry(4).buffer(parameterBuffer_, 0, sizeof(Params)),
        };
        WGPUBindGroup bindGroup = cachedBindGroup(
            radixLayout_, entries, "deterministic_radix_bind_group");
        if (!bindGroup) return false;
        bindGroups[passIndex] = bindGroup;
    }

    WGPUComputePassDescriptor passDesc{};
    WGPUComputePassEncoder compute =
        wgpuCommandEncoderBeginComputePass(encoder, &passDesc);
    for (uint32_t passIndex = 0; passIndex < passCount; ++passIndex) {
        const uint32_t dynamicOffset =
            (parameterBaseSlot + passIndex) * kParameterStride;
        wgpuComputePassEncoderSetBindGroup(
            compute, 0, bindGroups[passIndex], 1, &dynamicOffset);
        if (blocks != 0u) {
            wgpuComputePassEncoderSetPipeline(compute, radixHistogramPipeline_);
            if (indirectDispatchBuffer) {
                wgpuComputePassEncoderDispatchWorkgroupsIndirect(
                    compute, indirectDispatchBuffer, indirectWorkOffset);
            } else {
                wgpuComputePassEncoderDispatchWorkgroups(compute, blocks, 1, 1);
            }
        }
        wgpuComputePassEncoderSetPipeline(compute, radixPrefixPipeline_);
        if (indirectDispatchBuffer) {
            wgpuComputePassEncoderDispatchWorkgroupsIndirect(
                compute, indirectDispatchBuffer, indirectScalarOffset);
        } else {
            wgpuComputePassEncoderDispatchWorkgroups(compute, 1, 1, 1);
        }
        if (blocks != 0u) {
            wgpuComputePassEncoderSetPipeline(compute, radixScatterPipeline_);
            if (indirectDispatchBuffer) {
                wgpuComputePassEncoderDispatchWorkgroupsIndirect(
                    compute, indirectDispatchBuffer, indirectWorkOffset);
            } else {
                wgpuComputePassEncoderDispatchWorkgroups(compute, blocks, 1, 1);
            }
        }
    }
    wgpuComputePassEncoderEnd(compute);
    wgpuComputePassEncoderRelease(compute);
    flushParams(parameterBaseSlot, passCount);
    return true;
}

bool DeterministicGpuPrimitives::encodeAdjacentUnique(
    WGPUCommandEncoder encoder, WGPUBuffer input, WGPUBuffer output,
    uint32_t count, uint32_t logicalKeyWords) {
    if (!encoder || !input || !output || count > capacity_
        || (logicalKeyWords != 1u && logicalKeyWords != 2u)) return false;
    const uint32_t blocks = (count + workgroupSize_ - 1u) / workgroupSize_;
    writeParams(2u, Params{{count, 0u, blocks, logicalKeyWords}});
    const std::array<gpu::BindGroupEntry, 3> markEntries = {
        gpu::BindGroupEntry(30).buffer(input),
        gpu::BindGroupEntry(32).buffer(predicatesBuffer_),
        gpu::BindGroupEntry(4).buffer(parameterBuffer_, 0, sizeof(Params)),
    };
    WGPUBindGroup markBindGroup = cachedBindGroup(
        uniqueMarkLayout_, markEntries,
        "deterministic_unique_mark_bind_group");
    if (!markBindGroup) return false;
    const uint32_t markOffset = 2u * kParameterStride;
    WGPUComputePassDescriptor passDesc{};
    WGPUComputePassEncoder markPass =
        wgpuCommandEncoderBeginComputePass(encoder, &passDesc);
    wgpuComputePassEncoderSetBindGroup(
        markPass, 0, markBindGroup, 1, &markOffset);
    if (blocks != 0u) {
        wgpuComputePassEncoderSetPipeline(markPass, uniqueMarkPipeline_);
        wgpuComputePassEncoderDispatchWorkgroups(markPass, blocks, 1, 1);
    }
    wgpuComputePassEncoderEnd(markPass);
    wgpuComputePassEncoderRelease(markPass);

    if (!encodeScanAt(encoder, predicatesBuffer_, offsetsBuffer_, count, 6u))
        return false;
    const std::array<gpu::BindGroupEntry, 6> scatterEntries = {
        gpu::BindGroupEntry(30).buffer(input),
        gpu::BindGroupEntry(31).buffer(output),
        gpu::BindGroupEntry(32).buffer(predicatesBuffer_),
        gpu::BindGroupEntry(33).buffer(offsetsBuffer_),
        gpu::BindGroupEntry(14).buffer(resultBuffer_),
        gpu::BindGroupEntry(4).buffer(parameterBuffer_, 0, sizeof(Params)),
    };
    WGPUBindGroup scatterBindGroup = cachedBindGroup(
        uniqueScatterLayout_, scatterEntries,
        "deterministic_unique_scatter_bind_group");
    if (!scatterBindGroup) return false;
    WGPUComputePassEncoder scatterPass =
        wgpuCommandEncoderBeginComputePass(encoder, &passDesc);
    wgpuComputePassEncoderSetBindGroup(
        scatterPass, 0, scatterBindGroup, 1, &markOffset);
    if (blocks != 0u) {
        wgpuComputePassEncoderSetPipeline(scatterPass, uniqueScatterPipeline_);
        wgpuComputePassEncoderDispatchWorkgroups(scatterPass, blocks, 1, 1);
    }
    wgpuComputePassEncoderSetPipeline(scatterPass, uniqueFinalizePipeline_);
    wgpuComputePassEncoderDispatchWorkgroups(scatterPass, 1, 1, 1);
    wgpuComputePassEncoderEnd(scatterPass);
    wgpuComputePassEncoderRelease(scatterPass);
    flushParams(2u, 1u);
    return true;
}

bool DeterministicGpuPrimitives::encodeSortedMerge(
    WGPUCommandEncoder encoder, WGPUBuffer previous, uint32_t previousCount,
    WGPUBuffer current, uint32_t currentCount, WGPUBuffer output,
    WGPUBuffer outputTags) {
    if (!encoder || !previous || !current || !output || !outputTags
        || previousCount + uint64_t{currentCount} > capacity_) return false;
    writeParams(16u, Params{{previousCount, currentCount, 0u, 2u}});
    const std::array<gpu::BindGroupEntry, 6> entries = {
        gpu::BindGroupEntry(40).buffer(previous),
        gpu::BindGroupEntry(41).buffer(current),
        gpu::BindGroupEntry(42).buffer(output),
        gpu::BindGroupEntry(43).buffer(outputTags),
        gpu::BindGroupEntry(14).buffer(resultBuffer_),
        gpu::BindGroupEntry(4).buffer(parameterBuffer_, 0, sizeof(Params)),
    };
    WGPUBindGroup bindGroup = cachedBindGroup(
        mergeLayout_, entries, "deterministic_merge_bind_group");
    if (!bindGroup) return false;
    const uint32_t dynamicOffset = 16u * kParameterStride;
    WGPUComputePassDescriptor passDesc{};
    WGPUComputePassEncoder pass =
        wgpuCommandEncoderBeginComputePass(encoder, &passDesc);
    wgpuComputePassEncoderSetBindGroup(pass, 0, bindGroup, 1, &dynamicOffset);
    wgpuComputePassEncoderSetPipeline(pass, mergePipeline_);
    wgpuComputePassEncoderDispatchWorkgroups(pass, 1, 1, 1);
    wgpuComputePassEncoderEnd(pass);
    wgpuComputePassEncoderRelease(pass);
    flushParams(16u, 1u);
    return true;
}

bool DeterministicGpuPrimitives::encodeAssignFreeIds(
    WGPUCommandEncoder encoder, WGPUBuffer requests, uint32_t requestCount,
    WGPUBuffer freeIds, uint32_t freeIdCount, WGPUBuffer assignedIdsBuffer) {
    if (!encoder || !requests || !freeIds || !assignedIdsBuffer
        || requestCount > capacity_ || freeIdCount > capacity_) return false;
    if (!encodeScanAt(encoder, requests, offsetsBuffer_, requestCount, 7u))
        return false;
    const uint32_t blocks =
        (requestCount + workgroupSize_ - 1u) / workgroupSize_;
    writeParams(3u, Params{{requestCount, freeIdCount, blocks, 0u}});
    const std::array<gpu::BindGroupEntry, 6> entries = {
        gpu::BindGroupEntry(50).buffer(requests),
        gpu::BindGroupEntry(51).buffer(offsetsBuffer_),
        gpu::BindGroupEntry(52).buffer(freeIds),
        gpu::BindGroupEntry(53).buffer(assignedIdsBuffer),
        gpu::BindGroupEntry(14).buffer(resultBuffer_),
        gpu::BindGroupEntry(4).buffer(parameterBuffer_, 0, sizeof(Params)),
    };
    WGPUBindGroup bindGroup = cachedBindGroup(
        assignLayout_, entries, "deterministic_assign_bind_group");
    if (!bindGroup) return false;
    const uint32_t dynamicOffset = 3u * kParameterStride;
    WGPUComputePassDescriptor passDesc{};
    WGPUComputePassEncoder pass =
        wgpuCommandEncoderBeginComputePass(encoder, &passDesc);
    wgpuComputePassEncoderSetBindGroup(pass, 0, bindGroup, 1, &dynamicOffset);
    if (blocks != 0u) {
        wgpuComputePassEncoderSetPipeline(pass, assignScatterPipeline_);
        wgpuComputePassEncoderDispatchWorkgroups(pass, blocks, 1, 1);
    }
    wgpuComputePassEncoderSetPipeline(pass, assignFinalizePipeline_);
    wgpuComputePassEncoderDispatchWorkgroups(pass, 1, 1, 1);
    wgpuComputePassEncoderEnd(pass);
    wgpuComputePassEncoderRelease(pass);
    flushParams(3u, 1u);
    return true;
}

void DeterministicGpuPrimitives::shutdown() {
    releaseCachedBindGroups();
    releaseHandle(scanBlocksPipeline_, wgpuComputePipelineRelease);
    releaseHandle(scanPrefixPipeline_, wgpuComputePipelineRelease);
    releaseHandle(scanAddPipeline_, wgpuComputePipelineRelease);
    releaseHandle(compactScatterPipeline_, wgpuComputePipelineRelease);
    releaseHandle(compactFinalizePipeline_, wgpuComputePipelineRelease);
    releaseHandle(radixHistogramPipeline_, wgpuComputePipelineRelease);
    releaseHandle(radixPrefixPipeline_, wgpuComputePipelineRelease);
    releaseHandle(radixScatterPipeline_, wgpuComputePipelineRelease);
    releaseHandle(uniqueMarkPipeline_, wgpuComputePipelineRelease);
    releaseHandle(uniqueScatterPipeline_, wgpuComputePipelineRelease);
    releaseHandle(uniqueFinalizePipeline_, wgpuComputePipelineRelease);
    releaseHandle(mergePipeline_, wgpuComputePipelineRelease);
    releaseHandle(assignScatterPipeline_, wgpuComputePipelineRelease);
    releaseHandle(assignFinalizePipeline_, wgpuComputePipelineRelease);
    releaseHandle(scanPipelineLayout_, wgpuPipelineLayoutRelease);
    releaseHandle(compactPipelineLayout_, wgpuPipelineLayoutRelease);
    releaseHandle(radixPipelineLayout_, wgpuPipelineLayoutRelease);
    releaseHandle(uniqueMarkPipelineLayout_, wgpuPipelineLayoutRelease);
    releaseHandle(uniqueScatterPipelineLayout_, wgpuPipelineLayoutRelease);
    releaseHandle(mergePipelineLayout_, wgpuPipelineLayoutRelease);
    releaseHandle(assignPipelineLayout_, wgpuPipelineLayoutRelease);
    releaseHandle(scanLayout_, wgpuBindGroupLayoutRelease);
    releaseHandle(compactLayout_, wgpuBindGroupLayoutRelease);
    releaseHandle(radixLayout_, wgpuBindGroupLayoutRelease);
    releaseHandle(uniqueMarkLayout_, wgpuBindGroupLayoutRelease);
    releaseHandle(uniqueScatterLayout_, wgpuBindGroupLayoutRelease);
    releaseHandle(mergeLayout_, wgpuBindGroupLayoutRelease);
    releaseHandle(assignLayout_, wgpuBindGroupLayoutRelease);
    releaseHandle(shaderModule_, wgpuShaderModuleRelease);
    releaseBuffer(parameterBuffer_);
    releaseBuffer(blockSumsBuffer_);
    releaseBuffer(blockPrefixBuffer_);
    releaseBuffer(offsetsBuffer_);
    releaseBuffer(predicatesBuffer_);
    releaseBuffer(radixScratchBuffer_);
    releaseBuffer(radixHistogramBuffer_);
    releaseBuffer(radixOffsetsBuffer_);
    releaseBuffer(radixDigitBasesBuffer_);
    releaseBuffer(resultBuffer_);
    parameterUpload_.clear();
    device_ = nullptr;
    queue_ = nullptr;
    capacity_ = 0;
    workgroupSize_ = 0;
    blockCapacity_ = 0;
    scratchBytes_ = 0;
    shaderPath_.clear();
}

} // namespace voxy::physics
