// ═══════════════════════════════════════════════════════════════════════════════
// test_camera.cpp - Camera Unit Tests
// ═══════════════════════════════════════════════════════════════════════════════

#include <gtest/gtest.h>
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_access.hpp>
#include <cmath>
#include <cstdio>

#include "camera/camera.hpp"

namespace voxy::test {

// ═══════════════════════════════════════════════════════════════════════════════
// CameraConfig Tests
// ═══════════════════════════════════════════════════════════════════════════════

TEST(CameraConfigTest, DefaultValues) {
    CameraConfig config;
    EXPECT_FLOAT_EQ(config.fovY, glm::radians(60.0f));
    EXPECT_FLOAT_EQ(config.nearPlane, 0.1f);
    EXPECT_FLOAT_EQ(config.farPlane, 10000.0f);
    EXPECT_FLOAT_EQ(config.aspectRatio, 16.0f / 9.0f);
}

TEST(CameraConfigTest, WithFovDegrees) {
    auto config = CameraConfig::withFovDegrees(90.0f);
    EXPECT_FLOAT_EQ(config.fovY, glm::radians(90.0f));
}

// ═══════════════════════════════════════════════════════════════════════════════
// Camera Construction Tests
// ═══════════════════════════════════════════════════════════════════════════════

TEST(CameraTest, DefaultConstruction) {
    Camera cam;
    
    // Default position is origin
    EXPECT_FLOAT_EQ(cam.position().x, 0.0f);
    EXPECT_FLOAT_EQ(cam.position().y, 0.0f);
    EXPECT_FLOAT_EQ(cam.position().z, 0.0f);
    
    // Default orientation: yaw=0, pitch=0 means looking along +Z
    EXPECT_FLOAT_EQ(cam.yaw(), 0.0f);
    EXPECT_FLOAT_EQ(cam.pitch(), 0.0f);
    
    // Forward should be approximately (0, 0, 1)
    EXPECT_NEAR(cam.forward().x, 0.0f, 0.001f);
    EXPECT_NEAR(cam.forward().y, 0.0f, 0.001f);
    EXPECT_NEAR(cam.forward().z, 1.0f, 0.001f);
}

TEST(CameraTest, ConstructWithPosition) {
    glm::vec3 pos{100.0f, 50.0f, -200.0f};
    Camera cam(pos);
    
    EXPECT_FLOAT_EQ(cam.position().x, 100.0f);
    EXPECT_FLOAT_EQ(cam.position().y, 50.0f);
    EXPECT_FLOAT_EQ(cam.position().z, -200.0f);
}

TEST(CameraTest, ConstructWithPositionAndConfig) {
    glm::vec3 pos{10.0f, 20.0f, 30.0f};
    CameraConfig config;
    config.fovY = glm::radians(90.0f);
    config.nearPlane = 1.0f;
    config.farPlane = 5000.0f;
    
    Camera cam(pos, config);
    
    EXPECT_EQ(cam.position(), pos);
    EXPECT_FLOAT_EQ(cam.fovY(), glm::radians(90.0f));
    EXPECT_FLOAT_EQ(cam.nearPlane(), 1.0f);
    EXPECT_FLOAT_EQ(cam.farPlane(), 5000.0f);
}

TEST(CameraTest, ConstructWithLookAt) {
    glm::vec3 pos{0.0f, 0.0f, 0.0f};
    glm::vec3 target{0.0f, 0.0f, 10.0f};  // Looking along +Z
    
    Camera cam(pos, target);
    
    // Forward should point towards target
    EXPECT_NEAR(cam.forward().x, 0.0f, 0.001f);
    EXPECT_NEAR(cam.forward().y, 0.0f, 0.001f);
    EXPECT_NEAR(cam.forward().z, 1.0f, 0.001f);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Position Tests
// ═══════════════════════════════════════════════════════════════════════════════

TEST(CameraTest, SetPosition) {
    Camera cam;
    glm::vec3 newPos{5.0f, 10.0f, 15.0f};
    cam.setPosition(newPos);
    
    EXPECT_EQ(cam.position(), newPos);
}

TEST(CameraTest, MoveWorldSpace) {
    Camera cam;
    cam.setPosition({10.0f, 0.0f, 0.0f});
    cam.move({5.0f, 3.0f, -2.0f});
    
    EXPECT_FLOAT_EQ(cam.position().x, 15.0f);
    EXPECT_FLOAT_EQ(cam.position().y, 3.0f);
    EXPECT_FLOAT_EQ(cam.position().z, -2.0f);
}

TEST(CameraTest, MoveLocalSpace) {
    Camera cam;
    cam.setPosition({0.0f, 0.0f, 0.0f});
    // Default: forward is +Z, right is +X, up is +Y
    
    // Move 5 units forward (along +Z)
    cam.moveLocal({0.0f, 0.0f, 5.0f});
    
    EXPECT_NEAR(cam.position().x, 0.0f, 0.001f);
    EXPECT_NEAR(cam.position().y, 0.0f, 0.001f);
    EXPECT_NEAR(cam.position().z, 5.0f, 0.001f);
}

TEST(CameraTest, MoveLocalSpaceRotated) {
    Camera cam;
    cam.setPosition({0.0f, 0.0f, 0.0f});
    
    // Rotate 90 degrees (yaw = PI/2), now forward is +X
    cam.setYaw(glm::half_pi<float>());
    
    // Move 5 units forward (should now be along +X)
    cam.moveLocal({0.0f, 0.0f, 5.0f});
    
    EXPECT_NEAR(cam.position().x, 5.0f, 0.001f);
    EXPECT_NEAR(cam.position().y, 0.0f, 0.001f);
    EXPECT_NEAR(cam.position().z, 0.0f, 0.001f);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Orientation Tests
// ═══════════════════════════════════════════════════════════════════════════════

TEST(CameraTest, SetYaw) {
    Camera cam;
    
    // Yaw = 0: forward is +Z
    cam.setYaw(0.0f);
    EXPECT_NEAR(cam.forward().x, 0.0f, 0.001f);
    EXPECT_NEAR(cam.forward().z, 1.0f, 0.001f);
    
    // Yaw = PI/2: forward is +X
    cam.setYaw(glm::half_pi<float>());
    EXPECT_NEAR(cam.forward().x, 1.0f, 0.001f);
    EXPECT_NEAR(cam.forward().z, 0.0f, 0.001f);
    
    // Yaw = PI: forward is -Z
    cam.setYaw(glm::pi<float>());
    EXPECT_NEAR(cam.forward().x, 0.0f, 0.001f);
    EXPECT_NEAR(cam.forward().z, -1.0f, 0.001f);
}

TEST(CameraTest, SetPitch) {
    Camera cam;
    
    // Pitch = 0: forward is horizontal
    cam.setPitch(0.0f);
    EXPECT_NEAR(cam.forward().y, 0.0f, 0.001f);
    
    // Pitch up (positive): forward.y increases
    cam.setPitch(glm::radians(45.0f));
    EXPECT_GT(cam.forward().y, 0.3f);  // Should be looking up
}

TEST(CameraTest, PitchClampedToAvoidGimbalLock) {
    Camera cam;
    
    // Try to set pitch beyond limits
    cam.setPitch(glm::pi<float>());  // 180 degrees - way beyond limit
    
    // Should be clamped to just under 90 degrees
    EXPECT_LT(cam.pitch(), glm::half_pi<float>());
    EXPECT_GT(cam.pitch(), glm::half_pi<float>() - 0.1f);
}

TEST(CameraTest, Rotate) {
    Camera cam;
    cam.setYaw(0.0f);
    cam.setPitch(0.0f);
    
    cam.rotate(glm::radians(90.0f), glm::radians(30.0f));
    
    EXPECT_NEAR(cam.yaw(), glm::radians(90.0f), 0.001f);
    EXPECT_NEAR(cam.pitch(), glm::radians(30.0f), 0.001f);
}

TEST(CameraTest, LookAt) {
    Camera cam;
    cam.setPosition({0.0f, 0.0f, 0.0f});
    
    // Look at point along +X axis
    cam.lookAt({10.0f, 0.0f, 0.0f});
    
    EXPECT_NEAR(cam.forward().x, 1.0f, 0.001f);
    EXPECT_NEAR(cam.forward().y, 0.0f, 0.001f);
    EXPECT_NEAR(cam.forward().z, 0.0f, 0.001f);
}

TEST(CameraTest, LookAtWithElevation) {
    Camera cam;
    cam.setPosition({0.0f, 0.0f, 0.0f});
    
    // Look at point above and forward
    cam.lookAt({0.0f, 10.0f, 10.0f});
    
    // Should be looking up (positive pitch)
    EXPECT_GT(cam.pitch(), 0.0f);
    
    // Forward should point towards target
    glm::vec3 expected = glm::normalize(glm::vec3{0.0f, 10.0f, 10.0f});
    EXPECT_NEAR(cam.forward().x, expected.x, 0.01f);
    EXPECT_NEAR(cam.forward().y, expected.y, 0.01f);
    EXPECT_NEAR(cam.forward().z, expected.z, 0.01f);
}

TEST(CameraTest, DirectionVectorsOrthogonal) {
    Camera cam;
    cam.setYaw(glm::radians(45.0f));
    cam.setPitch(glm::radians(30.0f));
    
    // Forward, right, and up should be mutually orthogonal
    float dotFR = glm::dot(cam.forward(), cam.right());
    float dotFU = glm::dot(cam.forward(), cam.up());
    float dotRU = glm::dot(cam.right(), cam.up());
    
    EXPECT_NEAR(dotFR, 0.0f, 0.001f);
    EXPECT_NEAR(dotFU, 0.0f, 0.001f);
    EXPECT_NEAR(dotRU, 0.0f, 0.001f);
}

TEST(CameraTest, DirectionVectorsNormalized) {
    Camera cam;
    cam.setYaw(glm::radians(123.0f));
    cam.setPitch(glm::radians(-45.0f));
    
    EXPECT_NEAR(glm::length(cam.forward()), 1.0f, 0.001f);
    EXPECT_NEAR(glm::length(cam.right()), 1.0f, 0.001f);
    EXPECT_NEAR(glm::length(cam.up()), 1.0f, 0.001f);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Projection Parameter Tests
// ═══════════════════════════════════════════════════════════════════════════════

TEST(CameraTest, SetFovY) {
    Camera cam;
    cam.setFovY(glm::radians(90.0f));
    EXPECT_FLOAT_EQ(cam.fovY(), glm::radians(90.0f));
}

TEST(CameraTest, SetFovYDegrees) {
    Camera cam;
    cam.setFovYDegrees(90.0f);
    EXPECT_NEAR(cam.fovY(), glm::radians(90.0f), 0.0001f);
}

TEST(CameraTest, SetAspectRatioFloat) {
    Camera cam;
    cam.setAspectRatio(2.0f);
    EXPECT_FLOAT_EQ(cam.aspectRatio(), 2.0f);
}

TEST(CameraTest, SetAspectRatioFromDimensions) {
    Camera cam;
    cam.setAspectRatio(1920, 1080);
    EXPECT_NEAR(cam.aspectRatio(), 16.0f / 9.0f, 0.001f);
}

TEST(CameraTest, SetAspectRatioZeroHeightIgnored) {
    Camera cam;
    float originalAspect = cam.aspectRatio();
    cam.setAspectRatio(1920, 0);  // Should be ignored
    EXPECT_FLOAT_EQ(cam.aspectRatio(), originalAspect);
}

TEST(CameraTest, SetClipPlanes) {
    Camera cam;
    cam.setClipPlanes(0.5f, 5000.0f);
    EXPECT_FLOAT_EQ(cam.nearPlane(), 0.5f);
    EXPECT_FLOAT_EQ(cam.farPlane(), 5000.0f);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Matrix Tests
// ═══════════════════════════════════════════════════════════════════════════════

TEST(CameraTest, ViewMatrixIdentityAtOrigin) {
    Camera cam;
    cam.setPosition({0.0f, 0.0f, 0.0f});
    cam.setYaw(0.0f);
    cam.setPitch(0.0f);
    
    glm::mat4 view = cam.viewMatrix();
    
    // Origin should map to origin in view space
    glm::vec4 origin = view * glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
    EXPECT_NEAR(origin.x, 0.0f, 0.001f);
    EXPECT_NEAR(origin.y, 0.0f, 0.001f);
    EXPECT_NEAR(origin.z, 0.0f, 0.001f);
}

TEST(CameraTest, ViewMatrixTranslatesPosition) {
    Camera cam;
    cam.setPosition({10.0f, 20.0f, 30.0f});
    cam.setYaw(0.0f);
    cam.setPitch(0.0f);
    
    glm::mat4 view = cam.viewMatrix();
    
    // Camera position should map to origin in view space
    glm::vec4 camPosView = view * glm::vec4(10.0f, 20.0f, 30.0f, 1.0f);
    EXPECT_NEAR(camPosView.x, 0.0f, 0.001f);
    EXPECT_NEAR(camPosView.y, 0.0f, 0.001f);
    EXPECT_NEAR(camPosView.z, 0.0f, 0.001f);
}

TEST(CameraTest, ProjectionMatrixPerspective) {
    Camera cam;
    cam.setFovYDegrees(60.0f);
    cam.setAspectRatio(16.0f / 9.0f);
    cam.setClipPlanes(0.1f, 1000.0f);
    
    glm::mat4 proj = cam.projectionMatrix();
    
    // Projection matrix should have proper perspective properties
    // GLM uses column-major order: proj[col][row]
    // The (2,2) and (3,2) elements control depth mapping
    EXPECT_NE(proj[2][2], 0.0f);
    EXPECT_NE(proj[3][2], 0.0f);
    
    // Project uses GLM_FORCE_DEPTH_ZERO_TO_ONE and GLM_FORCE_LEFT_HANDED for WebGPU
    // In this configuration, (2,3) is +1 instead of -1 (OpenGL default)
    EXPECT_FLOAT_EQ(proj[2][3], 1.0f);
}

TEST(CameraTest, ViewProjectionMatrixCombinesCorrectly) {
    Camera cam;
    cam.setPosition({5.0f, 10.0f, 15.0f});
    cam.setYaw(glm::radians(45.0f));
    cam.setPitch(glm::radians(15.0f));
    
    glm::mat4 vp = cam.viewProjectionMatrix();
    glm::mat4 expected = cam.projectionMatrix() * cam.viewMatrix();
    
    // Compare matrices element by element
    for (int col = 0; col < 4; ++col) {
        for (int row = 0; row < 4; ++row) {
            EXPECT_NEAR(vp[col][row], expected[col][row], 0.0001f);
        }
    }
}

TEST(CameraTest, InverseViewMatrixIsInverse) {
    Camera cam;
    cam.setPosition({5.0f, 10.0f, 15.0f});
    cam.setYaw(glm::radians(45.0f));
    
    glm::mat4 view = cam.viewMatrix();
    glm::mat4 invView = cam.inverseViewMatrix();
    glm::mat4 identity = view * invView;
    
    // Should be approximately identity
    for (int col = 0; col < 4; ++col) {
        for (int row = 0; row < 4; ++row) {
            float expected = (col == row) ? 1.0f : 0.0f;
            EXPECT_NEAR(identity[col][row], expected, 0.0001f);
        }
    }
}

TEST(CameraTest, InverseProjectionMatrixIsInverse) {
    Camera cam;
    cam.setFovYDegrees(90.0f);
    cam.setAspectRatio(1.5f);
    
    glm::mat4 proj = cam.projectionMatrix();
    glm::mat4 invProj = cam.inverseProjectionMatrix();
    glm::mat4 identity = proj * invProj;
    
    // Should be approximately identity
    for (int col = 0; col < 4; ++col) {
        for (int row = 0; row < 4; ++row) {
            float expected = (col == row) ? 1.0f : 0.0f;
            EXPECT_NEAR(identity[col][row], expected, 0.0001f);
        }
    }
}

TEST(CameraTest, InverseViewProjectionMatrixIsInverse) {
    Camera cam;
    cam.setPosition({1.0f, 2.0f, 3.0f});
    cam.setYaw(glm::radians(30.0f));
    cam.setPitch(glm::radians(10.0f));
    
    glm::mat4 vp = cam.viewProjectionMatrix();
    glm::mat4 invVP = cam.inverseViewProjectionMatrix();
    glm::mat4 identity = vp * invVP;
    
    // Should be approximately identity
    for (int col = 0; col < 4; ++col) {
        for (int row = 0; row < 4; ++row) {
            float expected = (col == row) ? 1.0f : 0.0f;
            EXPECT_NEAR(identity[col][row], expected, 0.001f);
        }
    }
}

// ═══════════════════════════════════════════════════════════════════════════════
// Lazy Matrix Computation Tests
// ═══════════════════════════════════════════════════════════════════════════════

TEST(CameraTest, MatricesAreCached) {
    Camera cam;
    
    // Get matrices twice
    const glm::mat4& view1 = cam.viewMatrix();
    const glm::mat4& view2 = cam.viewMatrix();
    
    // Should return same cached reference
    EXPECT_EQ(&view1, &view2);
}

TEST(CameraTest, MatricesInvalidatedByPosition) {
    Camera cam;
    
    glm::mat4 view1 = cam.viewMatrix();
    cam.setPosition({1.0f, 2.0f, 3.0f});
    glm::mat4 view2 = cam.viewMatrix();
    
    // Matrices should be different after position change
    EXPECT_NE(view1[3][0], view2[3][0]);
}

TEST(CameraTest, MatricesInvalidatedByRotation) {
    Camera cam;
    
    glm::mat4 view1 = cam.viewMatrix();
    cam.setYaw(glm::radians(45.0f));
    glm::mat4 view2 = cam.viewMatrix();
    
    // Matrices should be different after rotation
    EXPECT_NE(view1[0][0], view2[0][0]);
}

TEST(CameraTest, ProjectionInvalidatedByFov) {
    Camera cam;
    
    glm::mat4 proj1 = cam.projectionMatrix();
    cam.setFovYDegrees(90.0f);
    glm::mat4 proj2 = cam.projectionMatrix();
    
    // Matrices should be different
    EXPECT_NE(proj1[1][1], proj2[1][1]);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Utility Tests
// ═══════════════════════════════════════════════════════════════════════════════

TEST(CameraTest, ScreenToWorldRayCenter) {
    Camera cam;
    cam.setPosition({0.0f, 0.0f, 0.0f});
    cam.setYaw(0.0f);
    cam.setPitch(0.0f);
    
    // Center of screen (0,0 in NDC) should give forward direction
    glm::vec3 ray = cam.screenToWorldRay({0.0f, 0.0f});
    
    EXPECT_NEAR(ray.x, cam.forward().x, 0.01f);
    EXPECT_NEAR(ray.y, cam.forward().y, 0.01f);
    EXPECT_NEAR(ray.z, cam.forward().z, 0.01f);
}

TEST(CameraTest, ScreenToWorldRayIsNormalized) {
    Camera cam;
    cam.setPosition({10.0f, 20.0f, 30.0f});
    cam.setYaw(glm::radians(45.0f));
    cam.setPitch(glm::radians(15.0f));
    
    // Various screen positions
    std::vector<glm::vec2> ndcPoints = {
        {0.0f, 0.0f},
        {1.0f, 1.0f},
        {-1.0f, -1.0f},
        {0.5f, -0.5f}
    };
    
    for (const auto& ndc : ndcPoints) {
        glm::vec3 ray = cam.screenToWorldRay(ndc);
        EXPECT_NEAR(glm::length(ray), 1.0f, 0.001f);
    }
}

TEST(CameraTest, UpdateMatricesForcesComputation) {
    Camera cam;
    cam.setPosition({1.0f, 2.0f, 3.0f});
    
    // Force update
    cam.updateMatrices();
    
    // Matrices should be valid and consistent
    glm::mat4 vp = cam.viewProjectionMatrix();
    glm::mat4 expected = cam.projectionMatrix() * cam.viewMatrix();
    
    for (int col = 0; col < 4; ++col) {
        for (int row = 0; row < 4; ++row) {
            EXPECT_NEAR(vp[col][row], expected[col][row], 0.0001f);
        }
    }
}

} // namespace voxy::test

