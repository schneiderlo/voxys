// ═══════════════════════════════════════════════════════════════════════════════
// session.cpp - RIDGEBREAK local freeride session
// ═══════════════════════════════════════════════════════════════════════════════

#include "moto/session.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <limits>
#include <string_view>
#include <unordered_set>
#include <utility>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

namespace voxy::moto {
namespace {

constexpr uint64_t kMaximumVmeshBytes = 64u * 1024u * 1024u;
constexpr float kFixedStep = 1.0f / 60.0f;
constexpr int kMaximumCatchUpSteps = 8;
constexpr float kRemountInputGrace = 1.25f;
constexpr std::string_view kDustPartPrefix = "fx_dust_";

// The authored chassis reference is near the engine/frame center at y=0.57 m.
// Moving it to the origin lets BikeState::chassisPosition be the root pose.
constexpr float kModelChassisReferenceHeight = 0.57f;
constexpr glm::vec3 kFrontAxle(0.0f, 0.31f, 0.71f);
constexpr glm::vec3 kRearAxle(0.0f, 0.31f, -0.71f);
constexpr glm::vec3 kSteeringHead(0.0f, 0.92f, 0.42f);
constexpr glm::vec3 kRiderHips(0.0f, 0.94f, -0.20f);

bool finite(float value) noexcept {
    return std::isfinite(value);
}

bool finite(const glm::vec3& value) noexcept {
    return finite(value.x) && finite(value.y) && finite(value.z);
}

bool identityNode(const VmeshNode& node) noexcept {
    const auto near = [](float a, float b) {
        constexpr float epsilon = 1.0e-6f;
        return std::abs(a - b) <= epsilon;
    };
    return node.parent == -1 && node.skinIndex == -1 &&
           near(node.translation[0], 0.0f) &&
           near(node.translation[1], 0.0f) &&
           near(node.translation[2], 0.0f) &&
           near(node.rotation[0], 0.0f) &&
           near(node.rotation[1], 0.0f) &&
           near(node.rotation[2], 0.0f) &&
           near(node.rotation[3], 1.0f) && near(node.scale[0], 1.0f) &&
           near(node.scale[1], 1.0f) && near(node.scale[2], 1.0f);
}

glm::mat4 pivotRotation(const glm::vec3& pivot, float angle,
                        const glm::vec3& axis) {
    glm::mat4 transform(1.0f);
    transform = glm::translate(transform, pivot);
    transform = glm::rotate(transform, angle, axis);
    return glm::translate(transform, -pivot);
}

glm::mat4 bikeRoot(const BikeState& state) {
    return glm::translate(glm::mat4(1.0f), state.chassisPosition) *
           glm::mat4_cast(state.chassisOrientation) *
           glm::translate(glm::mat4(1.0f),
                          glm::vec3(0.0f, -kModelChassisReferenceHeight, 0.0f));
}

bool isFrontWheelPart(std::string_view name) noexcept {
    return name == "wheel_f" || name == "rim_f" || name == "hub_f" ||
           name == "disc_f";
}

bool isRearWheelPart(std::string_view name) noexcept {
    return name == "wheel_r" || name == "rim_r" || name == "hub_r" ||
           name == "disc_r";
}

bool isSteeringPart(std::string_view name) noexcept {
    return isFrontWheelPart(name) || name == "fork_l" || name == "fork_r" ||
           name == "bar" || name == "grip_l" || name == "grip_r" ||
           name == "numberplate_f" || name == "fender_f" ||
           name == "triple_clamp";
}

std::vector<MotoPartPose> makePartPoses(const auto& bikeParts,
                                        const auto& riderParts,
                                        const MotoSessionConfig& config,
                                        const BikeState& state,
                                        const BikeInput& rawInput) {
    std::vector<MotoPartPose> poses;
    poses.reserve(bikeParts.size() + riderParts.size());

    const glm::mat4 root = bikeRoot(state);
    const glm::mat4 steering = pivotRotation(
        kSteeringHead, state.steeringAngle, glm::vec3(0.0f, 1.0f, 0.0f));
    const glm::mat4 frontLift = glm::translate(
        glm::mat4(1.0f), glm::vec3(0.0f, state.frontSuspension, 0.0f));
    const glm::mat4 rearLift = glm::translate(
        glm::mat4(1.0f), glm::vec3(0.0f, state.rearSuspension, 0.0f));
    const glm::mat4 frontSpin = pivotRotation(
        kFrontAxle, -state.frontWheelSpin, glm::vec3(1.0f, 0.0f, 0.0f));
    const glm::mat4 rearSpin = pivotRotation(
        kRearAxle, -state.rearWheelSpin, glm::vec3(1.0f, 0.0f, 0.0f));

    for (const auto& part : bikeParts) {
        glm::mat4 local(1.0f);
        if (isFrontWheelPart(part.name)) {
            local = steering * frontLift * frontSpin;
        } else if (isRearWheelPart(part.name)) {
            local = rearLift * rearSpin;
        } else if (isSteeringPart(part.name)) {
            local = steering;
        }
        poses.push_back(MotoPartPose{config.bikeAssetIndex, part.meshIndex,
                                     root * local});
    }

    const float steer = finite(rawInput.steer)
        ? std::clamp(rawInput.steer, -1.0f, 1.0f) : 0.0f;
    const float lean = finite(rawInput.lean)
        ? std::clamp(rawInput.lean, -1.0f, 1.0f) : 0.0f;
    const float throttle = finite(rawInput.throttle)
        ? std::clamp(rawInput.throttle, 0.0f, 1.0f) : 0.0f;
    const float riderRoll = std::clamp(lean * 0.07f + steer * 0.035f,
                                       -0.10f, 0.10f);
    const float riderPitch = std::clamp(
        throttle * 0.035f + (rawInput.seated ? 0.0f : 0.065f) +
            (rawInput.duck ? 0.025f : 0.0f),
        0.0f, 0.12f);
    glm::mat4 riderPivot =
        pivotRotation(kRiderHips, riderRoll, glm::vec3(0.0f, 0.0f, 1.0f)) *
        pivotRotation(kRiderHips, riderPitch, glm::vec3(1.0f, 0.0f, 0.0f));

    // The authored rider is made from rigid pieces, so use one coherent stunt
    // body transform. This clearly communicates ejection, downed, and remount
    // states without pretending the asset has a skeleton that it does not.
    if (state.crash == CrashState::HighSided) {
        const float t = std::clamp(state.crashTime, 0.0f, 1.0f);
        riderPivot = glm::translate(glm::mat4(1.0f),
            glm::vec3(0.0f, 1.25f * t, 0.80f * t)) *
            pivotRotation(kRiderHips, -1.8f * t,
                          glm::vec3(1.0f, 0.0f, 0.0f));
    } else if (state.crash == CrashState::WipedOut) {
        const float t = std::clamp(state.crashTime * 1.5f, 0.0f, 1.0f);
        riderPivot = glm::translate(glm::mat4(1.0f),
            glm::vec3(0.75f * t, 0.35f * t, -0.25f * t)) *
            pivotRotation(kRiderHips, 1.45f * t,
                          glm::vec3(0.0f, 0.0f, 1.0f));
    } else if (state.crash == CrashState::OnGround ||
               state.crash == CrashState::Remounting) {
        const float recovered = state.crash == CrashState::Remounting
            ? std::clamp(state.remountTime / 0.85f, 0.0f, 1.0f) : 0.0f;
        const float fallen = 1.0f - recovered;
        riderPivot = glm::translate(glm::mat4(1.0f),
            glm::vec3(0.70f * fallen, -0.30f * fallen, 0.0f)) *
            pivotRotation(kRiderHips, 1.45f * fallen,
                          glm::vec3(0.0f, 0.0f, 1.0f));
    }

    for (const auto& part : riderParts) {
        poses.push_back(MotoPartPose{config.riderAssetIndex, part.meshIndex,
                                     root * riderPivot});
    }
    return poses;
}

MotoCameraPose desiredCamera(const BikeState& state) {
    glm::vec3 forward = state.chassisOrientation * glm::vec3(0.0f, 0.0f, 1.0f);
    forward.y = 0.0f;
    const float forwardLength2 = glm::dot(forward, forward);
    if (!finite(forward) || !finite(forwardLength2) ||
        forwardLength2 < 1.0e-8f) {
        forward = glm::vec3(0.0f, 0.0f, 1.0f);
    } else {
        forward /= std::sqrt(forwardLength2);
    }

    glm::vec3 lookAhead = finite(state.chassisLinearVelocity)
        ? state.chassisLinearVelocity * 0.15f : glm::vec3(0.0f);
    const float lookAheadLength2 = glm::dot(lookAhead, lookAhead);
    constexpr float maximumLookAhead = 1.25f;
    if (!finite(lookAheadLength2)) {
        lookAhead = glm::vec3(0.0f);
    } else if (lookAheadLength2 > maximumLookAhead * maximumLookAhead) {
        lookAhead *= maximumLookAhead / std::sqrt(lookAheadLength2);
    }

    // Keep the hero lane centered while retaining a small shoulder offset for
    // bike silhouette. Speed extends the look-ahead and chase arm gradually;
    // this adds landing visibility without an abrupt FOV or camera-mode cut.
    const glm::vec3 right(forward.z, 0.0f, -forward.x);
    const float speedPresentation = std::clamp(
        std::abs(finite(state.speed) ? state.speed : 0.0f) / 28.0f,
        0.0f, 1.0f);
    MotoCameraPose camera;
    camera.target = state.chassisPosition + lookAhead
        + forward * (2.00f + 1.18f * speedPresentation)
        + glm::vec3(0.0f, 0.79f, 0.0f);
    camera.position = state.chassisPosition + lookAhead
        - forward * (4.72f + 0.58f * speedPresentation)
        + right * 0.58f
        + glm::vec3(0.0f, 1.92f + 0.22f * speedPresentation, 0.0f);
    return camera;
}

float interpolateAngle(float previous, float current, float alpha) noexcept {
    const float delta = std::remainder(current - previous,
                                       glm::two_pi<float>());
    return previous + delta * alpha;
}

BikeState interpolatePresentationState(const BikeState& previous,
                                       const BikeState& current,
                                       float alpha) {
    alpha = std::clamp(finite(alpha) ? alpha : 0.0f, 0.0f, 1.0f);
    BikeState result = current;
    result.chassisPosition = glm::mix(previous.chassisPosition,
                                     current.chassisPosition, alpha);
    result.chassisOrientation = glm::normalize(glm::slerp(
        previous.chassisOrientation, current.chassisOrientation, alpha));
    result.chassisLinearVelocity = glm::mix(
        previous.chassisLinearVelocity, current.chassisLinearVelocity, alpha);
    result.chassisAngularVelocity = glm::mix(
        previous.chassisAngularVelocity, current.chassisAngularVelocity,
        alpha);
    result.frontSuspension = glm::mix(previous.frontSuspension,
                                      current.frontSuspension, alpha);
    result.rearSuspension = glm::mix(previous.rearSuspension,
                                     current.rearSuspension, alpha);
    result.steeringAngle = glm::mix(previous.steeringAngle,
                                    current.steeringAngle, alpha);
    result.frontWheelSpin = interpolateAngle(previous.frontWheelSpin,
                                             current.frontWheelSpin, alpha);
    result.rearWheelSpin = interpolateAngle(previous.rearWheelSpin,
                                            current.rearWheelSpin, alpha);
    if (previous.crash == current.crash) {
        result.crashTime = glm::mix(previous.crashTime,
                                    current.crashTime, alpha);
        result.remountTime = glm::mix(previous.remountTime,
                                      current.remountTime, alpha);
    }
    return result;
}

float signedNoise(uint32_t value) noexcept {
    value ^= value >> 16u;
    value *= 0x7feb352du;
    value ^= value >> 15u;
    value *= 0x846ca68bu;
    value ^= value >> 16u;
    return static_cast<float>(value & 0xFFFFu) / 32767.5f - 1.0f;
}

glm::mat4 groundBasis(const glm::vec3& position, const glm::vec3& normal,
                      const glm::vec3& heading) {
    glm::vec3 forward = heading - normal * glm::dot(heading, normal);
    if (!finite(forward) || glm::dot(forward, forward) < 1.0e-8f) {
        forward = glm::vec3(0.0f, 0.0f, 1.0f);
    } else {
        forward = glm::normalize(forward);
    }
    glm::vec3 right = glm::normalize(glm::cross(normal, forward));
    forward = glm::normalize(glm::cross(right, normal));
    glm::mat4 result(1.0f);
    result[0] = glm::vec4(right, 0.0f);
    result[1] = glm::vec4(normal, 0.0f);
    result[2] = glm::vec4(forward, 0.0f);
    result[3] = glm::vec4(position, 1.0f);
    return result;
}

}  // namespace

bool MotoSession::loadModelParts(const std::filesystem::path& path,
                                 std::vector<ModelPart>* parts,
                                 std::string* error) {
    if (parts == nullptr) {
        if (error != nullptr) *error = "null model part destination";
        return false;
    }

    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input) {
        if (error != nullptr) *error = "could not open VMESH file: " + path.string();
        return false;
    }
    const std::streampos end = input.tellg();
    if (end < 0) {
        if (error != nullptr) *error = "could not determine VMESH file size";
        return false;
    }
    const uint64_t byteCount = static_cast<uint64_t>(end);
    if (byteCount > kMaximumVmeshBytes) {
        if (error != nullptr) *error = "VMESH file exceeds 64 MiB limit";
        return false;
    }
    if (byteCount == 0u) {
        if (error != nullptr) *error = "VMESH file is empty";
        return false;
    }

    std::vector<uint8_t> bytes(static_cast<size_t>(byteCount));
    input.seekg(0, std::ios::beg);
    input.read(reinterpret_cast<char*>(bytes.data()),
               static_cast<std::streamsize>(bytes.size()));
    if (!input || input.peek() != std::char_traits<char>::eof()) {
        if (error != nullptr) *error = "could not read complete VMESH file";
        return false;
    }

    VmeshData model;
    std::string parseError;
    if (!readVmesh(bytes.data(), bytes.size(), &model, &parseError)) {
        if (error != nullptr) *error = "invalid VMESH file: " + parseError;
        return false;
    }
    if (model.nodes.empty() || model.header.meshCount == 0u ||
        model.nodes.size() != model.header.meshCount) {
        if (error != nullptr) *error = "VMESH has missing model nodes";
        return false;
    }

    std::vector<ModelPart> result;
    result.reserve(model.nodes.size());
    std::unordered_set<std::string> names;
    std::unordered_set<uint32_t> meshIndices;
    for (const VmeshNode& node : model.nodes) {
        if (!identityNode(node)) {
            if (error != nullptr) *error = "VMESH node is not an identity root";
            return false;
        }
        if (node.meshIndex == std::numeric_limits<uint32_t>::max() ||
            node.meshIndex >= model.header.meshCount) {
            if (error != nullptr) *error = "VMESH node has an invalid mesh index";
            return false;
        }
        if (node.nameOffset == 0u || node.nameOffset >= model.stringBlob.size()) {
            if (error != nullptr) *error = "VMESH node is missing a name";
            return false;
        }
        const size_t nameEnd = model.stringBlob.find('\0', node.nameOffset);
        if (nameEnd == std::string::npos || nameEnd == node.nameOffset) {
            if (error != nullptr) *error = "VMESH node has an invalid name";
            return false;
        }
        std::string name(model.stringBlob.data() + node.nameOffset,
                         nameEnd - node.nameOffset);
        if (!names.insert(name).second ||
            !meshIndices.insert(node.meshIndex).second) {
            if (error != nullptr) *error = "VMESH has duplicate model nodes";
            return false;
        }
        result.push_back(ModelPart{std::move(name), node.meshIndex});
    }
    if (meshIndices.size() != model.header.meshCount) {
        if (error != nullptr) *error = "VMESH has a mesh without a model node";
        return false;
    }

    *parts = std::move(result);
    return true;
}

bool MotoSession::initialize(const MotoSessionConfig& config,
                             std::string* error) {
    std::vector<ModelPart> bikeParts;
    std::vector<ModelPart> riderParts;
    if (!loadModelParts(config.bikeAsset, &bikeParts, error) ||
        !loadModelParts(config.riderAsset, &riderParts, error)) {
        return false;
    }

    MotoSessionConfig stagedConfig = config;
    Bike stagedBike;
    stagedBike.reset(stagedConfig.spawnPosition, stagedConfig.spawnYaw);
    const BikeInput neutralInput{};
    std::vector<MotoPartPose> stagedPoses = makePartPoses(
        bikeParts, riderParts, stagedConfig, stagedBike.state(), neutralInput);
    const MotoCameraPose stagedCamera = desiredCamera(stagedBike.state());

    config_ = std::move(stagedConfig);
    bikeParts_ = std::move(bikeParts);
    riderParts_ = std::move(riderParts);
    partPoses_ = std::move(stagedPoses);
    bike_.reset(config_.spawnPosition, config_.spawnYaw);
    previousBikeState_ = bike_.state();
    presentationBikeState_ = bike_.state();
    cameraPose_ = stagedCamera;
    accumulator_ = 0.0f;
    pendingShifts_.fill(0);
    pendingShiftHead_ = 0u;
    pendingShiftCount_ = 0u;
    droppedShiftInputs_ = 0u;
    pendingRemount_ = false;
    pendingRemountAge_ = 0.0f;
    fixedStepStateCount_ = 0u;
    dustParticles_.fill(DustParticle{});
    groundPoint_ = config_.spawnPosition - glm::vec3(0.0f, 0.55f, 0.0f);
    groundNormal_ = glm::vec3(0.0f, 1.0f, 0.0f);
    dustEmissionAccumulator_ = 0.0f;
    dustSequence_ = 0u;
    feedbackWasAirborne_ = true;
    initialized_ = true;
    rebuildPartPoses(neutralInput, presentationBikeState_);
    if (error != nullptr) error->clear();
    return true;
}

void MotoSession::shutdown() {
    bikeParts_.clear();
    riderParts_.clear();
    partPoses_.clear();
    cameraPose_ = MotoCameraPose{};
    accumulator_ = 0.0f;
    pendingShifts_.fill(0);
    pendingShiftHead_ = 0u;
    pendingShiftCount_ = 0u;
    droppedShiftInputs_ = 0u;
    pendingRemount_ = false;
    pendingRemountAge_ = 0.0f;
    fixedStepStateCount_ = 0u;
    dustParticles_.fill(DustParticle{});
    groundPoint_ = glm::vec3(0.0f);
    groundNormal_ = glm::vec3(0.0f, 1.0f, 0.0f);
    dustEmissionAccumulator_ = 0.0f;
    dustSequence_ = 0u;
    feedbackWasAirborne_ = true;
    previousBikeState_ = BikeState{};
    presentationBikeState_ = BikeState{};
    initialized_ = false;
}

void MotoSession::update(
    const BikeInput& input,
    const std::function<float(float x, float z)>& heightAt,
    float deltaTime) {
    update(input, heightAt, deltaTime, {}, {});
}

void MotoSession::update(
    const BikeInput& input,
    const std::function<float(float x, float z)>& heightAt,
    float deltaTime,
    const std::function<MotoFixedStepMode()>& fixedStepMode,
    const std::function<void(const BikeState&)>& onFixedStep) {
    if (!initialized_) return;
    fixedStepStateCount_ = 0u;

    const float frameDelta = finite(deltaTime) && deltaTime > 0.0f
        ? std::min(deltaTime, kFixedStep * kMaximumCatchUpSteps) : 0.0f;
    accumulator_ = std::min(accumulator_ + frameDelta,
                            kFixedStep * kMaximumCatchUpSteps);

    // Render/input sampling can run faster than the fixed simulation. Preserve
    // one-shot controls until a fixed tick actually observes them.
    auto enqueueShift = [&](int8_t direction) {
        if (pendingShiftCount_ >= pendingShifts_.size()) {
            droppedShiftInputs_ = std::min(droppedShiftInputs_ + 1u,
                                           1'000'000u);
            return;
        }
        const size_t tail = (static_cast<size_t>(pendingShiftHead_) +
                             pendingShiftCount_) % pendingShifts_.size();
        pendingShifts_[tail] = direction;
        ++pendingShiftCount_;
    };
    if (input.shiftUp) enqueueShift(1);
    if (input.shiftDown) enqueueShift(-1);
    if (input.remount) {
        pendingRemount_ = true;
        pendingRemountAge_ = 0.0f;
    }

    BikeInput stepInput = input;
    auto applyPendingShift = [&]() {
        const int8_t direction = pendingShiftCount_ > 0u
            ? pendingShifts_[pendingShiftHead_] : 0;
        stepInput.shiftUp = direction > 0;
        stepInput.shiftDown = direction < 0;
    };
    applyPendingShift();
    stepInput.remount = pendingRemount_;
    int steps = 0;
    while (accumulator_ >= kFixedStep && steps < kMaximumCatchUpSteps) {
        const bool gridOwned = fixedStepMode
            && fixedStepMode() == MotoFixedStepMode::HoldGrid;
        previousBikeState_ = bike_.state();
        if (gridOwned) {
            // A held tick is still a canonical 60 Hz tick. Restore the exact
            // configured grid state without touching the wall-clock
            // accumulator, then let race authority advance before selecting
            // the mode for a possible next catch-up tick.
            bike_.reset(config_.spawnPosition, config_.spawnYaw);
            pendingShifts_.fill(0);
            pendingShiftHead_ = 0u;
            pendingShiftCount_ = 0u;
            pendingRemount_ = false;
            pendingRemountAge_ = 0.0f;
        } else {
            bike_.step(stepInput, heightAt, config_.waterHeight, kFixedStep);
        }
        fixedStepStates_[fixedStepStateCount_++] = bike_.state();
        if (onFixedStep) onFixedStep(bike_.state());
        if (!gridOwned && (stepInput.shiftUp || stepInput.shiftDown) &&
            pendingShiftCount_ > 0u) {
            pendingShifts_[pendingShiftHead_] = 0;
            pendingShiftHead_ = static_cast<uint8_t>(
                (pendingShiftHead_ + 1u) % pendingShifts_.size());
            --pendingShiftCount_;
        }
        if (!gridOwned && stepInput.remount) {
            pendingRemountAge_ += kFixedStep;
            const CrashState crash = bike_.state().crash;
            if (crash == CrashState::Remounting ||
                crash == CrashState::Riding ||
                pendingRemountAge_ >= kRemountInputGrace) {
                pendingRemount_ = false;
                pendingRemountAge_ = 0.0f;
            }
        }
        applyPendingShift();
        stepInput.remount = pendingRemount_;
        accumulator_ -= kFixedStep;
        ++steps;
    }
    if (accumulator_ < 0.0f) accumulator_ = 0.0f;

    const float interpolationAlpha = std::clamp(
        accumulator_ / kFixedStep, 0.0f, 1.0f);
    presentationBikeState_ = interpolatePresentationState(
        previousBikeState_, bike_.state(), interpolationAlpha);
    updateVisualFeedback(input, heightAt, frameDelta,
                         presentationBikeState_);
    rebuildPartPoses(input, presentationBikeState_);
    updateCamera(frameDelta, presentationBikeState_);
}

void MotoSession::reset() {
    if (!initialized_) return;
    bike_.reset(config_.spawnPosition, config_.spawnYaw);
    previousBikeState_ = bike_.state();
    presentationBikeState_ = bike_.state();
    accumulator_ = 0.0f;
    pendingShifts_.fill(0);
    pendingShiftHead_ = 0u;
    pendingShiftCount_ = 0u;
    droppedShiftInputs_ = 0u;
    pendingRemount_ = false;
    pendingRemountAge_ = 0.0f;
    fixedStepStateCount_ = 0u;
    dustParticles_.fill(DustParticle{});
    dustEmissionAccumulator_ = 0.0f;
    dustSequence_ = 0u;
    feedbackWasAirborne_ = true;
    rebuildPartPoses(BikeInput{}, presentationBikeState_);
    cameraPose_ = desiredCamera(bike_.state());
}

void MotoSession::resetAt(glm::vec3 position, float yaw) {
    if (!initialized_ || !finite(position) || !finite(yaw)) return;
    config_.spawnPosition = position;
    config_.spawnYaw = yaw;
    reset();
}

void MotoSession::rebuildPartPoses(const BikeInput& input,
                                   const BikeState& poseState) {
    partPoses_ = makePartPoses(bikeParts_, riderParts_, config_, poseState,
                               input);
    glm::vec3 heading = poseState.chassisOrientation
        * glm::vec3(0.0f, 0.0f, 1.0f);
    const float height = std::max(
        poseState.chassisPosition.y - groundPoint_.y - 0.55f, 0.0f);
    const float shadowFade = std::clamp(1.0f - height / 4.0f, 0.0f, 1.0f);
    const float shadowScale = 1.0f + std::min(height * 0.12f, 0.55f);
    const glm::mat4 shadowTransform = groundBasis(
        groundPoint_ + groundNormal_ * 0.025f, groundNormal_, heading)
        * glm::scale(glm::mat4(1.0f),
                     glm::vec3(shadowScale, 1.0f, shadowScale));

    const auto groundAtOffset = [&](float forwardOffset) {
        glm::vec3 flatHeading(heading.x, 0.0f, heading.z);
        if (glm::dot(flatHeading, flatHeading) < 1.0e-8f) {
            flatHeading = glm::vec3(0.0f, 0.0f, 1.0f);
        } else {
            flatHeading = glm::normalize(flatHeading);
        }
        glm::vec3 point = groundPoint_ + flatHeading * forwardOffset;
        point.y = groundPoint_.y -
            (groundNormal_.x * (point.x - groundPoint_.x)
             + groundNormal_.z * (point.z - groundPoint_.z))
                / std::max(groundNormal_.y, 0.1f);
        return point + groundNormal_ * 0.032f;
    };

    for (size_t index = 0u; index < bikeParts_.size(); ++index) {
        MotoPartPose& pose = partPoses_[index];
        const std::string_view name = bikeParts_[index].name;
        if (name == "shadow_core" || name == "shadow_mid"
            || name == "shadow_soft") {
            pose.modelMatrix = shadowTransform;
            pose.tintColor.a = shadowFade;
        } else if (name == "contact_patch_f" || name == "contact_patch_r") {
            const bool front = name == "contact_patch_f";
            const bool touching = front ? poseState.frontTouch
                                        : poseState.rearTouch;
            pose.modelMatrix = groundBasis(
                groundAtOffset(front ? 0.71f : -0.71f), groundNormal_, heading);
            pose.tintColor.a = touching ? 1.0f : 0.0f;
        } else if (name.size() >= kDustPartPrefix.size()
                   && name.substr(0u, kDustPartPrefix.size())
                       == kDustPartPrefix) {
            uint32_t particleIndex = 0u;
            bool validIndex = name.size() > kDustPartPrefix.size();
            for (size_t digit = kDustPartPrefix.size();
                 validIndex && digit < name.size(); ++digit) {
                const char value = name[digit];
                validIndex = value >= '0' && value <= '9';
                if (validIndex) {
                    particleIndex = particleIndex * 10u
                        + static_cast<uint32_t>(value - '0');
                }
            }
            if (!validIndex || particleIndex >= dustParticles_.size()) continue;
            const DustParticle& particle = dustParticles_[particleIndex];
            if (particle.lifetime <= 0.0f
                || particle.age >= particle.lifetime) {
                pose.modelMatrix = glm::translate(
                    glm::mat4(1.0f), glm::vec3(0.0f, -10000.0f, 0.0f));
                pose.tintColor.a = 0.0f;
                continue;
            }
            const float life = particle.lifetime > 0.0f
                ? particle.age / particle.lifetime : 1.0f;
            const float fade = std::clamp(1.0f - life, 0.0f, 1.0f);
            const float size = particle.size * (0.72f + life * 0.45f);
            pose.modelMatrix = glm::translate(
                glm::mat4(1.0f), particle.position)
                * glm::scale(glm::mat4(1.0f), glm::vec3(size));
            pose.tintColor = glm::vec4(0.82f, 0.68f, 0.48f,
                                       0.86f * fade * fade);
        }
    }
}

void MotoSession::updateVisualFeedback(
    const BikeInput& input,
    const std::function<float(float x, float z)>& heightAt,
    float deltaTime, const BikeState& poseState) {
    if (!heightAt || !finite(deltaTime) || deltaTime <= 0.0f) return;
    const float x = poseState.chassisPosition.x;
    const float z = poseState.chassisPosition.z;
    const float center = heightAt(x, z);
    const float left = heightAt(x - 0.35f, z);
    const float right = heightAt(x + 0.35f, z);
    const float back = heightAt(x, z - 0.35f);
    const float front = heightAt(x, z + 0.35f);
    if (finite(center) && finite(left) && finite(right)
        && finite(back) && finite(front)) {
        groundPoint_ = glm::vec3(x, center, z);
        groundNormal_ = glm::normalize(
            glm::vec3(left - right, 0.70f, back - front));
    }

    for (DustParticle& particle : dustParticles_) {
        if (particle.age >= particle.lifetime) continue;
        particle.age += deltaTime;
        if (particle.age >= particle.lifetime) continue;
        particle.position += particle.velocity * deltaTime;
        particle.velocity *= std::exp(-3.4f * deltaTime);
        particle.velocity.y -= 0.55f * deltaTime;
        const float ground = heightAt(particle.position.x,
                                      particle.position.z);
        if (finite(ground) && particle.position.y < ground + 0.045f) {
            particle.position.y = ground + 0.045f;
            particle.velocity.y = std::max(particle.velocity.y, 0.0f) * 0.1f;
        }
    }
    glm::vec3 heading = poseState.chassisOrientation
        * glm::vec3(0.0f, 0.0f, 1.0f);
    heading.y = 0.0f;
    if (!finite(heading) || glm::dot(heading, heading) < 1.0e-8f) {
        heading = glm::vec3(0.0f, 0.0f, 1.0f);
    } else {
        heading = glm::normalize(heading);
    }
    const glm::vec3 side(heading.z, 0.0f, -heading.x);
    const glm::vec3 rear = groundPoint_ - heading * 0.72f
        + groundNormal_ * 0.045f;

    auto spawn = [&](float energy) {
        DustParticle& particle =
            dustParticles_[dustSequence_ % dustParticles_.size()];
        const float lateral = signedNoise(dustSequence_ * 3u + 1u);
        const float lift = 0.65f + 0.35f
            * signedNoise(dustSequence_ * 3u + 2u);
        particle.position = rear + side * lateral * 0.16f
            + groundNormal_ * 0.055f;
        particle.velocity = -heading * (0.18f + energy * 0.32f)
            + side * lateral * 0.42f
            + glm::vec3(0.0f, 0.28f + lift * energy * 0.42f, 0.0f)
            + poseState.chassisLinearVelocity * 0.20f;
        particle.age = 0.0f;
        particle.lifetime = 0.36f + 0.18f
            * (signedNoise(dustSequence_ * 3u + 3u) * 0.5f + 0.5f);
        particle.size = (0.12f + energy * 0.10f)
            * (0.80f + 0.20f
               * (signedNoise(dustSequence_ * 5u + 4u) * 0.5f + 0.5f));
        ++dustSequence_;
    };

    const float speed = std::abs(poseState.speed);
    const float throttle = finite(input.throttle)
        ? std::clamp(input.throttle, 0.0f, 1.0f) : 0.0f;
    if (poseState.rearTouch && speed > 2.0f) {
        const float emissionRate = std::clamp(
            (speed - 1.5f) * 1.0f + throttle * 11.0f, 0.0f, 28.0f);
        dustEmissionAccumulator_ += emissionRate * deltaTime;
        uint32_t spawned = 0u;
        while (dustEmissionAccumulator_ >= 1.0f && spawned < 4u) {
            spawn(std::clamp(speed / 20.0f + throttle * 0.35f, 0.2f, 1.25f));
            dustEmissionAccumulator_ -= 1.0f;
            ++spawned;
        }
    } else {
        dustEmissionAccumulator_ = std::min(dustEmissionAccumulator_, 1.0f);
    }
    if (feedbackWasAirborne_ && !poseState.airborne
        && poseState.lastAirTime > 0.12f) {
        const uint32_t burst = std::min<uint32_t>(
            8u, 3u + static_cast<uint32_t>(poseState.lastAirTime * 2.0f));
        for (uint32_t index = 0u; index < burst; ++index) {
            spawn(std::clamp(0.45f + poseState.lastAirTime * 0.18f,
                             0.45f, 1.3f));
        }
    }
    feedbackWasAirborne_ = poseState.airborne;
}

void MotoSession::updateCamera(float deltaTime, const BikeState& poseState) {
    const MotoCameraPose desired = desiredCamera(poseState);
    if (!finite(cameraPose_.position) || !finite(cameraPose_.target)) {
        cameraPose_ = desired;
        return;
    }
    const float alpha = finite(deltaTime) && deltaTime > 0.0f
        ? 1.0f - std::exp(-8.0f * deltaTime) : 0.0f;
    cameraPose_.position += (desired.position - cameraPose_.position) * alpha;
    cameraPose_.target += (desired.target - cameraPose_.target) * alpha;
}

}  // namespace voxy::moto
