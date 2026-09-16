#pragma once

#include "game/adventure/building_catalog.hpp"
#include "game/construction/construction_types.hpp"
#include <span>
#include <string_view>
#include <vector>

namespace voxy::game::adventure {

struct PlacePart;
using GridPosition=construction::GridPosition;
enum class BlueprintKind : uint8_t { None=0,StarterRoom=1,WideStoneStep=2 };
struct BuildingBlueprintDefinition {
    BlueprintKind kind=BlueprintKind::None;
    std::string_view name,description;
    MaterialCost cost{};
    PieceKind anchorPiece=PieceKind::Foundation;
};
[[nodiscard]] std::span<const BuildingBlueprintDefinition> buildingBlueprints() noexcept;
[[nodiscard]] const BuildingBlueprintDefinition* buildingBlueprintDefinition(BlueprintKind) noexcept;
// Pure candidate geometry at an arbitrary world anchor. This neither unlocks
// recipes nor grants pieces: the session checks quest entitlement and charges
// the full catalog cost before accepting the complete construction candidate.
// All parts request one new structure; callers retain the existing canonical
// .02 m lattice, bottom-centre anchor and yaw-only quarter-turn convention.
// Invalid kinds, rotations or coordinate overflow return an empty layout.
[[nodiscard]] std::vector<PlacePart> buildingBlueprintLayout(BlueprintKind,
    GridPosition anchor,uint8_t yawQuarterTurns,std::string& error);

} // namespace voxy::game::adventure
