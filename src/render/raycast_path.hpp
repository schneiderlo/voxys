// ═══════════════════════════════════════════════════════════════════════════════
// raycast_path.hpp - Compute Ray-Caster Rendering Path (C++20)
// ═══════════════════════════════════════════════════════════════════════════════
// Implements the primary terrain rendering path using hierarchical DDA ray-casting.
// Features:
//   - Compute shader ray-caster with 8×8 workgroups
//   - Hierarchical traversal using max-height mip pyramid
//   - R32Float depth/shadow plus RGBA16Float water attributes for compositing
//   - Distance-based LOD termination
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
// Raycast Path Configuration
// ─────────────────────────────────────────────────────────────────────────────

/// Configuration for the raycast path renderer
struct RaycastPathConfig {
    std::filesystem::path shaderPath;    ///< Path to terrain_raycast.wgsl shader
    float heightScale;                    ///< World-space height range
    float cellScale;                      ///< World-space size per heightmap cell
    float fogDensity;                     ///< Exponential fog density

    /// Default configuration
    static RaycastPathConfig defaults() {
        return RaycastPathConfig{
            .shaderPath = "shaders/terrain_raycast.wgsl",
            .heightScale = 500.0f,
            .cellScale = 1.0f,
            .fogDensity = 0.0001f
        };
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// Raycast Path Renderer
// ─────────────────────────────────────────────────────────────────────────────

/// Compute shader terrain ray-caster using hierarchical DDA traversal.
/// This is the primary rendering path for efficient large terrain rendering.
class RaycastPath {
public:
    /// Workgroup size for compute dispatch (matches shader)
    static constexpr uint32_t WORKGROUP_SIZE_X = 8;
    static constexpr uint32_t WORKGROUP_SIZE_Y = 8;

    RaycastPath() = default;
    ~RaycastPath();

    // Non-copyable
    RaycastPath(const RaycastPath&) = delete;
    RaycastPath& operator=(const RaycastPath&) = delete;

    // Movable
    RaycastPath(RaycastPath&& other) noexcept;
    RaycastPath& operator=(RaycastPath&& other) noexcept;

    // ─────────────────────────────────────────────────────────────────────────
    // Initialization
    // ─────────────────────────────────────────────────────────────────────────

    /// Initialize the raycast path renderer
    /// @param device WebGPU device
    /// @param queue WebGPU queue
    /// @param outputWidth Width of the depth output texture
    /// @param outputHeight Height of the depth output texture
    /// @param config Renderer configuration
    /// @return true on success
    [[nodiscard]] bool init(WGPUDevice device, WGPUQueue queue,
                            uint32_t outputWidth, uint32_t outputHeight,
                            const RaycastPathConfig& config = RaycastPathConfig::defaults());

    /// Check if initialized
    [[nodiscard]] bool isInitialized() const noexcept { return pipeline_ != nullptr; }

    /// Release all GPU resources
    void shutdown();

    // ─────────────────────────────────────────────────────────────────────────
    // Resize
    // ─────────────────────────────────────────────────────────────────────────

    /// Resize the depth output texture
    /// @param width New width
    /// @param height New height
    /// @return true on success
    [[nodiscard]] bool resize(uint32_t width, uint32_t height);

    /// Get current output dimensions
    [[nodiscard]] uint32_t getOutputWidth() const noexcept { return outputWidth_; }
    [[nodiscard]] uint32_t getOutputHeight() const noexcept { return outputHeight_; }

    // ─────────────────────────────────────────────────────────────────────────
    // Heightmap Binding
    // ─────────────────────────────────────────────────────────────────────────

    /// Set the heightmap texture to ray-cast against
    /// Must be called before dispatch() and after init()
    /// @param heightmapView Texture view of R16Uint heightmap with mip chain
    /// @param width Heightmap width in samples
    /// @param height Heightmap height in samples
    void setHeightmap(WGPUTextureView heightmapView, uint32_t width, uint32_t height);

    /// Set the baked shadow height field texture (R16Uint, see shadow_bake.hpp).
    /// Optional: without it a 1x1 zero fallback is bound (everything lit).
    void setShadowMap(WGPUTextureView shadowMapView);

    /// Bind the shared FFT displacement cascades.
    void setWaterSimulation(WGPUTextureView displacementView,
                            WGPUTextureView coastView,
                            WGPUSampler sampler);

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

    /// Dispatch the compute shader to ray-cast the terrain
    /// @param encoder Command encoder
    void dispatch(
        WGPUCommandEncoder encoder,
        WGPUQuerySet timestampQuerySet = nullptr,
        uint32_t timestampBegin = WGPU_QUERY_SET_INDEX_UNDEFINED,
        uint32_t timestampEnd = WGPU_QUERY_SET_INDEX_UNDEFINED);

    // ─────────────────────────────────────────────────────────────────────────
    // Accessors
    // ─────────────────────────────────────────────────────────────────────────

    /// Get uniform buffer (for external updates if needed)
    [[nodiscard]] WGPUBuffer getUniformBuffer() const noexcept { return uniformBuffer_; }

    /// Get depth output texture view (for use in blit pass)
    [[nodiscard]] WGPUTextureView getDepthOutputView() const noexcept { return depthOutputView_; }

    /// Get depth output texture (for advanced usage)
    [[nodiscard]] WGPUTexture getDepthOutputTexture() const noexcept { return depthOutputTexture_; }

    /// Get shadow output texture view (for use in blit pass)
    [[nodiscard]] WGPUTextureView getShadowOutputView() const noexcept { return shadowOutputView_; }

    /// Get shadow output texture (for advanced usage)
    [[nodiscard]] WGPUTexture getShadowOutputTexture() const noexcept { return shadowOutputTexture_; }

    /// Get material output texture view (for use in blit pass)
    [[nodiscard]] WGPUTextureView getMaterialOutputView() const noexcept { return materialOutputView_; }

    /// Get material output texture (for advanced usage)
    [[nodiscard]] WGPUTexture getMaterialOutputTexture() const noexcept { return materialOutputTexture_; }

    /// Get the camera-static terrain depth cache used by the water composite.
    [[nodiscard]] WGPUTextureView getTerrainDepthCacheView() const noexcept {
        return terrainDepthCacheView_;
    }

    /// Get the matching camera-static terrain shadow cache.
    [[nodiscard]] WGPUTextureView getTerrainShadowCacheView() const noexcept {
        return terrainShadowCacheView_;
    }

    /// True when this frame used the settled-camera terrain cache.
    [[nodiscard]] bool isUsingStaticCache() const noexcept {
        return usingStaticCache_;
    }

    /// True only on a frame which refreshed the settled-camera terrain cache.
    [[nodiscard]] bool didRefreshStaticCache() const noexcept {
        return staticCacheRefreshed_;
    }

    /// Get current camera uniforms (for debugging)
    [[nodiscard]] const CameraUniforms& getUniforms() const noexcept;

    /// Get number of workgroups that will be dispatched
    [[nodiscard]] uint32_t getWorkgroupCountX() const noexcept;
    [[nodiscard]] uint32_t getWorkgroupCountY() const noexcept;

private:
    // ─────────────────────────────────────────────────────────────────────────
    // Internal Methods
    // ─────────────────────────────────────────────────────────────────────────

    bool createDepthOutputTexture();
    bool createUniformBuffer();
    bool createBindGroupLayout();
    bool createPipeline(const RaycastPathConfig& config);
    bool createBindGroup();
    void updateUniformBuffer();
    void updateStaticUniforms();

    // ─────────────────────────────────────────────────────────────────────────
    // GPU Resources
    // ─────────────────────────────────────────────────────────────────────────

    WGPUDevice device_ = nullptr;
    WGPUQueue queue_ = nullptr;

    // Pipeline resources
    WGPUShaderModule shaderModule_ = nullptr;
    WGPUPipelineLayout pipelineLayout_ = nullptr;
    WGPUComputePipeline pipeline_ = nullptr;
    WGPUShaderModule compositeShaderModule_ = nullptr;
    WGPUPipelineLayout compositePipelineLayout_ = nullptr;
    WGPUComputePipeline compositePipeline_ = nullptr;

    // Bind group resources
    WGPUBindGroupLayout bindGroupLayout_ = nullptr;
    WGPUBindGroup bindGroup_ = nullptr;
    WGPUBindGroup staticBindGroup_ = nullptr;
    WGPUBindGroupLayout compositeBindGroupLayout_ = nullptr;
    WGPUBindGroup compositeBindGroup_ = nullptr;

    // Buffers
    WGPUBuffer uniformBuffer_ = nullptr;
    WGPUBuffer staticUniformBuffer_ = nullptr;

    // Output textures
    WGPUTexture depthOutputTexture_ = nullptr;
    WGPUTextureView depthOutputView_ = nullptr;
    WGPUTexture shadowOutputTexture_ = nullptr;
    WGPUTextureView shadowOutputView_ = nullptr;
    WGPUTexture materialOutputTexture_ = nullptr;
    WGPUTextureView materialOutputView_ = nullptr;
    WGPUTexture terrainDepthCacheTexture_ = nullptr;
    WGPUTextureView terrainDepthCacheView_ = nullptr;
    WGPUTexture terrainShadowCacheTexture_ = nullptr;
    WGPUTextureView terrainShadowCacheView_ = nullptr;
    uint32_t outputWidth_ = 0;
    uint32_t outputHeight_ = 0;

    // Heightmap binding
    WGPUTextureView heightmapView_ = nullptr;
    uint32_t heightmapWidth_ = 0;
    uint32_t heightmapHeight_ = 0;

    // Baked shadow height field binding (fallback = 1x1 zero, everything lit)
    WGPUTextureView shadowMapView_ = nullptr;
    WGPUTexture fallbackShadowTexture_ = nullptr;
    WGPUTextureView fallbackShadowView_ = nullptr;

    // Borrowed from WaterSimulation.
    WGPUTextureView waterDisplacementView_ = nullptr;
    WGPUTextureView waterCoastView_ = nullptr;
    WGPUSampler waterDisplacementSampler_ = nullptr;

    // State
    CameraUniforms* uniforms_ = nullptr;  // Pointer to heap-allocated uniforms
    CameraUniforms* staticUniforms_ = nullptr;
    RaycastPathConfig config_ = RaycastPathConfig::defaults();
    bool uniformsDirty_ = true;
    bool staticUniformsDirty_ = true;
    bool staticCacheDirty_ = true;
    bool staticStateChangedSinceDispatch_ = true;
    bool usingStaticCache_ = false;
    bool staticCacheRefreshed_ = false;
    bool bindGroupDirty_ = true;
};

} // namespace voxy::render
