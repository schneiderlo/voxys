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

    /// Attach a heightmap as streamed, full-resolution collision tiles.
    /// The sample storage must remain alive until clearTerrain() or shutdown().
    [[nodiscard]] bool setTerrain(std::span<const uint16_t> samples,
                                  uint32_t width, uint32_t height,
                                  float heightScale, float cellScale);
    void setTerrainGpuResources(const TerrainGpuResources& resources);
    void clearTerrain();
    [[nodiscard]] bool hasTerrain() const noexcept;

    void setWaterPlane(float height, bool enabled = true);
    void setWaterSurfaceSampler(WaterSurfaceSampler sampler);

    [[nodiscard]] CharacterHandle createCharacter(
        const glm::vec3& feetPosition,
        const CharacterSettings& settings);
    void destroyCharacter(CharacterHandle handle);
    [[nodiscard]] bool setCharacterPosition(CharacterHandle handle,
                                            const glm::vec3& feetPosition);
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

    void update(float deltaTime);
    void encodeGpuStep(WGPUCommandEncoder encoder);

    [[nodiscard]] bool submitQueries(
        std::span<const PhysicsQueryRequest> requests,
        uint64_t resultTick = 0);
    [[nodiscard]] std::optional<PhysicsQueryBatch> pollQueryResults();
    void setEventReadbackEnabled(bool enabled);
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
