// ═══════════════════════════════════════════════════════════════════════════════
// world.hpp - RIDGEBREAK mixed-biome open world
// ═══════════════════════════════════════════════════════════════════════════════
// Procedural heightfield world generator. Produces a single 8192 x 8192
// heightmap (R16, matching the engine terrain contract) with three biomes:
// alpine peaks, desert canyons, and a coastal flat with a lake. It also places
// big-air features (ramps, cliffs, jumps) and reports a spawn point and race
// route for the game layer.

#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include <glm/glm.hpp>

namespace voxy::moto {

// Instrumented current race pace used to size the canonical production lap.
inline constexpr float kMeasuredRacePaceMetresPerSecond = 23.0f;
inline constexpr float kTargetRaceLapSeconds = 360.0f;

enum class Biome : uint8_t {
    Alpine = 0,
    Desert = 1,
    Coast = 2,
};

enum class FeatureKind : uint8_t {
    DirtRamp = 0,     // launch ramp, riders fly off the lip
    CliffLip = 1,     // natural cliff edge, big drop
    TableJump = 2,    // ramp + flat landing
    Road = 3,         // flat ribbon along a route
};

// Mesh order in data/moto/track.vmesh. World generation emits only typed,
// bounded placements; the renderer resolves them to mesh indices after the
// terrain and asset are both resident.
enum class TrackPropKind : uint8_t {
    StartArch = 0,
    MarkerStake = 1,
    Chevron = 2,
    HayBale = 3,
    TireStack = 4,
    FencePanel = 5,
    Rock = 6,
    Scrub = 7,
    ArchBanner = 8,
    RutStrip = 9,
    LandingPatch = 10,
    HeroBerm = 11,
    ArchFooting = 12,
};

struct WorldFeature {
    FeatureKind kind = FeatureKind::DirtRamp;
    glm::vec2 position{0.0f};      // world (x, z)
    float heading = 0.0f;          // radians, pointing up the takeoff direction
    float scale = 1.0f;            // ramp size multiplier
};

struct WorldTrackProp {
    TrackPropKind kind = TrackPropKind::MarkerStake;
    glm::vec2 position{0.0f};      // world (x, z), Y is sampled after generation
    float heading = 0.0f;
    float scale = 1.0f;
};

struct WorldGenConfig {
    uint32_t size = 8192;          // heightmap resolution
    float heightScale = 480.0f;    // world Y range
    float cellScale = 4.0f;        // world metres per texel
    uint32_t seed = 20260731;
    // Biome region placement, normalized coordinates in [0,1]^2 of the map.
    glm::vec2 alpineCenter{0.32f, 0.30f};
    glm::vec2 desertCenter{0.72f, 0.68f};
    glm::vec2 coastCenter{0.30f, 0.74f};
};

struct WorldGenResult {
    std::vector<uint16_t> samples;      // size*size R16 samples
    std::vector<uint8_t> biomeMap;      // size*size biome IDs
    glm::vec3 spawnPosition{0.0f};      // world position for player spawn
    float spawnHeading = 0.0f;          // nearest practice takeoff direction
    std::vector<WorldFeature> features; // ramps and cliffs
    // Bounded macro surface map. RGB is a deterministic terrain overview;
    // alpha is exposed-soil weight for the route, ruts and feature approaches.
    uint32_t surfaceMapSize = 0u;
    std::vector<uint8_t> surfaceMap;     // surfaceMapSize^2 RGBA8
    std::vector<WorldTrackProp> trackProps;
    // Closed race route (world positions in order). The production-sized
    // world targets kTargetRaceLapSeconds at the measured race pace.
    std::vector<glm::vec2> raceRoute;
};

/// Generate the world. Deterministic for a fixed config.
[[nodiscard]] bool generateWorld(const WorldGenConfig& config,
                                 WorldGenResult* out, std::string* error);

}  // namespace voxy::moto
