#include "game/adventure/spatial_queries.hpp"
#include <algorithm>
#include <cmath>
#include <limits>

namespace voxy::game::adventure {
namespace {
constexpr double infinity=std::numeric_limits<double>::infinity();
bool finite(glm::dvec3 p) noexcept {return std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.z);}
bool inRange(glm::dvec3 p) noexcept {return finite(p)&&glm::all(glm::lessThanEqual(glm::abs(p),glm::dvec3(100000)));}
struct Gap {double distance;glm::dvec3 normal;};
// Exact distance from an upright capsule's vertical axis segment to an AABB.
Gap capsuleGap(glm::dvec3 feet,double radius,double height,const AdventureSpatialQueries::Solid& b) noexcept {
    const auto low=feet+glm::dvec3(0,radius,0),high=feet+glm::dvec3(0,height-radius,0);
    glm::dvec3 delta(low.x-std::clamp(low.x,b.minimum.x,b.maximum.x),0,
        low.z-std::clamp(low.z,b.minimum.z,b.maximum.z));
    if(high.y<b.minimum.y)delta.y=high.y-b.minimum.y;
    else if(low.y>b.maximum.y)delta.y=low.y-b.maximum.y;
    const double length=glm::length(delta);
    if(length>1e-14)return {length-radius,delta/length};
    double penetration=infinity;glm::dvec3 normal(0);
    for(int axis=0;axis<3;++axis) {
        const double positive=b.maximum[axis]-low[axis]+radius;
        const double negative=high[axis]-b.minimum[axis]+radius;
        if(positive<penetration){penetration=positive;normal=glm::dvec3(0);normal[axis]=1;}
        if(negative<penetration){penetration=negative;normal=glm::dvec3(0);normal[axis]=-1;}
    }
    return {-penetration,normal};
}
AdventureSpatialQueries::SweepResult castBox(glm::dvec3 from,glm::dvec3 to,double radius,double height,
    const AdventureSpatialQueries::Solid& box) noexcept {
    const auto delta=to-from;const double length=glm::length(delta);
    AdventureSpatialQueries::SweepResult result{true,false,false,length,{0,0,0}};
    double travelled=0;
    for(int i=0;i<32;++i) {
        const auto gap=capsuleGap(from+(length>0?delta*(travelled/length):glm::dvec3(0)),radius,height,box);
        if(gap.distance< -1e-7)return {true,true,travelled==0,travelled,gap.normal};
        const double closing=-glm::dot(delta,gap.normal);
        if(length<1e-12||closing<=1e-12)return result;
        if(gap.distance<=1e-7)return {true,true,false,travelled,gap.normal};
        travelled+=gap.distance*length/closing;
        if(travelled>length)return result;
    }
    return {false,true,false,travelled,{0,0,0}};
}
void merge(AdventureSpatialQueries::SweepResult& best,const AdventureSpatialQueries::SweepResult& next) noexcept {
    if(!next.complete) {best.complete=false;return;}
    if(next.hit&&(!best.hit||next.distance<best.distance||next.startOverlapped)) {
        const bool complete=best.complete;best=next;best.complete=complete;
    }
}
bool capsuleArguments(glm::dvec3 from,glm::dvec3 to,double radius,double height) noexcept {
    return inRange(from)&&inRange(to)&&std::isfinite(radius)&&radius>0&&radius<=4
        &&std::isfinite(height)&&height>=2*radius&&height<=10&&glm::length(to-from)<=128;
}
}

bool AdventureSpatialQueries::bindTerrain(terrain::lego::Surface value) noexcept {
    if(!value.valid())return false;
    terrain_=value;return true;
}
bool AdventureSpatialQueries::publish(std::span<const Solid> source,uint64_t revision) {
    if(!revision||revision<=revision_||source.size()>maximumSolids)return false;
    std::map<Sector,std::vector<uint16_t>> sectors;
    for(size_t i=0;i<source.size();++i) {
        const auto& b=source[i];
        if(!construction::isValid(b.structure)||!construction::isValid(b.part)
            ||b.structure.world!=b.part.world||!inRange(b.minimum)||!inRange(b.maximum)
            ||glm::any(glm::greaterThanEqual(b.minimum,b.maximum))
            ||glm::any(glm::greaterThan(b.maximum-b.minimum,glm::dvec3(64))))return false;
        const int x0=int(std::floor(b.minimum.x/sectorSize)),x1=int(std::floor(b.maximum.x/sectorSize));
        const int z0=int(std::floor(b.minimum.z/sectorSize)),z1=int(std::floor(b.maximum.z/sectorSize));
        for(int z=z0;z<=z1;++z)for(int x=x0;x<=x1;++x)sectors[{x,z}].push_back(uint16_t(i));
    }
    std::vector<Solid> copy(source.begin(),source.end());
    solids_=std::move(copy);sectors_=std::move(sectors);revision_=revision;return true;
}
bool AdventureSpatialQueries::candidates(glm::dvec3 lo,glm::dvec3 hi,
    std::array<uint16_t,maximumSolids>& out,size_t& count) const noexcept {
    count=0;
    if(!inRange(lo)||!inRange(hi)||glm::any(glm::greaterThan(lo,hi)))return false;
    const int x0=int(std::floor(lo.x/sectorSize)),x1=int(std::floor(hi.x/sectorSize));
    const int z0=int(std::floor(lo.z/sectorSize)),z1=int(std::floor(hi.z/sectorSize));
    if(int64_t(x1-x0+1)*int64_t(z1-z0+1)>64)return false;
    std::array<bool,maximumSolids> seen{};
    for(int z=z0;z<=z1;++z)for(int x=x0;x<=x1;++x) {
        const auto bucket=sectors_.find({x,z});if(bucket==sectors_.end())continue;
        for(const auto index:bucket->second)if(!seen[index]) {
            seen[index]=true;const auto& b=solids_[index];
            if(glm::all(glm::lessThanEqual(lo,b.maximum))&&glm::all(glm::greaterThanEqual(hi,b.minimum)))out[count++]=index;
        }
    }
    return true;
}
bool AdventureSpatialQueries::clearCapsule(glm::dvec3 feet,double radius,double height) const noexcept {
    if(!terrain_.valid()||!capsuleArguments(feet,feet,radius,height))return false;
    if(double(terrain::lego::supportHeight(terrain_,glm::vec2(feet.x,feet.z),float(radius)))>feet.y+1e-5)return false;
    std::array<uint16_t,maximumSolids> found{};size_t count=0;
    if(!candidates(feet-glm::dvec3(radius,0,radius),feet+glm::dvec3(radius,height,radius),found,count))return false;
    for(size_t i=0;i<count;++i)if(capsuleGap(feet,radius,height,solids_[found[i]]).distance< -1e-7)return false;
    return true;
}
double AdventureSpatialQueries::supportHeight(glm::dvec2 p,double radius,double limit) const noexcept {
    if(!terrain_.valid()||!std::isfinite(p.x)||!std::isfinite(p.y)||!std::isfinite(radius)
        ||radius<=0||radius>4||!std::isfinite(limit))return -infinity;
    double result=terrain::lego::supportHeight(terrain_,glm::vec2(p),float(radius));
    if(result>limit+1e-6)result=-infinity;
    std::array<uint16_t,maximumSolids> found{};size_t count=0;
    if(!candidates({p.x-radius,-100000,p.y-radius},{p.x+radius,limit+radius,p.y+radius},found,count))return -infinity;
    for(size_t i=0;i<count;++i) {
        const auto& b=solids_[found[i]];
        const auto nearest=glm::clamp(p,glm::dvec2(b.minimum.x,b.minimum.z),glm::dvec2(b.maximum.x,b.maximum.z));
        const double distance=glm::length(p-nearest);
        if(distance>radius)continue;
        const double top=b.maximum.y+std::sqrt(std::max(0.,radius*radius-distance*distance))-radius;
        if(top<=limit+1e-6)result=std::max(result,top);
    }
    return result;
}
AdventureSpatialQueries::SweepResult AdventureSpatialQueries::sweepCapsule(glm::dvec3 from,glm::dvec3 to,double radius,double height) const noexcept {
    if(!terrain_.valid()||!capsuleArguments(from,to,radius,height))return {};
    auto result=expedition::sweepCoveTerrainSphere(terrain_,from+glm::dvec3(0,radius,0),to+glm::dvec3(0,radius,0),radius,true);
    std::array<uint16_t,maximumSolids> found{};size_t count=0;
    if(!candidates(glm::min(from,to)-glm::dvec3(radius,0,radius),glm::max(from,to)+glm::dvec3(radius,height,radius),found,count))return {};
    for(size_t i=0;i<count;++i)merge(result,castBox(from,to,radius,height,solids_[found[i]]));
    return result;
}
AdventureSpatialQueries::SweepResult AdventureSpatialQueries::sweepSphere(glm::dvec3 from,glm::dvec3 to,double radius,uint64_t expected) const noexcept {
    if((expected&&expected!=revision_)||!capsuleArguments(from,to,radius,radius*2))return {};
    return sweepCapsule(from-glm::dvec3(0,radius,0),to-glm::dvec3(0,radius,0),radius,radius*2);
}
AdventureSpatialQueries::RayHit AdventureSpatialQueries::raycast(glm::dvec3 origin,glm::dvec3 direction,double distance) const noexcept {
    if(!terrain_.valid()||!inRange(origin)||!finite(direction)||glm::length(direction)<1e-10
        ||!std::isfinite(distance)||distance<=0||distance>64)return {};
    direction=glm::normalize(direction);const auto end=origin+direction*distance;
    // A microscopic sphere shares the exact column/stud terrain implementation.
    // Its <=10 micrometre conservative offset is below the .02 m placement grid.
    const auto terrain=expedition::sweepCoveTerrainSphere(terrain_,origin,end,.00001,true);
    if(!terrain.complete)return {};
    RayHit result{true,terrain.hit,terrain.hit,terrain.hit?terrain.distance:distance,
        origin+direction*(terrain.hit?terrain.distance:distance),terrain.normal,{},{}};
    std::array<uint16_t,maximumSolids> found{};size_t count=0;
    if(!candidates(glm::min(origin,end),glm::max(origin,end),found,count))return {};
    for(size_t i=0;i<count;++i) {
        const auto& b=solids_[found[i]];double enter=0,exit=distance;glm::dvec3 normal(0);bool hit=true;
        for(int axis=0;axis<3;++axis) {
            if(std::abs(direction[axis])<1e-14) {if(origin[axis]<b.minimum[axis]||origin[axis]>b.maximum[axis])hit=false;continue;}
            double a=(b.minimum[axis]-origin[axis])/direction[axis],z=(b.maximum[axis]-origin[axis])/direction[axis];
            const double sign=direction[axis]>0?-1.:1.;if(a>z)std::swap(a,z);
            if(a>enter){enter=a;normal=glm::dvec3(0);normal[axis]=sign;}exit=std::min(exit,z);
            if(enter>exit)hit=false;
        }
        if(hit&&enter<=result.distance)result={true,true,false,enter,origin+direction*enter,normal,b.structure,b.part};
    }
    return result;
}
expedition::CoveCamera::Sweep AdventureSpatialQueries::cameraSweep() const noexcept {
    return {this,[](const void* self,glm::dvec3 from,glm::dvec3 to,double radius,uint64_t revision) noexcept {
        return static_cast<const AdventureSpatialQueries*>(self)->sweepSphere(from,to,radius,revision);
    }};
}
} // namespace voxy::game::adventure
