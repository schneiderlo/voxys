// ═══════════════════════════════════════════════════════════════════════════════
// textures.cpp - Terrain Texture Loading and Management Implementation
// ═══════════════════════════════════════════════════════════════════════════════

#include "terrain/textures.hpp"
#include "terrain/biomes.hpp"
#include "gpu/resources.hpp"
#include "core/log.hpp"

#include <array>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>

// stb_image for texture loading (implementation in core/stb_impl.cpp)
// Note: STBI_NO_STDIO is defined, so we must use stbi_load_from_memory
#if __has_include(<stb_image.h>)
    #include <stb_image.h>
#endif

namespace voxy::terrain {

namespace {

float clamp01(float value) noexcept {
    return std::clamp(value, 0.0f, 1.0f);
}

float smoothstep(float edge0, float edge1, float value) noexcept {
    const float t = clamp01((value - edge0) / (edge1 - edge0));
    return t * t * (3.0f - 2.0f * t);
}

uint32_t hash2D(int32_t x, int32_t y) noexcept {
    uint32_t h = static_cast<uint32_t>(x) * 0x8da6b343u;
    h ^= static_cast<uint32_t>(y) * 0xd8163841u;
    h ^= h >> 13u;
    h *= 0x85ebca6bu;
    h ^= h >> 16u;
    return h;
}

float hash01(int32_t x, int32_t y) noexcept {
    return static_cast<float>(hash2D(x, y) & 0x00ffffffu) / 16777215.0f;
}

bool hasHeightSamples(const TerrainTextureConfig& config) noexcept {
    return !config.heightSamples.empty() &&
           config.heightmapWidth > 1 &&
           config.heightmapHeight > 1 &&
           config.heightSamples.size() >= static_cast<size_t>(config.heightmapWidth) *
                                         static_cast<size_t>(config.heightmapHeight);
}

float sampleHeightMeters(const TerrainTextureConfig& config, uint32_t x, uint32_t y) noexcept {
    if (!hasHeightSamples(config)) {
        return 0.0f;
    }

    x = std::min(x, config.heightmapWidth - 1);
    y = std::min(y, config.heightmapHeight - 1);
    const uint16_t raw = config.heightSamples[static_cast<size_t>(y) * config.heightmapWidth + x];
    const float normalized = static_cast<float>(raw) / 65535.0f;
    return (normalized * 2.0f - 1.0f) * config.heightScale;
}

float estimateSlope(const TerrainTextureConfig& config, uint32_t x, uint32_t y) noexcept {
    if (!hasHeightSamples(config)) {
        return 0.0f;
    }

    const uint32_t xl = x > 0 ? x - 1 : x;
    const uint32_t xr = std::min(x + 1, config.heightmapWidth - 1);
    const uint32_t yd = y > 0 ? y - 1 : y;
    const uint32_t yu = std::min(y + 1, config.heightmapHeight - 1);
    const float hL = sampleHeightMeters(config, xl, y);
    const float hR = sampleHeightMeters(config, xr, y);
    const float hD = sampleHeightMeters(config, x, yd);
    const float hU = sampleHeightMeters(config, x, yu);
    const float horizontal = std::max(config.cellScale * 2.0f, 0.001f);
    const float dx = (hR - hL) / horizontal;
    const float dy = (hU - hD) / horizontal;
    return std::sqrt(dx * dx + dy * dy);
}

struct ColorMap {
    std::vector<uint8_t> pixels;
    uint32_t width = 0;
    uint32_t height = 0;

    [[nodiscard]] bool valid() const noexcept {
        return !pixels.empty() && width > 0 && height > 0;
    }
};

struct MaterialPack {
    ColorMap grass;
    ColorMap forest;
    ColorMap dry;
    ColorMap rock;
    ColorMap snow;

    [[nodiscard]] bool valid() const noexcept {
        return grass.valid() || forest.valid() || dry.valid() || rock.valid() || snow.valid();
    }
};

ColorMap loadColorMap(const std::filesystem::path& path) {
    ColorMap map;
    if (!std::filesystem::exists(path)) {
        return map;
    }

    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        return map;
    }

    auto fileSize = file.tellg();
    file.seekg(0, std::ios::beg);
    std::vector<uint8_t> fileData(static_cast<size_t>(fileSize));
    if (!file.read(reinterpret_cast<char*>(fileData.data()), fileSize)) {
        return map;
    }

    int width = 0;
    int height = 0;
    int channels = 0;
    uint8_t* decoded = stbi_load_from_memory(
        fileData.data(),
        static_cast<int>(fileData.size()),
        &width,
        &height,
        &channels,
        3
    );

    if (!decoded || width <= 0 || height <= 0) {
        if (decoded) {
            stbi_image_free(decoded);
        }
        return map;
    }

    map.width = static_cast<uint32_t>(width);
    map.height = static_cast<uint32_t>(height);
    map.pixels.assign(decoded, decoded + static_cast<size_t>(width) * static_cast<size_t>(height) * 3u);
    stbi_image_free(decoded);
    return map;
}

const MaterialPack& getGeneratedMaterialPack() {
    static const MaterialPack pack = [] {
        const std::filesystem::path root = "data/generated/cc0_texture_pack/maps";
        MaterialPack loaded;
        loaded.grass = loadColorMap(root / "grass_color.jpg");
        loaded.forest = loadColorMap(root / "forest_floor_color.jpg");
        loaded.dry = loadColorMap(root / "dry_ground_color.jpg");
        loaded.rock = loadColorMap(root / "rock_color.jpg");
        loaded.snow = loadColorMap(root / "snow_color.jpg");
        return loaded;
    }();
    return pack;
}

const ColorMap* colorMapForRole(const MaterialPack& pack, BiomeSurfaceRole role) noexcept {
    switch (role) {
        case BiomeSurfaceRole::Grass: return pack.grass.valid() ? &pack.grass : nullptr;
        case BiomeSurfaceRole::Forest: return pack.forest.valid() ? &pack.forest : nullptr;
        case BiomeSurfaceRole::Dry:
        case BiomeSurfaceRole::Beach: return pack.dry.valid() ? &pack.dry : nullptr;
        case BiomeSurfaceRole::Rock: return pack.rock.valid() ? &pack.rock : nullptr;
        case BiomeSurfaceRole::Snow: return pack.snow.valid() ? &pack.snow : nullptr;
        case BiomeSurfaceRole::Water: return nullptr;
        default: return nullptr;
    }
}

std::array<float, 3> sampleColorMap(const ColorMap& map, float worldX, float worldY) noexcept {
    const float tileScale = 0.32f;
    const auto u = static_cast<uint32_t>(std::floor(std::abs(worldX * tileScale))) % map.width;
    const auto v = static_cast<uint32_t>(std::floor(std::abs(worldY * tileScale))) % map.height;
    const size_t idx = (static_cast<size_t>(v) * map.width + u) * 3u;
    return {
        static_cast<float>(map.pixels[idx + 0]) / 255.0f,
        static_cast<float>(map.pixels[idx + 1]) / 255.0f,
        static_cast<float>(map.pixels[idx + 2]) / 255.0f,
    };
}

void applyMinecraftStyleOverlay(std::vector<uint8_t>& pixels, uint32_t width, uint32_t height,
                                const TerrainTextureConfig& config) {
    if (pixels.size() != static_cast<size_t>(width) * static_cast<size_t>(height) * 4u ||
        width == 0 || height == 0) {
        return;
    }
    if (!config.minecraftStyleEnhancement) {
        return;
    }

    const bool useHeight = hasHeightSamples(config);
    const float terrainWorldWidth = useHeight
        ? static_cast<float>(config.heightmapWidth - 1u) * config.cellScale
        : 2048.0f;
    const float terrainWorldHeight = useHeight
        ? static_cast<float>(config.heightmapHeight - 1u) * config.cellScale
        : 2048.0f;
    const float originX = terrainWorldWidth * 0.5f;
    const float originZ = terrainWorldHeight * 0.5f;
    const float invMaxX = 1.0f / static_cast<float>(std::max(width - 1u, 1u));
    const float invMaxY = 1.0f / static_cast<float>(std::max(height - 1u, 1u));
    const MaterialPack& materialPack = getGeneratedMaterialPack();

    for (uint32_t y = 0; y < height; ++y) {
        const float v = static_cast<float>(y) * invMaxY;
        const float worldZ = v * terrainWorldHeight - originZ;

        for (uint32_t x = 0; x < width; ++x) {
            const float u = static_cast<float>(x) * invMaxX;
            const float worldX = u * terrainWorldWidth - originX;
            const size_t idx = (static_cast<size_t>(y) * width + x) * 4u;

            float r = static_cast<float>(pixels[idx + 0]) / 255.0f;
            float g = static_cast<float>(pixels[idx + 1]) / 255.0f;
            float b = static_cast<float>(pixels[idx + 2]) / 255.0f;

            auto blend = [&](float tr, float tg, float tb, float alpha) {
                const float a = clamp01(alpha);
                r = r * (1.0f - a) + tr * a;
                g = g * (1.0f - a) + tg * a;
                b = b * (1.0f - a) + tb * a;
            };

            const float maxRG = std::max(r, g);
            const float maxRGB = std::max(maxRG, b);
            const float minRGB = std::min(std::min(r, g), b);
            const float chroma = maxRGB - minRGB;
            const float brightness = (r + g + b) / 3.0f;
            const float waterLike = smoothstep(0.02f, 0.22f, b - maxRG * 0.72f);
            const float snowLike = smoothstep(0.66f, 0.88f, brightness) * (1.0f - smoothstep(0.08f, 0.22f, chroma));
            const float greenBias = g - (r * 0.45f + b * 0.25f);

            const uint32_t hx = useHeight
                ? std::min(static_cast<uint32_t>(static_cast<uint64_t>(x) * (config.heightmapWidth - 1u) / std::max(width - 1u, 1u)),
                           config.heightmapWidth - 1u)
                : 0u;
            const uint32_t hy = useHeight
                ? std::min(static_cast<uint32_t>(static_cast<uint64_t>(y) * (config.heightmapHeight - 1u) / std::max(height - 1u, 1u)),
                           config.heightmapHeight - 1u)
                : 0u;
            const float heightM = useHeight ? sampleHeightMeters(config, hx, hy) : (brightness - 0.5f) * 120.0f;
            const float slope = useHeight ? estimateSlope(config, hx, hy) : clamp01(chroma * 2.0f);
            const BiomeSample biome = sampleBiome(BiomeSampleInput{
                .worldX = worldX,
                .worldZ = worldZ,
                .heightM = heightM,
                .slope = slope,
                .surfaceMoistureBias = std::clamp(greenBias * 0.18f, -0.08f, 0.08f),
            });
            const float biomeStrength = useHeight ? 0.74f : 0.34f;
            blend(biome.colorR, biome.colorG, biome.colorB, biomeStrength);
            if (const ColorMap* colorMap = colorMapForRole(materialPack, biome.role)) {
                const auto material = sampleColorMap(*colorMap, worldX, worldZ);
                blend(material[0], material[1], material[2], useHeight ? 0.34f : 0.18f);
            }

            const int32_t blockX = static_cast<int32_t>(x / 3u);
            const int32_t blockY = static_cast<int32_t>(y / 3u);
            const float block = hash01(blockX, blockY);
            const float tint = 0.93f + 0.13f * block;
            r *= tint;
            g *= tint;
            b *= tint;

            if (waterLike > 0.02f) {
                const float wave = 0.94f + 0.08f * hash01(static_cast<int32_t>(x / 5u), static_cast<int32_t>(y / 5u));
                blend(0.05f * wave, 0.28f * wave, 0.43f * wave, waterLike * 0.70f);
            }

            if (biome.water > 0.02f) {
                blend(biome.colorR, biome.colorG, biome.colorB, biome.water * 0.95f);
            }

            const float dryGrass = clamp01((r - g) * 1.8f + 0.20f) * (1.0f - waterLike) * (1.0f - snowLike);
            blend(0.48f, 0.44f, 0.25f, dryGrass * 0.22f);

            const float rockLike = smoothstep(0.18f, 0.42f, brightness) * (1.0f - smoothstep(0.18f, 0.34f, chroma));
            blend(0.42f, 0.41f, 0.37f, rockLike * 0.30f * (1.0f - waterLike));

            const float cellSize = 8.0f;
            const int32_t cellX = static_cast<int32_t>(std::floor(worldX / cellSize));
            const int32_t cellY = static_cast<int32_t>(std::floor(worldZ / cellSize));
            const float density = clamp01(biome.treeDensity * (1.0f - smoothstep(0.45f, 0.90f, slope)));
            if (hash01(cellX + 41, cellY + 2) < density * 0.74f) {
                const float centerX = (static_cast<float>(cellX) + hash01(cellX + 11, cellY + 7)) * cellSize;
                const float centerY = (static_cast<float>(cellY) + hash01(cellX + 23, cellY + 19)) * cellSize;
                const float dx = worldX - centerX;
                const float dy = worldZ - centerY;
                const float radius = 1.4f + 2.0f * hash01(cellX + 5, cellY + 31);
                const float canopy = (1.0f - smoothstep(radius * 0.55f, radius, std::sqrt(dx * dx + dy * dy))) *
                                     density * (1.0f - biome.river) * (1.0f - waterLike) * (1.0f - snowLike);
                blend(0.05f, 0.18f, 0.06f, canopy * 0.88f);

                if (hash01(static_cast<int32_t>(x), static_cast<int32_t>(y)) > 0.86f) {
                    blend(0.12f, 0.075f, 0.035f, canopy * 0.25f);
                }
            }

            if (snowLike > 0.02f || biome.snow > 0.02f) {
                blend(0.88f, 0.91f, 0.87f, std::max(snowLike * 0.55f, biome.snow * 0.80f));
            }

            pixels[idx + 0] = static_cast<uint8_t>(std::clamp(r, 0.0f, 1.0f) * 255.0f);
            pixels[idx + 1] = static_cast<uint8_t>(std::clamp(g, 0.0f, 1.0f) * 255.0f);
            pixels[idx + 2] = static_cast<uint8_t>(std::clamp(b, 0.0f, 1.0f) * 255.0f);
            pixels[idx + 3] = 255;
        }
    }
}

} // namespace

// ═══════════════════════════════════════════════════════════════════════════════
// TerrainTextures Implementation
// ═══════════════════════════════════════════════════════════════════════════════

TerrainTextures::~TerrainTextures() {
    shutdown();
}

TerrainTextures::TerrainTextures(TerrainTextures&& other) noexcept
    : device_(other.device_)
    , queue_(other.queue_)
    , albedoTexture_(other.albedoTexture_)
    , albedoView_(other.albedoView_)
    , albedoWidth_(other.albedoWidth_)
    , albedoHeight_(other.albedoHeight_)
    , lightmapTexture_(other.lightmapTexture_)
    , lightmapView_(other.lightmapView_)
    , lightmapWidth_(other.lightmapWidth_)
    , lightmapHeight_(other.lightmapHeight_)
    , sampler_(other.sampler_)
    , config_(other.config_)
{
    // Null out the source
    other.device_ = nullptr;
    other.queue_ = nullptr;
    other.albedoTexture_ = nullptr;
    other.albedoView_ = nullptr;
    other.lightmapTexture_ = nullptr;
    other.lightmapView_ = nullptr;
    other.sampler_ = nullptr;
}

TerrainTextures& TerrainTextures::operator=(TerrainTextures&& other) noexcept {
    if (this != &other) {
        shutdown();
        
        device_ = other.device_;
        queue_ = other.queue_;
        albedoTexture_ = other.albedoTexture_;
        albedoView_ = other.albedoView_;
        albedoWidth_ = other.albedoWidth_;
        albedoHeight_ = other.albedoHeight_;
        lightmapTexture_ = other.lightmapTexture_;
        lightmapView_ = other.lightmapView_;
        lightmapWidth_ = other.lightmapWidth_;
        lightmapHeight_ = other.lightmapHeight_;
        sampler_ = other.sampler_;
        config_ = other.config_;
        
        other.device_ = nullptr;
        other.queue_ = nullptr;
        other.albedoTexture_ = nullptr;
        other.albedoView_ = nullptr;
        other.lightmapTexture_ = nullptr;
        other.lightmapView_ = nullptr;
        other.sampler_ = nullptr;
    }
    return *this;
}

void TerrainTextures::shutdown() {
    if (albedoView_) {
        wgpuTextureViewRelease(albedoView_);
        albedoView_ = nullptr;
    }
    if (albedoTexture_) {
        wgpuTextureRelease(albedoTexture_);
        albedoTexture_ = nullptr;
    }
    if (lightmapView_) {
        wgpuTextureViewRelease(lightmapView_);
        lightmapView_ = nullptr;
    }
    if (lightmapTexture_) {
        wgpuTextureRelease(lightmapTexture_);
        lightmapTexture_ = nullptr;
    }
    if (sampler_) {
        wgpuSamplerRelease(sampler_);
        sampler_ = nullptr;
    }
    
    device_ = nullptr;
    queue_ = nullptr;
    albedoWidth_ = 0;
    albedoHeight_ = 0;
    lightmapWidth_ = 0;
    lightmapHeight_ = 0;
}

// ─────────────────────────────────────────────────────────────────────────────
// Initialization
// ─────────────────────────────────────────────────────────────────────────────

bool TerrainTextures::init(WGPUDevice device, WGPUQueue queue,
                           const TerrainTextureConfig& config) {
    LOG_SCOPE("TerrainTextures::init");
    
    if (isInitialized()) {
        LOG_ERROR("TerrainTextures::init: already initialized");
        return false;
    }
    
    if (!device || !queue) {
        LOG_ERROR("TerrainTextures::init: device or queue is null");
        return false;
    }
    
    device_ = device;
    queue_ = queue;
    config_ = config;
    
    // Create sampler first
    if (!createSampler()) {
        LOG_ERROR("Failed to create sampler");
        shutdown();
        return false;
    }
    
    // Load or create albedo texture
    if (!config.albedoPath.empty() && std::filesystem::exists(config.albedoPath)) {
        if (!loadAlbedo(config.albedoPath)) {
            LOG_WARN("Failed to load albedo texture, creating placeholder");
            if (!createPlaceholderAlbedo(config.placeholderWidth, config.placeholderHeight)) {
                LOG_ERROR("Failed to create placeholder albedo texture");
                shutdown();
                return false;
            }
        }
    } else {
        if (!createPlaceholderAlbedo(config.placeholderWidth, config.placeholderHeight)) {
            LOG_ERROR("Failed to create placeholder albedo texture");
            shutdown();
            return false;
        }
    }
    
    // Load or create lightmap texture
    if (!config.lightmapPath.empty() && std::filesystem::exists(config.lightmapPath)) {
        if (!loadLightmap(config.lightmapPath)) {
            LOG_WARN("Failed to load lightmap texture, creating white lightmap");
            if (!createWhiteLightmap(1, 1)) {
                LOG_ERROR("Failed to create white lightmap texture");
                shutdown();
                return false;
            }
        }
    } else if (config.minecraftStyleEnhancement && hasHeightSamples(config)) {
        const uint32_t maxSize = std::max(config.generatedLightmapMaxSize, 1u);
        const float aspect = static_cast<float>(config.heightmapWidth) /
                             static_cast<float>(std::max(config.heightmapHeight, 1u));
        uint32_t lightmapWidth = std::min(config.heightmapWidth, maxSize);
        uint32_t lightmapHeight = std::min(config.heightmapHeight, maxSize);
        if (aspect >= 1.0f) {
            lightmapHeight = std::max(1u, static_cast<uint32_t>(
                std::round(static_cast<float>(lightmapWidth) / aspect)));
        } else {
            lightmapWidth = std::max(1u, static_cast<uint32_t>(
                std::round(static_cast<float>(lightmapHeight) * aspect)));
        }

        auto lightmap = generateTerrainLightmapData(lightmapWidth, lightmapHeight, config);
        if (!uploadLightmapTexture(lightmap, lightmapWidth, lightmapHeight)) {
            LOG_WARN("Failed to upload generated terrain lightmap, creating white lightmap");
            if (!createWhiteLightmap(1, 1)) {
                LOG_ERROR("Failed to create white lightmap texture");
                shutdown();
                return false;
            }
        }
    } else {
        if (!createWhiteLightmap(1, 1)) {
            LOG_ERROR("Failed to create white lightmap texture");
            shutdown();
            return false;
        }
    }
    
    LOG_INFO("TerrainTextures initialized: albedo {}x{}, lightmap {}x{}",
             albedoWidth_, albedoHeight_, lightmapWidth_, lightmapHeight_);
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Sampler Creation
// ─────────────────────────────────────────────────────────────────────────────

bool TerrainTextures::createSampler() {
    gpu::SamplerDesc samplerDesc = gpu::SamplerDesc::linear("terrain_sampler");
    sampler_ = gpu::createSampler(device_, samplerDesc);
    
    if (!sampler_) {
        LOG_ERROR("Failed to create terrain sampler");
        return false;
    }
    
    LOG_DEBUG("Created terrain sampler");
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Texture Loading
// ─────────────────────────────────────────────────────────────────────────────

bool TerrainTextures::loadAlbedo(const std::filesystem::path& path) {
    LOG_SCOPE("TerrainTextures::loadAlbedo");
    
    // Read file into memory (STBI_NO_STDIO is defined)
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        LOG_ERROR("Failed to open albedo texture file: {}", path.string());
        return false;
    }
    
    auto fileSize = file.tellg();
    file.seekg(0, std::ios::beg);
    
    std::vector<uint8_t> fileData(static_cast<size_t>(fileSize));
    if (!file.read(reinterpret_cast<char*>(fileData.data()), fileSize)) {
        LOG_ERROR("Failed to read albedo texture file: {}", path.string());
        return false;
    }
    file.close();
    
    int width, height, channels;
    uint8_t* data = stbi_load_from_memory(fileData.data(), static_cast<int>(fileData.size()),
                                           &width, &height, &channels, 4);
    
    if (!data) {
        LOG_ERROR("Failed to decode albedo texture: {}", path.string());
        return false;
    }
    
    LOG_DEBUG("Loaded albedo texture: {}x{} from {}", width, height, path.string());
    
    // Copy data to vector
    std::vector<uint8_t> pixels(data, data + (width * height * 4));
    stbi_image_free(data);
    applyMinecraftStyleOverlay(pixels, static_cast<uint32_t>(width), static_cast<uint32_t>(height), config_);
    
    return uploadAlbedoTexture(pixels, static_cast<uint32_t>(width), static_cast<uint32_t>(height));
}

bool TerrainTextures::loadLightmap(const std::filesystem::path& path) {
    LOG_SCOPE("TerrainTextures::loadLightmap");
    
    // Read file into memory (STBI_NO_STDIO is defined)
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        LOG_ERROR("Failed to open lightmap texture file: {}", path.string());
        return false;
    }
    
    auto fileSize = file.tellg();
    file.seekg(0, std::ios::beg);
    
    std::vector<uint8_t> fileData(static_cast<size_t>(fileSize));
    if (!file.read(reinterpret_cast<char*>(fileData.data()), fileSize)) {
        LOG_ERROR("Failed to read lightmap texture file: {}", path.string());
        return false;
    }
    file.close();
    
    int width, height, channels;
    uint8_t* data = stbi_load_from_memory(fileData.data(), static_cast<int>(fileData.size()),
                                           &width, &height, &channels, 1);
    
    if (!data) {
        LOG_ERROR("Failed to decode lightmap texture: {}", path.string());
        return false;
    }
    
    LOG_DEBUG("Loaded lightmap texture: {}x{} from {}", width, height, path.string());
    
    // Copy data to vector
    std::vector<uint8_t> pixels(data, data + (width * height));
    stbi_image_free(data);
    
    return uploadLightmapTexture(pixels, static_cast<uint32_t>(width), static_cast<uint32_t>(height));
}

// ─────────────────────────────────────────────────────────────────────────────
// Placeholder Texture Generation
// ─────────────────────────────────────────────────────────────────────────────

bool TerrainTextures::createPlaceholderAlbedo(uint32_t width, uint32_t height) {
    LOG_SCOPE("TerrainTextures::createPlaceholderAlbedo");
    
    auto data = generateTerrainColorData(width, height);
    applyMinecraftStyleOverlay(data, width, height, config_);
    return uploadAlbedoTexture(data, width, height);
}

bool TerrainTextures::createWhiteLightmap(uint32_t width, uint32_t height) {
    LOG_SCOPE("TerrainTextures::createWhiteLightmap");
    
    auto data = generateWhiteLightmapData(width, height);
    return uploadLightmapTexture(data, width, height);
}

// ─────────────────────────────────────────────────────────────────────────────
// Texture Upload
// ─────────────────────────────────────────────────────────────────────────────

bool TerrainTextures::uploadAlbedoTexture(const std::vector<uint8_t>& data,
                                           uint32_t width, uint32_t height) {
    // Release old texture if exists
    if (albedoView_) {
        wgpuTextureViewRelease(albedoView_);
        albedoView_ = nullptr;
    }
    if (albedoTexture_) {
        wgpuTextureRelease(albedoTexture_);
        albedoTexture_ = nullptr;
    }
    
    // Create texture
    gpu::TextureDesc texDesc = gpu::TextureDesc::tex2D(
        width, height,
        WGPUTextureFormat_RGBA8Unorm,
        WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst,
        "terrain_albedo"
    );
    
    albedoTexture_ = gpu::createTexture(device_, texDesc);
    if (!albedoTexture_) {
        LOG_ERROR("Failed to create albedo texture");
        return false;
    }
    
    // Upload data
    uint32_t bytesPerRow = width * 4;
    gpu::writeTexture(queue_, albedoTexture_,
                      std::span<const std::byte>(reinterpret_cast<const std::byte*>(data.data()), data.size()),
                      width, height, bytesPerRow);
    
    // Create texture view
    gpu::TextureViewDesc viewDesc{};
    viewDesc.label = "terrain_albedo_view";
    viewDesc.format = WGPUTextureFormat_RGBA8Unorm;
    
    albedoView_ = gpu::createTextureView(albedoTexture_, viewDesc);
    if (!albedoView_) {
        LOG_ERROR("Failed to create albedo texture view");
        return false;
    }
    
    albedoWidth_ = width;
    albedoHeight_ = height;
    
    LOG_DEBUG("Created albedo texture: {}x{}", width, height);
    return true;
}

bool TerrainTextures::uploadLightmapTexture(const std::vector<uint8_t>& data,
                                             uint32_t width, uint32_t height) {
    // Release old texture if exists
    if (lightmapView_) {
        wgpuTextureViewRelease(lightmapView_);
        lightmapView_ = nullptr;
    }
    if (lightmapTexture_) {
        wgpuTextureRelease(lightmapTexture_);
        lightmapTexture_ = nullptr;
    }
    
    // Create texture
    gpu::TextureDesc texDesc = gpu::TextureDesc::tex2D(
        width, height,
        WGPUTextureFormat_R8Unorm,
        WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst,
        "terrain_lightmap"
    );
    
    lightmapTexture_ = gpu::createTexture(device_, texDesc);
    if (!lightmapTexture_) {
        LOG_ERROR("Failed to create lightmap texture");
        return false;
    }
    
    // Upload data
    uint32_t bytesPerRow = width;
    gpu::writeTexture(queue_, lightmapTexture_,
                      std::span<const std::byte>(reinterpret_cast<const std::byte*>(data.data()), data.size()),
                      width, height, bytesPerRow);
    
    // Create texture view
    gpu::TextureViewDesc viewDesc{};
    viewDesc.label = "terrain_lightmap_view";
    viewDesc.format = WGPUTextureFormat_R8Unorm;
    
    lightmapView_ = gpu::createTextureView(lightmapTexture_, viewDesc);
    if (!lightmapView_) {
        LOG_ERROR("Failed to create lightmap texture view");
        return false;
    }
    
    lightmapWidth_ = width;
    lightmapHeight_ = height;
    
    LOG_DEBUG("Created lightmap texture: {}x{}", width, height);
    return true;
}

// ═══════════════════════════════════════════════════════════════════════════════
// Utility Functions
// ═══════════════════════════════════════════════════════════════════════════════

std::vector<uint8_t> generateTerrainColorData(uint32_t width, uint32_t height) {
    std::vector<uint8_t> data(width * height * 4);
    
    // Generate a procedural terrain color pattern for Canyon biome
    // Base colors for canyon (reddish/brownish rock)
    // Reddish-brown base
    const uint8_t baseR = 180;
    const uint8_t baseG = 110;
    const uint8_t baseB = 80;
    
    for (uint32_t y = 0; y < height; ++y) {
        for (uint32_t x = 0; x < width; ++x) {
            size_t idx = (y * width + x) * 4;
            
            // Add some subtle variation based on position
            // Using a simple pseudo-random pattern based on coordinates
            float fx = static_cast<float>(x) / static_cast<float>(width);
            float fy = static_cast<float>(y) / static_cast<float>(height);
            
            // Simple procedural noise-like variation
            float noise = std::sin(fx * 25.0f) * std::cos(fy * 25.0f) * 0.5f + 0.5f;
            // Add some banding for strata effect
            float strata = std::sin(fy * 80.0f + noise * 5.0f) * 0.5f + 0.5f;

            float variation = noise * 0.15f + strata * 0.15f;
            
            // Apply variation to base colors
            float vr = std::clamp(1.0f + variation * 0.4f, 0.8f, 1.3f);
            float vg = std::clamp(1.0f + variation * 0.3f, 0.8f, 1.2f);
            float vb = std::clamp(1.0f + variation * 0.2f, 0.8f, 1.2f);
            
            data[idx + 0] = static_cast<uint8_t>(std::clamp(baseR * vr, 0.0f, 255.0f));
            data[idx + 1] = static_cast<uint8_t>(std::clamp(baseG * vg, 0.0f, 255.0f));
            data[idx + 2] = static_cast<uint8_t>(std::clamp(baseB * vb, 0.0f, 255.0f));
            data[idx + 3] = 255;  // Full alpha
        }
    }
    return data;
}

std::vector<uint8_t> generateWhiteLightmapData(uint32_t width, uint32_t height) {
    // Create a white lightmap (full light visibility everywhere)
    std::vector<uint8_t> data(width * height, 255);
    return data;
}

std::vector<uint8_t> generateTerrainLightmapData(uint32_t width, uint32_t height,
                                                 const TerrainTextureConfig& config) {
    if (width == 0 || height == 0) {
        return {};
    }
    if (!hasHeightSamples(config)) {
        return generateWhiteLightmapData(width, height);
    }

    std::vector<uint8_t> data(static_cast<size_t>(width) * height, 255);

    const float terrainWorldWidth = static_cast<float>(config.heightmapWidth - 1u) * config.cellScale;
    const float terrainWorldHeight = static_cast<float>(config.heightmapHeight - 1u) * config.cellScale;
    const float originX = terrainWorldWidth * 0.5f;
    const float originZ = terrainWorldHeight * 0.5f;
    const float invMaxX = 1.0f / static_cast<float>(std::max(width - 1u, 1u));
    const float invMaxY = 1.0f / static_cast<float>(std::max(height - 1u, 1u));

    constexpr std::array<std::array<int32_t, 2>, 8> offsets = {{
        {{ 3,  0}}, {{-3,  0}}, {{ 0,  3}}, {{ 0, -3}},
        {{ 7,  7}}, {{-7,  7}}, {{ 7, -7}}, {{-7, -7}},
    }};
    constexpr std::array<float, 8> weights = {
        0.055f, 0.040f, 0.050f, 0.040f,
        0.035f, 0.030f, 0.030f, 0.025f,
    };

    auto sampleCoord = [&](uint32_t x, uint32_t y) -> std::array<uint32_t, 2> {
        const uint32_t hx = std::min(
            static_cast<uint32_t>(static_cast<uint64_t>(x) * (config.heightmapWidth - 1u) /
                                  std::max(width - 1u, 1u)),
            config.heightmapWidth - 1u
        );
        const uint32_t hy = std::min(
            static_cast<uint32_t>(static_cast<uint64_t>(y) * (config.heightmapHeight - 1u) /
                                  std::max(height - 1u, 1u)),
            config.heightmapHeight - 1u
        );
        return {hx, hy};
    };

    for (uint32_t y = 0; y < height; ++y) {
        const float v = static_cast<float>(y) * invMaxY;
        const float worldZ = v * terrainWorldHeight - originZ;
        for (uint32_t x = 0; x < width; ++x) {
            const float u = static_cast<float>(x) * invMaxX;
            const float worldX = u * terrainWorldWidth - originX;
            const auto coord = sampleCoord(x, y);
            const uint32_t hx = coord[0];
            const uint32_t hy = coord[1];
            const float heightM = sampleHeightMeters(config, hx, hy);
            const float slope = estimateSlope(config, hx, hy);

            const BiomeSample biome = sampleBiome(BiomeSampleInput{
                .worldX = worldX,
                .worldZ = worldZ,
                .heightM = heightM,
                .slope = slope,
            });

            float occlusion = 0.0f;
            for (size_t i = 0; i < offsets.size(); ++i) {
                const int32_t nx = std::clamp(static_cast<int32_t>(hx) + offsets[i][0],
                                              0, static_cast<int32_t>(config.heightmapWidth - 1u));
                const int32_t ny = std::clamp(static_cast<int32_t>(hy) + offsets[i][1],
                                              0, static_cast<int32_t>(config.heightmapHeight - 1u));
                const float neighborH = sampleHeightMeters(config,
                                                           static_cast<uint32_t>(nx),
                                                           static_cast<uint32_t>(ny));
                const float cellDistance = std::sqrt(
                    static_cast<float>(offsets[i][0] * offsets[i][0] + offsets[i][1] * offsets[i][1])
                ) * std::max(config.cellScale, 0.001f);
                const float rise = neighborH - heightM - cellDistance * 0.18f;
                occlusion += smoothstep(0.0f, 5.0f + cellDistance * 0.08f, rise) * weights[i];
            }

            float visibility = 1.0f;
            visibility -= std::min(occlusion, 0.34f);
            visibility -= smoothstep(0.28f, 0.90f, slope) * 0.16f;
            visibility -= biome.treeDensity * 0.08f;

            if (biome.isWater()) {
                visibility = std::max(visibility, 0.86f);
            }
            if (biome.snow > 0.02f) {
                visibility = std::max(visibility, 0.92f);
            }

            const float block = hash01(static_cast<int32_t>(hx / 6u), static_cast<int32_t>(hy / 6u));
            visibility *= 0.96f + block * 0.055f;
            visibility = std::clamp(visibility, 0.48f, 1.0f);

            data[static_cast<size_t>(y) * width + x] =
                static_cast<uint8_t>(std::round(visibility * 255.0f));
        }
    }

    return data;
}

} // namespace voxy::terrain
