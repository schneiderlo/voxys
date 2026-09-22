#pragma once

#include "engine/platform/gamepad.hpp"
#include "game/expedition/cove_input_preferences.hpp"
#include <array>
#include <cmath>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

namespace voxy::game::adventure {

// The orbit uses authored -Z forward. The renderer's left-handed view makes
// screen-right cross(worldUp, forward), so yaw zero moves right toward -X.
// Preserve the router's analog magnitude; AdventurePlayer caps diagonal speed.
[[nodiscard]] inline glm::dvec2 adventureCameraRelativeMovement(
    std::array<double,2> movement,double orbitYaw) noexcept {
    const double sine=std::sin(orbitYaw),cosine=std::cos(orbitYaw);
    const glm::dvec2 forward(-sine,-cosine),right(-cosine,sine);
    return right*movement[0]+forward*movement[1];
}

// Camera elevation is positive when looking down. Free-looking does not dive;
// holding mouse-look (or steering with the pad) pitches forward/backward travel.
[[nodiscard]] inline glm::dvec3 adventureSwimmingMovement(
    std::array<double,2> movement,double yaw,double elevation,bool steer,
    bool rise,bool descend) noexcept {
    const auto horizontal=adventureCameraRelativeMovement(
        {movement[0],movement[1]*(steer?std::cos(elevation):1.)},yaw);
    const double vertical=(rise||descend)?double(rise)-double(descend)
        :(steer?-std::sin(elevation)*movement[1]:0.);
    return {horizontal.x,vertical,horizontal.y};
}

[[nodiscard]] inline expedition::CoveInputPreferences adventureInputDefaults() {
    using A=expedition::CoveAction;
    expedition::CoveInputPreferences preferences;
    preferences.bindings[static_cast<size_t>(A::Jump)].pad=static_cast<int>(PadButton::Confirm);
    preferences.bindings[static_cast<size_t>(A::Interact)].pad=static_cast<int>(PadButton::Tool);
    preferences.bindings[static_cast<size_t>(A::Hook)]={};
    // Reuse the shared router's raise/lower slots; adventure settings retain
    // their existing save schema and all focus/menu rearming protections.
    preferences.bindings[static_cast<size_t>(A::ReelIn)]={.pad=static_cast<int>(PadButton::RightTrigger)};
    preferences.bindings[static_cast<size_t>(A::PayOut)]={.key=88,.pad=static_cast<int>(PadButton::LeftTrigger)};
    return preferences;
}

// Building shares World movement with Workshop actions. Confirm belongs to
// placement in that context; retain keyboard Space and the untouched sample
// for the Workshop router so one button cannot both jump and place a piece.
[[nodiscard]] inline expedition::CoveInputSample adventureMovementSample(
    expedition::CoveInputSample sample,bool building,bool allowRunning=false) noexcept {
    // Shift is a pace modifier in creative play, not a different locomotion
    // chord. Normalize both held keys and press edges (including quick taps).
    // Workshop receives the original sample for fine placement.
    if(allowRunning) {
        sample.modifiers&=uint8_t(~1u);
        for(auto& modifiers:sample.pressModifiers)modifiers&=uint8_t(~1u);
    }
    if(building) {
        const auto confirm=static_cast<size_t>(PadButton::Confirm);
        sample.padDown[confirm]=false;
        sample.padPressed[confirm]=false;
    }
    return sample;
}

} // namespace voxy::game::adventure
