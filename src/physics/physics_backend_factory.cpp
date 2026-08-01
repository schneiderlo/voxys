#include "physics/physics_backend_factory.hpp"

#include "physics/box3d/box3d_backend.hpp"
#include "physics/gpu/gpu_physics_backend.hpp"
#include "physics/jolt/jolt_backend.hpp"

namespace voxy::physics::detail {

std::unique_ptr<IPhysicsBackend>
createPhysicsBackend(BackendType type) {
    switch (type) {
        case BackendType::JoltLegacy:
            return std::make_unique<JoltBackend>();
        case BackendType::Box3DReference:
            return std::make_unique<Box3DReferenceBackend>();
        case BackendType::WebGpuSoft:
            return std::make_unique<GpuPhysicsBackend>();
    }
    return nullptr;
}

} // namespace voxy::physics::detail
