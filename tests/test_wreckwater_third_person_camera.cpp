#include "client/wreckwater_third_person_camera.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <type_traits>
#include <utility>

namespace voxy::client {
namespace {

static_assert(std::is_nothrow_copy_constructible_v<
              WreckwaterThirdPersonCameraRig>);
static_assert(std::is_nothrow_copy_assignable_v<
              WreckwaterThirdPersonCameraRig>);
static_assert(std::is_trivially_copyable_v<
              WreckwaterThirdPersonCameraRig>);
static_assert(noexcept(
    std::declval<WreckwaterThirdPersonCameraRig&>().update(
        std::declval<
            const WreckwaterThirdPersonCameraTarget&>(),
        std::declval<
            const WreckwaterThirdPersonCameraInput&>(),
        1.0f / 60.0f)));
static_assert(noexcept(
    std::declval<WreckwaterThirdPersonCameraRig&>()
        .takeObstructionProbeRequest(
            std::declval<
                WreckwaterCameraObstructionProbeRequest&>())));
static_assert(noexcept(
    std::declval<WreckwaterThirdPersonCameraRig&>()
        .resolveObstructionProbe(
            std::declval<
                const
                WreckwaterCameraObstructionProbeResult&>())));

[[nodiscard]] WreckwaterThirdPersonCameraTarget cameraTarget(
    const glm::dvec3& position,
    const glm::vec3& velocity = glm::vec3(0.0f),
    uint64_t playerId = 11u,
    uint32_t characterHandle = 7u,
    uint32_t connectionGeneration = 3u) {
    return {
        .playerId = playerId,
        .characterHandle = characterHandle,
        .connectionGeneration = connectionGeneration,
        .worldTargetPosition =
            physics::worldPositionFromAbsolute(position),
        .cameraSectorTargetPosition =
            glm::vec3(position),
        .worldVelocity = velocity,
        .facing = {0.0f, 0.0f, 1.0f},
        .mode = game::WreckwaterCharacterMode::OnSkiff,
        .valid = true,
    };
}

[[nodiscard]] glm::dvec3 absolute(
    const physics::WorldPosition& position) {
    return physics::worldPositionToAbsolute(position);
}

void expectAccepted(
    const WreckwaterThirdPersonCameraFrameResult& result) {
    EXPECT_EQ(
        result.status,
        WreckwaterThirdPersonCameraStatus::Accepted)
        << wreckwaterThirdPersonCameraStatusName(result.status);
}

void expectAccepted(
    WreckwaterThirdPersonCameraStatus status) {
    EXPECT_EQ(
        status,
        WreckwaterThirdPersonCameraStatus::Accepted)
        << wreckwaterThirdPersonCameraStatusName(status);
}

void certifyMiss(WreckwaterThirdPersonCameraRig& rig) {
    WreckwaterCameraObstructionProbeRequest request;
    expectAccepted(rig.takeObstructionProbeRequest(request));
    expectAccepted(rig.resolveObstructionProbe({
        .identity = request.identity,
        .hitDistance = 0.0f,
        .hit = false,
        .completeScene = true,
    }));
}

struct RateResult {
    glm::dvec3 camera{0.0};
    glm::dvec3 target{0.0};
    float fov = 0.0f;
    float boomDistance = 0.0f;
};

[[nodiscard]] RateResult runAtRate(uint32_t rate) {
    WreckwaterThirdPersonCameraRig::Config config;
    config.maximumProbeEndpointDrift = 1.0f;
    WreckwaterThirdPersonCameraRig rig;
    EXPECT_TRUE(rig.initialize(config));
    constexpr glm::vec3 velocity{2.0f, 0.25f, -0.75f};
    expectAccepted(rig.update(
        cameraTarget({127.5, 4.0, -12.0}, velocity),
        {}, 1.0f / 60.0f));
    certifyMiss(rig);

    WreckwaterThirdPersonCameraInput input;
    input.orbit = {0.35f, 0.12f};
    input.zoom = 0.20f;
    const float dt = 1.0f / static_cast<float>(rate);
    const uint32_t frameCount = rate * 2u;
    for (uint32_t frame = 1u; frame <= frameCount; ++frame) {
        const double time =
            static_cast<double>(frame)
            / static_cast<double>(rate);
        const glm::dvec3 position =
            glm::dvec3(127.5, 4.0, -12.0)
            + glm::dvec3(velocity) * time;
        expectAccepted(rig.update(
            cameraTarget(position, velocity), input, dt));
        certifyMiss(rig);
    }
    const auto& pose = rig.pose();
    return {
        .camera = absolute(pose.cameraPosition),
        .target = absolute(pose.lookTargetPosition),
        .fov = pose.fovYRadians,
        .boomDistance = pose.boomDistance,
    };
}

TEST(WreckwaterThirdPersonCamera,
     AnalyticTrackingMatchesThirtySixtyAndOneFortyFourHertz) {
    const RateResult at30 = runAtRate(30u);
    const RateResult at60 = runAtRate(60u);
    const RateResult at144 = runAtRate(144u);

    for (int axis = 0; axis < 3; ++axis) {
        EXPECT_NEAR(at30.camera[axis], at60.camera[axis], 2.0e-3);
        EXPECT_NEAR(
            at144.camera[axis], at60.camera[axis], 2.0e-3);
        EXPECT_NEAR(at30.target[axis], at60.target[axis], 2.0e-4);
        EXPECT_NEAR(
            at144.target[axis], at60.target[axis], 2.0e-4);
    }
    EXPECT_NEAR(at30.fov, at60.fov, 2.0e-5f);
    EXPECT_NEAR(at144.fov, at60.fov, 2.0e-5f);
    EXPECT_NEAR(
        at30.boomDistance, at60.boomDistance, 2.0e-3f);
    EXPECT_NEAR(
        at144.boomDistance, at60.boomDistance, 2.0e-3f);
}

TEST(WreckwaterThirdPersonCamera,
     HitchUsesOnlyConfiguredBoundedDelta) {
    WreckwaterThirdPersonCameraRig::Config config;
    config.maximumDeltaSeconds = 0.04f;
    WreckwaterThirdPersonCameraRig hitch;
    WreckwaterThirdPersonCameraRig reference;
    ASSERT_TRUE(hitch.initialize(config));
    ASSERT_TRUE(reference.initialize(config));
    const auto target = cameraTarget({0.0, 2.0, 0.0});
    expectAccepted(hitch.update(target, {}, 1.0f / 60.0f));
    expectAccepted(reference.update(target, {}, 1.0f / 60.0f));

    WreckwaterThirdPersonCameraInput input;
    input.orbit = {0.4f, -0.2f};
    input.zoom = 0.75f;
    const auto hitchResult = hitch.update(target, input, 1.0f);
    const auto referenceResult =
        reference.update(target, input, 0.04f);
    expectAccepted(hitchResult);
    expectAccepted(referenceResult);

    EXPECT_TRUE(hitchResult.deltaTimeCapped);
    EXPECT_FLOAT_EQ(hitchResult.appliedDeltaSeconds, 0.04f);
    EXPECT_FALSE(referenceResult.deltaTimeCapped);
    EXPECT_EQ(hitch.telemetry().hitchFrames, 1u);
    const glm::dvec3 hitchPosition =
        absolute(hitch.pose().cameraPosition);
    const glm::dvec3 referencePosition =
        absolute(reference.pose().cameraPosition);
    for (int axis = 0; axis < 3; ++axis) {
        EXPECT_NEAR(
            hitchPosition[axis], referencePosition[axis],
            1.0e-6);
    }
    EXPECT_FLOAT_EQ(
        hitch.pose().fovYRadians,
        reference.pose().fovYRadians);
}

TEST(WreckwaterThirdPersonCamera,
     OccluderPullsInFastAndMissReleasesMoreSlowly) {
    WreckwaterThirdPersonCameraRig rig;
    ASSERT_TRUE(rig.initialize({}));
    const auto target = cameraTarget({0.0, 2.0, 0.0});
    expectAccepted(rig.update(target, {}, 1.0f / 60.0f));
    EXPECT_NEAR(
        rig.pose().boomDistance, 0.35f, 1.0e-5f);
    certifyMiss(rig);
    for (uint32_t frame = 0u; frame < 180u; ++frame) {
        expectAccepted(rig.update(target, {}, 1.0f / 60.0f));
    }
    const float openDistance = rig.pose().boomDistance;

    WreckwaterCameraObstructionProbeRequest request;
    expectAccepted(rig.takeObstructionProbeRequest(request));
    EXPECT_EQ(
        request.physicsQuery.type,
        physics::PhysicsQueryType::SphereCast);
    EXPECT_EQ(request.physicsQuery.maximumHits, 1u);
    EXPECT_FLOAT_EQ(request.physicsQuery.radius, 0.60f);
    EXPECT_EQ(request.physicsQuery.sector, request.worldStart.sector);
    EXPECT_EQ(request.physicsQuery.origin, request.worldStart.local);
    EXPECT_NEAR(
        request.physicsQuery.maximumDistance,
        request.pathDistance, 1.0e-6f);

    expectAccepted(rig.resolveObstructionProbe({
        .identity = request.identity,
        .hitDistance = 1.25f,
        .hit = true,
        .completeScene = true,
    }));
    const float pulledDistance = rig.pose().boomDistance;
    const float pullAmount = openDistance - pulledDistance;
    EXPECT_GT(pullAmount, 0.5f);
    EXPECT_TRUE(rig.pose().obstructionLimited);

    WreckwaterCameraObstructionProbeRequest releaseRequest;
    expectAccepted(
        rig.takeObstructionProbeRequest(releaseRequest));
    expectAccepted(rig.resolveObstructionProbe({
        .identity = releaseRequest.identity,
        .hitDistance = 0.0f,
        .hit = false,
        .completeScene = true,
    }));
    expectAccepted(rig.update(target, {}, 1.0f / 60.0f));
    const float releasedDistance = rig.pose().boomDistance;
    const float releaseAmount = releasedDistance - pulledDistance;
    EXPECT_GT(releaseAmount, 0.0f);
    EXPECT_LT(releaseAmount, pullAmount);

    for (uint32_t frame = 0u; frame < 180u; ++frame) {
        expectAccepted(rig.update(target, {}, 1.0f / 60.0f));
    }
    EXPECT_NEAR(rig.pose().boomDistance, openDistance, 2.0e-3f);
    EXPECT_EQ(rig.telemetry().probeHits, 1u);
    EXPECT_EQ(rig.telemetry().probeMisses, 2u);
}

TEST(WreckwaterThirdPersonCamera,
     OverflowIgnoresGarbageHitFieldsAndClampsImmediately) {
    WreckwaterThirdPersonCameraRig rig;
    ASSERT_TRUE(rig.initialize({}));
    const auto target = cameraTarget({0.0, 2.0, 0.0});
    expectAccepted(rig.update(target, {}, 1.0f / 60.0f));
    certifyMiss(rig);
    for (uint32_t frame = 0u; frame < 180u; ++frame) {
        expectAccepted(rig.update(target, {}, 1.0f / 60.0f));
    }
    ASSERT_GT(rig.pose().boomDistance, 4.0f);

    WreckwaterCameraObstructionProbeRequest request;
    expectAccepted(rig.takeObstructionProbeRequest(request));
    EXPECT_EQ(
        rig.resolveObstructionProbe({
            .identity = request.identity,
            .hitDistance =
                std::numeric_limits<float>::quiet_NaN(),
            .hit = false,
            .overflow = true,
        }),
        WreckwaterThirdPersonCameraStatus::
            ProbeResultOverflow);

    EXPECT_FALSE(rig.probeOutstanding());
    EXPECT_TRUE(rig.pose().valid);
    EXPECT_TRUE(rig.pose().obstructionLimited);
    EXPECT_NEAR(rig.pose().boomDistance, 0.35f, 1.0e-5f);
    EXPECT_EQ(rig.telemetry().probeOverflows, 1u);
    EXPECT_EQ(rig.telemetry().invalidProbeResults, 0u);

    WreckwaterCameraObstructionProbeRequest retry;
    expectAccepted(rig.takeObstructionProbeRequest(retry));
    EXPECT_GT(retry.identity.sequence, request.identity.sequence);
}

TEST(WreckwaterThirdPersonCamera,
     MalformedExactResultStaysCancelableButFailsClosed) {
    WreckwaterThirdPersonCameraRig rig;
    ASSERT_TRUE(rig.initialize({}));
    const auto target = cameraTarget({0.0, 2.0, 0.0});
    expectAccepted(rig.update(target, {}, 1.0f / 60.0f));
    certifyMiss(rig);
    for (uint32_t frame = 0u; frame < 180u; ++frame) {
        expectAccepted(rig.update(target, {}, 1.0f / 60.0f));
    }
    ASSERT_GT(rig.pose().boomDistance, 4.0f);

    WreckwaterCameraObstructionProbeRequest request;
    expectAccepted(rig.takeObstructionProbeRequest(request));
    EXPECT_EQ(
        rig.resolveObstructionProbe({
            .identity = request.identity,
            .hitDistance =
                std::numeric_limits<float>::infinity(),
            .hit = true,
            .completeScene = true,
        }),
        WreckwaterThirdPersonCameraStatus::
            InvalidProbeResult);

    EXPECT_TRUE(rig.probeOutstanding());
    EXPECT_TRUE(rig.pose().valid);
    EXPECT_NEAR(rig.pose().boomDistance, 0.35f, 1.0e-5f);
    EXPECT_EQ(rig.telemetry().invalidProbeResults, 1u);
    expectAccepted(
        rig.cancelObstructionProbe(request.identity));
    EXPECT_FALSE(rig.probeOutstanding());
}

TEST(WreckwaterThirdPersonCamera,
     PartialSceneMissCannotCertifyAnOpenBoom) {
    WreckwaterThirdPersonCameraRig rig;
    ASSERT_TRUE(rig.initialize({}));
    expectAccepted(rig.update(
        cameraTarget({0.0, 2.0, 0.0}),
        {}, 1.0f / 60.0f));

    WreckwaterCameraObstructionProbeRequest request;
    expectAccepted(rig.takeObstructionProbeRequest(request));
    EXPECT_EQ(
        rig.resolveObstructionProbe({
            .identity = request.identity,
            .hitDistance = 0.0f,
            .hit = false,
            // A rigid-body-only miss cannot certify rendered terrain.
            .completeScene = false,
        }),
        WreckwaterThirdPersonCameraStatus::
            InvalidProbeResult);
    EXPECT_TRUE(rig.probeOutstanding());
    EXPECT_TRUE(rig.pose().obstructionLimited);
    EXPECT_NEAR(rig.pose().boomDistance, 0.35f, 1.0e-5f);
    expectAccepted(
        rig.cancelObstructionProbe(request.identity));
}

TEST(WreckwaterThirdPersonCamera,
     SmallEndpointDriftIsCoveredByTheInflatedCast) {
    WreckwaterThirdPersonCameraRig::Config config;
    config.maximumProbeEndpointDrift = 0.50f;
    WreckwaterThirdPersonCameraRig rig;
    ASSERT_TRUE(rig.initialize(config));
    const auto target = cameraTarget({0.0, 2.0, 0.0});
    expectAccepted(rig.update(target, {}, 1.0f / 60.0f));

    WreckwaterCameraObstructionProbeRequest request;
    expectAccepted(rig.takeObstructionProbeRequest(request));
    EXPECT_FLOAT_EQ(request.physicsQuery.radius, 0.75f);

    WreckwaterThirdPersonCameraInput input;
    input.orbit.x = 0.05f;
    expectAccepted(rig.update(target, input, 1.0f / 60.0f));
    ASSERT_TRUE(rig.probeOutstanding());
    expectAccepted(rig.resolveObstructionProbe({
        .identity = request.identity,
        .hitDistance = 1.50f,
        .hit = true,
        .completeScene = true,
    }));

    EXPECT_FALSE(rig.probeOutstanding());
    EXPECT_LE(rig.pose().boomDistance, 1.35f + 1.0e-5f);
    EXPECT_EQ(rig.telemetry().probeHits, 1u);
    EXPECT_EQ(rig.telemetry().probeGeometryExpirations, 0u);
}

TEST(WreckwaterThirdPersonCamera,
     MateriallyStaleMissAndHitFailClosedBeforeReprobe) {
    WreckwaterThirdPersonCameraRig::Config config;
    config.maximumProbeEndpointDrift = 0.01f;
    WreckwaterThirdPersonCameraRig rig;
    ASSERT_TRUE(rig.initialize(config));
    auto target = cameraTarget({0.0, 2.0, 0.0});
    expectAccepted(rig.update(target, {}, 1.0f / 60.0f));
    certifyMiss(rig);
    for (uint32_t frame = 0u; frame < 180u; ++frame) {
        expectAccepted(rig.update(target, {}, 1.0f / 60.0f));
    }
    ASSERT_GT(rig.pose().boomDistance, 4.0f);

    WreckwaterCameraObstructionProbeRequest staleMiss;
    expectAccepted(rig.takeObstructionProbeRequest(staleMiss));
    target = cameraTarget({1.0, 2.0, 0.0});
    expectAccepted(rig.update(target, {}, 0.05f));
    EXPECT_TRUE(rig.probeOutstanding());
    EXPECT_NEAR(rig.pose().boomDistance, 0.35f, 1.0e-5f);
    EXPECT_EQ(
        rig.resolveObstructionProbe({
            .identity = staleMiss.identity,
            .hitDistance = 0.0f,
            .hit = false,
            .completeScene = true,
        }),
        WreckwaterThirdPersonCameraStatus::
            ProbeGeometryExpired);
    EXPECT_FALSE(rig.probeOutstanding());
    EXPECT_NEAR(rig.pose().boomDistance, 0.35f, 1.0e-5f);

    for (uint32_t frame = 0u; frame < 180u; ++frame) {
        expectAccepted(rig.update(target, {}, 1.0f / 60.0f));
    }
    WreckwaterCameraObstructionProbeRequest current;
    expectAccepted(rig.takeObstructionProbeRequest(current));
    expectAccepted(rig.resolveObstructionProbe({
        .identity = current.identity,
        .hitDistance = 0.0f,
        .hit = false,
        .completeScene = true,
    }));
    for (uint32_t frame = 0u; frame < 180u; ++frame) {
        expectAccepted(rig.update(target, {}, 1.0f / 60.0f));
    }
    ASSERT_GT(rig.pose().boomDistance, 4.0f);

    WreckwaterCameraObstructionProbeRequest staleHit;
    expectAccepted(rig.takeObstructionProbeRequest(staleHit));
    target = cameraTarget({2.0, 2.0, 0.0});
    expectAccepted(rig.update(target, {}, 0.05f));
    EXPECT_EQ(
        rig.resolveObstructionProbe({
            .identity = staleHit.identity,
            .hitDistance = 1.0f,
            .hit = true,
            .completeScene = true,
        }),
        WreckwaterThirdPersonCameraStatus::
            ProbeGeometryExpired);
    EXPECT_FALSE(rig.probeOutstanding());
    EXPECT_NEAR(rig.pose().boomDistance, 0.35f, 1.0e-5f);
    EXPECT_EQ(rig.telemetry().probeGeometryExpirations, 2u);

    WreckwaterCameraObstructionProbeRequest retry;
    expectAccepted(rig.takeObstructionProbeRequest(retry));
    EXPECT_GT(retry.identity.sequence, staleHit.identity.sequence);
}

TEST(WreckwaterThirdPersonCamera,
     RejectsStaleAndMismatchedProbeLifetimes) {
    WreckwaterThirdPersonCameraRig rig;
    ASSERT_TRUE(rig.initialize({}));
    auto target = cameraTarget({0.0, 2.0, 0.0});
    expectAccepted(rig.update(target, {}, 1.0f / 60.0f));

    WreckwaterCameraObstructionProbeRequest oldRequest;
    expectAccepted(rig.takeObstructionProbeRequest(oldRequest));
    target.worldTargetPosition =
        physics::worldPositionFromAbsolute({500.0, 2.0, 0.0});
    target.teleported = true;
    expectAccepted(rig.update(target, {}, 1.0f / 60.0f));
    EXPECT_FALSE(rig.probeOutstanding());
    EXPECT_EQ(
        rig.resolveObstructionProbe({
            .identity = oldRequest.identity,
            .hitDistance = 0.5f,
            .hit = true,
            .completeScene = true,
        }),
        WreckwaterThirdPersonCameraStatus::StaleProbeResult);

    WreckwaterCameraObstructionProbeRequest currentRequest;
    expectAccepted(
        rig.takeObstructionProbeRequest(currentRequest));
    auto wrongIdentity = currentRequest.identity;
    ++wrongIdentity.connectionGeneration;
    EXPECT_EQ(
        rig.resolveObstructionProbe({
            .identity = wrongIdentity,
            .hitDistance = 0.5f,
            .hit = true,
            .completeScene = true,
        }),
        WreckwaterThirdPersonCameraStatus::
            ProbeIdentityMismatch);
    EXPECT_TRUE(rig.probeOutstanding());
    expectAccepted(rig.resolveObstructionProbe({
        .identity = currentRequest.identity,
        .hitDistance = 0.0f,
        .hit = false,
        .completeScene = true,
    }));
    EXPECT_FALSE(rig.probeOutstanding());
    EXPECT_EQ(rig.telemetry().staleProbeResults, 1u);
    EXPECT_EQ(rig.telemetry().mismatchedProbeResults, 1u);
}

TEST(WreckwaterThirdPersonCamera,
     CanonicalOutputAndProbeCrossSectorExactly) {
    WreckwaterThirdPersonCameraRig::Config config;
    config.shoulderOffset = 0.0f;
    config.velocityLookAheadSeconds = 0.0f;
    WreckwaterThirdPersonCameraRig rig;
    ASSERT_TRUE(rig.initialize(config));

    auto target = cameraTarget({127.95, 3.0, 4.0});
    expectAccepted(rig.update(target, {}, 1.0f / 60.0f));
    double priorX = absolute(rig.pose().lookTargetPosition).x;
    bool crossed = false;
    for (uint32_t frame = 0u; frame < 180u; ++frame) {
        target.worldTargetPosition =
            physics::worldPositionFromAbsolute(
                {128.05, 3.0, 4.0});
        target.cameraSectorTargetPosition =
            {128.05f, 3.0f, 4.0f};
        expectAccepted(rig.update(
            target, {}, 1.0f / 60.0f));
        const auto& pose = rig.pose();
        ASSERT_TRUE(physics::isValidWorldPosition(
            pose.lookTargetPosition));
        ASSERT_TRUE(physics::isValidWorldPosition(
            pose.cameraPosition));
        const double currentX =
            absolute(pose.lookTargetPosition).x;
        EXPECT_GE(currentX + 1.0e-6, priorX);
        EXPECT_LT(currentX - priorX, 0.02);
        priorX = currentX;
        crossed = crossed
            || pose.lookTargetPosition.sector.x == 1;
    }
    EXPECT_TRUE(crossed);

    WreckwaterCameraObstructionProbeRequest request;
    expectAccepted(rig.takeObstructionProbeRequest(request));
    EXPECT_TRUE(physics::isValidWorldPosition(
        request.worldStart));
    EXPECT_TRUE(physics::isValidWorldPosition(
        request.worldEnd));
    EXPECT_EQ(request.worldStart.sector.x, 1);
    EXPECT_EQ(
        request.physicsQuery.sector,
        request.worldStart.sector);
    EXPECT_EQ(
        request.physicsQuery.origin,
        request.worldStart.local);
}

TEST(WreckwaterThirdPersonCamera,
     TeleportSnapsWithoutSweepingFromTheOldOrigin) {
    WreckwaterThirdPersonCameraRig rig;
    ASSERT_TRUE(rig.initialize({}));
    auto target = cameraTarget({10'000.0, 8.0, -3.0});
    expectAccepted(rig.update(target, {}, 1.0f / 60.0f));
    WreckwaterCameraObstructionProbeRequest oldRequest;
    expectAccepted(rig.takeObstructionProbeRequest(oldRequest));

    target.worldTargetPosition =
        physics::worldPositionFromAbsolute(
            {-10'000.0, 8.0, 6.0});
    target.cameraSectorTargetPosition =
        {-10'000.0f, 8.0f, 6.0f};
    target.teleported = true;
    const auto frame = rig.update(target, {}, 1.0f / 60.0f);
    expectAccepted(frame);
    EXPECT_TRUE(frame.snapped);
    EXPECT_FALSE(rig.probeOutstanding());
    EXPECT_NEAR(
        absolute(rig.pose().lookTargetPosition).x,
        -10'000.0, 1.0e-4);

    WreckwaterCameraObstructionProbeRequest newRequest;
    expectAccepted(rig.takeObstructionProbeRequest(newRequest));
    EXPECT_GT(
        newRequest.identity.sequence,
        oldRequest.identity.sequence);
    EXPECT_NEAR(
        absolute(newRequest.worldStart).x,
        -10'000.0, 1.0e-4);
    EXPECT_LT(newRequest.pathDistance, 10.0f);
    EXPECT_EQ(
        rig.resolveObstructionProbe({
            .identity = oldRequest.identity,
            .hitDistance = 1.0f,
            .hit = true,
            .completeScene = true,
        }),
        WreckwaterThirdPersonCameraStatus::StaleProbeResult);
}

TEST(WreckwaterThirdPersonCamera,
     RejectsInvalidConfigurationInputAndTargetAtomically) {
    WreckwaterThirdPersonCameraRig::Config badConfig;
    badConfig.maximumDeltaSeconds =
        std::numeric_limits<float>::quiet_NaN();
    WreckwaterThirdPersonCameraRig badRig;
    EXPECT_FALSE(badRig.initialize(badConfig));

    badConfig = {};
    badConfig.obstructionProbeRadius = 3.5f;
    badConfig.maximumProbeEndpointDrift = 0.75f;
    WreckwaterThirdPersonCameraRig oversizedCertificate;
    EXPECT_FALSE(oversizedCertificate.initialize(badConfig));

    badConfig = {};
    badConfig.maximumDeltaSeconds = 0.25f;
    badConfig.yawSpeedRadiansPerSecond = 13.0f;
    WreckwaterThirdPersonCameraRig ambiguousYawStep;
    EXPECT_FALSE(ambiguousYawStep.initialize(badConfig));

    WreckwaterThirdPersonCameraRig rig;
    ASSERT_TRUE(rig.initialize({}));
    auto target = cameraTarget({0.0, 2.0, 0.0});
    expectAccepted(rig.update(target, {}, 1.0f / 60.0f));
    const WreckwaterThirdPersonCameraPose acceptedPose =
        rig.pose();

    WreckwaterThirdPersonCameraInput invalidInput;
    invalidInput.orbit.x =
        std::numeric_limits<float>::infinity();
    EXPECT_EQ(
        rig.update(target, invalidInput, 1.0f / 60.0f).status,
        WreckwaterThirdPersonCameraStatus::InvalidInput);
    EXPECT_EQ(rig.pose(), acceptedPose);

    target.worldTargetPosition.local.x =
        physics::kWorldSectorHalf;
    EXPECT_EQ(
        rig.update(target, {}, 1.0f / 60.0f).status,
        WreckwaterThirdPersonCameraStatus::InvalidTarget);
    EXPECT_EQ(rig.pose(), acceptedPose);
    EXPECT_EQ(
        rig.update(
            cameraTarget({0.0, 2.0, 0.0}), {}, 0.0f)
            .status,
        WreckwaterThirdPersonCameraStatus::InvalidDeltaTime);
    EXPECT_EQ(rig.pose(), acceptedPose);
    EXPECT_EQ(rig.telemetry().framesRejected, 3u);
}

TEST(WreckwaterThirdPersonCamera,
     RejectsCameraPositionPastTheWorldSectorLimit) {
    WreckwaterThirdPersonCameraRig rig;
    ASSERT_TRUE(rig.initialize({}));
    auto target = cameraTarget({0.0, 0.0, 0.0});
    target.worldTargetPosition = {
        .sector = {
            std::numeric_limits<int32_t>::max(), 0, 0},
        .local = {127.9f, 0.0f, 0.0f},
    };
    target.cameraSectorTargetPosition = {127.9f, 0.0f, 0.0f};
    // Looking toward -X puts a conventional trailing camera on +X.
    target.facing = {-1.0f, 0.0f, 0.0f};

    EXPECT_EQ(
        rig.update(target, {}, 1.0f / 60.0f).status,
        WreckwaterThirdPersonCameraStatus::PositionOverflow);
    EXPECT_FALSE(rig.hasTarget());
    EXPECT_FALSE(rig.pose().valid);
}

TEST(WreckwaterThirdPersonCamera,
     ShoulderSwapIsRisingEdgeAndLimitsRemainBounded) {
    WreckwaterThirdPersonCameraRig::Config config;
    config.maximumProbeEndpointDrift = 1.0f;
    WreckwaterThirdPersonCameraRig rig;
    ASSERT_TRUE(rig.initialize(config));
    auto target = cameraTarget(
        {0.0, 2.0, 0.0}, {150.0f, 0.0f, 0.0f});
    expectAccepted(rig.update(target, {}, 1.0f / 60.0f));
    certifyMiss(rig);

    WreckwaterThirdPersonCameraInput input;
    input.orbit.y = 1.0f;
    input.zoom = 1.0f;
    input.shoulderSwapDown = true;
    expectAccepted(rig.update(target, input, 1.0f / 60.0f));
    certifyMiss(rig);
    EXPECT_EQ(
        rig.pose().shoulder, WreckwaterCameraShoulder::Left);
    for (uint32_t frame = 0u; frame < 600u; ++frame) {
        expectAccepted(rig.update(
            target, input, 1.0f / 60.0f));
        certifyMiss(rig);
    }
    EXPECT_EQ(
        rig.pose().shoulder, WreckwaterCameraShoulder::Left);
    const float maximumBoom =
        std::sqrt(8.0f * 8.0f + 0.55f * 0.55f);
    EXPECT_NEAR(
        rig.pose().unobstructedBoomDistance,
        maximumBoom, 1.0e-4f);
    for (uint32_t frame = 0u; frame < 120u; ++frame) {
        expectAccepted(rig.update(
            target, input, 1.0f / 60.0f));
        certifyMiss(rig);
    }
    EXPECT_NEAR(
        rig.pose().unobstructedBoomDistance,
        maximumBoom, 1.0e-4f);
    EXPECT_GE(rig.pose().fovYRadians, 0.79f);
    EXPECT_LE(rig.pose().fovYRadians, 1.40f);
    EXPECT_EQ(rig.telemetry().shoulderSwaps, 1u);

    input.shoulderSwapDown = false;
    expectAccepted(rig.update(target, input, 1.0f / 60.0f));
    certifyMiss(rig);
    input.shoulderSwapDown = true;
    expectAccepted(rig.update(target, input, 1.0f / 60.0f));
    EXPECT_EQ(
        rig.pose().shoulder, WreckwaterCameraShoulder::Right);
    EXPECT_EQ(rig.telemetry().shoulderSwaps, 2u);
}

TEST(WreckwaterThirdPersonCamera,
     ProbeCounterExhaustionFailsClosedWithoutWrapping) {
    WreckwaterThirdPersonCameraRig::Config exhaustedConfig;
    exhaustedConfig.initialProbeSequence =
        std::numeric_limits<uint64_t>::max();
    WreckwaterThirdPersonCameraRig exhausted;
    ASSERT_TRUE(exhausted.initialize(exhaustedConfig));
    expectAccepted(exhausted.update(
        cameraTarget({0.0, 2.0, 0.0}),
        {}, 1.0f / 60.0f));
    WreckwaterCameraObstructionProbeRequest output;
    EXPECT_EQ(
        exhausted.takeObstructionProbeRequest(output),
        WreckwaterThirdPersonCameraStatus::
            ProbeSequenceExhausted);
    EXPECT_FALSE(exhausted.probeOutstanding());
    EXPECT_EQ(
        exhausted.lastProbeSequence(),
        std::numeric_limits<uint64_t>::max());

    WreckwaterThirdPersonCameraRig::Config finalConfig;
    finalConfig.initialProbeSequence =
        std::numeric_limits<uint64_t>::max() - 1u;
    WreckwaterThirdPersonCameraRig final;
    ASSERT_TRUE(final.initialize(finalConfig));
    expectAccepted(final.update(
        cameraTarget({0.0, 2.0, 0.0}),
        {}, 1.0f / 60.0f));
    expectAccepted(final.takeObstructionProbeRequest(output));
    EXPECT_EQ(
        output.identity.sequence,
        std::numeric_limits<uint64_t>::max());
    expectAccepted(final.resolveObstructionProbe({
        .identity = output.identity,
        .hitDistance = 0.0f,
        .hit = false,
        .completeScene = true,
    }));
    EXPECT_EQ(
        final.takeObstructionProbeRequest(output),
        WreckwaterThirdPersonCameraStatus::
            ProbeSequenceExhausted);
}

} // namespace
} // namespace voxy::client
