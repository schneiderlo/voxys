#include "physics/gpu/gpu_buffer_arena.hpp"

#include "core/log.hpp"
#include "gpu/resources.hpp"

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

    const gpu::BufferDesc desc{
        .label = label,
        .size = size,
        .usage = static_cast<decltype(gpu::BufferDesc{}.usage)>(usage),
    };
    WGPUBuffer buffer = gpu::createBuffer(device_, desc);
    if (!buffer) {
        LOG_ERROR("Failed to allocate GPU physics buffer '{}' ({} bytes)",
                  label, size);
        return nullptr;
    }
    allocations_.push_back({buffer, size, scratch});
    if (scratch) {
        scratchBytes_ += static_cast<size_t>(size);
    } else {
        persistentBytes_ += static_cast<size_t>(size);
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
