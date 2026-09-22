#include "game/expedition/cove_camera.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>

namespace voxy::game::expedition {
namespace {
template<glm::length_t N> bool finite(glm::vec<N,double> v) noexcept {
    for(glm::length_t i=0;i<N;++i)if(!std::isfinite(v[i]))return false;
    return true;
}
double wrap(double a) noexcept {return std::remainder(a,2*std::numbers::pi);}
glm::dvec3 back(double yaw,double elevation) noexcept {
    return {std::cos(elevation)*std::sin(yaw),std::sin(elevation),std::cos(elevation)*std::cos(yaw)};
}
bool validBounds(const CoveCamera::Bounds& b) noexcept {
    return finite(b.minimum)&&finite(b.maximum)
        &&glm::all(glm::lessThanEqual(glm::abs(b.minimum),glm::dvec3(1e12)))
        &&glm::all(glm::lessThanEqual(glm::abs(b.maximum),glm::dvec3(1e12)))
        &&glm::all(glm::lessThanEqual(b.minimum,b.maximum))
        &&glm::all(glm::lessThanEqual(b.maximum-b.minimum,glm::dvec3(100000)));
}
double loadDistance(const CoveCamera::Bounds& bounds,glm::dvec3 anchor,glm::dvec3 forward,
                    CoveCamera::Projection projection) noexcept {
    const auto right=glm::normalize(glm::cross(forward,glm::dvec3(0,1,0)));
    const auto up=glm::cross(right,forward);
    const double yScale=1/std::tan(projection.verticalFov*.5),xScale=yScale/projection.aspect;
    double distance=0;
    for(unsigned i=0;i<8;++i) {
        const glm::dvec3 corner{i&1?bounds.maximum.x:bounds.minimum.x,
            i&2?bounds.maximum.y:bounds.minimum.y,i&4?bounds.maximum.z:bounds.minimum.z};
        const auto p=corner-anchor;const auto z=glm::dot(p,forward);
        distance=std::max({distance,projection.nearPlane-z,
            std::abs(glm::dot(p,right))*xScale/.85-z,std::abs(glm::dot(p,up))*yScale/.85-z});
    }
    return distance;
}

// Closest point on a column (unbounded downward) or closed stud cylinder.
// Casting against its sphere dilation uses a separating plane at each closest
// point. Advancing by gap/closingSpeed cannot skip this convex primitive.
template<class Closest> CoveCamera::SweepResult castConvex(
    glm::dvec3 start,glm::dvec3 direction,double length,double radius,Closest closest) noexcept {
    constexpr double tolerance=1e-8;
    double travel=0;
    for(unsigned iteration=0;iteration<64;++iteration) {
        const auto p=start+direction*travel;
        const auto delta=p-closest(p);const double distance=glm::length(delta),gap=distance-radius;
        if(!std::isfinite(gap))return {};
        const auto normal=distance>tolerance?delta/distance:glm::dvec3(0,1,0);
        if(gap<=tolerance)return {true,true,travel==0&&gap< -tolerance,travel,normal};
        const double closing=-glm::dot(normal,direction);
        if(closing<=0||length==0)return {true,false,false,length,{0,0,0}};
        const double step=gap/closing;
        if(step>length-travel)return {true,false,false,length,{0,0,0}};
        if(step<=0||travel+step==travel)return {};
        travel+=step;
    }
    return {};
}
}

std::optional<double> CoveCamera::nearPlaneRadius(Projection p) noexcept {
    if(!std::isfinite(p.verticalFov)||!std::isfinite(p.aspect)||!std::isfinite(p.nearPlane)
        ||p.verticalFov<.2||p.verticalFov>2.6||p.aspect<.2||p.aspect>8
        ||p.nearPlane<.01||p.nearPlane>1)return std::nullopt;
    const double halfHeight=p.nearPlane*std::tan(p.verticalFov*.5);
    const double radius=std::sqrt(p.nearPlane*p.nearPlane+halfHeight*halfHeight*(1+p.aspect*p.aspect))+presentationSkin;
    if(!std::isfinite(radius)||radius>4)return std::nullopt;
    return radius;
}
bool CoveCamera::settings(Settings value) noexcept {
    if(value.mode!=Mode::Orbit&&value.mode!=Mode::Chase)return false;
    if(!std::isfinite(value.distanceLimit)||value.distanceLimit<minimumDistance
        ||value.distanceLimit>extendedMaximumDistance)return false;
    settings_=value;userDistance_=std::min(userDistance_,value.distanceLimit);return true;
}
void CoveCamera::reset() noexcept {
    pose_={};yaw_=0;elevation_=.32;userDistance_=4.8;releaseDistance_=0;
    orbitQuietSeconds_=0;initialized_=false;recentering_=false;
}
bool CoveCamera::setUserDistance(double metres) noexcept {
    if(!std::isfinite(metres)||metres<minimumDistance||metres>settings_.distanceLimit)return false;
    userDistance_=metres;return true;
}
bool CoveCamera::restoreOrbit(double yaw,double elevation,double distance) noexcept {
    if(!std::isfinite(yaw)||!std::isfinite(elevation)||!std::isfinite(distance)
        ||elevation<minimumElevation||elevation>maximumElevation
        ||distance<minimumDistance||distance>settings_.distanceLimit)return false;
    yaw_=wrap(yaw);elevation_=elevation;userDistance_=distance;
    initialized_=true;recentering_=false;orbitQuietSeconds_=0;releaseDistance_=0;pose_={};
    pose_.yaw=yaw_;pose_.elevation=elevation_;pose_.requestedDistance=userDistance_;
    return true;
}
CoveCamera::Result CoveCamera::update(const Target& target,const Input& input,Projection projection,
                                     Sweep sweep,double seconds) noexcept {
    const auto radius=nearPlaneRadius(projection);
    if(!finite(target.anchor)||glm::any(glm::greaterThan(glm::abs(target.anchor),glm::dvec3(1e12)))
        ||!std::isfinite(target.facingYaw)||!finite(input.orbitRadians)
        ||!std::isfinite(input.zoomSteps)||!std::isfinite(seconds)||seconds<0||!radius
        ||(target.load&&!validBounds(*target.load)))return Result::InvalidInput;
    const double dt=std::min(seconds,.1);
    if(!initialized_) {yaw_=wrap(target.facingYaw);initialized_=true;}
    const bool manual=input.active&&glm::any(glm::notEqual(input.orbitRadians,glm::dvec2(0)));
    if(input.active) {
        yaw_=wrap(yaw_+std::clamp(input.orbitRadians.x,-2*std::numbers::pi,2*std::numbers::pi));
        elevation_=std::clamp(elevation_+input.orbitRadians.y,minimumElevation,maximumElevation);
        userDistance_=std::clamp(userDistance_*std::exp(-.14*std::clamp(input.zoomSteps,-100.,100.)),minimumDistance,settings_.distanceLimit);
        if(manual){orbitQuietSeconds_=0;recentering_=false;}
        else orbitQuietSeconds_=std::min(orbitQuietSeconds_+dt,10.);
        if(input.recenter)recentering_=true;
        const bool automatic=settings_.mode==Mode::Chase&&!settings_.reducedMotion
            &&target.chaseActive&&orbitQuietSeconds_>=1.5;
        if(!manual&&(recentering_||automatic)) {
            const double error=wrap(target.facingYaw-yaw_);
            // Bounded angular speed and exponential settling prevent a mode,
            // helm or boat-heading change from switching sides in one frame.
            const double step=std::clamp(error*(-std::expm1(-4*dt)),-1.5*dt,1.5*dt);
            yaw_=wrap(yaw_+step);
            if(std::abs(error)<1e-4)recentering_=false;
        }
    } else {orbitQuietSeconds_=0;recentering_=false;} // No deferred swing on menu return.

    const auto backwards=back(yaw_,elevation_);const auto forward=-backwards;
    const double fit=settings_.frameLoad&&target.load?loadDistance(*target.load,target.anchor,forward,projection):0;
    const double requested=std::clamp(std::max(userDistance_,fit),minimumDistance,settings_.distanceLimit);
    Pose next;next.eye=target.anchor;next.viewTarget=target.anchor+forward;
    next.forward=forward;next.yaw=yaw_;next.elevation=elevation_;next.requestedDistance=requested;
    next.sphereRadius=*radius;next.geometryTick=target.geometryTick;
    next.loadFramed=!settings_.frameLoad||!target.load;
    // This certificate belongs only to this call and exact borrowed scene.
    // Old endpoint stability never certifies moving machinery or stale poses.
    const auto result=sweep.cast?sweep.cast(sweep.context,target.anchor,
        target.anchor+backwards*requested,*radius,target.geometryTick):SweepResult{};
    if(!result.complete||!std::isfinite(result.distance)||result.distance<0||result.distance>requested+1e-8
        ||(result.startOverlapped&&!result.hit)) {
        next.status=Result::GeometryUnavailable;pose_=next;releaseDistance_=0;return next.status;
    }
    if(result.startOverlapped) {
        next.status=Result::AnchorOverlapped;pose_=next;releaseDistance_=0;return next.status;
    }
    const double available=result.hit?std::max(0.,result.distance-contactPadding):requested;
    // Every released point lies on the freshly certified anchor-to-eye
    // segment. Interpolating world eyes would cut across obstructions on turns.
    if(target.discontinuity||!pose_.valid)releaseDistance_=available;
    else if(available<releaseDistance_)releaseDistance_=available;
    else releaseDistance_+=(available-releaseDistance_)*(-std::expm1(-6*dt));
    releaseDistance_=std::clamp(releaseDistance_,0.,available);
    next.distance=releaseDistance_;next.eye=target.anchor+backwards*next.distance;
    // Never pass identical eye/target to lookAt, even for a valid hit at zero.
    next.viewTarget=next.eye+forward*std::max(next.distance,1.);
    next.valid=true;next.hideAvatar=next.distance<.65;
    next.loadFramed=!settings_.frameLoad||!target.load||fit<=next.distance+1e-8;
    next.status=available<requested?Result::Obstructed:Result::Ready;
    pose_=next;return next.status;
}

CoveCamera::SweepResult sweepCoveTerrainSphere(const terrain::lego::Surface& surface,
    glm::dvec3 start,glm::dvec3 end,double radius,bool lego,uint32_t maximumVisitedCells) noexcept {
    using Result=CoveCamera::SweepResult;
    if(!surface.valid()||uint64_t(surface.width)*surface.height!=surface.samples.size()
        ||surface.width>1000000||surface.height>1000000
        ||surface.cellScale<1e-4f||!finite(start)||!finite(end)
        ||!std::isfinite(radius)||radius<=0||radius>4
        ||maximumVisitedCells==0||maximumVisitedCells>4096)return {};
    const auto displacement=end-start;const double length=glm::length(displacement);
    if(!std::isfinite(length)||length>128)return {};
    const auto direction=length>0?displacement/length:glm::dvec3(0);
    const glm::dvec2 origin(surface.origin());const double cell=surface.cellScale;
    const auto lo=(glm::min(glm::dvec2(start.x,start.z),glm::dvec2(end.x,end.z))-radius+origin)/cell;
    const auto hi=(glm::max(glm::dvec2(start.x,start.z),glm::dvec2(end.x,end.z))+radius+origin)/cell;
    if(!finite(lo)||!finite(hi))return {};
    // Clamp in double before integer conversion, including wholly outside
    // segments. Never overflow an index on an invalid or remote coordinate.
    const auto beginX=static_cast<uint32_t>(std::clamp(std::floor(lo.x)-1,0.,double(surface.width-1)));
    const auto beginZ=static_cast<uint32_t>(std::clamp(std::floor(lo.y)-1,0.,double(surface.height-1)));
    const auto endX=static_cast<uint32_t>(std::clamp(std::floor(hi.x)+1,0.,double(surface.width-1)));
    const auto endZ=static_cast<uint32_t>(std::clamp(std::floor(hi.y)+1,0.,double(surface.height-1)));
    if(uint64_t(endX-beginX)*uint64_t(endZ-beginZ)>maximumVisitedCells)return {};
    Result best{true,false,false,length,{0,0,0}};
    const auto accept=[&](Result result) {
        if(!result.complete)return false;
        if(result.hit&&(!best.hit||result.distance<best.distance||result.startOverlapped))best=result;
        return true;
    };
    const double floor=-double(surface.heightScale);
    if(!accept(castConvex(start,direction,length,radius,[&](glm::dvec3 p) {
        return glm::dvec3(p.x,std::min(p.y,floor),p.z);
    })))return {};
    const auto heightAt=[&](uint32_t x,uint32_t z) {
        return -double(surface.heightScale)+double(surface.samples[size_t(z)*surface.width+x])
            *(2*double(surface.heightScale)/65535.);
    };
    for(uint32_t z=beginZ;z<endZ;++z)for(uint32_t x=beginX;x<endX;++x) {
        const glm::dvec2 low{double(x)*cell-origin.x,double(z)*cell-origin.y};
        const auto high=low+glm::dvec2(cell);
        const double top=lego?double(surface.cellTop(static_cast<int>(x),static_cast<int>(z)))
            :std::max({heightAt(x,z),heightAt(x+1,z),heightAt(x,z+1),heightAt(x+1,z+1)});
        if(!accept(castConvex(start,direction,best.distance,radius,[&](glm::dvec3 p) {
            return glm::dvec3(std::clamp(p.x,low.x,high.x),std::min(p.y,top),std::clamp(p.z,low.y,high.y));
        })))return {};
        if(lego) {
            const auto center=low+glm::dvec2(.5*cell);
            const double studRadius=double(terrain::lego::kStudRadius)*cell;
            const double cap=top+double(terrain::lego::kStudHeight)*cell;
            if(!accept(castConvex(start,direction,best.distance,radius,[&](glm::dvec3 p) {
                const auto radial=glm::dvec2(p.x,p.z)-center;const double d=glm::length(radial);
                const auto point=center+radial*(d>studRadius?studRadius/d:1.);
                return glm::dvec3(point.x,std::clamp(p.y,top,cap),point.y);
            })))return {};
        }
    }
    return best;
}
} // namespace voxy::game::expedition
