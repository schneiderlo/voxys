// ═══════════════════════════════════════════════════════════════════════════════
// mip_pipeline.hpp - GPU Max-Height Mip Chain Generation Pipeline (C++20)
// ═══════════════════════════════════════════════════════════════════════════════
// Provides GPU-accelerated generation of max-height mip chains for hierarchical
// ray-casting. Uses a compute shader to perform 2×2 max reduction per level.
// ═══════════════════════════════════════════════════════════════════════════════

#pragma once

#include <cstdint>
#include <filesystem>
#include <string_view>
#include <vector>

// WebGPU header - same API for native (wgpu-native) and WASM
#if defined(VOXY_WASM)
    #include <webgpu/webgpu.h>
#else
    #include <webgpu.h>
#endif

namespace voxy::render {

// ─────────────────────────────────────────────────────────────────────────────
// MipParams Structure (mirrors shader uniform)
// ─────────────────────────────────────────────────────────────────────────────

/// Parameters for mip generation compute shader
struct MipParams {
    uint32_t srcWidth;   ///< Source mip level width
    uint32_t srcHeight;  ///< Source mip level height
    uint32_t dstWidth;   ///< Destination mip level width
    uint32_t dstHeight;  ///< Destination mip level height
};

static_assert(sizeof(MipParams) == 16, "MipParams must be 16 bytes for GPU alignment");

// ─────────────────────────────────────────────────────────────────────────────
// MipGeneratorPipeline Class
// ─────────────────────────────────────────────────────────────────────────────

/// GPU pipeline for generating max-height mip chains.
/// 
/// This class manages the compute pipeline that generates mip levels for
/// heightmap textures. Each mip level stores the maximum height of its
/// corresponding 2×2 block from the previous level, enabling efficient
/// hierarchical ray-casting.
/// 
/// Usage:
/// ```cpp
/// MipGeneratorPipeline mipGen;
/// mipGen.init(device, "shaders/mip_generate.wgsl");
/// mipGen.generateMipChain(device, queue, heightmapTexture, mipLevelCount);
/// mipGen.shutdown();
/// ```
class MipGeneratorPipeline {
public:
    MipGeneratorPipeline() = default;
    ~MipGeneratorPipeline();
    
    // Non-copyable (due to GPU resources)
    MipGeneratorPipeline(const MipGeneratorPipeline&) = delete;
    MipGeneratorPipeline& operator=(const MipGeneratorPipeline&) = delete;
    
    // Movable
    MipGeneratorPipeline(MipGeneratorPipeline&& other) noexcept;
    MipGeneratorPipeline& operator=(MipGeneratorPipeline&& other) noexcept;
    
    // ─────────────────────────────────────────────────────────────────────────
    // Initialization
    // ─────────────────────────────────────────────────────────────────────────
    
    /// Initialize the mip generation pipeline
    /// @param device    WebGPU device
    /// @param shaderPath Path to mip_generate.wgsl shader file
    /// @return True if initialization succeeded
    [[nodiscard]] bool init(WGPUDevice device, const std::filesystem::path& shaderPath);
    
    /// Initialize with inline shader source (useful for embedded/WASM builds)
    /// @param device       WebGPU device
    /// @param shaderSource WGSL shader source code
    /// @return True if initialization succeeded
    [[nodiscard]] bool initWithSource(WGPUDevice device, std::string_view shaderSource);
    
    /// Check if the pipeline is initialized
    [[nodiscard]] bool isInitialized() const noexcept { return pipeline_ != nullptr; }
    
    /// Release all GPU resources
    void shutdown();
    
    // ─────────────────────────────────────────────────────────────────────────
    // Mip Generation
    // ─────────────────────────────────────────────────────────────────────────
    
    /// Generate a full mip chain for a heightmap texture.
    /// 
    /// The texture must be created with:
    /// - Format: R16Uint
    /// - Usage: TextureBinding | StorageBinding (for writing mips)
    /// - MipLevelCount > 1
    /// 
    /// @param device       WebGPU device
    /// @param queue        WebGPU queue (for submitting commands)
    /// @param texture      Heightmap texture with pre-allocated mip levels
    /// @param mipLevelCount Number of mip levels to generate (excluding base)
    /// @return True if mip generation succeeded
    [[nodiscard]] bool generateMipChain(WGPUDevice device, WGPUQueue queue,
                                         WGPUTexture texture, uint32_t mipLevelCount);
    
    /// Generate a single mip level from the previous level.
    /// 
    /// @param device        WebGPU device
    /// @param queue         WebGPU queue
    /// @param texture       Heightmap texture
    /// @param srcMipLevel   Source mip level index
    /// @param dstMipLevel   Destination mip level index (usually srcMipLevel + 1)
    /// @return True if mip generation succeeded
    [[nodiscard]] bool generateSingleMip(WGPUDevice device, WGPUQueue queue,
                                          WGPUTexture texture,
                                          uint32_t srcMipLevel, uint32_t dstMipLevel);
    
    // ─────────────────────────────────────────────────────────────────────────
    // Configuration
    // ─────────────────────────────────────────────────────────────────────────
    
    /// Get workgroup size (8×8 threads)
    static constexpr uint32_t getWorkgroupSizeX() noexcept { return 8; }
    static constexpr uint32_t getWorkgroupSizeY() noexcept { return 8; }
    
    /// Calculate dispatch dimensions for a given output size
    [[nodiscard]] static constexpr uint32_t calculateDispatchX(uint32_t width) noexcept {
        return (width + getWorkgroupSizeX() - 1) / getWorkgroupSizeX();
    }
    
    [[nodiscard]] static constexpr uint32_t calculateDispatchY(uint32_t height) noexcept {
        return (height + getWorkgroupSizeY() - 1) / getWorkgroupSizeY();
    }

private:
    /// Create bind group for a single mip generation pass
    [[nodiscard]] WGPUBindGroup createMipBindGroup(WGPUDevice device,
                                                    WGPUTextureView srcView,
                                                    WGPUTextureView dstView,
                                                    uint32_t srcWidth, uint32_t srcHeight,
                                                    uint32_t dstWidth, uint32_t dstHeight);
    
    // GPU resources
    WGPUShaderModule shaderModule_ = nullptr;
    WGPUComputePipeline pipeline_ = nullptr;
    WGPUBindGroupLayout bindGroupLayout_ = nullptr;
    WGPUPipelineLayout pipelineLayout_ = nullptr;
    WGPUBuffer paramsBuffer_ = nullptr;
};

// ─────────────────────────────────────────────────────────────────────────────
// Utility Functions
// ─────────────────────────────────────────────────────────────────────────────

/// Calculate the number of mip levels for given dimensions
[[nodiscard]] constexpr uint32_t calculateMipLevelCount(uint32_t width, uint32_t height) noexcept {
    uint32_t size = std::max(width, height);
    uint32_t count = 1;
    while (size > 1) {
        size >>= 1;
        count++;
    }
    return count;
}

/// Calculate dimensions for a specific mip level
[[nodiscard]] constexpr std::pair<uint32_t, uint32_t> 
getMipDimensions(uint32_t baseWidth, uint32_t baseHeight, uint32_t level) noexcept {
    return {
        std::max(1u, baseWidth >> level),
        std::max(1u, baseHeight >> level)
    };
}

} // namespace voxy::render



