#pragma once
#include "game/adventure/adventure_session.hpp"
#include "game/adventure/spatial_queries.hpp"
namespace voxy::game::adventure {
struct CreativeVillagePiece {uint8_t mesh=0,yaw=0;glm::dvec3 feet{},scale{1};};
struct CreativeVillageGroup {
    uint16_t id=0;
    std::vector<CreativeVillagePiece> pieces;
    std::vector<AdventureSpatialQueries::Solid> solids;
    glm::dvec3 minimum{},maximum{};
};
// Derived village. A complete building (foundation, steps and roof) yields to
// player construction together. Suppressed groups stay absent until the next load.
class CreativeVillage {
public:
    static CreativeVillage admit(const AdventureState&,const terrain::lego::Surface&,
        std::span<const AdventureSpatialQueries::Solid> reserved,
        const CreativeVillage* previous=nullptr,
        std::span<const AdventureSpatialQueries::Solid> actorClearance={});
    const std::vector<CreativeVillageGroup>& groups() const noexcept {return groups_;}
    bool appendSolids(std::vector<AdventureSpatialQueries::Solid>&) const;
private:
    bool initialized_=false;
    std::vector<CreativeVillageGroup> groups_;
};
}
