#pragma once
#include "game/adventure/adventure_session.hpp"
#include "game/adventure/spatial_queries.hpp"

namespace voxy::game::adventure {
// Quarter-turn catalogue solids in canonical full-world metres. Never use a
// room's enclosing bounds as its collision. Atomic output on malformed input.
[[nodiscard]] bool compileSolids(const AdventureState&,std::vector<AdventureSpatialQueries::Solid>&,std::string&);
// Admission-only blockers for future door operation. Append these while
// choosing installed scenery, then leave them out of player/camera packets.
// Output is replaced atomically; an empty output is valid for old worlds.
[[nodiscard]] bool compileDoorSwingSolids(const AdventureState&,std::vector<AdventureSpatialQueries::Solid>&,std::string&);
// Includes door open state as well as part kind/transform; paint is cosmetic.
[[nodiscard]] bool buildingGeometryChanged(const AdventureState& before,const AdventureState& after,uint64_t part) noexcept;
[[nodiscard]] std::optional<double> terrainPlacementHeight(PieceKind,uint8_t yaw,
    glm::dvec2 position,const terrain::lego::Surface&) noexcept;
// Restore preflight includes protected content even for unchanged records;
// distance from the saved player is deliberately not an admission requirement.
[[nodiscard]] bool validateInstalledGeometry(const AdventureState&,
    const AdventureSpatialQueries&,std::string&,bool freeBuild=false);
// Validates complete support/solids for all changes. Reach, protected access,
// and overlap with the accepted player apply to added/moved parts only.
[[nodiscard]] bool validateConstruction(const AdventureState& before,const AdventureState& after,
    const AdventureSpatialQueries& accepted,std::string& error,bool freeBuild=false);
// Roof covers the bed, at least three cardinal sides have walls, and a nearby
// supported capsule has safe headroom. Returns a clear recovery point, never
// the center of the furniture collider. It grants no health or registration.
[[nodiscard]] bool usableBed(const AdventureState&,uint64_t component,
    const AdventureSpatialQueries&,PlayerPose& recovery,std::string& error);
// Shared furniture use reach and direct sight to a point above its surface.
[[nodiscard]] bool reachableComponent(const AdventureState&,uint64_t component,
    const AdventureSpatialQueries&,std::string& error,bool freeBuild=false);
[[nodiscard]] bool validateInteractions(const AdventureState& before,const AdventureState& after,
    const AdventureContent&,const AdventureSpatialQueries&,std::string& error);
// Optional layout only; grants no items or accepted geometry. Origin is the
// center of the 4x4 foundation footprint at its bottom. The caller charges and
// validates the entire layout and assigns one structure identity to all parts.
[[nodiscard]] std::vector<PlacePart> starterRoomLayout(GridPosition origin,uint8_t yaw,std::string& error);
} // namespace voxy::game::adventure
