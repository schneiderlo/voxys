#include "physics/physics_backend_factory.hpp"

#include "physics/gpu/gpu_physics_backend.hpp"

namespace voxy::physics::detail {

std::unique_ptr<IPhysicsBackend>
createPhysicsBackend(BackendType type) {
    if (type == BackendType::WebGpuSoft) {
        return std::make_unique<GpuPhysicsBackend>();
    }
    return nullptr;
}

} // namespace voxy::physics::detail
