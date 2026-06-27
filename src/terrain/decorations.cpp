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

float clamp01(float value) noexcept {
    return std::clamp(value, 0.0f, 1.0f);
}

float smoothstep(float edge0, float edge1, float value) noexcept {
    const float t = clamp01((value - edge0) / (edge1 - edge0));
    return t * t * (3.0f - 2.0f * t);
}

bool isForestLikeRole(BiomeSurfaceRole role) noexcept {
    return role == BiomeSurfaceRole::Forest ||
           role == BiomeSurfaceRole::Taiga ||
           role == BiomeSurfaceRole::Jungle ||
           role == BiomeSurfaceRole::Swamp;
}

float estimateGroundDecorationDensity(const BiomeSample& biome, float heightM, float slope) noexcept {
    switch (biome.role) {
    case BiomeSurfaceRole::Water:
        return (heightM > -1.1f && heightM < 2.8f && biome.river > 0.08f) ? 0.28f : 0.0f;
    case BiomeSurfaceRole::Beach:
        return (1.0f - smoothstep(0.22f, 0.55f, slope)) *
               (0.10f + biome.moisture * 0.08f);
    case BiomeSurfaceRole::Rock:
        return 0.16f + smoothstep(0.44f, 0.95f, slope) * 0.22f;
    case BiomeSurfaceRole::Snow:
    case BiomeSurfaceRole::Tundra:
        return smoothstep(0.46f, 0.95f, slope) * 0.08f;
    case BiomeSurfaceRole::Dry:
    case BiomeSurfaceRole::Badlands:
    case BiomeSurfaceRole::Savanna:
        return (1.0f - smoothstep(0.54f, 0.95f, slope)) *
               (0.22f + biome.moisture * 0.12f);
    case BiomeSurfaceRole::Forest:
    case BiomeSurfaceRole::Taiga:
    case BiomeSurfaceRole::Jungle:
        return (1.0f - smoothstep(0.50f, 0.90f, slope)) *
               (0.42f + biome.moisture * 0.24f);
    case BiomeSurfaceRole::Swamp:
        return (heightM > -0.5f && heightM < 9.0f) ? 0.54f + biome.moisture * 0.20f : 0.32f;
    case BiomeSurfaceRole::Meadow:
        return (1.0f - smoothstep(0.44f, 0.82f, slope)) *
               (0.62f + biome.moisture * 0.18f);
    case BiomeSurfaceRole::Grass:
        return (1.0f - smoothstep(0.48f, 0.88f, slope)) *
               (0.48f + biome.moisture * 0.24f);
    }

    return 0.0f;
}

DecorationKind chooseGroundDecorationKind(const BiomeSample& biome,
                                          float heightM,
                                          float slope,
                                          float choice) noexcept {
    if (biome.role == BiomeSurfaceRole::Water ||
        (heightM < 3.0f && biome.moisture > 0.68f && biome.river > 0.12f)) {
        return DecorationKind::ReedCluster;
    }
    if (biome.role == BiomeSurfaceRole::Rock ||
        biome.role == BiomeSurfaceRole::Snow ||
        biome.role == BiomeSurfaceRole::Tundra ||
        slope > 0.66f) {
        return DecorationKind::RockShard;
    }
    if (biome.role == BiomeSurfaceRole::Badlands) {
        if (choice < 0.18f && slope < 0.34f) {
            return DecorationKind::CactusColumn;
        }
        return choice < 0.58f ? DecorationKind::DryShrub : DecorationKind::RockShard;
    }
    if (biome.role == BiomeSurfaceRole::Dry) {
        if (choice < 0.22f && slope < 0.30f) {
            return DecorationKind::CactusColumn;
        }
        return choice < 0.74f ? DecorationKind::DryShrub : DecorationKind::RockShard;
    }
    if (biome.role == BiomeSurfaceRole::Savanna) {
        if (choice < 0.08f && slope < 0.26f) {
            return DecorationKind::CactusColumn;
        }
        return choice < 0.72f ? DecorationKind::DryShrub : DecorationKind::RockShard;
    }
    if (biome.role == BiomeSurfaceRole::Beach) {
        return choice < 0.46f ? DecorationKind::ReedCluster : DecorationKind::RockShard;
    }
    if (biome.role == BiomeSurfaceRole::Swamp) {
        if (choice < 0.13f) {
            return DecorationKind::MushroomCluster;
        }
        return choice < 0.66f ? DecorationKind::ReedCluster : DecorationKind::GrassClump;
    }
    if (isForestLikeRole(biome.role)) {
        const float mushroomChance = biome.role == BiomeSurfaceRole::Jungle ? 0.06f :
                                     biome.role == BiomeSurfaceRole::Taiga ? 0.10f : 0.12f;
        if (choice < mushroomChance) {
            return DecorationKind::MushroomCluster;
        }
        return choice < 0.90f ? DecorationKind::GrassClump : DecorationKind::FlowerPatch;
    }
    if (biome.role == BiomeSurfaceRole::Meadow) {
        return choice < 0.44f ? DecorationKind::FlowerPatch : DecorationKind::GrassClump;
    }

    const float flowerChance = std::clamp(0.12f + biome.moisture * 0.22f - heightM * 0.0016f,
                                          0.06f,
                                          0.28f);
    return choice < flowerChance ? DecorationKind::FlowerPatch : DecorationKind::GrassClump;
}

TerrainDecoration makeGroundDecoration(DecorationKind kind,
                                       float worldX,
                                       float heightM,
                                       float worldZ,
                                       const BiomeSample& biome,
                                       float sizeJitter,
                                       float shadeJitter) noexcept {
    TerrainDecoration decoration;
    decoration.x = worldX;
    decoration.y = heightM + 0.04f;
    decoration.z = worldZ;
    decoration.kind = kind;

    const float shade = 0.82f + 0.28f * shadeJitter;
    switch (kind) {
    case DecorationKind::GrassClump:
        decoration.radius = 0.38f + sizeJitter * 0.62f;
        decoration.height = 0.46f + sizeJitter * 0.92f;
        decoration.colorR = (0.07f + biome.moisture * 0.035f) * shade;
        decoration.colorG = (0.30f + biome.moisture * 0.18f) * shade;
        decoration.colorB = (0.055f + biome.moisture * 0.055f) * shade;
        break;
    case DecorationKind::FlowerPatch:
        decoration.radius = 0.34f + sizeJitter * 0.52f;
        decoration.height = 0.42f + sizeJitter * 0.72f;
        decoration.colorR = (0.09f + biome.moisture * 0.03f) * shade;
        decoration.colorG = (0.34f + biome.moisture * 0.14f) * shade;
        decoration.colorB = (0.07f + biome.moisture * 0.04f) * shade;
        break;
    case DecorationKind::ReedCluster:
        decoration.radius = 0.24f + sizeJitter * 0.48f;
        decoration.height = 1.05f + sizeJitter * 1.45f;
        decoration.colorR = (0.24f + biome.moisture * 0.06f) * shade;
        decoration.colorG = (0.32f + biome.moisture * 0.10f) * shade;
        decoration.colorB = (0.11f + biome.moisture * 0.03f) * shade;
        break;
    case DecorationKind::RockShard:
        decoration.radius = 0.34f + sizeJitter * 0.78f;
        decoration.height = 0.30f + sizeJitter * 0.82f;
        decoration.colorR = (0.32f + biome.colorR * 0.20f) * shade;
        decoration.colorG = (0.31f + biome.colorG * 0.18f) * shade;
        decoration.colorB = (0.29f + biome.colorB * 0.16f) * shade;
        break;
    case DecorationKind::DryShrub:
        decoration.radius = 0.42f + sizeJitter * 0.70f;
        decoration.height = 0.50f + sizeJitter * 0.86f;
        decoration.colorR = (0.34f + biome.moisture * 0.06f) * shade;
        decoration.colorG = (0.28f + biome.moisture * 0.07f) * shade;
        decoration.colorB = (0.11f + biome.moisture * 0.04f) * shade;
        break;
    case DecorationKind::MushroomCluster:
        decoration.radius = 0.22f + sizeJitter * 0.38f;
        decoration.height = 0.28f + sizeJitter * 0.34f;
        decoration.colorR = (0.46f + biome.moisture * 0.08f) * shade;
        decoration.colorG = (0.36f + biome.moisture * 0.06f) * shade;
        decoration.colorB = (0.28f + biome.moisture * 0.04f) * shade;
        break;
    case DecorationKind::CactusColumn:
        decoration.radius = 0.20f + sizeJitter * 0.24f;
        decoration.height = 1.85f + sizeJitter * 2.40f;
        decoration.colorR = (0.08f + biome.moisture * 0.03f) * shade;
        decoration.colorG = (0.34f + biome.moisture * 0.10f) * shade;
        decoration.colorB = (0.16f + biome.moisture * 0.04f) * shade;
        break;
    case DecorationKind::BroadleafTree:
    case DecorationKind::PineTree:
    case DecorationKind::JungleTree:
    case DecorationKind::AcaciaTree:
    case DecorationKind::CypressTree:
        break;
    }

    return decoration;
}

DecorationKind chooseTreeKind(const BiomeSample& biome, float heightM) noexcept {
    switch (biome.role) {
    case BiomeSurfaceRole::Taiga:
    case BiomeSurfaceRole::Tundra:
    case BiomeSurfaceRole::Snow:
        return DecorationKind::PineTree;
    case BiomeSurfaceRole::Jungle:
        return DecorationKind::JungleTree;
    case BiomeSurfaceRole::Savanna:
    case BiomeSurfaceRole::Badlands:
        return DecorationKind::AcaciaTree;
    case BiomeSurfaceRole::Swamp:
        return DecorationKind::CypressTree;
    case BiomeSurfaceRole::Meadow:
        return heightM > 42.0f ? DecorationKind::PineTree : DecorationKind::BroadleafTree;
    case BiomeSurfaceRole::Grass:
    case BiomeSurfaceRole::Forest:
    case BiomeSurfaceRole::Dry:
    case BiomeSurfaceRole::Rock:
    case BiomeSurfaceRole::Beach:
    case BiomeSurfaceRole::Water:
        break;
    }

    return (biome.temperature < 12.0f || heightM > 42.0f)
        ? DecorationKind::PineTree
        : DecorationKind::BroadleafTree;
}

void applyTreeShapeAndColor(TerrainDecoration& tree,
                            const BiomeSample& biome,
                            float sizeJitter,
                            float shapeJitter,
                            float shade) noexcept {
    switch (tree.kind) {
    case DecorationKind::PineTree:
        tree.height = 5.6f + sizeJitter * 6.8f;
        tree.radius = tree.height * 0.22f * (0.82f + 0.28f * shapeJitter);
        tree.colorR = 0.055f * shade;
        tree.colorG = (0.20f + biome.moisture * 0.05f) * shade;
        tree.colorB = 0.11f * shade;
        break;
    case DecorationKind::JungleTree:
        tree.height = 7.8f + sizeJitter * 8.6f;
        tree.radius = tree.height * 0.36f * (0.88f + 0.26f * shapeJitter);
        tree.colorR = 0.035f * shade;
        tree.colorG = (0.28f + biome.moisture * 0.13f) * shade;
        tree.colorB = 0.055f * shade;
        break;
    case DecorationKind::AcaciaTree:
        tree.height = 4.6f + sizeJitter * 4.7f;
        tree.radius = tree.height * 0.44f * (0.88f + 0.22f * shapeJitter);
        tree.colorR = (0.16f + biome.moisture * 0.04f) * shade;
        tree.colorG = (0.24f + biome.moisture * 0.06f) * shade;
        tree.colorB = 0.055f * shade;
        break;
    case DecorationKind::CypressTree:
        tree.height = 6.8f + sizeJitter * 6.0f;
        tree.radius = tree.height * 0.18f * (0.88f + 0.20f * shapeJitter);
        tree.colorR = 0.045f * shade;
        tree.colorG = (0.22f + biome.moisture * 0.08f) * shade;
        tree.colorB = 0.080f * shade;
        break;
    case DecorationKind::BroadleafTree:
    default:
        tree.height = 4.2f + sizeJitter * 4.8f;
        tree.radius = tree.height * 0.30f * (0.82f + 0.28f * shapeJitter);
        tree.colorR = 0.075f * shade;
        tree.colorG = (0.28f + biome.moisture * 0.08f) * shade;
        tree.colorB = 0.065f * shade;
        break;
    }
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

std::vector<TerrainDecoration> generateTerrainDecorations(const DecorationConfig& config) {
    std::vector<TerrainDecoration> decorations;
    if (!hasDecorationHeightSamples(config) ||
        (config.maxTrees == 0 && config.maxGroundDecorations == 0)) {
        return decorations;
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
    decorations.reserve(std::min<size_t>(
        estimatedCells,
        static_cast<size_t>(config.maxTrees) + static_cast<size_t>(config.maxGroundDecorations)
    ));

    uint32_t treeCount = 0;
    if (config.maxTrees > 0) {
        for (uint32_t y = first; y < config.height; y += adaptiveSpacing) {
            for (uint32_t x = first; x < config.width; x += adaptiveSpacing) {
                if (treeCount >= config.maxTrees) {
                    break;
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

                const float sizeJitter = hash01(cellX + 7, cellY + 53, config.seed);
                const float shapeJitter = hash01(cellX + 31, cellY + 11, config.seed);
                const float shade = 0.84f + 0.24f * hash01(cellX + 13, cellY + 47, config.seed);

                TerrainDecoration tree;
                tree.x = worldX;
                tree.y = heightM + 0.15f;
                tree.z = worldZ;
                tree.kind = chooseTreeKind(biome, heightM);
                applyTreeShapeAndColor(tree, biome, sizeJitter, shapeJitter, shade);
                decorations.push_back(tree);
                ++treeCount;
            }
        }
    }

    uint32_t groundCount = 0;
    if (config.maxGroundDecorations > 0) {
        const uint32_t groundSpacing = std::max({
            4u,
            adaptiveSpacing / 2u,
            std::max(config.width, config.height) / 640u
        });
        const uint32_t groundFirst = std::max(1u, groundSpacing / 2u);
        const uint32_t groundSeed = config.seed ^ 0x47726f75u;

        for (uint32_t y = groundFirst; y < config.height; y += groundSpacing) {
            for (uint32_t x = groundFirst; x < config.width; x += groundSpacing) {
                if (groundCount >= config.maxGroundDecorations) {
                    break;
                }

                const auto cellX = static_cast<int32_t>(x / groundSpacing);
                const auto cellY = static_cast<int32_t>(y / groundSpacing);
                const float jitterX = (hash01(cellX + 101, cellY + 19, groundSeed) - 0.5f) *
                                      static_cast<float>(groundSpacing) * 0.86f;
                const float jitterY = (hash01(cellX + 37, cellY + 113, groundSeed) - 0.5f) *
                                      static_cast<float>(groundSpacing) * 0.86f;
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

                const float density = estimateGroundDecorationDensity(biome, heightM, slope);
                const float keep = hash01(cellX + 79, cellY + 151, groundSeed);
                if (keep > density) {
                    continue;
                }

                const float choice = hash01(cellX + 193, cellY + 59, groundSeed);
                const DecorationKind kind = chooseGroundDecorationKind(biome, heightM, slope, choice);
                TerrainDecoration ground = makeGroundDecoration(
                    kind,
                    worldX,
                    heightM,
                    worldZ,
                    biome,
                    hash01(cellX + 211, cellY + 17, groundSeed),
                    hash01(cellX + 31, cellY + 223, groundSeed)
                );
                decorations.push_back(ground);
                ++groundCount;
            }
        }
    }

    return decorations;
}

std::vector<TreeDecoration> generateTreeDecorations(const DecorationConfig& config) {
    DecorationConfig treeOnlyConfig = config;
    treeOnlyConfig.maxGroundDecorations = 0;
    return generateTerrainDecorations(treeOnlyConfig);
}

} // namespace voxy::terrain
