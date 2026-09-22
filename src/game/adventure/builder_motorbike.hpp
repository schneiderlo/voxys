#pragma once
#include "game/adventure/adventure_player.hpp"
#include <optional>
#include <array>

namespace voxy::game::adventure {
// Assisted toy-bike controller. The accepted static world supplies all support
// and swept collision; rendering never moves the vehicle. All units are studs.
class BuilderMotorbike {
public:
    static constexpr double wheelRadius=.28*2.8, halfWheelbase=.63*2.8;
    static constexpr double fixedStep=1./60.;
    struct Input {double throttle=0,steer=0;bool brake=false;};
    struct State {
        glm::dvec3 feet{};
        double yaw=0,yawRate=0,speed=0,pitch=0,lean=0,steering=0,spin=0,verticalSpeed=0,groundOffset=0;
        // World-space axle heights and vertical momentum, rear then front.
        // The rigid body pose is derived from these wheel contacts.
        std::array<double,2> axleHeight{},axleVelocity{};
        std::array<bool,2> wheelGrounded{true,true};
        bool grounded=true;
        uint64_t tick=0;
    };
    bool place(const AdventureSpatialQueries&,glm::dvec3,double yaw,double waterHeight) noexcept;
    void advance(const AdventureSpatialQueries&,double seconds,Input,double waterHeight) noexcept;
    std::optional<glm::dvec3> dismount(const AdventureSpatialQueries&,double waterHeight) const noexcept;
    const State& state() const noexcept {return state_;}
    bool available() const noexcept {return available_;}
    void pause() noexcept {accumulator_=0;state_.speed=0;state_.yawRate=0;}
    void reset() noexcept {*this=BuilderMotorbike{};}
private:
    bool clear(const AdventureSpatialQueries&,glm::dvec3,double yaw) const noexcept;
    bool sweep(const AdventureSpatialQueries&,glm::dvec3,double,glm::dvec3,double) const noexcept;
    std::optional<glm::dvec3> ground(const AdventureSpatialQueries&,glm::dvec3,double,double) const noexcept;
    std::optional<std::array<double,2>> wheelSupport(const AdventureSpatialQueries&,
        glm::dvec3,double yaw,double pitch,double water) const noexcept;
    bool terrainPose(const AdventureSpatialQueries&,glm::dvec3,double yaw,double water,State&) const noexcept;
    void step(const AdventureSpatialQueries&,Input,double) noexcept;
    State state_{};
    bool available_=false;
    double accumulator_=0;
};
}
