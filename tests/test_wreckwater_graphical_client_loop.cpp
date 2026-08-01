#include "client/wreckwater_graphical_client_loop.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <optional>
#include <span>
#include <utility>
#include <vector>

namespace voxy::client {
namespace {

class FakeGraphicalTransport final
    : public network::IMultiplayerTransport {
public:
    struct SentFrame {
        uint32_t peerId = 0u;
        uint64_t connectionSerial = 0u;
        network::DeliveryClass delivery =
            network::DeliveryClass::Realtime;
        std::vector<std::byte> bytes;
    };

    void service() override { ++serviceCalls; }

    bool send(
        uint32_t peerId,
        network::DeliveryClass delivery,
        std::span<const std::byte> bytes) override {
        return send(peerId, 0u, delivery, bytes);
    }

    bool send(
        uint32_t peerId,
        uint64_t connectionSerial,
        network::DeliveryClass delivery,
        std::span<const std::byte> bytes) override {
        ++sendCalls;
        if (failedSendsRemaining != 0u) {
            --failedSendsRemaining;
            return false;
        }
        sent.push_back({
            .peerId = peerId,
            .connectionSerial = connectionSerial,
            .delivery = delivery,
            .bytes = {
                bytes.begin(), bytes.end()},
        });
        return true;
    }

    std::optional<network::MultiplayerTransportFrame>
    poll() override {
        if (inbound.empty()) return std::nullopt;
        network::MultiplayerTransportFrame frame =
            std::move(inbound.front());
        inbound.pop_front();
        return frame;
    }

    void close() override {
        closed = true;
        inbound.clear();
    }

    uint64_t serviceCalls = 0u;
    uint64_t sendCalls = 0u;
    uint32_t failedSendsRemaining = 0u;
    bool closed = false;
    std::deque<network::MultiplayerTransportFrame> inbound;
    std::vector<SentFrame> sent;
};

[[nodiscard]] std::unique_ptr<network::IMultiplayerTransport>
makeFakeTransport(FakeGraphicalTransport*& output) {
    auto transport = std::make_unique<FakeGraphicalTransport>();
    output = transport.get();
    return transport;
}

[[nodiscard]] network::WreckwaterClientRuntime::Config
runtimeConfig() {
    return {
        .serverPeerId = 0u,
        .serverConnectionSerial = 77u,
        .sessionId = 101u,
        .matchId = 202u,
        .worldId = 303u,
        .worldEpoch = 4u,
        .authorityEpoch = 5u,
        .localPlayerId = 1u,
        .maximumFramesPerPump = 16u,
        .firstClientRequestSequence = 1u,
    };
}

[[nodiscard]] WreckwaterGraphicalClientLoop::Config
loopConfig(uint32_t maximumCatchUpTicks = 4u) {
    WreckwaterGraphicalClientLoop::Config result;
    result.maximumCatchUpTicks = maximumCatchUpTicks;
    result.inputLeadTicks = 2u;
    result.controller.localPlayerId = 1u;
    result.controller.inputHistoryTicks = 128u;
    result.controller.stateHistoryTicks = 128u;
    result.controller.platformPredictionTicks = 128u;
    result.controller.movement.waterHeight = -100.0f;
    result.presentation.localPlayerId = 1u;
    result.presentation.roster = {{
        {
            .playerId = 4u,
            .crew = game::CrewId::CrewTwo,
            .crewSlot = 1u,
        },
        {
            .playerId = 2u,
            .crew = game::CrewId::CrewOne,
            .crewSlot = 1u,
        },
        {
            .playerId = 1u,
            .crew = game::CrewId::CrewOne,
            .crewSlot = 0u,
        },
        {
            .playerId = 3u,
            .crew = game::CrewId::CrewTwo,
            .crewSlot = 0u,
        },
    }};
    return result;
}

[[nodiscard]] network::WreckwaterEntityState skiff(
    network::NetEntityId netId,
    uint32_t skiffId,
    float x) {
    network::WreckwaterEntityState result;
    result.netEntityId = netId;
    result.netGeneration = 1u;
    result.kind = network::WreckwaterEntityKind::Skiff;
    result.crew = skiffId == 1u
        ? network::WreckwaterCrew::CrewOne
        : network::WreckwaterCrew::CrewTwo;
    result.localPosition = {x, 0.0f, 0.0f};
    result.orientation = {0.0f, 0.0f, 0.0f, 1.0f};
    result.shape = network::WreckwaterShape::Box;
    result.dimensions = {8.0f, 1.0f, 12.0f};
    result.skiff = {
        .skiffId = skiffId,
        .generation = 1u,
        .disposition =
            network::WreckwaterSkiffDisposition::Active,
    };
    return result;
}

[[nodiscard]] network::WreckwaterCharacterState character(
    uint64_t playerId,
    uint32_t connectionGeneration,
    const physics::WorldPosition& feet,
    const glm::vec3& velocity = glm::vec3(0.0f),
    game::WreckwaterCharacterMode mode =
        game::WreckwaterCharacterMode::OnSkiff,
    bool connected = true) {
    network::WreckwaterCharacterState result;
    result.characterHandle =
        0x0001'0000u + static_cast<uint32_t>(playerId);
    result.stateFlags = static_cast<uint32_t>(mode)
        | network::kWreckwaterCharacterStateActiveFlag
        | (connected
            ? network::kWreckwaterCharacterStateConnectedFlag
            : 0u);
    result.playerId = playerId;
    result.connectionGeneration = connectionGeneration;
    result.sector = {
        feet.sector.x, feet.sector.y, feet.sector.z};
    result.localFeetPosition = {
        feet.local.x, feet.local.y, feet.local.z};
    result.worldVelocity = {
        velocity.x, velocity.y, velocity.z};
    if (mode == game::WreckwaterCharacterMode::OnSkiff) {
        result.skiffId = playerId <= 2u ? 1u : 2u;
        result.skiffGeneration = 1u;
        result.skiffLocalFeetPosition = {
            feet.local.x, 0.5f, feet.local.z};
    }
    return result;
}

[[nodiscard]] network::WreckwaterCertifiedSnapshot snapshot(
    const network::WreckwaterClientRuntime::Config& config,
    uint64_t sequence,
    uint64_t evidenceTick,
    uint32_t localGeneration = 1u,
    const physics::WorldPosition& localFeet =
        physics::worldPositionFromAbsolute({-1.0, 0.5, 0.0}),
    const glm::vec3& localVelocity = glm::vec3(0.0f),
    game::WreckwaterCharacterMode localMode =
        game::WreckwaterCharacterMode::OnSkiff,
    bool localConnected = true) {
    network::WreckwaterCertifiedSnapshot result;
    result.sessionId = config.sessionId;
    result.matchId = config.matchId;
    result.worldId = config.worldId;
    result.worldEpoch = config.worldEpoch;
    result.authorityEpoch = config.authorityEpoch;
    result.snapshotSequence = sequence;
    result.applicationTick = evidenceTick + 2u;
    result.physicsEvidenceTick = evidenceTick;
    result.phase = network::WreckwaterPhase::Live;
    result.matchStateHash =
        static_cast<uint32_t>(0x1000u + sequence);
    result.eventStreamHash =
        static_cast<uint32_t>(0x2000u + sequence);
    result.entities = {
        skiff(10u, 1u, 0.0f),
        skiff(20u, 2u, 20.0f),
    };
    result.characters = {
        character(
            1u, localGeneration, localFeet,
            localVelocity, localMode, localConnected),
        character(
            2u, 1u,
            physics::worldPositionFromAbsolute(
                {1.0, 0.5, 0.0})),
        character(
            3u, 1u,
            physics::worldPositionFromAbsolute(
                {19.0, 0.5, 0.0})),
        character(
            4u, 1u,
            physics::worldPositionFromAbsolute(
                {21.0, 0.5, 0.0})),
    };
    EXPECT_TRUE(
        network::canonicalizeWreckwaterSnapshot(result));
    return result;
}

[[nodiscard]] network::MultiplayerTransportFrame snapshotFrame(
    const network::WreckwaterClientRuntime::Config& config,
    const network::WreckwaterCertifiedSnapshot& source,
    uint64_t connectionSerial) {
    const network::WreckwaterWriteResult inner =
        network::WreckwaterSnapshotCodec::encode(source);
    EXPECT_TRUE(inner)
        << network::wreckwaterCodecErrorName(inner.error);
    network::Packet packet;
    packet.header.payloadType =
        network::PacketPayloadType::Snapshot;
    packet.header.sessionId = config.sessionId;
    packet.header.worldId = config.worldId;
    packet.header.worldEpoch = config.worldEpoch;
    packet.header.authorityEpoch = config.authorityEpoch;
    packet.header.sequence = source.snapshotSequence;
    packet.header.tick = source.applicationTick;
    packet.payload = inner.bytes;
    const network::PacketWriteResult outer =
        network::PacketCodec::encode(
            packet, network::DeliveryClass::Realtime);
    EXPECT_TRUE(outer.error.empty()) << outer.error;
    return {
        .peerId = config.serverPeerId,
        .delivery = network::DeliveryClass::Realtime,
        .bytes = outer.bytes,
        .type = network::MultiplayerTransportFrameType::Data,
        .connectionSerial = connectionSerial,
    };
}

[[nodiscard]] network::MultiplayerTransportFrame lifecycleFrame(
    const network::WreckwaterClientRuntime::Config& config,
    network::MultiplayerTransportFrameType type,
    uint64_t connectionSerial) {
    return {
        .peerId = config.serverPeerId,
        .delivery = network::DeliveryClass::Realtime,
        .bytes = {},
        .type = type,
        .connectionSerial = connectionSerial,
    };
}

[[nodiscard]] network::MultiplayerTransportFrame malformedDataFrame(
    const network::WreckwaterClientRuntime::Config& config,
    uint64_t connectionSerial) {
    return {
        .peerId = config.serverPeerId,
        .delivery = network::DeliveryClass::Realtime,
        .bytes = {std::byte{0x7f}},
        .type = network::MultiplayerTransportFrameType::Data,
        .connectionSerial = connectionSerial,
    };
}

[[nodiscard]] network::WreckwaterCharacterInputRequest
decodeMovement(const FakeGraphicalTransport::SentFrame& frame) {
    const network::PacketReadResult outer =
        network::PacketCodec::decode(
            frame.bytes, frame.delivery);
    EXPECT_TRUE(outer.packet.has_value()) << outer.error;
    if (!outer.packet.has_value()) return {};
    const network::WreckwaterCharacterInputReadResult inner =
        network::WreckwaterCharacterInputRequestCodec::decode(
            outer.packet->payload);
    EXPECT_TRUE(inner.request.has_value())
        << network::wreckwaterCodecErrorName(inner.error);
    return inner.request.value_or(
        network::WreckwaterCharacterInputRequest{});
}

class Harness {
public:
    explicit Harness(
        WreckwaterGraphicalClientLoop::Config requested =
            loopConfig())
        : runtimeConfiguration(runtimeConfig()),
          runtime(
              runtimeConfiguration,
              makeFakeTransport(transport)),
          loopConfiguration(std::move(requested)) {
        EXPECT_TRUE(
            loop.initialize(loopConfiguration, runtime));
    }

    WreckwaterGraphicalClientFrameResult bind(
        const network::WreckwaterCertifiedSnapshot& initial) {
        transport->inbound.push_back(snapshotFrame(
            runtimeConfiguration, initial, 77u));
        return loop.frame({
            .renderTick = {
                .whole = initial.physicsEvidenceTick,
            },
        });
    }

    network::WreckwaterClientRuntime::Config runtimeConfiguration;
    FakeGraphicalTransport* transport = nullptr;
    network::WreckwaterClientRuntime runtime;
    WreckwaterGraphicalClientLoop::Config loopConfiguration;
    WreckwaterGraphicalClientLoop loop;
};

struct CadenceResult {
    physics::WorldPosition feet{};
    uint64_t predictedTick = 0u;
    uint64_t serviceCalls = 0u;
    std::array<
        network::WreckwaterCharacterInputRequest, 60u>
        requests{};
};

[[nodiscard]] CadenceResult runCadence(uint32_t renderHz) {
    Harness harness;
    const auto initial = snapshot(
        harness.runtimeConfiguration, 1u, 10u);
    const auto bound = harness.bind(initial);
    EXPECT_EQ(
        bound.status,
        WreckwaterGraphicalClientFrameStatus::Accepted);
    EXPECT_TRUE(bound.certifiedSampleConsumed);
    EXPECT_TRUE(bound.presentationUpdated);

    const uint64_t base =
        kWreckwaterGraphicalClientNanosecondsPerSecond
        / renderHz;
    const uint64_t remainder =
        kWreckwaterGraphicalClientNanosecondsPerSecond
        % renderHz;
    uint32_t simulated = 0u;
    for (uint32_t frameIndex = 0u;
         frameIndex < renderHz; ++frameIndex) {
        const auto frame = harness.loop.frame({
            .elapsedNanoseconds =
                base + (frameIndex < remainder ? 1u : 0u),
            .renderTick = {.whole = 10u},
            .controls = {
                .movement = {0.75f, 0.25f},
            },
        });
        EXPECT_EQ(
            frame.status,
            WreckwaterGraphicalClientFrameStatus::Accepted)
            << "frame " << frameIndex << " at " << renderHz
            << " Hz: "
            << wreckwaterGraphicalClientFrameStatusName(
                   frame.status);
        simulated += frame.simulationTicks;
    }
    EXPECT_EQ(simulated, 60u);
    EXPECT_EQ(harness.transport->sent.size(), 60u);
    EXPECT_EQ(harness.loop.telemetry().simulationTicks, 60u);

    CadenceResult result;
    const auto pose = harness.loop.controller().predictedPose();
    EXPECT_TRUE(pose);
    if (pose) {
        result.feet = pose.pose.feetPosition;
        result.predictedTick = pose.pose.tick;
    }
    result.serviceCalls = harness.transport->serviceCalls;
    for (size_t index = 0u;
         index < result.requests.size(); ++index) {
        result.requests[index] =
            decodeMovement(harness.transport->sent[index]);
        EXPECT_EQ(
            result.requests[index].characterInputSequence,
            index + 1u);
        EXPECT_EQ(
            result.requests[index].requestedApplicationTick,
            13u + index);
    }
    return result;
}

TEST(
    WreckwaterGraphicalClientLoop,
    OnePumpFeedsOnlyNewestExactSampleAndRendersRequestedTimeline) {
    Harness harness;
    harness.transport->inbound.push_back(snapshotFrame(
        harness.runtimeConfiguration,
        snapshot(
            harness.runtimeConfiguration, 1u, 10u),
        77u));
    harness.transport->inbound.push_back(snapshotFrame(
        harness.runtimeConfiguration,
        snapshot(
            harness.runtimeConfiguration, 2u, 13u),
        77u));

    const auto frame = harness.loop.frame({
        .renderTick = {
            .whole = 11u,
            .fraction = 0.5f,
        },
    });
    EXPECT_EQ(
        frame.status,
        WreckwaterGraphicalClientFrameStatus::Accepted);
    EXPECT_EQ(frame.runtime.snapshotsAccepted, 2u);
    EXPECT_TRUE(frame.certifiedSampleConsumed);
    EXPECT_EQ(
        harness.loop.latestConsumedSnapshotSequence(), 2u);
    EXPECT_EQ(
        harness.loop.controller().authoritativeTick(), 13u);
    EXPECT_EQ(
        harness.loop.controller().telemetry().
            certifiedSamplesAccepted,
        1u);
    EXPECT_EQ(
        harness.loop.telemetry().exactSamplesAccepted, 1u);
    ASSERT_TRUE(frame.presentationUpdated);

    const auto avatars = harness.loop.presentation().avatars();
    const auto remote = std::find_if(
        avatars.begin(), avatars.end(),
        [](const WreckwaterAvatarPresentation& avatar) {
            return avatar.playerId == 2u;
        });
    ASSERT_NE(remote, avatars.end());
    EXPECT_FALSE(remote->localPredicted);
    EXPECT_EQ(remote->poseTick, 11u);
    EXPECT_FLOAT_EQ(remote->poseFraction, 0.5f);
    const auto local = std::find_if(
        avatars.begin(), avatars.end(),
        [](const WreckwaterAvatarPresentation& avatar) {
            return avatar.playerId == 1u;
        });
    ASSERT_NE(local, avatars.end());
    EXPECT_TRUE(local->localPredicted);
    EXPECT_EQ(local->poseTick, 13u);
    EXPECT_FLOAT_EQ(local->poseFraction, 0.0f);
}

TEST(
    WreckwaterGraphicalClientLoop,
    ThirtySixtyAndOneFortyFourHertzProduceExactSameTicksAndInputs) {
    const CadenceResult thirty = runCadence(30u);
    const CadenceResult sixty = runCadence(60u);
    const CadenceResult oneFortyFour = runCadence(144u);

    EXPECT_EQ(thirty.predictedTick, 70u);
    EXPECT_EQ(sixty.predictedTick, thirty.predictedTick);
    EXPECT_EQ(oneFortyFour.predictedTick, thirty.predictedTick);
    EXPECT_EQ(sixty.feet, thirty.feet);
    EXPECT_EQ(oneFortyFour.feet, thirty.feet);
    EXPECT_EQ(sixty.requests, thirty.requests);
    EXPECT_EQ(oneFortyFour.requests, thirty.requests);
    EXPECT_EQ(thirty.serviceCalls, 31u);
    EXPECT_EQ(sixty.serviceCalls, 61u);
    EXPECT_EQ(oneFortyFour.serviceCalls, 145u);
}

TEST(
    WreckwaterGraphicalClientLoop,
    OneSecondHitchExecutesOnlyConfiguredCatchUpAndDropsWallTime) {
    Harness harness(loopConfig(4u));
    ASSERT_EQ(
        harness.bind(snapshot(
            harness.runtimeConfiguration, 1u, 10u)).status,
        WreckwaterGraphicalClientFrameStatus::Accepted);

    const auto hitch = harness.loop.frame({
        .elapsedNanoseconds =
            kWreckwaterGraphicalClientNanosecondsPerSecond,
        .renderTick = {.whole = 10u},
        .controls = {.movement = {1.0f, 0.0f}},
    });
    EXPECT_EQ(
        hitch.status,
        WreckwaterGraphicalClientFrameStatus::Accepted);
    EXPECT_EQ(hitch.simulationTicks, 4u);
    EXPECT_EQ(hitch.wallClockTicksDropped, 56u);
    EXPECT_EQ(harness.transport->sent.size(), 4u);
    EXPECT_EQ(
        harness.loop.controller().predictedTick(), 14u);
    EXPECT_EQ(
        harness.loop.telemetry().maximumSimulationTicksPerFrame,
        4u);
    EXPECT_EQ(harness.loop.telemetry().hitchFrames, 1u);
    EXPECT_EQ(
        harness.loop.telemetry().droppedWallClockTicks, 56u);
}

TEST(
    WreckwaterGraphicalClientLoop,
    RejectedRuntimeFrameKeepsLineageAndDefersOneShot) {
    Harness harness;
    ASSERT_EQ(
        harness.bind(snapshot(
            harness.runtimeConfiguration, 1u, 10u)).status,
        WreckwaterGraphicalClientFrameStatus::Accepted);
    const auto before = harness.loop.telemetry();
    const uint32_t characterHandle =
        harness.loop.controller().characterHandle();
    const uint32_t connectionGeneration =
        harness.loop.controller().connectionGeneration();

    harness.transport->inbound.push_back(malformedDataFrame(
        harness.runtimeConfiguration, 77u));
    const auto rejected = harness.loop.frame({
        .elapsedNanoseconds = 16'666'667u,
        .renderTick = {.whole = 10u},
        .controls = {
            .movement = {0.5f, 0.0f},
            .jumpDown = true,
        },
    });
    EXPECT_EQ(
        rejected.status,
        WreckwaterGraphicalClientFrameStatus::
            RuntimeFrameRejected);
    EXPECT_EQ(rejected.runtime.framesRejected, 1u);
    EXPECT_EQ(rejected.simulationTicks, 1u);
    EXPECT_EQ(
        harness.loop.latestConsumedSnapshotSequence(), 1u);
    EXPECT_EQ(
        harness.loop.controller().authoritativeTick(), 10u);
    EXPECT_EQ(
        harness.loop.controller().predictedTick(), 11u);
    EXPECT_EQ(
        harness.loop.controller().characterHandle(),
        characterHandle);
    EXPECT_EQ(
        harness.loop.controller().connectionGeneration(),
        connectionGeneration);
    EXPECT_EQ(
        harness.loop.telemetry().connectionFences,
        before.connectionFences);
    EXPECT_EQ(
        harness.loop.telemetry().generationFences,
        before.generationFences);
    EXPECT_EQ(
        harness.loop.telemetry().characterAvailabilityFences,
        before.characterAvailabilityFences);
    ASSERT_EQ(harness.transport->sent.size(), 1u);
    EXPECT_EQ(
        decodeMovement(harness.transport->sent[0]).inputFlags,
        0u);
    EXPECT_EQ(
        harness.loop.telemetry().jumpOneShotsSent, 0u);

    const auto clean = harness.loop.frame({
        .elapsedNanoseconds = 16'666'667u,
        .renderTick = {.whole = 10u},
        .controls = {
            .movement = {0.5f, 0.0f},
            .jumpDown = true,
        },
    });
    EXPECT_EQ(
        clean.status,
        WreckwaterGraphicalClientFrameStatus::Accepted);
    EXPECT_EQ(clean.simulationTicks, 1u);
    ASSERT_EQ(harness.transport->sent.size(), 2u);
    EXPECT_EQ(
        decodeMovement(harness.transport->sent[1]).inputFlags,
        network::kWreckwaterCharacterInputJumpFlag);
    EXPECT_EQ(
        harness.loop.telemetry().jumpOneShotsSent, 1u);

    static_cast<void>(harness.loop.frame({
        .renderTick = {.whole = 10u},
    }));
    harness.transport->failedSendsRemaining = 1u;
    const auto pendingBoard = harness.loop.frame({
        .elapsedNanoseconds = 16'666'666u,
        .renderTick = {.whole = 10u},
        .controls = {.boardDown = true},
    });
    ASSERT_EQ(
        pendingBoard.status,
        WreckwaterGraphicalClientFrameStatus::SendRetryPending);
    ASSERT_TRUE(harness.loop.sendPending());
    const uint64_t attemptsBeforeRejectedFrame =
        harness.transport->sendCalls;
    const uint64_t tickBeforeRejectedFrame =
        harness.loop.controller().predictedTick();

    harness.transport->inbound.push_back(malformedDataFrame(
        harness.runtimeConfiguration, 77u));
    const auto rejectedPending = harness.loop.frame({
        .elapsedNanoseconds = 16'666'667u,
        .renderTick = {.whole = 10u},
        .controls = {.boardDown = true},
    });
    EXPECT_EQ(
        rejectedPending.status,
        WreckwaterGraphicalClientFrameStatus::
            RuntimeFrameRejected);
    EXPECT_EQ(rejectedPending.simulationTicks, 0u);
    EXPECT_TRUE(rejectedPending.sendPending);
    EXPECT_EQ(
        harness.transport->sendCalls,
        attemptsBeforeRejectedFrame);
    EXPECT_EQ(
        harness.loop.controller().predictedTick(),
        tickBeforeRejectedFrame);

    const auto cleanRetry = harness.loop.frame({
        .renderTick = {.whole = 10u},
        .controls = {.boardDown = true},
    });
    EXPECT_EQ(
        cleanRetry.status,
        WreckwaterGraphicalClientFrameStatus::Accepted);
    EXPECT_EQ(cleanRetry.simulationTicks, 2u);
    ASSERT_EQ(harness.transport->sent.size(), 4u);
    EXPECT_EQ(
        decodeMovement(harness.transport->sent[2]).inputFlags,
        network::kWreckwaterCharacterInputBoardFlag);
    EXPECT_EQ(
        decodeMovement(harness.transport->sent[3]).inputFlags,
        0u);
    EXPECT_EQ(
        harness.loop.telemetry().boardOneShotsSent, 1u);
}

TEST(
    WreckwaterGraphicalClientLoop,
    FailedSendRetriesExactSampleAndOneShotRecordsOnlyAfterSuccess) {
    Harness harness;
    ASSERT_EQ(
        harness.bind(snapshot(
            harness.runtimeConfiguration, 1u, 10u)).status,
        WreckwaterGraphicalClientFrameStatus::Accepted);
    harness.transport->failedSendsRemaining = 1u;

    const auto failed = harness.loop.frame({
        .elapsedNanoseconds = 16'666'667u,
        .renderTick = {.whole = 10u},
        .controls = {
            .movement = {0.25f, -0.5f},
            .jumpDown = true,
            .boardDown = true,
        },
    });
    EXPECT_EQ(
        failed.status,
        WreckwaterGraphicalClientFrameStatus::SendRetryPending);
    EXPECT_EQ(failed.simulationTicks, 0u);
    EXPECT_TRUE(failed.sendPending);
    EXPECT_TRUE(harness.transport->sent.empty());
    EXPECT_EQ(
        harness.loop.controller().bufferedInputCount(), 0u);
    EXPECT_EQ(
        harness.loop.controller().telemetry().inputsRecorded, 0u);

    const auto retried = harness.loop.frame({
        .renderTick = {.whole = 10u},
        .controls = {
            .movement = {0.25f, -0.5f},
            .jumpDown = true,
            .boardDown = true,
        },
    });
    EXPECT_EQ(
        retried.status,
        WreckwaterGraphicalClientFrameStatus::Accepted);
    EXPECT_EQ(retried.simulationTicks, 1u);
    EXPECT_FALSE(retried.sendPending);
    ASSERT_EQ(harness.transport->sent.size(), 1u);
    const auto first =
        decodeMovement(harness.transport->sent[0]);
    EXPECT_EQ(first.characterInputSequence, 1u);
    EXPECT_EQ(
        first.inputFlags,
        network::kWreckwaterCharacterInputJumpFlag
            | network::kWreckwaterCharacterInputBoardFlag);
    EXPECT_EQ(
        harness.loop.controller().telemetry().inputsRecorded, 1u);

    ASSERT_EQ(
        harness.loop.frame({
            .elapsedNanoseconds = 16'666'667u,
            .renderTick = {.whole = 10u},
            .controls = {
                .movement = {0.25f, -0.5f},
                .jumpDown = true,
                .boardDown = true,
            },
        }).simulationTicks,
        1u);
    ASSERT_EQ(harness.transport->sent.size(), 2u);
    EXPECT_EQ(
        decodeMovement(harness.transport->sent[1]).inputFlags,
        0u);

    static_cast<void>(harness.loop.frame({
        .renderTick = {.whole = 10u},
        .controls = {
            .movement = {0.25f, -0.5f},
        },
    }));
    ASSERT_EQ(
        harness.loop.frame({
            .elapsedNanoseconds = 16'666'666u,
            .renderTick = {.whole = 10u},
            .controls = {
                .movement = {0.25f, -0.5f},
                .jumpDown = true,
            },
        }).simulationTicks,
        1u);
    ASSERT_EQ(harness.transport->sent.size(), 3u);
    EXPECT_EQ(
        decodeMovement(harness.transport->sent[2]).inputFlags,
        network::kWreckwaterCharacterInputJumpFlag);
    EXPECT_EQ(harness.loop.telemetry().sendRetries, 1u);
    EXPECT_EQ(harness.loop.telemetry().jumpOneShotsSent, 2u);
    EXPECT_EQ(harness.loop.telemetry().boardOneShotsSent, 1u);
    EXPECT_EQ(
        harness.loop.telemetry().
            maximumSendAttemptsForOneSample,
        2u);
}

TEST(
    WreckwaterGraphicalClientLoop,
    DisconnectPurgesRetryAndReconnectRequiresNewGenerationEdges) {
    Harness harness;
    ASSERT_EQ(
        harness.bind(snapshot(
            harness.runtimeConfiguration, 1u, 10u)).status,
        WreckwaterGraphicalClientFrameStatus::Accepted);
    harness.transport->failedSendsRemaining = 1u;
    ASSERT_EQ(
        harness.loop.frame({
            .elapsedNanoseconds = 16'666'667u,
            .renderTick = {.whole = 10u},
            .controls = {.jumpDown = true},
        }).status,
        WreckwaterGraphicalClientFrameStatus::SendRetryPending);
    ASSERT_TRUE(harness.loop.sendPending());

    harness.transport->inbound.push_back(lifecycleFrame(
        harness.runtimeConfiguration,
        network::MultiplayerTransportFrameType::Disconnected,
        77u));
    const auto disconnected = harness.loop.frame({
        .renderTick = {.whole = 10u},
        .controls = {.jumpDown = true},
    });
    EXPECT_EQ(
        disconnected.status,
        WreckwaterGraphicalClientFrameStatus::Disconnected);
    EXPECT_FALSE(harness.loop.sendPending());
    EXPECT_EQ(
        harness.loop.controller().binding(),
        WreckwaterCharacterControllerBinding::
            AwaitingCertifiedSnapshot);
    EXPECT_EQ(harness.loop.presentation().visibleCount(), 0u);

    FakeGraphicalTransport* replacement = nullptr;
    ASSERT_TRUE(harness.runtime.replaceTransport(
        makeFakeTransport(replacement)));
    replacement->inbound.push_back(lifecycleFrame(
        harness.runtimeConfiguration,
        network::MultiplayerTransportFrameType::Connected,
        78u));
    replacement->inbound.push_back(snapshotFrame(
        harness.runtimeConfiguration,
        snapshot(
            harness.runtimeConfiguration, 2u, 20u, 2u),
        78u));
    const auto rebound = harness.loop.frame({
        .renderTick = {.whole = 20u},
        .controls = {.jumpDown = true},
    });
    EXPECT_EQ(
        rebound.status,
        WreckwaterGraphicalClientFrameStatus::Accepted);
    EXPECT_EQ(
        harness.loop.controller().connectionGeneration(), 2u);
    EXPECT_TRUE(replacement->sent.empty());

    ASSERT_EQ(
        harness.loop.frame({
            .elapsedNanoseconds = 16'666'667u,
            .renderTick = {.whole = 20u},
            .controls = {.jumpDown = true},
        }).simulationTicks,
        1u);
    ASSERT_EQ(replacement->sent.size(), 1u);
    EXPECT_EQ(
        decodeMovement(replacement->sent[0]).inputFlags, 0u);
    EXPECT_EQ(
        decodeMovement(replacement->sent[0])
            .connectionGeneration,
        2u);
    EXPECT_EQ(
        decodeMovement(replacement->sent[0])
            .characterInputSequence,
        1u);

    static_cast<void>(harness.loop.frame({
        .renderTick = {.whole = 20u},
    }));
    ASSERT_EQ(
        harness.loop.frame({
            .elapsedNanoseconds = 16'666'667u,
            .renderTick = {.whole = 20u},
            .controls = {.jumpDown = true},
        }).simulationTicks,
        1u);
    ASSERT_EQ(replacement->sent.size(), 2u);
    EXPECT_EQ(
        decodeMovement(replacement->sent[1]).inputFlags,
        network::kWreckwaterCharacterInputJumpFlag);
    EXPECT_EQ(
        harness.loop.telemetry().pendingSamplesPurgedByFence,
        1u);
    EXPECT_GE(
        harness.loop.telemetry().oneShotsPurgedByFence, 1u);
}

TEST(
    WreckwaterGraphicalClientLoop,
    LocalPredictionAndCameraDescriptorCrossWorldSectorTogether) {
    Harness harness;
    const physics::WorldPosition nearBoundary =
        physics::worldPositionFromAbsolute(
            {127.95, 10.0, 0.0});
    ASSERT_EQ(
        harness.bind(snapshot(
            harness.runtimeConfiguration,
            1u, 100u, 1u, nearBoundary,
            {12.0f, 0.0f, 0.0f},
            game::WreckwaterCharacterMode::Airborne)).status,
        WreckwaterGraphicalClientFrameStatus::Accepted);

    const auto advanced = harness.loop.frame({
        .elapsedNanoseconds = 16'666'667u,
        .renderTick = {.whole = 100u},
        .cameraSector = {1, 0, 0},
    });
    EXPECT_EQ(
        advanced.status,
        WreckwaterGraphicalClientFrameStatus::Accepted);
    EXPECT_EQ(advanced.simulationTicks, 1u);
    ASSERT_TRUE(advanced.presentationUpdated);

    const auto pose = harness.loop.controller().predictedPose();
    ASSERT_TRUE(pose);
    EXPECT_EQ(pose.pose.feetPosition.sector.x, 1);
    EXPECT_GT(
        physics::worldPositionToAbsolute(
            pose.pose.feetPosition).x,
        128.0);

    const auto avatars = harness.loop.presentation().avatars();
    const auto found = std::find_if(
        avatars.begin(), avatars.end(),
        [](const WreckwaterAvatarPresentation& avatar) {
            return avatar.playerId == 1u;
        });
    ASSERT_NE(found, avatars.end());
    EXPECT_TRUE(found->localPredicted);
    EXPECT_EQ(found->worldFeetPosition.sector.x, 1);
    EXPECT_LT(found->cameraSectorFeetPosition.x, 0.0f);
    const auto& camera =
        harness.loop.presentation().thirdPersonCameraTarget();
    EXPECT_TRUE(camera.valid);
    EXPECT_EQ(camera.worldTargetPosition.sector.x, 1);
    EXPECT_LT(camera.cameraSectorTargetPosition.x, 0.0f);
}

} // namespace
} // namespace voxy::client
