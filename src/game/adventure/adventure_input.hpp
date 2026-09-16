#pragma once

#include "engine/platform/gamepad.hpp"
#include "game/expedition/cove_input_preferences.hpp"
#include <array>
#include <cmath>
#include <glm/vec2.hpp>

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

[[nodiscard]] inline expedition::CoveInputPreferences adventureInputDefaults() {
    using A=expedition::CoveAction;
    expedition::CoveInputPreferences preferences;
    preferences.bindings[static_cast<size_t>(A::Jump)].pad=static_cast<int>(PadButton::Confirm);
    preferences.bindings[static_cast<size_t>(A::Interact)].pad=static_cast<int>(PadButton::Tool);
    preferences.bindings[static_cast<size_t>(A::Hook)]={};
    return preferences;
}

// Building shares World movement with Workshop actions. Confirm belongs to
// placement in that context; retain keyboard Space and the untouched sample
// for the Workshop router so one button cannot both jump and place a piece.
[[nodiscard]] inline expedition::CoveInputSample adventureMovementSample(
    expedition::CoveInputSample sample,bool building) noexcept {
    if(building) {
        const auto confirm=static_cast<size_t>(PadButton::Confirm);
        sample.padDown[confirm]=false;
        sample.padPressed[confirm]=false;
    }
    return sample;
}

} // namespace voxy::game::adventure
