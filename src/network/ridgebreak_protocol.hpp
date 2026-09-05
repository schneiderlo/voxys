#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace voxy::network {

inline constexpr uint32_t kRidgebreakWireSchemaVersion = 1u;
inline constexpr uint32_t kRidgebreakMaximumPlayers = 4u;
inline constexpr uint32_t kRidgebreakMaximumRedundantInputs = 3u;

enum RidgebreakInputFlag : uint16_t {
    RidgebreakInputStanding = 1u << 0u,
    RidgebreakInputDucking = 1u << 1u,
    RidgebreakInputGearUp = 1u << 2u,
    RidgebreakInputGearDown = 1u << 3u,
    RidgebreakInputReset = 1u << 4u,
};

inline constexpr uint16_t kRidgebreakKnownInputFlags =
    RidgebreakInputStanding | RidgebreakInputDucking
    | RidgebreakInputGearUp | RidgebreakInputGearDown
    | RidgebreakInputReset;

struct RidgebreakInputSample {
    uint64_t requestedTick = 0u;
    uint64_t inputSequence = 0u;
    uint16_t throttleQ15 = 0u;
    uint16_t brakeQ15 = 0u;
    int16_t steerQ15 = 0;
    int16_t leanQ15 = 0;
    uint16_t flags = 0u;
    uint16_t reserved = 0u;

    [[nodiscard]] bool operator==(
        const RidgebreakInputSample&) const = default;
};

struct RidgebreakInputBundle {
    uint32_t schemaVersion = kRidgebreakWireSchemaVersion;
    uint32_t sampleCount = 0u;
    uint64_t connectionSerial = 0u;
    uint32_t connectionGeneration = 0u;
    uint32_t reserved = 0u;
    std::array<RidgebreakInputSample, kRidgebreakMaximumRedundantInputs>
        samples{};

    [[nodiscard]] bool operator==(
        const RidgebreakInputBundle&) const = default;
};

enum class RidgebreakPlayerLifecycle : uint8_t {
    Disconnected = 0u,
    Riding = 1u,
    Crashed = 2u,
};

struct RidgebreakPlayerState {
    uint32_t playerId = 0u;
    uint32_t connectionGeneration = 0u;
    uint64_t connectionSerial = 0u;
    uint64_t lastProcessedInputSequence = 0u;
    int32_t positionXMillimeters = 0;
    int32_t positionZMillimeters = 0;
    int32_t velocityXMillimetersPerSecond = 0;
    int32_t velocityZMillimetersPerSecond = 0;
    int32_t headingTurnsQ16 = 0;
    int32_t forwardSpeedMillimetersPerSecond = 0;
    int16_t leanQ15 = 0;
    uint8_t gear = 1u;
    RidgebreakPlayerLifecycle lifecycle =
        RidgebreakPlayerLifecycle::Disconnected;
    uint32_t stateFlags = 0u;

    [[nodiscard]] bool operator==(
        const RidgebreakPlayerState&) const = default;
};

struct RidgebreakSnapshot {
    uint32_t schemaVersion = kRidgebreakWireSchemaVersion;
    uint32_t playerCount = 0u;
    uint64_t authoritativeTick = 0u;
    // Hashes this tick and the canonical replicated player array only. Packet
    // delivery/ack history belongs to the authority transcript hash and must
    // not make equivalent gameplay snapshots disagree.
    uint64_t canonicalStateHash = 0u;
    std::array<RidgebreakPlayerState, kRidgebreakMaximumPlayers> players{};

    [[nodiscard]] bool operator==(
        const RidgebreakSnapshot&) const = default;
};

// Client prediction compares a locally retained state at authoritativeTick
// with this result. Inputs through acknowledgedInputSequence may be discarded;
// newer inputs are replayed after applying the correction.
struct RidgebreakReconciliation {
    uint64_t authoritativeTick = 0u;
    uint64_t acknowledgedInputSequence = 0u;
    int32_t positionErrorXMillimeters = 0;
    int32_t positionErrorZMillimeters = 0;
    int32_t speedErrorMillimetersPerSecond = 0;
    bool hardCorrectionRequired = false;
};

enum class RidgebreakCodecError : uint32_t {
    None = 0u,
    WrongSize,
    WrongSchema,
    InvalidCount,
    InvalidInput,
    InvalidOrdering,
    InvalidPlayer,
};

struct RidgebreakInputDecodeResult {
    std::optional<RidgebreakInputBundle> bundle;
    RidgebreakCodecError error = RidgebreakCodecError::None;
};

struct RidgebreakSnapshotDecodeResult {
    std::optional<RidgebreakSnapshot> snapshot;
    RidgebreakCodecError error = RidgebreakCodecError::None;
};

inline constexpr size_t kRidgebreakInputSampleBytes = 28u;
inline constexpr size_t kRidgebreakInputBundleBytes = 24u
    + kRidgebreakInputSampleBytes * kRidgebreakMaximumRedundantInputs;
inline constexpr size_t kRidgebreakPlayerStateBytes = 56u;
inline constexpr size_t kRidgebreakSnapshotBytes = 24u
    + kRidgebreakPlayerStateBytes * kRidgebreakMaximumPlayers;

[[nodiscard]] std::vector<std::byte> encodeRidgebreakInputBundle(
    const RidgebreakInputBundle& bundle);
[[nodiscard]] RidgebreakInputDecodeResult decodeRidgebreakInputBundle(
    std::span<const std::byte> bytes);

[[nodiscard]] std::vector<std::byte> encodeRidgebreakSnapshot(
    const RidgebreakSnapshot& snapshot);
[[nodiscard]] RidgebreakSnapshotDecodeResult decodeRidgebreakSnapshot(
    std::span<const std::byte> bytes);

[[nodiscard]] RidgebreakReconciliation reconcileRidgebreakPrediction(
    uint64_t authoritativeTick,
    const RidgebreakPlayerState& predictedAtAuthoritativeTick,
    const RidgebreakPlayerState& authoritative,
    uint32_t hardCorrectionDistanceMillimeters = 2'000u) noexcept;

} // namespace voxy::network
