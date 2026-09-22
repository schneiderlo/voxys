#include "game/adventure/creative_village.hpp"
#include "game/adventure/creative_village_geometry.hpp"
#include "game/adventure/creative_scenery.hpp"
#include "game/adventure/world_definition.hpp"
#include <algorithm>
#include <cmath>
#include <set>
namespace voxy::game::adventure {
namespace {
using Solid=AdventureSpatialQueries::Solid;
glm::dvec3 rotate(glm::dvec3 p,uint8_t yaw) {
    switch(yaw&3){case 1:return {p.z,p.y,-p.x};case 2:return {-p.x,p.y,-p.z};case 3:return {-p.z,p.y,p.x};default:return p;}
}
bool overlap(glm::dvec3 lo,glm::dvec3 hi,const Solid& b,double margin=0) {
    return glm::all(glm::lessThan(lo,b.maximum+glm::dvec3(margin)))&&glm::all(glm::greaterThan(hi,b.minimum-glm::dvec3(margin)));
}
std::pair<double,double> ground(const terrain::lego::Surface& s,glm::dvec2 p,glm::dvec2 extent) {
    if(!s.valid())return {INFINITY,INFINITY};
    const glm::dvec2 origin(s.origin());const double cell=s.cellScale;
    const int x0=int(std::floor((p.x-extent.x+origin.x)/cell)),z0=int(std::floor((p.y-extent.y+origin.y)/cell));
    const int x1=int(std::floor((p.x+extent.x+origin.x-1e-7)/cell)),z1=int(std::floor((p.y+extent.y+origin.y-1e-7)/cell));
    if(x0<0||z0<0||x1>=int(s.width)-1||z1>=int(s.height)-1)return {INFINITY,INFINITY};
    double lo=INFINITY,hi=-INFINITY;
    for(int z=z0;z<=z1;++z)for(int x=x0;x<=x1;++x){const double y=s.cellTop(x,z);lo=std::min(lo,y);hi=std::max(hi,y);}
    return {lo,hi};
}
std::pair<glm::dvec3,glm::dvec3> transformed(VillageBox b,const CreativeVillagePiece& piece) {
    glm::dvec3 lo(INFINITY),hi(-INFINITY);
    for(double x:{b.minimum.x,b.maximum.x})for(double y:{b.minimum.y,b.maximum.y})for(double z:{b.minimum.z,b.maximum.z}) {
        const auto p=piece.feet+rotate(glm::dvec3(x,y,z)*piece.scale,piece.yaw);lo=glm::min(lo,p);hi=glm::max(hi,p);
    }
    return {lo,hi};
}
void add(CreativeVillageGroup& group,construction::WorldNamespace world,CreativeVillagePiece piece) {
    const auto bounds=transformed(villageMeshBounds[piece.mesh],piece);
    group.minimum=glm::min(group.minimum,bounds.first);group.maximum=glm::max(group.maximum,bounds.second);
    group.pieces.push_back(piece);
    const auto part=creativeSceneryPartBase-1500000u-group.id;
    for(const auto& b:villageMeshSolids(piece.mesh)) {
        const auto box=transformed(b,piece);
        group.solids.push_back({{world,creativeSceneryStructureId},{world,part},box.first,box.second});
    }
}
CreativeVillageGroup building(uint16_t id,uint8_t mesh,glm::dvec2 center,uint8_t yaw,
    const terrain::lego::Surface& terrain,construction::WorldNamespace world) {
    CreativeVillageGroup group;group.id=id;group.minimum=glm::dvec3(INFINITY);group.maximum=glm::dvec3(-INFINITY);
    const glm::dvec2 extent=(mesh<2||mesh==8)?glm::dvec2(5,4):mesh==9?glm::dvec2(8.5,4):mesh==2?glm::dvec2(3):mesh==3?glm::dvec2(3.5):mesh==4?glm::dvec2(5,3):mesh==10?glm::dvec2(4,2.5):(mesh==11||mesh==12||mesh==14)?glm::dvec2(3,.8):mesh==13?glm::dvec2(1.6,1):glm::dvec2(2.2);
    const auto [lo,hi]=ground(terrain,center,(yaw&1)?glm::dvec2(extent.y,extent.x):extent);
    if(!std::isfinite(hi)||hi-lo>1.61||lo<double(installedWorld().waterHeight)+2)return group;
    const double base=hi+.025;
    add(group,world,{7,yaw,{center.x,lo-.025,center.y},{extent.x*2,base-lo+.025,extent.y*2}});
    add(group,world,{mesh,yaw,{center.x,base,center.y},{1,1,1}});
    if(mesh<4||mesh==8||mesh==9) {
        // A generous three-stud doorway and .28-stud steps fit the actual
        // 4.76-stud minifigure; no invisible full-building box across the room.
        const double front=-extent.y;
        const auto entry=glm::dvec3(center.x,0,center.y)+rotate({0,0,front-.5},yaw);
        const auto range=ground(terrain,{entry.x,entry.z},{1.7,1.7});
        const double floor=base+.32;
        const int count=std::clamp(int(std::ceil((floor-range.first)/.28)),1,8);
        for(int i=0;i<count;++i) {
            const auto p=glm::dvec3(center.x,0,center.y)+rotate({0,0,front-.425-.85*i},yaw);
            const auto g=ground(terrain,{p.x,p.z},{1.7,1.7});
            const double top=floor-.28*i;
            if(!std::isfinite(g.first)||top<=g.second+.18)continue;
            add(group,world,{7,yaw,{p.x,g.first-.02,p.z},{3.3,top-g.first+.02,.85}});
        }
    }
    return group;
}
}
CreativeVillage CreativeVillage::admit(const AdventureState& state,const terrain::lego::Surface& terrain,std::span<const Solid> reserved,const CreativeVillage* previous,std::span<const Solid> clearance) {
    CreativeVillage out;out.initialized_=true;if(!terrain.valid())return out;
    const bool continuing=previous&&previous->initialized_;
    size_t total=reserved.size();
    const auto admit=[&](CreativeVillageGroup group) {
        if(group.pieces.empty())return;
        if(continuing&&std::none_of(previous->groups_.begin(),previous->groups_.end(),[&](const auto& g){return g.id==group.id;}))return;
        for(const auto& b:reserved)if(overlap(group.minimum,group.maximum,b,.5))return;
        const auto part=creativeSceneryPartBase-1500000u-group.id;
        const auto alias=[&](uint64_t value){return value==creativeSceneryStructureId||value==part;};
        for(const auto& structure:state.structures){if(alias(structure.id))return;for(const auto& p:structure.parts)if(alias(p.id))return;}
        for(const auto& c:state.components)if(alias(c.id))return;
        if(!continuing) {
            const glm::dvec3 p(state.player.x,state.player.y,state.player.z);
            const Solid player{{},{},p-glm::dvec3(1.12,0,1.12),p+glm::dvec3(1.12,5.7,1.12)};
            // Player inside an open cottage is safe: test actual solids rather
            // than ejecting them merely for standing under its roof.
            for(const auto& b:group.solids)if(overlap(b.minimum,b.maximum,player))return;
            for(const auto& c:clearance)for(const auto& b:group.solids)if(overlap(b.minimum,b.maximum,c,.05))return;
        }
        if(total+group.solids.size()>AdventureSpatialQueries::maximumSolids)return;
        total+=group.solids.size();out.groups_.push_back(std::move(group));
    };
    uint16_t id=0;
    struct House {uint8_t mesh;glm::dvec2 center;uint8_t yaw;};
    // Staggered plots leave a central view corridor to the commons and tower.
    const std::array houses{
        House{0,{1064,-1050},0},House{8,{1082,-1034},0},
        House{0,{1127,-1040},0},House{1,{1154,-1044},0},
        House{1,{1067,-1086},2},House{9,{1088,-1092},2},
        House{8,{1130,-1096},2},House{9,{1168,-1100},2},
        House{1,{1048,-1034},0},House{8,{1110,-1011},0},House{1,{1176,-1036},0}};
    for(const auto& h:houses)admit(building(++id,h.mesh,h.center,h.yaw,terrain,state.world));
    admit(building(++id,2,{1110,-1035},0,terrain,state.world));
    admit(building(++id,3,{1042,-1068},0,terrain,state.world));
    for(const glm::dvec2 plot:std::array{glm::dvec2(1044,-1091),glm::dvec2(1086,-1058),glm::dvec2(1146,-1024),glm::dvec2(1173,-1064)})
        for(double x:{-5.3,5.3})for(double z:{-3.4,3.4})admit(building(++id,4,plot+glm::dvec2(x,z),0,terrain,state.world));
    admit(building(++id,6,{1110,-1067},0,terrain,state.world));
    for(const glm::dvec2 p:std::array{glm::dvec2(1101,-1055),glm::dvec2(1126,-1060)})admit(building(++id,10,p,0,terrain,state.world));
    // Each house has planted frontage, leaving a wide clear entrance.
    for(const auto& h:houses)for(double side:{-1.,1.}) {
        const auto p=glm::dvec3(h.center.x,0,h.center.y)+rotate({side*5.7,0,-6.8},h.yaw);
        admit(building(++id,side<0?11:12,{p.x,p.z},h.yaw,terrain,state.world));
    }
    for(const glm::dvec2 p:std::array{glm::dvec2(1103,-1055),glm::dvec2(1131,-1058),glm::dvec2(1095,-1089),glm::dvec2(1164,-1105)})admit(building(++id,13,p,0,terrain,state.world));
    // Gardens frame the arrival lane without blocking the view of the commons.
    // Broken hedgerows leave the house and farm entrances open.
    for(const auto p:std::array{glm::dvec2(1190,-1100),glm::dvec2(1182,-1088),glm::dvec2(1170,-1078),
        glm::dvec2(1150,-1082),glm::dvec2(1144,-1082),glm::dvec2(1138,-1082)})
        admit(building(++id,11,p,0,terrain,state.world));
    for(const auto p:std::array{glm::dvec2(1190,-1103),glm::dvec2(1185,-1098),
        glm::dvec2(1181,-1092),glm::dvec2(1176,-1086),glm::dvec2(1170,-1081),
        glm::dvec2(1148,-1079),glm::dvec2(1142,-1079),glm::dvec2(1136,-1079)})
        admit(building(++id,12,p,0,terrain,state.world));
    for(const auto p:std::array{glm::dvec2(1193,-1087),glm::dvec2(1188,-1080)})
        admit(building(++id,4,p,0,terrain,state.world));
    std::set<std::pair<int,int>> tiles;
    const auto lane=[&](glm::dvec2 a,glm::dvec2 b,int halfWidth=1) {
        const auto delta=b-a;const int steps=std::max(1,int(std::ceil(glm::length(delta)/1.5)));
        const auto forward=glm::length(delta)>0?glm::normalize(delta):glm::dvec2(1,0);const glm::dvec2 side(-forward.y,forward.x);
        for(int i=0;i<=steps;++i)for(int j=-halfWidth;j<=halfWidth;++j) {
            const auto p=a+delta*(double(i)/steps)+side*double(j*2);
            tiles.emplace(int(std::round(p.x/2))*2,int(std::round(p.y/2))*2);
        }
    };
    lane({1194,-1114},{1180,-1090});lane({1180,-1090},{1158,-1070});
    lane({1042,-1076},{1080,-1072});lane({1080,-1072},{1110,-1070});lane({1110,-1070},{1160,-1070});
    lane({1110,-1080},{1110,-1038});
    for(const auto& h:houses) {
        const auto entry=glm::dvec3(h.center.x,0,h.center.y)+rotate({0,0,-5},h.yaw);
        if(h.center.x==1082) {
            lane({1098,-1070},{1098,-1044},0);lane({1098,-1044},{1082,-1044},0);
            lane({1082,-1044},{entry.x,entry.z},0);
        } else if(h.center.y==-1011) {
            lane({1100,-1058},{1100,-1020},0);lane({1100,-1020},{1110,-1020},0);lane({1110,-1020},{entry.x,entry.z},0);
        } else lane({h.center.x,-1071},{entry.x,entry.z},0);
    }
    lane({1042,-1076},{1042,-1072});lane({1146,-1040},{1146,-1030});
    lane({1158,-1070},{1178,-1070});lane({1180,-1090},{1190,-1091},0);lane({1044,-1084},{1064,-1074});
    for(int x=1098;x<=1122;x+=2)for(int z=-1078;z<=-1058;z+=2)tiles.emplace(x,z);
    const size_t fixedGroups=out.groups_.size();
    for(const auto& [x,z]:tiles) {
        ++id;const auto [lo,hi]=ground(terrain,{x,z},{1,1});
        if(!std::isfinite(hi)||hi-lo>1.61||lo<double(installedWorld().waterHeight)+2)continue;
        bool blocked=false;
        for(size_t gi=0;gi<fixedGroups;++gi)if(const auto& g=out.groups_[gi];overlap({x-1.,lo,z-1.},{x+1.,hi+.35,z+1.},{{},{},g.minimum,g.maximum})){blocked=true;break;}
        if(blocked)continue;
        CreativeVillageGroup group;group.id=id;group.minimum=glm::dvec3(INFINITY);group.maximum=glm::dvec3(-INFINITY);
        // Small paving pieces follow each terrace instead of discarding a
        // whole 2x2 patch whenever it straddles two terrain elevations.
        if(hi-lo<.001) {
            add(group,state.world,{5,0,{double(x),lo+.002,double(z)},{1,.24/.35,1}});
        } else for(double dx:{-.5,.5})for(double dz:{-.5,.5}) {
            const auto [tileLo,tileHi]=ground(terrain,{x+dx,z+dz},{.5,.5});
            add(group,state.world,{5,0,{x+dx,tileLo+.002,z+dz},{.5,(tileHi-tileLo+.24)/.35,.5}});
        }
        admit(std::move(group));
    }
    return out;
}
bool CreativeVillage::appendSolids(std::vector<Solid>& solids) const {
    size_t count=solids.size();for(const auto& group:groups_)count+=group.solids.size();
    if(count>AdventureSpatialQueries::maximumSolids)return false;
    for(const auto& group:groups_)solids.insert(solids.end(),group.solids.begin(),group.solids.end());
    return true;
}
}
