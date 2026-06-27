// ═══════════════════════════════════════════════════════════════════════════════
// biomes.hpp - Shared Terrain Biome Sampling (C++20)
// ═══════════════════════════════════════════════════════════════════════════════

#pragma once

#include <cstdint>

namespace voxy::terrain {

inline constexpr uint32_t kDefaultBiomeSeed = 0x4d43524fu;

enum class BiomeSurfaceRole : uint32_t {
    Grass = 0,
    Forest = 1,
    Dry = 2,
    Rock = 3,
    Snow = 4,
    Beach = 5,
    Water = 6,
};

struct BiomeSampleInput {
    float worldX = 0.0f;
    float worldZ = 0.0f;
    float heightM = 0.0f;
    float slope = 0.0f;
    float surfaceMoistureBias = 0.0f;
    uint32_t seed = kDefaultBiomeSeed;
};

struct BiomeSample {
    float heightM = 0.0f;
    float slope = 0.0f;
    float moisture = 0.0f;
    float temperature = 18.0f;
    float river = 0.0f;
    float treeDensity = 0.0f;
    float water = 0.0f;
    float snow = 0.0f;
    float colorR = 0.42f;
    float colorG = 0.53f;
    float colorB = 0.24f;
    BiomeSurfaceRole role = BiomeSurfaceRole::Grass;

    [[nodiscard]] bool isWater() const noexcept { return role == BiomeSurfaceRole::Water; }
};

[[nodiscard]] float estimateBiomeRiverMask(float worldX, float worldZ, float heightM,
                                           float slope, float moisture,
                                           uint32_t seed = kDefaultBiomeSeed) noexcept;

[[nodiscard]] float estimateBiomeTreeDensity(float heightM, float slope, float moisture,
                                             float temperature, float river) noexcept;

[[nodiscard]] BiomeSample sampleBiome(const BiomeSampleInput& input) noexcept;

} // namespace voxy::terrain
