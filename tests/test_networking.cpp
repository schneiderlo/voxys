#include <gtest/gtest.h>

#include "network/authoritative_sandbox.hpp"
#include "network/protocol.hpp"
#include "network/replication.hpp"
#include "network/transport.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <limits>
#include <memory>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace voxy::network {
namespace {

using physics::deterministic::CanonicalReplayCommand;
using physics::deterministic::LockstepBody;
using physics::deterministic::LockstepBodyAlive;
using physics::deterministic::LockstepBodyAwake;
using physics::deterministic::ReplayCommandType;

LockstepBody body(uint32_t id, int32_t xQ12, int32_t yQ12) {
    LockstepBody result;
    result.identity = {id, 1u, LockstepBodyAlive | LockstepBodyAwake, 0u};
    result.sectorRadius = {
        0, 0, 0, physics::deterministic::kLockstepPositionOne / 2};
    result.positionInvMass = {
        xQ12, yQ12, 0, physics::deterministic::kLockstepVelocityOne};
    return result;
}

CanonicalReplayCommand impulse(
    uint64_t tick, uint32_t bodyId, uint32_t generation, int32_t xQ16) {
    CanonicalReplayCommand command;
    command.tick = tick;
    command.sequence = tick;
    command.producer = bodyId;
    command.type = ReplayCommandType::ApplyImpulse;
    command.body = bodyId;
    command.generation = generation;
    command.payload[0] = xQ16;
    return command;
}

AuthoritativeSnapshot snapshotFrom(
    const physics::deterministic::LockstepWorld& world,
    uint64_t tick, uint64_t islandId = 1u, uint32_t epoch = 1u) {
    AuthoritativeSnapshot snapshot;
    snapshot.tick = tick;
    snapshot.islandId = islandId;
    snapshot.authorityEpoch = epoch;
    snapshot.full = true;
    for (const auto& value : world.bodies()) {
        if ((value.identity[2] & LockstepBodyAlive) != 0u)
            snapshot.bodies.push_back(value);
    }
    snapshot.stateHash = snapshotStateHash(snapshot);
    return snapshot;
}

std::vector<LockstepBody> expand(
    const AuthoritativeSnapshot& snapshot, uint32_t capacity) {
    std::vector<LockstepBody> result(capacity);
    for (const auto& value : snapshot.bodies)
        result[value.identity[0]] = value;
    return result;
}

TEST(NetworkProtocol, CanonicalCommandsPacketsAndAcknowledgements) {
    CanonicalNetworkCommand move;
    move.tick = 7;
    move.sequence = 9;
    move.islandId = 11;
    move.authorityEpoch = 3;
    move.clientId = 2;
    move.type = NetworkCommandType::MoveInput;
    move.body = 4;
    move.generation = 5;
    move.payload[0] = 1'024;
    CanonicalNetworkCommand fire = move;
    fire.sequence = 2;
    fire.type = NetworkCommandType::FireMeteorRequest;

    const std::array commands{move, fire};
    const auto commandBytes = NetworkCommandCodec::encode(commands);
    const auto decodedCommands = NetworkCommandCodec::decode(commandBytes);
    ASSERT_TRUE(decodedCommands.commands.has_value()) << decodedCommands.error;
    ASSERT_EQ(decodedCommands.commands->size(), 2u);
    EXPECT_EQ((*decodedCommands.commands)[0].type,
              NetworkCommandType::MoveInput);
    EXPECT_EQ((*decodedCommands.commands)[1].type,
              NetworkCommandType::FireMeteorRequest);
    EXPECT_TRUE(isClientCommandAllowed(NetworkCommandType::MoveInput));
    EXPECT_FALSE(isClientCommandAllowed(NetworkCommandType::Correction));
    const auto replay = toReplayCommand(move);
    ASSERT_TRUE(replay.has_value());
    EXPECT_EQ(replay->type, ReplayCommandType::ApplyImpulse);
    auto invalidCommand = move;
    invalidCommand.type = static_cast<NetworkCommandType>(999u);
    EXPECT_FALSE(toReplayCommand(invalidCommand).has_value());
    EXPECT_TRUE(NetworkCommandCodec::encode(
        std::span<const CanonicalNetworkCommand>(&invalidCommand, 1u)).empty());
    auto invalidFlags = commandBytes;
    ASSERT_GT(invalidFlags.size(), 48u);
    invalidFlags[48] |= std::byte{0x80};
    EXPECT_FALSE(NetworkCommandCodec::decode(invalidFlags)
                     .commands.has_value());

    Packet packet;
    packet.header.payloadType = PacketPayloadType::Command;
    packet.header.sessionId = 10;
    packet.header.worldId = 20;
    packet.header.worldEpoch = 2;
    packet.header.authorityEpoch = 3;
    packet.header.sequence = 100;
    packet.header.ackSequence = 97;
    packet.header.ackBits = 0b1011;
    packet.header.tick = 7;
    packet.payload = commandBytes;
    const auto encoded = PacketCodec::encode(packet, DeliveryClass::Realtime);
    ASSERT_TRUE(encoded.error.empty()) << encoded.error;
    ASSERT_FALSE(encoded.bytes.empty());
    const auto decoded = PacketCodec::decode(
        encoded.bytes, DeliveryClass::Realtime);
    ASSERT_TRUE(decoded.packet.has_value()) << decoded.error;
    EXPECT_EQ(decoded.packet->header.sequence, 100u);
    EXPECT_EQ(decoded.packet->payload, commandBytes);

    auto corrupt = encoded.bytes;
    corrupt[20] ^= std::byte{1};
    EXPECT_EQ(PacketCodec::decode(corrupt, DeliveryClass::Realtime).error,
              "network packet checksum mismatch");
    packet.payload.resize(kConservativeRealtimeMtu);
    EXPECT_FALSE(PacketCodec::encode(packet, DeliveryClass::Realtime)
                     .error.empty());
    const auto invalidDelivery = static_cast<DeliveryClass>(99u);
    EXPECT_FALSE(PacketCodec::encode(packet, invalidDelivery).error.empty());
    EXPECT_FALSE(PacketCodec::decode(encoded.bytes, invalidDelivery)
                     .error.empty());
    packet.payload.clear();
    packet.header.protocolVersion = kNetworkProtocolVersion + 1u;
    EXPECT_FALSE(PacketCodec::encode(packet, DeliveryClass::ReliableControl)
                     .error.empty());

    AckWindow acknowledgements;
    EXPECT_TRUE(acknowledgements.observe(10));
    EXPECT_TRUE(acknowledgements.observe(8));
    EXPECT_FALSE(acknowledgements.observe(8));
    EXPECT_TRUE(acknowledgements.acknowledges(10));
    EXPECT_TRUE(acknowledgements.acknowledges(8));
    EXPECT_TRUE(acknowledgements.observe(75));
    EXPECT_FALSE(acknowledgements.acknowledges(10));
}

TEST(NetworkTransport, WebTransportFailsOverToThreeDataChannels) {
    bool primaryConnected = false;
    bool primaryClosed = false;
    std::vector<uint32_t> fallbackChannels;
    std::deque<TransportFrame> incoming;
    incoming.push_back({DeliveryClass::ReliableControl,
                        std::vector<std::byte>{std::byte{7}}});

    TransportCallbacks primary;
    primary.available = [] { return true; };
    primary.connect = [&primaryConnected](std::string_view url) {
        primaryConnected = url == "https://example.invalid/session";
        return primaryConnected;
    };
    primary.send = [](uint32_t, std::span<const std::byte>) { return false; };
    primary.close = [&primaryClosed] { primaryClosed = true; };

    TransportCallbacks fallback;
    fallback.available = [] { return true; };
    fallback.connect = [](std::string_view) { return true; };
    fallback.send = [&fallbackChannels](
        uint32_t channel, std::span<const std::byte>) {
        fallbackChannels.push_back(channel);
        return true;
    };
    fallback.poll = [&incoming]() -> std::optional<TransportFrame> {
        if (incoming.empty()) return std::nullopt;
        auto frame = std::move(incoming.front());
        incoming.pop_front();
        return frame;
    };
    fallback.close = [] {};

    RealtimeGateway gateway(
        std::make_unique<WebTransportEndpoint>(std::move(primary)),
        std::make_unique<WebRtcDataChannelEndpoint>(std::move(fallback)));
    ASSERT_TRUE(gateway.connect("https://example.invalid/session"));
    EXPECT_EQ(gateway.activeKind(), TransportKind::WebTransport);
    const std::array payload{std::byte{1}, std::byte{2}};
    EXPECT_TRUE(gateway.send(DeliveryClass::Realtime, payload));
    EXPECT_TRUE(primaryConnected);
    EXPECT_TRUE(primaryClosed);
    EXPECT_EQ(gateway.activeKind(), TransportKind::WebRtcDataChannel);
    EXPECT_TRUE(gateway.send(DeliveryClass::ReliableEvent, payload));
    EXPECT_TRUE(gateway.send(DeliveryClass::ReliableControl, payload));
    EXPECT_EQ(fallbackChannels, (std::vector<uint32_t>{0u, 1u, 2u}));
    ASSERT_TRUE(gateway.poll().has_value());
    incoming.push_back({
        static_cast<DeliveryClass>(99u),
        std::vector<std::byte>{std::byte{9}},
    });
    EXPECT_FALSE(gateway.poll().has_value());
    EXPECT_FALSE(gateway.send(static_cast<DeliveryClass>(99u), payload));
    EXPECT_EQ(fallbackChannels, (std::vector<uint32_t>{0u, 1u, 2u}));
    EXPECT_EQ(gateway.telemetry().failovers, 1u);
    EXPECT_EQ(gateway.telemetry().receivedFrames, 1u);
    std::vector<std::byte> tooLarge(kConservativeRealtimeMtu + 1u);
    EXPECT_FALSE(gateway.send(DeliveryClass::Realtime, tooLarge));
}

TEST(NetworkReplication, InputTickDeltaInterestAndAuthorityContracts) {
    InputRedundancyBuffer sender;
    for (uint64_t tick = 1; tick <= 5; ++tick) {
        EXPECT_TRUE(sender.push(InputFrame{
            .tick = tick,
            .sequence = 100u + tick,
            .buttons = 0u,
            .moveXQ16 = static_cast<int32_t>(tick),
        }));
    }
    const auto bundle = sender.bundle();
    ASSERT_EQ(bundle.size(), kInputRedundancyFrames);
    EXPECT_EQ(bundle.front().tick, 2u);
    EXPECT_EQ(bundle.back().tick, 5u);
    const auto decodedInputs = InputRedundancyBuffer::decode(
        InputRedundancyBuffer::encode(bundle));
    ASSERT_TRUE(decodedInputs.frames.has_value()) << decodedInputs.error;
    InputRedundancyBuffer receiver;
    EXPECT_EQ(receiver.ingest(*decodedInputs.frames).size(), bundle.size());
    EXPECT_TRUE(receiver.ingest(*decodedInputs.frames).empty());
    std::array<InputFrame, kMaximumInputFramesPerBundle + 1u> oversizedInputs{};
    EXPECT_TRUE(receiver.ingest(oversizedInputs).empty());
    sender.acknowledge(3);
    EXPECT_EQ(sender.acknowledgedTick(), 3u);

    TickSynchronizer synchronizer;
    EXPECT_TRUE(synchronizer.observe({
        .clientSendMicros = 1'000,
        .clientReceiveMicros = 41'000,
        .localReceiveTick = 100,
        .serverTick = 110,
    }));
    EXPECT_EQ(synchronizer.minimumRttMicros(), 40'000u);
    EXPECT_EQ(synchronizer.offsetTicks(), 11);
    EXPECT_EQ(synchronizer.estimatedServerTick(120), 131u);
    EXPECT_EQ(synchronizer.recommendedInputTick(120), 133u);
    TickSynchronizer zeroRtt;
    ASSERT_TRUE(zeroRtt.observe({
        .clientSendMicros = 1'000,
        .clientReceiveMicros = 1'000,
        .localReceiveTick = 10,
        .serverTick = 15,
    }));
    EXPECT_EQ(zeroRtt.minimumRttMicros(), 0u);
    EXPECT_EQ(zeroRtt.offsetTicks(), 5);
    ASSERT_TRUE(zeroRtt.observe({
        .clientSendMicros = 2'000,
        .clientReceiveMicros = 102'000,
        .localReceiveTick = 10,
        .serverTick = 1'000,
    }));
    EXPECT_EQ(zeroRtt.minimumRttMicros(), 0u);
    EXPECT_EQ(zeroRtt.offsetTicks(), 5);

    AuthoritativeSnapshot first;
    first.tick = 1;
    first.islandId = 9;
    first.authorityEpoch = 2;
    first.bodies = {body(1u, -4'096, 8'192), body(2u, 4'096, 8'192)};
    first.stateHash = snapshotStateHash(first);
    SnapshotHistory history;
    ASSERT_TRUE(history.store(first));
    AuthoritativeSnapshot second = first;
    second.tick = 2;
    second.bodies[0].positionInvMass[0] += 64;
    second.bodies.pop_back();
    second.stateHash = snapshotStateHash(second);
    const auto delta = history.deltaFrom(second, 1u);
    ASSERT_TRUE(delta.has_value());
    EXPECT_FALSE(delta->full);
    ASSERT_EQ(delta->bodies.size(), 1u);
    EXPECT_EQ(delta->removedBodyIds, (std::vector<uint32_t>{2u}));
    const auto decodedDelta = SnapshotCodec::decode(SnapshotCodec::encode(*delta));
    ASSERT_TRUE(decodedDelta.snapshot.has_value()) << decodedDelta.error;
    const auto rebuilt = history.applyDelta(*decodedDelta.snapshot);
    ASSERT_TRUE(rebuilt.has_value());
    EXPECT_EQ(rebuilt->stateHash, second.stateHash);
    EXPECT_EQ(rebuilt->bodies.size(), 1u);
    const auto decodedFull = SnapshotCodec::decode(SnapshotCodec::encode(first));
    ASSERT_TRUE(decodedFull.snapshot.has_value()) << decodedFull.error;
    EXPECT_EQ(decodedFull.snapshot->stateHash, first.stateHash);
    EXPECT_FALSE(history.deltaFrom(first, first.tick).has_value());
    auto conflicting = first;
    conflicting.bodies.front().positionInvMass[0] += 1;
    conflicting.stateHash = snapshotStateHash(conflicting);
    EXPECT_FALSE(history.store(conflicting));
    EXPECT_EQ(history.find(first.islandId, first.authorityEpoch, first.tick)
                  ->stateHash,
              first.stateHash);
    auto duplicate = first;
    duplicate.bodies.push_back(first.bodies.front());
    duplicate.stateHash = snapshotStateHash(duplicate);
    EXPECT_FALSE(history.applyDelta(duplicate).has_value());
    auto noncanonicalDeltaBytes = SnapshotCodec::encode(*delta);
    ASSERT_GE(noncanonicalDeltaBytes.size(), 4u);
    noncanonicalDeltaBytes[noncanonicalDeltaBytes.size() - 4u] = std::byte{1};
    noncanonicalDeltaBytes[noncanonicalDeltaBytes.size() - 3u] = std::byte{0};
    noncanonicalDeltaBytes[noncanonicalDeltaBytes.size() - 2u] = std::byte{0};
    noncanonicalDeltaBytes[noncanonicalDeltaBytes.size() - 1u] = std::byte{0};
    EXPECT_FALSE(SnapshotCodec::decode(noncanonicalDeltaBytes)
                     .snapshot.has_value());
    auto noncanonicalFullBytes = SnapshotCodec::encode(first);
    ASSERT_GT(noncanonicalFullBytes.size(), 32u);
    noncanonicalFullBytes[32] = std::byte{1};
    EXPECT_FALSE(SnapshotCodec::decode(noncanonicalFullBytes)
                     .snapshot.has_value());

    std::vector<LockstepBody> worldBodies(8);
    worldBodies[1] = body(1u, -1, 0);
    worldBodies[2] = body(2u, 33 * 4'096, 0);
    InterestGrid grid;
    EXPECT_TRUE(grid.rebuild(worldBodies));
    const auto nearby = grid.query(grid.cellFor(worldBodies[1]), 0u);
    EXPECT_EQ(nearby.bodyIds, (std::vector<uint32_t>{1u}));
    InterestGrid limited({
        .cellSizeQ12 =
            32 * physics::deterministic::kLockstepPositionOne,
        .maximumEntries = 1u,
        .maximumQueryBodies = 8u,
    });
    EXPECT_FALSE(limited.rebuild(worldBodies));
    EXPECT_TRUE(limited.query(limited.cellFor(worldBodies[1]), 0u).overflow);

    AuthorityTable authority;
    EXPECT_TRUE(authority.assign({
        .islandId = 9,
        .epoch = 1,
        .workerId = 4,
        .startTick = 0,
    }));
    EXPECT_TRUE(authority.accepts(9, 1, 5));
    const auto migrated = authority.advanceEpoch(9, 7, 10, {1u, 2u});
    ASSERT_TRUE(migrated.has_value());
    EXPECT_EQ(migrated->epoch, 2u);
    EXPECT_FALSE(authority.accepts(9, 1, 10));
    EXPECT_TRUE(authority.accepts(9, 2, 10));
    EXPECT_FALSE(authority.assign({
        .islandId = 9,
        .epoch = 2,
        .workerId = 99,
        .startTick = 10,
        .checkpointHash = {1u, 2u},
    }));
    EXPECT_FALSE(authority.assign({
        .islandId = 9,
        .epoch = 3,
        .workerId = 99,
        .startTick = 9,
    }));

    SnapshotAckTracker acknowledgements;
    EXPECT_FALSE(acknowledgements.acknowledgedTick(1).has_value());
    acknowledgements.acknowledge(1, 2);
    EXPECT_EQ(acknowledgements.acknowledgedTick(1), 2u);
}

TEST(NetworkPrediction, RestoresIslandAndReplaysBufferedInputs) {
    constexpr uint32_t capacity = 8;
    constexpr uint32_t contacts = 16;
    std::vector<LockstepBody> initial(capacity);
    initial[1] = body(1u, 0, 10 * 4'096);

    PredictionBubble::Config predictionConfig;
    predictionConfig.world.bodyCapacity = capacity;
    predictionConfig.world.contactCapacity = contacts;
    PredictionBubble prediction;
    ASSERT_TRUE(prediction.initialize(
        predictionConfig, 1u, 1u, 1u, initial));
    ASSERT_TRUE(prediction.setMembership(
        std::array<uint32_t, 1>{1u}, std::span<const uint32_t>{}));

    std::array<CanonicalReplayCommand, 4> commands;
    for (uint64_t tick = 1; tick <= commands.size(); ++tick) {
        commands[tick - 1u] = impulse(tick, 1u, 1u, 2'048);
        ASSERT_TRUE(prediction.predict(
            tick, std::span<const CanonicalReplayCommand>(
                &commands[tick - 1u], 1u)));
    }

    physics::deterministic::LockstepWorld authoritative;
    physics::deterministic::LockstepWorld::Config worldConfig;
    worldConfig.bodyCapacity = capacity;
    worldConfig.contactCapacity = contacts;
    ASSERT_TRUE(authoritative.initialize(worldConfig));
    ASSERT_TRUE(authoritative.setBodies(initial));
    ASSERT_TRUE(physics::deterministic::applyCanonicalReplayCommand(
        authoritative, commands[0]));
    static_cast<void>(authoritative.step(1u));
    // Tick 2 deliberately omits the predicted impulse.
    static_cast<void>(authoritative.step(2u));
    const auto correction = snapshotFrom(authoritative, 2u);

    const auto reconciled = prediction.reconcile(correction);
    EXPECT_TRUE(reconciled.accepted);
    EXPECT_TRUE(reconciled.rolledBack);
    EXPECT_EQ(reconciled.replayedTicks, 2u);
    EXPECT_EQ(prediction.telemetry().rollbacks, 1u);
    EXPECT_EQ(prediction.telemetry().replayedTicks, 2u);
    EXPECT_FALSE(prediction.correctionEvents().empty());
    EXPECT_GT(prediction.recordedCommands().size(), commands.size());

    physics::deterministic::LockstepWorld expected;
    ASSERT_TRUE(expected.initialize(worldConfig));
    ASSERT_TRUE(expected.setBodies(expand(correction, capacity)));
    for (uint32_t tick = 3; tick <= 4; ++tick) {
        ASSERT_TRUE(physics::deterministic::applyCanonicalReplayCommand(
            expected, commands[tick - 1u]));
        static_cast<void>(expected.step(tick));
    }
    EXPECT_EQ(prediction.snapshot().stateHash,
              snapshotFrom(expected, 4u).stateHash);
    const auto confirmed = prediction.reconcile(prediction.snapshot());
    EXPECT_TRUE(confirmed.matched);

    auto stale = prediction.snapshot();
    stale.authorityEpoch = 99;
    stale.stateHash = snapshotStateHash(stale);
    EXPECT_FALSE(prediction.reconcile(stale).accepted);
    EXPECT_EQ(prediction.telemetry().staleSnapshots, 1u);
}

TEST(NetworkPrediction, RejectedUpdatesPreserveTheWorkingTick) {
    PredictionBubble::Config config;
    config.world.bodyCapacity = 8u;
    config.world.contactCapacity = 16u;
    std::vector<LockstepBody> initial(config.world.bodyCapacity);
    initial[1] = body(1u, 0, 8'192);
    PredictionBubble prediction;
    ASSERT_TRUE(prediction.initialize(config, 1u, 1u, 1u, initial));
    const auto initialHash = prediction.snapshot().stateHash;

    std::array commands{
        impulse(1u, 1u, 1u, 1'024),
        impulse(1u, 7u, 1u, 1'024),
    };
    EXPECT_FALSE(prediction.predict(1u, commands));
    EXPECT_EQ(prediction.currentTick(), 0u);
    EXPECT_EQ(prediction.snapshot().stateHash, initialHash);
    EXPECT_TRUE(prediction.recordedCommands().empty());

    auto wrongTick = impulse(2u, 1u, 1u, 1'024);
    EXPECT_FALSE(prediction.predict(
        1u, std::span<const CanonicalReplayCommand>(&wrongTick, 1u)));
    EXPECT_EQ(prediction.currentTick(), 0u);
    EXPECT_EQ(prediction.snapshot().stateHash, initialHash);

    auto malformed = initial;
    malformed[1].sectorRadius[3] = 0;
    EXPECT_FALSE(prediction.initialize(config, 2u, 1u, 1u, malformed));
    EXPECT_EQ(prediction.currentTick(), 0u);
    EXPECT_EQ(prediction.snapshot().stateHash, initialHash);
    EXPECT_FALSE(prediction.setMembership(
        std::array<uint32_t, 2>{1u, 7u},
        std::span<const uint32_t>{}));
}

TEST(NetworkSandbox, TwoClientsPredictMeteorsRollbackAndRejectTransforms) {
    TwoClientMeteorBoxSandbox server;
    ASSERT_TRUE(server.initialize());
    const auto initial = server.currentSnapshot();
    ASSERT_TRUE(server.acknowledgeSnapshot(1u, 0u));

    PredictionBubble::Config bubbleConfig;
    bubbleConfig.world.bodyCapacity = 32;
    bubbleConfig.world.contactCapacity = 128;
    PredictionBubble clientOne;
    PredictionBubble clientTwo;
    const auto initialBodies = expand(initial, 32);
    ASSERT_TRUE(clientOne.initialize(
        bubbleConfig, server.islandId(), server.authorityEpoch(), 1u,
        initialBodies));
    ASSERT_TRUE(clientTwo.initialize(
        bubbleConfig, server.islandId(), server.authorityEpoch(), 2u,
        initialBodies));

    SnapshotHistory clientSnapshotHistory;
    ASSERT_TRUE(clientSnapshotHistory.store(initial));
    for (uint64_t tick = 1; tick <= 6; ++tick) {
        InputFrame one{
            .tick = tick,
            .sequence = 100u + tick,
            .buttons = tick == 3u ? 1u : 0u,
            .moveXQ16 = 8'192,
        };
        InputFrame two{
            .tick = tick,
            .sequence = 200u + tick,
            .moveXQ16 = -8'192,
        };
        std::vector<InputFrame> bundleOne{one};
        if (tick > 1u) {
            InputFrame repeated = one;
            repeated.tick = tick - 1u;
            repeated.sequence -= 1u;
            bundleOne.insert(bundleOne.begin(), repeated);
        }
        ASSERT_TRUE(server.submitInputs(
            1u, server.islandId(), server.authorityEpoch(), bundleOne));
        if (tick == 1u) {
            InputFrame future = two;
            future.tick = 2u;
            future.sequence = 202u;
            const std::array redundant{two, future};
            ASSERT_TRUE(server.submitInputs(
                2u, server.islandId(), server.authorityEpoch(), redundant));
        } else if (tick != 2u) {
            std::vector<InputFrame> bundleTwo{two};
            InputFrame repeated = two;
            repeated.tick = tick - 1u;
            repeated.sequence -= 1u;
            bundleTwo.insert(bundleTwo.begin(), repeated);
            ASSERT_TRUE(server.submitInputs(
                2u, server.islandId(), server.authorityEpoch(), bundleTwo));
        }

        std::optional<CanonicalReplayCommand> predictedOne;
        if (one.buttons == 0u)
            predictedOne = impulse(tick, 1u, 1u, one.moveXQ16);
        const auto predictedTwo = impulse(tick, 2u, 1u, two.moveXQ16);
        ASSERT_TRUE(clientOne.predict(
            tick, predictedOne.has_value()
                ? std::span<const CanonicalReplayCommand>(&*predictedOne, 1u)
                : std::span<const CanonicalReplayCommand>{}));
        ASSERT_TRUE(clientTwo.predict(
            tick, std::span<const CanonicalReplayCommand>(&predictedTwo, 1u)));
        ASSERT_TRUE(server.step());

        const auto authoritative = server.currentSnapshot();
        EXPECT_TRUE(clientOne.reconcile(authoritative).accepted);
        EXPECT_TRUE(clientTwo.reconcile(authoritative).accepted);
        if (tick == 1u) {
            const auto delta = server.snapshotFor(1u);
            ASSERT_TRUE(delta.has_value());
            EXPECT_FALSE(delta->full);
            const auto rebuilt = clientSnapshotHistory.applyDelta(*delta);
            ASSERT_TRUE(rebuilt.has_value());
            EXPECT_EQ(rebuilt->stateHash, authoritative.stateHash);
        }
    }
    EXPECT_EQ(server.telemetry().spawnedMeteors, 1u);
    EXPECT_GE(server.telemetry().duplicateInputs, 1u);
    EXPECT_GE(clientOne.telemetry().rollbacks, 1u);
    EXPECT_GE(clientTwo.telemetry().rollbacks, 1u);

    CanonicalNetworkCommand direct;
    direct.tick = server.currentTick() + 1u;
    direct.sequence = 10'000u;
    direct.islandId = server.islandId();
    direct.authorityEpoch = server.authorityEpoch();
    direct.clientId = 1u;
    direct.type = NetworkCommandType::MoveInput;
    direct.body = 1u;
    direct.generation = server.bodies()[1].identity[1];
    ASSERT_TRUE(server.submitCommand(direct));
    EXPECT_FALSE(server.submitCommand(direct));
    EXPECT_EQ(server.telemetry().duplicateCommands, 1u);

    auto wrongGeneration = server.bodies()[1];
    ++wrongGeneration.identity[1];
    EXPECT_FALSE(server.queueCorrection(1u, wrongGeneration));

    CanonicalNetworkCommand malicious;
    malicious.tick = server.currentTick() + 1u;
    malicious.sequence = 999;
    malicious.islandId = server.islandId();
    malicious.authorityEpoch = server.authorityEpoch();
    malicious.clientId = 1;
    malicious.type = NetworkCommandType::Correction;
    malicious.body = 1;
    malicious.generation = 1;
    EXPECT_FALSE(server.submitCommand(malicious));

    LockstepBody corrected = server.bodies()[1];
    corrected.positionInvMass[0] += 128;
    ASSERT_TRUE(server.queueCorrection(1u, corrected));
    ASSERT_TRUE(server.step());
    EXPECT_EQ(server.telemetry().correctionEvents, 1u);
    EXPECT_FALSE(server.recordedNetworkCommands().empty());
    EXPECT_FALSE(server.recordedReplayCommands().empty());
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

    const auto migrated = server.advanceAuthority(
        4u, server.currentTick() + 1u);
    ASSERT_TRUE(migrated.has_value());
    CanonicalNetworkCommand oldEpoch;
    oldEpoch.tick = server.currentTick() + 1u;
    oldEpoch.sequence = 1;
    oldEpoch.islandId = server.islandId();
    oldEpoch.authorityEpoch = migrated->epoch - 1u;
    oldEpoch.clientId = 1;
    oldEpoch.type = NetworkCommandType::MoveInput;
    oldEpoch.body = 1;
    oldEpoch.generation = server.bodies()[1].identity[1];
    EXPECT_FALSE(server.submitCommand(oldEpoch));
    EXPECT_GE(server.telemetry().staleEpochCommands, 1u);
}

} // namespace
} // namespace voxy::network
