#pragma once

#include "game/adventure/adventure_player.hpp"
#include <array>
#include <span>
#include <vector>

namespace voxy::game::adventure {

// A bounded local walking graph, derived from the same accepted terrain and
// building packet as AdventurePlayer. It has separate nodes for bridge decks,
// ground beneath them and upper floors. No saved state or second actor solver.
class LayeredNavigation {
public:
    LayeredNavigation()=default;
    LayeredNavigation(const LayeredNavigation&)=delete;
    LayeredNavigation& operator=(const LayeredNavigation&)=delete;
    LayeredNavigation(LayeredNavigation&&)=delete;
    LayeredNavigation& operator=(LayeredNavigation&&)=delete;
    static constexpr double cellSize=.5,tileSize=4;
    static constexpr size_t cellsPerTile=64,maximumLayers=8,maximumTiles=64;
    static constexpr size_t maximumNodes=maximumTiles*cellsPerTile*maximumLayers;
    static constexpr size_t maximumExpansions=4096,maximumWaypoints=512;
    struct Region {
        int32_t firstTileX=0,firstTileZ=0;
        uint8_t tilesX=1,tilesZ=1;
        double minimumFeet=-512,maximumFeet=512,waterHeight=-200;
    };
    enum class SearchStatus { Invalid,Building,Ready,Searching,Found,NoPath,Capacity,Stale };
    struct TileStamp {uint16_t tile=0;uint64_t generation=0;};
    struct Path {
        std::vector<glm::dvec3> points;
        std::vector<TileStamp> tiles;
        const LayeredNavigation* owner=nullptr;
        uint64_t configuration=0;
    };
    struct Work {
        size_t columns=0,expansions=0,endpointChecks=0,controllerTicks=0;
        size_t dirtyTiles=0,totalExpansions=0;
        SearchStatus status=SearchStatus::Invalid;
    };
    // Configuration allocates bounded graph/search storage. Invalid requests
    // preserve the previous configuration. Region coordinates are 4 m tiles.
    [[nodiscard]] bool configure(Region);
    // Call after accepted static publication. Exact sorted solid differences
    // invalidate only overlapping tiles (including capsule/edge clearance).
    // Unchanged tiles retain their generation and already sampled layers.
    [[nodiscard]] bool synchronize(const AdventureSpatialQueries&);
    // Incremental work: at most 64 columns and 32 endpoint checks / A*
    // expansions combined per call. Request itself performs no solver probes.
    // Each edge is proved with at most 24 ticks of the actual walking solver.
    [[nodiscard]] Work advance(const AdventureSpatialQueries&,size_t columnBudget=16,size_t expansionBudget=4);
    [[nodiscard]] SearchStatus request(const AdventureSpatialQueries&,glm::dvec3 from,glm::dvec3 to);
    [[nodiscard]] SearchStatus status() const noexcept {return status_;}
    [[nodiscard]] const Path& path() const noexcept {return path_;}
    [[nodiscard]] bool valid(const AdventureSpatialQueries&,const Path&) const noexcept;
    [[nodiscard]] uint64_t tileGeneration(size_t index) const noexcept;
    [[nodiscard]] size_t dirtyTileCount() const noexcept;
    [[nodiscard]] size_t layerCount(glm::dvec2 point) const noexcept;
private:
    friend class NavigationFollower;
    struct Column {std::array<double,maximumLayers> feet{};uint8_t count=0;bool complete=false;};
    struct Tile {uint64_t generation=1;uint16_t nextColumn=0;};
    struct SearchNode {double cost=0,priority=0;uint32_t parent=UINT32_MAX,heap=UINT32_MAX;bool reached=false,closed=false;};
    [[nodiscard]] size_t columnAt(glm::dvec2) const noexcept;
    [[nodiscard]] size_t tileForColumn(size_t) const noexcept;
    [[nodiscard]] glm::dvec3 nodePoint(uint32_t) const noexcept;
    [[nodiscard]] bool walkEdge(const AdventureSpatialQueries&,glm::dvec3,glm::dvec3,size_t&) const;
    void attach(const AdventureSpatialQueries&,Work&,size_t);
    void dirty(glm::dvec3,glm::dvec3);
    void push(uint32_t);
    [[nodiscard]] uint32_t pop();
    void finish();
    Region region_{};
    std::vector<Tile> tiles_;
    std::vector<Column> columns_;
    std::vector<SearchNode> search_;
    std::vector<uint32_t> heap_;
    std::vector<AdventureSpatialQueries::Solid> solids_;
    terrain::lego::Surface terrain_{};
    const AdventureSpatialQueries* source_=nullptr;
    uint64_t configuration_=0,revision_=0;
    size_t columnsX_=0,columnsZ_=0,totalExpansions_=0;
    uint32_t start_=UINT32_MAX,goal_=UINT32_MAX;
    uint32_t attachmentBest_=UINT32_MAX;
    size_t attachmentIndex_=0,attachmentEnd_=0;
    double attachmentDistance_=0;
    bool attaching_=false;
    glm::dvec3 from_{},to_{};
    SearchStatus status_=SearchStatus::Invalid;
    Path path_;
};

// Consumes one authoritative AdventurePlayer fixed step per call. A changed
// path tile stops movement before using stale occupancy; the caller can repath.
class NavigationFollower {
public:
    enum class FollowStatus { Idle,Walking,Arrived,Stale,Blocked };
    [[nodiscard]] bool begin(const AdventureSpatialQueries&,const LayeredNavigation&,
        const LayeredNavigation::Path&,const AdventurePlayer&);
    [[nodiscard]] FollowStatus advance(const AdventureSpatialQueries&,const LayeredNavigation&,AdventurePlayer&);
    [[nodiscard]] FollowStatus status() const noexcept {return status_;}
private:
    LayeredNavigation::Path path_;
    size_t waypoint_=1,stalled_=0;
    FollowStatus status_=FollowStatus::Idle;
};
} // namespace voxy::game::adventure
