#include <gtest/gtest.h>

#include "network/multiplayer_session.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <tuple>
#include <utility>
#include <vector>

namespace voxy::network {
namespace {

using physics::deterministic::LockstepBody;
using physics::deterministic::LockstepBodyAlive;
using physics::deterministic::LockstepBodyAwake;

struct SimulatedWireFrame {
    uint32_t source = 0;
    uint32_t destination = 0;
    DeliveryClass delivery = DeliveryClass::Realtime;
    std::vector<std::byte> bytes;
};

class DeterministicNetwork {
public:
    void enqueue(SimulatedWireFrame frame) {
        pending_.push_back(std::move(frame));
    }

    [[nodiscard]] std::optional<MultiplayerTransportFrame> poll(
        uint32_t endpoint) {
        auto& queue = inboxes_[endpoint];
        if (queue.empty()) return std::nullopt;
        SimulatedWireFrame wire = std::move(queue.front());
        queue.pop_front();
        return MultiplayerTransportFrame{
            .peerId = wire.source,
            .delivery = wire.delivery,
            .bytes = std::move(wire.bytes),
        };
    }

    void deliver(
        const std::function<bool(const SimulatedWireFrame&)>& select = {},
        const std::function<bool(const SimulatedWireFrame&)>& drop = {},
        bool reverse = false, bool duplicate = false) {
        std::vector<SimulatedWireFrame> selected;
        std::vector<SimulatedWireFrame> retained;
        for (auto& frame : pending_) {
            if (!select || select(frame))
                selected.push_back(std::move(frame));
            else
                retained.push_back(std::move(frame));
        }
        pending_ = std::move(retained);
        if (reverse) std::reverse(selected.begin(), selected.end());
        for (auto& frame : selected) {
            if (drop && drop(frame)) continue;
            if (duplicate) inboxes_[frame.destination].push_back(frame);
            inboxes_[frame.destination].push_back(std::move(frame));
        }
    }

    [[nodiscard]] size_t pending() const noexcept { return pending_.size(); }

    [[nodiscard]] size_t countPending(
        const std::function<bool(const SimulatedWireFrame&)>& select) const {
        return static_cast<size_t>(std::count_if(
            pending_.begin(), pending_.end(), select));
    }

    size_t rewriteSequence(
        const std::function<bool(const SimulatedWireFrame&)>& select,
        uint64_t sequence) {
        size_t rewritten = 0u;
        for (auto& frame : pending_) {
            if (!select(frame)) continue;
            const auto decoded =
                PacketCodec::decode(frame.bytes, frame.delivery);
            if (!decoded.packet.has_value()) continue;
            Packet replacement = *decoded.packet;
            replacement.header.sequence = sequence;
            const auto encoded =
                PacketCodec::encode(replacement, frame.delivery);
            if (!encoded.error.empty()) continue;
            frame.bytes = encoded.bytes;
            ++rewritten;
        }
        return rewritten;
    }

private:
    std::vector<SimulatedWireFrame> pending_;
    std::map<uint32_t, std::deque<SimulatedWireFrame>> inboxes_;
};

class SimulatedEndpoint final : public IMultiplayerTransport {
public:
    SimulatedEndpoint(
        uint32_t endpoint, std::shared_ptr<DeterministicNetwork> network)
        : endpoint_(endpoint), network_(std::move(network)) {}

    [[nodiscard]] bool send(
        uint32_t peerId, DeliveryClass delivery,
        std::span<const std::byte> bytes) override {
        if (closed_ || !network_) return false;
        network_->enqueue({
            .source = endpoint_,
            .destination = peerId,
            .delivery = delivery,
            .bytes = std::vector<std::byte>(bytes.begin(), bytes.end()),
        });
        return true;
    }

    [[nodiscard]] std::optional<MultiplayerTransportFrame> poll() override {
        return closed_ || !network_
            ? std::nullopt : network_->poll(endpoint_);
    }

    void close() override { closed_ = true; }

private:
    uint32_t endpoint_ = 0;
    std::shared_ptr<DeterministicNetwork> network_;
    bool closed_ = false;
};

struct TransportProbe {
    uint32_t closeCalls = 0u;
    uint32_t serviceCalls = 0u;
    bool failSends = false;
};

class ProbedEndpoint final : public IMultiplayerTransport {
public:
    explicit ProbedEndpoint(std::shared_ptr<TransportProbe> probe)
        : probe_(std::move(probe)) {}

    void service() override {
        if (probe_) ++probe_->serviceCalls;
    }

    [[nodiscard]] bool send(
        uint32_t, DeliveryClass,
        std::span<const std::byte>) override {
        return probe_ && !probe_->failSends;
    }

    [[nodiscard]] std::optional<MultiplayerTransportFrame>
    poll() override {
        return std::nullopt;
    }

    void close() override {
        if (probe_) ++probe_->closeCalls;
    }

private:
    std::shared_ptr<TransportProbe> probe_;
};

class LifecycleEndpoint final : public IMultiplayerTransport {
public:
    LifecycleEndpoint() {
        MultiplayerTransportFrame connected;
        connected.peerId = 0u;
        connected.type = MultiplayerTransportFrameType::Connected;
        connected.connectionSerial = 41u;
        frames_.push_back(std::move(connected));

        MultiplayerTransportFrame disconnected;
        disconnected.peerId = 0u;
        disconnected.type = MultiplayerTransportFrameType::Disconnected;
        disconnected.connectionSerial = 41u;
        frames_.push_back(std::move(disconnected));
    }

    [[nodiscard]] bool send(
        uint32_t, DeliveryClass,
        std::span<const std::byte>) override {
        return true;
    }

    [[nodiscard]] std::optional<MultiplayerTransportFrame>
    poll() override {
        if (frames_.empty()) return std::nullopt;
        auto frame = std::move(frames_.front());
        frames_.pop_front();
        return frame;
    }

    void close() override {}

private:
    std::deque<MultiplayerTransportFrame> frames_;
};

LockstepBody dynamicBody(uint32_t id, int32_t xQ12) {
    LockstepBody body;
    body.identity = {id, 1u, LockstepBodyAlive | LockstepBodyAwake, 0u};
    body.sectorRadius = {
        0, 0, 0, physics::deterministic::kLockstepPositionOne / 2};
    body.positionInvMass = {
        xQ12,
        4 * physics::deterministic::kLockstepPositionOne,
        0,
        physics::deterministic::kLockstepVelocityOne,
    };
    return body;
}

std::optional<Packet> packet(const SimulatedWireFrame& frame) {
    return PacketCodec::decode(frame.bytes, frame.delivery).packet;
}

bool packetIs(
    const SimulatedWireFrame& frame, PacketPayloadType type,
    std::optional<uint64_t> tick = std::nullopt) {
    const auto decoded = packet(frame);
    return decoded.has_value()
        && decoded->header.payloadType == type
        && (!tick.has_value() || decoded->header.tick == *tick);
}

bool packetAuthorityIs(
    const SimulatedWireFrame& frame, uint32_t authorityEpoch) {
    const auto decoded = packet(frame);
    return decoded.has_value()
        && decoded->header.authorityEpoch == authorityEpoch;
}

bool fullSnapshotPacket(
    const SimulatedWireFrame& frame,
    std::optional<uint64_t> tick = std::nullopt) {
    const auto decoded = packet(frame);
    if (!decoded.has_value()
        || decoded->header.payloadType != PacketPayloadType::Snapshot
        || (tick.has_value() && decoded->header.tick != *tick)) {
        return false;
    }
    const auto snapshot = SnapshotCodec::decode(decoded->payload);
    return snapshot.snapshot.has_value() && snapshot.snapshot->full;
}

MultiplayerSession::ServerConfig serverConfig() {
    MultiplayerSession::ServerConfig config;
    config.identity = {
        .sessionId = 0x1002u,
        .worldId = 0x2003u,
        .worldEpoch = 4u,
    };
    config.world.bodyCapacity = 16u;
    config.world.contactCapacity = 64u;
    config.world.gravityPerSubstepQ16 = 0;
    config.islandId = 7u;
    config.authorityEpoch = 1u;
    config.maximumClients = 8u;
    config.snapshotHistoryTicks = 16u;
    return config;
}

MultiplayerSession::ClientConfig clientConfig(
    uint32_t clientId, uint32_t controlledBody) {
    MultiplayerSession::ClientConfig config;
    config.identity = serverConfig().identity;
    config.clientId = clientId;
    config.controlledBody = controlledBody;
    config.islandId = serverConfig().islandId;
    config.authorityEpoch = serverConfig().authorityEpoch;
    config.prediction.world = serverConfig().world;
    config.prediction.historyTicks = 16u;
    config.snapshotHistoryTicks = 16u;
    return config;
}

InputFrame input(uint64_t tick, int32_t moveXQ16) {
    return InputFrame{
        .tick = tick,
        .moveXQ16 = moveXQ16,
    };
}

void expectClientMatches(
    const MultiplayerSession& server,
    const MultiplayerSession& client, uint32_t clientId) {
    const auto authoritative = server.snapshotForClient(clientId);
    const auto predicted = client.authoritativeSnapshot();
    ASSERT_TRUE(authoritative.has_value());
    ASSERT_TRUE(predicted.has_value());
    EXPECT_EQ(predicted->tick, authoritative->tick);
    EXPECT_EQ(predicted->authorityEpoch, authoritative->authorityEpoch);
    EXPECT_EQ(predicted->stateHash, authoritative->stateHash);
}

TEST(MultiplayerSessionIntegration,
     FourClientsRecoverLossReorderingDuplicatesAndCorrection) {
    auto network = std::make_shared<DeterministicNetwork>();
    std::vector<LockstepBody> bodies(16u);
    bodies[1] = dynamicBody(
        1u, -2 * physics::deterministic::kLockstepPositionOne);
    bodies[2] = dynamicBody(2u, 0);
    bodies[3] = dynamicBody(
        3u, 2 * physics::deterministic::kLockstepPositionOne);
    bodies[4] = dynamicBody(
        4u, 4 * physics::deterministic::kLockstepPositionOne);

    MultiplayerSession server;
    ASSERT_TRUE(server.initializeServer(
        serverConfig(), bodies,
        std::make_unique<SimulatedEndpoint>(0u, network)));
    std::vector<MultiplayerSession> clients(4u);
    for (uint32_t index = 0u; index < clients.size(); ++index) {
        const uint32_t clientId = index + 1u;
        ASSERT_TRUE(server.addClient({
            .clientId = clientId,
            .controlledBody = clientId,
            .interestRadiusCells = 4u,
        }));
        ASSERT_TRUE(clients[index].initializeClient(
            clientConfig(clientId, clientId),
            std::make_unique<SimulatedEndpoint>(clientId, network)));
    }

    // Initial full snapshots may be duplicated and reordered without
    // initializing a client twice.
    ASSERT_TRUE(server.sendSnapshots());
    network->deliver({}, {}, true, true);
    for (auto& client : clients) {
        static_cast<void>(client.pump());
        ASSERT_TRUE(client.clientReady());
        EXPECT_EQ(client.currentTick(), 0u);
        EXPECT_GE(client.telemetry().duplicatePackets, 1u);
    }
    network->deliver({}, {}, true, true);
    static_cast<void>(server.pump());
    EXPECT_GE(server.telemetry().duplicatePackets, clients.size());

    ASSERT_TRUE(clients[0].requestTickSync(1'000u));
    network->deliver();
    ASSERT_TRUE(server.pump(2'000u));
    network->deliver();
    ASSERT_TRUE(clients[0].pump(5'000u));
    ASSERT_NE(clients[0].tickSynchronizer(), nullptr);
    EXPECT_EQ(clients[0].tickSynchronizer()->minimumRttMicros(), 4'000u);

    // Every client predicts two ticks before the server advances. The first
    // packet from client 1 is lost. Its tick-1 frame is recovered from the
    // redundant tick-2 packet. All other packets arrive newest-first and are
    // duplicated.
    const std::array<int32_t, 4> movement{4'096, 0, 0, 0};
    for (uint32_t index = 0u; index < clients.size(); ++index) {
        ASSERT_TRUE(clients[index].advanceClientTick(
            input(1u, movement[index])));
        ASSERT_TRUE(clients[index].advanceClientTick(
            input(2u, movement[index])));
    }
    network->deliver(
        {},
        [](const SimulatedWireFrame& frame) {
            return frame.source == 1u && frame.destination == 0u
                && packetIs(frame, PacketPayloadType::Input, 1u);
        },
        true, true);
    static_cast<void>(server.pump());
    EXPECT_GE(server.telemetry().redundantInputsRecovered, 1u);
    EXPECT_GE(server.telemetry().duplicatePackets, clients.size());

    ASSERT_TRUE(server.stepServer());
    ASSERT_TRUE(server.stepServer());
    network->deliver({}, {}, true, true);
    for (auto& client : clients) static_cast<void>(client.pump());
    for (uint32_t index = 0u; index < clients.size(); ++index)
        expectClientMatches(server, clients[index], index + 1u);
    network->deliver({}, {}, true, true);
    static_cast<void>(server.pump());
    EXPECT_GT(server.telemetry().deltaSnapshots, 0u);

    // The server injects an authoritative correction while clients continue
    // predicting. Client 1 loses the correction tick snapshot, then recovers
    // from the next delta using its acknowledged tick-2 baseline.
    CanonicalNetworkCommand awake;
    awake.type = NetworkCommandType::SetAwakeRequest;
    awake.payload[0] = 1;
    ASSERT_TRUE(clients[0].sendClientCommand(awake));
    for (uint32_t index = 0u; index < clients.size(); ++index) {
        ASSERT_TRUE(clients[index].advanceClientTick(
            input(3u, movement[index])));
    }
    network->deliver({}, {}, true, false);
    ASSERT_TRUE(server.pump());
    EXPECT_EQ(server.telemetry().acceptedCommands, 1u);
    LockstepBody corrected = server.bodies()[1];
    corrected.positionInvMass[0] += 512;
    ASSERT_TRUE(server.queueCorrection(1u, corrected));
    ASSERT_TRUE(server.stepServer());
    network->deliver(
        {},
        [](const SimulatedWireFrame& frame) {
            return frame.destination == 1u
                && packetIs(frame, PacketPayloadType::Snapshot, 3u);
        },
        true, false);
    for (auto& client : clients) static_cast<void>(client.pump());

    for (uint32_t index = 0u; index < clients.size(); ++index) {
        ASSERT_TRUE(clients[index].advanceClientTick(
            input(4u, movement[index])));
    }
    network->deliver({}, {}, true, true);
    static_cast<void>(server.pump());
    ASSERT_TRUE(server.stepServer());
    network->deliver({}, {}, true, true);
    for (auto& client : clients) static_cast<void>(client.pump());
    for (uint32_t index = 0u; index < clients.size(); ++index)
        expectClientMatches(server, clients[index], index + 1u);

    ASSERT_NE(clients[0].predictionTelemetry(), nullptr);
    EXPECT_GE(clients[0].predictionTelemetry()->rollbacks, 1u);
    EXPECT_TRUE(std::is_sorted(
        server.recordedNetworkCommands().begin(),
        server.recordedNetworkCommands().end(),
        canonicalNetworkCommandLess));
    EXPECT_TRUE(std::is_sorted(
        server.recordedReplayCommands().begin(),
        server.recordedReplayCommands().end(),
        physics::deterministic::canonicalReplayCommandLess));
    EXPECT_TRUE(std::any_of(
        server.recordedNetworkCommands().begin(),
        server.recordedNetworkCommands().end(),
        [](const CanonicalNetworkCommand& command) {
            return (command.flags & NetworkCommandCorrectionEvent) != 0u;
        }));
}

TEST(MultiplayerSessionIntegration,
     AuthorityEpochForcesFullRebaseAndRejectsOldTraffic) {
    auto network = std::make_shared<DeterministicNetwork>();
    std::vector<LockstepBody> bodies(16u);
    bodies[1] = dynamicBody(1u, -4'096);
    bodies[2] = dynamicBody(2u, 4'096);

    MultiplayerSession server;
    ASSERT_TRUE(server.initializeServer(
        serverConfig(), bodies,
        std::make_unique<SimulatedEndpoint>(0u, network)));
    ASSERT_TRUE(server.addClient({1u, 1u, 4u}));
    ASSERT_TRUE(server.addClient({2u, 2u, 4u}));

    MultiplayerSession first;
    MultiplayerSession second;
    ASSERT_TRUE(first.initializeClient(
        clientConfig(1u, 1u),
        std::make_unique<SimulatedEndpoint>(1u, network)));
    ASSERT_TRUE(second.initializeClient(
        clientConfig(2u, 2u),
        std::make_unique<SimulatedEndpoint>(2u, network)));
    ASSERT_TRUE(server.sendSnapshots());
    network->deliver();
    ASSERT_TRUE(first.pump());
    ASSERT_TRUE(second.pump());
    network->deliver();
    ASSERT_TRUE(server.pump());

    const auto migrated = server.advanceAuthority(9u, 2u);
    ASSERT_TRUE(migrated.has_value());
    EXPECT_EQ(migrated->epoch, 2u);

    // A scheduled handoff does not change emitted authority early. Tick 1 is
    // still accepted and replicated under epoch 1.
    ASSERT_TRUE(first.advanceClientTick(input(1u, 4'096)));
    ASSERT_TRUE(second.advanceClientTick(input(1u, -4'096)));
    network->deliver();
    ASSERT_TRUE(server.pump());
    ASSERT_TRUE(server.stepServer());
    EXPECT_EQ(server.authorityEpoch(), 1u);
    network->deliver();
    ASSERT_TRUE(first.pump());
    ASSERT_TRUE(second.pump());
    EXPECT_EQ(first.authorityEpoch(), 1u);
    EXPECT_EQ(second.authorityEpoch(), 1u);
    network->deliver();
    ASSERT_TRUE(server.pump());

    // Tick-2 packets are still stamped epoch 1 when sent. The server activates
    // epoch 2 at that exact tick, rejects the old authority, and sends a full
    // rebase snapshot.
    ASSERT_TRUE(first.advanceClientTick(input(2u, 4'096)));
    ASSERT_TRUE(second.advanceClientTick(input(2u, -4'096)));
    network->deliver(
        [](const SimulatedWireFrame& frame) {
            return frame.destination == 0u;
        });
    ASSERT_TRUE(server.pump());
    ASSERT_TRUE(server.stepServer());
    EXPECT_EQ(server.currentTick(), 2u);
    EXPECT_EQ(server.telemetry().staleAuthorityPackets, 2u);

    EXPECT_EQ(network->rewriteSequence(
        [](const SimulatedWireFrame& frame) {
            return (frame.destination == 1u || frame.destination == 2u)
                && packetIs(frame, PacketPayloadType::Snapshot, 2u);
        },
        1u), 2u);
    network->deliver({}, {}, true, false);
    ASSERT_TRUE(first.pump());
    ASSERT_TRUE(second.pump());
    EXPECT_EQ(first.authorityEpoch(), 2u);
    EXPECT_EQ(second.authorityEpoch(), 2u);
    EXPECT_EQ(first.currentTick(), 2u);
    EXPECT_GE(first.telemetry().hardResyncs, 1u);

    network->deliver();
    ASSERT_TRUE(server.pump());
    ASSERT_TRUE(first.advanceClientTick(input(3u, 4'096)));
    ASSERT_TRUE(second.advanceClientTick(input(3u, -4'096)));
    network->deliver({}, {}, true, true);
    static_cast<void>(server.pump());
    ASSERT_TRUE(server.stepServer());
    network->deliver({}, {}, true, true);
    static_cast<void>(first.pump());
    static_cast<void>(second.pump());
    expectClientMatches(server, first, 1u);
    expectClientMatches(server, second, 2u);
    EXPECT_EQ(server.authorityEpoch(), 2u);
    EXPECT_GE(server.telemetry().authorityChanges, 1u);
    EXPECT_TRUE(server.removeClient(2u));
    EXPECT_FALSE(server.removeClient(2u));
    EXPECT_TRUE(server.addClient({2u, 2u, 4u}));
    ASSERT_TRUE(server.sendSnapshots());
    EXPECT_EQ(network->countPending(
        [](const SimulatedWireFrame& frame) {
            return frame.destination == 2u
                && fullSnapshotPacket(frame, 3u);
        }), 1u);
    const uint64_t decodeFailures =
        second.telemetry().snapshotDecodeFailures;
    network->deliver(
        [](const SimulatedWireFrame& frame) {
            return frame.destination == 2u;
        });
    ASSERT_TRUE(second.pump());
    EXPECT_EQ(second.telemetry().snapshotDecodeFailures, decodeFailures);
    expectClientMatches(server, second, 2u);
}

TEST(MultiplayerSessionIntegration,
     DeliveryClassesKeepIndependentSequenceWindows) {
    auto network = std::make_shared<DeterministicNetwork>();
    std::vector<LockstepBody> bodies(16u);
    bodies[1] = dynamicBody(1u, -4'096);
    bodies[2] = dynamicBody(2u, 4'096);

    MultiplayerSession server;
    ASSERT_TRUE(server.initializeServer(
        serverConfig(), bodies,
        std::make_unique<SimulatedEndpoint>(0u, network)));
    ASSERT_TRUE(server.addClient({1u, 1u, 4u}));
    ASSERT_TRUE(server.addClient({2u, 2u, 4u}));
    MultiplayerSession client;
    ASSERT_TRUE(client.initializeClient(
        clientConfig(1u, 1u),
        std::make_unique<SimulatedEndpoint>(1u, network)));
    ASSERT_TRUE(server.sendSnapshots());
    network->deliver(
        [](const SimulatedWireFrame& frame) {
            return frame.destination == 1u;
        });
    ASSERT_TRUE(client.pump());
    network->deliver(
        [](const SimulatedWireFrame& frame) {
            return frame.source == 1u && frame.destination == 0u;
        });
    ASSERT_TRUE(server.pump());

    // Delay one reliable command and one reliable-control snapshot behind
    // more than a complete realtime acknowledgement window.
    CanonicalNetworkCommand awake;
    awake.type = NetworkCommandType::SetAwakeRequest;
    awake.payload[0] = 1;
    ASSERT_TRUE(client.sendClientCommand(awake));
    ASSERT_TRUE(server.sendSnapshots());
    for (uint64_t sample = 0u; sample < 70u; ++sample)
        ASSERT_TRUE(client.requestTickSync(1'000u + sample));

    network->deliver(
        [](const SimulatedWireFrame& frame) {
            return frame.source == 1u && frame.destination == 0u
                && frame.delivery == DeliveryClass::Realtime;
        });
    ASSERT_TRUE(server.pump(2'000u));
    network->deliver(
        [](const SimulatedWireFrame& frame) {
            return frame.source == 0u && frame.destination == 1u
                && frame.delivery == DeliveryClass::Realtime;
        });
    ASSERT_TRUE(client.pump(5'000u));

    network->deliver(
        [](const SimulatedWireFrame& frame) {
            return frame.source == 1u && frame.destination == 0u
                && frame.delivery == DeliveryClass::ReliableEvent;
        });
    ASSERT_TRUE(server.pump());
    EXPECT_EQ(server.telemetry().acceptedCommands, 1u);

    network->deliver(
        [](const SimulatedWireFrame& frame) {
            return frame.source == 0u && frame.destination == 1u
                && frame.delivery == DeliveryClass::ReliableControl;
        });
    ASSERT_TRUE(client.pump());
    EXPECT_EQ(client.telemetry().duplicatePackets, 0u);
    EXPECT_EQ(client.telemetry().snapshotDecodeFailures, 0u);
}

TEST(MultiplayerSessionIntegration,
     AuthorityRebaseDropsFutureInputsFromThePreviousEpoch) {
    auto network = std::make_shared<DeterministicNetwork>();
    std::vector<LockstepBody> bodies(16u);
    bodies[1] = dynamicBody(1u, -4'096);
    bodies[2] = dynamicBody(2u, 4'096);

    MultiplayerSession server;
    ASSERT_TRUE(server.initializeServer(
        serverConfig(), bodies,
        std::make_unique<SimulatedEndpoint>(0u, network)));
    ASSERT_TRUE(server.addClient({1u, 1u, 4u}));
    ASSERT_TRUE(server.addClient({2u, 2u, 4u}));
    MultiplayerSession client;
    ASSERT_TRUE(client.initializeClient(
        clientConfig(1u, 1u),
        std::make_unique<SimulatedEndpoint>(1u, network)));
    ASSERT_TRUE(server.sendSnapshots());
    network->deliver(
        [](const SimulatedWireFrame& frame) {
            return frame.destination == 1u;
        });
    ASSERT_TRUE(client.pump());
    network->deliver(
        [](const SimulatedWireFrame& frame) {
            return frame.destination == 0u;
        });
    ASSERT_TRUE(server.pump());

    ASSERT_TRUE(server.advanceAuthority(7u, 1u).has_value());
    ASSERT_TRUE(client.advanceClientTick(input(1u, 1'024)));
    ASSERT_TRUE(client.advanceClientTick(input(2u, 1'024)));
    ASSERT_TRUE(client.advanceClientTick(input(3u, 1'024)));

    // Do not deliver the old-epoch input packets. The switch snapshot rewinds
    // the client from predicted tick 3 to authoritative tick 1.
    ASSERT_TRUE(server.stepServer());
    network->deliver(
        [](const SimulatedWireFrame& frame) {
            return frame.source == 0u && frame.destination == 1u
                && packetIs(frame, PacketPayloadType::Snapshot, 1u);
        });
    ASSERT_TRUE(client.pump());
    ASSERT_EQ(client.authorityEpoch(), 2u);
    ASSERT_EQ(client.currentTick(), 1u);

    ASSERT_TRUE(client.advanceClientTick(input(2u, 2'048)));
    network->deliver(
        [](const SimulatedWireFrame& frame) {
            return frame.source == 1u && frame.destination == 0u
                && packetAuthorityIs(frame, 2u)
                && packetIs(frame, PacketPayloadType::Input, 2u);
        });
    ASSERT_TRUE(server.pump());
    EXPECT_EQ(server.telemetry().acceptedInputs, 1u);
    EXPECT_EQ(server.telemetry().rejectedInputs, 0u);
}

TEST(MultiplayerSessionIntegration,
     LateJoinUsesABoundedFailClosedInterestSnapshot) {
    auto network = std::make_shared<DeterministicNetwork>();
    std::vector<LockstepBody> bodies(16u);
    for (uint32_t id = 1u; id <= 8u; ++id)
        bodies[id] = dynamicBody(id, static_cast<int32_t>(id * 512u));

    auto config = serverConfig();
    config.interest.maximumEntries = 16u;
    config.interest.maximumQueryBodies = 4u;
    MultiplayerSession server;
    ASSERT_TRUE(server.initializeServer(
        config, bodies,
        std::make_unique<SimulatedEndpoint>(0u, network)));
    ASSERT_TRUE(server.stepServer());
    ASSERT_TRUE(server.stepServer());
    ASSERT_TRUE(server.stepServer());

    EXPECT_FALSE(server.addClient({
        .clientId = 9u,
        .controlledBody = 1u,
        .interestRadiusCells = 4u,
        .maximumSnapshotBodies = 257u,
    }));
    ASSERT_TRUE(server.addClient({
        .clientId = 1u,
        .controlledBody = 1u,
        .interestRadiusCells = 4u,
        .maximumSnapshotBodies = 2u,
    }));
    EXPECT_FALSE(server.addClient({
        .clientId = 2u,
        .controlledBody = 1u,
        .interestRadiusCells = 4u,
        .maximumSnapshotBodies = 2u,
    }));
    const auto bounded = server.snapshotForClient(1u);
    ASSERT_TRUE(bounded.has_value());
    ASSERT_EQ(bounded->bodies.size(), 2u);
    EXPECT_EQ(bounded->bodies[0].identity[0], 1u);
    const auto boundedAgain = server.snapshotForClient(1u);
    ASSERT_TRUE(boundedAgain.has_value());
    ASSERT_EQ(boundedAgain->bodies.size(), 2u);
    EXPECT_EQ(
        boundedAgain->bodies[1].identity[0],
        bounded->bodies[1].identity[0]);

    auto lateConfig = clientConfig(1u, 1u);
    lateConfig.prediction.maximumPredictedBodies = 2u;
    MultiplayerSession lateClient;
    ASSERT_TRUE(lateClient.initializeClient(
        lateConfig,
        std::make_unique<SimulatedEndpoint>(1u, network)));
    ASSERT_TRUE(server.sendSnapshots());
    EXPECT_GE(server.telemetry().interestOverflows, 1u);
    network->deliver(
        [](const SimulatedWireFrame& frame) {
            return frame.destination == 1u;
        });
    ASSERT_TRUE(lateClient.pump());
    EXPECT_TRUE(lateClient.clientReady());
    EXPECT_EQ(lateClient.currentTick(), 3u);
    expectClientMatches(server, lateClient, 1u);
}

TEST(MultiplayerSessionIntegration,
     GamePolicyExpandsARequestIntoServerOnlyCommands) {
    auto network = std::make_shared<DeterministicNetwork>();
    std::vector<LockstepBody> bodies(16u);
    bodies[1] = dynamicBody(1u, -4'096);
    bodies[2] = dynamicBody(2u, 4'096);

    auto config = serverConfig();
    config.commandExpander = [](
        const CanonicalNetworkCommand& request,
        std::span<const LockstepBody> world,
        std::vector<CanonicalNetworkCommand>& output) {
        if (request.type != NetworkCommandType::FireMeteorRequest)
            return false;
        uint32_t bodyId = 0u;
        for (uint32_t id = 3u; id < world.size(); ++id) {
            if ((world[id].identity[2] & LockstepBodyAlive) == 0u) {
                bodyId = id;
                break;
            }
        }
        if (bodyId == 0u) return false;
        CanonicalNetworkCommand spawn;
        spawn.type = NetworkCommandType::SpawnBody;
        spawn.body = bodyId;
        spawn.generation = world[bodyId].identity[1] + 1u;
        spawn.payload[3] =
            physics::deterministic::kLockstepPositionOne / 4;
        spawn.payload[5] =
            6 * physics::deterministic::kLockstepPositionOne;
        spawn.payload[7] =
            physics::deterministic::kLockstepVelocityOne;
        spawn.payload[8] = 8'192;
        spawn.payload[11] = static_cast<int32_t>(
            LockstepBodyAlive | LockstepBodyAwake);
        output.push_back(spawn);
        return true;
    };

    MultiplayerSession server;
    ASSERT_TRUE(server.initializeServer(
        std::move(config), bodies,
        std::make_unique<SimulatedEndpoint>(0u, network)));
    ASSERT_TRUE(server.addClient({1u, 1u, 4u}));
    ASSERT_TRUE(server.addClient({2u, 2u, 4u}));
    MultiplayerSession client;
    ASSERT_TRUE(client.initializeClient(
        clientConfig(1u, 1u),
        std::make_unique<SimulatedEndpoint>(1u, network)));
    ASSERT_TRUE(server.sendSnapshots());
    network->deliver(
        [](const SimulatedWireFrame& frame) {
            return frame.destination == 1u;
        });
    ASSERT_TRUE(client.pump());
    network->deliver();
    ASSERT_TRUE(server.pump());

    CanonicalNetworkCommand fire;
    fire.type = NetworkCommandType::FireMeteorRequest;
    ASSERT_TRUE(client.sendClientCommand(fire));
    network->deliver();
    ASSERT_TRUE(server.pump());
    ASSERT_TRUE(server.stepServer());

    ASSERT_LT(3u, server.bodies().size());
    EXPECT_NE(server.bodies()[3].identity[2] & LockstepBodyAlive, 0u);
    EXPECT_TRUE(std::any_of(
        server.recordedNetworkCommands().begin(),
        server.recordedNetworkCommands().end(),
        [](const CanonicalNetworkCommand& command) {
            return command.type == NetworkCommandType::SpawnBody
                && (command.flags & NetworkCommandServerIssued) != 0u;
        }));
}

TEST(MultiplayerSessionIntegration,
     SameTickConflictsUseAuthenticatedClientOrderNotArrivalOrder) {
    const auto run = [](bool reverseArrival) {
        auto network = std::make_shared<DeterministicNetwork>();
        std::vector<LockstepBody> bodies(16u);
        bodies[1] = dynamicBody(1u, -4'096);
        bodies[2] = dynamicBody(2u, 4'096);

        auto config = serverConfig();
        config.commandExpander = [](
            const CanonicalNetworkCommand& request,
            std::span<const LockstepBody> world,
            std::vector<CanonicalNetworkCommand>& output) {
            if (request.type != NetworkCommandType::FireMeteorRequest)
                return false;
            uint32_t bodyId = 0u;
            for (uint32_t id = 3u; id < world.size(); ++id) {
                if ((world[id].identity[2] & LockstepBodyAlive) == 0u) {
                    bodyId = id;
                    break;
                }
            }
            if (bodyId == 0u) return false;
            CanonicalNetworkCommand spawn;
            spawn.type = NetworkCommandType::SpawnBody;
            spawn.body = bodyId;
            spawn.generation = world[bodyId].identity[1] + 1u;
            spawn.payload[3] =
                physics::deterministic::kLockstepPositionOne / 4;
            spawn.payload[5] =
                6 * physics::deterministic::kLockstepPositionOne;
            spawn.payload[7] =
                physics::deterministic::kLockstepVelocityOne;
            spawn.payload[11] = static_cast<int32_t>(
                LockstepBodyAlive | LockstepBodyAwake);
            output.push_back(spawn);
            return true;
        };

        MultiplayerSession server;
        EXPECT_TRUE(server.initializeServer(
            config, bodies,
            std::make_unique<SimulatedEndpoint>(0u, network)));
        EXPECT_TRUE(server.addClient({1u, 1u, 4u}));
        EXPECT_TRUE(server.addClient({2u, 2u, 4u}));

        const auto submit = [&network, &config](
            uint32_t clientId, uint32_t bodyId,
            uint64_t clientSequence) {
            CanonicalNetworkCommand request;
            request.tick = 1u;
            request.sequence = clientSequence;
            request.islandId = config.islandId;
            request.authorityEpoch = config.authorityEpoch;
            request.clientId = clientId;
            request.type = NetworkCommandType::FireMeteorRequest;
            request.body = bodyId;
            request.generation = 1u;
            const std::array commands{request};

            Packet wirePacket;
            wirePacket.header.sessionId = config.identity.sessionId;
            wirePacket.header.worldId = config.identity.worldId;
            wirePacket.header.worldEpoch = config.identity.worldEpoch;
            wirePacket.header.authorityEpoch = config.authorityEpoch;
            wirePacket.header.sequence = 1u;
            wirePacket.header.tick = 1u;
            wirePacket.header.payloadType = PacketPayloadType::Command;
            wirePacket.payload = NetworkCommandCodec::encode(commands);
            const auto encoded = PacketCodec::encode(
                wirePacket, DeliveryClass::ReliableEvent);
            EXPECT_TRUE(encoded.error.empty());
            network->enqueue({
                .source = clientId,
                .destination = 0u,
                .delivery = DeliveryClass::ReliableEvent,
                .bytes = encoded.bytes,
            });
        };

        // Client 1 deliberately has the larger client-local sequence.
        submit(1u, 1u, 100u);
        submit(2u, 2u, 1u);
        network->deliver({}, {}, reverseArrival);
        EXPECT_TRUE(server.pump());
        EXPECT_TRUE(server.stepServer());

        std::vector<std::tuple<uint32_t, uint32_t, uint64_t>> result;
        for (const auto& command : server.recordedNetworkCommands()) {
            if (command.type == NetworkCommandType::SpawnBody) {
                result.emplace_back(
                    command.clientId, command.body, command.sequence);
            }
        }
        return result;
    };

    const auto forward = run(false);
    const auto reversed = run(true);
    ASSERT_EQ(forward.size(), 2u);
    ASSERT_EQ(reversed.size(), 2u);
    EXPECT_EQ(forward, reversed);
    EXPECT_EQ(forward[0], std::make_tuple(1u, 3u, uint64_t{1u}));
    EXPECT_EQ(forward[1], std::make_tuple(2u, 4u, uint64_t{2u}));
}

TEST(MultiplayerSessionLifecycle,
     MoveAssignmentClosesItsDestinationTransport) {
    std::vector<LockstepBody> bodies(16u);
    bodies[1] = dynamicBody(1u, 0);
    bodies[2] = dynamicBody(2u, 4'096);
    auto sourceProbe = std::make_shared<TransportProbe>();
    auto destinationProbe = std::make_shared<TransportProbe>();

    MultiplayerSession source;
    MultiplayerSession destination;
    ASSERT_TRUE(source.initializeServer(
        serverConfig(), bodies,
        std::make_unique<ProbedEndpoint>(sourceProbe)));
    ASSERT_TRUE(destination.initializeServer(
        serverConfig(), bodies,
        std::make_unique<ProbedEndpoint>(destinationProbe)));

    destination = std::move(source);
    EXPECT_EQ(destinationProbe->closeCalls, 1u);
    EXPECT_EQ(sourceProbe->closeCalls, 0u);
    EXPECT_EQ(source.role(), MultiplayerSessionRole::None);
    EXPECT_EQ(
        destination.role(),
        MultiplayerSessionRole::AuthoritativeServer);

    destination.shutdown();
    EXPECT_EQ(sourceProbe->closeCalls, 1u);
    EXPECT_EQ(destinationProbe->closeCalls, 1u);
}

TEST(MultiplayerSessionLifecycle,
     PumpServicesTransportExactlyOnceBeforeBoundedDrain) {
    std::vector<LockstepBody> bodies(16u);
    bodies[1] = dynamicBody(1u, 0);
    const auto probe = std::make_shared<TransportProbe>();
    MultiplayerSession server;
    ASSERT_TRUE(server.initializeServer(
        serverConfig(), bodies,
        std::make_unique<ProbedEndpoint>(probe)));

    EXPECT_TRUE(server.pump(1'000u, 256u));
    EXPECT_EQ(probe->serviceCalls, 1u);
    EXPECT_TRUE(server.pump(2'000u, 1u));
    EXPECT_EQ(probe->serviceCalls, 2u);
    EXPECT_FALSE(server.pump(3'000u, 0u));
    EXPECT_EQ(probe->serviceCalls, 2u);
}

TEST(MultiplayerSessionLifecycle,
     CommittedTicksSurviveSendFailureAndRecordingsStayBounded) {
    std::vector<LockstepBody> bodies(16u);
    bodies[1] = dynamicBody(1u, 0);
    bodies[2] = dynamicBody(2u, 4'096);

    auto failingProbe = std::make_shared<TransportProbe>();
    failingProbe->failSends = true;
    MultiplayerSession failingServer;
    ASSERT_TRUE(failingServer.initializeServer(
        serverConfig(), bodies,
        std::make_unique<ProbedEndpoint>(failingProbe)));
    ASSERT_TRUE(failingServer.addClient({1u, 1u, 4u}));
    EXPECT_TRUE(failingServer.stepServer());
    EXPECT_EQ(failingServer.currentTick(), 1u);
    EXPECT_GE(failingServer.telemetry().transportSendFailures, 1u);

    auto boundedConfig = serverConfig();
    boundedConfig.recordingCapacity = 2u;
    MultiplayerSession boundedServer;
    ASSERT_TRUE(boundedServer.initializeServer(
        boundedConfig, bodies,
        std::make_unique<ProbedEndpoint>(
            std::make_shared<TransportProbe>())));
    for (uint32_t tick = 1u; tick <= 3u; ++tick) {
        CanonicalNetworkCommand awake;
        awake.type = NetworkCommandType::SetAwake;
        awake.body = 1u;
        awake.generation = 1u;
        awake.payload[0] = 1;
        ASSERT_TRUE(boundedServer.queueServerCommand(awake));
        ASSERT_TRUE(boundedServer.stepServer());
    }
    ASSERT_EQ(boundedServer.recordedNetworkCommands().size(), 2u);
    ASSERT_EQ(boundedServer.recordedReplayCommands().size(), 2u);
    EXPECT_EQ(boundedServer.recordedNetworkCommands().front().tick, 2u);
    EXPECT_EQ(boundedServer.recordedReplayCommands().front().tick, 2u);
    EXPECT_EQ(boundedServer.telemetry().recordingEvictions, 2u);
}

TEST(MultiplayerSessionLifecycle,
     TransportLifecycleFramesRemainOutsidePacketDecoding) {
    MultiplayerSession client;
    ASSERT_TRUE(client.initializeClient(
        clientConfig(1u, 1u),
        std::make_unique<LifecycleEndpoint>()));
    EXPECT_TRUE(client.pump());
    EXPECT_EQ(client.telemetry().receivedPackets, 0u);
    EXPECT_EQ(client.telemetry().malformedPackets, 0u);
    EXPECT_EQ(client.telemetry().foreignPackets, 0u);
}

TEST(MultiplayerSessionSecurity,
     OversizedAndInvalidTrafficCannotPoisonValidCommandState) {
    auto network = std::make_shared<DeterministicNetwork>();
    std::vector<LockstepBody> bodies(16u);
    bodies[1] = dynamicBody(1u, 0);
    bodies[2] = dynamicBody(2u, 4'096);
    const auto config = serverConfig();

    MultiplayerSession server;
    ASSERT_TRUE(server.initializeServer(
        config, bodies,
        std::make_unique<SimulatedEndpoint>(0u, network)));
    ASSERT_TRUE(server.addClient({1u, 1u, 4u}));

    network->enqueue({
        .source = 1u,
        .destination = 0u,
        .delivery = DeliveryClass::ReliableControl,
        .bytes = std::vector<std::byte>(32u * 1024u + 1u),
    });
    network->deliver();
    EXPECT_FALSE(server.pump());
    EXPECT_EQ(server.telemetry().malformedPackets, 1u);

    const auto enqueueCommand =
        [&network, &config](uint64_t packetSequence,
                            uint64_t commandSequence) {
        CanonicalNetworkCommand command;
        command.tick = 1u;
        command.sequence = commandSequence;
        command.islandId = config.islandId;
        command.authorityEpoch = config.authorityEpoch;
        command.clientId = 1u;
        command.type = NetworkCommandType::SetAwakeRequest;
        command.body = 1u;
        command.generation = 1u;
        command.payload[0] = 1;
        const std::array commands{command};
        Packet packet;
        packet.header.sessionId = config.identity.sessionId;
        packet.header.worldId = config.identity.worldId;
        packet.header.worldEpoch = config.identity.worldEpoch;
        packet.header.authorityEpoch = config.authorityEpoch;
        packet.header.sequence = packetSequence;
        packet.header.tick = 1u;
        packet.header.payloadType = PacketPayloadType::Command;
        packet.payload = NetworkCommandCodec::encode(commands);
        const auto encoded =
            PacketCodec::encode(packet, DeliveryClass::ReliableEvent);
        EXPECT_TRUE(encoded.error.empty());
        network->enqueue({
            .source = 1u,
            .destination = 0u,
            .delivery = DeliveryClass::ReliableEvent,
            .bytes = encoded.bytes,
        });
    };
    enqueueCommand(1u, 0u);
    enqueueCommand(2u, 1u);
    network->deliver();
    EXPECT_FALSE(server.pump());
    EXPECT_EQ(server.telemetry().rejectedCommands, 1u);
    EXPECT_EQ(server.telemetry().acceptedCommands, 1u);
    ASSERT_TRUE(server.stepServer());
    EXPECT_EQ(server.recordedNetworkCommands().size(), 1u);
}

} // namespace
} // namespace voxy::network
