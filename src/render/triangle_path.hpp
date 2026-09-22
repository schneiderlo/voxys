// ═══════════════════════════════════════════════════════════════════════════════
// triangle_path.hpp - Triangle Terrain Rendering Path (C++20)
// ═══════════════════════════════════════════════════════════════════════════════
// Implements the baseline triangle mesh terrain renderer using tiled instancing.
// Features:
//   - 64×64 quad tiles rendered via instancing
//   - Procedural vertex generation (no vertex buffer)
//   - Index buffer for efficient triangle rendering
//   - Height sampling from R16Uint texture
//   - Configurable LOD via step parameter
// ═══════════════════════════════════════════════════════════════════════════════

#pragma once

#include <cstdint>
#include <array>
#include <string_view>
#include <filesystem>
#include <span>

#include <glm/glm.hpp>

// WebGPU header - same API for native (wgpu-native) and WASM
#if defined(VOXY_WASM)
    #include <webgpu/webgpu.h>
#else
    #include <webgpu.h>
#endif

namespace voxy::render {

// ─────────────────────────────────────────────────────────────────────────────
// Camera Uniforms (Shared Structure)
// ─────────────────────────────────────────────────────────────────────────────

/// Unified camera uniforms structure shared between all shaders.
/// Must match the WGSL CameraUniforms struct exactly.
/// Total size: 544 bytes (aligned to 16 bytes)
struct CameraUniforms {
    glm::mat4 viewProj;        ///< offset: 0,   size: 64 - View-projection matrix
    glm::mat4 invViewProj;     ///< offset: 64,  size: 64 - Inverse view-projection
    glm::mat4 invView;         ///< offset: 128, size: 64 - Inverse view matrix
    glm::vec2 terrainSize;     ///< offset: 192, size: 8  - Heightmap dimensions
    glm::vec2 invTerrainSize;  ///< offset: 200, size: 8  - 1.0 / terrainSize
    glm::vec4 metrics;         ///< offset: 208, size: 16 - (heightScale, cellScale, step, fogDensity)
    glm::vec4 cameraPos;       ///< offset: 224, size: 16 - World-space camera position (.xyz)
    glm::vec4 invProjParams;   ///< offset: 240, size: 16 - Inverse projection params (.xy used)
    glm::vec4 lightDirVS;      ///< offset: 256, size: 16 - View-space light direction (.xyz)
    glm::vec4 frustumPlanes[6]; ///< offset: 272, size: 96 - Frustum planes
    glm::vec4 lightDirWS;      ///< offset: 368, size: 16 - World-space light direction (.xyz)
    glm::vec4 waterParams;     ///< offset: 384, size: 16 - (height, enabled, waveStrength, roughness)
    glm::vec4 waterColorA;     ///< offset: 400, size: 16 - shallow color rgb, reflection strength
    glm::vec4 waterColorB;     ///< offset: 416, size: 16 - deep color rgb, shore fade
    glm::vec4 waterMotion;     ///< offset: 432, size: 16 - (time, local surface offset, submerged, reserved)
    glm::vec4 lightingColor;   ///< offset: 448, size: 16 - sun rgb, intensity
    glm::vec4 ambientExposure; ///< offset: 464, size: 16 - ambient rgb, exposure
    glm::vec4 fogColor;        ///< offset: 480, size: 16 - fog rgb, day hour + 1 (zero = fixed sky)
    glm::vec4 waterOptics;     ///< offset: 496, size: 16 - IOR, distortion, absorption, scatter
    glm::vec4 waterFoam;       ///< offset: 512, size: 16 - size, opacity, coverage, reflection distance
    glm::vec4 waterSpectrum;   ///< offset: 528, size: 16 - broad/detail patch lengths
    // Total: 544 bytes

    /// Default constructor with sensible defaults
    CameraUniforms();

    /// Configure terrain parameters
    [[nodiscard]] bool setTerrain(
        uint32_t width, uint32_t height, float heightScale = 500.0f,
        float cellScale = 1.0f, float step = 1.0f,
        float fogDensity = 0.0001f);

    /// Update camera matrices from view and projection matrices
    [[nodiscard]] bool setCamera(
        const glm::mat4& view, const glm::mat4& proj,
        const glm::vec3& position);

    /// Set light direction in world space (will be converted to view space internally)
    /// Also sets ambient intensity in the w component
    [[nodiscard]] bool setLightDirection(
        const glm::vec3& worldDir, const glm::mat4& view,
        float ambient = 0.3f);

    /// Set Lego Mode flag (packed into invProjParams.z)
    void setLegoMode(bool enabled) {
        invProjParams.z = enabled ? 1.0f : 0.0f;
    }

    /// Configure water rendering parameters used by the raycast/blit path.
    [[nodiscard]] bool setWater(
        bool enabled, float height, const glm::vec3& shallowColor,
        const glm::vec3& deepColor, float roughness, float waveStrength,
        float reflectionStrength, float shoreFade);

    [[nodiscard]] bool setRendererMaterial(
        const glm::vec3& sunColor, float sunIntensity,
        const glm::vec3& ambientColor,
        const glm::vec3& atmosphericFogColor, float exposure, float waterIor,
        float waterDistortion, float waterAbsorptionScale,
        float waterScatterStrength, float foamSize, float foamOpacity,
        float foamCoverage, float reflectionDistance,
        const glm::vec2& spectrumPatchLengths);

    /// Advance animated water without changing its art controls.
    [[nodiscard]] bool setWaterTime(float seconds);

    /// Set the animated surface directly above/below the camera.
    [[nodiscard]] bool setCameraWaterSurfaceOffset(float offset);

    /// Verify a block before accepting a direct public copy.
    [[nodiscard]] bool isValid() const noexcept;
};

static_assert(sizeof(CameraUniforms) == 544, "CameraUniforms must be 544 bytes");

// ─────────────────────────────────────────────────────────────────────────────
// Triangle Path Configuration
// ─────────────────────────────────────────────────────────────────────────────

/// Configuration for the triangle path renderer
struct TrianglePathConfig {
    std::filesystem::path shaderPath;          ///< Path to terrain.wgsl shader
    WGPUTextureFormat colorFormat;             ///< Output color format (default: BGRA8Unorm)
    WGPUTextureFormat depthFormat;             ///< Depth buffer format (default: Depth32Float)
    float heightScale;                         ///< World-space height range
    float cellScale;                           ///< World-space size per heightmap cell
    float fogDensity;                          ///< Exponential fog density
    uint32_t lodStep;                          ///< LOD step (1 = full detail, 2 = half, etc.)

    /// Default configuration
    static TrianglePathConfig defaults() {
        return TrianglePathConfig{
            .shaderPath = "shaders/terrain.wgsl",
            .colorFormat = WGPUTextureFormat_BGRA8Unorm,
            .depthFormat = WGPUTextureFormat_Depth32Float,
            .heightScale = 500.0f,
            .cellScale = 1.0f,
            .fogDensity = 0.0001f,
            .lodStep = 1
        };
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// Triangle Path Renderer
// ─────────────────────────────────────────────────────────────────────────────

/// Triangle mesh terrain renderer using tiled instancing.
/// This is the baseline rendering path, used for debugging and as a fallback.
class TrianglePath {
public:
    /// Small patches keep the reusable 16-bit index stream in GPU L1 cache.
    static constexpr uint32_t TILE_QUADS = 32;
    static constexpr uint32_t TILE_VERTS = TILE_QUADS + 1;
    static constexpr uint32_t BASE_VERTICES_PER_TILE = TILE_VERTS * TILE_VERTS;
    static constexpr uint32_t SKIRT_VERTICES_PER_TILE = TILE_VERTS * 4;
    static constexpr uint32_t VERTICES_PER_TILE = BASE_VERTICES_PER_TILE + SKIRT_VERTICES_PER_TILE;
    static constexpr uint32_t BASE_INDICES_PER_TILE = TILE_QUADS * TILE_QUADS * 6;
    static constexpr uint32_t SKIRT_INDICES_PER_TILE = TILE_QUADS * 4 * 6;
    static constexpr uint32_t INDICES_PER_TILE = BASE_INDICES_PER_TILE + SKIRT_INDICES_PER_TILE;
    static constexpr uint32_t LOD_COUNT = 4;
    static constexpr uint32_t DISTANCE_BIN_COUNT = 8;
    static constexpr uint32_t INDIRECT_DRAW_COUNT = LOD_COUNT * DISTANCE_BIN_COUNT;

    TrianglePath() = default;
    ~TrianglePath();

    // Non-copyable
    TrianglePath(const TrianglePath&) = delete;
    TrianglePath& operator=(const TrianglePath&) = delete;

    // Movable
    TrianglePath(TrianglePath&& other) noexcept;
    TrianglePath& operator=(TrianglePath&& other) noexcept;

    // ─────────────────────────────────────────────────────────────────────────
    // Initialization
    // ─────────────────────────────────────────────────────────────────────────

    /// Initialize the triangle path renderer
    /// @param device WebGPU device
    /// @param queue WebGPU queue
    /// @param config Renderer configuration
    /// @return true on success
    [[nodiscard]] bool init(WGPUDevice device, WGPUQueue queue, 
                            const TrianglePathConfig& config = TrianglePathConfig::defaults());

    /// Check if initialized
    [[nodiscard]] bool isInitialized() const noexcept { return pipeline_ != nullptr; }

    /// Release all GPU resources
    void shutdown();

    // ─────────────────────────────────────────────────────────────────────────
    // Heightmap Binding
    // ─────────────────────────────────────────────────────────────────────────

    /// Set the heightmap texture to render
    /// Must be called before render() and after init()
    /// @param heightmapView Texture view of R16Uint heightmap
    /// @param width Heightmap width in samples
    /// @param height Heightmap height in samples
    [[nodiscard]] bool setHeightmap(
        WGPUTextureView heightmapView, uint32_t width, uint32_t height,
        std::span<const uint16_t> heightData = {});

    // ─────────────────────────────────────────────────────────────────────────
    // Texture Binding
    // ─────────────────────────────────────────────────────────────────────────

    /// Set albedo texture view (RGBA8)
    void setAlbedo(WGPUTextureView view) {
        albedoView_ = view;
        bindGroupDirty_ = true;
    }

    /// Set lightmap texture view (R8)
    void setLightmap(WGPUTextureView view) {
        lightmapView_ = view;
        bindGroupDirty_ = true;
    }

    /// Set sampler
    void setSampler(WGPUSampler sampler) {
        sampler_ = sampler;
        bindGroupDirty_ = true;
    }

    // ─────────────────────────────────────────────────────────────────────────
    // Rendering
    // ─────────────────────────────────────────────────────────────────────────

    /// Update camera uniforms
    /// @param view View matrix
    /// @param proj Projection matrix
    /// @param cameraPos World-space camera position
    void updateCamera(const glm::mat4& view, const glm::mat4& proj, const glm::vec3& cameraPos, float ambientIntensity = 0.3f);

    /// Copy a fully-built camera uniform block into this renderer
    void setCameraUniforms(const CameraUniforms& uniforms);

    /// Set Lego Mode
    void setLegoMode(bool enabled) {
        uniforms_.setLegoMode(enabled);
        uniformsDirty_ = true;
    }

    /// Set LOD step (1 = full detail)
    void setLODStep(uint32_t step);

    /// Get current LOD step
    [[nodiscard]] uint32_t getLODStep() const noexcept;

    /// Enable or disable wireframe rendering
    void setWireframe(bool enabled);

    /// Check if wireframe mode is enabled
    [[nodiscard]] bool isWireframe() const noexcept { return wireframeEnabled_; }

    /// Render the terrain
    /// @param encoder Command encoder
    /// @param colorView Color attachment texture view
    /// @param depthView Depth attachment texture view
    void render(WGPUCommandEncoder encoder, WGPUTextureView colorView, WGPUTextureView depthView);

    // ─────────────────────────────────────────────────────────────────────────
    // Accessors
    // ─────────────────────────────────────────────────────────────────────────

    /// Get uniform buffer (for external updates if needed)
    [[nodiscard]] WGPUBuffer getUniformBuffer() const noexcept { return uniformBuffer_; }

    /// Get current camera uniforms (for debugging)
    [[nodiscard]] const CameraUniforms& getUniforms() const noexcept { return uniforms_; }

    /// Get number of tiles that will be rendered
    [[nodiscard]] uint32_t getTileCount() const noexcept;

    /// Get total number of triangles that will be rendered
    [[nodiscard]] uint32_t getTriangleCount() const noexcept;

private:
    // ─────────────────────────────────────────────────────────────────────────
    // Internal Methods
    // ─────────────────────────────────────────────────────────────────────────

    bool createIndexBuffer();
    bool createUniformBuffer();
    bool createBindGroupLayout();
    bool createPipeline(const TrianglePathConfig& config);
    bool createBindGroup();
    [[nodiscard]] bool updateUniformBuffer();

    bool createComputeResources(const TrianglePathConfig& config);
    bool rebuildTerrainBuffers(std::span<const uint16_t> heightData);
    void releaseTerrainBuffers();
    [[nodiscard]] bool updateCullUniformBuffer();

    /// Calculate number of tiles for current terrain size and LOD
    [[nodiscard]] bool calculateTileCount();

    // ─────────────────────────────────────────────────────────────────────────
    // GPU Resources
    // ─────────────────────────────────────────────────────────────────────────

    WGPUDevice device_ = nullptr;
    WGPUQueue queue_ = nullptr;

    // Render Pipeline resources
    WGPUShaderModule shaderModule_ = nullptr;
    WGPUPipelineLayout pipelineLayout_ = nullptr;
    WGPURenderPipeline pipeline_ = nullptr;
    WGPURenderPipeline wireframePipeline_ = nullptr;

    // Compute Pipeline resources
    WGPUShaderModule computeModule_ = nullptr;
    WGPUPipelineLayout computePipelineLayout_ = nullptr;
    WGPUComputePipeline computePipeline_ = nullptr;
    WGPUBindGroupLayout computeBindGroupLayout_ = nullptr;
    WGPUBindGroup computeBindGroup_ = nullptr;

    // Bind group resources
    WGPUBindGroupLayout bindGroupLayout_ = nullptr;
    WGPUBindGroup bindGroup_ = nullptr;

    // Buffers
    WGPUBuffer indexBuffer_ = nullptr;
    WGPUBuffer uniformBuffer_ = nullptr;
    WGPUBuffer indirectBuffer_ = nullptr;
    WGPUBuffer indirectTemplateBuffer_ = nullptr;
    WGPUBuffer visibleIndicesBuffer_ = nullptr;
    WGPUBuffer tileBoundsBuffer_ = nullptr;
    WGPUBuffer cullUniformBuffer_ = nullptr;

    // Heightmap binding
    WGPUTextureView heightmapView_ = nullptr;
    uint32_t heightmapWidth_ = 0;
    uint32_t heightmapHeight_ = 0;
    
    // Texture bindings
    WGPUTextureView albedoView_ = nullptr;
    WGPUTextureView lightmapView_ = nullptr;
    WGPUSampler sampler_ = nullptr;

    // State
    CameraUniforms uniforms_;
    TrianglePathConfig config_ = TrianglePathConfig::defaults();
    uint32_t tilesX_ = 0;
    uint32_t tilesY_ = 0;
    std::array<uint32_t, LOD_COUNT> levelTilesX_{};
    std::array<uint32_t, LOD_COUNT> levelTilesY_{};
    std::array<uint32_t, LOD_COUNT> levelCandidateOffsets_{};
    uint32_t totalCandidateCount_ = 0;
    uint32_t visibilitySegmentCapacity_ = 0;
    bool uniformsDirty_ = true;
    bool cullUniformsDirty_ = true;
    bool bindGroupDirty_ = true;
    bool wireframeEnabled_ = false;
};

} // namespace voxy::render
