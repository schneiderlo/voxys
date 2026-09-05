#include <gtest/gtest.h>

#include "moto/race.hpp"
#include "moto/world.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <string>
#include <vector>

namespace voxy::moto {
namespace {

[[nodiscard]] float worldHeight(uint16_t sample, float heightScale) {
    return heightScale *
        (2.0f * static_cast<float>(sample) / 65'535.0f - 1.0f);
}

[[nodiscard]] uint32_t worldToSample(float coordinate,
                                     const WorldGenConfig& config) {
    const float half = 0.5f * static_cast<float>(config.size - 1u);
    return static_cast<uint32_t>(std::clamp(
        std::lround(coordinate / config.cellScale + half), 0l,
        static_cast<long>(config.size - 1u)));
}

[[nodiscard]] size_t indexAt(uint32_t x, uint32_t y,
                             const WorldGenConfig& config) {
    return static_cast<size_t>(y) * config.size + x;
}

[[nodiscard]] float terrainAt(glm::vec2 position,
                              const WorldGenConfig& config,
                              const WorldGenResult& world) {
    const uint32_t x = worldToSample(position.x, config);
    const uint32_t y = worldToSample(position.y, config);
    return worldHeight(world.samples[indexAt(x, y, config)],
                       config.heightScale);
}

[[nodiscard]] float routeLength(std::span<const glm::vec2> route) {
    float total = 0.0f;
    for (size_t index = 1u; index < route.size(); ++index) {
        total += glm::length(route[index] - route[index - 1u]);
    }
    return total;
}

[[nodiscard]] float pointSegmentDistance(glm::vec2 point, glm::vec2 from,
                                         glm::vec2 to) {
    const glm::vec2 segment = to - from;
    const float length2 = glm::dot(segment, segment);
    const float t = length2 > 1.0e-6f
        ? std::clamp(glm::dot(point - from, segment) / length2, 0.0f, 1.0f)
        : 0.0f;
    return glm::length(point - (from + segment * t));
}

[[nodiscard]] WorldGenResult makeWorld(uint32_t seed = 20260731u,
                                       uint32_t size = 256u) {
    WorldGenConfig config;
    config.size = size;
    config.seed = seed;
    WorldGenResult world;
    std::string error;
    EXPECT_TRUE(generateWorld(config, &world, &error)) << error;
    return world;
}

TEST(WorldGenerationTest, AcceptsCanonicalSizesAndProducesR16Range) {
    for (const uint32_t size : {16u, 256u}) {
        WorldGenConfig config;
        config.size = size;
        WorldGenResult world;
        std::string error;
        ASSERT_TRUE(generateWorld(config, &world, &error)) << error;
        const size_t expected = static_cast<size_t>(size) * size;
        ASSERT_EQ(world.samples.size(), expected);
        ASSERT_EQ(world.biomeMap.size(), expected);
        ASSERT_EQ(world.surfaceMapSize, size);
        ASSERT_EQ(world.surfaceMap.size(), expected * 4u);
        EXPECT_FALSE(world.trackProps.empty());
        EXPECT_LE(world.trackProps.size(), 160u);

        const auto [minimum, maximum] = std::minmax_element(
            world.samples.begin(), world.samples.end());
        EXPECT_LT(*minimum, uint16_t{32'768});
        EXPECT_GT(*maximum, uint16_t{32'768});
        EXPECT_GE(worldHeight(*minimum, config.heightScale),
                  -config.heightScale);
        EXPECT_LE(worldHeight(*maximum, config.heightScale),
                  config.heightScale);
    }
}

TEST(WorldGenerationTest, SameSeedIsBitIdentical) {
    const WorldGenResult first = makeWorld(0x12345678u);
    const WorldGenResult second = makeWorld(0x12345678u);
    EXPECT_EQ(first.samples, second.samples);
    EXPECT_EQ(first.biomeMap, second.biomeMap);
    EXPECT_EQ(first.surfaceMapSize, second.surfaceMapSize);
    EXPECT_EQ(first.surfaceMap, second.surfaceMap);
    EXPECT_EQ(first.trackProps.size(), second.trackProps.size());
    for (size_t i = 0; i < first.trackProps.size(); ++i) {
        EXPECT_EQ(first.trackProps[i].kind, second.trackProps[i].kind);
        EXPECT_EQ(first.trackProps[i].position, second.trackProps[i].position);
        EXPECT_FLOAT_EQ(first.trackProps[i].heading,
                        second.trackProps[i].heading);
        EXPECT_FLOAT_EQ(first.trackProps[i].scale,
                        second.trackProps[i].scale);
    }
    ASSERT_EQ(first.features.size(), second.features.size());
    for (size_t i = 0; i < first.features.size(); ++i) {
        EXPECT_EQ(first.features[i].kind, second.features[i].kind);
        EXPECT_EQ(first.features[i].position, second.features[i].position);
        EXPECT_FLOAT_EQ(first.features[i].heading, second.features[i].heading);
        EXPECT_FLOAT_EQ(first.features[i].scale, second.features[i].scale);
    }
    EXPECT_EQ(first.spawnPosition, second.spawnPosition);
    EXPECT_FLOAT_EQ(first.spawnHeading, second.spawnHeading);
    EXPECT_EQ(first.raceRoute, second.raceRoute);
}

TEST(WorldGenerationTest, DifferentSeedsChangeTerrain) {
    const WorldGenResult first = makeWorld(1u);
    const WorldGenResult second = makeWorld(2u);
    EXPECT_NE(first.samples, second.samples);
}

TEST(WorldGenerationTest, ContainsEveryBiomeAndSubmergedCoast) {
    WorldGenConfig config;
    config.size = 256;
    const WorldGenResult world = makeWorld(config.seed, config.size);
    std::array<size_t, 3> counts{};
    size_t submergedCoast = 0;
    for (size_t i = 0; i < world.biomeMap.size(); ++i) {
        ASSERT_LT(world.biomeMap[i], counts.size());
        ++counts[world.biomeMap[i]];
        if (world.biomeMap[i] == static_cast<uint8_t>(Biome::Coast) &&
            worldHeight(world.samples[i], config.heightScale) < 0.0f) {
            ++submergedCoast;
        }
    }
    for (const size_t count : counts) {
        EXPECT_GT(count, 0u);
    }
    EXPECT_GT(submergedCoast, 0u);
}

TEST(WorldGenerationTest, FeaturesAndCoastalSpawnAreValid) {
    WorldGenConfig config;
    config.size = 256;
    const WorldGenResult world = makeWorld(config.seed, config.size);
    const float extent = 0.5f * static_cast<float>(config.size - 1u) *
                         config.cellScale;
    std::array<bool, 4> found{};
    for (const WorldFeature& feature : world.features) {
        const size_t kind = static_cast<size_t>(feature.kind);
        ASSERT_LT(kind, found.size());
        found[kind] = true;
        EXPECT_TRUE(std::isfinite(feature.position.x));
        EXPECT_TRUE(std::isfinite(feature.position.y));
        EXPECT_TRUE(std::isfinite(feature.heading));
        EXPECT_GE(feature.position.x, -extent);
        EXPECT_LE(feature.position.x, extent);
        EXPECT_GE(feature.position.y, -extent);
        EXPECT_LE(feature.position.y, extent);
        EXPECT_GE(feature.heading, -3.141593f);
        EXPECT_LE(feature.heading, 3.141593f);
        EXPECT_GT(feature.scale, 0.0f);
    }
    for (const bool present : found) {
        EXPECT_TRUE(present);
    }

    EXPECT_GE(world.spawnPosition.x, -extent);
    EXPECT_LE(world.spawnPosition.x, extent);
    EXPECT_GE(world.spawnPosition.z, -extent);
    EXPECT_LE(world.spawnPosition.z, extent);
    const glm::vec2 spawnXZ{world.spawnPosition.x, world.spawnPosition.z};
    const float spawnTerrain = terrainAt(spawnXZ, config, world);
    EXPECT_GT(world.spawnPosition.y, spawnTerrain);
    const uint32_t spawnX = worldToSample(world.spawnPosition.x, config);
    const uint32_t spawnY = worldToSample(world.spawnPosition.z, config);
    EXPECT_EQ(world.biomeMap[indexAt(spawnX, spawnY, config)],
              static_cast<uint8_t>(Biome::Coast));

    float localMinimum = std::numeric_limits<float>::max();
    float localMaximum = std::numeric_limits<float>::lowest();
    for (int32_t dy = -1; dy <= 1; ++dy) {
        for (int32_t dx = -1; dx <= 1; ++dx) {
            const uint32_t x = static_cast<uint32_t>(
                static_cast<int32_t>(spawnX) + dx);
            const uint32_t y = static_cast<uint32_t>(
                static_cast<int32_t>(spawnY) + dy);
            const float height = worldHeight(world.samples[indexAt(x, y, config)],
                                             config.heightScale);
            localMinimum = std::min(localMinimum, height);
            localMaximum = std::max(localMaximum, height);
        }
    }
    EXPECT_LT(localMaximum - localMinimum, 0.1f);
}

TEST(WorldGenerationTest, RejectsInvalidConfiguration) {
    WorldGenResult world;
    std::string error;

    WorldGenConfig config;
    config.size = 15;
    EXPECT_FALSE(generateWorld(config, &world, &error));
    EXPECT_FALSE(error.empty());
    config.size = 8193;
    EXPECT_FALSE(generateWorld(config, &world, &error));

    config = {};
    config.heightScale = std::numeric_limits<float>::infinity();
    EXPECT_FALSE(generateWorld(config, &world, &error));
    config = {};
    config.cellScale = 0.0f;
    EXPECT_FALSE(generateWorld(config, &world, &error));
    config = {};
    config.desertCenter.x = std::numeric_limits<float>::quiet_NaN();
    EXPECT_FALSE(generateWorld(config, &world, &error));
    config = {};
    config.coastCenter.y = 1.01f;
    EXPECT_FALSE(generateWorld(config, &world, &error));
    config = {};
    config.coastCenter = config.alpineCenter;
    EXPECT_FALSE(generateWorld(config, &world, &error));
    config = {};
    EXPECT_FALSE(generateWorld(config, nullptr, &error));
}

TEST(WorldGenerationTest, RaceRouteIsClosedAndInBounds) {
    WorldGenConfig config;
    config.size = 256;
    const WorldGenResult world = makeWorld(config.seed, config.size);
    ASSERT_GE(world.raceRoute.size(), 16u);
    ASSERT_LE(world.raceRoute.size(), 32u);
    ASSERT_EQ(world.raceRoute.front(), world.raceRoute.back());
    const float extent = 0.5f * static_cast<float>(config.size - 1u) *
                         config.cellScale;
    for (const glm::vec2 point : world.raceRoute) {
        EXPECT_TRUE(std::isfinite(point.x));
        EXPECT_TRUE(std::isfinite(point.y));
        EXPECT_GE(point.x, -extent);
        EXPECT_LE(point.x, extent);
        EXPECT_GE(point.y, -extent);
        EXPECT_LE(point.y, extent);
    }
}

TEST(WorldGenerationTest, ProductionRouteTargetsSixMinutesAtMeasuredPace) {
    // This test map has production-scale metric extents with fewer texels, so
    // the route contract is exercised without allocating the full 8192 map.
    WorldGenConfig config;
    config.size = 1024u;
    config.cellScale = 10.0f;
    WorldGenResult world;
    std::string error;
    ASSERT_TRUE(generateWorld(config, &world, &error)) << error;

    const float lengthMetres = routeLength(world.raceRoute);
    const float lapSeconds = lengthMetres / kMeasuredRacePaceMetresPerSecond;
    EXPECT_GT(lengthMetres, 8'150.0f);
    EXPECT_LT(lengthMetres, 8'450.0f);
    EXPECT_NEAR(lapSeconds, kTargetRaceLapSeconds, 7.0f);

    const RaceConfig defaultCircuit;
    const float circuitBudgetSeconds =
        static_cast<float>(defaultCircuit.durationTicks)
        / static_cast<float>(defaultCircuit.tickRate);
    EXPECT_GE(circuitBudgetSeconds - lapSeconds, 20.0f);

    // Exercise the exact closure-removal and tangent construction used by
    // Application::initMoto, then run RaceSession's all-pair validation.
    std::vector<RaceCheckpoint> checkpoints;
    ASSERT_TRUE(buildDirectedRaceCheckpoints(
        world.raceRoute, 18.0f, 12.0f, &checkpoints, &error)) << error;
    ASSERT_EQ(checkpoints.size(), 20u);
    EXPECT_NE(checkpoints.front().center, checkpoints.back().center);
    RaceSession race;
    RaceConfig raceConfig;
    ASSERT_TRUE(race.configure(raceConfig, checkpoints, &error)) << error;
}

TEST(WorldGenerationTest, RouteAndSpawnHaveDenseNearbyActionFeatures) {
    WorldGenConfig config;
    config.size = 512u;
    const WorldGenResult world = makeWorld(config.seed, config.size);

    size_t nearbySpawn = 0u;
    const glm::vec2 spawn{world.spawnPosition.x, world.spawnPosition.z};
    const WorldFeature* nearestAction = nullptr;
    float nearestDistance2 = std::numeric_limits<float>::infinity();
    for (const WorldFeature& feature : world.features) {
        if (feature.kind != FeatureKind::Road
            && glm::length(feature.position - spawn) <= 130.0f) {
            ++nearbySpawn;
        }
        if (feature.kind != FeatureKind::Road) {
            const glm::vec2 delta = feature.position - spawn;
            const float distance2 = glm::dot(delta, delta);
            if (distance2 > 1.0e-4f && distance2 < nearestDistance2) {
                nearestDistance2 = distance2;
                nearestAction = &feature;
            }
        }
    }
    EXPECT_GE(nearbySpawn, 8u);
    ASSERT_NE(nearestAction, nullptr);
    EXPECT_FLOAT_EQ(world.spawnHeading, nearestAction->heading);
    const glm::vec2 spawnForward{std::sin(world.spawnHeading),
                                 std::cos(world.spawnHeading)};
    const glm::vec2 takeoffForward{std::sin(nearestAction->heading),
                                   std::cos(nearestAction->heading)};
    EXPECT_NEAR(glm::dot(spawnForward, takeoffForward), 1.0f, 1.0e-6f);
    const glm::vec2 directionToFeature = glm::normalize(
        nearestAction->position - spawn);
    EXPECT_GT(glm::dot(directionToFeature, takeoffForward), 0.99f);

    // Every route segment has two on-line authored interactions. This catches
    // the old sparse layout where several hundred metres had no gameplay beat.
    ASSERT_GE(world.raceRoute.size(), 2u);
    for (size_t segment = 1u; segment < world.raceRoute.size(); ++segment) {
        const glm::vec2 from = world.raceRoute[segment - 1u];
        const glm::vec2 to = world.raceRoute[segment];
        size_t onSegment = 0u;
        for (const WorldFeature& feature : world.features) {
            if (feature.kind == FeatureKind::Road) continue;
            if (pointSegmentDistance(feature.position, from, to) <= 1.0f
                && glm::dot(feature.position - from, to - from) > 0.0f
                && glm::dot(feature.position - to, from - to) > 0.0f) {
                ++onSegment;
            }
        }
        EXPECT_GE(onSegment, 2u) << "sparse route segment " << segment - 1u;
    }
}

TEST(WorldGenerationTest, SurfaceSplatAndStaticTrackKitMarkSpawnPractice) {
    WorldGenConfig config;
    config.size = 512u;
    const WorldGenResult world = makeWorld(config.seed, config.size);
    ASSERT_EQ(world.surfaceMapSize, 512u);
    ASSERT_EQ(world.surfaceMap.size(), size_t{512u} * 512u * 4u);

    const float worldDiameter = static_cast<float>(config.size - 1u)
        * config.cellScale;
    const auto surfaceIndex = [&](glm::vec2 position) {
        const glm::vec2 uv = glm::clamp(
            position / worldDiameter + glm::vec2(0.5f),
            glm::vec2(0.0f), glm::vec2(1.0f));
        const uint32_t x = static_cast<uint32_t>(std::lround(
            uv.x * static_cast<float>(world.surfaceMapSize - 1u)));
        const uint32_t y = static_cast<uint32_t>(std::lround(
            uv.y * static_cast<float>(world.surfaceMapSize - 1u)));
        return (static_cast<size_t>(y) * world.surfaceMapSize + x) * 4u;
    };
    const glm::vec2 spawn(world.spawnPosition.x, world.spawnPosition.z);
    const size_t spawnTexel = surfaceIndex(spawn);
    EXPECT_GT(world.surfaceMap[spawnTexel + 3u], 180u);

    const WorldFeature* nearest = nullptr;
    float nearestDistance2 = std::numeric_limits<float>::infinity();
    for (const WorldFeature& feature : world.features) {
        if (feature.kind == FeatureKind::Road) continue;
        const float distance2 = glm::dot(feature.position - spawn,
                                         feature.position - spawn);
        if (distance2 < nearestDistance2) {
            nearestDistance2 = distance2;
            nearest = &feature;
        }
    }
    ASSERT_NE(nearest, nullptr);
    EXPECT_EQ(nearest->kind, FeatureKind::TableJump);
    EXPECT_GT(world.surfaceMap[surfaceIndex(nearest->position) + 3u], 180u);
    const glm::vec2 heroVector = nearest->position - spawn;
    ASSERT_GT(glm::length(heroVector), 1.0f);
    const glm::vec2 heroForward = glm::normalize(heroVector);
    const glm::vec2 heroSide(heroForward.y, -heroForward.x);
    const glm::vec2 heroMidpoint = spawn + heroVector * 0.5f;
    const uint8_t heroSoil = world.surfaceMap[
        surfaceIndex(heroMidpoint) + 3u];
    const uint8_t grassShoulder = world.surfaceMap[
        surfaceIndex(heroMidpoint + heroSide * 14.0f) + 3u];
    EXPECT_GT(heroSoil, grassShoulder + 64u)
        << "hero dirt lane must read clearly against its grass shoulder";

    ASSERT_FALSE(world.trackProps.empty());
    EXPECT_EQ(world.trackProps.size(), 64u);
    std::array<size_t, 13> kindCounts{};
    const float extent = worldDiameter * 0.5f;
    std::array<std::vector<float>, 2> rutAlong;
    const glm::vec2 forward(std::sin(world.spawnHeading),
                            std::cos(world.spawnHeading));
    const glm::vec2 side(forward.y, -forward.x);
    for (const WorldTrackProp& prop : world.trackProps) {
        const size_t kind = static_cast<size_t>(prop.kind);
        ASSERT_LT(kind, kindCounts.size());
        ++kindCounts[kind];
        EXPECT_TRUE(std::isfinite(prop.heading));
        EXPECT_GT(prop.scale, 0.0f);
        EXPECT_GE(prop.position.x, -extent);
        EXPECT_LE(prop.position.x, extent);
        EXPECT_GE(prop.position.y, -extent);
        EXPECT_LE(prop.position.y, extent);
        EXPECT_LE(glm::length(prop.position - spawn), 220.0f);
        if (prop.kind == TrackPropKind::RutStrip) {
            const glm::vec2 delta = prop.position - spawn;
            const float along = glm::dot(delta, forward);
            const float lateral = glm::dot(delta, side);
            EXPECT_GE(along, 8.9f);
            EXPECT_LE(along, 146.0f);
            EXPECT_NEAR(std::abs(lateral), 0.48f, 1.0e-3f);
            EXPECT_NEAR(std::remainder(prop.heading - world.spawnHeading,
                                       2.0f * 3.14159265358979323846f),
                        0.0f, 1.0e-5f);
            rutAlong[lateral < 0.0f ? 0u : 1u].push_back(along);
        }
    }
    EXPECT_EQ(kindCounts[static_cast<size_t>(TrackPropKind::StartArch)], 1u);
    EXPECT_EQ(kindCounts[static_cast<size_t>(TrackPropKind::ArchBanner)], 1u);
    EXPECT_EQ(kindCounts[static_cast<size_t>(TrackPropKind::ArchFooting)], 1u);
    EXPECT_EQ(kindCounts[static_cast<size_t>(TrackPropKind::MarkerStake)], 2u);
    EXPECT_EQ(kindCounts[static_cast<size_t>(TrackPropKind::Chevron)], 2u);
    EXPECT_EQ(kindCounts[static_cast<size_t>(TrackPropKind::HayBale)], 8u);
    EXPECT_EQ(kindCounts[static_cast<size_t>(TrackPropKind::FencePanel)], 6u);
    EXPECT_EQ(kindCounts[static_cast<size_t>(TrackPropKind::RutStrip)], 36u);
    EXPECT_EQ(kindCounts[static_cast<size_t>(TrackPropKind::LandingPatch)], 1u);
    EXPECT_EQ(kindCounts[static_cast<size_t>(TrackPropKind::HeroBerm)], 6u);
    EXPECT_EQ(kindCounts[static_cast<size_t>(TrackPropKind::TireStack)], 0u);
    EXPECT_EQ(kindCounts[static_cast<size_t>(TrackPropKind::Rock)], 0u);
    EXPECT_EQ(kindCounts[static_cast<size_t>(TrackPropKind::Scrub)], 0u);
    for (std::vector<float>& wheelLine : rutAlong) {
        ASSERT_EQ(wheelLine.size(), 18u);
        std::sort(wheelLine.begin(), wheelLine.end());
        for (size_t segment = 1u; segment < wheelLine.size(); ++segment) {
            // Eight-metre decals may meet at their transparent boundary but
            // never overlap into a stacked dark band.
            EXPECT_GE(wheelLine[segment] - wheelLine[segment - 1u], 7.99f);
        }
    }
}

} // namespace
} // namespace voxy::moto
