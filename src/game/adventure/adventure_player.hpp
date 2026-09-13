#pragma once

#include "game/adventure/spatial_queries.hpp"
#include "game/expedition/cove_player.hpp"

namespace voxy::game::adventure {

// A connected idle controller does not own the building pointer. Retain pad
// aim after actual pad use until the mouse acts or that controller disconnects.
[[nodiscard]] constexpr bool adventurePadOwnsAim(bool previous,bool mouseActivity,
    bool padConnected,bool padActivity) noexcept {
    return padConnected&&!mouseActivity&&(padActivity||previous);
}

// Solo authority for this upright robot. It has no boat slots, GPU readbacks or
// independent animation movement; presentation consumes the accepted state.
class AdventurePlayer {
public:
    using Mode=expedition::CovePlayer::Mode;
    struct State {glm::dvec3 feet{},velocity{};double facingYaw=0;uint64_t tick=0;Mode mode=Mode::Walking;};
    struct Input {glm::dvec2 movement{};bool jump=false;};
    static constexpr double radius=.3,height=1.7,eyeHeight=1.55,fixedStep=1./60.;
    [[nodiscard]] bool initialize(const AdventureSpatialQueries&,glm::dvec3 spawn,double waterHeight,double yaw=0) noexcept;
    [[nodiscard]] bool restore(State) noexcept;
    void advance(double seconds,Input) noexcept;
    void discardPendingInput() noexcept {accumulator_=0;pendingJump_=false;}
    [[nodiscard]] State state() const noexcept {return state_;}
    [[nodiscard]] glm::dvec3 feet() const noexcept {return state_.feet;}
    [[nodiscard]] glm::dvec3 worldVelocity() const noexcept {return state_.velocity;}
    [[nodiscard]] double facingYaw() const noexcept {return state_.facingYaw;}
    [[nodiscard]] Mode mode() const noexcept {return state_.mode;}
    [[nodiscard]] uint64_t tick() const noexcept {return state_.tick;}
private:
    [[nodiscard]] glm::dvec3 move(glm::dvec3 from,glm::dvec3 displacement) const noexcept;
    void step(Input) noexcept;
    const AdventureSpatialQueries* queries_=nullptr;
    State state_{};
    double waterHeight_=-200,accumulator_=0;
    bool pendingJump_=false;
};
} // namespace voxy::game::adventure
