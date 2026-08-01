#include "client/wreckwater_client_visual_clock.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <limits>

namespace voxy::client {
namespace {

struct RateResult {
    network::WreckwaterPhysicsRenderTick tick{};
    uint64_t evidence = 0u;
};

[[nodiscard]] RateResult runAtRate(uint32_t rate) {
    WreckwaterClientVisualClock clock;
    EXPECT_TRUE(clock.initialize({}));
    auto result = clock.advance(0u, 100u);
    EXPECT_TRUE(result);
    EXPECT_TRUE(result.initializedFromEvidence);

    uint64_t previousNanoseconds = 0u;
    constexpr uint64_t durationSeconds = 2u;
    const uint64_t frameCount = durationSeconds * rate;
    for (uint64_t frame = 1u; frame <= frameCount; ++frame) {
        const uint64_t nowNanoseconds =
            frame * 1'000'000'000u / rate;
        const uint64_t evidence =
            100u + nowNanoseconds * 60u / 1'000'000'000u;
        result = clock.advance(
            nowNanoseconds - previousNanoseconds, evidence);
        EXPECT_TRUE(result)
            << wreckwaterClientVisualClockStatusName(result.status);
        previousNanoseconds = nowNanoseconds;
    }
    return {
        .tick = clock.renderTick(),
        .evidence = clock.latestEvidenceTick(),
    };
}

TEST(WreckwaterClientVisualClock,
     MatchesThirtySixtyAndOneFortyFourHertz) {
    const RateResult at30 = runAtRate(30u);
    const RateResult at60 = runAtRate(60u);
    const RateResult at144 = runAtRate(144u);

    EXPECT_EQ(at30.evidence, 220u);
    EXPECT_EQ(at60.evidence, at30.evidence);
    EXPECT_EQ(at144.evidence, at30.evidence);
    EXPECT_EQ(at30.tick.whole, 218u);
    EXPECT_EQ(at60.tick.whole, at30.tick.whole);
    EXPECT_EQ(at144.tick.whole, at30.tick.whole);
    EXPECT_NEAR(at30.tick.fraction, 0.0f, 1.0e-7f);
    EXPECT_NEAR(at60.tick.fraction, 0.0f, 1.0e-7f);
    EXPECT_NEAR(at144.tick.fraction, 0.0f, 1.0e-7f);
}

TEST(WreckwaterClientVisualClock,
     SnapshotGapUsesOnlyCertifiedExtrapolationWindow) {
    WreckwaterClientVisualClock clock;
    ASSERT_TRUE(clock.initialize({
        .interpolationDelayTicks = 2u,
        .maximumExtrapolationTicks = 3u,
    }));
    ASSERT_TRUE(clock.advance(0u, 100u));

    auto result = clock.advance(1'000'000'000u, 100u);
    ASSERT_TRUE(result);
    EXPECT_TRUE(result.clampedToExtrapolationLimit);
    EXPECT_EQ(result.renderTick.whole, 103u);
    EXPECT_FLOAT_EQ(result.renderTick.fraction, 0.0f);

    result = clock.advance(500'000'000u, 100u);
    ASSERT_TRUE(result);
    EXPECT_TRUE(result.clampedToExtrapolationLimit);
    EXPECT_EQ(result.renderTick.whole, 103u);

    result = clock.advance(0u, 110u);
    ASSERT_TRUE(result);
    EXPECT_TRUE(result.advancedToDelayFloor);
    EXPECT_EQ(result.renderTick.whole, 108u);
}

TEST(WreckwaterClientVisualClock,
     RegressionIsRejectedUntilExplicitReset) {
    WreckwaterClientVisualClock clock;
    ASSERT_TRUE(clock.initialize({}));
    ASSERT_TRUE(clock.advance(0u, 500u));
    ASSERT_TRUE(clock.advance(250'000'000u, 500u));
    const auto before = clock.renderTick();

    const auto rejected = clock.advance(1'000'000'000u, 499u);
    EXPECT_EQ(
        rejected.status,
        WreckwaterClientVisualClockStatus::RegressingEvidenceTick);
    EXPECT_EQ(clock.renderTick(), before);
    EXPECT_EQ(clock.latestEvidenceTick(), 500u);

    clock.reset();
    const auto accepted = clock.advance(0u, 7u);
    ASSERT_TRUE(accepted);
    EXPECT_TRUE(accepted.initializedFromEvidence);
    EXPECT_EQ(accepted.renderTick.whole, 5u);
    EXPECT_EQ(clock.latestEvidenceTick(), 7u);
}

TEST(WreckwaterClientVisualClock,
     InvalidConfigurationAndHugeElapsedClampToCertifiedCeiling) {
    WreckwaterClientVisualClock clock;
    EXPECT_FALSE(clock.initialize({
        .interpolationDelayTicks =
            kWreckwaterClientVisualClockMaximumInterpolationDelayTicks
                + 1u,
    }));

    ASSERT_TRUE(clock.initialize({
        .interpolationDelayTicks = 0u,
        .maximumExtrapolationTicks = 0u,
    }));
    ASSERT_TRUE(clock.advance(
        0u, std::numeric_limits<uint64_t>::max()));
    const auto overflow = clock.advance(
        std::numeric_limits<uint64_t>::max(),
        std::numeric_limits<uint64_t>::max());
    ASSERT_TRUE(overflow);
    EXPECT_TRUE(overflow.clampedToExtrapolationLimit);
    EXPECT_EQ(
        overflow.renderTick.whole,
        std::numeric_limits<uint64_t>::max());
    EXPECT_FLOAT_EQ(overflow.renderTick.fraction, 0.0f);
}

TEST(WreckwaterClientVisualClock,
     FractionStaysCanonicalAtUpperBoundary) {
    WreckwaterClientVisualClock clock;
    ASSERT_TRUE(clock.initialize({
        .interpolationDelayTicks = 2u,
        .maximumExtrapolationTicks = 3u,
    }));
    ASSERT_TRUE(clock.advance(0u, 100u));
    const auto result =
        clock.advance(16'666'666u, 100u);
    ASSERT_TRUE(result);
    EXPECT_EQ(result.renderTick.whole, 98u);
    EXPECT_GE(result.renderTick.fraction, 0.9999998f);
    EXPECT_LT(result.renderTick.fraction, 1.0f);
}

TEST(WreckwaterClientVisualClock,
     MaximumWholeTickCannotRetainFraction) {
    WreckwaterClientVisualClock clock;
    ASSERT_TRUE(clock.initialize({
        .interpolationDelayTicks = 2u,
        .maximumExtrapolationTicks = 3u,
    }));
    ASSERT_TRUE(clock.advance(
        0u, std::numeric_limits<uint64_t>::max()));

    const auto result = clock.advance(
        50'000'000u,
        std::numeric_limits<uint64_t>::max());
    ASSERT_TRUE(result);
    EXPECT_TRUE(result.clampedToExtrapolationLimit);
    EXPECT_EQ(
        result.renderTick.whole,
        std::numeric_limits<uint64_t>::max());
    EXPECT_FLOAT_EQ(result.renderTick.fraction, 0.0f);
}

} // namespace
} // namespace voxy::client
