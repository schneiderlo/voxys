// ═══════════════════════════════════════════════════════════════════════════════
// textures.hpp - Terrain Texture Loading and Management (C++20)
// ═══════════════════════════════════════════════════════════════════════════════
// Provides loading and management of terrain textures for the blit pass:
//   - Terrain albedo/color texture (RGBA8)
//   - Lightmap texture for ambient occlusion/sky visibility (R8)
//   - Associated samplers
// ═══════════════════════════════════════════════════════════════════════════════

#pragma once

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
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
    uint32_t placeholderWidth = 256;     ///< Width for placeholder textures
    uint32_t placeholderHeight = 256;    ///< Height for placeholder textures
    std::span<const uint16_t> heightSamples; ///< Optional heightmap samples for biome/material baking
    uint32_t heightmapWidth = 0;         ///< Width of heightSamples
    uint32_t heightmapHeight = 0;        ///< Height of heightSamples
    float heightScale = 1.0f;            ///< World-space height scale for biome thresholds
    float cellScale = 1.0f;              ///< World-space horizontal spacing for slope estimates
    bool minecraftStyleEnhancement = true; ///< Bake biome colors, rivers, and canopy detail
    uint32_t generatedLightmapMaxSize = 2048; ///< Cap for generated terrain AO/lightmap textures
    bool enableGeneratedTextureCache = true; ///< Persist expensive generated albedo/lightmap bakes
    std::filesystem::path generatedTextureCacheDir = "data/generated/texture_cache";
    
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
    [[nodiscard]] bool isInitialized() const noexcept { return albedoTexture_ != nullptr; }

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

    /// Get terrain normal texture
    [[nodiscard]] WGPUTexture getNormalTexture() const noexcept { return normalTexture_; }

    /// Get terrain normal texture view
    [[nodiscard]] WGPUTextureView getNormalView() const noexcept { return normalView_; }

    /// Get linear sampler for texture sampling
    [[nodiscard]] WGPUSampler getSampler() const noexcept { return sampler_; }

    /// Get albedo texture dimensions
    [[nodiscard]] uint32_t getAlbedoWidth() const noexcept { return albedoWidth_; }
    [[nodiscard]] uint32_t getAlbedoHeight() const noexcept { return albedoHeight_; }

    /// Get lightmap texture dimensions
    [[nodiscard]] uint32_t getLightmapWidth() const noexcept { return lightmapWidth_; }
    [[nodiscard]] uint32_t getLightmapHeight() const noexcept { return lightmapHeight_; }

    /// Get normal texture dimensions
    [[nodiscard]] uint32_t getNormalWidth() const noexcept { return normalWidth_; }
    [[nodiscard]] uint32_t getNormalHeight() const noexcept { return normalHeight_; }

private:
    // ─────────────────────────────────────────────────────────────────────────
    // Internal Methods
    // ─────────────────────────────────────────────────────────────────────────

    bool createSampler();
    bool uploadAlbedoTexture(const std::vector<uint8_t>& data, uint32_t width, uint32_t height);
    bool uploadLightmapTexture(const std::vector<uint8_t>& data, uint32_t width, uint32_t height);
    bool uploadNormalTexture(const std::vector<uint8_t>& data, uint32_t width, uint32_t height);

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

    // Normal texture (RGBA8, world-space normal encoded in RGB)
    WGPUTexture normalTexture_ = nullptr;
    WGPUTextureView normalView_ = nullptr;
    uint32_t normalWidth_ = 0;
    uint32_t normalHeight_ = 0;

    // Sampler
    WGPUSampler sampler_ = nullptr;

    // Configuration
    TerrainTextureConfig config_ = TerrainTextureConfig::defaults();
    std::string generatedCacheKey_;
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

/// Generate terrain-aware R8 light visibility data.
/// Falls back to solid white if the config has no height samples.
[[nodiscard]] std::vector<uint8_t> generateTerrainLightmapData(
    uint32_t width,
    uint32_t height,
    const TerrainTextureConfig& config);

/// Generate terrain-aware RGBA8 world normal data.
/// Falls back to flat +Y normals if the config has no height samples.
[[nodiscard]] std::vector<uint8_t> generateTerrainNormalData(
    uint32_t width,
    uint32_t height,
    const TerrainTextureConfig& config);

} // namespace voxy::terrain
