#include "network/multiplayer_session.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <limits>
#include <tuple>
#include <utility>

namespace voxy::network {
namespace {

using physics::deterministic::CanonicalReplayCommand;
using physics::deterministic::LockstepBody;
using physics::deterministic::LockstepBodyAlive;
using physics::deterministic::ReplayCommandType;

constexpr uint32_t kSnapshotAckSchema = 1;
constexpr uint32_t kSnapshotAckRequestFull = 1u << 0u;
constexpr uint32_t kTickSyncSchema = 1;
constexpr uint32_t kTickSyncRequest = 1;
constexpr uint32_t kTickSyncResponse = 2;
constexpr int32_t kMaximumInputQ16 = 65'536;
constexpr int32_t kMaximumAppliedImpulseQ16 = 16'384;
constexpr size_t kMaximumClientInboundFrameBytes = 32u * 1024u;
constexpr uint32_t kMaximumSessionSnapshotBodies = 256u;

bool bodyAlive(const LockstepBody& body) noexcept {
    return (body.identity[2] & LockstepBodyAlive) != 0u;
}

bool inputValueValid(int32_t value) noexcept {
    return value >= -kMaximumInputQ16 && value <= kMaximumInputQ16;
}

bool validDelivery(DeliveryClass delivery) noexcept {
    switch (delivery) {
        case DeliveryClass::Realtime:
        case DeliveryClass::ReliableEvent:
        case DeliveryClass::ReliableControl:
            return true;
    }
    return false;
}

size_t deliveryIndex(DeliveryClass delivery) noexcept {
    switch (delivery) {
        case DeliveryClass::Realtime: return 0u;
        case DeliveryClass::ReliableEvent: return 1u;
        case DeliveryClass::ReliableControl: return 2u;
    }
    return 0u;
}

void appendU32(std::vector<std::byte>& bytes, uint32_t value) {
    for (uint32_t shift = 0; shift < 32u; shift += 8u)
        bytes.push_back(std::byte{static_cast<uint8_t>(value >> shift)});
}

void appendU64(std::vector<std::byte>& bytes, uint64_t value) {
    appendU32(bytes, static_cast<uint32_t>(value));
    appendU32(bytes, static_cast<uint32_t>(value >> 32u));
}

bool readU32(std::span<const std::byte> bytes, size_t& offset,
             uint32_t& value) noexcept {
    if (offset > bytes.size() || bytes.size() - offset < 4u) return false;
    value = 0u;
    for (uint32_t byte = 0; byte < 4u; ++byte) {
        value |= std::to_integer<uint32_t>(bytes[offset + byte])
            << (byte * 8u);
    }
    offset += 4u;
    return true;
}

bool readU64(std::span<const std::byte> bytes, size_t& offset,
             uint64_t& value) noexcept {
    uint32_t low = 0u;
    uint32_t high = 0u;
    if (!readU32(bytes, offset, low) || !readU32(bytes, offset, high))
        return false;
    value = uint64_t{low} | (uint64_t{high} << 32u);
    return true;
}

std::vector<std::byte> snapshotAckPayload(
    uint32_t flags, uint64_t tick, uint32_t stateHash) {
    std::vector<std::byte> bytes;
    bytes.reserve(24u);
    appendU32(bytes, kSnapshotAckSchema);
    appendU32(bytes, flags);
    appendU64(bytes, tick);
    appendU32(bytes, stateHash);
    appendU32(bytes, 0u);
    return bytes;
}

bool decodeSnapshotAck(
    std::span<const std::byte> bytes, uint32_t& flags,
    uint64_t& tick, uint32_t& stateHash) noexcept {
    size_t offset = 0u;
    uint32_t schema = 0u;
    uint32_t reserved = 0u;
    return readU32(bytes, offset, schema)
        && readU32(bytes, offset, flags)
        && readU64(bytes, offset, tick)
        && readU32(bytes, offset, stateHash)
        && readU32(bytes, offset, reserved)
        && offset == bytes.size() && schema == kSnapshotAckSchema
        && (flags & ~kSnapshotAckRequestFull) == 0u && reserved == 0u;
}

std::vector<std::byte> tickSyncPayload(
    uint32_t mode, uint64_t clientSendMicros,
    uint64_t localTick, uint64_t serverTick) {
    std::vector<std::byte> bytes;
    bytes.reserve(32u);
    appendU32(bytes, kTickSyncSchema);
    appendU32(bytes, mode);
    appendU64(bytes, clientSendMicros);
    appendU64(bytes, localTick);
    appendU64(bytes, serverTick);
    return bytes;
}

bool decodeTickSync(
    std::span<const std::byte> bytes, uint32_t& mode,
    uint64_t& clientSendMicros, uint64_t& localTick,
    uint64_t& serverTick) noexcept {
    size_t offset = 0u;
    uint32_t schema = 0u;
    return readU32(bytes, offset, schema)
        && readU32(bytes, offset, mode)
        && readU64(bytes, offset, clientSendMicros)
        && readU64(bytes, offset, localTick)
        && readU64(bytes, offset, serverTick)
        && offset == bytes.size() && schema == kTickSyncSchema
        && (mode == kTickSyncRequest || mode == kTickSyncResponse);
}

bool validIdentity(const MultiplayerSessionIdentity& identity) noexcept {
    return identity.sessionId != 0u && identity.worldId != 0u
        && identity.worldEpoch != 0u;
}

bool validClientRequestShape(
    const CanonicalNetworkCommand& command) noexcept {
    if (!isClientCommandAllowed(command.type)
        || command.clientId == 0u || command.flags != 0u) {
        return false;
    }
    switch (command.type) {
        case NetworkCommandType::MoveInput:
        case NetworkCommandType::ApplyImpulseRequest:
            return inputValueValid(command.payload[0])
                && inputValueValid(command.payload[1])
                && inputValueValid(command.payload[2]);
        case NetworkCommandType::SetAwakeRequest:
        case NetworkCommandType::FireMeteorRequest:
            return true;
        case NetworkCommandType::SpawnBody:
        case NetworkCommandType::DestroyBody:
        case NetworkCommandType::Correction:
        case NetworkCommandType::SetVelocity:
        case NetworkCommandType::SetAwake:
            return false;
    }
    return false;
}

bool takeSequence(uint64_t& next, uint64_t& sequence) noexcept {
    if (next == 0u || next == std::numeric_limits<uint64_t>::max())
        return false;
    sequence = next++;
    return true;
}

bool serverCommandType(NetworkCommandType type) noexcept {
    switch (type) {
        case NetworkCommandType::SpawnBody:
        case NetworkCommandType::DestroyBody:
        case NetworkCommandType::Correction:
        case NetworkCommandType::SetVelocity:
        case NetworkCommandType::SetAwake:
            return true;
        case NetworkCommandType::MoveInput:
        case NetworkCommandType::FireMeteorRequest:
        case NetworkCommandType::ApplyImpulseRequest:
        case NetworkCommandType::SetAwakeRequest:
            return false;
    }
    return false;
}

uint32_t sessionCommandPriority(NetworkCommandType type) noexcept {
    switch (type) {
        case NetworkCommandType::DestroyBody: return 0u;
        case NetworkCommandType::SpawnBody: return 1u;
        case NetworkCommandType::Correction: return 2u;
        case NetworkCommandType::SetVelocity: return 3u;
        case NetworkCommandType::MoveInput:
        case NetworkCommandType::ApplyImpulseRequest: return 4u;
        case NetworkCommandType::FireMeteorRequest: return 5u;
        case NetworkCommandType::SetAwakeRequest:
        case NetworkCommandType::SetAwake: return 6u;
    }
    return std::numeric_limits<uint32_t>::max();
}

bool sessionRequestLess(
    const CanonicalNetworkCommand& lhs,
    const CanonicalNetworkCommand& rhs) noexcept {
    return std::tuple{
               lhs.tick, sessionCommandPriority(lhs.type), lhs.clientId,
               lhs.sequence, lhs.body, lhs.generation, lhs.payload}
         < std::tuple{
               rhs.tick, sessionCommandPriority(rhs.type), rhs.clientId,
               rhs.sequence, rhs.body, rhs.generation, rhs.payload};
}

} // namespace

MultiplayerSession::~MultiplayerSession() { shutdown(); }

MultiplayerSession::MultiplayerSession(MultiplayerSession&& other) noexcept {
    *this = std::move(other);
}

MultiplayerSession& MultiplayerSession::operator=(
    MultiplayerSession&& other) noexcept {
    if (this == &other) return *this;
    shutdown();
    role_ = std::exchange(
        other.role_, MultiplayerSessionRole::None);
    transport_ = std::move(other.transport_);
    serverConfig_ = std::move(other.serverConfig_);
    clientConfig_ = std::move(other.clientConfig_);
    serverWorld_ = std::move(other.serverWorld_);
    interestGrid_ = std::move(other.interestGrid_);
    authority_ = std::move(other.authority_);
    pendingAuthority_ = std::move(other.pendingAuthority_);
    snapshotAcks_ = std::move(other.snapshotAcks_);
    serverPeers_ = std::move(other.serverPeers_);
    retiredServerPeers_ = std::move(other.retiredServerPeers_);
    pendingRequests_ = std::move(other.pendingRequests_);
    pendingServerCommands_ = std::move(other.pendingServerCommands_);
    serverTick_ = other.serverTick_;
    nextServerCommandSequence_ = other.nextServerCommandSequence_;
    prediction_ = std::move(other.prediction_);
    tickSynchronizer_ = std::move(other.tickSynchronizer_);
    localInputs_ = std::move(other.localInputs_);
    clientSnapshots_ = std::move(other.clientSnapshots_);
    receivedServerPackets_ = std::move(other.receivedServerPackets_);
    nextClientPacketSequences_ = other.nextClientPacketSequences_;
    nextClientCommandSequence_ = other.nextClientCommandSequence_;
    nextClientInputSequence_ = other.nextClientInputSequence_;
    lastAcceptedSnapshotTick_ = other.lastAcceptedSnapshotTick_;
    clientReady_ = std::exchange(other.clientReady_, false);
    recordedNetworkCommands_ =
        std::move(other.recordedNetworkCommands_);
    recordedReplayCommands_ =
        std::move(other.recordedReplayCommands_);
    telemetry_ = other.telemetry_;
    return *this;
}

bool MultiplayerSession::initializeServer(
    ServerConfig config, std::span<const LockstepBody> initialBodies,
    std::unique_ptr<IMultiplayerTransport> transport) {
    shutdown();
    if (!transport || !validIdentity(config.identity)
        || config.islandId == 0u || config.authorityEpoch == 0u
        || config.maximumClients < 2u || config.inputFutureWindow == 0u
        || config.maximumCommandsPerClientTick == 0u
        || config.maximumCommandsPerClientTick
            > kMaximumCommandsPerPacket
        || config.snapshotHistoryTicks == 0u
        || config.snapshotHistoryTicks > 256u
        || config.snapshotIntervalTicks == 0u
        || config.recordingCapacity == 0u
        || config.interest.cellSizeQ12 <= 0
        || config.interest.maximumEntries == 0u
        || config.interest.maximumQueryBodies == 0u
        || config.world.bodyCapacity == 0u
        || config.world.contactCapacity == 0u) {
        return false;
    }
    physics::deterministic::LockstepWorld world;
    if (!world.initialize(config.world) || !world.setBodies(initialBodies))
        return false;
    AuthorityTable authority;
    if (!authority.assign({
            .islandId = config.islandId,
            .epoch = config.authorityEpoch,
            .workerId = config.workerId,
            .startTick = 0u,
            .checkpointHash = {},
        })) {
        return false;
    }
    serverConfig_ = std::move(config);
    serverWorld_ = std::move(world);
    interestGrid_ = InterestGrid(serverConfig_.interest);
    static_cast<void>(interestGrid_.rebuild(serverWorld_.bodies()));
    authority_ = std::move(authority);
    snapshotAcks_ = {};
    transport_ = std::move(transport);
    serverPeers_.clear();
    retiredServerPeers_.clear();
    pendingRequests_.clear();
    pendingServerCommands_.clear();
    pendingAuthority_.reset();
    recordedNetworkCommands_.clear();
    recordedReplayCommands_.clear();
    serverTick_ = 0u;
    nextServerCommandSequence_ = 1u;
    telemetry_ = {};
    role_ = MultiplayerSessionRole::AuthoritativeServer;
    return true;
}

bool MultiplayerSession::initializeClient(
    ClientConfig config,
    std::unique_ptr<IMultiplayerTransport> transport) {
    shutdown();
    if (!transport || !validIdentity(config.identity)
        || config.clientId == 0u || config.controlledBody == 0u
        || config.islandId == 0u || config.authorityEpoch == 0u
        || config.snapshotHistoryTicks == 0u
        || config.snapshotHistoryTicks > 256u
        || config.commandFutureWindow == 0u
        || config.prediction.historyTicks == 0u
        || config.prediction.maximumPredictedBodies == 0u
        || config.prediction.world.bodyCapacity == 0u
        || config.prediction.world.contactCapacity == 0u
        || config.controlledBody >= config.prediction.world.bodyCapacity) {
        return false;
    }
    clientConfig_ = config;
    transport_ = std::move(transport);
    prediction_ = {};
    tickSynchronizer_ = TickSynchronizer(config.tickSync);
    localInputs_ = InputRedundancyBuffer(
        std::max(config.snapshotHistoryTicks, kInputRedundancyFrames));
    clientSnapshots_ = SnapshotHistory(config.snapshotHistoryTicks);
    receivedServerPackets_ = {};
    nextClientPacketSequences_ = {1u, 1u, 1u};
    nextClientCommandSequence_ = 1u;
    nextClientInputSequence_ = 1u;
    lastAcceptedSnapshotTick_ = 0u;
    clientReady_ = false;
    recordedNetworkCommands_.clear();
    recordedReplayCommands_.clear();
    telemetry_ = {};
    role_ = MultiplayerSessionRole::Client;
    return true;
}

void MultiplayerSession::shutdown() {
    if (transport_) transport_->close();
    transport_.reset();
    role_ = MultiplayerSessionRole::None;
    serverPeers_.clear();
    retiredServerPeers_.clear();
    pendingRequests_.clear();
    pendingServerCommands_.clear();
    pendingAuthority_.reset();
    serverWorld_.clear();
    prediction_ = {};
    clientReady_ = false;
}

bool MultiplayerSession::addClient(
    const ClientRegistration& registration) {
    if (role_ != MultiplayerSessionRole::AuthoritativeServer
        || registration.clientId == 0u
        || registration.controlledBody == 0u
        || registration.controlledBody >= serverWorld_.bodies().size()
        || !bodyAlive(serverWorld_.bodies()[registration.controlledBody])
        || serverWorld_.bodies()[registration.controlledBody].identity[0]
            != registration.controlledBody
        || serverPeer(registration.clientId) != nullptr
        || serverPeers_.size() >= serverConfig_.maximumClients
        || registration.interestRadiusCells > 64u
        || registration.maximumSnapshotBodies == 0u
        || registration.maximumSnapshotBodies
            > kMaximumSessionSnapshotBodies
        || registration.maximumSnapshotBodies
            > serverConfig_.interest.maximumQueryBodies
        || (!serverConfig_.allowSharedControlledBodies
            && std::any_of(
                serverPeers_.begin(), serverPeers_.end(),
                [&registration](const ServerPeer& peer) {
                    return peer.controlledBody
                        == registration.controlledBody;
                }))) {
        return false;
    }
    ServerPeer peer;
    const auto retired = std::find_if(
        retiredServerPeers_.begin(), retiredServerPeers_.end(),
        [&registration](const ServerPeer& candidate) {
            return candidate.clientId == registration.clientId;
        });
    const bool resumed = retired != retiredServerPeers_.end();
    if (resumed) {
        peer = std::move(*retired);
        retiredServerPeers_.erase(retired);
    }
    peer.clientId = registration.clientId;
    peer.controlledBody = registration.controlledBody;
    peer.interestRadiusCells = registration.interestRadiusCells;
    peer.maximumSnapshotBodies =
        registration.maximumSnapshotBodies;
    if (!resumed) {
        peer.receivedInputs = InputRedundancyBuffer(
            std::max(serverConfig_.snapshotHistoryTicks,
                     kInputRedundancyFrames));
    }
    peer.snapshots = SnapshotHistory(serverConfig_.snapshotHistoryTicks);
    peer.forceFullSnapshot = true;
    serverPeers_.push_back(std::move(peer));
    std::stable_sort(serverPeers_.begin(), serverPeers_.end(),
        [](const ServerPeer& lhs, const ServerPeer& rhs) {
            return lhs.clientId < rhs.clientId;
        });
    return true;
}

bool MultiplayerSession::removeClient(uint32_t clientId) {
    if (role_ != MultiplayerSessionRole::AuthoritativeServer)
        return false;
    auto iterator = std::lower_bound(
        serverPeers_.begin(), serverPeers_.end(), clientId,
        [](const ServerPeer& peer, uint32_t id) {
            return peer.clientId < id;
        });
    if (iterator == serverPeers_.end() || iterator->clientId != clientId)
        return false;
    ServerPeer retired = std::move(*iterator);
    serverPeers_.erase(iterator);
    retired.snapshots =
        SnapshotHistory(serverConfig_.snapshotHistoryTicks);
    retired.forceFullSnapshot = true;
    const auto oldRetired = std::find_if(
        retiredServerPeers_.begin(), retiredServerPeers_.end(),
        [clientId](const ServerPeer& peer) {
            return peer.clientId == clientId;
        });
    if (oldRetired != retiredServerPeers_.end())
        *oldRetired = std::move(retired);
    else
        retiredServerPeers_.push_back(std::move(retired));
    if (retiredServerPeers_.size() > serverConfig_.maximumClients) {
        retiredServerPeers_.erase(retiredServerPeers_.begin());
    }
    std::erase_if(pendingRequests_,
        [clientId](const PendingRequest& pending) {
            return pending.command.clientId == clientId;
        });
    snapshotAcks_.reset(clientId);
    return true;
}

MultiplayerSession::ServerPeer* MultiplayerSession::serverPeer(
    uint32_t clientId) noexcept {
    auto iterator = std::lower_bound(
        serverPeers_.begin(), serverPeers_.end(), clientId,
        [](const ServerPeer& peer, uint32_t id) {
            return peer.clientId < id;
        });
    return iterator != serverPeers_.end() && iterator->clientId == clientId
        ? &*iterator : nullptr;
}

const MultiplayerSession::ServerPeer* MultiplayerSession::serverPeer(
    uint32_t clientId) const noexcept {
    auto iterator = std::lower_bound(
        serverPeers_.begin(), serverPeers_.end(), clientId,
        [](const ServerPeer& peer, uint32_t id) {
            return peer.clientId < id;
        });
    return iterator != serverPeers_.end() && iterator->clientId == clientId
        ? &*iterator : nullptr;
}

bool MultiplayerSession::validateIdentity(
    const PacketHeader& header) const noexcept {
    const MultiplayerSessionIdentity* identity = nullptr;
    if (role_ == MultiplayerSessionRole::AuthoritativeServer)
        identity = &serverConfig_.identity;
    else if (role_ == MultiplayerSessionRole::Client)
        identity = &clientConfig_.identity;
    return identity != nullptr
        && header.sessionId == identity->sessionId
        && header.worldId == identity->worldId
        && header.worldEpoch == identity->worldEpoch;
}

bool MultiplayerSession::pump(uint64_t nowMicros, uint32_t maxFrames) {
    if (role_ == MultiplayerSessionRole::None || !transport_
        || maxFrames == 0u) {
        return false;
    }
    transport_->service();
    bool allValid = true;
    for (uint32_t count = 0u; count < maxFrames; ++count) {
        auto frame = transport_->poll();
        if (!frame.has_value()) break;
        if (!frame->isData()) continue;
        ++telemetry_.receivedPackets;
        const bool valid =
            role_ == MultiplayerSessionRole::AuthoritativeServer
            ? processServerFrame(*frame, nowMicros)
            : processClientFrame(*frame, nowMicros);
        allValid = valid && allValid;
    }
    return allValid;
}

bool MultiplayerSession::processServerFrame(
    const MultiplayerTransportFrame& frame, uint64_t nowMicros) {
    ServerPeer* peer = serverPeer(frame.peerId);
    if (peer == nullptr || !validDelivery(frame.delivery)) {
        ++telemetry_.foreignPackets;
        return false;
    }
    if (frame.bytes.size() > kMaximumClientInboundFrameBytes) {
        ++telemetry_.malformedPackets;
        return false;
    }
    const auto decoded = PacketCodec::decode(frame.bytes, frame.delivery);
    if (!decoded.packet.has_value()) {
        ++telemetry_.malformedPackets;
        return false;
    }
    const Packet& packet = *decoded.packet;
    if (!validateIdentity(packet.header)
        || packet.header.flags != 0u) {
        ++telemetry_.foreignPackets;
        return false;
    }
    if (packet.header.authorityEpoch != serverConfig_.authorityEpoch) {
        ++telemetry_.staleAuthorityPackets;
        return false;
    }
    AckWindow& received =
        peer->receivedPackets[deliveryIndex(frame.delivery)];
    if (packet.header.sequence == 0u
        || !received.observe(packet.header.sequence)) {
        ++telemetry_.duplicatePackets;
        return false;
    }
    switch (packet.header.payloadType) {
        case PacketPayloadType::Input:
            if (frame.delivery != DeliveryClass::Realtime) break;
            return processInputPacket(*peer, packet);
        case PacketPayloadType::Command:
            if (frame.delivery != DeliveryClass::ReliableEvent) break;
            return processCommandPacket(*peer, packet);
        case PacketPayloadType::SnapshotAck:
            if (frame.delivery == DeliveryClass::ReliableEvent) break;
            return processSnapshotAck(*peer, packet);
        case PacketPayloadType::TickSync:
            if (frame.delivery != DeliveryClass::Realtime) break;
            static_cast<void>(nowMicros);
            return processServerTickSync(*peer, packet);
        case PacketPayloadType::Snapshot:
        case PacketPayloadType::StateHash:
        case PacketPayloadType::Event:
        case PacketPayloadType::Control:
        case PacketPayloadType::Checkpoint:
        case PacketPayloadType::Correction:
            ++telemetry_.malformedPackets;
            return false;
    }
    ++telemetry_.malformedPackets;
    return false;
}

bool MultiplayerSession::processClientFrame(
    const MultiplayerTransportFrame& frame, uint64_t nowMicros) {
    if (frame.peerId != 0u || !validDelivery(frame.delivery)) {
        ++telemetry_.foreignPackets;
        return false;
    }
    const auto decoded = PacketCodec::decode(frame.bytes, frame.delivery);
    if (!decoded.packet.has_value()) {
        ++telemetry_.malformedPackets;
        return false;
    }
    const Packet& packet = *decoded.packet;
    if (!validateIdentity(packet.header)
        || packet.header.flags != 0u) {
        ++telemetry_.foreignPackets;
        return false;
    }
    if (packet.header.sequence == 0u) {
        ++telemetry_.malformedPackets;
        return false;
    }
    const bool newerAuthority =
        packet.header.authorityEpoch > clientConfig_.authorityEpoch;
    if (newerAuthority) {
        // A destination worker may begin its packet sequences at one. Only a
        // validated full snapshot may reset the delivery windows.
        if (packet.header.payloadType != PacketPayloadType::Snapshot
            || frame.delivery != DeliveryClass::ReliableControl)
            return false;
        const bool accepted = processClientSnapshot(packet);
        if (!accepted) return false;
        receivedServerPackets_ = {};
        static_cast<void>(
            receivedServerPackets_[deliveryIndex(frame.delivery)]
                .observe(packet.header.sequence));
        return true;
    }
    if (packet.header.authorityEpoch < clientConfig_.authorityEpoch) {
        ++telemetry_.staleAuthorityPackets;
        return false;
    }
    AckWindow& received =
        receivedServerPackets_[deliveryIndex(frame.delivery)];
    if (!received.observe(packet.header.sequence)) {
        ++telemetry_.duplicatePackets;
        return false;
    }
    switch (packet.header.payloadType) {
        case PacketPayloadType::Snapshot:
            if (frame.delivery == DeliveryClass::ReliableEvent) break;
            return processClientSnapshot(packet);
        case PacketPayloadType::TickSync:
            if (frame.delivery != DeliveryClass::Realtime) break;
            return processClientTickSync(packet, nowMicros);
        case PacketPayloadType::Input:
        case PacketPayloadType::Command:
        case PacketPayloadType::SnapshotAck:
        case PacketPayloadType::StateHash:
        case PacketPayloadType::Event:
        case PacketPayloadType::Control:
        case PacketPayloadType::Checkpoint:
        case PacketPayloadType::Correction:
            ++telemetry_.malformedPackets;
            return false;
    }
    ++telemetry_.malformedPackets;
    return false;
}

bool MultiplayerSession::validateClientRequest(
    const ServerPeer& peer, const CanonicalNetworkCommand& command,
    bool fromInput) const {
    if (!validClientRequestShape(command)
        || command.clientId != peer.clientId
        || command.islandId != serverConfig_.islandId
        || !authority_.accepts(command.islandId, command.authorityEpoch,
                               command.tick)
        || command.tick <= serverTick_
        || command.tick > serverTick_ + serverConfig_.inputFutureWindow
        || command.body != peer.controlledBody
        || command.body >= serverWorld_.bodies().size()
        || !bodyAlive(serverWorld_.bodies()[command.body])
        || command.generation
            != serverWorld_.bodies()[command.body].identity[1]) {
        return false;
    }
    if (fromInput) {
        const auto existing = std::find_if(
            pendingRequests_.begin(), pendingRequests_.end(),
            [&command](const PendingRequest& request) {
                return request.fromInput
                    && request.command.clientId == command.clientId
                    && request.command.tick == command.tick;
            });
        if (existing != pendingRequests_.end()) {
            return existing->command.sequence < command.sequence;
        }
    }
    const size_t commandsThisTick = static_cast<size_t>(std::count_if(
        pendingRequests_.begin(), pendingRequests_.end(),
        [&command](const PendingRequest& request) {
            return request.command.clientId == command.clientId
                && request.command.tick == command.tick;
        }));
    return commandsThisTick
        < serverConfig_.maximumCommandsPerClientTick;
}

bool MultiplayerSession::queueClientRequest(
    ServerPeer& peer, CanonicalNetworkCommand command, bool fromInput) {
    if (!validateClientRequest(peer, command, fromInput)) return false;
    if (fromInput) {
        auto existing = std::find_if(
            pendingRequests_.begin(), pendingRequests_.end(),
            [&command](const PendingRequest& request) {
                return request.fromInput
                    && request.command.clientId == command.clientId
                    && request.command.tick == command.tick;
            });
        if (existing != pendingRequests_.end()) {
            existing->command = command;
            return true;
        }
    }
    pendingRequests_.push_back({command, fromInput});
    return true;
}

bool MultiplayerSession::processInputPacket(
    ServerPeer& peer, const Packet& packet) {
    const auto decoded = InputRedundancyBuffer::decode(packet.payload);
    if (!decoded.frames.has_value()) {
        ++telemetry_.malformedPackets;
        return false;
    }
    if (decoded.frames->empty()
        || packet.header.tick != decoded.frames->back().tick) {
        ++telemetry_.malformedPackets;
        return false;
    }
    const uint64_t newestTick = decoded.frames->empty()
        ? 0u : decoded.frames->back().tick;
    bool allValid = true;
    for (const InputFrame& frame : *decoded.frames) {
        if (frame.sequence == 0u || frame.tick <= serverTick_
            || frame.tick > serverTick_ + serverConfig_.inputFutureWindow
            || !inputValueValid(frame.moveXQ16)
            || !inputValueValid(frame.moveZQ16)
            || !inputValueValid(frame.lookXQ16)
            || !inputValueValid(frame.lookYQ16)) {
            ++telemetry_.rejectedInputs;
            allValid = false;
            continue;
        }
        CanonicalNetworkCommand command;
        command.tick = frame.tick;
        command.sequence = frame.sequence;
        command.islandId = serverConfig_.islandId;
        command.authorityEpoch = serverConfig_.authorityEpoch;
        command.clientId = peer.clientId;
        command.type = NetworkCommandType::MoveInput;
        command.body = peer.controlledBody;
        command.generation =
            serverWorld_.bodies()[peer.controlledBody].identity[1];
        command.payload[0] = frame.moveXQ16;
        command.payload[2] = frame.moveZQ16;
        const std::array one{frame};
        const auto accepted = peer.receivedInputs.ingest(one);
        if (accepted.empty()) continue;
        if (!queueClientRequest(peer, command, true)) {
            ++telemetry_.rejectedInputs;
            allValid = false;
            continue;
        }
        ++telemetry_.acceptedInputs;
        if (frame.tick < newestTick)
            ++telemetry_.redundantInputsRecovered;
    }
    return allValid;
}

bool MultiplayerSession::processCommandPacket(
    ServerPeer& peer, const Packet& packet) {
    const auto decoded = NetworkCommandCodec::decode(packet.payload);
    if (!decoded.commands.has_value()) {
        ++telemetry_.malformedPackets;
        return false;
    }
    if (decoded.commands->empty()
        || packet.header.tick != decoded.commands->back().tick) {
        ++telemetry_.malformedPackets;
        return false;
    }
    bool allValid = true;
    for (auto command : *decoded.commands) {
        if (command.sequence == 0u
            || command.clientId != peer.clientId
            || !validateClientRequest(peer, command, false)) {
            ++telemetry_.rejectedCommands;
            allValid = false;
            continue;
        }
        if (!peer.receivedCommands.observe(command.sequence)
            || !queueClientRequest(peer, command, false)) {
            ++telemetry_.rejectedCommands;
            allValid = false;
            continue;
        }
        ++telemetry_.acceptedCommands;
    }
    return allValid;
}

bool MultiplayerSession::processSnapshotAck(
    ServerPeer& peer, const Packet& packet) {
    uint32_t flags = 0u;
    uint64_t tick = 0u;
    uint32_t stateHash = 0u;
    if (!decodeSnapshotAck(packet.payload, flags, tick, stateHash)
        || packet.header.tick != tick) {
        ++telemetry_.malformedPackets;
        return false;
    }
    if ((flags & kSnapshotAckRequestFull) != 0u) {
        peer.forceFullSnapshot = true;
        ++telemetry_.fullSnapshotRequests;
        return true;
    }
    const auto snapshot = peer.snapshots.find(
        serverConfig_.islandId, serverConfig_.authorityEpoch, tick);
    if (!snapshot.has_value() || snapshot->stateHash != stateHash) {
        ++telemetry_.malformedPackets;
        return false;
    }
    snapshotAcks_.acknowledge(peer.clientId, tick);
    peer.forceFullSnapshot = false;
    ++telemetry_.snapshotAcks;
    return true;
}

bool MultiplayerSession::processServerTickSync(
    ServerPeer& peer, const Packet& packet) {
    uint32_t mode = 0u;
    uint64_t clientSendMicros = 0u;
    uint64_t localTick = 0u;
    uint64_t unusedServerTick = 0u;
    if (!decodeTickSync(packet.payload, mode, clientSendMicros,
                        localTick, unusedServerTick)
        || mode != kTickSyncRequest
        || packet.header.tick != localTick) {
        ++telemetry_.malformedPackets;
        return false;
    }
    const auto payload = tickSyncPayload(
        kTickSyncResponse, clientSendMicros, localTick, serverTick_);
    return sendPacket(peer.clientId, DeliveryClass::Realtime,
                      PacketPayloadType::TickSync, serverTick_, payload);
}

bool MultiplayerSession::sendPacket(
    uint32_t peerId, DeliveryClass delivery,
    PacketPayloadType payloadType, uint64_t tick,
    std::span<const std::byte> payload) {
    if (!transport_ || role_ == MultiplayerSessionRole::None) return false;
    Packet packet;
    packet.header.payloadType = payloadType;
    if (role_ == MultiplayerSessionRole::AuthoritativeServer) {
        ServerPeer* peer = serverPeer(peerId);
        if (peer == nullptr) return false;
        const size_t channel = deliveryIndex(delivery);
        packet.header.sessionId = serverConfig_.identity.sessionId;
        packet.header.worldId = serverConfig_.identity.worldId;
        packet.header.worldEpoch = serverConfig_.identity.worldEpoch;
        packet.header.authorityEpoch = serverConfig_.authorityEpoch;
        if (!takeSequence(
                peer->nextPacketSequences[channel],
                packet.header.sequence)) {
            return false;
        }
        packet.header.ackSequence =
            peer->receivedPackets[channel].latest();
        packet.header.ackBits = peer->receivedPackets[channel].bits();
    } else {
        if (peerId != 0u) return false;
        const size_t channel = deliveryIndex(delivery);
        packet.header.sessionId = clientConfig_.identity.sessionId;
        packet.header.worldId = clientConfig_.identity.worldId;
        packet.header.worldEpoch = clientConfig_.identity.worldEpoch;
        packet.header.authorityEpoch = clientConfig_.authorityEpoch;
        if (!takeSequence(
                nextClientPacketSequences_[channel],
                packet.header.sequence)) {
            return false;
        }
        packet.header.ackSequence =
            receivedServerPackets_[channel].latest();
        packet.header.ackBits = receivedServerPackets_[channel].bits();
    }
    packet.header.tick = tick;
    packet.payload.assign(payload.begin(), payload.end());
    const auto encoded = PacketCodec::encode(packet, delivery);
    if (!encoded.error.empty()
        || !transport_->send(peerId, delivery, encoded.bytes)) {
        ++telemetry_.transportSendFailures;
        return false;
    }
    ++telemetry_.sentPackets;
    telemetry_.sentBytes += encoded.bytes.size();
    return true;
}

bool MultiplayerSession::defaultExpand(
    const CanonicalNetworkCommand& request,
    std::vector<CanonicalNetworkCommand>& output) const {
    switch (request.type) {
        case NetworkCommandType::MoveInput:
        case NetworkCommandType::ApplyImpulseRequest: {
            CanonicalNetworkCommand command = request;
            for (uint32_t axis = 0u; axis < 3u; ++axis) {
                command.payload[axis] = std::clamp(
                    command.payload[axis], -kMaximumAppliedImpulseQ16,
                    kMaximumAppliedImpulseQ16);
            }
            output.push_back(command);
            return true;
        }
        case NetworkCommandType::SetAwakeRequest:
            output.push_back(request);
            return true;
        case NetworkCommandType::FireMeteorRequest:
        case NetworkCommandType::SpawnBody:
        case NetworkCommandType::DestroyBody:
        case NetworkCommandType::Correction:
        case NetworkCommandType::SetVelocity:
        case NetworkCommandType::SetAwake:
            return false;
    }
    return false;
}

bool MultiplayerSession::executeRequest(
    const CanonicalNetworkCommand& request, bool serverAuthored) {
    std::vector<CanonicalNetworkCommand> expanded;
    if (serverAuthored) {
        expanded.push_back(request);
    } else if (serverConfig_.commandExpander) {
        if (!serverConfig_.commandExpander(
                request, serverWorld_.bodies(), expanded)) {
            return false;
        }
    } else if (!defaultExpand(request, expanded)) {
        return false;
    }
    if (expanded.empty() || expanded.size() > kMaximumCommandsPerPacket)
        return false;

    auto replacement = serverWorld_;
    std::vector<CanonicalNetworkCommand> authoritative;
    std::vector<CanonicalReplayCommand> replayCommands;
    authoritative.reserve(expanded.size());
    replayCommands.reserve(expanded.size());
    if (nextServerCommandSequence_ == 0u
        || expanded.size()
            > std::numeric_limits<uint64_t>::max()
                - nextServerCommandSequence_) {
        return false;
    }
    uint64_t sequence = nextServerCommandSequence_;
    for (auto command : expanded) {
        command.tick = request.tick;
        command.sequence = sequence++;
        command.islandId = serverConfig_.islandId;
        command.authorityEpoch = serverConfig_.authorityEpoch;
        command.clientId = request.clientId;
        command.flags |= NetworkCommandServerIssued;
        constexpr uint32_t knownFlags = NetworkCommandServerIssued
            | NetworkCommandCorrectionEvent;
        if ((command.flags & ~knownFlags) != 0u
            || command.body >= replacement.bodies().size()) {
            return false;
        }
        const auto replay = toReplayCommand(command);
        if (!replay.has_value()
            || !physics::deterministic::applyCanonicalReplayCommand(
                replacement, *replay)) {
            return false;
        }
        authoritative.push_back(command);
        replayCommands.push_back(*replay);
    }
    serverWorld_ = std::move(replacement);
    nextServerCommandSequence_ = sequence;
    for (const auto& command : authoritative) recordNetwork(command);
    for (const auto& command : replayCommands) recordReplay(command);
    return true;
}

void MultiplayerSession::recordNetwork(
    const CanonicalNetworkCommand& command) {
    auto iterator = std::upper_bound(
        recordedNetworkCommands_.begin(), recordedNetworkCommands_.end(),
        command, [](const auto& value, const auto& existing) {
            return canonicalNetworkCommandLess(value, existing);
        });
    recordedNetworkCommands_.insert(iterator, command);
    if (recordedNetworkCommands_.size()
        > serverConfig_.recordingCapacity) {
        const size_t evicted = recordedNetworkCommands_.size()
            - serverConfig_.recordingCapacity;
        recordedNetworkCommands_.erase(
            recordedNetworkCommands_.begin(),
            recordedNetworkCommands_.begin()
                + static_cast<std::ptrdiff_t>(evicted));
        telemetry_.recordingEvictions += evicted;
    }
}

void MultiplayerSession::recordReplay(
    const CanonicalReplayCommand& command) {
    auto iterator = std::upper_bound(
        recordedReplayCommands_.begin(), recordedReplayCommands_.end(),
        command, [](const auto& value, const auto& existing) {
            return physics::deterministic::canonicalReplayCommandLess(
                value, existing);
        });
    recordedReplayCommands_.insert(iterator, command);
    if (recordedReplayCommands_.size()
        > serverConfig_.recordingCapacity) {
        const size_t evicted = recordedReplayCommands_.size()
            - serverConfig_.recordingCapacity;
        recordedReplayCommands_.erase(
            recordedReplayCommands_.begin(),
            recordedReplayCommands_.begin()
                + static_cast<std::ptrdiff_t>(evicted));
        telemetry_.recordingEvictions += evicted;
    }
}

bool MultiplayerSession::stepServer() {
    if (role_ != MultiplayerSessionRole::AuthoritativeServer
        || serverTick_ >= std::numeric_limits<uint32_t>::max()) {
        return false;
    }
    const uint64_t tick = serverTick_ + 1u;
    if (pendingAuthority_.has_value()
        && pendingAuthority_->startTick == tick) {
        if (!authority_.assign(*pendingAuthority_)) return false;
        serverConfig_.authorityEpoch = pendingAuthority_->epoch;
        for (auto& peer : serverPeers_) {
            peer.snapshots =
                SnapshotHistory(serverConfig_.snapshotHistoryTicks);
            peer.forceFullSnapshot = true;
        }
        pendingAuthority_.reset();
        ++telemetry_.authorityChanges;
    }

    std::vector<PendingRequest> due;
    std::vector<PendingRequest> futureRequests;
    for (const PendingRequest& pending : pendingRequests_) {
        if (pending.command.tick < tick) {
            ++telemetry_.rejectedCommands;
        } else if (pending.command.tick > tick) {
            futureRequests.push_back(pending);
        } else if (!authority_.accepts(
                       pending.command.islandId,
                       pending.command.authorityEpoch,
                       pending.command.tick)) {
            ++telemetry_.rejectedCommands;
            ++telemetry_.staleAuthorityPackets;
        } else {
            due.push_back(pending);
        }
    }
    pendingRequests_ = std::move(futureRequests);

    std::vector<CanonicalNetworkCommand> futureServer;
    for (const auto& command : pendingServerCommands_) {
        if (command.tick < tick) {
            ++telemetry_.rejectedCommands;
        } else if (command.tick > tick) {
            futureServer.push_back(command);
        } else {
            due.push_back({command, false});
        }
    }
    pendingServerCommands_ = std::move(futureServer);

    // Client requests and server corrections share one canonical order.
    // Keeping them in separate loops would silently move corrections after
    // impulses even though the protocol priority says the opposite.
    std::stable_sort(due.begin(), due.end(),
        [](const PendingRequest& lhs, const PendingRequest& rhs) {
            return sessionRequestLess(lhs.command, rhs.command);
        });
    for (const auto& pending : due) {
        const bool serverAuthored =
            (pending.command.flags & NetworkCommandServerIssued) != 0u;
        if (!executeRequest(pending.command, serverAuthored)) {
            ++telemetry_.rejectedCommands;
        }
    }

    static_cast<void>(
        serverWorld_.step(static_cast<uint32_t>(tick)));
    serverTick_ = tick;
    if (!interestGrid_.rebuild(serverWorld_.bodies()))
        ++telemetry_.interestOverflows;
    const bool shouldSnapshot =
        serverTick_ % serverConfig_.snapshotIntervalTicks == 0u;
    if (shouldSnapshot) static_cast<void>(sendSnapshots());
    return true;
}

bool MultiplayerSession::queueServerCommand(
    CanonicalNetworkCommand command) {
    if (role_ != MultiplayerSessionRole::AuthoritativeServer)
        return false;
    if (command.tick == 0u) command.tick = serverTick_ + 1u;
    if (command.tick <= serverTick_
        || command.tick > serverTick_ + serverConfig_.inputFutureWindow
        || command.body >= serverWorld_.bodies().size()
        || !serverCommandType(command.type)) {
        return false;
    }
    command.islandId = serverConfig_.islandId;
    command.authorityEpoch = serverConfig_.authorityEpoch;
    command.flags |= NetworkCommandServerIssued;
    pendingServerCommands_.push_back(command);
    return true;
}

bool MultiplayerSession::queueCorrection(
    uint32_t bodyId, const LockstepBody& authoritativeBody) {
    if (role_ != MultiplayerSessionRole::AuthoritativeServer
        || bodyId == 0u || bodyId >= serverWorld_.bodies().size()
        || authoritativeBody.identity[0] != bodyId
        || !bodyAlive(authoritativeBody)
        || !bodyAlive(serverWorld_.bodies()[bodyId])
        || authoritativeBody.identity[1]
            != serverWorld_.bodies()[bodyId].identity[1]) {
        return false;
    }
    CanonicalNetworkCommand correction;
    correction.tick = serverTick_ + 1u;
    correction.islandId = serverConfig_.islandId;
    correction.authorityEpoch = serverConfig_.authorityEpoch;
    correction.type = NetworkCommandType::Correction;
    correction.body = bodyId;
    correction.generation = authoritativeBody.identity[1];
    correction.flags = NetworkCommandServerIssued
        | NetworkCommandCorrectionEvent;
    correction.payload[0] = authoritativeBody.sectorRadius[0];
    correction.payload[1] = authoritativeBody.sectorRadius[1];
    correction.payload[2] = authoritativeBody.sectorRadius[2];
    correction.payload[4] = authoritativeBody.positionInvMass[0];
    correction.payload[5] = authoritativeBody.positionInvMass[1];
    correction.payload[6] = authoritativeBody.positionInvMass[2];
    correction.payload[8] = authoritativeBody.linearVelocity[0];
    correction.payload[9] = authoritativeBody.linearVelocity[1];
    correction.payload[10] = authoritativeBody.linearVelocity[2];
    return queueServerCommand(correction);
}

std::optional<IslandAuthority> MultiplayerSession::advanceAuthority(
    uint32_t destinationWorker, uint64_t startTick) {
    if (role_ != MultiplayerSessionRole::AuthoritativeServer
        || startTick <= serverTick_ || pendingAuthority_.has_value()) {
        return std::nullopt;
    }
    const auto current = authority_.find(serverConfig_.islandId);
    if (!current.has_value()
        || current->epoch == std::numeric_limits<uint32_t>::max()) {
        return std::nullopt;
    }
    const auto snapshot = makeFullSnapshot();
    const std::array<uint64_t, 2> hash{
        snapshot.stateHash,
        (uint64_t{snapshot.stateHash} << 32u) | snapshot.stateHash,
    };
    IslandAuthority next{
        .islandId = serverConfig_.islandId,
        .epoch = current->epoch + 1u,
        .workerId = destinationWorker,
        .startTick = startTick,
        .checkpointHash = hash,
    };
    pendingAuthority_ = next;
    return next;
}

AuthoritativeSnapshot MultiplayerSession::makeFullSnapshot(
    std::span<const uint32_t> bodyIds) const {
    AuthoritativeSnapshot snapshot;
    snapshot.tick = serverTick_;
    snapshot.islandId = serverConfig_.islandId;
    snapshot.authorityEpoch = serverConfig_.authorityEpoch;
    snapshot.full = true;
    if (bodyIds.empty()) {
        for (const auto& body : serverWorld_.bodies()) {
            if (bodyAlive(body)) snapshot.bodies.push_back(body);
        }
    } else {
        snapshot.bodies.reserve(bodyIds.size());
        for (uint32_t id : bodyIds) {
            if (id < serverWorld_.bodies().size()
                && bodyAlive(serverWorld_.bodies()[id])) {
                snapshot.bodies.push_back(serverWorld_.bodies()[id]);
            }
        }
    }
    std::stable_sort(snapshot.bodies.begin(), snapshot.bodies.end(),
        [](const LockstepBody& lhs, const LockstepBody& rhs) {
            return std::tie(lhs.identity[0], lhs.identity[1])
                < std::tie(rhs.identity[0], rhs.identity[1]);
        });
    snapshot.bodies.erase(std::unique(
        snapshot.bodies.begin(), snapshot.bodies.end(),
        [](const LockstepBody& lhs, const LockstepBody& rhs) {
            return lhs.identity[0] == rhs.identity[0];
        }), snapshot.bodies.end());
    snapshot.stateHash = snapshotStateHash(snapshot);
    return snapshot;
}

std::optional<AuthoritativeSnapshot>
MultiplayerSession::makeClientSnapshot(
    const ServerPeer& peer, bool* interestOverflow) const {
    if (peer.controlledBody >= serverWorld_.bodies().size()
        || !bodyAlive(serverWorld_.bodies()[peer.controlledBody])) {
        return std::nullopt;
    }
    const auto interest = interestGrid_.query(
        interestGrid_.cellFor(
            serverWorld_.bodies()[peer.controlledBody]),
        peer.interestRadiusCells);
    std::vector<uint32_t> bodyIds = interest.bodyIds;
    std::erase(bodyIds, peer.controlledBody);
    const InterestCell center = interestGrid_.cellFor(
        serverWorld_.bodies()[peer.controlledBody]);
    // The world is immutable during selection. Compute each cell distance
    // once instead of repeating three integer divisions per comparison side.
    std::vector<std::pair<uint64_t, uint32_t>> rankedBodies;
    rankedBodies.reserve(bodyIds.size());
    for (const uint32_t id : bodyIds) {
        const InterestCell cell = interestGrid_.cellFor(serverWorld_.bodies()[id]);
        uint64_t distance = 0u;
        for (uint32_t axis = 0u; axis < 3u; ++axis) {
            const int64_t delta = int64_t{cell.coordinate[axis]}
                - center.coordinate[axis];
            distance += static_cast<uint64_t>(delta * delta);
        }
        rankedBodies.emplace_back(distance, id);
    }
    std::stable_sort(rankedBodies.begin(), rankedBodies.end());
    for (size_t index = 0u; index < bodyIds.size(); ++index)
        bodyIds[index] = rankedBodies[index].second;
    const size_t otherCapacity =
        peer.maximumSnapshotBodies > 0u
        ? peer.maximumSnapshotBodies - 1u : 0u;
    const bool peerLimitExceeded = bodyIds.size() > otherCapacity;
    if (peerLimitExceeded) bodyIds.resize(otherCapacity);
    bodyIds.push_back(peer.controlledBody);
    std::sort(bodyIds.begin(), bodyIds.end());
    if (interestOverflow != nullptr) {
        *interestOverflow = interest.overflow || peerLimitExceeded;
    }
    return makeFullSnapshot(bodyIds);
}

bool MultiplayerSession::sendSnapshots() {
    if (role_ != MultiplayerSessionRole::AuthoritativeServer)
        return false;
    bool allSent = true;
    for (auto& peer : serverPeers_) {
        bool interestOverflow = false;
        auto current = makeClientSnapshot(peer, &interestOverflow);
        if (interestOverflow) ++telemetry_.interestOverflows;
        if (!current.has_value()
            || !peer.snapshots.store(*current)) {
            allSent = false;
            continue;
        }
        AuthoritativeSnapshot outgoing = *current;
        bool delta = false;
        const auto acknowledged =
            snapshotAcks_.acknowledgedTick(peer.clientId);
        if (!peer.forceFullSnapshot && acknowledged.has_value()
            && *acknowledged < current->tick) {
            const auto candidate =
                peer.snapshots.deltaFrom(*current, *acknowledged);
            if (candidate.has_value()) {
                const auto fullBytes = SnapshotCodec::encode(*current);
                const auto deltaBytes = SnapshotCodec::encode(*candidate);
                if (!deltaBytes.empty() && !fullBytes.empty()
                    && deltaBytes.size() < fullBytes.size()) {
                    outgoing = *candidate;
                    delta = true;
                }
            }
        }
        const auto payload = SnapshotCodec::encode(outgoing);
        if (payload.empty()) {
            allSent = false;
            continue;
        }
        DeliveryClass delivery = DeliveryClass::ReliableControl;
        if (delta
            && payload.size() + kNetworkPacketOverheadBytes
                <= kConservativeRealtimeMtu) {
            delivery = DeliveryClass::Realtime;
        }
        if (!sendPacket(peer.clientId, delivery,
                        PacketPayloadType::Snapshot,
                        outgoing.tick, payload)) {
            allSent = false;
            continue;
        }
        if (delta) ++telemetry_.deltaSnapshots;
        else ++telemetry_.fullSnapshots;
    }
    return allSent;
}

bool MultiplayerSession::initializeOrRebaseClient(
    const AuthoritativeSnapshot& snapshot, bool hardResync) {
    if (!snapshot.full
        || snapshot.islandId != clientConfig_.islandId
        || snapshot.authorityEpoch == 0u
        || snapshot.bodies.size()
            > clientConfig_.prediction.maximumPredictedBodies
        || snapshot.tick > std::numeric_limits<uint32_t>::max()) {
        return false;
    }
    std::vector<LockstepBody> bodies(
        clientConfig_.prediction.world.bodyCapacity);
    for (const auto& body : snapshot.bodies) {
        if (body.identity[0] >= bodies.size()) return false;
        bodies[body.identity[0]] = body;
    }
    if (clientConfig_.controlledBody >= bodies.size()
        || !bodyAlive(bodies[clientConfig_.controlledBody])) {
        return false;
    }
    PredictionBubble replacement;
    if (!replacement.initialize(
            clientConfig_.prediction, snapshot.islandId,
            snapshot.authorityEpoch, clientConfig_.controlledBody,
            bodies, snapshot.tick)) {
        return false;
    }
    std::vector<uint32_t> predictedBodies;
    predictedBodies.reserve(snapshot.bodies.size());
    for (const auto& body : snapshot.bodies)
        predictedBodies.push_back(body.identity[0]);
    if (!replacement.setMembership(
            predictedBodies, std::span<const uint32_t>{})) {
        return false;
    }
    prediction_ = std::move(replacement);
    clientConfig_.authorityEpoch = snapshot.authorityEpoch;
    // Inputs queued against the previous authority are not meaningful after
    // a rebase. Do not retransmit them under the new epoch.
    localInputs_ = InputRedundancyBuffer(std::max(
        clientConfig_.snapshotHistoryTicks, kInputRedundancyFrames));
    clientReady_ = true;
    if (hardResync) ++telemetry_.hardResyncs;
    return true;
}

bool MultiplayerSession::processClientSnapshot(const Packet& packet) {
    const auto decoded = SnapshotCodec::decode(packet.payload);
    if (!decoded.snapshot.has_value()) {
        ++telemetry_.snapshotDecodeFailures;
        return false;
    }
    AuthoritativeSnapshot snapshot = *decoded.snapshot;
    if (snapshot.tick != packet.header.tick
        || snapshot.islandId != clientConfig_.islandId
        || snapshot.authorityEpoch != packet.header.authorityEpoch
        || snapshot.authorityEpoch < clientConfig_.authorityEpoch) {
        ++telemetry_.staleAuthorityPackets;
        return false;
    }

    const bool epochChanged =
        snapshot.authorityEpoch > clientConfig_.authorityEpoch;
    if (epochChanged && !snapshot.full) {
        ++telemetry_.snapshotDecodeFailures;
        static_cast<void>(requestFullSnapshot());
        return false;
    }
    if (!epochChanged && clientReady_
        && snapshot.tick < lastAcceptedSnapshotTick_) {
        return true;
    }

    AuthoritativeSnapshot full;
    if (snapshot.full) {
        full = snapshot;
    } else {
        const auto rebuilt = clientSnapshots_.applyDelta(snapshot);
        if (!rebuilt.has_value()) {
            ++telemetry_.snapshotDecodeFailures;
            static_cast<void>(requestFullSnapshot());
            return false;
        }
        full = *rebuilt;
    }
    if (full.bodies.size()
        > clientConfig_.prediction.maximumPredictedBodies) {
        ++telemetry_.snapshotDecodeFailures;
        static_cast<void>(requestFullSnapshot());
        return false;
    }
    const auto existing = clientSnapshots_.find(
        full.islandId, full.authorityEpoch, full.tick);
    if (existing.has_value()
        && existing->stateHash != full.stateHash) {
        ++telemetry_.snapshotDecodeFailures;
        return false;
    }

    bool accepted = true;
    if (!clientReady_ || epochChanged) {
        accepted = initializeOrRebaseClient(
            full, clientReady_ || epochChanged);
        if (accepted && epochChanged) ++telemetry_.authorityChanges;
    } else {
        if (full.tick > prediction_.currentTick()) {
            const uint64_t gap = full.tick - prediction_.currentTick();
            if (gap > clientConfig_.prediction.historyTicks) {
                accepted = initializeOrRebaseClient(full, true);
            } else {
                while (accepted
                       && prediction_.currentTick() < full.tick) {
                    accepted = prediction_.predict(
                        prediction_.currentTick() + 1u, {});
                }
            }
        }
        if (accepted) accepted = prediction_.reconcile(full).accepted;
        if (accepted) {
            std::vector<uint32_t> predictedBodies;
            predictedBodies.reserve(full.bodies.size());
            for (const auto& body : full.bodies)
                predictedBodies.push_back(body.identity[0]);
            accepted = prediction_.setMembership(
                predictedBodies, std::span<const uint32_t>{});
        }
    }
    if (!accepted) {
        ++telemetry_.snapshotDecodeFailures;
        static_cast<void>(requestFullSnapshot());
        return false;
    }
    if (!clientSnapshots_.store(full)) {
        ++telemetry_.snapshotDecodeFailures;
        return false;
    }
    localInputs_.acknowledge(full.tick);
    lastAcceptedSnapshotTick_ = full.tick;
    static_cast<void>(sendSnapshotAck(full));
    return true;
}

bool MultiplayerSession::sendSnapshotAck(
    const AuthoritativeSnapshot& snapshot) {
    const auto payload =
        snapshotAckPayload(0u, snapshot.tick, snapshot.stateHash);
    return sendPacket(0u, DeliveryClass::Realtime,
                      PacketPayloadType::SnapshotAck,
                      snapshot.tick, payload);
}

bool MultiplayerSession::requestFullSnapshot() {
    const uint64_t tick = clientReady_
        ? prediction_.currentTick() : 0u;
    const auto payload =
        snapshotAckPayload(kSnapshotAckRequestFull, tick, 0u);
    ++telemetry_.fullSnapshotRequests;
    return sendPacket(0u, DeliveryClass::ReliableControl,
                      PacketPayloadType::SnapshotAck, tick, payload);
}

bool MultiplayerSession::advanceClientTick(InputFrame input) {
    if (role_ != MultiplayerSessionRole::Client || !clientReady_
        || input.tick != prediction_.currentTick() + 1u
        || !inputValueValid(input.moveXQ16)
        || !inputValueValid(input.moveZQ16)
        || !inputValueValid(input.lookXQ16)
        || !inputValueValid(input.lookYQ16)) {
        return false;
    }
    if (input.sequence == 0u) {
        if (!takeSequence(
                nextClientInputSequence_, input.sequence)) {
            return false;
        }
    } else {
        if (input.sequence == std::numeric_limits<uint64_t>::max())
            return false;
        nextClientInputSequence_ =
            std::max(nextClientInputSequence_, input.sequence + 1u);
    }

    const auto currentBodies = prediction_.bodies();
    if (clientConfig_.controlledBody >= currentBodies.size()
        || !bodyAlive(currentBodies[clientConfig_.controlledBody])) {
        return false;
    }
    CanonicalReplayCommand replay;
    replay.tick = input.tick;
    replay.sequence = input.sequence;
    replay.producer = clientConfig_.clientId;
    replay.type = ReplayCommandType::ApplyImpulse;
    replay.body = clientConfig_.controlledBody;
    replay.generation =
        currentBodies[clientConfig_.controlledBody].identity[1];
    replay.payload[0] = std::clamp(
        input.moveXQ16, -kMaximumAppliedImpulseQ16,
        kMaximumAppliedImpulseQ16);
    replay.payload[2] = std::clamp(
        input.moveZQ16, -kMaximumAppliedImpulseQ16,
        kMaximumAppliedImpulseQ16);
    auto replacementInputs = localInputs_;
    if (!replacementInputs.push(input)) return false;
    const std::array commands{replay};
    if (!prediction_.predict(input.tick, commands)) {
        return false;
    }
    localInputs_ = std::move(replacementInputs);
    const auto bundle = localInputs_.bundle();
    const auto payload = InputRedundancyBuffer::encode(bundle);
    // Prediction has committed. A transient send loss is not a failed client
    // tick; redundancy on the next packet is the recovery path.
    static_cast<void>(sendPacket(
        0u, DeliveryClass::Realtime, PacketPayloadType::Input,
        input.tick, payload));
    return true;
}

bool MultiplayerSession::sendClientCommand(
    CanonicalNetworkCommand command) {
    if (role_ != MultiplayerSessionRole::Client || !clientReady_
        || !isClientCommandAllowed(command.type)) {
        return false;
    }
    if (command.tick == 0u)
        command.tick = prediction_.currentTick() + 1u;
    if (command.tick <= prediction_.currentTick()
        || command.tick > prediction_.currentTick()
                + clientConfig_.commandFutureWindow) {
        return false;
    }
    if (!takeSequence(
            nextClientCommandSequence_, command.sequence)) {
        return false;
    }
    command.islandId = clientConfig_.islandId;
    command.authorityEpoch = clientConfig_.authorityEpoch;
    command.clientId = clientConfig_.clientId;
    command.flags = 0u;
    if (command.body == 0u)
        command.body = clientConfig_.controlledBody;
    if (command.body != clientConfig_.controlledBody
        || command.body >= prediction_.bodies().size()) {
        return false;
    }
    command.generation = prediction_.bodies()[command.body].identity[1];
    if (!validClientRequestShape(command)) return false;
    const std::array commands{command};
    const auto payload = NetworkCommandCodec::encode(commands);
    return !payload.empty()
        && sendPacket(0u, DeliveryClass::ReliableEvent,
                      PacketPayloadType::Command,
                      command.tick, payload);
}

bool MultiplayerSession::requestTickSync(uint64_t nowMicros) {
    if (role_ != MultiplayerSessionRole::Client) return false;
    const uint64_t tick = clientReady_
        ? prediction_.currentTick() : 0u;
    const auto payload =
        tickSyncPayload(kTickSyncRequest, nowMicros, tick, 0u);
    return sendPacket(0u, DeliveryClass::Realtime,
                      PacketPayloadType::TickSync, tick, payload);
}

bool MultiplayerSession::processClientTickSync(
    const Packet& packet, uint64_t nowMicros) {
    uint32_t mode = 0u;
    uint64_t clientSendMicros = 0u;
    uint64_t localTick = 0u;
    uint64_t serverTick = 0u;
    if (!decodeTickSync(packet.payload, mode, clientSendMicros,
                        localTick, serverTick)
        || mode != kTickSyncResponse
        || packet.header.tick != serverTick
        || nowMicros < clientSendMicros) {
        ++telemetry_.malformedPackets;
        return false;
    }
    const uint64_t receiveTick = clientReady_
        ? prediction_.currentTick() : localTick;
    return tickSynchronizer_.observe({
        .clientSendMicros = clientSendMicros,
        .clientReceiveMicros = nowMicros,
        .localReceiveTick = receiveTick,
        .serverTick = serverTick,
    });
}

uint64_t MultiplayerSession::currentTick() const noexcept {
    if (role_ == MultiplayerSessionRole::AuthoritativeServer)
        return serverTick_;
    if (role_ == MultiplayerSessionRole::Client && clientReady_)
        return prediction_.currentTick();
    return 0u;
}

uint64_t MultiplayerSession::islandId() const noexcept {
    if (role_ == MultiplayerSessionRole::AuthoritativeServer)
        return serverConfig_.islandId;
    if (role_ == MultiplayerSessionRole::Client)
        return clientConfig_.islandId;
    return 0u;
}

uint32_t MultiplayerSession::authorityEpoch() const noexcept {
    if (role_ == MultiplayerSessionRole::AuthoritativeServer)
        return serverConfig_.authorityEpoch;
    if (role_ == MultiplayerSessionRole::Client)
        return clientConfig_.authorityEpoch;
    return 0u;
}

std::span<const LockstepBody> MultiplayerSession::bodies() const noexcept {
    if (role_ == MultiplayerSessionRole::AuthoritativeServer)
        return serverWorld_.bodies();
    if (role_ == MultiplayerSessionRole::Client && clientReady_)
        return prediction_.bodies();
    return {};
}

std::optional<AuthoritativeSnapshot>
MultiplayerSession::authoritativeSnapshot() const {
    if (role_ == MultiplayerSessionRole::AuthoritativeServer)
        return makeFullSnapshot();
    if (role_ == MultiplayerSessionRole::Client && clientReady_)
        return prediction_.snapshot();
    return std::nullopt;
}

std::optional<AuthoritativeSnapshot>
MultiplayerSession::snapshotForClient(uint32_t clientId) const {
    if (role_ != MultiplayerSessionRole::AuthoritativeServer)
        return std::nullopt;
    const ServerPeer* peer = serverPeer(clientId);
    return peer != nullptr ? makeClientSnapshot(*peer) : std::nullopt;
}

const PredictionTelemetry* MultiplayerSession::predictionTelemetry()
    const noexcept {
    return role_ == MultiplayerSessionRole::Client && clientReady_
        ? &prediction_.telemetry() : nullptr;
}

const TickSynchronizer* MultiplayerSession::tickSynchronizer()
    const noexcept {
    return role_ == MultiplayerSessionRole::Client
        ? &tickSynchronizer_ : nullptr;
}

} // namespace voxy::network
