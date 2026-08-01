#pragma once

#include "game/wreckwater_character_movement.hpp"

#include <cstdint>
#include <optional>
#include <span>

namespace voxy::game {

// One allocation-free 60 Hz character simulation quantum. Connection,
// sequence-window, platform-continuity, and tick-certification policy remain
// outside this kernel. Callers provide already canonical exact platform
// samples for the prior and current tick.
struct WreckwaterCharacterStepInput {
    const WreckwaterCharacterMovementAuthority::Config& config;
    const WreckwaterCharacterState& priorState;
    const WreckwaterCharacterInput& input;
    std::span<const WreckwaterCharacterPlatformSample> previousPlatforms;
    std::span<const WreckwaterCharacterPlatformSample> currentPlatforms;
    uint64_t tick = 0u;
};

struct WreckwaterCharacterStepResult {
    WreckwaterCharacterStatus status =
        WreckwaterCharacterStatus::InvalidInput;
    WreckwaterCharacterState nextState{};
    std::optional<WreckwaterCharacterTransition> transition;

    [[nodiscard]] explicit operator bool() const noexcept {
        return status == WreckwaterCharacterStatus::Accepted;
    }
};

[[nodiscard]] WreckwaterCharacterStepResult wreckwaterCharacterStep(
    const WreckwaterCharacterStepInput& input) noexcept;

} // namespace voxy::game
