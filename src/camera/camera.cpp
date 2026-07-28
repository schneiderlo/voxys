// ═══════════════════════════════════════════════════════════════════════════════
// camera.cpp - Camera System Implementation
// ═══════════════════════════════════════════════════════════════════════════════

#include "camera/camera.hpp"

#include <glm/gtc/matrix_transform.hpp>
#include <cmath>

namespace voxy {

// ═══════════════════════════════════════════════════════════════════════════════
// Construction
// ═══════════════════════════════════════════════════════════════════════════════

Camera::Camera() {
    updateOrientation();
}

Camera::Camera(const glm::vec3& position, const CameraConfig& config)
    : Camera() {
    setPosition(position);
    setFovY(config.fovY);
    setAspectRatio(config.aspectRatio);
    setClipPlanes(config.nearPlane, config.farPlane);
}

Camera::Camera(const glm::vec3& position, const glm::vec3& target, const CameraConfig& config)
    : Camera(position, config) {
    lookAt(target);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Orientation
// ═══════════════════════════════════════════════════════════════════════════════

bool Camera::isFinite(const glm::vec2& value) noexcept {
    return std::isfinite(value.x) && std::isfinite(value.y);
}

bool Camera::isFinite(const glm::vec3& value) noexcept {
    return std::isfinite(value.x)
        && std::isfinite(value.y)
        && std::isfinite(value.z);
}

float Camera::canonicalYaw(float yaw) noexcept {
    return std::remainder(yaw, glm::two_pi<float>());
}

void Camera::updateOrientation() {
    // Compute forward vector from yaw and pitch
    // Convention: yaw=0 looks along +Z, yaw=PI/2 looks along +X
    // pitch=0 is horizontal, pitch>0 looks up
    forward_.x = std::cos(pitch_) * std::sin(yaw_);
    forward_.y = std::sin(pitch_);
    forward_.z = std::cos(pitch_) * std::cos(yaw_);
    forward_ = glm::normalize(forward_);

    // Compute right vector (cross product with world up)
    // Note: We use world up (0,1,0) which works for most camera orientations
    constexpr glm::vec3 worldUp{0.0f, 1.0f, 0.0f};
    right_ = glm::normalize(glm::cross(worldUp, forward_));

    // Compute camera up vector (perpendicular to forward and right)
    up_ = glm::normalize(glm::cross(forward_, right_));
}

void Camera::lookAt(const glm::vec3& target) {
    if (!isFinite(target)) return;
    glm::vec3 direction = target - position_;
    float length = glm::length(direction);
    
    if (!std::isfinite(length) || length < 0.0001f) {
        // Target is too close to camera position
        return;
    }
    
    direction /= length;  // Normalize
    
    // Extract yaw from direction
    // yaw = atan2(x, z) gives angle from +Z axis
    yaw_ = std::atan2(direction.x, direction.z);
    
    // Extract pitch from direction
    // pitch = asin(y) gives angle from horizontal plane
    pitch_ = std::asin(glm::clamp(direction.y, -1.0f, 1.0f));
    
    // Clamp pitch to avoid gimbal lock
    constexpr float maxPitch = glm::half_pi<float>() - 0.01f;
    pitch_ = glm::clamp(pitch_, -maxPitch, maxPitch);
    
    updateOrientation();
    markViewDirty();
}

// ═══════════════════════════════════════════════════════════════════════════════
// Matrix Computation
// ═══════════════════════════════════════════════════════════════════════════════

void Camera::ensureViewMatrix() const {
    if (!viewDirty_) return;
    
    // Compute view matrix using lookAt
    glm::vec3 target = position_ + forward_;
    viewMatrix_ = glm::lookAt(position_, target, up_);
    inverseViewMatrix_ = glm::inverse(viewMatrix_);
    
    viewDirty_ = false;
}

void Camera::ensureProjectionMatrix() const {
    if (!projectionDirty_) return;
    
    // Compute perspective projection matrix
    projectionMatrix_ = glm::perspective(fovY_, aspectRatio_, nearPlane_, farPlane_);
    inverseProjectionMatrix_ = glm::inverse(projectionMatrix_);
    
    projectionDirty_ = false;
}

void Camera::ensureViewProjectionMatrix() const {
    if (!viewProjectionDirty_) return;
    
    ensureViewMatrix();
    ensureProjectionMatrix();
    
    viewProjectionMatrix_ = projectionMatrix_ * viewMatrix_;
    inverseViewProjectionMatrix_ = glm::inverse(viewProjectionMatrix_);
    
    viewProjectionDirty_ = false;
}

const glm::mat4& Camera::viewMatrix() const {
    ensureViewMatrix();
    return viewMatrix_;
}

const glm::mat4& Camera::projectionMatrix() const {
    ensureProjectionMatrix();
    return projectionMatrix_;
}

const glm::mat4& Camera::viewProjectionMatrix() const {
    ensureViewProjectionMatrix();
    return viewProjectionMatrix_;
}

const glm::mat4& Camera::inverseViewMatrix() const {
    ensureViewMatrix();
    return inverseViewMatrix_;
}

const glm::mat4& Camera::inverseProjectionMatrix() const {
    ensureProjectionMatrix();
    return inverseProjectionMatrix_;
}

const glm::mat4& Camera::inverseViewProjectionMatrix() const {
    ensureViewProjectionMatrix();
    return inverseViewProjectionMatrix_;
}

void Camera::updateMatrices() const {
    ensureViewProjectionMatrix();
}

// ═══════════════════════════════════════════════════════════════════════════════
// Utility
// ═══════════════════════════════════════════════════════════════════════════════

glm::vec3 Camera::screenToWorldRay(const glm::vec2& ndc) const {
    if (!isFinite(ndc)) return forward_;

    // Convert NDC to clip space (z=1 for a point on the far plane direction)
    glm::vec4 clipPos{ndc.x, ndc.y, 1.0f, 1.0f};
    
    // Transform to world space
    glm::vec4 worldPos = inverseViewProjectionMatrix() * clipPos;
    
    // Perspective divide
    if (!std::isfinite(worldPos.w) || std::abs(worldPos.w) < 1.0e-8f) {
        return forward_;
    }
    worldPos /= worldPos.w;
    
    // Compute direction from camera position
    glm::vec3 rayDir = glm::vec3(worldPos) - position_;
    const float rayLength = glm::length(rayDir);
    if (!isFinite(rayDir) || !std::isfinite(rayLength)
        || rayLength < 1.0e-8f) {
        return forward_;
    }
    return rayDir / rayLength;
}

} // namespace voxy



