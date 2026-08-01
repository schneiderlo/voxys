// ═══════════════════════════════════════════════════════════════════════════════
// resources.cpp - WebGPU Resource Creation Helpers Implementation
// ═══════════════════════════════════════════════════════════════════════════════

#include "gpu/resources.hpp"
#include "core/log.hpp"

#include <algorithm>
#include <fstream>
#include <cstring>
#include <cmath>
#include <limits>

namespace voxy::gpu {

// ═══════════════════════════════════════════════════════════════════════════════
// Buffer Creation
// ═══════════════════════════════════════════════════════════════════════════════

bool isBufferDescriptorValid(const BufferDesc& desc) noexcept {
    if (desc.size == 0u || desc.usage == WGPUBufferUsage_None) return false;

    constexpr WGPUBufferUsageFlags knownUsage =
        WGPUBufferUsage_MapRead | WGPUBufferUsage_MapWrite
        | WGPUBufferUsage_CopySrc | WGPUBufferUsage_CopyDst
        | WGPUBufferUsage_Index | WGPUBufferUsage_Vertex
        | WGPUBufferUsage_Uniform | WGPUBufferUsage_Storage
        | WGPUBufferUsage_Indirect | WGPUBufferUsage_QueryResolve;
    if ((desc.usage & ~knownUsage) != 0u) return false;

    const bool mapRead = (desc.usage & WGPUBufferUsage_MapRead) != 0u;
    const bool mapWrite = (desc.usage & WGPUBufferUsage_MapWrite) != 0u;
    constexpr WGPUBufferUsageFlags mapReadUsage =
        WGPUBufferUsage_MapRead | WGPUBufferUsage_CopyDst;
    constexpr WGPUBufferUsageFlags mapWriteUsage =
        WGPUBufferUsage_MapWrite | WGPUBufferUsage_CopySrc;
    if (mapRead && mapWrite) return false;
    if (mapRead && (desc.usage & ~mapReadUsage) != 0u) {
        return false;
    }
    if (mapWrite && (desc.usage & ~mapWriteUsage) != 0u) {
        return false;
    }
    return !desc.mappedAtCreation || (desc.size & 3u) == 0u;
}

bool isBufferWriteDataValid(
    uint64_t bufferSize, WGPUBufferUsageFlags usage, uint64_t offset,
    size_t dataSize) noexcept {
    if ((usage & WGPUBufferUsage_CopyDst) == 0u
        || (offset & 3u) != 0u || (dataSize & 3u) != 0u
        || offset > bufferSize) {
        return false;
    }
    return static_cast<uint64_t>(dataSize) <= bufferSize - offset;
}

WGPUBuffer createBuffer(WGPUDevice device, const BufferDesc& desc) {
    if (!device) {
        LOG_ERROR("Cannot create buffer: device is null");
        return nullptr;
    }
    
    if (!isBufferDescriptorValid(desc)) {
        LOG_ERROR("Cannot create buffer: invalid descriptor for '{}' "
                  "(size {}, usage {}, mapped {})",
                  desc.label, desc.size, static_cast<uint64_t>(desc.usage),
                  desc.mappedAtCreation);
        return nullptr;
    }
    
    WGPUBufferDescriptor bufferDesc{};
    bufferDesc.nextInChain = nullptr;
    const std::string label(desc.label);
    WGPU_SET_LABEL(bufferDesc, label.empty() ? nullptr : label.c_str());
    bufferDesc.usage = desc.usage;
    bufferDesc.size = desc.size;
    bufferDesc.mappedAtCreation = desc.mappedAtCreation;
    
    WGPUBuffer buffer = wgpuDeviceCreateBuffer(device, &bufferDesc);
    
    if (!buffer) {
        LOG_ERROR("Failed to create buffer '{}' (size: {} bytes)", 
                  desc.label, desc.size);
        return nullptr;
    }
    
    LOG_TRACE("Created buffer '{}': {} bytes", desc.label, desc.size);
    return buffer;
}

WGPUBuffer createBufferWithData(WGPUDevice device, WGPUQueue queue,
                                 const BufferDesc& desc,
                                 std::span<const std::byte> data) {
    if (!queue) {
        LOG_ERROR("Cannot create buffer with data: queue is null");
        return nullptr;
    }
    if (desc.mappedAtCreation) {
        LOG_ERROR("Cannot queue-upload into a buffer mapped at creation");
        return nullptr;
    }
    
    if (data.size() > desc.size) {
        LOG_ERROR("Data size ({}) exceeds buffer size ({})", data.size(), desc.size);
        return nullptr;
    }
    
    // Create buffer with CopyDst if not already set
    BufferDesc actualDesc = desc;
    if (!(actualDesc.usage & WGPUBufferUsage_CopyDst)) {
        actualDesc.usage |= WGPUBufferUsage_CopyDst;
    }
    
    WGPUBuffer buffer = createBuffer(device, actualDesc);
    if (!buffer) {
        return nullptr;
    }
    
    // Upload data
    if (!data.empty() && !writeBuffer(queue, buffer, 0, data)) {
        wgpuBufferDestroy(buffer);
        wgpuBufferRelease(buffer);
        return nullptr;
    }
    
    return buffer;
}

bool writeBuffer(WGPUQueue queue, WGPUBuffer buffer, uint64_t offset,
                 std::span<const std::byte> data) {
    if (!queue || !buffer) {
        LOG_ERROR("Cannot write buffer: queue or buffer is null");
        return false;
    }
    const uint64_t bufferSize = wgpuBufferGetSize(buffer);
    const WGPUBufferUsageFlags usage = wgpuBufferGetUsage(buffer);
    if (!isBufferWriteDataValid(
            bufferSize, usage, offset, data.size())) {
        LOG_ERROR("Cannot write {} bytes at offset {} into a {}-byte buffer "
                  "with usage {}",
                  data.size(), offset, bufferSize,
                  static_cast<uint64_t>(usage));
        return false;
    }
    if (data.empty()) {
        return true;
    }
    
    wgpuQueueWriteBuffer(queue, buffer, offset, data.data(), data.size());
    return true;
}

// ═══════════════════════════════════════════════════════════════════════════════
// Texture Creation
// ═══════════════════════════════════════════════════════════════════════════════

WGPUTexture createTexture(WGPUDevice device, const TextureDesc& desc) {
    if (!device) {
        LOG_ERROR("Cannot create texture: device is null");
        return nullptr;
    }
    
    const uint32_t maximumMipLevels =
        calculateMipLevelCount(desc.width, desc.height);
    if (desc.width == 0 || desc.height == 0
        || desc.depthOrArrayLayers == 0 || desc.mipLevelCount == 0
        || desc.mipLevelCount > maximumMipLevels
        || (desc.sampleCount != 1 && desc.sampleCount != 4)
        || (desc.sampleCount > 1 && desc.mipLevelCount != 1)
        || desc.format == WGPUTextureFormat_Undefined
        || desc.usage == WGPUTextureUsage_None) {
        LOG_ERROR("Cannot create texture: invalid descriptor for '{}' "
                  "({}x{}x{}, {} mips, {} samples, format {}, usage {})",
                  desc.label, desc.width, desc.height,
                  desc.depthOrArrayLayers, desc.mipLevelCount,
                  desc.sampleCount, static_cast<int>(desc.format),
                  static_cast<uint64_t>(desc.usage));
        return nullptr;
    }
    
    WGPUTextureDescriptor textureDesc{};
    textureDesc.nextInChain = nullptr;
    const std::string label(desc.label);
    WGPU_SET_LABEL(textureDesc, label.empty() ? nullptr : label.c_str());
    textureDesc.usage = desc.usage;
    textureDesc.dimension = desc.dimension;
    textureDesc.size.width = desc.width;
    textureDesc.size.height = desc.height;
    textureDesc.size.depthOrArrayLayers = desc.depthOrArrayLayers;
    textureDesc.format = desc.format;
    textureDesc.mipLevelCount = desc.mipLevelCount;
    textureDesc.sampleCount = desc.sampleCount;
    textureDesc.viewFormatCount = 0;
    textureDesc.viewFormats = nullptr;
    
    WGPUTexture texture = wgpuDeviceCreateTexture(device, &textureDesc);
    
    if (!texture) {
        LOG_ERROR("Failed to create texture '{}' ({}x{}, format: {})", 
                  desc.label, desc.width, desc.height, static_cast<int>(desc.format));
        return nullptr;
    }
    
    LOG_TRACE("Created texture '{}': {}x{}, {} mip levels", 
              desc.label, desc.width, desc.height, desc.mipLevelCount);
    return texture;
}

WGPUTexture createTextureWithData(WGPUDevice device, WGPUQueue queue,
                                   const TextureDesc& desc,
                                   std::span<const std::byte> data,
                                   uint32_t bytesPerRow) {
    if (!queue) {
        LOG_ERROR("Cannot create texture with data: queue is null");
        return nullptr;
    }

    const uint32_t bytesPerTexel = getBytesPerPixel(desc.format);
    if (!isTextureUploadDataValid(
            data.size(), desc.width, desc.height, bytesPerRow,
            bytesPerTexel)) {
        LOG_ERROR("Texture data is too small or has an invalid row stride: {} bytes for {}x{} at {} bytes/row",
                  data.size(), desc.width, desc.height, bytesPerRow);
        return nullptr;
    }
    
    // Create texture with CopyDst if not already set
    TextureDesc actualDesc = desc;
    if (!(actualDesc.usage & WGPUTextureUsage_CopyDst)) {
        actualDesc.usage |= WGPUTextureUsage_CopyDst;
    }
    
    WGPUTexture texture = createTexture(device, actualDesc);
    if (!texture) {
        return nullptr;
    }
    
    // Upload data to base mip level
    if (!writeTexture(
            queue, texture, data, desc.width, desc.height, bytesPerRow, 0)) {
        wgpuTextureDestroy(texture);
        wgpuTextureRelease(texture);
        return nullptr;
    }
    
    return texture;
}

bool writeTexture(WGPUQueue queue, WGPUTexture texture,
                  std::span<const std::byte> data,
                  uint32_t width, uint32_t height, uint32_t bytesPerRow,
                  uint32_t mipLevel) {
    if (!queue || !texture) {
        LOG_ERROR("Cannot write texture: queue or texture is null");
        return false;
    }

    const uint32_t mipLevelCount = wgpuTextureGetMipLevelCount(texture);
    const uint32_t textureWidth = wgpuTextureGetWidth(texture);
    const uint32_t textureHeight = wgpuTextureGetHeight(texture);
    if (mipLevel >= mipLevelCount) {
        LOG_ERROR("Texture upload mip {} is outside {} levels",
                  mipLevel, mipLevelCount);
        return false;
    }
    const uint32_t mipWidth = std::max(textureWidth >> mipLevel, 1u);
    const uint32_t mipHeight = std::max(textureHeight >> mipLevel, 1u);
    if (width > mipWidth || height > mipHeight) {
        LOG_ERROR("Texture upload extent {}x{} exceeds mip {} extent {}x{}",
                  width, height, mipLevel, mipWidth, mipHeight);
        return false;
    }

    const uint32_t bytesPerTexel =
        getBytesPerPixel(wgpuTextureGetFormat(texture));
    if (!isTextureUploadDataValid(
            data.size(), width, height, bytesPerRow, bytesPerTexel)) {
        LOG_ERROR("Texture data is too small or has an invalid row stride: {} bytes for {}x{} at {} bytes/row",
                  data.size(), width, height, bytesPerRow);
        return false;
    }

    CompatImageCopyTexture destination = makeTextureCopyDest(texture, mipLevel, {0, 0, 0});
    
    WGPUExtent3D writeSize{};
    writeSize.width = width;
    writeSize.height = height;
    writeSize.depthOrArrayLayers = 1;
    
    // queue.writeTexture only requires block-byte alignment. The separate
    // 256-byte rule applies to buffer-to-texture command copies.
    CompatTextureDataLayout dataLayout =
        makeTextureDataLayout(0, bytesPerRow, height);
    wgpuQueueWriteTexture(queue, &destination, data.data(), data.size(),
                          &dataLayout, &writeSize);
    return true;
}

// ═══════════════════════════════════════════════════════════════════════════════
// Texture View Creation
// ═══════════════════════════════════════════════════════════════════════════════

bool isTextureViewRangeValid(
    uint32_t textureMipLevels, uint32_t textureLayers,
    const TextureViewDesc& desc) noexcept {
    return textureMipLevels != 0u && textureLayers != 0u
        && desc.mipLevelCount != 0u && desc.arrayLayerCount != 0u
        && desc.baseMipLevel < textureMipLevels
        && desc.mipLevelCount
            <= textureMipLevels - desc.baseMipLevel
        && desc.baseArrayLayer < textureLayers
        && desc.arrayLayerCount
            <= textureLayers - desc.baseArrayLayer;
}

WGPUTextureView createTextureView(WGPUTexture texture, const TextureViewDesc& desc) {
    if (!texture) {
        LOG_ERROR("Cannot create texture view: texture is null");
        return nullptr;
    }

    const uint32_t textureMipLevels = wgpuTextureGetMipLevelCount(texture);
    const uint32_t textureLayers = wgpuTextureGetDepthOrArrayLayers(texture);
    const WGPUTextureDimension textureDimension =
        wgpuTextureGetDimension(texture);
    const WGPUTextureFormat textureFormat = wgpuTextureGetFormat(texture);
    const uint32_t sampleCount = wgpuTextureGetSampleCount(texture);
    if (!isTextureViewRangeValid(textureMipLevels, textureLayers, desc)
        || (desc.format != WGPUTextureFormat_Undefined
            && desc.format != textureFormat)) {
        LOG_ERROR("Cannot create texture view '{}': invalid format or range",
                  desc.label);
        return nullptr;
    }

    const bool validDimension = [&]() {
        switch (desc.dimension) {
            case WGPUTextureViewDimension_Undefined:
                return true;
            case WGPUTextureViewDimension_1D:
                return textureDimension == WGPUTextureDimension_1D
                    && desc.arrayLayerCount == 1u;
            case WGPUTextureViewDimension_2D:
                return textureDimension == WGPUTextureDimension_2D
                    && desc.arrayLayerCount == 1u;
            case WGPUTextureViewDimension_2DArray:
                return textureDimension == WGPUTextureDimension_2D;
            case WGPUTextureViewDimension_Cube:
                return textureDimension == WGPUTextureDimension_2D
                    && desc.arrayLayerCount == 6u
                    && std::max(
                        wgpuTextureGetWidth(texture) >> desc.baseMipLevel, 1u)
                        == std::max(
                            wgpuTextureGetHeight(texture)
                                >> desc.baseMipLevel, 1u);
            case WGPUTextureViewDimension_CubeArray:
                return textureDimension == WGPUTextureDimension_2D
                    && (desc.arrayLayerCount % 6u) == 0u
                    && std::max(
                        wgpuTextureGetWidth(texture) >> desc.baseMipLevel, 1u)
                        == std::max(
                            wgpuTextureGetHeight(texture)
                                >> desc.baseMipLevel, 1u);
            case WGPUTextureViewDimension_3D:
                return textureDimension == WGPUTextureDimension_3D
                    && desc.baseArrayLayer == 0u
                    && desc.arrayLayerCount == 1u;
            default:
                return false;
        }
    }();
    const bool validAspect =
        desc.aspect == WGPUTextureAspect_All
        || (desc.aspect == WGPUTextureAspect_DepthOnly
            && isDepthStencilFormat(textureFormat))
        || (desc.aspect == WGPUTextureAspect_StencilOnly
            && (textureFormat == WGPUTextureFormat_Depth24PlusStencil8
                || textureFormat
                    == WGPUTextureFormat_Depth32FloatStencil8));
    if (!validDimension || !validAspect
        || (sampleCount > 1u
            && (desc.dimension != WGPUTextureViewDimension_2D
                || desc.mipLevelCount != 1u))) {
        LOG_ERROR("Cannot create texture view '{}': incompatible dimension, "
                  "aspect, or sample count", desc.label);
        return nullptr;
    }
    
    WGPUTextureViewDescriptor viewDesc{};
    viewDesc.nextInChain = nullptr;
    const std::string label(desc.label);
    WGPU_SET_LABEL(viewDesc, label.empty() ? nullptr : label.c_str());
    viewDesc.format = desc.format;
    viewDesc.dimension = desc.dimension;
    viewDesc.baseMipLevel = desc.baseMipLevel;
    viewDesc.mipLevelCount = desc.mipLevelCount;
    viewDesc.baseArrayLayer = desc.baseArrayLayer;
    viewDesc.arrayLayerCount = desc.arrayLayerCount;
    viewDesc.aspect = desc.aspect;
    
    WGPUTextureView view = wgpuTextureCreateView(texture, &viewDesc);
    
    if (!view) {
        LOG_ERROR("Failed to create texture view '{}'", desc.label);
        return nullptr;
    }
    
    return view;
}

WGPUTextureView createMipView(WGPUTexture texture, uint32_t mipLevel,
                               WGPUTextureFormat format) {
    TextureViewDesc desc{};
    desc.format = format;
    desc.baseMipLevel = mipLevel;
    desc.mipLevelCount = 1;
    return createTextureView(texture, desc);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Sampler Creation
// ═══════════════════════════════════════════════════════════════════════════════

bool isSamplerDescriptorValid(const SamplerDesc& desc) noexcept {
    const auto validAddressMode = [](WGPUAddressMode mode) {
        return mode == WGPUAddressMode_ClampToEdge
            || mode == WGPUAddressMode_Repeat
            || mode == WGPUAddressMode_MirrorRepeat;
    };
    const auto validFilter = [](WGPUFilterMode mode) {
        return mode == WGPUFilterMode_Nearest
            || mode == WGPUFilterMode_Linear;
    };
    const auto validMipmapFilter = [](WGPUMipmapFilterMode mode) {
        return mode == WGPUMipmapFilterMode_Nearest
            || mode == WGPUMipmapFilterMode_Linear;
    };

    if (!validAddressMode(desc.addressModeU)
        || !validAddressMode(desc.addressModeV)
        || !validAddressMode(desc.addressModeW)
        || !validFilter(desc.magFilter)
        || !validFilter(desc.minFilter)
        || !validMipmapFilter(desc.mipmapFilter)
        || !std::isfinite(desc.lodMinClamp)
        || !std::isfinite(desc.lodMaxClamp)
        || desc.lodMinClamp < 0.0f
        || desc.lodMaxClamp < desc.lodMinClamp
        || desc.maxAnisotropy < 1u
        || desc.maxAnisotropy > 16u) {
        return false;
    }

    return desc.maxAnisotropy == 1u
        || (desc.magFilter == WGPUFilterMode_Linear
            && desc.minFilter == WGPUFilterMode_Linear
            && desc.mipmapFilter == WGPUMipmapFilterMode_Linear);
}

WGPUSampler createSampler(WGPUDevice device, const SamplerDesc& desc) {
    if (!device) {
        LOG_ERROR("Cannot create sampler: device is null");
        return nullptr;
    }
    if (!isSamplerDescriptorValid(desc)) {
        LOG_ERROR("Cannot create sampler '{}': invalid descriptor", desc.label);
        return nullptr;
    }
    
    WGPUSamplerDescriptor samplerDesc{};
    samplerDesc.nextInChain = nullptr;
    const std::string label(desc.label);
    WGPU_SET_LABEL(samplerDesc, label.empty() ? nullptr : label.c_str());
    samplerDesc.addressModeU = desc.addressModeU;
    samplerDesc.addressModeV = desc.addressModeV;
    samplerDesc.addressModeW = desc.addressModeW;
    samplerDesc.magFilter = desc.magFilter;
    samplerDesc.minFilter = desc.minFilter;
    samplerDesc.mipmapFilter = desc.mipmapFilter;
    samplerDesc.lodMinClamp = desc.lodMinClamp;
    samplerDesc.lodMaxClamp = desc.lodMaxClamp;
    samplerDesc.compare = desc.compare;
    samplerDesc.maxAnisotropy = desc.maxAnisotropy;
    
    WGPUSampler sampler = wgpuDeviceCreateSampler(device, &samplerDesc);
    
    if (!sampler) {
        LOG_ERROR("Failed to create sampler '{}'", desc.label);
        return nullptr;
    }
    
    LOG_TRACE("Created sampler '{}'", desc.label);
    return sampler;
}

// ═══════════════════════════════════════════════════════════════════════════════
// Bind Group Layout Entry Builder
// ═══════════════════════════════════════════════════════════════════════════════

BindGroupLayoutEntry::BindGroupLayoutEntry(uint32_t binding) noexcept {
    std::memset(&entry_, 0, sizeof(entry_));
    entry_.nextInChain = nullptr;
    entry_.binding = binding;
    entry_.visibility = WGPUShaderStage_None;
}

BindGroupLayoutEntry& BindGroupLayoutEntry::visibility(WGPUShaderStageFlags stages) noexcept {
    entry_.visibility = stages;
    return *this;
}

BindGroupLayoutEntry& BindGroupLayoutEntry::vertexVisible() noexcept {
    entry_.visibility |= WGPUShaderStage_Vertex;
    return *this;
}

BindGroupLayoutEntry& BindGroupLayoutEntry::fragmentVisible() noexcept {
    entry_.visibility |= WGPUShaderStage_Fragment;
    return *this;
}

BindGroupLayoutEntry& BindGroupLayoutEntry::computeVisible() noexcept {
    entry_.visibility |= WGPUShaderStage_Compute;
    return *this;
}

BindGroupLayoutEntry& BindGroupLayoutEntry::allStagesVisible() noexcept {
    entry_.visibility = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment | WGPUShaderStage_Compute;
    return *this;
}

BindGroupLayoutEntry& BindGroupLayoutEntry::uniformBuffer(bool hasDynamicOffset,
                                                           uint64_t minBindingSize) noexcept {
    entry_.buffer.type = WGPUBufferBindingType_Uniform;
    entry_.buffer.hasDynamicOffset = hasDynamicOffset;
    entry_.buffer.minBindingSize = minBindingSize;
    return *this;
}

BindGroupLayoutEntry& BindGroupLayoutEntry::storageBuffer(bool readOnly,
                                                           bool hasDynamicOffset,
                                                           uint64_t minBindingSize) noexcept {
    entry_.buffer.type = readOnly ? WGPUBufferBindingType_ReadOnlyStorage 
                                  : WGPUBufferBindingType_Storage;
    entry_.buffer.hasDynamicOffset = hasDynamicOffset;
    entry_.buffer.minBindingSize = minBindingSize;
    return *this;
}

BindGroupLayoutEntry& BindGroupLayoutEntry::texture(WGPUTextureSampleType sampleType,
                                                     WGPUTextureViewDimension viewDimension,
                                                     bool multisampled) noexcept {
    entry_.texture.sampleType = sampleType;
    entry_.texture.viewDimension = viewDimension;
    entry_.texture.multisampled = multisampled;
    return *this;
}

BindGroupLayoutEntry& BindGroupLayoutEntry::storageTexture(WGPUStorageTextureAccess access,
                                                            WGPUTextureFormat format,
                                                            WGPUTextureViewDimension viewDimension) noexcept {
    entry_.storageTexture.access = access;
    entry_.storageTexture.format = format;
    entry_.storageTexture.viewDimension = viewDimension;
    return *this;
}

BindGroupLayoutEntry& BindGroupLayoutEntry::sampler(WGPUSamplerBindingType type) noexcept {
    entry_.sampler.type = type;
    return *this;
}

// ═══════════════════════════════════════════════════════════════════════════════
// Bind Group Layout Creation
// ═══════════════════════════════════════════════════════════════════════════════

WGPUBindGroupLayout createBindGroupLayout(WGPUDevice device,
                                           std::span<const BindGroupLayoutEntry> entries,
                                           std::string_view label) {
    std::vector<WGPUBindGroupLayoutEntry> rawEntries;
    rawEntries.reserve(entries.size());
    for (const auto& entry : entries) {
        rawEntries.push_back(entry.get());
    }
    return createBindGroupLayout(device, std::span(rawEntries), label);
}

WGPUBindGroupLayout createBindGroupLayout(WGPUDevice device,
                                           std::span<const WGPUBindGroupLayoutEntry> entries,
                                           std::string_view label) {
    if (!device) {
        LOG_ERROR("Cannot create bind group layout: device is null");
        return nullptr;
    }
    
    WGPUBindGroupLayoutDescriptor layoutDesc{};
    layoutDesc.nextInChain = nullptr;
    const std::string labelString(label);
    WGPU_SET_LABEL(
        layoutDesc, labelString.empty() ? nullptr : labelString.c_str());
    layoutDesc.entryCount = entries.size();
    layoutDesc.entries = entries.data();
    
    WGPUBindGroupLayout layout = wgpuDeviceCreateBindGroupLayout(device, &layoutDesc);
    
    if (!layout) {
        LOG_ERROR("Failed to create bind group layout '{}'", label);
        return nullptr;
    }
    
    LOG_TRACE("Created bind group layout '{}' with {} entries", label, entries.size());
    return layout;
}

// ═══════════════════════════════════════════════════════════════════════════════
// Bind Group Entry Builder
// ═══════════════════════════════════════════════════════════════════════════════

BindGroupEntry::BindGroupEntry(uint32_t binding) noexcept {
    std::memset(&entry_, 0, sizeof(entry_));
    entry_.binding = binding;
}

BindGroupEntry& BindGroupEntry::buffer(WGPUBuffer buf, uint64_t offset, uint64_t size) noexcept {
    entry_.buffer = buf;
    entry_.offset = offset;
    entry_.size = size;
    return *this;
}

BindGroupEntry& BindGroupEntry::sampler(WGPUSampler samp) noexcept {
    entry_.sampler = samp;
    return *this;
}

BindGroupEntry& BindGroupEntry::textureView(WGPUTextureView view) noexcept {
    entry_.textureView = view;
    return *this;
}

// ═══════════════════════════════════════════════════════════════════════════════
// Bind Group Creation
// ═══════════════════════════════════════════════════════════════════════════════

WGPUBindGroup createBindGroup(WGPUDevice device,
                               WGPUBindGroupLayout layout,
                               std::span<const BindGroupEntry> entries,
                               std::string_view label) {
    std::vector<WGPUBindGroupEntry> rawEntries;
    rawEntries.reserve(entries.size());
    for (const auto& entry : entries) {
        rawEntries.push_back(entry.get());
    }
    return createBindGroup(device, layout, std::span(rawEntries), label);
}

WGPUBindGroup createBindGroup(WGPUDevice device,
                               WGPUBindGroupLayout layout,
                               std::span<const WGPUBindGroupEntry> entries,
                               std::string_view label) {
    if (!device) {
        LOG_ERROR("Cannot create bind group: device is null");
        return nullptr;
    }
    
    if (!layout) {
        LOG_ERROR("Cannot create bind group: layout is null");
        return nullptr;
    }
    
    WGPUBindGroupDescriptor bindGroupDesc{};
    bindGroupDesc.nextInChain = nullptr;
    const std::string labelString(label);
    WGPU_SET_LABEL(
        bindGroupDesc, labelString.empty() ? nullptr : labelString.c_str());
    bindGroupDesc.layout = layout;
    bindGroupDesc.entryCount = entries.size();
    bindGroupDesc.entries = entries.data();
    
    WGPUBindGroup bindGroup = wgpuDeviceCreateBindGroup(device, &bindGroupDesc);
    
    if (!bindGroup) {
        LOG_ERROR("Failed to create bind group '{}'", label);
        return nullptr;
    }
    
    LOG_TRACE("Created bind group '{}' with {} entries", label, entries.size());
    return bindGroup;
}

// ═══════════════════════════════════════════════════════════════════════════════
// Shader Module Creation
// ═══════════════════════════════════════════════════════════════════════════════

WGPUShaderModule createShaderModule(WGPUDevice device,
                                     std::string_view wgslSource,
                                     std::string_view label) {
    if (!device) {
        LOG_ERROR("Cannot create shader module: device is null");
        return nullptr;
    }
    
    if (wgslSource.empty()) {
        LOG_ERROR("Cannot create shader module: source is empty");
        return nullptr;
    }
    
#if defined(VOXY_WASM)
    // Emscripten WebGPU uses different struct names
    WGPUShaderSourceWGSL wgslDesc{};
    wgslDesc.chain.sType = WGPUSType_ShaderSourceWGSL;
    wgslDesc.code.data = wgslSource.data();
    wgslDesc.code.length = wgslSource.size();
    
    WGPUShaderModuleDescriptor moduleDesc{};
    moduleDesc.nextInChain = &wgslDesc.chain;
    const std::string labelString(label);
    WGPU_SET_LABEL(
        moduleDesc, labelString.empty() ? nullptr : labelString.c_str());
#else
    // wgpu-native API
    const std::string source(wgslSource);
    WGPUShaderModuleWGSLDescriptor wgslDesc{};
    wgslDesc.chain.sType = WGPUSType_ShaderModuleWGSLDescriptor;
    wgslDesc.code = source.c_str();
    
    WGPUShaderModuleDescriptor moduleDesc{};
    moduleDesc.nextInChain = &wgslDesc.chain;
    const std::string labelString(label);
    WGPU_SET_LABEL(
        moduleDesc, labelString.empty() ? nullptr : labelString.c_str());
#endif
    
    WGPUShaderModule module = wgpuDeviceCreateShaderModule(device, &moduleDesc);
    
    if (!module) {
        LOG_ERROR("Failed to create shader module '{}'", label);
        return nullptr;
    }
    
    LOG_DEBUG("Created shader module '{}' ({} bytes)", label, wgslSource.size());
    return module;
}

std::optional<std::string_view> findEmbeddedShaderSource(
    std::string_view logicalPath,
    std::span<const ShaderSource> embeddedSources) noexcept {
    std::optional<std::string_view> result;
    for (const ShaderSource& candidate : embeddedSources) {
        if (candidate.logicalPath != logicalPath) continue;
        if (result.has_value() || candidate.wgsl.empty())
            return std::nullopt;
        result = candidate.wgsl;
    }
    return result;
}

WGPUShaderModule loadShaderModule(WGPUDevice device,
                                   const std::filesystem::path& path,
                                   std::string_view label,
                                   std::span<const ShaderSource>
                                       embeddedSources) {
    if (!device) {
        LOG_ERROR("Cannot load shader module: device is null");
        return nullptr;
    }

    if (!embeddedSources.empty()) {
        const std::string logicalPath = path.generic_string();
        const std::optional<std::string_view> source =
            findEmbeddedShaderSource(logicalPath, embeddedSources);
        if (!source.has_value()) {
            LOG_ERROR(
                "Trusted shader bundle has no source for: {}",
                logicalPath);
            return nullptr;
        }
        const std::string actualLabel =
            label.empty() ? path.filename().string()
                          : std::string(label);
        return createShaderModule(device, *source, actualLabel);
    }

    std::ifstream file(path, std::ios::ate | std::ios::binary);
    if (!file.is_open()) {
        LOG_ERROR("Failed to open shader file: {}", path.string());
        return nullptr;
    }
    
    // Get file size and read content
    const auto size = file.tellg();
    constexpr std::streamoff kMaximumShaderBytes = 16ll * 1024ll * 1024ll;
    if (size <= 0
        || size > std::numeric_limits<std::streamsize>::max()
        || size > kMaximumShaderBytes) {
        LOG_ERROR("Shader file is empty or too large: {}", path.string());
        return nullptr;
    }
    file.seekg(0);
    
    std::string source;
    source.resize(static_cast<size_t>(size));
    file.read(source.data(), static_cast<std::streamsize>(size));
    
    if (file.fail()) {
        LOG_ERROR("Failed to read shader file: {}", path.string());
        return nullptr;
    }
    
    // Use filename as label if not provided
    std::string actualLabel = label.empty() ? path.filename().string() : std::string(label);
    
    return createShaderModule(device, source, actualLabel);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Pipeline Layout Creation
// ═══════════════════════════════════════════════════════════════════════════════

WGPUPipelineLayout createPipelineLayout(WGPUDevice device,
                                         std::span<const WGPUBindGroupLayout> bindGroupLayouts,
                                         std::string_view label) {
    if (!device) {
        LOG_ERROR("Cannot create pipeline layout: device is null");
        return nullptr;
    }
    
    WGPUPipelineLayoutDescriptor layoutDesc{};
    layoutDesc.nextInChain = nullptr;
    const std::string labelString(label);
    WGPU_SET_LABEL(
        layoutDesc, labelString.empty() ? nullptr : labelString.c_str());
    layoutDesc.bindGroupLayoutCount = bindGroupLayouts.size();
    layoutDesc.bindGroupLayouts = bindGroupLayouts.data();
    
    WGPUPipelineLayout layout = wgpuDeviceCreatePipelineLayout(device, &layoutDesc);
    
    if (!layout) {
        LOG_ERROR("Failed to create pipeline layout '{}'", label);
        return nullptr;
    }
    
    LOG_TRACE("Created pipeline layout '{}' with {} bind group layouts", 
              label, bindGroupLayouts.size());
    return layout;
}

// ═══════════════════════════════════════════════════════════════════════════════
// Utility Functions
// ═══════════════════════════════════════════════════════════════════════════════

uint32_t getBytesPerPixel(WGPUTextureFormat format) noexcept {
    switch (format) {
        // 8-bit formats
        case WGPUTextureFormat_R8Unorm:
        case WGPUTextureFormat_R8Snorm:
        case WGPUTextureFormat_R8Uint:
        case WGPUTextureFormat_R8Sint:
            return 1;
        
        // 16-bit formats
        case WGPUTextureFormat_R16Uint:
        case WGPUTextureFormat_R16Sint:
        case WGPUTextureFormat_R16Float:
        case WGPUTextureFormat_RG8Unorm:
        case WGPUTextureFormat_RG8Snorm:
        case WGPUTextureFormat_RG8Uint:
        case WGPUTextureFormat_RG8Sint:
        case WGPUTextureFormat_Depth16Unorm:
            return 2;
        
        // 32-bit formats
        case WGPUTextureFormat_R32Float:
        case WGPUTextureFormat_R32Uint:
        case WGPUTextureFormat_R32Sint:
        case WGPUTextureFormat_RG16Uint:
        case WGPUTextureFormat_RG16Sint:
        case WGPUTextureFormat_RG16Float:
        case WGPUTextureFormat_RGBA8Unorm:
        case WGPUTextureFormat_RGBA8UnormSrgb:
        case WGPUTextureFormat_RGBA8Snorm:
        case WGPUTextureFormat_RGBA8Uint:
        case WGPUTextureFormat_RGBA8Sint:
        case WGPUTextureFormat_BGRA8Unorm:
        case WGPUTextureFormat_BGRA8UnormSrgb:
        case WGPUTextureFormat_RGB10A2Uint:
        case WGPUTextureFormat_RGB10A2Unorm:
        case WGPUTextureFormat_RG11B10Ufloat:
        case WGPUTextureFormat_RGB9E5Ufloat:
        case WGPUTextureFormat_Depth32Float:
        case WGPUTextureFormat_Depth24Plus:
            return 4;
        
        // 48-bit formats (treat as 6 bytes)
        case WGPUTextureFormat_Depth24PlusStencil8:
        case WGPUTextureFormat_Depth32FloatStencil8:
            return 8;  // Actually variable, but return conservative estimate
        
        // 64-bit formats
        case WGPUTextureFormat_RG32Float:
        case WGPUTextureFormat_RG32Uint:
        case WGPUTextureFormat_RG32Sint:
        case WGPUTextureFormat_RGBA16Uint:
        case WGPUTextureFormat_RGBA16Sint:
        case WGPUTextureFormat_RGBA16Float:
            return 8;
        
        // 128-bit formats
        case WGPUTextureFormat_RGBA32Float:
        case WGPUTextureFormat_RGBA32Uint:
        case WGPUTextureFormat_RGBA32Sint:
            return 16;
        
        default:
            return 0;
    }
}

bool isDepthStencilFormat(WGPUTextureFormat format) noexcept {
    switch (format) {
        case WGPUTextureFormat_Depth16Unorm:
        case WGPUTextureFormat_Depth24Plus:
        case WGPUTextureFormat_Depth24PlusStencil8:
        case WGPUTextureFormat_Depth32Float:
        case WGPUTextureFormat_Depth32FloatStencil8:
            return true;
        default:
            return false;
    }
}

} // namespace voxy::gpu
