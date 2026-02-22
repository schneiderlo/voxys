// ═══════════════════════════════════════════════════════════════════════════════
// resources.hpp - WebGPU Resource Creation Helpers (C++20)
// ═══════════════════════════════════════════════════════════════════════════════
// Provides convenient factory functions and descriptor builders for creating
// WebGPU resources: buffers, textures, samplers, bind groups, and shader modules.
// ═══════════════════════════════════════════════════════════════════════════════

#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>
#include <optional>
#include <filesystem>

// WebGPU compatibility layer - handles API differences between implementations
#include "webgpu_compat.hpp"

namespace voxy::gpu {

// ═══════════════════════════════════════════════════════════════════════════════
// Buffer Creation
// ═══════════════════════════════════════════════════════════════════════════════

/// Buffer usage flags for common use cases
enum class BufferUsage : uint32_t {
    Vertex        = WGPUBufferUsage_Vertex,
    Index         = WGPUBufferUsage_Index,
    Uniform       = WGPUBufferUsage_Uniform,
    Storage       = WGPUBufferUsage_Storage,
    CopySrc       = WGPUBufferUsage_CopySrc,
    CopyDst       = WGPUBufferUsage_CopyDst,
    Indirect      = WGPUBufferUsage_Indirect,
    QueryResolve  = WGPUBufferUsage_QueryResolve,
    MapRead       = WGPUBufferUsage_MapRead,
    MapWrite      = WGPUBufferUsage_MapWrite,
};

/// Combine buffer usage flags
[[nodiscard]] constexpr WGPUBufferUsageFlags operator|(BufferUsage a, BufferUsage b) noexcept {
    return static_cast<WGPUBufferUsageFlags>(a) | static_cast<WGPUBufferUsageFlags>(b);
}

[[nodiscard]] constexpr WGPUBufferUsageFlags operator|(WGPUBufferUsageFlags a, BufferUsage b) noexcept {
    return a | static_cast<WGPUBufferUsageFlags>(b);
}

/// Configuration for buffer creation
struct BufferDesc {
    std::string_view label = "";
    uint64_t size = 0;
    WGPUBufferUsageFlags usage = WGPUBufferUsage_None;
    bool mappedAtCreation = false;
    
    /// Create a uniform buffer descriptor
    [[nodiscard]] static BufferDesc uniform(uint64_t size, std::string_view label = "") noexcept {
        return BufferDesc{
            .label = label,
            .size = size,
            .usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst
        };
    }
    
    /// Create a vertex buffer descriptor
    [[nodiscard]] static BufferDesc vertex(uint64_t size, std::string_view label = "") noexcept {
        return BufferDesc{
            .label = label,
            .size = size,
            .usage = WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst
        };
    }
    
    /// Create an index buffer descriptor
    [[nodiscard]] static BufferDesc index(uint64_t size, std::string_view label = "") noexcept {
        return BufferDesc{
            .label = label,
            .size = size,
            .usage = WGPUBufferUsage_Index | WGPUBufferUsage_CopyDst
        };
    }
    
    /// Create a storage buffer descriptor
    [[nodiscard]] static BufferDesc storage(uint64_t size, bool readOnly = false, 
                                            std::string_view label = "") noexcept {
        return BufferDesc{
            .label = label,
            .size = size,
            .usage = static_cast<WGPUBufferUsageFlags>(
                WGPUBufferUsage_Storage | WGPUBufferUsage_CopyDst | 
                (readOnly ? 0 : WGPUBufferUsage_CopySrc))
        };
    }
    
    /// Create a staging buffer for uploads (mapped at creation)
    [[nodiscard]] static BufferDesc staging(uint64_t size, std::string_view label = "") noexcept {
        return BufferDesc{
            .label = label,
            .size = size,
            .usage = WGPUBufferUsage_CopySrc | WGPUBufferUsage_MapWrite,
            .mappedAtCreation = true
        };
    }
};

/// Create a GPU buffer
/// @param device The WebGPU device
/// @param desc Buffer descriptor
/// @return The created buffer, or nullptr on failure
[[nodiscard]] WGPUBuffer createBuffer(WGPUDevice device, const BufferDesc& desc);

/// Create a buffer and immediately upload data to it
/// @param device The WebGPU device
/// @param queue The WebGPU queue
/// @param desc Buffer descriptor
/// @param data Data to upload (size must match desc.size)
/// @return The created buffer, or nullptr on failure
[[nodiscard]] WGPUBuffer createBufferWithData(WGPUDevice device, WGPUQueue queue,
                                               const BufferDesc& desc, 
                                               std::span<const std::byte> data);

/// Templated version for typed data
template<typename T>
[[nodiscard]] WGPUBuffer createBufferWithData(WGPUDevice device, WGPUQueue queue,
                                               const BufferDesc& desc,
                                               std::span<const T> data) {
    return createBufferWithData(device, queue, desc, 
        std::span<const std::byte>(reinterpret_cast<const std::byte*>(data.data()), 
                                    data.size_bytes()));
}

/// Write data to an existing buffer
void writeBuffer(WGPUQueue queue, WGPUBuffer buffer, uint64_t offset, 
                 std::span<const std::byte> data);

/// Templated version for typed data
template<typename T>
void writeBuffer(WGPUQueue queue, WGPUBuffer buffer, uint64_t offset, const T& data) {
    writeBuffer(queue, buffer, offset, 
        std::span<const std::byte>(reinterpret_cast<const std::byte*>(&data), sizeof(T)));
}

// ═══════════════════════════════════════════════════════════════════════════════
// Texture Creation
// ═══════════════════════════════════════════════════════════════════════════════

/// Configuration for texture creation
struct TextureDesc {
    std::string_view label = "";
    uint32_t width = 1;
    uint32_t height = 1;
    uint32_t depthOrArrayLayers = 1;
    uint32_t mipLevelCount = 1;
    uint32_t sampleCount = 1;
    WGPUTextureDimension dimension = WGPUTextureDimension_2D;
    WGPUTextureFormat format = WGPUTextureFormat_RGBA8Unorm;
    WGPUTextureUsageFlags usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst;
    
    /// Create a 2D texture descriptor
    [[nodiscard]] static TextureDesc tex2D(uint32_t width, uint32_t height, 
                                            WGPUTextureFormat format,
                                            WGPUTextureUsageFlags usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst,
                                            std::string_view label = "") noexcept {
        return TextureDesc{
            .label = label,
            .width = width,
            .height = height,
            .format = format,
            .usage = usage
        };
    }
    
    /// Create a 2D texture descriptor with mip chain
    [[nodiscard]] static TextureDesc tex2DMipmapped(uint32_t width, uint32_t height,
                                                     WGPUTextureFormat format,
                                                     WGPUTextureUsageFlags usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst,
                                                     std::string_view label = "") noexcept {
        uint32_t mipCount = 1;
        uint32_t size = std::max(width, height);
        while (size > 1) {
            size >>= 1;
            mipCount++;
        }
        return TextureDesc{
            .label = label,
            .width = width,
            .height = height,
            .mipLevelCount = mipCount,
            .format = format,
            .usage = usage
        };
    }
    
    /// Create a depth texture descriptor
    [[nodiscard]] static TextureDesc depth(uint32_t width, uint32_t height,
                                            WGPUTextureFormat format = WGPUTextureFormat_Depth32Float,
                                            std::string_view label = "") noexcept {
        return TextureDesc{
            .label = label,
            .width = width,
            .height = height,
            .format = format,
            .usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_TextureBinding
        };
    }
    
    /// Create a storage texture descriptor (for compute shaders)
    [[nodiscard]] static TextureDesc storage(uint32_t width, uint32_t height,
                                              WGPUTextureFormat format,
                                              std::string_view label = "") noexcept {
        return TextureDesc{
            .label = label,
            .width = width,
            .height = height,
            .format = format,
            .usage = WGPUTextureUsage_StorageBinding | WGPUTextureUsage_TextureBinding
        };
    }
    
    /// Create a render target texture descriptor
    [[nodiscard]] static TextureDesc renderTarget(uint32_t width, uint32_t height,
                                                   WGPUTextureFormat format,
                                                   std::string_view label = "") noexcept {
        return TextureDesc{
            .label = label,
            .width = width,
            .height = height,
            .format = format,
            .usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_TextureBinding
        };
    }
};

/// Create a GPU texture
/// @param device The WebGPU device
/// @param desc Texture descriptor
/// @return The created texture, or nullptr on failure
[[nodiscard]] WGPUTexture createTexture(WGPUDevice device, const TextureDesc& desc);

/// Create a texture and immediately upload data to it
/// @param device The WebGPU device
/// @param queue The WebGPU queue
/// @param desc Texture descriptor
/// @param data Pixel data to upload
/// @param bytesPerRow Bytes per row in the source data
/// @return The created texture, or nullptr on failure
[[nodiscard]] WGPUTexture createTextureWithData(WGPUDevice device, WGPUQueue queue,
                                                 const TextureDesc& desc,
                                                 std::span<const std::byte> data,
                                                 uint32_t bytesPerRow);

/// Write data to an existing texture
void writeTexture(WGPUQueue queue, WGPUTexture texture, 
                  std::span<const std::byte> data,
                  uint32_t width, uint32_t height, uint32_t bytesPerRow,
                  uint32_t mipLevel = 0);

// ═══════════════════════════════════════════════════════════════════════════════
// Texture View Creation
// ═══════════════════════════════════════════════════════════════════════════════

/// Configuration for texture view creation
struct TextureViewDesc {
    std::string_view label = "";
    WGPUTextureFormat format = WGPUTextureFormat_Undefined;  // Use texture format if Undefined
    WGPUTextureViewDimension dimension = WGPUTextureViewDimension_2D;
    uint32_t baseMipLevel = 0;
    uint32_t mipLevelCount = 1;  // WGPU_MIP_LEVEL_COUNT_UNDEFINED for all
    uint32_t baseArrayLayer = 0;
    uint32_t arrayLayerCount = 1;
    WGPUTextureAspect aspect = WGPUTextureAspect_All;
};

/// Create a texture view
/// @param texture The source texture
/// @param desc View descriptor (optional - uses defaults if not provided)
/// @return The created texture view, or nullptr on failure
[[nodiscard]] WGPUTextureView createTextureView(WGPUTexture texture, 
                                                 const TextureViewDesc& desc = {});

/// Create a texture view for a specific mip level
[[nodiscard]] WGPUTextureView createMipView(WGPUTexture texture, uint32_t mipLevel,
                                             WGPUTextureFormat format = WGPUTextureFormat_Undefined);

// ═══════════════════════════════════════════════════════════════════════════════
// Sampler Creation
// ═══════════════════════════════════════════════════════════════════════════════

/// Configuration for sampler creation
struct SamplerDesc {
    std::string_view label = "";
    WGPUAddressMode addressModeU = WGPUAddressMode_ClampToEdge;
    WGPUAddressMode addressModeV = WGPUAddressMode_ClampToEdge;
    WGPUAddressMode addressModeW = WGPUAddressMode_ClampToEdge;
    WGPUFilterMode magFilter = WGPUFilterMode_Linear;
    WGPUFilterMode minFilter = WGPUFilterMode_Linear;
    WGPUMipmapFilterMode mipmapFilter = WGPUMipmapFilterMode_Linear;
    float lodMinClamp = 0.0f;
    float lodMaxClamp = 32.0f;
    WGPUCompareFunction compare = WGPUCompareFunction_Undefined;
    uint16_t maxAnisotropy = 1;
    
    /// Create a linear filtering sampler (default)
    [[nodiscard]] static SamplerDesc linear(std::string_view label = "") noexcept {
        return SamplerDesc{.label = label};
    }
    
    /// Create a nearest-neighbor (point) sampler
    [[nodiscard]] static SamplerDesc nearest(std::string_view label = "") noexcept {
        return SamplerDesc{
            .label = label,
            .magFilter = WGPUFilterMode_Nearest,
            .minFilter = WGPUFilterMode_Nearest,
            .mipmapFilter = WGPUMipmapFilterMode_Nearest
        };
    }
    
    /// Create a repeating sampler
    [[nodiscard]] static SamplerDesc repeat(WGPUFilterMode filter = WGPUFilterMode_Linear,
                                             std::string_view label = "") noexcept {
        return SamplerDesc{
            .label = label,
            .addressModeU = WGPUAddressMode_Repeat,
            .addressModeV = WGPUAddressMode_Repeat,
            .addressModeW = WGPUAddressMode_Repeat,
            .magFilter = filter,
            .minFilter = filter
        };
    }
    
    /// Create a comparison sampler (for shadow mapping)
    [[nodiscard]] static SamplerDesc comparison(WGPUCompareFunction compare = WGPUCompareFunction_Less,
                                                 std::string_view label = "") noexcept {
        return SamplerDesc{
            .label = label,
            .compare = compare
        };
    }
    
    /// Create an anisotropic sampler
    [[nodiscard]] static SamplerDesc anisotropic(uint16_t maxAnisotropy = 16,
                                                  std::string_view label = "") noexcept {
        return SamplerDesc{
            .label = label,
            .maxAnisotropy = maxAnisotropy
        };
    }
};

/// Create a GPU sampler
/// @param device The WebGPU device
/// @param desc Sampler descriptor
/// @return The created sampler, or nullptr on failure
[[nodiscard]] WGPUSampler createSampler(WGPUDevice device, const SamplerDesc& desc);

// ═══════════════════════════════════════════════════════════════════════════════
// Bind Group Layout Creation
// ═══════════════════════════════════════════════════════════════════════════════

/// Bind group layout entry builder (fluent API)
class BindGroupLayoutEntry {
public:
    explicit BindGroupLayoutEntry(uint32_t binding) noexcept;
    
    /// Set visibility to specific shader stages
    BindGroupLayoutEntry& visibility(WGPUShaderStageFlags stages) noexcept;
    
    /// Convenience: visible to vertex shader
    BindGroupLayoutEntry& vertexVisible() noexcept;
    
    /// Convenience: visible to fragment shader
    BindGroupLayoutEntry& fragmentVisible() noexcept;
    
    /// Convenience: visible to compute shader
    BindGroupLayoutEntry& computeVisible() noexcept;
    
    /// Convenience: visible to all shader stages
    BindGroupLayoutEntry& allStagesVisible() noexcept;
    
    /// Configure as a uniform buffer binding
    BindGroupLayoutEntry& uniformBuffer(bool hasDynamicOffset = false,
                                          uint64_t minBindingSize = 0) noexcept;
    
    /// Configure as a storage buffer binding
    BindGroupLayoutEntry& storageBuffer(bool readOnly = false,
                                          bool hasDynamicOffset = false,
                                          uint64_t minBindingSize = 0) noexcept;
    
    /// Configure as a sampled texture binding
    BindGroupLayoutEntry& texture(WGPUTextureSampleType sampleType = WGPUTextureSampleType_Float,
                                   WGPUTextureViewDimension viewDimension = WGPUTextureViewDimension_2D,
                                   bool multisampled = false) noexcept;
    
    /// Configure as a storage texture binding
    BindGroupLayoutEntry& storageTexture(WGPUStorageTextureAccess access,
                                          WGPUTextureFormat format,
                                          WGPUTextureViewDimension viewDimension = WGPUTextureViewDimension_2D) noexcept;
    
    /// Configure as a sampler binding
    BindGroupLayoutEntry& sampler(WGPUSamplerBindingType type = WGPUSamplerBindingType_Filtering) noexcept;
    
    /// Get the raw WebGPU entry
    [[nodiscard]] const WGPUBindGroupLayoutEntry& get() const noexcept { return entry_; }
    
private:
    WGPUBindGroupLayoutEntry entry_{};
};

/// Create a bind group layout from entries
/// @param device The WebGPU device
/// @param entries Layout entries
/// @param label Optional label
/// @return The created bind group layout, or nullptr on failure
[[nodiscard]] WGPUBindGroupLayout createBindGroupLayout(
    WGPUDevice device,
    std::span<const BindGroupLayoutEntry> entries,
    std::string_view label = "");

/// Overload accepting raw WGPUBindGroupLayoutEntry array
[[nodiscard]] WGPUBindGroupLayout createBindGroupLayout(
    WGPUDevice device,
    std::span<const WGPUBindGroupLayoutEntry> entries,
    std::string_view label = "");

// ═══════════════════════════════════════════════════════════════════════════════
// Bind Group Creation
// ═══════════════════════════════════════════════════════════════════════════════

/// Bind group entry builder (fluent API)
class BindGroupEntry {
public:
    explicit BindGroupEntry(uint32_t binding) noexcept;
    
    /// Bind a buffer (or buffer range)
    BindGroupEntry& buffer(WGPUBuffer buffer, uint64_t offset = 0, 
                           uint64_t size = WGPU_WHOLE_SIZE) noexcept;
    
    /// Bind a sampler
    BindGroupEntry& sampler(WGPUSampler sampler) noexcept;
    
    /// Bind a texture view
    BindGroupEntry& textureView(WGPUTextureView textureView) noexcept;
    
    /// Get the raw WebGPU entry
    [[nodiscard]] const WGPUBindGroupEntry& get() const noexcept { return entry_; }
    
private:
    WGPUBindGroupEntry entry_{};
};

/// Create a bind group from entries
/// @param device The WebGPU device
/// @param layout The bind group layout
/// @param entries Bind group entries
/// @param label Optional label
/// @return The created bind group, or nullptr on failure
[[nodiscard]] WGPUBindGroup createBindGroup(
    WGPUDevice device,
    WGPUBindGroupLayout layout,
    std::span<const BindGroupEntry> entries,
    std::string_view label = "");

/// Overload accepting raw WGPUBindGroupEntry array
[[nodiscard]] WGPUBindGroup createBindGroup(
    WGPUDevice device,
    WGPUBindGroupLayout layout,
    std::span<const WGPUBindGroupEntry> entries,
    std::string_view label = "");

// ═══════════════════════════════════════════════════════════════════════════════
// Shader Module Creation
// ═══════════════════════════════════════════════════════════════════════════════

/// Create a shader module from WGSL source code
/// @param device The WebGPU device
/// @param wgslSource WGSL shader source code
/// @param label Optional label for debugging
/// @return The created shader module, or nullptr on failure
[[nodiscard]] WGPUShaderModule createShaderModule(WGPUDevice device,
                                                   std::string_view wgslSource,
                                                   std::string_view label = "");

/// Load a shader module from a file
/// @param device The WebGPU device
/// @param path Path to the WGSL shader file
/// @param label Optional label (uses filename if empty)
/// @return The created shader module, or nullptr on failure
[[nodiscard]] WGPUShaderModule loadShaderModule(WGPUDevice device,
                                                 const std::filesystem::path& path,
                                                 std::string_view label = "");

// ═══════════════════════════════════════════════════════════════════════════════
// Pipeline Layout Creation
// ═══════════════════════════════════════════════════════════════════════════════

/// Create a pipeline layout from bind group layouts
/// @param device The WebGPU device
/// @param bindGroupLayouts Array of bind group layouts
/// @param label Optional label
/// @return The created pipeline layout, or nullptr on failure
[[nodiscard]] WGPUPipelineLayout createPipelineLayout(
    WGPUDevice device,
    std::span<const WGPUBindGroupLayout> bindGroupLayouts,
    std::string_view label = "");

// ═══════════════════════════════════════════════════════════════════════════════
// Utility Functions
// ═══════════════════════════════════════════════════════════════════════════════

/// Get bytes per pixel for a texture format
[[nodiscard]] uint32_t getBytesPerPixel(WGPUTextureFormat format) noexcept;

/// Check if a format is a depth/stencil format
[[nodiscard]] bool isDepthStencilFormat(WGPUTextureFormat format) noexcept;

/// Calculate aligned size for uniform buffers (WebGPU requires 256-byte alignment)
[[nodiscard]] constexpr uint64_t alignUniformBufferSize(uint64_t size) noexcept {
    constexpr uint64_t alignment = 256;
    return (size + alignment - 1) & ~(alignment - 1);
}

/// Calculate mip level count for a texture dimension
[[nodiscard]] constexpr uint32_t calculateMipLevelCount(uint32_t width, uint32_t height) noexcept {
    uint32_t size = std::max(width, height);
    uint32_t count = 1;
    while (size > 1) {
        size >>= 1;
        count++;
    }
    return count;
}

} // namespace voxy::gpu




