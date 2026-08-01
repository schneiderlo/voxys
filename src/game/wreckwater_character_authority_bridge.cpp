#include "game/wreckwater_character_authority_bridge.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>
#include <utility>

namespace voxy::game {
namespace {

constexpr uint64_t kFnvOffset64 = 14'695'981'039'346'656'037ull;
constexpr uint64_t kFnvPrime64 = 1'099'511'628'211ull;
constexpr size_t kInvalidIndex = std::numeric_limits<size_t>::max();

void incrementSaturated(uint64_t& value) noexcept {
    if (value != std::numeric_limits<uint64_t>::max()) ++value;
}

void addSaturated(uint64_t& value, uint64_t increment) noexcept {
    if (increment > std::numeric_limits<uint64_t>::max() - value) {
        value = std::numeric_limits<uint64_t>::max();
    } else {
        value += increment;
    }
}

void canonicalizeZero(float& value) noexcept {
    if (value == 0.0f) value = 0.0f;
}

void canonicalizeZero(glm::vec2& value) noexcept {
    canonicalizeZero(value.x);
    canonicalizeZero(value.y);
}

void hashU32(uint64_t& hash, uint32_t value) noexcept {
    for (uint32_t shift = 0u; shift < 32u; shift += 8u) {
        hash ^= static_cast<uint8_t>(value >> shift);
        hash *= kFnvPrime64;
    }
}

void hashU64(uint64_t& hash, uint64_t value) noexcept {
    hashU32(hash, static_cast<uint32_t>(value));
    hashU32(hash, static_cast<uint32_t>(value >> 32u));
}

void hashFloat(uint64_t& hash, float value) noexcept {
    hashU32(hash, std::bit_cast<uint32_t>(value));
}

void hashInput(
    uint64_t& hash, const WreckwaterCharacterInput& input) noexcept {
    hashU64(hash, input.targetTick);
    hashU64(hash, input.characterInputSequence);
    hashU32(hash, input.character);
    hashU64(hash, input.playerId);
    hashU32(hash, input.connectionGeneration);
    hashFloat(hash, input.move.x);
    hashFloat(hash, input.move.y);
    hashU32(hash, input.jump ? 1u : 0u);
    hashU32(hash, input.board ? 1u : 0u);
}

[[nodiscard]] size_t inputSlotIndex(uint64_t tick) noexcept {
    return static_cast<size_t>(
        static_cast<uint32_t>(
            tick
            % static_cast<uint64_t>(
                kWreckwaterCharacterAuthorityBridgeInputRingSlots)));
}

} // namespace

const char* wreckwaterCharacterAuthorityBridgeStatusName(
    WreckwaterCharacterAuthorityBridgeStatus status) noexcept {
    switch (status) {
        case WreckwaterCharacterAuthorityBridgeStatus::Accepted:
            return "accepted";
        case WreckwaterCharacterAuthorityBridgeStatus::
                IgnoredLowerSequence:
            return "ignored lower sequence";
        case WreckwaterCharacterAuthorityBridgeStatus::NotInitialized:
            return "not initialized";
        case WreckwaterCharacterAuthorityBridgeStatus::
                InvalidConfiguration:
            return "invalid configuration";
        case WreckwaterCharacterAuthorityBridgeStatus::InvalidRoster:
            return "invalid roster";
        case WreckwaterCharacterAuthorityBridgeStatus::InvalidInput:
            return "invalid input";
        case WreckwaterCharacterAuthorityBridgeStatus::UnknownPlayer:
            return "unknown player";
        case WreckwaterCharacterAuthorityBridgeStatus::UnknownCharacter:
            return "unknown character";
        case WreckwaterCharacterAuthorityBridgeStatus::
                StaleCharacterIdentity:
            return "stale character identity";
        case WreckwaterCharacterAuthorityBridgeStatus::PlayerMismatch:
            return "player mismatch";
        case WreckwaterCharacterAuthorityBridgeStatus::Disconnected:
            return "disconnected";
        case WreckwaterCharacterAuthorityBridgeStatus::
                AlreadyDisconnected:
            return "already disconnected";
        case WreckwaterCharacterAuthorityBridgeStatus::AlreadyConnected:
            return "already connected";
        case WreckwaterCharacterAuthorityBridgeStatus::
                StaleConnectionGeneration:
            return "stale connection generation";
        case WreckwaterCharacterAuthorityBridgeStatus::
                ConnectionGenerationExhausted:
            return "connection generation exhausted";
        case WreckwaterCharacterAuthorityBridgeStatus::
                ReplayedInputSequence:
            return "replayed input sequence";
        case WreckwaterCharacterAuthorityBridgeStatus::
                InputSequenceExhausted:
            return "input sequence exhausted";
        case WreckwaterCharacterAuthorityBridgeStatus::
                InputSequenceJumpTooLarge:
            return "input sequence jump too large";
        case WreckwaterCharacterAuthorityBridgeStatus::StaleInputTick:
            return "stale input tick";
        case WreckwaterCharacterAuthorityBridgeStatus::
                InputFutureWindowExceeded:
            return "input future window exceeded";
        case WreckwaterCharacterAuthorityBridgeStatus::
                InputHistoryOverflow:
            return "input history overflow";
        case WreckwaterCharacterAuthorityBridgeStatus::StalePoseTick:
            return "stale pose tick";
        case WreckwaterCharacterAuthorityBridgeStatus::PoseTickGap:
            return "pose tick gap";
        case WreckwaterCharacterAuthorityBridgeStatus::
                PoseReadbackOverflow:
            return "pose readback overflow";
        case WreckwaterCharacterAuthorityBridgeStatus::
                PosePlatformCountMismatch:
            return "pose platform count mismatch";
        case WreckwaterCharacterAuthorityBridgeStatus::PoseRejected:
            return "pose rejected";
        case WreckwaterCharacterAuthorityBridgeStatus::TickExhausted:
            return "tick exhausted";
    }
    return "unknown";
}

WreckwaterCharacterAuthorityBridgeStatus
WreckwaterCharacterAuthorityBridge::mapMovementStatus(
    WreckwaterCharacterStatus status) noexcept {
    switch (status) {
        case WreckwaterCharacterStatus::Accepted:
            return WreckwaterCharacterAuthorityBridgeStatus::Accepted;
        case WreckwaterCharacterStatus::NotInitialized:
            return WreckwaterCharacterAuthorityBridgeStatus::
                NotInitialized;
        case WreckwaterCharacterStatus::InvalidConfiguration:
            return WreckwaterCharacterAuthorityBridgeStatus::
                InvalidConfiguration;
        case WreckwaterCharacterStatus::InvalidInput:
        case WreckwaterCharacterStatus::WrongInputTick:
        case WreckwaterCharacterStatus::NonMonotonicTick:
            return WreckwaterCharacterAuthorityBridgeStatus::
                InvalidInput;
        case WreckwaterCharacterStatus::UnknownCharacter:
            return WreckwaterCharacterAuthorityBridgeStatus::
                UnknownCharacter;
        case WreckwaterCharacterStatus::StaleCharacterIdentity:
            return WreckwaterCharacterAuthorityBridgeStatus::
                StaleCharacterIdentity;
        case WreckwaterCharacterStatus::PlayerMismatch:
        case WreckwaterCharacterStatus::DuplicatePlayer:
            return WreckwaterCharacterAuthorityBridgeStatus::
                PlayerMismatch;
        case WreckwaterCharacterStatus::Disconnected:
            return WreckwaterCharacterAuthorityBridgeStatus::
                Disconnected;
        case WreckwaterCharacterStatus::AlreadyDisconnected:
            return WreckwaterCharacterAuthorityBridgeStatus::
                AlreadyDisconnected;
        case WreckwaterCharacterStatus::AlreadyConnected:
            return WreckwaterCharacterAuthorityBridgeStatus::
                AlreadyConnected;
        case WreckwaterCharacterStatus::StaleConnectionGeneration:
            return WreckwaterCharacterAuthorityBridgeStatus::
                StaleConnectionGeneration;
        case WreckwaterCharacterStatus::ConnectionGenerationExhausted:
            return WreckwaterCharacterAuthorityBridgeStatus::
                ConnectionGenerationExhausted;
        case WreckwaterCharacterStatus::ReplayedInputSequence:
            return WreckwaterCharacterAuthorityBridgeStatus::
                ReplayedInputSequence;
        case WreckwaterCharacterStatus::InputSequenceExhausted:
            return WreckwaterCharacterAuthorityBridgeStatus::
                InputSequenceExhausted;
        case WreckwaterCharacterStatus::InputSequenceJumpTooLarge:
            return WreckwaterCharacterAuthorityBridgeStatus::
                InputSequenceJumpTooLarge;
        case WreckwaterCharacterStatus::CharacterCapacityExceeded:
        case WreckwaterCharacterStatus::PlatformCapacityExceeded:
        case WreckwaterCharacterStatus::DuplicatePlatformIdentity:
        case WreckwaterCharacterStatus::StalePlatformIdentity:
        case WreckwaterCharacterStatus::PlatformMotionDiscontinuity:
        case WreckwaterCharacterStatus::StateOutOfRange:
        case WreckwaterCharacterStatus::IdentityExhausted:
        case WreckwaterCharacterStatus::TransitionCapacityExceeded:
            return WreckwaterCharacterAuthorityBridgeStatus::
                PoseRejected;
    }
    return WreckwaterCharacterAuthorityBridgeStatus::PoseRejected;
}

bool WreckwaterCharacterAuthorityBridge::initialize(
    const std::array<
        WreckwaterCharacterSpawn,
        kWreckwaterMaximumCharacters>& roster) noexcept {
    return initialize(Config{}, roster);
}

bool WreckwaterCharacterAuthorityBridge::initialize(
    const Config& config,
    const std::array<
        WreckwaterCharacterSpawn,
        kWreckwaterMaximumCharacters>& requestedRoster) noexcept {
    if (config.maximumReadbackLagTicks == 0u
        || config.maximumReadbackLagTicks
            > kWreckwaterCharacterAuthorityBridgeMaximumReadbackLagTicks
        || config.movement.maximumCharacters
            != kWreckwaterMaximumCharacters) {
        return false;
    }

    auto canonicalRoster = requestedRoster;
    for (size_t outer = 0u; outer < canonicalRoster.size(); ++outer) {
        size_t selected = outer;
        for (size_t inner = outer + 1u;
             inner < canonicalRoster.size(); ++inner) {
            if (canonicalRoster[inner].playerId
                < canonicalRoster[selected].playerId) {
                selected = inner;
            }
        }
        if (selected != outer)
            std::swap(canonicalRoster[outer], canonicalRoster[selected]);
    }
    for (size_t index = 0u; index < canonicalRoster.size(); ++index) {
        if (canonicalRoster[index].playerId == 0u
            || canonicalRoster[index].connectionGeneration == 0u
            || (index != 0u
                && canonicalRoster[index - 1u].playerId
                    == canonicalRoster[index].playerId)) {
            return false;
        }
    }

    WreckwaterCharacterMovementAuthority nextAuthority;
    if (!nextAuthority.initialize(config.movement)) return false;
    std::array<
        WreckwaterCharacterAuthorityBridgePlayerState,
        kWreckwaterMaximumCharacters> nextPlayers{};
    for (size_t index = 0u; index < canonicalRoster.size(); ++index) {
        const WreckwaterCharacterSpawnResult spawned =
            nextAuthority.spawnCharacter(canonicalRoster[index]);
        if (!spawned) return false;
        nextPlayers[index] = {
            .playerId = canonicalRoster[index].playerId,
            .character = spawned.handle,
            .connectionGeneration =
                canonicalRoster[index].connectionGeneration,
            .connected = true,
        };
    }

    config_ = config;
    config_.movement = nextAuthority.config();
    authority_ = nextAuthority;
    players_ = nextPlayers;
    inputHistory_ = {};
    transitionOutput_ = {};
    transitionOutputCount_ = 0u;
    telemetry_ = {};
    state_ = {};
    initialized_ = true;
    refreshPlayerState();
    updateStateHash();
    return true;
}

WreckwaterCharacterAuthorityBridgeStatus
WreckwaterCharacterAuthorityBridge::findPlayerIndex(
    WreckwaterCharacterHandle character, PlayerId playerId,
    size_t& index) const noexcept {
    index = kInvalidIndex;
    if (playerId == 0u)
        return WreckwaterCharacterAuthorityBridgeStatus::UnknownPlayer;
    for (size_t candidate = 0u;
         candidate < players_.size(); ++candidate) {
        if (players_[candidate].playerId == playerId) {
            index = candidate;
            break;
        }
    }
    if (index == kInvalidIndex)
        return WreckwaterCharacterAuthorityBridgeStatus::UnknownPlayer;
    if (character == kInvalidWreckwaterCharacter)
        return WreckwaterCharacterAuthorityBridgeStatus::
            UnknownCharacter;
    if (players_[index].character != character) {
        const uint32_t requestedSlot =
            physics::characterHandleSlot(character);
        const uint32_t expectedSlot =
            physics::characterHandleSlot(players_[index].character);
        return requestedSlot == expectedSlot
            ? WreckwaterCharacterAuthorityBridgeStatus::
                StaleCharacterIdentity
            : WreckwaterCharacterAuthorityBridgeStatus::
                PlayerMismatch;
    }
    return WreckwaterCharacterAuthorityBridgeStatus::Accepted;
}

bool WreckwaterCharacterAuthorityBridge::validSequenceInsertion(
    size_t playerIndex, uint64_t targetTick,
    uint64_t characterInputSequence) const noexcept {
    const WreckwaterCharacterState* character = authority_.character(
        players_[playerIndex].character);
    if (character == nullptr) return false;

    uint64_t predecessorTick = 0u;
    uint64_t predecessorSequence =
        character->latestCharacterInputSequence;
    uint64_t successorTick = 0u;
    uint64_t successorSequence = 0u;
    bool successorFound = false;
    for (const InputSlot& slot : inputHistory_[playerIndex]) {
        if (!slot.occupied || slot.tick == targetTick) continue;
        if (slot.tick < targetTick && slot.tick > predecessorTick) {
            predecessorTick = slot.tick;
            predecessorSequence =
                slot.input.characterInputSequence;
        } else if (
            slot.tick > targetTick
            && (!successorFound || slot.tick < successorTick)) {
            successorTick = slot.tick;
            successorSequence =
                slot.input.characterInputSequence;
            successorFound = true;
        }
        if (slot.input.characterInputSequence
            == characterInputSequence) {
            return false;
        }
    }

    const uint64_t maximumAdvance =
        static_cast<uint64_t>(
            config_.movement.maximumInputSequenceAdvance);
    if (characterInputSequence <= predecessorSequence
        || characterInputSequence - predecessorSequence
            > maximumAdvance) {
        return false;
    }
    if (successorFound
        && (successorSequence <= characterInputSequence
            || successorSequence - characterInputSequence
                > maximumAdvance)) {
        return false;
    }
    return true;
}

WreckwaterCharacterAuthorityBridgeStatus
WreckwaterCharacterAuthorityBridge::submitInput(
    const WreckwaterCharacterInput& requested) noexcept {
    if (!initialized_) {
        incrementSaturated(telemetry_.inputPacketsRejected);
        return WreckwaterCharacterAuthorityBridgeStatus::
            NotInitialized;
    }
    if (requested.targetTick == 0u
        || requested.characterInputSequence == 0u
        || requested.playerId == 0u
        || !std::isfinite(requested.move.x)
        || !std::isfinite(requested.move.y)) {
        incrementSaturated(telemetry_.inputPacketsRejected);
        return WreckwaterCharacterAuthorityBridgeStatus::InvalidInput;
    }
    if (requested.characterInputSequence
        == std::numeric_limits<uint64_t>::max()) {
        incrementSaturated(telemetry_.inputPacketsRejected);
        return WreckwaterCharacterAuthorityBridgeStatus::
            InputSequenceExhausted;
    }
    const float moveLengthSquared =
        requested.move.x * requested.move.x
        + requested.move.y * requested.move.y;
    if (!std::isfinite(moveLengthSquared)
        || moveLengthSquared > 1.0002f) {
        incrementSaturated(telemetry_.inputPacketsRejected);
        return WreckwaterCharacterAuthorityBridgeStatus::InvalidInput;
    }

    const uint64_t lastTick = authority_.lastClosedTick();
    if (requested.targetTick <= lastTick) {
        incrementSaturated(telemetry_.inputPacketsRejected);
        return WreckwaterCharacterAuthorityBridgeStatus::
            StaleInputTick;
    }
    if (requested.targetTick - lastTick
        > static_cast<uint64_t>(
            config_.maximumReadbackLagTicks)) {
        incrementSaturated(telemetry_.inputPacketsRejected);
        return WreckwaterCharacterAuthorityBridgeStatus::
            InputFutureWindowExceeded;
    }

    size_t playerIndex = kInvalidIndex;
    const WreckwaterCharacterAuthorityBridgeStatus found =
        findPlayerIndex(
            requested.character, requested.playerId, playerIndex);
    if (found
        != WreckwaterCharacterAuthorityBridgeStatus::Accepted) {
        incrementSaturated(telemetry_.inputPacketsRejected);
        return found;
    }
    const auto& playerState = players_[playerIndex];
    if (!playerState.connected) {
        incrementSaturated(telemetry_.inputPacketsRejected);
        return WreckwaterCharacterAuthorityBridgeStatus::Disconnected;
    }
    if (requested.connectionGeneration
        != playerState.connectionGeneration) {
        incrementSaturated(telemetry_.inputPacketsRejected);
        return WreckwaterCharacterAuthorityBridgeStatus::
            StaleConnectionGeneration;
    }

    const size_t slotIndex = inputSlotIndex(requested.targetTick);
    const InputSlot& existing =
        inputHistory_[playerIndex][slotIndex];
    if (existing.occupied
        && existing.tick != requested.targetTick) {
        incrementSaturated(telemetry_.inputPacketsRejected);
        return WreckwaterCharacterAuthorityBridgeStatus::
            InputHistoryOverflow;
    }
    for (const InputSlot& slot : inputHistory_[playerIndex]) {
        if (slot.occupied
            && slot.tick != requested.targetTick
            && slot.input.characterInputSequence
                == requested.characterInputSequence) {
            incrementSaturated(telemetry_.inputPacketsRejected);
            return WreckwaterCharacterAuthorityBridgeStatus::
                ReplayedInputSequence;
        }
    }
    if (existing.occupied) {
        if (requested.characterInputSequence
            == existing.input.characterInputSequence) {
            incrementSaturated(telemetry_.inputPacketsRejected);
            return WreckwaterCharacterAuthorityBridgeStatus::
                ReplayedInputSequence;
        }
        if (requested.characterInputSequence
            < existing.input.characterInputSequence) {
            incrementSaturated(telemetry_.inputPacketsSuperseded);
            return WreckwaterCharacterAuthorityBridgeStatus::
                IgnoredLowerSequence;
        }
    }
    if (!validSequenceInsertion(
            playerIndex, requested.targetTick,
            requested.characterInputSequence)) {
        incrementSaturated(telemetry_.inputPacketsRejected);
        return WreckwaterCharacterAuthorityBridgeStatus::
            InputSequenceJumpTooLarge;
    }

    WreckwaterCharacterInput canonical = requested;
    if (moveLengthSquared > 1.0f) {
        const float inverseLength =
            1.0f / std::sqrt(moveLengthSquared);
        canonical.move *= inverseLength;
    }
    canonicalizeZero(canonical.move);
    inputHistory_[playerIndex][slotIndex] = {
        .input = canonical,
        .tick = canonical.targetTick,
        .occupied = true,
    };
    incrementSaturated(telemetry_.inputPacketsAccepted);
    refreshPlayerState();
    telemetry_.maximumBufferedInputs = std::max(
        telemetry_.maximumBufferedInputs,
        state_.bufferedInputCount);
    updateStateHash();
    return WreckwaterCharacterAuthorityBridgeStatus::Accepted;
}

void WreckwaterCharacterAuthorityBridge::clearTransitionOutput()
    noexcept {
    transitionOutput_ = {};
    transitionOutputCount_ = 0u;
}

void WreckwaterCharacterAuthorityBridge::setLifecycleTransition(
    WreckwaterCharacterAuthorityBridgeTransitionKind kind,
    size_t playerIndex, uint32_t priorGeneration,
    uint32_t nextGeneration) noexcept {
    clearTransitionOutput();
    const WreckwaterCharacterState* character = authority_.character(
        players_[playerIndex].character);
    WreckwaterCharacterAuthorityBridgeTransition transition;
    transition.kind = kind;
    transition.certifiedTick = authority_.lastClosedTick();
    transition.character = players_[playerIndex].character;
    transition.playerId = players_[playerIndex].playerId;
    transition.priorConnectionGeneration = priorGeneration;
    transition.nextConnectionGeneration = nextGeneration;
    if (character != nullptr) {
        transition.from = character->mode;
        transition.to = character->mode;
        transition.skiffId = character->skiffId;
        transition.skiffGeneration = character->skiffGeneration;
        transition.skiffBody = character->skiffBody;
    }
    transitionOutput_[0] = transition;
    transitionOutputCount_ = 1u;
}

WreckwaterCharacterAuthorityBridgeStatus
WreckwaterCharacterAuthorityBridge::disconnectCharacter(
    WreckwaterCharacterHandle character, PlayerId playerId,
    uint32_t connectionGeneration) noexcept {
    if (!initialized_) {
        incrementSaturated(telemetry_.lifecycleOperationsRejected);
        return WreckwaterCharacterAuthorityBridgeStatus::
            NotInitialized;
    }
    size_t playerIndex = kInvalidIndex;
    const WreckwaterCharacterAuthorityBridgeStatus found =
        findPlayerIndex(character, playerId, playerIndex);
    if (found
        != WreckwaterCharacterAuthorityBridgeStatus::Accepted) {
        incrementSaturated(telemetry_.lifecycleOperationsRejected);
        return found;
    }

    WreckwaterCharacterMovementAuthority nextAuthority = authority_;
    const WreckwaterCharacterStatus movementStatus =
        nextAuthority.disconnectCharacter(
            character, playerId, connectionGeneration);
    if (movementStatus != WreckwaterCharacterStatus::Accepted) {
        incrementSaturated(telemetry_.lifecycleOperationsRejected);
        return mapMovementStatus(movementStatus);
    }
    auto nextHistory = inputHistory_;
    nextHistory[playerIndex] = {};

    authority_ = nextAuthority;
    inputHistory_ = nextHistory;
    refreshPlayerState();
    setLifecycleTransition(
        WreckwaterCharacterAuthorityBridgeTransitionKind::
            Disconnected,
        playerIndex, connectionGeneration, connectionGeneration);
    incrementSaturated(telemetry_.disconnects);
    updateStateHash();
    return WreckwaterCharacterAuthorityBridgeStatus::Accepted;
}

WreckwaterCharacterAuthorityBridgeStatus
WreckwaterCharacterAuthorityBridge::reconnectCharacter(
    WreckwaterCharacterHandle character, PlayerId playerId,
    uint32_t priorConnectionGeneration,
    uint32_t nextConnectionGeneration) noexcept {
    if (!initialized_) {
        incrementSaturated(telemetry_.lifecycleOperationsRejected);
        return WreckwaterCharacterAuthorityBridgeStatus::
            NotInitialized;
    }
    size_t playerIndex = kInvalidIndex;
    const WreckwaterCharacterAuthorityBridgeStatus found =
        findPlayerIndex(character, playerId, playerIndex);
    if (found
        != WreckwaterCharacterAuthorityBridgeStatus::Accepted) {
        incrementSaturated(telemetry_.lifecycleOperationsRejected);
        return found;
    }

    WreckwaterCharacterMovementAuthority nextAuthority = authority_;
    const WreckwaterCharacterStatus movementStatus =
        nextAuthority.reconnectCharacter(
            character, playerId, priorConnectionGeneration,
            nextConnectionGeneration);
    if (movementStatus != WreckwaterCharacterStatus::Accepted) {
        incrementSaturated(telemetry_.lifecycleOperationsRejected);
        return mapMovementStatus(movementStatus);
    }
    auto nextHistory = inputHistory_;
    nextHistory[playerIndex] = {};

    authority_ = nextAuthority;
    inputHistory_ = nextHistory;
    refreshPlayerState();
    setLifecycleTransition(
        WreckwaterCharacterAuthorityBridgeTransitionKind::
            Reconnected,
        playerIndex, priorConnectionGeneration,
        nextConnectionGeneration);
    incrementSaturated(telemetry_.reconnects);
    updateStateHash();
    return WreckwaterCharacterAuthorityBridgeStatus::Accepted;
}

WreckwaterCharacterAuthorityBridgeTickResult
WreckwaterCharacterAuthorityBridge::closeCertifiedPlatformTick(
    const WreckwaterCharacterCertifiedPlatformTick& frame) noexcept {
    WreckwaterCharacterAuthorityBridgeTickResult result;
    result.tick = frame.tick;
    result.stateHash = state_.stateHash;
    if (!initialized_) {
        incrementSaturated(telemetry_.poseTicksRejected);
        return result;
    }

    const uint64_t lastTick = authority_.lastClosedTick();
    if (lastTick == std::numeric_limits<uint64_t>::max()) {
        result.status =
            WreckwaterCharacterAuthorityBridgeStatus::TickExhausted;
        incrementSaturated(telemetry_.poseTicksRejected);
        return result;
    }
    if (frame.tick <= lastTick) {
        result.status =
            WreckwaterCharacterAuthorityBridgeStatus::StalePoseTick;
        incrementSaturated(telemetry_.poseTicksRejected);
        return result;
    }
    if (frame.tick != lastTick + 1u) {
        result.status =
            WreckwaterCharacterAuthorityBridgeStatus::PoseTickGap;
        incrementSaturated(telemetry_.poseTicksRejected);
        return result;
    }
    if (frame.overflow) {
        result.status = WreckwaterCharacterAuthorityBridgeStatus::
            PoseReadbackOverflow;
        incrementSaturated(telemetry_.poseTicksRejected);
        return result;
    }
    if (frame.platformCount
        != static_cast<uint32_t>(
            kWreckwaterMaximumCharacterPlatforms)) {
        result.status = WreckwaterCharacterAuthorityBridgeStatus::
            PosePlatformCountMismatch;
        incrementSaturated(telemetry_.poseTicksRejected);
        return result;
    }

    WreckwaterCharacterMovementAuthority nextAuthority = authority_;
    for (size_t playerIndex = 0u;
         playerIndex < players_.size(); ++playerIndex) {
        const size_t slotIndex = inputSlotIndex(frame.tick);
        const InputSlot& slot =
            inputHistory_[playerIndex][slotIndex];
        if (!slot.occupied) continue;
        if (slot.tick != frame.tick) {
            result.status = WreckwaterCharacterAuthorityBridgeStatus::
                InputHistoryOverflow;
            incrementSaturated(telemetry_.poseTicksRejected);
            return result;
        }
        const WreckwaterCharacterStatus submitted =
            nextAuthority.submitInput(slot.input);
        if (submitted != WreckwaterCharacterStatus::Accepted) {
            result.status = mapMovementStatus(submitted);
            result.movementStatus = submitted;
            incrementSaturated(telemetry_.poseTicksRejected);
            return result;
        }
    }

    const WreckwaterCharacterTickResult movement =
        nextAuthority.closeExactTick(
            frame.tick,
            std::span<
                const WreckwaterCharacterPlatformSample>(
                frame.platforms.data(),
                kWreckwaterMaximumCharacterPlatforms));
    result.movementStatus = movement.status;
    if (!movement) {
        result.status =
            WreckwaterCharacterAuthorityBridgeStatus::PoseRejected;
        incrementSaturated(telemetry_.poseTicksRejected);
        return result;
    }

    std::array<
        WreckwaterCharacterAuthorityBridgeTransition,
        kWreckwaterCharacterAuthorityBridgeMaximumTransitions>
        nextTransitions{};
    size_t nextTransitionCount = 0u;
    for (const WreckwaterCharacterTransition& source :
         movement.transitions) {
        if (nextTransitionCount >= nextTransitions.size()) {
            result.status =
                WreckwaterCharacterAuthorityBridgeStatus::
                    PoseRejected;
            result.movementStatus =
                WreckwaterCharacterStatus::
                    TransitionCapacityExceeded;
            incrementSaturated(telemetry_.poseTicksRejected);
            return result;
        }
        uint32_t connectionGeneration = 0u;
        for (const auto& playerState : players_) {
            if (playerState.character == source.character) {
                connectionGeneration =
                    playerState.connectionGeneration;
                break;
            }
        }
        nextTransitions[nextTransitionCount++] = {
            .kind =
                WreckwaterCharacterAuthorityBridgeTransitionKind::
                    Movement,
            .certifiedTick = source.tick,
            .character = source.character,
            .playerId = source.playerId,
            .priorConnectionGeneration = connectionGeneration,
            .nextConnectionGeneration = connectionGeneration,
            .from = source.from,
            .to = source.to,
            .skiffId = source.skiffId,
            .skiffGeneration = source.skiffGeneration,
            .skiffBody = source.skiffBody,
        };
    }

    authority_ = nextAuthority;
    for (size_t playerIndex = 0u;
         playerIndex < players_.size(); ++playerIndex) {
        const size_t slotIndex = inputSlotIndex(frame.tick);
        InputSlot& slot = inputHistory_[playerIndex][slotIndex];
        if (slot.occupied && slot.tick == frame.tick) slot = {};
    }
    transitionOutput_ = nextTransitions;
    transitionOutputCount_ = nextTransitionCount;
    refreshPlayerState();
    updateStateHash();

    incrementSaturated(telemetry_.poseTicksAccepted);
    addSaturated(
        telemetry_.inputsApplied,
        static_cast<uint64_t>(movement.appliedInputCount));
    addSaturated(
        telemetry_.neutralInputsApplied,
        static_cast<uint64_t>(movement.neutralInputCount));
    addSaturated(
        telemetry_.movementTransitions, movement.transitions.size());
    result.status =
        WreckwaterCharacterAuthorityBridgeStatus::Accepted;
    result.appliedInputCount = movement.appliedInputCount;
    result.neutralInputCount = movement.neutralInputCount;
    result.transitions = {
        transitionOutput_.data(), transitionOutputCount_};
    result.stateHash = state_.stateHash;
    return result;
}

void WreckwaterCharacterAuthorityBridge::refreshPlayerState() noexcept {
    uint32_t totalBuffered = 0u;
    for (size_t playerIndex = 0u;
         playerIndex < players_.size(); ++playerIndex) {
        auto& playerState = players_[playerIndex];
        playerState.bufferedInputCount = 0u;
        playerState.oldestBufferedTick = 0u;
        playerState.latestBufferedTick = 0u;
        for (const InputSlot& slot : inputHistory_[playerIndex]) {
            if (!slot.occupied) continue;
            ++playerState.bufferedInputCount;
            if (playerState.oldestBufferedTick == 0u
                || slot.tick < playerState.oldestBufferedTick) {
                playerState.oldestBufferedTick = slot.tick;
            }
            playerState.latestBufferedTick = std::max(
                playerState.latestBufferedTick, slot.tick);
        }
        totalBuffered += playerState.bufferedInputCount;
        const WreckwaterCharacterState* character =
            authority_.character(playerState.character);
        if (character != nullptr) {
            playerState.connectionGeneration =
                character->connectionGeneration;
            playerState.connected = character->connected;
        }
    }
    state_.initialized = initialized_;
    state_.lastCertifiedTick = authority_.lastClosedTick();
    state_.authorityStateHash = authority_.stateHash();
    state_.bufferedInputCount = totalBuffered;
}

void WreckwaterCharacterAuthorityBridge::updateStateHash() noexcept {
    uint64_t hash = kFnvOffset64;
    hashU32(
        hash, kWreckwaterCharacterAuthorityBridgeSchemaVersion);
    hashU32(hash, config_.maximumReadbackLagTicks);
    hashU64(hash, authority_.stateHash());
    for (size_t playerIndex = 0u;
         playerIndex < players_.size(); ++playerIndex) {
        const auto& playerState = players_[playerIndex];
        hashU64(hash, playerState.playerId);
        hashU32(hash, playerState.character);
        hashU32(hash, playerState.connectionGeneration);
        hashU32(hash, playerState.connected ? 1u : 0u);
        for (const InputSlot& slot : inputHistory_[playerIndex]) {
            hashU32(hash, slot.occupied ? 1u : 0u);
            hashU64(hash, slot.tick);
            hashInput(hash, slot.input);
        }
    }
    state_.stateHash = hash;
}

const WreckwaterCharacterAuthorityBridgePlayerState*
WreckwaterCharacterAuthorityBridge::player(
    PlayerId playerId) const noexcept {
    if (!initialized_ || playerId == 0u) return nullptr;
    for (const auto& playerState : players_) {
        if (playerState.playerId == playerId) return &playerState;
    }
    return nullptr;
}

WreckwaterCharacterAuthorityBridgeStorageState
WreckwaterCharacterAuthorityBridge::storageState() const noexcept {
    return {
        .players = players_.data(),
        .inputHistory = inputHistory_.data(),
        .transitions = transitionOutput_.data(),
        .playerCapacity = players_.size(),
        .inputCapacityPerPlayer =
            kWreckwaterCharacterAuthorityBridgeInputRingSlots,
        .transitionCapacity = transitionOutput_.size(),
        .authority = authority_.storageState(),
    };
}

} // namespace voxy::game
