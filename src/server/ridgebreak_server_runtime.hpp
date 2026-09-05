#pragma once

#include "network/session_transport.hpp"
#include "server/ridgebreak_authority.hpp"

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

namespace voxy::server {

enum class RidgebreakServerFault : uint32_t {
    None = 0u,
    InvalidConfiguration,
    InvalidLifecycle,
    ConnectionGenerationExhausted,
    RegistrationRejected,
    AdmissionCommitFailed,
    SnapshotBuildFailed,
};

struct RidgebreakServerTelemetry {
    uint64_t serviceCalls = 0u;
    uint64_t framesPolled = 0u;
    uint64_t lifecycleFrames = 0u;
    uint64_t staleLifecycleFrames = 0u;
    uint64_t registrationRejections = 0u;
    uint64_t inputFramesAccepted = 0u;
    uint64_t inputFramesRejected = 0u;
    uint64_t snapshotsSent = 0u;
    uint64_t snapshotSendFailures = 0u;
    uint64_t snapshotBytesSent = 0u;
    uint64_t snapshotRetransmissionsSent = 0u;
    uint64_t snapshotSupersessions = 0u;
    uint64_t simulationTicks = 0u;
};

struct RidgebreakServerTickResult {
    uint32_t framesPolled = 0u;
    uint32_t inputsAccepted = 0u;
    uint32_t inputsRejected = 0u;
    uint32_t snapshotsSent = 0u;
    RidgebreakIngressError lastIngressError = RidgebreakIngressError::None;
};

class RidgebreakServerRuntime {
public:
    struct Config {
        RidgebreakAuthoritySession::Config authority{};
        uint32_t maximumFramesPerTick = 64u;
        uint32_t snapshotRetryIntervalTicks = 6u;
        uint32_t maximumSnapshotSendAttempts = 8u;
    };

    [[nodiscard]] bool initialize(
        const Config& config,
        std::unique_ptr<network::IMultiplayerTransport> transport) noexcept;
    [[nodiscard]] RidgebreakServerTickResult tickOnce() noexcept;
    void serviceTransportOnly() noexcept;
    [[nodiscard]] uint32_t retransmitLastSnapshot() noexcept;
    void close() noexcept;

    [[nodiscard]] bool faulted() const noexcept {
        return fault_ != RidgebreakServerFault::None;
    }
    [[nodiscard]] RidgebreakServerFault fault() const noexcept {
        return fault_;
    }
    [[nodiscard]] RidgebreakRegistrationError lastRegistrationError()
        const noexcept { return lastRegistrationError_; }
    [[nodiscard]] const RidgebreakServerTelemetry& telemetry() const noexcept {
        return telemetry_;
    }
    [[nodiscard]] const RidgebreakAuthoritySession& authority() const noexcept {
        return authority_;
    }
    [[nodiscard]] const std::optional<network::RidgebreakSnapshot>&
    lastBroadcastSnapshot() const noexcept { return lastBroadcastSnapshot_; }
    [[nodiscard]] uint32_t pendingSnapshotPeers() const noexcept;

private:
    struct Peer {
        uint64_t connectionSerial = 0u;
        uint32_t lastConnectionGeneration = 0u;
        uint64_t pendingSnapshotSequence = 0u;
        uint64_t lastSnapshotAttemptTick = 0u;
        uint32_t snapshotSendAttempts = 0u;
        bool active = false;
    };

    void fail(RidgebreakServerFault fault) noexcept;
    [[nodiscard]] bool processLifecycle(
        const network::MultiplayerTransportFrame& frame) noexcept;
    void retireAcknowledgedSnapshots() noexcept;
    void clearPendingSnapshot(uint32_t index) noexcept;
    [[nodiscard]] bool sendPendingSnapshot(
        uint32_t index, bool retransmission,
        RidgebreakServerTickResult* result = nullptr) noexcept;

    Config config_{};
    std::unique_ptr<network::IMultiplayerTransport> transport_;
    RidgebreakAuthoritySession authority_{};
    std::array<Peer, kRidgebreakAuthorityPlayerCount> peers_{};
    std::array<std::vector<std::byte>,
               kRidgebreakAuthorityPlayerCount> lastSnapshotFrames_{};
    std::array<uint64_t,
               kRidgebreakAuthorityPlayerCount> lastSnapshotSerials_{};
    std::optional<network::RidgebreakSnapshot> lastBroadcastSnapshot_;
    RidgebreakServerTelemetry telemetry_{};
    RidgebreakServerFault fault_ = RidgebreakServerFault::None;
    RidgebreakRegistrationError lastRegistrationError_ =
        RidgebreakRegistrationError::None;
    bool initialized_ = false;
};

} // namespace voxy::server
