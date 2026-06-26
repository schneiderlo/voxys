// ═══════════════════════════════════════════════════════════════════════════════
// blit_path.hpp - Fullscreen Blit/Lighting Rendering Path (C++20)
// ═══════════════════════════════════════════════════════════════════════════════
// Implements the fullscreen lighting pass that composites the ray-caster output
// into a final shaded image.
// Features:
//   - Fullscreen triangle rendering (no vertex buffer)
//   - Depth texture sampling from ray-cast pass
//   - Terrain albedo and lightmap texture sampling
//   - Sky rendering with gradient
//   - Screen-space normal reconstruction
//   - Complete lighting model (diffuse, ambient, specular)
//   - Exponential fog
// ═══════════════════════════════════════════════════════════════════════════════

#pragma once

#include <cstdint>
#include <filesystem>

#include <glm/glm.hpp>

// WebGPU header - same API for native (wgpu-native) and WASM
#if defined(VOXY_WASM)
    #include <webgpu/webgpu.h>
#else
    #include <webgpu.h>
#endif

// Forward declaration for CameraUniforms (shared with triangle_path)
namespace voxy::render {
    struct CameraUniforms;
}

namespace voxy::render {

// ─────────────────────────────────────────────────────────────────────────────
// Blit Path Configuration
// ─────────────────────────────────────────────────────────────────────────────

/// Configuration for the blit path renderer
struct BlitPathConfig {
    std::filesystem::path shaderPath;    ///< Path to ray_blit.wgsl shader
    WGPUTextureFormat colorFormat;       ///< Output color format (default: BGRA8Unorm)
    float heightScale;                   ///< World-space height range
    float cellScale;                     ///< World-space size per heightmap cell
    float fogDensity;                    ///< Exponential fog density

    /// Default configuration
    static BlitPathConfig defaults() {
        return BlitPathConfig{
            .shaderPath = "shaders/ray_blit.wgsl",
            .colorFormat = WGPUTextureFormat_BGRA8Unorm,
            .heightScale = 500.0f,
            .cellScale = 1.0f,
            .fogDensity = 0.0001f
        };
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// Blit Path Renderer
// ─────────────────────────────────────────────────────────────────────────────

/// Fullscreen blit/lighting pass that composites ray-cast output into final image.
/// This pass takes the depth texture from the ray-caster and applies:
/// - Sky rendering for pixels with no terrain hit
/// - Position reconstruction from depth
/// - Screen-space normal reconstruction
/// - Terrain texture and lightmap sampling
/// - Lighting (diffuse, ambient, specular)
/// - Fog
class BlitPath {
public:
    BlitPath() = default;
    ~BlitPath();

    // Non-copyable
    BlitPath(const BlitPath&) = delete;
    BlitPath& operator=(const BlitPath&) = delete;

    // Movable
    BlitPath(BlitPath&& other) noexcept;
    BlitPath& operator=(BlitPath&& other) noexcept;

    // ─────────────────────────────────────────────────────────────────────────
    // Initialization
    // ─────────────────────────────────────────────────────────────────────────

    /// Initialize the blit path renderer
    /// @param device WebGPU device
    /// @param queue WebGPU queue
    /// @param config Renderer configuration
    /// @return true on success
    [[nodiscard]] bool init(WGPUDevice device, WGPUQueue queue,
                            const BlitPathConfig& config = BlitPathConfig::defaults());

    /// Check if initialized
    [[nodiscard]] bool isInitialized() const noexcept { return pipeline_ != nullptr; }

    /// Release all GPU resources
    void shutdown();

    // ─────────────────────────────────────────────────────────────────────────
    // Input Bindings
    // ─────────────────────────────────────────────────────────────────────────

    /// Set the depth texture from the ray-cast pass
    /// Must be called before render() and after init()
    /// @param depthView Texture view of R32Float depth from ray-caster
    void setDepthTexture(WGPUTextureView depthView);

    /// Set the shadow texture from the ray-cast pass
    /// @param shadowView Texture view of R32Float shadow visibility from ray-caster
    void setShadowTexture(WGPUTextureView shadowView);

    /// Set the terrain albedo texture
    /// @param terrainView Texture view of terrain color/albedo
    void setTerrainTexture(WGPUTextureView terrainView);

    /// Set the lightmap texture (ambient occlusion / sky visibility)
    /// @param lightmapView Texture view of lightmap
    void setLightmapTexture(WGPUTextureView lightmapView);

    /// Set terrain parameters (size, scale)
    /// @param width Heightmap width in samples
    /// @param height Heightmap height in samples
    void setTerrainSize(uint32_t width, uint32_t height);

    // ─────────────────────────────────────────────────────────────────────────
    // Debug Visualization
    // ─────────────────────────────────────────────────────────────────────────

    /// Set debug visualization mode
    /// @param mode 0=none, 1=depth, 2=normals, 3=mip_levels
    void setDebugMode(uint32_t mode);

    /// Get current debug mode
    [[nodiscard]] uint32_t getDebugMode() const noexcept { return debugMode_; }

    /// Set max depth for depth visualization
    void setDebugMaxDepth(float maxDepth);

    // ─────────────────────────────────────────────────────────────────────────
    // Rendering
    // ─────────────────────────────────────────────────────────────────────────

    /// Update camera uniforms
    /// @param view View matrix
    /// @param proj Projection matrix
    /// @param cameraPos World-space camera position
    void updateCamera(const glm::mat4& view, const glm::mat4& proj, const glm::vec3& cameraPos, float ambientIntensity = 0.3f);

    /// Set Lego Mode
    void setLegoMode(bool enabled);

    /// Copy a fully-built camera uniform block into this renderer
    void setCameraUniforms(const CameraUniforms& uniforms);

    /// Render the fullscreen blit pass
    /// @param encoder Command encoder
    /// @param colorView Output color attachment texture view (swapchain)
    void render(WGPUCommandEncoder encoder, WGPUTextureView colorView);

    // ─────────────────────────────────────────────────────────────────────────
    // Accessors
    // ─────────────────────────────────────────────────────────────────────────

    /// Get uniform buffer (for external updates if needed)
    [[nodiscard]] WGPUBuffer getUniformBuffer() const noexcept { return uniformBuffer_; }

    /// Get current camera uniforms (for debugging)
    [[nodiscard]] const CameraUniforms& getUniforms() const noexcept;

    /// Get the sampler used for terrain/lightmap textures
    [[nodiscard]] WGPUSampler getSampler() const noexcept { return sampler_; }

private:
    // ─────────────────────────────────────────────────────────────────────────
    // Internal Methods
    // ─────────────────────────────────────────────────────────────────────────

    bool createUniformBuffer();
    bool createSampler();
    bool createBindGroupLayout();
    bool createPipeline(const BlitPathConfig& config);
    bool createBindGroup();
    void updateUniformBuffer();

    // ─────────────────────────────────────────────────────────────────────────
    // GPU Resources
    // ─────────────────────────────────────────────────────────────────────────

    WGPUDevice device_ = nullptr;
    WGPUQueue queue_ = nullptr;

    // Pipeline resources
    WGPUShaderModule shaderModule_ = nullptr;
    WGPUPipelineLayout pipelineLayout_ = nullptr;
    WGPURenderPipeline pipeline_ = nullptr;

    // Bind group resources
    WGPUBindGroupLayout bindGroupLayout_ = nullptr;
    WGPUBindGroup bindGroup_ = nullptr;

    // Buffers
    WGPUBuffer uniformBuffer_ = nullptr;
    WGPUBuffer debugUniformBuffer_ = nullptr;

    // Sampler
    WGPUSampler sampler_ = nullptr;

    // Input textures (not owned)
    WGPUTextureView depthView_ = nullptr;
    WGPUTextureView shadowView_ = nullptr;
    WGPUTextureView terrainView_ = nullptr;
    WGPUTextureView lightmapView_ = nullptr;

    // Terrain parameters
    uint32_t terrainWidth_ = 256;
    uint32_t terrainHeight_ = 256;

    // State
    CameraUniforms* uniforms_ = nullptr;  // Pointer to heap-allocated uniforms
    BlitPathConfig config_ = BlitPathConfig::defaults();
    bool uniformsDirty_ = true;
    bool bindGroupDirty_ = true;
    
    // Debug visualization state
    uint32_t debugMode_ = 0;
    float debugMaxDepth_ = 5000.0f;
    bool debugUniformsDirty_ = true;
};

} // namespace voxy::render
