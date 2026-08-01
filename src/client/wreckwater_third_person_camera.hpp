#pragma once

#include "client/wreckwater_character_presentation.hpp"
#include "physics/physics_types.hpp"

#include <cstdint>

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

namespace voxy::client {

enum class WreckwaterThirdPersonCameraStatus : uint32_t {
    Accepted = 0u,
    NotInitialized,
    InvalidConfiguration,
    InvalidDeltaTime,
    InvalidInput,
    InvalidTarget,
    PositionOverflow,
    ProbeUnavailable,
    ProbeOutstanding,
    ProbeSequenceExhausted,
    StaleProbeResult,
    ProbeGeometryExpired,
    ProbeIdentityMismatch,
    InvalidProbeResult,
    ProbeResultOverflow,
};

[[nodiscard]] const char* wreckwaterThirdPersonCameraStatusName(
    WreckwaterThirdPersonCameraStatus status) noexcept;

enum class WreckwaterCameraShoulder : uint32_t {
    Left = 0u,
    Right = 1u,
};

// Normalized render-frame controls. Orbit and zoom are rates, not mouse
// deltas. The input adapter may convert raw device deltas before calling the
// rig. shoulderSwapDown is a button level; the rig detects its rising edge.
struct WreckwaterThirdPersonCameraInput {
    glm::vec2 orbit{0.0f};
    float zoom = 0.0f;
    bool shoulderSwapDown = false;

    [[nodiscard]] bool operator==(
        const WreckwaterThirdPersonCameraInput&) const = default;
};

struct WreckwaterCameraObstructionProbeIdentity {
    uint64_t sequence = 0u;
    uint64_t playerId = 0u;
    uint32_t characterHandle = 0u;
    uint32_t connectionGeneration = 0u;

    [[nodiscard]] bool operator==(
        const WreckwaterCameraObstructionProbeIdentity&) const =
        default;
};

// Fixed-storage adapter payload. physicsQuery is the rigid-body component for
// PhysicsWorld::submitQueries(); worldStart/worldEnd describe the same sweep
// that the adapter must also run against terrain and every rendered domain.
//
// PhysicsQueryRequest::requestId carries the low 32 bits of sequence. The
// async owner must retain the complete identity beside the submitted request
// and return it with the result. requestId alone is not a lifetime fence.
struct WreckwaterCameraObstructionProbeRequest {
    WreckwaterCameraObstructionProbeIdentity identity{};
    physics::PhysicsQueryRequest physicsQuery{};
    physics::WorldPosition worldStart{};
    physics::WorldPosition worldEnd{};
    float pathDistance = 0.0f;
};

// The async adapter selects the closest valid hit from PhysicsQueryOutput.
// A miss uses hit=false and hitDistance=0. overflow reports that the backend
// could not prove a complete result; all other fields are ignored and the
// current pose is clamped fail-closed.
struct WreckwaterCameraObstructionProbeResult {
    WreckwaterCameraObstructionProbeIdentity identity{};
    float hitDistance = 0.0f;
    bool hit = false;
    // True only after the adapter has combined every obstruction domain used
    // by the rendered scene. A rigid-body-only miss is not a safe camera miss
    // when terrain or another static domain is rendered.
    bool completeScene = false;
    bool overflow = false;

    [[nodiscard]] bool operator==(
        const WreckwaterCameraObstructionProbeResult&) const =
        default;
};

struct WreckwaterThirdPersonCameraPose {
    physics::WorldPosition cameraPosition{};
    physics::WorldPosition lookTargetPosition{};
    // Same coordinate frame as cameraPosition.local. This is ready for
    // Camera::lookAt() after setting Camera's sector/local position.
    glm::vec3 cameraSectorLookTarget{0.0f};
    glm::vec3 forward{0.0f, 0.0f, 1.0f};
    glm::vec3 right{1.0f, 0.0f, 0.0f};
    glm::vec3 up{0.0f, 1.0f, 0.0f};
    float fovYRadians = 0.0f;
    float boomDistance = 0.0f;
    float unobstructedBoomDistance = 0.0f;
    float obstructionScale = 1.0f;
    WreckwaterCameraShoulder shoulder =
        WreckwaterCameraShoulder::Right;
    bool obstructionLimited = false;
    bool valid = false;

    [[nodiscard]] bool operator==(
        const WreckwaterThirdPersonCameraPose&) const = default;
};

struct WreckwaterThirdPersonCameraFrameResult {
    WreckwaterThirdPersonCameraStatus status =
        WreckwaterThirdPersonCameraStatus::NotInitialized;
    float appliedDeltaSeconds = 0.0f;
    bool deltaTimeCapped = false;
    bool snapped = false;

    [[nodiscard]] explicit operator bool() const noexcept {
        return status == WreckwaterThirdPersonCameraStatus::Accepted;
    }
};

struct WreckwaterThirdPersonCameraTelemetry {
    uint64_t framesAccepted = 0u;
    uint64_t framesRejected = 0u;
    uint64_t hitchFrames = 0u;
    uint64_t trackingSnaps = 0u;
    uint64_t shoulderSwaps = 0u;
    uint64_t probesIssued = 0u;
    uint64_t probeHits = 0u;
    uint64_t probeMisses = 0u;
    uint64_t probeOverflows = 0u;
    uint64_t staleProbeResults = 0u;
    uint64_t probeGeometryExpirations = 0u;
    uint64_t mismatchedProbeResults = 0u;
    uint64_t invalidProbeResults = 0u;

    [[nodiscard]] bool operator==(
        const WreckwaterThirdPersonCameraTelemetry&) const =
        default;
};

// Fixed-storage third-person presentation camera.
//
// Collision geometry and PhysicsWorld polling stay outside this class.
// takeObstructionProbeRequest() permits exactly one in-flight request.
class WreckwaterThirdPersonCameraRig {
public:
    struct Config {
        float maximumDeltaSeconds = 1.0f / 20.0f;

        float targetSmoothingFrequencyHz = 5.0f;
        float boomSmoothingFrequencyHz = 8.0f;
        float fovSmoothingFrequencyHz = 4.0f;
        float obstructionReleaseRate = 5.0f;

        float yawSpeedRadiansPerSecond = 3.5f;
        float pitchSpeedRadiansPerSecond = 2.5f;
        float zoomSpeedMetersPerSecond = 5.0f;
        float initialPitchRadians = 0.31f;
        float minimumPitchRadians = -0.17f;
        float maximumPitchRadians = 1.13f;
        float initialDistance = 4.5f;
        float minimumDistance = 1.5f;
        float maximumDistance = 8.0f;
        float shoulderOffset = 0.55f;
        WreckwaterCameraShoulder initialShoulder =
            WreckwaterCameraShoulder::Right;

        float velocityLookAheadSeconds = 0.18f;
        float maximumLookAheadDistance = 2.5f;
        float maximumTargetSpeed = 150.0f;
        float teleportDistance = 12.0f;

        float minimumFovYRadians = 0.79f;
        float baseFovYRadians = 1.05f;
        float maximumFovYRadians = 1.40f;
        float maximumSpeedFovBoostRadians = 0.17f;
        float speedForMaximumFovBoost = 20.0f;

        float obstructionProbeRadius = 0.25f;
        // The submitted sphere cast is inflated by this margin. A completed
        // result may certify the current boom only while both current
        // endpoints remain within this distance of the submitted endpoints.
        float maximumProbeEndpointDrift = 0.35f;
        float obstructionPadding = 0.15f;
        float minimumObstructedDistance = 0.35f;
        uint32_t obstructionQueryFlags = 0u;

        // Recovery/checkpoint seed. The first request uses seed + 1.
        // UINT64_MAX is accepted so exhaustion can fail closed explicitly.
        uint64_t initialProbeSequence = 0u;
    };

    [[nodiscard]] bool initialize(const Config& config) noexcept;
    void reset() noexcept;

    [[nodiscard]] WreckwaterThirdPersonCameraFrameResult update(
        const WreckwaterThirdPersonCameraTarget& target,
        const WreckwaterThirdPersonCameraInput& input,
        float deltaSeconds) noexcept;

    // Generates and marks one request in flight. If submission fails, call
    // cancelObstructionProbe() with the returned identity.
    [[nodiscard]] WreckwaterThirdPersonCameraStatus
    takeObstructionProbeRequest(
        WreckwaterCameraObstructionProbeRequest& output) noexcept;

    [[nodiscard]] WreckwaterThirdPersonCameraStatus
    resolveObstructionProbe(
        const WreckwaterCameraObstructionProbeResult& result)
        noexcept;

    [[nodiscard]] WreckwaterThirdPersonCameraStatus
    cancelObstructionProbe(
        const WreckwaterCameraObstructionProbeIdentity& identity)
        noexcept;

    [[nodiscard]] bool initialized() const noexcept {
        return initialized_;
    }
    [[nodiscard]] bool hasTarget() const noexcept {
        return hasTarget_;
    }
    [[nodiscard]] bool probeOutstanding() const noexcept {
        return probeOutstanding_;
    }
    [[nodiscard]] const WreckwaterThirdPersonCameraPose&
    pose() const noexcept {
        return pose_;
    }
    [[nodiscard]] const WreckwaterCameraObstructionProbeRequest&
    outstandingProbe() const noexcept {
        return outstandingProbe_;
    }
    [[nodiscard]] uint64_t lastProbeSequence() const noexcept {
        return lastProbeSequence_;
    }
    [[nodiscard]] const WreckwaterThirdPersonCameraTelemetry&
    telemetry() const noexcept {
        return telemetry_;
    }

private:
    [[nodiscard]] WreckwaterThirdPersonCameraFrameResult updateImpl(
        const WreckwaterThirdPersonCameraTarget& target,
        const WreckwaterThirdPersonCameraInput& input,
        float deltaSeconds) noexcept;
    [[nodiscard]] WreckwaterThirdPersonCameraStatus rebuildPose()
        noexcept;
    [[nodiscard]] bool probeGeometryCovered(
        const physics::WorldPosition& requestStart,
        const physics::WorldPosition& requestEnd) const noexcept;
    void invalidateObstructionCertificate() noexcept;
    void clampObstructionFailClosed() noexcept;
    void invalidateProbe() noexcept;

    Config config_{};
    WreckwaterThirdPersonCameraPose pose_{};
    physics::WorldPosition smoothedTarget_{};
    physics::WorldPosition lastRawTarget_{};
    physics::WorldPosition unobstructedCamera_{};
    glm::vec3 smoothedTargetVelocity_{0.0f};

    float desiredYaw_ = 0.0f;
    float smoothedYaw_ = 0.0f;
    float yawVelocity_ = 0.0f;
    float desiredPitch_ = 0.0f;
    float smoothedPitch_ = 0.0f;
    float pitchVelocity_ = 0.0f;
    float desiredDistance_ = 0.0f;
    float smoothedDistance_ = 0.0f;
    float distanceVelocity_ = 0.0f;
    float desiredShoulderOffset_ = 0.0f;
    float smoothedShoulderOffset_ = 0.0f;
    float shoulderVelocity_ = 0.0f;
    float desiredFovY_ = 0.0f;
    float smoothedFovY_ = 0.0f;
    float fovVelocity_ = 0.0f;
    float obstructionTargetScale_ = 1.0f;
    float obstructionScale_ = 1.0f;

    uint64_t targetPlayerId_ = 0u;
    uint32_t targetCharacterHandle_ = 0u;
    uint32_t targetConnectionGeneration_ = 0u;
    WreckwaterCameraShoulder shoulder_ =
        WreckwaterCameraShoulder::Right;
    bool previousShoulderSwapDown_ = false;
    bool hasTarget_ = false;
    bool initialized_ = false;

    uint64_t lastProbeSequence_ = 0u;
    WreckwaterCameraObstructionProbeRequest outstandingProbe_{};
    physics::WorldPosition certifiedProbeStart_{};
    physics::WorldPosition certifiedProbeEnd_{};
    bool probeOutstanding_ = false;
    bool outstandingProbeGeometryExpired_ = false;
    bool obstructionCertificateValid_ = false;
    WreckwaterThirdPersonCameraTelemetry telemetry_{};
};

} // namespace voxy::client
