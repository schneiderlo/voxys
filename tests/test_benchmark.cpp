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

TEST(BrowserJourneyBenchmarkTest, DrivesExactRealVolleysAndCompletes) {
    BrowserJourneyBenchmark journey;
    ASSERT_TRUE(journey.start(
        BrowserJourneyConfig{
            .targetBodies = 100,
            .warmupTicks = 2,
            .impactTicks = 3,
            .settleTicks = 2,
            .bodiesPerVolley = 64,
            .ticksPerVolley = 1,
            .shape = BrowserJourneyShape::Sphere,
        },
        3, 10, 20));

    EXPECT_EQ(journey.status(), BrowserJourneyStatus::Warming);
    EXPECT_EQ(journey.advance(20, 3), 0u);
    EXPECT_EQ(journey.advance(21, 3), 0u);

    EXPECT_EQ(journey.advance(22, 3), 64u);
    ASSERT_TRUE(journey.reportVolley(22, 64, 64));
    journey.recordFrame({
        .frame = 11,
        .physicsTick = 22,
        .wallMilliseconds = 16.7f,
        .cpuMilliseconds = 2.0f,
        .residentBodies = 67,
        .activeBodies = 64,
    });

    // An uncapped renderer may submit multiple frames per physics tick. Only
    // the first one can create another production volley.
    EXPECT_EQ(journey.advance(22, 67), 0u);
    journey.recordFrame({
        .frame = 12,
        .physicsTick = 22,
        .wallMilliseconds = 1.0f,
        .cpuMilliseconds = 0.4f,
        .residentBodies = 67,
        .activeBodies = 64,
    });

    EXPECT_EQ(journey.advance(23, 67), 36u);
    ASSERT_TRUE(journey.reportVolley(23, 36, 36));
    journey.recordFrame({
        .frame = 13,
        .physicsTick = 23,
        .wallMilliseconds = 34.0f,
        .cpuMilliseconds = 2.5f,
        .residentBodies = 103,
        .activeBodies = 100,
        .candidatePairs = 700,
        .contacts = 250,
        .terrainContactBodies = 90,
        .submittedPrimitives = 103,
    });
    EXPECT_EQ(journey.spawnedBodies(), 100u);
    EXPECT_EQ(journey.volleyCount(), 2u);
    EXPECT_EQ(journey.shape(), BrowserJourneyShape::Sphere);
    EXPECT_EQ(journey.status(), BrowserJourneyStatus::Impact);

    EXPECT_EQ(journey.advance(24, 103), 0u);
    journey.recordFrame({
        .frame = 14,
        .physicsTick = 24,
        .wallMilliseconds = 16.7f,
        .cpuMilliseconds = 1.5f,
        .residentBodies = 103,
        .activeBodies = 100,
    });
    EXPECT_EQ(journey.advance(26, 103), 0u);
    journey.recordFrame({
        .frame = 15,
        .physicsTick = 26,
        .wallMilliseconds = 16.7f,
        .cpuMilliseconds = 1.2f,
        .residentBodies = 103,
        .activeBodies = 80,
    });
    EXPECT_EQ(journey.status(), BrowserJourneyStatus::Settling);

    EXPECT_EQ(journey.advance(28, 103), 0u);
    journey.recordFrame({
        .frame = 16,
        .physicsTick = 28,
        .wallMilliseconds = 16.7f,
        .cpuMilliseconds = 1.0f,
        .residentBodies = 103,
        .activeBodies = 25,
    });

    EXPECT_EQ(journey.status(), BrowserJourneyStatus::Complete);
    EXPECT_TRUE(journey.passed());
    EXPECT_EQ(journey.sampleCount(), 6u);
    journey.recordFrame({
        .frame = 17,
        .physicsTick = 29,
        .wallMilliseconds = 50.0f,
        .cpuMilliseconds = 10.0f,
        .residentBodies = 103,
        .activeBodies = 25,
    });
    EXPECT_EQ(journey.sampleCount(), 6u);

    const std::string json = journey.resultJson();
    EXPECT_NE(json.find("\"schema\":\"voxys.browser_journey.v1\""),
              std::string::npos);
    EXPECT_NE(json.find("\"missed_deadlines_60hz\":1"),
              std::string::npos);
    EXPECT_NE(json.find("\"missed_deadlines_90hz\":6"),
              std::string::npos);
    EXPECT_NE(json.find("\"missed_deadlines_120hz\":7"),
              std::string::npos);
    EXPECT_NE(json.find("\"expected_final_bodies\":103"),
              std::string::npos);
    EXPECT_NE(json.find("\"layout\":\"pile\""),
              std::string::npos);
    EXPECT_NE(json.find("\"shape\":\"sphere\""),
              std::string::npos);
    EXPECT_NE(json.find("\"terrain_contact_bodies\":90"),
              std::string::npos);
    EXPECT_NE(json.find("\"submitted_primitives\":103"),
              std::string::npos);
    EXPECT_NE(json.find("\"throwing\":{\"frame\":{\"count\":3"),
              std::string::npos);
}

TEST(BrowserJourneyBenchmarkTest, PartialVolleyFailsImmediately) {
    BrowserJourneyBenchmark journey;
    ASSERT_TRUE(journey.start(
        BrowserJourneyConfig{
            .targetBodies = 128,
            .warmupTicks = 0,
            .impactTicks = 1,
            .settleTicks = 1,
            .bodiesPerVolley = 128,
        },
        0, 0, 5));

    EXPECT_EQ(journey.advance(5, 0), 128u);
    EXPECT_FALSE(journey.reportVolley(5, 128, 127));
    EXPECT_EQ(journey.status(), BrowserJourneyStatus::Failed);
    EXPECT_STREQ(journey.failureReason(), "partial_spawn");
    EXPECT_FALSE(journey.passed());
}

TEST(BrowserJourneyBenchmarkTest, PacesVolleysByPhysicsTick) {
    BrowserJourneyBenchmark journey;
    ASSERT_TRUE(journey.start(
        BrowserJourneyConfig{
            .targetBodies = 256,
            .warmupTicks = 0,
            .impactTicks = 1,
            .settleTicks = 1,
            .bodiesPerVolley = 128,
            .ticksPerVolley = 4,
        },
        0, 0, 10));

    ASSERT_EQ(journey.advance(10, 0), 128u);
    ASSERT_TRUE(journey.reportVolley(10, 128, 128));
    EXPECT_EQ(journey.advance(11, 128), 0u);
    EXPECT_EQ(journey.advance(13, 128), 0u);
    EXPECT_EQ(journey.advance(14, 128), 128u);
}

TEST(BrowserJourneyBenchmarkTest, RejectsClockAndFinalBodyMismatch) {
    BrowserJourneyBenchmark journey;
    ASSERT_TRUE(journey.start(
        BrowserJourneyConfig{
            .targetBodies = 1,
            .warmupTicks = 1,
            .impactTicks = 1,
            .settleTicks = 1,
            .bodiesPerVolley = 1,
        },
        0, 0, 10));
    EXPECT_EQ(journey.advance(9, 0), 0u);
    EXPECT_EQ(journey.status(), BrowserJourneyStatus::Failed);
    EXPECT_STREQ(journey.failureReason(), "physics_clock_regression");

    ASSERT_TRUE(journey.start(
        BrowserJourneyConfig{
            .targetBodies = 1,
            .warmupTicks = 0,
            .impactTicks = 1,
            .settleTicks = 1,
            .bodiesPerVolley = 1,
        },
        0, 0, 10));
    ASSERT_EQ(journey.advance(10, 0), 1u);
    ASSERT_TRUE(journey.reportVolley(10, 1, 1));
    journey.recordFrame({
        .frame = 1,
        .physicsTick = 10,
        .wallMilliseconds = 16.7f,
        .cpuMilliseconds = 1.0f,
        .residentBodies = 1,
        .activeBodies = 1,
    });
    EXPECT_EQ(journey.advance(11, 1), 0u);
    journey.recordFrame({
        .frame = 2,
        .physicsTick = 11,
        .wallMilliseconds = 16.7f,
        .cpuMilliseconds = 1.0f,
        .residentBodies = 1,
        .activeBodies = 1,
    });
    EXPECT_EQ(journey.advance(12, 0), 0u);
    EXPECT_EQ(journey.status(), BrowserJourneyStatus::Failed);
    EXPECT_STREQ(journey.failureReason(), "body_count_mismatch");
}

TEST(BrowserJourneyBenchmarkTest, ObservesPrebuiltCubePyramidWithoutSpawning) {
    BrowserJourneyBenchmark journey;
    ASSERT_TRUE(journey.start(
        BrowserJourneyConfig{
            .targetBodies = 0,
            .warmupTicks = 1,
            .impactTicks = 2,
            .settleTicks = 1,
            .bodiesPerVolley = 128,
            .ticksPerVolley = 4,
            .layout = BrowserJourneyLayout::CubePyramid,
            .shape = BrowserJourneyShape::Cube,
        },
        20'000, 5, 10));

    EXPECT_EQ(journey.advance(10, 20'000), 0u);
    EXPECT_EQ(journey.advance(11, 20'000), 0u);
    EXPECT_EQ(journey.status(), BrowserJourneyStatus::Impact);
    journey.recordFrame({
        .frame = 6,
        .physicsTick = 11,
        .wallMilliseconds = 12.0f,
        .cpuMilliseconds = 1.0f,
        .residentBodies = 20'000,
        .activeBodies = 17'696,
        .candidatePairs = 80'000,
        .contacts = 72'000,
        .submittedPrimitives = 20'000,
    });

    EXPECT_EQ(journey.advance(13, 20'000), 0u);
    EXPECT_EQ(journey.status(), BrowserJourneyStatus::Settling);
    journey.recordFrame({
        .frame = 7,
        .physicsTick = 13,
        .wallMilliseconds = 10.0f,
        .cpuMilliseconds = 0.8f,
        .residentBodies = 20'000,
        .activeBodies = 17'696,
        .submittedPrimitives = 20'000,
    });
    EXPECT_EQ(journey.advance(14, 20'000), 0u);
    journey.recordFrame({
        .frame = 8,
        .physicsTick = 14,
        .wallMilliseconds = 9.0f,
        .cpuMilliseconds = 0.7f,
        .residentBodies = 20'000,
        .activeBodies = 17'696,
        .submittedPrimitives = 20'000,
    });

    EXPECT_TRUE(journey.passed());
    EXPECT_EQ(journey.spawnedBodies(), 0u);
    EXPECT_EQ(journey.volleyCount(), 0u);
    EXPECT_EQ(journey.layout(), BrowserJourneyLayout::CubePyramid);
    const std::string json = journey.resultJson();
    EXPECT_NE(json.find("\"layout\":\"triangle\""), std::string::npos);
    EXPECT_NE(json.find("\"baseline_bodies\":20000"),
              std::string::npos);
    EXPECT_NE(json.find("\"expected_final_bodies\":20000"),
              std::string::npos);
}

TEST(BrowserJourneyBenchmarkTest, RejectsInvalidConfiguration) {
    BrowserJourneyBenchmark journey;
    EXPECT_FALSE(journey.start(
        BrowserJourneyConfig{
            .targetBodies = 0,
            .warmupTicks = 1,
            .impactTicks = 1,
            .settleTicks = 1,
            .bodiesPerVolley = 128,
        },
        0, 0, 0));
    EXPECT_EQ(journey.status(), BrowserJourneyStatus::Failed);
    EXPECT_STREQ(journey.failureReason(), "invalid_configuration");

    EXPECT_FALSE(journey.start(
        BrowserJourneyConfig{
            .targetBodies = 1,
            .warmupTicks = 1,
            .impactTicks = 1,
            .settleTicks = 1,
            .bodiesPerVolley = 1,
            .ticksPerVolley = 1,
            .layout = BrowserJourneyLayout::CubePyramid,
            .shape = BrowserJourneyShape::Cube,
        },
        20'000, 0, 0));
    EXPECT_STREQ(journey.failureReason(), "invalid_configuration");

    EXPECT_FALSE(journey.start(
        BrowserJourneyConfig{
            .targetBodies = 1,
            .warmupTicks = 1,
            .impactTicks = 1,
            .settleTicks = 1,
            .bodiesPerVolley = 1,
            .ticksPerVolley = 0,
        },
        0, 0, 0));
    EXPECT_STREQ(journey.failureReason(), "invalid_configuration");

    EXPECT_FALSE(journey.start(
        BrowserJourneyConfig{
            .targetBodies = 1,
            .warmupTicks = 1,
            .impactTicks = 1,
            .settleTicks = 1,
            .bodiesPerVolley = 1,
            .ticksPerVolley = 1,
            .layout = static_cast<BrowserJourneyLayout>(99u),
        },
        0, 0, 0));
    EXPECT_STREQ(journey.failureReason(), "invalid_configuration");

    EXPECT_FALSE(journey.start(
        BrowserJourneyConfig{
            .targetBodies = 1,
            .warmupTicks = 1,
            .impactTicks = 1,
            .settleTicks = 1,
            .bodiesPerVolley = 1,
            .ticksPerVolley = 1,
            .shape = static_cast<BrowserJourneyShape>(99u),
        },
        0, 0, 0));
    EXPECT_STREQ(journey.failureReason(), "invalid_configuration");
}

} // namespace voxy::perf
