#include "network/wreckwater_client_runtime.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <utility>
#include <vector>

namespace voxy::network {
namespace {

class FakeRuntimeTransport final : public IMultiplayerTransport {
public:
    struct SentFrame {
        uint32_t peerId = 0u;
        uint64_t connectionSerial = 0u;
        DeliveryClass delivery = DeliveryClass::Realtime;
        std::vector<std::byte> bytes;
    };

    void service() override { ++serviceCalls; }

    bool send(
        uint32_t peerId, DeliveryClass delivery,
        std::span<const std::byte> bytes) override {
        ++legacySendCalls;
        if (!allowSend) return false;
        sent.push_back({
            .peerId = peerId,
            .delivery = delivery,
            .bytes = std::vector<std::byte>(bytes.begin(), bytes.end()),
        });
        return true;
    }

    bool send(
        uint32_t peerId, uint64_t connectionSerial,
        DeliveryClass delivery,
        std::span<const std::byte> bytes) override {
        ++serialSendCalls;
        if (!allowSend) return false;
        sent.push_back({
            .peerId = peerId,
            .connectionSerial = connectionSerial,
            .delivery = delivery,
            .bytes = std::vector<std::byte>(bytes.begin(), bytes.end()),
        });
        return true;
    }

    std::optional<MultiplayerTransportFrame> poll() override {
        ++pollCalls;
        if (inbound.empty()) return std::nullopt;
        MultiplayerTransportFrame frame = std::move(inbound.front());
        inbound.pop_front();
        return frame;
    }

    void close() override {
        closed = true;
        inbound.clear();
    }

    uint64_t serviceCalls = 0u;
    uint64_t pollCalls = 0u;
    uint64_t legacySendCalls = 0u;
    uint64_t serialSendCalls = 0u;
    bool allowSend = true;
    bool closed = false;
    std::deque<MultiplayerTransportFrame> inbound;
    std::vector<SentFrame> sent;
};

WreckwaterClientRuntime::Config runtimeConfig() {
    return {
        .serverPeerId = 0u,
        .serverConnectionSerial = 77u,
        .sessionId = 101u,
        .matchId = 202u,
        .worldId = 303u,
        .worldEpoch = 4u,
        .authorityEpoch = 5u,
        .maximumFramesPerPump = 16u,
        .firstClientRequestSequence = 1u,
    };
}

WreckwaterEntityState makeSkiff(
    NetEntityId netId, uint32_t skiffId, float x) {
    WreckwaterEntityState entity;
    entity.netEntityId = netId;
    entity.netGeneration = 1u;
    entity.kind = WreckwaterEntityKind::Skiff;
    entity.crew = skiffId % 2u == 0u
        ? WreckwaterCrew::CrewTwo
        : WreckwaterCrew::CrewOne;
    entity.localPosition = {x, 0.0f, 0.0f};
    entity.orientation = {0.0f, 0.0f, 0.0f, 1.0f};
    entity.shape = WreckwaterShape::Box;
    entity.dimensions = {3.0f, 1.0f, 6.0f};
    entity.skiff = {
        .skiffId = skiffId,
        .generation = 1u,
        .disposition = WreckwaterSkiffDisposition::Active,
    };
    return entity;
}

WreckwaterCharacterState makeCharacter(
    uint32_t playerId, uint32_t skiffId, float x) {
    WreckwaterCharacterState character;
    character.characterHandle = (1u << 16u) | playerId;
    character.stateFlags =
        static_cast<uint32_t>(WreckwaterCharacterMode::OnSkiff) |
        kWreckwaterCharacterStateActiveFlag |
        kWreckwaterCharacterStateConnectedFlag;
    character.playerId = playerId;
    character.connectionGeneration = 1u;
    character.localFeetPosition = {x, 1.0f, 0.0f};
    character.skiffId = skiffId;
    character.skiffGeneration = 1u;
    character.skiffLocalFeetPosition = {x, 0.5f, 0.0f};
    return character;
}

WreckwaterCertifiedSnapshot makeSnapshot(
    const WreckwaterClientRuntime::Config& config,
    uint64_t sequence, uint64_t applicationTick,
    uint64_t evidenceTick, float x = 0.0f) {
    WreckwaterCertifiedSnapshot snapshot;
    snapshot.sessionId = config.sessionId;
    snapshot.matchId = config.matchId;
    snapshot.worldId = config.worldId;
    snapshot.worldEpoch = config.worldEpoch;
    snapshot.authorityEpoch = config.authorityEpoch;
    snapshot.snapshotSequence = sequence;
    snapshot.applicationTick = applicationTick;
    snapshot.physicsEvidenceTick = evidenceTick;
    snapshot.phase = WreckwaterPhase::Live;
    snapshot.matchStateHash =
        static_cast<uint32_t>(0x1000u + sequence);
    snapshot.eventStreamHash =
        static_cast<uint32_t>(0x2000u + sequence);
    snapshot.entities = {makeSkiff(10u, 11u, x)};
    snapshot.characters = {
        makeCharacter(1u, 11u, -1.0f),
        makeCharacter(2u, 11u, -0.5f),
        makeCharacter(3u, 11u, 0.5f),
        makeCharacter(4u, 11u, 1.0f),
    };
    EXPECT_TRUE(canonicalizeWreckwaterSnapshot(snapshot));
    return snapshot;
}

Packet makeSnapshotPacket(
    const WreckwaterClientRuntime::Config& config,
    const WreckwaterCertifiedSnapshot& snapshot) {
    const WreckwaterWriteResult inner =
        WreckwaterSnapshotCodec::encode(snapshot);
    EXPECT_TRUE(inner) << wreckwaterCodecErrorName(inner.error);
    Packet packet;
    packet.header.payloadType = PacketPayloadType::Snapshot;
    packet.header.sessionId = config.sessionId;
    packet.header.worldId = config.worldId;
    packet.header.worldEpoch = config.worldEpoch;
    packet.header.authorityEpoch = config.authorityEpoch;
    packet.header.sequence = snapshot.snapshotSequence;
    packet.header.tick = snapshot.applicationTick;
    packet.payload = inner.bytes;
    return packet;
}

MultiplayerTransportFrame frameFromPacket(
    const WreckwaterClientRuntime::Config& config, const Packet& packet,
    DeliveryClass delivery = DeliveryClass::Realtime) {
    const PacketWriteResult encoded = PacketCodec::encode(packet, delivery);
    EXPECT_TRUE(encoded.error.empty()) << encoded.error;
    return {
        .peerId = config.serverPeerId,
        .delivery = delivery,
        .bytes = encoded.bytes,
        .type = MultiplayerTransportFrameType::Data,
        .connectionSerial = config.serverConnectionSerial,
    };
}

MultiplayerTransportFrame snapshotFrame(
    const WreckwaterClientRuntime::Config& config,
    const WreckwaterCertifiedSnapshot& snapshot) {
    return frameFromPacket(config, makeSnapshotPacket(config, snapshot));
}

MultiplayerTransportFrame lifecycleFrame(
    const WreckwaterClientRuntime::Config& config,
    MultiplayerTransportFrameType type) {
    return {
        .peerId = config.serverPeerId,
        .delivery = DeliveryClass::Realtime,
        .bytes = {},
        .type = type,
        .connectionSerial = config.serverConnectionSerial,
    };
}

Packet decodedSentPacket(
    const FakeRuntimeTransport::SentFrame& sent) {
    const PacketReadResult decoded =
        PacketCodec::decode(sent.bytes, sent.delivery);
    EXPECT_TRUE(decoded.packet.has_value()) << decoded.error;
    return decoded.packet.value_or(Packet{});
}

WreckwaterActionRequest decodedSentAction(
    const FakeRuntimeTransport::SentFrame& sent) {
    const Packet packet = decodedSentPacket(sent);
    const WreckwaterActionReadResult decoded =
        WreckwaterActionRequestCodec::decode(packet.payload);
    EXPECT_TRUE(decoded.request.has_value())
        << wreckwaterCodecErrorName(decoded.error);
    return decoded.request.value_or(WreckwaterActionRequest{});
}

WreckwaterCharacterInputRequest decodedSentCharacterInput(
    const FakeRuntimeTransport::SentFrame& sent) {
    const Packet packet = decodedSentPacket(sent);
    const WreckwaterCharacterInputReadResult decoded =
        WreckwaterCharacterInputRequestCodec::decode(packet.payload);
    EXPECT_TRUE(decoded.request.has_value())
        << wreckwaterCodecErrorName(decoded.error);
    return decoded.request.value_or(
        WreckwaterCharacterInputRequest{});
}

TEST(
    WreckwaterClientRuntimeTest,
    ServicesOnceAndSamplesOnCertifiedEvidenceTimeline) {
    const WreckwaterClientRuntime::Config config = runtimeConfig();
    auto transport = std::make_unique<FakeRuntimeTransport>();
    FakeRuntimeTransport* fake = transport.get();
    fake->inbound.push_back(lifecycleFrame(
        config, MultiplayerTransportFrameType::Connected));
    fake->inbound.push_back(snapshotFrame(
        config, makeSnapshot(config, 1u, 102u, 100u, -3.0f)));
    fake->inbound.push_back(snapshotFrame(
        config, makeSnapshot(config, 2u, 105u, 103u, 3.0f)));

    WreckwaterClientRuntime runtime(config, std::move(transport));
    ASSERT_TRUE(runtime.initialized());
    const WreckwaterClientPumpResult pumped = runtime.pump();
    ASSERT_TRUE(pumped)
        << wreckwaterClientRuntimeErrorName(pumped.lastError);
    EXPECT_EQ(fake->serviceCalls, 1u);
    EXPECT_EQ(pumped.framesPolled, 3u);
    EXPECT_EQ(pumped.snapshotsAccepted, 2u);
    EXPECT_TRUE(runtime.hasSnapshot());
    EXPECT_EQ(runtime.latestPhysicsEvidenceTick(), 103u);

    const WreckwaterClientSampleResult sampled =
        runtime.sampleVisual({.whole = 101u, .fraction = 0.5f});
    ASSERT_TRUE(sampled);
    ASSERT_EQ(sampled.sample.entityCount, 1u);
    EXPECT_EQ(
        sampled.sample.entities[0].motionMode,
        WreckwaterVisualMotionMode::Interpolated);
    EXPECT_NEAR(
        sampled.sample.entities[0].visualPose.localPosition.x,
        0.0f, 1.0e-6f);
    EXPECT_EQ(sampled.sample.authoritative.applicationTick, 105u);
    EXPECT_EQ(sampled.sample.authoritative.physicsEvidenceTick, 103u);

    const WreckwaterClientSampleResult latest = runtime.latestSample();
    ASSERT_TRUE(latest);
    EXPECT_EQ(latest.sample.authoritative.snapshotSequence, 2u);
    EXPECT_EQ(fake->serviceCalls, 1u);

    EXPECT_TRUE(runtime.pump());
    EXPECT_EQ(fake->serviceCalls, 2u);
}

TEST(
    WreckwaterClientRuntimeTest,
    PumpBudgetIsBoundedAndNeverHidesAdditionalServiceCalls) {
    WreckwaterClientRuntime::Config config = runtimeConfig();
    config.maximumFramesPerPump = 2u;
    auto transport = std::make_unique<FakeRuntimeTransport>();
    FakeRuntimeTransport* fake = transport.get();
    for (uint32_t index = 0u; index < 5u; ++index) {
        fake->inbound.push_back(lifecycleFrame(
            config, MultiplayerTransportFrameType::Connected));
    }
    WreckwaterClientRuntime runtime(config, std::move(transport));

    EXPECT_EQ(runtime.pump().framesPolled, 2u);
    EXPECT_EQ(fake->serviceCalls, 1u);
    EXPECT_EQ(fake->inbound.size(), 3u);
    EXPECT_EQ(runtime.pump().framesPolled, 2u);
    EXPECT_EQ(fake->serviceCalls, 2u);
    EXPECT_EQ(runtime.pump().framesPolled, 1u);
    EXPECT_EQ(fake->serviceCalls, 3u);
    EXPECT_TRUE(fake->inbound.empty());
    EXPECT_EQ(runtime.telemetry().maximumFramesDrainedInPump, 2u);
}

TEST(
    WreckwaterClientRuntimeTest,
    LearnsInitialServerSerialOnlyFromConnected) {
    WreckwaterClientRuntime::Config config = runtimeConfig();
    config.serverConnectionSerial = 0u;
    auto transport = std::make_unique<FakeRuntimeTransport>();
    FakeRuntimeTransport* fake = transport.get();
    WreckwaterClientRuntime runtime(config, std::move(transport));
    ASSERT_TRUE(runtime.initialized());
    EXPECT_FALSE(runtime.connectionActive());
    EXPECT_EQ(runtime.serverConnectionSerial(), 0u);
    EXPECT_FALSE(runtime.disconnectForTransportReplacement());
    EXPECT_FALSE(runtime.replaceTransport(
        std::make_unique<FakeRuntimeTransport>()));
    EXPECT_EQ(
        runtime.sendHelm(1u, 0, 0).error,
        WreckwaterClientRuntimeError::NoActiveConnection);
    EXPECT_EQ(runtime.nextClientRequestSequence(), 1u);

    MultiplayerTransportFrame dataBeforeConnected = snapshotFrame(
        config, makeSnapshot(config, 1u, 3u, 1u));
    dataBeforeConnected.connectionSerial = 88u;
    fake->inbound.push_back(std::move(dataBeforeConnected));
    EXPECT_EQ(
        runtime.pump().lastError,
        WreckwaterClientRuntimeError::StaleConnectionFrame);
    EXPECT_FALSE(runtime.hasSnapshot());

    MultiplayerTransportFrame connected = lifecycleFrame(
        config, MultiplayerTransportFrameType::Connected);
    connected.connectionSerial = 88u;
    MultiplayerTransportFrame first = snapshotFrame(
        config, makeSnapshot(config, 1u, 3u, 1u));
    first.connectionSerial = 88u;
    fake->inbound.push_back(std::move(connected));
    fake->inbound.push_back(std::move(first));
    const WreckwaterClientPumpResult pumped = runtime.pump();
    ASSERT_TRUE(pumped);
    EXPECT_EQ(pumped.snapshotsAccepted, 1u);
    EXPECT_TRUE(runtime.connectionActive());
    EXPECT_EQ(runtime.serverConnectionSerial(), 88u);
    EXPECT_EQ(runtime.telemetry().connectionSerialBindings, 1u);

    ASSERT_TRUE(runtime.sendHelm(4u, 0, 0));
    ASSERT_EQ(fake->sent.size(), 1u);
    EXPECT_EQ(fake->sent.front().connectionSerial, 88u);
}

TEST(
    WreckwaterClientRuntimeTest,
    ExplicitReconnectRejectsOldSerialAndPreservesClientState) {
    const WreckwaterClientRuntime::Config config = runtimeConfig();
    auto firstTransport =
        std::make_unique<FakeRuntimeTransport>();
    FakeRuntimeTransport* firstFake = firstTransport.get();
    WreckwaterClientRuntime runtime(
        config, std::move(firstTransport));

    firstFake->inbound.push_back(snapshotFrame(
        config, makeSnapshot(config, 1u, 3u, 1u, -2.0f)));
    ASSERT_TRUE(runtime.pump());
    ASSERT_TRUE(runtime.sendHelm(4u, 1, -1));
    EXPECT_EQ(runtime.nextClientRequestSequence(), 2u);
    EXPECT_EQ(runtime.snapshotBuffer().size(), 1u);

    firstFake->inbound.push_back(lifecycleFrame(
        config, MultiplayerTransportFrameType::Disconnected));
    ASSERT_TRUE(runtime.pump());
    EXPECT_FALSE(runtime.connectionActive());
    EXPECT_FALSE(runtime.replaceTransport(nullptr));

    auto replacement =
        std::make_unique<FakeRuntimeTransport>();
    FakeRuntimeTransport* replacementFake = replacement.get();
    ASSERT_TRUE(runtime.replaceTransport(std::move(replacement)));
    EXPECT_EQ(runtime.telemetry().transportReplacements, 1u);
    EXPECT_EQ(runtime.serverConnectionSerial(), 0u);
    EXPECT_EQ(runtime.nextClientRequestSequence(), 2u);
    EXPECT_EQ(runtime.snapshotBuffer().size(), 1u);

    replacementFake->inbound.push_back(snapshotFrame(
        config, makeSnapshot(config, 2u, 6u, 4u, 2.0f)));
    EXPECT_EQ(
        runtime.pump().lastError,
        WreckwaterClientRuntimeError::StaleConnectionFrame);

    replacementFake->inbound.push_back(lifecycleFrame(
        config, MultiplayerTransportFrameType::Connected));
    EXPECT_EQ(
        runtime.pump().lastError,
        WreckwaterClientRuntimeError::StaleConnectionFrame);
    EXPECT_FALSE(runtime.connectionActive());

    MultiplayerTransportFrame reconnected = lifecycleFrame(
        config, MultiplayerTransportFrameType::Connected);
    reconnected.connectionSerial = 78u;
    replacementFake->inbound.push_back(std::move(reconnected));
    ASSERT_TRUE(runtime.pump());
    EXPECT_TRUE(runtime.connectionActive());
    EXPECT_EQ(runtime.serverConnectionSerial(), 78u);
    EXPECT_FALSE(runtime.replaceTransport(
        std::make_unique<FakeRuntimeTransport>()));

    MultiplayerTransportFrame staleOldData = snapshotFrame(
        config, makeSnapshot(config, 2u, 6u, 4u, 2.0f));
    replacementFake->inbound.push_back(std::move(staleOldData));
    EXPECT_EQ(
        runtime.pump().lastError,
        WreckwaterClientRuntimeError::StaleConnectionFrame);

    MultiplayerTransportFrame resumed = snapshotFrame(
        config, makeSnapshot(config, 2u, 6u, 4u, 2.0f));
    resumed.connectionSerial = 78u;
    replacementFake->inbound.push_back(std::move(resumed));
    ASSERT_TRUE(runtime.pump());
    EXPECT_EQ(runtime.snapshotBuffer().size(), 2u);
    EXPECT_EQ(runtime.nextClientRequestSequence(), 2u);

    const WreckwaterClientSampleResult between =
        runtime.sampleVisual({.whole = 2u, .fraction = 0.5f});
    ASSERT_TRUE(between);
    EXPECT_EQ(
        between.sample.entities[0].motionMode,
        WreckwaterVisualMotionMode::Interpolated);

    const WreckwaterClientActionSendResult sent =
        runtime.sendCargoAction(
            7u, WreckwaterAction::Tow, 1u, 1u, 1u);
    ASSERT_TRUE(sent);
    EXPECT_EQ(sent.clientRequestSequence, 2u);
    ASSERT_EQ(replacementFake->sent.size(), 1u);
    EXPECT_EQ(
        replacementFake->sent.front().connectionSerial, 78u);
    const Packet packet =
        decodedSentPacket(replacementFake->sent.front());
    EXPECT_EQ(packet.header.ackSequence, 2u);
    EXPECT_EQ(packet.header.ackBits, 3u);
}

TEST(
    WreckwaterClientRuntimeTest,
    LocalDisconnectFencesSerialBeforeTransportReplacement) {
    const WreckwaterClientRuntime::Config config = runtimeConfig();
    auto transport = std::make_unique<FakeRuntimeTransport>();
    FakeRuntimeTransport* fake = transport.get();
    WreckwaterClientRuntime runtime(config, std::move(transport));

    fake->inbound.push_back(snapshotFrame(
        config, makeSnapshot(config, 1u, 3u, 1u)));
    ASSERT_TRUE(runtime.pump());
    ASSERT_TRUE(runtime.sendHelm(4u, 0, 0));
    EXPECT_FALSE(runtime.replaceTransport(
        std::make_unique<FakeRuntimeTransport>()));

    ASSERT_TRUE(runtime.disconnectForTransportReplacement());
    EXPECT_FALSE(runtime.connectionActive());
    EXPECT_FALSE(runtime.disconnectForTransportReplacement());
    EXPECT_EQ(runtime.telemetry().localTransportDisconnects, 1u);
    EXPECT_EQ(runtime.nextClientRequestSequence(), 2u);
    EXPECT_EQ(runtime.snapshotBuffer().size(), 1u);

    auto replacement =
        std::make_unique<FakeRuntimeTransport>();
    FakeRuntimeTransport* replacementFake = replacement.get();
    ASSERT_TRUE(runtime.replaceTransport(std::move(replacement)));
    MultiplayerTransportFrame connected = lifecycleFrame(
        config, MultiplayerTransportFrameType::Connected);
    connected.connectionSerial = 78u;
    replacementFake->inbound.push_back(std::move(connected));
    ASSERT_TRUE(runtime.pump());
    EXPECT_EQ(runtime.serverConnectionSerial(), 78u);

    ASSERT_TRUE(runtime.sendHelm(5u, 0, 0));
    ASSERT_EQ(replacementFake->sent.size(), 1u);
    EXPECT_EQ(
        replacementFake->sent.front().connectionSerial, 78u);
    EXPECT_EQ(
        decodedSentAction(replacementFake->sent.front())
            .clientRequestSequence,
        2u);
}

TEST(
    WreckwaterClientRuntimeTest,
    RejectsStaleConnectionSessionSequenceAndPostDisconnectData) {
    const WreckwaterClientRuntime::Config config = runtimeConfig();
    const WreckwaterCertifiedSnapshot first =
        makeSnapshot(config, 1u, 102u, 100u);
    const MultiplayerTransportFrame valid = snapshotFrame(config, first);
    auto transport = std::make_unique<FakeRuntimeTransport>();
    FakeRuntimeTransport* fake = transport.get();
    fake->inbound.push_back(valid);
    WreckwaterClientRuntime runtime(config, std::move(transport));
    ASSERT_TRUE(runtime.pump());

    fake->inbound.push_back(valid);
    WreckwaterClientPumpResult result = runtime.pump();
    EXPECT_EQ(
        result.lastError,
        WreckwaterClientRuntimeError::SnapshotRejected);
    EXPECT_EQ(
        result.lastReplicationError,
        WreckwaterClientReplicationError::NonMonotonicSequence);

    MultiplayerTransportFrame staleSerial = valid;
    staleSerial.connectionSerial += 1u;
    fake->inbound.push_back(std::move(staleSerial));
    result = runtime.pump();
    EXPECT_EQ(
        result.lastError,
        WreckwaterClientRuntimeError::StaleConnectionFrame);

    Packet staleSession = makeSnapshotPacket(
        config, makeSnapshot(config, 2u, 105u, 103u));
    staleSession.header.sessionId += 1u;
    fake->inbound.push_back(frameFromPacket(config, staleSession));
    result = runtime.pump();
    EXPECT_EQ(
        result.lastError,
        WreckwaterClientRuntimeError::OuterIdentityMismatch);

    WreckwaterCertifiedSnapshot staleMatch =
        makeSnapshot(config, 2u, 105u, 103u);
    staleMatch.matchId += 1u;
    EXPECT_TRUE(canonicalizeWreckwaterSnapshot(staleMatch));
    Packet staleMatchPacket = makeSnapshotPacket(config, staleMatch);
    fake->inbound.push_back(frameFromPacket(config, staleMatchPacket));
    result = runtime.pump();
    EXPECT_EQ(
        result.lastError,
        WreckwaterClientRuntimeError::SnapshotIdentityMismatch);

    fake->inbound.push_back(lifecycleFrame(
        config, MultiplayerTransportFrameType::Disconnected));
    EXPECT_TRUE(runtime.pump());
    EXPECT_FALSE(runtime.connectionActive());
    fake->inbound.push_back(snapshotFrame(
        config, makeSnapshot(config, 2u, 105u, 103u)));
    result = runtime.pump();
    EXPECT_EQ(
        result.lastError,
        WreckwaterClientRuntimeError::StaleConnectionFrame);

    MultiplayerTransportFrame reconnect = lifecycleFrame(
        config, MultiplayerTransportFrameType::Connected);
    reconnect.connectionSerial += 1u;
    fake->inbound.push_back(std::move(reconnect));
    result = runtime.pump();
    EXPECT_EQ(
        result.lastError,
        WreckwaterClientRuntimeError::StaleConnectionFrame);
    EXPECT_FALSE(runtime.connectionActive());
}

TEST(
    WreckwaterClientRuntimeTest,
    RealtimeLaneAcceptsOnlyCertifiedSnapshotPackets) {
    const WreckwaterClientRuntime::Config config = runtimeConfig();
    auto transport = std::make_unique<FakeRuntimeTransport>();
    FakeRuntimeTransport* fake = transport.get();
    WreckwaterClientRuntime runtime(config, std::move(transport));

    WreckwaterActionRequest request;
    request.requestedApplicationTick = 1u;
    request.clientRequestSequence = 1u;
    request.action = WreckwaterAction::Helm;
    const WreckwaterWriteResult action =
        WreckwaterActionRequestCodec::encode(request);
    ASSERT_TRUE(action);
    Packet wrongPayload;
    wrongPayload.header.payloadType = PacketPayloadType::Input;
    wrongPayload.header.sessionId = config.sessionId;
    wrongPayload.header.worldId = config.worldId;
    wrongPayload.header.worldEpoch = config.worldEpoch;
    wrongPayload.header.authorityEpoch = config.authorityEpoch;
    wrongPayload.header.sequence = 1u;
    wrongPayload.header.tick = 1u;
    wrongPayload.payload = action.bytes;
    fake->inbound.push_back(frameFromPacket(config, wrongPayload));
    WreckwaterClientPumpResult result = runtime.pump();
    EXPECT_EQ(
        result.lastError,
        WreckwaterClientRuntimeError::WrongPayloadType);

    Packet malformedInner = wrongPayload;
    malformedInner.header.payloadType = PacketPayloadType::Snapshot;
    malformedInner.payload = {std::byte{0x01u}};
    fake->inbound.push_back(frameFromPacket(config, malformedInner));
    result = runtime.pump();
    EXPECT_EQ(
        result.lastError,
        WreckwaterClientRuntimeError::SnapshotDecodeFailed);

    MultiplayerTransportFrame corrupt = snapshotFrame(
        config, makeSnapshot(config, 1u, 3u, 1u));
    ASSERT_FALSE(corrupt.bytes.empty());
    corrupt.bytes.back() ^= std::byte{0x80u};
    fake->inbound.push_back(std::move(corrupt));
    result = runtime.pump();
    EXPECT_EQ(
        result.lastError,
        WreckwaterClientRuntimeError::OuterPacketDecodeFailed);

    MultiplayerTransportFrame wrongLane = snapshotFrame(
        config, makeSnapshot(config, 1u, 3u, 1u));
    wrongLane.delivery = DeliveryClass::ReliableEvent;
    fake->inbound.push_back(std::move(wrongLane));
    result = runtime.pump();
    EXPECT_EQ(
        result.lastError,
        WreckwaterClientRuntimeError::WrongDeliveryClass);
    EXPECT_FALSE(runtime.hasSnapshot());
}

TEST(
    WreckwaterClientRuntimeTest,
    RejectedFramesDoNotPoisonSnapshotAcknowledgements) {
    const WreckwaterClientRuntime::Config config = runtimeConfig();
    auto transport = std::make_unique<FakeRuntimeTransport>();
    FakeRuntimeTransport* fake = transport.get();
    WreckwaterClientRuntime runtime(config, std::move(transport));

    MultiplayerTransportFrame invalidLifecycle = lifecycleFrame(
        config, MultiplayerTransportFrameType::Connected);
    invalidLifecycle.bytes.push_back(std::byte{0x01u});
    fake->inbound.push_back(std::move(invalidLifecycle));
    EXPECT_EQ(
        runtime.pump().lastError,
        WreckwaterClientRuntimeError::InvalidLifecycleFrame);

    MultiplayerTransportFrame oversized = snapshotFrame(
        config, makeSnapshot(config, 1u, 3u, 1u));
    oversized.bytes.resize(kConservativeRealtimeMtu + 1u);
    fake->inbound.push_back(std::move(oversized));
    EXPECT_EQ(
        runtime.pump().lastError,
        WreckwaterClientRuntimeError::OversizedFrame);

    const WreckwaterCertifiedSnapshot first =
        makeSnapshot(config, 1u, 3u, 1u);
    Packet mismatchedSequence = makeSnapshotPacket(config, first);
    mismatchedSequence.header.sequence = 9u;
    fake->inbound.push_back(
        frameFromPacket(config, mismatchedSequence));
    EXPECT_EQ(
        runtime.pump().lastError,
        WreckwaterClientRuntimeError::SnapshotIdentityMismatch);

    Packet mismatchedTick = makeSnapshotPacket(config, first);
    mismatchedTick.header.tick = 4u;
    fake->inbound.push_back(frameFromPacket(config, mismatchedTick));
    EXPECT_EQ(
        runtime.pump().lastError,
        WreckwaterClientRuntimeError::SnapshotIdentityMismatch);

    fake->inbound.push_back(snapshotFrame(config, first));
    ASSERT_TRUE(runtime.pump());
    ASSERT_TRUE(runtime.sendHelm(4u, 0, 0));
    ASSERT_EQ(fake->sent.size(), 1u);
    const Packet sent = decodedSentPacket(fake->sent.front());
    EXPECT_EQ(sent.header.ackSequence, 1u);
    EXPECT_EQ(sent.header.ackBits, 1u);
}

TEST(
    WreckwaterClientRuntimeTest,
    SendsHelmRealtimeAndCargoActionsReliablyWithSafeMonotonicPayloads) {
    const WreckwaterClientRuntime::Config config = runtimeConfig();
    auto transport = std::make_unique<FakeRuntimeTransport>();
    FakeRuntimeTransport* fake = transport.get();
    fake->inbound.push_back(snapshotFrame(
        config, makeSnapshot(config, 9u, 30u, 28u)));
    WreckwaterClientRuntime runtime(config, std::move(transport));
    ASSERT_TRUE(runtime.pump());

    const WreckwaterClientActionSendResult firstHelm =
        runtime.sendHelm(
            31u, kWreckwaterHelmAxisMaximum,
            kWreckwaterHelmAxisMinimum);
    ASSERT_TRUE(firstHelm);
    EXPECT_EQ(firstHelm.clientRequestSequence, 1u);
    ASSERT_EQ(fake->sent.size(), 1u);
    EXPECT_EQ(fake->legacySendCalls, 0u);
    EXPECT_EQ(fake->serialSendCalls, 1u);
    EXPECT_EQ(fake->sent[0].peerId, config.serverPeerId);
    EXPECT_EQ(
        fake->sent[0].connectionSerial,
        config.serverConnectionSerial);
    EXPECT_EQ(fake->sent[0].delivery, DeliveryClass::Realtime);
    EXPECT_EQ(
        fake->sent[0].bytes.size(),
        kWreckwaterFramedActionRequestBytes);
    EXPECT_LE(
        fake->sent[0].bytes.size(), kConservativeRealtimeMtu);
    const Packet helmPacket = decodedSentPacket(fake->sent[0]);
    EXPECT_EQ(helmPacket.header.payloadType, PacketPayloadType::Input);
    EXPECT_EQ(helmPacket.header.sessionId, config.sessionId);
    EXPECT_EQ(helmPacket.header.worldId, config.worldId);
    EXPECT_EQ(helmPacket.header.worldEpoch, config.worldEpoch);
    EXPECT_EQ(helmPacket.header.authorityEpoch, config.authorityEpoch);
    EXPECT_EQ(helmPacket.header.sequence, 1u);
    EXPECT_EQ(helmPacket.header.ackSequence, 9u);
    EXPECT_EQ(helmPacket.header.ackBits, 1u);
    const WreckwaterActionRequest helm =
        decodedSentAction(fake->sent[0]);
    EXPECT_EQ(helm.schemaVersion, kWreckwaterWireSchemaVersion);
    EXPECT_EQ(helm.action, WreckwaterAction::Helm);
    EXPECT_EQ(helm.clientRequestSequence, 1u);
    EXPECT_EQ(helm.cargoId, 0u);
    EXPECT_EQ(helm.cargoGeneration, 0u);
    EXPECT_EQ(helm.observedCargoRevision, 0u);
    EXPECT_EQ(helm.helmThrottleQ15, kWreckwaterHelmAxisMaximum);
    EXPECT_EQ(helm.helmSteeringQ15, kWreckwaterHelmAxisMinimum);

    const WreckwaterClientActionSendResult invalidHelm =
        runtime.sendHelm(
            32u, std::numeric_limits<int16_t>::min(), 0);
    EXPECT_EQ(
        invalidHelm.error,
        WreckwaterClientRuntimeError::ActionEncodeFailed);
    EXPECT_EQ(invalidHelm.codecError, WreckwaterCodecError::InvalidControl);
    EXPECT_EQ(runtime.nextClientRequestSequence(), 2u);

    constexpr std::array actions{
        WreckwaterAction::Tow,
        WreckwaterAction::Cut,
        WreckwaterAction::Steal,
        WreckwaterAction::Bank,
    };
    for (size_t index = 0u; index < actions.size(); ++index) {
        const WreckwaterClientActionSendResult sent =
            runtime.sendCargoAction(
                33u + static_cast<uint64_t>(index), actions[index],
                7u, 8u, 9u);
        ASSERT_TRUE(sent);
        EXPECT_EQ(
            sent.clientRequestSequence,
            2u + static_cast<uint64_t>(index));
        const FakeRuntimeTransport::SentFrame& frame =
            fake->sent[index + 1u];
        EXPECT_EQ(frame.delivery, DeliveryClass::ReliableEvent);
        EXPECT_EQ(
            decodedSentPacket(frame).header.payloadType,
            PacketPayloadType::Command);
        const WreckwaterActionRequest decoded =
            decodedSentAction(frame);
        EXPECT_EQ(decoded.action, actions[index]);
        EXPECT_EQ(decoded.cargoId, 7u);
        EXPECT_EQ(decoded.cargoGeneration, 8u);
        EXPECT_EQ(decoded.observedCargoRevision, 9u);
        EXPECT_EQ(decoded.helmThrottleQ15, 0);
        EXPECT_EQ(decoded.helmSteeringQ15, 0);
    }

    EXPECT_EQ(
        runtime.sendCargoAction(
            40u, WreckwaterAction::Helm, 1u, 1u, 1u)
            .error,
        WreckwaterClientRuntimeError::InvalidAction);
    EXPECT_EQ(runtime.nextClientRequestSequence(), 6u);
    const WreckwaterClientActionSendResult invalidCargo =
        runtime.sendCargoAction(
            40u, WreckwaterAction::Tow, 0u, 1u, 1u);
    EXPECT_EQ(
        invalidCargo.error,
        WreckwaterClientRuntimeError::ActionEncodeFailed);
    EXPECT_EQ(invalidCargo.codecError, WreckwaterCodecError::InvalidIdentity);
    EXPECT_EQ(runtime.nextClientRequestSequence(), 6u);

    EXPECT_EQ(
        runtime.sendHelm(35u, 0, 0).error,
        WreckwaterClientRuntimeError::NonMonotonicRequestTick);
    EXPECT_EQ(runtime.nextClientRequestSequence(), 6u);

    fake->allowSend = false;
    const WreckwaterClientActionSendResult failedSend =
        runtime.sendCargoAction(
            41u, WreckwaterAction::Tow, 1u, 1u, 1u);
    EXPECT_EQ(
        failedSend.error,
        WreckwaterClientRuntimeError::TransportSendFailed);
    EXPECT_EQ(failedSend.clientRequestSequence, 6u);
    EXPECT_EQ(runtime.nextClientRequestSequence(), 6u);
    EXPECT_EQ(runtime.telemetry().cargoRequestsSent, 4u);
    EXPECT_EQ(runtime.telemetry().sendFailures, 1u);

    fake->allowSend = true;
    const WreckwaterClientActionSendResult retriedSend =
        runtime.sendCargoAction(
            41u, WreckwaterAction::Tow, 1u, 1u, 1u);
    ASSERT_TRUE(retriedSend);
    EXPECT_EQ(retriedSend.clientRequestSequence, 6u);
    EXPECT_EQ(runtime.nextClientRequestSequence(), 7u);
    EXPECT_EQ(runtime.telemetry().cargoRequestsSent, 5u);
    ASSERT_EQ(fake->sent.size(), 6u);
    const WreckwaterActionRequest retried =
        decodedSentAction(fake->sent.back());
    EXPECT_EQ(retried.requestedApplicationTick, 41u);
    EXPECT_EQ(retried.clientRequestSequence, 6u);
}

TEST(
    WreckwaterClientRuntimeTest,
    SendsCharacterAndHelmForSameTickWithOneOrderedSequence) {
    const WreckwaterClientRuntime::Config config = runtimeConfig();
    auto transport = std::make_unique<FakeRuntimeTransport>();
    FakeRuntimeTransport* fake = transport.get();
    WreckwaterClientRuntime runtime(config, std::move(transport));

    ASSERT_TRUE(runtime.sendHelm(7u, 12'000, -4'000));
    const WreckwaterClientActionSendResult character =
        runtime.sendCharacterInput(
            7u, 0x0001'0001u, 3u,
            20'000, -10'000, true, true);
    ASSERT_TRUE(character);
    EXPECT_EQ(character.clientRequestSequence, 2u);
    EXPECT_EQ(character.characterInputSequence, 1u);
    ASSERT_EQ(fake->sent.size(), 2u);
    EXPECT_EQ(fake->sent[1].delivery, DeliveryClass::Realtime);
    EXPECT_EQ(
        fake->sent[1].bytes.size(),
        kWreckwaterFramedCharacterInputRequestBytes);

    const Packet outer = decodedSentPacket(fake->sent[1]);
    EXPECT_EQ(outer.header.payloadType, PacketPayloadType::Input);
    EXPECT_EQ(outer.header.sequence, 2u);
    EXPECT_EQ(outer.header.tick, 7u);
    const WreckwaterCharacterInputRequest decoded =
        decodedSentCharacterInput(fake->sent[1]);
    EXPECT_EQ(decoded.characterHandle, 0x0001'0001u);
    EXPECT_EQ(decoded.connectionGeneration, 3u);
    EXPECT_EQ(decoded.clientRequestSequence, 2u);
    EXPECT_EQ(decoded.characterInputSequence, 1u);
    EXPECT_EQ(decoded.moveXQ15, 20'000);
    EXPECT_EQ(decoded.moveZQ15, -10'000);
    EXPECT_EQ(
        decoded.inputFlags,
        kWreckwaterCharacterInputJumpFlag |
            kWreckwaterCharacterInputBoardFlag);
    EXPECT_EQ(runtime.telemetry().helmRequestsSent, 1u);
    EXPECT_EQ(runtime.telemetry().characterInputsSent, 1u);
    EXPECT_EQ(runtime.nextClientRequestSequence(), 3u);
    EXPECT_EQ(runtime.nextCharacterInputSequence(), 2u);

    EXPECT_EQ(
        runtime.sendCharacterInput(
            6u, 0x0001'0001u, 3u, 0, 0, false, false)
            .error,
        WreckwaterClientRuntimeError::NonMonotonicRequestTick);
    EXPECT_EQ(
        runtime.sendCharacterInput(
            8u, 0u, 3u, 0, 0, false, false)
            .error,
        WreckwaterClientRuntimeError::CharacterInputEncodeFailed);
    EXPECT_EQ(runtime.nextClientRequestSequence(), 3u);
    EXPECT_EQ(runtime.nextCharacterInputSequence(), 2u);
}

TEST(
    WreckwaterClientRuntimeTest,
    CharacterSequenceIgnoresGlobalTrafficAndResetsOnNewGeneration) {
    const WreckwaterClientRuntime::Config config = runtimeConfig();
    auto transport = std::make_unique<FakeRuntimeTransport>();
    FakeRuntimeTransport* fake = transport.get();
    WreckwaterClientRuntime runtime(config, std::move(transport));

    const WreckwaterClientActionSendResult first =
        runtime.sendCharacterInput(
            1u, 0x0001'0001u, 3u, 1, 2, false, false);
    ASSERT_TRUE(first);
    EXPECT_EQ(first.clientRequestSequence, 1u);
    EXPECT_EQ(first.characterInputSequence, 1u);

    for (uint64_t index = 0u; index < 1'025u; ++index) {
        ASSERT_TRUE(runtime.sendHelm(2u + index, 0, 0));
    }
    fake->allowSend = false;
    const WreckwaterClientActionSendResult failedAfterPause =
        runtime.sendCharacterInput(
            1'027u, 0x0001'0001u, 3u,
            3, 4, false, false);
    EXPECT_EQ(
        failedAfterPause.error,
        WreckwaterClientRuntimeError::TransportSendFailed);
    EXPECT_EQ(failedAfterPause.clientRequestSequence, 1'027u);
    EXPECT_EQ(failedAfterPause.characterInputSequence, 2u);
    EXPECT_EQ(runtime.nextClientRequestSequence(), 1'027u);
    EXPECT_EQ(runtime.nextCharacterInputSequence(), 2u);

    fake->allowSend = true;
    const WreckwaterClientActionSendResult afterPause =
        runtime.sendCharacterInput(
            1'027u, 0x0001'0001u, 3u,
            3, 4, false, false);
    ASSERT_TRUE(afterPause);
    EXPECT_EQ(afterPause.clientRequestSequence, 1'027u);
    EXPECT_EQ(afterPause.characterInputSequence, 2u);
    const WreckwaterCharacterInputRequest paused =
        decodedSentCharacterInput(fake->sent.back());
    EXPECT_EQ(paused.clientRequestSequence, 1'027u);
    EXPECT_EQ(paused.characterInputSequence, 2u);

    const WreckwaterClientActionSendResult reconnected =
        runtime.sendCharacterInput(
            1'028u, 0x0001'0001u, 4u,
            5, 6, false, false);
    ASSERT_TRUE(reconnected);
    EXPECT_EQ(reconnected.clientRequestSequence, 1'028u);
    EXPECT_EQ(reconnected.characterInputSequence, 1u);
    const WreckwaterCharacterInputRequest reset =
        decodedSentCharacterInput(fake->sent.back());
    EXPECT_EQ(reset.characterInputSequence, 1u);
    EXPECT_EQ(runtime.nextClientRequestSequence(), 1'029u);
    EXPECT_EQ(runtime.nextCharacterInputSequence(), 2u);

    EXPECT_EQ(
        runtime.sendCharacterInput(
            1'029u, 0x0001'0001u, 3u,
            0, 0, false, false)
            .codecError,
        WreckwaterCodecError::InvalidIdentity);
    EXPECT_EQ(
        runtime.sendCharacterInput(
            1'029u, 0x0001'0002u, 4u,
            0, 0, false, false)
            .codecError,
        WreckwaterCodecError::InvalidIdentity);
    EXPECT_EQ(runtime.nextClientRequestSequence(), 1'029u);
    EXPECT_EQ(runtime.nextCharacterInputSequence(), 2u);
}

TEST(
    WreckwaterClientRuntimeTest,
    LocalCharacterWaitsForCertifiedIdentityAcrossReconnect) {
    WreckwaterClientRuntime::Config config = runtimeConfig();
    config.localPlayerId = 1u;
    auto firstTransport =
        std::make_unique<FakeRuntimeTransport>();
    FakeRuntimeTransport* firstFake = firstTransport.get();
    WreckwaterClientRuntime runtime(
        config, std::move(firstTransport));

    EXPECT_EQ(
        runtime.localCharacterBindingState(),
        WreckwaterLocalCharacterBindingState::
            AwaitingAuthoritativeGeneration);
    EXPECT_EQ(
        runtime.sendLocalCharacterInput(
            1u, 1, 2, false, false).error,
        WreckwaterClientRuntimeError::CharacterIdentityUnavailable);
    EXPECT_EQ(runtime.nextClientRequestSequence(), 1u);
    EXPECT_EQ(runtime.nextCharacterInputSequence(), 0u);

    firstFake->inbound.push_back(snapshotFrame(
        config, makeSnapshot(config, 1u, 3u, 1u)));
    ASSERT_TRUE(runtime.pump());
    EXPECT_EQ(
        runtime.localCharacterBindingState(),
        WreckwaterLocalCharacterBindingState::Ready);
    EXPECT_EQ(runtime.localCharacterHandle(), 0x0001'0001u);
    EXPECT_EQ(runtime.localCharacterConnectionGeneration(), 1u);
    EXPECT_EQ(runtime.nextCharacterInputSequence(), 1u);

    const WreckwaterClientActionSendResult first =
        runtime.sendLocalCharacterInput(
            4u, 10, -20, true, false);
    ASSERT_TRUE(first);
    EXPECT_EQ(first.clientRequestSequence, 1u);
    EXPECT_EQ(first.characterInputSequence, 1u);
    ASSERT_EQ(firstFake->sent.size(), 1u);
    const WreckwaterCharacterInputRequest firstRequest =
        decodedSentCharacterInput(firstFake->sent[0]);
    EXPECT_EQ(firstRequest.characterHandle, 0x0001'0001u);
    EXPECT_EQ(firstRequest.connectionGeneration, 1u);

    ASSERT_TRUE(runtime.disconnectForTransportReplacement());
    EXPECT_EQ(
        runtime.localCharacterBindingState(),
        WreckwaterLocalCharacterBindingState::
            AwaitingAuthoritativeGeneration);
    EXPECT_EQ(runtime.localCharacterHandle(), 0u);
    EXPECT_EQ(runtime.localCharacterConnectionGeneration(), 0u);
    EXPECT_EQ(runtime.nextCharacterInputSequence(), 0u);

    auto replacement =
        std::make_unique<FakeRuntimeTransport>();
    FakeRuntimeTransport* replacementFake = replacement.get();
    ASSERT_TRUE(runtime.replaceTransport(std::move(replacement)));
    MultiplayerTransportFrame connected = lifecycleFrame(
        config, MultiplayerTransportFrameType::Connected);
    connected.connectionSerial = 78u;
    replacementFake->inbound.push_back(std::move(connected));
    ASSERT_TRUE(runtime.pump());
    EXPECT_EQ(
        runtime.sendLocalCharacterInput(
            5u, 0, 0, false, false).error,
        WreckwaterClientRuntimeError::CharacterIdentityUnavailable);

    MultiplayerTransportFrame staleGeneration = snapshotFrame(
        config, makeSnapshot(config, 2u, 6u, 4u));
    staleGeneration.connectionSerial = 78u;
    replacementFake->inbound.push_back(
        std::move(staleGeneration));
    ASSERT_TRUE(runtime.pump());
    EXPECT_EQ(
        runtime.localCharacterBindingState(),
        WreckwaterLocalCharacterBindingState::
            AwaitingAuthoritativeGeneration);
    EXPECT_EQ(
        runtime.sendLocalCharacterInput(
            7u, 0, 0, false, false).error,
        WreckwaterClientRuntimeError::CharacterIdentityUnavailable);
    EXPECT_EQ(runtime.nextClientRequestSequence(), 2u);

    WreckwaterCertifiedSnapshot newerGeneration =
        makeSnapshot(config, 3u, 9u, 7u);
    newerGeneration.characters[0].connectionGeneration = 2u;
    ASSERT_TRUE(canonicalizeWreckwaterSnapshot(newerGeneration));
    MultiplayerTransportFrame resumed = snapshotFrame(
        config, newerGeneration);
    resumed.connectionSerial = 78u;
    replacementFake->inbound.push_back(std::move(resumed));
    ASSERT_TRUE(runtime.pump());
    EXPECT_EQ(
        runtime.localCharacterBindingState(),
        WreckwaterLocalCharacterBindingState::Ready);
    EXPECT_EQ(runtime.localCharacterConnectionGeneration(), 2u);

    const WreckwaterClientActionSendResult afterReconnect =
        runtime.sendLocalCharacterInput(
            10u, -30, 40, false, true);
    ASSERT_TRUE(afterReconnect);
    EXPECT_EQ(afterReconnect.clientRequestSequence, 2u);
    EXPECT_EQ(afterReconnect.characterInputSequence, 1u);
    ASSERT_EQ(replacementFake->sent.size(), 1u);
    const WreckwaterCharacterInputRequest resumedRequest =
        decodedSentCharacterInput(replacementFake->sent[0]);
    EXPECT_EQ(resumedRequest.characterHandle, 0x0001'0001u);
    EXPECT_EQ(resumedRequest.connectionGeneration, 2u);
    EXPECT_EQ(resumedRequest.characterInputSequence, 1u);
}

TEST(
    WreckwaterClientRuntimeTest,
    LocalRedundancyCoversAlternatingLossAndThreePacketBursts) {
    WreckwaterClientRuntime::Config config = runtimeConfig();
    config.localPlayerId = 1u;
    auto transport = std::make_unique<FakeRuntimeTransport>();
    FakeRuntimeTransport* fake = transport.get();
    fake->inbound.push_back(snapshotFrame(
        config, makeSnapshot(config, 1u, 3u, 1u)));
    WreckwaterClientRuntime runtime(config, std::move(transport));
    ASSERT_TRUE(runtime.pump());
    ASSERT_EQ(
        runtime.localCharacterBindingState(),
        WreckwaterLocalCharacterBindingState::Ready);

    constexpr uint64_t kLogicalSamples = 10u;
    for (uint64_t sequence = 1u;
         sequence <= kLogicalSamples; ++sequence) {
        const WreckwaterClientActionSendResult sent =
            runtime.sendLocalCharacterInput(
                19u + sequence,
                static_cast<int16_t>(sequence * 100u),
                -static_cast<int16_t>(sequence * 50u),
                sequence == 3u, sequence == 5u);
        ASSERT_TRUE(sent);
        EXPECT_EQ(sent.clientRequestSequence, sequence);
        EXPECT_EQ(sent.characterInputSequence, sequence);

        const Packet outer = decodedSentPacket(fake->sent.back());
        EXPECT_EQ(outer.header.sequence, sequence);
        EXPECT_EQ(outer.header.tick, 19u + sequence);
        const WreckwaterCharacterInputRequest request =
            decodedSentCharacterInput(fake->sent.back());
        const uint32_t expectedRedundancy = static_cast<uint32_t>(
            std::min<uint64_t>(sequence - 1u, 3u));
        EXPECT_EQ(
            request.redundantInputCount, expectedRedundancy);
        EXPECT_EQ(
            request.characterInputSequence, sequence);
        EXPECT_EQ(
            request.requestedApplicationTick, 19u + sequence);
        for (uint32_t index = 0u;
             index < expectedRedundancy; ++index) {
            const uint64_t expectedSequence =
                sequence - expectedRedundancy + index;
            const WreckwaterCharacterInputSample& redundant =
                request.redundantInputs[index];
            EXPECT_EQ(
                redundant.characterInputSequence,
                expectedSequence);
            EXPECT_EQ(
                redundant.requestedApplicationTick,
                19u + expectedSequence);
            EXPECT_EQ(
                redundant.inputFlags,
                (expectedSequence == 3u
                     ? kWreckwaterCharacterInputJumpFlag
                     : 0u)
                    | (expectedSequence == 5u
                           ? kWreckwaterCharacterInputBoardFlag
                           : 0u));
        }
    }
    ASSERT_EQ(fake->sent.size(), kLogicalSamples);

    const auto proveDelivery =
        [&](const auto& delivered) {
            std::array<bool, kLogicalSamples + 1u> observed{};
            uint32_t uniqueJumpCount = 0u;
            uint32_t uniqueBoardCount = 0u;
            for (size_t packetIndex = 0u;
                 packetIndex < fake->sent.size(); ++packetIndex) {
                if (!delivered(packetIndex + 1u)) continue;
                const WreckwaterCharacterInputRequest request =
                    decodedSentCharacterInput(
                        fake->sent[packetIndex]);
                const auto observe =
                    [&](const WreckwaterCharacterInputSample& sample) {
                        ASSERT_LE(
                            sample.characterInputSequence,
                            kLogicalSamples);
                        const size_t index = static_cast<size_t>(
                            sample.characterInputSequence);
                        if (observed[index]) return;
                        observed[index] = true;
                        if ((sample.inputFlags
                             & kWreckwaterCharacterInputJumpFlag)
                            != 0u) {
                            ++uniqueJumpCount;
                        }
                        if ((sample.inputFlags
                             & kWreckwaterCharacterInputBoardFlag)
                            != 0u) {
                            ++uniqueBoardCount;
                        }
                    };
                for (uint32_t index = 0u;
                     index < request.redundantInputCount; ++index) {
                    observe(request.redundantInputs[index]);
                }
                observe({
                    .requestedApplicationTick =
                        request.requestedApplicationTick,
                    .characterInputSequence =
                        request.characterInputSequence,
                    .moveXQ15 = request.moveXQ15,
                    .moveZQ15 = request.moveZQ15,
                    .inputFlags = request.inputFlags,
                });
            }
            for (uint64_t sequence = 1u;
                 sequence <= kLogicalSamples; ++sequence) {
                EXPECT_TRUE(observed[sequence])
                    << "missing logical sample " << sequence;
            }
            EXPECT_EQ(uniqueJumpCount, 1u);
            EXPECT_EQ(uniqueBoardCount, 1u);
        };

    // Packet 1 is dropped, packet 2 is delivered, and so on.
    proveDelivery([](size_t packet) {
        return packet % 2u == 0u;
    });
    // Any logical sample first emitted in packets 3..5 is recovered by
    // packet 6, which still contains the preceding three samples.
    proveDelivery([](size_t packet) {
        return packet < 3u || packet > 5u;
    });

    EXPECT_EQ(runtime.pendingCharacterInputCount(), 3u);
    EXPECT_EQ(
        runtime.telemetry().characterInputSamplesRetransmitted,
        24u);
    EXPECT_EQ(
        runtime.telemetry().
            characterInputSamplesRetiredUnacknowledged,
        7u);
    EXPECT_EQ(
        runtime.telemetry().maximumCharacterInputRedundancy, 3u);

    WreckwaterCertifiedSnapshot acknowledgement =
        makeSnapshot(config, 2u, 32u, 30u);
    acknowledgement.characters[0]
        .lastAppliedCharacterInputSequence = 9u;
    ASSERT_TRUE(canonicalizeWreckwaterSnapshot(acknowledgement));
    fake->inbound.push_back(
        snapshotFrame(config, acknowledgement));
    ASSERT_TRUE(runtime.pump());
    EXPECT_EQ(runtime.pendingCharacterInputCount(), 1u);
    EXPECT_EQ(
        runtime.telemetry().characterInputSamplesAcknowledged,
        2u);

    acknowledgement =
        makeSnapshot(config, 3u, 35u, 33u);
    acknowledgement.characters[0]
        .lastAppliedCharacterInputSequence = 10u;
    ASSERT_TRUE(canonicalizeWreckwaterSnapshot(acknowledgement));
    fake->inbound.push_back(
        snapshotFrame(config, acknowledgement));
    ASSERT_TRUE(runtime.pump());
    EXPECT_EQ(runtime.pendingCharacterInputCount(), 0u);

    ASSERT_TRUE(runtime.sendLocalCharacterInput(
        36u, 1, 2, false, false));
    ASSERT_EQ(runtime.pendingCharacterInputCount(), 1u);
    WreckwaterCertifiedSnapshot nextGeneration =
        makeSnapshot(config, 4u, 38u, 36u);
    nextGeneration.characters[0].connectionGeneration = 2u;
    ASSERT_TRUE(canonicalizeWreckwaterSnapshot(nextGeneration));
    fake->inbound.push_back(
        snapshotFrame(config, nextGeneration));
    ASSERT_TRUE(runtime.pump());
    EXPECT_EQ(runtime.pendingCharacterInputCount(), 0u);
    EXPECT_EQ(runtime.nextCharacterInputSequence(), 1u);
    EXPECT_EQ(
        runtime.telemetry().characterInputHistoryPurges, 1u);
    EXPECT_EQ(
        runtime.telemetry().characterInputHistoryPurgedSamples, 1u);

    const WreckwaterClientActionSendResult restarted =
        runtime.sendLocalCharacterInput(
            39u, 3, 4, false, false);
    ASSERT_TRUE(restarted);
    EXPECT_EQ(restarted.characterInputSequence, 1u);
    const WreckwaterCharacterInputRequest restartedRequest =
        decodedSentCharacterInput(fake->sent.back());
    EXPECT_EQ(restartedRequest.connectionGeneration, 2u);
    EXPECT_EQ(restartedRequest.redundantInputCount, 0u);
}

TEST(
    WreckwaterClientRuntimeTest,
    SequenceExhaustionAndCloseFailClosed) {
    WreckwaterClientRuntime::Config config = runtimeConfig();
    config.firstClientRequestSequence =
        std::numeric_limits<uint64_t>::max();
    auto transport = std::make_unique<FakeRuntimeTransport>();
    FakeRuntimeTransport* fake = transport.get();
    WreckwaterClientRuntime runtime(config, std::move(transport));

    const WreckwaterClientActionSendResult last =
        runtime.sendHelm(1u, 0, 0);
    ASSERT_TRUE(last);
    EXPECT_EQ(
        last.clientRequestSequence,
        std::numeric_limits<uint64_t>::max());
    EXPECT_EQ(runtime.nextClientRequestSequence(), 0u);
    EXPECT_EQ(
        runtime.sendHelm(2u, 0, 0).error,
        WreckwaterClientRuntimeError::RequestSequenceExhausted);

    runtime.close();
    EXPECT_TRUE(fake->closed);
    EXPECT_FALSE(runtime.connectionActive());
    EXPECT_EQ(
        runtime.sendHelm(3u, 0, 0).error,
        WreckwaterClientRuntimeError::NoActiveConnection);
}

TEST(
    WreckwaterClientRuntimeTest,
    CertifiedCharacterSequenceExhaustionFailsClosedUntilNewGeneration) {
    WreckwaterClientRuntime::Config config = runtimeConfig();
    config.localPlayerId = 1u;
    auto transport = std::make_unique<FakeRuntimeTransport>();
    FakeRuntimeTransport* fake = transport.get();
    WreckwaterCertifiedSnapshot exhausted =
        makeSnapshot(config, 1u, 3u, 1u);
    exhausted.characters[0].lastAppliedCharacterInputSequence =
        std::numeric_limits<uint64_t>::max() - 1u;
    ASSERT_TRUE(canonicalizeWreckwaterSnapshot(exhausted));
    fake->inbound.push_back(snapshotFrame(config, exhausted));

    WreckwaterClientRuntime runtime(config, std::move(transport));
    ASSERT_TRUE(runtime.pump());
    EXPECT_EQ(runtime.nextCharacterInputSequence(), 0u);
    EXPECT_EQ(
        runtime.sendLocalCharacterInput(
            4u, 0, 0, true, true).error,
        WreckwaterClientRuntimeError::RequestSequenceExhausted);
    EXPECT_EQ(runtime.nextClientRequestSequence(), 1u);
    EXPECT_TRUE(fake->sent.empty());

    WreckwaterCertifiedSnapshot reconnected =
        makeSnapshot(config, 2u, 6u, 4u);
    reconnected.characters[0].connectionGeneration = 2u;
    ASSERT_TRUE(canonicalizeWreckwaterSnapshot(reconnected));
    fake->inbound.push_back(snapshotFrame(config, reconnected));
    ASSERT_TRUE(runtime.pump());
    EXPECT_EQ(runtime.nextCharacterInputSequence(), 1u);
    const WreckwaterClientActionSendResult first =
        runtime.sendLocalCharacterInput(
            7u, 0, 0, true, true);
    ASSERT_TRUE(first);
    EXPECT_EQ(first.characterInputSequence, 1u);
}

TEST(
    WreckwaterClientRuntimeTest,
    FirstSliceSnapshotAndActionRemainInsideRealtimeMtu) {
    const WreckwaterClientRuntime::Config config = runtimeConfig();
    WreckwaterCertifiedSnapshot snapshot =
        makeSnapshot(config, 1u, 3u, 1u);
    snapshot.entities = {
        makeSkiff(10u, 11u, -10.0f),
        makeSkiff(20u, 21u, 0.0f),
        makeSkiff(30u, 31u, 10.0f),
    };
    EXPECT_TRUE(canonicalizeWreckwaterSnapshot(snapshot));
    const WreckwaterWriteResult inner =
        WreckwaterSnapshotCodec::encode(snapshot);
    ASSERT_TRUE(inner);
    EXPECT_EQ(inner.bytes.size(), kWreckwaterFirstSliceSnapshotBytes);
    const PacketWriteResult outer = PacketCodec::encode(
        makeSnapshotPacket(config, snapshot),
        DeliveryClass::Realtime);
    ASSERT_TRUE(outer.error.empty());
    EXPECT_EQ(
        outer.bytes.size(),
        kWreckwaterFirstSliceSnapshotBytes
            + kNetworkPacketOverheadBytes);
    EXPECT_LE(outer.bytes.size(), kConservativeRealtimeMtu);
    EXPECT_EQ(kWreckwaterFramedActionRequestBytes, 148u);
}

TEST(
    WreckwaterClientRuntimeTest,
    RepeatedTrafficKeepsRuntimeHistoryAtConfiguredCapacity) {
    WreckwaterClientRuntime::Config config = runtimeConfig();
    config.snapshotBuffer.historySnapshots = 4u;
    auto transport = std::make_unique<FakeRuntimeTransport>();
    FakeRuntimeTransport* fake = transport.get();
    WreckwaterClientRuntime runtime(config, std::move(transport));

    constexpr uint64_t iterations = 128u;
    for (uint64_t index = 0u; index < iterations; ++index) {
        const uint64_t evidenceTick = index * 3u;
        fake->inbound.push_back(snapshotFrame(
            config,
            makeSnapshot(
                config, index + 1u, evidenceTick + 2u,
                evidenceTick, static_cast<float>(index))));
        ASSERT_TRUE(runtime.pump());
        ASSERT_TRUE(runtime.sendHelm(evidenceTick + 3u, 1, -1));
        fake->sent.clear();
    }
    EXPECT_EQ(runtime.snapshotBuffer().size(), 4u);
    EXPECT_EQ(runtime.snapshotBuffer().capacity(), 4u);
    EXPECT_EQ(
        runtime.snapshotBuffer().telemetry().historyHighWater, 4u);
    EXPECT_EQ(
        runtime.snapshotBuffer().telemetry().evictedSnapshots,
        iterations - 4u);
    EXPECT_EQ(runtime.telemetry().serviceCalls, iterations);
    EXPECT_EQ(runtime.telemetry().helmRequestsSent, iterations);
    EXPECT_EQ(runtime.telemetry().acceptedSnapshots, iterations);
}

TEST(
    WreckwaterClientRuntimeTest,
    InvalidConfigurationNeverTouchesTransport) {
    WreckwaterClientRuntime::Config config = runtimeConfig();
    config.sessionId = 0u;
    auto transport = std::make_unique<FakeRuntimeTransport>();
    FakeRuntimeTransport* fake = transport.get();
    WreckwaterClientRuntime runtime(config, std::move(transport));
    EXPECT_FALSE(runtime.initialized());
    EXPECT_EQ(
        runtime.pump().lastError,
        WreckwaterClientRuntimeError::InvalidConfiguration);
    EXPECT_EQ(fake->serviceCalls, 0u);
    EXPECT_EQ(
        runtime.sendHelm(1u, 0, 0).error,
        WreckwaterClientRuntimeError::InvalidConfiguration);
    EXPECT_EQ(
        runtime.sendLocalCharacterInput(
            1u, 0, 0, false, false).error,
        WreckwaterClientRuntimeError::InvalidConfiguration);
}

} // namespace
} // namespace voxy::network
