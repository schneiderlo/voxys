// ═══════════════════════════════════════════════════════════════════════════════
// test_controller.cpp - Unit tests for Camera Controller
// ═══════════════════════════════════════════════════════════════════════════════

#include <gtest/gtest.h>
#include "camera/controller.hpp"
#include "camera/camera.hpp"
#include "engine/platform/input.hpp"

#include <cmath>

namespace voxy {

// ─────────────────────────────────────────────────────────────────────────────
// FreeFlyConfig Tests
// ─────────────────────────────────────────────────────────────────────────────

TEST(FreeFlyConfigTest, DefaultValues) {
    FreeFlyConfig config;
    
    EXPECT_FLOAT_EQ(config.baseSpeed, 50.0f);
    EXPECT_FLOAT_EQ(config.boostMultiplier, 5.0f);
    EXPECT_FLOAT_EQ(config.mouseSensitivity, 0.002f);
    EXPECT_FALSE(config.invertY);
}

TEST(FreeFlyConfigTest, WithSpeed) {
    FreeFlyConfig config = FreeFlyConfig::withSpeed(100.0f);
    
    EXPECT_FLOAT_EQ(config.baseSpeed, 100.0f);
    // Other values should be defaults
    EXPECT_FLOAT_EQ(config.boostMultiplier, 5.0f);
}

// ─────────────────────────────────────────────────────────────────────────────
// FreeFlyController Construction Tests
// ─────────────────────────────────────────────────────────────────────────────

TEST(FreeFlyControllerTest, DefaultConstruction) {
    FreeFlyController controller;
    
    EXPECT_FALSE(controller.hasCamera());
    EXPECT_EQ(controller.camera(), nullptr);
    EXPECT_FALSE(controller.isMoving());
    EXPECT_FLOAT_EQ(controller.currentSpeed(), 0.0f);
}

TEST(FreeFlyControllerTest, ConstructWithCamera) {
    Camera camera;
    FreeFlyController controller(camera);
    
    EXPECT_TRUE(controller.hasCamera());
    EXPECT_EQ(controller.camera(), &camera);
}

TEST(FreeFlyControllerTest, ConstructWithConfig) {
    Camera camera;
    FreeFlyConfig config;
    config.baseSpeed = 100.0f;
    config.boostMultiplier = 10.0f;
    
    FreeFlyController controller(camera, config);
    
    EXPECT_FLOAT_EQ(controller.baseSpeed(), 100.0f);
    EXPECT_FLOAT_EQ(controller.boostMultiplier(), 10.0f);
}

// ─────────────────────────────────────────────────────────────────────────────
// Camera Attachment Tests
// ─────────────────────────────────────────────────────────────────────────────

TEST(FreeFlyControllerTest, AttachCamera) {
    FreeFlyController controller;
    Camera camera;
    
    EXPECT_FALSE(controller.hasCamera());
    
    controller.attachCamera(camera);
    
    EXPECT_TRUE(controller.hasCamera());
    EXPECT_EQ(controller.camera(), &camera);
}

TEST(FreeFlyControllerTest, DetachCamera) {
    Camera camera;
    FreeFlyController controller(camera);
    
    EXPECT_TRUE(controller.hasCamera());
    
    controller.detachCamera();
    
    EXPECT_FALSE(controller.hasCamera());
    EXPECT_EQ(controller.camera(), nullptr);
}

// ─────────────────────────────────────────────────────────────────────────────
// Configuration Setters Tests
// ─────────────────────────────────────────────────────────────────────────────

TEST(FreeFlyControllerTest, SetBaseSpeed) {
    FreeFlyController controller;
    
    controller.setBaseSpeed(75.0f);
    
    EXPECT_FLOAT_EQ(controller.baseSpeed(), 75.0f);
}

TEST(FreeFlyControllerTest, SetBoostMultiplier) {
    FreeFlyController controller;
    
    controller.setBoostMultiplier(3.0f);
    
    EXPECT_FLOAT_EQ(controller.boostMultiplier(), 3.0f);
}

TEST(FreeFlyControllerTest, SetMouseSensitivity) {
    FreeFlyController controller;
    
    controller.setMouseSensitivity(0.005f);
    
    EXPECT_FLOAT_EQ(controller.mouseSensitivity(), 0.005f);
}

TEST(FreeFlyControllerTest, SetInvertY) {
    FreeFlyController controller;
    
    EXPECT_FALSE(controller.invertY());
    
    controller.setInvertY(true);
    
    EXPECT_TRUE(controller.invertY());
}

TEST(FreeFlyControllerTest, SetConfig) {
    FreeFlyController controller;
    
    FreeFlyConfig config;
    config.baseSpeed = 200.0f;
    config.boostMultiplier = 2.0f;
    config.mouseSensitivity = 0.001f;
    config.invertY = true;
    
    controller.setConfig(config);
    
    EXPECT_FLOAT_EQ(controller.baseSpeed(), 200.0f);
    EXPECT_FLOAT_EQ(controller.boostMultiplier(), 2.0f);
    EXPECT_FLOAT_EQ(controller.mouseSensitivity(), 0.001f);
    EXPECT_TRUE(controller.invertY());
}

// ─────────────────────────────────────────────────────────────────────────────
// Update Without Camera Tests
// ─────────────────────────────────────────────────────────────────────────────

TEST(FreeFlyControllerTest, UpdateWithoutCamera) {
    FreeFlyController controller;
    Input input;
    
    // Should not crash without camera
    input.beginFrame();
    controller.update(0.016f, input);
    input.endFrame();
    
    EXPECT_FALSE(controller.isMoving());
    SUCCEED();
}

// ─────────────────────────────────────────────────────────────────────────────
// Movement Tests
// ─────────────────────────────────────────────────────────────────────────────

TEST(FreeFlyControllerTest, ForwardMovement) {
    Camera camera(glm::vec3(0.0f, 0.0f, 0.0f));
    FreeFlyController controller(camera);
    Input input;
    
    glm::vec3 initialPos = camera.position();
    glm::vec3 forward = camera.forward();
    
    // Simulate W key press
    input.onKeyDown(static_cast<int>(Key::W));
    input.beginFrame();
    controller.update(1.0f, input);  // 1 second delta
    input.endFrame();
    
    // Camera should have moved forward
    EXPECT_TRUE(controller.isMoving());
    EXPECT_GT(controller.currentSpeed(), 0.0f);
    
    glm::vec3 newPos = camera.position();
    glm::vec3 movement = newPos - initialPos;
    
    // Movement should be roughly in the forward direction
    float dot = glm::dot(glm::normalize(movement), forward);
    EXPECT_GT(dot, 0.99f);  // Should be very close to 1
}

TEST(FreeFlyControllerTest, BackwardMovement) {
    Camera camera(glm::vec3(0.0f, 0.0f, 0.0f));
    FreeFlyController controller(camera);
    Input input;
    
    glm::vec3 initialPos = camera.position();
    glm::vec3 forward = camera.forward();
    
    // Simulate S key press
    input.onKeyDown(static_cast<int>(Key::S));
    input.beginFrame();
    controller.update(1.0f, input);
    input.endFrame();
    
    glm::vec3 newPos = camera.position();
    glm::vec3 movement = newPos - initialPos;
    
    // Movement should be roughly in the -forward direction
    float dot = glm::dot(glm::normalize(movement), forward);
    EXPECT_LT(dot, -0.99f);  // Should be very close to -1
}

TEST(FreeFlyControllerTest, LeftMovement) {
    Camera camera(glm::vec3(0.0f, 0.0f, 0.0f));
    FreeFlyController controller(camera);
    Input input;
    
    glm::vec3 initialPos = camera.position();
    glm::vec3 right = camera.right();
    
    // Simulate A key press
    input.onKeyDown(static_cast<int>(Key::A));
    input.beginFrame();
    controller.update(1.0f, input);
    input.endFrame();
    
    glm::vec3 newPos = camera.position();
    glm::vec3 movement = newPos - initialPos;
    
    // Movement should be roughly in the -right direction (left)
    float dot = glm::dot(glm::normalize(movement), right);
    EXPECT_LT(dot, -0.99f);
}

TEST(FreeFlyControllerTest, RightMovement) {
    Camera camera(glm::vec3(0.0f, 0.0f, 0.0f));
    FreeFlyController controller(camera);
    Input input;
    
    glm::vec3 initialPos = camera.position();
    glm::vec3 right = camera.right();
    
    // Simulate D key press
    input.onKeyDown(static_cast<int>(Key::D));
    input.beginFrame();
    controller.update(1.0f, input);
    input.endFrame();
    
    glm::vec3 newPos = camera.position();
    glm::vec3 movement = newPos - initialPos;
    
    // Movement should be roughly in the right direction
    float dot = glm::dot(glm::normalize(movement), right);
    EXPECT_GT(dot, 0.99f);
}

TEST(FreeFlyControllerTest, UpMovement) {
    Camera camera(glm::vec3(0.0f, 0.0f, 0.0f));
    FreeFlyController controller(camera);
    Input input;
    
    glm::vec3 initialPos = camera.position();
    
    // Simulate E key press
    input.onKeyDown(static_cast<int>(Key::E));
    input.beginFrame();
    controller.update(1.0f, input);
    input.endFrame();
    
    glm::vec3 newPos = camera.position();
    
    // Y should have increased
    EXPECT_GT(newPos.y, initialPos.y);
}

TEST(FreeFlyControllerTest, UpMovementWithSpace) {
    Camera camera(glm::vec3(0.0f, 0.0f, 0.0f));
    FreeFlyController controller(camera);
    Input input;
    
    glm::vec3 initialPos = camera.position();
    
    // Simulate Space key press
    input.onKeyDown(static_cast<int>(Key::Space));
    input.beginFrame();
    controller.update(1.0f, input);
    input.endFrame();
    
    glm::vec3 newPos = camera.position();
    
    // Y should have increased
    EXPECT_GT(newPos.y, initialPos.y);
}

TEST(FreeFlyControllerTest, DownMovement) {
    Camera camera(glm::vec3(0.0f, 100.0f, 0.0f));  // Start high
    FreeFlyController controller(camera);
    Input input;
    
    glm::vec3 initialPos = camera.position();
    
    // Simulate Q key press
    input.onKeyDown(static_cast<int>(Key::Q));
    input.beginFrame();
    controller.update(1.0f, input);
    input.endFrame();
    
    glm::vec3 newPos = camera.position();
    
    // Y should have decreased
    EXPECT_LT(newPos.y, initialPos.y);
}

TEST(FreeFlyControllerTest, DownMovementWithCtrl) {
    Camera camera(glm::vec3(0.0f, 100.0f, 0.0f));
    FreeFlyController controller(camera);
    Input input;
    
    glm::vec3 initialPos = camera.position();
    
    // Simulate Ctrl key press
    input.onKeyDown(static_cast<int>(Key::Ctrl));
    input.beginFrame();
    controller.update(1.0f, input);
    input.endFrame();
    
    glm::vec3 newPos = camera.position();
    
    // Y should have decreased
    EXPECT_LT(newPos.y, initialPos.y);
}

// ─────────────────────────────────────────────────────────────────────────────
// Speed Boost Tests
// ─────────────────────────────────────────────────────────────────────────────

TEST(FreeFlyControllerTest, SpeedBoost) {
    Camera camera(glm::vec3(0.0f, 0.0f, 0.0f));
    FreeFlyConfig config;
    config.baseSpeed = 10.0f;
    config.boostMultiplier = 5.0f;
    FreeFlyController controller(camera, config);
    Input input;
    
    // First, move without boost
    input.onKeyDown(static_cast<int>(Key::W));
    input.beginFrame();
    controller.update(1.0f, input);
    input.endFrame();
    
    EXPECT_FLOAT_EQ(controller.currentSpeed(), 10.0f);
    float normalDistance = glm::length(camera.position());
    
    // Reset camera
    camera.setPosition(glm::vec3(0.0f));
    
    // Move with boost
    input.onKeyDown(static_cast<int>(Key::W));
    input.onKeyDown(static_cast<int>(Key::Shift));
    input.beginFrame();
    controller.update(1.0f, input);
    input.endFrame();
    
    EXPECT_FLOAT_EQ(controller.currentSpeed(), 50.0f);  // 10 * 5
    float boostedDistance = glm::length(camera.position());
    
    // Boosted distance should be 5x normal
    EXPECT_NEAR(boostedDistance / normalDistance, 5.0f, 0.001f);
}

// ─────────────────────────────────────────────────────────────────────────────
// No Movement When No Keys Pressed
// ─────────────────────────────────────────────────────────────────────────────

TEST(FreeFlyControllerTest, NoMovementWhenIdle) {
    Camera camera(glm::vec3(10.0f, 20.0f, 30.0f));
    FreeFlyController controller(camera);
    Input input;
    
    glm::vec3 initialPos = camera.position();
    
    // Update without any keys pressed
    input.beginFrame();
    controller.update(1.0f, input);
    input.endFrame();
    
    EXPECT_FALSE(controller.isMoving());
    EXPECT_FLOAT_EQ(controller.currentSpeed(), 0.0f);
    
    // Position should not change
    EXPECT_FLOAT_EQ(camera.position().x, initialPos.x);
    EXPECT_FLOAT_EQ(camera.position().y, initialPos.y);
    EXPECT_FLOAT_EQ(camera.position().z, initialPos.z);
}

// ─────────────────────────────────────────────────────────────────────────────
// Diagonal Movement Tests
// ─────────────────────────────────────────────────────────────────────────────

TEST(FreeFlyControllerTest, DiagonalMovementNormalized) {
    Camera camera(glm::vec3(0.0f, 0.0f, 0.0f));
    FreeFlyConfig config;
    config.baseSpeed = 10.0f;
    FreeFlyController controller(camera, config);
    Input input;
    
    // Move forward
    input.onKeyDown(static_cast<int>(Key::W));
    input.beginFrame();
    controller.update(1.0f, input);
    input.endFrame();
    
    float forwardDistance = glm::length(camera.position());
    
    // Reset camera
    camera.setPosition(glm::vec3(0.0f));
    
    // Move diagonally (forward + right)
    input.onKeyDown(static_cast<int>(Key::W));
    input.onKeyDown(static_cast<int>(Key::D));
    input.beginFrame();
    controller.update(1.0f, input);
    input.endFrame();
    
    float diagonalDistance = glm::length(camera.position());
    
    // Diagonal distance should be same as straight (normalized)
    EXPECT_NEAR(diagonalDistance, forwardDistance, 0.001f);
}

// ─────────────────────────────────────────────────────────────────────────────
// Mouse Look Tests (requires mouse capture)
// ─────────────────────────────────────────────────────────────────────────────

TEST(FreeFlyControllerTest, MouseLookRequiresCapture) {
    Camera camera(glm::vec3(0.0f));
    FreeFlyController controller(camera);
    Input input;
    
    float initialYaw = camera.yaw();
    float initialPitch = camera.pitch();
    
    // Move mouse without capturing
    input.beginFrame();
    input.onMouseMove(100.0f, 100.0f);
    input.endFrame();
    
    input.beginFrame();
    input.onMouseMove(200.0f, 200.0f);  // Delta of 100,100
    controller.update(0.016f, input);
    input.endFrame();
    
    // Camera should not have rotated (mouse not captured)
    EXPECT_FLOAT_EQ(camera.yaw(), initialYaw);
    EXPECT_FLOAT_EQ(camera.pitch(), initialPitch);
}

TEST(FreeFlyControllerTest, MouseLookWhenCaptured) {
    Camera camera(glm::vec3(0.0f));
    FreeFlyController controller(camera);
    Input input;
    
    float initialYaw = camera.yaw();
    
    // Capture mouse
    input.captureMouse();
    
    // Frame 1: establish initial position
    input.beginFrame();
    input.onMouseMove(0.0f, 0.0f);
    input.endFrame();
    
    // Frame 2: move mouse (delta is computed manually)
    input.onMouseMove(100.0f, 0.0f);  // Move mouse right
    input.beginFrame();
    input.computeDeltas();
    input.endFrame();
    
    // Frame 3: delta from frame 2 is now available
    input.beginFrame();
    controller.update(0.016f, input);
    input.endFrame();
    
    // Camera yaw should have changed
    EXPECT_NE(camera.yaw(), initialYaw);
}

// ─────────────────────────────────────────────────────────────────────────────
// Mouse Capture Toggle Tests
// ─────────────────────────────────────────────────────────────────────────────

TEST(FreeFlyControllerTest, LeftClickCapturesMouse) {
    Camera camera(glm::vec3(0.0f));
    FreeFlyController controller(camera);
    Input input;
    
    EXPECT_FALSE(input.isMouseCaptured());
    
    // Simulate left click
    input.onMouseDown(static_cast<int>(MouseButton::Left));
    input.beginFrame();
    controller.update(0.016f, input);
    input.endFrame();
    
    EXPECT_TRUE(input.isMouseCaptured());
}

TEST(FreeFlyControllerTest, EscapeReleasesMouse) {
    Camera camera(glm::vec3(0.0f));
    FreeFlyController controller(camera);
    Input input;
    
    // First capture
    input.captureMouse();
    EXPECT_TRUE(input.isMouseCaptured());
    
    // Press Escape
    input.onKeyDown(static_cast<int>(Key::Escape));
    input.beginFrame();
    controller.update(0.016f, input);
    input.endFrame();
    
    EXPECT_FALSE(input.isMouseCaptured());
}

// ─────────────────────────────────────────────────────────────────────────────
// Velocity Tests
// ─────────────────────────────────────────────────────────────────────────────

TEST(FreeFlyControllerTest, VelocityReflectsMovement) {
    Camera camera(glm::vec3(0.0f));
    FreeFlyController controller(camera);
    Input input;
    
    // No movement = zero velocity
    EXPECT_FLOAT_EQ(controller.velocity().x, 0.0f);
    EXPECT_FLOAT_EQ(controller.velocity().y, 0.0f);
    EXPECT_FLOAT_EQ(controller.velocity().z, 0.0f);
    
    // Move forward
    input.onKeyDown(static_cast<int>(Key::W));
    input.beginFrame();
    controller.update(0.016f, input);
    input.endFrame();
    
    // Velocity should be non-zero
    EXPECT_GT(glm::length(controller.velocity()), 0.0f);
}

TEST(FreeFlyControllerTest, VelocityZeroWhenStopped) {
    Camera camera(glm::vec3(0.0f));
    FreeFlyController controller(camera);
    Input input;
    
    // Move forward
    input.onKeyDown(static_cast<int>(Key::W));
    input.beginFrame();
    controller.update(0.016f, input);
    input.endFrame();
    
    EXPECT_GT(glm::length(controller.velocity()), 0.0f);
    
    // Stop moving
    input.onKeyUp(static_cast<int>(Key::W));
    input.beginFrame();
    controller.update(0.016f, input);
    input.endFrame();
    
    // Velocity should be zero
    EXPECT_FLOAT_EQ(glm::length(controller.velocity()), 0.0f);
}

} // namespace voxy

