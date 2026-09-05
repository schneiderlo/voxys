#include "moto/session.hpp"
#include "moto/race.hpp"
#include "moto/world.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

namespace voxy::moto {
namespace {

bool finite(const glm::vec3& value) {
    return std::isfinite(value.x) && std::isfinite(value.y) &&
           std::isfinite(value.z);
}

bool matricesDiffer(const glm::mat4& a, const glm::mat4& b,
                    float epsilon = 1.0e-5f) {
    for (int column = 0; column < 4; ++column) {
        for (int row = 0; row < 4; ++row) {
            if (std::abs(a[column][row] - b[column][row]) > epsilon) {
                return true;
            }
        }
    }
    return false;
}

MotoSessionConfig testConfig() {
    MotoSessionConfig config;
    config.bikeAsset = "data/moto/bike.vmesh";
    config.riderAsset = "data/moto/rider.vmesh";
    config.bikeAssetIndex = 7u;
    config.riderAssetIndex = 11u;
    config.spawnPosition = glm::vec3(2.0f, 0.57f, -3.0f);
    config.spawnYaw = 0.35f;
    return config;
}

TEST(MotoSession, LoadsAuthoredAssetsAndBuildsValidPartPoses) {
    MotoSession session;
    const MotoSessionConfig config = testConfig();
    std::string error = "not cleared";

    ASSERT_TRUE(session.initialize(config, &error)) << error;
    EXPECT_TRUE(error.empty());
    ASSERT_TRUE(session.isInitialized());
    constexpr size_t bikePartCount = 52u;
    constexpr size_t riderPartCount = 24u;
    ASSERT_EQ(session.partPoses().size(), bikePartCount + riderPartCount);

    for (size_t index = 0; index < session.partPoses().size(); ++index) {
        const MotoPartPose& pose = session.partPoses()[index];
        if (index < bikePartCount) {
            EXPECT_EQ(pose.assetIndex, config.bikeAssetIndex);
            EXPECT_LT(pose.meshIndex, bikePartCount);
        } else {
            EXPECT_EQ(pose.assetIndex, config.riderAssetIndex);
            EXPECT_LT(pose.meshIndex, riderPartCount);
        }
    }
}

TEST(MotoSession, HeroAssetsForbidDetachedChunksAndOpaqueBackPatch) {
    const auto load = [](const char* path, VmeshData* asset) {
        std::ifstream input(path, std::ios::binary | std::ios::ate);
        if (!input) return false;
        const std::streampos end = input.tellg();
        if (end <= 0) return false;
        std::vector<uint8_t> bytes(static_cast<size_t>(end));
        input.seekg(0, std::ios::beg);
        input.read(reinterpret_cast<char*>(bytes.data()),
                   static_cast<std::streamsize>(bytes.size()));
        std::string error;
        return static_cast<bool>(input) &&
               readVmesh(bytes.data(), bytes.size(), asset, &error);
    };
    VmeshData bike;
    VmeshData rider;
    ASSERT_TRUE(load("data/moto/bike.vmesh", &bike));
    ASSERT_TRUE(load("data/moto/rider.vmesh", &rider));
    for (const VmeshNode& node : bike.nodes) {
        EXPECT_FALSE(std::string_view(bike.name(node.nameOffset)).starts_with(
            "fx_chunk_"));
    }
    for (const VmeshNode& node : rider.nodes) {
        EXPECT_NE(std::string_view(rider.name(node.nameOffset)), "back_plate");
    }
}

TEST(MotoSession, ContactRoostStaysSmallShortAndGrounded) {
    MotoSession session;
    const MotoSessionConfig config = testConfig();
    std::string error;
    ASSERT_TRUE(session.initialize(config, &error)) << error;
    BikeInput input;
    input.throttle = 0.85f;
    const auto flatGround = [](float, float) { return 0.0f; };
    for (int frame = 0; frame < 180; ++frame) {
        session.update(input, flatGround, 1.0f / 60.0f);
    }

    constexpr size_t firstDustPose = 36u;
    constexpr size_t dustPoseCount = 16u;
    ASSERT_GE(session.partPoses().size(), firstDustPose + dustPoseCount);
    size_t visible = 0u;
    for (size_t index = firstDustPose;
         index < firstDustPose + dustPoseCount; ++index) {
        const MotoPartPose& pose = session.partPoses()[index];
        EXPECT_LE(pose.tintColor.a, 0.86f);
        if (pose.tintColor.a <= 0.0f) continue;
        ++visible;
        const float scale = std::max({
            glm::length(glm::vec3(pose.modelMatrix[0])),
            glm::length(glm::vec3(pose.modelMatrix[1])),
            glm::length(glm::vec3(pose.modelMatrix[2]))});
        EXPECT_LE(scale, 0.30f);
        const glm::vec3 position(pose.modelMatrix[3]);
        EXPECT_GE(position.y, 0.04f);
        EXPECT_LE(position.y, 0.70f);
        glm::vec3 offset = position - session.bikeState().chassisPosition;
        offset.y = 0.0f;
        EXPECT_LE(glm::length(offset), 12.0f);
    }
    EXPECT_GT(visible, 0u);
}

TEST(MotoSession, FlatGroundThrottleMovesBikePartsAndWheelMatrices) {
    MotoSession session;
    const MotoSessionConfig config = testConfig();
    std::string error;
    ASSERT_TRUE(session.initialize(config, &error)) << error;

    const glm::vec3 initialPosition = session.bikeState().chassisPosition;
    const glm::mat4 initialFrame = session.partPoses()[8].modelMatrix;
    const glm::mat4 initialRearWheel = session.partPoses()[1].modelMatrix;

    BikeInput input;
    input.throttle = 0.75f;
    input.seated = false;
    const auto flatGround = [](float, float) { return 0.0f; };
    for (int frame = 0; frame < 180; ++frame) {
        session.update(input, flatGround, 1.0f / 60.0f);
    }

    EXPECT_GT(session.bikeState().totalDistance, 0.05f);
    EXPECT_GT(glm::length(session.bikeState().chassisPosition - initialPosition),
              0.05f);
    EXPECT_TRUE(matricesDiffer(session.partPoses()[8].modelMatrix,
                               initialFrame));
    EXPECT_TRUE(matricesDiffer(session.partPoses()[1].modelMatrix,
                               initialRearWheel));

    const MotoCameraPose& camera = session.cameraPose();
    ASSERT_TRUE(finite(camera.position));
    ASSERT_TRUE(finite(camera.target));
    glm::vec3 forward = session.bikeState().chassisOrientation *
                        glm::vec3(0.0f, 0.0f, 1.0f);
    forward.y = 0.0f;
    ASSERT_GT(glm::length(forward), 1.0e-4f);
    forward = glm::normalize(forward);
    EXPECT_LT(glm::dot(camera.position - session.bikeState().chassisPosition,
                       forward),
              0.0f);
}

TEST(MotoSession, ResetRestoresConfiguredSpawnAndYaw) {
    MotoSession session;
    const MotoSessionConfig config = testConfig();
    std::string error;
    ASSERT_TRUE(session.initialize(config, &error)) << error;

    BikeInput input;
    input.throttle = 1.0f;
    const auto flatGround = [](float, float) { return 0.0f; };
    for (int frame = 0; frame < 90; ++frame) {
        session.update(input, flatGround, 1.0f / 60.0f);
    }
    session.reset();

    EXPECT_EQ(session.bikeState().chassisPosition, config.spawnPosition);
    const glm::quat expected = glm::angleAxis(
        config.spawnYaw, glm::vec3(0.0f, 1.0f, 0.0f));
    EXPECT_NEAR(std::abs(glm::dot(session.bikeState().chassisOrientation,
                                  expected)),
                1.0f, 1.0e-6f);
    EXPECT_FLOAT_EQ(session.bikeState().totalDistance, 0.0f);
    EXPECT_TRUE(finite(session.cameraPose().position));
}

TEST(MotoSession, CircuitResetRecoversCrashAndPersistsGridPose) {
    MotoSession session;
    MotoSessionConfig config = testConfig();
    config.spawnPosition.y = 9.0f;
    std::string error;
    ASSERT_TRUE(session.initialize(config, &error)) << error;
    const auto flatGround = [](float, float) { return 0.0f; };

    for (int frame = 0;
         frame < 360 && session.bikeState().crash == CrashState::Riding;
         ++frame) {
        session.update(BikeInput{}, flatGround, 1.0f / 60.0f);
    }
    ASSERT_NE(session.bikeState().crash, CrashState::Riding);

    const glm::vec3 gridPosition{-14.0f, 1.0f, 22.0f};
    constexpr float gridYaw = -1.1f;
    session.resetAt(gridPosition, gridYaw);
    EXPECT_EQ(session.bikeState().crash, CrashState::Riding);
    EXPECT_EQ(session.bikeState().chassisPosition, gridPosition);
    EXPECT_TRUE(session.fixedStepStates().empty());
    const glm::quat expected = glm::angleAxis(
        gridYaw, glm::vec3(0.0f, 1.0f, 0.0f));
    EXPECT_NEAR(std::abs(glm::dot(session.bikeState().chassisOrientation,
                                  expected)),
                1.0f, 1.0e-6f);

    BikeInput input;
    input.throttle = 1.0f;
    session.update(input, flatGround, 1.0f / 60.0f);
    session.reset();
    EXPECT_EQ(session.bikeState().crash, CrashState::Riding);
    EXPECT_EQ(session.bikeState().chassisPosition, gridPosition);
    EXPECT_NEAR(std::abs(glm::dot(session.bikeState().chassisOrientation,
                                  expected)),
                1.0f, 1.0e-6f);
}

TEST(MotoSession, GeneratedCircuitOwnsEveryCountdownTickAndLaunchesOnGreen) {
    WorldGenConfig worldConfig;
    worldConfig.size = 256u;
    WorldGenResult world;
    std::string error;
    ASSERT_TRUE(generateWorld(worldConfig, &world, &error)) << error;

    const auto heightAt = [&](float x, float z) {
        const float half = 0.5f * static_cast<float>(worldConfig.size - 1u);
        const uint32_t sampleX = static_cast<uint32_t>(std::clamp(
            std::lround(x / worldConfig.cellScale + half), 0l,
            static_cast<long>(worldConfig.size - 1u)));
        const uint32_t sampleZ = static_cast<uint32_t>(std::clamp(
            std::lround(z / worldConfig.cellScale + half), 0l,
            static_cast<long>(worldConfig.size - 1u)));
        const uint16_t sample = world.samples[
            static_cast<size_t>(sampleZ) * worldConfig.size + sampleX];
        return worldConfig.heightScale
            * (2.0f * static_cast<float>(sample) / 65'535.0f - 1.0f);
    };

    std::vector<RaceCheckpoint> checkpoints;
    ASSERT_TRUE(buildDirectedRaceCheckpoints(
        world.raceRoute, 18.0f, 12.0f, &checkpoints, &error)) << error;
    for (RaceCheckpoint& checkpoint : checkpoints) {
        checkpoint.center.y = heightAt(
            checkpoint.center.x, checkpoint.center.z) + 2.0f;
    }
    CircuitGridPose grid;
    ASSERT_TRUE(buildCircuitGridPose(
        checkpoints, 10.0f, &grid, &error)) << error;
    grid.position.y = heightAt(grid.position.x, grid.position.z) + 1.0f;

    MotoSessionConfig sessionConfig = testConfig();
    sessionConfig.spawnPosition = grid.position;
    sessionConfig.spawnYaw = grid.yaw;
    MotoSession session;
    ASSERT_TRUE(session.initialize(sessionConfig, &error)) << error;

    RaceConfig raceConfig;
    raceConfig.countdownTicks = 3u;
    RaceSession race;
    ASSERT_TRUE(race.configure(raceConfig, checkpoints, &error)) << error;
    ASSERT_TRUE(race.join(1u));
    ASSERT_TRUE(race.start());

    struct ObservedTick {
        RacePhase phaseBeforeAuthority = RacePhase::Lobby;
        BikeState bike{};
    };
    std::vector<ObservedTick> ticks;
    BikeInput heldThrottle;
    heldThrottle.throttle = 1.0f;
    const auto fixedStepMode = [&]() {
        const MotoRaceApplicationPolicy policy =
            evaluateMotoRaceApplicationPolicy(
                false, false, race.config().mode, race.phase(),
                race.countdownTicksRemaining());
        return policy.gridOwned
            ? MotoFixedStepMode::HoldGrid
            : MotoFixedStepMode::Simulate;
    };
    const auto observeFixedStep = [&](const BikeState& state) {
        ticks.push_back({race.phase(), state});
        const RaceRiderFrame frame{
            .player = 1u,
            .position = state.chassisPosition,
        };
        race.step(std::span(&frame, 1u));
    };

    // Two half-render-frames must retain one full fixed tick of wall-clock
    // time. The following catch-up frame then crosses green mid-frame.
    session.update(heldThrottle, heightAt, 1.0f / 120.0f,
                   fixedStepMode, observeFixedStep);
    EXPECT_TRUE(ticks.empty());
    EXPECT_EQ(race.tick(), 0u);
    session.update(heldThrottle, heightAt, 1.0f / 120.0f,
                   fixedStepMode, observeFixedStep);
    ASSERT_EQ(ticks.size(), 1u);
    EXPECT_EQ(race.tick(), 1u);
    const float threeTicks = std::nextafter(
        3.0f / 60.0f, std::numeric_limits<float>::infinity());
    session.update(heldThrottle, heightAt, threeTicks,
                   fixedStepMode, observeFixedStep);

    ASSERT_EQ(ticks.size(), 4u);
    EXPECT_EQ(race.tick(), 4u);
    EXPECT_EQ(race.phase(), RacePhase::Running);
    EXPECT_EQ(race.runningTick(), 1u);
    for (size_t index = 0u; index < 3u; ++index) {
        const BikeState& held = ticks[index].bike;
        EXPECT_EQ(ticks[index].phaseBeforeAuthority, RacePhase::Countdown);
        EXPECT_EQ(held.chassisPosition, grid.position);
        EXPECT_EQ(held.chassisLinearVelocity, glm::vec3(0.0f));
        EXPECT_EQ(held.chassisAngularVelocity, glm::vec3(0.0f));
        EXPECT_FLOAT_EQ(held.frontSuspension, 0.0f);
        EXPECT_FLOAT_EQ(held.rearSuspension, 0.0f);
        EXPECT_FLOAT_EQ(held.frontWheelAngularVelocity, 0.0f);
        EXPECT_FLOAT_EQ(held.rearWheelAngularVelocity, 0.0f);
        EXPECT_FLOAT_EQ(held.throttle, 0.0f);
        EXPECT_EQ(held.crash, CrashState::Riding);
        const glm::vec3 forward = held.chassisOrientation
            * glm::vec3(0.0f, 0.0f, 1.0f);
        EXPECT_NEAR(forward.x, std::sin(grid.yaw), 1.0e-6f);
        EXPECT_NEAR(forward.z, std::cos(grid.yaw), 1.0e-6f);
    }
    EXPECT_EQ(ticks[2].bike.chassisPosition, grid.position);
    EXPECT_EQ(ticks[2].bike.chassisLinearVelocity, glm::vec3(0.0f));
    EXPECT_EQ(ticks[3].phaseBeforeAuthority, RacePhase::Running);
    EXPECT_FLOAT_EQ(ticks[3].bike.throttle, 1.0f);
    EXPECT_NE(ticks[3].bike.chassisLinearVelocity, glm::vec3(0.0f));
}

TEST(MotoSession, InvalidAssetFailsWithoutPartialInitialization) {
    MotoSession session;
    MotoSessionConfig config = testConfig();
    config.riderAsset = "data/moto/not-a-vmesh.vmesh";
    std::string error;

    EXPECT_FALSE(session.initialize(config, &error));
    EXPECT_FALSE(error.empty());
    EXPECT_FALSE(session.isInitialized());
    EXPECT_TRUE(session.partPoses().empty());
}

TEST(MotoSession, InvalidDeltaDoesNotCorruptStateOrCamera) {
    MotoSession session;
    const MotoSessionConfig config = testConfig();
    std::string error;
    ASSERT_TRUE(session.initialize(config, &error)) << error;

    BikeInput input;
    input.throttle = 1.0f;
    const auto flatGround = [](float, float) { return 0.0f; };
    session.update(input, flatGround, std::numeric_limits<float>::quiet_NaN());
    session.update(input, flatGround, -1.0f);

    EXPECT_EQ(session.bikeState().chassisPosition, config.spawnPosition);
    EXPECT_TRUE(finite(session.cameraPose().position));
    EXPECT_TRUE(finite(session.cameraPose().target));
}

TEST(MotoSession, FixedTickBikeStateIsIndependentOfFramePartition) {
    MotoSession sixtyFps;
    MotoSession oneTwentyFps;
    const MotoSessionConfig config = testConfig();
    std::string error;
    ASSERT_TRUE(sixtyFps.initialize(config, &error)) << error;
    ASSERT_TRUE(oneTwentyFps.initialize(config, &error)) << error;
    const auto rollingGround = [](float x, float z) {
        return 0.04f * std::sin(x * 0.2f) + 0.06f * std::sin(z * 0.1f);
    };
    BikeInput input;
    input.throttle = 0.7f;
    input.steer = 0.18f;
    input.lean = -0.10f;

    for (int frame = 0; frame < 120; ++frame) {
        sixtyFps.update(input, rollingGround, 1.0f / 60.0f);
    }
    for (int frame = 0; frame < 240; ++frame) {
        oneTwentyFps.update(input, rollingGround, 1.0f / 120.0f);
    }

    const BikeState& a = sixtyFps.bikeState();
    const BikeState& b = oneTwentyFps.bikeState();
    EXPECT_EQ(a.chassisPosition, b.chassisPosition);
    EXPECT_EQ(a.chassisOrientation, b.chassisOrientation);
    EXPECT_EQ(a.chassisLinearVelocity, b.chassisLinearVelocity);
    EXPECT_EQ(a.chassisAngularVelocity, b.chassisAngularVelocity);
    EXPECT_EQ(a.trickScore, b.trickScore);
    EXPECT_EQ(a.crash, b.crash);
}

TEST(MotoSession, ExposesEveryCanonicalFixedStateProducedByFrame) {
    MotoSession session;
    const MotoSessionConfig config = testConfig();
    std::string error;
    ASSERT_TRUE(session.initialize(config, &error)) << error;
    const auto flatGround = [](float, float) { return 0.0f; };

    session.update(BikeInput{}, flatGround, 1.0f / 120.0f);
    EXPECT_TRUE(session.fixedStepStates().empty());

    session.update(BikeInput{}, flatGround, 4.0f / 60.0f);
    ASSERT_EQ(session.fixedStepStates().size(), 4u);
    EXPECT_EQ(session.fixedStepStates().back().chassisPosition,
              session.bikeState().chassisPosition);
    EXPECT_EQ(session.fixedStepStates().back().tricksLanded,
              session.bikeState().tricksLanded);

    session.update(BikeInput{}, flatGround, 0.0f);
    EXPECT_TRUE(session.fixedStepStates().empty());
    session.reset();
    EXPECT_TRUE(session.fixedStepStates().empty());
}

TEST(MotoSession, HalfTickShiftEdgesAreLatchedUntilSimulationConsumesThem) {
    MotoSession session;
    const MotoSessionConfig config = testConfig();
    std::string error;
    ASSERT_TRUE(session.initialize(config, &error)) << error;
    const auto flatGround = [](float, float) { return 0.0f; };

    BikeInput shiftUp;
    shiftUp.shiftUp = true;
    session.update(shiftUp, flatGround, 1.0f / 120.0f);
    EXPECT_EQ(session.bikeState().gear, 1u);
    session.update(BikeInput{}, flatGround, 1.0f / 120.0f);
    EXPECT_EQ(session.bikeState().gear, 2u);

    BikeInput shiftDown;
    shiftDown.shiftDown = true;
    session.update(shiftDown, flatGround, 1.0f / 120.0f);
    EXPECT_EQ(session.bikeState().gear, 2u);
    session.update(BikeInput{}, flatGround, 1.0f / 120.0f);
    EXPECT_EQ(session.bikeState().gear, 1u);

    // Opposing edges sampled before one fixed tick are both retained. They
    // execute in deterministic up-then-down order rather than dropping one.
    session.update(shiftUp, flatGround, 1.0f / 120.0f);
    session.update(shiftDown, flatGround, 1.0f / 120.0f);
    EXPECT_EQ(session.bikeState().gear, 2u);
    session.update(BikeInput{}, flatGround, 1.0f / 60.0f);
    EXPECT_EQ(session.bikeState().gear, 1u);

    // Chronology is preserved in the opposite order as well.
    session.update(shiftUp, flatGround, 1.0f / 60.0f);
    ASSERT_EQ(session.bikeState().gear, 2u);
    session.update(shiftDown, flatGround, 1.0f / 120.0f);
    session.update(shiftUp, flatGround, 1.0f / 120.0f);
    EXPECT_EQ(session.bikeState().gear, 1u);
    session.update(BikeInput{}, flatGround, 1.0f / 60.0f);
    EXPECT_EQ(session.bikeState().gear, 2u);
}

TEST(MotoSession, ShiftQueueOverflowDropsNewestAndReportsTelemetry) {
    MotoSession session;
    const MotoSessionConfig config = testConfig();
    std::string error;
    ASSERT_TRUE(session.initialize(config, &error)) << error;
    BikeInput shift;
    shift.shiftUp = true;
    const auto flatGround = [](float, float) { return 0.0f; };

    for (int edge = 0; edge < 10; ++edge) {
        session.update(shift, flatGround, 0.0f);
    }
    EXPECT_EQ(session.droppedShiftInputs(), 2u);
    session.reset();
    EXPECT_EQ(session.droppedShiftInputs(), 0u);
}

TEST(MotoSession, HalfTickRemountEdgeSurvivesUntilNextFixedTick) {
    MotoSession session;
    MotoSessionConfig config = testConfig();
    config.spawnPosition.y = 9.0f;
    std::string error;
    ASSERT_TRUE(session.initialize(config, &error)) << error;
    const auto flatGround = [](float, float) { return 0.0f; };

    for (int frame = 0;
         frame < 360 && session.bikeState().crash != CrashState::OnGround;
         ++frame) {
        session.update(BikeInput{}, flatGround, 1.0f / 60.0f);
    }
    ASSERT_EQ(session.bikeState().crash, CrashState::OnGround);

    // Press immediately, while grounded but before groundedBeforeRemount.
    BikeInput remount;
    remount.remount = true;
    session.update(remount, flatGround, 1.0f / 120.0f);
    EXPECT_EQ(session.bikeState().crash, CrashState::OnGround);
    session.update(BikeInput{}, flatGround, 1.0f / 120.0f);
    EXPECT_EQ(session.bikeState().crash, CrashState::OnGround);

    bool accepted = false;
    for (int frame = 0; frame < 60; ++frame) {
        session.update(BikeInput{}, flatGround, 1.0f / 60.0f);
        if (session.bikeState().crash == CrashState::Remounting) {
            accepted = true;
            break;
        }
    }
    EXPECT_TRUE(accepted);
    EXPECT_LT(session.bikeState().crashTime, 3.75f);
}

TEST(MotoSession, PresentationPoseInterpolatesWithoutChangingFixedState) {
    MotoSession session;
    MotoSessionConfig config = testConfig();
    config.spawnPosition.y = 9.0f;
    std::string error;
    ASSERT_TRUE(session.initialize(config, &error)) << error;
    const auto flatGround = [](float, float) { return 0.0f; };

    const glm::vec3 initial = session.bikeState().chassisPosition;
    session.update(BikeInput{}, flatGround, 1.0f / 60.0f);
    const BikeState fixedAfterTick = session.bikeState();
    ASSERT_NE(fixedAfterTick.chassisPosition, initial);
    EXPECT_EQ(session.presentationBikeState().chassisPosition, initial);

    session.update(BikeInput{}, flatGround, 1.0f / 120.0f);
    const BikeState& fixedAfterHalfFrame = session.bikeState();
    const BikeState& presentation = session.presentationBikeState();
    EXPECT_EQ(fixedAfterHalfFrame.chassisPosition,
              fixedAfterTick.chassisPosition);
    const glm::vec3 expected = glm::mix(initial,
                                        fixedAfterTick.chassisPosition, 0.5f);
    EXPECT_NEAR(presentation.chassisPosition.x, expected.x, 1.0e-6f);
    EXPECT_NEAR(presentation.chassisPosition.y, expected.y, 1.0e-6f);
    EXPECT_NEAR(presentation.chassisPosition.z, expected.z, 1.0e-6f);
}

TEST(MotoSession, CrashAndRemountHaveDistinctRiderPresentation) {
    MotoSession session;
    MotoSessionConfig config = testConfig();
    config.spawnPosition.y = 9.0f;
    std::string error;
    ASSERT_TRUE(session.initialize(config, &error)) << error;
    constexpr size_t riderStart = 52u;
    ASSERT_GT(session.partPoses().size(), riderStart);
    const glm::mat4 ridingRider = session.partPoses()[riderStart].modelMatrix;
    const auto flatGround = [](float, float) { return 0.0f; };

    bool sawCrashPose = false;
    bool sawRemountPose = false;
    bool recovered = false;
    for (int frame = 0; frame < 600; ++frame) {
        session.update(BikeInput{}, flatGround, 1.0f / 60.0f);
        if (session.bikeState().crash == CrashState::HighSided ||
            session.bikeState().crash == CrashState::WipedOut) {
            sawCrashPose = sawCrashPose || matricesDiffer(
                session.partPoses()[riderStart].modelMatrix, ridingRider, 0.01f);
        }
        sawRemountPose = sawRemountPose ||
            session.bikeState().crash == CrashState::Remounting;
        if (sawRemountPose &&
            session.bikeState().crash == CrashState::Riding) {
            recovered = true;
            break;
        }
    }

    EXPECT_TRUE(sawCrashPose);
    EXPECT_TRUE(sawRemountPose);
    EXPECT_TRUE(recovered);
    EXPECT_TRUE(finite(session.cameraPose().position));
    EXPECT_TRUE(finite(session.cameraPose().target));
}

}  // namespace
}  // namespace voxy::moto
