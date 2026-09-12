#pragma once

#include "game/assets/fixture_registry.hpp"

namespace voxy::render {

// CPU-only, presentation-only geometry in the installed fixture's metre frame.
// The renderer owns its own copy. No element is a part or durable content ID.
struct CoveDockMarkings {
    static constexpr uint64_t maximumGpuBytes = 16u * 1024u;
    static constexpr uint32_t maximumDraws = 3;
    static constexpr double surfaceLift = .0015;
    static constexpr double edgeInset = .04;
    static constexpr double socketMargin = .02;
    moto::VmeshData mesh;
    game::assets::RigidPrefab prefab;
    std::vector<uint32_t> dockPlacements;
};

// Input must be original installed content, never a workshop/owned overlay.
// Supports the upright 4x2 m Cove plate dock with an unobstructed orthogonal
// route from spawn to boarding. Refusal leaves output unchanged.
[[nodiscard]] bool makeCoveDockMarkings(const game::assets::LoadedAssetFixture& installed,
    CoveDockMarkings& output, std::string& error);

// Revalidates the bounded mesh before owner allocation. Caller counts are not
// trusted. Also enforces lit, opaque, texture-free materials and one rigid root.
[[nodiscard]] bool prepareCoveDockMarkingMesh(const moto::VmeshData& mesh,
    game::assets::RigidPrefab& output, std::string& error);

} // namespace voxy::render
