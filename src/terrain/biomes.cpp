// ═══════════════════════════════════════════════════════════════════════════════
// biomes.cpp - Shared Terrain Biome Sampling Implementation
// ═══════════════════════════════════════════════════════════════════════════════

#include "terrain/biomes.hpp"

#include <algorithm>
#include <cmath>

namespace voxy::terrain {

namespace {

float clamp01(float value) noexcept {
    return std::clamp(value, 0.0f, 1.0f);
}

float smoothstep(float edge0, float edge1, float value) noexcept {
    const float t = clamp01((value - edge0) / (edge1 - edge0));
    return t * t * (3.0f - 2.0f * t);
}

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

float lerp(float a, float b, float t) noexcept {
    return a + (b - a) * t;
}

float valueNoise(float x, float y, uint32_t seed) noexcept {
    const auto ix = static_cast<int32_t>(std::floor(x));
    const auto iy = static_cast<int32_t>(std::floor(y));
    const float fx = x - static_cast<float>(ix);
    const float fy = y - static_cast<float>(iy);
    const float ux = fx * fx * (3.0f - 2.0f * fx);
    const float uy = fy * fy * (3.0f - 2.0f * fy);

    const float a = hash01(ix, iy, seed);
    const float b = hash01(ix + 1, iy, seed);
    const float c = hash01(ix, iy + 1, seed);
    const float d = hash01(ix + 1, iy + 1, seed);
    return lerp(lerp(a, b, ux), lerp(c, d, ux), uy);
}

struct BiomeSurface {
    float r;
    float g;
    float b;
    float treeDensity;
    float water;
    float snow;
    BiomeSurfaceRole role;
};

BiomeSurface classifyBiomeSurface(float heightM, float slope, float moisture,
                                  float temperature, float river) noexcept {
    const bool ocean = heightM < -1.0f;
    const bool beach = !ocean && std::abs(heightM) < 3.5f && slope < 0.45f;
    const bool highland = heightM > 38.0f;
    const bool mountain = heightM > 70.0f;
    const bool steep = slope > 0.62f;
    const bool cold = temperature < 5.0f;
    const bool cool = temperature >= 5.0f && temperature < 12.0f;
    const bool hot = temperature > 24.0f;

    if (ocean) {
        return {0.025f, 0.16f, 0.31f, 0.0f, 1.0f, 0.0f, BiomeSurfaceRole::Water};
    }
    if (river > 0.02f) {
        return {0.10f, 0.42f, 0.50f, 0.0f, river, 0.0f, BiomeSurfaceRole::Water};
    }
    if (beach) {
        return {0.68f, 0.60f, 0.42f, 0.0f, 0.0f, 0.0f, BiomeSurfaceRole::Beach};
    }
    if ((mountain && cold) || (heightM > 84.0f && moisture > 0.30f)) {
        return {0.88f, 0.91f, 0.87f, 0.0f, 0.0f, 1.0f, BiomeSurfaceRole::Snow};
    }
    if (steep || (mountain && moisture < 0.55f)) {
        return {0.43f, 0.42f, 0.38f, 0.0f, 0.0f, 0.0f, BiomeSurfaceRole::Rock};
    }
    if (hot && moisture < 0.33f) {
        return {0.58f, 0.51f, 0.27f, 0.0f, 0.0f, 0.0f, BiomeSurfaceRole::Dry};
    }
    if (hot && moisture < 0.48f) {
        return {0.50f, 0.49f, 0.24f, 0.12f, 0.0f, 0.0f, BiomeSurfaceRole::Dry};
    }
    if (heightM < 8.0f && moisture > 0.72f) {
        return {0.18f, 0.31f, 0.13f, 0.86f, 0.0f, 0.0f, BiomeSurfaceRole::Forest};
    }
    if (cool || cold) {
        const float trees = smoothstep(0.38f, 0.76f, moisture);
        return {0.14f, 0.30f, 0.16f, trees, 0.0f, 0.0f,
                trees > 0.18f ? BiomeSurfaceRole::Forest : BiomeSurfaceRole::Grass};
    }
    if (highland && moisture > 0.40f) {
        return {0.38f, 0.55f, 0.25f, 0.22f, 0.0f, 0.0f, BiomeSurfaceRole::Grass};
    }

    const float trees = smoothstep(0.46f, 0.82f, moisture);
    if (trees > 0.08f) {
        return {0.16f, 0.36f, 0.14f, trees, 0.0f, 0.0f,
                trees > 0.22f ? BiomeSurfaceRole::Forest : BiomeSurfaceRole::Grass};
    }
    return {0.42f, 0.53f, 0.24f, 0.0f, 0.0f, 0.0f, BiomeSurfaceRole::Grass};
}

} // namespace

float estimateBiomeRiverMask(float worldX, float worldZ, float heightM,
                             float slope, float moisture, uint32_t seed) noexcept {
    const float flowWarp = std::sin(worldZ * 0.008f + worldX * 0.004f) * 2.5f +
                           std::sin((worldX + worldZ) * 0.003f) * 1.8f;
    const float channelA = std::abs(std::sin(worldX * 0.020f + worldZ * 0.006f + flowWarp));
    const float channelB = std::abs(std::sin(worldX * -0.008f + worldZ * 0.017f + flowWarp * 0.7f + 2.1f));
    const float trunkRiver = 1.0f - smoothstep(0.004f, 0.022f, std::min(channelA, channelB));
    const float tributary = 1.0f - smoothstep(0.004f, 0.015f,
        std::abs(std::sin(worldX * 0.035f - worldZ * 0.021f + flowWarp * 1.3f + 1.4f)));
    const float riverBasin = smoothstep(0.60f, 0.88f,
        valueNoise(worldX * 0.002f + 9.0f, worldZ * 0.002f + 15.0f, seed ^ 0x52697672u));
    const float lowland = smoothstep(-2.0f, 7.0f, heightM) *
                          (1.0f - smoothstep(30.0f, 58.0f, heightM));
    const float flat = 1.0f - smoothstep(0.22f, 0.48f, slope);
    return clamp01(std::max(trunkRiver, tributary * 0.22f) *
                   riverBasin *
                   smoothstep(0.36f, 0.68f, moisture) *
                   lowland *
                   flat);
}

float estimateBiomeTreeDensity(float heightM, float slope, float moisture,
                               float temperature, float river) noexcept {
    return classifyBiomeSurface(heightM, slope, moisture, temperature, river).treeDensity;
}

BiomeSample sampleBiome(const BiomeSampleInput& input) noexcept {
    const float climateNoise = valueNoise(input.worldX * 0.006f + 19.0f,
                                          input.worldZ * 0.006f + 71.0f,
                                          input.seed ^ 0x42696f6du);
    const float moistureNoise = valueNoise(input.worldX * 0.004f + 113.0f,
                                           input.worldZ * 0.004f + 29.0f,
                                           input.seed ^ 0x5261696eu);
    const float temperature = 18.0f +
                              10.0f * (climateNoise - 0.5f) -
                              input.heightM * 0.10f +
                              5.0f * std::sin((input.worldX + 900.0f) * 0.002f);
    const float moisture = clamp01(moistureNoise +
                                   input.surfaceMoistureBias -
                                   input.slope * 0.25f +
                                   smoothstep(-8.0f, 12.0f, input.heightM) * 0.12f);
    const float river = estimateBiomeRiverMask(input.worldX, input.worldZ, input.heightM,
                                               input.slope, moisture, input.seed);
    const BiomeSurface surface = classifyBiomeSurface(input.heightM, input.slope,
                                                      moisture, temperature, river);

    return BiomeSample{
        .heightM = input.heightM,
        .slope = input.slope,
        .moisture = moisture,
        .temperature = temperature,
        .river = river,
        .treeDensity = surface.treeDensity,
        .water = surface.water,
        .snow = surface.snow,
        .colorR = surface.r,
        .colorG = surface.g,
        .colorB = surface.b,
        .role = surface.role,
    };
}

} // namespace voxy::terrain
