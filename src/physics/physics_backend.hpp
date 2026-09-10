#pragma once

#include "physics/physics_types.hpp"
#include "physics/authored_shape_resources.hpp"
#include "physics/authored_body_frame.hpp"

#include <memory>
#include <span>
#include <vector>

namespace voxy::physics {

// Internal execution boundary. GPU-specific scheduling and render resources are
// added here without exposing a concrete SDK through PhysicsWorld.
class IPhysicsBackend {
public:
    virtual ~IPhysicsBackend() = default;

    [[nodiscard]] virtual bool initialize(const PhysicsInitContext& context) = 0;
    virtual void shutdown() = 0;
    [[nodiscard]] virtual bool isInitialized() const noexcept = 0;
    [[nodiscard]] virtual BackendType type() const noexcept = 0;
    [[nodiscard]] virtual BackendCapabilities capabilities() const noexcept = 0;

    [[nodiscard]] virtual ShapeResourceError enableAuthoredShapeResources(
        const ShapeResourceLimits&) noexcept { return ShapeResourceError::Unsupported; }
    [[nodiscard]] virtual IAuthoredShapeResources* authoredShapeResources() noexcept { return nullptr; }
    [[nodiscard]] virtual AuthoredBodySpawnResult spawnAuthoredBody(const AuthoredBodySpawnDesc&) { return {}; }
    [[nodiscard]] virtual AuthoredBodyError configureAuthoredWaterBody(const AuthoredWaterBodyDesc&) {
        return AuthoredBodyError::Unsupported;
    }
    [[nodiscard]] virtual bool setAuthoredHelm(BodyHandle, float, float) noexcept { return false; }
    [[nodiscard]] virtual ShapeResourceSubmission prepareGpuSubmission(ShapeResourceError& error) noexcept {
        error = ShapeResourceError::Unsupported; return {};
    }
    [[nodiscard]] virtual ShapeResourceError submitGpuSubmission(ShapeResourceSubmission,
        std::span<const WGPUCommandBuffer>) noexcept { return ShapeResourceError::Unsupported; }
    [[nodiscard]] virtual ShapeResourceError discardGpuSubmission(ShapeResourceSubmission) noexcept {
        return ShapeResourceError::Unsupported;
    }

    [[nodiscard]] virtual bool setTerrain(std::span<const uint16_t> samples,
                                          uint32_t width, uint32_t height,
                                          float heightScale,
                                          float cellScale) = 0;
    // Explicit opt-in: an unsupported backend must not silently use ramps.
    [[nodiscard]] virtual bool setLegoTerrain(std::span<const uint16_t>,
        uint32_t, uint32_t, float, float) { return false; }
    virtual void setTerrainGpuResources(const TerrainGpuResources&) {}
    virtual void clearTerrain() = 0;
    [[nodiscard]] virtual bool hasTerrain() const noexcept = 0;

    virtual void setWaterPlane(float height, bool enabled) = 0;
    virtual void setWaterSurfaceSampler(WaterSurfaceSampler sampler) = 0;
    virtual void setWaterGpuResources(const WaterGpuResources&) {}
    [[nodiscard]] virtual bool stageWaterGpuFrame(const WaterGpuFrame&) noexcept { return false; }

    [[nodiscard]] virtual CharacterHandle createCharacter(
        const glm::vec3& feetPosition,
        const CharacterSettings& settings) = 0;
    [[nodiscard]] virtual CharacterHandle createCharacter(
        const WorldPosition& feetPosition,
        const CharacterSettings& settings) {
        if (!isValidWorldPosition(feetPosition)) return InvalidCharacter;
        return createCharacter(
            glm::vec3(worldPositionToAbsolute(feetPosition)), settings);
    }
    virtual void destroyCharacter(CharacterHandle handle) = 0;
    [[nodiscard]] virtual bool setCharacterPosition(
        CharacterHandle handle, const glm::vec3& feetPosition) = 0;
    [[nodiscard]] virtual bool setCharacterPosition(
        CharacterHandle handle, const WorldPosition& feetPosition) {
        return isValidWorldPosition(feetPosition)
            && setCharacterPosition(
                handle, glm::vec3(worldPositionToAbsolute(feetPosition)));
    }
    [[nodiscard]] virtual CharacterMotion moveCharacter(
        CharacterHandle handle,
        const glm::vec3& desiredHorizontalVelocity,
        bool jump, float jumpSpeed, float gravity, float terminalVelocity,
        float deltaTime) = 0;

    [[nodiscard]] virtual bool throwBody(ThrowableShape shape,
                                         const glm::vec3& position,
                                         const glm::vec3& velocity) = 0;

    // Stable-handle command path. CPU reference backends retain their existing
    // immediate compatibility API until their compact render bridge is active.
    [[nodiscard]] virtual BodyHandle spawnBody(const BodySpawnDesc&) {
        return {};
    }
    [[nodiscard]] virtual bool destroyBody(BodyHandle) { return false; }
    virtual void enqueue(std::span<const PhysicsCommand>) {}
    [[nodiscard]] virtual AttachmentHandle createDistanceAttachment(
        const DistanceAttachmentDesc&) { return {}; }
    [[nodiscard]] virtual bool destroyAttachment(AttachmentHandle) {
        return false;
    }
    [[nodiscard]] virtual bool setAttachmentTargetLength(
        AttachmentHandle, float) { return false; }
    [[nodiscard]] virtual bool setAttachmentMotorSpeed(
        AttachmentHandle, float) { return false; }
    [[nodiscard]] virtual PreparedPhysicsMutation prepareMutationBatch(
        const PhysicsMutationBatch&) noexcept {
        return {};
    }
    [[nodiscard]] virtual PhysicsMutationResult commitPrepared(
        const PreparedPhysicsMutation&) noexcept {
        return {};
    }
    [[nodiscard]] virtual bool discardPrepared(
        const PreparedPhysicsMutation&) noexcept {
        return false;
    }

    // CPU backends execute here. A GPU backend can use this call to advance its
    // fixed-tick scheduler; GPU command encoding will be a separate interface.
    virtual void stepCpu(float deltaTime) = 0;
    [[nodiscard]] virtual bool scheduleFixedTicks(uint32_t) { return false; }
    // Owned worlds only. Retain already scheduled work, clear clock debt and
    // prevent new time accrual. Optionally reserve a bounded final control tick
    // atomically when entering pause. The caller must still drain its evidence.
    [[nodiscard]] virtual bool setSchedulingPaused(bool, uint32_t) noexcept { return false; }
    virtual void encodeGpuStep(WGPUCommandEncoder) {}
    [[nodiscard]] virtual PhysicsEncodeReport encodeGpuStepChecked(
        WGPUCommandEncoder) {
        return {};
    }

    [[nodiscard]] virtual bool submitQueries(
        std::span<const PhysicsQueryRequest>, uint64_t) { return false; }
    [[nodiscard]] virtual std::optional<PhysicsQueryBatch> pollQueryResults() {
        return std::nullopt;
    }
    virtual bool setEventReadbackEnabled(bool enabled) {
        return !enabled;
    }
    [[nodiscard]] virtual std::optional<PhysicsEventBatch> pollEvents() {
        return std::nullopt;
    }
    [[nodiscard]] virtual std::optional<PhysicsGpuStageTiming>
    pollGpuStageTimings() { return std::nullopt; }

    [[nodiscard]] virtual PhysicsRenderView renderView() const { return {}; }
    virtual void requestDebugSnapshot(DebugSnapshotRequest) {}
    [[nodiscard]] virtual std::optional<DebugSnapshot> pollDebugSnapshot() {
        return std::nullopt;
    }

    [[nodiscard]] virtual std::vector<DynamicBodySnapshot> dynamicBodies(
        size_t additionalCapacity) const = 0;
    [[nodiscard]] virtual DynamicBodyReadStats lastDynamicBodyReadStats()
        const noexcept = 0;
    [[nodiscard]] virtual PhysicsStats stats() const noexcept = 0;
    [[nodiscard]] virtual PhysicsStepStats lastStepStats() const noexcept = 0;
    [[nodiscard]] virtual uint64_t encodedTick() const noexcept { return 0; }
    [[nodiscard]] virtual PhysicsTickFrontier tickFrontier() const noexcept { return {}; }
};

} // namespace voxy::physics
