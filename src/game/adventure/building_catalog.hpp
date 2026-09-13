#pragma once

#include "game/adventure/item_catalog.hpp"
#include "geometry/grid_types.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace voxy::game::adventure {

// Frozen content IDs. The installed mesh index is uint32_t(kind) - 1.
enum class PieceKind : uint8_t {
    Foundation = 1, Floor, Wall, Doorway, Roof, Stair, Beam,
    Brick1x2, Brick2x2, Brick2x4, Bed, Chest, Workbench, Pier, Count
};
enum class FurnitureKind : uint8_t { None, Bed, Chest, Workbench };
using GridBox = geometry::GridBox;
using BuildingCost = MaterialCost;
inline constexpr size_t kBuildingPieceCount = 14;

struct BuildingDefinition {
    PieceKind kind{};
    std::string_view key, name;
    GridBox bounds{};
    std::span<const GridBox> solids;
    BuildingCost cost{};
    FurnitureKind furniture = FurnitureKind::None;
    bool terrainAnchor = false;
};

// Local canonical coordinates: +Y up, -Z forward, bottom-centred origin;
// one lattice tick = .02m. Solids, not the aggregate bounds, are authoritative.
// Stair rises toward -Z in six .16m steps. Roof is a flat .32m plate.
[[nodiscard]] std::span<const BuildingDefinition> buildingCatalog() noexcept;
[[nodiscard]] const BuildingDefinition* buildingDefinition(PieceKind) noexcept;
[[nodiscard]] bool pieceKindValid(uint32_t) noexcept;
// SHA-256 of the installed canonical JSON source. Part of adventure content
// identity; change both generated catalog and installed geometry together.
[[nodiscard]] std::string_view buildingCatalogFingerprint() noexcept;

} // namespace voxy::game::adventure
