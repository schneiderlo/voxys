#pragma once

#include "physics/physics_world.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>
#include <glm/gtc/matrix_transform.hpp>

namespace voxy::game::adventure {

// Source frames from the actual 2527c01 assembly, at one unit per stud.
// The barrel mesh retains its original 15-degree tilt in shared root coordinates.
struct BuilderCannon {
    static constexpr glm::dvec3 pivot{0,1.799994707,.401407957};
    static constexpr double sourceElevation=std::numbers::pi/12;
    static constexpr double muzzleLength=3.5,maximumElevation=.34;
    static constexpr float ballRadius=.44f,shotSpeed=48;
    static constexpr uint64_t reloadTicks=60,lifetimeTicks=150;
    static constexpr size_t maximumShots=8;
    double heading=0,yaw=0,elevation=sourceElevation,recoil=0;
    uint64_t nextShotTick=0,shotsFired=0;
    struct Shot {physics::BodyHandle body{};uint64_t expires=0;};
    std::array<Shot,maximumShots> shots{};

    void aim(double horizontal,double vertical,double seconds) noexcept {
        if(!std::isfinite(seconds)||!std::isfinite(horizontal)||!std::isfinite(vertical))return;
        seconds=std::clamp(seconds,0.,.1);
        yaw=std::clamp(yaw+std::clamp(horizontal,-1.,1.)*seconds*.65,-.7,.7);
        elevation=std::clamp(elevation+std::clamp(vertical,-1.,1.)*seconds*.45,.02,maximumElevation);
        recoil*=std::exp(-12*seconds);
    }
    glm::dvec3 direction() const noexcept {
        return {std::sin(heading+yaw)*std::cos(elevation),std::sin(elevation),std::cos(heading+yaw)*std::cos(elevation)};
    }
    glm::dmat4 baseMatrix(glm::dvec3 feet) const {
        return glm::translate(glm::dmat4(1),feet)*glm::rotate(glm::dmat4(1),heading+yaw,glm::dvec3(0,1,0));
    }
    glm::dmat4 barrelMatrix(glm::dvec3 feet) const {
        const glm::dvec3 localDirection{0,std::sin(elevation),std::cos(elevation)};
        return baseMatrix(feet)*glm::translate(glm::dmat4(1),pivot-localDirection*recoil)
            *glm::rotate(glm::dmat4(1),-(elevation-sourceElevation),glm::dvec3(1,0,0))
            *glm::translate(glm::dmat4(1),-pivot);
    }
    glm::dvec3 muzzle(glm::dvec3 feet) const {
        return glm::dvec3(baseMatrix(feet)*glm::dvec4(pivot,1))+direction()*muzzleLength;
    }
    physics::BodySpawnDesc projectile(glm::dvec3 feet) const {
        // Clear the rim before admission; the runtime separately checks scenery.
        const auto position=physics::worldPositionFromAbsolute(muzzle(feet)+direction()*(double(ballRadius)+.12));
        physics::BodySpawnDesc desc;
        desc.position=position.local;desc.sector=position.sector;
        desc.dimensions=glm::vec3(ballRadius);desc.linearVelocity=glm::vec3(direction())*shotSpeed;
        desc.inverseMass=.5f;desc.bullet=true;
        desc.material=physics::PhysicsMaterial{.friction=.55f,.restitution=.18f,.rollingResistance=.04f};
        return desc;
    }
    size_t liveShots() const noexcept {
        return std::count_if(shots.begin(),shots.end(),[](const Shot& s){return s.body.valid();});
    }
    bool clear(physics::PhysicsWorld& world) noexcept {
        bool complete=true;
        for(auto& shot:shots)if(shot.body.valid()) {
            if(world.destroyBody(shot.body))shot={};else complete=false;
        }
        if(complete){nextShotTick=0;recoil=0;}
        return complete;
    }
    void retire(physics::PhysicsWorld& world) noexcept {
        for(auto& shot:shots)if(shot.body.valid()&&world.encodedTick()>=shot.expires)
            if(world.destroyBody(shot.body))shot={};
    }
    bool fire(physics::PhysicsWorld& world,glm::dvec3 feet) {
        const auto tick=world.encodedTick();
        if(tick<nextShotTick)return false;
        auto slot=std::find_if(shots.begin(),shots.end(),[](const Shot& s){return !s.body.valid();});
        if(slot==shots.end())return false;
        const auto body=world.spawnBody(projectile(feet));if(!body.valid())return false;
        *slot={body,tick+lifetimeTicks};nextShotTick=tick+reloadTicks;++shotsFired;recoil=.18;
        return true;
    }
};

} // namespace voxy::game::adventure
