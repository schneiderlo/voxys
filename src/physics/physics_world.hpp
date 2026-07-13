// Cross-platform Jolt Physics integration.
//
// Jolt stays behind this PIMPL boundary so most of voxy does not need to parse
// its large header set. The implementation deliberately uses the same
// single-threaded scheduler and terrain tiling policy on native and WASM.

#pragma once

#include <glm/vec3.hpp>
#include <glm/vec2.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <vector>

namespace voxy::physics {

class PhysicsWorld {
public:
    using CharacterHandle = uint32_t;
    static constexpr CharacterHandle InvalidCharacter = 0;

    enum class ThrowableShape : uint32_t {
        Sphere = 0,
        Cube,
        Box,
        Capsule,
        Cylinder,
        Count
    };

    struct DynamicBodySnapshot {
        ThrowableShape shape = ThrowableShape::Sphere;
        glm::vec3 position{0.0f};
        glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
        glm::vec3 dimensions{1.0f};
        // Rendering hint only. Transform fields remain the authoritative state.
        bool active = false;
    };

    struct WaterSurfaceSample {
        float heightOffset = 0.0f;
        glm::vec2 slope{0.0f};
        glm::vec3 velocity{0.0f};
    };

    using WaterSurfaceSampler =
        std::function<WaterSurfaceSample(glm::vec2 position, float timeSeconds)>;

    struct CharacterSettings {
        float radius = 0.4f;
        float height = 1.8f;
        float maxSlopeAngleDegrees = 45.0f;
        float stepUp = 0.5f;
        float stepDown = 0.5f;
    };

    struct CharacterMotion {
        glm::vec3 position{0.0f}; // Feet position in world space.
        glm::vec3 velocity{0.0f};
        glm::vec3 groundNormal{0.0f, 1.0f, 0.0f};
        bool grounded = false;
        bool onSteepGround = false;
    };

    PhysicsWorld();
    ~PhysicsWorld();

    PhysicsWorld(const PhysicsWorld&) = delete;
    PhysicsWorld& operator=(const PhysicsWorld&) = delete;
    PhysicsWorld(PhysicsWorld&&) noexcept;
    PhysicsWorld& operator=(PhysicsWorld&&) noexcept;

    [[nodiscard]] bool initialize();
    void shutdown();
    [[nodiscard]] bool isInitialized() const noexcept;

    /// Attach a heightmap as streamed, full-resolution collision tiles.
    /// The sample storage must remain alive until clearTerrain() or shutdown().
    [[nodiscard]] bool setTerrain(std::span<const uint16_t> samples,
                                  uint32_t width, uint32_t height,
                                  float heightScale, float cellScale);
    void clearTerrain();
    [[nodiscard]] bool hasTerrain() const noexcept;

    /// Configure the horizontal water surface used for rigid-body buoyancy.
    void setWaterPlane(float height, bool enabled = true);
    void setWaterSurfaceSampler(WaterSurfaceSampler sampler);

    [[nodiscard]] CharacterHandle createCharacter(
        const glm::vec3& feetPosition,
        const CharacterSettings& settings);
    void destroyCharacter(CharacterHandle handle);
    [[nodiscard]] bool setCharacterPosition(CharacterHandle handle,
                                            const glm::vec3& feetPosition);

    /// Advance one virtual character. Long frames are internally sub-stepped.
    [[nodiscard]] CharacterMotion moveCharacter(
        CharacterHandle handle,
        const glm::vec3& desiredHorizontalVelocity,
        bool jump,
        float jumpSpeed,
        float gravity,
        float terminalVelocity,
        float deltaTime);

    /// Spawn one visible rigid body. Bodies remain until world shutdown.
    [[nodiscard]] bool throwBody(ThrowableShape shape,
                                 const glm::vec3& position,
                                 const glm::vec3& velocity);

    /// Advance simulated rigid bodies. Call once per application frame.
    void update(float deltaTime);

    /// Copy current transforms for rendering.
    [[nodiscard]] std::vector<DynamicBodySnapshot> dynamicBodies() const;

    [[nodiscard]] static const char* throwableShapeName(ThrowableShape shape) noexcept;
    [[nodiscard]] static glm::vec3 throwableShapeDimensions(ThrowableShape shape) noexcept;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace voxy::physics
