#include "client/wreckwater_application_client.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <vector>

namespace voxy::client {
namespace {

struct TransportState {
    uint64_t serviceCalls = 0u;
    uint64_t closeCalls = 0u;
};

class LifecycleTransport final
    : public network::IMultiplayerTransport {
public:
    explicit LifecycleTransport(TransportState& state)
        : state_(state) {}

    void service() override { ++state_.serviceCalls; }

    bool send(
        uint32_t,
        network::DeliveryClass,
        std::span<const std::byte>) override {
        return false;
    }

    std::optional<network::MultiplayerTransportFrame>
    poll() override {
        return std::nullopt;
    }

    void close() override { ++state_.closeCalls; }

private:
    TransportState& state_;
};

[[nodiscard]] WreckwaterApplicationClient::Config
applicationClientConfig() {
    WreckwaterApplicationClient::Config result;
    result.runtime = {
        .serverPeerId = 0u,
        .serverConnectionSerial = 0u,
        .sessionId = 101u,
        .matchId = 202u,
        .worldId = 303u,
        .worldEpoch = 4u,
        .authorityEpoch = 5u,
        .localPlayerId = 1u,
        .maximumFramesPerPump = 16u,
        .firstClientRequestSequence = 1u,
    };
    result.graphical.maximumCatchUpTicks = 4u;
    result.graphical.inputLeadTicks = 8u;
    result.graphical.useCertifiedVisualClock = true;
    result.graphical.controller.localPlayerId = 1u;
    result.graphical.controller.inputHistoryTicks = 128u;
    result.graphical.controller.stateHistoryTicks = 128u;
    result.graphical.controller.platformPredictionTicks = 128u;
    result.graphical.presentation.localPlayerId = 1u;
    result.graphical.presentation.roster = {{
        {1u, game::CrewId::CrewOne, 0u},
        {2u, game::CrewId::CrewOne, 1u},
        {3u, game::CrewId::CrewTwo, 0u},
        {4u, game::CrewId::CrewTwo, 1u},
    }};
    return result;
}

[[nodiscard]] WreckwaterCameraObstructionProbeRequest
cameraProbeRequest() {
    constexpr uint64_t sequence = (uint64_t{1u} << 32u) + 7u;
    return {
        .identity = {
            .sequence = sequence,
            .playerId = 1u,
            .characterHandle = 0x0001'0001u,
            .connectionGeneration = 3u,
        },
        .physicsQuery = {
            .requestId = static_cast<uint32_t>(sequence),
            .type = physics::PhysicsQueryType::SphereCast,
            .maximumHits = 1u,
            .origin = {0.0f, 2.0f, -1.0f},
            .radius = 0.5f,
            .direction = {0.0f, 0.0f, 1.0f},
            .maximumDistance = 2.0f,
            .sector = {0, 0, 0},
        },
        .worldStart = {
            .sector = {0, 0, 0},
            .local = {0.0f, 2.0f, -1.0f},
        },
        .worldEnd = {
            .sector = {0, 0, 0},
            .local = {0.0f, 2.0f, 1.0f},
        },
        .pathDistance = 2.0f,
    };
}

[[nodiscard]] WreckwaterCameraTerrainView terrainView(
    std::span<const uint16_t> samples) {
    return {
        .samples = samples,
        .width = 5u,
        .height = 5u,
        .heightScale = 10.0f,
        .cellScale = 1.0f,
    };
}

TEST(WreckwaterApplicationClient,
     CameraRelativeDigitalControlsAreNormalized) {
    WreckwaterGraphicalClientControls controls;
    EXPECT_TRUE(mapWreckwaterCameraRelativeControls(
        {
            .forward = true,
            .right = true,
            .jumpDown = true,
            .boardDown = true,
        },
        {0.0f, 0.0f, -1.0f},
        {1.0f, 0.0f, 0.0f},
        controls));
    const float inverseRootTwo = 1.0f / std::sqrt(2.0f);
    EXPECT_NEAR(controls.movement.x, inverseRootTwo, 1.0e-6f);
    EXPECT_NEAR(controls.movement.y, -inverseRootTwo, 1.0e-6f);
    EXPECT_TRUE(controls.jumpDown);
    EXPECT_TRUE(controls.boardDown);
}

TEST(WreckwaterApplicationClient,
     VerticalForwardFallsBackToHorizontalRightBasis) {
    WreckwaterGraphicalClientControls controls;
    EXPECT_TRUE(mapWreckwaterCameraRelativeControls(
        {.forward = true},
        {0.0f, 1.0f, 0.0f},
        {1.0f, 0.0f, 0.0f},
        controls));
    EXPECT_FLOAT_EQ(controls.movement.x, 0.0f);
    EXPECT_FLOAT_EQ(controls.movement.y, -1.0f);

    EXPECT_FALSE(mapWreckwaterCameraRelativeControls(
        {},
        {
            std::numeric_limits<float>::quiet_NaN(),
            0.0f,
            0.0f,
        },
        {1.0f, 0.0f, 0.0f},
        controls));
}

TEST(WreckwaterApplicationClient,
     OwnsOnePumpPerFrameAndClosesTransportOnce) {
    TransportState transportState;
    WreckwaterApplicationClient client;
    ASSERT_TRUE(client.initialize(
        applicationClientConfig(),
        std::make_unique<LifecycleTransport>(
            transportState)));

    const auto first = client.frame({
        .elapsedNanoseconds = 16'666'667u,
    });
    EXPECT_EQ(
        first.graphical.status,
        WreckwaterGraphicalClientFrameStatus::Disconnected);
    EXPECT_EQ(transportState.serviceCalls, 1u);

    static_cast<void>(client.frame({
        .elapsedNanoseconds = 8'333'333u,
    }));
    EXPECT_EQ(transportState.serviceCalls, 2u);
    EXPECT_TRUE(client.visibleInstances().empty());

    client.close();
    EXPECT_FALSE(client.initialized());
    EXPECT_EQ(client.runtime(), nullptr);
    EXPECT_FALSE(client.graphicalLoop().initialized());
    EXPECT_FALSE(client.cameraRig().initialized());
    EXPECT_EQ(transportState.closeCalls, 1u);
    client.close();
    EXPECT_EQ(transportState.closeCalls, 1u);
}

TEST(WreckwaterApplicationClient,
     NullTransportFailureLeavesCleanState) {
    WreckwaterApplicationClient client;
    EXPECT_FALSE(client.initialize(
        applicationClientConfig(), nullptr));
    EXPECT_FALSE(client.initialized());
    EXPECT_EQ(client.runtime(), nullptr);
    EXPECT_FALSE(client.graphicalLoop().initialized());
    EXPECT_FALSE(client.cameraRig().initialized());
    EXPECT_TRUE(client.visibleInstances().empty());
}

TEST(WreckwaterApplicationClient,
     TerrainProbeConservativelyCoversFlatAndSharpTerrain) {
    std::vector<uint16_t> samples(25u, 32'768u);
    const auto request = cameraProbeRequest();
    auto view = terrainView(samples);
    EXPECT_TRUE(wreckwaterCameraTerrainViewSupports(
        view, request.pathDistance,
        request.physicsQuery.radius));

    const auto clear =
        wreckwaterCameraTerrainSphereCast(request, view);
    ASSERT_TRUE(clear);
    EXPECT_FALSE(clear.hit);
    EXPECT_FLOAT_EQ(clear.hitDistance, 0.0f);

    samples[2u * 5u + 2u] =
        std::numeric_limits<uint16_t>::max();
    const auto sharp =
        wreckwaterCameraTerrainSphereCast(
            request, terrainView(samples));
    ASSERT_TRUE(sharp);
    EXPECT_TRUE(sharp.hit);
    EXPECT_GE(sharp.hitDistance, 0.0f);
    EXPECT_LE(sharp.hitDistance, request.pathDistance);
}

TEST(WreckwaterApplicationClient,
     TerrainProbeBudgetExhaustionCannotBecomeMiss) {
    const std::vector<uint16_t> samples(25u, 0u);
    auto view = terrainView(samples);
    view.maximumVisitedCells = 1u;
    EXPECT_FALSE(wreckwaterCameraTerrainViewSupports(
        view, 2.0f, 0.5f));
    const auto result =
        wreckwaterCameraTerrainSphereCast(
            cameraProbeRequest(), view);
    EXPECT_EQ(
        result.status,
        WreckwaterCameraTerrainCastStatus::
            WorkBudgetExceeded);
    EXPECT_FALSE(result);
}

TEST(WreckwaterApplicationClient,
     ProbeMergeRetainsFullIdentityAndChoosesClosestDomain) {
    const auto request = cameraProbeRequest();
    WreckwaterCameraTerrainCastResult terrain{
        .status = WreckwaterCameraTerrainCastStatus::Accepted,
        .hitDistance = 1.5f,
        .hit = true,
    };
    physics::PhysicsQueryBatch bodies;
    bodies.tick = 99u;
    bodies.outputs.push_back({
        .requestId = request.physicsQuery.requestId,
        .type = physics::PhysicsQueryType::SphereCast,
        .hits = {{
            .requestId = request.physicsQuery.requestId,
            .bodyIndex = 4u,
            .bodyGeneration = 2u,
            .type = physics::PhysicsQueryType::SphereCast,
            .fraction = 0.375f,
            .distance = 0.75f,
            .point = {0.0f, 2.0f, -0.25f},
            .normal = {0.0f, 0.0f, -1.0f},
        }},
    });

    const auto merged =
        mergeWreckwaterCameraObstructionProbeResult(
            request, terrain, bodies);
    EXPECT_EQ(merged.identity, request.identity);
    EXPECT_GT(
        merged.identity.sequence,
        std::numeric_limits<uint32_t>::max());
    EXPECT_TRUE(merged.completeScene);
    EXPECT_FALSE(merged.overflow);
    EXPECT_TRUE(merged.hit);
    EXPECT_FLOAT_EQ(merged.hitDistance, 0.75f);

    bodies.outputs.front().hits.clear();
    const auto terrainOnly =
        mergeWreckwaterCameraObstructionProbeResult(
            request, terrain, bodies);
    EXPECT_TRUE(terrainOnly.completeScene);
    EXPECT_TRUE(terrainOnly.hit);
    EXPECT_FLOAT_EQ(terrainOnly.hitDistance, 1.5f);
}

TEST(WreckwaterApplicationClient,
     ProbeMergeFailsClosedOnMissingOrMismatchedBodyEvidence) {
    const auto request = cameraProbeRequest();
    const WreckwaterCameraTerrainCastResult terrain{
        .status = WreckwaterCameraTerrainCastStatus::Accepted,
    };
    physics::PhysicsQueryBatch bodies;
    auto missing =
        mergeWreckwaterCameraObstructionProbeResult(
            request, terrain, bodies);
    EXPECT_EQ(missing.identity, request.identity);
    EXPECT_TRUE(missing.overflow);
    EXPECT_FALSE(missing.completeScene);

    bodies.outputs.push_back({
        .requestId = request.physicsQuery.requestId + 1u,
        .type = physics::PhysicsQueryType::SphereCast,
        .hits = {},
    });
    const auto mismatched =
        mergeWreckwaterCameraObstructionProbeResult(
            request, terrain, bodies);
    EXPECT_TRUE(mismatched.overflow);
    EXPECT_FALSE(mismatched.completeScene);

    bodies.outputs.front().requestId =
        request.physicsQuery.requestId;
    const auto completeMiss =
        mergeWreckwaterCameraObstructionProbeResult(
            request, terrain, bodies);
    EXPECT_FALSE(completeMiss.overflow);
    EXPECT_TRUE(completeMiss.completeScene);
    EXPECT_FALSE(completeMiss.hit);
    EXPECT_FLOAT_EQ(completeMiss.hitDistance, 0.0f);
}

} // namespace
} // namespace voxy::client
