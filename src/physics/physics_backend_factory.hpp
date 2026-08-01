#pragma once

#include "physics/physics_backend.hpp"

#include <memory>

namespace voxy::physics::detail {

// The broad product target supplies every selectable backend. The headless
// authority supplies only WebGpuSoft. PhysicsWorld remains unaware of which
// factory implementation its final binary selected.
[[nodiscard]] std::unique_ptr<IPhysicsBackend>
createPhysicsBackend(BackendType type);

} // namespace voxy::physics::detail
