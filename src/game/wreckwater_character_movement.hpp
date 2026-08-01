#pragma once

#include "game/wreckwater_match.hpp"
#include "physics/physics_types.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#include <glm/gtc/quaternion.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

namespace voxy::game {

inline constexpr uint32_t kWreckwaterCharacterMovementSchemaVersion = 1u;
inline constexpr size_t kWreckwaterMaximumCharacters =
    kWreckwaterPlayerCount;
inline constexpr size_t kWreckwaterMaximumCharacterPlatforms =
    kWreckwaterSkiffCount;
inline constexpr size_t kWreckwaterMaximumCharacterTransitionsPerTick =
    kWreckwaterMaximumCharacters;

using WreckwaterCharacterHandle = physics::CharacterHandle;
inline constexpr WreckwaterCharacterHandle kInvalidWreckwaterCharacter =
    physics::InvalidCharacter;

enum class WreckwaterCharacterMode : uint32_t {
    Airborne = 0u,
    OnSkiff,
    Swimming,
};

enum class WreckwaterCharacterStatus : uint32_t {
    Accepted = 0u,
    NotInitialized,
    InvalidConfiguration,
    InvalidInput,
    WrongInputTick,
    NonMonotonicTick,
    CharacterCapacityExceeded,
    PlatformCapacityExceeded,
    DuplicatePlayer,
    DuplicatePlatformIdentity,
    StalePlatformIdentity,
    PlatformMotionDiscontinuity,
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
    StateOutOfRange,
    IdentityExhausted,
    TransitionCapacityExceeded,
};

[[nodiscard]] const char* wreckwaterCharacterStatusName(
    WreckwaterCharacterStatus status) noexcept;

// Pose is the authoritative skiff transform at the exact character tick.
// linearVelocity and angularVelocity are world-space. deckLocalHeight and
// deckHalfExtents describe the walkable top surface in the skiff's frame.
struct WreckwaterCharacterPlatformSample {
    SkiffId skiffId = 0u;
    uint32_t skiffGeneration = 0u;
    physics::BodyHandle body{};
    physics::WorldPosition position{};
    glm::quat orientation{1.0f, 0.0f, 0.0f, 0.0f};
    glm::vec3 linearVelocity{0.0f};
    glm::vec3 angularVelocity{0.0f};
    glm::vec2 deckHalfExtents{0.0f};
    float deckLocalHeight = 0.0f;

    [[nodiscard]] bool operator==(
        const WreckwaterCharacterPlatformSample&) const = default;
};

struct WreckwaterCharacterSpawn {
    PlayerId playerId = 0u;
    uint32_t connectionGeneration = 0u;
    WreckwaterCharacterMode mode = WreckwaterCharacterMode::Airborne;
    physics::WorldPosition feetPosition{};
    glm::vec3 worldVelocity{0.0f};

    // Required only for OnSkiff. The first matching platform sample makes
    // feetPosition exact from skiffLocalFeetPosition.
    SkiffId skiffId = 0u;
    uint32_t skiffGeneration = 0u;
    physics::BodyHandle skiffBody{};
    glm::vec3 skiffLocalFeetPosition{0.0f};
    glm::vec3 skiffLocalVelocity{0.0f};
};

struct WreckwaterCharacterSpawnResult {
    WreckwaterCharacterStatus status =
        WreckwaterCharacterStatus::NotInitialized;
    WreckwaterCharacterHandle handle = kInvalidWreckwaterCharacter;

    [[nodiscard]] explicit operator bool() const noexcept {
        return status == WreckwaterCharacterStatus::Accepted
            && handle != kInvalidWreckwaterCharacter;
    }
};

// move is a normalized skiff-local X/Z request while aboard and a world X/Z
// request in air or water. jump jumps from a deck or strokes upward in water.
// board requests bounded edge climb assistance while swimming.
struct WreckwaterCharacterInput {
    uint64_t targetTick = 0u;
    // Independent movement ordering domain, reset by reconnectCharacter().
    uint64_t characterInputSequence = 0u;
    WreckwaterCharacterHandle character = kInvalidWreckwaterCharacter;
    PlayerId playerId = 0u;
    uint32_t connectionGeneration = 0u;
    glm::vec2 move{0.0f};
    bool jump = false;
    bool board = false;
};

struct WreckwaterCharacterState {
    WreckwaterCharacterHandle handle = kInvalidWreckwaterCharacter;
    PlayerId playerId = 0u;
    uint32_t connectionGeneration = 0u;
    WreckwaterCharacterMode mode = WreckwaterCharacterMode::Airborne;
    physics::WorldPosition feetPosition{};
    glm::vec3 worldVelocity{0.0f};
    SkiffId skiffId = 0u;
    uint32_t skiffGeneration = 0u;
    physics::BodyHandle skiffBody{};
    glm::vec3 skiffLocalFeetPosition{0.0f};
    glm::vec3 skiffLocalVelocity{0.0f};
    uint64_t latestCharacterInputSequence = 0u;
    uint64_t lastAppliedCharacterInputSequence = 0u;
    uint64_t lastInputTick = 0u;
    uint64_t lastTransitionTick = 0u;
    bool active = false;
    bool connected = false;

    [[nodiscard]] bool operator==(
        const WreckwaterCharacterState&) const = default;
};

struct WreckwaterCharacterTransition {
    uint64_t tick = 0u;
    WreckwaterCharacterHandle character = kInvalidWreckwaterCharacter;
    PlayerId playerId = 0u;
    WreckwaterCharacterMode from = WreckwaterCharacterMode::Airborne;
    WreckwaterCharacterMode to = WreckwaterCharacterMode::Airborne;
    SkiffId skiffId = 0u;
    uint32_t skiffGeneration = 0u;
    physics::BodyHandle skiffBody{};

    [[nodiscard]] bool operator==(
        const WreckwaterCharacterTransition&) const = default;
};

struct WreckwaterCharacterTickResult {
    WreckwaterCharacterStatus status =
        WreckwaterCharacterStatus::NotInitialized;
    uint64_t tick = 0u;
    uint32_t appliedInputCount = 0u;
    uint32_t neutralInputCount = 0u;
    std::span<const WreckwaterCharacterTransition> transitions{};
    uint64_t stateHash = 0u;

    [[nodiscard]] explicit operator bool() const noexcept {
        return status == WreckwaterCharacterStatus::Accepted;
    }
};

struct WreckwaterCharacterStorageState {
    const void* characters = nullptr;
    const void* platformHistory = nullptr;
    const void* pendingInputs = nullptr;
    const void* transitions = nullptr;
    size_t characterCapacity = 0u;
    size_t platformCapacity = 0u;
    size_t pendingInputCapacity = 0u;
    size_t transitionCapacity = 0u;

    [[nodiscard]] bool operator==(
        const WreckwaterCharacterStorageState&) const = default;
};

// A bounded, single-threaded, authoritative 2v2 movement core. It owns no
// dynamic storage. Every accepted tick is exactly 1/60 second. Platform frames
// are validated and staged before any live character state is changed.
class WreckwaterCharacterMovementAuthority {
public:
    struct Config {
        uint32_t tickRateHz = kWreckwaterTickRateHz;
        uint32_t maximumCharacters = kWreckwaterPlayerCount;

        physics::CharacterSettings capsule{};
        float deckMaximumSpeed = 5.0f;
        float deckAcceleration = 32.0f;
        float deckBraking = 40.0f;
        float airMaximumSpeed = 4.0f;
        float airAcceleration = 8.0f;
        float jumpSpeed = 6.0f;
        float gravity = 19.62f;
        float terminalVelocity = 40.0f;

        float waterHeight = 0.0f;
        float waterEnterDepth = 0.10f;
        float waterExitHeight = 0.20f;
        float swimFullSubmersionDepth = 1.20f;
        float swimBuoyancyRatio = 2.10f;
        float waterLinearDrag = 2.25f;
        float swimMaximumSpeed = 3.25f;
        float swimAcceleration = 10.0f;
        float swimStrokeAcceleration = 14.0f;

        float boardAssistHorizontalReach = 0.80f;
        float boardAssistMaximumClimb = 1.25f;
        float boardAssistMaximumDrop = 0.35f;
        float boardAssistMaximumRelativeSpeed = 8.0f;
        float landingSkin = 0.10f;
        float minimumWalkableDeckUp = 0.70710678f;

        float maximumDeckHalfExtent = 64.0f;
        float maximumPlatformLinearSpeed = 300.0f;
        float maximumPlatformAngularSpeed = 40.0f;
        float maximumPlatformLinearAcceleration = 600.0f;
        float maximumPlatformAngularAcceleration = 240.0f;
        float platformPositionTolerance = 0.01f;
        float platformAngleTolerance = 0.005f;
        float maximumCharacterWorldSpeed = 8'192.0f;
        uint32_t maximumInputSequenceAdvance = 1'024u;
    };

    [[nodiscard]] static bool validConfig(
        const Config& config) noexcept;

    [[nodiscard]] bool initialize() noexcept;
    [[nodiscard]] bool initialize(const Config& config) noexcept;

    [[nodiscard]] WreckwaterCharacterSpawnResult spawnCharacter(
        const WreckwaterCharacterSpawn& spawn) noexcept;
    [[nodiscard]] WreckwaterCharacterStatus destroyCharacter(
        WreckwaterCharacterHandle character, PlayerId playerId) noexcept;

    [[nodiscard]] WreckwaterCharacterStatus disconnectCharacter(
        WreckwaterCharacterHandle character, PlayerId playerId,
        uint32_t connectionGeneration) noexcept;
    [[nodiscard]] WreckwaterCharacterStatus reconnectCharacter(
        WreckwaterCharacterHandle character, PlayerId playerId,
        uint32_t priorConnectionGeneration,
        uint32_t nextConnectionGeneration) noexcept;

    [[nodiscard]] WreckwaterCharacterStatus submitInput(
        const WreckwaterCharacterInput& input) noexcept;
    [[nodiscard]] WreckwaterCharacterTickResult closeExactTick(
        uint64_t tick,
        std::span<const WreckwaterCharacterPlatformSample> platforms)
        noexcept;

    [[nodiscard]] bool initialized() const noexcept {
        return initialized_;
    }
    [[nodiscard]] uint64_t lastClosedTick() const noexcept {
        return lastClosedTick_;
    }
    [[nodiscard]] uint64_t stateHash() const noexcept {
        return stateHash_;
    }
    [[nodiscard]] const Config& config() const noexcept {
        return config_;
    }
    [[nodiscard]] std::span<const WreckwaterCharacterState>
    characters() const noexcept {
        return characters_;
    }
    [[nodiscard]] const WreckwaterCharacterState* character(
        WreckwaterCharacterHandle handle) const noexcept;
    [[nodiscard]] WreckwaterCharacterStorageState storageState()
        const noexcept;

private:
    struct PendingInput {
        WreckwaterCharacterInput input{};
        bool occupied = false;
    };

    struct PlatformHistory {
        WreckwaterCharacterPlatformSample sample{};
        bool known = false;
        bool available = false;
    };

    [[nodiscard]] static bool validMode(
        WreckwaterCharacterMode mode) noexcept;
    [[nodiscard]] static bool validPlatform(
        const Config& config,
        WreckwaterCharacterPlatformSample& platform) noexcept;
    [[nodiscard]] WreckwaterCharacterStatus findCharacterIndex(
        WreckwaterCharacterHandle character, size_t& index) const noexcept;
    void updateStateHash() noexcept;

    Config config_{};
    std::array<WreckwaterCharacterState,
               kWreckwaterMaximumCharacters> characters_{};
    std::array<uint16_t, kWreckwaterMaximumCharacters> generations_{};
    std::array<PendingInput,
               kWreckwaterMaximumCharacters> pendingInputs_{};
    std::array<PlatformHistory,
               kWreckwaterMaximumCharacterPlatforms> platformHistory_{};
    std::array<WreckwaterCharacterTransition,
               kWreckwaterMaximumCharacterTransitionsPerTick>
        transitionOutput_{};
    size_t transitionOutputCount_ = 0u;
    uint64_t lastClosedTick_ = 0u;
    uint64_t configurationHash_ = 0u;
    uint64_t stateHash_ = 0u;
    bool initialized_ = false;
};

// Shared one-tick logical-lifetime and rigid-motion fence. The caller invokes
// this only for a known logical skiff; `previousAvailable` distinguishes a
// continuous sample from a same-generation reappearance.
// Both authority and prediction use this exact policy before stepping state.
[[nodiscard]] WreckwaterCharacterStatus
wreckwaterCharacterPlatformSuccessorStatus(
    const WreckwaterCharacterMovementAuthority::Config& config,
    const WreckwaterCharacterPlatformSample& previous,
    bool previousAvailable,
    const WreckwaterCharacterPlatformSample& current) noexcept;

} // namespace voxy::game
