#include "game/expedition/cove_water_clock.hpp"
#include <gtest/gtest.h>
#include <limits>

namespace voxy::game::expedition {

TEST(CoveWaterClock, SlowRenderingAndRepeatedFramesCannotAccelerateWaves) {
    CoveWaterClock clock;
    ASSERT_TRUE(clock.reset(0, 0));
    for (uint64_t tick = 1; tick <= 120; ++tick) {
        // A .1-second displayed frame previously advanced six times faster
        // than the single admitted physics tick. No display delta is an input.
        auto next = clock.prepare(tick, true);
        ASSERT_TRUE(next);
        clock = *next;
        for (int stalledFrame = 0; stalledFrame < 12; ++stalledFrame) {
            next = clock.prepare(tick, true);
            ASSERT_TRUE(next);
            EXPECT_DOUBLE_EQ(next->seconds(), clock.seconds());
        }
    }
    EXPECT_NEAR(clock.seconds(), 2.0, 1e-12);
}

TEST(CoveWaterClock, DiscardedPreparationAndPausedMaintenanceDoNotSpendTime) {
    CoveWaterClock clock;
    ASSERT_TRUE(clock.reset(300, 17.25));
    const auto abandoned = clock.prepare(301, true);
    ASSERT_TRUE(abandoned);
    EXPECT_DOUBLE_EQ(clock.seconds(), 17.25);
    const auto retry = clock.prepare(301, true);
    ASSERT_TRUE(retry);
    EXPECT_DOUBLE_EQ(retry->seconds(), abandoned->seconds());
    clock = *retry;
    const double paused = clock.seconds();
    for (uint64_t tick = 302; tick <= 310; ++tick) {
        const auto neutral = clock.prepare(tick, false);
        ASSERT_TRUE(neutral);
        clock = *neutral;
        EXPECT_DOUBLE_EQ(clock.seconds(), paused);
    }
    const auto resumed = clock.prepare(311, true);
    ASSERT_TRUE(resumed);
    EXPECT_NEAR(resumed->seconds() - paused, 1.0 / 60.0, 1e-13);
}

TEST(CoveWaterClock, ReloadPreservesPhaseEvenBeyondExactDoubleTickRange) {
    constexpr uint64_t savedTick = (uint64_t{1} << 60) + 17;
    CoveWaterClock restored;
    ASSERT_TRUE(restored.reset(savedTick, 125.375));
    const auto settling = restored.prepare(savedTick + 1, false);
    ASSERT_TRUE(settling);
    EXPECT_DOUBLE_EQ(settling->seconds(), 125.375);
    const auto resumed = settling->prepare(savedTick + 2, true);
    ASSERT_TRUE(resumed);
    EXPECT_NEAR(resumed->seconds(), 125.375 + 1.0 / 60.0, 1e-12);
    EXPECT_GT(resumed->phase(), settling->phase());
}

TEST(CoveWaterClock, WavePeriodWrapKeepsSavedElapsedTime) {
    CoveWaterClock clock;
    ASSERT_TRUE(clock.reset(9, 4096.0 - 1.0 / 120.0));
    const auto next = clock.prepare(10, true);
    ASSERT_TRUE(next);
    EXPECT_GT(next->seconds(), 4096);
    EXPECT_NEAR(next->phase(), 1.0 / 120.0, 1e-7);
    ASSERT_TRUE(clock.reset(9, 4096.0 - 1e-6));
    EXPECT_FLOAT_EQ(clock.phase(), 0);
}

TEST(CoveWaterClock, InvalidOrMultiTickInputPreservesThePriorClock) {
    CoveWaterClock clock;
    EXPECT_FALSE(clock.prepare(0, true));
    ASSERT_TRUE(clock.reset(UINT64_MAX - 1, 12));
    EXPECT_FALSE(clock.reset(0, -1));
    EXPECT_FALSE(clock.reset(0, std::numeric_limits<double>::infinity()));
    EXPECT_FALSE(clock.reset(0, std::numeric_limits<double>::quiet_NaN()));
    EXPECT_FALSE(clock.prepare(UINT64_MAX - 2, true));
    EXPECT_FALSE(clock.prepare(0, true));
    const auto last = clock.prepare(UINT64_MAX, true);
    ASSERT_TRUE(last);
    EXPECT_EQ(last->tick(), UINT64_MAX);
    EXPECT_DOUBLE_EQ(clock.seconds(), 12);
    ASSERT_TRUE(clock.reset(1, 0));
    EXPECT_FALSE(clock.prepare(3, true));
}

TEST(CoveWaterClock, LargeSavedSecondsDoNotAccumulateFractionalTickDrift) {
    CoveWaterClock clock;
    ASSERT_TRUE(clock.reset(0, 1e12 - 60));
    for (uint64_t tick = 1; tick <= 3600; ++tick) {
        const auto next = clock.prepare(tick, true);
        ASSERT_TRUE(next);
        clock = *next;
        EXPECT_DOUBLE_EQ(clock.seconds(), 1e12 - 60 + static_cast<double>(tick) / 60.0);
    }
    EXPECT_DOUBLE_EQ(clock.seconds(), 1e12);
    EXPECT_FALSE(clock.prepare(3601, true));
    EXPECT_FALSE(clock.reset(0, 1e12 + 1));
    EXPECT_TRUE(clock.prepare(3601, false));
}

} // namespace voxy::game::expedition
