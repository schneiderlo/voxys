// ═══════════════════════════════════════════════════════════════════════════════
// controller.hpp - Camera Controller System
// ═══════════════════════════════════════════════════════════════════════════════
// Provides camera controllers for user interaction:
//   - FreeFlyController: WASD movement + mouse look for free exploration
// ═══════════════════════════════════════════════════════════════════════════════

#pragma once

#include "camera/camera.hpp"
#include "engine/platform/input.hpp"

namespace voxy {

// ─────────────────────────────────────────────────────────────────────────────
// Controller Configuration
// ─────────────────────────────────────────────────────────────────────────────

/// Configuration for free-fly camera controller
struct FreeFlyConfig {
    float baseSpeed = 50.0f;           ///< Base movement speed (units/second)
    float boostMultiplier = 5.0f;      ///< Speed multiplier when Shift is held
    float mouseSensitivity = 0.002f;   ///< Mouse look sensitivity (radians/pixel)
    bool invertY = false;              ///< Invert vertical mouse look
    
    /// Create config with custom speed
    static FreeFlyConfig withSpeed(float speed) {
        FreeFlyConfig config;
        config.baseSpeed = speed;
        return config;
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// Free-Fly Camera Controller
// ─────────────────────────────────────────────────────────────────────────────

/// Free-fly camera controller for unrestricted 3D navigation.
/// 
/// Controls:
///   - WASD: Move forward/backward/left/right
///   - E/Space: Move up
///   - Q/Ctrl: Move down
///   - Mouse (when captured): Look around
///   - Shift: Speed boost
///   - Left Click: Capture mouse
///   - Escape: Release mouse
class FreeFlyController {
public:
    // ─────────────────────────────────────────────────────────────────────────
    // Construction
    // ─────────────────────────────────────────────────────────────────────────
    
    /// Default constructor
    FreeFlyController() = default;
    
    /// Constructor with camera reference
    explicit FreeFlyController(Camera& camera, const FreeFlyConfig& config = FreeFlyConfig{});
    
    // ─────────────────────────────────────────────────────────────────────────
    // Configuration
    // ─────────────────────────────────────────────────────────────────────────
    
    /// Get current configuration
    [[nodiscard]] const FreeFlyConfig& config() const noexcept { return config_; }
    
    /// Set configuration
    void setConfig(const FreeFlyConfig& config) { config_ = config; }
    
    /// Get base movement speed
    [[nodiscard]] float baseSpeed() const noexcept { return config_.baseSpeed; }
    
    /// Set base movement speed
    void setBaseSpeed(float speed) { config_.baseSpeed = speed; }
    
    /// Get boost multiplier
    [[nodiscard]] float boostMultiplier() const noexcept { return config_.boostMultiplier; }
    
    /// Set boost multiplier
    void setBoostMultiplier(float multiplier) { config_.boostMultiplier = multiplier; }
    
    /// Get mouse sensitivity
    [[nodiscard]] float mouseSensitivity() const noexcept { return config_.mouseSensitivity; }
    
    /// Set mouse sensitivity
    void setMouseSensitivity(float sensitivity) { config_.mouseSensitivity = sensitivity; }
    
    /// Get invert Y setting
    [[nodiscard]] bool invertY() const noexcept { return config_.invertY; }
    
    /// Set invert Y
    void setInvertY(bool invert) { config_.invertY = invert; }
    
    // ─────────────────────────────────────────────────────────────────────────
    // Camera Access
    // ─────────────────────────────────────────────────────────────────────────
    
    /// Attach to a camera
    void attachCamera(Camera& camera) { camera_ = &camera; }
    
    /// Detach from camera
    void detachCamera() { camera_ = nullptr; }
    
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
    
    /// Check if currently moving
    [[nodiscard]] bool isMoving() const noexcept { return isMoving_; }
    
    /// Get current effective speed (including boost if active)
    [[nodiscard]] float currentSpeed() const noexcept { return currentSpeed_; }
    
    /// Get velocity from last update (world space)
    [[nodiscard]] const glm::vec3& velocity() const noexcept { return velocity_; }
    
private:
    // ─────────────────────────────────────────────────────────────────────────
    // Internal Methods
    // ─────────────────────────────────────────────────────────────────────────
    
    /// Process mouse look input
    void processMouseLook(const Input& input);
    
    /// Process keyboard movement input
    void processMovement(float deltaTime, const Input& input);
    
    /// Process mouse capture/release
    void processMouseCapture(Input& input);
    
    // ─────────────────────────────────────────────────────────────────────────
    // State
    // ─────────────────────────────────────────────────────────────────────────
    
    Camera* camera_ = nullptr;
    FreeFlyConfig config_;
    
    // Movement state
    glm::vec3 velocity_{0.0f};
    float currentSpeed_ = 0.0f;
    bool isMoving_ = false;
};

// ─────────────────────────────────────────────────────────────────────────────
// Inline Implementations
// ─────────────────────────────────────────────────────────────────────────────

inline FreeFlyController::FreeFlyController(Camera& camera, const FreeFlyConfig& config)
    : camera_(&camera)
    , config_(config)
{
}

} // namespace voxy

