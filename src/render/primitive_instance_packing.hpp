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
inline constexpr size_t kMaximumPrimitiveUploadRanges = 16;

struct alignas(16) GpuInstance {
    glm::mat4 model{1.0f};
    glm::vec4 color{1.0f};
};

static_assert(sizeof(GpuInstance) == 80);

struct PrimitiveInstanceBatch {
    std::vector<GpuInstance> instances;
    std::vector<uint64_t> cacheTokens;
    std::array<uint32_t, kPrimitiveShapeCount> firstInstances{};
    std::array<uint32_t, kPrimitiveShapeCount> instanceCounts{};
    bool forceFullUpload = false;
};

struct PrimitiveInstanceUploadRange {
    size_t firstInstance = 0;
    size_t instanceCount = 0;
};

struct PrimitiveInstanceUploadPlan {
    std::array<PrimitiveInstanceUploadRange,
               kMaximumPrimitiveUploadRanges> ranges{};
    size_t rangeCount = 0;
    size_t byteCount = 0;
    bool fullUpload = false;
};

struct CachedPrimitiveInstance {
    physics::PhysicsWorld::DynamicBodySnapshot body{
        physics::PhysicsWorld::ThrowableShape::Count};
    GpuInstance instance;
    uint64_t token = 0;
};

struct PrimitiveInstanceCache {
    std::vector<CachedPrimitiveInstance> entries;
    uint64_t nextToken = 1;
};

[[nodiscard]] PrimitiveInstanceBatch packPrimitiveInstances(
    std::span<const physics::PhysicsWorld::DynamicBodySnapshot> bodies);
[[nodiscard]] PrimitiveInstanceBatch packPrimitiveInstances(
    std::span<const physics::PhysicsWorld::DynamicBodySnapshot> bodies,
    PrimitiveInstanceCache& cache);

/// Returns a bounded set of byte-exact updates. A nonzero token names one
/// immutable cached GpuInstance value; token zero is always dirty. A full
/// upload is selected for a new/recreated buffer, missing token history, at
/// least 50% dirty data, or more ranges than are worth submitting separately.
[[nodiscard]] PrimitiveInstanceUploadPlan planPrimitiveInstanceUpload(
    size_t instanceCount, std::span<const uint64_t> currentCacheTokens,
    std::span<const uint64_t> previouslyUploadedCacheTokens,
    bool bufferContentsValid, bool forceFullUpload = false) noexcept;

} // namespace voxy::render::detail
