#pragma once

#include "network/protocol.hpp"
#include "network/ridgebreak_protocol.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <utility>
#include <vector>

namespace voxy::server {

inline constexpr uint32_t kRidgebreakAuthorityPlayerCount = 4u;
inline constexpr uint32_t kRidgebreakAuthorityTickRate = 60u;
inline constexpr uint32_t kRidgebreakAuthoritySnapshotRate = 20u;
inline constexpr size_t kRidgebreakAuthorityFramedInputBytes =
    network::kNetworkPacketOverheadBytes
    + network::kRidgebreakInputBundleBytes;
inline constexpr size_t kRidgebreakAuthorityFramedSnapshotBytes =
    network::kNetworkPacketOverheadBytes
    + network::kRidgebreakSnapshotBytes;
static_assert(kRidgebreakAuthorityFramedInputBytes == 192u);
static_assert(kRidgebreakAuthorityFramedSnapshotBytes == 332u);
static_assert(kRidgebreakAuthorityFramedSnapshotBytes
              <= network::kConservativeRealtimeMtu);

enum class RidgebreakIngressError : uint32_t {
    None = 0u,
    UnknownPeer,
    InactivePeer,
    WrongDeliveryClass,
    PacketDecodeFailed,
    WrongPayloadType,
    IdentityMismatch,
    InvalidAcknowledgement,
    DuplicatePacket,
    InputDecodeFailed,
    ConnectionIdentityMismatch,
    PacketSequenceJump,
    InputSequenceJump,
    InputOutsideTickWindow,
    InputRateExceeded,
};

enum class RidgebreakRegistrationError : uint32_t {
    None = 0u,
    NotInitialized,
    InvalidIdentity,
    ActivePeerCollision,
    ActivePlayerCollision,
    ReusedConnectionSerial,
    NonIncreasingGeneration,
    NoInactiveSlot,
};

struct RidgebreakIngressResult {
    RidgebreakIngressError error = RidgebreakIngressError::None;
    uint32_t acceptedSamples = 0u;
    uint32_t redundantSamples = 0u;
    uint32_t expiredSamples = 0u;

    [[nodiscard]] explicit operator bool() const noexcept {
        return error == RidgebreakIngressError::None;
    }
};

struct RidgebreakAuthorityTelemetry {
    uint64_t acceptedPackets = 0u;
    uint64_t rejectedPackets = 0u;
    uint64_t duplicatePackets = 0u;
    uint64_t acceptedInputSamples = 0u;
    uint64_t redundantInputSamples = 0u;
    uint64_t expiredInputSamples = 0u;
    uint64_t heldInputTicks = 0u;
    uint64_t neutralInputTicks = 0u;
    uint64_t simulationTicks = 0u;
    uint64_t snapshotsBuilt = 0u;
    uint64_t snapshotBytesBuilt = 0u;
};

struct RidgebreakPeerView {
    uint32_t peerId = 0u;
    uint32_t playerId = 0u;
    uint32_t connectionGeneration = 0u;
    uint64_t connectionSerial = 0u;
    uint64_t latestReceivedPacketSequence = 0u;
    uint64_t latestProcessedInputSequence = 0u;
    uint64_t latestSentSnapshotSequence = 0u;
    uint64_t latestAcknowledgedSnapshotSequence = 0u;
    bool active = false;
};

class RidgebreakAuthoritySession {
public:
    class SnapshotBatch {
    public:
        SnapshotBatch(const SnapshotBatch&) = delete;
        SnapshotBatch& operator=(const SnapshotBatch&) = delete;
        SnapshotBatch(SnapshotBatch&&) noexcept = default;
        SnapshotBatch& operator=(SnapshotBatch&&) noexcept = default;
        [[nodiscard]] const network::RidgebreakSnapshot& snapshot()
            const noexcept { return snapshot_; }

    private:
        explicit SnapshotBatch(network::RidgebreakSnapshot snapshot)
            : snapshot_(std::move(snapshot)) {}
        network::RidgebreakSnapshot snapshot_{};
        uint32_t usedPeerMask_ = 0u;
        friend class RidgebreakAuthoritySession;
    };

    struct Config {
        uint64_t sessionId = 1u;
        uint64_t worldId = 1u;
        uint32_t worldEpoch = 1u;
        uint32_t authorityEpoch = 1u;
        uint32_t inputFutureWindowTicks = 8u;
        uint32_t inputHistoryWindowTicks = 16u;
        uint32_t maximumPacketsPerPeerTick = 4u;
        uint32_t maximumHeldInputTicks = 6u;
        uint32_t snapshotRateHz = kRidgebreakAuthoritySnapshotRate;
        uint64_t maximumPacketSequenceAdvance = 256u;
        uint64_t maximumInputSequenceAdvance = 64u;
    };

    [[nodiscard]] bool initialize(const Config& config) noexcept;
    [[nodiscard]] bool registerClient(
        uint32_t peerId, uint32_t playerId,
        uint64_t connectionSerial,
        uint32_t connectionGeneration) noexcept;
    [[nodiscard]] bool disconnectClient(uint32_t peerId) noexcept;

    [[nodiscard]] RidgebreakIngressResult ingest(
        uint32_t peerId, network::DeliveryClass delivery,
        std::span<const std::byte> framedPacket) noexcept;

    // Executes exactly one canonical 1/60-second authority tick.
    void step() noexcept;

    [[nodiscard]] bool snapshotDue() const noexcept;
    [[nodiscard]] network::RidgebreakSnapshot canonicalSnapshot() const noexcept;
    // Gameplay state is stable across equivalent simulations even when packet
    // delivery and acknowledgement histories differ. The transcript hash also
    // commits future-influencing fences, queues and sequence windows. It
    // deliberately excludes observational telemetry and the last diagnostic
    // error, neither of which changes future simulation.
    [[nodiscard]] uint64_t gameplayStateHash() const noexcept;
    [[nodiscard]] uint64_t authorityTranscriptHash() const noexcept;
    [[nodiscard]] SnapshotBatch captureSnapshotBatch() const noexcept;
    [[nodiscard]] std::optional<std::vector<std::byte>> buildSnapshotPacket(
        uint32_t peerId) noexcept;
    [[nodiscard]] std::optional<std::vector<std::byte>> buildSnapshotPacket(
        uint32_t peerId, SnapshotBatch& batch) noexcept;

    [[nodiscard]] uint64_t tick() const noexcept { return tick_; }
    [[nodiscard]] const Config& config() const noexcept { return config_; }
    [[nodiscard]] const RidgebreakAuthorityTelemetry& telemetry() const noexcept {
        return telemetry_;
    }
    [[nodiscard]] RidgebreakRegistrationError lastRegistrationError()
        const noexcept { return lastRegistrationError_; }
    [[nodiscard]] std::optional<RidgebreakPeerView> peerView(
        uint32_t peerId) const noexcept;

private:
    static constexpr uint32_t kPendingInputSlots = 16u;

    struct PendingInput {
        uint64_t tick = 0u;
        network::RidgebreakInputSample sample{};
        bool valid = false;
    };

    struct Peer {
        uint32_t peerId = 0u;
        uint32_t playerId = 0u;
        uint32_t connectionGeneration = 0u;
        uint64_t connectionSerial = 0u;
        bool occupied = false;
        bool active = false;
        uint64_t disconnectedTick = 0u;
        network::AckWindow receivedPackets{};
        uint64_t latestReceivedInputSequence = 0u;
        uint64_t latestProcessedInputSequence = 0u;
        uint64_t outboundSequence = 0u;
        uint64_t latestAcknowledgedSnapshotSequence = 0u;
        uint32_t packetsThisTick = 0u;
        uint32_t ticksSinceFreshInput = 0u;
        network::RidgebreakInputSample heldInput{};
        std::array<PendingInput, kPendingInputSlots> pending{};
        int32_t positionRemainderX = 0;
        int32_t positionRemainderZ = 0;
        int32_t spawnXMillimeters = 0;
        int32_t spawnZMillimeters = 0;
        network::RidgebreakPlayerState state{};
    };

    [[nodiscard]] Peer* findPeer(uint32_t peerId) noexcept;
    [[nodiscard]] const Peer* findPeer(uint32_t peerId) const noexcept;
    [[nodiscard]] static bool acknowledgedBy(
        uint64_t ackSequence, uint64_t ackBits,
        uint64_t sequence) noexcept;
    void simulate(Peer& peer, const network::RidgebreakInputSample& input,
                  bool freshInput) noexcept;
    void populateCanonicalPlayers(
        network::RidgebreakSnapshot& snapshot) const noexcept;
    [[nodiscard]] uint64_t transcriptHash() const noexcept;

    Config config_{};
    std::array<Peer, kRidgebreakAuthorityPlayerCount> peers_{};
    RidgebreakAuthorityTelemetry telemetry_{};
    // Four fixed player identities make exact stale-lifetime fences bounded.
    // Each player may reconnect until its non-wrapping identity domains end.
    std::array<uint64_t, kRidgebreakAuthorityPlayerCount>
        highestConnectionSerials_{};
    std::array<uint32_t, kRidgebreakAuthorityPlayerCount>
        highestConnectionGenerations_{};
    // Server-issued serials are globally monotonic. AckWindow gives an exact
    // bounded replay fence while permitting a small amount of cross-player
    // lifecycle reordering.
    network::AckWindow acceptedConnectionSerials_{};
    RidgebreakRegistrationError lastRegistrationError_ =
        RidgebreakRegistrationError::None;
    uint64_t tick_ = 0u;
    bool initialized_ = false;
};

} // namespace voxy::server
