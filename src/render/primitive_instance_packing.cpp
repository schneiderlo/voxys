#include "render/primitive_instance_packing.hpp"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>

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

bool finiteVec(const glm::vec3& value) noexcept {
    return std::isfinite(value.x) && std::isfinite(value.y)
        && std::isfinite(value.z);
}

bool finiteQuat(const glm::quat& value) noexcept {
    return std::isfinite(value.w) && std::isfinite(value.x)
        && std::isfinite(value.y) && std::isfinite(value.z);
}

bool finiteMat(const glm::mat4& value) noexcept {
    for (glm::length_t column = 0; column < 4; ++column) {
        for (glm::length_t row = 0; row < 4; ++row) {
            if (!std::isfinite(value[column][row])) return false;
        }
    }
    return true;
}

bool buildGpuInstance(
    const physics::PhysicsWorld::DynamicBodySnapshot& body,
    GpuInstance& instance) noexcept {
    if (!isRenderablePrimitiveSnapshot(body)) return false;
    const float rotationLengthSquared =
        glm::dot(body.rotation, body.rotation);
    const glm::quat rotation =
        rotationLengthSquared > std::numeric_limits<float>::min()
            ? glm::normalize(body.rotation)
            : glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    instance.model = glm::translate(glm::mat4(1.0f), body.position)
                   * glm::mat4_cast(rotation)
                   * glm::scale(glm::mat4(1.0f), body.dimensions);
    if (!finiteMat(instance.model)) return false;
    instance.color = shapeColor(body.shape);
    return true;
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
            if (!buildGpuInstance(body, instance)) continue;
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
    batch.cacheTokens.reserve(bodies.size());
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
            if (!isRenderablePrimitiveSnapshot(body)) continue;

            if (!body.active) {
                auto& cached = cache.entries[bodyIndex];
                if (cached.body.shape != Shape::Count
                    && sameBodyInput(cached.body, body)) {
                    batch.instances.push_back(cached.instance);
                    batch.cacheTokens.push_back(cached.token);
                    ++batch.instanceCounts[shapeIndex];
                    continue;
                }

                if (cache.nextToken == 0) {
                    for (auto& entry : cache.entries) {
                        entry.body.shape = Shape::Count;
                        entry.token = 0;
                    }
                    cache.nextToken = 1;
                    batch.forceFullUpload = true;
                }
                GpuInstance nextInstance;
                if (!buildGpuInstance(body, nextInstance)) continue;
                cached.body = body;
                cached.instance = nextInstance;
                cached.token = cache.nextToken++;
                batch.instances.push_back(cached.instance);
                batch.cacheTokens.push_back(cached.token);
                ++batch.instanceCounts[shapeIndex];
                continue;
            }

            GpuInstance instance;
            if (!buildGpuInstance(body, instance)) continue;
            batch.instances.push_back(instance);
            batch.cacheTokens.push_back(0);
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

bool isRenderablePrimitiveSnapshot(
    const physics::PhysicsWorld::DynamicBodySnapshot& body) noexcept {
    const uint32_t shape = static_cast<uint32_t>(body.shape);
    if (shape >= static_cast<uint32_t>(Shape::Count)
        || !finiteVec(body.position) || !finiteQuat(body.rotation)
        || !finiteVec(body.dimensions)
        || body.dimensions.x <= 0.0f || body.dimensions.y <= 0.0f
        || body.dimensions.z <= 0.0f) {
        return false;
    }
    const float rotationLengthSquared =
        glm::dot(body.rotation, body.rotation);
    return std::isfinite(rotationLengthSquared);
}

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

PrimitiveInstanceUploadPlan planPrimitiveInstanceUpload(
    size_t instanceCount, std::span<const uint64_t> currentCacheTokens,
    std::span<const uint64_t> previouslyUploadedCacheTokens,
    bool bufferContentsValid, bool forceFullUpload) noexcept {
    PrimitiveInstanceUploadPlan plan;
    if (instanceCount == 0) return plan;

    const auto useFullUpload = [&] {
        plan.rangeCount = 0;
        plan.byteCount = instanceCount * sizeof(GpuInstance);
        plan.fullUpload = true;
        return plan;
    };
    if (!bufferContentsValid || forceFullUpload
        || currentCacheTokens.size() != instanceCount
        || previouslyUploadedCacheTokens.size() != instanceCount) {
        return useFullUpload();
    }

    size_t dirtyCount = 0;
    for (size_t index = 0; index < instanceCount; ++index) {
        const uint64_t token = currentCacheTokens[index];
        if (token != 0 && token == previouslyUploadedCacheTokens[index]) {
            continue;
        }

        ++dirtyCount;
        if (dirtyCount >= instanceCount / 2 + instanceCount % 2) {
            return useFullUpload();
        }

        if (plan.rangeCount != 0) {
            auto& previousRange = plan.ranges[plan.rangeCount - 1];
            if (previousRange.firstInstance + previousRange.instanceCount
                == index) {
                ++previousRange.instanceCount;
                continue;
            }
        }
        if (plan.rangeCount == plan.ranges.size()) return useFullUpload();
        plan.ranges[plan.rangeCount++] = {index, 1};
    }

    plan.byteCount = dirtyCount * sizeof(GpuInstance);
    return plan;
}

} // namespace voxy::render::detail
