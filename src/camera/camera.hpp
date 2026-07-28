// ═══════════════════════════════════════════════════════════════════════════════
// camera.hpp - Camera System (C++20)
// ═══════════════════════════════════════════════════════════════════════════════
// Implements camera state management and matrix computation for voxy.
// Features:
//   - Camera state (position, orientation, FOV)
//   - View matrix computation via lookAt
//   - Projection matrix computation via perspective
//   - Inverse matrices for ray generation
//   - Yaw/pitch orientation system
// ═══════════════════════════════════════════════════════════════════════════════

#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>

#include <cmath>

namespace voxy {

// ─────────────────────────────────────────────────────────────────────────────
// Camera Configuration
// ─────────────────────────────────────────────────────────────────────────────

/// Configuration parameters for camera behavior
struct CameraConfig {
    float fovY = glm::radians(60.0f);    ///< Vertical field of view (radians)
    float nearPlane = 0.1f;               ///< Near clip plane distance
    float farPlane = 10000.0f;            ///< Far clip plane distance
    float aspectRatio = 16.0f / 9.0f;     ///< Width / Height

    /// Create config with specific FOV in degrees
    static CameraConfig withFovDegrees(float fovDegrees) {
        CameraConfig config;
        config.fovY = glm::radians(fovDegrees);
        return config;
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// Camera Class
// ─────────────────────────────────────────────────────────────────────────────

/// Camera with position, orientation, and matrix computation.
/// Uses yaw/pitch angles for orientation control.
class Camera {
public:
    // ─────────────────────────────────────────────────────────────────────────
    // Construction
    // ─────────────────────────────────────────────────────────────────────────

    /// Default constructor - camera at origin looking along -Z
    Camera();

    /// Constructor with position and config
    explicit Camera(const glm::vec3& position, const CameraConfig& config = CameraConfig{});

    /// Constructor with position and look-at target
    Camera(const glm::vec3& position, const glm::vec3& target, 
           const CameraConfig& config = CameraConfig{});

    // ─────────────────────────────────────────────────────────────────────────
    // Position
    // ─────────────────────────────────────────────────────────────────────────

    /// Get camera world-space position
    [[nodiscard]] const glm::vec3& position() const noexcept { return position_; }

    /// Integer sector containing the local camera position. Matrices continue
    /// to use position() so all render math remains camera-near f32.
    [[nodiscard]] const glm::ivec3& worldSector() const noexcept {
        return worldSector_;
    }

    void setWorldSector(const glm::ivec3& sector) noexcept {
        worldSector_ = sector;
    }

    void setWorldPosition(const glm::ivec3& sector, const glm::vec3& local) {
        if (isFinite(local)) {
            worldSector_ = sector;
            setPosition(local);
        }
    }

    /// Set camera world-space position
    void setPosition(const glm::vec3& pos);

    /// Move camera by offset in world space
    void move(const glm::vec3& offset);

    /// Move camera by offset in local space (forward/right/up)
    void moveLocal(const glm::vec3& offset);

    // ─────────────────────────────────────────────────────────────────────────
    // Orientation
    // ─────────────────────────────────────────────────────────────────────────

    /// Get yaw angle (horizontal rotation, radians)
    [[nodiscard]] float yaw() const noexcept { return yaw_; }

    /// Get pitch angle (vertical rotation, radians)
    [[nodiscard]] float pitch() const noexcept { return pitch_; }

    /// Set yaw angle (radians). Will not clamp.
    void setYaw(float yaw);

    /// Set pitch angle (radians). Clamped to (-PI/2, PI/2) to prevent gimbal lock.
    void setPitch(float pitch);

    /// Add to yaw angle (radians)
    void rotate(float deltaYaw, float deltaPitch);

    /// Make camera look at a target position
    void lookAt(const glm::vec3& target);

    /// Get normalized forward direction
    [[nodiscard]] const glm::vec3& forward() const noexcept { return forward_; }

    /// Get normalized right direction
    [[nodiscard]] const glm::vec3& right() const noexcept { return right_; }

    /// Get normalized up direction
    [[nodiscard]] const glm::vec3& up() const noexcept { return up_; }

    // ─────────────────────────────────────────────────────────────────────────
    // Projection Parameters
    // ─────────────────────────────────────────────────────────────────────────

    /// Get vertical field of view (radians)
    [[nodiscard]] float fovY() const noexcept { return fovY_; }

    /// Set vertical field of view (radians)
    void setFovY(float fov);

    /// Set vertical field of view (degrees)
    void setFovYDegrees(float fovDegrees);

    /// Get aspect ratio (width / height)
    [[nodiscard]] float aspectRatio() const noexcept { return aspectRatio_; }

    /// Set aspect ratio (width / height)
    void setAspectRatio(float aspect);

    /// Set aspect ratio from dimensions
    void setAspectRatio(uint32_t width, uint32_t height);

    /// Get near clip plane distance
    [[nodiscard]] float nearPlane() const noexcept { return nearPlane_; }

    /// Get far clip plane distance
    [[nodiscard]] float farPlane() const noexcept { return farPlane_; }

    /// Set near and far clip planes
    void setClipPlanes(float nearPlane, float farPlane);

    // ─────────────────────────────────────────────────────────────────────────
    // Matrices
    // ─────────────────────────────────────────────────────────────────────────

    /// Get view matrix (world -> view space)
    [[nodiscard]] const glm::mat4& viewMatrix() const;

    /// Get projection matrix (view -> clip space)
    [[nodiscard]] const glm::mat4& projectionMatrix() const;

    /// Get combined view-projection matrix
    [[nodiscard]] const glm::mat4& viewProjectionMatrix() const;

    /// Get inverse view matrix (view -> world space)
    [[nodiscard]] const glm::mat4& inverseViewMatrix() const;

    /// Get inverse projection matrix (clip -> view space)
    [[nodiscard]] const glm::mat4& inverseProjectionMatrix() const;

    /// Get inverse view-projection matrix (clip -> world space)
    [[nodiscard]] const glm::mat4& inverseViewProjectionMatrix() const;

    // ─────────────────────────────────────────────────────────────────────────
    // Utility
    // ─────────────────────────────────────────────────────────────────────────

    /// Force immediate recalculation of all matrices
    void updateMatrices() const;

    /// Get a ray direction for a screen coordinate (normalized device coordinates)
    /// @param ndc Normalized device coordinates (-1 to 1)
    /// @return World-space ray direction (normalized)
    [[nodiscard]] glm::vec3 screenToWorldRay(const glm::vec2& ndc) const;

private:
    // ─────────────────────────────────────────────────────────────────────────
    // Internal Methods
    // ─────────────────────────────────────────────────────────────────────────

    /// Recompute forward/right/up from yaw and pitch
    void updateOrientation();

    /// Mark view-related matrices as dirty
    void markViewDirty() const;

    /// Mark projection-related matrices as dirty
    void markProjectionDirty() const;

    /// Ensure view matrix is up to date
    void ensureViewMatrix() const;

    /// Ensure projection matrix is up to date
    void ensureProjectionMatrix() const;

    /// Ensure view-projection matrix is up to date
    void ensureViewProjectionMatrix() const;

    [[nodiscard]] static bool isFinite(const glm::vec2& value) noexcept;
    [[nodiscard]] static bool isFinite(const glm::vec3& value) noexcept;
    [[nodiscard]] static float canonicalYaw(float yaw) noexcept;

    // ─────────────────────────────────────────────────────────────────────────
    // State
    // ─────────────────────────────────────────────────────────────────────────

    // Position
    glm::vec3 position_{0.0f, 0.0f, 0.0f};
    glm::ivec3 worldSector_{0};

    // Orientation (Euler angles in radians)
    float yaw_ = 0.0f;    // Horizontal rotation (0 = looking along +Z)
    float pitch_ = 0.0f;  // Vertical rotation (0 = horizontal)

    // Direction vectors (derived from yaw/pitch)
    glm::vec3 forward_{0.0f, 0.0f, 1.0f};
    glm::vec3 right_{1.0f, 0.0f, 0.0f};
    glm::vec3 up_{0.0f, 1.0f, 0.0f};

    // Projection parameters
    float fovY_ = glm::radians(60.0f);
    float aspectRatio_ = 16.0f / 9.0f;
    float nearPlane_ = 0.1f;
    float farPlane_ = 10000.0f;

    // Cached matrices (mutable for lazy computation)
    mutable glm::mat4 viewMatrix_{1.0f};
    mutable glm::mat4 projectionMatrix_{1.0f};
    mutable glm::mat4 viewProjectionMatrix_{1.0f};
    mutable glm::mat4 inverseViewMatrix_{1.0f};
    mutable glm::mat4 inverseProjectionMatrix_{1.0f};
    mutable glm::mat4 inverseViewProjectionMatrix_{1.0f};

    // Dirty flags for lazy matrix computation
    mutable bool viewDirty_ = true;
    mutable bool projectionDirty_ = true;
    mutable bool viewProjectionDirty_ = true;
};

// ─────────────────────────────────────────────────────────────────────────────
// Inline Implementations
// ─────────────────────────────────────────────────────────────────────────────

inline void Camera::setPosition(const glm::vec3& pos) {
    if (!isFinite(pos)) return;
    position_ = pos;
    markViewDirty();
}

inline void Camera::move(const glm::vec3& offset) {
    if (!isFinite(offset)) return;
    const glm::vec3 next = position_ + offset;
    if (!isFinite(next)) return;
    position_ = next;
    markViewDirty();
}

inline void Camera::moveLocal(const glm::vec3& offset) {
    if (!isFinite(offset)) return;
    const glm::vec3 next =
        position_ + right_ * offset.x + up_ * offset.y + forward_ * offset.z;
    if (!isFinite(next)) return;
    position_ = next;
    markViewDirty();
}

inline void Camera::setYaw(float yaw) {
    if (!std::isfinite(yaw)) return;
    yaw_ = canonicalYaw(yaw);
    updateOrientation();
    markViewDirty();
}

inline void Camera::setPitch(float pitch) {
    if (!std::isfinite(pitch)) return;
    // Clamp pitch to avoid gimbal lock
    constexpr float maxPitch = glm::half_pi<float>() - 0.01f;
    pitch_ = glm::clamp(pitch, -maxPitch, maxPitch);
    updateOrientation();
    markViewDirty();
}

inline void Camera::rotate(float deltaYaw, float deltaPitch) {
    if (!std::isfinite(deltaYaw) || !std::isfinite(deltaPitch)) return;
    yaw_ = canonicalYaw(yaw_ + canonicalYaw(deltaYaw));
    constexpr float maxPitch = glm::half_pi<float>() - 0.01f;
    pitch_ = glm::clamp(pitch_ + deltaPitch, -maxPitch, maxPitch);
    updateOrientation();
    markViewDirty();
}

inline void Camera::setFovY(float fov) {
    if (!std::isfinite(fov) || fov <= 0.0f || fov >= glm::pi<float>()) {
        return;
    }
    fovY_ = fov;
    markProjectionDirty();
}

inline void Camera::setFovYDegrees(float fovDegrees) {
    setFovY(glm::radians(fovDegrees));
}

inline void Camera::setAspectRatio(float aspect) {
    if (!std::isfinite(aspect) || aspect <= 0.0f) return;
    aspectRatio_ = aspect;
    markProjectionDirty();
}

inline void Camera::setAspectRatio(uint32_t width, uint32_t height) {
    if (width > 0 && height > 0) {
        aspectRatio_ = static_cast<float>(width) / static_cast<float>(height);
        markProjectionDirty();
    }
}

inline void Camera::setClipPlanes(float nearPlane, float farPlane) {
    if (!std::isfinite(nearPlane) || !std::isfinite(farPlane)
        || nearPlane <= 0.0f || farPlane <= nearPlane) {
        return;
    }
    nearPlane_ = nearPlane;
    farPlane_ = farPlane;
    markProjectionDirty();
}

inline void Camera::markViewDirty() const {
    viewDirty_ = true;
    viewProjectionDirty_ = true;
}

inline void Camera::markProjectionDirty() const {
    projectionDirty_ = true;
    viewProjectionDirty_ = true;
}

} // namespace voxy

