#pragma once

#include "physics/physics_world.hpp"
#include "render/primitive_material.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <span>

namespace voxy::game::adventure {

// Own only handles and launch commands. Motion, contacts, sleep and rendering
// use the application's GPU PhysicsWorld, with no CPU pose simulation.
struct BrickThrower {
    static constexpr uint32_t burstSize=100,maximumLive=600;
    static constexpr uint64_t lifetimeTicks=1800;
    static constexpr glm::vec3 dimensions{1.96f,1.14f,.96f};
    struct Brick {physics::BodyHandle body{};uint64_t expires=0;};
    std::array<Brick,maximumLive> bricks{};
    mutable std::array<uint32_t,maximumLive> renderIds{};
    uint64_t thrown=0;

    // Clicks are edge-triggered. A right-button drag remains camera control.
    bool rightPending=false;
    double rightTravel=0;
    uint32_t input(bool allowed,bool leftPressed,bool rightPressed,bool rightReleased,glm::vec2 drag) noexcept {
        if(!allowed){rightPending=false;rightTravel=0;return 0;}
        if(rightPressed){rightPending=true;rightTravel=0;}
        if(rightPending)rightTravel+=glm::length(glm::dvec2(drag));
        uint32_t count=leftPressed?1u:0u;
        if(rightReleased) {
            if(rightPending&&rightTravel<=5.)count=burstSize;
            rightPending=false;rightTravel=0;
        }
        return count;
    }

    static physics::BodySpawnDesc projectile(glm::dvec3 hand,double yaw,double elevation,
        uint32_t index,uint32_t count) {
        const glm::dvec3 forward(-std::sin(yaw),0,-std::cos(yaw));
        const glm::dvec3 right(-std::cos(yaw),0,std::sin(yaw));
        // A spaced volume clears the figure and avoids initial interpenetration.
        // 2.6 exceeds the complete brick's bounding-sphere diameter, including studs.
        const double x=count>1?(double(index%5)-2)*2.6:0;
        const double y=count>1?double((index/5)%4)*2.6:0;
        const double z=count>1?double(index/20)*2.6:0;
        const auto position=physics::worldPositionFromAbsolute(hand+forward*(3.+z)+right*x+glm::dvec3(0,y,0));
        physics::BodySpawnDesc desc;
        desc.shape=physics::ThrowableShape::Box;desc.dimensions=dimensions;
        desc.position=position.local;desc.sector=position.sector;
        desc.orientation=glm::angleAxis(float(yaw),glm::vec3(0,1,0));
        const double pitch=std::clamp(-elevation+.22,.12,.65);
        const auto direction=glm::normalize(forward*std::cos(pitch)+glm::dvec3(0,std::sin(pitch),0)+right*(x*.012));
        desc.linearVelocity=glm::vec3(direction)*28.f;
        desc.angularVelocity={3.5f,5.f,2.5f};desc.bullet=true;
        // Match Jolt's rigid-body defaults. Its contacts have no additional
        // rolling-resistance torque; friction and angular damping settle spin.
        physics::PhysicsMaterial material{.friction=.65f,.restitution=.25f,.rollingResistance=0.f};
        material.flags=(render::packPrimitiveMaterial({{.82f,.16f,.055f},.38f,0.f,true})&0x0fffffffu)
            |physics::kLegoBrickMaterial;
        desc.material=material;
        return desc;
    }
    size_t live() const noexcept {
        return static_cast<size_t>(std::count_if(bricks.begin(),bricks.end(),[](const Brick& b){return b.body.valid();}));
    }
    std::span<const uint32_t> bodyIds() const noexcept {
        size_t count=0;
        for(const auto& brick:bricks)if(brick.body.valid())renderIds[count++]=brick.body.index;
        return {renderIds.data(),count};
    }
    void retire(physics::PhysicsWorld& world,bool all=false) noexcept {
        for(auto& brick:bricks)if(brick.body.valid()) {
            if(all)brick.expires=0; // Retry deferred retirement on the next frame.
            if(world.encodedTick()>=brick.expires&&world.destroyBody(brick.body))brick={};
        }
    }
    uint32_t launch(physics::PhysicsWorld& world,glm::dvec3 hand,double yaw,double elevation,uint32_t count) {
        if(world.backendType()!=physics::BackendType::WebGpuSoft||!std::isfinite(yaw)||!std::isfinite(elevation)
            ||!std::isfinite(hand.x)||!std::isfinite(hand.y)||!std::isfinite(hand.z))return 0;
        count=std::min(count,burstSize);
        retire(world);
        uint32_t spawned=0;
        for(auto& slot:bricks) {
            if(spawned==count)break;
            if(slot.body.valid())continue;
            const auto body=world.spawnBody(projectile(hand,yaw,elevation,spawned,count));
            if(!body.valid())break;
            slot={body,world.encodedTick()+lifetimeTicks};++spawned;
        }
        thrown+=spawned;return spawned;
    }
};
} // namespace voxy::game::adventure
