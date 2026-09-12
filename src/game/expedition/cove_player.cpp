#include "game/expedition/cove_player.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <glm/gtc/quaternion.hpp>

namespace voxy::game::expedition {
namespace {
constexpr double skin = .005, stepHeight = .36;
bool finite(glm::dvec3 p) { return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z); }
glm::dvec3 metres(construction::GridPosition p) { return glm::dvec3(p.x, p.y, p.z) * .02; }

double turnedHeading(double yaw,const glm::dmat4& before,const glm::dmat4& after) noexcept {
    const auto direction=glm::dmat3(after)*glm::transpose(glm::dmat3(before))*glm::dvec3(-std::sin(yaw),0,-std::cos(yaw));
    return std::hypot(direction.x,direction.z)>1e-8?std::atan2(-direction.x,-direction.z):yaw;
}

struct Gap {double distance;glm::dvec3 normal;};
// Exact closest points between a line segment and an axis-aligned box. Each
// face crossing divides the squared distance into one quadratic interval.
Gap capsuleBox(glm::dvec3 feet,double radius,double height,glm::dvec3 lo,
    glm::dvec3 hi,const glm::dmat4& pose) noexcept {
    const auto rotation=glm::dmat3(pose),inverse=glm::transpose(rotation);
    const auto a=inverse*(feet+glm::dvec3(0,radius,0)-glm::dvec3(pose[3]));
    const auto b=inverse*(feet+glm::dvec3(0,height-radius,0)-glm::dvec3(pose[3]));
    const auto direction=b-a;
    std::array<double,8> points{0,1};size_t count=2;
    for(int axis=0;axis<3;++axis)if(std::abs(direction[axis])>1e-14)
        for(double face:{lo[axis],hi[axis]}) {
            const double t=(face-a[axis])/direction[axis];
            if(t>0&&t<1)points[count++]=t;
        }
    // Two endpoints plus at most two face crossings on each of three axes.
    // Insertion sort expresses that eight-element bound without std::sort's
    // larger internal insertion-sort threshold triggering optimized warnings.
    for(size_t i=1;i<count;++i) {
        const double value=points[i];size_t j=i;
        while(j>0&&value<points[j-1]){points[j]=points[j-1];--j;}
        points[j]=value;
    }
    double best=std::numeric_limits<double>::infinity();glm::dvec3 separation(0);
    const auto evaluate=[&](double t) {
        const auto point=a+direction*t,delta=point-glm::clamp(point,lo,hi);
        const double squared=glm::dot(delta,delta);
        if(squared<best){best=squared;separation=delta;}
    };
    for(size_t i=1;i<count;++i) {
        evaluate(points[i-1]);evaluate(points[i]);
        const auto midpoint=a+direction*((points[i-1]+points[i])*.5);
        double quadratic=0,linear=0;
        for(int axis=0;axis<3;++axis) {
            const double face=midpoint[axis]<lo[axis]?lo[axis]:hi[axis];
            if(midpoint[axis]>=lo[axis]&&midpoint[axis]<=hi[axis])continue;
            quadratic+=direction[axis]*direction[axis];linear+=(a[axis]-face)*direction[axis];
        }
        if(quadratic>0)evaluate(std::clamp(-linear/quadratic,points[i-1],points[i]));
    }
    if(best>1e-24) {
        const double distance=std::sqrt(best);
        return {distance-radius,rotation*(separation/distance)};
    }
    double penetration=std::numeric_limits<double>::infinity();glm::dvec3 normal(0);
    for(int axis=0;axis<3;++axis) {
        const double positive=hi[axis]-std::min(a[axis],b[axis])+radius;
        const double negative=std::max(a[axis],b[axis])-lo[axis]+radius;
        if(positive<penetration){penetration=positive;normal=glm::dvec3(0);normal[axis]=1;}
        if(negative<penetration){penetration=negative;normal=glm::dvec3(0);normal[axis]=-1;}
    }
    return {-penetration,rotation*normal};
}

CovePlayer::SweepResult castCapsule(glm::dvec3 start,glm::dvec3 end,double radius,
    double height,glm::dvec3 lo,glm::dvec3 hi,const glm::dmat4& pose,double margin=0) noexcept {
    const auto delta=end-start;const double length=glm::length(delta);
    CovePlayer::SweepResult result{true,false,false,length,{0,0,0}};
    double traveled=0;
    for(int iteration=0;iteration<32;++iteration) {
        const auto at=start+(length>0?delta*(traveled/length):glm::dvec3(0));
        const auto gap=capsuleBox(at,radius,height,lo,hi,pose);
        if(gap.distance<margin-1e-7) {
            result.hit=true;result.startOverlapped=traveled==0;result.distance=traveled;result.normal=gap.normal;return result;
        }
        // Distance to a convex obstacle is convex along a straight trajectory.
        // A separating/tangent closest-plane derivative cannot turn into a hit.
        if(length<1e-12||glm::dot(delta,gap.normal)>=-1e-12)return result;
        if(gap.distance<=margin+1e-7) {
            result.hit=true;result.distance=traveled;result.normal=gap.normal;return result;
        }
        // The closest separating plane bounds the whole convex proxy. Using
        // its closing speed also certifies near-parallel clear paths promptly.
        traveled+=(gap.distance-margin)*length/(-glm::dot(delta,gap.normal));
        if(traveled>length)return result;
    }
    // Bounded conservative advancement cannot certify the unvisited remainder.
    result.complete=false;result.hit=true;result.distance=traveled;return result;
}
}

bool CovePlayer::initialize(const assets::LoadedAssetFixture& scene, Ground ground, std::string& error, std::span<const uint32_t> boatSlots, GroundSupport support) {
    if (!scene.registry.navigation || !ground) { error = "Cove navigation and ground are required"; return false; }
    CovePlayer candidate;
    candidate.navigation_ = *scene.registry.navigation;
    candidate.ground_ = std::move(ground);candidate.groundSupport_=std::move(support);
    for (size_t index = 0; index < scene.registry.placements.size(); ++index) {
        const auto& cargo=candidate.navigation_.cargoPlacements;
        if(std::find(cargo.begin(),cargo.end(),index)!=cargo.end()) continue;
        const auto& placement = scene.registry.placements[index];
        const auto& members = candidate.navigation_.boatPlacements;
        const bool boat = std::find(members.begin(), members.end(), index) != members.end();
        // A dismantled craft slot is absent, never newly classified as scenery.
        if(!boat && std::find(boatSlots.begin(),boatSlots.end(),index)!=boatSlots.end())continue;
        if (placement.prototype || placement.bundleIndex >= scene.bundles.size()) {
            error = "Cove collision requires admitted authored parts"; return false;
        }
        for (const auto& proxy : scene.bundles[placement.bundleIndex]->sidecar().part.collision) {
            const auto local = construction::boxBounds(proxy);
            const auto bounds = local ? construction::transformBounds(placement.placement, *local) : std::nullopt;
            if (!bounds || candidate.boxes_.size() >= 1024) { error = "Cove collision bounds/capacity"; return false; }
            candidate.boxes_.push_back({metres(bounds->minimum), metres(bounds->maximum), boat,static_cast<uint32_t>(index)});
        }
    }
    for (const auto& [point,name] : std::array<std::pair<glm::dvec3,const char*>,4>{{
            {candidate.navigation_.spawn,"Dock spawn"},{candidate.navigation_.dockBoarding,"Dock boarding"},
            {candidate.navigation_.boatBoarding,"Boat boarding"},{candidate.navigation_.helmStanding,"Helm standing"}}}) {
        auto p=point;p.y+=skin;
        if (!finite(p) || !candidate.clear(p) || !candidate.supported(p)) {
            error=std::string(name)+" needs clear standing room and support";return false;
        }
    }
    if (!candidate.boatSupport(candidate.navigation_.boatBoarding)
        || !candidate.boatSupport(candidate.navigation_.helmStanding)
        || candidate.boatSupport(candidate.navigation_.spawn)
        || candidate.boatSupport(candidate.navigation_.dockBoarding)) {
        error = "Cove boarding/helm positions must match dock and boat collision ownership"; return false;
    }
    const auto look = candidate.navigation_.lookTarget - candidate.navigation_.spawn;
    if (!finite(look) || std::hypot(look.x, look.z) < .01) { error = "Cove starting view is invalid"; return false; }
    candidate.reset();
    *this = std::move(candidate);
    error.clear();
    return true;
}

bool CovePlayer::setStaticObstacles(std::span<const StaticObstacle> boxes) noexcept {
    if(boxes.size()>staticObstacles_.size())return false;
    for(const auto& b:boxes)if(!finite(b.minimum)||!finite(b.maximum)
        ||glm::any(glm::greaterThanEqual(b.minimum,b.maximum)))return false;
    std::copy(boxes.begin(),boxes.end(),staticObstacles_.begin());staticObstacleCount_=boxes.size();return true;
}

namespace {
bool rigidPose(const glm::dmat4& pose) noexcept {
    for(int c=0;c<4;++c)for(int r=0;r<4;++r)if(!std::isfinite(pose[c][r]))return false;
    if(pose[0][3]!=0||pose[1][3]!=0||pose[2][3]!=0||pose[3][3]!=1)return false;
    const auto rotation=glm::dmat3(pose);const auto unit=glm::transpose(rotation)*rotation;
    for(int c=0;c<3;++c)for(int r=0;r<3;++r)if(std::abs(unit[c][r]-(c==r?1.:0.))>1e-5)return false;
    return std::abs(glm::determinant(rotation)-1)<1e-5;
}
}

bool CovePlayer::setSceneObstacles(std::span<const SceneObstacle> obstacles,uint64_t observedTick) noexcept {
    if(obstacles.size()>sceneObstacles_.size()||observedTick!=geometryTick_)return false;
    for(const auto& b:obstacles)if(!finite(b.minimum)||!finite(b.maximum)
        ||glm::any(glm::greaterThanEqual(b.minimum,b.maximum))||!rigidPose(b.sceneFromObstacle))return false;
    if(sceneObstaclePacketBound_&&observedTick==sceneObstacleTick_) {
        if(obstacles.size()!=sceneObstacleCount_)return false;
        for(size_t i=0;i<obstacles.size();++i)if(obstacles[i].minimum!=sceneObstacles_[i].minimum
            ||obstacles[i].maximum!=sceneObstacles_[i].maximum||obstacles[i].sceneFromObstacle!=sceneObstacles_[i].sceneFromObstacle)return false;
        return true;
    }
    previousSceneObstacles_=sceneObstacles_;previousSceneObstacleCount_=sceneObstacleCount_;
    std::copy(obstacles.begin(),obstacles.end(),sceneObstacles_.begin());sceneObstacleCount_=obstacles.size();
    sceneObstacleTick_=observedTick;sceneObstaclePacketBound_=true;return true;
}

bool CovePlayer::bindBoatRoots(std::span<const BoatRoot> roots,std::span<const BoatPart> parts,
    construction::DurableId helmRoot) noexcept {
    if(roots.empty()||roots.size()>roots_.size()||parts.empty()||parts.size()>assets::kMaximumFixturePlacements)return false;
    const auto find=[&](construction::DurableId key)->std::optional<uint8_t>{
        for(size_t i=0;i<roots.size();++i)if(roots[i].key==key)return static_cast<uint8_t>(i);
        return {};
    };
    for(size_t i=0;i<roots.size();++i) {
        if(!construction::isValid(roots[i].key)||!rigidPose(roots[i].sceneFromBoat)
            ||!finite(roots[i].originVelocity)||!finite(roots[i].angularVelocity)
            ||glm::any(glm::greaterThan(glm::abs(roots[i].originVelocity),glm::dvec3(150)))
            ||glm::any(glm::greaterThan(glm::abs(roots[i].angularVelocity),glm::dvec3(40)))
            ||roots[i].observedTick!=roots[0].observedTick)return false;
        for(size_t j=0;j<i;++j)if(roots[j].key==roots[i].key)return false;
    }
    const auto helm=find(helmRoot);if(!helm)return false;
    std::array<const BoatPart*,assets::kMaximumFixturePlacements> slots{};std::array<bool,assets::kMaximumFixturePlacements> used{};
    for(const auto& part:parts) {
        const auto root=find(part.root);
        if(part.placement>=slots.size()||slots[part.placement]||!root||!construction::isValid(part.part)
            ||part.part.world!=helmRoot.world)return false;
        for(const auto& previous:slots)if(previous&&previous->part==part.part)return false;
        slots[part.placement]=&part;used[*root]=true;
    }
    for(size_t i=0;i<roots.size();++i)if(!used[i]||roots[i].key.world!=helmRoot.world)return false;
    for(const auto& b:boxes_)if(b.boat&&(b.placement>=slots.size()||!slots[b.placement]))return false;
    for(const auto& part:parts)if(std::none_of(boxes_.begin(),boxes_.end(),[&](const auto& b){return b.boat&&b.placement==part.placement;}))return false;
    const auto support=[&](glm::dvec3 point)->std::optional<uint8_t>{
        for(const auto& b:boxes_)if(b.boat&&std::abs(point.y-b.max.y)<=.02
            &&point.x+radius>b.min.x&&point.x-radius<b.max.x&&point.z+radius>b.min.z&&point.z-radius<b.max.z)
            return find(slots[b.placement]->root);
        return {};
    };
    const auto boarding=support(navigation_.boatBoarding);
    if(!boarding)return false;
    std::optional<uint8_t> rider=helm;
    if(onBoat_) {
        const auto previous=state().root;
        rider=rootCount_?find(previous):support(feet_);
        if(!rider)return false;
    }
    for(auto& b:boxes_)if(b.boat){b.root=*find(slots[b.placement]->root);b.part=slots[b.placement]->part;}
    std::copy(roots.begin(),roots.end(),roots_.begin());rootCount_=roots.size();
    helmRoot_=*helm;boardingRoot_=*boarding;riderRoot_=*rider;
    sceneFromBoat_=roots_[helmRoot_].sceneFromBoat;dynamicBoat_=true;
    previousRoots_=pendingRoots_=roots_;geometryTick_=advancedGeometryTick_=roots[0].observedTick;
    pendingGeometryTick_=geometryTick_;pendingRootMask_=0;return true;
}

bool CovePlayer::setBoatRootTransform(construction::DurableId key,const glm::dmat4& pose) noexcept {
    if(!rigidPose(pose))return false;
    for(size_t i=0;i<rootCount_;++i)if(roots_[i].key==key) {
        if(onBoat_&&riderRoot_==i)facingYaw_=turnedHeading(facingYaw_,roots_[i].sceneFromBoat,pose);
        roots_[i].sceneFromBoat=pose;if(i==helmRoot_)sceneFromBoat_=pose;
        geometryTick_=pendingGeometryTick_=advancedGeometryTick_=0;pendingRootMask_=0;return true;
    }
    return false;
}

bool CovePlayer::setBoatRootMotion(construction::DurableId key,const glm::dmat4& pose,
    glm::dvec3 originVelocity,glm::dvec3 angularVelocity,uint64_t observedTick) noexcept {
    if(!rootCount_||!observedTick||!rigidPose(pose)||!finite(originVelocity)||!finite(angularVelocity)
        ||glm::any(glm::greaterThan(glm::abs(originVelocity),glm::dvec3(150)))
        ||glm::any(glm::greaterThan(glm::abs(angularVelocity),glm::dvec3(40)))
        ||observedTick<geometryTick_||observedTick<pendingGeometryTick_)return false;
    size_t index=0;while(index<rootCount_&&roots_[index].key!=key)++index;
    if(index==rootCount_)return false;
    const auto same=[&](const BoatRoot& root){return root.sceneFromBoat==pose
        &&root.originVelocity==originVelocity&&root.angularVelocity==angularVelocity;};
    if(observedTick==geometryTick_)return same(roots_[index]);
    if(observedTick!=pendingGeometryTick_){pendingRoots_=roots_;pendingRootMask_=0;pendingGeometryTick_=observedTick;}
    const uint32_t bit=uint32_t{1}<<index;
    if((pendingRootMask_&bit)&&!same(pendingRoots_[index]))return false;
    pendingRoots_[index]={key,pose,originVelocity,angularVelocity,observedTick};pendingRootMask_|=bit;
    const uint32_t complete=rootCount_==32?UINT32_MAX:(uint32_t{1}<<rootCount_)-1;
    if(pendingRootMask_==complete) {
        previousRoots_=roots_;roots_=pendingRoots_;geometryTick_=observedTick;
        sceneFromBoat_=roots_[helmRoot_].sceneFromBoat;pendingRootMask_=0;
    }
    return true;
}

void CovePlayer::setBoatTransform(const glm::dmat4& transform) noexcept {
    if(!rigidPose(transform))return;
    if(onBoat_&&(!rootCount_||riderRoot_==helmRoot_))facingYaw_=turnedHeading(facingYaw_,riderTransform(),transform);
    sceneFromBoat_=transform;dynamicBoat_=true;geometryTick_=pendingGeometryTick_=advancedGeometryTick_=0;pendingRootMask_=0;
    if(rootCount_)roots_[helmRoot_].sceneFromBoat=transform;
}

glm::dvec3 CovePlayer::pointVelocity(uint8_t root,glm::dvec3 point) const noexcept {
    if(!rootCount_)return {};
    const auto& motion=roots_[root];
    return motion.originVelocity+glm::cross(motion.angularVelocity,point-glm::dvec3(motion.sceneFromBoat[3]));
}

CovePlayer::Contact CovePlayer::nearestContact(glm::dvec3 point) const noexcept {
    Contact closest{std::numeric_limits<double>::infinity(),{0,1,0},nullptr};
    for(const auto& box:boxes_) {
        const auto pose=box.boat&&dynamicBoat_?(rootCount_?roots_[box.root].sceneFromBoat:sceneFromBoat_):glm::dmat4(1);
        const auto gap=capsuleBox(point,radius,height,box.min,box.max,pose);
        if(gap.distance<closest.gap)closest={gap.distance,gap.normal,&box};
    }
    for(size_t i=0;i<staticObstacleCount_;++i) {
        const auto& box=staticObstacles_[i];
        const auto gap=capsuleBox(point,radius,height,box.minimum,box.maximum,glm::dmat4(1));
        if(gap.distance<closest.gap)closest={gap.distance,gap.normal,nullptr};
    }
    for(size_t i=0;i<sceneObstacleCount_;++i) {
        const auto& box=sceneObstacles_[i];const auto gap=capsuleBox(point,radius,height,box.minimum,box.maximum,box.sceneFromObstacle);
        if(gap.distance<closest.gap)closest={gap.distance,gap.normal,nullptr};
    }
    return closest;
}

bool CovePlayer::sceneClear(glm::dvec3 point) const noexcept {
    if(nearestContact(point).gap<-1e-6)return false;
    if(groundSupport_)try {const double support=groundSupport_(point.x,point.z,radius);
        if(!std::isfinite(support)||point.y+1e-6<support)return false;
    }catch(...){return false;}
    return true;
}

glm::dvec3 CovePlayer::moveScene(glm::dvec3 start,glm::dvec3 displacement) const noexcept {
    for(int iteration=0;iteration<5;++iteration) {
        const auto contact=nearestContact(start);
        if(contact.gap>=skin-1e-7)break;
        start+=contact.normal*std::min(2.0,skin-contact.gap);
    }
    for(int iteration=0;iteration<4&&glm::length(displacement)>1e-8;++iteration) {
        const double length=glm::length(displacement);SweepResult nearest{true,false,false,length,{0,0,0}};
        for(const auto& box:boxes_) {
            const auto pose=box.boat&&dynamicBoat_?(rootCount_?roots_[box.root].sceneFromBoat:sceneFromBoat_):glm::dmat4(1);
            const auto hit=castCapsule(start,start+displacement,radius,height,box.min,box.max,pose,skin);
            if(hit.hit&&hit.distance<nearest.distance)nearest=hit;
        }
        for(size_t i=0;i<staticObstacleCount_;++i) {
            const auto& box=staticObstacles_[i];
            const auto hit=castCapsule(start,start+displacement,radius,height,box.minimum,box.maximum,glm::dmat4(1),skin);
            if(hit.hit&&hit.distance<nearest.distance)nearest=hit;
        }
        for(size_t i=0;i<sceneObstacleCount_;++i) {
            const auto& box=sceneObstacles_[i];
            const auto hit=castCapsule(start,start+displacement,radius,height,box.minimum,box.maximum,box.sceneFromObstacle,skin);
            if(hit.hit&&hit.distance<nearest.distance)nearest=hit;
        }
        if(terrainSweep_) {
            SweepResult hit;
            try {hit=terrainSweep_(start+glm::dvec3(0,radius,0),start+displacement+glm::dvec3(0,radius,0),radius);}catch(...){break;}
            if(!hit.complete||!std::isfinite(hit.distance)||hit.distance<0||hit.distance>length+1e-6
                ||(hit.hit&&(!finite(hit.normal)||glm::length(hit.normal)<.5)))break;
            if(hit.hit&&hit.distance<nearest.distance)nearest=hit;
        }
        if(!nearest.hit){start+=displacement;break;}
        start+=displacement*(nearest.distance/length);
        displacement*=1-nearest.distance/length;
        if(!nearest.complete||glm::length(nearest.normal)<.5)break;
        displacement-=nearest.normal*std::min(0.0,glm::dot(displacement,nearest.normal));
    }
    return start;
}

CovePlayer::SweepResult CovePlayer::sweepSphere(glm::dvec3 start,glm::dvec3 end,
    double sphereRadius,uint64_t expectedTick) const noexcept {
    if(!ground_||!terrainSweep_||!finite(start)||!finite(end)||!std::isfinite(sphereRadius)
        ||sphereRadius<=0||sphereRadius>4||(expectedTick&&expectedTick!=geometryTick_)
        ||(geometryTick_&&(!sceneObstaclePacketBound_||sceneObstacleTick_!=geometryTick_)))return {};
    const double length=glm::length(end-start);if(!std::isfinite(length)||length>128)return {};
    SweepResult nearest;
    try {nearest=terrainSweep_(start,end,sphereRadius);}catch(...){return {};}
    if(!nearest.complete||!std::isfinite(nearest.distance)||nearest.distance<0||nearest.distance>length+1e-6
        ||(nearest.hit&&(!finite(nearest.normal)||glm::length(nearest.normal)<.5)))return {};
    if(!nearest.hit)nearest.distance=length;
    const auto feet=start-glm::dvec3(0,sphereRadius,0),endFeet=end-glm::dvec3(0,sphereRadius,0);
    const auto merge=[&](SweepResult hit) {
        if(!hit.complete){nearest.complete=false;return;}
        if(hit.hit&&(!nearest.hit||hit.distance<nearest.distance
            ||(hit.startOverlapped&&!nearest.startOverlapped&&hit.distance<=nearest.distance)))nearest=hit;
    };
    for(const auto& box:boxes_) {
        const auto pose=box.boat&&dynamicBoat_?(rootCount_?roots_[box.root].sceneFromBoat:sceneFromBoat_):glm::dmat4(1);
        merge(castCapsule(feet,endFeet,sphereRadius,2*sphereRadius,box.min,box.max,pose));
        if(!nearest.complete)return nearest;
    }
    for(size_t i=0;i<staticObstacleCount_;++i) {
        const auto& box=staticObstacles_[i];merge(castCapsule(feet,endFeet,sphereRadius,2*sphereRadius,box.minimum,box.maximum,glm::dmat4(1)));
        if(!nearest.complete)return nearest;
    }
    for(size_t i=0;i<sceneObstacleCount_;++i) {
        const auto& box=sceneObstacles_[i];merge(castCapsule(feet,endFeet,sphereRadius,2*sphereRadius,box.minimum,box.maximum,box.sceneFromObstacle));
        if(!nearest.complete)return nearest;
    }
    return nearest;
}

bool CovePlayer::collisionApplies(const Box& b) const noexcept {
    if(!dynamicBoat_)return true;
    if(b.boat!=onBoat_)return false;
    return !onBoat_||!rootCount_||b.root==riderRoot_;
}

construction::DurableId CovePlayer::supportingPart() const noexcept {
    if(!onBoat_||!rootCount_||(mode_!=Mode::Walking&&mode_!=Mode::Helm))return {};
    const auto point=feet();
    for(const auto& b:boxes_)if(b.boat&&b.root==riderRoot_) {
        const auto contact=capsuleBox(point,radius,height,b.min,b.max,riderTransform());
        if(std::abs(contact.distance-skin)<.025&&contact.normal.y>=.7071067811865476)return b.part;
    }
    return {};
}

bool CovePlayer::clear(glm::dvec3 p) const noexcept {
    const auto lo = p + glm::dvec3(-radius, 0, -radius);
    const auto hi = p + glm::dvec3(radius, height, radius);
    for (const auto& b : boxes_) {
        if (!collisionApplies(b)) continue;
        if (lo.x < b.max.x - 1e-8 && hi.x > b.min.x + 1e-8 &&
            lo.y < b.max.y - 1e-8 && hi.y > b.min.y + 1e-8 &&
            lo.z < b.max.z - 1e-8 && hi.z > b.min.z + 1e-8) return false;
    }
    if(!onBoat_)for(size_t i=0;i<staticObstacleCount_;++i){
        const auto& b=staticObstacles_[i];
        if(glm::all(glm::lessThan(lo,b.maximum-glm::dvec3(1e-8)))
            &&glm::all(glm::greaterThan(hi,b.minimum+glm::dvec3(1e-8))))return false;
    }
    return true;
}

double CovePlayer::groundHeight(glm::dvec3 p) const {
    if(groundSupport_) {const double value=groundSupport_(p.x,p.z,radius);
        return std::isfinite(value)?value:std::numeric_limits<double>::infinity();}
    double top = -std::numeric_limits<double>::infinity();
    for (double x : {-radius, 0.0, radius}) for (double z : {-radius, 0.0, radius}) {
        const double h = ground_(p.x + x, p.z + z);
        if (std::isfinite(h)) top = std::max(top, h);
    }
    return top;
}

bool CovePlayer::supported(glm::dvec3 p) const {
    if (std::abs(p.y - groundHeight(p)) <= .02) return true;
    for (const auto& b : boxes_) {
        if (!collisionApplies(b)) continue;
        if (std::abs(p.y - b.max.y) <= .02 && p.x + radius > b.min.x && p.x - radius < b.max.x
            && p.z + radius > b.min.z && p.z - radius < b.max.z) return true;
    }
    if(!onBoat_)for(size_t i=0;i<staticObstacleCount_;++i){
        const auto& b=staticObstacles_[i];
        if(std::abs(p.y-b.maximum.y)<=.02&&p.x+radius>b.minimum.x&&p.x-radius<b.maximum.x
            &&p.z+radius>b.minimum.z&&p.z-radius<b.maximum.z)return true;
    }
    return false;
}

bool CovePlayer::boatSupport(glm::dvec3 p) const noexcept {
    if(dynamicBoat_&&!onBoat_)return false;
    for (const auto& b : boxes_) if (b.boat && (!rootCount_||b.root==riderRoot_) && std::abs(p.y - b.max.y) <= .02
        && p.x + radius > b.min.x && p.x - radius < b.max.x
        && p.z + radius > b.min.z && p.z - radius < b.max.z) return true;
    return false;
}

bool CovePlayer::settleSupport(glm::dvec3& point,double distance,bool allowSnap) {
    constexpr double walkable=.7071067811865476;
    const double rise=allowSnap?.025:0;
    const auto start=point+glm::dvec3(0,rise,0),end=point-glm::dvec3(0,distance,0);
    const double length=rise+distance;
    SweepResult nearest{true,false,false,length,{0,1,0}};const Box* support=nullptr;
    const auto consider=[&](const Box* box,glm::dvec3 lo,glm::dvec3 hi,const glm::dmat4& pose) {
        auto hit=castCapsule(start,end,radius,height,lo,hi,pose,skin);
        if(hit.complete&&hit.hit&&!hit.startOverlapped&&hit.normal.y>=walkable
            &&(!nearest.hit||hit.distance<nearest.distance)) {nearest=hit;support=box;}
    };
    for(const auto& box:boxes_) {
        const auto pose=box.boat&&dynamicBoat_?(rootCount_?roots_[box.root].sceneFromBoat:sceneFromBoat_):glm::dmat4(1);
        consider(&box,box.min,box.max,pose);
    }
    for(size_t i=0;i<staticObstacleCount_;++i)consider(nullptr,staticObstacles_[i].minimum,staticObstacles_[i].maximum,glm::dmat4(1));
    for(size_t i=0;i<sceneObstacleCount_;++i)consider(nullptr,sceneObstacles_[i].minimum,sceneObstacles_[i].maximum,sceneObstacles_[i].sceneFromObstacle);
    const double terrain=groundHeight(point)+skin;
    if(terrain<=start.y+1e-7&&terrain>=end.y-1e-7&&(!nearest.hit||start.y-terrain<nearest.distance)) {
        nearest={true,true,false,std::max(0.0,start.y-terrain),{0,1,0}};support=nullptr;
    }
    if(!nearest.hit)return false;
    auto candidate=start-glm::dvec3(0,nearest.distance,0);
    if(!sceneClear(candidate)||candidate.y<-.8)return false;
    point=candidate;supportNormal_=nearest.normal;
    onBoat_=support&&support->boat;
    if(onBoat_)riderRoot_=support->root;
    feet_=onBoat_?glm::dvec3(glm::inverse(riderTransform())*glm::dvec4(point,1)):point;
    return true;
}

void CovePlayer::leaveSupport(glm::dvec3 point,glm::dvec3 relativeVelocity) noexcept {
    worldVelocity_=(onBoat_?pointVelocity(riderRoot_,point):glm::dvec3(0))+relativeVelocity;
    worldVelocity_=glm::clamp(worldVelocity_,glm::dvec3(-150),glm::dvec3(150));
    feet_=point;onBoat_=false;mode_=Mode::Airborne;verticalSpeed_=worldVelocity_.y;supportNormal_={0,1,0};
}

void CovePlayer::reset() noexcept {
    feet_ = navigation_.spawn + glm::dvec3(0, skin, 0);
    verticalSpeed_ = accumulator_ = 0;worldVelocity_={0,0,0};supportNormal_={0,1,0};
    const auto direction=navigation_.lookTarget-navigation_.spawn;
    facingYaw_=std::atan2(-direction.x,-direction.z);
    tick_ = interactions_ = 0;advancedGeometryTick_=geometryTick_;
    mode_ = Mode::Walking;
    onBoat_ = pendingJump_ = pendingInteraction_ = false;
}

CovePlayer::State CovePlayer::state() const noexcept {
    return {feet_,verticalSpeed_,tick_,interactions_,mode_,onBoat_,onBoat_&&rootCount_?roots_[riderRoot_].key:construction::DurableId{},
        1,worldVelocity_,facingYaw_};
}

bool CovePlayer::restore(const State& saved) {
    const bool modern=saved.locomotionVersion==1;
    if (!ground_ || saved.locomotionVersion>1||!finite(saved.feet) || glm::any(glm::greaterThan(glm::abs(saved.feet),glm::dvec3(1'000'000)))
        || !std::isfinite(saved.verticalSpeed) || saved.verticalSpeed < (modern?-150:-30) || saved.verticalSpeed > (modern?150:6)
        || saved.mode<Mode::Walking || saved.mode>Mode::Helm || (saved.onBoat && !dynamicBoat_)
        || ((saved.mode==Mode::Walking||saved.mode==Mode::Helm)&&saved.verticalSpeed!=0)
        || (!modern&&saved.mode!=Mode::Airborne&&saved.verticalSpeed!=0)
        || (modern&&(!finite(saved.worldVelocity)||glm::any(glm::greaterThan(glm::abs(saved.worldVelocity),glm::dvec3(150)))
            ||!std::isfinite(saved.facingYaw)||std::abs(saved.facingYaw)>std::numbers::pi
            ||((saved.mode==Mode::Airborne||saved.mode==Mode::Swimming)&&saved.verticalSpeed!=saved.worldVelocity.y)
            ||(saved.mode==Mode::Airborne&&saved.onBoat)))
        || (saved.mode==Mode::Helm && (!saved.onBoat
            || glm::length(saved.feet-navigation_.helmStanding-glm::dvec3(0,skin,0))>(modern?.5:1e-6)))
        || (saved.mode==Mode::Swimming && (saved.onBoat || std::abs(saved.feet.y+.8)>1e-6))) return false;
    CovePlayer candidate=*this;
    if(saved.onBoat&&rootCount_) {
        const auto root=std::find_if(roots_.begin(),roots_.begin()+static_cast<std::ptrdiff_t>(rootCount_),[&](const auto& r){return r.key==saved.root;});
        if(root==roots_.begin()+static_cast<std::ptrdiff_t>(rootCount_))return false;
        candidate.riderRoot_=static_cast<uint8_t>(root-roots_.begin());
        if(saved.mode==Mode::Helm&&candidate.riderRoot_!=helmRoot_)return false;
    } else if(saved.root!=construction::DurableId{})return false;
    candidate.feet_=saved.feet;candidate.verticalSpeed_=saved.verticalSpeed;
    candidate.tick_=saved.tick;candidate.interactions_=saved.interactions;
    candidate.mode_=saved.mode;candidate.onBoat_=saved.onBoat;
    candidate.worldVelocity_=modern?saved.worldVelocity:glm::dvec3(0,saved.verticalSpeed,0);
    candidate.facingYaw_=modern?saved.facingYaw:0;
    if(!modern&&saved.onBoat) {
        const auto point=candidate.feet();
        candidate.worldVelocity_=candidate.pointVelocity(candidate.riderRoot_,point)
            +glm::dmat3(candidate.riderTransform())*glm::dvec3(0,saved.verticalSpeed,0);
        if(saved.mode==Mode::Airborne) {candidate.feet_=point;candidate.onBoat_=false;candidate.verticalSpeed_=candidate.worldVelocity_.y;}
    }
    if(!finite(candidate.worldVelocity_)||glm::any(glm::greaterThan(glm::abs(candidate.worldVelocity_),glm::dvec3(150))))return false;
    const auto savedRider=candidate.riderRoot_;
    auto point=candidate.feet();
    try {
        if (!candidate.sceneClear(point)||point.y+skin<candidate.groundHeight(point))return false;
        if(saved.mode==Mode::Walking||saved.mode==Mode::Helm) {
            // Validate without moving the durable saved feet during resume.
            auto contact=point;
            if(!candidate.settleSupport(contact,.025,true)||glm::length(contact-point)>.025
                ||candidate.onBoat_!=saved.onBoat||(saved.onBoat&&candidate.riderRoot_!=savedRider))return false;
            candidate.feet_=saved.feet;
        }
    }catch(...){return false;}
    candidate.accumulator_=0;candidate.pendingJump_=candidate.pendingInteraction_=false;
    candidate.advancedGeometryTick_=candidate.geometryTick_;
    *this=std::move(candidate);return true;
}

CovePlayer::Interaction CovePlayer::interaction() const noexcept {
    if (mode_ == Mode::Helm) return Interaction::LeaveHelm;
    if (mode_ != Mode::Walking&&mode_!=Mode::Swimming) return Interaction::None;
    if (onBoat_) {
        if ((!rootCount_||riderRoot_==helmRoot_)&&glm::length(feet_ - navigation_.helmStanding) < .85) return Interaction::UseHelm;
        if ((!rootCount_||riderRoot_==boardingRoot_)&&glm::length(feet_ - navigation_.boatBoarding) < .9
            && glm::length(feet() - navigation_.dockBoarding) < 3.2) return Interaction::ReturnToDock;
    } else {
        const auto board=glm::dvec3(boardingTransform()*glm::dvec4(navigation_.boatBoarding,1));
        const bool alongside=mode_==Mode::Walking&&glm::length(feet_ - navigation_.dockBoarding)<.9
            &&glm::length(board-feet_)<3.2;
        const bool fromWater=mode_==Mode::Swimming&&std::hypot(board.x-feet_.x,board.z-feet_.z)<1.35
            &&board.y>=-.65&&board.y-feet_.y<2.5
            &&glm::length(pointVelocity(boardingRoot_,board)-worldVelocity_)<3;
        if(alongside||fromWater)return Interaction::Board;
    }
    return Interaction::None;
}

bool CovePlayer::requestInteraction() noexcept {
    if (pendingInteraction_ || interaction() == Interaction::None) return false;
    pendingInteraction_ = true;return true;
}

void CovePlayer::advance(double seconds, Input input) {
    if (!ground_ || !std::isfinite(seconds) || seconds < 0 || !std::isfinite(input.movement.x)
        || !std::isfinite(input.movement.y)) return;
    const double length = glm::length(input.movement);
    if (length > 1) input.movement /= length;
    pendingJump_ = pendingJump_ || input.jump;
    if(geometryTick_&&(!sceneObstaclePacketBound_||sceneObstacleTick_!=geometryTick_))return;
    accumulator_ += std::min(seconds, .25);
    const int steps=static_cast<int>((accumulator_+1e-12)/fixedStep);
    const bool interpolate=steps>0&&geometryTick_&&advancedGeometryTick_!=geometryTick_;
    const auto target=roots_;const auto targetScenery=sceneObstacles_;
    for(int index=0;index<steps;++index) {
        if(interpolate)for(size_t root=0;root<rootCount_;++root) {
            const double fraction=double(index+1)/steps;
            const auto& previous=previousRoots_[root];const auto& next=target[root];
            auto pose=glm::mat4_cast(glm::slerp(glm::quat_cast(glm::dmat3(previous.sceneFromBoat)),
                glm::quat_cast(glm::dmat3(next.sceneFromBoat)),fraction));
            pose[3]=glm::dvec4(glm::mix(glm::dvec3(previous.sceneFromBoat[3]),glm::dvec3(next.sceneFromBoat[3]),fraction),1);
            if(onBoat_&&riderRoot_==root)facingYaw_=turnedHeading(facingYaw_,index?roots_[root].sceneFromBoat:previous.sceneFromBoat,pose);
            roots_[root].sceneFromBoat=pose;
            roots_[root].originVelocity=glm::mix(previous.originVelocity,next.originVelocity,fraction);
            roots_[root].angularVelocity=glm::mix(previous.angularVelocity,next.angularVelocity,fraction);
            if(root==helmRoot_)sceneFromBoat_=pose;
        }
        if(interpolate&&sceneObstacleCount_==previousSceneObstacleCount_)for(size_t i=0;i<sceneObstacleCount_;++i) {
            const auto& before=previousSceneObstacles_[i];const auto& after=targetScenery[i];
            if(before.minimum!=after.minimum||before.maximum!=after.maximum)continue;
            const double fraction=double(index+1)/steps;
            auto pose=glm::mat4_cast(glm::slerp(glm::quat_cast(glm::dmat3(before.sceneFromObstacle)),
                glm::quat_cast(glm::dmat3(after.sceneFromObstacle)),fraction));
            pose[3]=glm::dvec4(glm::mix(glm::dvec3(before.sceneFromObstacle[3]),glm::dvec3(after.sceneFromObstacle[3]),fraction),1);
            sceneObstacles_[i].sceneFromObstacle=pose;
        }
        input.jump = pendingJump_;pendingJump_=false;step(input);
        accumulator_-=fixedStep;if(tick_!=UINT64_MAX)++tick_;
    }
    if(interpolate){roots_=target;sceneObstacles_=targetScenery;sceneFromBoat_=roots_[helmRoot_].sceneFromBoat;advancedGeometryTick_=geometryTick_;}
}

void CovePlayer::step(Input input) {
    if (pendingInteraction_) {
        pendingInteraction_=false;const auto action=interaction();
        const bool previousBoat=onBoat_;const auto previousRoot=riderRoot_;const auto previousFeet=feet_;
        auto target=feet();
        if(action==Interaction::Board)target=glm::dvec3(boardingTransform()*glm::dvec4(navigation_.boatBoarding+glm::dvec3(0,skin,0),1));
        if(action==Interaction::ReturnToDock)target=navigation_.dockBoarding+glm::dvec3(0,skin,0);
        if(action==Interaction::UseHelm)target=glm::dvec3(sceneFromBoat_*glm::dvec4(navigation_.helmStanding+glm::dvec3(0,skin,0),1));
        // Edge assistance must have clear standing room; it never bypasses
        // the ownership/root checks or reaches a fast or submerged craft.
        if(action!=Interaction::None&&sceneClear(target)&&settleSupport(target,.08,true)) {
            verticalSpeed_=0;worldVelocity_=onBoat_?pointVelocity(riderRoot_,target):glm::dvec3(0);
            if(interactions_!=UINT64_MAX)++interactions_;
            mode_=action==Interaction::UseHelm?Mode::Helm:Mode::Walking;
            if(mode_==Mode::Helm)facingYaw_=turnedHeading(0,glm::dmat4(1),sceneFromBoat_);
            return;
        }
        onBoat_=previousBoat;riderRoot_=previousRoot;feet_=previousFeet;
    }
    auto point=feet();
    const auto wanted=glm::dvec3(input.movement.x,0,input.movement.y);
    if(glm::length(wanted)>1e-8)facingYaw_=std::atan2(-wanted.x,-wanted.z);
    const bool wasSupported=mode_==Mode::Walking||mode_==Mode::Helm;
    if(wasSupported) {
        // Depenetration follows a rolled support with an upright capsule.
        point=moveScene(point,{});
        if(point.y<-.65||!settleSupport(point,.08,true))leaveSupport(point,mode_==Mode::Helm?glm::dvec3(0):wanted*3.6);
        else if(mode_==Mode::Helm) {
            verticalSpeed_=0;worldVelocity_=onBoat_?pointVelocity(riderRoot_,point):glm::dvec3(0);return;
        }
    }
    if(mode_==Mode::Walking) {
        auto tangent=wanted-supportNormal_*glm::dot(wanted,supportNormal_);
        if(glm::length(tangent)>1e-8)tangent*=glm::length(wanted)/glm::length(tangent);
        const auto relative=tangent*3.6;
        if(input.jump){leaveSupport(point,relative+glm::dvec3(0,6,0));}
        else {
            const auto previous=point;const auto displacement=relative*fixedStep;
            auto moved=moveScene(point,displacement);
            const double requested=glm::length(glm::dvec2(displacement.x,displacement.z));
            const double progress=glm::length(glm::dvec2(moved.x-point.x,moved.z-point.z));
            if(requested>1e-8&&progress+1e-6<requested) {
                const auto raised=moveScene(point,{0,stepHeight,0});
                if(raised.y>=point.y+stepHeight-1e-6) {
                    auto across=moveScene(raised,displacement);
                    if(glm::length(glm::dvec2(across.x-point.x,across.z-point.z))>progress+1e-6
                        &&settleSupport(across,stepHeight+.08,true))moved=across;
                }
            }
            const double terrain=groundHeight(moved)+skin;
            if(terrain>moved.y&&terrain-previous.y<=stepHeight+skin&&sceneClear({moved.x,terrain,moved.z}))moved.y=terrain;
            else if(terrain>moved.y+stepHeight)moved=previous;
            if(settleSupport(moved,stepHeight+.01,true)) {
                verticalSpeed_=0;worldVelocity_=(onBoat_?pointVelocity(riderRoot_,moved):glm::dvec3(0))+(moved-previous)/fixedStep;
                worldVelocity_=glm::clamp(worldVelocity_,glm::dvec3(-150),glm::dvec3(150));return;
            }
            // Walking off an edge inherits the supporting body's velocity once.
            leaveSupport(moved,relative);point=moved;
        }
    }
    if(mode_==Mode::Swimming) {
        const auto desired=wanted*2.;auto horizontal=glm::dvec3(worldVelocity_.x,0,worldVelocity_.z);
        const auto difference=desired-horizontal;const double change=glm::length(difference);
        if(change>0)horizontal+=difference*std::min(1.,8*fixedStep/change);
        worldVelocity_={horizontal.x,0,horizontal.z};
        auto moved=moveScene(point,worldVelocity_*fixedStep);moved.y=-.8;
        const double terrain=groundHeight(moved)+skin;
        if(terrain>=-.8&&terrain<=point.y+stepHeight&&sceneClear({moved.x,terrain,moved.z})) {
            moved.y=terrain;if(settleSupport(moved,.03,true)){mode_=Mode::Walking;verticalSpeed_=0;return;}
        }
        feet_=moved;onBoat_=false;verticalSpeed_=0;return;
    }
    // Air steering preserves inherited speed; releasing input never erases the
    // translating/turning platform contribution captured at takeoff.
    if(glm::length(wanted)>1e-8) {
        auto horizontal=glm::dvec3(worldVelocity_.x,0,worldVelocity_.z);
        const auto desired=wanted*std::max(3.6,glm::length(horizontal));const auto difference=desired-horizontal;
        const double change=glm::length(difference);
        if(change>0)horizontal+=difference*std::min(1.,12*fixedStep/change);
        worldVelocity_.x=horizontal.x;worldVelocity_.z=horizontal.z;
    }
    worldVelocity_.x=std::clamp(worldVelocity_.x,-150.,150.);worldVelocity_.z=std::clamp(worldVelocity_.z,-150.,150.);
    worldVelocity_.y=std::max(-150.,worldVelocity_.y-18*fixedStep);
    const auto displacement=worldVelocity_*fixedStep;
    const auto moved=moveScene(point,displacement);auto landing=moved;
    const double terrain=groundHeight(landing)+skin;
    if(worldVelocity_.y<=0&&landing.y<=terrain&&point.y>=terrain-.025)landing.y=terrain;
    bool descendingRelative=worldVelocity_.y<=0;
    if(!descendingRelative)for(const auto& box:boxes_)if(box.boat) {
        const auto pose=dynamicBoat_?(rootCount_?roots_[box.root].sceneFromBoat:sceneFromBoat_):glm::dmat4(1);
        const auto contact=capsuleBox(landing,radius,height,box.min,box.max,pose);
        if(contact.distance<=skin+.03&&contact.normal.y>=.7071067811865476
            &&glm::dot(worldVelocity_-pointVelocity(box.root,landing),contact.normal)<=0) {descendingRelative=true;break;}
    }
    if(descendingRelative&&settleSupport(landing,.03,true)) {
        mode_=Mode::Walking;verticalSpeed_=0;worldVelocity_=onBoat_?pointVelocity(riderRoot_,landing):glm::dvec3(0);return;
    }
    const auto achieved=(moved-point)/fixedStep;
    // Only remove blocked components. The free horizontal momentum persists.
    for(int axis=0;axis<3;++axis)if(std::abs(achieved[axis])+1e-6<std::abs(worldVelocity_[axis]))worldVelocity_[axis]=achieved[axis];
    feet_=moved;onBoat_=false;verticalSpeed_=worldVelocity_.y;
    if(feet_.y<-.8) {feet_.y=-.8;worldVelocity_.y=verticalSpeed_=0;mode_=Mode::Swimming;}
}

const char* CovePlayer::modeName(Mode mode) noexcept {
    switch (mode) { case Mode::Walking: return "walking"; case Mode::Airborne: return "airborne";
        case Mode::Swimming: return "swimming"; case Mode::Helm: return "helm"; }
    return "unknown";
}
const char* CovePlayer::interactionName(Interaction action) noexcept {
    switch (action) { case Interaction::None: return "none"; case Interaction::Board: return "board";
        case Interaction::ReturnToDock: return "dock"; case Interaction::UseHelm: return "helm";
        case Interaction::LeaveHelm: return "leave-helm"; }
    return "none";
}
} // namespace voxy::game::expedition
