// Cross-platform Jolt Physics integration.
//
// Jolt stays behind this PIMPL boundary so most of voxy does not need to parse
// its large header set. The implementation deliberately uses the same
// single-threaded scheduler and terrain tiling policy on native and WASM.

#pragma once

#include <glm/vec3.hpp>

#include <cstdint>
#include <memory>
#include <span>

namespace voxy::physics {

class PhysicsWorld {
public:
    using CharacterHandle = uint32_t;
    static constexpr CharacterHandle InvalidCharacter = 0;

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

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace voxy::physics
