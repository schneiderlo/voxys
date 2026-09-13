#include "game/adventure/adventure_player.hpp"
#include <algorithm>
#include <cmath>
#include <numbers>

namespace voxy::game::adventure {
namespace {
constexpr double skin=.005,stepHeight=.36;
bool finite(glm::dvec3 p) noexcept {return std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.z);}
}
bool AdventurePlayer::initialize(const AdventureSpatialQueries& queries,glm::dvec3 spawn,double waterHeight,double yaw) noexcept {
    if(!std::isfinite(waterHeight)||!std::isfinite(yaw)||!queries.clearCapsule(spawn,radius,height))return false;
    const double support=queries.supportHeight({spawn.x,spawn.z},radius,spawn.y+.01);
    if(!std::isfinite(support)||std::abs(spawn.y-support)> .025||spawn.y<waterHeight-.65)return false;
    queries_=&queries;state_={spawn,{},std::remainder(yaw,2*std::numbers::pi),0,Mode::Walking};
    waterHeight_=waterHeight;discardPendingInput();return true;
}
bool AdventurePlayer::restore(State value) noexcept {
    if(!queries_||!finite(value.feet)||!finite(value.velocity)||!std::isfinite(value.facingYaw)
        ||glm::any(glm::greaterThan(glm::abs(value.velocity),glm::dvec3(150)))
        ||value.mode<Mode::Walking||value.mode>Mode::Swimming||!queries_->clearCapsule(value.feet,radius,height))return false;
    if(value.mode==Mode::Walking) {
        const double support=queries_->supportHeight({value.feet.x,value.feet.z},radius,value.feet.y+.01);
        if(!std::isfinite(support)||std::abs(support-value.feet.y)>.025||value.feet.y<waterHeight_-.65)return false;
    }
    if(value.mode==Mode::Swimming&&std::abs(value.feet.y-(waterHeight_-.8))>.025)return false;
    value.facingYaw=std::remainder(value.facingYaw,2*std::numbers::pi);state_=value;discardPendingInput();return true;
}
glm::dvec3 AdventurePlayer::move(glm::dvec3 from,glm::dvec3 displacement) const noexcept {
    for(int i=0;i<4&&glm::length(displacement)>1e-8;++i) {
        const auto cast=queries_->sweepCapsule(from,from+displacement,radius,height);
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
void AdventurePlayer::advance(double seconds,Input input) noexcept {
    if(!queries_||!std::isfinite(seconds)||seconds<0||!std::isfinite(input.movement.x)||!std::isfinite(input.movement.y))return;
    const double magnitude=glm::length(input.movement);if(magnitude>1)input.movement/=magnitude;
    pendingJump_=pendingJump_||input.jump;accumulator_+=std::min(seconds,.25);
    for(int steps=0;steps<15&&accumulator_+1e-12>=fixedStep;++steps) {
        accumulator_-=fixedStep;input.jump=pendingJump_;pendingJump_=false;step(input);
        if(state_.tick!=UINT64_MAX)++state_.tick;
    }
}
void AdventurePlayer::step(Input input) noexcept {
    const auto before=state_.feet;const glm::dvec3 wanted(input.movement.x,0,input.movement.y);
    if(glm::length(wanted)>1e-8)state_.facingYaw=std::atan2(-wanted.x,-wanted.z);
    if(state_.mode==Mode::Walking) {
        const double support=queries_->supportHeight({before.x,before.z},radius,before.y+.015);
        if(!std::isfinite(support)||before.y-support>.03)state_.mode=Mode::Airborne;
        else if(input.jump){state_.mode=Mode::Airborne;state_.velocity=wanted*3.6+glm::dvec3(0,6,0);}
        else {
            const auto displacement=wanted*(3.6*fixedStep);auto point=move(before,displacement);
            const double wantedDistance=glm::length(displacement),actual=glm::length(glm::dvec2(point.x-before.x,point.z-before.z));
            if(wantedDistance>1e-8&&actual+1e-6<wantedDistance) {
                const auto raised=move(before,{0,stepHeight,0});
                if(raised.y>=before.y+stepHeight-1e-6) {
                    auto across=move(raised,displacement);
                    const double top=queries_->supportHeight({across.x,across.z},radius,before.y+stepHeight);
                    if(std::isfinite(top)&&top>=before.y-stepHeight&&top<=before.y+stepHeight
                        &&glm::length(glm::dvec2(across.x-before.x,across.z-before.z))>actual+1e-6
                        &&queries_->clearCapsule({across.x,top+skin,across.z},radius,height))point={across.x,top+skin,across.z};
                }
            }
            const double top=queries_->supportHeight({point.x,point.z},radius,point.y+.015);
            if(std::isfinite(top)&&point.y-top<=stepHeight+.01&&queries_->clearCapsule({point.x,top+skin,point.z},radius,height)) {
                point.y=top+skin;state_.feet=point;state_.velocity=(point-before)/fixedStep;return;
            }
            state_.feet=point;state_.mode=Mode::Airborne;state_.velocity=wanted*3.6;
        }
    }
    if(state_.mode==Mode::Swimming) {
        auto point=move(state_.feet,wanted*(2*fixedStep));
        const double top=queries_->supportHeight({point.x,point.z},radius,point.y+stepHeight);
        if(std::isfinite(top)&&top>=waterHeight_-.8&&queries_->clearCapsule({point.x,top+skin,point.z},radius,height)) {
            point.y=top+skin;state_.mode=Mode::Walking;
        } else point.y=waterHeight_-.8;
        state_.feet=point;state_.velocity=(point-before)/fixedStep;return;
    }
    if(glm::length(wanted)>1e-8) {
        const glm::dvec3 horizontal(state_.velocity.x,0,state_.velocity.z);const auto change=wanted*3.6-horizontal;
        const double length=glm::length(change);const auto next=horizontal+change*std::min(1.,12*fixedStep/std::max(length,1e-10));
        state_.velocity.x=next.x;state_.velocity.z=next.z;
    }
    state_.velocity.y=std::max(-150.,state_.velocity.y-18*fixedStep);
    const auto from=state_.feet,point=move(from,state_.velocity*fixedStep);
    const double top=queries_->supportHeight({point.x,point.z},radius,from.y+.01);
    if(state_.velocity.y<=0&&std::isfinite(top)&&point.y<=top+.02&&from.y>=top-.01
        &&queries_->clearCapsule({point.x,top+skin,point.z},radius,height)) {
        state_.feet={point.x,top+skin,point.z};state_.velocity={};state_.mode=Mode::Walking;return;
    }
    const auto achieved=(point-from)/fixedStep;
    for(int axis=0;axis<3;++axis)if(std::abs(achieved[axis])+1e-6<std::abs(state_.velocity[axis]))state_.velocity[axis]=achieved[axis];
    state_.feet=point;
    if(point.y<waterHeight_-.8){state_.feet.y=waterHeight_-.8;state_.velocity.y=0;state_.mode=Mode::Swimming;}
}
} // namespace voxy::game::adventure
