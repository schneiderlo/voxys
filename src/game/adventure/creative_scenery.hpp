#pragma once
#include "game/adventure/adventure_session.hpp"
#include "game/adventure/spatial_queries.hpp"
#include <set>
#include <memory>
#include "game/adventure/creative_village.hpp"

namespace voxy::game::adventure {
inline constexpr uint64_t creativeSceneryStructureId=UINT64_MAX-20000u;
inline constexpr uint64_t creativeSceneryPartBase=UINT64_MAX-21000u;
inline bool isCreativeScenerySolid(const AdventureSpatialQueries::Solid& s) noexcept {
    return s.structure.counter==creativeSceneryStructureId
        &&s.part.counter<creativeSceneryPartBase&&s.part.counter>=creativeSceneryPartBase-2097152;
}
enum class CreativePropKind : uint8_t { Broadleaf, Pine, Flowers, Rocks, Bench, Crate };
struct CreativeProp {
    uint32_t id=0;
    CreativePropKind kind{};
    glm::dvec3 feet{};
    uint8_t yawQuarterTurns=0;
    glm::dvec3 minimum{},maximum{}; // Complete visible bounds, for construction admission.
    uint8_t forestVariant=255; // Six authored silhouettes; 255 retains the village prop kit.
};
// Installed scenery, never saved as player-owned bricks. New construction takes
// precedence over a whole prop. Hidden props stay hidden until the next load,
// so deleting a brick cannot respawn a trunk through a rider. Initial admission
// also reserves the saved player. The admitted list drives BOTH draws/collision.
class CreativeScenery {
public:
    static constexpr size_t maximumProps=2048;
    static constexpr double forestDrawDistance=2000;
    // At 8-stud spacing this contains fewer than 1,500 trunks, even in the
    // densest grove. Leaves ample travel margin around the 32-stud refresh cell.
    static constexpr double forestCollisionRadius=160;
    static constexpr uint32_t forestSeed=0x937a25u;
    static constexpr uint32_t forestRecipeVersion=3;
    static CreativeScenery admit(const AdventureState&,const terrain::lego::Surface&,
        std::span<const AdventureSpatialQueries::Solid> constructionAndDoorSwings,
        const CreativeScenery* previous=nullptr,
        std::span<const AdventureSpatialQueries::Solid> newPropClearance={});
    bool needsRefresh(glm::dvec2 focus) const noexcept;
    bool appendSolids(construction::WorldNamespace,std::vector<AdventureSpatialQueries::Solid>&) const;
    const CreativeVillage& village() const noexcept {return village_;}
    const std::vector<CreativeProp>& props() const noexcept {return props_;}
    // Trees outside the collision neighbourhood use the same IDs and feet as
    // their nearby counterparts. They never consume the physics solid budget.
    const std::vector<CreativeProp>& distantTrees() const noexcept {return distantTrees_;}
    // Immutable source identity lets rendering retain prepared geometry when
    // only the collision neighbourhood moves inside the same forest region.
    const std::shared_ptr<const std::vector<CreativeProp>>& forestSource() const noexcept {return forestSource_;}
private:
    CreativeVillage village_;
    bool initialized_=false;
    glm::ivec2 cell_{};
    std::set<uint32_t> suppressed_;
    std::vector<CreativeProp> props_;
    glm::ivec2 forestCell_{};
    terrain::lego::Surface forestTerrain_{};
    std::shared_ptr<const std::vector<CreativeProp>> forestSource_;
    std::map<std::pair<int,int>,std::shared_ptr<const std::vector<CreativeProp>>> forestSourceTiles_;
    std::vector<CreativeProp> distantTrees_;
};
}
