#include <gtest/gtest.h>

#include "perf/benchmark.hpp"

namespace voxy::perf {

TEST(BenchmarkRunnerTest, ReportsObservedLatencyPercentilesAfterWarmup) {
    BenchmarkRunner runner;
    runner.start({BenchmarkScenario{
        .name = "Deterministic",
        .cameraPos = {},
        .cameraTarget = {},
        .frameCount = 5,
    }});

    for (const double frameMs : {100.0, 4.0, 1.0, 3.0, 2.0}) {
        FrameStats stats;
        stats.totalMs = frameMs;
        runner.onFrame(stats);
    }

    ASSERT_FALSE(runner.isRunning());
    ASSERT_EQ(runner.getResults().size(), 1u);
    const BenchmarkResult& result = runner.getResults().front();
    EXPECT_DOUBLE_EQ(result.p50FrameMs, 2.0);
    EXPECT_DOUBLE_EQ(result.p95FrameMs, 4.0);
    EXPECT_DOUBLE_EQ(result.p99FrameMs, 4.0);
}

} // namespace voxy::perf
