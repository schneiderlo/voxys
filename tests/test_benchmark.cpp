#include <gtest/gtest.h>

#include "perf/benchmark.hpp"

namespace voxy::perf {

TEST(BenchmarkRunnerTest, ReportsObservedLatencyPercentilesAfterWarmup) {
    BenchmarkRunner runner;
    runner.setPhysicsBackend("jolt_legacy");
    runner.start({BenchmarkScenario{
        .name = "Deterministic",
        .cameraPos = {},
        .cameraTarget = {},
        .frameCount = 5,
    }});

    for (const double frameMs : {100.0, 4.0, 1.0, 3.0, 2.0}) {
        FrameStats stats;
        stats.totalMs = frameMs;
        stats.physicsSimulationMs = 2.0;
        stats.physicsWaterMs = 0.5;
        stats.physicsSnapshotMs = 0.25;
        stats.primitiveCullMs = 0.125;
        stats.primitivePackingMs = 0.75;
        stats.primitiveUploadMs = 0.375;
        stats.primitiveRenderMs = 1.25;
        runner.onFrame(stats);
    }

    ASSERT_FALSE(runner.isRunning());
    ASSERT_EQ(runner.getResults().size(), 1u);
    const BenchmarkResult& result = runner.getResults().front();
    EXPECT_EQ(result.physicsBackend, "jolt_legacy");
    EXPECT_DOUBLE_EQ(result.p50FrameMs, 2.0);
    EXPECT_DOUBLE_EQ(result.p95FrameMs, 4.0);
    EXPECT_DOUBLE_EQ(result.p99FrameMs, 4.0);
    EXPECT_DOUBLE_EQ(result.avgPhysicsSimulationMs, 2.0);
    EXPECT_DOUBLE_EQ(result.avgPhysicsWaterMs, 0.5);
    EXPECT_DOUBLE_EQ(result.avgPhysicsSnapshotMs, 0.25);
    EXPECT_DOUBLE_EQ(result.avgPrimitiveCullMs, 0.125);
    EXPECT_DOUBLE_EQ(result.avgPrimitivePackingMs, 0.75);
    EXPECT_DOUBLE_EQ(result.avgPrimitiveUploadMs, 0.375);
    EXPECT_DOUBLE_EQ(result.avgPrimitiveRenderMs, 1.25);
}

} // namespace voxy::perf
