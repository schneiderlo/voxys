// ═══════════════════════════════════════════════════════════════════════════════
// character_controller.cpp - Character Controller Implementation
// ═══════════════════════════════════════════════════════════════════════════════

#include "camera/character_controller.hpp"
#include "physics/terrain_topology.hpp"
#include "terrain/lego_surface.hpp"
#include "terrain/heightmap.hpp"
#include "physics/physics_world.hpp"
#include "core/log.hpp"

#include <glm/geometric.hpp>
#include <glm/gtc/constants.hpp>
#include <cmath>

namespace voxy {

namespace {

[[nodiscard]] bool finiteNonNegative(float value) noexcept {
    return std::isfinite(value) && value >= 0.0f;
}

[[nodiscard]] bool isValidCharacterConfig(
    const CharacterConfig& config) noexcept {
    const double jumpVelocitySquared =
        2.0 * static_cast<double>(config.gravity)
            * static_cast<double>(config.jumpHeight);
    return finiteNonNegative(config.walkSpeed)
        && finiteNonNegative(config.runSpeed)
        && finiteNonNegative(config.gravity)
        && finiteNonNegative(config.jumpHeight)
        && std::isfinite(jumpVelocitySquared)
        && jumpVelocitySquared
            <= static_cast<double>(std::numeric_limits<float>::max())
        && std::isfinite(config.terminalVelocity)
        && config.terminalVelocity > 0.0f
        && finiteNonNegative(config.groundOffset)
        && std::isfinite(config.collisionRadius)
        && config.collisionRadius > 0.0f
        && std::isfinite(config.collisionHeight)
        && config.collisionHeight >= 2.0f * config.collisionRadius
        && std::isfinite(config.maxSlopeAngle)
        && config.maxSlopeAngle >= 0.0f
        && config.maxSlopeAngle < 90.0f
        && finiteNonNegative(config.slopeSlideSpeed)
        && finiteNonNegative(config.maxStepDown)
        && finiteNonNegative(config.mouseSensitivity)
        && std::isfinite(config.heightScale)
        && config.heightScale > 0.0f
        && std::isfinite(config.cellScale)
        && config.cellScale > 0.0f
        && std::isfinite(config.terrainWidth)
        && config.terrainWidth > 0.0f
        && std::isfinite(config.terrainHeight)
        && config.terrainHeight > 0.0f;
}

[[nodiscard]] bool finiteVector(const glm::vec3& value) noexcept {
    return std::isfinite(value.x)
        && std::isfinite(value.y)
        && std::isfinite(value.z);
}

} // anonymous namespace

// ═══════════════════════════════════════════════════════════════════════════════
// Construction
// ═══════════════════════════════════════════════════════════════════════════════

CharacterController::CharacterController(Camera& camera, const terrain::Heightmap* heightmap,
                                         const CharacterConfig& config)
    : camera_(&camera)
    , heightmap_(heightmap)
{
    setConfig(config);
    // Initialize on terrain
    if (camera_) {
        const auto pos = camera_->position();
        lastTerrainHeight_ = sampleTerrainHeight(pos.x, pos.z);
        terrainNormal_ = sampleTerrainNormal(pos.x, pos.z);
        isWalkableSlope_ = canWalkOnSlope(terrainNormal_);
    }
}

CharacterController::~CharacterController() {
    detachPhysicsWorld();
}

void CharacterController::setConfig(const CharacterConfig& config) {
    if (!isValidCharacterConfig(config)) {
        LOG_ERROR("Rejected invalid character-controller configuration");
        return;
    }
    config_ = config;
    if (physicsWorld_ && physicsCharacter_ != physics::PhysicsWorld::InvalidCharacter) {
        physicsWorld_->destroyCharacter(physicsCharacter_);
        physicsCharacter_ = physics::PhysicsWorld::InvalidCharacter;
    }
}

void CharacterController::attachCamera(Camera& camera) {
    if (camera_ == &camera) return;
    if (physicsWorld_ && physicsCharacter_ != physics::PhysicsWorld::InvalidCharacter) {
        physicsWorld_->destroyCharacter(physicsCharacter_);
        physicsCharacter_ = physics::PhysicsWorld::InvalidCharacter;
    }
    camera_ = &camera;
    velocity_ = glm::vec3(0.0f);
    state_ = CharacterState::Falling;
    jumpRequested_ = false;
}

void CharacterController::detachCamera() {
    if (physicsWorld_ && physicsCharacter_ != physics::PhysicsWorld::InvalidCharacter) {
        physicsWorld_->destroyCharacter(physicsCharacter_);
        physicsCharacter_ = physics::PhysicsWorld::InvalidCharacter;
    }
    camera_ = nullptr;
    velocity_ = glm::vec3(0.0f);
    state_ = CharacterState::Falling;
    jumpRequested_ = false;
}

void CharacterController::attachPhysicsWorld(physics::PhysicsWorld& world) {
    if (physicsWorld_ == &world) {
        return;
    }
    detachPhysicsWorld();
    physicsWorld_ = &world;
}

void CharacterController::detachPhysicsWorld() {
    if (physicsWorld_ && physicsCharacter_ != physics::PhysicsWorld::InvalidCharacter) {
        physicsWorld_->destroyCharacter(physicsCharacter_);
    }
    physicsCharacter_ = physics::PhysicsWorld::InvalidCharacter;
    physicsWorld_ = nullptr;
}

void CharacterController::syncPhysicsPosition() {
    if (!camera_ || !physicsWorld_) {
        return;
    }
    if (ensurePhysicsCharacter()) {
        (void)physicsWorld_->setCharacterPosition(
            physicsCharacter_, feetWorldPosition());
        velocity_ = glm::vec3(0.0f);
        state_ = CharacterState::Falling;
    }
}

// ═══════════════════════════════════════════════════════════════════════════════
// Update
// ═══════════════════════════════════════════════════════════════════════════════

void CharacterController::update(float deltaTime, Input& input) {
    if (!camera_) {
        return;
    }
    if (!std::isfinite(deltaTime) || deltaTime < 0.0f) {
        deltaTime = 0.0f;
    } else {
        deltaTime = std::min(deltaTime, 0.1f);
    }
    
    // Handle mouse capture/release
    processMouseCapture(input);
    
    // Process mouse look (only when captured)
    processMouseLook(input);
    
    // Process keyboard movement
    processMovement(deltaTime, input);
    
    if (ensurePhysicsCharacter()) {
        updatePhysicsCharacter(deltaTime);
    } else {
        // Lightweight fallback for standalone controllers with no world attached.
        applyPhysics(deltaTime);
        handleGroundCollision(deltaTime);
    }

    jumpRequested_ = false;
    
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
    
    // Forward/Backward. Arrow keys are layout-independent alternates to WASD,
    // which is reported positionally (US layout) and so shifts on AZERTY/QWERTZ.
    if (input.isKeyDown(Key::W) || input.isKeyDown(Key::Up)) {
        moveDir += forward;
    }
    if (input.isKeyDown(Key::S) || input.isKeyDown(Key::Down)) {
        moveDir -= forward;
    }

    // Left/Right (strafe)
    if (input.isKeyDown(Key::A) || input.isKeyDown(Key::Left)) {
        moveDir -= right;
    }
    if (input.isKeyDown(Key::D) || input.isKeyDown(Key::Right)) {
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
        jumpRequested_ = true;
        if (!physicsWorld_) {
            velocity_.y = config_.jumpVelocity();
            state_ = CharacterState::Jumping;
        }
        LOG_INFO("Jump! velocity.y = {:.1f}", config_.jumpVelocity());
    }
}

// ═══════════════════════════════════════════════════════════════════════════════
// Physics
// ═══════════════════════════════════════════════════════════════════════════════

bool CharacterController::ensurePhysicsCharacter() {
    if (!physicsWorld_ || !physicsWorld_->isInitialized() || !camera_) {
        return false;
    }
    if (physicsCharacter_ != physics::PhysicsWorld::InvalidCharacter) {
        return true;
    }

    physics::PhysicsWorld::CharacterSettings settings;
    settings.radius = config_.collisionRadius;
    settings.height = config_.collisionHeight;
    settings.maxSlopeAngleDegrees = config_.maxSlopeAngle;
    settings.stepUp = 0.5f;
    settings.stepDown = std::min(config_.maxStepDown, 0.5f);
    physicsCharacter_ = physicsWorld_->createCharacter(
        feetWorldPosition(), settings);
    return physicsCharacter_ != physics::PhysicsWorld::InvalidCharacter;
}

void CharacterController::updatePhysicsCharacter(float deltaTime) {
    const auto motion = physicsWorld_->moveCharacter(
        physicsCharacter_, glm::vec3(velocity_.x, 0.0f, velocity_.z),
        jumpRequested_, config_.jumpVelocity(), config_.gravity,
        config_.terminalVelocity, deltaTime);

    velocity_ = motion.velocity;
    terrainNormal_ = motion.groundNormal;
    isWalkableSlope_ = !motion.onSteepGround;
    lastTerrainHeight_ = sampleTerrainHeight(motion.position.x, motion.position.z);

    if (motion.grounded) {
        state_ = CharacterState::Grounded;
    } else if (motion.velocity.y > 0.0f) {
        state_ = CharacterState::Jumping;
    } else {
        state_ = CharacterState::Falling;
    }

    glm::vec3 cameraPosition = motion.position;
    cameraPosition.y += config_.groundOffset;
    camera_->setWorldPosition(motion.sector, cameraPosition);
}

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
        // Grounded: follow the terrain up and down small steps so the walk stays
        // glued to bumps and stairs. But if the ground has dropped away by more
        // than maxStepDown (walking off a ledge/cliff), don't teleport hundreds of
        // units straight down - start falling and let gravity carry us instead.
        if (feetY - lastTerrainHeight_ > config_.maxStepDown) {
            state_ = CharacterState::Falling;
        } else {
            glm::vec3 newPos = pos;
            newPos.y = lastTerrainHeight_ + config_.groundOffset;
            camera_->setPosition(newPos);
        }
    }
}

// ═══════════════════════════════════════════════════════════════════════════════
// Terrain Sampling
// ═══════════════════════════════════════════════════════════════════════════════

glm::vec2 CharacterController::worldToHeightmapUV(float worldX, float worldZ) const {
    if (!std::isfinite(worldX) || !std::isfinite(worldZ)) {
        return glm::vec2(0.5f);
    }
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
    if (!std::isfinite(worldX) || !std::isfinite(worldZ)) {
        return 0.0f;
    }
    // Use custom sampler if provided
    if (heightSampler_) {
        const float height = heightSampler_(worldX, worldZ);
        return std::isfinite(height) ? height : 0.0f;
    }
    
    // Use heightmap if available
    if (!heightmap_ || !heightmap_->isLoaded()) {
        return 0.0f;
    }
    
    // Convert world coords to heightmap pixel coords
    glm::vec2 uv = worldToHeightmapUV(worldX, worldZ);
    
    float hmX = uv.x * static_cast<float>(heightmap_->getWidth() - 1);
    float hmZ = uv.y * static_cast<float>(heightmap_->getHeight() - 1);
    
    if (config_.legoTerrain) {
        return terrain::lego::Surface{heightmap_->getData(),heightmap_->getWidth(),heightmap_->getHeight(),
            config_.heightScale,config_.cellScale}.heightAt(worldX,worldZ);
    }
    // Sample with bilinear interpolation
    const float height = physics::terrain_topology::worldHeight(
        heightmap_->sampleBilinear(hmX, hmZ), config_.heightScale);
    return std::isfinite(height) ? height : 0.0f;
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
    
    const float length = glm::length(normal);
    if (!finiteVector(normal) || !std::isfinite(length)
        || length < 1.0e-6f) {
        return glm::vec3(0.0f, 1.0f, 0.0f);
    }
    return normal / length;
}

bool CharacterController::canWalkOnSlope(const glm::vec3& normal) const {
    // Check if slope angle is within walkable limit
    const float length = glm::length(normal);
    if (!finiteVector(normal) || !std::isfinite(length)
        || length < 1.0e-6f) {
        return false;
    }
    const float cosAngle = normal.y / length;
    float maxCosAngle = std::cos(glm::radians(config_.maxSlopeAngle));
    return cosAngle >= maxCosAngle;
}

} // namespace voxy
