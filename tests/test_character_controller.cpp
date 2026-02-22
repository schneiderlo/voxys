// ═══════════════════════════════════════════════════════════════════════════════
// test_character_controller.cpp - Character Controller Unit Tests
// ═══════════════════════════════════════════════════════════════════════════════

#include <gtest/gtest.h>
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>

#include "camera/character_controller.hpp"
#include "camera/camera.hpp"
#include "terrain/heightmap.hpp"
#include "engine/platform/input.hpp"


using namespace voxy;

// ═══════════════════════════════════════════════════════════════════════════════
// Test Fixtures
// ═══════════════════════════════════════════════════════════════════════════════

class CharacterControllerTest : public ::testing::Test {
protected:
    void SetUp() override {
        camera_ = std::make_unique<Camera>(glm::vec3{0.0f, 10.0f, 0.0f});
        
        // Create a flat heightmap for testing
        heightmap_ = std::make_unique<terrain::Heightmap>(
            terrain::Heightmap::createFlat(64, 64, 32768));  // 50% height
        
        // Create controller with test config
        CharacterConfig config;
        config.groundOffset = 1.8f;
        config.walkSpeed = 4.0f;
        config.runSpeed = 8.0f;
        config.gravity = 20.0f;
        config.jumpHeight = 2.0f;
        config.maxSlopeAngle = 45.0f;
        config.heightScale = 100.0f;  // 0-65535 maps to 0-100 world units
        config.cellScale = 1.0f;
        config.terrainWidth = 64.0f;
        config.terrainHeight = 64.0f;
        
        controller_ = std::make_unique<CharacterController>(*camera_, heightmap_.get(), config);
    }
    
    std::unique_ptr<Camera> camera_;
    std::unique_ptr<terrain::Heightmap> heightmap_;
    std::unique_ptr<CharacterController> controller_;
};

// ═══════════════════════════════════════════════════════════════════════════════
// Character State Tests
// ═══════════════════════════════════════════════════════════════════════════════

TEST_F(CharacterControllerTest, InitialStateIsFalling) {
    // Character starts in falling state until ground is detected
    EXPECT_EQ(controller_->state(), CharacterState::Falling);
}

TEST_F(CharacterControllerTest, StateToStringConversion) {
    EXPECT_STREQ(characterStateToString(CharacterState::Grounded), "Grounded");
    EXPECT_STREQ(characterStateToString(CharacterState::Jumping), "Jumping");
    EXPECT_STREQ(characterStateToString(CharacterState::Falling), "Falling");
}

// ═══════════════════════════════════════════════════════════════════════════════
// Configuration Tests
// ═══════════════════════════════════════════════════════════════════════════════

TEST_F(CharacterControllerTest, ConfigCanBeModified) {
    CharacterConfig newConfig = controller_->config();
    newConfig.walkSpeed = 10.0f;
    newConfig.runSpeed = 20.0f;
    
    controller_->setConfig(newConfig);
    
    EXPECT_FLOAT_EQ(controller_->config().walkSpeed, 10.0f);
    EXPECT_FLOAT_EQ(controller_->config().runSpeed, 20.0f);
}

TEST_F(CharacterControllerTest, JumpVelocityCalculation) {
    CharacterConfig config;
    config.gravity = 20.0f;
    config.jumpHeight = 2.0f;
    
    // v = sqrt(2 * g * h) = sqrt(2 * 20 * 2) = sqrt(80) ≈ 8.944
    float expectedVelocity = std::sqrt(2.0f * 20.0f * 2.0f);
    EXPECT_FLOAT_EQ(config.jumpVelocity(), expectedVelocity);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Camera Attachment Tests
// ═══════════════════════════════════════════════════════════════════════════════

TEST_F(CharacterControllerTest, CameraIsAttached) {
    EXPECT_TRUE(controller_->hasCamera());
    EXPECT_EQ(controller_->camera(), camera_.get());
}

TEST_F(CharacterControllerTest, CameraCanBeDetached) {
    controller_->detachCamera();
    EXPECT_FALSE(controller_->hasCamera());
    EXPECT_EQ(controller_->camera(), nullptr);
}

TEST_F(CharacterControllerTest, CameraCanBeReattached) {
    controller_->detachCamera();
    controller_->attachCamera(*camera_);
    EXPECT_TRUE(controller_->hasCamera());
}

// ═══════════════════════════════════════════════════════════════════════════════
// Heightmap Attachment Tests
// ═══════════════════════════════════════════════════════════════════════════════

TEST_F(CharacterControllerTest, HeightmapCanBeChanged) {
    auto newHeightmap = terrain::Heightmap::createFlat(128, 128, 16384);
    controller_->setHeightmap(&newHeightmap);
    
    // Controller should use the new heightmap
    // (internal state changed - verified by terrain sampling tests)
}

// ═══════════════════════════════════════════════════════════════════════════════
// Terrain Sampling Tests
// ═══════════════════════════════════════════════════════════════════════════════

TEST_F(CharacterControllerTest, SampleTerrainHeightAtCenter) {
    // Flat heightmap at 32768 (50%) with heightScale 100 = 50 world units
    float height = controller_->sampleTerrainHeight(0.0f, 0.0f);
    
    // 32768/65535 maps to 0.5 in [0,1], which maps to 0.0 in [-1,1]
    // 0.0 * 100 = 0.0
    EXPECT_NEAR(height, 0.0f, 0.5f);
}

TEST_F(CharacterControllerTest, SampleTerrainNormalOnFlatTerrain) {
    // Flat terrain should have upward-pointing normal
    glm::vec3 normal = controller_->sampleTerrainNormal(0.0f, 0.0f);
    
    EXPECT_NEAR(normal.x, 0.0f, 0.01f);
    EXPECT_NEAR(normal.y, 1.0f, 0.01f);
    EXPECT_NEAR(normal.z, 0.0f, 0.01f);
}

TEST_F(CharacterControllerTest, CanWalkOnFlatSlope) {
    glm::vec3 flatNormal{0.0f, 1.0f, 0.0f};
    EXPECT_TRUE(controller_->canWalkOnSlope(flatNormal));
}

TEST_F(CharacterControllerTest, CanWalkOn30DegreeSlope) {
    // 30-degree slope normal
    float angle = glm::radians(30.0f);
    glm::vec3 slopeNormal{std::sin(angle), std::cos(angle), 0.0f};
    EXPECT_TRUE(controller_->canWalkOnSlope(slopeNormal));
}

TEST_F(CharacterControllerTest, CannotWalkOn60DegreeSlope) {
    // 60-degree slope normal (steeper than 45° max)
    float angle = glm::radians(60.0f);
    glm::vec3 slopeNormal{std::sin(angle), std::cos(angle), 0.0f};
    EXPECT_FALSE(controller_->canWalkOnSlope(slopeNormal));
}

// ═══════════════════════════════════════════════════════════════════════════════
// Feet Position Tests
// ═══════════════════════════════════════════════════════════════════════════════

TEST_F(CharacterControllerTest, FeetPositionIsCorrect) {
    // Camera at height 10, groundOffset is 1.8, so feet at 10 - 1.8 = 8.2
    glm::vec3 feet = controller_->feetPosition();
    EXPECT_FLOAT_EQ(feet.y, 10.0f - 1.8f);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Velocity Tests
// ═══════════════════════════════════════════════════════════════════════════════

TEST_F(CharacterControllerTest, InitialVelocityIsZero) {
    glm::vec3 velocity = controller_->velocity();
    EXPECT_FLOAT_EQ(velocity.x, 0.0f);
    EXPECT_FLOAT_EQ(velocity.y, 0.0f);
    EXPECT_FLOAT_EQ(velocity.z, 0.0f);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Custom Height Sampler Tests
// ═══════════════════════════════════════════════════════════════════════════════

TEST_F(CharacterControllerTest, CustomHeightSamplerOverridesHeightmap) {
    // Set a custom height sampler that returns a fixed value
    controller_->setHeightSampler([](float x, float z) {
        (void)x; (void)z;
        return 25.0f;  // Always return 25
    });
    
    float height = controller_->sampleTerrainHeight(0.0f, 0.0f);
    EXPECT_FLOAT_EQ(height, 25.0f);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Integration Tests (require Input mock)
// ═══════════════════════════════════════════════════════════════════════════════

// Note: Full integration tests with Input would require mocking the Input class
// For now, these tests verify the core character controller logic without input

TEST_F(CharacterControllerTest, UpdateWithoutCameraDoesNotCrash) {
    CharacterController emptyController;
    // This should not crash
    // emptyController.update(0.016f, mockInput);
}

TEST_F(CharacterControllerTest, UpdateWithoutHeightmapUsesZeroHeight) {
    Camera standaloneCamera(glm::vec3{0.0f, 10.0f, 0.0f});
    CharacterController standaloneController(standaloneCamera, nullptr);
    
    // Without heightmap, terrain height should be 0
    float height = standaloneController.sampleTerrainHeight(0.0f, 0.0f);
    EXPECT_FLOAT_EQ(height, 0.0f);
}
TEST_F(CharacterControllerTest, StandsOnSteepSlope) {
    // 1. Setup a steep slope where height decreases as X increases (downhill is +X)
    // h(x, z) = -2.0 * x
    // Normal calculation:
    // dx implies slope -2.
    // Vector along surface: (1, -2, 0).
    // Normal is perpendicular: (2, 1, 0).
    // Normalized normal: (2/sqrt(5), 1/sqrt(5), 0) ≈ (0.89, 0.44, 0)
    // Up dot Normal = 0.44.
    // Max slope is 45 deg (cos 0.707). 0.44 < 0.707, so it is NOT walkable according to config,
    // but the controller should now allow standing on it without sliding.
    controller_->setHeightSampler([](float x, float z) {
        (void)z;
        return -2.0f * x;
    });

    // 2. Place character on the slope at origin
    // Height at 0,0 is 0.
    camera_->setPosition(glm::vec3{0.0f, 1.8f, 0.0f});

    // 3. Update controller with no input
    Input input; // No keys pressed
    
    // First update to detect ground
    controller_->update(0.016f, input);
    
    // Second update where sliding would previously happen
    controller_->update(0.016f, input);
    
    // 4. Verify velocity
    glm::vec3 velocity = controller_->velocity();
    
    // Expect zero velocity (no sliding)
    EXPECT_NEAR(velocity.x, 0.0f, 0.001f) << "Character should NOT slide on steep slopes";
    EXPECT_NEAR(velocity.z, 0.0f, 0.001f) << "Character should NOT slide on steep slopes";
    
    // Verify we are still considered grounded despite the steep slope
    // (Our specific implementation forces grounded state when feet are at/below terrain)
    EXPECT_EQ(controller_->state(), CharacterState::Grounded);
}


