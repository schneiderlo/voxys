#include "game/adventure/creative_scenery.hpp"
#include "game/adventure/world_definition.hpp"
#include "game/adventure/adventure_player.hpp"
#include "game/adventure/forest_geometry.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <unordered_map>
#include <unordered_set>

namespace voxy::game::adventure {
namespace {
using Solid=AdventureSpatialQueries::Solid;
using Kind=CreativePropKind;
// Corresponds to authored mesh bounds (including exposed studs). No scaling.
constexpr std::array<glm::dvec3,6> low{{{-3.87,0,-3.37},{-1.97,0,-1.97},{-1.02,0,-.78},{-1.50,0,-.90},{-2.25,0,-.72},{-1,0,-1.07}}};
constexpr std::array<glm::dvec3,6> high{{{3.52,10.10,2.77},{1.97,7.56,1.97},{.97,1.23,.83},{1.5,1.46,.9},{2.25,3.24,.75},{1,1.78,1.07}}};
glm::dvec3 rotate(glm::dvec3 p,uint8_t yaw) {
    switch(yaw&3){case 1:return {p.z,p.y,-p.x};case 2:return {-p.x,p.y,-p.z};case 3:return {-p.z,p.y,p.x};default:return p;}
}
bool overlap(glm::dvec3 lo,glm::dvec3 hi,const Solid& b,double margin=0) {
    return glm::all(glm::lessThan(lo,b.maximum+glm::dvec3(margin)))&&glm::all(glm::greaterThan(hi,b.minimum-glm::dvec3(margin)));
}
std::pair<double,double> ground(const terrain::lego::Surface& s,glm::dvec2 center,glm::dvec2 extent) {
    if(!s.valid())return {INFINITY,INFINITY};
    const auto origin=glm::dvec2(s.origin());const double cell=s.cellScale;
    const auto a=center-extent,b=center+extent;
    const int x0=int(std::floor((a.x+origin.x)/cell)),z0=int(std::floor((a.y+origin.y)/cell));
    const int x1=int(std::floor((b.x+origin.x-1e-7)/cell)),z1=int(std::floor((b.y+origin.y-1e-7)/cell));
    if(x0<0||z0<0||x1>=int(s.width)-1||z1>=int(s.height)-1)return {INFINITY,INFINITY};
    double lo=INFINITY,hi=-INFINITY;
    for(int z=z0;z<=z1;++z)for(int x=x0;x<=x1;++x) {
        const double y=s.cellTop(x,z);lo=std::min(lo,y);hi=std::max(hi,y);
    }
    return {lo,hi};
}
struct Site {Kind kind;double right,forward;uint8_t yaw=0;};
// Asymmetric islands: open bay cone, a sheltered side bench, and a distant grove.
constexpr std::array sites{
    Site{Kind::Broadleaf,-18,18},Site{Kind::Broadleaf,-28,28,1},Site{Kind::Pine,-31,16},
    Site{Kind::Broadleaf,20,-8,2},Site{Kind::Pine,29,-16},Site{Kind::Pine,21,-24,3},
    Site{Kind::Broadleaf,-45,34,2},Site{Kind::Pine,-40,20},Site{Kind::Broadleaf,-6,-30},Site{Kind::Pine,8,-34},
    Site{Kind::Bench,-21,4,1},Site{Kind::Crate,-25,1},
    Site{Kind::Rocks,-13,11},Site{Kind::Rocks,15,-13,1},Site{Kind::Rocks,-32,30,3},
    Site{Kind::Rocks,25,-27},Site{Kind::Rocks,-43,24},Site{Kind::Rocks,-12,-27},
    Site{Kind::Flowers,-10,9},Site{Kind::Flowers,-12,8,1},Site{Kind::Flowers,-14,13,2},
    Site{Kind::Flowers,-17,14},Site{Kind::Flowers,-21,19,3},Site{Kind::Flowers,-25,24},
    Site{Kind::Flowers,11,11},Site{Kind::Flowers,12,13,2},Site{Kind::Flowers,16,-8},
    Site{Kind::Flowers,20,-15,1},Site{Kind::Flowers,25,-23},Site{Kind::Flowers,27,-28,2},
    Site{Kind::Flowers,-33,24},Site{Kind::Flowers,-35,26,3},Site{Kind::Flowers,-4,-24},
    Site{Kind::Flowers,2,-27,1},Site{Kind::Flowers,-44,29},Site{Kind::Flowers,-17,3}
};
constexpr std::array villageTrees{
        glm::dvec2(1026,-1080),glm::dvec2(1023,-1068),glm::dvec2(1031,-1056),
        glm::dvec2(1060,-1019),glm::dvec2(1055,-1009),glm::dvec2(1067,-1005),
        glm::dvec2(1082,-1009),glm::dvec2(1090,-1001),
        glm::dvec2(1135,-1006),glm::dvec2(1144,-999),glm::dvec2(1151,-1008),
        glm::dvec2(1194,-1055),glm::dvec2(1202,-1047),glm::dvec2(1198,-1035)};
// Collision boxes follow physical trunks, leaf courses, bench legs/seat/back,
// and rock inner volumes. Flowers are decorative and never snag a bike.
std::vector<std::pair<glm::dvec3,glm::dvec3>> boxes(Kind kind) {
    switch(kind) {
    case Kind::Broadleaf:return {{{-.48,0,-.48},{.48,5.76,.48}}};
    case Kind::Pine:return {{{-.37,0,-.37},{.37,6.68,.37}},
        {{-1.55,2.9,-1.55},{1.55,3.65,1.55}},{{-1.1,3.65,-1.1},{1.1,4.66,1.1}},
        {{-1.25,4.18,-1.25},{1.25,4.95,1.25}},{{-.85,4.95,-.85},{.85,5.96,.85}},
        {{-.85,5.46,-.85},{.85,6.2,.85}},{{-.5,6.2,-.5},{.5,7.38,.5}}};
    case Kind::Rocks:return {{{-1.2,0,-.6},{0,1.28,.6}},{{.5,0,-.15},{1.3,.64,.65}}};
    case Kind::Bench:return {{{-1.77,0,-.6},{-1.33,1.5,.6}},{{1.33,0,-.6},{1.77,1.5,.6}},{{-2.25,1.5,-.72},{2.25,1.78,.72}},{{-2.25,1.78,.46},{2.25,3.24,.75}}};
    case Kind::Crate:return {{low[5],high[5]}};
    default:return {};
    }
}
std::vector<std::pair<glm::dvec3,glm::dvec3>> boxes(const CreativeProp& p) {
    if(p.forestVariant<forestGeometry.size()) {
        const auto& g=forestGeometry[p.forestVariant];return {{g.trunkMinimum,g.trunkMaximum}};
    }
    return boxes(p.kind);
}
std::pair<glm::dvec3,glm::dvec3> bounds(glm::dvec3 lo,glm::dvec3 hi,const CreativeProp& p) {
    glm::dvec3 a(INFINITY),b(-INFINITY);
    for(double x:{lo.x,hi.x})for(double y:{lo.y,hi.y})for(double z:{lo.z,hi.z}) {const auto q=rotate({x,y,z},p.yawQuarterTurns)+p.feet;a=glm::min(a,q);b=glm::max(b,q);}
    return {a,b};
}
// Explicit integer hashing/interpolation: independent of call order, threads,
// floating point noise libraries, and which side of a region loads first.
uint32_t forestHash(int x,int z,uint32_t salt=0) {
    uint32_t h=uint32_t(x)*374761393u^uint32_t(z)*668265263u^CreativeScenery::forestSeed^salt;
    h=(h^(h>>13))*1274126177u;return h^(h>>16);
}
int floorDiv(int n,int d) {return n>=0?n/d:-int((uint32_t(-n)+uint32_t(d)-1)/uint32_t(d));}
int noise(int x,int z,int scale,uint32_t salt) {
    const int ix=floorDiv(x,scale),iz=floorDiv(z,scale);
    const auto smooth=[](uint64_t t){return ((t*t>>16)*(196608-2*t))>>16;};
    const auto tx=smooth(uint64_t(x-ix*scale)*65536/uint32_t(scale));
    const auto tz=smooth(uint64_t(z-iz*scale)*65536/uint32_t(scale));
    const auto mix=[](uint64_t a,uint64_t b,uint64_t t){return (a*(65536-t)+b*t)>>16;};
    return int(mix(mix(forestHash(ix,iz,salt)&65535,forestHash(ix+1,iz,salt)&65535,tx),
                   mix(forestHash(ix,iz+1,salt)&65535,forestHash(ix+1,iz+1,salt)&65535,tx),tz));
}
std::vector<CreativeProp> generateForestTile(const terrain::lego::Surface& terrain,glm::ivec2 tile) {
    std::vector<CreativeProp> trees;trees.reserve(128);
    for(int z=tile.y*16;z<(tile.y+1)*16;++z)
    for(int x=tile.x*16;x<(tile.x+1)*16;++x) {
        if(x< -512||x>=512||z< -512||z>=512)continue;
        const uint32_t h=forestHash(x,z,0x723ac1u);
        // Quarter-stud jitter guarantees three studs between trunks while
        // allowing broad crowns to overlap into a continuous, walkable canopy.
        const glm::dvec2 point(x*8+1.5+double((h>>8)%21)*.25,z*8+1.5+double((h>>20)%21)*.25);
        const int px=int(std::floor(point.x)),pz=int(std::floor(point.y));
        // Fixed settlement footprint: cottages, gardens, arrival lane,
        // blacksmith and cannon. Measure from the village edge, never the
        // moving player, so the meadow stays identical across region loads.
        constexpr glm::dvec2 villageMinimum(1020,-1140),villageMaximum(1248,-990);
        constexpr double meadowWidth=60,woodlandTransition=90;
        const auto outside=glm::max(glm::max(villageMinimum-point,point-villageMaximum),glm::dvec2(0));
        const double villageDistance=glm::length(outside);
        if(villageDistance<meadowWidth)continue;
        // Vary the edge outwards while preserving the whole open buffer.
        const double edgeOffset=18.*double(noise(px,pz,96,0x6147u))/65535.;
        const double edge=std::clamp((villageDistance-meadowWidth-edgeOffset)/woodlandTransition,0.,1.);
        const double villageDensity=edge*edge*(3.-2.*edge);
        if(villageDensity==0)continue;
        // Preserve the authored orchard regardless of streaming/admission order.
        if(std::any_of(villageTrees.begin(),villageTrees.end(),[&](auto p){return glm::length(point-p)<9;}))continue;
        // Warp the ecology fields, not the independent jittered trunk lattice.
        // This produces winding forest edges without cell seams or load-order state.
        const int wx=px+(noise(px,pz,320,0xa312u)-32768)/280;
        const int wz=pz+(noise(px,pz,320,0xb934u)-32768)/280;
        const int woodland=noise(wx,wz,384,0x92317u);
        const int grove=noise(wx,wz,80,0x9f12u);
        int density=std::clamp((woodland-15500)*3+(grove-32768)/2,0,61000);
        // A shared glade mask makes openings legible; the edge gradually fills
        // with young trees instead of a uniformly sparse carpet everywhere.
        density=int(int64_t(density)*std::clamp(noise(wx,wz,112,0x5391u)-14500,0,17000)/17000);
        density=int(double(density)*villageDensity);
        if(int(forestHash(x,z,0xab12u)&65535)>=density)continue;
        const auto [lo,hi]=ground(terrain,point,glm::dvec2(.72));
        if(!std::isfinite(hi)||hi-lo>1.3||lo<double(installedWorld().waterHeight)+4||hi>220)continue;
        // Reject cliffs beyond the root footprint. Thin gradually at the treeline.
        const auto relief=ground(terrain,point,glm::dvec2(3));
        if(!std::isfinite(relief.second)||relief.second-relief.first>5.2)continue;
        if(hi>140&&double(forestHash(x,z,0x739u)&65535)/65535.>(220-hi)/80)continue;
        const int species=noise(wx,wz,256,0x1aa31u);
        const uint32_t choice=forestHash(x,z,0x827u)%100;
        const bool pine=choice<uint32_t(species>39000?72:hi>100?40:12);
        const uint32_t age=forestHash(x,z,0x891u)%100;
        const bool young=age<uint32_t(density<26000?62:18);
        const uint8_t variant=pine?uint8_t(young?5:4):young?2:
            uint8_t(species<26000?(age<62?0:1):(age<28?3:age<65?1:0));
        CreativeProp p{1024u+uint32_t(z+512)*1024u+uint32_t(x+512),pine?Kind::Pine:Kind::Broadleaf,{point.x,lo+.002,point.y},uint8_t(h&3)};
        p.forestVariant=variant;
        const auto& geometry=forestGeometry[variant];
        const auto box=bounds(geometry.minimum,geometry.maximum,p);p.minimum=box.first;p.maximum=box.second;
        trees.push_back(p);
    }
    return trees;
}
// Broad phase for reservations; distant forest must not scan every brick.
class Reservations {
    using Key=std::pair<int,int>;
    std::map<Key,std::vector<const Solid*>> cells_;
    std::vector<const Solid*> large_;
public:
    explicit Reservations(std::span<const Solid> solids) {
        for(const auto& s:solids) {
            const int x0=int(std::floor((s.minimum.x-.8)/64)),x1=int(std::floor((s.maximum.x+.8)/64));
            const int z0=int(std::floor((s.minimum.z-.8)/64)),z1=int(std::floor((s.maximum.z+.8)/64));
            if(int64_t(x1-x0+1)*(z1-z0+1)>256){large_.push_back(&s);continue;}
            for(int z=z0;z<=z1;++z)for(int x=x0;x<=x1;++x)cells_[{x,z}].push_back(&s);
        }
    }
    bool blocks(const CreativeProp& p) const {
        for(const auto* s:large_)if(overlap(p.minimum,p.maximum,*s,.8))return true;
        const int x0=int(std::floor(p.minimum.x/64)),x1=int(std::floor(p.maximum.x/64));
        const int z0=int(std::floor(p.minimum.z/64)),z1=int(std::floor(p.maximum.z/64));
        for(int z=z0;z<=z1;++z)for(int x=x0;x<=x1;++x) {
            const auto it=cells_.find({x,z});if(it==cells_.end())continue;
            for(const auto* s:it->second)if(overlap(p.minimum,p.maximum,*s,.8))return true;
        }
        return false;
    }
};

}
bool CreativeScenery::needsRefresh(glm::dvec2 focus) const noexcept {
    return !initialized_||cell_!=glm::ivec2(std::floor(focus.x/32),std::floor(focus.y/32));
}
CreativeScenery CreativeScenery::admit(const AdventureState& state,const terrain::lego::Surface& terrain,std::span<const Solid> owned,const CreativeScenery* previous,std::span<const Solid> clearance) {
    CreativeScenery out;out.initialized_=true;
    const glm::dvec2 focus(state.player.x,state.player.z);
    if(!terrain.valid()||!std::isfinite(focus.x)||!std::isfinite(focus.y)||glm::any(glm::greaterThan(glm::abs(focus),glm::dvec2(10000))))return out;
    out.cell_=glm::ivec2(std::floor(focus.x/32),std::floor(focus.y/32));
    const glm::dvec2 center=glm::dvec2(out.cell_)*32.+16.;
    out.village_=CreativeVillage::admit(state,terrain,owned,previous?&previous->village_:nullptr,clearance);
    std::vector<Solid> reserved(owned.begin(),owned.end());
    if(!out.village_.appendSolids(reserved))return out;
    // Admission envelopes below protect visual bounds, but never enter the
    // published collision set. In particular, every paving tile has an envelope.
    const size_t reservedCollisionCount=reserved.size();
    // Include full roofs/sails as tree admission envelopes as well as physical
    // walls. Otherwise a decorative canopy could grow through an open room.
    for(const auto& g:out.village_.groups())reserved.push_back({{},{},g.minimum,g.maximum});
    const bool continuing=previous&&previous->initialized_;
    if(continuing)out.suppressed_=previous->suppressed_;
    const glm::dvec2 forward(-std::sin(creativeStartYaw),-std::cos(creativeStartYaw)),right(-forward.y,forward.x);
    struct Candidate {uint32_t id;Kind kind;glm::dvec2 point;uint8_t yaw;bool authored;const CreativeProp* tree=nullptr;};
    std::vector<Candidate> candidates;
    for(size_t i=0;i<sites.size();++i) {
        const auto& s=sites[i];const auto point=creativeStart+right*s.right+forward*s.forward;
        if(glm::length(point-center)<320)candidates.push_back({uint32_t(i+1),s.kind,point,s.yaw,true});
    }
    // Orchard and woodland clusters tie the settlement edge into the meadow.
    // These stable IDs remain outside the starting clearing's ID range.
    for(size_t i=0;i<villageTrees.size();++i)if(glm::length(villageTrees[i]-center)<320)
        candidates.push_back({uint32_t(100+i),i%4==0?Kind::Pine:Kind::Broadleaf,villageTrees[i],uint8_t(i&3),true});
    out.forestCell_=glm::ivec2(std::floor(focus.x/128),std::floor(focus.y/128));
    out.forestTerrain_=terrain;
    const bool sameTerrain=continuing&&previous->forestTerrain_.samples.data()==terrain.samples.data()
        &&previous->forestTerrain_.width==terrain.width&&previous->forestTerrain_.height==terrain.height
        &&previous->forestTerrain_.heightScale==terrain.heightScale&&previous->forestTerrain_.cellScale==terrain.cellScale;
    if(sameTerrain && previous->forestCell_==out.forestCell_) {
        out.forestSource_=previous->forestSource_;out.forestSourceTiles_=previous->forestSourceTiles_;
    }
    if(!out.forestSource_) {
        // Adjacent regions share immutable 128-unit tiles. Travel generates
        // only the entering fringe, preserving the exact original recipe.
        const glm::dvec2 forestCenter=glm::dvec2(out.forestCell_)*128.+64.;
        constexpr double radius=2144;
        std::vector<CreativeProp> trees;trees.reserve(60000);
        for(int z=int(std::floor((forestCenter.y-radius)/128));z<=int(std::floor((forestCenter.y+radius)/128));++z)
        for(int x=int(std::floor((forestCenter.x-radius)/128));x<=int(std::floor((forestCenter.x+radius)/128));++x) {
            const glm::dvec2 minimum=glm::dvec2(x,z)*128.;
            const auto delta=glm::clamp(forestCenter,minimum,minimum+128.)-forestCenter;
            if(glm::dot(delta,delta)>radius*radius)continue;
            const auto key=std::pair(x,z);
            std::shared_ptr<const std::vector<CreativeProp>> tile;
            if(sameTerrain) {
                const auto found=previous->forestSourceTiles_.find(key);
                if(found!=previous->forestSourceTiles_.end())tile=found->second;
            }
            if(!tile)tile=std::make_shared<const std::vector<CreativeProp>>(generateForestTile(terrain,{x,z}));
            out.forestSourceTiles_.emplace(key,tile);
            for(const auto& tree:*tile) {
                const auto distance=glm::dvec2(tree.feet.x,tree.feet.z)-forestCenter;
                if(glm::dot(distance,distance)<=radius*radius)trees.push_back(tree);
            }
        }
        std::sort(trees.begin(),trees.end(),[](const auto& a,const auto& b){return a.id<b.id;});
        out.forestSource_=std::make_shared<const std::vector<CreativeProp>>(std::move(trees));
    }
    const Reservations reservations(reserved);
    std::unordered_set<uint64_t> identities;
    for(const auto& s:state.structures){identities.insert(s.id);for(const auto& p:s.parts)identities.insert(p.id);}
    for(const auto& c:state.components)identities.insert(c.id);
    const bool namespaceBlocked=identities.contains(creativeSceneryStructureId);
    std::unordered_map<uint32_t,const CreativeProp*> previousProps;
    if(continuing)for(const auto& p:previous->props_)previousProps.emplace(p.id,&p);
    out.distantTrees_.reserve(out.forestSource_->size());
    for(const auto& p:*out.forestSource_) {
        if(out.suppressed_.contains(p.id))continue;
        if(namespaceBlocked||identities.contains(creativeSceneryPartBase-p.id)||reservations.blocks(p)) {
            out.suppressed_.insert(p.id);continue;
        }
        const glm::dvec2 point(p.feet.x,p.feet.z);
        if(glm::length(point-center)<=forestCollisionRadius)
            candidates.push_back({p.id,p.kind,point,p.yawQuarterTurns,false,&p});
        else out.distantTrees_.push_back(p);
    }
    // Ground details follow grove edges instead of competing with canopy trees
    // for a single scatter slot. Their identity range is separate from trees.
    for(int z=int(std::floor((center.y-180)/24));z<=int(std::floor((center.y+180)/24));++z)
    for(int x=int(std::floor((center.x-180)/24));x<=int(std::floor((center.x+180)/24));++x) {
        if(x< -256||x>=256||z< -256||z>=256)continue;
        const uint32_t h=forestHash(x,z,0x73293u);
        const glm::dvec2 point(x*24+2+int((h>>8)%9),z*24+2+int((h>>16)%9));
        if(glm::length(point-center)>180||glm::length(point-creativeStart)<72)continue;
        const int woodland=noise(int(point.x),int(point.y),384,0x92317u);
        const bool flowers=woodland>16000&&woodland<36000;
        if(h%100>uint32_t(flowers?32:10))continue;
        candidates.push_back({1100000u+uint32_t(z+256)*512u+uint32_t(x+256),flowers?Kind::Flowers:Kind::Rocks,point,uint8_t(h&3),false});
    }
    // Preserve authored clusters; prioritize nearby world cells if at capacity.
    std::sort(candidates.begin(),candidates.end(),[&](const auto& a,const auto& b) {
        if(a.authored!=b.authored)return a.authored;
        if(a.authored)return a.id<b.id;
        const auto da=glm::length(a.point-center),db=glm::length(b.point-center);
        return da==db?a.id<b.id:da<db;
    });
    size_t collisionCount=reservedCollisionCount;
    for(const auto& c:candidates) {
        // Capacity defers a prop; only actual construction/clearance conflicts
        // suppress its identity. A later refresh may have room for it again.
        if(out.props_.size()>=maximumProps)continue;
        if(out.suppressed_.contains(c.id))continue;
        CreativeProp p;bool found=false,existing=false;
        const auto old=previousProps.find(c.id);
        if(old!=previousProps.end()){p=*old->second;found=existing=true;}
        else if(c.tree){p=*c.tree;found=true;}
        for(int attempt=0;attempt<(c.authored?49:1)&&!found;++attempt) {
            const int ring=attempt?1+(attempt-1)/8:0,dir=attempt?(attempt-1)%8:0;
            const double angle=dir*3.141592653589793/4;
            const auto raw=c.point+glm::dvec2(std::cos(angle),std::sin(angle))*double(ring)*.75;
            const glm::dvec2 point(std::round(raw.x),std::round(raw.y));
            glm::dvec2 extent=c.kind==Kind::Bench?glm::dvec2(2.25,.75):c.kind==Kind::Crate?glm::dvec2(1,1.07):c.kind==Kind::Rocks?glm::dvec2(1.5,.9):glm::dvec2(.95);
            if(c.yaw&1)std::swap(extent.x,extent.y);
            const auto [lo,hi]=ground(terrain,point,extent);
            const double tolerance=c.kind==Kind::Bench||c.kind==Kind::Crate?.02:c.kind==Kind::Flowers?.33:.65;
            if(!std::isfinite(hi)||hi-lo>tolerance||lo<double(installedWorld().waterHeight)+2)continue;
            if(!c.authored&&hi>180&&c.kind!=Kind::Rocks)continue;
            const auto kind=c.kind;
            p={c.id,kind,{point.x,lo+.002,point.y},c.yaw};
            const auto box=bounds(low[size_t(p.kind)],high[size_t(p.kind)],p);p.minimum=box.first;p.maximum=box.second;
            const auto closest=glm::clamp(creativeStart,glm::dvec2(p.minimum.x,p.minimum.z),glm::dvec2(p.maximum.x,p.maximum.z));
            if(c.kind!=Kind::Flowers&&glm::length(closest-creativeStart)<13)continue;
            bool collision=false;
            for(const auto& q:out.props_)if(overlap(p.minimum,p.maximum,{{},{},q.minimum,q.maximum},.15)){collision=true;break;}
            if(!collision)found=true;
        }
        if(!found)continue;
        bool blocked=false;
        const auto aliases=[&](uint64_t id){return id==creativeSceneryStructureId||id==creativeSceneryPartBase-p.id;};
        for(const auto& structure:state.structures) {
            blocked|=aliases(structure.id);
            for(const auto& part:structure.parts)blocked|=aliases(part.id);
        }
        for(const auto& component:state.components)blocked|=aliases(component.id);
        for(const auto& b:reserved)if(overlap(p.minimum,p.maximum,b,.8)){blocked=true;break;}
        if(!existing) {
            const glm::dvec3 feet(state.player.x,state.player.y,state.player.z);
            const double radius=AdventurePlayer::creativeRadius*AdventurePlayer::creativeScale;
            const Solid actor{{},{},feet-glm::dvec3(radius,0,radius),feet+glm::dvec3(radius,5.7,radius)};
            // A canopy above the player/bike is safe. Only physical trunks
            // exclude a newly streamed tree around those moving reservations.
            if(p.forestVariant<forestGeometry.size()) {
                for(const auto& [lo,hi]:boxes(p)) {
                    const auto b=bounds(lo,hi,p);
                    blocked|=overlap(b.first,b.second,actor,.2);
                    for(const auto& clearanceSolid:clearance)blocked|=overlap(b.first,b.second,clearanceSolid,.2);
                }
            } else {
                if(overlap(p.minimum,p.maximum,actor,.2))blocked=true;
                for(const auto& b:clearance)if(overlap(p.minimum,p.maximum,b,.2)){blocked=true;break;}
            }
        }
        if(blocked){out.suppressed_.insert(c.id);continue;}
        const size_t count=boxes(p).size();
        if(collisionCount+count>AdventureSpatialQueries::maximumSolids)continue;
        collisionCount+=count;out.props_.push_back(p);
    }
    return out;
}
bool CreativeScenery::appendSolids(construction::WorldNamespace world,std::vector<Solid>& output) const {
    std::vector<Solid> added;
    for(const auto& p:props_) {
        const auto structure=creativeSceneryStructureId,part=creativeSceneryPartBase-p.id;
        // No reserved ID may alias a restored/user solid, even in legacy saves.
        for(const auto& solid:output)if(solid.structure.counter==structure||solid.part.counter==part)return false;
        for(const auto& [lo,hi]:boxes(p)) {
            const auto b=bounds(lo,hi,p);added.push_back({{world,structure},{world,part},b.first,b.second});
        }
    }
    if(output.size()+added.size()>AdventureSpatialQueries::maximumSolids)return false;
    output.insert(output.end(),added.begin(),added.end());return village_.appendSolids(output);
}
}
