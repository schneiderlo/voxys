#include <gtest/gtest.h>
#include "render/day_night.hpp"
#include <limits>

namespace voxy::render {

TEST(DayNightTest, ClockWrapsAndAdvancesIndependentlyOfFrameRate) {
    EXPECT_DOUBLE_EQ(wrapDayHour(24.0), 0.0);
    EXPECT_DOUBLE_EQ(wrapDayHour(-1.0), 23.0);
    double at30 = 23.9, at144 = at30;
    for (int i = 0; i < 30 * 60; ++i) at30 = advanceDayHour(at30, 1.0 / 30.0, 24.0);
    for (int i = 0; i < 144 * 60; ++i) at144 = advanceDayHour(at144, 1.0 / 144.0, 24.0);
    EXPECT_NEAR(at30, 0.9, 1e-9);
    EXPECT_NEAR(at144, at30, 1e-9);
}

TEST(DayNightTest, PausesAndStallsDoNotSkipTheEvening) {
    EXPECT_DOUBLE_EQ(advanceDayHour(17.0, 0.0, 24.0), 17.0);
    EXPECT_DOUBLE_EQ(advanceDayHour(17.0, -1.0, 24.0), 17.0);
    EXPECT_DOUBLE_EQ(advanceDayHour(17.0, std::numeric_limits<double>::infinity(), 24.0), 17.0);
    EXPECT_DOUBLE_EQ(advanceDayHour(17.0, 1.0, 0.0), 17.0);
    EXPECT_NEAR(advanceDayHour(17.0, 300.0, 24.0), 17.0 + 0.25 / 60.0, 1e-10);
}

TEST(DayNightTest, NightsKeepReadableCoolFillAndSunsetsStayWarm) {
    const auto day = sampleDayNight(12.0);
    const auto dusk = sampleDayNight(17.5);
    const auto night = sampleDayNight(0.0);
    EXPECT_GT(day.intensity, night.intensity * 3.0f);
    EXPECT_GT(dusk.lightColor.r, dusk.lightColor.b * 2.0f);
    EXPECT_GT(night.lightColor.b, night.lightColor.r);
    EXPECT_GT(night.ambientColor.r * night.ambientIntensity, 0.10f);
    EXPECT_GT(night.lightDirection.y, 0.8f);
    EXPECT_LT(night.sunDirection.y, -0.8f);
}

TEST(DayNightTest, WholeCycleIsFiniteAndEmittedLightIsContinuous) {
    auto previous = sampleDayNight(-0.002);
    for (int i = 0; i <= 12000; ++i) {
        const auto light = sampleDayNight(i * 0.002);
        EXPECT_NEAR(glm::length(light.lightDirection), 1.0f, 1e-6f);
        EXPECT_TRUE(std::isfinite(light.exposure));
        EXPECT_GE(light.intensity, 0.0f);
        EXPECT_LT(glm::length(light.lightColor * light.intensity
            - previous.lightColor * previous.intensity), 0.035f);
        EXPECT_LT(glm::length(light.ambientColor - previous.ambientColor), 0.01f);
        EXPECT_LT(glm::length(light.fogColor - previous.fogColor), 0.01f);
        previous = light;
    }
    const auto a = sampleDayNight(0.0), b = sampleDayNight(24.0);
    EXPECT_FLOAT_EQ(a.intensity, b.intensity);
    EXPECT_FLOAT_EQ(a.exposure, b.exposure);
    EXPECT_EQ(a.lightDirection, b.lightDirection);
}

} // namespace voxy::render
