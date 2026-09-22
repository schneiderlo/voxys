#include "game/adventure/adventure_player.hpp"
#include <algorithm>
#include <cmath>
#include <numbers>

namespace voxy::game::adventure {
namespace {
constexpr double skin=.005,stepHeight=.36;
bool finite(glm::dvec3 p) noexcept {return std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.z);}
}
bool AdventurePlayer::initialize(const AdventureSpatialQueries& queries,glm::dvec3 spawn,double waterHeight,double yaw,double scale,double baseRadius) noexcept {
    if(!std::isfinite(baseRadius)||baseRadius<radius||baseRadius>creativeRadius
        ||!std::isfinite(scale)||scale<1||scale>creativeScale
        ||!std::isfinite(waterHeight)||!std::isfinite(yaw)
        ||!queries.clearCapsule(spawn,baseRadius*scale,height*scale))return false;
    const double support=queries.supportHeight({spawn.x,spawn.z},baseRadius*scale,spawn.y+.01);
    const bool swimming=spawn.y<=waterHeight-swimImmersion*scale;
    if(!swimming&&(!std::isfinite(support)||std::abs(spawn.y-support)>.025))return false;
    scale_=scale;baseRadius_=baseRadius;movementScale_=std::sqrt(scale);
    queries_=&queries;state_={spawn,{},std::remainder(yaw,2*std::numbers::pi),0,swimming?Mode::Swimming:Mode::Walking};
    waterHeight_=waterHeight;contactVelocity_={};discardPendingInput();return true;
}
bool AdventurePlayer::restore(State value) noexcept {
    if(!queries_||!finite(value.feet)||!finite(value.velocity)||!std::isfinite(value.facingYaw)
        ||glm::any(glm::greaterThan(glm::abs(value.velocity),glm::dvec3(150)))
        ||value.mode<Mode::Walking||value.mode>Mode::Swimming||!queries_->clearCapsule(value.feet,bodyRadius(),bodyHeight()))return false;
    if(value.mode==Mode::Walking) {
        const double support=queries_->supportHeight({value.feet.x,value.feet.z},bodyRadius(),value.feet.y+.01);
        if(!std::isfinite(support)||std::abs(support-value.feet.y)>.025||value.feet.y<swimSurfaceHeight())return false;
    }
    if(value.mode==Mode::Swimming&&value.feet.y>swimSurfaceHeight()+.025)return false;
    value.facingYaw=std::remainder(value.facingYaw,2*std::numbers::pi);state_=value;contactVelocity_={};discardPendingInput();return true;
}
void AdventurePlayer::addContactImpulse(glm::dvec3 velocityChange) noexcept {
    if(!finite(velocityChange))return;
    // Keep the controlled figure upright. Horizontal momentum is resolved
    // through the same terrain/building sweeps as ordinary walking.
    velocityChange.y=0;
    contactVelocity_+=velocityChange;
    const double speed=glm::length(contactVelocity_);
    if(speed>12)contactVelocity_*=12/speed;
}
glm::dvec3 AdventurePlayer::move(glm::dvec3 from,glm::dvec3 displacement) const noexcept {
    for(int i=0;i<4&&glm::length(displacement)>1e-8;++i) {
        const auto cast=queries_->sweepCapsule(from,from+displacement,bodyRadius(),bodyHeight());
        if(!cast.complete||cast.startOverlapped)return from;
        if(!cast.hit)return from+displacement;
        const double length=glm::length(displacement),travel=std::clamp(cast.distance-skin,0.,length);
        const auto direction=displacement/length;from+=direction*travel;
        displacement-=direction*travel;
        const double blocked=glm::dot(displacement,cast.normal);
        if(blocked<0)displacement-=cast.normal*blocked;else return from;
    }
    return from;
}
glm::dvec3 AdventurePlayer::groundMove(glm::dvec3 from,glm::dvec3 displacement) const noexcept {
    const double climb=scale_==creativeScale?creativeStepHeight:stepHeight*scale_;
    auto point=move(from,displacement);
    const double wantedDistance=glm::length(displacement);
    const double actual=glm::length(glm::dvec2(point.x-from.x,point.z-from.z));
    if(wantedDistance>1e-8&&actual+1e-6<wantedDistance) {
        const auto raised=move(from,{0,climb,0});
        if(raised.y>=from.y+climb-1e-6) {
            const auto across=move(raised,displacement);
            const double top=queries_->supportHeight({across.x,across.z},bodyRadius(),from.y+climb);
            if(std::isfinite(top)&&top>=from.y-climb&&top<=from.y+climb
                &&glm::length(glm::dvec2(across.x-from.x,across.z-from.z))>actual+1e-6
                &&queries_->clearCapsule({across.x,top+skin,across.z},bodyRadius(),bodyHeight()))point={across.x,top+skin,across.z};
        }
    }
    return point;
}
void AdventurePlayer::swim(Input input) noexcept {
    const auto before=state_.feet;
    const double surface=swimSurfaceHeight();
    glm::dvec3 direction(input.movement.x,input.swimVertical,input.movement.y);
    const double magnitude=glm::length(direction);
    if(magnitude>1)direction/=magnitude;
    const auto desired=direction*(swimSpeed*movementScale_)+contactVelocity_;
    const auto change=desired-state_.velocity;
    state_.velocity+=change*std::min(1.,14*movementScale_*fixedStep/std::max(glm::length(change),1e-10));
    auto displacement=state_.velocity*fixedStep;
    // Cap the requested sweep, never teleport up through a ceiling or platform.
    displacement.y=std::min(displacement.y,surface-before.y);
    auto point=move(before,displacement);
    // Wade out using the same headroom and step sweeps as walking. Diving never
    // climbs a bank, and a submerged floor does not turn swimming into walking.
    const double exitHeight=surface+.01*scale_;
    if(before.y>=surface-.08*scale_&&input.swimVertical>=0) {
        // Use movement intent here: wall contact can reduce the accepted
        // velocity below the sweep skin before the foot reaches a bank's lip.
        const auto shore=groundMove(before,{desired.x*fixedStep,0,desired.z*fixedStep});
        const double top=queries_->supportHeight({shore.x,shore.z},bodyRadius(),shore.y+.015);
        if(std::isfinite(top)&&top>=exitHeight&&std::abs(shore.y-top)<=.025
            &&queries_->clearCapsule({shore.x,top+skin,shore.z},bodyRadius(),bodyHeight())) {
            point={shore.x,top+skin,shore.z};state_.mode=Mode::Walking;
        }
    }
    state_.feet=point;state_.velocity=(point-before)/fixedStep;
}
void AdventurePlayer::advance(double seconds,Input input) noexcept {
    if(!queries_||!std::isfinite(seconds)||seconds<0||!std::isfinite(input.movement.x)||!std::isfinite(input.movement.y)
        ||!std::isfinite(input.speedScale)||!std::isfinite(input.swimVertical))return;
    input.speedScale=std::clamp(input.speedScale,1.,2.);
    input.swimVertical=std::clamp(input.swimVertical,-1.,1.);
    pendingJump_=pendingJump_||input.jump;accumulator_+=std::min(seconds,.25);
    for(int steps=0;steps<15&&accumulator_+1e-12>=fixedStep;++steps) {
        accumulator_-=fixedStep;input.jump=pendingJump_;pendingJump_=false;step(input);
        contactVelocity_*=std::exp(-8*fixedStep);
        if(state_.tick!=UINT64_MAX)++state_.tick;
    }
}
void AdventurePlayer::step(Input input) noexcept {
    const auto before=state_.feet;glm::dvec3 wanted(input.movement.x,0,input.movement.y);
    if(glm::length(wanted)>1e-8)state_.facingYaw=std::atan2(-wanted.x,-wanted.z);
    const double surface=swimSurfaceHeight();
    if(state_.mode!=Mode::Swimming&&before.y<=surface)state_.mode=Mode::Swimming;
    if(state_.mode==Mode::Swimming){swim(input);return;}
    const double magnitude=glm::length(wanted);if(magnitude>1)wanted/=magnitude;
    if(state_.mode==Mode::Walking) {
        const double walkStepHeight=scale_==creativeScale?creativeStepHeight:stepHeight*scale_;
        const double support=queries_->supportHeight({before.x,before.z},bodyRadius(),before.y+.015);
        if(!std::isfinite(support)||before.y-support>.03)state_.mode=Mode::Airborne;
        else if(input.jump){state_.mode=Mode::Airborne;state_.velocity=wanted*(3.6*movementScale_*input.speedScale)+glm::dvec3(0,6*movementScale_,0);}
        else {
            const auto displacement=(wanted*(3.6*movementScale_*input.speedScale)+contactVelocity_)*fixedStep;auto point=groundMove(before,displacement);
            const double top=queries_->supportHeight({point.x,point.z},bodyRadius(),point.y+.015);
            if(std::isfinite(top)&&point.y-top<=walkStepHeight+.01&&queries_->clearCapsule({point.x,top+skin,point.z},bodyRadius(),bodyHeight())) {
                // Enter water at chest depth even while the ground remains
                // within step-down range; do not keep walking on the seabed.
                if(top+skin<=surface) {
                    point=move(before,{displacement.x,std::min(0.,surface-before.y),displacement.z});
                    state_.mode=Mode::Swimming;
                } else point.y=top+skin;
                state_.feet=point;state_.velocity=(point-before)/fixedStep;
                if(state_.mode==Mode::Swimming)state_.velocity.y=0;
                return;
            }
            state_.feet=point;state_.mode=Mode::Airborne;state_.velocity=wanted*(3.6*movementScale_*input.speedScale);
        }
    }
    state_.velocity+=contactVelocity_;contactVelocity_={};
    if(glm::length(wanted)>1e-8) {
        const glm::dvec3 horizontal(state_.velocity.x,0,state_.velocity.z);const auto change=wanted*(3.6*movementScale_*input.speedScale)-horizontal;
        const double length=glm::length(change);const auto next=horizontal+change*std::min(1.,12*movementScale_*fixedStep/std::max(length,1e-10));
        state_.velocity.x=next.x;state_.velocity.z=next.z;
    }
    state_.velocity.y=std::max(-150.,state_.velocity.y-18*fixedStep);
    const auto from=state_.feet;
    auto displacement=state_.velocity*fixedStep;
    if(from.y>=surface&&displacement.y<0)displacement.y=std::max(displacement.y,surface-from.y);
    const auto point=move(from,displacement);
    const double top=queries_->supportHeight({point.x,point.z},bodyRadius(),from.y+.01);
    if(state_.velocity.y<=0&&std::isfinite(top)&&point.y<=top+.02&&from.y>=top-.01
        &&queries_->clearCapsule({point.x,top+skin,point.z},bodyRadius(),bodyHeight())) {
        state_.feet={point.x,top+skin,point.z};state_.velocity={};state_.mode=Mode::Walking;return;
    }
    const auto achieved=(point-from)/fixedStep;
    for(int axis=0;axis<3;++axis)if(std::abs(achieved[axis])+1e-6<std::abs(state_.velocity[axis]))state_.velocity[axis]=achieved[axis];
    state_.feet=point;
    if(point.y<=surface){state_.velocity.y=0;state_.mode=Mode::Swimming;}
}
} // namespace voxy::game::adventure
