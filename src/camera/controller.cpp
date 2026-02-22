// ═══════════════════════════════════════════════════════════════════════════════
// controller.cpp - Camera Controller Implementation
// ═══════════════════════════════════════════════════════════════════════════════

#include "camera/controller.hpp"
#include "core/log.hpp"

#include <glm/geometric.hpp>

namespace voxy {

// ═══════════════════════════════════════════════════════════════════════════════
// Update
// ═══════════════════════════════════════════════════════════════════════════════

void FreeFlyController::update(float deltaTime, Input& input) {
    if (!camera_) {
        return;
    }
    
    // Handle mouse capture/release
    processMouseCapture(input);
    
    // Process mouse look (only when captured)
    processMouseLook(input);
    
    // Process keyboard movement
    processMovement(deltaTime, input);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Mouse Capture
// ═══════════════════════════════════════════════════════════════════════════════

void FreeFlyController::processMouseCapture(Input& input) {
    // Left click to capture mouse
    if (input.wasMouseButtonPressed(MouseButton::Left)) {
        if (!input.isMouseCaptured()) {
            input.captureMouse();
        }
    }
    
    // Escape to release mouse
    if (input.wasKeyPressed(Key::Escape) && input.isMouseCaptured()) {
        input.releaseMouse();
    }
}

// ═══════════════════════════════════════════════════════════════════════════════
// Mouse Look
// ═══════════════════════════════════════════════════════════════════════════════

void FreeFlyController::processMouseLook(const Input& input) {
    if (!input.isMouseCaptured()) {
        return;
    }
    
    glm::vec2 delta = input.mouseDelta();
    
    if (delta.x == 0.0f && delta.y == 0.0f) {
        return;
    }
    
    
    // Apply sensitivity
    float deltaYaw = delta.x * config_.mouseSensitivity;
    float deltaPitch = delta.y * config_.mouseSensitivity;
    
    // Invert Y if configured
    if (config_.invertY) {
        deltaPitch = -deltaPitch;
    }
    
    // Apply rotation to camera
    camera_->rotate(deltaYaw, -deltaPitch);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Keyboard Movement
// ═══════════════════════════════════════════════════════════════════════════════

void FreeFlyController::processMovement(float deltaTime, const Input& input) {
    // Calculate movement direction in local space
    glm::vec3 moveDir{0.0f};
    
    // Forward/Backward (along camera forward direction)
    if (input.isKeyDown(Key::W)) {
        moveDir += camera_->forward();
    }
    if (input.isKeyDown(Key::S)) {
        moveDir -= camera_->forward();
    }
    
    // Left/Right (along camera right direction)
    if (input.isKeyDown(Key::A)) {
        moveDir -= camera_->right();
    }
    if (input.isKeyDown(Key::D)) {
        moveDir += camera_->right();
    }
    
    // Up/Down (world Y axis for consistent up/down movement)
    constexpr glm::vec3 worldUp{0.0f, 1.0f, 0.0f};
    
    if (input.isKeyDown(Key::E) || input.isKeyDown(Key::Space)) {
        moveDir += worldUp;
    }
    if (input.isKeyDown(Key::Q) || input.isKeyDown(Key::Ctrl)) {
        moveDir -= worldUp;
    }
    
    // Check if moving
    float dirLength = glm::length(moveDir);
    isMoving_ = dirLength > 0.0001f;
    
    if (!isMoving_) {
        velocity_ = glm::vec3{0.0f};
        currentSpeed_ = 0.0f;
        return;
    }
    
    // Normalize direction
    moveDir /= dirLength;
    
    // Calculate speed (with boost)
    currentSpeed_ = config_.baseSpeed;
    if (input.isKeyDown(Key::Shift)) {
        currentSpeed_ *= config_.boostMultiplier;
    }
    
    // Calculate velocity and apply to camera
    velocity_ = moveDir * currentSpeed_ * deltaTime;
    camera_->move(velocity_);
}

} // namespace voxy


