#include <gtest/gtest.h>

#include "render/primitive_culling.hpp"
#include "render/primitive_instance_packing.hpp"

#include <array>
#include <cmath>
#include <limits>
#include <vector>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

namespace voxy::render {
namespace {

using Snapshot = physics::PhysicsWorld::DynamicBodySnapshot;
using Shape = physics::PhysicsWorld::ThrowableShape;

Snapshot bodyAt(float x, float y, float z) {
    return {Shape::Cube, glm::vec3(x, y, z),
            glm::quat(1.0f, 0.0f, 0.0f, 0.0f), glm::vec3(0.1f), false};
}

TEST(PrimitiveCullingTest, RejectsSeparatedBodiesAndPreservesOrder) {
    std::vector<Snapshot> bodies = {
        bodyAt(-0.5f, 0.0f, 0.5f),
        bodyAt(10.0f, 0.0f, 0.5f),
        bodyAt(0.5f, 0.0f, 0.5f),
    };
    const Frustum frustum = Frustum::fromViewProj(glm::mat4(1.0f));

    EXPECT_EQ(cullPrimitiveSnapshotsInPlace(bodies, frustum), 1u);
    ASSERT_EQ(bodies.size(), 2u);
    EXPECT_FLOAT_EQ(bodies[0].position.x, -0.5f);
    EXPECT_FLOAT_EQ(bodies[1].position.x, 0.5f);

    const auto packed = detail::packPrimitiveInstances(bodies);
    ASSERT_EQ(packed.instances.size(), 2u);
    EXPECT_FLOAT_EQ(packed.instances[0].model[3][0], -0.5f);
    EXPECT_FLOAT_EQ(packed.instances[1].model[3][0], 0.5f);
}

TEST(PrimitiveCullingTest, FailsOpenForNonFiniteInputs) {
    const Frustum frustum = Frustum::fromViewProj(glm::mat4(1.0f));
    std::vector<Snapshot> bodies = {
        bodyAt(std::numeric_limits<float>::quiet_NaN(), 0.0f, 0.5f),
        bodyAt(10.0f, 0.0f, 0.5f),
        bodyAt(10.0f, 0.0f, 0.5f),
    };
    bodies[1].dimensions.x = std::numeric_limits<float>::infinity();
    bodies[2].rotation.w = std::numeric_limits<float>::quiet_NaN();

    EXPECT_EQ(cullPrimitiveSnapshotsInPlace(bodies, frustum), 0u);
    EXPECT_EQ(bodies.size(), 3u);
}

TEST(PrimitiveCullingTest, FailsOpenForInvalidFrustumOrClipEdge) {
    Frustum invalid = Frustum::fromViewProj(glm::mat4(1.0f));
    invalid.planes.back().normal.x =
        std::numeric_limits<float>::quiet_NaN();
    std::vector<Snapshot> outside = {bodyAt(10.0f, 0.0f, 0.5f)};
    EXPECT_EQ(cullPrimitiveSnapshotsInPlace(outside, invalid), 0u);

    const Frustum valid = Frustum::fromViewProj(glm::mat4(1.0f));
    const Snapshot nearRightEdge = bodyAt(1.0505f, 0.0f, 0.5f);
    EXPECT_FALSE(primitiveOutsideFrustum(nearRightEdge, valid));
}

TEST(PrimitiveCullingTest, ConservativeBoundContainsTransformedCubeCorners) {
    const Frustum frustum = Frustum::fromViewProj(glm::mat4(1.0f));
    constexpr std::array<float, 2> signs = {-0.5f, 0.5f};
    uint32_t culledCount = 0;

    for (uint32_t index = 0; index < 1000; ++index) {
        Snapshot body = bodyAt(
            -12.0f + static_cast<float>(index % 97) * 0.25f,
            -3.0f + static_cast<float>(index % 29) * 0.2f,
            -2.0f + static_cast<float>(index % 23) * 0.2f);
        body.dimensions = glm::vec3(
            0.2f + static_cast<float>(index % 7) * 0.3f,
            0.3f + static_cast<float>(index % 5) * 0.4f,
            0.4f + static_cast<float>(index % 3) * 0.5f);
        body.rotation = glm::quat(
            0.2f + static_cast<float>(index % 11) * 0.17f,
            -0.8f + static_cast<float>(index % 13) * 0.11f,
            0.4f - static_cast<float>(index % 17) * 0.07f,
            -0.3f + static_cast<float>(index % 19) * 0.05f);
        if (!primitiveOutsideFrustum(body, frustum)) continue;
        ++culledCount;

        const glm::mat4 model = glm::translate(glm::mat4(1.0f), body.position)
                              * glm::mat4_cast(body.rotation)
                              * glm::scale(glm::mat4(1.0f), body.dimensions);
        bool separatedByOnePlane = false;
        for (const Plane& plane : frustum.planes) {
            bool allCornersOutside = true;
            for (float x : signs) {
                for (float y : signs) {
                    for (float z : signs) {
                        const glm::vec3 point = glm::vec3(
                            model * glm::vec4(x, y, z, 1.0f));
                        allCornersOutside &=
                            glm::dot(plane.normal, point) + plane.distance < 0.0f;
                    }
                }
            }
            separatedByOnePlane |= allCornersOutside;
        }
        EXPECT_TRUE(separatedByOnePlane);
    }
    EXPECT_GT(culledCount, 100u);
}

TEST(PrimitiveCullingTest, KeepsCullingAtMeasuredCrossover) {
    PrimitiveCullController controller;
    std::vector<Snapshot> bodies;
    bodies.reserve(256);
    for (uint32_t index = 0; index < 256; ++index) {
        bodies.push_back(bodyAt(index < 64 ? 10.0f : 0.0f, 0.0f, 0.5f));
    }

    const auto stats = controller.cull(bodies, glm::mat4(1.0f));
    EXPECT_TRUE(stats.evaluated);
    EXPECT_TRUE(stats.enabled);
    EXPECT_FLOAT_EQ(stats.measuredRejectionRatio, 0.25f);
    EXPECT_EQ(stats.submittedCount, 192u);
}

TEST(PrimitiveCullingTest, BypassesLowRejectionScenesAndReprobes) {
    PrimitiveCullController controller;
    std::vector<Snapshot> visible(256, bodyAt(0.0f, 0.0f, 0.5f));

    auto firstFrame = visible;
    const auto first = controller.cull(firstFrame, glm::mat4(1.0f));
    EXPECT_TRUE(first.evaluated);
    EXPECT_FALSE(first.enabled);
    EXPECT_EQ(first.submittedCount, first.inputCount);

    for (uint32_t frame = 0; frame < 30; ++frame) {
        auto bypassedFrame = visible;
        const auto bypassed = controller.cull(bypassedFrame, glm::mat4(1.0f));
        EXPECT_FALSE(bypassed.evaluated);
        EXPECT_EQ(bypassedFrame.size(), visible.size());
    }

    std::vector<Snapshot> outside(256, bodyAt(10.0f, 0.0f, 0.5f));
    const auto reprobe = controller.cull(outside, glm::mat4(1.0f));
    EXPECT_TRUE(reprobe.evaluated);
    EXPECT_TRUE(reprobe.enabled);
    EXPECT_EQ(reprobe.submittedCount, 0u);
}

} // namespace
} // namespace voxy::render
