#include <gtest/gtest.h>

#include "game/wreckwater_vessel_damage.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>

namespace voxy::game {
namespace {

std::array<WreckwaterVesselPose, kWreckwaterDamageSkiffCount>
vesselPoses(float height = -2.0f) {
    return {{
        {
            .skiffId = 1u,
            .skiffGeneration = 1u,
            .body = {1u, 1u},
            .position = {.local = {-12.0f, height, 0.0f}},
        },
        {
            .skiffId = 2u,
            .skiffGeneration = 1u,
            .body = {2u, 1u},
            .position = {.local = {12.0f, height, 0.0f}},
        },
    }};
}

WreckwaterContactHitEvidence hit(
    uint64_t tick, uint32_t source, physics::BodyHandle skiff,
    glm::vec3 localAnchor, float impulse = 10.0f) {
    WreckwaterContactHitEvidence result{
        .physicsTick = tick,
        .sourceId = source,
        .bodyA = skiff,
        .bodyB = {3u, 1u},
        .featureA = source * 2u,
        .featureB = source * 2u + 1u,
        .localAnchorA = localAnchor,
        .localAnchorB = {0.0f, 0.0f, 0.0f},
        .normalAtoB = {1.0f, 0.0f, 0.0f},
        .normalImpulse = impulse,
        .impactSpeed = 4.0f,
        .materialA = WreckwaterImpactMaterial::Timber,
        .materialB = WreckwaterImpactMaterial::Steel,
    };
    EXPECT_TRUE(canonicalizeWreckwaterContactHit(result));
    return result;
}

WreckwaterVesselDamageAuthority::Config testConfig() {
    WreckwaterVesselDamageAuthority::Config config;
    config.edgeBreakImpulse = 1'000.0f;
    config.respawnDelayPhysicsTicks = 3u;
    config.significantFragmentLifetimeTicks = 30u;
    return config;
}

TEST(
    WreckwaterVesselDamageAuthorityTest,
    InitializesFixedCapacityGenerationalTopology) {
    WreckwaterVesselDamageAuthority authority;
    const auto poses = vesselPoses();
    ASSERT_TRUE(authority.initialize(testConfig(), poses));
    EXPECT_EQ(
        kWreckwaterMaximumDamageLifecycleIntentsPerTick,
        kWreckwaterMaximumSignificantFragments
            + kWreckwaterDamageSkiffCount);

    ASSERT_EQ(
        authority.vessels().size(),
        kWreckwaterDamageSkiffCount);
    ASSERT_EQ(
        authority.compartments().size(),
        kWreckwaterMaximumCompartments);
    ASSERT_EQ(
        authority.structuralEdges().size(),
        kWreckwaterMaximumStructuralEdges);
    ASSERT_EQ(
        authority.breaches().size(),
        kWreckwaterMaximumBreaches);
    ASSERT_EQ(
        authority.significantFragments().size(),
        kWreckwaterMaximumSignificantFragments);
    for (const WreckwaterCompartmentState& compartment
         : authority.compartments()) {
        EXPECT_TRUE(compartment.active);
        EXPECT_TRUE(compartment.handle.valid());
        EXPECT_FLOAT_EQ(compartment.waterCubicMetres, 0.0f);
    }
    for (const WreckwaterStructuralEdgeState& edge
         : authority.structuralEdges()) {
        EXPECT_TRUE(edge.active);
        EXPECT_TRUE(edge.handle.valid());
        EXPECT_FALSE(edge.broken);
    }
    for (const WreckwaterBreachState& breach
         : authority.breaches()) {
        EXPECT_TRUE(breach.handle.valid());
        EXPECT_FALSE(breach.active);
    }
}

TEST(
    WreckwaterVesselDamageAuthorityTest,
    RejectsDuplicatePhysicalBodyAuthority) {
    WreckwaterVesselDamageAuthority authority;
    auto poses = vesselPoses();
    poses[1].body = poses[0].body;
    EXPECT_FALSE(authority.initialize(testConfig(), poses));
}

TEST(
    WreckwaterVesselDamageAuthorityTest,
    CanonicalContactSwapPreservesBodyRelativeEvidence) {
    WreckwaterContactHitEvidence evidence{
        .physicsTick = 9u,
        .sourceId = 7u,
        .bodyA = {9u, 3u},
        .bodyB = {2u, 8u},
        .featureA = 11u,
        .featureB = 22u,
        .localAnchorA = {1.0f, 2.0f, 3.0f},
        .localAnchorB = {4.0f, 5.0f, 6.0f},
        .normalAtoB = {2.0f, 0.0f, 0.0f},
        .normalImpulse = 4.0f,
        .impactSpeed = 3.0f,
        .materialA = WreckwaterImpactMaterial::Steel,
        .materialB = WreckwaterImpactMaterial::Timber,
    };
    ASSERT_TRUE(canonicalizeWreckwaterContactHit(evidence));
    EXPECT_EQ(evidence.bodyA, (physics::BodyHandle{2u, 8u}));
    EXPECT_EQ(evidence.bodyB, (physics::BodyHandle{9u, 3u}));
    EXPECT_EQ(evidence.featureA, 22u);
    EXPECT_EQ(evidence.featureB, 11u);
    EXPECT_EQ(evidence.localAnchorA, glm::vec3(4.0f, 5.0f, 6.0f));
    EXPECT_EQ(evidence.localAnchorB, glm::vec3(1.0f, 2.0f, 3.0f));
    EXPECT_EQ(evidence.normalAtoB, glm::vec3(-1.0f, 0.0f, 0.0f));
    EXPECT_EQ(
        evidence.materialA, WreckwaterImpactMaterial::Timber);
    EXPECT_EQ(
        evidence.materialB, WreckwaterImpactMaterial::Steel);
}

TEST(
    WreckwaterVesselDamageAuthorityTest,
    ContactInsertionOrderProducesIdenticalState) {
    auto config = testConfig();
    config.edgeBreakImpulse = 100.0f;
    const auto poses = vesselPoses(5.0f);
    WreckwaterVesselDamageAuthority first;
    WreckwaterVesselDamageAuthority second;
    ASSERT_TRUE(first.initialize(config, poses));
    ASSERT_TRUE(second.initialize(config, poses));

    const std::array contacts{
        hit(1u, 10u, {1u, 1u}, {-2.0f, -0.3f, 2.0f}, 20.0f),
        hit(1u, 11u, {1u, 1u}, {2.0f, -0.3f, -2.0f}, 35.0f),
        hit(1u, 12u, {2u, 1u}, {-2.0f, -0.3f, -2.0f}, 15.0f),
    };
    const std::array reversed{
        contacts[2], contacts[0], contacts[1]};
    const auto firstResult = first.closeExactTick({
        .physicsTick = 1u,
        .vessels = poses,
        .contacts = contacts,
    });
    const auto secondResult = second.closeExactTick({
        .physicsTick = 1u,
        .vessels = poses,
        .contacts = reversed,
    });
    ASSERT_TRUE(firstResult);
    ASSERT_TRUE(secondResult);
    EXPECT_EQ(first.stateHash(), second.stateHash());
    EXPECT_TRUE(std::equal(
        first.structuralEdges().begin(),
        first.structuralEdges().end(),
        second.structuralEdges().begin(),
        [](const auto& lhs, const auto& rhs) {
            return lhs.handle == rhs.handle
                && lhs.accumulatedDamageMilliImpulse
                    == rhs.accumulatedDamageMilliImpulse
                && lhs.broken == rhs.broken;
        }));
}

TEST(
    WreckwaterVesselDamageAuthorityTest,
    ContactCapacityDuplicateAndStaleIdentityFailClosed) {
    const auto poses = vesselPoses(5.0f);
    {
        WreckwaterVesselDamageAuthority authority;
        ASSERT_TRUE(authority.initialize(testConfig(), poses));
        std::array<WreckwaterContactHitEvidence,
            kWreckwaterMaximumContactHitsPerTick + 1u> contacts{};
        for (size_t index = 0u; index < contacts.size(); ++index) {
            contacts[index] = hit(
                1u, static_cast<uint32_t>(index + 1u),
                {1u, 1u}, {-2.0f, 0.0f, 2.0f});
        }
        const auto result = authority.closeExactTick({
            .physicsTick = 1u,
            .vessels = poses,
            .contacts = contacts,
        });
        EXPECT_EQ(
            result.status,
            WreckwaterVesselDamageStatus::
                ContactCapacityExceeded);
        EXPECT_EQ(authority.lastClosedPhysicsTick(), 0u);
    }
    {
        WreckwaterVesselDamageAuthority authority;
        ASSERT_TRUE(authority.initialize(testConfig(), poses));
        const auto one =
            hit(1u, 3u, {1u, 1u}, {-2.0f, 0.0f, 2.0f});
        const std::array duplicate{one, one};
        const auto result = authority.closeExactTick({
            .physicsTick = 1u,
            .vessels = poses,
            .contacts = duplicate,
        });
        EXPECT_EQ(
            result.status,
            WreckwaterVesselDamageStatus::
                DuplicateContactIdentity);
    }
    {
        WreckwaterVesselDamageAuthority authority;
        ASSERT_TRUE(authority.initialize(testConfig(), poses));
        const std::array stale{
            hit(1u, 4u, {1u, 2u}, {-2.0f, 0.0f, 2.0f})};
        const auto result = authority.closeExactTick({
            .physicsTick = 1u,
            .vessels = poses,
            .contacts = stale,
        });
        EXPECT_EQ(
            result.status,
            WreckwaterVesselDamageStatus::StaleBodyIdentity);
    }
}

TEST(
    WreckwaterVesselDamageAuthorityTest,
    ExtremeFiniteImpulseQuantizationSaturatesWithoutOverflow) {
    auto config = testConfig();
    config.edgeBreakImpulse = std::numeric_limits<float>::max();
    const auto poses = vesselPoses(20.0f);
    WreckwaterVesselDamageAuthority authority;
    ASSERT_TRUE(authority.initialize(config, poses));
    std::array extreme{
        hit(1u, 1u, {1u, 1u}, {-2.0f, 0.0f, 2.0f},
            std::numeric_limits<float>::max())};
    extreme[0].impactSpeed = std::numeric_limits<float>::max();
    extreme[0].materialB = WreckwaterImpactMaterial::Rock;
    const auto result = authority.closeExactTick({
        .physicsTick = 1u,
        .vessels = poses,
        .contacts = extreme,
    });
    ASSERT_TRUE(result);
    EXPECT_EQ(result.brokenEdgeCount, 1u);
    const auto broken = std::find_if(
        authority.structuralEdges().begin(),
        authority.structuralEdges().end(),
        [](const auto& edge) { return edge.broken; });
    ASSERT_NE(broken, authority.structuralEdges().end());
    EXPECT_EQ(
        broken->accumulatedDamageMilliImpulse,
        std::numeric_limits<uint64_t>::max());
    EXPECT_EQ(
        broken->breakDamageMilliImpulse,
        std::numeric_limits<uint64_t>::max());
}

TEST(
    WreckwaterVesselDamageAuthorityTest,
    FractureBreachesFloodsLoadsOffCenterSinksThenRespawnsFresh) {
    auto config = testConfig();
    config.edgeBreakImpulse = 1.0f;
    config.breachAreaSquareMetres = 0.5f;
    config.compartmentCapacityCubicMetres = 0.01f;
    config.sinkFloodedFraction = 0.20f;
    config.respawnDelayPhysicsTicks = 2u;
    config.significantFragmentLifetimeTicks = 100u;
    auto poses = vesselPoses(-3.0f);
    WreckwaterVesselDamageAuthority authority;
    ASSERT_TRUE(authority.initialize(config, poses));

    const std::array impact{
        hit(1u, 1u, {1u, 1u}, {-2.0f, -0.3f, 2.0f}, 5.0f)};
    const auto fractured = authority.closeExactTick({
        .physicsTick = 1u,
        .vessels = poses,
        .contacts = impact,
    });
    ASSERT_TRUE(fractured)
        << wreckwaterVesselDamageStatusName(fractured.status);
    EXPECT_EQ(fractured.brokenEdgeCount, 1u);
    EXPECT_EQ(fractured.openedBreachCount, 1u);
    ASSERT_EQ(fractured.floodForces.size(), 1u);
    const WreckwaterFloodForceIntent load =
        fractured.floodForces.front();
    EXPECT_LT(load.localPoint.x, 0.0f);
    EXPECT_LT(load.worldForce.y, 0.0f);
    EXPECT_NE(
        glm::cross(load.localPoint, load.worldForce).z, 0.0f)
        << "off-centre floodwater must create roll torque";
    ASSERT_EQ(fractured.lifecycleIntents.size(), 2u);
    EXPECT_TRUE(std::any_of(
        fractured.lifecycleIntents.begin(),
        fractured.lifecycleIntents.end(), [](const auto& intent) {
            return intent.type
                == WreckwaterDamageLifecycleIntentType::
                    SpawnSignificantFragment;
        }));
    EXPECT_TRUE(std::any_of(
        fractured.lifecycleIntents.begin(),
        fractured.lifecycleIntents.end(), [](const auto& intent) {
            return intent.type
                == WreckwaterDamageLifecycleIntentType::SkiffSunk;
        }));
    EXPECT_EQ(
        authority.vessels()[0].lifecycle,
        WreckwaterVesselLifecycle::SunkWaitingForRespawn);

    poses[0].available = false;
    ASSERT_TRUE(authority.closeExactTick({
        .physicsTick = 2u,
        .vessels = poses,
    }));
    const auto due = authority.closeExactTick({
        .physicsTick = 3u,
        .vessels = poses,
    });
    ASSERT_TRUE(due);
    const auto respawn = std::find_if(
        due.lifecycleIntents.begin(), due.lifecycleIntents.end(),
        [](const auto& intent) {
            return intent.type
                == WreckwaterDamageLifecycleIntentType::SkiffRespawn;
        });
    ASSERT_NE(respawn, due.lifecycleIntents.end());
    EXPECT_EQ(respawn->nextSkiffGeneration, 2u);

    const auto oldCompartment =
        authority.compartments().front().handle;
    ASSERT_EQ(
        authority.confirmRespawn(
            1u, 1u, 2u, {1u, 2u}),
        WreckwaterVesselDamageStatus::Accepted);
    EXPECT_EQ(
        authority.vessels()[0].lifecycle,
        WreckwaterVesselLifecycle::Active);
    EXPECT_EQ(authority.vessels()[0].body,
              (physics::BodyHandle{1u, 2u}));
    EXPECT_EQ(
        authority.compartments().front().handle.generation,
        oldCompartment.generation + 1u);
    EXPECT_FLOAT_EQ(
        authority.compartments().front().waterCubicMetres, 0.0f);
    EXPECT_FALSE(authority.breaches().front().active);
}

TEST(
    WreckwaterVesselDamageAuthorityTest,
    SignificantFragmentsAreBoundedAndRetireDeterministically) {
    auto config = testConfig();
    config.edgeBreakImpulse = 1.0f;
    config.significantFragmentLifetimeTicks = 1u;
    const auto poses = vesselPoses(20.0f);
    WreckwaterVesselDamageAuthority authority;
    ASSERT_TRUE(authority.initialize(config, poses));

    const std::array<glm::vec3, 4> anchors{{
        {-2.0f, -0.3f, 2.0f},
        {2.0f, -0.3f, 2.0f},
        {-2.0f, -0.3f, -2.0f},
        {2.0f, -0.3f, -2.0f},
    }};
    std::array<WreckwaterContactHitEvidence, 8> contacts{};
    for (size_t index = 0u; index < contacts.size(); ++index) {
        contacts[index] = hit(
            1u, static_cast<uint32_t>(index + 1u),
            {static_cast<uint32_t>(index / 4u + 1u), 1u},
            anchors[index % 4u], 5.0f);
    }
    const auto broken = authority.closeExactTick({
        .physicsTick = 1u,
        .vessels = poses,
        .contacts = contacts,
    });
    ASSERT_TRUE(broken);
    EXPECT_EQ(broken.brokenEdgeCount, 8u);
    EXPECT_EQ(broken.suppressedFragmentCount, 0u);
    EXPECT_EQ(
        std::count_if(
            authority.significantFragments().begin(),
            authority.significantFragments().end(),
            [](const auto& fragment) { return fragment.active; }),
        8);

    const auto retired = authority.closeExactTick({
        .physicsTick = 2u,
        .vessels = poses,
    });
    ASSERT_TRUE(retired);
    EXPECT_EQ(retired.lifecycleIntents.size(), 8u);
    EXPECT_TRUE(std::all_of(
        retired.lifecycleIntents.begin(),
        retired.lifecycleIntents.end(), [](const auto& intent) {
            return intent.type
                == WreckwaterDamageLifecycleIntentType::
                    RetireSignificantFragment;
        }));
    EXPECT_EQ(
        std::count_if(
            authority.significantFragments().begin(),
            authority.significantFragments().end(),
            [](const auto& fragment) { return fragment.active; }),
        0);
}

TEST(
    WreckwaterVesselDamageAuthorityTest,
    MixedRetireAndSpawnRespectsProvenLifecycleBound) {
    auto config = testConfig();
    config.edgeBreakImpulse = 1.0f;
    config.significantFragmentLifetimeTicks = 1u;
    const auto poses = vesselPoses(20.0f);
    WreckwaterVesselDamageAuthority authority;
    ASSERT_TRUE(authority.initialize(config, poses));
    const std::array<glm::vec3, 4> anchors{{
        {-2.0f, -0.3f, 2.0f},
        {2.0f, -0.3f, 2.0f},
        {-2.0f, -0.3f, -2.0f},
        {2.0f, -0.3f, -2.0f},
    }};
    std::array<WreckwaterContactHitEvidence, 4> firstHits{};
    std::array<WreckwaterContactHitEvidence, 4> secondHits{};
    for (size_t index = 0u; index < anchors.size(); ++index) {
        firstHits[index] = hit(
            1u, static_cast<uint32_t>(index + 1u),
            {1u, 1u}, anchors[index], 5.0f);
        secondHits[index] = hit(
            2u, static_cast<uint32_t>(index + 5u),
            {2u, 1u}, anchors[index], 5.0f);
    }
    const auto first = authority.closeExactTick({
        .physicsTick = 1u,
        .vessels = poses,
        .contacts = firstHits,
    });
    ASSERT_TRUE(first);
    ASSERT_EQ(first.lifecycleIntents.size(), 4u);

    const auto mixed = authority.closeExactTick({
        .physicsTick = 2u,
        .vessels = poses,
        .contacts = secondHits,
    });
    ASSERT_TRUE(mixed);
    EXPECT_EQ(mixed.lifecycleIntents.size(), 8u);
    EXPECT_LE(
        mixed.lifecycleIntents.size(),
        kWreckwaterMaximumDamageLifecycleIntentsPerTick);
    EXPECT_EQ(
        std::count_if(
            mixed.lifecycleIntents.begin(),
            mixed.lifecycleIntents.end(), [](const auto& intent) {
                return intent.type
                    == WreckwaterDamageLifecycleIntentType::
                        RetireSignificantFragment;
            }),
        4);
    EXPECT_EQ(
        std::count_if(
            mixed.lifecycleIntents.begin(),
            mixed.lifecycleIntents.end(), [](const auto& intent) {
                return intent.type
                    == WreckwaterDamageLifecycleIntentType::
                        SpawnSignificantFragment;
            }),
        4);
}

TEST(
    WreckwaterVesselDamageAuthorityTest,
    GenerationExhaustionDoesNotPartiallyAdvanceTopology) {
    auto config = testConfig();
    config.edgeBreakImpulse = 1.0f;
    config.breachAreaSquareMetres = 0.5f;
    config.compartmentCapacityCubicMetres = 0.01f;
    config.sinkFloodedFraction = 0.20f;
    config.respawnDelayPhysicsTicks = 1u;
    config.significantFragmentLifetimeTicks = 100u;
    auto poses = vesselPoses(-3.0f);
    poses[0].skiffGeneration =
        std::numeric_limits<uint32_t>::max();
    WreckwaterVesselDamageAuthority authority;
    ASSERT_TRUE(authority.initialize(config, poses));
    const std::array impact{
        hit(1u, 1u, {1u, 1u}, {-2.0f, -0.3f, 2.0f}, 5.0f)};
    ASSERT_TRUE(authority.closeExactTick({
        .physicsTick = 1u,
        .vessels = poses,
        .contacts = impact,
    }));
    const auto priorCompartments = std::array{
        authority.compartments()[0],
        authority.compartments()[1],
        authority.compartments()[2],
        authority.compartments()[3],
    };
    poses[0].available = false;
    const auto exhausted = authority.closeExactTick({
        .physicsTick = 2u,
        .vessels = poses,
    });
    EXPECT_EQ(
        exhausted.status,
        WreckwaterVesselDamageStatus::IdentityExhausted);
    for (size_t index = 0u; index < priorCompartments.size(); ++index) {
        EXPECT_EQ(
            authority.compartments()[index].handle,
            priorCompartments[index].handle);
        EXPECT_FLOAT_EQ(
            authority.compartments()[index].waterCubicMetres,
            priorCompartments[index].waterCubicMetres);
    }
    EXPECT_EQ(
        authority.vessels()[0].lifecycle,
        WreckwaterVesselLifecycle::SunkWaitingForRespawn);
}

} // namespace
} // namespace voxy::game
