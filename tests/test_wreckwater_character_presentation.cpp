#include "client/wreckwater_character_presentation.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
#include <span>
#include <utility>

#include <glm/geometric.hpp>

namespace voxy::client {
namespace {

TEST(WreckwaterAvatarPrimitiveProxyBatch,
     VisibleInstancesUsesOnlyPublishedCount) {
    WreckwaterAvatarPrimitiveProxyBatch batch;
    batch.count = 2u;
    EXPECT_EQ(batch.visibleInstances().size(), 2u);
    EXPECT_EQ(
        batch.visibleInstances().data(),
        batch.instances.data());

    batch.count = std::numeric_limits<uint32_t>::max();
    EXPECT_EQ(
        batch.visibleInstances().size(),
        batch.instances.size());
}

constexpr uint64_t kLocalPlayer = 1u;

static_assert(noexcept(
    std::declval<WreckwaterCharacterPresentation&>().update(
        std::declval<
            const WreckwaterCharacterController&>(),
        std::declval<
            const network::WreckwaterClientSample&>(),
        std::declval<const glm::ivec3&>())));
static_assert(noexcept(
    std::declval<WreckwaterCharacterPresentation&>()
        .rebaseCameraSector(
            std::declval<const glm::ivec3&>())));

struct VisualCharacter {
    network::WreckwaterCharacterState authoritative{};
    physics::WorldPosition visualFeet{};
    glm::vec3 visualVelocity{0.0f};
};

[[nodiscard]] glm::dvec3 absolute(
    const physics::WorldPosition& position) {
    return physics::worldPositionToAbsolute(position);
}

[[nodiscard]] network::WreckwaterCharacterState character(
    uint64_t playerId,
    const physics::WorldPosition& authoritativeFeet,
    const glm::vec3& authoritativeVelocity = glm::vec3(0.0f),
    uint32_t connectionGeneration = 1u,
    bool connected = true,
    game::WreckwaterCharacterMode mode =
        game::WreckwaterCharacterMode::Airborne,
    uint32_t characterHandle = 0u) {
    network::WreckwaterCharacterState result;
    result.characterHandle = characterHandle != 0u
        ? characterHandle
        : static_cast<uint32_t>(0x0001'0000u + playerId);
    result.stateFlags = static_cast<uint32_t>(mode)
        | network::kWreckwaterCharacterStateActiveFlag
        | (connected
            ? network::kWreckwaterCharacterStateConnectedFlag
            : 0u);
    result.playerId = playerId;
    result.connectionGeneration = connectionGeneration;
    result.sector = {
        authoritativeFeet.sector.x,
        authoritativeFeet.sector.y,
        authoritativeFeet.sector.z,
    };
    result.localFeetPosition = {
        authoritativeFeet.local.x,
        authoritativeFeet.local.y,
        authoritativeFeet.local.z,
    };
    result.worldVelocity = {
        authoritativeVelocity.x,
        authoritativeVelocity.y,
        authoritativeVelocity.z,
    };
    EXPECT_TRUE(
        network::canonicalizeWreckwaterCharacterState(result));
    return result;
}

[[nodiscard]] VisualCharacter visualCharacter(
    uint64_t playerId,
    const glm::dvec3& visualFeet,
    const glm::vec3& visualVelocity = glm::vec3(0.0f),
    uint32_t connectionGeneration = 1u,
    bool connected = true,
    game::WreckwaterCharacterMode mode =
        game::WreckwaterCharacterMode::Airborne,
    uint32_t characterHandle = 0u) {
    const physics::WorldPosition visual =
        physics::worldPositionFromAbsolute(visualFeet);
    return {
        .authoritative = character(
            playerId, visual, {}, connectionGeneration,
            connected, mode, characterHandle),
        .visualFeet = visual,
        .visualVelocity = visualVelocity,
    };
}

[[nodiscard]] std::array<VisualCharacter, 4u>
defaultCharacters() {
    return {{
        visualCharacter(1u, {90.0, 10.0, 0.0}),
        visualCharacter(2u, {20.0, 10.0, 0.0}),
        visualCharacter(3u, {30.0, 10.0, 0.0}),
        visualCharacter(4u, {40.0, 10.0, 0.0}),
    }};
}

[[nodiscard]] network::WreckwaterClientSample sample(
    uint64_t sequence,
    uint64_t tick,
    float fraction,
    const std::array<VisualCharacter, 4u>& characters) {
    network::WreckwaterClientSample result;
    result.requestedPhysicsTick = {
        .whole = tick, .fraction = fraction};
    result.evaluatedPhysicsTick = result.requestedPhysicsTick;
    result.authoritative.schemaVersion =
        network::kWreckwaterWireSchemaVersion;
    result.authoritative.flags =
        network::kWreckwaterCertifiedFullSnapshotFlag;
    result.authoritative.identity = {
        .sessionId = 101u,
        .matchId = 202u,
        .worldId = 303u,
        .worldEpoch = 4u,
        .authorityEpoch = 5u,
    };
    result.authoritative.snapshotSequence = sequence;
    result.authoritative.applicationTick = tick;
    result.authoritative.physicsEvidenceTick = tick;
    result.authoritative.phase = network::WreckwaterPhase::Live;
    result.characterCount =
        static_cast<uint32_t>(characters.size());
    for (size_t index = 0u; index < characters.size(); ++index) {
        auto& destination = result.characters[index];
        const auto& source = characters[index];
        destination.characterHandle =
            source.authoritative.characterHandle;
        destination.authoritativeState =
            source.authoritative;
        destination.motionMode =
            network::WreckwaterVisualMotionMode::Interpolated;
        destination.visualPose.sector = {
            source.visualFeet.sector.x,
            source.visualFeet.sector.y,
            source.visualFeet.sector.z,
        };
        destination.visualPose.localFeetPosition = {
            source.visualFeet.local.x,
            source.visualFeet.local.y,
            source.visualFeet.local.z,
        };
        destination.visualPose.worldVelocity = {
            source.visualVelocity.x,
            source.visualVelocity.y,
            source.visualVelocity.z,
        };
    }
    return result;
}

[[nodiscard]] WreckwaterCharacterPresentation::Config
presentationConfig() {
    WreckwaterCharacterPresentation::Config result;
    result.roster = {{
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
    result.localPlayerId = kLocalPlayer;
    result.facingSpeedThreshold = 0.1f;
    result.teleportDistance = 6.0f;
    return result;
}

[[nodiscard]] network::WreckwaterClientSample localControllerSample(
    uint64_t sequence,
    uint64_t tick,
    const physics::WorldPosition& feet,
    uint32_t generation = 1u,
    uint32_t handle = 0x0001'0001u) {
    auto characters = defaultCharacters();
    characters[0] = {
        .authoritative = character(
            kLocalPlayer, feet, {}, generation, true,
            game::WreckwaterCharacterMode::Airborne, handle),
        .visualFeet = feet,
    };
    auto result = sample(sequence, tick, 0.0f, characters);
    result.characterCount = 1u;
    return result;
}

[[nodiscard]] WreckwaterCharacterController boundController(
    const physics::WorldPosition& feet =
        physics::worldPositionFromAbsolute({1.0, 10.0, 0.0}),
    uint32_t generation = 1u,
    uint32_t handle = 0x0001'0001u) {
    WreckwaterCharacterController controller;
    WreckwaterCharacterController::Config config;
    config.localPlayerId = kLocalPlayer;
    config.movement.waterHeight = -100.0f;
    EXPECT_TRUE(controller.initialize(config));
    EXPECT_EQ(
        controller.receiveLatestCertifiedSample(
            localControllerSample(
                1u, 10u, feet, generation, handle)),
        WreckwaterCharacterControllerStatus::Accepted);
    return controller;
}

[[nodiscard]] const WreckwaterAvatarPresentation& avatar(
    const WreckwaterCharacterPresentation& presentation,
    uint64_t playerId) {
    for (const auto& candidate : presentation.avatars()) {
        if (candidate.playerId == playerId) return candidate;
    }
    ADD_FAILURE() << "missing presentation player " << playerId;
    return presentation.avatars().front();
}

void expectAccepted(
    WreckwaterCharacterPresentationStatus status) {
    EXPECT_EQ(
        status,
        WreckwaterCharacterPresentationStatus::Accepted)
        << wreckwaterCharacterPresentationStatusName(status);
}

TEST(WreckwaterCharacterPresentation,
     LocalRenderPoseOverridesOneSampleAndRemotesStayVisual) {
    auto controller = boundController();
    WreckwaterCharacterPresentation presentation;
    ASSERT_TRUE(
        presentation.initialize(presentationConfig()));

    auto characters = defaultCharacters();
    characters[1].authoritative =
        character(
            2u,
            physics::worldPositionFromAbsolute(
                {-50.0, 10.0, 0.0}));
    const auto frame = sample(1u, 10u, 0.25f, characters);
    expectAccepted(
        presentation.update(controller, frame, {0, 0, 0}));

    ASSERT_EQ(presentation.avatars().size(), 4u);
    EXPECT_EQ(presentation.visibleCount(), 4u);
    const auto& local = avatar(presentation, 1u);
    EXPECT_TRUE(local.localPredicted);
    EXPECT_NEAR(
        absolute(local.worldFeetPosition).x, 1.0, 1.0e-6);
    EXPECT_EQ(local.crew, game::CrewId::CrewOne);
    EXPECT_EQ(local.crewSlot, 0u);
    EXPECT_EQ(local.poseTick, 10u);
    EXPECT_EQ(local.poseFraction, 0.0f);

    const auto& remote = avatar(presentation, 2u);
    EXPECT_FALSE(remote.localPredicted);
    EXPECT_NEAR(
        absolute(remote.worldFeetPosition).x, 20.0, 1.0e-6);
    EXPECT_NEAR(
        remote.cameraSectorFeetPosition.x, 20.0f, 1.0e-6f);
    EXPECT_EQ(remote.poseTick, 10u);
    EXPECT_FLOAT_EQ(remote.poseFraction, 0.25f);

    uint32_t localCount = 0u;
    for (const auto& candidate : presentation.avatars()) {
        if (candidate.localPredicted) ++localCount;
    }
    EXPECT_EQ(localCount, 1u);
    EXPECT_EQ(
        presentation.telemetry().localPredictionOverrides, 1u);

    const auto& camera =
        presentation.thirdPersonCameraTarget();
    ASSERT_TRUE(camera.valid);
    EXPECT_NEAR(
        absolute(camera.worldTargetPosition).x, 1.0, 1.0e-6);
    EXPECT_NEAR(
        absolute(camera.worldTargetPosition).y, 11.35, 1.0e-5);
    EXPECT_EQ(
        presentation.primitiveProxyBatch().count, 4u);
    const auto& localProxy =
        presentation.primitiveProxyBatch().instances[0];
    EXPECT_EQ(
        localProxy.shape, physics::ThrowableShape::Capsule);
    EXPECT_NEAR(localProxy.position.x, 1.0f, 1.0e-6f);
    EXPECT_NEAR(localProxy.position.y, 10.9f, 1.0e-5f);
}

TEST(WreckwaterCharacterPresentation,
     LowSpeedRetainsFacingAndReconnectPurgesIt) {
    auto controller = boundController();
    WreckwaterCharacterPresentation presentation;
    ASSERT_TRUE(
        presentation.initialize(presentationConfig()));

    auto characters = defaultCharacters();
    characters[1].visualVelocity = {2.0f, 0.0f, 0.0f};
    expectAccepted(presentation.update(
        controller, sample(1u, 10u, 0.0f, characters),
        {0, 0, 0}));
    EXPECT_NEAR(avatar(presentation, 2u).facing.x, 1.0f, 1e-6f);
    EXPECT_NEAR(avatar(presentation, 2u).facing.z, 0.0f, 1e-6f);

    characters[1].visualFeet =
        physics::worldPositionFromAbsolute(
            {20.1, 10.0, 0.0});
    characters[1].visualVelocity = {0.0f, 0.0f, 0.01f};
    expectAccepted(presentation.update(
        controller, sample(2u, 11u, 0.0f, characters),
        {0, 0, 0}));
    EXPECT_NEAR(avatar(presentation, 2u).facing.x, 1.0f, 1e-6f);
    EXPECT_FALSE(avatar(presentation, 2u).teleported);
    EXPECT_FALSE(avatar(presentation, 2u).discontinuity);

    characters[1].authoritative.stateFlags &=
        ~network::kWreckwaterCharacterStateConnectedFlag;
    ASSERT_TRUE(
        network::canonicalizeWreckwaterCharacterState(
            characters[1].authoritative));
    expectAccepted(presentation.update(
        controller, sample(3u, 12u, 0.0f, characters),
        {0, 0, 0}));
    EXPECT_FALSE(avatar(presentation, 2u).visible);
    EXPECT_TRUE(avatar(presentation, 2u).discontinuity);

    characters[1] = visualCharacter(
        2u, {20.2, 10.0, 0.0}, {}, 2u);
    expectAccepted(presentation.update(
        controller, sample(4u, 13u, 0.0f, characters),
        {0, 0, 0}));
    const auto& reconnected = avatar(presentation, 2u);
    EXPECT_TRUE(reconnected.visible);
    EXPECT_TRUE(reconnected.teleported);
    EXPECT_TRUE(reconnected.discontinuity);
    EXPECT_NEAR(reconnected.facing.x, 0.0f, 1e-6f);
    EXPECT_NEAR(reconnected.facing.z, 1.0f, 1e-6f);
    EXPECT_EQ(reconnected.connectionGeneration, 2u);
    EXPECT_GE(presentation.telemetry().facingRetentions, 1u);
    EXPECT_GE(presentation.telemetry().visibilityPurges, 1u);
}

TEST(WreckwaterCharacterPresentation,
     SectorCrossingAndCameraRebaseDoNotCreateTeleport) {
    auto controller = boundController();
    WreckwaterCharacterPresentation presentation;
    auto config = presentationConfig();
    config.teleportDistance = 1.0f;
    ASSERT_TRUE(presentation.initialize(config));

    auto characters = defaultCharacters();
    characters[1].visualFeet = {
        .sector = {0, 0, 0},
        .local = {127.9f, 10.0f, 0.0f},
    };
    expectAccepted(presentation.update(
        controller, sample(1u, 10u, 0.0f, characters),
        {0, 0, 0}));

    characters[1].visualFeet = {
        .sector = {1, 0, 0},
        .local = {-127.9f, 10.0f, 0.0f},
    };
    const auto crossed = sample(2u, 11u, 0.0f, characters);
    expectAccepted(
        presentation.update(controller, crossed, {0, 0, 0}));
    EXPECT_FALSE(avatar(presentation, 2u).teleported);
    EXPECT_FALSE(avatar(presentation, 2u).discontinuity);
    EXPECT_NEAR(
        avatar(presentation, 2u)
            .cameraSectorFeetPosition.x,
        128.1f, 2.0e-5f);

    expectAccepted(
        presentation.rebaseCameraSector({1, 0, 0}));
    EXPECT_FALSE(avatar(presentation, 2u).teleported);
    EXPECT_FALSE(avatar(presentation, 2u).discontinuity);
    EXPECT_NEAR(
        avatar(presentation, 2u)
            .cameraSectorFeetPosition.x,
        -127.9f, 1.0e-5f);
    EXPECT_EQ(
        presentation.telemetry().cameraSectorRebases, 1u);
    const auto proxies =
        presentation.primitiveProxyBatch().visibleInstances();
    ASSERT_EQ(proxies.size(), 4u);
    for (const auto& proxy : proxies) {
        EXPECT_EQ(proxy.sector, (glm::ivec3{1, 0, 0}));
    }
    EXPECT_NEAR(proxies[1].position.x, -127.9f, 1.0e-5f);
}

TEST(WreckwaterCharacterPresentation,
     TeleportAndModeChangeRaiseDistinctDiscontinuities) {
    auto controller = boundController();
    WreckwaterCharacterPresentation presentation;
    ASSERT_TRUE(
        presentation.initialize(presentationConfig()));
    auto characters = defaultCharacters();
    expectAccepted(presentation.update(
        controller, sample(1u, 10u, 0.0f, characters),
        {0, 0, 0}));

    characters[2].visualFeet =
        physics::worldPositionFromAbsolute(
            {60.0, 10.0, 0.0});
    expectAccepted(presentation.update(
        controller, sample(2u, 11u, 0.0f, characters),
        {0, 0, 0}));
    EXPECT_TRUE(avatar(presentation, 3u).teleported);
    EXPECT_TRUE(avatar(presentation, 3u).discontinuity);

    characters[2].visualFeet =
        physics::worldPositionFromAbsolute(
            {60.1, 10.0, 0.0});
    characters[2].authoritative = character(
        3u, characters[2].visualFeet, {}, 1u, true,
        game::WreckwaterCharacterMode::Swimming);
    expectAccepted(presentation.update(
        controller, sample(3u, 12u, 0.0f, characters),
        {0, 0, 0}));
    EXPECT_FALSE(avatar(presentation, 3u).teleported);
    EXPECT_TRUE(avatar(presentation, 3u).discontinuity);
    EXPECT_EQ(
        avatar(presentation, 3u).mode,
        game::WreckwaterCharacterMode::Swimming);
}

TEST(WreckwaterCharacterPresentation,
     InvalidRosterAndMalformedUpdatesLeaveRosterAtomic) {
    auto invalidConfig = presentationConfig();
    invalidConfig.roster[0].playerId =
        invalidConfig.roster[1].playerId;
    WreckwaterCharacterPresentation invalid;
    EXPECT_FALSE(invalid.initialize(invalidConfig));

    auto controller = boundController();
    WreckwaterCharacterPresentation presentation;
    ASSERT_TRUE(
        presentation.initialize(presentationConfig()));
    const auto characters = defaultCharacters();
    expectAccepted(presentation.update(
        controller, sample(1u, 10u, 0.0f, characters),
        {0, 0, 0}));
    std::array<WreckwaterAvatarPresentation, 4u> before{};
    std::copy(
        presentation.avatars().begin(),
        presentation.avatars().end(), before.begin());

    auto duplicate = sample(2u, 11u, 0.0f, characters);
    duplicate.characters[3] = duplicate.characters[1];
    EXPECT_EQ(
        presentation.update(
            controller, duplicate, {0, 0, 0}),
        WreckwaterCharacterPresentationStatus::InvalidSample);
    EXPECT_TRUE(std::equal(
        presentation.avatars().begin(),
        presentation.avatars().end(), before.begin()));

    auto missingLocal = sample(2u, 11u, 0.0f, characters);
    for (uint32_t index = 0u; index < 3u; ++index) {
        missingLocal.characters[index] =
            missingLocal.characters[index + 1u];
    }
    missingLocal.characterCount = 3u;
    EXPECT_EQ(
        presentation.update(
            controller, missingLocal, {0, 0, 0}),
        WreckwaterCharacterPresentationStatus::
            LocalSampleMissing);
    EXPECT_TRUE(std::equal(
        presentation.avatars().begin(),
        presentation.avatars().end(), before.begin()));

    auto mismatchedLocal = sample(2u, 11u, 0.0f, characters);
    mismatchedLocal.characters[0].authoritativeState
        .connectionGeneration = 2u;
    ASSERT_TRUE(
        network::canonicalizeWreckwaterCharacterState(
            mismatchedLocal.characters[0]
                .authoritativeState));
    EXPECT_EQ(
        presentation.update(
            controller, mismatchedLocal, {0, 0, 0}),
        WreckwaterCharacterPresentationStatus::
            LocalIdentityMismatch);
    EXPECT_TRUE(std::equal(
        presentation.avatars().begin(),
        presentation.avatars().end(), before.begin()));
}

TEST(WreckwaterCharacterPresentation,
     FarCameraSectorFailsWithoutPartiallyRebasingRoster) {
    auto controller = boundController();
    WreckwaterCharacterPresentation presentation;
    auto config = presentationConfig();
    config.maximumCameraSectorDelta = 2u;
    ASSERT_TRUE(presentation.initialize(config));
    const auto characters = defaultCharacters();
    expectAccepted(presentation.update(
        controller, sample(1u, 10u, 0.0f, characters),
        {0, 0, 0}));
    const auto before = avatar(presentation, 2u);
    const auto beforeTarget =
        presentation.thirdPersonCameraTarget();
    const auto beforeProxies =
        presentation.primitiveProxyBatch();

    EXPECT_EQ(
        presentation.rebaseCameraSector({100, 0, 0}),
        WreckwaterCharacterPresentationStatus::
            PositionOverflow);
    EXPECT_EQ(avatar(presentation, 2u), before);
    EXPECT_EQ(
        presentation.thirdPersonCameraTarget(),
        beforeTarget);
    EXPECT_EQ(
        presentation.primitiveProxyBatch().count,
        beforeProxies.count);
    const auto& afterProxies =
        presentation.primitiveProxyBatch();
    for (size_t index = 0u;
         index < afterProxies.instances.size(); ++index) {
        EXPECT_EQ(
            afterProxies.instances[index].shape,
            beforeProxies.instances[index].shape);
        EXPECT_EQ(
            afterProxies.instances[index].position,
            beforeProxies.instances[index].position);
        EXPECT_EQ(
            afterProxies.instances[index].rotation,
            beforeProxies.instances[index].rotation);
        EXPECT_EQ(
            afterProxies.instances[index].dimensions,
            beforeProxies.instances[index].dimensions);
        EXPECT_EQ(
            afterProxies.instances[index].active,
            beforeProxies.instances[index].active);
        EXPECT_EQ(
            afterProxies.instances[index].sector,
            beforeProxies.instances[index].sector);
    }
    EXPECT_EQ(
        presentation.telemetry().cameraSectorRebases, 0u);
}

TEST(WreckwaterCharacterPresentation,
     IdenticalInputsRetainFacingDeterministically) {
    auto controller = boundController();
    WreckwaterCharacterPresentation first;
    WreckwaterCharacterPresentation second;
    ASSERT_TRUE(first.initialize(presentationConfig()));
    ASSERT_TRUE(second.initialize(presentationConfig()));

    auto characters = defaultCharacters();
    characters[3].visualVelocity = {-1.0f, 0.0f, 1.0f};
    const auto moving = sample(1u, 10u, 0.5f, characters);
    expectAccepted(first.update(controller, moving, {1, 0, -1}));
    expectAccepted(second.update(controller, moving, {1, 0, -1}));

    characters[3].visualVelocity = {0.0f, 0.0f, 0.0f};
    characters[3].visualFeet =
        physics::worldPositionFromAbsolute(
            {40.05, 10.0, 0.0});
    const auto stopped = sample(2u, 11u, 0.25f, characters);
    expectAccepted(first.update(controller, stopped, {1, 0, -1}));
    expectAccepted(second.update(controller, stopped, {1, 0, -1}));

    EXPECT_TRUE(std::equal(
        first.avatars().begin(), first.avatars().end(),
        second.avatars().begin()));
    EXPECT_EQ(
        first.thirdPersonCameraTarget(),
        second.thirdPersonCameraTarget());
    EXPECT_EQ(first.telemetry(), second.telemetry());
    EXPECT_NEAR(
        avatar(first, 4u).facing.x,
        -glm::inversesqrt(2.0f), 1.0e-6f);
    EXPECT_NEAR(
        avatar(first, 4u).facing.z,
        glm::inversesqrt(2.0f), 1.0e-6f);
}

} // namespace
} // namespace voxy::client
