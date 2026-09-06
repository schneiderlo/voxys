// ═══════════════════════════════════════════════════════════════════════════════
// character_controller.hpp - Character Controller System
// ═══════════════════════════════════════════════════════════════════════════════
// Provides a grounded character controller for first-person terrain exploration:
//   - Ground detection via heightmap sampling
//   - Gravity and jumping with state machine
//   - Slope handling with configurable max angle
//   - Terrain collision
// ═══════════════════════════════════════════════════════════════════════════════

#pragma once

#include "camera/camera.hpp"
#include "engine/platform/input.hpp"
#include "physics/physics_types.hpp"

#include <glm/glm.hpp>
#include <cmath>
#include <functional>
#include <limits>

namespace voxy {

namespace physics {
class PhysicsWorld;
}

namespace terrain {
    class Heightmap;
}

/// World-space distance covered by a row of height samples. There are
/// sampleCount - 1 intervals between sample centers. A one-sample heightmap is
/// constant, so keep a non-zero extent to make coordinate conversion defined.
[[nodiscard]] constexpr float terrainSampleExtent(uint32_t sampleCount,
                                                   float cellScale) noexcept {
    return static_cast<float>(sampleCount > 1 ? sampleCount - 1 : 1) * cellScale;
}

// ─────────────────────────────────────────────────────────────────────────────
// Character State
// ─────────────────────────────────────────────────────────────────────────────

/// Character movement state
enum class CharacterState {
    Grounded,   ///< On the ground, can move and jump
    Jumping,    ///< Rising from a jump
    Falling     ///< Falling through the air
};

/// Convert CharacterState to string
[[nodiscard]] const char* characterStateToString(CharacterState state) noexcept;

// ─────────────────────────────────────────────────────────────────────────────
// Character Controller Configuration
// ─────────────────────────────────────────────────────────────────────────────

/// Configuration for character controller
struct CharacterConfig {
    // Movement speeds
    float walkSpeed = 4.0f;           ///< Walking speed (units/sec)
    float runSpeed = 8.0f;            ///< Running speed when Shift held (units/sec)
    
    // Physics
    float gravity = 20.0f;            ///< Downward acceleration (units/sec²)
    float jumpHeight = 2.0f;          ///< Maximum jump height (units)
    float terminalVelocity = 50.0f;   ///< Maximum falling speed (units/sec)
    
    // Character dimensions
    float groundOffset = 1.8f;        ///< Eye height above terrain
    float collisionRadius = 0.4f;     ///< Collision capsule radius
    float collisionHeight = 1.8f;     ///< Full collision capsule height
    
    // Slope handling
    float maxSlopeAngle = 45.0f;      ///< Maximum walkable slope (degrees)
    float slopeSlideSpeed = 5.0f;     ///< Speed when sliding down steep slopes
    float maxStepDown = 2.0f;         ///< Max drop the ground-follow snaps down; larger drops start a fall
    
    // Mouse look
    float mouseSensitivity = 0.002f;  ///< Mouse look sensitivity (radians/pixel)
    bool invertY = false;             ///< Invert vertical mouse look
    
    // Terrain scale (must match terrain settings)
    float heightScale = 1.0f;         ///< Terrain height scale factor
    bool legoTerrain = false;
    float cellScale = 1.0f;           ///< World-space size per heightmap cell
    float terrainWidth = 255.0f;      ///< Distance between first/last X sample centers
    float terrainHeight = 255.0f;     ///< Distance between first/last Z sample centers
    
    /// Calculate jump velocity from jump height and gravity
    [[nodiscard]] float jumpVelocity() const noexcept {
        // v = sqrt(2 * g * h)
        const double squared = 2.0 * static_cast<double>(gravity)
                             * static_cast<double>(jumpHeight);
        if (!std::isfinite(squared) || squared < 0.0
            || squared > static_cast<double>(
                std::numeric_limits<float>::max())) {
            return 0.0f;
        }
        return static_cast<float>(std::sqrt(squared));
    }
    
    /// Create config with custom speeds
    static CharacterConfig withSpeed(float walk, float run) {
        CharacterConfig config;
        config.walkSpeed = walk;
        config.runSpeed = run;
        return config;
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// Character Controller
// ─────────────────────────────────────────────────────────────────────────────

/// Character controller for grounded first-person terrain navigation.
/// 
/// Controls:
///   - WASD: Move forward/backward/left/right
///   - Space: Jump
///   - Shift: Run
///   - Mouse (when captured): Look around
///   - Left Click: Capture mouse
///   - Escape: Release mouse
class CharacterController {
public:
    /// Height sampling callback type
    using HeightSampler = std::function<float(float worldX, float worldZ)>;
    
    // ─────────────────────────────────────────────────────────────────────────
    // Construction
    // ─────────────────────────────────────────────────────────────────────────
    
    /// Default constructor
    CharacterController() = default;
    ~CharacterController();

    CharacterController(const CharacterController&) = delete;
    CharacterController& operator=(const CharacterController&) = delete;
    CharacterController(CharacterController&&) = delete;
    CharacterController& operator=(CharacterController&&) = delete;
    
    /// Constructor with camera and heightmap
    CharacterController(Camera& camera, const terrain::Heightmap* heightmap,
                        const CharacterConfig& config = CharacterConfig{});
    
    // ─────────────────────────────────────────────────────────────────────────
    // Configuration
    // ─────────────────────────────────────────────────────────────────────────
    
    /// Get current configuration
    [[nodiscard]] const CharacterConfig& config() const noexcept { return config_; }
    
    /// Set configuration
    void setConfig(const CharacterConfig& config);
    
    /// Set heightmap for terrain sampling
    void setHeightmap(const terrain::Heightmap* heightmap) { heightmap_ = heightmap; }
    
    /// Set custom height sampler (overrides heightmap)
    void setHeightSampler(HeightSampler sampler) { heightSampler_ = std::move(sampler); }

    /// Use Jolt for character collision and movement.
    void attachPhysicsWorld(physics::PhysicsWorld& world);
    void detachPhysicsWorld();

    /// Teleport the Jolt character to the camera's current feet position.
    /// Call when switching from free-fly to character control.
    void syncPhysicsPosition();
    
    // ─────────────────────────────────────────────────────────────────────────
    // Camera Access
    // ─────────────────────────────────────────────────────────────────────────
    
    /// Attach to a camera
    void attachCamera(Camera& camera);
    
    /// Detach from camera
    void detachCamera();
    
    /// Check if camera is attached
    [[nodiscard]] bool hasCamera() const noexcept { return camera_ != nullptr; }
    
    /// Get attached camera (may be null)
    [[nodiscard]] Camera* camera() noexcept { return camera_; }
    [[nodiscard]] const Camera* camera() const noexcept { return camera_; }
    
    // ─────────────────────────────────────────────────────────────────────────
    // Update
    // ─────────────────────────────────────────────────────────────────────────
    
    /// Update controller state and apply to camera.
    /// @param deltaTime Time since last update (seconds)
    /// @param input Input state for this frame
    void update(float deltaTime, Input& input);
    
    // ─────────────────────────────────────────────────────────────────────────
    // State Queries
    // ─────────────────────────────────────────────────────────────────────────
    
    /// Get current character state
    [[nodiscard]] CharacterState state() const noexcept { return state_; }
    
    /// Check if grounded
    [[nodiscard]] bool isGrounded() const noexcept { return state_ == CharacterState::Grounded; }
    
    /// Check if in air (jumping or falling)
    [[nodiscard]] bool isInAir() const noexcept { return state_ != CharacterState::Grounded; }
    
    /// Get current velocity (world space)
    [[nodiscard]] const glm::vec3& velocity() const noexcept { return velocity_; }
    
    /// Get current position (feet position)
    [[nodiscard]] glm::vec3 feetPosition() const noexcept;
    [[nodiscard]] physics::WorldPosition feetWorldPosition() const noexcept;
    
    /// Get terrain height at current position
    [[nodiscard]] float terrainHeightAtPosition() const noexcept { return lastTerrainHeight_; }
    
    /// Get terrain normal at current position
    [[nodiscard]] const glm::vec3& terrainNormal() const noexcept { return terrainNormal_; }
    
    /// Check if standing on walkable slope
    [[nodiscard]] bool isOnWalkableSlope() const noexcept { return isWalkableSlope_; }
    
    // ─────────────────────────────────────────────────────────────────────────
    // Terrain Sampling
    // ─────────────────────────────────────────────────────────────────────────
    
    /// Sample terrain height at world coordinates
    [[nodiscard]] float sampleTerrainHeight(float worldX, float worldZ) const;
    
    /// Sample terrain normal at world coordinates
    [[nodiscard]] glm::vec3 sampleTerrainNormal(float worldX, float worldZ) const;
    
    /// Check if slope is walkable at world coordinates
    [[nodiscard]] bool canWalkOnSlope(const glm::vec3& normal) const;
    
private:
    // ─────────────────────────────────────────────────────────────────────────
    // Internal Methods
    // ─────────────────────────────────────────────────────────────────────────
    
    /// Process mouse look input
    void processMouseLook(const Input& input);
    
    /// Process movement input and compute desired velocity
    void processMovement(float deltaTime, const Input& input);
    
    /// Process mouse capture/release
    void processMouseCapture(Input& input);
    
    /// Apply gravity and physics
    void applyPhysics(float deltaTime);
    
    /// Handle ground collision and state transitions
    void handleGroundCollision(float deltaTime);

    /// Lazily create and advance the Jolt virtual character.
    [[nodiscard]] bool ensurePhysicsCharacter();
    void updatePhysicsCharacter(float deltaTime);
    
    /// Convert world coordinates to heightmap UV
    [[nodiscard]] glm::vec2 worldToHeightmapUV(float worldX, float worldZ) const;
    
    // ─────────────────────────────────────────────────────────────────────────
    // State
    // ─────────────────────────────────────────────────────────────────────────
    
    Camera* camera_ = nullptr;
    const terrain::Heightmap* heightmap_ = nullptr;
    HeightSampler heightSampler_;
    CharacterConfig config_;
    physics::PhysicsWorld* physicsWorld_ = nullptr;
    uint32_t physicsCharacter_ = 0;
    
    // Physics state
    CharacterState state_ = CharacterState::Falling;
    glm::vec3 velocity_{0.0f};
    
    // Cached terrain data
    float lastTerrainHeight_ = 0.0f;
    glm::vec3 terrainNormal_{0.0f, 1.0f, 0.0f};
    bool isWalkableSlope_ = true;
    bool jumpRequested_ = false;
};

// ─────────────────────────────────────────────────────────────────────────────
// Inline Implementations
// ─────────────────────────────────────────────────────────────────────────────

inline const char* characterStateToString(CharacterState state) noexcept {
    switch (state) {
        case CharacterState::Grounded: return "Grounded";
        case CharacterState::Jumping:  return "Jumping";
        case CharacterState::Falling:  return "Falling";
    }
    return "Unknown";
}

inline glm::vec3 CharacterController::feetPosition() const noexcept {
    if (!camera_) return glm::vec3{0.0f};
    glm::vec3 pos = camera_->position();
    pos.y -= config_.groundOffset;
    return pos;
}

inline physics::WorldPosition
CharacterController::feetWorldPosition() const noexcept {
    return physics::canonicalWorldPosition(
        camera_ ? camera_->worldSector() : glm::ivec3(0),
        glm::dvec3(feetPosition()));
}

} // namespace voxy
