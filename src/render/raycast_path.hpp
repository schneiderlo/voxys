// ═══════════════════════════════════════════════════════════════════════════════
// raycast_path.hpp - Compute Ray-Caster Rendering Path (C++20)
// ═══════════════════════════════════════════════════════════════════════════════
// Implements the primary terrain rendering path using hierarchical DDA ray-casting.
// Features:
//   - Compute shader ray-caster with 8×8 workgroups
//   - Hierarchical traversal using max-height mip pyramid
//   - R32Float depth output texture for compositing
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

    /// Dispatch the compute shader to ray-cast the terrain
    /// @param encoder Command encoder
    void dispatch(WGPUCommandEncoder encoder);

    // ─────────────────────────────────────────────────────────────────────────
    // Accessors
    // ─────────────────────────────────────────────────────────────────────────

    /// Get uniform buffer (for external updates if needed)
    [[nodiscard]] WGPUBuffer getUniformBuffer() const noexcept { return uniformBuffer_; }

    /// Get depth output texture view (for use in blit pass)
    [[nodiscard]] WGPUTextureView getDepthOutputView() const noexcept { return depthOutputView_; }

    /// Get depth output texture (for advanced usage)
    [[nodiscard]] WGPUTexture getDepthOutputTexture() const noexcept { return depthOutputTexture_; }

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

    // ─────────────────────────────────────────────────────────────────────────
    // GPU Resources
    // ─────────────────────────────────────────────────────────────────────────

    WGPUDevice device_ = nullptr;
    WGPUQueue queue_ = nullptr;

    // Pipeline resources
    WGPUShaderModule shaderModule_ = nullptr;
    WGPUPipelineLayout pipelineLayout_ = nullptr;
    WGPUComputePipeline pipeline_ = nullptr;

    // Bind group resources
    WGPUBindGroupLayout bindGroupLayout_ = nullptr;
    WGPUBindGroup bindGroup_ = nullptr;

    // Buffers
    WGPUBuffer uniformBuffer_ = nullptr;

    // Depth output texture
    WGPUTexture depthOutputTexture_ = nullptr;
    WGPUTextureView depthOutputView_ = nullptr;
    uint32_t outputWidth_ = 0;
    uint32_t outputHeight_ = 0;

    // Heightmap binding
    WGPUTextureView heightmapView_ = nullptr;
    uint32_t heightmapWidth_ = 0;
    uint32_t heightmapHeight_ = 0;

    // State
    CameraUniforms* uniforms_ = nullptr;  // Pointer to heap-allocated uniforms
    RaycastPathConfig config_ = RaycastPathConfig::defaults();
    bool uniformsDirty_ = true;
    bool bindGroupDirty_ = true;
};

} // namespace voxy::render

