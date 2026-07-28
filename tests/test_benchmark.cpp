#include <gtest/gtest.h>

#include "perf/benchmark.hpp"

#include <limits>

namespace voxy::perf {

TEST(BenchmarkRunnerTest, ReportsObservedLatencyPercentilesAfterWarmup) {
    BenchmarkRunner runner;
    runner.setPhysicsBackend("jolt_legacy");
    runner.setExpectedBodyCount(400);
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
        stats.physicsResidentBodies = 400;
        stats.physicsActiveBodies = 400;
        stats.physicsActiveBodiesObserved = true;
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
    EXPECT_EQ(result.minResidentBodies, 400u);
    EXPECT_EQ(result.maxResidentBodies, 400u);
    EXPECT_EQ(result.minActiveBodies, 400u);
    EXPECT_EQ(result.activeBodySamples, 5u);
    EXPECT_TRUE(result.bodyCountInvariantPassed);
    EXPECT_TRUE(runner.passed());
}

TEST(BenchmarkRunnerTest, RejectsAFrameWithSleepingBodies) {
    BenchmarkRunner runner;
    runner.setExpectedBodyCount(400);
    runner.start({BenchmarkScenario{
        .name = "Sleeping body guard",
        .cameraPos = {},
        .cameraTarget = {},
        .frameCount = 2,
    }});

    FrameStats stats;
    stats.physicsResidentBodies = 400;
    stats.physicsActiveBodies = 400;
    stats.physicsActiveBodiesObserved = true;
    runner.onFrame(stats);
    stats.physicsActiveBodies = 399;
    runner.onFrame(stats);

    ASSERT_EQ(runner.getResults().size(), 1u);
    EXPECT_EQ(runner.getResults().front().minActiveBodies, 399u);
    EXPECT_FALSE(runner.getResults().front().bodyCountInvariantPassed);
    EXPECT_FALSE(runner.passed());
}

TEST(BenchmarkRunnerTest, RejectsUnobservedGpuActivity) {
    BenchmarkRunner runner;
    runner.setExpectedBodyCount(400);
    runner.start({BenchmarkScenario{
        .name = "Missing telemetry guard",
        .cameraPos = {},
        .cameraTarget = {},
        .frameCount = 1,
    }});

    FrameStats stats;
    stats.physicsResidentBodies = 400;
    stats.physicsActiveBodies = 400;
    runner.onFrame(stats);

    ASSERT_EQ(runner.getResults().size(), 1u);
    EXPECT_EQ(runner.getResults().front().activeBodySamples, 0u);
    EXPECT_FALSE(runner.getResults().front().bodyCountInvariantPassed);
    EXPECT_FALSE(runner.passed());
}

TEST(BenchmarkRunnerTest, GuardsAggregateThroughput) {
    BenchmarkRunner runner;
    runner.setMinimumThroughputFps(400.0);
    runner.start({BenchmarkScenario{
        .name = "Throughput guard",
        .cameraPos = {},
        .cameraTarget = {},
        .frameCount = 2,
    }});

    FrameStats stats;
    stats.totalMs = 2.0;
    runner.onFrame(stats);
    stats.totalMs = 3.0;
    runner.onFrame(stats);

    EXPECT_DOUBLE_EQ(runner.overallThroughputFps(), 400.0);
    EXPECT_TRUE(runner.passed());

    runner.setMinimumThroughputFps(400.1);
    EXPECT_FALSE(runner.passed());
}

TEST(BenchmarkRunnerTest, AggregateThroughputIsNotMeanScenarioFps) {
    BenchmarkRunner runner;
    runner.setMinimumThroughputFps(201.0);
    runner.start({
        BenchmarkScenario{
            .name = "Fast",
            .cameraPos = {},
            .cameraTarget = {},
            .frameCount = 1,
        },
        BenchmarkScenario{
            .name = "Slow",
            .cameraPos = {},
            .cameraTarget = {},
            .frameCount = 1,
        },
    });

    FrameStats stats;
    stats.totalMs = 1.0;
    runner.onFrame(stats);
    stats.totalMs = 9.0;
    runner.onFrame(stats);

    // 2 frames / 10 ms = 200 FPS. Averaging the scenario rates would
    // incorrectly report (1000 + 111.1) / 2 = 555.6 FPS.
    EXPECT_DOUBLE_EQ(runner.overallThroughputFps(), 200.0);
    EXPECT_FALSE(runner.passed());

    runner.setMinimumThroughputFps(200.0);
    EXPECT_TRUE(runner.passed());
    EXPECT_DOUBLE_EQ(runner.getResults()[0].minFrameMs, 1.0);
    EXPECT_DOUBLE_EQ(runner.getResults()[0].maxFrameMs, 1.0);
}

TEST(BenchmarkRunnerTest, RejectsInvalidScenariosBeforeStarting) {
    BenchmarkRunner runner;
    runner.start({BenchmarkScenario{
        .name = "Zero frames",
        .cameraPos = {},
        .cameraTarget = {},
        .frameCount = 0,
    }});
    EXPECT_FALSE(runner.isRunning());
    EXPECT_TRUE(runner.getResults().empty());

    runner.start({BenchmarkScenario{
        .name = "Non-finite camera",
        .cameraPos = {
            std::numeric_limits<float>::quiet_NaN(), 0.0f, 0.0f},
        .cameraTarget = {},
        .frameCount = 1,
    }});
    EXPECT_FALSE(runner.isRunning());

    runner.start({
        BenchmarkScenario{
            .name = "Too many total frames A",
            .cameraPos = {},
            .cameraTarget = {},
            .frameCount = 600'000u,
        },
        BenchmarkScenario{
            .name = "Too many total frames B",
            .cameraPos = {},
            .cameraTarget = {},
            .frameCount = 600'000u,
        },
    });
    EXPECT_FALSE(runner.isRunning());

    std::vector<BenchmarkScenario> tooMany(1'025u);
    for (auto& scenario : tooMany) {
        scenario.name = "bounded";
        scenario.frameCount = 1u;
    }
    runner.start(tooMany);
    EXPECT_FALSE(runner.isRunning());
}

TEST(BenchmarkRunnerTest, InvalidFrameTelemetryCannotProduceAPassingRun) {
    BenchmarkRunner runner;
    runner.start({BenchmarkScenario{
        .name = "Invalid telemetry",
        .cameraPos = {},
        .cameraTarget = {},
        .frameCount = 1,
    }});

    FrameStats stats;
    stats.totalMs = std::numeric_limits<double>::quiet_NaN();
    EXPECT_FALSE(runner.onFrame(stats));
    EXPECT_FALSE(runner.isRunning());
    EXPECT_TRUE(runner.getResults().empty());
    EXPECT_FALSE(runner.passed());

    runner.start({BenchmarkScenario{
        .name = "Finite overflow telemetry",
        .cameraPos = {},
        .cameraTarget = {},
        .frameCount = 2,
    }});
    stats = {};
    stats.totalMs = std::numeric_limits<double>::max();
    EXPECT_FALSE(runner.onFrame(stats));
    EXPECT_FALSE(runner.isRunning());
    EXPECT_TRUE(runner.getResults().empty());
    EXPECT_FALSE(runner.passed());
}

} // namespace voxy::perf
