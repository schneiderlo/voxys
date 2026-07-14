#include "physics/physics_types.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace voxy::physics {

bool isValidWorldPosition(const WorldPosition& position) noexcept {
    for (int axis = 0; axis < 3; ++axis) {
        if (!std::isfinite(position.local[axis])
            || position.local[axis] < -kWorldSectorHalf
            || position.local[axis] >= kWorldSectorHalf) return false;
    }
    return true;
}

WorldPosition canonicalWorldPosition(
    const glm::ivec3& sector, const glm::dvec3& local) noexcept {
    WorldPosition result;
    constexpr int64_t minimumSector = std::numeric_limits<int32_t>::min();
    constexpr int64_t maximumSector = std::numeric_limits<int32_t>::max();
    for (int axis = 0; axis < 3; ++axis) {
        if (!std::isfinite(local[axis])) {
            result.sector[axis] = sector[axis];
            result.local[axis] = 0.0f;
            continue;
        }
        const double shiftValue = std::floor(
            (local[axis] + double{kWorldSectorHalf})
            / double{kWorldSectorSize});
        const int64_t shift = shiftValue <= double{minimumSector}
            ? minimumSector
            : shiftValue >= double{maximumSector}
                ? maximumSector
                : static_cast<int64_t>(shiftValue);
        const int64_t candidate = int64_t{sector[axis]} + shift;
        if (candidate < minimumSector) {
            result.sector[axis] = std::numeric_limits<int32_t>::min();
            result.local[axis] = -kWorldSectorHalf;
            continue;
        }
        if (candidate > maximumSector) {
            result.sector[axis] = std::numeric_limits<int32_t>::max();
            result.local[axis] = std::nextafter(
                kWorldSectorHalf, -std::numeric_limits<float>::infinity());
            continue;
        }
        result.sector[axis] = static_cast<int32_t>(candidate);
        const double canonical = local[axis]
            - static_cast<double>(shift) * double{kWorldSectorSize};
        result.local[axis] = static_cast<float>(std::clamp(
            canonical, -double{kWorldSectorHalf},
            std::nextafter(double{kWorldSectorHalf},
                           -std::numeric_limits<double>::infinity())));
    }
    return result;
}

WorldPosition worldPositionFromAbsolute(
    const glm::dvec3& absolute) noexcept {
    return canonicalWorldPosition(glm::ivec3(0), absolute);
}

glm::dvec3 worldPositionToAbsolute(
    const WorldPosition& position) noexcept {
    return glm::dvec3(position.sector) * double{kWorldSectorSize}
         + glm::dvec3(position.local);
}

bool worldPositionRelativeToSector(
    const WorldPosition& position, const glm::ivec3& referenceSector,
    glm::vec3& relative, int32_t maximumSectorDelta) noexcept {
    if (!isValidWorldPosition(position) || maximumSectorDelta < 0)
        return false;
    glm::ivec3 delta;
    for (int axis = 0; axis < 3; ++axis) {
        const int64_t wideDelta = int64_t{position.sector[axis]}
                                - referenceSector[axis];
        if (wideDelta < -int64_t{maximumSectorDelta}
            || wideDelta > int64_t{maximumSectorDelta}) return false;
        delta[axis] = static_cast<int32_t>(wideDelta);
    }
    relative = position.local + glm::vec3(delta) * kWorldSectorSize;
    return true;
}

const char* backendTypeName(BackendType type) noexcept {
    switch (type) {
        case BackendType::JoltLegacy: return "jolt_legacy";
        case BackendType::Box3DReference: return "box3d_reference";
        case BackendType::WebGpuSoft: return "webgpu_soft";
    }
    return "unknown";
}

BackendType backendTypeFromName(std::string_view name,
                                BackendType fallback) noexcept {
    if (name == "jolt" || name == "jolt_legacy") {
        return BackendType::JoltLegacy;
    }
    if (name == "box3d" || name == "box3d_reference") {
        return BackendType::Box3DReference;
    }
    if (name == "webgpu" || name == "webgpu_soft" || name == "gpu") {
        return BackendType::WebGpuSoft;
    }
    return fallback;
}

const char* physicsArithmeticModeName(PhysicsArithmeticMode mode) noexcept {
    switch (mode) {
        case PhysicsArithmeticMode::FastFloat: return "fast_float";
        case PhysicsArithmeticMode::DeterministicFloat:
            return "deterministic_float";
        case PhysicsArithmeticMode::StrictLockstep: return "strict_lockstep";
    }
    return "unknown";
}

const char* joltJobSystemModeName(JoltJobSystemMode mode) noexcept {
    switch (mode) {
        case JoltJobSystemMode::SingleThreaded: return "single_threaded";
        case JoltJobSystemMode::ThreadPool: return "thread_pool";
    }
    return "unknown";
}

JoltJobSystemMode joltJobSystemModeFromName(
    std::string_view name, JoltJobSystemMode fallback) noexcept {
    if (name == "single" || name == "single_threaded") {
        return JoltJobSystemMode::SingleThreaded;
    }
    if (name == "threads" || name == "thread_pool" || name == "multi") {
        return JoltJobSystemMode::ThreadPool;
    }
    return fallback;
}

const char* physicsGpuStageName(PhysicsGpuStage stage) noexcept {
    switch (stage) {
        case PhysicsGpuStage::CommandsAndActiveCompaction:
            return "commands_active_compaction";
        case PhysicsGpuStage::ContinuousCollision: return "ccd";
        case PhysicsGpuStage::ForcesAndWater: return "forces_water";
        case PhysicsGpuStage::BroadPhase: return "broad_phase";
        case PhysicsGpuStage::NarrowPhase: return "narrow_phase";
        case PhysicsGpuStage::DynamicSolver: return "dynamic_solver";
        case PhysicsGpuStage::StaticContacts: return "static_contacts";
        case PhysicsGpuStage::IslandsAndSleeping: return "islands_sleeping";
        case PhysicsGpuStage::TickFinalize: return "tick_finalize";
        case PhysicsGpuStage::Count: break;
    }
    return "unknown";
}

double PhysicsGpuStageTiming::totalMilliseconds() const noexcept {
    double total = 0.0;
    for (double stageMilliseconds : milliseconds) total += stageMilliseconds;
    return total;
}

const char* throwableShapeName(ThrowableShape shape) noexcept {
    switch (shape) {
        case ThrowableShape::Sphere: return "ball";
        case ThrowableShape::Cube: return "cube";
        case ThrowableShape::Box: return "rectangle";
        case ThrowableShape::Capsule: return "capsule";
        case ThrowableShape::Cylinder: return "cylinder";
        case ThrowableShape::Count: break;
    }
    return "unknown";
}

glm::vec3 throwableShapeDimensions(ThrowableShape shape) noexcept {
    switch (shape) {
        case ThrowableShape::Sphere: return glm::vec3(1.2f);
        case ThrowableShape::Cube: return glm::vec3(1.1f);
        case ThrowableShape::Box: return glm::vec3(1.8f, 0.8f, 1.0f);
        case ThrowableShape::Capsule: return glm::vec3(0.7f, 1.8f, 0.7f);
        case ThrowableShape::Cylinder: return glm::vec3(0.9f, 1.1f, 0.9f);
        case ThrowableShape::Count: break;
    }
    return glm::vec3(1.0f);
}

} // namespace voxy::physics
