#pragma once

#include "client/wreckwater_character_controller.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#include <glm/gtc/quaternion.hpp>
#include <glm/vec3.hpp>

namespace voxy::client {

inline constexpr uint32_t kWreckwaterAvatarPresentationCapacity =
    game::kWreckwaterPlayerCount;

enum class WreckwaterCharacterPresentationStatus : uint32_t {
    Accepted = 0u,
    NotInitialized,
    InvalidConfiguration,
    InvalidSample,
    SnapshotIdentityMismatch,
    StaleSample,
    LocalSampleMissing,
    LocalIdentityMismatch,
    LocalPoseUnavailable,
    PositionOverflow,
};

[[nodiscard]] const char* wreckwaterCharacterPresentationStatusName(
    WreckwaterCharacterPresentationStatus status) noexcept;

struct WreckwaterCharacterPresentationRosterSlot {
    uint64_t playerId = 0u;
    game::CrewId crew = game::CrewId::None;
    uint32_t crewSlot = 0u;

    [[nodiscard]] bool operator==(
        const WreckwaterCharacterPresentationRosterSlot&) const =
        default;
};

struct WreckwaterAvatarPresentation {
    uint64_t playerId = 0u;
    uint32_t characterHandle = 0u;
    uint32_t connectionGeneration = 0u;
    game::CrewId crew = game::CrewId::None;
    uint32_t crewSlot = 0u;
    uint64_t sourceSnapshotSequence = 0u;
    uint64_t poseTick = 0u;
    float poseFraction = 0.0f;
    game::WreckwaterCharacterMode mode =
        game::WreckwaterCharacterMode::Airborne;
    physics::WorldPosition worldFeetPosition{};
    // Relative to the origin of cameraSector, not camera local position.
    glm::vec3 cameraSectorFeetPosition{0.0f};
    glm::vec3 worldVelocity{0.0f};
    glm::vec3 facing{0.0f, 0.0f, 1.0f};
    game::SkiffId skiffId = 0u;
    uint32_t skiffGeneration = 0u;
    bool localPredicted = false;
    bool visible = false;
    bool teleported = false;
    bool discontinuity = false;

    [[nodiscard]] bool operator==(
        const WreckwaterAvatarPresentation&) const = default;
};

struct WreckwaterThirdPersonCameraTarget {
    uint64_t playerId = 0u;
    uint32_t characterHandle = 0u;
    uint32_t connectionGeneration = 0u;
    physics::WorldPosition worldTargetPosition{};
    glm::vec3 cameraSectorTargetPosition{0.0f};
    glm::vec3 worldVelocity{0.0f};
    glm::vec3 facing{0.0f, 0.0f, 1.0f};
    game::WreckwaterCharacterMode mode =
        game::WreckwaterCharacterMode::Airborne;
    bool valid = false;
    bool teleported = false;
    bool discontinuity = false;

    [[nodiscard]] bool operator==(
        const WreckwaterThirdPersonCameraTarget&) const = default;
};

// Deliberately plain capsule proxies. PrimitivePath::setInstances() accepts
// this fixed batch directly. This is an integration/debug seam, not an
// art-complete avatar renderer or an authority physics body.
struct WreckwaterAvatarPrimitiveProxyBatch {
    std::array<
        physics::DynamicBodySnapshot,
        kWreckwaterAvatarPresentationCapacity>
        instances{};
    uint32_t count = 0u;

    [[nodiscard]] std::span<const physics::DynamicBodySnapshot>
    visibleInstances() const noexcept {
        const size_t visible =
            count <= instances.size() ? count : instances.size();
        return std::span<const physics::DynamicBodySnapshot>(
            instances.data(), visible);
    }
};

struct WreckwaterCharacterPresentationTelemetry {
    uint64_t samplesAccepted = 0u;
    uint64_t samplesRejected = 0u;
    uint64_t localPredictionOverrides = 0u;
    uint64_t visibilityPurges = 0u;
    uint64_t teleports = 0u;
    uint64_t discontinuities = 0u;
    uint64_t facingUpdates = 0u;
    uint64_t facingRetentions = 0u;
    uint64_t cameraSectorRebases = 0u;
    uint32_t visibleHighWater = 0u;

    [[nodiscard]] bool operator==(
        const WreckwaterCharacterPresentationTelemetry&) const =
        default;
};

class WreckwaterCharacterPresentation {
public:
    struct Config {
        std::array<
            WreckwaterCharacterPresentationRosterSlot,
            kWreckwaterAvatarPresentationCapacity>
            roster{};
        uint64_t localPlayerId = 0u;
        float facingSpeedThreshold = 0.10f;
        float teleportDistance = 6.0f;
        uint32_t maximumCameraSectorDelta = 4'096u;
        float cameraTargetHeight = 1.35f;
        glm::vec3 defaultFacing{0.0f, 0.0f, 1.0f};
        glm::vec3 primitiveProxyDimensions{0.8f, 1.8f, 0.8f};
    };

    [[nodiscard]] bool initialize(const Config& config) noexcept;
    void reset() noexcept;

    // `sample` is an already accepted replication sample. The local sampled
    // character is identity evidence only: its visual pose is never emitted.
    [[nodiscard]] WreckwaterCharacterPresentationStatus update(
        const WreckwaterCharacterController& localController,
        const network::WreckwaterClientSample& sample,
        const glm::ivec3& cameraSector) noexcept;

    // Rebuilds camera-relative avatar/proxy descriptors without consuming a
    // second network sample. Use this when the camera rig crosses a sector
    // after the frame's presentation sample has already been built.
    [[nodiscard]] WreckwaterCharacterPresentationStatus
    rebaseCameraSector(const glm::ivec3& cameraSector) noexcept;

    [[nodiscard]] bool initialized() const noexcept {
        return initialized_;
    }
    [[nodiscard]] std::span<const WreckwaterAvatarPresentation>
    avatars() const noexcept {
        return avatars_;
    }
    [[nodiscard]] uint32_t visibleCount() const noexcept {
        return visibleCount_;
    }
    [[nodiscard]] const WreckwaterThirdPersonCameraTarget&
    thirdPersonCameraTarget() const noexcept {
        return cameraTarget_;
    }
    [[nodiscard]] const WreckwaterAvatarPrimitiveProxyBatch&
    primitiveProxyBatch() const noexcept {
        return primitiveProxies_;
    }
    [[nodiscard]] const WreckwaterCharacterPresentationTelemetry&
    telemetry() const noexcept {
        return telemetry_;
    }

private:
    struct SlotState {
        uint32_t characterHandle = 0u;
        uint32_t connectionGeneration = 0u;
        game::WreckwaterCharacterMode mode =
            game::WreckwaterCharacterMode::Airborne;
        physics::WorldPosition worldFeetPosition{};
        glm::vec3 facing{0.0f, 0.0f, 1.0f};
        game::SkiffId skiffId = 0u;
        uint32_t skiffGeneration = 0u;
        bool visible = false;
        bool hasFacing = false;
    };

    [[nodiscard]] size_t rosterIndex(uint64_t playerId) const noexcept;
    [[nodiscard]] WreckwaterCharacterPresentationStatus applyVisible(
        size_t index,
        uint32_t characterHandle,
        uint32_t connectionGeneration,
        game::WreckwaterCharacterMode mode,
        const physics::WorldPosition& worldFeetPosition,
        const glm::vec3& worldVelocity,
        game::SkiffId skiffId,
        uint32_t skiffGeneration,
        uint64_t snapshotSequence,
        uint64_t poseTick,
        float poseFraction,
        bool localPredicted,
        const glm::ivec3& cameraSector,
        std::array<SlotState,
                   kWreckwaterAvatarPresentationCapacity>& states,
        std::array<WreckwaterAvatarPresentation,
                   kWreckwaterAvatarPresentationCapacity>& avatars,
        WreckwaterCharacterPresentationTelemetry& telemetry) const
        noexcept;
    void applyHidden(
        size_t index,
        uint32_t characterHandle,
        uint32_t connectionGeneration,
        std::array<SlotState,
                   kWreckwaterAvatarPresentationCapacity>& states,
        std::array<WreckwaterAvatarPresentation,
                   kWreckwaterAvatarPresentationCapacity>& avatars,
        WreckwaterCharacterPresentationTelemetry& telemetry) const
        noexcept;
    [[nodiscard]] WreckwaterCharacterPresentationStatus
    buildCameraAndPrimitiveDescriptors(
        const glm::ivec3& cameraSector,
        const std::array<WreckwaterAvatarPresentation,
                         kWreckwaterAvatarPresentationCapacity>&
            avatars,
        WreckwaterThirdPersonCameraTarget& cameraTarget,
        WreckwaterAvatarPrimitiveProxyBatch& proxies) const noexcept;

    Config config_{};
    std::array<
        WreckwaterCharacterPresentationRosterSlot,
        kWreckwaterAvatarPresentationCapacity>
        roster_{};
    std::array<
        SlotState, kWreckwaterAvatarPresentationCapacity>
        states_{};
    std::array<
        WreckwaterAvatarPresentation,
        kWreckwaterAvatarPresentationCapacity>
        avatars_{};
    WreckwaterThirdPersonCameraTarget cameraTarget_{};
    WreckwaterAvatarPrimitiveProxyBatch primitiveProxies_{};
    network::WreckwaterClientReplicationIdentity identity_{};
    uint64_t latestSnapshotSequence_ = 0u;
    uint32_t visibleCount_ = 0u;
    bool initialized_ = false;
    bool identityPinned_ = false;
    WreckwaterCharacterPresentationTelemetry telemetry_{};
};

} // namespace voxy::client
