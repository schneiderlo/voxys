// ═══════════════════════════════════════════════════════════════════════════════
// bike.cpp - RIDGEBREAK deterministic motorcycle dynamics
// ═══════════════════════════════════════════════════════════════════════════════

#include "moto/bike.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <glm/gtc/constants.hpp>

namespace voxy::moto {
namespace {

constexpr float kGravity = 9.81f;
constexpr float kMaxLinearSpeed = 120.0f;
constexpr float kMaxAngularSpeed = 30.0f;
constexpr float kMaxWheelAngularSpeed = 700.0f;
constexpr float kPositionLimit = 1'000'000.0f;
constexpr float kMinimumScoredAirTime = 0.22f;
constexpr float kFullRotationThreshold = 1.72f * glm::pi<float>();
constexpr float kWhipThreshold = 0.58f;
constexpr uint64_t kMaximumScore = 1'000'000'000'000ull;
constexpr float kTerrainSweepSpacing = 0.01f;
constexpr int kMaximumTerrainSweepSteps = 256;
constexpr int kTerrainToiIterations = 8;
constexpr float kTerrainNormalProbe = 0.0075f;
constexpr float kTerrainContactSlop = 0.002f;

bool finite(float value) noexcept {
    return std::isfinite(value);
}

bool finite(const glm::vec3& value) noexcept {
    return finite(value.x) && finite(value.y) && finite(value.z);
}

bool finite(const glm::quat& value) noexcept {
    return finite(value.w) && finite(value.x) && finite(value.y) &&
           finite(value.z);
}

float finiteOr(float value, float fallback) noexcept {
    return finite(value) ? value : fallback;
}

float safePositive(float value, float fallback) noexcept {
    return finite(value) && value > 0.0f ? value : fallback;
}

glm::vec3 limited(glm::vec3 value, float maximum) noexcept {
    if (!finite(value)) {
        return glm::vec3(0.0f);
    }
    const float lengthSquared = glm::dot(value, value);
    if (!finite(lengthSquared) || lengthSquared > maximum * maximum) {
        const float length = std::sqrt(std::max(lengthSquared, 1.0e-12f));
        value *= maximum / length;
    }
    return value;
}

glm::vec3 normalizedOr(const glm::vec3& value,
                       const glm::vec3& fallback) noexcept {
    const float lengthSquared = glm::dot(value, value);
    if (!finite(lengthSquared) || lengthSquared < 1.0e-10f) {
        return fallback;
    }
    return value / std::sqrt(lengthSquared);
}

float approach(float value, float target, float amount) noexcept {
    if (value < target) {
        return std::min(value + amount, target);
    }
    return std::max(value - amount, target);
}

float opposingForce(float velocity, float magnitude) noexcept {
    if (std::abs(velocity) < 0.03f) {
        return 0.0f;
    }
    return -std::copysign(magnitude, velocity);
}

uint32_t trickBit(TrickFlags trick) noexcept {
    return static_cast<uint32_t>(trick);
}

uint32_t popcount(uint32_t value) noexcept {
    uint32_t count = 0u;
    while (value != 0u) {
        value &= value - 1u;
        ++count;
    }
    return count;
}

float horizontalHeading(const glm::vec3& forward, float fallback) noexcept {
    if (!finite(forward.x) || !finite(forward.z) ||
        forward.x * forward.x + forward.z * forward.z < 1.0e-8f) {
        return fallback;
    }
    return std::atan2(forward.x, forward.z);
}

struct Contact {
    glm::vec3 offset{0.0f};
    glm::vec3 normal{0.0f, 1.0f, 0.0f};
    glm::vec3 forward{0.0f, 0.0f, 1.0f};
    float compression = 0.0f;
    float load = 0.0f;
    float longitudinalSpeed = 0.0f;
    bool touching = false;
};

}  // namespace

Bike::Bike(const BikeConfig& config) : config_(config) {
    const BikeConfig defaults;
    config_.chassisMass = safePositive(config_.chassisMass,
                                       defaults.chassisMass);
    config_.wheelbase = safePositive(config_.wheelbase, defaults.wheelbase);
    config_.wheelRadius = safePositive(config_.wheelRadius,
                                       defaults.wheelRadius);
    config_.rearWheelMass = safePositive(config_.rearWheelMass,
                                         defaults.rearWheelMass);
    config_.frontWheelMass = safePositive(config_.frontWheelMass,
                                          defaults.frontWheelMass);
    config_.rearWheelInertia = safePositive(config_.rearWheelInertia,
                                            defaults.rearWheelInertia);
    config_.frontWheelInertia = safePositive(config_.frontWheelInertia,
                                             defaults.frontWheelInertia);
    if (!finite(config_.rearAnchorLocal)) {
        config_.rearAnchorLocal = defaults.rearAnchorLocal;
    }
    if (!finite(config_.frontAnchorLocal)) {
        config_.frontAnchorLocal = defaults.frontAnchorLocal;
    }
    config_.frontSuspensionTravel = safePositive(
        config_.frontSuspensionTravel, defaults.frontSuspensionTravel);
    config_.rearSuspensionTravel = safePositive(
        config_.rearSuspensionTravel, defaults.rearSuspensionTravel);
    config_.frontSuspensionStiffness = safePositive(
        config_.frontSuspensionStiffness,
        defaults.frontSuspensionStiffness);
    config_.frontSuspensionDamping = safePositive(
        config_.frontSuspensionDamping, defaults.frontSuspensionDamping);
    config_.rearSuspensionStiffness = safePositive(
        config_.rearSuspensionStiffness, defaults.rearSuspensionStiffness);
    config_.rearSuspensionDamping = safePositive(
        config_.rearSuspensionDamping, defaults.rearSuspensionDamping);
    if (!finite(config_.chassisInertia) || config_.chassisInertia.x <= 0.0f ||
        config_.chassisInertia.y <= 0.0f ||
        config_.chassisInertia.z <= 0.0f) {
        config_.chassisInertia = defaults.chassisInertia;
    }
    config_.engineMaxTorque = safePositive(config_.engineMaxTorque,
                                           defaults.engineMaxTorque);
    config_.idleRPM = safePositive(config_.idleRPM, defaults.idleRPM);
    config_.peakRPM = std::max(safePositive(config_.peakRPM, defaults.peakRPM),
                               config_.idleRPM + 1.0f);
    config_.redlineRPM = std::max(
        safePositive(config_.redlineRPM, defaults.redlineRPM),
        config_.peakRPM + 1.0f);
    config_.primaryRatio = safePositive(config_.primaryRatio,
                                        defaults.primaryRatio);
    config_.finalDriveRatio = safePositive(config_.finalDriveRatio,
                                           defaults.finalDriveRatio);
    config_.gearCount = std::clamp(config_.gearCount, 1u, 6u);
    for (uint32_t i = 0; i < 6u; ++i) {
        config_.gearRatios[i] = safePositive(config_.gearRatios[i],
                                             defaults.gearRatios[i]);
    }
    config_.rearBrakeTorque = safePositive(config_.rearBrakeTorque,
                                           defaults.rearBrakeTorque);
    config_.frontBrakeTorque = safePositive(config_.frontBrakeTorque,
                                            defaults.frontBrakeTorque);
    config_.brakeBias = std::clamp(finiteOr(config_.brakeBias,
                                            defaults.brakeBias),
                                   0.0f, 1.0f);
    config_.rearGrip = safePositive(config_.rearGrip, defaults.rearGrip);
    config_.frontGrip = safePositive(config_.frontGrip, defaults.frontGrip);
    config_.maxDriveForce = safePositive(config_.maxDriveForce,
                                         defaults.maxDriveForce);
    config_.longitudinalSlipPeak = safePositive(
        config_.longitudinalSlipPeak, defaults.longitudinalSlipPeak);
    config_.slipReferenceSpeed = safePositive(
        config_.slipReferenceSpeed, defaults.slipReferenceSpeed);
    config_.wheelAngularDamping = std::max(finiteOr(
        config_.wheelAngularDamping, defaults.wheelAngularDamping), 0.0f);
    config_.dragCoefficient = std::max(finiteOr(
        config_.dragCoefficient, defaults.dragCoefficient), 0.0f);
    config_.rollingResistance = std::max(finiteOr(
        config_.rollingResistance, defaults.rollingResistance), 0.0f);
    config_.steeringMaxAngle = safePositive(config_.steeringMaxAngle,
                                            defaults.steeringMaxAngle);
    config_.steeringSpeed = safePositive(config_.steeringSpeed,
                                         defaults.steeringSpeed);
    config_.steeringSelfCenter = safePositive(config_.steeringSelfCenter,
                                              defaults.steeringSelfCenter);
    config_.leanTorque = safePositive(config_.leanTorque, defaults.leanTorque);
    config_.wheelieTorque = safePositive(config_.wheelieTorque,
                                         defaults.wheelieTorque);
    config_.stoppieTorque = safePositive(config_.stoppieTorque,
                                         defaults.stoppieTorque);
    config_.airPitchTorque = safePositive(config_.airPitchTorque,
                                          defaults.airPitchTorque);
    config_.airRollTorque = safePositive(config_.airRollTorque,
                                         defaults.airRollTorque);
    config_.airYawTorque = safePositive(config_.airYawTorque,
                                        defaults.airYawTorque);
    config_.airAngularDamping = safePositive(config_.airAngularDamping,
                                             defaults.airAngularDamping);
    config_.landingAssistTorque = safePositive(config_.landingAssistTorque,
                                               defaults.landingAssistTorque);
    config_.groundedBeforeRemount = std::max(finiteOr(
        config_.groundedBeforeRemount, defaults.groundedBeforeRemount), 0.0f);
    config_.automaticRemountDelay = std::max(
        finiteOr(config_.automaticRemountDelay,
                 defaults.automaticRemountDelay),
        config_.groundedBeforeRemount);
    config_.remountDuration = safePositive(config_.remountDuration,
                                           defaults.remountDuration);
    config_.barrierCrashSpeed = safePositive(config_.barrierCrashSpeed,
                                             defaults.barrierCrashSpeed);
    config_.barrierRestitution = std::clamp(finiteOr(
        config_.barrierRestitution, defaults.barrierRestitution), 0.0f, 0.25f);
    reset(glm::vec3(0.0f, 1.0f, 0.0f), 0.0f);
}

Bike::~Bike() = default;

void Bike::reset(const glm::vec3& position, float yaw) {
    state_ = BikeState{};
    state_.chassisPosition = finite(position) ? position
                                              : glm::vec3(0.0f, 1.0f, 0.0f);
    state_.chassisPosition = glm::clamp(
        state_.chassisPosition, glm::vec3(-kPositionLimit),
        glm::vec3(kPositionLimit));
    yaw = finiteOr(yaw, 0.0f);
    yaw = std::remainder(yaw, glm::two_pi<float>());
    state_.chassisOrientation = glm::angleAxis(
        yaw, glm::vec3(0.0f, 1.0f, 0.0f));
    state_.engineRPM = config_.idleRPM;
    state_.gear = 1u;
    state_.airborne = true;
}

void Bike::step(const BikeInput& rawInput,
                const std::function<float(float, float)>& heightAt,
                float waterHeight, float dt) {
    if (!finite(dt) || dt <= 0.0f) {
        return;
    }
    dt = std::min(dt, 1.0f / 30.0f);

    BikeInput input = rawInput;
    input.throttle = std::clamp(finiteOr(input.throttle, 0.0f), 0.0f, 1.0f);
    input.brake = std::clamp(finiteOr(input.brake, 0.0f), 0.0f, 1.0f);
    input.steer = std::clamp(finiteOr(input.steer, 0.0f), -1.0f, 1.0f);
    input.lean = std::clamp(finiteOr(input.lean, 0.0f), -1.0f, 1.0f);
    state_.throttle = input.throttle;
    // Barrier telemetry describes this fixed tick, not a stale historical hit.
    state_.barrierImpactSpeed = 0.0f;

    if (input.shiftUp && state_.gear < config_.gearCount) {
        ++state_.gear;
    } else if (input.shiftDown && state_.gear > 1u) {
        --state_.gear;
    }

    const float steeringTarget = input.steer * config_.steeringMaxAngle;
    state_.steeringAngle = approach(
        state_.steeringAngle, steeringTarget, config_.steeringSpeed * dt);
    if (std::abs(input.steer) < 1.0e-4f) {
        state_.steeringAngle = approach(
            state_.steeringAngle, 0.0f,
            config_.steeringSelfCenter * config_.steeringMaxAngle * dt);
    }
    state_.steeringAngle = std::clamp(state_.steeringAngle,
                                      -config_.steeringMaxAngle,
                                      config_.steeringMaxAngle);

    if (!finite(state_.chassisPosition)) {
        state_.chassisPosition = glm::vec3(0.0f, 1.0f, 0.0f);
    }
    if (!finite(state_.chassisLinearVelocity)) {
        state_.chassisLinearVelocity = glm::vec3(0.0f);
    }
    if (!finite(state_.chassisAngularVelocity)) {
        state_.chassisAngularVelocity = glm::vec3(0.0f);
    }
    state_.frontWheelAngularVelocity = std::clamp(
        finiteOr(state_.frontWheelAngularVelocity, 0.0f),
        -kMaxWheelAngularSpeed, kMaxWheelAngularSpeed);
    state_.rearWheelAngularVelocity = std::clamp(
        finiteOr(state_.rearWheelAngularVelocity, 0.0f),
        -kMaxWheelAngularSpeed, kMaxWheelAngularSpeed);
    const float orientationLength2 = glm::dot(state_.chassisOrientation,
                                              state_.chassisOrientation);
    if (!finite(state_.chassisOrientation) || !finite(orientationLength2) ||
        orientationLength2 < 1.0e-8f) {
        state_.chassisOrientation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    } else {
        state_.chassisOrientation /= std::sqrt(orientationLength2);
    }

    const glm::quat orientation = state_.chassisOrientation;
    const glm::vec3 bodyForward = normalizedOr(
        orientation * glm::vec3(0.0f, 0.0f, 1.0f),
        glm::vec3(0.0f, 0.0f, 1.0f));
    const glm::vec3 bodyRight = normalizedOr(
        orientation * glm::vec3(1.0f, 0.0f, 0.0f),
        glm::vec3(1.0f, 0.0f, 0.0f));
    const glm::vec3 bodyUp = normalizedOr(
        orientation * glm::vec3(0.0f, 1.0f, 0.0f),
        glm::vec3(0.0f, 1.0f, 0.0f));

    auto sampleHeight = [&](float x, float z, bool& valid) {
        valid = false;
        if (!heightAt || !finite(x) || !finite(z)) {
            return 0.0f;
        }
        const float height = heightAt(x, z);
        if (!finite(height)) {
            return 0.0f;
        }
        valid = true;
        return std::clamp(height, -100'000.0f, 100'000.0f);
    };

    const bool wasAirborne = state_.airborne;
    const float completedAirTime = state_.airTime;
    const float landingVelocity = state_.chassisLinearVelocity.y;
    const float totalMass = config_.chassisMass + config_.frontWheelMass +
                            config_.rearWheelMass;
    glm::vec3 totalForce(0.0f, -totalMass * kGravity, 0.0f);
    glm::vec3 totalTorque(0.0f);

    auto makeContact = [&](const glm::vec3& configuredAnchor, float travel,
                           float stiffness, float damping,
                           float previousCompression, float steering) {
        Contact contact;
        contact.offset = orientation * configuredAnchor;
        const glm::vec3 anchorWorld = state_.chassisPosition + contact.offset;
        bool centerValid = false;
        const float centerHeight = sampleHeight(anchorWorld.x, anchorWorld.z,
                                                centerValid);
        if (!centerValid) {
            return contact;
        }

        constexpr float epsilon = 0.15f;
        bool xpValid = false;
        bool xmValid = false;
        bool zpValid = false;
        bool zmValid = false;
        const float xp = sampleHeight(anchorWorld.x + epsilon, anchorWorld.z,
                                      xpValid);
        const float xm = sampleHeight(anchorWorld.x - epsilon, anchorWorld.z,
                                      xmValid);
        const float zp = sampleHeight(anchorWorld.x, anchorWorld.z + epsilon,
                                      zpValid);
        const float zm = sampleHeight(anchorWorld.x, anchorWorld.z - epsilon,
                                      zmValid);
        const float dx = xpValid && xmValid ? (xp - xm) / (2.0f * epsilon)
                                            : 0.0f;
        const float dz = zpValid && zmValid ? (zp - zm) / (2.0f * epsilon)
                                            : 0.0f;
        contact.normal = normalizedOr(glm::vec3(-dx, 1.0f, -dz),
                                      glm::vec3(0.0f, 1.0f, 0.0f));

        const float wheelDistance = anchorWorld.y - centerHeight;
        const float rawCompression = config_.wheelRadius + travel -
                                     wheelDistance;
        if (rawCompression <= 0.0f || wheelDistance < -config_.wheelRadius) {
            return contact;
        }
        contact.touching = true;
        contact.compression = std::clamp(rawCompression, 0.0f, travel);
        const float compressionSpeed = std::clamp(
            (contact.compression - previousCompression) / dt, -20.0f, 20.0f);
        const float sprungLoad = stiffness * contact.compression +
                                 damping * compressionSpeed;
        contact.load = std::clamp(sprungLoad, 0.0f,
                                  totalMass * kGravity * 8.0f);

        glm::vec3 wheelForward = bodyForward;
        if (steering != 0.0f) {
            wheelForward = glm::angleAxis(steering, contact.normal) *
                           wheelForward;
        }
        wheelForward -= contact.normal * glm::dot(wheelForward,
                                                   contact.normal);
        contact.forward = normalizedOr(wheelForward,
                                       glm::vec3(0.0f, 0.0f, 1.0f));
        const glm::vec3 pointVelocity = state_.chassisLinearVelocity +
            glm::cross(state_.chassisAngularVelocity, contact.offset);
        contact.longitudinalSpeed = glm::dot(pointVelocity, contact.forward);
        return contact;
    };

    Contact rear = makeContact(config_.rearAnchorLocal,
                               config_.rearSuspensionTravel,
                               config_.rearSuspensionStiffness,
                               config_.rearSuspensionDamping,
                               state_.rearSuspension, 0.0f);
    Contact front = makeContact(config_.frontAnchorLocal,
                                config_.frontSuspensionTravel,
                                config_.frontSuspensionStiffness,
                                config_.frontSuspensionDamping,
                                state_.frontSuspension,
                                state_.steeringAngle);
    state_.rearSuspension = rear.compression;
    state_.frontSuspension = front.compression;
    state_.rearTouch = rear.touching;
    state_.frontTouch = front.touching;
    state_.airborne = !rear.touching && !front.touching;
    const float upright = glm::dot(bodyUp, glm::vec3(0.0f, 1.0f, 0.0f));
    const bool tookOff = state_.crash == CrashState::Riding &&
                         state_.airborne && !wasAirborne;
    const bool landed = state_.crash == CrashState::Riding &&
                        !state_.airborne && wasAirborne;
    if (tookOff) {
        state_.airTime = dt;
        state_.aerialRotation = glm::vec3(0.0f);
        state_.aerialTricks = 0u;
        state_.lastLanding = LandingQuality::None;
        state_.lastTrickPoints = 0u;
    } else if (state_.airborne && state_.crash == CrashState::Riding) {
        state_.airTime = std::min(state_.airTime + dt, 10'000.0f);
    } else {
        state_.airTime = 0.0f;
    }

    if (state_.airborne && state_.crash == CrashState::Riding) {
        const glm::vec3 localAngularVelocity =
            glm::conjugate(orientation) * state_.chassisAngularVelocity;
        if (finite(localAngularVelocity)) {
            state_.aerialRotation += localAngularVelocity * dt;
            state_.aerialRotation = glm::clamp(
                state_.aerialRotation, glm::vec3(-100.0f),
                glm::vec3(100.0f));
            if (state_.aerialRotation.x <= -kFullRotationThreshold) {
                state_.aerialTricks |= trickBit(TrickFlags::Backflip);
            }
            if (state_.aerialRotation.x >= kFullRotationThreshold) {
                state_.aerialTricks |= trickBit(TrickFlags::Frontflip);
            }
            if (std::abs(state_.aerialRotation.z) >=
                kFullRotationThreshold) {
                state_.aerialTricks |= trickBit(TrickFlags::BarrelRoll);
            }
            if (std::abs(state_.aerialRotation.y) >= kWhipThreshold) {
                state_.aerialTricks |= trickBit(TrickFlags::Whip);
            }
        }
    }

    if (landed) {
        state_.lastAirTime = completedAirTime;
        state_.lastLandedTricks = 0u;
        const float impactSpeed = std::max(0.0f, -landingVelocity);
        const glm::vec3 landingNormal = rear.touching ? rear.normal
                                                      : front.normal;
        const float surfaceUpright = glm::dot(bodyUp, landingNormal);
        const float pitchError = std::abs(glm::dot(bodyForward,
                                                   landingNormal));
        const float angularSpeed = glm::length(
            state_.chassisAngularVelocity);
        const bool violentImpact = impactSpeed > 11.5f;
        const bool badAttitude = surfaceUpright < 0.34f || pitchError > 0.78f;
        const bool uncontrolledSpin = angularSpeed > 11.0f;
        if (violentImpact || badAttitude || uncontrolledSpin) {
            state_.lastLanding = LandingQuality::Crashed;
            state_.lastTrickPoints = 0u;
            state_.comboCount = 0u;
            state_.crash = violentImpact ? CrashState::HighSided
                                         : CrashState::WipedOut;
            state_.crashTime = 0.0f;
            state_.remountTime = 0.0f;
            state_.chassisAngularVelocity += bodyRight *
                (violentImpact ? 5.5f : 2.5f) + bodyForward * 3.0f;
        } else if (completedAirTime >= kMinimumScoredAirTime) {
            if (impactSpeed < 5.2f && surfaceUpright > 0.92f &&
                pitchError < 0.20f && angularSpeed < 2.4f) {
                state_.lastLanding = LandingQuality::Perfect;
            } else if (impactSpeed < 8.0f && surfaceUpright > 0.72f &&
                       pitchError < 0.45f && angularSpeed < 5.5f) {
                state_.lastLanding = LandingQuality::Clean;
            } else {
                state_.lastLanding = LandingQuality::Sketchy;
            }

            uint32_t landedTricks = state_.aerialTricks;
            // A whip is an out-and-back movement, not merely landing while
            // facing another direction. Full rotations remain valid because
            // their signed accumulated angle is expected to be near ±2*pi.
            if ((landedTricks & trickBit(TrickFlags::Whip)) != 0u &&
                std::abs(state_.aerialRotation.y) > 0.55f) {
                landedTricks &= ~trickBit(TrickFlags::Whip);
            }
            state_.lastLandedTricks = landedTricks;

            uint32_t basePoints = 0u;
            if ((landedTricks & trickBit(TrickFlags::Whip)) != 0u) {
                basePoints += 350u;
            }
            if ((landedTricks & trickBit(TrickFlags::Backflip)) != 0u) {
                basePoints += 1000u;
            }
            if ((landedTricks & trickBit(TrickFlags::Frontflip)) != 0u) {
                basePoints += 1000u;
            }
            if ((landedTricks &
                 trickBit(TrickFlags::BarrelRoll)) != 0u) {
                basePoints += 1200u;
            }
            if (basePoints != 0u) {
                basePoints += static_cast<uint32_t>(std::min(
                    completedAirTime * 100.0f, 1000.0f));
                state_.comboCount = std::min(state_.comboCount + 1u, 20u);
                state_.bestCombo = std::max(state_.bestCombo,
                                            state_.comboCount);
                state_.tricksLanded = std::min(
                    state_.tricksLanded + popcount(landedTricks),
                    1'000'000u);
                const uint32_t qualityPercent =
                    state_.lastLanding == LandingQuality::Perfect ? 200u :
                    state_.lastLanding == LandingQuality::Clean ? 125u : 65u;
                const uint32_t comboPercent = 100u +
                    (state_.comboCount - 1u) * 20u;
                const uint64_t awarded =
                    (static_cast<uint64_t>(basePoints) * qualityPercent *
                     comboPercent + 5000u) / 10'000u;
                state_.lastTrickPoints = static_cast<uint32_t>(std::min(
                    awarded, static_cast<uint64_t>(
                                 std::numeric_limits<uint32_t>::max())));
                state_.trickScore = std::min(
                    state_.trickScore + state_.lastTrickPoints,
                    kMaximumScore);
            }
        }
    } else if (state_.crash == CrashState::Riding && !state_.airborne &&
               upright < 0.30f) {
        state_.lastLanding = LandingQuality::Crashed;
        state_.comboCount = 0u;
        state_.crash = CrashState::WipedOut;
        state_.crashTime = 0.0f;
        state_.remountTime = 0.0f;
        state_.chassisAngularVelocity += bodyForward * 4.0f;
    }

    const float driveRatio = config_.primaryRatio * config_.finalDriveRatio *
                             config_.gearRatio(state_.gear);
    const float wheelRPM = std::abs(state_.rearWheelAngularVelocity) *
        (30.0f / glm::pi<float>());
    const float coupledRPM = wheelRPM * driveRatio;
    const float targetRPM = std::clamp(
        std::max(config_.idleRPM, coupledRPM), config_.idleRPM,
        config_.redlineRPM);
    state_.engineRPM = approach(state_.engineRPM, targetRPM, 18'000.0f * dt);

    float torqueFactor = 1.0f;
    if (state_.engineRPM <= config_.peakRPM) {
        const float range = std::max(config_.peakRPM - config_.idleRPM, 1.0f);
        torqueFactor = 0.55f + 0.45f *
            (state_.engineRPM - config_.idleRPM) / range;
    } else {
        const float range = std::max(config_.redlineRPM - config_.peakRPM,
                                     1.0f);
        torqueFactor = 1.0f - 0.85f *
            (state_.engineRPM - config_.peakRPM) / range;
    }
    torqueFactor = std::clamp(torqueFactor, 0.0f, 1.0f);

    float rearTireReactionTorque = 0.0f;
    float frontTireReactionTorque = 0.0f;
    auto applyContact = [&](Contact& contact, float grip,
                            float wheelAngularVelocity,
                            float wheelInertia,
                            float& tireReactionTorque) {
        if (!contact.touching) {
            return;
        }
        const glm::vec3 pointVelocity = state_.chassisLinearVelocity +
            glm::cross(state_.chassisAngularVelocity, contact.offset);
        const glm::vec3 wheelRight = normalizedOr(
            glm::cross(contact.normal, contact.forward), bodyRight);
        const float lateralSpeed = glm::dot(pointVelocity, wheelRight);
        const float lateralLimit = grip * contact.load;
        const float lateralForce = std::clamp(
            -lateralSpeed * totalMass * 7.0f,
            -lateralLimit, lateralLimit);
        const float remainingGrip = std::sqrt(std::max(
            0.0f, lateralLimit * lateralLimit -
                  lateralForce * lateralForce));

        const float wheelSurfaceSpeed = wheelAngularVelocity *
                                        config_.wheelRadius;
        const float slipDenominator = std::max(
            std::abs(contact.longitudinalSpeed),
            config_.slipReferenceSpeed);
        const float slipRatio = std::clamp(
            (wheelSurfaceSpeed - contact.longitudinalSpeed) /
                slipDenominator,
            -20.0f, 20.0f);
        float tireForce = grip * contact.load * std::tanh(
            slipRatio / config_.longitudinalSlipPeak);
        // Do not allow explicit integration to drive the wheel through the
        // no-slip speed in one tick. Load/grip still sets the force curve;
        // this momentum cap only regularizes launch, lock, and touchdown.
        const float recouplingForce = std::abs(
            wheelSurfaceSpeed - contact.longitudinalSpeed) * wheelInertia /
            (config_.wheelRadius * config_.wheelRadius * dt);
        tireForce = std::clamp(tireForce, -recouplingForce, recouplingForce);
        tireForce = std::clamp(tireForce, -remainingGrip, remainingGrip);
        const float rollingForce = opposingForce(
            contact.longitudinalSpeed,
            config_.rollingResistance * contact.load);
        const float longitudinalForce = std::clamp(
            tireForce + rollingForce, -remainingGrip, remainingGrip);
        tireReactionTorque = -tireForce * config_.wheelRadius;
        const glm::vec3 force = contact.normal * contact.load +
                                wheelRight * lateralForce +
                                contact.forward * longitudinalForce;
        totalForce += force;
        totalTorque += glm::cross(contact.offset, force);
    };

    const bool riding = state_.crash == CrashState::Riding;
    float rearDriveTorque = 0.0f;
    if (riding && state_.engineRPM < config_.redlineRPM) {
        const float crankTorque = config_.engineMaxTorque * torqueFactor *
                                  input.throttle;
        rearDriveTorque = std::min(
            config_.maxDriveForce * config_.wheelRadius,
            crankTorque * driveRatio);
    }
    const float frontBrakeTorque = riding
        ? input.brake * config_.frontBrakeTorque * config_.brakeBias : 0.0f;
    const float rearBrakeTorque = riding
        ? input.brake * config_.rearBrakeTorque * (1.0f - config_.brakeBias)
        : 0.0f;
    applyContact(rear, config_.rearGrip,
                 state_.rearWheelAngularVelocity,
                 config_.rearWheelInertia, rearTireReactionTorque);
    applyContact(front, config_.frontGrip,
                 state_.frontWheelAngularVelocity,
                 config_.frontWheelInertia, frontTireReactionTorque);

    const auto integrateWheel = [&](float angularVelocity, float inertia,
                                    float appliedTorque, float brakeTorque) {
        angularVelocity += appliedTorque / inertia * dt;
        angularVelocity = approach(
            angularVelocity, 0.0f, brakeTorque / inertia * dt);
        angularVelocity *= std::max(
            0.0f, 1.0f - config_.wheelAngularDamping * dt);
        return std::clamp(angularVelocity,
                          -kMaxWheelAngularSpeed,
                          kMaxWheelAngularSpeed);
    };
    state_.rearWheelAngularVelocity = integrateWheel(
        state_.rearWheelAngularVelocity, config_.rearWheelInertia,
        rearDriveTorque + rearTireReactionTorque, rearBrakeTorque);
    state_.frontWheelAngularVelocity = integrateWheel(
        state_.frontWheelAngularVelocity, config_.frontWheelInertia,
        frontTireReactionTorque, frontBrakeTorque);

    const float linearSpeed = glm::length(state_.chassisLinearVelocity);
    if (linearSpeed > 1.0e-4f) {
        totalForce -= state_.chassisLinearVelocity * linearSpeed *
                      config_.dragCoefficient;
    }

    if (riding && state_.airborne) {
        const float pitchCommand = input.brake - input.throttle;
        const float postureScale = input.seated ? 0.78f : 1.0f;
        totalTorque += bodyRight * (config_.airPitchTorque * pitchCommand *
                                    postureScale);
        totalTorque += bodyForward * (config_.airRollTorque * input.lean);
        totalTorque += bodyUp * (config_.airYawTorque * input.steer *
                                 (0.75f + 0.25f * std::abs(input.lean)));
        totalTorque -= state_.chassisAngularVelocity *
                       config_.airAngularDamping;

        // A modest correction while descending rewards releasing the stunt
        // controls before touchdown. It cannot rescue a late full rotation.
        const float controlMagnitude = std::max(
            {std::abs(pitchCommand), std::abs(input.lean),
             std::abs(input.steer)});
        if (state_.chassisLinearVelocity.y < -0.5f &&
            controlMagnitude < 0.15f) {
            const glm::vec3 correctionAxis = glm::cross(
                bodyUp, glm::vec3(0.0f, 1.0f, 0.0f));
            totalTorque += correctionAxis * config_.landingAssistTorque;
        }
    } else if (riding) {
        const float roll = std::asin(std::clamp(bodyRight.y, -1.0f, 1.0f));
        const float rollRate = glm::dot(state_.chassisAngularVelocity,
                                        bodyForward);
        totalTorque += bodyForward * (config_.leanTorque * input.lean -
            config_.leanTorque * 1.5f * roll - 180.0f * rollRate);
        const float pitch = std::asin(std::clamp(bodyForward.y, -1.0f, 1.0f));
        const float pitchRate = glm::dot(state_.chassisAngularVelocity,
                                         bodyRight);
        totalTorque += bodyRight * (5'000.0f * pitch -
                                    350.0f * pitchRate);
        const float posture = input.seated ? 1.0f : 0.45f;
        const float duckScale = input.duck ? 0.75f : 1.0f;
        totalTorque += bodyRight * duckScale * posture *
            (config_.stoppieTorque * input.brake -
             config_.wheelieTorque * input.throttle);
    }

    if (finite(waterHeight)) {
        const float bodyBottom = state_.chassisPosition.y - 0.45f;
        const float submerged = std::clamp(
            (waterHeight - bodyBottom) / 0.9f, 0.0f, 1.0f);
        if (submerged > 0.0f) {
            totalForce.y += totalMass * kGravity * 1.08f *
                            submerged;
            const float waterSpeed = glm::length(state_.chassisLinearVelocity);
            totalForce -= state_.chassisLinearVelocity * submerged *
                totalMass * (3.0f + 0.35f * waterSpeed);
            totalTorque -= state_.chassisAngularVelocity * submerged * 210.0f;
        }
    }

    if (state_.crash != CrashState::Riding) {
        state_.crashTime = std::min(state_.crashTime + dt, 10'000.0f);
        const float damping = state_.crash == CrashState::Remounting
            ? 420.0f : 95.0f;
        totalTorque -= state_.chassisAngularVelocity * damping;
    }

    state_.chassisLinearVelocity += totalForce / totalMass * dt;
    const glm::vec3 localTorque = glm::conjugate(orientation) * totalTorque;
    const glm::vec3 localAngularAcceleration(
        localTorque.x / config_.chassisInertia.x,
        localTorque.y / config_.chassisInertia.y,
        localTorque.z / config_.chassisInertia.z);
    state_.chassisAngularVelocity += orientation *
                                     localAngularAcceleration * dt;
    state_.chassisLinearVelocity = limited(state_.chassisLinearVelocity,
                                           kMaxLinearSpeed);
    state_.chassisAngularVelocity = limited(state_.chassisAngularVelocity,
                                            kMaxAngularSpeed);

    const glm::vec3 oldPosition = state_.chassisPosition;
    integrate(dt);

    // Contact generation happens before integration, so a fast wheel can move
    // from above a heightfield to deeply below it in one tick. Sweep the wheel
    // bottoms along the attempted motion. Large height discontinuities behave
    // as barriers; vertical/deep penetrations are projected back to the
    // surface. The bounded sample count keeps the cost deterministic.
    const glm::vec3 attemptedPosition = state_.chassisPosition;
    const glm::quat attemptedOrientation = state_.chassisOrientation;
    const glm::vec3 attemptedDisplacement = attemptedPosition - oldPosition;
    const float horizontalDistance = std::sqrt(
        attemptedDisplacement.x * attemptedDisplacement.x +
        attemptedDisplacement.z * attemptedDisplacement.z);
    const int sweepSteps = std::clamp(
        static_cast<int>(std::ceil(horizontalDistance /
                                   kTerrainSweepSpacing)),
        1, kMaximumTerrainSweepSteps);
    const float deepBarrierThreshold = std::max(
        config_.frontSuspensionTravel, config_.rearSuspensionTravel) + 0.15f;
    bool hitDeepBarrier = false;
    float barrierFraction = 1.0f;
    float barrierBlockedFraction = 1.0f;
    float attemptedFinalPenetration = 0.0f;

    auto wheelPenetration = [&](const glm::vec3& position,
                                const glm::quat& sampleOrientation,
                                const glm::vec3& anchor) {
        const glm::vec3 wheelCenter = position + sampleOrientation * anchor;
        bool valid = false;
        const float terrainHeight = sampleHeight(
            wheelCenter.x, wheelCenter.z, valid);
        if (!valid) return 0.0f;
        return terrainHeight + config_.wheelRadius - wheelCenter.y;
    };

    auto penetrationAtFraction = [&](float fraction) {
        const glm::vec3 samplePosition = oldPosition +
                                         attemptedDisplacement * fraction;
        glm::quat sampleOrientation = glm::slerp(
            orientation, attemptedOrientation, fraction);
        const float sampleOrientationLength2 = glm::dot(sampleOrientation,
                                                        sampleOrientation);
        if (!finite(sampleOrientation) ||
            !finite(sampleOrientationLength2) ||
            sampleOrientationLength2 < 1.0e-8f) {
            sampleOrientation = orientation;
        } else {
            sampleOrientation /= std::sqrt(sampleOrientationLength2);
        }
        return std::max(
            wheelPenetration(samplePosition, sampleOrientation,
                             config_.rearAnchorLocal),
            wheelPenetration(samplePosition, sampleOrientation,
                             config_.frontAnchorLocal));
    };

    for (int sample = 1; sample <= sweepSteps; ++sample) {
        const float fraction = static_cast<float>(sample) /
                               static_cast<float>(sweepSteps);
        const float penetration = penetrationAtFraction(fraction);
        if (sample == sweepSteps) {
            attemptedFinalPenetration = penetration;
        }
        if (penetration > deepBarrierThreshold &&
            horizontalDistance > 0.05f) {
            hitDeepBarrier = true;
            float safeFraction = static_cast<float>(sample - 1) /
                                 static_cast<float>(sweepSteps);
            float blockedFraction = fraction;
            for (int iteration = 0; iteration < kTerrainToiIterations;
                 ++iteration) {
                const float middle = 0.5f *
                                     (safeFraction + blockedFraction);
                if (penetrationAtFraction(middle) > deepBarrierThreshold) {
                    blockedFraction = middle;
                } else {
                    safeFraction = middle;
                }
            }
            barrierFraction = std::max(
                0.0f, safeFraction -
                          0.001f / std::max(horizontalDistance, 0.001f));
            barrierBlockedFraction = blockedFraction;
            break;
        }
    }

    if (hitDeepBarrier) {
        state_.chassisPosition = oldPosition +
                                 attemptedDisplacement * barrierFraction;
        state_.chassisOrientation = glm::normalize(glm::slerp(
            orientation, attemptedOrientation, barrierFraction));
        const glm::vec3 travelDirection = normalizedOr(
            glm::vec3(attemptedDisplacement.x, 0.0f,
                      attemptedDisplacement.z),
            glm::vec3(0.0f));
        const glm::vec3 blockedPosition = oldPosition +
            attemptedDisplacement * barrierBlockedFraction;
        glm::quat blockedOrientation = glm::normalize(glm::slerp(
            orientation, attemptedOrientation, barrierBlockedFraction));
        if (!finite(blockedOrientation)) blockedOrientation = orientation;
        const glm::vec3 rearCenter = blockedPosition + blockedOrientation *
                                     config_.rearAnchorLocal;
        const glm::vec3 frontCenter = blockedPosition + blockedOrientation *
                                      config_.frontAnchorLocal;
        const float rearPenetration = wheelPenetration(
            blockedPosition, blockedOrientation, config_.rearAnchorLocal);
        const float frontPenetration = wheelPenetration(
            blockedPosition, blockedOrientation, config_.frontAnchorLocal);
        const glm::vec3 impactCenter = frontPenetration > rearPenetration
            ? frontCenter : rearCenter;

        bool xpValid = false;
        bool xmValid = false;
        bool zpValid = false;
        bool zmValid = false;
        const float xp = sampleHeight(impactCenter.x + kTerrainNormalProbe,
                                      impactCenter.z, xpValid);
        const float xm = sampleHeight(impactCenter.x - kTerrainNormalProbe,
                                      impactCenter.z, xmValid);
        const float zp = sampleHeight(impactCenter.x,
                                      impactCenter.z + kTerrainNormalProbe,
                                      zpValid);
        const float zm = sampleHeight(impactCenter.x,
                                      impactCenter.z - kTerrainNormalProbe,
                                      zmValid);
        const float gradientX = xpValid && xmValid ? xp - xm : 0.0f;
        const float gradientZ = zpValid && zmValid ? zp - zm : 0.0f;
        glm::vec3 barrierIntoDirection = normalizedOr(
            glm::vec3(gradientX, 0.0f, gradientZ), travelDirection);
        if (glm::dot(barrierIntoDirection, travelDirection) < 0.0f) {
            barrierIntoDirection = -barrierIntoDirection;
        }
        const float intoBarrier = glm::dot(state_.chassisLinearVelocity,
                                           barrierIntoDirection);
        if (intoBarrier > 0.0f) {
            state_.barrierImpactSpeed = intoBarrier;
            state_.chassisLinearVelocity -= barrierIntoDirection *
                intoBarrier * (1.0f + config_.barrierRestitution);
            if (state_.crash == CrashState::Riding &&
                intoBarrier >= config_.barrierCrashSpeed) {
                state_.crash = CrashState::HighSided;
                state_.crashTime = 0.0f;
                state_.remountTime = 0.0f;
                state_.lastLanding = LandingQuality::Crashed;
                state_.lastLandedTricks = 0u;
                state_.lastTrickPoints = 0u;
                state_.comboCount = 0u;
                const float angularImpulse = std::clamp(
                    intoBarrier * 0.22f, 3.0f, 7.0f);
                const glm::vec3 impactPitchAxis = normalizedOr(
                    glm::cross(glm::vec3(0.0f, 1.0f, 0.0f),
                               barrierIntoDirection), bodyRight);
                state_.chassisAngularVelocity +=
                    impactPitchAxis * angularImpulse +
                    barrierIntoDirection * 2.0f;
                state_.chassisAngularVelocity = limited(
                    state_.chassisAngularVelocity, kMaxAngularSpeed);
            }
        }
        state_.chassisAngularVelocity *= 0.85f;
    }

    const float finalPenetration = hitDeepBarrier
        ? std::max(wheelPenetration(state_.chassisPosition,
                                    state_.chassisOrientation,
                                    config_.rearAnchorLocal),
                   wheelPenetration(state_.chassisPosition,
                                    state_.chassisOrientation,
                                    config_.frontAnchorLocal))
        : attemptedFinalPenetration;
    if (finalPenetration > 0.0f &&
        (!hitDeepBarrier || finalPenetration <= deepBarrierThreshold)) {
        state_.chassisPosition.y += finalPenetration + kTerrainContactSlop;
        state_.chassisPosition.y = std::clamp(
            state_.chassisPosition.y, -kPositionLimit, kPositionLimit);
        state_.chassisAngularVelocity *= 0.98f;
    }

    if (state_.crash != CrashState::Riding) {
        bool groundValid = false;
        const float ground = sampleHeight(state_.chassisPosition.x,
                                          state_.chassisPosition.z,
                                          groundValid);
        if (groundValid && state_.chassisPosition.y < ground + 0.20f) {
            state_.chassisPosition.y = ground + 0.20f;
            if (state_.chassisLinearVelocity.y < 0.0f) {
                state_.chassisLinearVelocity.y *= -0.18f;
            }
            state_.chassisLinearVelocity.x *= 0.88f;
            state_.chassisLinearVelocity.z *= 0.88f;
            state_.chassisAngularVelocity *= 0.86f;
        }
        if ((state_.crash == CrashState::HighSided ||
             state_.crash == CrashState::WipedOut) &&
            (state_.crashTime > 4.0f ||
             (state_.crashTime > 1.5f &&
              glm::length(state_.chassisLinearVelocity) < 0.8f &&
              glm::length(state_.chassisAngularVelocity) < 0.8f))) {
            state_.crash = CrashState::OnGround;
            state_.remountTime = 0.0f;
        }
        if (state_.crash == CrashState::OnGround) {
            state_.remountTime = std::min(state_.remountTime + dt,
                                          10'000.0f);
            const bool groundedLongEnough =
                state_.remountTime >= config_.groundedBeforeRemount;
            if (groundedLongEnough &&
                (input.remount ||
                 state_.crashTime >= config_.automaticRemountDelay)) {
                state_.crash = CrashState::Remounting;
                state_.remountTime = 0.0f;
                const glm::vec3 currentForward =
                    state_.chassisOrientation * glm::vec3(0.0f, 0.0f, 1.0f);
                state_.recoveryHeading = horizontalHeading(
                    currentForward, state_.recoveryHeading);
            }
        } else if (state_.crash == CrashState::Remounting) {
            state_.remountTime = std::min(state_.remountTime + dt,
                                          config_.remountDuration);
            state_.chassisLinearVelocity *= std::max(0.0f, 1.0f - 9.0f * dt);
            state_.chassisAngularVelocity *= std::max(0.0f,
                                                       1.0f - 12.0f * dt);
            const glm::quat uprightOrientation = glm::angleAxis(
                state_.recoveryHeading, glm::vec3(0.0f, 1.0f, 0.0f));
            const float recoveryAlpha = std::clamp(
                8.0f * dt, 0.0f, 1.0f);
            state_.chassisOrientation = glm::normalize(glm::slerp(
                state_.chassisOrientation, uprightOrientation,
                recoveryAlpha));
            if (groundValid) {
                state_.chassisPosition.y = std::max(
                    state_.chassisPosition.y, ground + 0.57f);
            }
            if (state_.remountTime >= config_.remountDuration) {
                state_.chassisOrientation = uprightOrientation;
                state_.chassisLinearVelocity = glm::vec3(0.0f);
                state_.chassisAngularVelocity = glm::vec3(0.0f);
                state_.frontSuspension = 0.0f;
                state_.rearSuspension = 0.0f;
                state_.frontWheelAngularVelocity = 0.0f;
                state_.rearWheelAngularVelocity = 0.0f;
                state_.frontTouch = false;
                state_.rearTouch = false;
                state_.airborne = true;
                state_.crash = CrashState::Riding;
                state_.crashTime = 0.0f;
                state_.remountTime = 0.0f;
            }
        }
    }

    const glm::vec3 displacement = state_.chassisPosition - oldPosition;
    state_.totalDistance = std::min(
        state_.totalDistance + std::sqrt(displacement.x * displacement.x +
                                        displacement.z * displacement.z),
        1.0e9f);
    const glm::vec3 newForward = normalizedOr(
        state_.chassisOrientation * glm::vec3(0.0f, 0.0f, 1.0f),
        glm::vec3(0.0f, 0.0f, 1.0f));
    state_.speed = glm::dot(state_.chassisLinearVelocity, newForward);

    state_.frontWheelSpin = std::remainder(
        state_.frontWheelSpin + state_.frontWheelAngularVelocity * dt,
        glm::two_pi<float>());
    state_.rearWheelSpin = std::remainder(
        state_.rearWheelSpin + state_.rearWheelAngularVelocity * dt,
        glm::two_pi<float>());
}

void Bike::step(const BikeInput& input, float flatHeight, float waterHeight,
                float dt) {
    const float safeHeight = finiteOr(flatHeight, 0.0f);
    step(input,
         [safeHeight](float, float) noexcept { return safeHeight; },
         waterHeight, dt);
}

void Bike::integrate(float dt) {
    state_.chassisPosition += state_.chassisLinearVelocity * dt;
    state_.chassisPosition = glm::clamp(
        state_.chassisPosition, glm::vec3(-kPositionLimit),
        glm::vec3(kPositionLimit));

    const glm::quat omega(0.0f, state_.chassisAngularVelocity.x,
                          state_.chassisAngularVelocity.y,
                          state_.chassisAngularVelocity.z);
    state_.chassisOrientation +=
        0.5f * dt * omega * state_.chassisOrientation;
    const float lengthSquared = glm::dot(state_.chassisOrientation,
                                         state_.chassisOrientation);
    if (!finite(lengthSquared) || lengthSquared < 1.0e-8f) {
        state_.chassisOrientation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    } else {
        state_.chassisOrientation /= std::sqrt(lengthSquared);
    }
}

}  // namespace voxy::moto
