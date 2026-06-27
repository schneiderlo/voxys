// ═══════════════════════════════════════════════════════════════════════════════
// textures.cpp - Terrain Texture Loading and Management Implementation
// ═══════════════════════════════════════════════════════════════════════════════

#include "terrain/textures.hpp"
#include "terrain/biomes.hpp"
#include "gpu/resources.hpp"
#include "core/log.hpp"

#include <array>
#include <algorithm>
#include <iomanip>
#include <limits>
#include <cmath>
#include <cstring>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <system_error>
#include <utility>

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

float lerp(float a, float b, float t) noexcept {
    return a + (b - a) * t;
}

float valueNoise(float x, float y, uint32_t seed = 0u) noexcept {
    const auto ix = static_cast<int32_t>(std::floor(x));
    const auto iy = static_cast<int32_t>(std::floor(y));
    const float fx = x - static_cast<float>(ix);
    const float fy = y - static_cast<float>(iy);
    const float ux = fx * fx * (3.0f - 2.0f * fx);
    const float uy = fy * fy * (3.0f - 2.0f * fy);
    const int32_t sx = static_cast<int32_t>(seed & 0xffffu);
    const int32_t sy = static_cast<int32_t>((seed >> 16u) & 0xffffu);
    const float a = hash01(ix + sx, iy + sy);
    const float b = hash01(ix + 1 + sx, iy + sy);
    const float c = hash01(ix + sx, iy + 1 + sy);
    const float d = hash01(ix + 1 + sx, iy + 1 + sy);
    return lerp(lerp(a, b, ux), lerp(c, d, ux), uy);
}

float ridgedNoise(float x, float y, uint32_t seed = 0u) noexcept {
    return 1.0f - std::abs(valueNoise(x, y, seed) * 2.0f - 1.0f);
}

float fbmNoise(float x, float y, uint32_t seed = 0u) noexcept {
    float sum = 0.0f;
    float amp = 0.5f;
    float frequency = 1.0f;
    float normalizer = 0.0f;
    for (uint32_t octave = 0; octave < 4u; ++octave) {
        sum += valueNoise(x * frequency, y * frequency, seed + octave * 0x9e37u) * amp;
        normalizer += amp;
        amp *= 0.5f;
        frequency *= 2.0f;
    }
    return normalizer > 0.0f ? sum / normalizer : 0.0f;
}

float stripePattern(float value, float width = 0.20f) noexcept {
    const float f = std::abs(std::sin(value));
    const float band = 1.0f - smoothstep(width, width + 0.42f, f);
    return band * band * (3.0f - 2.0f * band);
}

uint32_t wrapIndex(int32_t value, uint32_t size) noexcept {
    const int32_t wrapped = value % static_cast<int32_t>(size);
    return static_cast<uint32_t>(wrapped < 0 ? wrapped + static_cast<int32_t>(size) : wrapped);
}

std::array<float, 2> warpedMaterialCoord(float worldX,
                                         float worldY,
                                         float scale,
                                         float warpAmount,
                                         uint32_t seed) noexcept {
    const float warpX = (fbmNoise(worldX * 0.014f + 17.0f,
                                  worldY * 0.014f - 29.0f,
                                  seed) - 0.5f) * warpAmount;
    const float warpY = (fbmNoise(worldX * 0.014f - 53.0f,
                                  worldY * 0.014f + 11.0f,
                                  seed ^ 0x62756d70u) - 0.5f) * warpAmount;

    const float wx = worldX + warpX;
    const float wy = worldY + warpY;
    return {
        (wx * 0.8660254f + wy * 0.5f) * scale,
        (wy * 0.8660254f - wx * 0.5f) * scale,
    };
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
        case BiomeSurfaceRole::Grass:
        case BiomeSurfaceRole::Meadow:
        case BiomeSurfaceRole::Tundra: return pack.grass.valid() ? &pack.grass : nullptr;
        case BiomeSurfaceRole::Forest:
        case BiomeSurfaceRole::Taiga:
        case BiomeSurfaceRole::Jungle:
        case BiomeSurfaceRole::Swamp: return pack.forest.valid() ? &pack.forest : nullptr;
        case BiomeSurfaceRole::Dry:
        case BiomeSurfaceRole::Savanna:
        case BiomeSurfaceRole::Badlands:
        case BiomeSurfaceRole::Beach: return pack.dry.valid() ? &pack.dry : nullptr;
        case BiomeSurfaceRole::Rock: return pack.rock.valid() ? &pack.rock : nullptr;
        case BiomeSurfaceRole::Snow: return pack.snow.valid() ? &pack.snow : nullptr;
        case BiomeSurfaceRole::Water: return nullptr;
        default: return nullptr;
    }
}

std::array<float, 3> sampleColorMap(const ColorMap& map, float worldX, float worldY) noexcept {
    const auto uv = warpedMaterialCoord(worldX, worldY, 0.23f, 23.0f, 0x746578u);
    const float u = uv[0];
    const float v = uv[1];
    const auto ix = static_cast<int32_t>(std::floor(u));
    const auto iy = static_cast<int32_t>(std::floor(v));
    const auto x0 = wrapIndex(ix, map.width);
    const auto y0 = wrapIndex(iy, map.height);
    const auto x1 = wrapIndex(ix + 1, map.width);
    const auto y1 = wrapIndex(iy + 1, map.height);
    const float tx = u - std::floor(u);
    const float ty = v - std::floor(v);

    auto sample = [&](uint32_t sx, uint32_t sy) {
        const size_t idx = (static_cast<size_t>(sy) * map.width + sx) * 3u;
        return std::array<float, 3>{
            static_cast<float>(map.pixels[idx + 0]) / 255.0f,
            static_cast<float>(map.pixels[idx + 1]) / 255.0f,
            static_cast<float>(map.pixels[idx + 2]) / 255.0f,
        };
    };

    const auto c00 = sample(x0, y0);
    const auto c10 = sample(x1, y0);
    const auto c01 = sample(x0, y1);
    const auto c11 = sample(x1, y1);
    return {
        lerp(lerp(c00[0], c10[0], tx), lerp(c01[0], c11[0], tx), ty),
        lerp(lerp(c00[1], c10[1], tx), lerp(c01[1], c11[1], tx), ty),
        lerp(lerp(c00[2], c10[2], tx), lerp(c01[2], c11[2], tx), ty),
    };
}

struct HashAccumulator {
    uint64_t value = 1469598103934665603ull;

    void addBytes(const void* data, size_t size) noexcept {
        const auto* bytes = static_cast<const uint8_t*>(data);
        for (size_t i = 0; i < size; ++i) {
            value ^= bytes[i];
            value *= 1099511628211ull;
        }
    }

    template <typename T>
    void addPod(const T& pod) noexcept {
        addBytes(&pod, sizeof(T));
    }

    void addString(std::string_view text) noexcept {
        addBytes(text.data(), text.size());
        const uint8_t terminator = 0xffu;
        addBytes(&terminator, 1);
    }
};

std::string hex64(uint64_t value) {
    std::ostringstream out;
    out << std::hex << std::setw(16) << std::setfill('0') << value;
    return out.str();
}

void addPathFingerprint(HashAccumulator& hash, const std::filesystem::path& path) {
    hash.addString(path.generic_string());
    std::error_code ec;
    const bool exists = std::filesystem::exists(path, ec);
    hash.addPod(exists);
    if (!exists || ec) {
        return;
    }

    const auto size = std::filesystem::file_size(path, ec);
    if (!ec) {
        hash.addPod(size);
    }

    const auto writeTime = std::filesystem::last_write_time(path, ec);
    if (!ec) {
        const auto count = writeTime.time_since_epoch().count();
        hash.addPod(count);
    }
}

bool textureCacheEnabled(const TerrainTextureConfig& config) noexcept {
#if defined(VOXY_WASM)
    return false;
#else
    return config.enableGeneratedTextureCache &&
           config.minecraftStyleEnhancement &&
           !config.generatedTextureCacheDir.empty() &&
           hasHeightSamples(config);
#endif
}

std::string makeTextureCacheKey(const TerrainTextureConfig& config) {
    if (!textureCacheEnabled(config)) {
        return {};
    }

    HashAccumulator hash;
    hash.addString("voxy-terrain-texture-cache-v10");
    addPathFingerprint(hash, config.albedoPath);
    addPathFingerprint(hash, config.lightmapPath);
    addPathFingerprint(hash, "data/generated/cc0_texture_pack/manifest.json");
    addPathFingerprint(hash, "data/generated/cc0_texture_pack/maps/grass_color.jpg");
    addPathFingerprint(hash, "data/generated/cc0_texture_pack/maps/forest_floor_color.jpg");
    addPathFingerprint(hash, "data/generated/cc0_texture_pack/maps/dry_ground_color.jpg");
    addPathFingerprint(hash, "data/generated/cc0_texture_pack/maps/rock_color.jpg");
    addPathFingerprint(hash, "data/generated/cc0_texture_pack/maps/snow_color.jpg");

    hash.addPod(config.placeholderWidth);
    hash.addPod(config.placeholderHeight);
    hash.addPod(config.heightmapWidth);
    hash.addPod(config.heightmapHeight);
    hash.addPod(config.heightScale);
    hash.addPod(config.cellScale);
    hash.addPod(config.minecraftStyleEnhancement);
    hash.addPod(config.generatedLightmapMaxSize);
    hash.addBytes(config.heightSamples.data(), config.heightSamples.size_bytes());
    return hex64(hash.value);
}

std::filesystem::path textureCachePath(const TerrainTextureConfig& config,
                                       std::string_view key,
                                       std::string_view kind) {
    std::string filename;
    filename.reserve(key.size() + kind.size() + 16u);
    filename.append(key);
    filename.push_back('_');
    filename.append(kind);
    filename.append((kind == "albedo" || kind == "normal") ? ".rgba8" : ".r8");
    return config.generatedTextureCacheDir / filename;
}

struct CachedTexture {
    std::vector<uint8_t> pixels;
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t channels = 0;
};

std::optional<CachedTexture> readTextureCache(const TerrainTextureConfig& config,
                                              std::string_view key,
                                              std::string_view kind,
                                              uint32_t expectedChannels) {
    if (key.empty()) {
        return std::nullopt;
    }

    const auto path = textureCachePath(config, key, kind);
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        return std::nullopt;
    }

    std::string line;
    if (!std::getline(file, line) || line != "VOXY_TEXTURE_CACHE_V1") {
        return std::nullopt;
    }

    CachedTexture cached;
    std::string readKey;
    std::string readKind;
    while (std::getline(file, line)) {
        if (line == "END") {
            break;
        }
        const auto eq = line.find('=');
        if (eq == std::string::npos) {
            return std::nullopt;
        }
        const std::string name = line.substr(0, eq);
        const std::string value = line.substr(eq + 1);
        try {
            if (name == "key") {
                readKey = value;
            } else if (name == "kind") {
                readKind = value;
            } else if (name == "width") {
                cached.width = static_cast<uint32_t>(std::stoul(value));
            } else if (name == "height") {
                cached.height = static_cast<uint32_t>(std::stoul(value));
            } else if (name == "channels") {
                cached.channels = static_cast<uint32_t>(std::stoul(value));
            }
        } catch (...) {
            return std::nullopt;
        }
    }

    if (readKey != key || readKind != kind ||
        cached.width == 0 || cached.height == 0 ||
        cached.channels != expectedChannels) {
        return std::nullopt;
    }

    const uint64_t byteCount = static_cast<uint64_t>(cached.width) *
                               static_cast<uint64_t>(cached.height) *
                               static_cast<uint64_t>(cached.channels);
    if (byteCount == 0 || byteCount > std::numeric_limits<size_t>::max()) {
        return std::nullopt;
    }

    cached.pixels.resize(static_cast<size_t>(byteCount));
    if (!file.read(reinterpret_cast<char*>(cached.pixels.data()),
                   static_cast<std::streamsize>(cached.pixels.size()))) {
        return std::nullopt;
    }

    return cached;
}

void writeTextureCache(const TerrainTextureConfig& config,
                       std::string_view key,
                       std::string_view kind,
                       uint32_t width,
                       uint32_t height,
                       uint32_t channels,
                       const std::vector<uint8_t>& pixels) {
    if (key.empty() || pixels.size() != static_cast<size_t>(width) * height * channels) {
        return;
    }

    std::error_code ec;
    std::filesystem::create_directories(config.generatedTextureCacheDir, ec);
    if (ec) {
        LOG_WARN("Failed to create terrain texture cache directory '{}': {}",
                 config.generatedTextureCacheDir.string(), ec.message());
        return;
    }

    const auto finalPath = textureCachePath(config, key, kind);
    auto tempPath = finalPath;
    tempPath += ".tmp";

    {
        std::ofstream file(tempPath, std::ios::binary | std::ios::trunc);
        if (!file.is_open()) {
            LOG_WARN("Failed to open terrain texture cache file for writing: {}", tempPath.string());
            return;
        }

        file << "VOXY_TEXTURE_CACHE_V1\n";
        file << "key=" << key << "\n";
        file << "kind=" << kind << "\n";
        file << "width=" << width << "\n";
        file << "height=" << height << "\n";
        file << "channels=" << channels << "\n";
        file << "END\n";
        file.write(reinterpret_cast<const char*>(pixels.data()),
                   static_cast<std::streamsize>(pixels.size()));
        if (!file.good()) {
            LOG_WARN("Failed while writing terrain texture cache file: {}", tempPath.string());
            std::filesystem::remove(tempPath, ec);
            return;
        }
    }

    std::filesystem::remove(finalPath, ec);
    ec.clear();
    std::filesystem::rename(tempPath, finalPath, ec);
    if (ec) {
        LOG_WARN("Failed to publish terrain texture cache file '{}': {}",
                 finalPath.string(), ec.message());
        std::filesystem::remove(tempPath, ec);
    }
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
            const float sourceWaterLike = smoothstep(0.02f, 0.22f, b - maxRG * 0.72f);
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
            const float moistureBias = std::clamp(greenBias * 0.18f, -0.08f, 0.08f);
            const BiomeSample biome = sampleBiome(BiomeSampleInput{
                .worldX = worldX,
                .worldZ = worldZ,
                .heightM = heightM,
                .slope = slope,
                .surfaceMoistureBias = moistureBias,
            });
            const float ecotoneProbe = std::clamp(
                std::max(terrainWorldWidth, terrainWorldHeight) * 0.0045f,
                18.0f,
                64.0f
            );
            const BiomeSample biomeEast = sampleBiome(BiomeSampleInput{
                .worldX = worldX + ecotoneProbe,
                .worldZ = worldZ,
                .heightM = heightM,
                .slope = slope,
                .surfaceMoistureBias = moistureBias,
            });
            const BiomeSample biomeNorth = sampleBiome(BiomeSampleInput{
                .worldX = worldX,
                .worldZ = worldZ + ecotoneProbe,
                .heightM = heightM,
                .slope = slope,
                .surfaceMoistureBias = moistureBias,
            });
            const float waterLike = clamp01(std::max(sourceWaterLike, biome.water));
            if (useHeight) {
                const float reliefShade = 0.94f + std::clamp((brightness - 0.50f) * 0.18f, -0.08f, 0.09f);
                const float sourceCarry = biome.isWater() ? 0.10f : 0.035f;
                r = lerp(biome.colorR * reliefShade, r, sourceCarry);
                g = lerp(biome.colorG * reliefShade, g, sourceCarry);
                b = lerp(biome.colorB * reliefShade, b, sourceCarry);
            }

            const float biomeStrength = useHeight ? 0.24f : 0.34f;
            blend(biome.colorR, biome.colorG, biome.colorB, biomeStrength);
            if (const ColorMap* colorMap = colorMapForRole(materialPack, biome.role)) {
                const auto material = sampleColorMap(*colorMap, worldX, worldZ);
                blend(material[0], material[1], material[2], useHeight ? 0.34f : 0.18f);
            }

            const float ecotoneRoleMix =
                (biomeEast.role != biome.role ? 0.50f : 0.0f) +
                (biomeNorth.role != biome.role ? 0.50f : 0.0f);
            if (ecotoneRoleMix > 0.0f && biome.role != BiomeSurfaceRole::Water) {
                const float edgeNoise = ridgedNoise(worldX * 0.035f + 5.0f,
                                                    worldZ * 0.035f - 11.0f,
                                                    0x45646765u);
                const float ecotone = ecotoneRoleMix *
                                      smoothstep(0.40f, 0.86f, edgeNoise) *
                                      (1.0f - waterLike) *
                                      (1.0f - smoothstep(0.72f, 1.08f, slope));
                const float avgR = (biome.colorR + biomeEast.colorR + biomeNorth.colorR) / 3.0f;
                const float avgG = (biome.colorG + biomeEast.colorG + biomeNorth.colorG) / 3.0f;
                const float avgB = (biome.colorB + biomeEast.colorB + biomeNorth.colorB) / 3.0f;
                blend(avgR, avgG, avgB, ecotone * 0.20f);

                switch (biome.role) {
                    case BiomeSurfaceRole::Forest:
                    case BiomeSurfaceRole::Taiga:
                    case BiomeSurfaceRole::Jungle:
                    case BiomeSurfaceRole::Swamp:
                        blend(0.13f, 0.18f, 0.075f, ecotone * 0.18f);
                        break;
                    case BiomeSurfaceRole::Meadow:
                    case BiomeSurfaceRole::Grass:
                        blend(0.62f, 0.60f, 0.26f, ecotone * 0.16f);
                        break;
                    case BiomeSurfaceRole::Savanna:
                    case BiomeSurfaceRole::Dry:
                        blend(0.58f, 0.46f, 0.19f, ecotone * 0.18f);
                        break;
                    case BiomeSurfaceRole::Badlands:
                        blend(0.70f, 0.31f, 0.16f, ecotone * 0.18f);
                        break;
                    case BiomeSurfaceRole::Tundra:
                    case BiomeSurfaceRole::Snow:
                        blend(0.72f, 0.78f, 0.68f, ecotone * 0.20f);
                        break;
                    case BiomeSurfaceRole::Beach:
                        blend(0.76f, 0.67f, 0.45f, ecotone * 0.18f);
                        break;
                    case BiomeSurfaceRole::Rock:
                    case BiomeSurfaceRole::Water:
                        break;
                }
            }

            const float continental = valueNoise(worldX * 0.010f + 31.0f,
                                                 worldZ * 0.010f - 17.0f,
                                                 0x4d617465u);
            const float local = valueNoise(worldX * 0.072f - 9.0f,
                                           worldZ * 0.072f + 43.0f,
                                           0x44657461u);
            const float fine = hash01(static_cast<int32_t>(std::floor(worldX * 1.15f)),
                                      static_cast<int32_t>(std::floor(worldZ * 1.15f)));
            const float micro = fbmNoise(worldX * 0.36f + 101.0f,
                                         worldZ * 0.36f - 77.0f,
                                         0x4d696372u);
            const float materialVariation = 0.94f + 0.06f * continental + 0.03f * local + 0.018f * micro;
            r *= materialVariation;
            g *= materialVariation;
            b *= materialVariation;

            const float broadPatch = fbmNoise(worldX * 0.024f - 8.0f,
                                              worldZ * 0.024f + 19.0f,
                                              0x50617463u);
            const float tint = 0.980f + 0.040f * broadPatch;
            r *= tint;
            g *= tint;
            b *= tint;

            if (waterLike > 0.02f) {
                const float wave = 0.94f + 0.08f * valueNoise(worldX * 0.14f, worldZ * 0.14f, 0x57617665u);
                blend(0.05f * wave, 0.28f * wave, 0.43f * wave, waterLike * 0.70f);
            }

            if (biome.water > 0.02f) {
                blend(biome.colorR, biome.colorG, biome.colorB, biome.water * 0.95f);
            }

            const float riverInfluence = smoothstep(0.08f, 0.55f, biome.river);
            if (riverInfluence > 0.01f && waterLike < 0.92f) {
                const float bank = riverInfluence *
                                   (1.0f - waterLike) *
                                   (1.0f - smoothstep(0.50f, 1.05f, slope));
                const float gravel = valueNoise(worldX * 0.42f + 7.0f,
                                                worldZ * 0.42f - 3.0f,
                                                0x4772766cu);
                const float pebble = hash01(static_cast<int32_t>(std::floor(worldX * 1.75f)) + 37,
                                            static_cast<int32_t>(std::floor(worldZ * 1.75f)) - 91);
                blend(0.36f, 0.33f, 0.26f, bank * (0.16f + gravel * 0.18f));
                blend(0.16f, 0.17f, 0.14f, bank * smoothstep(0.78f, 1.0f, pebble) * 0.20f);
                blend(0.05f, 0.13f, 0.13f, riverInfluence * (1.0f - waterLike) * 0.10f);
            }

            const float dryGrass = clamp01((r - g) * 1.8f + 0.20f) * (1.0f - waterLike) * (1.0f - snowLike);
            blend(0.48f, 0.44f, 0.25f, dryGrass * 0.22f);

            const float rockLike = smoothstep(0.18f, 0.42f, brightness) * (1.0f - smoothstep(0.18f, 0.34f, chroma));
            blend(0.42f, 0.41f, 0.37f, rockLike * 0.18f * (1.0f - waterLike));

            switch (biome.role) {
                case BiomeSurfaceRole::Grass:
                case BiomeSurfaceRole::Meadow:
                case BiomeSurfaceRole::Forest: {
                    const float meadow = ridgedNoise(worldX * 0.060f, worldZ * 0.060f, 0x47726173u);
                    const float tuft = smoothstep(0.56f, 0.90f, meadow) *
                                       (1.0f - smoothstep(0.35f, 0.74f, slope)) *
                                       (1.0f - biome.river) * (1.0f - waterLike);
                    const bool forest = biome.role == BiomeSurfaceRole::Forest;
                    const bool meadowRole = biome.role == BiomeSurfaceRole::Meadow;
                    blend(0.10f, forest ? 0.24f : 0.34f, 0.08f,
                          tuft * (forest ? 0.26f : 0.18f));
                    if (forest) {
                        const float leafLitter = ridgedNoise(worldX * 0.115f,
                                                             worldZ * 0.115f,
                                                             0x4c656166u);
                        const float forestFloor = leafLitter *
                                                  (1.0f - smoothstep(0.36f, 0.82f, slope)) *
                                                  (1.0f - waterLike);
                        blend(0.19f, 0.13f, 0.065f, forestFloor * 0.22f);
                        const float mushroom = hash01(static_cast<int32_t>(std::floor(worldX * 0.92f)) + 503,
                                                      static_cast<int32_t>(std::floor(worldZ * 0.92f)) - 719);
                        if (mushroom > 0.992f && slope < 0.34f && waterLike < 0.02f) {
                            blend(0.72f, 0.24f, 0.13f, 0.18f);
                        }
                    }

                    const float flower = hash01(static_cast<int32_t>(std::floor(worldX * 0.75f)) + 211,
                                                static_cast<int32_t>(std::floor(worldZ * 0.75f)) - 43);
                    if ((biome.role == BiomeSurfaceRole::Grass || meadowRole) &&
                        flower > (meadowRole ? 0.968f : 0.992f) &&
                        slope < 0.28f && waterLike < 0.02f) {
                        blend(meadowRole ? 0.92f : 0.86f, 0.72f, meadowRole ? 0.36f : 0.28f,
                              meadowRole ? 0.20f : 0.16f);
                    }
                    break;
                }
                case BiomeSurfaceRole::Taiga: {
                    const float needleLitter = ridgedNoise(worldX * 0.075f, worldZ * 0.075f, 0x54616967u);
                    blend(0.08f, 0.22f, 0.12f, needleLitter * 0.28f * (1.0f - waterLike));
                    blend(0.28f, 0.22f, 0.12f, smoothstep(0.68f, 0.96f, local) * 0.12f);
                    break;
                }
                case BiomeSurfaceRole::Jungle: {
                    const float undergrowth = ridgedNoise(worldX * 0.095f, worldZ * 0.095f, 0x4a756e67u);
                    blend(0.035f, 0.24f, 0.055f, undergrowth * 0.40f * (1.0f - waterLike));
                    blend(0.12f, 0.08f, 0.035f, smoothstep(0.58f, 0.92f, local) * 0.20f);
                    break;
                }
                case BiomeSurfaceRole::Swamp: {
                    const auto reedUv = warpedMaterialCoord(worldX, worldZ, 0.080f, 14.0f, 0x52656564u);
                    const float reeds = smoothstep(0.58f, 0.93f,
                                                   ridgedNoise(reedUv[0], reedUv[1], 0x52656564u));
                    blend(0.035f, 0.18f, 0.08f, 0.34f * (1.0f - waterLike));
                    blend(0.14f, 0.24f, 0.10f, reeds * 0.16f * (1.0f - waterLike));
                    blend(0.04f, 0.22f, 0.24f, smoothstep(-0.8f, 4.0f, heightM) * 0.10f);
                    break;
                }
                case BiomeSurfaceRole::Dry: {
                    const float scrub = ridgedNoise(worldX * 0.045f, worldZ * 0.045f, 0x44727921u);
                    blend(0.64f, 0.56f, 0.28f, scrub * 0.16f * (1.0f - waterLike));
                    break;
                }
                case BiomeSurfaceRole::Savanna: {
                    const float straw = ridgedNoise(worldX * 0.055f, worldZ * 0.055f, 0x53617661u);
                    blend(0.58f, 0.52f, 0.22f, straw * 0.24f * (1.0f - waterLike));
                    blend(0.22f, 0.28f, 0.08f, smoothstep(0.70f, 0.96f, local) * 0.12f);
                    break;
                }
                case BiomeSurfaceRole::Badlands: {
                    const auto strataUv = warpedMaterialCoord(worldX, worldZ, 0.018f, 31.0f, 0x4261646cu);
                    const float strata = stripePattern(heightM * 0.22f + strataUv[0] * 2.3f, 0.16f);
                    const float redBand = stripePattern(heightM * 0.13f + strataUv[1] * 1.8f, 0.24f);
                    blend(0.68f, 0.36f, 0.18f, strata * 0.07f * (1.0f - waterLike));
                    blend(0.42f, 0.22f, 0.13f, redBand * 0.05f * (1.0f - waterLike));
                    break;
                }
                case BiomeSurfaceRole::Rock: {
                    const auto strataUv = warpedMaterialCoord(worldX, worldZ, 0.016f, 22.0f, 0x526f636bu);
                    const float strata = stripePattern(heightM * 0.18f + strataUv[0] * 2.2f + strataUv[1] * 1.1f,
                                                       0.16f);
                    const float scree = smoothstep(0.72f, 0.98f,
                                                   ridgedNoise(worldX * 0.18f, worldZ * 0.18f, 0x526f636bu));
                    blend(0.56f, 0.55f, 0.50f, strata * 0.045f * (1.0f - waterLike));
                    blend(0.25f, 0.25f, 0.23f, scree * 0.10f * (1.0f - waterLike));
                    break;
                }
                case BiomeSurfaceRole::Snow: {
                    const float grain = valueNoise(worldX * 0.20f, worldZ * 0.20f, 0x536e6f77u);
                    blend(0.92f, 0.95f, 0.91f, (0.58f + grain * 0.24f) * (1.0f - waterLike));
                    blend(0.70f, 0.78f, 0.86f, smoothstep(0.45f, 0.90f, slope) * 0.18f);
                    break;
                }
                case BiomeSurfaceRole::Tundra: {
                    const float moss = ridgedNoise(worldX * 0.070f, worldZ * 0.070f, 0x54756e64u);
                    blend(0.38f, 0.46f, 0.31f, moss * 0.20f * (1.0f - waterLike));
                    blend(0.78f, 0.82f, 0.74f, biome.snow * 0.36f);
                    break;
                }
                case BiomeSurfaceRole::Beach: {
                    const auto rippleUv = warpedMaterialCoord(worldX, worldZ, 0.060f, 9.0f, 0x42656163u);
                    const float ripples = stripePattern(rippleUv[0] * 4.0f + local * 1.2f, 0.18f);
                    const float shells = smoothstep(0.965f, 1.0f, fine);
                    blend(0.76f, 0.69f, 0.49f, ripples * 0.04f * (1.0f - waterLike));
                    blend(0.58f, 0.55f, 0.45f, shells * 0.12f * (1.0f - waterLike));
                    break;
                }
                case BiomeSurfaceRole::Water: {
                    const float shelf = 1.0f - smoothstep(-18.0f, -3.0f, heightM);
                    blend(0.02f, 0.18f, 0.29f, shelf * 0.26f + biome.water * 0.22f);
                    break;
                }
            }

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
                float canopyR = 0.05f;
                float canopyG = 0.18f;
                float canopyB = 0.06f;
                if (biome.role == BiomeSurfaceRole::Jungle) {
                    canopyR = 0.025f;
                    canopyG = 0.22f;
                    canopyB = 0.045f;
                } else if (biome.role == BiomeSurfaceRole::Savanna ||
                           biome.role == BiomeSurfaceRole::Badlands) {
                    canopyR = 0.17f;
                    canopyG = 0.23f;
                    canopyB = 0.055f;
                } else if (biome.role == BiomeSurfaceRole::Taiga ||
                           biome.role == BiomeSurfaceRole::Tundra) {
                    canopyR = 0.04f;
                    canopyG = 0.16f;
                    canopyB = 0.09f;
                } else if (biome.role == BiomeSurfaceRole::Swamp) {
                    canopyR = 0.035f;
                    canopyG = 0.15f;
                    canopyB = 0.065f;
                }
                blend(canopyR, canopyG, canopyB, canopy * 0.88f);

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
    , normalTexture_(other.normalTexture_)
    , normalView_(other.normalView_)
    , normalWidth_(other.normalWidth_)
    , normalHeight_(other.normalHeight_)
    , sampler_(other.sampler_)
    , config_(other.config_)
    , generatedCacheKey_(std::move(other.generatedCacheKey_))
{
    // Null out the source
    other.device_ = nullptr;
    other.queue_ = nullptr;
    other.albedoTexture_ = nullptr;
    other.albedoView_ = nullptr;
    other.lightmapTexture_ = nullptr;
    other.lightmapView_ = nullptr;
    other.normalTexture_ = nullptr;
    other.normalView_ = nullptr;
    other.sampler_ = nullptr;
    other.generatedCacheKey_.clear();
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
        normalTexture_ = other.normalTexture_;
        normalView_ = other.normalView_;
        normalWidth_ = other.normalWidth_;
        normalHeight_ = other.normalHeight_;
        sampler_ = other.sampler_;
        config_ = other.config_;
        generatedCacheKey_ = std::move(other.generatedCacheKey_);
        
        other.device_ = nullptr;
        other.queue_ = nullptr;
        other.albedoTexture_ = nullptr;
        other.albedoView_ = nullptr;
        other.lightmapTexture_ = nullptr;
        other.lightmapView_ = nullptr;
        other.normalTexture_ = nullptr;
        other.normalView_ = nullptr;
        other.sampler_ = nullptr;
        other.generatedCacheKey_.clear();
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
    if (normalView_) {
        wgpuTextureViewRelease(normalView_);
        normalView_ = nullptr;
    }
    if (normalTexture_) {
        wgpuTextureRelease(normalTexture_);
        normalTexture_ = nullptr;
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
    normalWidth_ = 0;
    normalHeight_ = 0;
    generatedCacheKey_.clear();
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
    generatedCacheKey_ = makeTextureCacheKey(config_);
    
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

        bool uploadedLightmap = false;
        if (auto cached = readTextureCache(config_, generatedCacheKey_, "lightmap", 1u)) {
            LOG_INFO("Loaded cached generated terrain lightmap: {}x{}",
                     cached->width, cached->height);
            uploadedLightmap = uploadLightmapTexture(cached->pixels, cached->width, cached->height);
        }

        if (!uploadedLightmap) {
            auto lightmap = generateTerrainLightmapData(lightmapWidth, lightmapHeight, config);
            writeTextureCache(config_, generatedCacheKey_, "lightmap",
                              lightmapWidth, lightmapHeight, 1u, lightmap);
            uploadedLightmap = uploadLightmapTexture(lightmap, lightmapWidth, lightmapHeight);
        }

        if (!uploadedLightmap) {
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

    if (config.minecraftStyleEnhancement && hasHeightSamples(config)) {
        const uint32_t maxSize = std::max(config.generatedLightmapMaxSize, 1u);
        const float aspect = static_cast<float>(config.heightmapWidth) /
                             static_cast<float>(std::max(config.heightmapHeight, 1u));
        uint32_t normalWidth = std::min(config.heightmapWidth, maxSize);
        uint32_t normalHeight = std::min(config.heightmapHeight, maxSize);
        if (aspect >= 1.0f) {
            normalHeight = std::max(1u, static_cast<uint32_t>(
                std::round(static_cast<float>(normalWidth) / aspect)));
        } else {
            normalWidth = std::max(1u, static_cast<uint32_t>(
                std::round(static_cast<float>(normalHeight) * aspect)));
        }

        bool uploadedNormal = false;
        if (auto cached = readTextureCache(config_, generatedCacheKey_, "normal", 4u)) {
            LOG_INFO("Loaded cached generated terrain normal map: {}x{}",
                     cached->width, cached->height);
            uploadedNormal = uploadNormalTexture(cached->pixels, cached->width, cached->height);
        }

        if (!uploadedNormal) {
            auto normalMap = generateTerrainNormalData(normalWidth, normalHeight, config);
            writeTextureCache(config_, generatedCacheKey_, "normal",
                              normalWidth, normalHeight, 4u, normalMap);
            uploadedNormal = uploadNormalTexture(normalMap, normalWidth, normalHeight);
        }

        if (!uploadedNormal) {
            LOG_WARN("Failed to upload generated terrain normal map, creating flat normal map");
            std::vector<uint8_t> flatNormal = {128u, 255u, 128u, 255u};
            if (!uploadNormalTexture(flatNormal, 1, 1)) {
                LOG_ERROR("Failed to create flat normal texture");
                shutdown();
                return false;
            }
        }
    } else {
        std::vector<uint8_t> flatNormal = {128u, 255u, 128u, 255u};
        if (!uploadNormalTexture(flatNormal, 1, 1)) {
            LOG_ERROR("Failed to create flat normal texture");
            shutdown();
            return false;
        }
    }
    
    LOG_INFO("TerrainTextures initialized: albedo {}x{}, lightmap {}x{}, normal {}x{}",
             albedoWidth_, albedoHeight_, lightmapWidth_, lightmapHeight_, normalWidth_, normalHeight_);
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

    if (auto cached = readTextureCache(config_, generatedCacheKey_, "albedo", 4u)) {
        LOG_INFO("Loaded cached generated terrain albedo: {}x{}",
                 cached->width, cached->height);
        return uploadAlbedoTexture(cached->pixels, cached->width, cached->height);
    }
    
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
    writeTextureCache(config_, generatedCacheKey_, "albedo",
                      static_cast<uint32_t>(width), static_cast<uint32_t>(height), 4u, pixels);
    
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

    if (auto cached = readTextureCache(config_, generatedCacheKey_, "albedo", 4u)) {
        LOG_INFO("Loaded cached generated terrain albedo: {}x{}",
                 cached->width, cached->height);
        return uploadAlbedoTexture(cached->pixels, cached->width, cached->height);
    }
    
    auto data = generateTerrainColorData(width, height);
    applyMinecraftStyleOverlay(data, width, height, config_);
    writeTextureCache(config_, generatedCacheKey_, "albedo", width, height, 4u, data);
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

bool TerrainTextures::uploadNormalTexture(const std::vector<uint8_t>& data,
                                           uint32_t width,
                                           uint32_t height) {
    if (normalView_) {
        wgpuTextureViewRelease(normalView_);
        normalView_ = nullptr;
    }
    if (normalTexture_) {
        wgpuTextureRelease(normalTexture_);
        normalTexture_ = nullptr;
    }

    gpu::TextureDesc texDesc = gpu::TextureDesc::tex2D(
        width, height,
        WGPUTextureFormat_RGBA8Unorm,
        WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst,
        "terrain_normal"
    );

    normalTexture_ = gpu::createTexture(device_, texDesc);
    if (!normalTexture_) {
        LOG_ERROR("Failed to create normal texture");
        return false;
    }

    const uint32_t bytesPerRow = width * 4u;
    gpu::writeTexture(queue_, normalTexture_,
                      std::span<const std::byte>(reinterpret_cast<const std::byte*>(data.data()), data.size()),
                      width, height, bytesPerRow);

    gpu::TextureViewDesc viewDesc{};
    viewDesc.label = "terrain_normal_view";
    viewDesc.format = WGPUTextureFormat_RGBA8Unorm;

    normalView_ = gpu::createTextureView(normalTexture_, viewDesc);
    if (!normalView_) {
        LOG_ERROR("Failed to create normal texture view");
        return false;
    }

    normalWidth_ = width;
    normalHeight_ = height;

    LOG_DEBUG("Created normal texture: {}x{}", width, height);
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

            const float lightGrain = valueNoise(worldX * 0.060f - 13.0f,
                                                worldZ * 0.060f + 37.0f,
                                                0x4c696768u);
            visibility *= 0.975f + lightGrain * 0.030f;
            visibility = std::clamp(visibility, 0.48f, 1.0f);

            data[static_cast<size_t>(y) * width + x] =
                static_cast<uint8_t>(std::round(visibility * 255.0f));
        }
    }

    return data;
}

std::vector<uint8_t> generateTerrainNormalData(uint32_t width, uint32_t height,
                                               const TerrainTextureConfig& config) {
    if (width == 0 || height == 0) {
        return {};
    }

    std::vector<uint8_t> data(static_cast<size_t>(width) * height * 4u, 255u);
    if (!hasHeightSamples(config)) {
        for (uint32_t y = 0; y < height; ++y) {
            for (uint32_t x = 0; x < width; ++x) {
                const size_t idx = (static_cast<size_t>(y) * width + x) * 4u;
                data[idx + 0] = 128u;
                data[idx + 1] = 255u;
                data[idx + 2] = 128u;
                data[idx + 3] = 255u;
            }
        }
        return data;
    }

    const uint32_t sampleStepX = std::max(1u, config.heightmapWidth / std::max(width, 1u));
    const uint32_t sampleStepY = std::max(1u, config.heightmapHeight / std::max(height, 1u));
    const float worldStepX = static_cast<float>(sampleStepX) * std::max(config.cellScale, 0.001f);
    const float worldStepY = static_cast<float>(sampleStepY) * std::max(config.cellScale, 0.001f);
    const float invMaxX = 1.0f / static_cast<float>(std::max(width - 1u, 1u));
    const float invMaxY = 1.0f / static_cast<float>(std::max(height - 1u, 1u));

    auto sampleCoord = [&](uint32_t x, uint32_t y) -> std::array<uint32_t, 2> {
        const uint32_t hx = std::min(
            static_cast<uint32_t>(std::round(static_cast<float>(x) * invMaxX *
                                             static_cast<float>(config.heightmapWidth - 1u))),
            config.heightmapWidth - 1u
        );
        const uint32_t hy = std::min(
            static_cast<uint32_t>(std::round(static_cast<float>(y) * invMaxY *
                                             static_cast<float>(config.heightmapHeight - 1u))),
            config.heightmapHeight - 1u
        );
        return {hx, hy};
    };

    auto sampleOffset = [&](uint32_t hx, uint32_t hy, int32_t dx, int32_t dy) {
        const int32_t x = std::clamp(static_cast<int32_t>(hx) + dx,
                                     0, static_cast<int32_t>(config.heightmapWidth - 1u));
        const int32_t y = std::clamp(static_cast<int32_t>(hy) + dy,
                                     0, static_cast<int32_t>(config.heightmapHeight - 1u));
        return sampleHeightMeters(config, static_cast<uint32_t>(x), static_cast<uint32_t>(y));
    };

    auto encodeNormal = [](float n) {
        return static_cast<uint8_t>(std::clamp(n * 0.5f + 0.5f, 0.0f, 1.0f) * 255.0f);
    };

    for (uint32_t y = 0; y < height; ++y) {
        for (uint32_t x = 0; x < width; ++x) {
            const auto coord = sampleCoord(x, y);
            const uint32_t hx = coord[0];
            const uint32_t hy = coord[1];

            const float hL = sampleOffset(hx, hy, -static_cast<int32_t>(sampleStepX), 0);
            const float hR = sampleOffset(hx, hy, static_cast<int32_t>(sampleStepX), 0);
            const float hD = sampleOffset(hx, hy, 0, -static_cast<int32_t>(sampleStepY));
            const float hU = sampleOffset(hx, hy, 0, static_cast<int32_t>(sampleStepY));
            const float dx = (hR - hL) / std::max(worldStepX * 2.0f, 0.001f);
            const float dz = (hU - hD) / std::max(worldStepY * 2.0f, 0.001f);

            const float strength = 0.68f;
            const float nx = -dx * strength;
            const float ny = 1.0f;
            const float nz = -dz * strength;
            const float invLen = 1.0f / std::max(std::sqrt(nx * nx + ny * ny + nz * nz), 0.001f);

            const size_t idx = (static_cast<size_t>(y) * width + x) * 4u;
            data[idx + 0] = encodeNormal(nx * invLen);
            data[idx + 1] = encodeNormal(ny * invLen);
            data[idx + 2] = encodeNormal(nz * invLen);
            data[idx + 3] = 255u;
        }
    }

    return data;
}

} // namespace voxy::terrain
