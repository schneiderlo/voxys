// ═══════════════════════════════════════════════════════════════════════════════
// textures.hpp - Terrain Texture Loading and Management (C++20)
// ═══════════════════════════════════════════════════════════════════════════════
// Provides loading and management of terrain textures for the blit pass:
//   - Terrain albedo/color texture (RGBA8)
//   - Lightmap texture for ambient occlusion/sky visibility (R8)
//   - Associated samplers
// ═══════════════════════════════════════════════════════════════════════════════

#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string_view>
#include <vector>

// WebGPU header - same API for native (wgpu-native) and WASM
#if defined(VOXY_WASM)
    #include <webgpu/webgpu.h>
#else
    #include <webgpu.h>
#endif

namespace voxy::terrain {

// ─────────────────────────────────────────────────────────────────────────────
// Terrain Texture Configuration
// ─────────────────────────────────────────────────────────────────────────────

/// Configuration for terrain textures
struct TerrainTextureConfig {
    std::filesystem::path albedoPath;    ///< Path to albedo texture (optional)
    std::filesystem::path lightmapPath;  ///< Path to lightmap texture (optional)
    /// Directory containing the four CC0 terrain material families.
    /// Missing or invalid assets fall back to a small procedural array so the
    /// renderer never binds an incomplete material set.
    std::filesystem::path materialDirectory = "data/materials";
    uint32_t placeholderWidth = 256;     ///< Width for placeholder textures
    uint32_t placeholderHeight = 256;    ///< Height for placeholder textures
    
    /// Default configuration
    static TerrainTextureConfig defaults() {
        return TerrainTextureConfig{};
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// Terrain Textures Class
// ─────────────────────────────────────────────────────────────────────────────

/// Manages terrain textures (albedo and lightmap) for the blit pass.
/// If no textures are loaded, generates procedural placeholders.
class TerrainTextures {
public:
    TerrainTextures() = default;
    ~TerrainTextures();

    // Non-copyable (GPU resources)
    TerrainTextures(const TerrainTextures&) = delete;
    TerrainTextures& operator=(const TerrainTextures&) = delete;

    // Movable
    TerrainTextures(TerrainTextures&& other) noexcept;
    TerrainTextures& operator=(TerrainTextures&& other) noexcept;

    // ─────────────────────────────────────────────────────────────────────────
    // Initialization
    // ─────────────────────────────────────────────────────────────────────────

    /// Initialize terrain textures
    /// If albedoPath is empty, creates a procedural placeholder texture
    /// If lightmapPath is empty, creates a white lightmap (full visibility)
    /// @param device WebGPU device
    /// @param queue WebGPU queue
    /// @param config Texture configuration
    /// @return true on success
    [[nodiscard]] bool init(WGPUDevice device, WGPUQueue queue,
                            const TerrainTextureConfig& config = TerrainTextureConfig::defaults());

    /// Check if initialized
    [[nodiscard]] bool isInitialized() const noexcept {
        return albedoTexture_ && albedoView_ && lightmapTexture_
            && lightmapView_ && materialAlbedoTexture_
            && materialAlbedoView_ && materialNormalRoughnessTexture_
            && materialNormalRoughnessView_ && sampler_;
    }

    /// Release all GPU resources
    void shutdown();

    // ─────────────────────────────────────────────────────────────────────────
    // Texture Loading
    // ─────────────────────────────────────────────────────────────────────────

    /// Load albedo texture from file (RGBA8)
    /// Supports PNG and JPG formats
    /// @param path Path to texture file
    /// @return true on success
    [[nodiscard]] bool loadAlbedo(const std::filesystem::path& path);

    /// Load lightmap texture from file (grayscale, stored as R8)
    /// @param path Path to texture file
    /// @return true on success
    [[nodiscard]] bool loadLightmap(const std::filesystem::path& path);

    /// Create procedural placeholder albedo texture
    /// Generates a green terrain color with some variation
    /// @param width Texture width
    /// @param height Texture height
    /// @return true on success
    [[nodiscard]] bool createPlaceholderAlbedo(uint32_t width, uint32_t height);

    /// Create white lightmap (full light visibility everywhere)
    /// @param width Texture width
    /// @param height Texture height
    /// @return true on success
    [[nodiscard]] bool createWhiteLightmap(uint32_t width, uint32_t height);

    /// Load sand, soil, grass, and rock PBR maps into two bounded texture
    /// arrays. The source names and layer order are fixed by the CC0 pack.
    [[nodiscard]] bool loadTerrainMaterials(
        const std::filesystem::path& directory);

    /// Create a complete four-layer fallback when the optional source pack is
    /// unavailable. This keeps validation and headless tests self-contained.
    [[nodiscard]] bool createFallbackTerrainMaterials(
        uint32_t width = 64u, uint32_t height = 64u);

    // ─────────────────────────────────────────────────────────────────────────
    // Accessors
    // ─────────────────────────────────────────────────────────────────────────

    /// Get albedo texture
    [[nodiscard]] WGPUTexture getAlbedoTexture() const noexcept { return albedoTexture_; }

    /// Get albedo texture view
    [[nodiscard]] WGPUTextureView getAlbedoView() const noexcept { return albedoView_; }

    /// Get lightmap texture
    [[nodiscard]] WGPUTexture getLightmapTexture() const noexcept { return lightmapTexture_; }

    /// Get lightmap texture view
    [[nodiscard]] WGPUTextureView getLightmapView() const noexcept { return lightmapView_; }

    /// Four layers in this order: dry/wet sand, soil, grass, exposed rock.
    [[nodiscard]] WGPUTexture getMaterialAlbedoTexture() const noexcept {
        return materialAlbedoTexture_;
    }
    [[nodiscard]] WGPUTextureView getMaterialAlbedoView() const noexcept {
        return materialAlbedoView_;
    }
    [[nodiscard]] WGPUTexture getMaterialNormalRoughnessTexture() const noexcept {
        return materialNormalRoughnessTexture_;
    }
    [[nodiscard]] WGPUTextureView getMaterialNormalRoughnessView() const noexcept {
        return materialNormalRoughnessView_;
    }

    /// Get linear sampler for texture sampling
    [[nodiscard]] WGPUSampler getSampler() const noexcept { return sampler_; }

    /// Get albedo texture dimensions
    [[nodiscard]] uint32_t getAlbedoWidth() const noexcept { return albedoWidth_; }
    [[nodiscard]] uint32_t getAlbedoHeight() const noexcept { return albedoHeight_; }

    /// Get lightmap texture dimensions
    [[nodiscard]] uint32_t getLightmapWidth() const noexcept { return lightmapWidth_; }
    [[nodiscard]] uint32_t getLightmapHeight() const noexcept { return lightmapHeight_; }

    [[nodiscard]] uint32_t getMaterialWidth() const noexcept {
        return materialWidth_;
    }
    [[nodiscard]] uint32_t getMaterialHeight() const noexcept {
        return materialHeight_;
    }

    static constexpr uint32_t kMaterialLayerCount = 4u;

private:
    // ─────────────────────────────────────────────────────────────────────────
    // Internal Methods
    // ─────────────────────────────────────────────────────────────────────────

    bool createSampler();
    bool uploadAlbedoTexture(std::span<const uint8_t> data,
                             uint32_t width, uint32_t height);
    bool uploadLightmapTexture(const std::vector<uint8_t>& data, uint32_t width, uint32_t height);
    bool uploadTerrainMaterialArrays(
        const std::array<std::vector<uint8_t>, kMaterialLayerCount>& albedo,
        const std::array<std::vector<uint8_t>, kMaterialLayerCount>&
            normalRoughness,
        uint32_t width, uint32_t height);

    // ─────────────────────────────────────────────────────────────────────────
    // GPU Resources
    // ─────────────────────────────────────────────────────────────────────────

    WGPUDevice device_ = nullptr;
    WGPUQueue queue_ = nullptr;

    // Albedo texture (RGBA8)
    WGPUTexture albedoTexture_ = nullptr;
    WGPUTextureView albedoView_ = nullptr;
    uint32_t albedoWidth_ = 0;
    uint32_t albedoHeight_ = 0;

    // Lightmap texture (R8)
    WGPUTexture lightmapTexture_ = nullptr;
    WGPUTextureView lightmapView_ = nullptr;
    uint32_t lightmapWidth_ = 0;
    uint32_t lightmapHeight_ = 0;

    // Bounded four-layer material arrays. Both are RGBA8 with complete mips.
    // The albedo array uses an sRGB view for hardware linearization. Detail is
    // linear UNORM with tangent-space NormalGL XYZ and perceptual roughness A.
    WGPUTexture materialAlbedoTexture_ = nullptr;
    WGPUTextureView materialAlbedoView_ = nullptr;
    WGPUTexture materialNormalRoughnessTexture_ = nullptr;
    WGPUTextureView materialNormalRoughnessView_ = nullptr;
    uint32_t materialWidth_ = 0;
    uint32_t materialHeight_ = 0;

    // Sampler
    WGPUSampler sampler_ = nullptr;

    // Configuration
    TerrainTextureConfig config_ = TerrainTextureConfig::defaults();
};

// ─────────────────────────────────────────────────────────────────────────────
// Utility Functions
// ─────────────────────────────────────────────────────────────────────────────

/// Generate procedural terrain color data
/// Creates a green-ish terrain pattern with some height variation
/// @param width Texture width
/// @param height Texture height
/// @return RGBA8 pixel data
[[nodiscard]] std::vector<uint8_t> generateTerrainColorData(uint32_t width, uint32_t height);

/// Generate solid white lightmap data
/// @param width Texture width
/// @param height Texture height
/// @return R8 pixel data (all 255)
[[nodiscard]] std::vector<uint8_t> generateWhiteLightmapData(uint32_t width, uint32_t height);

/// Downsample one RGBA8 sRGB mip level in linear light. Area filtering keeps
/// every source texel represented when either source dimension is odd.
[[nodiscard]] std::vector<uint8_t> downsampleTerrainAlbedoSrgb(
    std::span<const uint8_t> source, uint32_t width, uint32_t height);

/// Downsample tangent-space NormalGL XYZ + perceptual roughness. Normals are
/// renormalized and normal variance raises the mip roughness to avoid sparkle.
[[nodiscard]] std::vector<uint8_t> downsampleTerrainNormalRoughness(
    std::span<const uint8_t> source, uint32_t width, uint32_t height);

} // namespace voxy::terrain
