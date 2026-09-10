#include "game/expedition/workshop_camera.hpp"
#include <algorithm>
#include <cmath>
#include <numbers>

namespace voxy::game::expedition {
namespace {
template<glm::length_t N> bool finite(glm::vec<N,double> value) {
    for(glm::length_t i=0;i<N;++i)if(!std::isfinite(value[i]))return false;
    return true;
}
}
glm::dvec3 WorkshopCamera::back() const noexcept {
    return {std::cos(elevation_)*std::sin(yaw_),std::sin(elevation_),std::cos(elevation_)*std::cos(yaw_)};
}
glm::dvec3 WorkshopCamera::right() const noexcept { return {-std::cos(yaw_),0,std::sin(yaw_)}; }
glm::dvec3 WorkshopCamera::up() const noexcept { return glm::cross(-back(),right()); }
glm::dvec3 WorkshopCamera::viewTarget() const noexcept {
    const glm::dvec2 center{(rectangle_.x+rectangle_.z)*.5,(rectangle_.y+rectangle_.w)*.5};
    return target_-right()*(center.x*distance_/projection_.x)-up()*(center.y*distance_/projection_.y);
}
glm::dvec3 WorkshopCamera::eye() const noexcept { return viewTarget()+back()*distance_; }
bool WorkshopCamera::viewport(glm::dvec2 projection,glm::dvec4 rectangle) noexcept {
    if(!finite(projection)||!finite(rectangle)||projection.x<=0||projection.y<=0
        ||rectangle.x< -1||rectangle.y< -1||rectangle.z>1||rectangle.w>1
        ||rectangle.z-rectangle.x<.1||rectangle.w-rectangle.y<.1)return false;
    projection_=projection;rectangle_=rectangle;return true;
}
bool WorkshopCamera::frame(WorkshopBounds bounds) noexcept {
    if(!finite(bounds.minimum)||!finite(bounds.maximum)
        ||glm::any(glm::greaterThan(bounds.minimum,bounds.maximum)))return false;
    const auto center=bounds.minimum+(bounds.maximum-bounds.minimum)*.5;
    const double cx=(rectangle_.x+rectangle_.z)*.5,cy=(rectangle_.y+rectangle_.w)*.5;
    const auto r=right(),u=up(),forward=-back();double distance=minimumDistance;
    // Solve the four frustum inequalities for every actual bounds corner.
    // This fits asymmetric/tall builds into the unobstructed canvas rectangle,
    // including perspective depth, rather than assuming a sphere at screen center.
    for(unsigned corner=0;corner<8;++corner) {
        const glm::dvec3 p{corner&1?bounds.maximum.x:bounds.minimum.x,
            corner&2?bounds.maximum.y:bounds.minimum.y,corner&4?bounds.maximum.z:bounds.minimum.z};
        const auto delta=p-center;const double x=glm::dot(delta,r)*projection_.x,
            y=glm::dot(delta,u)*projection_.y,z=glm::dot(delta,forward);
        distance=std::max({distance,.25-z,(rectangle_.x*z-x)/(cx-rectangle_.x),
            (x-rectangle_.z*z)/(rectangle_.z-cx),(rectangle_.y*z-y)/(cy-rectangle_.y),
            (y-rectangle_.w*z)/(rectangle_.w-cy)});
    }
    if(!std::isfinite(distance)||distance>maximumDistance)return false;
    target_=center;distance_=distance;return true;
}
void WorkshopCamera::orbit(double yawDelta,double elevationDelta) noexcept {
    if(!std::isfinite(yawDelta)||!std::isfinite(elevationDelta))return;
    yaw_=std::remainder(yaw_+std::clamp(yawDelta,-10.,10.),2*std::numbers::pi);
    elevation_=std::clamp(elevation_+elevationDelta,-1.4,1.4);
}
void WorkshopCamera::zoom(double steps) noexcept {
    if(!std::isfinite(steps))return;
    distance_=std::clamp(distance_*std::exp(-.14*std::clamp(steps,-100.,100.)),minimumDistance,maximumDistance);
}
void WorkshopCamera::pan(glm::dvec2 pixels,double viewportHeight) noexcept {
    if(!finite(pixels)||!std::isfinite(viewportHeight)||viewportHeight<=0)return;
    const auto movement=(-right()*pixels.x+up()*pixels.y)*(2*distance_/(projection_.y*viewportHeight));
    const auto next=target_+movement;
    if(finite(next)&&glm::all(glm::lessThanEqual(glm::abs(next),glm::dvec3(100000))))target_=next;
}
} // namespace voxy::game::expedition
