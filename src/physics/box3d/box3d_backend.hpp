#pragma once

#include "physics/physics_backend.hpp"

#include <memory>

namespace voxy::physics {

// Pinned Box3D CPU correctness oracle and production fallback.
class Box3DReferenceBackend final : public IPhysicsBackend {
public:
    Box3DReferenceBackend();
    ~Box3DReferenceBackend() override;

    Box3DReferenceBackend(const Box3DReferenceBackend&) = delete;
    Box3DReferenceBackend& operator=(const Box3DReferenceBackend&) = delete;
    Box3DReferenceBackend(Box3DReferenceBackend&&) noexcept;
    Box3DReferenceBackend& operator=(Box3DReferenceBackend&&) noexcept;

    [[nodiscard]] bool initialize(const PhysicsInitContext& context) override;
    void shutdown() override;
    [[nodiscard]] bool isInitialized() const noexcept override;
    [[nodiscard]] BackendType type() const noexcept override;
    [[nodiscard]] BackendCapabilities capabilities() const noexcept override;

    [[nodiscard]] bool setTerrain(std::span<const uint16_t> samples,
                                  uint32_t width, uint32_t height,
                                  float heightScale,
                                  float cellScale) override;
    void clearTerrain() override;
    [[nodiscard]] bool hasTerrain() const noexcept override;

    void setWaterPlane(float height, bool enabled) override;
    void setWaterSurfaceSampler(WaterSurfaceSampler sampler) override;

    [[nodiscard]] CharacterHandle createCharacter(
        const glm::vec3& feetPosition,
        const CharacterSettings& settings) override;
    void destroyCharacter(CharacterHandle handle) override;
    [[nodiscard]] bool setCharacterPosition(
        CharacterHandle handle, const glm::vec3& feetPosition) override;
    [[nodiscard]] CharacterMotion moveCharacter(
        CharacterHandle handle,
        const glm::vec3& desiredHorizontalVelocity,
        bool jump, float jumpSpeed, float gravity, float terminalVelocity,
        float deltaTime) override;

    [[nodiscard]] bool throwBody(ThrowableShape shape,
                                 const glm::vec3& position,
                                 const glm::vec3& velocity) override;
    void stepCpu(float deltaTime) override;

    [[nodiscard]] std::vector<DynamicBodySnapshot> dynamicBodies(
        size_t additionalCapacity) const override;
    [[nodiscard]] DynamicBodyReadStats lastDynamicBodyReadStats()
        const noexcept override;
    [[nodiscard]] PhysicsStats stats() const noexcept override;
    [[nodiscard]] PhysicsStepStats lastStepStats() const noexcept override;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace voxy::physics
