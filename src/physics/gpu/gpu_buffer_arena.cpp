#include "physics/gpu/gpu_buffer_arena.hpp"

#include "core/log.hpp"
#include "gpu/resources.hpp"

#include <limits>

namespace voxy::physics {

GpuBufferArena::~GpuBufferArena() { shutdown(); }

void GpuBufferArena::initialize(WGPUDevice device) noexcept {
    shutdown();
    device_ = device;
}

WGPUBuffer GpuBufferArena::create(std::string_view label, uint64_t size,
                                  uint64_t usage, bool scratch) {
    if (!device_ || size == 0) {
        return nullptr;
    }
    if (size > std::numeric_limits<size_t>::max()) {
        LOG_ERROR("GPU physics buffer '{}' cannot be represented in host "
                  "memory telemetry ({} bytes)", label, size);
        return nullptr;
    }
    const size_t accountedSize = static_cast<size_t>(size);
    const size_t previousBytes = scratch ? scratchBytes_ : persistentBytes_;
    if (accountedSize > std::numeric_limits<size_t>::max() - previousBytes) {
        LOG_ERROR("GPU physics buffer '{}' would overflow memory telemetry",
                  label);
        return nullptr;
    }

    const gpu::BufferDesc desc{
        .label = label,
        .size = size,
        .usage = static_cast<decltype(gpu::BufferDesc{}.usage)>(usage),
    };
    // Reserve the owning record before creating the WebGPU object. If vector
    // growth throws, there is no live GPU allocation to leak.
    allocations_.push_back({nullptr, size, scratch});
    WGPUBuffer buffer = gpu::createBuffer(device_, desc);
    if (!buffer) {
        allocations_.pop_back();
        LOG_ERROR("Failed to allocate GPU physics buffer '{}' ({} bytes)",
                  label, size);
        return nullptr;
    }
    allocations_.back().buffer = buffer;
    if (scratch) {
        scratchBytes_ += accountedSize;
    } else {
        persistentBytes_ += accountedSize;
    }
    return buffer;
}

void GpuBufferArena::shutdown() {
    for (auto& allocation : allocations_) {
        if (allocation.buffer) {
            wgpuBufferDestroy(allocation.buffer);
            wgpuBufferRelease(allocation.buffer);
        }
    }
    allocations_.clear();
    persistentBytes_ = 0;
    scratchBytes_ = 0;
    device_ = nullptr;
}

} // namespace voxy::physics
