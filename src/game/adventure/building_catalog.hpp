#pragma once

#include "game/adventure/item_catalog.hpp"
#include "geometry/grid_types.hpp"

#include <glm/vec3.hpp>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace voxy::game::adventure {

// Frozen content IDs. IDs 1–14 use legacy mesh kind-1. HingedDoor reuses
// Doorway's frame plus the separate articulated leaf add-on.
enum class PieceKind : uint8_t {
    Foundation = 1, Floor, Wall, Doorway, Roof, Stair, Beam,
    Brick1x2, Brick2x2, Brick2x4, Bed, Chest, Workbench, Pier, HingedDoor,
    FrontierFoundation, FrontierDeck, FrontierWall, FrontierDoorway, FrontierRoof,
    FrontierStairs, FrontierBed, FrontierChest, FrontierWorkbench, Count
};
enum class FurnitureKind : uint8_t { None, Bed, Chest, Workbench, Door };
using GridBox = geometry::GridBox;
using BuildingCost = MaterialCost;
inline constexpr size_t kLegacyBuildingPieceCount = 15;
inline constexpr size_t kBuildingPieceCount = 24;

struct BuildingDefinition {
    PieceKind kind{};
    std::string_view key, name;
    GridBox bounds{};
    std::span<const GridBox> solids;
    BuildingCost cost{};
    FurnitureKind furniture = FurnitureKind::None;
    bool terrainAnchor = false;
};

// A module composes installed meshes; offsets are metres in the module's local
// frame. The runtime applies part rotation to both the offset and source mesh.
struct BuildingVisual {
    PieceKind source{};
    glm::dvec3 offset{0.0};
    glm::dvec3 scale{1.0};
};
[[nodiscard]] std::span<const BuildingVisual> buildingVisuals(PieceKind) noexcept;
[[nodiscard]] PieceKind buildingIconKind(PieceKind) noexcept;
[[nodiscard]] bool isFrontierPiece(PieceKind) noexcept;
[[nodiscard]] std::string_view frontierBuildingCatalogFingerprint() noexcept;

// Local canonical coordinates: +Y up, -Z forward, bottom-centred origin;
// one lattice tick = .02m. Solids, not the aggregate bounds, are authoritative.
// Stair rises toward -Z in six .16m steps. Roof is a flat .32m plate.
[[nodiscard]] std::span<const BuildingDefinition> buildingCatalog() noexcept;
[[nodiscard]] const BuildingDefinition* buildingDefinition(PieceKind) noexcept;
[[nodiscard]] bool pieceKindValid(uint32_t) noexcept;
// Domain-separated SHA-256 of the frozen legacy JSON plus the door add-on.
// Legacy identity remains explicit for exact schema1–5 migration.
[[nodiscard]] std::string_view buildingCatalogFingerprint() noexcept;
[[nodiscard]] std::string_view legacyBuildingCatalogFingerprint() noexcept;

} // namespace voxy::game::adventure
