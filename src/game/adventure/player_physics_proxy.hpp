#pragma once

#include "game/adventure/brick_thrower.hpp"

namespace voxy::game::adventure {

// The walking controller owns the figure. This invisible upright capsule
// supplies its accepted movement to GPU contacts; only contact impulses return
// to gameplay. Brick poses never leave the GPU for collision or rendering.
struct PlayerPhysicsProxy {
    static constexpr float mass=8.f;
    physics::BodyHandle body{};
    glm::dvec3 previousCenter{};
    bool retiring=false;
    uint64_t contacts=0;

    void clear(physics::PhysicsWorld& world) {
        retiring=body.valid();
        if(retiring&&world.destroyBody(body)){body={};retiring=false;}
    }
    void sync(physics::PhysicsWorld& world,glm::dvec3 feet,double radius,double height,bool enabled=true) {
        if(retiring||!enabled){clear(world);return;}
        if(world.backendType()!=physics::BackendType::WebGpuSoft)return;
        const auto center=feet+glm::dvec3(0,height*.5,0);
        const auto position=physics::worldPositionFromAbsolute(center);
        if(!body.valid()) {
            physics::BodySpawnDesc desc;
            desc.shape=physics::ThrowableShape::Capsule;
            desc.position=position.local;desc.sector=position.sector;
            desc.dimensions={float(radius*2),float(height),float(radius*2)};
            desc.inverseMass=0;
            desc.material=physics::PhysicsMaterial{.friction=.65f,.restitution=0.f,
                .rollingResistance=0.f,.density=1.f,.flags=physics::kInvisiblePhysicsMaterial};
            body=world.spawnBody(desc);previousCenter=center;
        }
        if(!body.valid())return;
        physics::PhysicsCommand target;
        target.type=physics::PhysicsCommandType::SetKinematicTarget;target.body=body;
        target.a=glm::vec4(position.local,0);target.b={0,0,0,1};target.sector=position.sector;
        if(glm::length(center-previousCenter)>height) {
            // Recovery/visiting a landmark is a teleport, never a high-speed
            // sweep that flings every brick between the two locations.
            auto teleport=target;teleport.type=physics::PhysicsCommandType::Teleport;
            const std::array commands{teleport,target};world.enqueue(commands);
        } else world.enqueue(std::span(&target,1));
        previousCenter=center;
    }
    glm::dvec3 contactImpulse(const physics::PhysicsEvent& event,const BrickThrower& thrower) {
        if(retiring||!body.valid()||event.type!=physics::PhysicsEventType::ContactHit
            ||!std::isfinite(event.impulse)||event.impulse<=0
            ||!std::isfinite(event.impactSpeed)||event.impactSpeed<1.f)return {};
        const bool playerA=event.bodyHandleA()==body;
        if(!playerA&&event.bodyHandleB()!=body)return {};
        const auto other=playerA?event.bodyHandleB():event.bodyHandleA();
        if(std::none_of(thrower.bricks.begin(),thrower.bricks.end(),[&](const auto& b){return b.body==other;}))return {};
        const auto normal=glm::dvec3(playerA?-event.normalAtoB:event.normalAtoB);
        if(!std::isfinite(normal.x)||!std::isfinite(normal.y)||!std::isfinite(normal.z))return {};
        ++contacts;
        return normal*double(std::min(event.impulse/mass,12.f));
    }
};
} // namespace voxy::game::adventure
