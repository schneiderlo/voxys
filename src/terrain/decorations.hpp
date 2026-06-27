// ═══════════════════════════════════════════════════════════════════════════════
// decorations.hpp - Biome-Driven Terrain Decoration Placement (C++20)
// ═══════════════════════════════════════════════════════════════════════════════

#pragma once

#include <cstdint>
#include <span>
#include <vector>

namespace voxy::terrain {

enum class DecorationKind : uint32_t {
    BroadleafTree = 0,
    PineTree = 1,
    GrassClump = 2,
    FlowerPatch = 3,
    ReedCluster = 4,
    RockShard = 5,
    DryShrub = 6,
    JungleTree = 7,
    AcaciaTree = 8,
    CypressTree = 9,
    MushroomCluster = 10,
    CactusColumn = 11,
};

[[nodiscard]] constexpr bool isTreeDecoration(DecorationKind kind) noexcept {
    return kind == DecorationKind::BroadleafTree ||
           kind == DecorationKind::PineTree ||
           kind == DecorationKind::JungleTree ||
           kind == DecorationKind::AcaciaTree ||
           kind == DecorationKind::CypressTree;
}

struct TerrainDecoration {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float radius = 1.0f;
    float height = 4.0f;
    float colorR = 0.10f;
    float colorG = 0.28f;
    float colorB = 0.08f;
    DecorationKind kind = DecorationKind::BroadleafTree;
};

using TreeDecoration = TerrainDecoration;

struct DecorationConfig {
    std::span<const uint16_t> heightSamples;
    uint32_t width = 0;
    uint32_t height = 0;
    float heightScale = 1.0f;
    float cellScale = 1.0f;
    uint32_t spacingCells = 10;
    uint32_t maxTrees = 22000;
    uint32_t maxGroundDecorations = 11000;
    uint32_t seed = 0x4d43524fu;
    bool enabled = true;
};

[[nodiscard]] bool hasDecorationHeightSamples(const DecorationConfig& config) noexcept;

[[nodiscard]] float sampleDecorationHeightMeters(const DecorationConfig& config,
                                                 uint32_t x, uint32_t y) noexcept;

[[nodiscard]] float estimateDecorationSlope(const DecorationConfig& config,
                                            uint32_t x, uint32_t y) noexcept;

[[nodiscard]] std::vector<TerrainDecoration> generateTerrainDecorations(const DecorationConfig& config);

[[nodiscard]] std::vector<TreeDecoration> generateTreeDecorations(const DecorationConfig& config);

} // namespace voxy::terrain
