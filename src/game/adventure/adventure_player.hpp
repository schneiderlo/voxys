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

// Solo authority for this upright character. It has no boat slots, GPU readbacks or
// independent animation movement; presentation consumes the accepted state.
class AdventurePlayer {
public:
    using Mode=expedition::CovePlayer::Mode;
    struct State {glm::dvec3 feet{},velocity{};double facingYaw=0;uint64_t tick=0;Mode mode=Mode::Walking;};
    // Running and the trusted combat resolver supply a bounded pace multiplier.
    // Faster movement uses the same step, sweep and support solver.
    struct Input {
        glm::dvec2 movement{};
        bool jump=false;
        double speedScale=1;
        // Held vertical intent in water; jump remains a press edge on land.
        double swimVertical=0;
    };
    static constexpr double radius=.3,height=1.7,eyeHeight=1.55,fixedStep=1./60.;
    static constexpr double swimSpeed=2.4,swimImmersion=1.15;
    // One world unit is one stud spacing. The original 1.7-unit adventure
    // figure was human-metre sized; creative figures stand 4.76 studs tall.
    static constexpr double creativeScale=2.8,creativeRadius=.4;
    // LDraw bricks rise 24 LDU / 20 LDU per stud = 1.2 studs.
    // Creative walking can clear one brick plus contact tolerance.
    static constexpr double creativeStepHeight=1.26;
    [[nodiscard]] bool initialize(const AdventureSpatialQueries&,glm::dvec3 spawn,double waterHeight,double yaw=0,double scale=1,double baseRadius=radius) noexcept;
    [[nodiscard]] double bodyScale() const noexcept {return scale_;}
    [[nodiscard]] double bodyRadius() const noexcept {return baseRadius_*scale_;}
    [[nodiscard]] double bodyHeight() const noexcept {return height*scale_;}
    [[nodiscard]] double swimSurfaceHeight() const noexcept {return waterHeight_-swimImmersion*scale_;}
    [[nodiscard]] bool restore(State) noexcept;
    void advance(double seconds,Input) noexcept;
    void addContactImpulse(glm::dvec3 velocityChange) noexcept;
    void discardPendingInput() noexcept {accumulator_=0;pendingJump_=false;}
    [[nodiscard]] State state() const noexcept {return state_;}
    [[nodiscard]] glm::dvec3 feet() const noexcept {return state_.feet;}
    [[nodiscard]] glm::dvec3 worldVelocity() const noexcept {return state_.velocity;}
    [[nodiscard]] double facingYaw() const noexcept {return state_.facingYaw;}
    [[nodiscard]] Mode mode() const noexcept {return state_.mode;}
    [[nodiscard]] uint64_t tick() const noexcept {return state_.tick;}
    [[nodiscard]] bool usesWorld(const AdventureSpatialQueries& queries,double waterHeight) const noexcept {
        return queries_==&queries&&waterHeight_==waterHeight;
    }
private:
    [[nodiscard]] glm::dvec3 move(glm::dvec3 from,glm::dvec3 displacement) const noexcept;
    [[nodiscard]] glm::dvec3 groundMove(glm::dvec3 from,glm::dvec3 displacement) const noexcept;
    void swim(Input) noexcept;
    void step(Input) noexcept;
    const AdventureSpatialQueries* queries_=nullptr;
    State state_{};
    glm::dvec3 contactVelocity_{};
    double waterHeight_=-200,accumulator_=0;
    double scale_=1,movementScale_=1,baseRadius_=radius;
    bool pendingJump_=false;
};
} // namespace voxy::game::adventure
