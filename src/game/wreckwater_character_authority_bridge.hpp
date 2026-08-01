#pragma once

#include "game/wreckwater_character_movement.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace voxy::game {

inline constexpr uint32_t
    kWreckwaterCharacterAuthorityBridgeSchemaVersion = 1u;
inline constexpr uint32_t
    kWreckwaterCharacterAuthorityBridgeMaximumReadbackLagTicks = 16u;
inline constexpr size_t
    kWreckwaterCharacterAuthorityBridgeInputRingSlots =
        kWreckwaterCharacterAuthorityBridgeMaximumReadbackLagTicks + 1u;
inline constexpr size_t
    kWreckwaterCharacterAuthorityBridgeMaximumTransitions =
        kWreckwaterMaximumCharacters;
static_assert(
    kWreckwaterCharacterAuthorityBridgeInputRingSlots
    > kWreckwaterCharacterAuthorityBridgeMaximumReadbackLagTicks);
static_assert(
    kWreckwaterCharacterAuthorityBridgeMaximumTransitions
    == kWreckwaterPlayerCount);

enum class WreckwaterCharacterAuthorityBridgeStatus : uint32_t {
    Accepted = 0u,
    IgnoredLowerSequence,
    NotInitialized,
    InvalidConfiguration,
    InvalidRoster,
    InvalidInput,
    UnknownPlayer,
    UnknownCharacter,
    StaleCharacterIdentity,
    PlayerMismatch,
    Disconnected,
    AlreadyDisconnected,
    AlreadyConnected,
    StaleConnectionGeneration,
    ConnectionGenerationExhausted,
    ReplayedInputSequence,
    InputSequenceExhausted,
    InputSequenceJumpTooLarge,
    StaleInputTick,
    InputFutureWindowExceeded,
    InputHistoryOverflow,
    StalePoseTick,
    PoseTickGap,
    PoseReadbackOverflow,
    PosePlatformCountMismatch,
    PoseRejected,
    TickExhausted,
};

[[nodiscard]] const char*
wreckwaterCharacterAuthorityBridgeStatusName(
    WreckwaterCharacterAuthorityBridgeStatus status) noexcept;

struct WreckwaterCharacterCertifiedPlatformTick {
    uint64_t tick = 0u;
    std::array<
        WreckwaterCharacterPlatformSample,
        kWreckwaterMaximumCharacterPlatforms> platforms{};
    uint32_t platformCount =
        static_cast<uint32_t>(kWreckwaterMaximumCharacterPlatforms);
    bool overflow = false;
};

enum class WreckwaterCharacterAuthorityBridgeTransitionKind : uint32_t {
    Movement = 0u,
    Disconnected,
    Reconnected,
};

struct WreckwaterCharacterAuthorityBridgeTransition {
    WreckwaterCharacterAuthorityBridgeTransitionKind kind =
        WreckwaterCharacterAuthorityBridgeTransitionKind::Movement;
    uint64_t certifiedTick = 0u;
    WreckwaterCharacterHandle character =
        kInvalidWreckwaterCharacter;
    PlayerId playerId = 0u;
    uint32_t priorConnectionGeneration = 0u;
    uint32_t nextConnectionGeneration = 0u;
    WreckwaterCharacterMode from =
        WreckwaterCharacterMode::Airborne;
    WreckwaterCharacterMode to =
        WreckwaterCharacterMode::Airborne;
    SkiffId skiffId = 0u;
    uint32_t skiffGeneration = 0u;
    physics::BodyHandle skiffBody{};

    [[nodiscard]] bool operator==(
        const WreckwaterCharacterAuthorityBridgeTransition&) const =
        default;
};

struct WreckwaterCharacterAuthorityBridgeTickResult {
    WreckwaterCharacterAuthorityBridgeStatus status =
        WreckwaterCharacterAuthorityBridgeStatus::NotInitialized;
    WreckwaterCharacterStatus movementStatus =
        WreckwaterCharacterStatus::NotInitialized;
    uint64_t tick = 0u;
    uint32_t appliedInputCount = 0u;
    uint32_t neutralInputCount = 0u;
    std::span<
        const WreckwaterCharacterAuthorityBridgeTransition>
        transitions{};
    uint64_t stateHash = 0u;

    [[nodiscard]] explicit operator bool() const noexcept {
        return status
            == WreckwaterCharacterAuthorityBridgeStatus::Accepted;
    }
};

struct WreckwaterCharacterAuthorityBridgePlayerState {
    PlayerId playerId = 0u;
    WreckwaterCharacterHandle character =
        kInvalidWreckwaterCharacter;
    uint32_t connectionGeneration = 0u;
    uint32_t bufferedInputCount = 0u;
    uint64_t oldestBufferedTick = 0u;
    uint64_t latestBufferedTick = 0u;
    bool connected = false;

    [[nodiscard]] bool operator==(
        const WreckwaterCharacterAuthorityBridgePlayerState&) const =
        default;
};

struct WreckwaterCharacterAuthorityBridgeState {
    uint64_t lastCertifiedTick = 0u;
    uint64_t authorityStateHash = 0u;
    uint64_t stateHash = 0u;
    uint32_t bufferedInputCount = 0u;
    bool initialized = false;

    [[nodiscard]] bool operator==(
        const WreckwaterCharacterAuthorityBridgeState&) const =
        default;
};

struct WreckwaterCharacterAuthorityBridgeTelemetry {
    uint64_t inputPacketsAccepted = 0u;
    uint64_t inputPacketsSuperseded = 0u;
    uint64_t inputPacketsRejected = 0u;
    uint64_t poseTicksAccepted = 0u;
    uint64_t poseTicksRejected = 0u;
    uint64_t inputsApplied = 0u;
    uint64_t neutralInputsApplied = 0u;
    uint64_t disconnects = 0u;
    uint64_t reconnects = 0u;
    uint64_t lifecycleOperationsRejected = 0u;
    uint64_t movementTransitions = 0u;
    uint32_t maximumBufferedInputs = 0u;

    [[nodiscard]] bool operator==(
        const WreckwaterCharacterAuthorityBridgeTelemetry&) const =
        default;
};

struct WreckwaterCharacterAuthorityBridgeStorageState {
    const void* players = nullptr;
    const void* inputHistory = nullptr;
    const void* transitions = nullptr;
    size_t playerCapacity = 0u;
    size_t inputCapacityPerPlayer = 0u;
    size_t transitionCapacity = 0u;
    WreckwaterCharacterStorageState authority{};

    [[nodiscard]] bool operator==(
        const WreckwaterCharacterAuthorityBridgeStorageState&) const =
        default;
};

// Fixed-roster adapter between asynchronous certified skiff poses and the
// exact-tick movement core. It owns no dynamic storage. Inputs can lead the
// certified frontier by at most maximumReadbackLagTicks.
class WreckwaterCharacterAuthorityBridge {
public:
    struct Config {
        WreckwaterCharacterMovementAuthority::Config movement{};
        uint32_t maximumReadbackLagTicks =
            kWreckwaterCharacterAuthorityBridgeMaximumReadbackLagTicks;
    };

    [[nodiscard]] bool initialize(
        const std::array<
            WreckwaterCharacterSpawn,
            kWreckwaterMaximumCharacters>& roster) noexcept;
    [[nodiscard]] bool initialize(
        const Config& config,
        const std::array<
            WreckwaterCharacterSpawn,
            kWreckwaterMaximumCharacters>& roster) noexcept;

    [[nodiscard]] WreckwaterCharacterAuthorityBridgeStatus
    submitInput(const WreckwaterCharacterInput& input) noexcept;

    [[nodiscard]] WreckwaterCharacterAuthorityBridgeStatus
    disconnectCharacter(
        WreckwaterCharacterHandle character, PlayerId playerId,
        uint32_t connectionGeneration) noexcept;
    [[nodiscard]] WreckwaterCharacterAuthorityBridgeStatus
    reconnectCharacter(
        WreckwaterCharacterHandle character, PlayerId playerId,
        uint32_t priorConnectionGeneration,
        uint32_t nextConnectionGeneration) noexcept;

    [[nodiscard]] WreckwaterCharacterAuthorityBridgeTickResult
    closeCertifiedPlatformTick(
        const WreckwaterCharacterCertifiedPlatformTick& frame)
        noexcept;

    [[nodiscard]] bool initialized() const noexcept {
        return initialized_;
    }
    [[nodiscard]] const Config& config() const noexcept {
        return config_;
    }
    [[nodiscard]] const WreckwaterCharacterMovementAuthority&
    authority() const noexcept {
        return authority_;
    }
    [[nodiscard]] std::span<
        const WreckwaterCharacterAuthorityBridgePlayerState>
    players() const noexcept {
        return players_;
    }
    [[nodiscard]] const
        WreckwaterCharacterAuthorityBridgePlayerState*
    player(PlayerId playerId) const noexcept;
    [[nodiscard]] std::span<
        const WreckwaterCharacterAuthorityBridgeTransition>
    lastTransitions() const noexcept {
        return {
            transitionOutput_.data(), transitionOutputCount_};
    }
    [[nodiscard]] const
        WreckwaterCharacterAuthorityBridgeTelemetry&
    telemetry() const noexcept {
        return telemetry_;
    }
    [[nodiscard]] const WreckwaterCharacterAuthorityBridgeState&
    state() const noexcept {
        return state_;
    }
    [[nodiscard]] WreckwaterCharacterAuthorityBridgeStorageState
    storageState() const noexcept;

private:
    struct InputSlot {
        WreckwaterCharacterInput input{};
        uint64_t tick = 0u;
        bool occupied = false;
    };

    [[nodiscard]] static
        WreckwaterCharacterAuthorityBridgeStatus
    mapMovementStatus(WreckwaterCharacterStatus status) noexcept;
    [[nodiscard]] WreckwaterCharacterAuthorityBridgeStatus
    findPlayerIndex(
        WreckwaterCharacterHandle character, PlayerId playerId,
        size_t& index) const noexcept;
    [[nodiscard]] bool validSequenceInsertion(
        size_t playerIndex, uint64_t targetTick,
        uint64_t characterInputSequence) const noexcept;
    void refreshPlayerState() noexcept;
    void updateStateHash() noexcept;
    void clearTransitionOutput() noexcept;
    void setLifecycleTransition(
        WreckwaterCharacterAuthorityBridgeTransitionKind kind,
        size_t playerIndex, uint32_t priorGeneration,
        uint32_t nextGeneration) noexcept;

    Config config_{};
    WreckwaterCharacterMovementAuthority authority_{};
    std::array<
        WreckwaterCharacterAuthorityBridgePlayerState,
        kWreckwaterMaximumCharacters> players_{};
    std::array<
        std::array<
            InputSlot,
            kWreckwaterCharacterAuthorityBridgeInputRingSlots>,
        kWreckwaterMaximumCharacters> inputHistory_{};
    std::array<
        WreckwaterCharacterAuthorityBridgeTransition,
        kWreckwaterCharacterAuthorityBridgeMaximumTransitions>
        transitionOutput_{};
    size_t transitionOutputCount_ = 0u;
    WreckwaterCharacterAuthorityBridgeTelemetry telemetry_{};
    WreckwaterCharacterAuthorityBridgeState state_{};
    bool initialized_ = false;
};

} // namespace voxy::game
