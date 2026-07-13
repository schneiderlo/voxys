#include "render/primitive_instance_packing.hpp"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <bit>

namespace voxy::render::detail {
namespace {

using Shape = physics::PhysicsWorld::ThrowableShape;

glm::vec4 shapeColor(Shape shape) {
    switch (shape) {
        case Shape::Sphere: return {0.95f, 0.28f, 0.18f, 1.0f};
        case Shape::Cube: return {0.20f, 0.62f, 0.95f, 1.0f};
        case Shape::Box: return {0.96f, 0.70f, 0.16f, 1.0f};
        case Shape::Capsule: return {0.42f, 0.85f, 0.36f, 1.0f};
        case Shape::Cylinder: return {0.68f, 0.38f, 0.92f, 1.0f};
        case Shape::Count: break;
    }
    return glm::vec4(1.0f);
}

bool sameFloatBits(float lhs, float rhs) {
    return std::bit_cast<uint32_t>(lhs) == std::bit_cast<uint32_t>(rhs);
}

bool sameBodyInput(const physics::PhysicsWorld::DynamicBodySnapshot& lhs,
                   const physics::PhysicsWorld::DynamicBodySnapshot& rhs) {
    return lhs.shape == rhs.shape
        && sameFloatBits(lhs.position.x, rhs.position.x)
        && sameFloatBits(lhs.position.y, rhs.position.y)
        && sameFloatBits(lhs.position.z, rhs.position.z)
        && sameFloatBits(lhs.rotation.x, rhs.rotation.x)
        && sameFloatBits(lhs.rotation.y, rhs.rotation.y)
        && sameFloatBits(lhs.rotation.z, rhs.rotation.z)
        && sameFloatBits(lhs.rotation.w, rhs.rotation.w)
        && sameFloatBits(lhs.dimensions.x, rhs.dimensions.x)
        && sameFloatBits(lhs.dimensions.y, rhs.dimensions.y)
        && sameFloatBits(lhs.dimensions.z, rhs.dimensions.z);
}

PrimitiveInstanceBatch packPrimitiveInstancesImpl(
    std::span<const physics::PhysicsWorld::DynamicBodySnapshot> bodies) {
    PrimitiveInstanceBatch batch;
    batch.instances.reserve(bodies.size());

    for (uint32_t shapeIndex = 0;
         shapeIndex < static_cast<uint32_t>(Shape::Count); ++shapeIndex) {
        batch.firstInstances[shapeIndex] =
            static_cast<uint32_t>(batch.instances.size());
        for (const auto& body : bodies) {
            if (static_cast<uint32_t>(body.shape) != shapeIndex) continue;

            GpuInstance instance;
            instance.model = glm::translate(glm::mat4(1.0f), body.position)
                           * glm::mat4_cast(body.rotation)
                           * glm::scale(glm::mat4(1.0f), body.dimensions);
            instance.color = shapeColor(body.shape);
            batch.instances.push_back(instance);
            ++batch.instanceCounts[shapeIndex];
        }
    }
    return batch;
}

PrimitiveInstanceBatch packPrimitiveInstancesCached(
    std::span<const physics::PhysicsWorld::DynamicBodySnapshot> bodies,
    PrimitiveInstanceCache& cache) {
    PrimitiveInstanceBatch batch;
    batch.instances.reserve(bodies.size());
    if (cache.entries.size() < bodies.size()) {
        cache.entries.resize(bodies.size());
    }

    for (uint32_t shapeIndex = 0;
         shapeIndex < static_cast<uint32_t>(Shape::Count); ++shapeIndex) {
        batch.firstInstances[shapeIndex] =
            static_cast<uint32_t>(batch.instances.size());
        for (size_t bodyIndex = 0; bodyIndex < bodies.size(); ++bodyIndex) {
            const auto& body = bodies[bodyIndex];
            if (static_cast<uint32_t>(body.shape) != shapeIndex) continue;

            if (!body.active) {
                auto& cached = cache.entries[bodyIndex];
                if (cached.body.shape != Shape::Count
                    && sameBodyInput(cached.body, body)) {
                    batch.instances.push_back(cached.instance);
                    ++batch.instanceCounts[shapeIndex];
                    continue;
                }

                cached.body = body;
                cached.instance.model =
                    glm::translate(glm::mat4(1.0f), body.position)
                  * glm::mat4_cast(body.rotation)
                  * glm::scale(glm::mat4(1.0f), body.dimensions);
                cached.instance.color = shapeColor(body.shape);
                batch.instances.push_back(cached.instance);
                ++batch.instanceCounts[shapeIndex];
                continue;
            }

            GpuInstance instance;
            instance.model = glm::translate(glm::mat4(1.0f), body.position)
                           * glm::mat4_cast(body.rotation)
                           * glm::scale(glm::mat4(1.0f), body.dimensions);
            instance.color = shapeColor(body.shape);
            batch.instances.push_back(instance);
            ++batch.instanceCounts[shapeIndex];
        }
    }
    return batch;
}

bool shouldUseInstanceCache(
    std::span<const physics::PhysicsWorld::DynamicBodySnapshot> bodies) {
    constexpr size_t kActivitySamples = 64;
    constexpr size_t kMinimumInactiveDivisor = 8;
    const size_t sampleCount = std::min(bodies.size(), kActivitySamples);
    if (sampleCount == 0) return false;

    size_t inactiveCount = 0;
    for (size_t sample = 0; sample < sampleCount; ++sample) {
        const size_t bodyIndex = sample * bodies.size() / sampleCount;
        inactiveCount += bodies[bodyIndex].active ? 0u : 1u;
    }
    return inactiveCount * kMinimumInactiveDivisor >= sampleCount;
}

} // namespace

PrimitiveInstanceBatch packPrimitiveInstances(
    std::span<const physics::PhysicsWorld::DynamicBodySnapshot> bodies) {
    return packPrimitiveInstancesImpl(bodies);
}

PrimitiveInstanceBatch packPrimitiveInstances(
    std::span<const physics::PhysicsWorld::DynamicBodySnapshot> bodies,
    PrimitiveInstanceCache& cache) {
    if (!shouldUseInstanceCache(bodies)) {
        return packPrimitiveInstancesImpl(bodies);
    }
    return packPrimitiveInstancesCached(bodies, cache);
}

} // namespace voxy::render::detail
