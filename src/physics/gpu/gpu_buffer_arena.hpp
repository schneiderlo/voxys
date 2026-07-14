#pragma once

#include "gpu/webgpu_compat.hpp"

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

namespace voxy::physics {

class GpuBufferArena {
public:
    GpuBufferArena() = default;
    ~GpuBufferArena();

    GpuBufferArena(const GpuBufferArena&) = delete;
    GpuBufferArena& operator=(const GpuBufferArena&) = delete;

    void initialize(WGPUDevice device) noexcept;
    [[nodiscard]] WGPUBuffer create(std::string_view label, uint64_t size,
                                    uint64_t usage,
                                    bool scratch = false);
    void shutdown();

    [[nodiscard]] size_t persistentBytes() const noexcept {
        return persistentBytes_;
    }
    [[nodiscard]] size_t scratchBytes() const noexcept { return scratchBytes_; }

private:
    struct Allocation {
        WGPUBuffer buffer = nullptr;
        uint64_t size = 0;
        bool scratch = false;
    };

    WGPUDevice device_ = nullptr;
    std::vector<Allocation> allocations_;
    size_t persistentBytes_ = 0;
    size_t scratchBytes_ = 0;
};

} // namespace voxy::physics
