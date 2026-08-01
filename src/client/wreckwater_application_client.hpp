#pragma once

#include "client/wreckwater_graphical_client_loop.hpp"
#include "client/wreckwater_third_person_camera.hpp"
#include "network/session_transport.hpp"
#include "network/wreckwater_client_runtime.hpp"

#include <cstdint>
#include <memory>
#include <span>

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

namespace voxy::client {

struct WreckwaterApplicationDigitalControls {
    bool forward = false;
    bool backward = false;
    bool left = false;
    bool right = false;
    bool jumpDown = false;
    bool boardDown = false;

    [[nodiscard]] bool operator==(
        const WreckwaterApplicationDigitalControls&) const = default;
};

// Maps WASD-style levels into world X/Z using the horizontal camera basis.
// Returns false for non-finite or fully degenerate camera directions.
[[nodiscard]] bool mapWreckwaterCameraRelativeControls(
    const WreckwaterApplicationDigitalControls& digital,
    const glm::vec3& cameraForward,
    const glm::vec3& cameraRight,
    WreckwaterGraphicalClientControls& output) noexcept;

inline constexpr uint32_t
    kWreckwaterCameraTerrainDefaultMaximumIntervals = 256u;
inline constexpr uint64_t
    kWreckwaterCameraTerrainDefaultMaximumVisitedCells = 65'536u;

struct WreckwaterCameraTerrainView {
    std::span<const uint16_t> samples{};
    uint32_t width = 0u;
    uint32_t height = 0u;
    float heightScale = 0.0f;
    float cellScale = 0.0f;
    uint32_t maximumIntervals =
        kWreckwaterCameraTerrainDefaultMaximumIntervals;
    uint64_t maximumVisitedCells =
        kWreckwaterCameraTerrainDefaultMaximumVisitedCells;
};

enum class WreckwaterCameraTerrainCastStatus : uint32_t {
    Accepted = 0u,
    InvalidRequest,
    InvalidTerrain,
    WorkBudgetExceeded,
};

struct WreckwaterCameraTerrainCastResult {
    WreckwaterCameraTerrainCastStatus status =
        WreckwaterCameraTerrainCastStatus::InvalidRequest;
    float hitDistance = 0.0f;
    bool hit = false;

    [[nodiscard]] explicit operator bool() const noexcept {
        return status
            == WreckwaterCameraTerrainCastStatus::Accepted;
    }
};

// Bounded conservative heightfield coverage for the camera sphere cast.
// False positives shorten the boom; budget exhaustion never becomes a miss.
[[nodiscard]] bool wreckwaterCameraTerrainViewSupports(
    const WreckwaterCameraTerrainView& terrain,
    float maximumPathDistance,
    float sphereRadius) noexcept;

[[nodiscard]] WreckwaterCameraTerrainCastResult
wreckwaterCameraTerrainSphereCast(
    const WreckwaterCameraObstructionProbeRequest& request,
    const WreckwaterCameraTerrainView& terrain) noexcept;

// Merges the complete static-heightfield result with one exact async rigid
// body batch. Malformed or incomplete body evidence becomes overflow.
[[nodiscard]] WreckwaterCameraObstructionProbeResult
mergeWreckwaterCameraObstructionProbeResult(
    const WreckwaterCameraObstructionProbeRequest& request,
    const WreckwaterCameraTerrainCastResult& terrain,
    const physics::PhysicsQueryBatch& rigidBodies) noexcept;

struct WreckwaterApplicationClientFrameInput {
    uint64_t elapsedNanoseconds = 0u;
    glm::ivec3 cameraSector{0};
    WreckwaterGraphicalClientControls controls{};
    WreckwaterThirdPersonCameraInput camera{};
};

struct WreckwaterApplicationClientFrameResult {
    WreckwaterGraphicalClientFrameResult graphical{};
    WreckwaterThirdPersonCameraFrameResult camera{};
    WreckwaterCharacterPresentationStatus presentationRebaseStatus =
        WreckwaterCharacterPresentationStatus::Accepted;
    bool cameraUpdated = false;
    bool presentationRebased = false;
};

// Application-facing owner for the injected transport, runtime, graphical
// loop, and third-person camera. frame() is the only runtime pump path.
class WreckwaterApplicationClient {
public:
    struct Config {
        network::WreckwaterClientRuntime::Config runtime{};
        WreckwaterGraphicalClientLoop::Config graphical{};
        WreckwaterThirdPersonCameraRig::Config camera{};
    };

    WreckwaterApplicationClient() = default;
    ~WreckwaterApplicationClient();

    WreckwaterApplicationClient(
        const WreckwaterApplicationClient&) = delete;
    WreckwaterApplicationClient& operator=(
        const WreckwaterApplicationClient&) = delete;
    WreckwaterApplicationClient(
        WreckwaterApplicationClient&&) = delete;
    WreckwaterApplicationClient& operator=(
        WreckwaterApplicationClient&&) = delete;

    [[nodiscard]] bool initialize(
        const Config& config,
        std::unique_ptr<network::IMultiplayerTransport> transport);
    void close() noexcept;

    [[nodiscard]] WreckwaterApplicationClientFrameResult frame(
        const WreckwaterApplicationClientFrameInput& input);

    [[nodiscard]] bool initialized() const noexcept {
        return initialized_;
    }
    [[nodiscard]] const network::WreckwaterClientRuntime*
    runtime() const noexcept {
        return runtime_.get();
    }
    [[nodiscard]] const WreckwaterGraphicalClientLoop&
    graphicalLoop() const noexcept {
        return graphical_;
    }
    [[nodiscard]] const WreckwaterThirdPersonCameraRig&
    cameraRig() const noexcept {
        return camera_;
    }
    [[nodiscard]] std::span<const physics::DynamicBodySnapshot>
    visibleInstances() const noexcept {
        return graphical_.presentation()
            .primitiveProxyBatch().visibleInstances();
    }
    [[nodiscard]] bool cameraProbeOutstanding() const noexcept {
        return camera_.probeOutstanding();
    }
    [[nodiscard]] WreckwaterThirdPersonCameraStatus
    takeCameraObstructionProbe(
        WreckwaterCameraObstructionProbeRequest& output) noexcept {
        return camera_.takeObstructionProbeRequest(output);
    }
    [[nodiscard]] WreckwaterThirdPersonCameraStatus
    resolveCameraObstructionProbe(
        const WreckwaterCameraObstructionProbeResult& result) noexcept {
        return camera_.resolveObstructionProbe(result);
    }
    [[nodiscard]] WreckwaterThirdPersonCameraStatus
    cancelCameraObstructionProbe(
        const WreckwaterCameraObstructionProbeIdentity& identity)
        noexcept {
        return camera_.cancelObstructionProbe(identity);
    }

private:
    std::unique_ptr<network::WreckwaterClientRuntime> runtime_;
    WreckwaterGraphicalClientLoop graphical_{};
    WreckwaterThirdPersonCameraRig camera_{};
    bool initialized_ = false;
};

} // namespace voxy::client
