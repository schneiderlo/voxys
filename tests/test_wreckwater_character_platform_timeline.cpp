#include "client/wreckwater_character_platform_timeline.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
#include <span>
#include <utility>

#include <glm/geometric.hpp>
#include <glm/gtc/quaternion.hpp>

namespace voxy::client {
namespace {

static_assert(noexcept(
    std::declval<WreckwaterCharacterPlatformTimeline&>()
        .replaceCertifiedBase(
            1u, 1u,
            std::declval<
                std::span<
                    const game::
                        WreckwaterCharacterPlatformSample>>(),
            1u)));
static_assert(noexcept(
    std::declval<WreckwaterCharacterPlatformTimeline&>()
        .generateThrough(1u)));

[[nodiscard]] glm::dvec3 absolute(
    const physics::WorldPosition& position) {
    return physics::worldPositionToAbsolute(position);
}

[[nodiscard]] game::WreckwaterCharacterPlatformSample platform(
    game::SkiffId skiffId,
    const glm::dvec3& position,
    const glm::vec3& linearVelocity = glm::vec3(0.0f),
    const glm::vec3& angularVelocity = glm::vec3(0.0f),
    uint32_t generation = 1u) {
    return {
        .skiffId = skiffId,
        .skiffGeneration = generation,
        .body = {
            .index = 0x8000'0000u | skiffId,
            .generation = generation,
        },
        .position = physics::worldPositionFromAbsolute(position),
        .orientation =
            glm::quat(1.0f, 0.0f, 0.0f, 0.0f),
        .linearVelocity = linearVelocity,
        .angularVelocity = angularVelocity,
        .deckHalfExtents = {4.0f, 3.0f},
        .deckLocalHeight = 0.5f,
    };
}

[[nodiscard]] WreckwaterCharacterPlatformTimeline::Config config(
    uint32_t horizon =
        kWreckwaterCharacterPlatformTimelineDefaultPredictionTicks) {
    WreckwaterCharacterPlatformTimeline::Config result;
    result.predictionHorizonTicks = horizon;
    return result;
}

void expectAccepted(
    WreckwaterCharacterPlatformTimelineStatus status) {
    EXPECT_EQ(
        status,
        WreckwaterCharacterPlatformTimelineStatus::Accepted)
        << wreckwaterCharacterPlatformTimelineStatusName(status);
}

TEST(WreckwaterCharacterPlatformTimeline,
     EmitsExactSixtyHertzTranslationAndRotationAcrossSector) {
    WreckwaterCharacterPlatformTimeline timeline;
    ASSERT_TRUE(timeline.initialize(config()));
    constexpr float angularSpeed = 12.0f;
    const auto base = platform(
        11u, {127.99, 2.0, 0.0},
        {2.0f, 0.0f, 0.0f},
        {0.0f, angularSpeed, 0.0f});

    expectAccepted(timeline.replaceCertifiedBase(
        7u, 100u, std::span(&base, 1u), 100u));
    expectAccepted(timeline.generateThrough(101u));
    const auto frame = timeline.frame(101u);
    ASSERT_TRUE(frame);
    ASSERT_EQ(frame.platforms.size(), 1u);
    const auto& emitted = frame.platforms[0];

    EXPECT_EQ(frame.tick, 101u);
    EXPECT_EQ(frame.certifiedSnapshotSequence, 7u);
    EXPECT_EQ(frame.certifiedBaseTick, 100u);
    EXPECT_EQ(emitted.skiffId, base.skiffId);
    EXPECT_EQ(emitted.skiffGeneration, base.skiffGeneration);
    EXPECT_EQ(emitted.body, base.body);
    EXPECT_EQ(emitted.deckHalfExtents, base.deckHalfExtents);
    EXPECT_EQ(emitted.deckLocalHeight, base.deckLocalHeight);
    EXPECT_EQ(emitted.linearVelocity, base.linearVelocity);
    EXPECT_EQ(emitted.angularVelocity, base.angularVelocity);
    EXPECT_EQ(emitted.position.sector.x, 1);
    EXPECT_NEAR(
        absolute(emitted.position).x,
        absolute(base.position).x + 2.0 / 60.0, 4.0e-6);

    const glm::quat expected = glm::angleAxis(
        angularSpeed / 60.0f,
        glm::vec3(0.0f, 1.0f, 0.0f));
    EXPECT_NEAR(emitted.orientation.w, expected.w, 1.0e-7f);
    EXPECT_NEAR(emitted.orientation.x, expected.x, 1.0e-7f);
    EXPECT_NEAR(emitted.orientation.y, expected.y, 1.0e-7f);
    EXPECT_NEAR(emitted.orientation.z, expected.z, 1.0e-7f);
}

TEST(WreckwaterCharacterPlatformTimeline,
     CorrectionReplacesFutureAndImpossibleCorrectionIsAtomic) {
    WreckwaterCharacterPlatformTimeline timeline;
    ASSERT_TRUE(timeline.initialize(config(8u)));
    const auto base = platform(
        11u, {0.0, 2.0, 0.0}, {6.0f, 0.0f, 0.0f});
    expectAccepted(timeline.replaceCertifiedBase(
        1u, 100u, std::span(&base, 1u), 105u));
    const auto staleFuture = timeline.frame(105u);
    ASSERT_TRUE(staleFuture);
    const double staleX =
        absolute(staleFuture.platforms[0].position).x;

    auto correction = base;
    correction.position = physics::worldPositionFromAbsolute(
        {0.205, 2.0, 0.0});
    correction.linearVelocity = {6.5f, 0.0f, 0.0f};
    expectAccepted(timeline.replaceCertifiedBase(
        2u, 102u, std::span(&correction, 1u), 105u));
    const auto correctedFuture = timeline.frame(105u);
    ASSERT_TRUE(correctedFuture);
    const double correctedX =
        absolute(correctedFuture.platforms[0].position).x;
    EXPECT_NE(correctedX, staleX);
    EXPECT_NEAR(
        correctedX, 0.205 + 3.0 * 6.5 / 60.0, 1.0e-6);
    EXPECT_EQ(
        correctedFuture.certifiedSnapshotSequence, 2u);
    EXPECT_EQ(correctedFuture.certifiedBaseTick, 102u);

    const auto acceptedFuture = correctedFuture.platforms[0];
    auto impossible = correction;
    impossible.position = physics::worldPositionFromAbsolute(
        {100.0, 2.0, 0.0});
    EXPECT_EQ(
        timeline.replaceCertifiedBase(
            3u, 103u, std::span(&impossible, 1u), 105u),
        WreckwaterCharacterPlatformTimelineStatus::
            ImpossibleCorrection);
    const auto afterRejected = timeline.frame(105u);
    ASSERT_TRUE(afterRejected);
    EXPECT_EQ(afterRejected.platforms[0], acceptedFuture);
    EXPECT_EQ(
        afterRejected.certifiedSnapshotSequence, 2u);

    EXPECT_EQ(
        timeline.replaceCertifiedBase(
            2u, 103u, std::span(&correction, 1u), 105u),
        WreckwaterCharacterPlatformTimelineStatus::
            StaleCertifiedSnapshot);
    EXPECT_EQ(
        timeline.frame(105u).platforms[0], acceptedFuture);
}

TEST(WreckwaterCharacterPlatformTimeline,
     DisappearanceReplacementAndDeckMutationFailClosed) {
    WreckwaterCharacterPlatformTimeline timeline;
    ASSERT_TRUE(timeline.initialize(config(8u)));
    const auto base = platform(11u, {0.0, 2.0, 0.0});
    expectAccepted(timeline.replaceCertifiedBase(
        1u, 10u, std::span(&base, 1u), 12u));
    const auto before = timeline.frame(12u).platforms[0];

    EXPECT_EQ(
        timeline.replaceCertifiedBase(2u, 11u, {}, 12u),
        WreckwaterCharacterPlatformTimelineStatus::
            PlatformLifetimeMutation);
    EXPECT_EQ(timeline.frame(12u).platforms[0], before);

    auto replacement = base;
    replacement.skiffGeneration = 2u;
    replacement.body.generation = 2u;
    EXPECT_EQ(
        timeline.replaceCertifiedBase(
            2u, 11u, std::span(&replacement, 1u), 12u),
        WreckwaterCharacterPlatformTimelineStatus::
            PlatformLifetimeMutation);

    auto changedDeck = base;
    changedDeck.deckHalfExtents.x += 1.0f;
    EXPECT_EQ(
        timeline.replaceCertifiedBase(
            2u, 11u, std::span(&changedDeck, 1u), 12u),
        WreckwaterCharacterPlatformTimelineStatus::
            PlatformLifetimeMutation);
    EXPECT_EQ(timeline.frame(12u).platforms[0], before);
}

TEST(WreckwaterCharacterPlatformTimeline,
     RejectsHorizonCapacityAndCounterExhaustion) {
    WreckwaterCharacterPlatformTimeline timeline;
    ASSERT_TRUE(timeline.initialize(config(2u)));
    const auto base = platform(11u, {0.0, 2.0, 0.0});
    expectAccepted(timeline.replaceCertifiedBase(
        1u, 10u, std::span(&base, 1u), 10u));
    expectAccepted(timeline.generateThrough(12u));
    EXPECT_EQ(
        timeline.generateThrough(13u),
        WreckwaterCharacterPlatformTimelineStatus::
            PredictionHorizonExceeded);
    EXPECT_EQ(timeline.generatedThroughTick(), 12u);

    WreckwaterCharacterPlatformTimeline sequence;
    ASSERT_TRUE(sequence.initialize(config()));
    EXPECT_EQ(
        sequence.replaceCertifiedBase(
            std::numeric_limits<uint64_t>::max(),
            1u, std::span(&base, 1u), 1u),
        WreckwaterCharacterPlatformTimelineStatus::
            SnapshotSequenceExhausted);
    EXPECT_FALSE(sequence.hasCertifiedBase());

    WreckwaterCharacterPlatformTimeline tick;
    ASSERT_TRUE(tick.initialize(config()));
    EXPECT_EQ(
        tick.replaceCertifiedBase(
            1u, std::numeric_limits<uint64_t>::max(),
            std::span(&base, 1u),
            std::numeric_limits<uint64_t>::max()),
        WreckwaterCharacterPlatformTimelineStatus::
            TickExhausted);

    std::array<
        game::WreckwaterCharacterPlatformSample, 3u>
        tooMany{
            base,
            platform(12u, {10.0, 2.0, 0.0}),
            platform(13u, {20.0, 2.0, 0.0}),
        };
    WreckwaterCharacterPlatformTimeline capacity;
    ASSERT_TRUE(capacity.initialize(config()));
    EXPECT_EQ(
        capacity.replaceCertifiedBase(
            1u, 1u, tooMany, 1u),
        WreckwaterCharacterPlatformTimelineStatus::
            PlatformCapacityExceeded);

    auto edge = base;
    edge.position = {
        .sector = {
            std::numeric_limits<int32_t>::max(), 0, 0},
        .local = {127.0f, 2.0f, 0.0f},
    };
    edge.linearVelocity = {300.0f, 0.0f, 0.0f};
    WreckwaterCharacterPlatformTimeline overflow;
    ASSERT_TRUE(overflow.initialize(config()));
    expectAccepted(overflow.replaceCertifiedBase(
        1u, 1u, std::span(&edge, 1u), 1u));
    EXPECT_EQ(
        overflow.generateThrough(2u),
        WreckwaterCharacterPlatformTimelineStatus::
            PositionOverflow);
    EXPECT_EQ(overflow.frameCount(), 1u);
}

TEST(WreckwaterCharacterPlatformTimeline,
     TwoSkiffsAreOrderIndependentAndBitRepeatable) {
    const auto firstPlatform = platform(
        11u, {-10.0, 2.0, 5.0},
        {3.0f, 0.0f, -1.0f},
        {0.0f, 2.0f, 0.0f});
    const auto secondPlatform = platform(
        22u, {20.0, 3.0, -8.0},
        {-4.0f, 0.0f, 2.0f},
        {1.0f, 0.0f, 0.0f});
    const std::array ordered{firstPlatform, secondPlatform};
    const std::array reversed{secondPlatform, firstPlatform};

    WreckwaterCharacterPlatformTimeline first;
    WreckwaterCharacterPlatformTimeline second;
    ASSERT_TRUE(first.initialize(config(16u)));
    ASSERT_TRUE(second.initialize(config(16u)));
    expectAccepted(first.replaceCertifiedBase(
        9u, 50u, ordered, 60u));
    expectAccepted(second.replaceCertifiedBase(
        9u, 50u, reversed, 60u));

    for (uint64_t tick = 50u; tick <= 60u; ++tick) {
        const auto lhs = first.frame(tick);
        const auto rhs = second.frame(tick);
        ASSERT_TRUE(lhs);
        ASSERT_TRUE(rhs);
        EXPECT_EQ(lhs.tick, rhs.tick);
        EXPECT_EQ(
            lhs.certifiedSnapshotSequence,
            rhs.certifiedSnapshotSequence);
        EXPECT_TRUE(std::equal(
            lhs.platforms.begin(), lhs.platforms.end(),
            rhs.platforms.begin(), rhs.platforms.end()));
    }
    EXPECT_EQ(first.telemetry(), second.telemetry());
}

} // namespace
} // namespace voxy::client
