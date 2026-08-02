#include "network/deterministic_adversity_transport.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <deque>
#include <iterator>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <utility>
#include <vector>

namespace voxy::network {
namespace {

class ScriptedTransport final : public IMultiplayerTransport {
public:
    struct SentFrame {
        uint32_t peerId = 0u;
        uint64_t connectionSerial = 0u;
        DeliveryClass delivery = DeliveryClass::Realtime;
        std::vector<std::byte> bytes;

        [[nodiscard]] bool operator==(
            const SentFrame&) const = default;
    };

    void service() override { ++serviceCalls; }

    bool send(
        uint32_t peerId, DeliveryClass delivery,
        std::span<const std::byte> bytes) override {
        return accept(
            peerId, 0u, delivery, bytes);
    }

    bool send(
        uint32_t peerId, uint64_t connectionSerial,
        DeliveryClass delivery,
        std::span<const std::byte> bytes) override {
        return accept(
            peerId, connectionSerial, delivery, bytes);
    }

    std::optional<MultiplayerTransportFrame> poll() override {
        if (closed || inbound.empty()) return std::nullopt;
        MultiplayerTransportFrame frame =
            std::move(inbound.front());
        inbound.pop_front();
        return frame;
    }

    void close() override {
        closed = true;
        inbound.clear();
    }

    bool accept(
        uint32_t peerId, uint64_t connectionSerial,
        DeliveryClass delivery,
        std::span<const std::byte> bytes) {
        ++sendCalls;
        if (failSends != 0u) {
            --failSends;
            return false;
        }
        sent.push_back({
            .peerId = peerId,
            .connectionSerial = connectionSerial,
            .delivery = delivery,
            .bytes =
                std::vector<std::byte>(
                    bytes.begin(), bytes.end()),
        });
        return true;
    }

    uint64_t serviceCalls = 0u;
    uint64_t sendCalls = 0u;
    uint32_t failSends = 0u;
    bool closed = false;
    std::deque<MultiplayerTransportFrame> inbound;
    std::vector<SentFrame> sent;
};

struct Harness {
    std::unique_ptr<DeterministicAdversityTransport> adversity;
    ScriptedTransport* underlying = nullptr;
};

DeterministicAdversityTransportConfig testConfig() {
    DeterministicAdversityTransportConfig config;
    config.seed = 0x0123456789abcdefull;
    config.maximumPeers = 2u;
    config.maximumQueuedDataFramesPerPeer = 8u;
    config.maximumQueuedLifecycleFramesPerPeer = 4u;
    config.maximumFrameBytes = 64u;
    config.maximumUnderlyingFramesPerService = 64u;
    config.maximumOutgoingFramesPerService = 64u;
    return config;
}

Harness makeHarness(
    DeterministicAdversityTransportConfig config = testConfig()) {
    auto underlying = std::make_unique<ScriptedTransport>();
    ScriptedTransport* raw = underlying.get();
    std::string error;
    auto adversity = DeterministicAdversityTransport::create(
        std::move(underlying), config, &error);
    EXPECT_NE(adversity, nullptr) << error;
    return {
        .adversity = std::move(adversity),
        .underlying = raw,
    };
}

std::vector<std::byte> payload(uint8_t value) {
    return {
        static_cast<std::byte>(value),
        static_cast<std::byte>(value ^ 0x5au),
    };
}

TEST(DeterministicAdversityTransportTest,
     LatestRealtimeSendSupersedesQueuedAdversityState) {
    auto config = testConfig();
    config.outgoing.minimumDelayServiceQuanta = 4u;
    config.outgoing.maximumDelayServiceQuanta = 4u;
    auto harness = makeHarness(config);
    const auto stale = payload(0x21u);
    const auto latest = payload(0x71u);

    ASSERT_TRUE(harness.adversity->sendLatestRealtime(1u, 9u, stale));
    ASSERT_TRUE(harness.adversity->sendLatestRealtime(1u, 9u, latest));
    EXPECT_EQ(harness.adversity->queuedOutboundFrames(), 1u);
    EXPECT_EQ(
        harness.adversity->telemetry().outboundRealtimeSupersessions, 1u);

    for (uint32_t quantum = 0u; quantum < 5u; ++quantum)
        harness.adversity->service();
    ASSERT_EQ(harness.underlying->sent.size(), 1u);
    EXPECT_EQ(harness.underlying->sent.front().bytes, latest);
}

MultiplayerTransportFrame lifecycle(
    MultiplayerTransportFrameType type, uint64_t serial,
    uint32_t peerId = 0u) {
    return {
        .peerId = peerId,
        .delivery = DeliveryClass::ReliableControl,
        .bytes = {},
        .type = type,
        .connectionSerial = serial,
    };
}

MultiplayerTransportFrame data(
    uint8_t value, DeliveryClass delivery, uint64_t serial,
    uint32_t peerId = 0u) {
    return {
        .peerId = peerId,
        .delivery = delivery,
        .bytes = payload(value),
        .type = MultiplayerTransportFrameType::Data,
        .connectionSerial = serial,
    };
}

std::vector<MultiplayerTransportFrame> drain(
    DeterministicAdversityTransport& transport) {
    std::vector<MultiplayerTransportFrame> frames;
    for (;;) {
        std::optional<MultiplayerTransportFrame> frame =
            transport.poll();
        if (!frame.has_value()) break;
        frames.push_back(std::move(*frame));
    }
    return frames;
}

TEST(
    DeterministicAdversityTransportTest,
    FixedQueueBackpressureAndReliableRetryPreserveBytes) {
    DeterministicAdversityTransportConfig config = testConfig();
    config.maximumQueuedDataFramesPerPeer = 2u;
    config.outgoing.minimumDelayServiceQuanta = 0u;
    config.outgoing.maximumDelayServiceQuanta = 0u;
    Harness harness = makeHarness(config);
    ASSERT_NE(harness.adversity, nullptr);
    harness.underlying->failSends = 1u;

    const std::vector<std::byte> first = payload(1u);
    const std::vector<std::byte> second = payload(2u);
    EXPECT_TRUE(harness.adversity->send(
        7u, 19u, DeliveryClass::ReliableEvent, first));
    EXPECT_TRUE(harness.adversity->send(
        7u, 19u, DeliveryClass::ReliableControl, second));
    EXPECT_FALSE(harness.adversity->send(
        7u, 19u, DeliveryClass::ReliableEvent, payload(3u)));
    EXPECT_EQ(harness.adversity->queuedOutboundFrames(), 2u);
    EXPECT_EQ(
        harness.adversity->telemetry().outboundDataHighWater,
        2u);

    harness.adversity->service();
    EXPECT_EQ(harness.adversity->queuedOutboundFrames(), 2u);
    EXPECT_TRUE(harness.underlying->sent.empty());
    EXPECT_EQ(
        harness.adversity->telemetry().reliableSendRetries,
        1u);

    harness.adversity->service();
    ASSERT_EQ(harness.underlying->sent.size(), 2u);
    EXPECT_EQ(harness.underlying->sent[0].bytes, first);
    EXPECT_EQ(harness.underlying->sent[1].bytes, second);
    EXPECT_EQ(
        harness.underlying->sent[0].connectionSerial, 19u);
    EXPECT_EQ(harness.adversity->queuedOutboundFrames(), 0u);
    EXPECT_FALSE(harness.adversity->faulted());
}

TEST(
    DeterministicAdversityTransportTest,
    LifecycleFencesDelayedDataAndClosePurgesAllDelivery) {
    DeterministicAdversityTransportConfig config = testConfig();
    config.maximumQueuedDataFramesPerPeer = 1u;
    config.incoming.minimumDelayServiceQuanta = 2u;
    config.incoming.maximumDelayServiceQuanta = 2u;
    Harness harness = makeHarness(config);
    ASSERT_NE(harness.adversity, nullptr);

    harness.underlying->inbound.push_back(lifecycle(
        MultiplayerTransportFrameType::Connected, 41u));
    harness.underlying->inbound.push_back(data(
        9u, DeliveryClass::ReliableEvent, 41u));
    harness.underlying->inbound.push_back(lifecycle(
        MultiplayerTransportFrameType::Disconnected, 41u));

    harness.adversity->service();
    EXPECT_TRUE(drain(*harness.adversity).empty());
    harness.adversity->service();
    EXPECT_TRUE(drain(*harness.adversity).empty());
    harness.adversity->service();
    const std::vector<MultiplayerTransportFrame> frames =
        drain(*harness.adversity);
    ASSERT_EQ(frames.size(), 3u);
    EXPECT_EQ(
        frames[0].type,
        MultiplayerTransportFrameType::Connected);
    EXPECT_EQ(
        frames[1].type, MultiplayerTransportFrameType::Data);
    EXPECT_EQ(frames[1].bytes, payload(9u));
    EXPECT_EQ(
        frames[2].type,
        MultiplayerTransportFrameType::Disconnected);
    for (const MultiplayerTransportFrame& frame : frames)
        EXPECT_EQ(frame.connectionSerial, 41u);

    harness.underlying->inbound.push_back(lifecycle(
        MultiplayerTransportFrameType::Connected, 42u));
    harness.adversity->service();
    EXPECT_GT(harness.adversity->queuedInboundFrames(), 0u);
    harness.adversity->close();
    EXPECT_TRUE(harness.adversity->closed());
    EXPECT_TRUE(harness.underlying->closed);
    EXPECT_FALSE(harness.adversity->poll().has_value());
    harness.adversity->service();
    EXPECT_FALSE(harness.adversity->poll().has_value());
    EXPECT_GT(
        harness.adversity->telemetry().closePurgedFrames, 0u);
}

TEST(
    DeterministicAdversityTransportTest,
    NewConnectionSerialPurgesStaleDelayedFrames) {
    DeterministicAdversityTransportConfig config = testConfig();
    config.incoming.minimumDelayServiceQuanta = 3u;
    config.incoming.maximumDelayServiceQuanta = 3u;
    config.outgoing.minimumDelayServiceQuanta = 3u;
    config.outgoing.maximumDelayServiceQuanta = 3u;
    Harness harness = makeHarness(config);
    ASSERT_NE(harness.adversity, nullptr);

    harness.underlying->inbound.push_back(lifecycle(
        MultiplayerTransportFrameType::Connected, 10u));
    harness.underlying->inbound.push_back(data(
        1u, DeliveryClass::Realtime, 10u));
    harness.adversity->service();
    EXPECT_TRUE(harness.adversity->send(
        0u, 10u, DeliveryClass::ReliableEvent, payload(2u)));
    EXPECT_EQ(harness.adversity->queuedInboundFrames(), 2u);
    EXPECT_EQ(harness.adversity->queuedOutboundFrames(), 1u);

    harness.underlying->inbound.push_back(lifecycle(
        MultiplayerTransportFrameType::Connected, 11u));
    harness.underlying->inbound.push_back(data(
        3u, DeliveryClass::Realtime, 11u));
    harness.adversity->service();
    EXPECT_GE(
        harness.adversity->telemetry().staleFramesPurged, 3u);
    EXPECT_EQ(harness.adversity->queuedOutboundFrames(), 0u);

    harness.adversity->service();
    harness.adversity->service();
    harness.adversity->service();
    const std::vector<MultiplayerTransportFrame> frames =
        drain(*harness.adversity);
    ASSERT_EQ(frames.size(), 2u);
    EXPECT_EQ(
        frames[0].type,
        MultiplayerTransportFrameType::Connected);
    EXPECT_EQ(frames[0].connectionSerial, 11u);
    EXPECT_EQ(frames[1].connectionSerial, 11u);
    EXPECT_EQ(frames[1].bytes, payload(3u));
    EXPECT_TRUE(harness.underlying->sent.empty());
}

TEST(
    DeterministicAdversityTransportTest,
    RealtimeMayDropWhileReliableDataIsRetained) {
    DeterministicAdversityTransportConfig config = testConfig();
    config.incoming.realtimeDropPermille = 1'000u;
    config.outgoing.realtimeDropPermille = 1'000u;
    Harness harness = makeHarness(config);
    ASSERT_NE(harness.adversity, nullptr);

    harness.underlying->inbound.push_back(lifecycle(
        MultiplayerTransportFrameType::Connected, 77u));
    harness.underlying->inbound.push_back(data(
        1u, DeliveryClass::Realtime, 77u));
    harness.underlying->inbound.push_back(data(
        2u, DeliveryClass::ReliableEvent, 77u));
    harness.adversity->service();
    const std::vector<MultiplayerTransportFrame> frames =
        drain(*harness.adversity);
    ASSERT_EQ(frames.size(), 2u);
    EXPECT_EQ(
        frames[0].type,
        MultiplayerTransportFrameType::Connected);
    EXPECT_EQ(frames[1].bytes, payload(2u));
    EXPECT_EQ(
        harness.adversity->telemetry().inboundRealtimeDrops,
        1u);

    EXPECT_TRUE(harness.adversity->send(
        0u, 77u, DeliveryClass::Realtime, payload(3u)));
    EXPECT_TRUE(harness.adversity->send(
        0u, 77u, DeliveryClass::ReliableEvent, payload(4u)));
    harness.adversity->service();
    ASSERT_EQ(harness.underlying->sent.size(), 1u);
    EXPECT_EQ(harness.underlying->sent[0].bytes, payload(4u));
    EXPECT_EQ(
        harness.adversity->telemetry().outboundRealtimeDrops,
        1u);
}

TEST(
    DeterministicAdversityTransportTest,
    OptionalTerminalHoldbackConvergesBeforeDisconnect) {
    DeterministicAdversityTransportConfig config = testConfig();
    config.incoming.realtimeDropPermille = 1'000u;
    config.preserveLastRealtimeBeforeDisconnect = true;
    Harness harness = makeHarness(config);
    ASSERT_NE(harness.adversity, nullptr);

    harness.underlying->inbound.push_back(lifecycle(
        MultiplayerTransportFrameType::Connected, 88u));
    harness.underlying->inbound.push_back(data(
        9u, DeliveryClass::Realtime, 88u));
    harness.underlying->inbound.push_back(lifecycle(
        MultiplayerTransportFrameType::Disconnected, 88u));
    harness.adversity->service();

    const std::vector<MultiplayerTransportFrame> frames =
        drain(*harness.adversity);
    ASSERT_EQ(frames.size(), 3u);
    EXPECT_EQ(
        frames[0].type,
        MultiplayerTransportFrameType::Connected);
    EXPECT_EQ(frames[1].bytes, payload(9u));
    EXPECT_EQ(
        frames[2].type,
        MultiplayerTransportFrameType::Disconnected);
    EXPECT_EQ(
        harness.adversity->telemetry().inboundRealtimeDrops,
        1u);
    EXPECT_EQ(
        harness.adversity->telemetry()
            .inboundRealtimeDisconnectRecoveries,
        1u);
    EXPECT_EQ(harness.adversity->queuedInboundFrames(), 0u);
}

struct ReproResult {
    std::vector<ScriptedTransport::SentFrame> sent;
    std::vector<MultiplayerTransportFrame> received;
    DeterministicAdversityTransportTelemetry telemetry;
};

ReproResult runRepro(uint64_t seed) {
    DeterministicAdversityTransportConfig config = testConfig();
    config.seed = seed;
    config.maximumQueuedDataFramesPerPeer = 64u;
    config.incoming.minimumDelayServiceQuanta = 1u;
    config.incoming.maximumDelayServiceQuanta = 5u;
    config.incoming.realtimeDropPermille = 200u;
    config.incoming.realtimeDuplicatePermille = 400u;
    config.incoming.reliableDuplicatePermille = 400u;
    config.incoming.reorderPermille = 1'000u;
    config.outgoing = config.incoming;
    Harness harness = makeHarness(config);
    if (harness.adversity == nullptr) return {};

    harness.underlying->inbound.push_back(lifecycle(
        MultiplayerTransportFrameType::Connected, 90u));
    for (uint8_t index = 1u; index <= 20u; ++index) {
        harness.underlying->inbound.push_back(data(
            index,
            index % 3u == 0u
                ? DeliveryClass::ReliableEvent
                : DeliveryClass::Realtime,
            90u));
        (void)harness.adversity->send(
            0u, 90u,
            index % 4u == 0u
                ? DeliveryClass::ReliableControl
                : DeliveryClass::Realtime,
            payload(static_cast<uint8_t>(index + 40u)));
    }

    ReproResult result;
    for (uint32_t quantum = 0u; quantum < 16u; ++quantum) {
        harness.adversity->service();
        std::vector<MultiplayerTransportFrame> ready =
            drain(*harness.adversity);
        result.received.insert(
            result.received.end(),
            std::make_move_iterator(ready.begin()),
            std::make_move_iterator(ready.end()));
    }
    result.sent = harness.underlying->sent;
    result.telemetry = harness.adversity->telemetry();
    return result;
}

TEST(
    DeterministicAdversityTransportTest,
    SeedProducesByteExactReproducibleSchedule) {
    const ReproResult first = runRepro(0x1111222233334444ull);
    const ReproResult second = runRepro(0x1111222233334444ull);
    ASSERT_FALSE(first.sent.empty());
    ASSERT_FALSE(first.received.empty());
    EXPECT_EQ(first.sent, second.sent);
    EXPECT_EQ(first.received, second.received);
    EXPECT_EQ(first.telemetry, second.telemetry);
    EXPECT_GT(
        first.telemetry.inboundReorders
            + first.telemetry.outboundReorders,
        0u);

    const ReproResult different =
        runRepro(0x5555666677778888ull);
    EXPECT_TRUE(
        first.sent != different.sent
        || first.received != different.received);
}

TEST(
    DeterministicAdversityTransportTest,
    ReliableDuplicateIsExactAndAckWindowRejectsReplay) {
    DeterministicAdversityTransportConfig config = testConfig();
    config.maximumFrameBytes = kConservativeRealtimeMtu;
    config.outgoing.reliableDuplicatePermille = 1'000u;
    config.outgoing.minimumDelayServiceQuanta = 0u;
    config.outgoing.maximumDelayServiceQuanta = 0u;
    Harness harness = makeHarness(config);
    ASSERT_NE(harness.adversity, nullptr);

    Packet packet;
    packet.header.payloadType = PacketPayloadType::Command;
    packet.header.sequence = 71u;
    packet.payload = payload(7u);
    const PacketWriteResult encoded =
        PacketCodec::encode(
            packet, DeliveryClass::ReliableEvent);
    ASSERT_TRUE(encoded.error.empty()) << encoded.error;
    ASSERT_TRUE(harness.adversity->send(
        0u, 123u, DeliveryClass::ReliableEvent,
        encoded.bytes));
    harness.adversity->service();
    ASSERT_EQ(harness.underlying->sent.size(), 2u);
    EXPECT_EQ(
        harness.underlying->sent[0],
        harness.underlying->sent[1]);
    EXPECT_EQ(
        harness.adversity->telemetry()
            .outboundDuplicatesQueued,
        1u);
    EXPECT_EQ(
        harness.adversity->telemetry()
            .outboundReliableDuplicatesQueued,
        1u);

    AckWindow received;
    const PacketReadResult first = PacketCodec::decode(
        harness.underlying->sent[0].bytes,
        DeliveryClass::ReliableEvent);
    const PacketReadResult replay = PacketCodec::decode(
        harness.underlying->sent[1].bytes,
        DeliveryClass::ReliableEvent);
    ASSERT_TRUE(first.packet.has_value()) << first.error;
    ASSERT_TRUE(replay.packet.has_value()) << replay.error;
    EXPECT_TRUE(received.observe(first.packet->header.sequence));
    const uint64_t latest = received.latest();
    const uint64_t bits = received.bits();
    EXPECT_FALSE(received.observe(replay.packet->header.sequence));
    EXPECT_EQ(received.latest(), latest);
    EXPECT_EQ(received.bits(), bits);
    EXPECT_TRUE(received.acknowledges(71u));
}

TEST(
    DeterministicAdversityTransportTest,
    ScheduleSequenceExhaustionNeverWraps) {
    DeterministicAdversityTransportConfig config = testConfig();
    config.firstScheduleSequence =
        std::numeric_limits<uint64_t>::max();
    Harness harness = makeHarness(config);
    ASSERT_NE(harness.adversity, nullptr);

    EXPECT_TRUE(harness.adversity->send(
        0u, DeliveryClass::ReliableEvent, payload(1u)));
    EXPECT_FALSE(harness.adversity->send(
        0u, DeliveryClass::ReliableEvent, payload(2u)));
    harness.adversity->service();
    ASSERT_EQ(harness.underlying->sent.size(), 1u);
    EXPECT_EQ(harness.underlying->sent[0].bytes, payload(1u));
    EXPECT_GE(
        harness.adversity->telemetry()
            .scheduleSequenceExhaustions,
        1u);
    EXPECT_FALSE(harness.adversity->faulted());

    DeterministicAdversityTransportConfig incomingConfig =
        config;
    Harness inbound = makeHarness(incomingConfig);
    ASSERT_NE(inbound.adversity, nullptr);
    inbound.underlying->inbound.push_back(lifecycle(
        MultiplayerTransportFrameType::Connected, 1u));
    inbound.underlying->inbound.push_back(data(
        3u, DeliveryClass::ReliableEvent, 1u));
    inbound.adversity->service();
    EXPECT_TRUE(inbound.adversity->faulted());
    EXPECT_EQ(
        inbound.adversity->lastError(),
        DeterministicAdversityTransportError::
            ScheduleSequenceExhausted);
    EXPECT_TRUE(inbound.adversity->closed());
    EXPECT_FALSE(inbound.adversity->poll().has_value());
}

TEST(
    DeterministicAdversityTransportTest,
    LongRunRemainsWithinFixedCapacity) {
    DeterministicAdversityTransportConfig config = testConfig();
    config.maximumQueuedDataFramesPerPeer = 4u;
    config.incoming.realtimeDropPermille = 1'000u;
    config.outgoing.realtimeDropPermille = 1'000u;
    Harness harness = makeHarness(config);
    ASSERT_NE(harness.adversity, nullptr);

    for (uint32_t index = 0u; index < 100'000u; ++index) {
        EXPECT_TRUE(harness.adversity->send(
            0u, DeliveryClass::Realtime,
            payload(static_cast<uint8_t>(index))));
        harness.adversity->service();
    }
    EXPECT_EQ(harness.adversity->queuedInboundFrames(), 0u);
    EXPECT_EQ(harness.adversity->queuedOutboundFrames(), 0u);
    EXPECT_LE(
        harness.adversity->telemetry().inboundDataHighWater,
        config.maximumQueuedDataFramesPerPeer);
    EXPECT_LE(
        harness.adversity->telemetry().outboundDataHighWater,
        config.maximumQueuedDataFramesPerPeer);
    EXPECT_EQ(
        harness.adversity->telemetry().outboundRealtimeDrops,
        100'000u);
    EXPECT_FALSE(harness.adversity->faulted());
}

} // namespace
} // namespace voxy::network
