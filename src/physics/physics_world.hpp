// Cross-platform physics facade.
//
// Concrete engines stay behind IPhysicsBackend so callers do not parse Jolt,
// Box3D, or WebGPU implementation headers.

#pragma once

#include "physics/physics_types.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace voxy::physics {

class IPhysicsBackend;
class IAuthoredShapeResources;
struct ShapeResourceLimits;
struct ShapeResourceSubmission;
struct AuthoredBodySpawnDesc;
struct AuthoredBodySpawnResult;
struct AuthoredWaterBodyDesc;
enum class AuthoredBodyError : uint8_t;
enum class ShapeResourceError : uint8_t;

class PhysicsWorld {
public:
    // Source-compatible names while shared backend types live outside the
    // facade and can be consumed by IPhysicsBackend.
    using CharacterHandle = physics::CharacterHandle;
    static constexpr CharacterHandle InvalidCharacter =
        physics::InvalidCharacter;
    using ThrowableShape = physics::ThrowableShape;
    using DynamicBodySnapshot = physics::DynamicBodySnapshot;
    using DynamicBodyReadStats = physics::DynamicBodyReadStats;
    using WaterSurfaceSample = physics::WaterSurfaceSample;
    using WaterSurfaceSampler = physics::WaterSurfaceSampler;
    using CharacterSettings = physics::CharacterSettings;
    using CharacterMotion = physics::CharacterMotion;

    PhysicsWorld();
    ~PhysicsWorld();

    PhysicsWorld(const PhysicsWorld&) = delete;
    PhysicsWorld& operator=(const PhysicsWorld&) = delete;
    PhysicsWorld(PhysicsWorld&&) noexcept;
    PhysicsWorld& operator=(PhysicsWorld&&) noexcept;

    [[nodiscard]] bool initialize();
    [[nodiscard]] bool initialize(const PhysicsInitContext& context);
    void shutdown();
    [[nodiscard]] bool isInitialized() const noexcept;
    [[nodiscard]] BackendType backendType() const noexcept;
    [[nodiscard]] BackendCapabilities capabilities() const noexcept;
    [[nodiscard]] PhysicsStats stats() const noexcept;
    [[nodiscard]] PhysicsStepStats lastStepStats() const noexcept;
    [[nodiscard]] uint64_t encodedTick() const noexcept;
    [[nodiscard]] PhysicsTickFrontier tickFrontier() const noexcept;

    // Opt-in bounded resource allocation. CPU backends explicitly refuse;
    // this does not enable authored-body support or a primitive fallback.
    // Configure once per initialized world, then poll the borrowed resource
    // interface until Ready. Close/drain it before normal world shutdown.
    [[nodiscard]] ShapeResourceError enableAuthoredShapeResources() noexcept;
    [[nodiscard]] ShapeResourceError enableAuthoredShapeResources(
        const ShapeResourceLimits& limits) noexcept;
    [[nodiscard]] IAuthoredShapeResources* authoredShapeResources() noexcept;

    // Discrete rigid body using the prepared exterior and mass. LEGO terrain
    // and authored bullet requests are unsupported; water modules are separate.
    [[nodiscard]] AuthoredBodySpawnResult spawnAuthoredBody(const AuthoredBodySpawnDesc&);
    [[nodiscard]] AuthoredBodyError configureAuthoredWaterBody(const AuthoredWaterBodyDesc&);
    [[nodiscard]] bool setAuthoredHelm(BodyHandle, float throttle, float steering) noexcept;
    // Declare all live/pending authored shapes BEFORE creating a frame encoder.
    // This boundary owns the real queue submission, including physics/rendering.
    [[nodiscard]] ShapeResourceSubmission prepareGpuSubmission(ShapeResourceError&) noexcept;
    [[nodiscard]] ShapeResourceError submitGpuSubmission(ShapeResourceSubmission,
        std::span<const WGPUCommandBuffer>) noexcept;
    // Release commands/encoders first. Discard after physics encoding fail-stops
    // the world because host tick/mutation state has already advanced.
    [[nodiscard]] ShapeResourceError discardGpuSubmission(ShapeResourceSubmission) noexcept;

    /// Attach a heightmap as streamed, full-resolution collision tiles.
    /// The sample storage must remain alive until clearTerrain() or shutdown().
    [[nodiscard]] bool setTerrain(std::span<const uint16_t> samples,
                                  uint32_t width, uint32_t height,
                                  float heightScale, float cellScale);
    [[nodiscard]] bool setLegoTerrain(std::span<const uint16_t> samples,
        uint32_t width, uint32_t height, float heightScale, float cellScale);
    void setTerrainGpuResources(const TerrainGpuResources& resources);
    void clearTerrain();
    [[nodiscard]] bool hasTerrain() const noexcept;

    void setWaterPlane(float height, bool enabled = true);
    void setWaterSurfaceSampler(WaterSurfaceSampler sampler);
    void setWaterGpuResources(const WaterGpuResources& resources);
    [[nodiscard]] bool stageWaterGpuFrame(const WaterGpuFrame& frame) noexcept;

    [[nodiscard]] CharacterHandle createCharacter(
        const glm::vec3& feetPosition,
        const CharacterSettings& settings);
    [[nodiscard]] CharacterHandle createCharacter(
        const WorldPosition& feetPosition,
        const CharacterSettings& settings);
    void destroyCharacter(CharacterHandle handle);
    [[nodiscard]] bool setCharacterPosition(CharacterHandle handle,
                                            const glm::vec3& feetPosition);
    [[nodiscard]] bool setCharacterPosition(
        CharacterHandle handle, const WorldPosition& feetPosition);
    [[nodiscard]] CharacterMotion moveCharacter(
        CharacterHandle handle,
        const glm::vec3& desiredHorizontalVelocity,
        bool jump, float jumpSpeed, float gravity, float terminalVelocity,
        float deltaTime);

    [[nodiscard]] bool throwBody(ThrowableShape shape,
                                 const glm::vec3& position,
                                 const glm::vec3& velocity);

    [[nodiscard]] BodyHandle spawnBody(const BodySpawnDesc& desc);
    [[nodiscard]] bool destroyBody(BodyHandle handle);
    void enqueue(std::span<const PhysicsCommand> commands);
    [[nodiscard]] AttachmentHandle createDistanceAttachment(
        const DistanceAttachmentDesc& desc);
    [[nodiscard]] bool destroyAttachment(AttachmentHandle handle);
    [[nodiscard]] bool setAttachmentTargetLength(
        AttachmentHandle handle, float targetLength);
    [[nodiscard]] bool setAttachmentMotorSpeed(
        AttachmentHandle handle, float motorSpeed);
    [[nodiscard]] PreparedPhysicsMutation prepareMutationBatch(
        const PhysicsMutationBatch& batch) noexcept;
    [[nodiscard]] PhysicsMutationResult commitPrepared(
        const PreparedPhysicsMutation& prepared) noexcept;
    [[nodiscard]] bool discardPrepared(
        const PreparedPhysicsMutation& prepared) noexcept;

    void update(float deltaTime);
    [[nodiscard]] bool scheduleFixedTicks(uint32_t tickCount);
    [[nodiscard]] bool setSchedulingPaused(bool paused, uint32_t finalTicks = 0) noexcept;
    void encodeGpuStep(WGPUCommandEncoder encoder);
    [[nodiscard]] PhysicsEncodeReport encodeGpuStepChecked(
        WGPUCommandEncoder encoder);

    [[nodiscard]] bool submitQueries(
        std::span<const PhysicsQueryRequest> requests,
        uint64_t resultTick = 0);
    [[nodiscard]] std::optional<PhysicsQueryBatch> pollQueryResults();
    bool setEventReadbackEnabled(bool enabled);
    [[nodiscard]] std::optional<PhysicsEventBatch> pollEvents();
    [[nodiscard]] std::optional<PhysicsGpuStageTiming> pollGpuStageTimings();

    [[nodiscard]] PhysicsRenderView renderView() const;
    void requestDebugSnapshot(DebugSnapshotRequest request);
    [[nodiscard]] std::optional<DebugSnapshot> pollDebugSnapshot();

    [[nodiscard]] std::vector<DynamicBodySnapshot> dynamicBodies(
        size_t additionalCapacity = 0) const;
    [[nodiscard]] DynamicBodyReadStats lastDynamicBodyReadStats() const noexcept;

    [[nodiscard]] static const char* throwableShapeName(
        ThrowableShape shape) noexcept;
    [[nodiscard]] static glm::vec3 throwableShapeDimensions(
        ThrowableShape shape) noexcept;

private:
    std::unique_ptr<IPhysicsBackend> backend_;
};

} // namespace voxy::physics
