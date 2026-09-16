#include "game/adventure/layered_navigation.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <tuple>

namespace voxy::game::adventure {
namespace {
constexpr size_t missing=std::numeric_limits<size_t>::max();
constexpr uint32_t noNode=UINT32_MAX;
bool finite(glm::dvec3 p) {return std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.z);}
auto solidKey(const AdventureSpatialQueries::Solid& s) {
    return std::tie(s.structure,s.part,s.minimum.x,s.minimum.y,s.minimum.z,s.maximum.x,s.maximum.y,s.maximum.z);
}
bool sameTerrain(const terrain::lego::Surface& a,const terrain::lego::Surface& b) {
    return a.samples.data()==b.samples.data()&&a.samples.size()==b.samples.size()
        &&a.width==b.width&&a.height==b.height&&a.heightScale==b.heightScale&&a.cellScale==b.cellScale;
}
}

bool LayeredNavigation::configure(Region region) {
    if(!region.tilesX||!region.tilesZ||region.tilesX>8||region.tilesZ>8
        ||size_t(region.tilesX)*region.tilesZ>maximumTiles
        ||region.firstTileX< -24998||region.firstTileZ< -24998
        ||int64_t(region.firstTileX)+region.tilesX>24998||int64_t(region.firstTileZ)+region.tilesZ>24998
        ||!std::isfinite(region.minimumFeet)||!std::isfinite(region.maximumFeet)||!std::isfinite(region.waterHeight)
        ||region.minimumFeet< -99990||region.maximumFeet>99990||region.minimumFeet>=region.maximumFeet
        ||configuration_==UINT64_MAX)return false;
    const size_t count=size_t(region.tilesX)*region.tilesZ;
    std::vector<Tile> tiles(count);
    std::vector<Column> columns(count*cellsPerTile);
    std::vector<SearchNode> search(columns.size()*maximumLayers);
    std::vector<uint32_t> heap;heap.reserve(search.size());
    tiles_=std::move(tiles);columns_=std::move(columns);search_=std::move(search);heap_=std::move(heap);
    region_=region;columnsX_=size_t(region.tilesX)*8;columnsZ_=size_t(region.tilesZ)*8;
    solids_.clear();terrain_={};source_=nullptr;revision_=0;++configuration_;
    path_={};start_=goal_=noNode;attaching_=false;totalExpansions_=0;status_=SearchStatus::Building;return true;
}
size_t LayeredNavigation::columnAt(glm::dvec2 p) const noexcept {
    if(!std::isfinite(p.x)||!std::isfinite(p.y)||columns_.empty())return missing;
    const double x=(p.x-double(region_.firstTileX)*tileSize)/cellSize;
    const double z=(p.y-double(region_.firstTileZ)*tileSize)/cellSize;
    if(x<0||z<0||x>=double(columnsX_)||z>=double(columnsZ_))return missing;
    return size_t(z)*columnsX_+size_t(x);
}
size_t LayeredNavigation::tileForColumn(size_t column) const noexcept {
    return (column/columnsX_/8)*region_.tilesX+(column%columnsX_)/8;
}
glm::dvec3 LayeredNavigation::nodePoint(uint32_t node) const noexcept {
    const size_t column=node/maximumLayers;
    return {double(region_.firstTileX)*tileSize+(double(column%columnsX_)+.5)*cellSize,
        columns_[column].feet[node%maximumLayers],
        double(region_.firstTileZ)*tileSize+(double(column/columnsX_)+.5)*cellSize};
}
size_t LayeredNavigation::dirtyTileCount() const noexcept {
    return size_t(std::count_if(tiles_.begin(),tiles_.end(),[](const Tile& t){return t.nextColumn<cellsPerTile;}));
}
uint64_t LayeredNavigation::tileGeneration(size_t index) const noexcept {
    return index<tiles_.size()?tiles_[index].generation:0;
}
size_t LayeredNavigation::layerCount(glm::dvec2 point) const noexcept {
    const auto c=columnAt(point);
    return c!=missing&&tiles_[tileForColumn(c)].nextColumn==cellsPerTile&&columns_[c].complete?columns_[c].count:0;
}
void LayeredNavigation::dirty(glm::dvec3 minimum,glm::dvec3 maximum) {
    // Include the capsule and the short segment joining neighboring samples.
    minimum-=glm::dvec3(AdventurePlayer::radius+cellSize);
    maximum+=glm::dvec3(AdventurePlayer::radius+cellSize);
    for(size_t i=0;i<tiles_.size();++i) {
        const double x=(double(region_.firstTileX)+double(i%region_.tilesX))*tileSize;
        const double z=(double(region_.firstTileZ)+double(i/region_.tilesX))*tileSize;
        if(maximum.x<x||minimum.x>x+tileSize||maximum.z<z||minimum.z>z+tileSize)continue;
        auto& tile=tiles_[i];
        if(tile.nextColumn==cellsPerTile)++tile.generation;
        tile.nextColumn=0;
    }
}
bool LayeredNavigation::synchronize(const AdventureSpatialQueries& queries) {
    if(tiles_.empty()||!queries.revision()||!queries.terrain().valid())return false;
    if(source_==&queries&&revision_==queries.revision()&&sameTerrain(terrain_,queries.terrain()))return true;
    const auto& terrain=queries.terrain();
    const glm::dvec2 origin(terrain.origin());
    const glm::dvec2 low(double(region_.firstTileX)*tileSize,double(region_.firstTileZ)*tileSize);
    const glm::dvec2 high=low+glm::dvec2(region_.tilesX,region_.tilesZ)*tileSize;
    if(glm::any(glm::lessThan(low-glm::dvec2(AdventurePlayer::radius),-origin))
        ||glm::any(glm::greaterThanEqual(high+glm::dvec2(AdventurePlayer::radius),origin)))return false;
    for(const auto& tile:tiles_)if(tile.generation==UINT64_MAX)return false;
    const bool replaced=source_!=&queries||!sameTerrain(terrain_,terrain);
    if(replaced&&configuration_==UINT64_MAX)return false;
    std::vector<AdventureSpatialQueries::Solid> next(queries.solids().begin(),queries.solids().end());
    std::sort(next.begin(),next.end(),[](const auto& a,const auto& b){return solidKey(a)<solidKey(b);});
    if(replaced) {
        ++configuration_;
        for(auto& tile:tiles_){++tile.generation;tile.nextColumn=0;}
    } else {
        size_t a=0,b=0;
        while(a<solids_.size()||b<next.size()) {
            if(a<solids_.size()&&b<next.size()&&solidKey(solids_[a])==solidKey(next[b])){++a;++b;continue;}
            if(b==next.size()||(a<solids_.size()&&solidKey(solids_[a])<solidKey(next[b]))) {
                dirty(solids_[a].minimum,solids_[a].maximum);++a;
            } else {dirty(next[b].minimum,next[b].maximum);++b;}
        }
    }
    solids_=std::move(next);source_=&queries;terrain_=terrain;revision_=queries.revision();
    if(dirtyTileCount()) {heap_.clear();path_={};attaching_=false;status_=SearchStatus::Building;}
    return true;
}
bool LayeredNavigation::walkEdge(const AdventureSpatialQueries& queries,glm::dvec3 from,glm::dvec3 to,size_t& ticks) const {
    if(!finite(from)||!finite(to)||glm::length(glm::dvec2(to.x-from.x,to.z-from.z))>1.5
        ||std::abs(to.y-from.y)>1.1||to.y<region_.waterHeight-.65)return false;
    AdventurePlayer probe;if(!probe.initialize(queries,from,region_.waterHeight))return false;
    for(int step=0;step<24;++step) {
        const auto p=probe.feet();const glm::dvec2 delta(to.x-p.x,to.z-p.z);const double distance=glm::length(delta);
        if(distance<.008&&std::abs(p.y-to.y)<.025&&probe.mode()==AdventurePlayer::Mode::Walking)return true;
        if(distance<.001)return false;
        const double amount=std::min(1.,distance/(3.6*AdventurePlayer::fixedStep));
        probe.advance(AdventurePlayer::fixedStep,{delta*(amount/distance),false});++ticks;
        // Walking graph edges never intentionally jump, swim or fall. The
        // caller can add those explicit traversal actions in a later task.
        if(probe.mode()!=AdventurePlayer::Mode::Walking||glm::length(probe.feet()-p)<1e-7)return false;
    }
    return false;
}
void LayeredNavigation::attach(const AdventureSpatialQueries& queries,Work& work,size_t budget) {
    constexpr std::array<glm::ivec2,9> offsets{{{0,0},{1,0},{-1,0},{0,1},{0,-1},{1,1},{1,-1},{-1,1},{-1,-1}}};
    while(attaching_&&work.endpointChecks<budget) {
        const auto point=attachmentEnd_?to_:from_;
        const auto column=columnAt({point.x,point.z});
        const int cx=int(column%columnsX_),cz=int(column/columnsX_);
        while(attachmentIndex_<offsets.size()*maximumLayers&&work.endpointChecks<budget) {
            const auto index=attachmentIndex_++;
            const auto offset=offsets[index/maximumLayers];const auto layer=index%maximumLayers;
            const int x=cx+offset.x,z=cz+offset.y;
            if(x<0||z<0||x>=int(columnsX_)||z>=int(columnsZ_))continue;
            const size_t c=size_t(z)*columnsX_+size_t(x);const auto& samples=columns_[c];
            if(!samples.complete||layer>=samples.count)continue;
            const uint32_t n=uint32_t(c*maximumLayers+layer);const auto p=nodePoint(n);
            const double distance=glm::length(p-point);if(distance>=attachmentDistance_)continue;
            ++work.endpointChecks;
            if(attachmentEnd_?walkEdge(queries,p,point,work.controllerTicks):walkEdge(queries,point,p,work.controllerTicks)) {
                attachmentBest_=n;attachmentDistance_=distance;
            }
        }
        if(attachmentIndex_!=offsets.size()*maximumLayers)return;
        if(attachmentBest_==noNode){attaching_=false;status_=SearchStatus::NoPath;return;}
        (attachmentEnd_?goal_:start_)=attachmentBest_;
        if(++attachmentEnd_==2) {
            attaching_=false;std::fill(search_.begin(),search_.end(),SearchNode{});
            auto& start=search_[start_];start.reached=true;start.cost=glm::length(from_-nodePoint(start_));
            start.priority=start.cost+glm::length(nodePoint(start_)-nodePoint(goal_));push(start_);return;
        }
        attachmentIndex_=0;attachmentBest_=noNode;attachmentDistance_=std::numeric_limits<double>::infinity();
    }
}
void LayeredNavigation::push(uint32_t node) {
    auto& entry=search_[node];
    if(entry.heap==noNode){entry.heap=uint32_t(heap_.size());heap_.push_back(node);}
    size_t at=entry.heap;
    while(at) {
        const size_t parent=(at-1)/2;
        const auto a=heap_[at],b=heap_[parent];
        if(std::tie(search_[b].priority,b)<=std::tie(search_[a].priority,a))break;
        std::swap(heap_[at],heap_[parent]);search_[a].heap=uint32_t(parent);search_[b].heap=uint32_t(at);at=parent;
    }
}
uint32_t LayeredNavigation::pop() {
    const auto result=heap_.front(),last=heap_.back();heap_.pop_back();search_[result].heap=noNode;
    if(heap_.empty())return result;
    heap_.front()=last;search_[last].heap=0;size_t at=0;
    while(at*2+1<heap_.size()) {
        size_t best=at*2+1;
        if(best+1<heap_.size()) {
            const auto a=heap_[best],b=heap_[best+1];
            if(std::tie(search_[b].priority,b)<std::tie(search_[a].priority,a))++best;
        }
        const auto a=heap_[at],b=heap_[best];
        if(std::tie(search_[a].priority,a)<=std::tie(search_[b].priority,b))break;
        std::swap(heap_[at],heap_[best]);search_[a].heap=uint32_t(best);search_[b].heap=uint32_t(at);at=best;
    }
    return result;
}
LayeredNavigation::SearchStatus LayeredNavigation::request(const AdventureSpatialQueries& queries,glm::dvec3 from,glm::dvec3 to) {
    path_={};heap_.clear();totalExpansions_=0;attaching_=false;
    if(source_!=&queries||revision_!=queries.revision()||!sameTerrain(terrain_,queries.terrain()))return status_=SearchStatus::Stale;
    if(dirtyTileCount())return status_=SearchStatus::Building;
    if(!finite(from)||!finite(to)||columnAt({from.x,from.z})==missing||columnAt({to.x,to.z})==missing
        ||from.y<region_.minimumFeet||from.y>region_.maximumFeet||to.y<region_.minimumFeet||to.y>region_.maximumFeet)
        return status_=SearchStatus::Invalid;
    if(!columns_[columnAt({from.x,from.z})].complete||!columns_[columnAt({to.x,to.z})].complete)return status_=SearchStatus::Capacity;
    from_=from;to_=to;start_=goal_=noNode;attaching_=true;
    attachmentIndex_=attachmentEnd_=0;attachmentBest_=noNode;attachmentDistance_=std::numeric_limits<double>::infinity();
    return status_=SearchStatus::Searching;
}
void LayeredNavigation::finish() {
    Path result;result.owner=this;result.configuration=configuration_;
    for(uint32_t node=goal_;node!=noNode;node=search_[node].parent) {
        if(result.points.size()+2>=maximumWaypoints){status_=SearchStatus::Capacity;return;}
        result.points.push_back(nodePoint(node));
    }
    std::reverse(result.points.begin(),result.points.end());
    result.points.insert(result.points.begin(),from_);result.points.push_back(to_);
    std::array<bool,maximumTiles> used{};
    for(size_t p=1;p<result.points.size();++p) {
        const auto low=glm::min(result.points[p-1],result.points[p])-glm::dvec3(AdventurePlayer::radius);
        const auto high=glm::max(result.points[p-1],result.points[p])+glm::dvec3(AdventurePlayer::radius);
        for(size_t i=0;i<tiles_.size();++i) {
            const double x=(double(region_.firstTileX)+double(i%region_.tilesX))*tileSize;
            const double z=(double(region_.firstTileZ)+double(i/region_.tilesX))*tileSize;
            if(high.x>=x&&low.x<=x+tileSize&&high.z>=z&&low.z<=z+tileSize)used[i]=true;
        }
    }
    for(size_t i=0;i<tiles_.size();++i)if(used[i])result.tiles.push_back({uint16_t(i),tiles_[i].generation});
    path_=std::move(result);status_=SearchStatus::Found;
}
LayeredNavigation::Work LayeredNavigation::advance(const AdventureSpatialQueries& queries,size_t columnBudget,size_t expansionBudget) {
    Work work;
    if(source_!=&queries||revision_!=queries.revision()||!sameTerrain(terrain_,queries.terrain())) {status_=SearchStatus::Stale;work.status=status_;return work;}
    columnBudget=std::min(columnBudget,size_t(64));expansionBudget=std::min(expansionBudget,size_t(32));
    for(size_t tile=0;tile<tiles_.size()&&work.columns<columnBudget;++tile) {
        auto& t=tiles_[tile];
        while(t.nextColumn<cellsPerTile&&work.columns<columnBudget) {
            const size_t x=(tile%region_.tilesX)*8+t.nextColumn%8;
            const size_t z=(tile/region_.tilesX)*8+t.nextColumn/8;
            const size_t index=z*columnsX_+x;auto& column=columns_[index];column={};
            const glm::dvec2 point(double(region_.firstTileX)*tileSize+(double(x)+.5)*cellSize,
                double(region_.firstTileZ)*tileSize+(double(z)+.5)*cellSize);
            const auto found=queries.walkableFeet(point,region_.minimumFeet,region_.maximumFeet,column.feet);
            column.complete=found.complete;
            if(found.complete)for(size_t layer=0;layer<found.count;++layer)
                if(column.feet[layer]>=region_.waterHeight-.65)column.feet[column.count++]=column.feet[layer];
            ++t.nextColumn;++work.columns;
        }
    }
    work.dirtyTiles=dirtyTileCount();
    if(work.dirtyTiles){work.status=SearchStatus::Building;return work;}
    if(status_==SearchStatus::Building)status_=SearchStatus::Ready;
    if(status_==SearchStatus::Searching&&attaching_)attach(queries,work,expansionBudget);
    constexpr std::array<glm::ivec2,8> offsets{{{1,0},{-1,0},{0,1},{0,-1},{1,1},{1,-1},{-1,1},{-1,-1}}};
    while(status_==SearchStatus::Searching&&!attaching_&&work.expansions+work.endpointChecks<expansionBudget) {
        if(heap_.empty()) {
            status_=std::any_of(columns_.begin(),columns_.end(),[](const Column& c){return !c.complete;})?SearchStatus::Capacity:SearchStatus::NoPath;break;
        }
        if(totalExpansions_>=maximumExpansions){status_=SearchStatus::Capacity;break;}
        const auto current=pop();search_[current].closed=true;++work.expansions;++totalExpansions_;
        if(current==goal_){finish();break;}
        const auto from=nodePoint(current);const size_t c=current/maximumLayers;
        for(const auto delta:offsets) {
            const int x=int(c%columnsX_)+delta.x,z=int(c/columnsX_)+delta.y;
            if(x<0||z<0||x>=int(columnsX_)||z>=int(columnsZ_))continue;
            const size_t nextColumn=size_t(z)*columnsX_+size_t(x);const auto& column=columns_[nextColumn];
            if(!column.complete)continue;
            for(uint8_t layer=0;layer<column.count;++layer) {
                const auto next=uint32_t(nextColumn*maximumLayers+layer);auto& record=search_[next];
                if(record.closed)continue;
                const auto to=nodePoint(next);const double cost=search_[current].cost+glm::length(to-from);
                if((record.reached&&record.cost<=cost)||!walkEdge(queries,from,to,work.controllerTicks))continue;
                record.reached=true;record.cost=cost;record.priority=cost+glm::length(to-nodePoint(goal_));record.parent=current;push(next);
            }
        }
    }
    work.totalExpansions=totalExpansions_;work.status=status_;return work;
}
bool LayeredNavigation::valid(const AdventureSpatialQueries& queries,const Path& path) const noexcept {
    if(source_!=&queries||revision_!=queries.revision()||!sameTerrain(terrain_,queries.terrain())||path.owner!=this||path.configuration!=configuration_
        ||path.points.size()<2||path.points.size()>maximumWaypoints||path.tiles.empty()||path.tiles.size()>maximumTiles)return false;
    for(const auto stamp:path.tiles)if(stamp.tile>=tiles_.size()||tiles_[stamp.tile].generation!=stamp.generation
        ||tiles_[stamp.tile].nextColumn!=cellsPerTile)return false;
    return true;
}
bool NavigationFollower::begin(const AdventureSpatialQueries& queries,const LayeredNavigation& nav,
    const LayeredNavigation::Path& path,const AdventurePlayer& actor) {
    if(!nav.valid(queries,path)||!actor.usesWorld(queries,nav.region_.waterHeight)
        ||actor.mode()!=AdventurePlayer::Mode::Walking||glm::length(actor.feet()-path.points.front())>.05)return false;
    path_=path;waypoint_=1;stalled_=0;status_=FollowStatus::Walking;return true;
}
NavigationFollower::FollowStatus NavigationFollower::advance(const AdventureSpatialQueries& queries,const LayeredNavigation& nav,AdventurePlayer& actor) {
    if(status_!=FollowStatus::Walking)return status_;
    if(!nav.valid(queries,path_)||!actor.usesWorld(queries,nav.region_.waterHeight))return status_=FollowStatus::Stale;
    auto before=actor.feet();
    while(waypoint_<path_.points.size()&&glm::length(glm::dvec2(path_.points[waypoint_].x-before.x,path_.points[waypoint_].z-before.z))<.008
        &&std::abs(path_.points[waypoint_].y-before.y)<.025)++waypoint_;
    if(waypoint_==path_.points.size()) {actor.advance(AdventurePlayer::fixedStep,{});return status_=FollowStatus::Arrived;}
    const auto target=path_.points[waypoint_];const glm::dvec2 delta(target.x-before.x,target.z-before.z);const double distance=glm::length(delta);
    if(distance<1e-8)return status_=FollowStatus::Blocked;
    const double amount=std::min(1.,distance/(3.6*AdventurePlayer::fixedStep));
    actor.advance(AdventurePlayer::fixedStep,{delta*(amount/distance),false});
    stalled_=glm::length(actor.feet()-before)<1e-7?stalled_+1:0;
    if(stalled_>=8||actor.mode()!=AdventurePlayer::Mode::Walking)return status_=FollowStatus::Blocked;
    return status_;
}
} // namespace voxy::game::adventure
