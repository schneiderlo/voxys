#pragma once

#include "physics/physics_types.hpp"

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

    [[nodiscard]] virtual bool setTerrain(std::span<const uint16_t> samples,
                                          uint32_t width, uint32_t height,
                                          float heightScale,
                                          float cellScale) = 0;
    virtual void setTerrainGpuResources(const TerrainGpuResources&) {}
    virtual void clearTerrain() = 0;
    [[nodiscard]] virtual bool hasTerrain() const noexcept = 0;

    virtual void setWaterPlane(float height, bool enabled) = 0;
    virtual void setWaterSurfaceSampler(WaterSurfaceSampler sampler) = 0;
    virtual void setWaterGpuResources(const WaterGpuResources&) {}

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

    // CPU backends execute here. A GPU backend can use this call to advance its
    // fixed-tick scheduler; GPU command encoding will be a separate interface.
    virtual void stepCpu(float deltaTime) = 0;
    virtual void encodeGpuStep(WGPUCommandEncoder) {}

    [[nodiscard]] virtual bool submitQueries(
        std::span<const PhysicsQueryRequest>, uint64_t) { return false; }
    [[nodiscard]] virtual std::optional<PhysicsQueryBatch> pollQueryResults() {
        return std::nullopt;
    }
    virtual void setEventReadbackEnabled(bool) {}
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
};

} // namespace voxy::physics
