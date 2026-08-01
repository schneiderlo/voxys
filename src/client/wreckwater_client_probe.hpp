#pragma once

#include "network/native_tcp_transport.hpp"
#include "network/wreckwater_client_replication.hpp"
#include "network/wreckwater_protocol.hpp"

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace voxy::client {

inline constexpr uint32_t kWreckwaterProbePeerCount = 4u;
inline constexpr uint64_t kWreckwaterProbeDefaultMaximumPumpTicks = 900u;
inline constexpr uint64_t kWreckwaterProbeMaximumPumpTicks =
    network::kWreckwaterMaximumApplicationTick;
inline constexpr uint64_t kWreckwaterProbeMaximumActionLeadTicks = 4u;
inline constexpr uint32_t
    kWreckwaterProbeChaosMinimumDelayServiceQuanta = 5u;
inline constexpr uint32_t
    kWreckwaterProbeChaosMaximumDelayServiceQuanta = 10u;
inline constexpr uint32_t
    kWreckwaterProbeAuthorityCharacterLeadWindowTicks = 16u;
// A certified snapshot can spend 10 service quanta inbound and its movement
// input another 10 outbound. A 24-tick target leaves four exact ticks on the
// slow path. On the fast 5 + 5 path it reaches the authority/character bridge
// at most 14 ticks ahead, inside their 16-tick proof windows.
inline constexpr uint64_t kWreckwaterProbeCharacterTargetLeadTicks = 24u;
inline constexpr int16_t kWreckwaterProbeCharacterMoveQ15 = 24'576;
inline constexpr uint64_t
    kWreckwaterProbeMinimumCertifiedDeckDisplacementMillimetres = 50u;
static_assert(
    kWreckwaterProbeCharacterTargetLeadTicks
    > 2u * kWreckwaterProbeChaosMaximumDelayServiceQuanta);
static_assert(
    kWreckwaterProbeCharacterTargetLeadTicks
        - 2u * kWreckwaterProbeChaosMinimumDelayServiceQuanta
    <= kWreckwaterProbeAuthorityCharacterLeadWindowTicks);
// The chaos proof can add 20 service quanta of round-trip delay. At the
// 20 Hz snapshot cadence, 16 snapshots leave a bounded retry margin without
// issuing a second logical command before the first result can return.
inline constexpr uint64_t kWreckwaterProbeCargoRetrySnapshots = 16u;
inline constexpr uint64_t kWreckwaterProbeSnapshotHeartbeatTicks = 60u;

struct WreckwaterClientProbeOptions {
    std::string serverAddress = "127.0.0.1";
    uint16_t serverPort = 7777u;
    uint32_t peerId = 0u;
    network::NativeTcpAuthenticationKey authenticationKey{};
    uint64_t sessionId = 1u;
    uint64_t matchId = 1u;
    uint64_t worldId = 1u;
    uint32_t worldEpoch = 1u;
    uint32_t authorityEpoch = 1u;
    uint64_t maximumPumpTicks =
        kWreckwaterProbeDefaultMaximumPumpTicks;
    uint64_t reconnectAtPumpTick = 0u;
    // Zero disables the deterministic application-frame adversity decorator.
    uint64_t chaosSeed = 0u;
    bool help = false;
};

enum class WreckwaterClientProbeOptionError : uint32_t {
    None = 0u,
    UnknownOption,
    MissingValue,
    DuplicateOption,
    InvalidServerAddress,
    InvalidPort,
    InvalidPeerId,
    InvalidAuthenticationKey,
    MissingPeerId,
    MissingAuthenticationKey,
    InvalidIdentity,
    InvalidMaximumPumpTicks,
    InvalidReconnectTick,
    InvalidChaosSeed,
};

struct WreckwaterClientProbeOptionResult {
    WreckwaterClientProbeOptionError error =
        WreckwaterClientProbeOptionError::None;

    [[nodiscard]] explicit operator bool() const noexcept {
        return error == WreckwaterClientProbeOptionError::None;
    }
};

[[nodiscard]] const char* wreckwaterClientProbeOptionErrorName(
    WreckwaterClientProbeOptionError error) noexcept;

// Arguments exclude argv[0]. Authentication text is decoded directly into
// fixed bytes and is never retained as a string by this parser.
[[nodiscard]] WreckwaterClientProbeOptionResult
parseWreckwaterClientProbeOptions(
    std::span<const std::string_view> arguments,
    WreckwaterClientProbeOptions& options);

[[nodiscard]] bool wreckwaterProbeIsHelmPeer(
    uint32_t peerId) noexcept;
[[nodiscard]] bool wreckwaterProbeIsDeckPeer(
    uint32_t peerId) noexcept;

// Bounds operational snapshot logs while retaining the first observation,
// logical match/cargo transitions, and one heartbeat per simulated second.
class WreckwaterClientProbeSnapshotLogGate {
public:
    [[nodiscard]] bool shouldLog(
        const network::WreckwaterClientSample& sample) noexcept;

private:
    uint64_t applicationTick_ = 0u;
    network::WreckwaterPhase phase_ =
        network::WreckwaterPhase::Warmup;
    network::WreckwaterOutcomeType outcome_ =
        network::WreckwaterOutcomeType::Undecided;
    network::WreckwaterCrew winner_ = network::WreckwaterCrew::None;
    uint32_t crewOneScore_ = 0u;
    uint32_t crewTwoScore_ = 0u;
    std::optional<network::WreckwaterCargoLogicalState> cargo_;
    bool initialized_ = false;
};

struct WreckwaterClientProbeAction {
    network::WreckwaterAction action =
        network::WreckwaterAction::Helm;
    uint64_t requestedApplicationTick = 0u;
    int16_t helmThrottleQ15 = 0;
    int16_t helmSteeringQ15 = 0;
    uint32_t cargoId = 0u;
    uint32_t cargoGeneration = 0u;
    uint32_t observedCargoRevision = 0u;
    uint64_t sourceSnapshotSequence = 0u;

    [[nodiscard]] bool operator==(
        const WreckwaterClientProbeAction&) const = default;
};

// Deterministic proof traffic. It derives cargo requests only from exact
// authoritative snapshot fields. A pending request remains byte-for-byte
// stable until the transport accepts it.
class WreckwaterClientProbeScript {
public:
    explicit WreckwaterClientProbeScript(uint32_t peerId) noexcept;

    [[nodiscard]] bool initialized() const noexcept {
        return initialized_;
    }

    [[nodiscard]] std::optional<WreckwaterClientProbeAction> plan(
        const network::WreckwaterClientSample& sample) noexcept;

    [[nodiscard]] bool markSent(
        const WreckwaterClientProbeAction& action) noexcept;

    [[nodiscard]] uint64_t lastSuccessfulRequestTick() const noexcept {
        return lastSuccessfulRequestTick_;
    }
    [[nodiscard]] bool hasPendingAction() const noexcept {
        return pending_.has_value();
    }

private:
    [[nodiscard]] std::optional<WreckwaterClientProbeAction>
    planHelm(
        const network::WreckwaterClientSample& sample) noexcept;
    [[nodiscard]] std::optional<WreckwaterClientProbeAction>
    planDeck(
        const network::WreckwaterClientSample& sample) noexcept;

    uint32_t peerId_ = 0u;
    uint64_t lastSuccessfulRequestTick_ = 0u;
    uint64_t lastEvaluatedDeckSnapshotSequence_ = 0u;
    uint64_t lastCargoAttemptSnapshotSequence_ = 0u;
    uint32_t lastCargoAttemptGeneration_ = 0u;
    uint32_t lastCargoAttemptRevision_ = 0u;
    std::optional<WreckwaterClientProbeAction> pending_;
    bool initialized_ = false;
};

struct WreckwaterClientProbeCharacterInput {
    uint64_t requestedApplicationTick = 0u;
    int16_t moveXQ15 = 0;
    int16_t moveZQ15 = 0;
    bool jump = false;
    bool board = false;
    uint64_t sourceSnapshotSequence = 0u;
    uint64_t sourceEvidenceTick = 0u;
    uint32_t sourceConnectionGeneration = 0u;

    [[nodiscard]] bool operator==(
        const WreckwaterClientProbeCharacterInput&) const = default;
};

// Emits one new exact-tick movement sample per accepted certified snapshot.
// A transport backpressure retry is byte-for-byte stable. Later snapshots
// provide temporal redundancy for realtime loss without replaying one logical
// input or coupling movement ticks to Helm/cargo ticks.
class WreckwaterClientProbeCharacterScript {
public:
    explicit WreckwaterClientProbeCharacterScript(
        uint32_t peerId) noexcept;

    [[nodiscard]] bool initialized() const noexcept {
        return initialized_;
    }

    [[nodiscard]] std::optional<WreckwaterClientProbeCharacterInput>
    plan(const network::WreckwaterClientSample& sample) noexcept;

    [[nodiscard]] bool markSent(
        const WreckwaterClientProbeCharacterInput& input) noexcept;

    void discardPending() noexcept {
        pending_.reset();
    }

    [[nodiscard]] uint64_t lastSuccessfulRequestTick() const noexcept {
        return lastSuccessfulRequestTick_;
    }
    [[nodiscard]] bool hasPendingInput() const noexcept {
        return pending_.has_value();
    }

private:
    uint32_t peerId_ = 0u;
    uint64_t lastSuccessfulRequestTick_ = 0u;
    uint64_t lastEvaluatedSnapshotSequence_ = 0u;
    std::optional<WreckwaterClientProbeCharacterInput> pending_;
    bool initialized_ = false;
};

[[nodiscard]] bool wreckwaterClientProbeHasExactCharacterRoster(
    const network::WreckwaterClientSample& sample) noexcept;

// Canonical field-wise hash of the four authoritative character records.
// Zero means the sample did not contain the exact fixed 2v2 roster.
[[nodiscard]] uint64_t wreckwaterClientProbeCharacterRosterHash(
    const network::WreckwaterClientSample& sample) noexcept;

struct WreckwaterClientProbeFinalSnapshot {
    uint64_t sequence = 0u;
    uint64_t applicationTick = 0u;
    uint64_t evidenceTick = 0u;
    network::WreckwaterPhase phase =
        network::WreckwaterPhase::Warmup;
    network::WreckwaterOutcomeType outcome =
        network::WreckwaterOutcomeType::Undecided;
    network::WreckwaterCrew winner = network::WreckwaterCrew::None;
    uint32_t crewOneScore = 0u;
    uint32_t crewTwoScore = 0u;
    uint32_t stateHash = 0u;
    uint32_t eventHash = 0u;
    uint64_t serializedHash = 0u;

    [[nodiscard]] bool operator==(
        const WreckwaterClientProbeFinalSnapshot&) const = default;
};

[[nodiscard]] WreckwaterClientProbeFinalSnapshot
wreckwaterClientProbeFinalSnapshot(
    const network::WreckwaterClientSample& sample) noexcept;

[[nodiscard]] const char* wreckwaterProbeActionName(
    network::WreckwaterAction action) noexcept;
[[nodiscard]] const char* wreckwaterProbePhaseName(
    network::WreckwaterPhase phase) noexcept;
[[nodiscard]] const char* wreckwaterProbeOutcomeName(
    network::WreckwaterOutcomeType outcome) noexcept;
[[nodiscard]] const char* wreckwaterProbeCrewName(
    network::WreckwaterCrew crew) noexcept;

} // namespace voxy::client
