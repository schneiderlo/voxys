// Tests for benchmark statistics collection.

#include <gtest/gtest.h>

#include "perf/benchmark.hpp"

namespace voxy::perf {

TEST(BenchmarkRunnerTest, SkipsTeleportWarmupFrames) {
    BenchmarkRunner runner;
    uint32_t cameraUpdates = 0;
    runner.setCameraCallback([&](const glm::vec3&, const glm::vec3&) {
        cameraUpdates++;
    });

    runner.start({
        BenchmarkScenario{
            .name = "test",
            .cameraPos = glm::vec3(0.0f),
            .cameraTarget = glm::vec3(1.0f),
            .frameCount = 3,
        },
    });

    FrameStats warmup;
    warmup.totalMs = 1000.0;
    warmup.renderMs = 1000.0;
    for (uint32_t i = 0; i < BenchmarkRunner::kWarmupFrames; ++i) {
        EXPECT_TRUE(runner.onFrame(warmup));
    }

    FrameStats measured;
    measured.totalMs = 2.0;
    measured.renderMs = 1.5;
    EXPECT_TRUE(runner.onFrame(measured));

    measured.totalMs = 4.0;
    measured.renderMs = 3.5;
    EXPECT_TRUE(runner.onFrame(measured));

    measured.totalMs = 6.0;
    measured.renderMs = 5.5;
    EXPECT_FALSE(runner.onFrame(measured));

    ASSERT_EQ(runner.getResults().size(), 1u);
    const BenchmarkResult& result = runner.getResults().front();
    EXPECT_EQ(cameraUpdates, 1u);
    EXPECT_DOUBLE_EQ(result.totalTimeMs, 12.0);
    EXPECT_DOUBLE_EQ(result.avgFrameMs, 4.0);
    EXPECT_DOUBLE_EQ(result.minFrameMs, 2.0);
    EXPECT_DOUBLE_EQ(result.maxFrameMs, 6.0);
    EXPECT_DOUBLE_EQ(result.avgRenderMs, 3.5);
}

}  // namespace voxy::perf
