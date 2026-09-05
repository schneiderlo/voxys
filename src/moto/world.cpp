// ═══════════════════════════════════════════════════════════════════════════════
// world.cpp - RIDGEBREAK mixed-biome open-world generation
// ═══════════════════════════════════════════════════════════════════════════════

#include "moto/world.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <new>
#include <stdexcept>
#include <utility>

namespace voxy::moto {
namespace {

constexpr float kPi = 3.14159265358979323846f;
constexpr float kTwoPi = 2.0f * kPi;

struct Weights {
    float alpine;
    float desert;
    float coast;
};

[[nodiscard]] float saturate(float value) noexcept {
    return std::clamp(value, 0.0f, 1.0f);
}

[[nodiscard]] float smooth(float value) noexcept {
    value = saturate(value);
    return value * value * (3.0f - 2.0f * value);
}

[[nodiscard]] float smoothRange(float low, float high, float value) noexcept {
    return smooth((value - low) / (high - low));
}

[[nodiscard]] float normalizedHeading(float heading) noexcept {
    heading = std::fmod(heading + kPi, kTwoPi);
    if (heading < 0.0f) {
        heading += kTwoPi;
    }
    return heading - kPi;
}

[[nodiscard]] uint32_t mixBits(uint32_t value) noexcept {
    value ^= value >> 16u;
    value *= 0x7feb352du;
    value ^= value >> 15u;
    value *= 0x846ca68bu;
    return value ^ (value >> 16u);
}

[[nodiscard]] uint32_t hash2(uint32_t seed, int32_t x, int32_t y) noexcept {
    uint32_t value = seed ^ (static_cast<uint32_t>(x) * 0x9e3779b9u);
    value ^= static_cast<uint32_t>(y) * 0x85ebca6bu;
    return mixBits(value);
}

[[nodiscard]] float hashSigned(uint32_t seed, int32_t x, int32_t y) noexcept {
    constexpr float scale = 1.0f / 8'388'607.5f;
    return static_cast<float>(hash2(seed, x, y) >> 8u) * scale - 1.0f;
}

[[nodiscard]] float valueNoise(float x, float y, uint32_t seed) noexcept {
    const auto ix = static_cast<int32_t>(std::floor(x));
    const auto iy = static_cast<int32_t>(std::floor(y));
    const float tx = smooth(x - static_cast<float>(ix));
    const float ty = smooth(y - static_cast<float>(iy));
    const float a = std::lerp(hashSigned(seed, ix, iy),
                              hashSigned(seed, ix + 1, iy), tx);
    const float b = std::lerp(hashSigned(seed, ix, iy + 1),
                              hashSigned(seed, ix + 1, iy + 1), tx);
    return std::lerp(a, b, ty);
}

[[nodiscard]] float fbm(float x, float y, uint32_t seed) noexcept {
    float result = 0.0f;
    float amplitude = 0.54f;
    float total = 0.0f;
    for (uint32_t octave = 0; octave < 4; ++octave) {
        result += amplitude * valueNoise(x, y, seed + octave * 0x68bc21ebu);
        total += amplitude;
        x = x * 2.03f + 7.1f;
        y = y * 2.03f - 5.7f;
        amplitude *= 0.49f;
    }
    return result / total;
}

[[nodiscard]] float distanceSquared(glm::vec2 a, glm::vec2 b) noexcept {
    const glm::vec2 delta = a - b;
    return delta.x * delta.x + delta.y * delta.y;
}

[[nodiscard]] Weights biomeWeights(glm::vec2 uv,
                                    const WorldGenConfig& config) noexcept {
    const auto radial = [](float distance2) {
        const float inverse = 1.0f / (0.018f + distance2);
        return inverse * inverse;
    };
    Weights weights{
        radial(distanceSquared(uv, config.alpineCenter)),
        radial(distanceSquared(uv, config.desertCenter)),
        radial(distanceSquared(uv, config.coastCenter)),
    };
    const float inverseSum = 1.0f /
        (weights.alpine + weights.desert + weights.coast);
    weights.alpine *= inverseSum;
    weights.desert *= inverseSum;
    weights.coast *= inverseSum;
    return weights;
}

[[nodiscard]] Biome classifyBiome(glm::vec2 uv,
                                  const WorldGenConfig& config) noexcept {
    const float alpine = distanceSquared(uv, config.alpineCenter);
    const float desert = distanceSquared(uv, config.desertCenter);
    const float coast = distanceSquared(uv, config.coastCenter);
    if (coast < alpine && coast < desert) {
        return Biome::Coast;
    }
    if (desert < alpine) {
        return Biome::Desert;
    }
    return Biome::Alpine;
}

[[nodiscard]] float proceduralHeight(glm::vec2 uv,
                                     const WorldGenConfig& config) noexcept {
    const float alpineBase = fbm(uv.x * 7.0f, uv.y * 7.0f,
                                 config.seed ^ 0xa341316cu);
    const float alpineRidge = 1.0f - std::abs(fbm(
        uv.x * 14.0f + 2.0f, uv.y * 14.0f - 3.0f,
        config.seed ^ 0xc8013ea4u));
    const float alpine = 0.055f + 0.105f * alpineBase +
                         0.285f * alpineRidge * alpineRidge;

    const float desertNoise = fbm(uv.x * 8.0f - 4.0f,
                                  uv.y * 8.0f + 1.0f,
                                  config.seed ^ 0xad90777du);
    const float mesa = smoothRange(-0.28f, 0.38f, desertNoise);
    const glm::vec2 desertLocal = uv - config.desertCenter;
    const float phase = static_cast<float>(mixBits(config.seed ^ 0x7e95761eu)) *
                        (kTwoPi / 4'294'967'295.0f);
    const float canyonLine = desertLocal.x * 0.72f - desertLocal.y * 0.69f +
        0.026f * std::sin((desertLocal.x + desertLocal.y) * 39.0f + phase);
    const float canyon = 1.0f - smoothRange(0.012f, 0.063f,
                                           std::abs(canyonLine));
    const float desert = 0.018f + 0.125f * mesa +
                         0.028f * desertNoise - 0.165f * canyon;

    const glm::vec2 coastLocal = uv - config.coastCenter;
    const float lakeDistance2 =
        (coastLocal.x * coastLocal.x) / (0.125f * 0.125f) +
        (coastLocal.y * coastLocal.y) / (0.090f * 0.090f);
    const float lake = 1.0f - smoothRange(0.24f, 1.0f, lakeDistance2);
    const float coastNoise = fbm(uv.x * 5.0f + 9.0f,
                                 uv.y * 5.0f - 6.0f,
                                 config.seed ^ 0x9e3779b9u);
    const float coastRise = smoothRange(0.008f, 0.115f,
                                        coastLocal.x * coastLocal.x +
                                        coastLocal.y * coastLocal.y);
    const float coast = 0.014f + 0.017f * coastNoise +
                        0.034f * coastRise - 0.175f * lake;

    const Weights weights = biomeWeights(uv, config);
    return std::clamp(weights.alpine * alpine +
                      weights.desert * desert +
                      weights.coast * coast, -0.92f, 0.92f);
}

[[nodiscard]] uint16_t encodeHeight(float worldY, float heightScale) noexcept {
    const float normalized = saturate(worldY / heightScale * 0.5f + 0.5f);
    return static_cast<uint16_t>(normalized * 65'535.0f + 0.5f);
}

[[nodiscard]] float decodeHeight(uint16_t sample, float heightScale) noexcept {
    return heightScale *
        (2.0f * static_cast<float>(sample) / 65'535.0f - 1.0f);
}

[[nodiscard]] size_t sampleIndex(uint32_t x, uint32_t y,
                                 uint32_t size) noexcept {
    return static_cast<size_t>(y) * size + x;
}

[[nodiscard]] float readHeight(const WorldGenResult& world, uint32_t x,
                               uint32_t y,
                               const WorldGenConfig& config) noexcept {
    x = std::min(x, config.size - 1u);
    y = std::min(y, config.size - 1u);
    return decodeHeight(world.samples[sampleIndex(x, y, config.size)],
                        config.heightScale);
}

void writeHeight(WorldGenResult& world, uint32_t x, uint32_t y, float height,
                 const WorldGenConfig& config) noexcept {
    world.samples[sampleIndex(x, y, config.size)] =
        encodeHeight(height, config.heightScale);
}

[[nodiscard]] float halfExtentSamples(const WorldGenConfig& config) noexcept {
    return 0.5f * static_cast<float>(config.size - 1u);
}

[[nodiscard]] glm::vec2 texelToWorld(float x, float y,
                                     const WorldGenConfig& config) noexcept {
    const float half = halfExtentSamples(config);
    return {(x - half) * config.cellScale, (y - half) * config.cellScale};
}

[[nodiscard]] glm::vec2 normalizedToTexel(glm::vec2 uv,
                                          const WorldGenConfig& config) noexcept {
    const float last = static_cast<float>(config.size - 1u);
    return {std::clamp(uv.x, 0.0f, 1.0f) * last,
            std::clamp(uv.y, 0.0f, 1.0f) * last};
}

[[nodiscard]] glm::vec2 worldToTexel(glm::vec2 world,
                                     const WorldGenConfig& config) noexcept {
    const float half = halfExtentSamples(config);
    return {world.x / config.cellScale + half,
            world.y / config.cellScale + half};
}

[[nodiscard]] glm::vec2 worldToNormalized(
    glm::vec2 world, const WorldGenConfig& config) noexcept {
    const glm::vec2 texel = worldToTexel(world, config);
    const float inverseLast = 1.0f / static_cast<float>(config.size - 1u);
    return texel * inverseLast;
}

void flattenDisc(WorldGenResult& world, glm::vec2 center, float radius,
                 float target, const WorldGenConfig& config) noexcept {
    const int32_t minX = std::max(0, static_cast<int32_t>(std::floor(center.x - radius)));
    const int32_t maxX = std::min(static_cast<int32_t>(config.size) - 1,
                                 static_cast<int32_t>(std::ceil(center.x + radius)));
    const int32_t minY = std::max(0, static_cast<int32_t>(std::floor(center.y - radius)));
    const int32_t maxY = std::min(static_cast<int32_t>(config.size) - 1,
                                 static_cast<int32_t>(std::ceil(center.y + radius)));
    const float inverseRadius = 1.0f / radius;
    for (int32_t y = minY; y <= maxY; ++y) {
        for (int32_t x = minX; x <= maxX; ++x) {
            const float dx = static_cast<float>(x) - center.x;
            const float dy = static_cast<float>(y) - center.y;
            const float distance = std::sqrt(dx * dx + dy * dy) * inverseRadius;
            if (distance >= 1.0f) {
                continue;
            }
            const float blend = 1.0f - smoothRange(0.58f, 1.0f, distance);
            const float current = readHeight(world, static_cast<uint32_t>(x),
                                             static_cast<uint32_t>(y), config);
            writeHeight(world, static_cast<uint32_t>(x), static_cast<uint32_t>(y),
                        std::lerp(current, target, blend), config);
        }
    }
}

void stampRoadSegment(WorldGenResult& world, glm::vec2 fromWorld,
                      glm::vec2 toWorld, const WorldGenConfig& config) noexcept {
    const glm::vec2 from = worldToTexel(fromWorld, config);
    const glm::vec2 to = worldToTexel(toWorld, config);
    const glm::vec2 delta = to - from;
    const float length = std::sqrt(delta.x * delta.x + delta.y * delta.y);
    const uint32_t steps = std::max(1u, static_cast<uint32_t>(std::ceil(length)));
    const float radius = std::clamp(3.5f / config.cellScale, 1.0f, 4.0f);
    const glm::vec2 direction = delta / length;
    for (uint32_t step = 0; step <= steps; ++step) {
        const float t = static_cast<float>(step) / static_cast<float>(steps);
        const glm::vec2 center = from + delta * t;
        const glm::vec2 probe = center + direction * (radius + 1.0f);
        const auto x = static_cast<uint32_t>(std::clamp(
            std::lround(probe.x), 0l, static_cast<long>(config.size - 1u)));
        const auto y = static_cast<uint32_t>(std::clamp(
            std::lround(probe.y), 0l, static_cast<long>(config.size - 1u)));
        flattenDisc(world, center, radius, readHeight(world, x, y, config), config);
    }
}

enum class StampShape { Ramp, Table, Cliff };

void stampFeatureShape(WorldGenResult& world, glm::vec2 center, float heading,
                       StampShape shape, const WorldGenConfig& config) noexcept {
    // Metric profiles preserve useful curvature on the 4 m production grid.
    // The old universal 16x5 m stamp collapsed to four longitudinal samples,
    // making every practice jump a razor-edged triangular pyramid.
    const bool metricPracticeProfile = config.size >= 256u;
    const float lengthMetres = metricPracticeProfile
        ? (shape == StampShape::Table ? 52.0f
           : (shape == StampShape::Ramp ? 36.0f : 16.0f))
        : 16.0f;
    const float halfWidthMetres = metricPracticeProfile
        ? (shape == StampShape::Table ? 8.0f
           : (shape == StampShape::Ramp ? 7.0f : 2.5f))
        : 2.5f;
    const float length = std::clamp(
        lengthMetres / config.cellScale, 4.0f, 30.0f);
    const float halfWidth = std::clamp(
        halfWidthMetres / config.cellScale, 1.25f, 8.0f);
    const float extent = length + halfWidth + 2.0f;
    const int32_t minX = std::max(0, static_cast<int32_t>(std::floor(center.x - extent)));
    const int32_t maxX = std::min(static_cast<int32_t>(config.size) - 1,
                                 static_cast<int32_t>(std::ceil(center.x + extent)));
    const int32_t minY = std::max(0, static_cast<int32_t>(std::floor(center.y - extent)));
    const int32_t maxY = std::min(static_cast<int32_t>(config.size) - 1,
                                 static_cast<int32_t>(std::ceil(center.y + extent)));
    const float sine = std::sin(heading);
    const float cosine = std::cos(heading);
    const glm::vec2 direction(sine, cosine);
    const glm::vec2 start = center - direction * (0.42f * length);
    const auto startX = static_cast<uint32_t>(std::clamp(
        std::lround(start.x), 0l, static_cast<long>(config.size - 1u)));
    const auto startY = static_cast<uint32_t>(std::clamp(
        std::lround(start.y), 0l, static_cast<long>(config.size - 1u)));
    const float base = readHeight(world, startX, startY, config);

    for (int32_t y = minY; y <= maxY; ++y) {
        for (int32_t x = minX; x <= maxX; ++x) {
            const float dx = static_cast<float>(x) - center.x;
            const float dy = static_cast<float>(y) - center.y;
            const float along = dx * direction.x + dy * direction.y;
            const float side = std::abs(-dx * direction.y + dy * direction.x);
            const float normalizedAlong = along / length;
            if (side >= halfWidth || normalizedAlong < -0.5f ||
                normalizedAlong > 0.62f) {
                continue;
            }
            const float sideMask = 1.0f - smoothRange(0.62f, 1.0f,
                                                      side / halfWidth);
            float target = base;
            float longitudinalMask = 1.0f;
            if (shape == StampShape::Ramp) {
                const float rise = std::min(4.0f, config.heightScale * 0.02f);
                target += rise * smoothRange(-0.48f, 0.48f, normalizedAlong);
                longitudinalMask = smoothRange(-0.5f, -0.35f, normalizedAlong);
                if (normalizedAlong > 0.5f) {
                    continue;
                }
            } else if (shape == StampShape::Table) {
                const float rise = std::min(3.5f, config.heightScale * 0.018f);
                float profile = 0.0f;
                if (normalizedAlong < -0.16f) {
                    profile = smoothRange(-0.48f, -0.16f, normalizedAlong);
                } else if (normalizedAlong < 0.12f) {
                    const float crown = normalizedAlong + 0.02f;
                    profile = 1.0f - 0.75f * crown * crown;
                } else {
                    profile = 1.0f - smoothRange(0.12f, 0.58f,
                                                  normalizedAlong);
                }
                target += rise * profile;
                longitudinalMask = smoothRange(-0.5f, -0.37f, normalizedAlong) *
                    (1.0f - smoothRange(0.55f, 0.62f, normalizedAlong));
            } else {
                const float upper = std::min(2.0f, config.heightScale * 0.01f);
                const float drop = std::min(30.0f, config.heightScale * 0.10f);
                const float face = smoothRange(-0.015f, 0.035f, normalizedAlong);
                target += upper - (upper + drop) * face;
                longitudinalMask = smoothRange(-0.5f, -0.40f, normalizedAlong) *
                    (1.0f - smoothRange(0.50f, 0.62f, normalizedAlong));
            }
            const float blend = sideMask * longitudinalMask;
            const float current = readHeight(world, static_cast<uint32_t>(x),
                                             static_cast<uint32_t>(y), config);
            writeHeight(world, static_cast<uint32_t>(x), static_cast<uint32_t>(y),
                        std::lerp(current, target, blend), config);
        }
    }
}

[[nodiscard]] glm::vec2 clampedFeatureUv(glm::vec2 uv) noexcept {
    return {std::clamp(uv.x, 0.055f, 0.945f),
            std::clamp(uv.y, 0.055f, 0.945f)};
}

void addNativeFeature(WorldGenResult& world, FeatureKind kind, glm::vec2 uv,
                      float heading, const WorldGenConfig& config) {
    uv = clampedFeatureUv(uv);
    heading = normalizedHeading(heading);
    const glm::vec2 texel = normalizedToTexel(uv, config);
    const glm::vec2 position = texelToWorld(texel.x, texel.y, config);
    world.features.push_back({kind, position, heading, 1.0f});
    const StampShape shape = kind == FeatureKind::DirtRamp
        ? StampShape::Ramp
        : (kind == FeatureKind::TableJump ? StampShape::Table
                                          : StampShape::Cliff);
    stampFeatureShape(world, texel, heading, shape, config);
}

void buildRouteAndRoads(WorldGenResult& world,
                        const WorldGenConfig& config) {
    constexpr uint32_t pointCount = 20;
    // Size the production loop in metres, not as a fixed percentage of an
    // arbitrarily large heightmap. The 20-gon around this ellipse is roughly
    // 8.3 km: six minutes at the instrumented current 23 m/s race pace. Tiny
    // worlds retain a proportional footprint so the route remains in bounds.
    const float worldDiameter =
        static_cast<float>(config.size - 1u) * config.cellScale;
    const float radiusX = std::min(0.34f, 1'400.0f / worldDiameter);
    const float radiusY = std::min(0.31f, 1'250.0f / worldDiameter);
    const float phase = static_cast<float>(mixBits(config.seed ^ 0x51ed270bu)) *
                        (kTwoPi / 4'294'967'295.0f);
    world.raceRoute.reserve(pointCount + 1u);
    for (uint32_t point = 0; point < pointCount; ++point) {
        const float angle = phase + kTwoPi * static_cast<float>(point) /
                                      static_cast<float>(pointCount);
        const glm::vec2 uv{0.50f + radiusX * std::cos(angle),
                           0.51f + radiusY * std::sin(angle)};
        const glm::vec2 texel = normalizedToTexel(uv, config);
        world.raceRoute.push_back(texelToWorld(texel.x, texel.y, config));
    }
    world.raceRoute.push_back(world.raceRoute.front());

    for (uint32_t point = 0; point < pointCount; ++point) {
        const glm::vec2 from = world.raceRoute[point];
        const glm::vec2 to = world.raceRoute[point + 1u];
        stampRoadSegment(world, from, to, config);
        if ((point % 4u) == 0u) {
            const glm::vec2 delta = to - from;
            world.features.push_back({FeatureKind::Road,
                                      (from + to) * 0.5f,
                                       std::atan2(delta.x, delta.y),
                                      1.0f});
        }
    }

    // A motocross route needs repeated readable beats. Two authored takeoffs
    // per segment keep the production line below ~210 m between interactions,
    // while alternating silhouettes avoids a copy-pasted rhythm.
    for (uint32_t point = 0; point < pointCount; ++point) {
        const glm::vec2 from = world.raceRoute[point];
        const glm::vec2 to = world.raceRoute[point + 1u];
        const glm::vec2 delta = to - from;
        const float heading = std::atan2(delta.x, delta.y);
        for (uint32_t beat = 0u; beat < 2u; ++beat) {
            const float t = beat == 0u ? 0.28f : 0.72f;
            addNativeFeature(
                world,
                ((point + beat) & 1u) == 0u ? FeatureKind::DirtRamp
                                             : FeatureKind::TableJump,
                worldToNormalized(from + delta * t, config), heading, config);
        }
    }
}

[[nodiscard]] glm::vec2 chooseSpawnTexel(
    const WorldGenConfig& config) noexcept {
    const float seedAngle = static_cast<float>(mixBits(config.seed ^ 0xb5297a4du)) *
                            (kTwoPi / 4'294'967'295.0f);
    for (uint32_t candidate = 0; candidate < 32; ++candidate) {
        const float angle = seedAngle + kTwoPi * static_cast<float>(candidate) / 32.0f;
        const glm::vec2 uv = config.coastCenter +
            glm::vec2(std::cos(angle), std::sin(angle)) * 0.135f;
        if (uv.x < 0.055f || uv.x > 0.945f ||
            uv.y < 0.055f || uv.y > 0.945f ||
            classifyBiome(uv, config) != Biome::Coast) {
            continue;
        }
        return normalizedToTexel(uv, config);
    }
    return normalizedToTexel(clampedFeatureUv(config.coastCenter), config);
}

void buildSpawn(WorldGenResult& world, const WorldGenConfig& config) noexcept {
    const glm::vec2 texel = chooseSpawnTexel(config);
    const auto x = static_cast<uint32_t>(std::clamp(
        std::lround(texel.x), 0l, static_cast<long>(config.size - 1u)));
    const auto y = static_cast<uint32_t>(std::clamp(
        std::lround(texel.y), 0l, static_cast<long>(config.size - 1u)));
    const float target = std::max(readHeight(world, x, y, config),
                                  config.heightScale * 0.024f);
    flattenDisc(world, texel, std::clamp(static_cast<float>(config.size) / 42.0f,
                                         3.5f, 9.0f), target, config);
    const float terrainY = readHeight(world, x, y, config);
    const glm::vec2 position = texelToWorld(texel.x, texel.y, config);
    world.spawnPosition = {position.x,
                           terrainY + std::max(1.0f, config.cellScale * 0.5f),
                           position.y};
}

[[nodiscard]] float pointSegmentDistance(
    glm::vec2 point, glm::vec2 from, glm::vec2 to) noexcept {
    const glm::vec2 segment = to - from;
    const float length2 = distanceSquared(from, to);
    if (length2 <= 1.0e-8f) return std::sqrt(distanceSquared(point, from));
    const float t = std::clamp(glm::dot(point - from, segment) / length2,
                               0.0f, 1.0f);
    return std::sqrt(distanceSquared(point, from + segment * t));
}

void stampSurfaceSegment(WorldGenResult& world, glm::vec2 from, glm::vec2 to,
                         float coreRadius, float featherRadius,
                         const WorldGenConfig& config,
                         float colorDarkening = 0.0f) noexcept {
    const uint32_t size = world.surfaceMapSize;
    if (size == 0u || world.surfaceMap.empty()) return;
    const float worldDiameter =
        static_cast<float>(config.size - 1u) * config.cellScale;
    const float texelsPerMetre = static_cast<float>(size - 1u) / worldDiameter;
    const glm::vec2 halfWorld(worldDiameter * 0.5f);
    const auto toSurface = [&](glm::vec2 value) {
        return (value + halfWorld) * texelsPerMetre;
    };
    const glm::vec2 a = toSurface(from);
    const glm::vec2 b = toSurface(to);
    const float outer = std::max(featherRadius * texelsPerMetre, 1.15f);
    const float core = std::max(coreRadius * texelsPerMetre, 0.35f);
    const int32_t minX = std::max(0, static_cast<int32_t>(
        std::floor(std::min(a.x, b.x) - outer - 1.0f)));
    const int32_t maxX = std::min(static_cast<int32_t>(size) - 1,
        static_cast<int32_t>(std::ceil(std::max(a.x, b.x) + outer + 1.0f)));
    const int32_t minY = std::max(0, static_cast<int32_t>(
        std::floor(std::min(a.y, b.y) - outer - 1.0f)));
    const int32_t maxY = std::min(static_cast<int32_t>(size) - 1,
        static_cast<int32_t>(std::ceil(std::max(a.y, b.y) + outer + 1.0f)));
    for (int32_t y = minY; y <= maxY; ++y) {
        for (int32_t x = minX; x <= maxX; ++x) {
            const float distance = pointSegmentDistance(
                {static_cast<float>(x) + 0.5f,
                 static_cast<float>(y) + 0.5f}, a, b);
            const float weight = 1.0f - smoothRange(core, outer, distance);
            if (weight <= 0.0f) continue;
            const size_t alpha =
                (static_cast<size_t>(y) * size + static_cast<uint32_t>(x))
                * 4u + 3u;
            world.surfaceMap[alpha] = std::max(
                world.surfaceMap[alpha],
                static_cast<uint8_t>(std::lround(weight * 255.0f)));
            if (colorDarkening > 0.0f) {
                const float gain = 1.0f - colorDarkening * weight;
                for (size_t channel = 1u; channel <= 3u; ++channel) {
                    world.surfaceMap[alpha - channel] = static_cast<uint8_t>(
                        std::lround(world.surfaceMap[alpha - channel] * gain));
                }
            }
        }
    }
}

void buildSurfaceMap(WorldGenResult& world,
                     const WorldGenConfig& config) {
    world.surfaceMapSize = std::min(config.size, 2048u);
    const uint32_t size = world.surfaceMapSize;
    world.surfaceMap.assign(static_cast<size_t>(size) * size * 4u, 0u);
    const float sourceScale = static_cast<float>(config.size - 1u) /
                              static_cast<float>(size - 1u);
    constexpr uint32_t noiseSeed = 0x62a9d9edu;
    for (uint32_t y = 0u; y < size; ++y) {
        const uint32_t sourceY = std::min(
            static_cast<uint32_t>(std::lround(
                static_cast<float>(y) * sourceScale)),
            config.size - 1u);
        for (uint32_t x = 0u; x < size; ++x) {
            const uint32_t sourceX = std::min(
                static_cast<uint32_t>(std::lround(
                    static_cast<float>(x) * sourceScale)),
                config.size - 1u);
            const Biome biome = static_cast<Biome>(world.biomeMap[
                sampleIndex(sourceX, sourceY, config.size)]);
            const std::array<int, 3> color = biome == Biome::Desert
                ? std::array<int, 3>{145, 119, 76}
                : (biome == Biome::Alpine
                    ? std::array<int, 3>{74, 98, 55}
                    : std::array<int, 3>{68, 102, 53});
            const int variation = static_cast<int>(
                hash2(config.seed ^ noiseSeed,
                      static_cast<int32_t>(x), static_cast<int32_t>(y))
                >> 28u) - 8;
            const size_t index =
                (static_cast<size_t>(y) * size + x) * 4u;
            for (size_t channel = 0u; channel < 3u; ++channel) {
                world.surfaceMap[index + channel] = static_cast<uint8_t>(
                    std::clamp(color[channel] + variation, 0, 255));
            }
        }
    }

    const glm::vec2 spawn(world.spawnPosition.x, world.spawnPosition.z);
    const WorldFeature* heroFeature = nullptr;
    float heroDistance2 = std::numeric_limits<float>::infinity();
    for (const WorldFeature& feature : world.features) {
        if (feature.kind == FeatureKind::Road) continue;
        const float candidate = distanceSquared(feature.position, spawn);
        if (candidate < heroDistance2) {
            heroDistance2 = candidate;
            heroFeature = &feature;
        }
    }

    for (size_t point = 1u; point < world.raceRoute.size(); ++point) {
        stampSurfaceSegment(world, world.raceRoute[point - 1u],
                            world.raceRoute[point], 5.5f, 11.0f, config);
    }
    for (const WorldFeature& feature : world.features) {
        if (feature.kind == FeatureKind::Road) continue;
        const glm::vec2 direction(std::sin(feature.heading),
                                  std::cos(feature.heading));
        stampSurfaceSegment(world, feature.position - direction * 48.0f,
                            feature.position + direction * 42.0f,
                            5.0f, 10.0f, config);
        if (&feature == heroFeature) {
            // One legible hero lane leads from the gate to the nearest jump.
            // Connecting every feature produced a dark starburst on terrain.
            stampSurfaceSegment(world, spawn, feature.position,
                                5.0f, 10.0f, config);
        }
    }

    // Convert splat weight into the macro color used by the triangle fallback.
    for (size_t texel = 0u; texel < world.surfaceMap.size() / 4u; ++texel) {
        const size_t index = texel * 4u;
        const float soil = static_cast<float>(world.surfaceMap[index + 3u]) /
                           255.0f;
        constexpr std::array<float, 3> soilColor{105.0f, 76.0f, 45.0f};
        for (size_t channel = 0u; channel < 3u; ++channel) {
            world.surfaceMap[index + channel] = static_cast<uint8_t>(
                std::lround(std::lerp(
                    static_cast<float>(world.surfaceMap[index + channel]),
                    soilColor[channel], soil)));
        }
    }
    // The darker pass happens after soil recoloring, so it remains an actual
    // visible racing groove instead of being swallowed by the broad core mask.
    if (heroFeature != nullptr) {
        const glm::vec2 direction(std::sin(heroFeature->heading),
                                  std::cos(heroFeature->heading));
        const glm::vec2 side(direction.y, -direction.x);
        stampSurfaceSegment(world, spawn + side * 1.4f,
                            heroFeature->position + side * 1.4f,
                            0.65f, 1.65f, config, 0.32f);
    }
}

void buildTrackProps(WorldGenResult& world, const WorldGenConfig& config) {
    constexpr size_t kMaximumTrackProps = 160u;
    const glm::vec2 spawn(world.spawnPosition.x, world.spawnPosition.z);
    const glm::vec2 spawnDirection(std::sin(world.spawnHeading),
                                   std::cos(world.spawnHeading));
    const glm::vec2 spawnSide(spawnDirection.y, -spawnDirection.x);
    const float extent = 0.5f * static_cast<float>(config.size - 1u)
        * config.cellScale;
    auto add = [&](TrackPropKind kind, glm::vec2 position, float heading,
                   float scale = 1.0f) {
        if (world.trackProps.size() >= kMaximumTrackProps) return;
        position = glm::clamp(position, glm::vec2(-extent), glm::vec2(extent));
        world.trackProps.push_back({kind, position,
                                    normalizedHeading(heading), scale});
    };

    add(TrackPropKind::StartArch, spawn + spawnDirection * 12.0f,
        world.spawnHeading, 0.68f);
    add(TrackPropKind::ArchBanner, spawn + spawnDirection * 12.0f,
        world.spawnHeading, 0.68f);
    add(TrackPropKind::ArchFooting, spawn + spawnDirection * 12.0f,
        world.spawnHeading, 0.68f);
    std::vector<const WorldFeature*> nearby;
    for (const WorldFeature& feature : world.features) {
        if (feature.kind != FeatureKind::Road
            && distanceSquared(feature.position, spawn) < 190.0f * 190.0f) {
            nearby.push_back(&feature);
        }
    }
    std::sort(nearby.begin(), nearby.end(), [&](const WorldFeature* a,
                                                 const WorldFeature* b) {
        return distanceSquared(a->position, spawn) <
               distanceSquared(b->position, spawn);
    });
    nearby.resize(std::min<size_t>(nearby.size(), 8u));
    if (!nearby.empty()) {
        const WorldFeature& feature = *nearby.front();
        const glm::vec2 direction(std::sin(feature.heading),
                                  std::cos(feature.heading));
        const glm::vec2 side(direction.y, -direction.x);
        constexpr float along = -18.0f;
        add(TrackPropKind::MarkerStake,
            feature.position + direction * along - side * 5.5f,
            feature.heading);
        add(TrackPropKind::MarkerStake,
            feature.position + direction * along + side * 5.5f,
            feature.heading);
        add(TrackPropKind::Chevron,
            feature.position - direction * 17.0f - side * 7.0f,
            feature.heading, 0.92f);
        add(TrackPropKind::Chevron,
            feature.position - direction * 17.0f + side * 7.0f,
            feature.heading, 0.92f);
    }

    // Tire-width local detail cannot survive the 4 m production heightfield.
    // Keep one coherent 150 m hero lane instead of radiating three decal lines
    // from spawn. The old radial layout created the giant near-black X that
    // read as a broken shadow in every chase-camera capture.
    for (uint32_t segment = 0u; segment < 18u; ++segment) {
        const float along = 9.0f + static_cast<float>(segment) * 8.0f;
        const glm::vec2 center = spawn + spawnDirection * along;
        add(TrackPropKind::RutStrip, center - spawnSide * 0.48f,
            world.spawnHeading, 1.0f);
        add(TrackPropKind::RutStrip, center + spawnSide * 0.48f,
            world.spawnHeading, 1.0f);
    }
    if (!nearby.empty()) {
        const WorldFeature* feature = nearby.front();
        const glm::vec2 direction(std::sin(feature->heading),
                                  std::cos(feature->heading));
        add(TrackPropKind::LandingPatch,
            feature->position + direction * 18.0f,
            feature->heading, 1.0f);
    }

    for (int sideSign : {-1, 1}) {
        for (uint32_t panel = 0u; panel < 3u; ++panel) {
            add(TrackPropKind::FencePanel,
                spawn + spawnDirection *
                    (32.0f + static_cast<float>(panel) * 12.0f)
                      + spawnSide * (static_cast<float>(sideSign) * 20.0f),
                world.spawnHeading);
        }
    }
    // Low, physical berms break the foreground sheet without changing the
    // motorcycle collision contract. Alternate them along both lane edges.
    for (uint32_t berm = 0u; berm < 6u; ++berm) {
        const float along = 34.0f + static_cast<float>(berm) * 18.0f;
        const float sideSign = (berm & 1u) == 0u ? -1.0f : 1.0f;
        add(TrackPropKind::HeroBerm,
            spawn + spawnDirection * along
                + spawnSide * sideSign
                    * (5.8f + 0.7f * static_cast<float>(berm % 3u)),
            world.spawnHeading + (sideSign < 0.0f ? 0.0f : kPi),
            0.82f + 0.08f * static_cast<float>(berm % 3u));
    }

    // One safety language: paired hay bales at four readable braking points.
    // Rocks, shrubs, and tire stacks created visual noise and hovering props.
    for (uint32_t station = 0u; station < 4u; ++station) {
        const float along = 54.0f + 28.0f * static_cast<float>(station);
        for (float sideSign : {-1.0f, 1.0f}) {
            add(TrackPropKind::HayBale,
                spawn + spawnDirection * along
                    + spawnSide * sideSign * 9.5f,
                world.spawnHeading, 0.90f);
        }
    }
}

[[nodiscard]] bool finiteVec(glm::vec2 value) noexcept {
    return std::isfinite(value.x) && std::isfinite(value.y);
}

[[nodiscard]] bool validCenter(glm::vec2 value) noexcept {
    return finiteVec(value) && value.x >= 0.0f && value.x <= 1.0f &&
           value.y >= 0.0f && value.y <= 1.0f;
}

void setError(std::string* error, const char* message) noexcept {
    if (error == nullptr) {
        return;
    }
#if defined(__cpp_exceptions)
    try {
        *error = message;
    } catch (...) {
        // Reporting an allocation failure must not let another allocation escape.
    }
#else
    *error = message;
#endif
}

[[nodiscard]] bool validateConfig(const WorldGenConfig& config,
                                  std::string* error) noexcept {
    if (config.size < 16u || config.size > 8192u) {
        setError(error, "world size must be in [16, 8192]");
        return false;
    }
    if (!std::isfinite(config.heightScale) || config.heightScale < 1.0f ||
        config.heightScale > 100'000.0f) {
        setError(error, "heightScale must be finite and in [1, 100000]");
        return false;
    }
    if (!std::isfinite(config.cellScale) || config.cellScale < 0.01f ||
        config.cellScale > 10'000.0f) {
        setError(error, "cellScale must be finite and in [0.01, 10000]");
        return false;
    }
    if (!validCenter(config.alpineCenter) || !validCenter(config.desertCenter) ||
        !validCenter(config.coastCenter)) {
        setError(error, "biome centers must be finite and within [0, 1]");
        return false;
    }
    constexpr float minimumCenterDistance2 = 0.05f * 0.05f;
    if (distanceSquared(config.alpineCenter, config.desertCenter) <
            minimumCenterDistance2 ||
        distanceSquared(config.alpineCenter, config.coastCenter) <
            minimumCenterDistance2 ||
        distanceSquared(config.desertCenter, config.coastCenter) <
            minimumCenterDistance2) {
        setError(error, "biome centers must be at least 0.05 apart");
        return false;
    }
    return true;
}

void generateBaseTerrain(WorldGenResult& world,
                         const WorldGenConfig& config) {
    const uint32_t coarseSize = std::min(config.size, 129u);
    std::vector<float> coarse(static_cast<size_t>(coarseSize) * coarseSize);
    const float coarseLast = static_cast<float>(coarseSize - 1u);
    for (uint32_t y = 0; y < coarseSize; ++y) {
        for (uint32_t x = 0; x < coarseSize; ++x) {
            const glm::vec2 uv{static_cast<float>(x) / coarseLast,
                               static_cast<float>(y) / coarseLast};
            coarse[sampleIndex(x, y, coarseSize)] =
                proceduralHeight(uv, config);
        }
    }

    const uint32_t detailSize = std::min(
        1025u, std::max(3u, (config.size - 1u) / 8u + 2u));
    std::vector<float> detail(static_cast<size_t>(detailSize) * detailSize);
    for (uint32_t y = 0; y < detailSize; ++y) {
        for (uint32_t x = 0; x < detailSize; ++x) {
            detail[sampleIndex(x, y, detailSize)] = hashSigned(
                config.seed ^ 0x1b56c4e9u, static_cast<int32_t>(x),
                static_cast<int32_t>(y));
        }
    }

    const size_t sampleCount = static_cast<size_t>(config.size) * config.size;
    world.samples.resize(sampleCount);
    world.biomeMap.resize(sampleCount);
    const float last = static_cast<float>(config.size - 1u);
    const float coarseScale = coarseLast / last;
    const float detailScale = static_cast<float>(detailSize - 1u) / last;
    const float worldDiameter = last * config.cellScale;
    const float reliefScale = std::clamp(worldDiameter / 6'000.0f,
                                         0.24f, 1.0f);
    for (uint32_t y = 0; y < config.size; ++y) {
        const float sourceY = static_cast<float>(y) * coarseScale;
        const uint32_t y0 = std::min(static_cast<uint32_t>(sourceY), coarseSize - 1u);
        const uint32_t y1 = std::min(y0 + 1u, coarseSize - 1u);
        const float ty = smooth(sourceY - static_cast<float>(y0));
        const float detailY = static_cast<float>(y) * detailScale;
        const uint32_t detailY0 = std::min(static_cast<uint32_t>(detailY),
                                           detailSize - 1u);
        const uint32_t detailY1 = std::min(detailY0 + 1u, detailSize - 1u);
        const float detailTy = smooth(detailY - static_cast<float>(detailY0));
        const float v = static_cast<float>(y) / last;
        for (uint32_t x = 0; x < config.size; ++x) {
            const float sourceX = static_cast<float>(x) * coarseScale;
            const uint32_t x0 = std::min(static_cast<uint32_t>(sourceX), coarseSize - 1u);
            const uint32_t x1 = std::min(x0 + 1u, coarseSize - 1u);
            const float tx = smooth(sourceX - static_cast<float>(x0));
            const float top = std::lerp(coarse[sampleIndex(x0, y0, coarseSize)],
                                        coarse[sampleIndex(x1, y0, coarseSize)], tx);
            const float bottom = std::lerp(coarse[sampleIndex(x0, y1, coarseSize)],
                                           coarse[sampleIndex(x1, y1, coarseSize)], tx);
            const glm::vec2 uv{static_cast<float>(x) / last, v};
            const Biome biome = classifyBiome(uv, config);
            const float detailAmplitude = biome == Biome::Coast ? 0.00022f : 0.00065f;
            const float detailX = static_cast<float>(x) * detailScale;
            const uint32_t detailX0 = std::min(static_cast<uint32_t>(detailX),
                                               detailSize - 1u);
            const uint32_t detailX1 = std::min(detailX0 + 1u, detailSize - 1u);
            const float detailTx = smooth(detailX - static_cast<float>(detailX0));
            const float detailTop = std::lerp(
                detail[sampleIndex(detailX0, detailY0, detailSize)],
                detail[sampleIndex(detailX1, detailY0, detailSize)], detailTx);
            const float detailBottom = std::lerp(
                detail[sampleIndex(detailX0, detailY1, detailSize)],
                detail[sampleIndex(detailX1, detailY1, detailSize)], detailTx);
            const float fineHeight = detailAmplitude *
                std::lerp(detailTop, detailBottom, detailTy);
            const size_t index = sampleIndex(x, y, config.size);
            world.samples[index] = encodeHeight(
                (std::lerp(top, bottom, ty) * reliefScale + fineHeight) *
                    config.heightScale,
                config.heightScale);
            world.biomeMap[index] = static_cast<uint8_t>(biome);
        }
    }
}

} // namespace

bool generateWorld(const WorldGenConfig& config, WorldGenResult* out,
                   std::string* error) {
    if (out == nullptr) {
        setError(error, "output world must not be null");
        return false;
    }
    if (!validateConfig(config, error)) {
        return false;
    }

    const auto generate = [&]() {
        WorldGenResult generated;
        generateBaseTerrain(generated, config);
        buildRouteAndRoads(generated, config);

        const float seedAngle = static_cast<float>(mixBits(config.seed ^ 0x243f6a88u)) *
                                (kTwoPi / 4'294'967'295.0f);
        addNativeFeature(generated, FeatureKind::DirtRamp,
                         config.desertCenter + glm::vec2(0.075f, -0.045f),
                         seedAngle, config);
        addNativeFeature(generated, FeatureKind::TableJump,
                         config.coastCenter + glm::vec2(-0.10f, -0.075f),
                         seedAngle + 1.73f, config);
        addNativeFeature(generated, FeatureKind::CliffLip,
                         config.alpineCenter + glm::vec2(0.085f, 0.055f),
                         seedAngle - 1.19f, config);
        buildSpawn(generated, config);

        const float worldDiameter =
            static_cast<float>(config.size - 1u) * config.cellScale;
        const glm::vec2 spawnUv = worldToNormalized(
            {generated.spawnPosition.x, generated.spawnPosition.z}, config);
        // The first minute must not begin in an empty field. Place a compact,
        // readable practice park around spawn with eight distinct approaches.
        // Distances are metric on production worlds and safely contract on
        // tiny test maps.
        for (uint32_t feature = 0u; feature < 8u; ++feature) {
            const float angle = seedAngle + kTwoPi
                * static_cast<float>(feature) / 8.0f;
            const float requestedDistance = feature == 0u ? 58.0f
                : 82.0f + 22.0f * static_cast<float>(feature % 3u);
            const float normalizedDistance = std::min(
                requestedDistance / worldDiameter, 0.04f);
            const glm::vec2 direction{std::cos(angle), std::sin(angle)};
            addNativeFeature(
                generated,
                feature == 0u || (feature & 1u) != 0u
                    ? FeatureKind::TableJump : FeatureKind::DirtRamp,
                spawnUv + direction * normalizedDistance,
                normalizedHeading(std::atan2(direction.x, direction.y)),
                config);
        }

        float nearestFeatureDistance2 =
            std::numeric_limits<float>::infinity();
        const glm::vec2 spawn{generated.spawnPosition.x,
                              generated.spawnPosition.z};
        for (const WorldFeature& feature : generated.features) {
            if (feature.kind == FeatureKind::Road) continue;
            const float distance2 = distanceSquared(feature.position, spawn);
            if (distance2 > 1.0e-4f
                && distance2 < nearestFeatureDistance2) {
                nearestFeatureDistance2 = distance2;
                generated.spawnHeading = feature.heading;
            }
        }

        buildSurfaceMap(generated, config);
        buildTrackProps(generated, config);

        *out = std::move(generated);
        if (error != nullptr) {
            error->clear();
        }
        return true;
    };

#if defined(__cpp_exceptions)
    try {
        return generate();
    } catch (const std::bad_alloc&) {
        setError(error, "world allocation failed");
    } catch (const std::length_error&) {
        setError(error, "world allocation size is not representable");
    }
    return false;
#else
    return generate();
#endif
}

} // namespace voxy::moto
