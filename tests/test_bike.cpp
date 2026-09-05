#include <gtest/gtest.h>

#include "moto/bike.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

#include <glm/gtc/constants.hpp>

namespace voxy::moto {
namespace {

constexpr float kStep = 1.0f / 60.0f;
constexpr float kDry = -1000.0f;

void settle(Bike& bike, int ticks = 180) {
    for (int i = 0; i < ticks; ++i) {
        bike.step(BikeInput{}, 0.0f, kDry, kStep);
    }
}

bool finiteState(const BikeState& state) {
    const auto finiteVec = [](const glm::vec3& value) {
        return std::isfinite(value.x) && std::isfinite(value.y) &&
               std::isfinite(value.z);
    };
    return finiteVec(state.chassisPosition) &&
           finiteVec(state.chassisLinearVelocity) &&
           finiteVec(state.chassisAngularVelocity) &&
           std::isfinite(state.chassisOrientation.w) &&
           std::isfinite(state.chassisOrientation.x) &&
           std::isfinite(state.chassisOrientation.y) &&
           std::isfinite(state.chassisOrientation.z) &&
           std::isfinite(state.engineRPM) && std::isfinite(state.speed) &&
           std::isfinite(state.frontWheelAngularVelocity) &&
           std::isfinite(state.rearWheelAngularVelocity) &&
           std::isfinite(state.totalDistance) &&
           std::isfinite(state.lastAirTime) &&
           finiteVec(state.aerialRotation) &&
           std::isfinite(state.crashTime) &&
           std::isfinite(state.remountTime) &&
           std::isfinite(state.recoveryHeading) &&
           std::isfinite(state.barrierImpactSpeed);
}

TEST(Bike, ResetRestoresPoseAndPowertrain) {
    Bike bike;
    BikeInput accelerate;
    accelerate.throttle = 1.0f;
    for (int i = 0; i < 60; ++i) {
        bike.step(accelerate, 0.0f, kDry, kStep);
    }

    bike.reset(glm::vec3(2.0f, 3.0f, -4.0f),
               glm::half_pi<float>());
    const BikeState& state = bike.state();
    EXPECT_EQ(state.chassisPosition, glm::vec3(2.0f, 3.0f, -4.0f));
    EXPECT_NEAR((state.chassisOrientation * glm::vec3(0.0f, 0.0f, 1.0f)).x,
                1.0f, 1.0e-5f);
    EXPECT_EQ(state.gear, 1u);
    EXPECT_FLOAT_EQ(state.engineRPM, bike.config().idleRPM);
    EXPECT_FLOAT_EQ(state.totalDistance, 0.0f);
    EXPECT_EQ(state.crash, CrashState::Riding);
    EXPECT_TRUE(state.airborne);
}

TEST(Bike, AcceleratesForwardOnFlatGroundAndRunsGears) {
    Bike bike;
    settle(bike);
    const float startZ = bike.state().chassisPosition.z;
    BikeInput input;
    input.throttle = 1.0f;
    for (int i = 0; i < 240; ++i) {
        input.shiftUp = i == 90 || i == 160;
        bike.step(input, 0.0f, kDry, kStep);
    }

    EXPECT_EQ(bike.state().crash, CrashState::Riding);
    EXPECT_GT(bike.state().chassisPosition.z, startZ + 12.0f);
    EXPECT_GT(bike.state().speed, 4.0f);
    EXPECT_GT(bike.state().totalDistance, 12.0f);
    EXPECT_EQ(bike.state().gear, 3u);
    EXPECT_GE(bike.state().engineRPM, bike.config().idleRPM);
    EXPECT_NE(bike.state().rearWheelSpin, 0.0f);
}

TEST(Bike, BrakingReducesSpeedMoreThanCoasting) {
    Bike braking;
    Bike coasting;
    settle(braking);
    settle(coasting);
    BikeInput throttle;
    throttle.throttle = 0.75f;
    for (int i = 0; i < 150; ++i) {
        braking.step(throttle, 0.0f, kDry, kStep);
        coasting.step(throttle, 0.0f, kDry, kStep);
    }
    const float initialSpeed = braking.state().speed;

    BikeInput brake;
    brake.brake = 1.0f;
    for (int i = 0; i < 50; ++i) {
        braking.step(brake, 0.0f, kDry, kStep);
        coasting.step(BikeInput{}, 0.0f, kDry, kStep);
    }

    EXPECT_GT(initialSpeed, 3.0f);
    EXPECT_LT(std::abs(braking.state().speed),
              std::abs(coasting.state().speed));
    EXPECT_LT(std::abs(braking.state().speed), initialSpeed * 0.65f);
}

TEST(Bike, DrivenRearWheelBuildsLaunchSlipBeforeChassisCatchesUp) {
    Bike bike;
    settle(bike);
    BikeInput launch;
    launch.throttle = 1.0f;
    for (int tick = 0; tick < 8; ++tick)
        bike.step(launch, 0.0f, kDry, kStep);

    const BikeState& state = bike.state();
    const float rearSurfaceSpeed = state.rearWheelAngularVelocity *
                                   bike.config().wheelRadius;
    EXPECT_TRUE(state.rearTouch);
    EXPECT_GT(state.speed, 0.0f);
    EXPECT_GT(rearSurfaceSpeed, state.speed + 0.20f);
    EXPECT_GT(state.rearWheelAngularVelocity,
              state.frontWheelAngularVelocity + 1.0f);
    EXPECT_TRUE(finiteState(state));
}

TEST(Bike, HardBrakingCanApproachFrontLockWithoutReversingWheel) {
    Bike bike;
    settle(bike);
    BikeInput drive;
    drive.throttle = 0.85f;
    for (int tick = 0; tick < 180; ++tick)
        bike.step(drive, 0.0f, kDry, kStep);
    const float speedBefore = bike.state().speed;
    ASSERT_GT(speedBefore, 4.0f);

    BikeInput brake;
    brake.brake = 1.0f;
    for (int tick = 0; tick < 4; ++tick)
        bike.step(brake, 0.0f, kDry, kStep);
    const float frontSurfaceSpeed = bike.state().frontWheelAngularVelocity *
                                    bike.config().wheelRadius;
    EXPECT_GE(bike.state().frontWheelAngularVelocity, -0.01f);
    EXPECT_LT(frontSurfaceSpeed, std::abs(bike.state().speed) * 0.55f);

    for (int tick = 0; tick < 36; ++tick)
        bike.step(brake, 0.0f, kDry, kStep);
    EXPECT_LT(std::abs(bike.state().speed), speedBefore * 0.72f);
    EXPECT_TRUE(finiteState(bike.state()));
}

TEST(Bike, AirborneWheelsSpinIndependentlyAndDecayWithoutContact) {
    BikeConfig config;
    config.airPitchTorque = 0.001f;
    Bike bike(config);
    bike.reset(glm::vec3(0.0f, 20.0f, 0.0f), 0.0f);
    BikeInput throttle;
    throttle.throttle = 1.0f;
    for (int tick = 0; tick < 24; ++tick)
        bike.step(throttle, 0.0f, kDry, kStep);
    const float drivenSpin = bike.state().rearWheelAngularVelocity;
    ASSERT_TRUE(bike.state().airborne);
    ASSERT_GT(drivenSpin, 20.0f);
    EXPECT_NEAR(bike.state().frontWheelAngularVelocity, 0.0f, 1.0e-6f);

    for (int tick = 0; tick < 60; ++tick)
        bike.step(BikeInput{}, 0.0f, kDry, kStep);
    EXPECT_TRUE(bike.state().airborne);
    EXPECT_GT(bike.state().rearWheelAngularVelocity, drivenSpin * 0.70f);
    EXPECT_LT(bike.state().rearWheelAngularVelocity, drivenSpin);
    EXPECT_NEAR(bike.state().frontWheelAngularVelocity, 0.0f, 1.0e-6f);
    EXPECT_TRUE(finiteState(bike.state()));
}

TEST(Bike, LandingRecouplesSpinningWheelWithoutEnergySpike) {
    BikeConfig config;
    config.airPitchTorque = 0.001f;
    Bike bike(config);
    bike.reset(glm::vec3(0.0f, 2.2f, 0.0f), 0.0f);
    BikeInput throttle;
    throttle.throttle = 1.0f;

    bool touched = false;
    float maximumWheelDelta = 0.0f;
    float previousRear = bike.state().rearWheelAngularVelocity;
    for (int tick = 0; tick < 180; ++tick) {
        const BikeInput input = tick < 18 ? throttle : BikeInput{};
        bike.step(input, 0.0f, kDry, kStep);
        maximumWheelDelta = std::max(
            maximumWheelDelta,
            std::abs(bike.state().rearWheelAngularVelocity - previousRear));
        previousRear = bike.state().rearWheelAngularVelocity;
        touched = touched || bike.state().rearTouch || bike.state().frontTouch;
        ASSERT_TRUE(finiteState(bike.state()));
        ASSERT_LT(glm::length(bike.state().chassisLinearVelocity), 40.0f);
        ASSERT_LT(std::abs(bike.state().rearWheelAngularVelocity), 700.01f);
        if (touched && tick > 60) break;
    }

    EXPECT_TRUE(touched);
    EXPECT_LT(maximumWheelDelta, 180.0f);
    EXPECT_NE(bike.state().crash, CrashState::HighSided);
    EXPECT_TRUE(finiteState(bike.state()));
}

TEST(Bike, LongTireDrivelineRunRemainsFiniteAndEnergyBounded) {
    Bike bike;
    settle(bike);
    const auto terrain = [](float x, float z) {
        return 0.12f * std::sin(x * 0.09f) +
               0.16f * std::sin(z * 0.06f);
    };
    for (int tick = 0; tick < 4'000; ++tick) {
        BikeInput input;
        input.throttle = tick % 300 < 190 ? 0.90f : 0.0f;
        input.brake = tick % 300 >= 230 ? 0.85f : 0.0f;
        input.steer = 0.25f * std::sin(static_cast<float>(tick) * 0.017f);
        input.lean = -0.6f * input.steer;
        bike.step(input, terrain, kDry, kStep);
        ASSERT_TRUE(finiteState(bike.state())) << tick;
        ASSERT_LE(glm::length(bike.state().chassisLinearVelocity), 120.01f);
        ASSERT_LE(std::abs(bike.state().frontWheelAngularVelocity), 700.01f);
        ASSERT_LE(std::abs(bike.state().rearWheelAngularVelocity), 700.01f);
    }
}

TEST(Bike, CoastingLosesSpeedAndStillTravelsForward) {
    Bike bike;
    settle(bike);
    BikeInput input;
    input.throttle = 0.7f;
    for (int i = 0; i < 140; ++i) {
        bike.step(input, 0.0f, kDry, kStep);
    }
    const float beforeSpeed = bike.state().speed;
    const float beforeDistance = bike.state().totalDistance;
    for (int i = 0; i < 120; ++i) {
        bike.step(BikeInput{}, 0.0f, kDry, kStep);
    }

    EXPECT_GT(beforeSpeed, 2.0f);
    EXPECT_GT(bike.state().totalDistance, beforeDistance);
    EXPECT_LT(bike.state().speed, beforeSpeed);
    EXPECT_GT(bike.state().speed, 0.0f);
}

TEST(Bike, RampLaunchAccumulatesAirTime) {
    Bike bike;
    settle(bike);
    BikeInput input;
    input.throttle = 1.0f;
    const auto ramp = [](float, float z) {
        if (z < 3.0f) {
            return 0.0f;
        }
        if (z < 7.0f) {
            return (z - 3.0f) * 0.30f;
        }
        return -2.0f;
    };

    float maximumAirTime = 0.0f;
    bool leftRamp = false;
    for (int i = 0; i < 300; ++i) {
        bike.step(input, ramp, kDry, kStep);
        maximumAirTime = std::max(maximumAirTime, bike.state().airTime);
        leftRamp = leftRamp ||
                   (bike.state().airborne &&
                    bike.state().chassisPosition.z > 7.0f);
        if (bike.state().chassisPosition.z > 13.0f) {
            break;
        }
    }

    EXPECT_TRUE(leftRamp);
    EXPECT_GT(maximumAirTime, 0.12f);
    EXPECT_GT(bike.state().totalDistance, 7.0f);
}

TEST(Bike, HardLandingStartsDeterministicCrashTumble) {
    Bike bike;
    bike.reset(glm::vec3(0.0f, 9.0f, 0.0f), 0.0f);
    bool crashed = false;
    for (int i = 0; i < 240; ++i) {
        bike.step(BikeInput{}, 0.0f, kDry, kStep);
        if (bike.state().crash != CrashState::Riding) {
            crashed = true;
            break;
        }
    }

    EXPECT_TRUE(crashed);
    EXPECT_EQ(bike.state().crash, CrashState::HighSided);
    EXPECT_GT(glm::length(bike.state().chassisAngularVelocity), 1.0f);
}

TEST(Bike, GentleLandingIsGradedWithoutAwardingPhantomTricks) {
    Bike bike;
    bike.reset(glm::vec3(0.0f, 2.0f, 0.0f), 0.0f);
    for (int i = 0; i < 180 && bike.state().lastAirTime == 0.0f; ++i) {
        bike.step(BikeInput{}, 0.0f, kDry, kStep);
    }

    EXPECT_EQ(bike.state().crash, CrashState::Riding);
    EXPECT_GT(bike.state().lastAirTime, 0.20f);
    EXPECT_NE(bike.state().lastLanding, LandingQuality::None);
    EXPECT_NE(bike.state().lastLanding, LandingQuality::Crashed);
    EXPECT_EQ(bike.state().lastLandedTricks, 0u);
    EXPECT_EQ(bike.state().lastTrickPoints, 0u);
    EXPECT_EQ(bike.state().trickScore, 0u);
}

TEST(Bike, AirControlsRecognizeAndScoreAStableWhip) {
    Bike bike;
    bike.reset(glm::vec3(0.0f, 6.0f, 0.0f), 0.0f);

    bool sawWhip = false;
    for (int i = 0; i < 240; ++i) {
        BikeInput input;
        if (!sawWhip) {
            input.steer = 1.0f;
        } else {
            // A real whip is pulled back in line before touchdown. The local
            // angular-rate term brakes the return instead of overshooting it.
            const glm::vec3 localAngular = glm::conjugate(
                bike.state().chassisOrientation) *
                bike.state().chassisAngularVelocity;
            input.steer = std::clamp(
                -4.0f * bike.state().aerialRotation.y -
                    1.6f * localAngular.y,
                -1.0f, 1.0f);
        }
        bike.step(input, 0.0f, kDry, kStep);
        sawWhip = sawWhip ||
            (bike.state().aerialTricks &
             static_cast<uint32_t>(TrickFlags::Whip)) != 0u;
        if (bike.state().lastAirTime > 0.0f) break;
    }

    EXPECT_TRUE(sawWhip);
    EXPECT_EQ(bike.state().crash, CrashState::Riding);
    EXPECT_NE(bike.state().lastLanding, LandingQuality::Crashed);
    EXPECT_NE(bike.state().lastLandedTricks &
                  static_cast<uint32_t>(TrickFlags::Whip),
              0u) << "landing yaw=" << bike.state().aerialRotation.y
                  << " air=" << bike.state().lastAirTime;
    EXPECT_GT(bike.state().lastTrickPoints, 0u);
    EXPECT_EQ(bike.state().comboCount, 1u);
    EXPECT_EQ(bike.state().bestCombo, 1u);
    EXPECT_EQ(bike.state().tricksLanded, 1u);
    EXPECT_EQ(bike.state().trickScore, bike.state().lastTrickPoints);
}

TEST(Bike, CrashSettlesAndAutomaticallyRemountsUpright) {
    Bike bike;
    bike.reset(glm::vec3(0.0f, 9.0f, 0.0f), 0.0f);

    bool sawCrash = false;
    bool sawGrounded = false;
    bool sawRemount = false;
    bool recovered = false;
    for (int i = 0; i < 720; ++i) {
        bike.step(BikeInput{}, 0.0f, kDry, kStep);
        sawCrash = sawCrash || bike.state().crash == CrashState::HighSided ||
                   bike.state().crash == CrashState::WipedOut;
        sawGrounded = sawGrounded ||
                      bike.state().crash == CrashState::OnGround;
        sawRemount = sawRemount ||
                     bike.state().crash == CrashState::Remounting;
        if (sawCrash && bike.state().crash == CrashState::Riding) {
            recovered = true;
            break;
        }
    }

    EXPECT_TRUE(sawCrash);
    EXPECT_TRUE(sawGrounded);
    EXPECT_TRUE(sawRemount);
    EXPECT_TRUE(recovered);
    EXPECT_NEAR(glm::length(bike.state().chassisOrientation), 1.0f, 1.0e-5f);
    EXPECT_GT(glm::dot(bike.state().chassisOrientation *
                           glm::vec3(0.0f, 1.0f, 0.0f),
                       glm::vec3(0.0f, 1.0f, 0.0f)),
              0.999f);
    EXPECT_EQ(bike.state().lastLanding, LandingQuality::Crashed);
    EXPECT_TRUE(finiteState(bike.state()));
}

TEST(Bike, PlayerRequestedRemountBeatsLongAutomaticTimeout) {
    BikeConfig config;
    config.groundedBeforeRemount = 0.20f;
    config.automaticRemountDelay = 100.0f;
    config.remountDuration = 0.30f;
    Bike bike(config);
    bike.reset(glm::vec3(0.0f, 9.0f, 0.0f), 0.0f);

    while (bike.state().crash != CrashState::OnGround) {
        bike.step(BikeInput{}, 0.0f, kDry, kStep);
        ASSERT_LT(bike.state().crashTime, 10.0f);
    }
    for (int i = 0; i < 15; ++i) {
        bike.step(BikeInput{}, 0.0f, kDry, kStep);
    }
    BikeInput request;
    request.remount = true;
    bike.step(request, 0.0f, kDry, kStep);
    EXPECT_EQ(bike.state().crash, CrashState::Remounting);

    for (int i = 0; i < 30 && bike.state().crash != CrashState::Riding; ++i) {
        bike.step(BikeInput{}, 0.0f, kDry, kStep);
    }
    EXPECT_EQ(bike.state().crash, CrashState::Riding);
    EXPECT_LT(bike.state().crashTime, 1.0f);
}

TEST(Bike, CrashedTrickAttemptNeverAwardsPoints) {
    Bike bike;
    bike.reset(glm::vec3(0.0f, 9.0f, 0.0f), 0.0f);
    BikeInput spin;
    spin.steer = 1.0f;
    bool attempted = false;
    for (int i = 0; i < 240 && bike.state().crash == CrashState::Riding; ++i) {
        bike.step(spin, 0.0f, kDry, kStep);
        attempted = attempted || bike.state().aerialTricks != 0u;
    }

    EXPECT_TRUE(attempted);
    EXPECT_EQ(bike.state().lastLanding, LandingQuality::Crashed);
    EXPECT_EQ(bike.state().lastLandedTricks, 0u);
    EXPECT_EQ(bike.state().lastTrickPoints, 0u);
    EXPECT_EQ(bike.state().trickScore, 0u);
    EXPECT_EQ(bike.state().comboCount, 0u);
}

TEST(Bike, IdenticalFixedStepInputsProduceIdenticalGameplayState) {
    Bike first;
    Bike second;
    const auto terrain = [](float x, float z) {
        return 0.13f * std::sin(x * 0.11f) +
               0.21f * std::sin(z * 0.07f);
    };
    for (int tick = 0; tick < 900; ++tick) {
        BikeInput input;
        input.throttle = tick < 600 ? 0.82f : 0.20f;
        input.brake = tick >= 600 ? 0.25f : 0.0f;
        input.steer = std::sin(static_cast<float>(tick) * 0.013f) * 0.4f;
        input.lean = -input.steer * 0.7f;
        input.seated = tick % 180 < 110;
        input.duck = tick % 240 > 190;
        input.shiftUp = tick == 180 || tick == 360;
        first.step(input, terrain, kDry, kStep);
        second.step(input, terrain, kDry, kStep);
    }

    const BikeState& a = first.state();
    const BikeState& b = second.state();
    EXPECT_EQ(a.chassisPosition, b.chassisPosition);
    EXPECT_EQ(a.chassisOrientation, b.chassisOrientation);
    EXPECT_EQ(a.chassisLinearVelocity, b.chassisLinearVelocity);
    EXPECT_EQ(a.chassisAngularVelocity, b.chassisAngularVelocity);
    EXPECT_EQ(a.frontWheelAngularVelocity, b.frontWheelAngularVelocity);
    EXPECT_EQ(a.rearWheelAngularVelocity, b.rearWheelAngularVelocity);
    EXPECT_EQ(a.frontWheelSpin, b.frontWheelSpin);
    EXPECT_EQ(a.rearWheelSpin, b.rearWheelSpin);
    EXPECT_EQ(a.aerialRotation, b.aerialRotation);
    EXPECT_EQ(a.aerialTricks, b.aerialTricks);
    EXPECT_EQ(a.lastLandedTricks, b.lastLandedTricks);
    EXPECT_EQ(a.lastLanding, b.lastLanding);
    EXPECT_EQ(a.trickScore, b.trickScore);
    EXPECT_EQ(a.comboCount, b.comboCount);
    EXPECT_EQ(a.crash, b.crash);
    EXPECT_TRUE(finiteState(a));
}

TEST(Bike, ClampsInvalidAndNonFiniteInput) {
    Bike bike;
    settle(bike);
    BikeInput invalid;
    invalid.throttle = std::numeric_limits<float>::infinity();
    invalid.brake = std::numeric_limits<float>::quiet_NaN();
    invalid.steer = -std::numeric_limits<float>::infinity();
    invalid.lean = std::numeric_limits<float>::quiet_NaN();
    const auto invalidTerrain = [](float, float) {
        return std::numeric_limits<float>::quiet_NaN();
    };
    for (int i = 0; i < 30; ++i) {
        bike.step(invalid, invalidTerrain,
                  std::numeric_limits<float>::quiet_NaN(), kStep);
    }
    bike.step(invalid, 0.0f, kDry,
              std::numeric_limits<float>::quiet_NaN());

    EXPECT_TRUE(finiteState(bike.state()));
    EXPECT_GE(bike.state().throttle, 0.0f);
    EXPECT_LE(bike.state().throttle, 1.0f);
    EXPECT_LE(std::abs(bike.state().steeringAngle),
              bike.config().steeringMaxAngle);
    EXPECT_NEAR(glm::length(bike.state().chassisOrientation), 1.0f, 1.0e-5f);
}

TEST(Bike, WaterDampsMotionAndProvidesBuoyancy) {
    Bike wet;
    Bike dry;
    wet.reset(glm::vec3(0.0f, 2.0f, 0.0f), 0.0f);
    dry.reset(glm::vec3(0.0f, 2.0f, 0.0f), 0.0f);
    BikeInput input;
    input.throttle = 1.0f;
    for (int i = 0; i < 90; ++i) {
        wet.step(input, -20.0f, 3.0f, kStep);
        dry.step(input, -20.0f, kDry, kStep);
    }

    EXPECT_LT(glm::length(wet.state().chassisLinearVelocity),
              glm::length(dry.state().chassisLinearVelocity));
    EXPECT_GT(wet.state().chassisPosition.y, dry.state().chassisPosition.y);
    EXPECT_TRUE(finiteState(wet.state()));
}

TEST(Bike, ProjectsOutOfSuddenDeepTerrainPenetration) {
    Bike bike;
    settle(bike);
    bike.step(BikeInput{}, 20.0f, kDry, kStep);

    const BikeState& state = bike.state();
    const glm::vec3 rearCenter = state.chassisPosition +
        state.chassisOrientation * bike.config().rearAnchorLocal;
    const glm::vec3 frontCenter = state.chassisPosition +
        state.chassisOrientation * bike.config().frontAnchorLocal;
    EXPECT_GE(rearCenter.y - bike.config().wheelRadius, 20.0f - 1.0e-4f);
    EXPECT_GE(frontCenter.y - bike.config().wheelRadius, 20.0f - 1.0e-4f);
    EXPECT_TRUE(finiteState(state));
}

TEST(Bike, FastBikeCannotTunnelThroughTwentyMetreHeightStep) {
    Bike bike;
    settle(bike);
    BikeInput throttle;
    throttle.throttle = 1.0f;
    for (int tick = 0; tick < 360; ++tick) {
        bike.step(throttle, 0.0f, kDry, kStep);
    }
    ASSERT_GT(bike.state().speed, 4.0f);

    const glm::vec3 frontOffset = bike.state().chassisOrientation *
                                  bike.config().frontAnchorLocal;
    const float wallZ = bike.state().chassisPosition.z + frontOffset.z + 0.08f;
    const auto stepTerrain = [wallZ](float, float z) {
        return z >= wallZ ? 20.0f : 0.0f;
    };
    bike.step(throttle, stepTerrain, kDry, 1.0f / 30.0f);

    const BikeState& state = bike.state();
    const glm::vec3 frontCenter = state.chassisPosition +
        state.chassisOrientation * bike.config().frontAnchorLocal;
    // A vertical heightfield discontinuity is treated as a barrier, not a
    // teleport onto the plateau and never as permission to remain underneath.
    EXPECT_LT(frontCenter.z, wallZ);
    EXPECT_LT(state.chassisPosition.y, 5.0f);
    const float localGround = stepTerrain(frontCenter.x, frontCenter.z);
    EXPECT_GE(frontCenter.y - bike.config().wheelRadius,
              localGround - 1.0e-4f);
    EXPECT_TRUE(finiteState(state));
}

TEST(Bike, HighSpeedBarrierImpactCrashesAndDoesNotClimbWall) {
    BikeConfig config;
    config.maxDriveForce = 6000.0f;
    config.dragCoefficient = 0.05f;
    config.gearCount = 6u;
    Bike bike(config);
    settle(bike);
    BikeInput throttle;
    throttle.throttle = 1.0f;
    for (int tick = 0; tick < 1200 && bike.state().speed < 24.1f; ++tick) {
        throttle.shiftUp = tick == 100 || tick == 220 || tick == 360 ||
                           tick == 520 || tick == 700;
        bike.step(throttle, 0.0f, kDry, kStep);
    }
    ASSERT_GE(bike.state().speed, 24.1f);
    ASSERT_EQ(bike.state().crash, CrashState::Riding);

    const float speedBefore = bike.state().speed;
    const glm::vec3 forward = glm::normalize(
        bike.state().chassisOrientation * glm::vec3(0.0f, 0.0f, 1.0f));
    const glm::vec3 frontBefore = bike.state().chassisPosition +
        bike.state().chassisOrientation * bike.config().frontAnchorLocal;
    const float wallZ = frontBefore.z + 0.10f;
    const auto wall = [wallZ](float, float z) {
        return z >= wallZ ? 20.0f : 0.0f;
    };
    bike.step(throttle, wall, kDry, kStep);

    const BikeState& state = bike.state();
    const glm::vec3 frontAfter = state.chassisPosition +
        state.chassisOrientation * bike.config().frontAnchorLocal;
    EXPECT_EQ(state.crash, CrashState::HighSided);
    EXPECT_GE(state.barrierImpactSpeed, speedBefore * 0.90f);
    EXPECT_LE(state.barrierImpactSpeed, speedBefore * 1.10f);
    EXPECT_LT(frontAfter.z, wallZ);
    EXPECT_LT(state.chassisPosition.y, 5.0f);
    EXPECT_LE(glm::dot(state.chassisLinearVelocity, forward), 0.0f);
    EXPECT_LE(std::abs(glm::dot(state.chassisLinearVelocity, forward)),
              state.barrierImpactSpeed * 0.25f);
    EXPECT_GE(glm::length(state.chassisAngularVelocity), 2.0f);
    EXPECT_LE(glm::length(state.chassisAngularVelocity), 30.0f);
    EXPECT_TRUE(finiteState(state));

    bike.step(BikeInput{}, 0.0f, kDry, kStep);
    EXPECT_FLOAT_EQ(bike.state().barrierImpactSpeed, 0.0f);
}

TEST(Bike, DiagonalWallImpactPreservesTangentialVelocity) {
    BikeConfig config;
    config.maxDriveForce = 6000.0f;
    config.dragCoefficient = 0.05f;
    config.gearCount = 6u;
    Bike bike(config);
    bike.reset(glm::vec3(0.0f, 1.0f, 0.0f), glm::quarter_pi<float>());
    settle(bike);
    BikeInput throttle;
    throttle.throttle = 1.0f;
    for (int tick = 0; tick < 1200 && bike.state().speed < 24.1f; ++tick) {
        throttle.shiftUp = tick == 100 || tick == 220 || tick == 360 ||
                           tick == 520 || tick == 700;
        bike.step(throttle, 0.0f, kDry, kStep);
    }
    ASSERT_GE(bike.state().speed, 24.1f);
    const glm::vec3 velocityBefore = bike.state().chassisLinearVelocity;
    ASSERT_GT(velocityBefore.x, 15.0f);
    ASSERT_GT(velocityBefore.z, 15.0f);

    const glm::vec3 frontBefore = bike.state().chassisPosition +
        bike.state().chassisOrientation * bike.config().frontAnchorLocal;
    const float wallZ = frontBefore.z + 0.10f;
    const auto zWall = [wallZ](float, float z) {
        return z >= wallZ ? 20.0f : 0.0f;
    };
    bike.step(throttle, zWall, kDry, kStep);

    const BikeState& state = bike.state();
    EXPECT_EQ(state.crash, CrashState::HighSided);
    EXPECT_GE(state.barrierImpactSpeed, velocityBefore.z * 0.90f);
    EXPECT_LE(state.barrierImpactSpeed, velocityBefore.z * 1.10f);
    EXPECT_GT(state.chassisLinearVelocity.x, velocityBefore.x * 0.90f);
    EXPECT_LT(state.chassisLinearVelocity.x, velocityBefore.x * 1.05f);
    EXPECT_LE(state.chassisLinearVelocity.z, 0.0f);
    EXPECT_LE(std::abs(state.chassisLinearVelocity.z),
              state.barrierImpactSpeed * 0.25f);
    EXPECT_TRUE(finiteState(state));
}

TEST(Bike, ThinHeightfieldBarriersCannotHideBetweenSweepSamples) {
    constexpr std::array<float, 3> widths{0.02f, 0.05f, 0.10f};
    for (const float width : widths) {
        Bike bike;
        settle(bike);
        BikeInput throttle;
        throttle.throttle = 1.0f;
        for (int tick = 0; tick < 420; ++tick) {
            bike.step(throttle, 0.0f, kDry, kStep);
        }
        ASSERT_GT(bike.state().speed, 4.0f) << "width=" << width;

        const glm::vec3 frontBefore = bike.state().chassisPosition +
            bike.state().chassisOrientation * bike.config().frontAnchorLocal;
        const float shelfStart = frontBefore.z + 0.02f;
        const float shelfEnd = shelfStart + width;
        const auto thinShelf = [shelfStart, shelfEnd](float, float z) {
            return z >= shelfStart && z <= shelfEnd ? 20.0f : 0.0f;
        };
        bike.step(throttle, thinShelf, kDry, kStep);

        const glm::vec3 frontAfter = bike.state().chassisPosition +
            bike.state().chassisOrientation * bike.config().frontAnchorLocal;
        EXPECT_LT(frontAfter.z, shelfStart) << "width=" << width;
        EXPECT_LT(bike.state().chassisPosition.y, 5.0f)
            << "width=" << width;
        EXPECT_GT(bike.state().barrierImpactSpeed, 0.0f)
            << "width=" << width;
        EXPECT_TRUE(finiteState(bike.state())) << "width=" << width;
    }
}

}  // namespace
}  // namespace voxy::moto
