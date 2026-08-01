#pragma once

#include "network/replication.hpp"
#include "network/session_transport.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <vector>

namespace voxy::network {

enum class MultiplayerSessionRole : uint32_t {
    None = 0,
    AuthoritativeServer = 1,
    Client = 2,
};

struct MultiplayerSessionIdentity {
    uint64_t sessionId = 0;
    uint64_t worldId = 0;
    uint32_t worldEpoch = 1;
};

struct MultiplayerSessionTelemetry {
    uint64_t receivedPackets = 0;
    uint64_t sentPackets = 0;
    uint64_t sentBytes = 0;
    uint64_t malformedPackets = 0;
    uint64_t foreignPackets = 0;
    uint64_t duplicatePackets = 0;
    uint64_t staleAuthorityPackets = 0;
    uint64_t transportSendFailures = 0;
    uint64_t acceptedInputs = 0;
    uint64_t rejectedInputs = 0;
    uint64_t redundantInputsRecovered = 0;
    uint64_t acceptedCommands = 0;
    uint64_t rejectedCommands = 0;
    uint64_t fullSnapshots = 0;
    uint64_t deltaSnapshots = 0;
    uint64_t snapshotAcks = 0;
    uint64_t fullSnapshotRequests = 0;
    uint64_t snapshotDecodeFailures = 0;
    uint64_t interestOverflows = 0;
    uint64_t hardResyncs = 0;
    uint64_t authorityChanges = 0;
    uint64_t recordingEvictions = 0;
};

class MultiplayerSession {
public:
    using LockstepBody = physics::deterministic::LockstepBody;
    using CanonicalReplayCommand =
        physics::deterministic::CanonicalReplayCommand;

    // A game may deterministically expand a validated client request into one
    // or more authoritative commands. Output sequence/authority/flags are
    // always replaced by the session before execution. If no expander is
    // supplied, movement, impulse, and awake requests use the default policy.
    using CommandExpander = std::function<bool(
        const CanonicalNetworkCommand& request,
        std::span<const LockstepBody> bodies,
        std::vector<CanonicalNetworkCommand>& authoritativeCommands)>;

    struct ServerConfig {
        MultiplayerSessionIdentity identity{};
        physics::deterministic::LockstepWorld::Config world{};
        InterestGrid::Config interest{};
        uint64_t islandId = 1;
        uint32_t authorityEpoch = 1;
        uint32_t workerId = 0;
        uint32_t maximumClients = 64;
        uint32_t inputFutureWindow = 8;
        uint32_t maximumCommandsPerClientTick = 8;
        uint32_t snapshotHistoryTicks = 64;
        uint32_t snapshotIntervalTicks = 1;
        uint32_t recordingCapacity = 65'536;
        bool allowSharedControlledBodies = false;
        CommandExpander commandExpander{};
    };

    struct ClientConfig {
        MultiplayerSessionIdentity identity{};
        PredictionBubble::Config prediction{};
        uint32_t clientId = 0;
        uint32_t controlledBody = 0;
        uint64_t islandId = 1;
        uint32_t authorityEpoch = 1;
        uint32_t snapshotHistoryTicks = 64;
        uint32_t commandFutureWindow = 8;
        TickSynchronizer::Config tickSync{};
    };

    struct ClientRegistration {
        uint32_t clientId = 0;
        uint32_t controlledBody = 0;
        uint32_t interestRadiusCells = 2;
        uint32_t maximumSnapshotBodies = 256;
    };

    MultiplayerSession() = default;
    ~MultiplayerSession();

    MultiplayerSession(const MultiplayerSession&) = delete;
    MultiplayerSession& operator=(const MultiplayerSession&) = delete;
    MultiplayerSession(MultiplayerSession&&) noexcept;
    MultiplayerSession& operator=(MultiplayerSession&&) noexcept;

    [[nodiscard]] bool initializeServer(
        ServerConfig config, std::span<const LockstepBody> initialBodies,
        std::unique_ptr<IMultiplayerTransport> transport);
    [[nodiscard]] bool initializeClient(
        ClientConfig config,
        std::unique_ptr<IMultiplayerTransport> transport);
    void shutdown();

    [[nodiscard]] bool addClient(const ClientRegistration& registration);
    [[nodiscard]] bool removeClient(uint32_t clientId);

    // Drains at most maxFrames so a hostile or broken transport cannot starve
    // simulation. nowMicros is only consumed by tick-sync responses. False
    // means at least one frame was rejected; it is diagnostic, not a fatal
    // session state. Duplicate, stale, or malformed traffic leaves the
    // session usable and is counted in telemetry.
    [[nodiscard]] bool pump(
        uint64_t nowMicros = 0, uint32_t maxFrames = 256);

    // Server API. stepServer consumes already-pumped requests for exactly one
    // fixed tick and sends the configured replication update. Once the world
    // advances it returns true even if individual requests or snapshot sends
    // were rejected; those nonfatal failures are reported through telemetry.
    // False means no tick was committed, so retrying is safe.
    [[nodiscard]] bool stepServer();
    [[nodiscard]] bool sendSnapshots();
    [[nodiscard]] bool queueServerCommand(
        CanonicalNetworkCommand command);
    [[nodiscard]] bool queueCorrection(
        uint32_t bodyId, const LockstepBody& authoritativeBody);
    [[nodiscard]] std::optional<IslandAuthority> advanceAuthority(
        uint32_t destinationWorker, uint64_t startTick);

    // Client API. Local prediction advances even if this particular send is
    // lost; the next input bundle carries redundant earlier frames.
    [[nodiscard]] bool advanceClientTick(InputFrame input);
    [[nodiscard]] bool sendClientCommand(
        CanonicalNetworkCommand command);
    [[nodiscard]] bool requestTickSync(uint64_t nowMicros);

    [[nodiscard]] MultiplayerSessionRole role() const noexcept {
        return role_;
    }
    [[nodiscard]] bool clientReady() const noexcept {
        return role_ == MultiplayerSessionRole::Client && clientReady_;
    }
    [[nodiscard]] uint64_t currentTick() const noexcept;
    [[nodiscard]] uint64_t islandId() const noexcept;
    [[nodiscard]] uint32_t authorityEpoch() const noexcept;
    [[nodiscard]] std::span<const LockstepBody> bodies() const noexcept;
    [[nodiscard]] std::optional<AuthoritativeSnapshot>
    authoritativeSnapshot() const;
    [[nodiscard]] std::optional<AuthoritativeSnapshot>
    snapshotForClient(uint32_t clientId) const;
    [[nodiscard]] const MultiplayerSessionTelemetry& telemetry() const noexcept {
        return telemetry_;
    }
    [[nodiscard]] const PredictionTelemetry* predictionTelemetry()
        const noexcept;
    [[nodiscard]] const TickSynchronizer* tickSynchronizer() const noexcept;
    [[nodiscard]] std::span<const CanonicalNetworkCommand>
    recordedNetworkCommands() const noexcept {
        return recordedNetworkCommands_;
    }
    [[nodiscard]] std::span<const CanonicalReplayCommand>
    recordedReplayCommands() const noexcept {
        return recordedReplayCommands_;
    }

private:
    struct ServerPeer {
        uint32_t clientId = 0;
        uint32_t controlledBody = 0;
        uint32_t interestRadiusCells = 2;
        uint32_t maximumSnapshotBodies = 256;
        std::array<uint64_t, 3> nextPacketSequences{1u, 1u, 1u};
        std::array<AckWindow, 3> receivedPackets{};
        AckWindow receivedCommands{};
        InputRedundancyBuffer receivedInputs{64};
        SnapshotHistory snapshots{64};
        bool forceFullSnapshot = true;
    };

    struct PendingRequest {
        CanonicalNetworkCommand command{};
        bool fromInput = false;
    };

    [[nodiscard]] ServerPeer* serverPeer(uint32_t clientId) noexcept;
    [[nodiscard]] const ServerPeer* serverPeer(
        uint32_t clientId) const noexcept;
    [[nodiscard]] bool processServerFrame(
        const MultiplayerTransportFrame& frame, uint64_t nowMicros);
    [[nodiscard]] bool processClientFrame(
        const MultiplayerTransportFrame& frame, uint64_t nowMicros);
    [[nodiscard]] bool processInputPacket(
        ServerPeer& peer, const Packet& packet);
    [[nodiscard]] bool processCommandPacket(
        ServerPeer& peer, const Packet& packet);
    [[nodiscard]] bool processSnapshotAck(
        ServerPeer& peer, const Packet& packet);
    [[nodiscard]] bool processServerTickSync(
        ServerPeer& peer, const Packet& packet);
    [[nodiscard]] bool processClientSnapshot(const Packet& packet);
    [[nodiscard]] bool processClientTickSync(
        const Packet& packet, uint64_t nowMicros);
    [[nodiscard]] bool validateIdentity(
        const PacketHeader& header) const noexcept;
    [[nodiscard]] bool sendPacket(
        uint32_t peerId, DeliveryClass delivery,
        PacketPayloadType payloadType, uint64_t tick,
        std::span<const std::byte> payload);
    [[nodiscard]] bool queueClientRequest(
        ServerPeer& peer, CanonicalNetworkCommand command, bool fromInput);
    [[nodiscard]] bool validateClientRequest(
        const ServerPeer& peer, const CanonicalNetworkCommand& command,
        bool fromInput) const;
    [[nodiscard]] bool executeRequest(
        const CanonicalNetworkCommand& request, bool serverAuthored);
    [[nodiscard]] bool defaultExpand(
        const CanonicalNetworkCommand& request,
        std::vector<CanonicalNetworkCommand>& output) const;
    [[nodiscard]] AuthoritativeSnapshot makeFullSnapshot(
        std::span<const uint32_t> bodyIds = {}) const;
    [[nodiscard]] std::optional<AuthoritativeSnapshot>
    makeClientSnapshot(
        const ServerPeer& peer, bool* interestOverflow = nullptr) const;
    [[nodiscard]] bool initializeOrRebaseClient(
        const AuthoritativeSnapshot& snapshot, bool hardResync);
    [[nodiscard]] bool sendSnapshotAck(
        const AuthoritativeSnapshot& snapshot);
    [[nodiscard]] bool requestFullSnapshot();
    void recordNetwork(const CanonicalNetworkCommand& command);
    void recordReplay(const CanonicalReplayCommand& command);

    MultiplayerSessionRole role_ = MultiplayerSessionRole::None;
    std::unique_ptr<IMultiplayerTransport> transport_;
    ServerConfig serverConfig_{};
    ClientConfig clientConfig_{};
    physics::deterministic::LockstepWorld serverWorld_{};
    InterestGrid interestGrid_{};
    AuthorityTable authority_{};
    std::optional<IslandAuthority> pendingAuthority_;
    SnapshotAckTracker snapshotAcks_{};
    std::vector<ServerPeer> serverPeers_;
    // Recently disconnected authenticated peers retain bounded replay and
    // outbound sequence state. A same-session reconnect must not restart at
    // packet sequence one and become indistinguishable from delayed traffic.
    std::vector<ServerPeer> retiredServerPeers_;
    std::vector<PendingRequest> pendingRequests_;
    std::vector<CanonicalNetworkCommand> pendingServerCommands_;
    uint64_t serverTick_ = 0;
    uint64_t nextServerCommandSequence_ = 1;

    PredictionBubble prediction_{};
    TickSynchronizer tickSynchronizer_{};
    InputRedundancyBuffer localInputs_{64};
    SnapshotHistory clientSnapshots_{64};
    std::array<AckWindow, 3> receivedServerPackets_{};
    std::array<uint64_t, 3> nextClientPacketSequences_{1u, 1u, 1u};
    uint64_t nextClientCommandSequence_ = 1;
    uint64_t nextClientInputSequence_ = 1;
    uint64_t lastAcceptedSnapshotTick_ = 0;
    bool clientReady_ = false;

    std::vector<CanonicalNetworkCommand> recordedNetworkCommands_;
    std::vector<CanonicalReplayCommand> recordedReplayCommands_;
    MultiplayerSessionTelemetry telemetry_{};
};

} // namespace voxy::network
