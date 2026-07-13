#pragma once

#include "physics/physics_world.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include <glm/mat4x4.hpp>
#include <glm/vec4.hpp>

namespace voxy::render::detail {

inline constexpr size_t kPrimitiveShapeCount =
    static_cast<size_t>(physics::PhysicsWorld::ThrowableShape::Count);

struct alignas(16) GpuInstance {
    glm::mat4 model{1.0f};
    glm::vec4 color{1.0f};
};

static_assert(sizeof(GpuInstance) == 80);

struct PrimitiveInstanceBatch {
    std::vector<GpuInstance> instances;
    std::array<uint32_t, kPrimitiveShapeCount> firstInstances{};
    std::array<uint32_t, kPrimitiveShapeCount> instanceCounts{};
};

struct CachedPrimitiveInstance {
    physics::PhysicsWorld::DynamicBodySnapshot body{
        physics::PhysicsWorld::ThrowableShape::Count};
    GpuInstance instance;
};

struct PrimitiveInstanceCache {
    std::vector<CachedPrimitiveInstance> entries;
};

[[nodiscard]] PrimitiveInstanceBatch packPrimitiveInstances(
    std::span<const physics::PhysicsWorld::DynamicBodySnapshot> bodies);
[[nodiscard]] PrimitiveInstanceBatch packPrimitiveInstances(
    std::span<const physics::PhysicsWorld::DynamicBodySnapshot> bodies,
    PrimitiveInstanceCache& cache);

} // namespace voxy::render::detail
