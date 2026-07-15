#pragma once

#include "physics/physics_backend.hpp"

#include <memory>

namespace voxy::physics {

class GpuPhysicsBackend final : public IPhysicsBackend {
public:
    GpuPhysicsBackend();
    ~GpuPhysicsBackend() override;

    GpuPhysicsBackend(const GpuPhysicsBackend&) = delete;
    GpuPhysicsBackend& operator=(const GpuPhysicsBackend&) = delete;
    GpuPhysicsBackend(GpuPhysicsBackend&&) noexcept;
    GpuPhysicsBackend& operator=(GpuPhysicsBackend&&) noexcept;

    [[nodiscard]] bool initialize(const PhysicsInitContext& context) override;
    void shutdown() override;
    [[nodiscard]] bool isInitialized() const noexcept override;
    [[nodiscard]] BackendType type() const noexcept override;
    [[nodiscard]] BackendCapabilities capabilities() const noexcept override;

    [[nodiscard]] bool setTerrain(std::span<const uint16_t> samples,
                                  uint32_t width, uint32_t height,
                                  float heightScale,
                                  float cellScale) override;
    void setTerrainGpuResources(
        const TerrainGpuResources& resources) override;
    void clearTerrain() override;
    [[nodiscard]] bool hasTerrain() const noexcept override;

    void setWaterPlane(float height, bool enabled) override;
    void setWaterSurfaceSampler(WaterSurfaceSampler sampler) override;

    [[nodiscard]] CharacterHandle createCharacter(
        const glm::vec3& feetPosition,
        const CharacterSettings& settings) override;
    [[nodiscard]] CharacterHandle createCharacter(
        const WorldPosition& feetPosition,
        const CharacterSettings& settings) override;
    void destroyCharacter(CharacterHandle handle) override;
    [[nodiscard]] bool setCharacterPosition(
        CharacterHandle handle, const glm::vec3& feetPosition) override;
    [[nodiscard]] bool setCharacterPosition(
        CharacterHandle handle,
        const WorldPosition& feetPosition) override;
    [[nodiscard]] CharacterMotion moveCharacter(
        CharacterHandle handle,
        const glm::vec3& desiredHorizontalVelocity,
        bool jump, float jumpSpeed, float gravity, float terminalVelocity,
        float deltaTime) override;

    [[nodiscard]] bool throwBody(ThrowableShape shape,
                                 const glm::vec3& position,
                                 const glm::vec3& velocity) override;
    [[nodiscard]] BodyHandle spawnBody(const BodySpawnDesc& desc) override;
    [[nodiscard]] bool destroyBody(BodyHandle handle) override;
    void enqueue(std::span<const PhysicsCommand> commands) override;

    void stepCpu(float deltaTime) override;
    void encodeGpuStep(WGPUCommandEncoder encoder) override;
    [[nodiscard]] bool submitQueries(
        std::span<const PhysicsQueryRequest> requests,
        uint64_t tick) override;
    [[nodiscard]] std::optional<PhysicsQueryBatch>
        pollQueryResults() override;
    void setEventReadbackEnabled(bool enabled) override;
    [[nodiscard]] std::optional<PhysicsEventBatch> pollEvents() override;
    [[nodiscard]] std::optional<PhysicsGpuStageTiming>
        pollGpuStageTimings() override;

    [[nodiscard]] PhysicsRenderView renderView() const override;
    void requestDebugSnapshot(DebugSnapshotRequest request) override;
    [[nodiscard]] std::optional<DebugSnapshot> pollDebugSnapshot() override;

    [[nodiscard]] std::vector<DynamicBodySnapshot> dynamicBodies(
        size_t additionalCapacity) const override;
    [[nodiscard]] DynamicBodyReadStats lastDynamicBodyReadStats()
        const noexcept override;
    [[nodiscard]] PhysicsStats stats() const noexcept override;
    [[nodiscard]] PhysicsStepStats lastStepStats() const noexcept override;
    [[nodiscard]] uint64_t encodedTick() const noexcept override;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace voxy::physics
