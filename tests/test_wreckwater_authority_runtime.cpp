#include <gtest/gtest.h>

#include "server/wreckwater_authority_runtime.hpp"

#include <algorithm>
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

namespace voxy::server {
namespace {

using network::DeliveryClass;
using network::MultiplayerTransportFrame;
using network::MultiplayerTransportFrameType;
using network::Packet;
using network::PacketCodec;
using network::PacketPayloadType;
using network::WreckwaterAction;
using network::WreckwaterActionRequest;
using network::WreckwaterActionRequestCodec;
using physics::AttachmentHandle;
using physics::BodyHandle;
using physics::PhysicsMutationBatch;
using physics::PhysicsMutationResult;
using physics::PhysicsMutationStatus;
using physics::PreparedPhysicsMutation;

class FakeTransport final : public network::IMultiplayerTransport {
public:
    struct SendAttempt {
        uint32_t peerId = 0u;
        uint64_t serial = 0u;
        DeliveryClass delivery = DeliveryClass::Realtime;
        std::vector<std::byte> bytes;
        bool succeeded = false;
    };

    bool acceptConnection(uint32_t peerId, uint64_t serial) override {
        admissions.emplace_back(peerId, serial);
        return !failAdmission;
    }

    void rejectConnection(uint32_t peerId, uint64_t serial) override {
        rejections.emplace_back(peerId, serial);
    }

    bool failAdmission = false;
    std::vector<std::pair<uint32_t, uint64_t>> admissions;
    std::vector<std::pair<uint32_t, uint64_t>> rejections;

    void service() override { ++serviceCalls; }

    bool send(
        uint32_t peerId, DeliveryClass delivery,
        std::span<const std::byte> bytes) override {
        return send(peerId, 0u, delivery, bytes);
    }

    bool send(
        uint32_t peerId, uint64_t serial,
        DeliveryClass delivery,
        std::span<const std::byte> bytes) override {
        const bool success = peerId != failedPeer;
        attempts.push_back({
            .peerId = peerId,
            .serial = serial,
            .delivery = delivery,
            .bytes = {bytes.begin(), bytes.end()},
            .succeeded = success,
        });
        return success;
    }

    std::optional<MultiplayerTransportFrame> poll() override {
        if (incoming.empty()) return std::nullopt;
        MultiplayerTransportFrame result =
            std::move(incoming.front());
        incoming.pop_front();
        return result;
    }

    void close() override {
        closed = true;
        ++closeCalls;
    }

    void push(MultiplayerTransportFrame frame) {
        incoming.push_back(std::move(frame));
    }

    uint64_t serviceCalls = 0u;
    uint64_t closeCalls = 0u;
    uint32_t failedPeer = 0u;
    bool closed = false;
    std::deque<MultiplayerTransportFrame> incoming;
    std::vector<SendAttempt> attempts;
};

class FakeAuthorityPhysics final : public IWreckwaterAuthorityPhysics {
public:
    bool authorityReady() const noexcept override {
        return ready;
    }

    physics::PhysicsWorld* nativeWorld() noexcept override {
        return nullptr;
    }

    bool enableAuthorityReadbackAndWater(
        float waterHeight) noexcept override {
        configuredWaterHeight = waterHeight;
        configured = ready;
        return configured;
    }

    BodyHandle spawnBody(
        const physics::BodySpawnDesc& desc) override {
        if (!configured) return {};
        size_t slot = spawned.size();
        for (size_t index = 0u; index < allocated.size(); ++index) {
            if (!allocated[index]) {
                slot = index;
                break;
            }
        }
        if (slot == spawned.size()) return {};
        if (generations[slot] == 0u) generations[slot] = 1u;
        const BodyHandle handle{
            static_cast<uint32_t>(slot + 1u), generations[slot]};
        spawned[slot] = {
            .handle = handle,
            .position = desc.position,
            .orientation = desc.orientation,
            .linearVelocity = desc.linearVelocity,
            .angularVelocity = desc.angularVelocity,
            .dimensions = desc.dimensions,
            .material = desc.material.value_or(
                physics::PhysicsMaterial{}),
            .shape = desc.shape,
            .alive = true,
            .awake = true,
            .sector = desc.sector,
        };
        allocated[slot] = true;
        ++spawnCount;
        return handle;
    }

    bool destroyBody(BodyHandle body) override {
        if (!body.valid() || body.index > spawned.size()) return false;
        const size_t slot = body.index - 1u;
        if (!allocated[slot] || spawned[slot].handle != body) {
            return false;
        }
        allocated[slot] = false;
        generations[slot] =
            generations[slot] == std::numeric_limits<uint32_t>::max()
                ? 1u : generations[slot] + 1u;
        spawned[slot].handle = {
            body.index, generations[slot]};
        spawned[slot].alive = false;
        spawned[slot].awake = false;
        spawned[slot].dimensions = {0.0f, 0.0f, 0.0f};
        spawned[slot].linearVelocity = {0.0f, 0.0f, 0.0f};
        spawned[slot].angularVelocity = {0.0f, 0.0f, 0.0f};
        ++destroyCount;
        return true;
    }

    void enqueue(
        std::span<const physics::PhysicsCommand> commands) override {
        pendingCommands.insert(
            pendingCommands.end(),
            commands.begin(), commands.end());
    }

    void serviceAsync() override { ++serviceCalls; }

    WreckwaterAuthorityPhysicsStepResult submitExactTick(
        uint64_t expectedTick, bool requestDebugPose,
        physics::DebugSnapshotRequest debugRequest) override {
        WreckwaterAuthorityPhysicsStepResult result;
        if (stepFailure
            != WreckwaterAuthorityPhysicsStepStatus::Submitted) {
            result.status = stepFailure;
            result.encodedTick = encodedTick;
            return result;
        }
        if (expectedTick != encodedTick + 1u) {
            result.status =
                WreckwaterAuthorityPhysicsStepStatus::
                    EncodedTickMismatch;
            result.encodedTick = encodedTick;
            return result;
        }
        commandHistory.push_back(pendingCommands);
        pendingCommands.clear();
        encodedTick = expectedTick;

        physics::PhysicsEventBatch events;
        if (nextEvents.has_value()) {
            events = std::move(*nextEvents);
            nextEvents.reset();
            if (events.tick == 0u) events.tick = expectedTick;
            for (physics::PhysicsEvent& event : events.events) {
                if (event.tick == 0u) event.tick = events.tick;
            }
        } else {
            events.tick = expectedTick;
        }
        if (!suppressEventReadback) {
            if (eventBatches.size()
                >= kWreckwaterAuthorityReadbackRingSlots) {
                eventRingOverwrite = true;
            } else {
                eventBatches.push_back(std::move(events));
                maximumQueuedEventBatches = std::max(
                    maximumQueuedEventBatches,
                    eventBatches.size());
            }
        }

        if (requestDebugPose && !suppressPoseReadback) {
            physics::DebugSnapshot snapshot;
            if (nextPose.has_value()) {
                snapshot = std::move(*nextPose);
                nextPose.reset();
                if (snapshot.tick == 0u) {
                    snapshot.tick = expectedTick;
                }
            } else {
                snapshot.tick = expectedTick;
                const uint32_t first = debugRequest.firstBody;
                const uint32_t count = debugRequest.bodyCount;
                for (uint32_t offset = 0u; offset < count; ++offset) {
                    const uint32_t bodyIndex = first + offset;
                    if (bodyIndex == 0u
                        || bodyIndex > spawned.size()) {
                        continue;
                    }
                    snapshot.bodies.push_back(
                        spawned[bodyIndex - 1u]);
                }
            }
            poseBatches.push_back(std::move(snapshot));
        }

        result.status =
            WreckwaterAuthorityPhysicsStepStatus::Submitted;
        result.encode = {
            .status = physics::PhysicsEncodeStatus::Encoded,
            .firstTick = expectedTick,
            .finalTick = expectedTick,
            .tickCount = 1u,
        };
        result.encodedTick = encodedTick;
        return result;
    }

    std::optional<physics::PhysicsEventBatch>
    pollEvents() override {
        if (eventBatches.empty()) return std::nullopt;
        if (eventCompletionDelayTicks != 0u
            && (encodedTick < eventBatches.front().tick
                || encodedTick - eventBatches.front().tick
                    < eventCompletionDelayTicks)) {
            return std::nullopt;
        }
        physics::PhysicsEventBatch result =
            std::move(eventBatches.front());
        eventBatches.pop_front();
        polledEventTicks.push_back(result.tick);
        return result;
    }

    std::optional<physics::DebugSnapshot>
    pollDebugSnapshot() override {
        if (poseBatches.empty()) return std::nullopt;
        physics::DebugSnapshot result =
            std::move(poseBatches.front());
        poseBatches.pop_front();
        return result;
    }

    PreparedPhysicsMutation prepare(
        const PhysicsMutationBatch& batch) noexcept override {
        ++prepareCalls;
        preparedDestroyCount = batch.attachmentDestroys.size();
        preparedCreateCount = batch.attachmentCreates.size();
        preparedCreatedCount = preparedCreateCount;
        for (size_t index = 0u; index < preparedCreateCount; ++index) {
            preparedCreated[index] = {
                nextAttachmentIndex++, 1u};
        }
        activeToken = nextToken++;
        return {
            .status = PhysicsMutationStatus::Prepared,
            .token = activeToken,
            .targetTick = encodedTick + 1u,
            .createdAttachments = std::span(
                preparedCreated.data(), preparedCreatedCount),
        };
    }

    PhysicsMutationResult commit(
        const PreparedPhysicsMutation& prepared) noexcept override {
        ++commitCalls;
        if (prepared.token == 0u || prepared.token != activeToken) {
            return {.status = PhysicsMutationStatus::InvalidToken};
        }
        activeToken = 0u;
        if (preparedCreatedCount != 0u) {
            lastCreated = preparedCreated[0];
        }
        return {
            .status = PhysicsMutationStatus::Committed,
            .targetTick = prepared.targetTick,
            .bodyCommandCount = 0u,
            .destroyedAttachmentCount =
                static_cast<uint32_t>(preparedDestroyCount),
            .createdAttachmentCount =
                static_cast<uint32_t>(
                    preparedCreateCount
                    + (malformedCommitCounts ? 1u : 0u)),
        };
    }

    bool discard(
        const PreparedPhysicsMutation& prepared) noexcept override {
        if (prepared.token == 0u || prepared.token != activeToken) {
            return false;
        }
        activeToken = 0u;
        return true;
    }

    const std::vector<physics::PhysicsCommand>* commandsForTick(
        uint64_t tick) const {
        if (tick == 0u || tick > commandHistory.size()) return nullptr;
        return &commandHistory[tick - 1u];
    }

    bool ready = true;
    bool configured = false;
    float configuredWaterHeight = 0.0f;
    uint64_t serviceCalls = 0u;
    uint64_t encodedTick = 0u;
    WreckwaterAuthorityPhysicsStepStatus stepFailure =
        WreckwaterAuthorityPhysicsStepStatus::Submitted;
    std::array<physics::DebugBodyState, 16u> spawned{};
    std::array<bool, 16u> allocated{};
    std::array<uint32_t, 16u> generations{};
    size_t spawnCount = 0u;
    size_t destroyCount = 0u;
    std::vector<physics::PhysicsCommand> pendingCommands;
    std::vector<std::vector<physics::PhysicsCommand>> commandHistory;
    std::deque<physics::PhysicsEventBatch> eventBatches;
    std::deque<physics::DebugSnapshot> poseBatches;
    std::optional<physics::PhysicsEventBatch> nextEvents;
    std::optional<physics::DebugSnapshot> nextPose;
    uint64_t nextToken = 1u;
    uint64_t activeToken = 0u;
    uint32_t prepareCalls = 0u;
    uint32_t commitCalls = 0u;
    bool malformedCommitCounts = false;
    bool suppressEventReadback = false;
    bool suppressPoseReadback = false;
    uint32_t eventCompletionDelayTicks = 0u;
    size_t maximumQueuedEventBatches = 0u;
    bool eventRingOverwrite = false;
    std::vector<uint64_t> polledEventTicks;
    size_t preparedDestroyCount = 0u;
    size_t preparedCreateCount = 0u;
    std::array<AttachmentHandle,
        game::kWreckwaterMaximumIntentsPerTick> preparedCreated{};
    size_t preparedCreatedCount = 0u;
    uint32_t nextAttachmentIndex = 100u;
    AttachmentHandle lastCreated{};
};

WreckwaterAuthorityRuntime::Config runtimeConfig() {
    WreckwaterAuthorityRuntime::Config config;
    config.sessionId = 700u;
    config.matchId = 701u;
    config.worldId = 702u;
    config.worldEpoch = 3u;
    config.authorityEpoch = 5u;
    config.worldEventStreamId = 9u;
    config.match.warmupTicks = 1u;
    config.match.liveTicks = 120u;
    config.match.overtimeTicks = 60u;
    config.match.maximumConnectionEventsPerPlayer = 16u;
    config.match.eventCapacity = 10'000u;
    config.liveWorld.maximumInteractionDistance = 100.0f;
    config.liveWorld.extractionRadius = 100.0f;
    config.liveWorld.extractionCenters[0] = {};
    config.liveWorld.extractionCenters[1] = {};
    config.replay.contentHash = 0x5752'5445'5354'0001ull;
    config.replay.maximumRecords = 16'384u;
    config.replay.maximumPayloadBytes = 4u * 1024u * 1024u;
    return config;
}

MultiplayerTransportFrame lifecycle(
    uint32_t peerId, uint64_t serial,
    MultiplayerTransportFrameType type) {
    return {
        .peerId = peerId,
        .delivery = DeliveryClass::Realtime,
        .bytes = {},
        .type = type,
        .connectionSerial = serial,
    };
}

struct ActionFrameOptions {
    uint32_t peerId = 1u;
    uint64_t serial = 101u;
    uint64_t sequence = 1u;
    uint64_t requestedTick = 1u;
    WreckwaterAction action = WreckwaterAction::Helm;
    DeliveryClass delivery = DeliveryClass::Realtime;
    PacketPayloadType payloadType = PacketPayloadType::Input;
    uint64_t sessionId = 700u;
    uint64_t worldId = 702u;
    uint32_t worldEpoch = 3u;
    uint32_t authorityEpoch = 5u;
    uint32_t cargoId = 1u;
    uint32_t cargoGeneration = 1u;
    uint32_t cargoRevision = 1u;
    int16_t throttle = 0;
    int16_t steering = 0;
};

MultiplayerTransportFrame actionFrame(
    const ActionFrameOptions& options) {
    WreckwaterActionRequest request;
    request.requestedApplicationTick = options.requestedTick;
    request.clientRequestSequence = options.sequence;
    request.action = options.action;
    if (options.action == WreckwaterAction::Helm) {
        request.helmThrottleQ15 = options.throttle;
        request.helmSteeringQ15 = options.steering;
    } else {
        request.cargoId = options.cargoId;
        request.cargoGeneration = options.cargoGeneration;
        request.observedCargoRevision = options.cargoRevision;
    }
    const network::WreckwaterWriteResult payload =
        WreckwaterActionRequestCodec::encode(request);
    EXPECT_TRUE(payload);

    Packet packet;
    packet.header.payloadType = options.payloadType;
    packet.header.sessionId = options.sessionId;
    packet.header.worldId = options.worldId;
    packet.header.worldEpoch = options.worldEpoch;
    packet.header.authorityEpoch = options.authorityEpoch;
    packet.header.sequence = options.sequence;
    packet.header.tick = options.requestedTick;
    packet.payload = payload.bytes;
    const network::PacketWriteResult encoded =
        PacketCodec::encode(packet, options.delivery);
    EXPECT_TRUE(encoded.error.empty());
    return {
        .peerId = options.peerId,
        .delivery = options.delivery,
        .bytes = encoded.bytes,
        .type = MultiplayerTransportFrameType::Data,
        .connectionSerial = options.serial,
    };
}

struct CharacterFrameOptions {
    uint32_t peerId = 1u;
    uint64_t serial = 101u;
    uint64_t sequence = 1u;
    // Zero keeps older test call sites concise by mirroring the outer
    // sequence. Adversarial sequence-domain tests set this explicitly.
    uint64_t characterInputSequence = 0u;
    uint64_t requestedTick = 1u;
    uint32_t characterHandle = 0u;
    uint32_t connectionGeneration = 1u;
    int16_t moveX = 0;
    int16_t moveZ = 0;
    uint32_t inputFlags = 0u;
    uint32_t redundantInputCount = 0u;
    std::array<
        network::WreckwaterCharacterInputSample,
        network::kWreckwaterCharacterInputMaximumRedundantSamples>
        redundantInputs{};
};

MultiplayerTransportFrame characterFrame(
    const CharacterFrameOptions& options) {
    network::WreckwaterCharacterInputRequest request;
    request.requestedApplicationTick = options.requestedTick;
    request.clientRequestSequence = options.sequence;
    request.characterInputSequence =
        options.characterInputSequence == 0u
        ? options.sequence : options.characterInputSequence;
    request.characterHandle = options.characterHandle;
    request.connectionGeneration = options.connectionGeneration;
    request.moveXQ15 = options.moveX;
    request.moveZQ15 = options.moveZ;
    request.inputFlags = options.inputFlags;
    request.redundantInputCount = options.redundantInputCount;
    request.redundantInputs = options.redundantInputs;
    const network::WreckwaterWriteResult payload =
        network::WreckwaterCharacterInputRequestCodec::encode(request);
    EXPECT_TRUE(payload)
        << network::wreckwaterCodecErrorName(payload.error);

    Packet packet;
    packet.header.payloadType = PacketPayloadType::Input;
    packet.header.sessionId = 700u;
    packet.header.worldId = 702u;
    packet.header.worldEpoch = 3u;
    packet.header.authorityEpoch = 5u;
    packet.header.sequence = options.sequence;
    packet.header.tick = options.requestedTick;
    packet.payload = payload.bytes;
    const network::PacketWriteResult encoded =
        PacketCodec::encode(packet, DeliveryClass::Realtime);
    EXPECT_TRUE(encoded.error.empty());
    return {
        .peerId = options.peerId,
        .delivery = DeliveryClass::Realtime,
        .bytes = encoded.bytes,
        .type = MultiplayerTransportFrameType::Data,
        .connectionSerial = options.serial,
    };
}

struct Harness {
    explicit Harness(
        WreckwaterAuthorityRuntime::Config config = runtimeConfig()) {
        auto ownedTransport = std::make_unique<FakeTransport>();
        transport = ownedTransport.get();
        runtime = std::make_unique<WreckwaterAuthorityRuntime>(
            std::move(config), std::move(ownedTransport), physics);
        EXPECT_TRUE(runtime->initialize());
    }

    void queueConnections(uint64_t serialBase = 100u) {
        for (uint32_t peer = 1u;
             peer <= kWreckwaterAuthorityPeerCount; ++peer) {
            transport->push(lifecycle(
                peer, serialBase + peer,
                MultiplayerTransportFrameType::Connected));
        }
    }

    void startAndEstablishEvidence(uint64_t serialBase = 100u) {
        queueConnections(serialBase);
        const WreckwaterAuthorityTickResult first = runtime->tick();
        EXPECT_TRUE(first.authorityStarted);
        EXPECT_TRUE(first.simulationAdvanced);
        EXPECT_EQ(runtime->match().currentTick(), 1u);
        const WreckwaterAuthorityTickResult second = runtime->tick();
        EXPECT_TRUE(second.simulationAdvanced);
        EXPECT_EQ(runtime->latestCertifiedEvidenceTick(), 1u);
        EXPECT_EQ(runtime->eventClosedThroughTick(), 1u);
        EXPECT_EQ(runtime->match().currentTick(), 2u);
    }

    FakeAuthorityPhysics physics;
    FakeTransport* transport = nullptr;
    std::unique_ptr<WreckwaterAuthorityRuntime> runtime;
};

const game::MatchEvent* lastEventOfType(
    const game::WreckwaterMatch& match,
    game::MatchEventType type) {
    const auto events = match.events();
    for (auto iterator = events.rbegin();
         iterator != events.rend(); ++iterator) {
        if (iterator->type == type) return &*iterator;
    }
    return nullptr;
}

TEST(WreckwaterAuthorityRuntime, ExplicitAdmissionStartsAndReconnectsTheMatch) {
    Harness harness;
    for (uint32_t peer = 1u; peer <= 4u; ++peer) {
        harness.transport->push(lifecycle(peer, 100u + peer,
            MultiplayerTransportFrameType::ConnectionRequested));
    }
    const auto started = harness.runtime->tick();
    ASSERT_TRUE(started.authorityStarted);
    ASSERT_TRUE(started.simulationAdvanced);
    ASSERT_EQ(harness.transport->admissions.size(), 4u);
    for (uint32_t peer = 1u; peer <= 4u; ++peer) {
        EXPECT_EQ(harness.transport->admissions[peer - 1u],
            std::make_pair(peer, uint64_t{100u + peer}));
    }
    const auto generation = harness.runtime->peer(1u)->connectionGeneration;
    harness.transport->push(lifecycle(1u, 201u,
        MultiplayerTransportFrameType::ConnectionRequested));
    harness.transport->push(lifecycle(1u, 101u,
        MultiplayerTransportFrameType::Disconnected));
    EXPECT_TRUE(harness.runtime->tick().simulationAdvanced);
    EXPECT_EQ(harness.runtime->peer(1u)->connectionSerial, 201u);
    EXPECT_EQ(harness.runtime->peer(1u)->connectionGeneration, generation + 1u);
    EXPECT_TRUE(harness.runtime->peer(1u)->active);
    harness.transport->push(lifecycle(1u, 201u,
        MultiplayerTransportFrameType::ConnectionRequested));
    harness.transport->push(lifecycle(9u, 209u,
        MultiplayerTransportFrameType::ConnectionRequested));
    EXPECT_TRUE(harness.runtime->tick().simulationAdvanced);
    EXPECT_EQ(harness.transport->admissions.size(), 5u);
    EXPECT_EQ(harness.transport->rejections.size(), 2u);
    EXPECT_EQ(harness.runtime->peer(1u)->connectionGeneration, generation + 1u);
}

TEST(WreckwaterAuthorityRuntime, FailedAdmissionStopsBeforeSimulationAdvances) {
    Harness harness;
    harness.transport->failAdmission = true;
    harness.transport->push(lifecycle(1u, 101u,
        MultiplayerTransportFrameType::ConnectionRequested));
    const auto result = harness.runtime->tick();
    EXPECT_FALSE(result.authorityStarted);
    EXPECT_FALSE(result.simulationAdvanced);
    EXPECT_EQ(harness.runtime->failStopReason(),
        WreckwaterAuthorityFailStopReason::TransportAdmissionFailed);
    EXPECT_EQ(harness.runtime->match().currentTick(), 0u);
    EXPECT_EQ(harness.transport->admissions.size(), 1u);
    EXPECT_EQ(harness.transport->rejections.size(), 1u);
}

TEST(WreckwaterAuthorityRuntime, FreezesUntilAllFourPeersConnect) {
    Harness harness;
    for (uint32_t peer = 1u; peer <= 3u; ++peer) {
        harness.transport->push(lifecycle(
            peer, 100u + peer,
            MultiplayerTransportFrameType::Connected));
    }
    const WreckwaterAuthorityTickResult frozen = harness.runtime->tick();
    EXPECT_FALSE(frozen.authorityStarted);
    EXPECT_FALSE(frozen.simulationAdvanced);
    EXPECT_TRUE(harness.runtime->frozen());
    EXPECT_EQ(harness.runtime->match().currentTick(), 0u);
    EXPECT_EQ(harness.physics.encodedTick, 0u);
    EXPECT_EQ(harness.transport->serviceCalls, 1u);

    harness.transport->push(lifecycle(
        4u, 104u, MultiplayerTransportFrameType::Connected));
    const WreckwaterAuthorityTickResult started = harness.runtime->tick();
    EXPECT_TRUE(started.authorityStarted);
    EXPECT_TRUE(started.simulationAdvanced);
    EXPECT_EQ(harness.runtime->match().currentTick(), 1u);
    EXPECT_EQ(harness.physics.spawnCount, 3u);
    EXPECT_EQ(harness.runtime->liveWorld().entityCount(), 7u);
}

TEST(
    WreckwaterAuthorityRuntime,
    RecordsRosterEventsAndExactPublishedSnapshotBytesWithoutArenaMovement) {
    Harness harness;
    ASSERT_TRUE(harness.runtime->replay().initialized());
    ASSERT_FALSE(harness.runtime->replay().finalized());
    ASSERT_EQ(harness.runtime->match().events().size(), 4u);
    ASSERT_EQ(harness.runtime->replay().records().size(), 4u);
    EXPECT_EQ(harness.runtime->telemetry().replayEventsRecorded, 4u);
    const game::WreckwaterReplayStorageState initialStorage =
        harness.runtime->replay().storageState();

    harness.startAndEstablishEvidence();

    const auto& replay = harness.runtime->replay();
    ASSERT_TRUE(harness.runtime->lastPublishedSnapshot().has_value());
    ASSERT_EQ(
        harness.runtime->telemetry().replayEventsRecorded,
        harness.runtime->match().events().size());
    ASSERT_EQ(
        harness.runtime->telemetry().replaySnapshotsRecorded, 1u);
    ASSERT_EQ(
        replay.records().size(),
        harness.runtime->match().events().size() + 1u);
    EXPECT_EQ(replay.storageState(), initialStorage);
    EXPECT_EQ(
        replay.records().back().type,
        game::WreckwaterReplayRecordType::CertifiedSnapshot);
    const network::WreckwaterSnapshotReadResult decoded =
        network::WreckwaterSnapshotCodec::decode(
            replay.recordPayload(replay.records().size() - 1u));
    ASSERT_TRUE(decoded)
        << network::wreckwaterCodecErrorName(decoded.error);
    EXPECT_EQ(
        *decoded.snapshot,
        *harness.runtime->lastPublishedSnapshot());
    EXPECT_EQ(
        harness.runtime->lastReplayError(),
        game::WreckwaterReplayError::None);
}

TEST(
    WreckwaterAuthorityRuntime,
    CharacterInputClosesOnlyOnExactCertifiedPlatformTick) {
    Harness harness;
    harness.queueConnections();
    const WreckwaterAuthorityTickResult started =
        harness.runtime->tick();
    ASSERT_TRUE(started.simulationAdvanced);
    ASSERT_TRUE(harness.runtime->characterAuthority().initialized());
    const auto peer = harness.runtime->peer(1u);
    ASSERT_TRUE(peer.has_value());
    ASSERT_NE(
        peer->character, game::kInvalidWreckwaterCharacter);
    ASSERT_EQ(peer->connectionGeneration, 1u);

    harness.transport->push(characterFrame({
        .peerId = 1u,
        .serial = 101u,
        .sequence = 1u,
        .requestedTick = 2u,
        .characterHandle = peer->character,
        .connectionGeneration = peer->connectionGeneration,
        .moveX = network::kWreckwaterHelmAxisMaximum,
        .inputFlags =
            network::kWreckwaterCharacterInputJumpFlag,
    }));
    const WreckwaterAuthorityTickResult submitted =
        harness.runtime->tick();
    ASSERT_FALSE(submitted.failStopped);
    ASSERT_EQ(submitted.framesAccepted, 1u);
    EXPECT_EQ(
        harness.runtime->telemetry().acceptedCharacterInputs, 1u);
    const auto* before =
        harness.runtime->characterAuthority().authority().character(
            peer->character);
    ASSERT_NE(before, nullptr);
    EXPECT_EQ(
        before->lastAppliedCharacterInputSequence, 0u);
    EXPECT_EQ(
        harness.runtime->characterAuthority().state()
            .lastCertifiedTick,
        1u);

    const WreckwaterAuthorityTickResult certified =
        harness.runtime->tick();
    ASSERT_FALSE(certified.failStopped)
        << wreckwaterAuthorityFailStopReasonName(
               harness.runtime->failStopReason());
    const auto* after =
        harness.runtime->characterAuthority().authority().character(
            peer->character);
    ASSERT_NE(after, nullptr);
    EXPECT_EQ(
        after->lastAppliedCharacterInputSequence, 1u);
    EXPECT_EQ(after->lastInputTick, 2u);
    EXPECT_EQ(
        harness.runtime->characterAuthority().state()
            .lastCertifiedTick,
        2u);
    EXPECT_EQ(
        harness.runtime->telemetry().certifiedCharacterTicks, 2u);

    ASSERT_TRUE(
        harness.runtime->lastPublishedSnapshot().has_value());
    const auto& snapshot =
        *harness.runtime->lastPublishedSnapshot();
    ASSERT_EQ(
        snapshot.characters.size(),
        game::kWreckwaterMaximumCharacters);
    EXPECT_EQ(
        snapshot.characters.front().characterHandle,
        peer->character);
    const network::WreckwaterWriteResult encoded =
        network::WreckwaterSnapshotCodec::encode(snapshot);
    ASSERT_TRUE(encoded);
    EXPECT_EQ(
        encoded.bytes.size(),
        network::kWreckwaterFirstSliceSnapshotBytes);

    harness.transport->push(characterFrame({
        .peerId = 1u,
        .serial = 101u,
        .sequence = 2u,
        .requestedTick = 4u,
        .characterHandle = peer->character + 1u,
        .connectionGeneration = peer->connectionGeneration,
    }));
    const WreckwaterAuthorityTickResult stale =
        harness.runtime->tick();
    EXPECT_EQ(stale.framesRejected, 1u);
    EXPECT_EQ(
        stale.lastIngressError,
        WreckwaterAuthorityIngressError::
            CharacterIdentityMismatch);
    EXPECT_EQ(
        harness.runtime->telemetry().rejectedCharacterInputs, 1u);
}

TEST(
    WreckwaterAuthorityRuntime,
    SameTickCharacterInputUsesHighestInnerSequence) {
    Harness harness;
    harness.queueConnections();
    ASSERT_TRUE(harness.runtime->tick().simulationAdvanced);
    const auto peer = harness.runtime->peer(1u);
    ASSERT_TRUE(peer.has_value());

    harness.transport->push(characterFrame({
        .peerId = 1u,
        .serial = 101u,
        .sequence = 1u,
        .characterInputSequence = 1u,
        .requestedTick = 2u,
        .characterHandle = peer->character,
        .connectionGeneration = peer->connectionGeneration,
        .moveX = 1'000,
    }));
    harness.transport->push(characterFrame({
        .peerId = 1u,
        .serial = 101u,
        .sequence = 2u,
        .characterInputSequence = 3u,
        .requestedTick = 2u,
        .characterHandle = peer->character,
        .connectionGeneration = peer->connectionGeneration,
        .moveX = 3'000,
    }));
    harness.transport->push(characterFrame({
        .peerId = 1u,
        .serial = 101u,
        .sequence = 3u,
        .characterInputSequence = 2u,
        .requestedTick = 2u,
        .characterHandle = peer->character,
        .connectionGeneration = peer->connectionGeneration,
        .moveX = 2'000,
    }));
    const WreckwaterAuthorityTickResult submitted =
        harness.runtime->tick();
    ASSERT_FALSE(submitted.failStopped);
    EXPECT_EQ(submitted.framesAccepted, 3u);
    EXPECT_EQ(submitted.framesRejected, 0u);
    EXPECT_EQ(
        harness.runtime->telemetry().acceptedCharacterInputs, 2u);
    EXPECT_EQ(
        harness.runtime->telemetry().supersededCharacterInputs, 1u);
    ASSERT_TRUE(harness.runtime->tick().simulationAdvanced);

    const auto* character =
        harness.runtime->characterAuthority().authority().character(
            peer->character);
    ASSERT_NE(character, nullptr);
    EXPECT_EQ(
        character->lastAppliedCharacterInputSequence, 3u);
    ASSERT_TRUE(harness.runtime->peer(1u).has_value());
    EXPECT_EQ(harness.runtime->peer(1u)->latestInboundSequence, 3u);
}

TEST(
    WreckwaterAuthorityRuntime,
    RedundantCharacterInputsConvergeAcrossReorderDuplicatesAndGenerationFence) {
    Harness harness;
    harness.queueConnections();
    ASSERT_TRUE(harness.runtime->tick().simulationAdvanced);
    const auto peer = harness.runtime->peer(1u);
    ASSERT_TRUE(peer.has_value());

    const auto sample = [](
                            uint64_t tick, uint64_t sequence,
                            uint32_t flags = 0u) {
        return network::WreckwaterCharacterInputSample{
            .requestedApplicationTick = tick,
            .characterInputSequence = sequence,
            .moveXQ15 = static_cast<int16_t>(sequence * 1'000u),
            .inputFlags = flags,
        };
    };
    const uint32_t oneShotFlags =
        network::kWreckwaterCharacterInputJumpFlag
        | network::kWreckwaterCharacterInputBoardFlag;

    const MultiplayerTransportFrame older = characterFrame({
        .peerId = 1u,
        .serial = 101u,
        .sequence = 1u,
        .characterInputSequence = 4u,
        .requestedTick = 5u,
        .characterHandle = peer->character,
        .connectionGeneration = peer->connectionGeneration,
        .moveX = 4'000,
        .redundantInputCount = 3u,
        .redundantInputs = {
            sample(2u, 1u, oneShotFlags),
            sample(3u, 2u),
            sample(4u, 3u),
        },
    });
    const MultiplayerTransportFrame newer = characterFrame({
        .peerId = 1u,
        .serial = 101u,
        .sequence = 2u,
        .characterInputSequence = 5u,
        .requestedTick = 6u,
        .characterHandle = peer->character,
        .connectionGeneration = peer->connectionGeneration,
        .moveX = 5'000,
        .redundantInputCount = 3u,
        .redundantInputs = {
            sample(3u, 2u),
            sample(4u, 3u),
            sample(5u, 4u),
        },
    });
    const MultiplayerTransportFrame newest = characterFrame({
        .peerId = 1u,
        .serial = 101u,
        .sequence = 3u,
        .characterInputSequence = 6u,
        .requestedTick = 7u,
        .characterHandle = peer->character,
        .connectionGeneration = peer->connectionGeneration,
        .moveX = 6'000,
        .redundantInputCount = 3u,
        .redundantInputs = {
            sample(4u, 3u),
            sample(5u, 4u),
            sample(6u, 5u),
        },
    });
    const MultiplayerTransportFrame wrongGeneration = characterFrame({
        .peerId = 1u,
        .serial = 101u,
        .sequence = 4u,
        .characterInputSequence = 7u,
        .requestedTick = 8u,
        .characterHandle = peer->character,
        .connectionGeneration =
            peer->connectionGeneration + 1u,
        .moveX = 7'000,
        .redundantInputCount = 1u,
        .redundantInputs = {sample(7u, 6u)},
    });
    const MultiplayerTransportFrame exhaustedInnerSequence =
        characterFrame({
            .peerId = 1u,
            .serial = 101u,
            .sequence = 5u,
            .characterInputSequence =
                std::numeric_limits<uint64_t>::max(),
            .requestedTick = 9u,
            .characterHandle = peer->character,
            .connectionGeneration = peer->connectionGeneration,
            .redundantInputCount = 1u,
            .redundantInputs = {sample(8u, 7u)},
        });

    // Outer packets arrive 2, 1, 3. Inner samples remain canonical and
    // converge even though six exact samples are repeated across packets.
    harness.transport->push(newer);
    harness.transport->push(older);
    harness.transport->push(newest);
    harness.transport->push(exhaustedInnerSequence);
    harness.transport->push(wrongGeneration);
    const WreckwaterAuthorityTickResult ingested =
        harness.runtime->tick();
    ASSERT_FALSE(ingested.failStopped);
    EXPECT_EQ(ingested.framesAccepted, 3u);
    EXPECT_EQ(ingested.framesRejected, 2u);
    EXPECT_EQ(
        ingested.lastIngressError,
        WreckwaterAuthorityIngressError::
            CharacterIdentityMismatch);
    EXPECT_EQ(
        harness.runtime->telemetry().acceptedCharacterInputs, 6u);
    EXPECT_EQ(
        harness.runtime->telemetry().replayedCharacterInputs, 6u);
    EXPECT_EQ(
        harness.runtime->telemetry().characterInputPacketsAccepted,
        3u);
    EXPECT_EQ(
        harness.runtime->telemetry().rejectedCharacterInputs, 2u);

    for (uint32_t quantum = 0u; quantum < 12u; ++quantum) {
        const auto* state =
            harness.runtime->characterAuthority().authority().character(
                peer->character);
        ASSERT_NE(state, nullptr);
        if (state->lastAppliedCharacterInputSequence == 6u) break;
        ASSERT_TRUE(harness.runtime->tick().simulationAdvanced);
    }
    const auto* applied =
        harness.runtime->characterAuthority().authority().character(
            peer->character);
    ASSERT_NE(applied, nullptr);
    EXPECT_EQ(applied->lastAppliedCharacterInputSequence, 6u);
    EXPECT_EQ(
        harness.runtime->characterAuthority().telemetry().inputsApplied,
        6u);

    // A new outer packet may still carry an already-closed one-shot sample.
    // It is expired, while the fresh primary sample remains admissible.
    const uint64_t nextTick =
        harness.runtime->match().currentTick() + 1u;
    harness.transport->push(characterFrame({
        .peerId = 1u,
        .serial = 101u,
        .sequence = 6u,
        .characterInputSequence = 7u,
        .requestedTick = nextTick,
        .characterHandle = peer->character,
        .connectionGeneration = peer->connectionGeneration,
        .moveX = 7'000,
        .redundantInputCount = 1u,
        .redundantInputs = {
            sample(2u, 1u, oneShotFlags),
        },
    }));
    const WreckwaterAuthorityTickResult late =
        harness.runtime->tick();
    ASSERT_FALSE(late.failStopped);
    EXPECT_EQ(late.framesAccepted, 1u);
    EXPECT_EQ(late.framesRejected, 0u);
    EXPECT_EQ(
        harness.runtime->telemetry().acceptedCharacterInputs, 7u);
    EXPECT_EQ(
        harness.runtime->telemetry().expiredCharacterInputs, 1u);
}

TEST(
    WreckwaterAuthorityRuntime,
    CharacterInnerSequenceSurvivesGlobalTrafficAndResetsOnReconnect) {
    const auto config = runtimeConfig();
    Harness harness(config);
    harness.queueConnections();
    ASSERT_TRUE(harness.runtime->tick().simulationAdvanced);
    auto peer = harness.runtime->peer(1u);
    ASSERT_TRUE(peer.has_value());
    ASSERT_EQ(peer->connectionGeneration, 1u);

    uint64_t outerSequence = 1u;
    harness.transport->push(characterFrame({
        .peerId = 1u,
        .serial = 101u,
        .sequence = outerSequence++,
        .characterInputSequence = 1u,
        .requestedTick = harness.runtime->match().currentTick() + 1u,
        .characterHandle = peer->character,
        .connectionGeneration = peer->connectionGeneration,
    }));
    ASSERT_EQ(harness.runtime->tick().framesAccepted, 1u);
    ASSERT_TRUE(harness.runtime->tick().simulationAdvanced);

    uint32_t globalTrafficRemaining = 1'025u;
    while (globalTrafficRemaining != 0u) {
        const uint32_t batch = std::min(
            globalTrafficRemaining, config.maximumFramesPerTick);
        const uint64_t targetTick =
            harness.runtime->match().currentTick() + 1u;
        for (uint32_t index = 0u; index < batch; ++index) {
            harness.transport->push(actionFrame({
                .peerId = 1u,
                .serial = 101u,
                .sequence = outerSequence++,
                .requestedTick = targetTick,
                .action = WreckwaterAction::Helm,
                .delivery = DeliveryClass::Realtime,
                .payloadType = PacketPayloadType::Input,
            }));
        }
        const WreckwaterAuthorityTickResult traffic =
            harness.runtime->tick();
        ASSERT_FALSE(traffic.failStopped);
        ASSERT_EQ(traffic.framesAccepted, batch);
        ASSERT_EQ(traffic.framesRejected, 0u);
        globalTrafficRemaining -= batch;
    }
    ASSERT_GT(outerSequence - 1u, 1'024u);

    harness.transport->push(characterFrame({
        .peerId = 1u,
        .serial = 101u,
        .sequence = outerSequence++,
        .characterInputSequence = 2u,
        .requestedTick = harness.runtime->match().currentTick() + 1u,
        .characterHandle = peer->character,
        .connectionGeneration = peer->connectionGeneration,
    }));
    ASSERT_EQ(harness.runtime->tick().framesAccepted, 1u);
    ASSERT_TRUE(harness.runtime->tick().simulationAdvanced);
    const auto* beforeReconnect =
        harness.runtime->characterAuthority().authority().character(
            peer->character);
    ASSERT_NE(beforeReconnect, nullptr);
    EXPECT_EQ(
        beforeReconnect->lastAppliedCharacterInputSequence, 2u);

    harness.transport->push(lifecycle(
        1u, 101u, MultiplayerTransportFrameType::Disconnected));
    harness.transport->push(lifecycle(
        1u, 201u, MultiplayerTransportFrameType::Connected));
    const WreckwaterAuthorityTickResult lifecycleResult =
        harness.runtime->tick();
    ASSERT_FALSE(lifecycleResult.failStopped);
    ASSERT_EQ(lifecycleResult.framesAccepted, 2u);
    peer = harness.runtime->peer(1u);
    ASSERT_TRUE(peer.has_value());
    ASSERT_EQ(peer->connectionGeneration, 2u);

    const uint64_t reconnectOuterSequence = outerSequence++;
    harness.transport->push(characterFrame({
        .peerId = 1u,
        .serial = 201u,
        .sequence = reconnectOuterSequence,
        .characterInputSequence = 1u,
        .requestedTick = harness.runtime->match().currentTick() + 1u,
        .characterHandle = peer->character,
        .connectionGeneration = peer->connectionGeneration,
    }));
    const WreckwaterAuthorityTickResult reconnectedInput =
        harness.runtime->tick();
    ASSERT_FALSE(reconnectedInput.failStopped);
    ASSERT_EQ(reconnectedInput.framesAccepted, 1u);
    ASSERT_EQ(reconnectedInput.framesRejected, 0u);
    ASSERT_TRUE(harness.runtime->tick().simulationAdvanced);

    const auto* reconnected =
        harness.runtime->characterAuthority().authority().character(
            peer->character);
    ASSERT_NE(reconnected, nullptr);
    EXPECT_EQ(
        reconnected->lastAppliedCharacterInputSequence, 1u);
    bool snapshotAcknowledgedReconnect = false;
    for (uint32_t attempt = 0u;
         attempt < kWreckwaterAuthoritySnapshotIntervalTicks + 1u;
         ++attempt) {
        if (harness.runtime->lastPublishedSnapshot().has_value()) {
            const auto& snapshot =
                *harness.runtime->lastPublishedSnapshot();
            const auto certifiedCharacter = std::find_if(
                snapshot.characters.begin(), snapshot.characters.end(),
                [handle = peer->character](
                    const network::WreckwaterCharacterState& character) {
                    return character.characterHandle == handle;
                });
            snapshotAcknowledgedReconnect =
                certifiedCharacter != snapshot.characters.end()
                && certifiedCharacter->connectionGeneration == 2u
                && certifiedCharacter
                    ->lastAppliedCharacterInputSequence == 1u;
            if (snapshotAcknowledgedReconnect) break;
        }
        ASSERT_TRUE(harness.runtime->tick().simulationAdvanced);
    }
    EXPECT_TRUE(snapshotAcknowledgedReconnect);
    const auto finalPeer = harness.runtime->peer(1u);
    ASSERT_TRUE(finalPeer.has_value());
    EXPECT_EQ(
        finalPeer->latestInboundSequence, reconnectOuterSequence);
}

TEST(
    WreckwaterAuthorityRuntime,
    RejectsUndersizedReplayBeforeSpawningPhysicalBodies) {
    auto config = runtimeConfig();
    config.replay.maximumRecords = 4u;
    config.replay.maximumPayloadBytes =
        4u * game::kWreckwaterReplayMatchEventBytes;
    FakeAuthorityPhysics physics;
    auto transport = std::make_unique<FakeTransport>();
    WreckwaterAuthorityRuntime runtime(
        config, std::move(transport), physics);

    EXPECT_FALSE(runtime.initialize());
    EXPECT_TRUE(runtime.failStopped());
    EXPECT_EQ(
        runtime.failStopReason(),
        WreckwaterAuthorityFailStopReason::InvalidConfiguration);
    EXPECT_EQ(physics.spawnCount, 0u);
    EXPECT_FALSE(runtime.replay().initialized());
}

TEST(
    WreckwaterAuthorityRuntime,
    TerminalExactPoseFinalizesRoundTrippableCertifiedReplay) {
    auto config = runtimeConfig();
    config.match.warmupTicks = 1u;
    config.match.liveTicks = 1u;
    config.match.overtimeTicks = 1u;
    Harness harness(config);
    harness.queueConnections();

    for (uint32_t quantum = 0u;
         quantum < 12u
         && !harness.runtime->replay().finalized()
         && !harness.runtime->failStopped();
         ++quantum) {
        static_cast<void>(harness.runtime->tick());
    }

    ASSERT_FALSE(harness.runtime->failStopped());
    ASSERT_TRUE(harness.runtime->replay().finalized());
    ASSERT_TRUE(harness.runtime->lastPublishedSnapshot().has_value());
    const auto& finalSnapshot =
        *harness.runtime->lastPublishedSnapshot();
    EXPECT_EQ(
        finalSnapshot.physicsEvidenceTick,
        harness.runtime->match().currentTick());
    EXPECT_EQ(
        finalSnapshot.matchStateHash,
        harness.runtime->match().stateHash());
    EXPECT_EQ(
        finalSnapshot.eventStreamHash,
        harness.runtime->match().eventStreamHash());
    EXPECT_EQ(
        harness.runtime->replay().summary().finalMatchStateHash,
        finalSnapshot.matchStateHash);
    EXPECT_EQ(
        harness.runtime->replay().summary().finalEventStreamHash,
        finalSnapshot.eventStreamHash);
    EXPECT_EQ(harness.runtime->telemetry().replayFinalizations, 1u);

    const game::WreckwaterReplayWriteResult encoded =
        game::WreckwaterReplayCodec::encode(
            harness.runtime->replay());
    ASSERT_TRUE(encoded)
        << game::wreckwaterReplayErrorName(encoded.error);
    const game::WreckwaterReplayReadResult decoded =
        game::WreckwaterReplayCodec::decode(encoded.bytes);
    ASSERT_TRUE(decoded)
        << game::wreckwaterReplayErrorName(decoded.error);
    EXPECT_EQ(
        decoded.archive->summary,
        harness.runtime->replay().summary());

    const uint64_t serviceCalls =
        harness.runtime->telemetry().serviceCalls;
    const uint64_t finalTick =
        harness.runtime->match().currentTick();
    const WreckwaterAuthorityTickResult terminal =
        harness.runtime->tick();
    EXPECT_FALSE(terminal.simulationAdvanced);
    EXPECT_FALSE(terminal.failStopped);
    EXPECT_EQ(
        harness.runtime->telemetry().serviceCalls, serviceCalls);
    EXPECT_EQ(harness.runtime->match().currentTick(), finalTick);
}

TEST(
    WreckwaterAuthorityRuntime,
    TerminalGameplayImpactSettlesSinkBeforeFinalReplayCertification) {
    auto config = runtimeConfig();
    config.match.warmupTicks = 1u;
    config.match.liveTicks = 1u;
    config.match.overtimeTicks = 1u;
    config.waterHeight = 10.0f;
    config.damage.edgeBreakImpulse = 1.0f;
    config.damage.breachAreaSquareMetres = 0.5f;
    config.damage.compartmentCapacityCubicMetres = 0.01f;
    config.damage.sinkFloodedFraction = 0.20f;
    config.damage.respawnDelayPhysicsTicks = 60u;
    Harness harness(config);
    harness.queueConnections();

    ASSERT_TRUE(harness.runtime->tick().simulationAdvanced);
    ASSERT_EQ(harness.runtime->match().currentTick(), 1u);

    physics::PhysicsEvent impact;
    impact.type = physics::PhysicsEventType::ContactHit;
    impact.bodyA = 1u;
    impact.bodyGenerationA = 1u;
    impact.bodyB = 3u;
    impact.bodyGenerationB = 1u;
    impact.featureId = 10u;
    impact.otherFeatureId = 20u;
    impact.sourceId = 77u;
    impact.localAnchorA = {-2.0f, -0.3f, 2.0f};
    impact.localAnchorB = {0.0f, 0.0f, 0.0f};
    impact.normalAtoB = {1.0f, 0.0f, 0.0f};
    impact.impulse = 5.0f;
    impact.impactSpeed = 4.0f;
    harness.physics.nextEvents =
        physics::PhysicsEventBatch{.events = {impact}};

    ASSERT_TRUE(harness.runtime->tick().simulationAdvanced);
    ASSERT_EQ(harness.runtime->match().currentTick(), 2u);
    const WreckwaterAuthorityTickResult settlement =
        harness.runtime->tick();
    ASSERT_TRUE(settlement.simulationAdvanced);
    ASSERT_FALSE(settlement.failStopped)
        << wreckwaterAuthorityFailStopReasonName(
               harness.runtime->failStopReason());
    ASSERT_EQ(
        harness.runtime->match().phase(),
        game::MatchPhase::Finished);
    const game::SkiffState* skiff =
        harness.runtime->match().skiff(1u);
    ASSERT_NE(skiff, nullptr);
    EXPECT_EQ(
        skiff->disposition, game::SkiffDisposition::Sunk);
    EXPECT_EQ(harness.runtime->telemetry().skiffsSunk, 1u);

    const WreckwaterAuthorityTickResult certified =
        harness.runtime->tick();
    ASSERT_FALSE(certified.failStopped)
        << wreckwaterAuthorityFailStopReasonName(
               harness.runtime->failStopReason());
    ASSERT_TRUE(harness.runtime->replay().finalized());
    ASSERT_TRUE(
        harness.runtime->lastPublishedSnapshot().has_value());
    const auto& snapshot =
        *harness.runtime->lastPublishedSnapshot();
    EXPECT_EQ(
        snapshot.applicationTick,
        snapshot.physicsEvidenceTick);
    ASSERT_EQ(snapshot.entities.size(), 3u);
    EXPECT_EQ(
        snapshot.entities[0].skiff.disposition,
        network::WreckwaterSkiffDisposition::Sunk);
    EXPECT_NE(
        lastEventOfType(
            harness.runtime->match(),
            game::MatchEventType::SkiffSunk),
        nullptr);
    EXPECT_TRUE(game::WreckwaterReplayCodec::encode(
        harness.runtime->replay()));
}

TEST(
    WreckwaterAuthorityRuntime,
    DerivesActorAndStampsServerTickAndExactEvidence) {
    Harness harness;
    harness.startAndEstablishEvidence();
    harness.transport->push(actionFrame({
        .peerId = 2u,
        .serial = 102u,
        .sequence = 10u,
        .requestedTick = 8u,
        .action = WreckwaterAction::Tow,
        .delivery = DeliveryClass::ReliableEvent,
        .payloadType = PacketPayloadType::Command,
    }));

    const WreckwaterAuthorityTickResult result = harness.runtime->tick();
    EXPECT_EQ(result.framesAccepted, 1u);
    EXPECT_EQ(result.framesRejected, 0u);
    EXPECT_TRUE(result.simulationAdvanced);
    ASSERT_EQ(harness.runtime->liveWorld().towCount(), 1u);
    const game::MatchEvent* event = lastEventOfType(
        harness.runtime->match(),
        game::MatchEventType::CargoTowAttached);
    ASSERT_NE(event, nullptr);
    EXPECT_EQ(event->playerId, 2u);
    EXPECT_EQ(event->crew, game::CrewId::CrewOne);
    EXPECT_EQ(event->targetSkiff, 1u);
    EXPECT_EQ(event->commandTick, 3u);
    EXPECT_EQ(event->sourcePhysicsTick, 2u);
    EXPECT_EQ(event->sourceSequence, 10u);
}

TEST(
    WreckwaterAuthorityRuntime,
    RejectsBadChannelsIdentitySerialAndReplay) {
    Harness harness;
    harness.startAndEstablishEvidence();
    harness.transport->push(actionFrame({
        .peerId = 1u,
        .serial = 101u,
        .sequence = 1u,
        .requestedTick = 3u,
        .action = WreckwaterAction::Helm,
        .delivery = DeliveryClass::ReliableEvent,
        .payloadType = PacketPayloadType::Input,
    }));
    harness.transport->push(actionFrame({
        .peerId = 2u,
        .serial = 102u,
        .sequence = 2u,
        .requestedTick = 3u,
        .action = WreckwaterAction::Tow,
        .delivery = DeliveryClass::Realtime,
        .payloadType = PacketPayloadType::Command,
    }));
    harness.transport->push(actionFrame({
        .peerId = 1u,
        .serial = 101u,
        .sequence = 3u,
        .requestedTick = 3u,
        .action = WreckwaterAction::Helm,
        .sessionId = 999u,
    }));
    harness.transport->push(actionFrame({
        .peerId = 1u,
        .serial = 999u,
        .sequence = 4u,
        .requestedTick = 3u,
        .action = WreckwaterAction::Helm,
    }));
    const MultiplayerTransportFrame valid = actionFrame({
        .peerId = 1u,
        .serial = 101u,
        .sequence = 5u,
        .requestedTick = 3u,
        .action = WreckwaterAction::Helm,
        .throttle = 12'000,
    });
    harness.transport->push(valid);
    harness.transport->push(valid);

    const WreckwaterAuthorityTickResult result = harness.runtime->tick();
    EXPECT_EQ(result.framesPolled, 6u);
    EXPECT_EQ(result.framesAccepted, 1u);
    EXPECT_EQ(result.framesRejected, 5u);
    EXPECT_EQ(
        result.lastIngressError,
        WreckwaterAuthorityIngressError::ReplayedSequence);
    EXPECT_EQ(harness.runtime->telemetry().replayedFrames, 1u);
    EXPECT_EQ(harness.runtime->telemetry().staleSerialFrames, 1u);
    EXPECT_FALSE(harness.runtime->failStopped());
}

TEST(WreckwaterAuthorityRuntime, DeckCannotSendHelm) {
    Harness harness;
    harness.startAndEstablishEvidence();
    harness.transport->push(actionFrame({
        .peerId = 2u,
        .serial = 102u,
        .sequence = 1u,
        .requestedTick = 3u,
        .action = WreckwaterAction::Helm,
        .throttle = 20'000,
    }));
    const WreckwaterAuthorityTickResult result = harness.runtime->tick();
    EXPECT_EQ(result.framesRejected, 1u);
    EXPECT_EQ(
        result.lastIngressError,
        WreckwaterAuthorityIngressError::SeatNotAuthorized);
}

TEST(WreckwaterAuthorityRuntime, RejectsAdvisoryTickOutsideBound) {
    Harness harness;
    harness.startAndEstablishEvidence();
    harness.transport->push(actionFrame({
        .peerId = 1u,
        .serial = 101u,
        .sequence = 1u,
        .requestedTick = 12u,
        .action = WreckwaterAction::Helm,
    }));
    const WreckwaterAuthorityTickResult result = harness.runtime->tick();
    EXPECT_EQ(result.framesAccepted, 0u);
    EXPECT_EQ(result.framesRejected, 1u);
    EXPECT_EQ(
        result.lastIngressError,
        WreckwaterAuthorityIngressError::
            RequestedTickOutOfWindow);
    const auto peer = harness.runtime->peer(1u);
    ASSERT_TRUE(peer.has_value());
    EXPECT_EQ(peer->latestInboundSequence, 0u);
}

TEST(
    WreckwaterAuthorityRuntime,
    ConfiguredRequestedTickLeadAcceptsExactBoundaryOnly) {
    auto config = runtimeConfig();
    config.maximumRequestedTickLead = 16u;
    Harness harness(config);
    harness.startAndEstablishEvidence();
    const uint64_t boundary =
        harness.runtime->match().currentTick() + 1u
        + config.maximumRequestedTickLead;
    harness.transport->push(actionFrame({
        .peerId = 1u,
        .serial = 101u,
        .sequence = 1u,
        .requestedTick = boundary,
        .action = WreckwaterAction::Helm,
    }));
    harness.transport->push(actionFrame({
        .peerId = 3u,
        .serial = 103u,
        .sequence = 1u,
        .requestedTick = boundary + 1u,
        .action = WreckwaterAction::Helm,
    }));
    const WreckwaterAuthorityTickResult result = harness.runtime->tick();
    EXPECT_EQ(result.framesAccepted, 1u);
    EXPECT_EQ(result.framesRejected, 1u);
    EXPECT_EQ(
        result.lastIngressError,
        WreckwaterAuthorityIngressError::
            RequestedTickOutOfWindow);
    ASSERT_TRUE(harness.runtime->peer(1u).has_value());
    ASSERT_TRUE(harness.runtime->peer(3u).has_value());
    EXPECT_EQ(harness.runtime->peer(1u)->latestInboundSequence, 1u);
    EXPECT_EQ(harness.runtime->peer(3u)->latestInboundSequence, 0u);
}

TEST(
    WreckwaterAuthorityRuntime,
    ReconnectReplacesSerialButPreservesReplayHighWater) {
    Harness harness;
    harness.startAndEstablishEvidence();
    harness.transport->push(actionFrame({
        .peerId = 1u,
        .serial = 101u,
        .sequence = 7u,
        .requestedTick = 3u,
        .action = WreckwaterAction::Helm,
        .throttle = 10'000,
    }));
    ASSERT_TRUE(harness.runtime->tick().simulationAdvanced);
    const auto before = harness.runtime->peer(1u);
    ASSERT_TRUE(before.has_value());
    EXPECT_EQ(before->connectionGeneration, 1u);
    EXPECT_EQ(before->latestInboundSequence, 7u);

    harness.transport->push(lifecycle(
        1u, 901u, MultiplayerTransportFrameType::Connected));
    harness.transport->push(actionFrame({
        .peerId = 1u,
        .serial = 101u,
        .sequence = 8u,
        .requestedTick = 4u,
        .action = WreckwaterAction::Helm,
        .throttle = 10'000,
    }));
    harness.transport->push(actionFrame({
        .peerId = 1u,
        .serial = 901u,
        .sequence = 7u,
        .requestedTick = 4u,
        .action = WreckwaterAction::Helm,
        .throttle = 10'000,
    }));
    harness.transport->push(actionFrame({
        .peerId = 1u,
        .serial = 901u,
        .sequence = 8u,
        .requestedTick = 4u,
        .action = WreckwaterAction::Helm,
        .throttle = 10'000,
    }));
    const WreckwaterAuthorityTickResult result = harness.runtime->tick();
    EXPECT_EQ(result.framesAccepted, 2u);
    EXPECT_EQ(result.framesRejected, 2u);
    const auto after = harness.runtime->peer(1u);
    ASSERT_TRUE(after.has_value());
    EXPECT_EQ(after->connectionSerial, 901u);
    EXPECT_NE(after->connectionId, before->connectionId);
    EXPECT_EQ(after->connectionGeneration, 2u);
    EXPECT_EQ(after->latestInboundSequence, 8u);
}

TEST(WreckwaterAuthorityRuntime, HelmTimesOutInServerTicks) {
    auto config = runtimeConfig();
    config.helmTimeoutTicks = 2u;
    Harness harness(config);
    harness.startAndEstablishEvidence();
    harness.transport->push(actionFrame({
        .peerId = 1u,
        .serial = 101u,
        .sequence = 1u,
        .requestedTick = 3u,
        .action = WreckwaterAction::Helm,
        .throttle = 32'000,
        .steering = 16'000,
    }));
    ASSERT_TRUE(harness.runtime->tick().simulationAdvanced);
    ASSERT_TRUE(harness.runtime->tick().simulationAdvanced);
    ASSERT_TRUE(harness.runtime->tick().simulationAdvanced);

    const auto* tickThree = harness.physics.commandsForTick(3u);
    const auto* tickFour = harness.physics.commandsForTick(4u);
    const auto* tickFive = harness.physics.commandsForTick(5u);
    ASSERT_NE(tickThree, nullptr);
    ASSERT_NE(tickFour, nullptr);
    ASSERT_NE(tickFive, nullptr);
    ASSERT_EQ(tickThree->size(), 4u);
    ASSERT_EQ(tickFour->size(), 4u);
    ASSERT_EQ(tickFive->size(), 4u);
    EXPECT_GT((*tickThree)[0].a.z, 1'000.0f);
    EXPECT_GT((*tickFour)[0].a.z, 1'000.0f);
    EXPECT_EQ((*tickFive)[0].a.z, 0.0f);
    EXPECT_EQ((*tickFive)[1].a.y, 0.0f);
}

TEST(
    WreckwaterAuthorityRuntime,
    SnapshotIsCanonicalMappedUnderMtuAndSendFailureIsIsolated) {
    Harness harness;
    harness.transport->failedPeer = 2u;
    harness.queueConnections();
    ASSERT_TRUE(harness.runtime->tick().simulationAdvanced);
    const WreckwaterAuthorityTickResult snapshotTick =
        harness.runtime->tick();
    EXPECT_EQ(snapshotTick.snapshotSendAttempts, 4u);
    EXPECT_EQ(snapshotTick.snapshotsSent, 3u);
    EXPECT_FALSE(harness.runtime->failStopped());
    EXPECT_EQ(harness.runtime->lastPublishedSnapshotSequence(), 1u);
    EXPECT_EQ(
        harness.runtime->telemetry().snapshotSendFailures, 1u);

    const auto sent = std::find_if(
        harness.transport->attempts.begin(),
        harness.transport->attempts.end(),
        [](const FakeTransport::SendAttempt& attempt) {
            return attempt.peerId == 1u && attempt.succeeded;
        });
    ASSERT_NE(sent, harness.transport->attempts.end());
    EXPECT_EQ(sent->serial, 101u);
    EXPECT_EQ(sent->delivery, DeliveryClass::Realtime);
    EXPECT_LE(
        sent->bytes.size(), network::kConservativeRealtimeMtu);
    const network::PacketReadResult outer =
        PacketCodec::decode(sent->bytes, sent->delivery);
    ASSERT_TRUE(outer.packet.has_value());
    EXPECT_EQ(outer.packet->header.sequence, 1u);
    EXPECT_EQ(outer.packet->header.tick, 1u);
    const network::WreckwaterSnapshotReadResult decoded =
        network::WreckwaterSnapshotCodec::decode(
            outer.packet->payload);
    ASSERT_TRUE(decoded.snapshot.has_value());
    const network::WreckwaterCertifiedSnapshot& snapshot =
        *decoded.snapshot;
    ASSERT_TRUE(
        harness.runtime->lastPublishedSnapshot().has_value());
    EXPECT_EQ(
        *harness.runtime->lastPublishedSnapshot(), snapshot);
    ASSERT_EQ(snapshot.entities.size(), 3u);
    EXPECT_EQ(snapshot.physicsEvidenceTick, 1u);
    EXPECT_EQ(snapshot.applicationTick, 1u);
    EXPECT_EQ(snapshot.entities[0].netEntityId, 1u);
    EXPECT_EQ(
        snapshot.entities[0].kind,
        network::WreckwaterEntityKind::Skiff);
    EXPECT_EQ(snapshot.entities[0].skiff.skiffId, 1u);
    EXPECT_EQ(snapshot.entities[1].netEntityId, 2u);
    EXPECT_EQ(snapshot.entities[1].skiff.skiffId, 2u);
    EXPECT_EQ(snapshot.entities[2].netEntityId, 3u);
    EXPECT_EQ(
        snapshot.entities[2].kind,
        network::WreckwaterEntityKind::Cargo);
    EXPECT_EQ(snapshot.entities[2].cargo.cargoId, 1u);
    const auto historical = std::find_if(
        harness.runtime->match().events().rbegin(),
        harness.runtime->match().events().rend(),
        [&snapshot](const game::MatchEvent& event) {
            return event.tick == snapshot.applicationTick;
        });
    ASSERT_NE(
        historical, harness.runtime->match().events().rend());
    EXPECT_EQ(snapshot.matchStateHash, historical->stateHash);

    harness.transport->attempts.clear();
    ASSERT_TRUE(harness.runtime->tick().simulationAdvanced);
    ASSERT_TRUE(harness.runtime->tick().simulationAdvanced);
    const WreckwaterAuthorityTickResult secondSnapshot =
        harness.runtime->tick();
    EXPECT_EQ(secondSnapshot.snapshotSendAttempts, 4u);
    EXPECT_EQ(harness.runtime->lastPublishedSnapshotSequence(), 2u);
}

TEST(
    WreckwaterAuthorityRuntime,
    MalformedCommittedMutationCountsPermanentlyFailStop) {
    Harness harness;
    harness.startAndEstablishEvidence();
    harness.physics.malformedCommitCounts = true;
    harness.transport->push(actionFrame({
        .peerId = 2u,
        .serial = 102u,
        .sequence = 10u,
        .requestedTick = 3u,
        .action = WreckwaterAction::Tow,
        .delivery = DeliveryClass::ReliableEvent,
        .payloadType = PacketPayloadType::Command,
    }));
    const WreckwaterAuthorityTickResult failed =
        harness.runtime->tick();
    EXPECT_TRUE(failed.failStopped);
    EXPECT_FALSE(failed.simulationAdvanced);
    EXPECT_EQ(
        harness.runtime->failStopReason(),
        WreckwaterAuthorityFailStopReason::MatchStepFailed);
    EXPECT_EQ(harness.physics.prepareCalls, 1u);
    EXPECT_EQ(harness.physics.commitCalls, 1u);
    EXPECT_EQ(harness.physics.encodedTick, 2u);
    const game::WreckwaterLiveApplyDiagnostic& diagnostic =
        harness.runtime->liveWorld().lastApplyDiagnostic();
    EXPECT_EQ(
        diagnostic.failure,
        game::WreckwaterLiveApplyFailure::CommittedCountMismatch);
    EXPECT_EQ(diagnostic.applicationTick, 3u);
    EXPECT_EQ(
        diagnostic.preparedStatus,
        PhysicsMutationStatus::Prepared);
    EXPECT_EQ(
        diagnostic.committedStatus,
        PhysicsMutationStatus::Committed);
    EXPECT_EQ(diagnostic.requestedCreateCount, 1u);
    EXPECT_EQ(diagnostic.committedCreateCount, 2u);
}

TEST(
    WreckwaterAuthorityRuntime,
    NonSnapshotTickAttachmentBreakUsesExactCertifiedEvidence) {
    Harness harness;
    harness.startAndEstablishEvidence();
    harness.transport->push(actionFrame({
        .peerId = 2u,
        .serial = 102u,
        .sequence = 10u,
        .requestedTick = 3u,
        .action = WreckwaterAction::Tow,
        .delivery = DeliveryClass::ReliableEvent,
        .payloadType = PacketPayloadType::Command,
    }));
    ASSERT_TRUE(harness.runtime->tick().simulationAdvanced);
    ASSERT_EQ(harness.runtime->liveWorld().towCount(), 1u);
    ASSERT_TRUE(harness.physics.lastCreated.valid());

    // Advance through tick 4 without a break. Tick 4 is a 20 Hz publication
    // tick; tick 5 deliberately is not.
    ASSERT_TRUE(harness.runtime->tick().simulationAdvanced);
    physics::PhysicsEvent broken;
    broken.type = physics::PhysicsEventType::AttachmentBreak;
    broken.sourceId = harness.physics.lastCreated.index;
    broken.featureId = harness.physics.lastCreated.generation;
    harness.physics.nextEvents = physics::PhysicsEventBatch{
        .events = {broken},
    };
    harness.physics.suppressPoseReadback = true;
    ASSERT_TRUE(harness.runtime->tick().simulationAdvanced);
    const WreckwaterAuthorityTickResult waiting =
        harness.runtime->tick();
    EXPECT_FALSE(waiting.simulationAdvanced);
    EXPECT_FALSE(waiting.failStopped);
    EXPECT_EQ(harness.runtime->match().currentTick(), 5u);
    EXPECT_EQ(harness.runtime->liveWorld().towCount(), 1u);

    physics::DebugSnapshot delayedPose;
    delayedPose.tick = 5u;
    delayedPose.bodies.assign(
        harness.physics.spawned.begin(),
        harness.physics.spawned.begin()
            + kWreckwaterAuthorityPhysicalBodyCount);
    harness.physics.poseBatches.push_back(
        std::move(delayedPose));
    harness.physics.suppressPoseReadback = false;
    const WreckwaterAuthorityTickResult resolved =
        harness.runtime->tick();
    ASSERT_TRUE(resolved.simulationAdvanced);
    EXPECT_FALSE(resolved.failStopped);

    const game::CargoState* cargo =
        harness.runtime->match().cargo(1u);
    ASSERT_NE(cargo, nullptr);
    EXPECT_EQ(cargo->disposition, game::CargoDisposition::Free);
    EXPECT_EQ(harness.runtime->liveWorld().towCount(), 0u);
    const game::MatchEvent* event = lastEventOfType(
        harness.runtime->match(),
        game::MatchEventType::AttachmentBroken);
    ASSERT_NE(event, nullptr);
    EXPECT_EQ(event->sourcePhysicsTick, 5u);
    EXPECT_EQ(event->commandTick, 6u);
    EXPECT_EQ(event->sourceStreamId, 9u);
    EXPECT_EQ(harness.runtime->latestCertifiedEvidenceTick(), 5u);
    ASSERT_TRUE(
        harness.runtime->lastPublishedSnapshot().has_value());
    EXPECT_EQ(
        harness.runtime->lastPublishedSnapshot()
            ->physicsEvidenceTick,
        4u);
    EXPECT_EQ(
        harness.runtime->lastPublishedSnapshotSequence(), 2u);
    EXPECT_FALSE(harness.runtime->failStopped());
    EXPECT_EQ(
        harness.runtime->liveWorld().lastApplyDiagnostic().failure,
        game::WreckwaterLiveApplyFailure::None);
}

TEST(
    WreckwaterAuthorityRuntime,
    ExactContactFracturesFloodsSinksDestroysAndRespawnsFreshBody) {
    auto config = runtimeConfig();
    config.waterHeight = 10.0f;
    config.damage.edgeBreakImpulse = 1.0f;
    config.damage.breachAreaSquareMetres = 0.5f;
    config.damage.compartmentCapacityCubicMetres = 0.01f;
    config.damage.sinkFloodedFraction = 0.20f;
    config.damage.respawnDelayPhysicsTicks = 2u;
    config.damage.significantFragmentLifetimeTicks = 2u;
    Harness harness(config);
    harness.startAndEstablishEvidence();

    physics::PhysicsEvent impact;
    impact.type = physics::PhysicsEventType::ContactHit;
    impact.bodyA = 1u;
    impact.bodyGenerationA = 1u;
    impact.bodyB = 3u;
    impact.bodyGenerationB = 1u;
    impact.featureId = 10u;
    impact.otherFeatureId = 20u;
    impact.sourceId = 77u;
    impact.localAnchorA = {-2.0f, -0.3f, 2.0f};
    impact.localAnchorB = {0.0f, 0.0f, 0.0f};
    impact.normalAtoB = {1.0f, 0.0f, 0.0f};
    impact.impulse = 5.0f;
    impact.impactSpeed = 4.0f;
    harness.physics.nextEvents =
        physics::PhysicsEventBatch{.events = {impact}};

    ASSERT_TRUE(harness.runtime->tick().simulationAdvanced);
    const WreckwaterAuthorityTickResult sunk =
        harness.runtime->tick();
    ASSERT_TRUE(sunk.simulationAdvanced);
    ASSERT_FALSE(sunk.failStopped)
        << wreckwaterAuthorityFailStopReasonName(
               harness.runtime->failStopReason());
    const game::SkiffState* skiff =
        harness.runtime->match().skiff(1u);
    ASSERT_NE(skiff, nullptr);
    EXPECT_EQ(skiff->disposition, game::SkiffDisposition::Sunk);
    EXPECT_EQ(skiff->generation, 1u);
    EXPECT_FALSE(harness.physics.allocated[0]);
    EXPECT_EQ(harness.physics.spawned[0].handle.index, 1u);
    EXPECT_EQ(harness.physics.spawned[0].handle.generation, 2u);
    EXPECT_EQ(
        harness.runtime->telemetry().structuralEdgesBroken, 1u);
    EXPECT_EQ(harness.runtime->telemetry().breachesOpened, 1u);
    EXPECT_EQ(harness.runtime->telemetry().skiffsSunk, 1u);
    EXPECT_EQ(
        harness.runtime->telemetry().significantFragmentsSpawned,
        1u);
    const auto* forceTick = harness.physics.commandsForTick(4u);
    ASSERT_NE(forceTick, nullptr);
    const auto floodForce = std::find_if(
        forceTick->begin(), forceTick->end(),
        [](const physics::PhysicsCommand& command) {
            return command.type
                == physics::PhysicsCommandType::
                    ApplyForceAtLocalPoint;
        });
    ASSERT_NE(floodForce, forceTick->end());
    EXPECT_LT(floodForce->a.y, 0.0f);
    EXPECT_LT(floodForce->b.x, 0.0f);

    ASSERT_TRUE(harness.runtime->tick().simulationAdvanced);
    const WreckwaterAuthorityTickResult respawned =
        harness.runtime->tick();
    ASSERT_TRUE(respawned.simulationAdvanced);
    ASSERT_FALSE(respawned.failStopped)
        << wreckwaterAuthorityFailStopReasonName(
               harness.runtime->failStopReason());
    skiff = harness.runtime->match().skiff(1u);
    ASSERT_NE(skiff, nullptr);
    EXPECT_EQ(skiff->disposition, game::SkiffDisposition::Active);
    EXPECT_EQ(skiff->generation, 2u);
    EXPECT_TRUE(harness.physics.allocated[0]);
    EXPECT_EQ(harness.physics.spawned[0].handle,
              (physics::BodyHandle{1u, 2u}));
    EXPECT_EQ(
        harness.runtime->vesselDamage().vessels()[0].body,
        (physics::BodyHandle{1u, 2u}));
    EXPECT_EQ(
        harness.runtime->vesselDamage().vessels()[0].lifecycle,
        game::WreckwaterVesselLifecycle::Active);
    EXPECT_EQ(
        harness.runtime->telemetry().significantFragmentsRetired,
        1u);
    EXPECT_EQ(harness.runtime->telemetry().skiffsRespawned, 1u);

    ASSERT_TRUE(harness.runtime->tick().simulationAdvanced);
    ASSERT_TRUE(harness.runtime->tick().simulationAdvanced);
    EXPECT_FALSE(harness.runtime->failStopped());
    ASSERT_TRUE(
        harness.runtime->lastPublishedSnapshot().has_value());
    const auto& snapshot =
        *harness.runtime->lastPublishedSnapshot();
    EXPECT_GE(snapshot.physicsEvidenceTick, 7u);
    ASSERT_EQ(snapshot.entities.size(), 3u);
    EXPECT_EQ(snapshot.entities[0].netGeneration, 2u);
    EXPECT_EQ(snapshot.entities[0].skiff.generation, 2u);
}

TEST(
    WreckwaterAuthorityRuntime,
    SameTickBankAndStealResolveOneWinnerWithoutAtomicAbort) {
    Harness harness;
    harness.startAndEstablishEvidence();
    harness.transport->push(actionFrame({
        .peerId = 2u,
        .serial = 102u,
        .sequence = 10u,
        .requestedTick = 3u,
        .action = WreckwaterAction::Tow,
        .delivery = DeliveryClass::ReliableEvent,
        .payloadType = PacketPayloadType::Command,
    }));
    ASSERT_TRUE(harness.runtime->tick().simulationAdvanced);
    ASSERT_EQ(harness.runtime->liveWorld().towCount(), 1u);

    // Both authenticated deck peers race on the same cargo revision. Match
    // ordering admits one mutation and deterministically rejects the loser.
    harness.transport->push(actionFrame({
        .peerId = 2u,
        .serial = 102u,
        .sequence = 11u,
        .requestedTick = 4u,
        .action = WreckwaterAction::Bank,
        .delivery = DeliveryClass::ReliableEvent,
        .payloadType = PacketPayloadType::Command,
        .cargoRevision = 2u,
    }));
    harness.transport->push(actionFrame({
        .peerId = 4u,
        .serial = 104u,
        .sequence = 1u,
        .requestedTick = 4u,
        .action = WreckwaterAction::Steal,
        .delivery = DeliveryClass::ReliableEvent,
        .payloadType = PacketPayloadType::Command,
        .cargoRevision = 2u,
    }));
    const WreckwaterAuthorityTickResult contested =
        harness.runtime->tick();
    EXPECT_TRUE(contested.simulationAdvanced);
    EXPECT_FALSE(contested.failStopped);
    EXPECT_EQ(
        harness.runtime->match().telemetry().transactionAborts, 0u);
    EXPECT_EQ(
        harness.runtime->match().telemetry().conflictRejections, 1u);
    EXPECT_EQ(harness.runtime->liveWorld().towCount(), 0u);
    const game::CargoState* cargo =
        harness.runtime->match().cargo(1u);
    ASSERT_NE(cargo, nullptr);
    EXPECT_EQ(
        cargo->disposition, game::CargoDisposition::Banked);
    EXPECT_EQ(
        harness.runtime->match().score(game::CrewId::CrewOne),
        runtimeConfig().match.bankScore);
}

TEST(
    WreckwaterAuthorityRuntime,
    EventOverflowAndGapPermanentlyFailStopBeforePublication) {
    {
        Harness harness;
        harness.queueConnections();
        ASSERT_TRUE(harness.runtime->tick().simulationAdvanced);
        ASSERT_FALSE(harness.physics.eventBatches.empty());
        harness.physics.eventBatches.front().overflow = true;
        const WreckwaterAuthorityTickResult failed =
            harness.runtime->tick();
        EXPECT_TRUE(failed.failStopped);
        EXPECT_EQ(
            harness.runtime->failStopReason(),
            WreckwaterAuthorityFailStopReason::
                EventReadbackOverflow);
        EXPECT_EQ(harness.runtime->match().currentTick(), 1u);
        EXPECT_EQ(harness.runtime->lastPublishedSnapshotSequence(), 0u);
    }
    {
        Harness harness;
        harness.queueConnections();
        ASSERT_TRUE(harness.runtime->tick().simulationAdvanced);
        ASSERT_FALSE(harness.physics.eventBatches.empty());
        harness.physics.eventBatches.front().tick = 2u;
        const WreckwaterAuthorityTickResult failed =
            harness.runtime->tick();
        EXPECT_TRUE(failed.failStopped);
        EXPECT_EQ(
            harness.runtime->failStopReason(),
            WreckwaterAuthorityFailStopReason::EventReadbackGap);
    }
}

TEST(WreckwaterAuthorityRuntime, PoseGapPermanentlyFailStops) {
    Harness harness;
    harness.queueConnections();
    ASSERT_TRUE(harness.runtime->tick().simulationAdvanced);
    ASSERT_FALSE(harness.physics.poseBatches.empty());
    harness.physics.poseBatches.front().tick = 2u;
    const WreckwaterAuthorityTickResult failed =
        harness.runtime->tick();
    EXPECT_TRUE(failed.failStopped);
    EXPECT_EQ(
        harness.runtime->failStopReason(),
        WreckwaterAuthorityFailStopReason::PoseReadbackGap);
    EXPECT_EQ(harness.runtime->lastPublishedSnapshotSequence(), 0u);
}

TEST(WreckwaterAuthorityRuntime, MissingEventReadbackTimesOut) {
    auto config = runtimeConfig();
    config.maximumEventReadbackLagTicks = 1u;
    Harness harness(config);
    harness.physics.suppressEventReadback = true;
    harness.queueConnections();
    ASSERT_TRUE(harness.runtime->tick().simulationAdvanced);
    ASSERT_TRUE(harness.runtime->tick().simulationAdvanced);
    const WreckwaterAuthorityTickResult failed =
        harness.runtime->tick();
    EXPECT_TRUE(failed.failStopped);
    EXPECT_EQ(
        harness.runtime->failStopReason(),
        WreckwaterAuthorityFailStopReason::EventReadbackTimeout);
    EXPECT_EQ(harness.physics.encodedTick, 2u);
}

TEST(
    WreckwaterAuthorityRuntime,
    EventReadbackLagNineAndSixteenRecoverInOrderWithoutRingAlias) {
    for (const uint32_t delay : std::array<uint32_t, 2u>{9u, 16u}) {
        SCOPED_TRACE(delay);
        auto config = runtimeConfig();
        config.maximumEventReadbackLagTicks =
            kWreckwaterAuthorityMaximumEventReadbackLagTicks;
        Harness harness(config);
        harness.physics.eventCompletionDelayTicks = delay;
        harness.queueConnections();

        constexpr uint64_t delayedTicks = 96u;
        for (uint64_t tick = 1u; tick <= delayedTicks; ++tick) {
            const WreckwaterAuthorityTickResult result =
                harness.runtime->tick();
            ASSERT_TRUE(result.simulationAdvanced) << tick;
            ASSERT_FALSE(result.failStopped) << tick;
        }
        EXPECT_FALSE(harness.physics.eventRingOverwrite);
        EXPECT_LE(
            harness.physics.maximumQueuedEventBatches,
            static_cast<size_t>(delay + 1u));
        EXPECT_LT(
            harness.physics.maximumQueuedEventBatches,
            static_cast<size_t>(
                kWreckwaterAuthorityReadbackRingSlots));
        ASSERT_FALSE(harness.physics.polledEventTicks.empty());
        for (size_t index = 0u;
             index < harness.physics.polledEventTicks.size(); ++index) {
            EXPECT_EQ(
                harness.physics.polledEventTicks[index],
                index + 1u);
        }

        // Release all outstanding callbacks. The runtime drains at most 16
        // batches per turn, but keeps exact order across that bounded drain.
        harness.physics.eventCompletionDelayTicks = 0u;
        for (uint32_t catchUp = 0u;
             catchUp < 3u
             && harness.runtime->eventClosedThroughTick()
                    < harness.physics.encodedTick;
             ++catchUp) {
            const WreckwaterAuthorityTickResult result =
                harness.runtime->tick();
            ASSERT_TRUE(result.simulationAdvanced);
            ASSERT_FALSE(result.failStopped);
        }
        EXPECT_EQ(
            harness.runtime->eventClosedThroughTick(),
            harness.physics.encodedTick - 1u);
        ASSERT_EQ(
            harness.physics.polledEventTicks.size(),
            harness.runtime->eventClosedThroughTick());
        for (size_t index = 0u;
             index < harness.physics.polledEventTicks.size(); ++index) {
            EXPECT_EQ(
                harness.physics.polledEventTicks[index],
                index + 1u);
        }
        EXPECT_FALSE(harness.physics.eventRingOverwrite);
        EXPECT_FALSE(harness.runtime->failStopped());
    }
}

TEST(
    WreckwaterAuthorityRuntime,
    EventReadbackLagSeventeenFailStopsBeforeThirtyTwoSlotAlias) {
    auto config = runtimeConfig();
    config.maximumEventReadbackLagTicks =
        kWreckwaterAuthorityMaximumEventReadbackLagTicks;
    Harness harness(config);
    harness.physics.eventCompletionDelayTicks =
        kWreckwaterAuthorityMaximumEventReadbackLagTicks + 1u;
    harness.queueConnections();

    for (uint64_t tick = 1u;
         tick
         <= kWreckwaterAuthorityMaximumEventReadbackLagTicks + 1u;
         ++tick) {
        const WreckwaterAuthorityTickResult result =
            harness.runtime->tick();
        ASSERT_TRUE(result.simulationAdvanced) << tick;
        ASSERT_FALSE(result.failStopped) << tick;
    }
    const WreckwaterAuthorityTickResult failed =
        harness.runtime->tick();
    EXPECT_TRUE(failed.failStopped);
    EXPECT_FALSE(failed.simulationAdvanced);
    EXPECT_EQ(
        harness.runtime->failStopReason(),
        WreckwaterAuthorityFailStopReason::EventReadbackTimeout);
    EXPECT_EQ(
        harness.physics.encodedTick,
        kWreckwaterAuthorityMaximumEventReadbackLagTicks + 1u);
    EXPECT_LT(
        harness.physics.maximumQueuedEventBatches,
        static_cast<size_t>(
            kWreckwaterAuthorityReadbackRingSlots));
    EXPECT_FALSE(harness.physics.eventRingOverwrite);
}

TEST(WreckwaterAuthorityRuntime, MissingPoseReadbackTimesOut) {
    auto config = runtimeConfig();
    config.maximumPoseReadbackLagTicks =
        kWreckwaterAuthoritySnapshotIntervalTicks;
    Harness harness(config);
    harness.physics.suppressPoseReadback = true;
    harness.queueConnections();
    for (uint32_t tick = 0u; tick < 5u; ++tick) {
        ASSERT_TRUE(harness.runtime->tick().simulationAdvanced);
    }
    const WreckwaterAuthorityTickResult failed =
        harness.runtime->tick();
    EXPECT_TRUE(failed.failStopped);
    EXPECT_EQ(
        harness.runtime->failStopReason(),
        WreckwaterAuthorityFailStopReason::PoseReadbackTimeout);
    EXPECT_EQ(harness.physics.encodedTick, 5u);
}

TEST(
    WreckwaterAuthorityRuntime,
    ServicesOnceAndDrainsOnlyConfiguredFrameBound) {
    auto config = runtimeConfig();
    config.maximumFramesPerTick = 2u;
    Harness harness(config);
    harness.transport->push(lifecycle(
        9u, 1u, MultiplayerTransportFrameType::Connected));
    harness.transport->push(lifecycle(
        9u, 2u, MultiplayerTransportFrameType::Connected));
    harness.transport->push(lifecycle(
        9u, 3u, MultiplayerTransportFrameType::Connected));
    const WreckwaterAuthorityTickResult first = harness.runtime->tick();
    EXPECT_EQ(harness.transport->serviceCalls, 1u);
    EXPECT_EQ(first.framesPolled, 2u);
    EXPECT_EQ(harness.transport->incoming.size(), 1u);
    const WreckwaterAuthorityTickResult second = harness.runtime->tick();
    EXPECT_EQ(harness.transport->serviceCalls, 2u);
    EXPECT_EQ(second.framesPolled, 1u);
}

} // namespace
} // namespace voxy::server
