#pragma once

#include "physics/authored_shape_resources.hpp"

#include <memory>
#include <string_view>

namespace voxy::physics {

// One read-only vec4<u32> storage binding. Integers and f32 are uploaded by
// their exact bit patterns. Descriptor index zero is always invalid.
struct alignas(16) GpuShapeHeapHeader {
    std::array<uint32_t,4> layout{}; // format, slot capacity, cell base row, face base row
    std::array<uint32_t,4> capacities{}; // node base row, total rows, cell capacity, face capacity
};
struct alignas(16) GpuShapeDescriptor {
    std::array<uint32_t,4> identity{}; // generation, format, first cell, cell count
    std::array<uint32_t,4> ranges{}; // first face, face count, first node, node count
    PackedShapeMass mass{};
    std::array<int32_t,4> minimum{};
    std::array<int32_t,4> maximum{};
};
static_assert(sizeof(GpuShapeHeapHeader)==32 && sizeof(GpuShapeDescriptor)==112);
static_assert(offsetof(GpuShapeDescriptor,mass)==32 && offsetof(GpuShapeDescriptor,minimum)==80);

using GpuShapePhase = ShapeResourcePhase;
using GpuShapeState = ShapeResourceState;
using GpuShapeError = ShapeResourceError;
using GpuShapeSubmission = ShapeResourceSubmission;
using GpuShapeStoreStats = ShapeResourceStats;
using GpuShapeStoreLimits = ShapeResourceLimits;

// Single-owner atlas and real queue lifetime. Up to eight checked GPU operations
// may be pending; one external encoder submission may be unresolved. Callback
// storage is preallocated and callback code touches only independent CPU state.
// The caller owns the matching compiled revision/provenance bindings.
class GpuAuthoredShapeStore final : public IAuthoredShapeResources {
public:
    [[nodiscard]] static std::unique_ptr<GpuAuthoredShapeStore> create(
        WGPUDevice, uint64_t uniquePoolIdentity, GpuShapeError&,
        GpuShapeStoreLimits = {});
    ~GpuAuthoredShapeStore() override;
    GpuAuthoredShapeStore(const GpuAuthoredShapeStore&) = delete;
    GpuAuthoredShapeStore& operator=(const GpuAuthoredShapeStore&) = delete;
    GpuAuthoredShapeStore(GpuAuthoredShapeStore&&) = delete;
    GpuAuthoredShapeStore& operator=(GpuAuthoredShapeStore&&) = delete;

    // Initial creation and each upload complete asynchronously through poll().
    // A refused upload does not consume the caller's prepared shape.
    [[nodiscard]] ShapeHandle upload(AuthoredShape&&, GpuShapeError&) noexcept override;
    [[nodiscard]] GpuShapeState state(ShapeHandle) const noexcept override;
    [[nodiscard]] const AuthoredShape* get(ShapeHandle) const noexcept override;
    [[nodiscard]] GpuShapeError retain(ShapeHandle) noexcept override;
    [[nodiscard]] GpuShapeError release(ShapeHandle) noexcept override;
    [[nodiscard]] GpuShapeError retire(ShapeHandle) noexcept override;

    // Reserve BEFORE creating/encoding work using these shape handles. This
    // opens validation scopes, so all calls on this device must remain on the
    // owning thread until submit/discard resolves the ticket. Nested scopes
    // must also be balanced. All buffer consumers must declare their shapes.
    [[nodiscard]] GpuShapeSubmission prepareSubmission(
        std::span<const ShapeHandle>, GpuShapeError&) noexcept override;
    // Executes the actual queue submission, then observes its scope results and
    // queue completion. There is no API to inject an estimated completed serial.
    [[nodiscard]] GpuShapeError submit(GpuShapeSubmission,
        std::span<const WGPUCommandBuffer>) noexcept override;
    // Release every encoder/command referring to the ticket before discarding.
    // A real empty submission/fence safely retires the reserved unused serial.
    [[nodiscard]] GpuShapeError discard(GpuShapeSubmission) noexcept override;
    void poll() noexcept override;
    // Blocks new admission and retires all shapes. Resolve an outstanding
    // ticket and release retained readers; poll until Closed. Failure is sticky.
    void close() noexcept override;
    [[nodiscard]] GpuShapeStoreStats stats() const noexcept override;
    [[nodiscard]] std::string_view failure() const noexcept override;
    // Borrowed buffer for a declared submission. It remains stable until close;
    // it is not permission to encode undeclared or stale shape references.
    [[nodiscard]] WGPUBuffer buffer() const noexcept override;
    [[nodiscard]] const GpuShapeHeapHeader& layout() const noexcept;
private:
    class Impl;
    explicit GpuAuthoredShapeStore(std::unique_ptr<Impl>);
    std::unique_ptr<Impl> impl_;
};
} // namespace voxy::physics
