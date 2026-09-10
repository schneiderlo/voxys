#pragma once

#include "gpu/webgpu_compat.hpp"
#include "physics/authored_shape_pool.hpp"

#include <string_view>

namespace voxy::physics {

enum class ShapeResourcePhase : uint8_t { Initializing, Ready, Closing, Closed, Failed };
enum class ShapeResourceState : uint8_t { Missing, Uploading, Ready, Retiring };
enum class ShapeResourceError : uint8_t {
    None, InvalidContext, InvalidLimits, Allocation, Capacity, NotReady,
    InvalidHandle, Busy, InvalidTicket, InvalidCommands, GpuFailure, Closed,
    Unsupported, NotInitialized, AlreadyConfigured,
};
struct ShapeResourceSubmission {
    uint64_t pool = 0, serial = 0;
    [[nodiscard]] bool valid() const noexcept { return pool != 0 && serial != 0; }
    [[nodiscard]] bool operator==(const ShapeResourceSubmission&) const = default;
};
struct ShapeResourceStats {
    ShapeResourcePhase phase = ShapeResourcePhase::Initializing;
    ShapePoolStats cpu{};
    uint64_t gpuBytes = 0, uploadBytes = 0;
    uint32_t pendingOperations = 0, pendingCallbacks = 0, residentRanges = 0;
    bool unresolvedSubmission = false;
};
struct ShapeResourceLimits {
    AuthoredShapePoolLimits cpu{};
    uint64_t gpuBytes = 32 * 1024 * 1024;
};

// Borrowed from one initialized PhysicsWorld. The backend owns this object;
// world shutdown/destruction ends every borrow. A moved world retains it.
// Resource preparation is separate from authored BODY support. A ready shape
// is not permission to spawn it through the legacy primitive-body API.
//
// Single device-owning thread. The caller retains matching compiled revision
// and provenance independently. Close/drain before normal shutdown; release
// every encoder/command and CPU borrow before abandoning a world. Abandonment
// never certifies pending completion and a new world gets a new pool identity.
class IAuthoredShapeResources {
public:
    virtual ~IAuthoredShapeResources() = default;
    // A refused upload preserves the supplied prepared value. None means the
    // upload was submitted; inspect state() after poll() for actual readiness.
    [[nodiscard]] virtual ShapeHandle upload(AuthoredShape&&, ShapeResourceError&) noexcept = 0;
    [[nodiscard]] virtual ShapeResourceState state(ShapeHandle) const noexcept = 0;
    // Retain before keeping a shape borrow through mutations/async events.
    [[nodiscard]] virtual const AuthoredShape* get(ShapeHandle) const noexcept = 0;
    [[nodiscard]] virtual ShapeResourceError retain(ShapeHandle) noexcept = 0;
    [[nodiscard]] virtual ShapeResourceError release(ShapeHandle) noexcept = 0;
    [[nodiscard]] virtual ShapeResourceError retire(ShapeHandle) noexcept = 0;

    // Declare EVERY used shape before creating its encoder. This opens device
    // scopes; balance any nested scopes on the same thread before resolution.
    [[nodiscard]] virtual ShapeResourceSubmission prepareSubmission(
        std::span<const ShapeHandle>, ShapeResourceError&) noexcept = 0;
    // Performs the actual queue submit and registers real scopes/completion.
    // It is not an acknowledgment for an independently submitted command.
    [[nodiscard]] virtual ShapeResourceError submit(ShapeResourceSubmission,
        std::span<const WGPUCommandBuffer>) noexcept = 0;
    // Release all ticket encoders/commands FIRST. Records a real empty submit.
    [[nodiscard]] virtual ShapeResourceError discard(ShapeResourceSubmission) noexcept = 0;
    virtual void poll() noexcept = 0;
    virtual void close() noexcept = 0;
    [[nodiscard]] virtual ShapeResourceStats stats() const noexcept = 0;
    [[nodiscard]] virtual std::string_view failure() const noexcept = 0;
    // Borrowed atlas for a declared submission only. All consumers must use
    // its versioned format and generation-checked views, never primitive casts.
    [[nodiscard]] virtual WGPUBuffer buffer() const noexcept = 0;
};

} // namespace voxy::physics
