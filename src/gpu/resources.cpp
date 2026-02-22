// ═══════════════════════════════════════════════════════════════════════════════
// resources.cpp - WebGPU Resource Creation Helpers Implementation
// ═══════════════════════════════════════════════════════════════════════════════

#include "gpu/resources.hpp"
#include "core/log.hpp"

#include <fstream>
#include <sstream>
#include <cstring>

namespace voxy::gpu {

// ═══════════════════════════════════════════════════════════════════════════════
// Buffer Creation
// ═══════════════════════════════════════════════════════════════════════════════

WGPUBuffer createBuffer(WGPUDevice device, const BufferDesc& desc) {
    if (!device) {
        LOG_ERROR("Cannot create buffer: device is null");
        return nullptr;
    }
    
    if (desc.size == 0) {
        LOG_ERROR("Cannot create buffer: size is 0");
        return nullptr;
    }
    
    WGPUBufferDescriptor bufferDesc{};
    bufferDesc.nextInChain = nullptr;
    WGPU_SET_LABEL(bufferDesc, desc.label.empty() ? nullptr : desc.label.data());
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
    wgpuQueueWriteBuffer(queue, buffer, 0, data.data(), data.size());
    
    return buffer;
}

void writeBuffer(WGPUQueue queue, WGPUBuffer buffer, uint64_t offset,
                 std::span<const std::byte> data) {
    if (!queue || !buffer) {
        LOG_ERROR("Cannot write buffer: queue or buffer is null");
        return;
    }
    
    wgpuQueueWriteBuffer(queue, buffer, offset, data.data(), data.size());
}

// ═══════════════════════════════════════════════════════════════════════════════
// Texture Creation
// ═══════════════════════════════════════════════════════════════════════════════

WGPUTexture createTexture(WGPUDevice device, const TextureDesc& desc) {
    if (!device) {
        LOG_ERROR("Cannot create texture: device is null");
        return nullptr;
    }
    
    if (desc.width == 0 || desc.height == 0) {
        LOG_ERROR("Cannot create texture: invalid dimensions ({}x{})", 
                  desc.width, desc.height);
        return nullptr;
    }
    
    WGPUTextureDescriptor textureDesc{};
    textureDesc.nextInChain = nullptr;
    WGPU_SET_LABEL(textureDesc, desc.label.empty() ? nullptr : desc.label.data());
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
    writeTexture(queue, texture, data, desc.width, desc.height, bytesPerRow, 0);
    
    return texture;
}

/// WebGPU requires bytesPerRow to be aligned to 256 bytes for texture writes
constexpr uint32_t TEXTURE_BYTES_PER_ROW_ALIGNMENT = 256;

/// Align a value up to the given alignment
[[nodiscard]] constexpr uint32_t alignUp(uint32_t value, uint32_t alignment) noexcept {
    return (value + alignment - 1) & ~(alignment - 1);
}

void writeTexture(WGPUQueue queue, WGPUTexture texture,
                  std::span<const std::byte> data,
                  uint32_t width, uint32_t height, uint32_t bytesPerRow,
                  uint32_t mipLevel) {
    if (!queue || !texture) {
        LOG_ERROR("Cannot write texture: queue or texture is null");
        return;
    }
    
    CompatImageCopyTexture destination = makeTextureCopyDest(texture, mipLevel, {0, 0, 0});
    
    WGPUExtent3D writeSize{};
    writeSize.width = width;
    writeSize.height = height;
    writeSize.depthOrArrayLayers = 1;
    
    // WebGPU requires bytesPerRow to be a multiple of 256 bytes
    const uint32_t alignedBytesPerRow = alignUp(bytesPerRow, TEXTURE_BYTES_PER_ROW_ALIGNMENT);
    
    if (alignedBytesPerRow == bytesPerRow) {
        // Data is already aligned, upload directly
        CompatTextureDataLayout dataLayout = makeTextureDataLayout(0, bytesPerRow, height);
        
        wgpuQueueWriteTexture(queue, &destination, data.data(), data.size(),
                              &dataLayout, &writeSize);
    } else {
        // Need to copy data with padding to meet alignment requirements
        const size_t alignedDataSize = static_cast<size_t>(alignedBytesPerRow) * height;
        std::vector<std::byte> alignedData(alignedDataSize, std::byte{0});
        
        // Copy each row with proper alignment
        const std::byte* srcPtr = data.data();
        std::byte* dstPtr = alignedData.data();
        for (uint32_t row = 0; row < height; ++row) {
            std::memcpy(dstPtr, srcPtr, bytesPerRow);
            srcPtr += bytesPerRow;
            dstPtr += alignedBytesPerRow;
        }
        
        CompatTextureDataLayout dataLayout = makeTextureDataLayout(0, alignedBytesPerRow, height);
        
        wgpuQueueWriteTexture(queue, &destination, alignedData.data(), alignedData.size(),
                              &dataLayout, &writeSize);
        
        LOG_TRACE("Texture write required row alignment: {} -> {} bytes/row", 
                  bytesPerRow, alignedBytesPerRow);
    }
}

// ═══════════════════════════════════════════════════════════════════════════════
// Texture View Creation
// ═══════════════════════════════════════════════════════════════════════════════

WGPUTextureView createTextureView(WGPUTexture texture, const TextureViewDesc& desc) {
    if (!texture) {
        LOG_ERROR("Cannot create texture view: texture is null");
        return nullptr;
    }
    
    WGPUTextureViewDescriptor viewDesc{};
    viewDesc.nextInChain = nullptr;
    WGPU_SET_LABEL(viewDesc, desc.label.empty() ? nullptr : desc.label.data());
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

WGPUSampler createSampler(WGPUDevice device, const SamplerDesc& desc) {
    if (!device) {
        LOG_ERROR("Cannot create sampler: device is null");
        return nullptr;
    }
    
    WGPUSamplerDescriptor samplerDesc{};
    samplerDesc.nextInChain = nullptr;
    WGPU_SET_LABEL(samplerDesc, desc.label.empty() ? nullptr : desc.label.data());
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
    WGPU_SET_LABEL(layoutDesc, label.empty() ? nullptr : label.data());
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
    WGPU_SET_LABEL(bindGroupDesc, label.empty() ? nullptr : label.data());
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
    WGPU_SET_LABEL(moduleDesc, label.empty() ? nullptr : label.data());
#else
    // wgpu-native API
    WGPUShaderModuleWGSLDescriptor wgslDesc{};
    wgslDesc.chain.sType = WGPUSType_ShaderModuleWGSLDescriptor;
    wgslDesc.code = wgslSource.data();
    
    WGPUShaderModuleDescriptor moduleDesc{};
    moduleDesc.nextInChain = &wgslDesc.chain;
    WGPU_SET_LABEL(moduleDesc, label.empty() ? nullptr : label.data());
#endif
    
    WGPUShaderModule module = wgpuDeviceCreateShaderModule(device, &moduleDesc);
    
    if (!module) {
        LOG_ERROR("Failed to create shader module '{}'", label);
        return nullptr;
    }
    
    LOG_DEBUG("Created shader module '{}' ({} bytes)", label, wgslSource.size());
    return module;
}

WGPUShaderModule loadShaderModule(WGPUDevice device,
                                   const std::filesystem::path& path,
                                   std::string_view label) {
    std::ifstream file(path, std::ios::ate | std::ios::binary);
    if (!file.is_open()) {
        LOG_ERROR("Failed to open shader file: {}", path.string());
        return nullptr;
    }
    
    // Get file size and read content
    const auto size = file.tellg();
    file.seekg(0);
    
    std::string source;
    source.resize(static_cast<size_t>(size));
    file.read(source.data(), size);
    
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
    WGPU_SET_LABEL(layoutDesc, label.empty() ? nullptr : label.data());
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
            LOG_WARN("Unknown texture format {} - assuming 4 bytes per pixel", 
                     static_cast<int>(format));
            return 4;
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




