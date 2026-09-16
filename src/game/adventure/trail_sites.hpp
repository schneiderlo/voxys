#pragma once

#include "game/adventure/adventure_session.hpp"
#include "game/adventure/adventure_trail_content.hpp"
#include "game/adventure/spatial_queries.hpp"
#include <array>
#include <string_view>

namespace voxy::game::adventure {
enum class TrailSiteId : uint8_t { SignalTerrace=1,SurveyOverlook=2,Relay=3,WatchArch=4 };
struct TrailSiteDefinition {
    TrailSiteId id{};
    std::string_view name,description;
    glm::dvec2 anchor{};
};
struct TrailSite {
    TrailSiteId id{};
    PlayerPose position{};
    bool available=false,active=false;
};
struct TrailPiece {
    PieceKind kind{};
    GridPosition position{};
    uint8_t yawQuarterTurns=0;
};
struct TrailGroup {
    uint8_t id=0;
    std::string_view name;
    bool available=false;
    std::vector<TrailPiece> pieces;
    std::vector<AdventureSpatialQueries::Solid> solids;
    glm::dvec3 minimum{},maximum{};
};
[[nodiscard]] std::span<const TrailSiteDefinition> trailSiteDefinitions() noexcept;
[[nodiscard]] const TrailSiteDefinition* trailSiteDefinition(TrailSiteId) noexcept;
[[nodiscard]] constexpr uint64_t trailStructureId(uint8_t group) noexcept {return UINT64_MAX-65536u-group;}
[[nodiscard]] constexpr uint64_t trailPartId(uint8_t group,uint8_t local) noexcept {return UINT64_MAX-66048u-uint64_t(group)*32u-local;}
[[nodiscard]] bool isTrailPartId(uint64_t) noexcept;
// Ground immutable installed site/reward records and append only IDs19..21.
// Existing IDs1..18, positions, yields and order are retained byte-for-byte.
// Failure preserves both outputs. Root enables trail progress after success.
[[nodiscard]] bool defaultTrailContent(const AdventureSpatialQueries& terrainBase,
    std::array<DiscoveryContent,2>& discoveries,std::vector<ResourceNode>& resources,std::string& error);
// A real owned sheltered bed, chest and bench in one structure; the bed must
// be within25m of the beacon and farther than40m from town. Registration is
// not required: an established town home may remain the player's recovery.
[[nodiscard]] FirstHomeReadiness fieldHomeReadiness(const AdventureState&,const AdventureSpatialQueries&);
// Paid candidate only. Never auto-publish or grant its material cost.
[[nodiscard]] std::array<PlacePart,3> signalTerraceStep() noexcept;

// Immutable-per-load installed scenery. Restore passes previous=nullptr. A
// conflict defers the complete group (render+solids); existing homes, recovery
// and player remain exact. This installs no player-owned furniture or inventory.
class TrailSites {
public:
    static constexpr size_t maximumGroups=4,maximumPieces=24,maximumSolids=32;
    [[nodiscard]] static TrailSites admit(const AdventureState&,const AdventureContent&,
        const AdventureSpatialQueries& base,const TrailSites* previous=nullptr);
    [[nodiscard]] const std::array<TrailGroup,maximumGroups>& groups() const noexcept {return groups_;}
    [[nodiscard]] const std::array<TrailSite,4>& sites() const noexcept {return sites_;}
    [[nodiscard]] const TrailSite* site(TrailSiteId) const noexcept;
    [[nodiscard]] bool appendSolids(construction::WorldNamespace,std::vector<AdventureSpatialQueries::Solid>&) const;
    [[nodiscard]] bool validateNewConstruction(const AdventureState& before,const AdventureState& after,std::string&) const;
    [[nodiscard]] bool discoveryReachable(uint8_t,PlayerPose,const AdventureSpatialQueries& published,std::string&) const;
    [[nodiscard]] bool relayReachable(PlayerPose,const AdventureSpatialQueries& published,std::string&) const;
private:
    construction::WorldNamespace world_{};
    std::array<TrailGroup,maximumGroups> groups_{};
    std::array<TrailSite,4> sites_{};
};
} // namespace voxy::game::adventure
