// ═══════════════════════════════════════════════════════════════════════════════
// character_controller.cpp - Character Controller Implementation
// ═══════════════════════════════════════════════════════════════════════════════

#include "camera/character_controller.hpp"
#include "terrain/heightmap.hpp"
#include "core/log.hpp"

#include <glm/geometric.hpp>
#include <glm/gtc/constants.hpp>
#include <cmath>

namespace voxy {

// ═══════════════════════════════════════════════════════════════════════════════
// Construction
// ═══════════════════════════════════════════════════════════════════════════════

CharacterController::CharacterController(Camera& camera, const terrain::Heightmap* heightmap,
                                         const CharacterConfig& config)
    : camera_(&camera)
    , heightmap_(heightmap)
    , config_(config)
{
    // Initialize on terrain
    if (camera_) {
        const auto pos = camera_->position();
        lastTerrainHeight_ = sampleTerrainHeight(pos.x, pos.z);
        terrainNormal_ = sampleTerrainNormal(pos.x, pos.z);
        isWalkableSlope_ = canWalkOnSlope(terrainNormal_);
    }
}

// ═══════════════════════════════════════════════════════════════════════════════
// Update
// ═══════════════════════════════════════════════════════════════════════════════

void CharacterController::update(float deltaTime, Input& input) {
    if (!camera_) {
        return;
    }
    
    // Handle mouse capture/release
    processMouseCapture(input);
    
    // Process mouse look (only when captured)
    processMouseLook(input);
    
    // Process keyboard movement
    processMovement(deltaTime, input);
    
    // Apply physics (gravity)
    applyPhysics(deltaTime);
    
    // Handle ground collision
    handleGroundCollision(deltaTime);
    
}

// ═══════════════════════════════════════════════════════════════════════════════
// Mouse Capture
// ═══════════════════════════════════════════════════════════════════════════════

void CharacterController::processMouseCapture(Input& input) {
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

void CharacterController::processMouseLook(const Input& input) {
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
// Movement
// ═══════════════════════════════════════════════════════════════════════════════

void CharacterController::processMovement(float deltaTime, const Input& input) {
    // Calculate movement direction from input (horizontal plane only)
    glm::vec3 forward = camera_->forward();
    glm::vec3 right = camera_->right();
    
    // Project onto horizontal plane for ground movement
    forward.y = 0.0f;
    right.y = 0.0f;
    
    if (glm::length(forward) > 0.001f) {
        forward = glm::normalize(forward);
    }
    if (glm::length(right) > 0.001f) {
        right = glm::normalize(right);
    }
    
    glm::vec3 moveDir{0.0f};
    
    // Forward/Backward
    if (input.isKeyDown(Key::W)) {
        moveDir += forward;
    }
    if (input.isKeyDown(Key::S)) {
        moveDir -= forward;
    }
    
    // Left/Right (strafe)
    if (input.isKeyDown(Key::A)) {
        moveDir -= right;
    }
    if (input.isKeyDown(Key::D)) {
        moveDir += right;
    }
    
    // Normalize direction if moving
    float dirLength = glm::length(moveDir);
    if (dirLength > 0.001f) {
        moveDir /= dirLength;
    }
    
    // Calculate speed (walk/run)
    float speed = config_.walkSpeed;
    if (input.isKeyDown(Key::Shift)) {
        speed = config_.runSpeed;
    }
    
    // Apply movement based on state
    if (state_ == CharacterState::Grounded) {
        // Calculate target velocity - allow unrestricted movement on any terrain
        glm::vec3 targetVel = moveDir * speed;
        velocity_.x = targetVel.x;
        velocity_.z = targetVel.z;
    } else {
        // Reduced air control
        const float airControl = 0.3f;
        velocity_.x += moveDir.x * speed * airControl * deltaTime;
        velocity_.z += moveDir.z * speed * airControl * deltaTime;
        
        // Clamp horizontal air velocity
        glm::vec2 horizVel{velocity_.x, velocity_.z};
        float horizSpeed = glm::length(horizVel);
        if (horizSpeed > speed) {
            horizVel = glm::normalize(horizVel) * speed;
            velocity_.x = horizVel.x;
            velocity_.z = horizVel.y;
        }
    }
    
    // Jump (only when grounded and on walkable slope)
    if (input.wasKeyPressed(Key::Space) && state_ == CharacterState::Grounded && isWalkableSlope_) {
        velocity_.y = config_.jumpVelocity();
        state_ = CharacterState::Jumping;
        LOG_INFO("Jump! velocity.y = {:.1f}", velocity_.y);
    }
}

// ═══════════════════════════════════════════════════════════════════════════════
// Physics
// ═══════════════════════════════════════════════════════════════════════════════

void CharacterController::applyPhysics(float deltaTime) {
    // Apply gravity when not grounded
    if (state_ != CharacterState::Grounded) {
        velocity_.y -= config_.gravity * deltaTime;
        
        // Clamp to terminal velocity
        if (velocity_.y < -config_.terminalVelocity) {
            velocity_.y = -config_.terminalVelocity;
        }
        
        // Transition from jumping to falling
        if (state_ == CharacterState::Jumping && velocity_.y <= 0.0f) {
            state_ = CharacterState::Falling;
        }
    }
    
    // Apply velocity to position
    glm::vec3 pos = camera_->position();
    pos += velocity_ * deltaTime;
    camera_->setPosition(pos);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Ground Collision
// ═══════════════════════════════════════════════════════════════════════════════

void CharacterController::handleGroundCollision(float deltaTime) {
    (void)deltaTime;  // Currently unused, but may be needed for smoothing
    
    if (!camera_) return;
    
    const glm::vec3 pos = camera_->position();
    
    // Sample terrain at current position
    lastTerrainHeight_ = sampleTerrainHeight(pos.x, pos.z);
    terrainNormal_ = sampleTerrainNormal(pos.x, pos.z);

    // Update walkable state based on normal
    isWalkableSlope_ = canWalkOnSlope(terrainNormal_);
    
    // Calculate expected foot position
    const float feetY = pos.y - config_.groundOffset;
    const float heightDiff = lastTerrainHeight_ - feetY;

    // Step Offset Logic:
    // If the step is small enough (e.g. < 0.5 units), we consider it "walkable"
    // regardless of the slope normal at the exact sampling point.
    // This helps traversing small bumps or stairs.
    const float stepOffset = 0.5f;
    if (heightDiff > 0.0f && heightDiff <= stepOffset && state_ == CharacterState::Grounded) {
         // Override slope check for small steps
         isWalkableSlope_ = true;
    }
    
    // Check if feet are below terrain
    if (feetY <= lastTerrainHeight_) {
        // Snap to ground
        glm::vec3 newPos = pos;
        newPos.y = lastTerrainHeight_ + config_.groundOffset;
        camera_->setPosition(newPos);
        
        // Land if we were in the air
        if (state_ != CharacterState::Grounded) {
            state_ = CharacterState::Grounded;
            velocity_.y = 0.0f;
            LOG_INFO("Character landed at terrain height {:.1f}, camera Y = {:.1f}", 
                     lastTerrainHeight_, newPos.y);
        }
    } else if (state_ == CharacterState::Grounded) {
        // When grounded, always follow terrain height (both up and down)
        // This allows unrestricted terrain traversal without getting stuck
        glm::vec3 newPos = pos;
        newPos.y = lastTerrainHeight_ + config_.groundOffset;
        camera_->setPosition(newPos);
    }
}

// ═══════════════════════════════════════════════════════════════════════════════
// Terrain Sampling
// ═══════════════════════════════════════════════════════════════════════════════

glm::vec2 CharacterController::worldToHeightmapUV(float worldX, float worldZ) const {
    // Convert world coordinates to UV in [0, 1]
    // Assumes terrain is centered at origin
    const float halfWidth = config_.terrainWidth * 0.5f;
    const float halfHeight = config_.terrainHeight * 0.5f;
    
    float u = (worldX + halfWidth) / config_.terrainWidth;
    float v = (worldZ + halfHeight) / config_.terrainHeight;
    
    // Clamp to valid range
    u = glm::clamp(u, 0.0f, 1.0f);
    v = glm::clamp(v, 0.0f, 1.0f);
    
    return {u, v};
}

float CharacterController::sampleTerrainHeight(float worldX, float worldZ) const {
    // Use custom sampler if provided
    if (heightSampler_) {
        return heightSampler_(worldX, worldZ);
    }
    
    // Use heightmap if available
    if (!heightmap_ || !heightmap_->isLoaded()) {
        return 0.0f;
    }
    
    // Convert world coords to heightmap pixel coords
    glm::vec2 uv = worldToHeightmapUV(worldX, worldZ);
    
    float hmX = uv.x * static_cast<float>(heightmap_->getWidth() - 1);
    float hmZ = uv.y * static_cast<float>(heightmap_->getHeight() - 1);
    
    // Sample with bilinear interpolation
    float normalizedHeight = heightmap_->sampleBilinear(hmX, hmZ) / 65535.0f;
    
    // Scale to world height (map [0, 1] to [-1, 1] to match renderer)
    return (normalizedHeight * 2.0f - 1.0f) * config_.heightScale;
}

glm::vec3 CharacterController::sampleTerrainNormal(float worldX, float worldZ) const {
    // Sample heights in a cross pattern for normal calculation
    const float step = config_.cellScale;
    
    float hL = sampleTerrainHeight(worldX - step, worldZ);
    float hR = sampleTerrainHeight(worldX + step, worldZ);
    float hU = sampleTerrainHeight(worldX, worldZ - step);
    float hD = sampleTerrainHeight(worldX, worldZ + step);
    
    // Compute normal from height differences
    glm::vec3 normal;
    normal.x = (hL - hR) / (2.0f * step);
    normal.y = 1.0f;
    normal.z = (hU - hD) / (2.0f * step);
    
    return glm::normalize(normal);
}

bool CharacterController::canWalkOnSlope(const glm::vec3& normal) const {
    // Check if slope angle is within walkable limit
    float cosAngle = glm::dot(normal, glm::vec3{0.0f, 1.0f, 0.0f});
    float maxCosAngle = std::cos(glm::radians(config_.maxSlopeAngle));
    return cosAngle >= maxCosAngle;
}

} // namespace voxy
