#pragma once
#include "game/adventure/adventure_session.hpp"
#include "game/adventure/spatial_queries.hpp"
#include <glm/vec3.hpp>
#include <span>
#include <string_view>

namespace voxy::game::adventure {
inline constexpr uint64_t frontierSceneryId=UINT64_MAX-31000;
inline constexpr uint64_t frontierResourceBase=UINT64_MAX-100000;
struct FrontierVisual {
    PieceKind kind=PieceKind::Brick2x2;
    glm::dvec3 feet{},scale{1};
    uint32_t paint=0;
    uint8_t yaw=0;
    uint32_t resource=0,site=0;
    bool light=false,solid=true;
};
struct FrontierDestination {
    uint32_t id=0;
    std::string_view name,region;
    glm::dvec3 feet{};
};
// One installed region recipe. Every visible solid and harvestable has the same
// durable identity and transform in rendering, queries and the content snapshot.
class FrontierWorld {
public:
    static constexpr size_t maximumVisuals=1800;
    bool initialize(const terrain::lego::Surface&,AdventureContent&,std::string&);
    const std::vector<FrontierVisual>& visuals() const noexcept {return visuals_;}
    const std::vector<FrontierDestination>& destinations() const noexcept {return destinations_;}
    glm::dvec3 start() const noexcept {return start_;}
    glm::dvec3 resident() const noexcept {return resident_;}
    std::span<const glm::dvec3> approachSamples() const noexcept {return approachSamples_;}
    bool visible(const FrontierVisual&,const AdventureState&) const noexcept;
    bool appendSolids(const AdventureState&,std::vector<AdventureSpatialQueries::Solid>&) const;
    std::vector<AdventureSpatialQueries::Solid> clearance() const;
    bool protectedEdit(const AdventureState&,const AdventureState&,std::string&) const;
    static uint64_t resourcePart(uint32_t id) noexcept {return frontierResourceBase-id;}
private:
    std::vector<FrontierVisual> visuals_;
    std::vector<FrontierDestination> destinations_;
    std::vector<glm::dvec3> servicePoints_;
    std::vector<glm::dvec3> approachSamples_;
    glm::dvec3 start_{},resident_{},camp_{};
};
} // namespace voxy::game::adventure
