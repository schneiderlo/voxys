#pragma once

#include "game/adventure/adventure_session.hpp"
#include "game/adventure/spatial_queries.hpp"
#include <array>
#include <string_view>

namespace voxy::game::adventure {
// Original installed prop mesh indices. Canonical dimensions are fixed; the
// rendering adapter may translate/quarter-turn these meshes, never stretch them.
enum class VillagePropKind : uint8_t { Roof=0, Planter=1, Tree=2, PathTile=3 };
struct VillagePiece { PieceKind kind{}; GridPosition position{}; uint8_t yawQuarterTurns=0; };
struct VillageProp { VillagePropKind kind{}; glm::dvec3 feet{}; uint8_t yawQuarterTurns=0; };
struct VillageGroup {
    uint8_t id=0;
    std::string_view name;
    bool available=false;
    std::vector<VillagePiece> pieces;
    std::vector<VillageProp> props;
    std::vector<AdventureSpatialQueries::Solid> solids;
    // Optional ordinary walking route: outside town, staircase foot, interior.
    std::vector<glm::dvec3> route;
    glm::dvec3 minimum{},maximum{};
};

[[nodiscard]] constexpr uint64_t villageStructureId(uint8_t group) noexcept {return UINT64_MAX-4096u-group;}
[[nodiscard]] constexpr uint64_t villagePartId(uint8_t group,uint8_t local) noexcept {return UINT64_MAX-8192u-static_cast<uint64_t>(group)*128u-local;}
[[nodiscard]] bool isVillagePartId(uint64_t) noexcept;
[[nodiscard]] glm::dvec3 villagePropSize(VillagePropKind) noexcept;

// Derived scenery never becomes player inventory or a functional furniture
// component. Restore passes previous=nullptr. A conflicting group is entirely
// absent (draws and colliders); the whole admission remains fixed during the
// running session, including deferred groups. Reevaluate only on fresh load.
// Existing saves keep exact structures and player pose.
class VillageLayout {
public:
    static constexpr size_t maximumGroups=8,maximumPieces=64,maximumProps=32,maximumSolids=256;
    [[nodiscard]] static VillageLayout admit(const AdventureState&,const AdventureContent&,
        const AdventureSpatialQueries& base,std::span<const AdventureSpatialQueries::Solid> baseSolids,
        const VillageLayout* previous=nullptr);
    [[nodiscard]] const std::array<VillageGroup,maximumGroups>& groups() const noexcept {return groups_;}
    [[nodiscard]] bool appendSolids(construction::WorldNamespace,std::vector<AdventureSpatialQueries::Solid>&) const;
    // Called for proposed player construction after ordinary construction rules.
    // Restored or unchanged parts retain their exact old placement. No blanket
    // expansion of the protected town/building exclusion is introduced.
    [[nodiscard]] bool validateNewConstruction(const AdventureState& before,const AdventureState& after,std::string& error) const;
private:
    construction::WorldNamespace world_{};
    std::array<VillageGroup,maximumGroups> groups_{};
};
}
