// ═══════════════════════════════════════════════════════════════════════════════
// decorations.cpp - Biome-Driven Terrain Decoration Placement Implementation
// ═══════════════════════════════════════════════════════════════════════════════

#include "terrain/decorations.hpp"
#include "terrain/biomes.hpp"

#include <algorithm>
#include <cmath>

namespace voxy::terrain {

namespace {

uint32_t hash2D(int32_t x, int32_t y, uint32_t seed) noexcept {
    uint32_t h = static_cast<uint32_t>(x) * 0x8da6b343u;
    h ^= static_cast<uint32_t>(y) * 0xd8163841u;
    h ^= seed * 0x9e3779b9u;
    h ^= h >> 13u;
    h *= 0x85ebca6bu;
    h ^= h >> 16u;
    return h;
}

float hash01(int32_t x, int32_t y, uint32_t seed) noexcept {
    return static_cast<float>(hash2D(x, y, seed) & 0x00ffffffu) / 16777215.0f;
}

} // namespace

bool hasDecorationHeightSamples(const DecorationConfig& config) noexcept {
    return config.enabled &&
           !config.heightSamples.empty() &&
           config.width > 2 &&
           config.height > 2 &&
           config.heightSamples.size() >= static_cast<size_t>(config.width) *
                                         static_cast<size_t>(config.height);
}

float sampleDecorationHeightMeters(const DecorationConfig& config,
                                   uint32_t x, uint32_t y) noexcept {
    if (!hasDecorationHeightSamples(config)) {
        return 0.0f;
    }

    x = std::min(x, config.width - 1);
    y = std::min(y, config.height - 1);
    const uint16_t raw = config.heightSamples[static_cast<size_t>(y) * config.width + x];
    const float normalized = static_cast<float>(raw) / 65535.0f;
    return (normalized * 2.0f - 1.0f) * config.heightScale;
}

float estimateDecorationSlope(const DecorationConfig& config,
                              uint32_t x, uint32_t y) noexcept {
    if (!hasDecorationHeightSamples(config)) {
        return 0.0f;
    }

    const uint32_t xl = x > 0 ? x - 1 : x;
    const uint32_t xr = std::min(x + 1, config.width - 1);
    const uint32_t yd = y > 0 ? y - 1 : y;
    const uint32_t yu = std::min(y + 1, config.height - 1);
    const float hL = sampleDecorationHeightMeters(config, xl, y);
    const float hR = sampleDecorationHeightMeters(config, xr, y);
    const float hD = sampleDecorationHeightMeters(config, x, yd);
    const float hU = sampleDecorationHeightMeters(config, x, yu);
    const float horizontal = std::max(config.cellScale * 2.0f, 0.001f);
    const float dx = (hR - hL) / horizontal;
    const float dz = (hU - hD) / horizontal;
    return std::sqrt(dx * dx + dz * dz);
}

std::vector<TreeDecoration> generateTreeDecorations(const DecorationConfig& config) {
    std::vector<TreeDecoration> trees;
    if (!hasDecorationHeightSamples(config) || config.maxTrees == 0) {
        return trees;
    }

    const uint32_t adaptiveSpacing = std::max({
        config.spacingCells,
        6u,
        std::max(config.width, config.height) / 360u
    });
    const float originX = 0.5f * static_cast<float>(config.width - 1u) * config.cellScale;
    const float originZ = 0.5f * static_cast<float>(config.height - 1u) * config.cellScale;
    const uint32_t first = adaptiveSpacing / 2u;

    const size_t estimatedCells =
        static_cast<size_t>(config.width / adaptiveSpacing + 1u) *
        static_cast<size_t>(config.height / adaptiveSpacing + 1u);
    trees.reserve(std::min<size_t>(estimatedCells / 3u, config.maxTrees));

    for (uint32_t y = first; y < config.height; y += adaptiveSpacing) {
        for (uint32_t x = first; x < config.width; x += adaptiveSpacing) {
            if (trees.size() >= config.maxTrees) {
                return trees;
            }

            const auto cellX = static_cast<int32_t>(x / adaptiveSpacing);
            const auto cellY = static_cast<int32_t>(y / adaptiveSpacing);
            const float jitterX = (hash01(cellX + 17, cellY + 3, config.seed) - 0.5f) *
                                  static_cast<float>(adaptiveSpacing) * 0.72f;
            const float jitterY = (hash01(cellX + 5, cellY + 29, config.seed) - 0.5f) *
                                  static_cast<float>(adaptiveSpacing) * 0.72f;
            const int32_t clampedX = std::clamp(
                static_cast<int32_t>(std::round(static_cast<float>(x) + jitterX)),
                1,
                static_cast<int32_t>(config.width - 2u)
            );
            const int32_t clampedY = std::clamp(
                static_cast<int32_t>(std::round(static_cast<float>(y) + jitterY)),
                1,
                static_cast<int32_t>(config.height - 2u)
            );
            const uint32_t sampleX = static_cast<uint32_t>(clampedX);
            const uint32_t sampleY = static_cast<uint32_t>(clampedY);

            const float heightM = sampleDecorationHeightMeters(config, sampleX, sampleY);
            const float slope = estimateDecorationSlope(config, sampleX, sampleY);
            const float worldX = static_cast<float>(sampleX) * config.cellScale - originX;
            const float worldZ = static_cast<float>(sampleY) * config.cellScale - originZ;
            const BiomeSample biome = sampleBiome(BiomeSampleInput{
                .worldX = worldX,
                .worldZ = worldZ,
                .heightM = heightM,
                .slope = slope,
                .seed = config.seed,
            });
            const float density = biome.treeDensity;
            const float keep = hash01(cellX + 41, cellY + 2, config.seed);
            if (keep > density * 0.88f) {
                continue;
            }

            const bool pine = biome.temperature < 12.0f || heightM > 42.0f;
            const float sizeJitter = hash01(cellX + 7, cellY + 53, config.seed);
            const float height = pine
                ? 5.5f + sizeJitter * 6.5f
                : 4.2f + sizeJitter * 4.8f;
            const float radius = height * (pine ? 0.22f : 0.30f) *
                                 (0.82f + 0.28f * hash01(cellX + 31, cellY + 11, config.seed));
            const float shade = 0.84f + 0.24f * hash01(cellX + 13, cellY + 47, config.seed);

            TreeDecoration tree;
            tree.x = worldX;
            tree.y = heightM + 0.15f;
            tree.z = worldZ;
            tree.radius = radius;
            tree.height = height;
            tree.kind = pine ? DecorationKind::PineTree : DecorationKind::BroadleafTree;
            if (pine) {
                tree.colorR = 0.055f * shade;
                tree.colorG = 0.22f * shade;
                tree.colorB = 0.10f * shade;
            } else {
                tree.colorR = 0.075f * shade;
                tree.colorG = (0.28f + biome.moisture * 0.08f) * shade;
                tree.colorB = 0.065f * shade;
            }
            trees.push_back(tree);
        }
    }

    return trees;
}

} // namespace voxy::terrain
