#include <gtest/gtest.h>

#include "network/deterministic_adversity_transport.hpp"
#include "network/native_tcp_transport.hpp"
#include "network/protocol.hpp"
#include "network/ridgebreak_client_replication.hpp"
#include "network/ridgebreak_protocol.hpp"
#include "server/ridgebreak_server_runtime.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace voxy::server {
namespace {

#if !defined(__EMSCRIPTEN__) \
    && (defined(__linux__) || defined(__APPLE__) || defined(__unix__))

class ScriptedRuntimeTransport final
    : public network::IMultiplayerTransport {
public:
    struct SentFrame {
        uint32_t peerId = 0u;
        uint64_t connectionSerial = 0u;
        std::vector<std::byte> bytes;
    };

    bool send(uint32_t peerId, network::DeliveryClass delivery,
              std::span<const std::byte> bytes) override {
        return send(peerId, 0u, delivery, bytes);
    }

    bool send(uint32_t peerId, uint64_t connectionSerial,
              network::DeliveryClass delivery,
              std::span<const std::byte> bytes) override {
        if (closed_ || delivery != network::DeliveryClass::Realtime)
            return false;
        if (rejectedSendsRemaining != 0u) {
            --rejectedSendsRemaining;
            return false;
        }
        sent.push_back({peerId, connectionSerial,
                        std::vector<std::byte>(bytes.begin(), bytes.end())});
        return true;
    }

    std::optional<network::MultiplayerTransportFrame> poll() override {
        if (inbound.empty()) return std::nullopt;
        auto frame = std::move(inbound.front());
        inbound.pop_front();
        return frame;
    }

    void close() override { closed_ = true; }

    void connect(uint32_t peerId, uint64_t serial) {
        inbound.push_back({
            .peerId = peerId,
            .delivery = network::DeliveryClass::ReliableControl,
            .bytes = {},
            .type = network::MultiplayerTransportFrameType::Connected,
            .connectionSerial = serial,
        });
    }

    std::deque<network::MultiplayerTransportFrame> inbound;
    std::vector<SentFrame> sent;
    uint32_t rejectedSendsRemaining = 0u;

private:
    bool closed_ = false;
};

network::NativeTcpAuthenticationKey key(uint32_t peerId) {
    network::NativeTcpAuthenticationKey result{};
    for (size_t i = 0u; i < result.size(); ++i) {
        result[i] = std::byte{static_cast<uint8_t>(
            1u + (peerId * 31u + static_cast<uint32_t>(i) * 17u) % 251u)};
    }
    return result;
}

network::NativeTcpContentDigest digest() {
    network::NativeTcpContentDigest result{};
    for (size_t i = 0u; i < result.size(); ++i)
        result[i] = std::byte{static_cast<uint8_t>(0x31u + i)};
    return result;
}

network::NativeTcpTransportLimits limits() {
    network::NativeTcpTransportLimits result;
    result.maximumPeers = 4u;
    result.maximumPendingConnections = 8u;
    result.maximumFrameBytes = network::kConservativeRealtimeMtu;
    result.maximumQueuedFramesPerPeer = 128u;
    result.maximumQueuedInboundFramesPerPeer = 128u;
    result.maximumAcceptsPerService = 8u;
    result.maximumFramesPerPeerPerService = 64u;
    result.maximumHandshakeServiceCalls = 20'000u;
    return result;
}

std::vector<std::byte> inputFrame(
    const RidgebreakAuthoritySession::Config& config,
    uint64_t connectionSerial, uint32_t generation,
    uint64_t packetSequence, uint64_t targetTick,
    uint16_t throttle, int16_t steer,
    uint64_t ackSequence = 0u, uint64_t ackBits = 0u) {
    network::RidgebreakInputBundle bundle;
    bundle.connectionSerial = connectionSerial;
    bundle.connectionGeneration = generation;
    bundle.sampleCount = 1u;
    bundle.samples[0].requestedTick = targetTick;
    bundle.samples[0].inputSequence = packetSequence;
    bundle.samples[0].throttleQ15 = throttle;
    bundle.samples[0].steerQ15 = steer;

    network::Packet packet;
    packet.header.payloadType = network::PacketPayloadType::Input;
    packet.header.sessionId = config.sessionId;
    packet.header.worldId = config.worldId;
    packet.header.worldEpoch = config.worldEpoch;
    packet.header.authorityEpoch = config.authorityEpoch;
    packet.header.sequence = packetSequence;
    packet.header.ackSequence = ackSequence;
    packet.header.ackBits = ackBits;
    packet.header.tick = targetTick;
    packet.payload = network::encodeRidgebreakInputBundle(bundle);
    auto encoded = network::PacketCodec::encode(
        packet, network::DeliveryClass::Realtime);
    EXPECT_TRUE(encoded.error.empty()) << encoded.error;
    return encoded.bytes;
}

std::unique_ptr<network::NativeTcpClientTransport> makeClient(
    uint16_t port, uint32_t peerId, std::string* error) {
    network::NativeTcpClientConfig clientConfig;
    clientConfig.serverAddress = "127.0.0.1";
    clientConfig.serverPort = port;
    clientConfig.credential = {
        .peerId = peerId,
        .authenticationKey = key(peerId),
    };
    clientConfig.expectedContentDigest = digest();
    clientConfig.limits = limits();
    clientConfig.limits.maximumPeers = 1u;
    clientConfig.limits.maximumPendingConnections = 1u;
    return network::NativeTcpClientTransport::create(
        std::move(clientConfig), error);
}

TEST(RidgebreakServerRuntime,
     FourRealSocketClientsConvergeThroughDeterministicAdversity) {
    network::NativeTcpServerConfig serverConfig;
    serverConfig.bindAddress = "127.0.0.1";
    serverConfig.port = 0u;
    serverConfig.limits = limits();
    serverConfig.expectedContentDigest = digest();
    for (uint32_t peerId = 1u; peerId <= 4u; ++peerId) {
        serverConfig.peers.push_back({
            .peerId = peerId,
            .authenticationKey = key(peerId),
        });
    }
    std::string error;
    auto nativeServer = network::NativeTcpServerTransport::create(
        std::move(serverConfig), &error);
    ASSERT_NE(nativeServer, nullptr) << error;
    const uint16_t port = nativeServer->listeningPort();

    network::DeterministicAdversityTransportConfig adversityConfig;
    adversityConfig.seed = 0x51d3b47a9c268ef0ull;
    adversityConfig.maximumPeers = 4u;
    adversityConfig.maximumQueuedDataFramesPerPeer = 128u;
    adversityConfig.maximumQueuedLifecycleFramesPerPeer = 4u;
    adversityConfig.maximumFrameBytes = network::kConservativeRealtimeMtu;
    adversityConfig.maximumUnderlyingFramesPerService = 128u;
    adversityConfig.maximumOutgoingFramesPerService = 128u;
    adversityConfig.incoming.minimumDelayServiceQuanta = 1u;
    adversityConfig.incoming.maximumDelayServiceQuanta = 4u;
    adversityConfig.incoming.realtimeDropPermille = 120u;
    adversityConfig.incoming.realtimeDuplicatePermille = 250u;
    adversityConfig.incoming.reorderPermille = 650u;
    adversityConfig.outgoing.minimumDelayServiceQuanta = 1u;
    adversityConfig.outgoing.maximumDelayServiceQuanta = 4u;
    adversityConfig.outgoing.realtimeDropPermille = 100u;
    adversityConfig.outgoing.realtimeDuplicatePermille = 200u;
    adversityConfig.outgoing.reorderPermille = 600u;
    auto adversity = network::DeterministicAdversityTransport::create(
        std::move(nativeServer), adversityConfig, &error);
    ASSERT_NE(adversity, nullptr) << error;
    auto* adversityView = adversity.get();

    RidgebreakServerRuntime runtime;
    RidgebreakServerRuntime::Config runtimeConfig;
    runtimeConfig.authority.inputFutureWindowTicks = 12u;
    runtimeConfig.authority.maximumPacketsPerPeerTick = 8u;
    ASSERT_TRUE(runtime.initialize(runtimeConfig, std::move(adversity)));

    std::array<std::unique_ptr<network::NativeTcpClientTransport>, 4u> clients;
    for (uint32_t peerId = 1u; peerId <= 4u; ++peerId) {
        clients[peerId - 1u] = makeClient(port, peerId, &error);
        ASSERT_NE(clients[peerId - 1u], nullptr) << error;
    }

    std::array<uint64_t, 4u> serials{};
    std::array<uint32_t, 4u> generations{};
    std::array<uint64_t, 4u> packetSequences{};
    std::array<uint64_t, 4u> snapshotsReceived{};
    std::array<network::RidgebreakClientSnapshotWindow, 4u> windows{};
    std::array<bool, 4u> windowInitialized{};
    struct AcceptedSnapshot {
        uint64_t packetSequence = 0u;
        network::RidgebreakSnapshot snapshot{};
    };
    std::array<std::vector<AcceptedSnapshot>, 4u> acceptedSnapshots{};
    uint64_t firstConnectionSerial = 0u;
    uint64_t replacementConnectionSerial = 0u;
    bool reconnectRequested = false;
    bool reconnectObserved = false;

    const auto drainClient = [&](uint32_t index) {
        clients[index]->service();
        while (auto frame = clients[index]->poll()) {
            if (frame->type
                == network::MultiplayerTransportFrameType::Connected) {
                if (index == 0u && firstConnectionSerial == 0u)
                    firstConnectionSerial = frame->connectionSerial;
                if (index == 0u && reconnectRequested
                    && frame->connectionSerial != firstConnectionSerial) {
                    replacementConnectionSerial = frame->connectionSerial;
                    reconnectObserved = true;
                }
                serials[index] = frame->connectionSerial;
                continue;
            }
            if (!frame->isData()) continue;
            EXPECT_EQ(frame->connectionSerial, serials[index]);
            if (!windowInitialized[index]) {
                const auto outer = network::PacketCodec::decode(
                    frame->bytes, frame->delivery);
                if (!outer.packet.has_value()
                    || outer.packet->header.payloadType
                        != network::PacketPayloadType::Snapshot) continue;
                const auto snapshot = network::decodeRidgebreakSnapshot(
                    outer.packet->payload);
                if (!snapshot.snapshot.has_value()) continue;
                const uint32_t playerId = index + 1u;
                for (uint32_t player = 0u;
                     player < snapshot.snapshot->playerCount; ++player) {
                    if (snapshot.snapshot->players[player].playerId
                        == playerId) {
                        generations[index] = snapshot.snapshot
                            ->players[player].connectionGeneration;
                    }
                }
                if (generations[index] == 0u) continue;
                ASSERT_TRUE(windows[index].reset({
                    .sessionId = runtimeConfig.authority.sessionId,
                    .worldId = runtimeConfig.authority.worldId,
                    .worldEpoch = runtimeConfig.authority.worldEpoch,
                    .authorityEpoch = runtimeConfig.authority.authorityEpoch,
                    .playerId = playerId,
                    .connectionSerial = serials[index],
                    .connectionGeneration = generations[index],
                }));
                windowInitialized[index] = true;
            }
            auto accepted = windows[index].accept(
                frame->connectionSerial, frame->delivery, frame->bytes);
            if (!accepted) continue;
            ++snapshotsReceived[index];
            acceptedSnapshots[index].push_back({
                .packetSequence = accepted.packetSequence,
                .snapshot = *accepted.snapshot,
            });
        }
    };

    for (uint32_t quantum = 0u; quantum < 3'000u; ++quantum) {
        for (auto& client : clients) client->service();
        (void)runtime.tickOnce();
        ASSERT_FALSE(runtime.faulted())
            << static_cast<uint32_t>(runtime.fault());
        for (uint32_t index = 0u; index < 4u; ++index) {
            drainClient(index);
            if (serials[index] == 0u
                || generations[index] == 0u) continue;
            const uint64_t nextSequence = packetSequences[index] + 1u;
            const uint64_t targetTick = runtime.authority().tick() + 10u;
            const auto bytes = inputFrame(
                runtimeConfig.authority, serials[index], generations[index],
                nextSequence, targetTick,
                static_cast<uint16_t>(20'000u + index * 2'000u),
                index % 2u == 0u ? int16_t{2'000} : int16_t{-2'000},
                windowInitialized[index] ? windows[index].latestSequence() : 0u,
                windowInitialized[index]
                    ? windows[index].acknowledgementBits() : 0u);
            if (clients[index]->send(
                    0u, serials[index], network::DeliveryClass::Realtime,
                    bytes)) {
                packetSequences[index] = nextSequence;
            }
        }
        if (quantum == 1'400u && firstConnectionSerial != 0u) {
            reconnectRequested = true;
            clients[0]->close();
            clients[0] = makeClient(port, 1u, &error);
            ASSERT_NE(clients[0], nullptr) << error;
            serials[0] = 0u;
            generations[0] = 0u;
            packetSequences[0] = 0u;
            windows[0] = {};
            windowInitialized[0] = false;
        }
        std::this_thread::yield();
    }

    // Let the normal tick path recover its one bounded in-flight broadcast.
    // No test-only retransmission call is used.
    bool convergedWithoutManualRecovery = false;
    for (uint32_t recovery = 0u; recovery < 1'000u; ++recovery) {
        for (auto& client : clients) client->service();
        (void)runtime.tickOnce();
        ASSERT_FALSE(runtime.faulted());
        for (uint32_t index = 0u; index < 4u; ++index) {
            drainClient(index);
            if (serials[index] == 0u || generations[index] == 0u) continue;
            const uint64_t nextSequence = packetSequences[index] + 1u;
            const auto bytes = inputFrame(
                runtimeConfig.authority, serials[index], generations[index],
                nextSequence, runtime.authority().tick() + 10u,
                static_cast<uint16_t>(20'000u + index * 2'000u),
                index % 2u == 0u ? int16_t{2'000} : int16_t{-2'000},
                windowInitialized[index] ? windows[index].latestSequence() : 0u,
                windowInitialized[index]
                    ? windows[index].acknowledgementBits() : 0u);
            if (clients[index]->send(
                    0u, serials[index], network::DeliveryClass::Realtime,
                    bytes)) {
                packetSequences[index] = nextSequence;
            }
        }
        if (runtime.pendingSnapshotPeers() == 0u
            && runtime.lastBroadcastSnapshot().has_value()
            && std::all_of(
                acceptedSnapshots.begin(), acceptedSnapshots.end(),
                [&](const auto& snapshots) {
                    return !snapshots.empty()
                        && snapshots.back().snapshot
                            == *runtime.lastBroadcastSnapshot();
                })) {
            convergedWithoutManualRecovery = true;
            break;
        }
        std::this_thread::yield();
    }

    ASSERT_FALSE(runtime.faulted());
    EXPECT_GE(runtime.authority().tick(), 3'000u);
    EXPECT_TRUE(convergedWithoutManualRecovery);
    ASSERT_TRUE(reconnectRequested);
    ASSERT_TRUE(reconnectObserved);
    EXPECT_NE(firstConnectionSerial, replacementConnectionSerial);
    const auto authorityState = runtime.authority().canonicalSnapshot();
    ASSERT_EQ(authorityState.playerCount, 4u);
    ASSERT_TRUE(runtime.lastBroadcastSnapshot().has_value());
    const auto& finalBroadcast = *runtime.lastBroadcastSnapshot();
    EXPECT_LE(finalBroadcast.authoritativeTick, runtime.authority().tick());
    ASSERT_EQ(finalBroadcast.playerCount, 4u);
    for (uint32_t index = 0u; index < 4u; ++index) {
        EXPECT_GT(snapshotsReceived[index], 0u);
        EXPECT_TRUE(windowInitialized[index]);
        EXPECT_GT(windows[index].latestSequence(), 0u);
        ASSERT_FALSE(acceptedSnapshots[index].empty());
        EXPECT_EQ(acceptedSnapshots[index].back().packetSequence,
                  windows[index].latestSequence());
        EXPECT_EQ(acceptedSnapshots[index].back().snapshot, finalBroadcast);
        EXPECT_GT(authorityState.players[index].lastProcessedInputSequence, 0u);
        EXPECT_GT(
            authorityState.players[index].forwardSpeedMillimetersPerSecond, 0);
    }
    EXPECT_EQ(finalBroadcast.players[0].connectionSerial,
              replacementConnectionSerial);
    EXPECT_EQ(finalBroadcast.players[0].connectionGeneration, 2u);
    EXPECT_GT(runtime.telemetry().inputFramesAccepted, 0u);
    EXPECT_GT(runtime.telemetry().snapshotsSent, 0u);
    EXPECT_GT(runtime.telemetry().snapshotRetransmissionsSent, 0u);
    EXPECT_GT(adversityView->telemetry().inboundRealtimeDrops, 0u);
    EXPECT_GT(adversityView->telemetry().inboundDuplicatesQueued, 0u);
    EXPECT_GT(adversityView->telemetry().inboundReorders, 0u);
    EXPECT_GT(adversityView->telemetry().outboundRealtimeDrops, 0u);
    EXPECT_GT(adversityView->telemetry().outboundReorders, 0u);
    EXPECT_GT(adversityView->telemetry().staleFramesPurged, 0u);

    runtime.close();
    for (auto& client : clients) client->close();
}

TEST(RidgebreakServerRuntime,
     RealSocketsSurviveThreeHundredPeerOneReconnectsAndAllRejoin) {
    network::NativeTcpServerConfig serverConfig;
    serverConfig.bindAddress = "127.0.0.1";
    serverConfig.port = 0u;
    serverConfig.limits = limits();
    serverConfig.expectedContentDigest = digest();
    for (uint32_t peerId = 1u; peerId <= 4u; ++peerId) {
        serverConfig.peers.push_back({
            .peerId = peerId,
            .authenticationKey = key(peerId),
        });
    }
    std::string error;
    auto nativeServer = network::NativeTcpServerTransport::create(
        std::move(serverConfig), &error);
    ASSERT_NE(nativeServer, nullptr) << error;
    auto* serverView = nativeServer.get();
    const uint16_t port = nativeServer->listeningPort();

    RidgebreakServerRuntime runtime;
    RidgebreakServerRuntime::Config runtimeConfig;
    runtimeConfig.snapshotRetryIntervalTicks = 1u;
    runtimeConfig.maximumSnapshotSendAttempts = 2u;
    ASSERT_TRUE(runtime.initialize(runtimeConfig, std::move(nativeServer)));

    std::array<std::unique_ptr<network::NativeTcpClientTransport>, 4u> clients;
    for (uint32_t peerId = 1u; peerId <= 4u; ++peerId) {
        clients[peerId - 1u] = makeClient(port, peerId, &error);
        ASSERT_NE(clients[peerId - 1u], nullptr) << error;
    }
    const auto discardClientFrames = [&]() {
        for (auto& client : clients) {
            if (!client) continue;
            while (client->poll().has_value()) {}
        }
    };
    const auto pumpUntil = [&](const auto& predicate,
                               uint32_t attempts = 20'000u) {
        for (uint32_t attempt = 0u; attempt < attempts; ++attempt) {
            for (auto& client : clients) {
                if (client) client->service();
            }
            (void)runtime.tickOnce();
            if (predicate()) return true;
            discardClientFrames();
            std::this_thread::yield();
        }
        return false;
    };
    ASSERT_TRUE(pumpUntil([&]() {
        for (uint32_t peer = 1u; peer <= 4u; ++peer) {
            if (!clients[peer - 1u]->connected()
                || !serverView->connected(peer)
                || !runtime.authority().peerView(peer)->active) return false;
        }
        return true;
    }));
    discardClientFrames();

    uint64_t priorPeerOneSerial = serverView->connectionSerial(1u);
    for (uint32_t reconnect = 0u; reconnect < 300u; ++reconnect) {
        auto oldClient = std::move(clients[0]);
        clients[0] = makeClient(port, 1u, &error);
        ASSERT_NE(clients[0], nullptr) << reconnect << ": " << error;
        ASSERT_TRUE(pumpUntil([&]() {
            const auto peer = runtime.authority().peerView(1u);
            return clients[0]->connected() && serverView->connected(1u)
                && peer.has_value() && peer->active
                && peer->connectionSerial == serverView->connectionSerial(1u)
                && peer->connectionSerial != priorPeerOneSerial;
        })) << reconnect;
        const uint64_t replacementSerial = serverView->connectionSerial(1u);
        for (uint32_t service = 0u;
             service < 100u && oldClient->connected(); ++service) {
            oldClient->service();
            std::this_thread::yield();
        }
        EXPECT_FALSE(oldClient->connected()) << reconnect;
        const std::array<std::byte, 1u> stalePayload{std::byte{0x5a}};
        EXPECT_FALSE(oldClient->send(
            0u, priorPeerOneSerial, network::DeliveryClass::Realtime,
            stalePayload)) << reconnect;
        oldClient->close();
        priorPeerOneSerial = replacementSerial;
        discardClientFrames();
    }
    EXPECT_EQ(runtime.telemetry().registrationRejections, 0u);

    // The noisy rider did not consume any other rider's bounded fence.
    for (uint32_t peer = 2u; peer <= 4u; ++peer) {
        const uint64_t oldSerial = serverView->connectionSerial(peer);
        clients[peer - 1u]->close();
        clients[peer - 1u] = makeClient(port, peer, &error);
        ASSERT_NE(clients[peer - 1u], nullptr) << error;
        ASSERT_TRUE(pumpUntil([&]() {
            const auto view = runtime.authority().peerView(peer);
            return clients[peer - 1u]->connected()
                && serverView->connected(peer)
                && view.has_value() && view->active
                && view->connectionSerial == serverView->connectionSerial(peer)
                && view->connectionSerial != oldSerial;
        })) << peer;
    }
    discardClientFrames();

    const uint64_t acceptedBefore = runtime.telemetry().inputFramesAccepted;
    for (uint32_t index = 0u; index < 4u; ++index) {
        const auto peer = runtime.authority().peerView(index + 1u);
        ASSERT_TRUE(peer.has_value());
        const auto bytes = inputFrame(
            runtimeConfig.authority, peer->connectionSerial,
            peer->connectionGeneration, 1u,
            runtime.authority().tick() + 4u, 24'000u, 0);
        ASSERT_TRUE(clients[index]->send(
            0u, peer->connectionSerial,
            network::DeliveryClass::Realtime, bytes));
    }
    ASSERT_TRUE(pumpUntil([&]() {
        return runtime.telemetry().inputFramesAccepted
            >= acceptedBefore + 4u;
    }));

    std::array<network::RidgebreakClientSnapshotWindow, 4u> windows;
    std::array<uint64_t, 4u> acceptedSnapshotSequences{};
    for (uint32_t index = 0u; index < 4u; ++index) {
        const auto peer = runtime.authority().peerView(index + 1u);
        ASSERT_TRUE(peer.has_value());
        ASSERT_TRUE(windows[index].reset({
            .sessionId = runtimeConfig.authority.sessionId,
            .worldId = runtimeConfig.authority.worldId,
            .worldEpoch = runtimeConfig.authority.worldEpoch,
            .authorityEpoch = runtimeConfig.authority.authorityEpoch,
            .playerId = index + 1u,
            .connectionSerial = peer->connectionSerial,
            .connectionGeneration = peer->connectionGeneration,
        }));
    }
    const auto receiveFreshSnapshots = [&]() {
        bool complete = true;
        for (uint32_t index = 0u; index < 4u; ++index) {
            clients[index]->service();
            while (auto frame = clients[index]->poll()) {
                if (!frame->isData()) continue;
                auto accepted = windows[index].accept(
                    frame->connectionSerial, frame->delivery, frame->bytes);
                if (accepted)
                    acceptedSnapshotSequences[index] = accepted.packetSequence;
            }
            complete = complete && acceptedSnapshotSequences[index] != 0u;
        }
        return complete;
    };
    for (uint32_t attempt = 0u;
         attempt < 20'000u && !receiveFreshSnapshots(); ++attempt) {
        (void)runtime.tickOnce();
        std::this_thread::yield();
    }
    for (uint64_t sequence : acceptedSnapshotSequences)
        ASSERT_NE(sequence, 0u);

    const uint64_t acknowledgementsBefore =
        runtime.telemetry().inputFramesAccepted;
    for (uint32_t index = 0u; index < 4u; ++index) {
        const auto peer = runtime.authority().peerView(index + 1u);
        const auto bytes = inputFrame(
            runtimeConfig.authority, peer->connectionSerial,
            peer->connectionGeneration, 2u,
            runtime.authority().tick() + 4u, 24'000u, 0,
            acceptedSnapshotSequences[index],
            windows[index].acknowledgementBits());
        ASSERT_TRUE(clients[index]->send(
            0u, peer->connectionSerial,
            network::DeliveryClass::Realtime, bytes));
    }
    ASSERT_TRUE(pumpUntil([&]() {
        if (runtime.telemetry().inputFramesAccepted
            < acknowledgementsBefore + 4u) return false;
        for (uint32_t index = 0u; index < 4u; ++index) {
            const auto peer = runtime.authority().peerView(index + 1u);
            if (!peer.has_value()
                || peer->latestAcknowledgedSnapshotSequence
                    < acceptedSnapshotSequences[index]) return false;
        }
        return true;
    }));
    EXPECT_FALSE(runtime.faulted());
    EXPECT_EQ(runtime.telemetry().registrationRejections, 0u);
    for (uint32_t peer = 1u; peer <= 4u; ++peer) {
        const auto authorityPeer = runtime.authority().peerView(peer);
        ASSERT_TRUE(authorityPeer.has_value());
        EXPECT_TRUE(authorityPeer->active);
        EXPECT_EQ(authorityPeer->connectionSerial,
                  serverView->connectionSerial(peer));
    }

    runtime.close();
    for (auto& client : clients) client->close();
}

TEST(RidgebreakServerRuntime,
     ReconnectChurnCannotConsumeAnotherRidersFence) {
    auto transport = std::make_unique<ScriptedRuntimeTransport>();
    auto* view = transport.get();
    RidgebreakServerRuntime runtime;
    RidgebreakServerRuntime::Config config;
    config.snapshotRetryIntervalTicks = 1u;
    config.maximumSnapshotSendAttempts = 2u;
    ASSERT_TRUE(runtime.initialize(config, std::move(transport)));

    for (uint32_t peer = 1u; peer <= 4u; ++peer)
        view->connect(peer, 100u + peer);
    (void)runtime.tickOnce();
    ASSERT_FALSE(runtime.faulted());

    for (uint32_t reconnect = 0u; reconnect < 300u; ++reconnect) {
        view->connect(1u, 1'000u + reconnect);
        (void)runtime.tickOnce();
        ASSERT_FALSE(runtime.faulted()) << reconnect;
    }

    EXPECT_EQ(runtime.telemetry().registrationRejections, 0u);
    EXPECT_GT(runtime.telemetry().snapshotsSent, 0u);
    EXPECT_EQ(runtime.authority().tick(), 301u);
    for (uint32_t peer = 2u; peer <= 4u; ++peer) {
        const auto healthy = runtime.authority().peerView(peer);
        ASSERT_TRUE(healthy.has_value());
        EXPECT_TRUE(healthy->active);
    }

    const auto riderBeforeReplay = runtime.authority().peerView(1u);
    ASSERT_TRUE(riderBeforeReplay.has_value());
    ASSERT_TRUE(riderBeforeReplay->active);
    view->connect(1u, 102u); // A serial already owned by peer two.
    (void)runtime.tickOnce();
    ASSERT_FALSE(runtime.faulted());
    const auto riderAfterReplay = runtime.authority().peerView(1u);
    ASSERT_TRUE(riderAfterReplay.has_value());
    EXPECT_TRUE(riderAfterReplay->active);
    EXPECT_EQ(riderAfterReplay->connectionSerial,
              riderBeforeReplay->connectionSerial);
    for (uint32_t peer = 2u; peer <= 4u; ++peer)
        EXPECT_TRUE(runtime.authority().peerView(peer)->active);
}

TEST(RidgebreakServerRuntime,
     AutomaticallyRetriesLatestSnapshotAfterBoundedBackpressure) {
    auto transport = std::make_unique<ScriptedRuntimeTransport>();
    auto* view = transport.get();
    view->rejectedSendsRemaining = 1u;
    RidgebreakServerRuntime runtime;
    RidgebreakServerRuntime::Config config;
    config.snapshotRetryIntervalTicks = 1u;
    config.maximumSnapshotSendAttempts = 4u;
    ASSERT_TRUE(runtime.initialize(config, std::move(transport)));
    view->connect(1u, 101u);

    for (uint32_t tick = 0u; tick < 4u; ++tick)
        (void)runtime.tickOnce();

    ASSERT_FALSE(runtime.faulted());
    EXPECT_EQ(runtime.telemetry().snapshotSendFailures, 1u);
    EXPECT_EQ(runtime.telemetry().snapshotRetransmissionsSent, 1u);
    EXPECT_EQ(runtime.pendingSnapshotPeers(), 1u);
    ASSERT_EQ(view->sent.size(), 1u);
    const auto outer = network::PacketCodec::decode(
        view->sent.front().bytes, network::DeliveryClass::Realtime);
    ASSERT_TRUE(outer.packet.has_value());
    EXPECT_EQ(outer.packet->header.payloadType,
              network::PacketPayloadType::Snapshot);
    EXPECT_EQ(outer.packet->header.tick, 3u);
}

TEST(RidgebreakServerRuntime,
     ExhaustedRetryBudgetSupersedesWithNewestSnapshot) {
    auto transport = std::make_unique<ScriptedRuntimeTransport>();
    auto* view = transport.get();
    view->rejectedSendsRemaining = 100u;
    RidgebreakServerRuntime runtime;
    RidgebreakServerRuntime::Config config;
    config.snapshotRetryIntervalTicks = 1u;
    config.maximumSnapshotSendAttempts = 2u;
    ASSERT_TRUE(runtime.initialize(config, std::move(transport)));
    view->connect(1u, 101u);

    for (uint32_t tick = 0u; tick < 6u; ++tick)
        (void)runtime.tickOnce();

    ASSERT_FALSE(runtime.faulted());
    EXPECT_EQ(runtime.telemetry().snapshotSendFailures, 3u);
    EXPECT_EQ(runtime.telemetry().snapshotRetransmissionsSent, 0u);
    EXPECT_EQ(runtime.telemetry().snapshotSupersessions, 1u);
    EXPECT_EQ(runtime.pendingSnapshotPeers(), 1u);
    ASSERT_TRUE(runtime.lastBroadcastSnapshot().has_value());
    EXPECT_EQ(runtime.lastBroadcastSnapshot()->authoritativeTick, 6u);
    EXPECT_TRUE(view->sent.empty());
}

#else

TEST(RidgebreakServerRuntime, NativeLoopbackUnavailable) {
    GTEST_SKIP() << "POSIX TCP loopback is unavailable";
}

#endif

} // namespace
} // namespace voxy::server
