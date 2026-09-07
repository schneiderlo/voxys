#pragma once

#include "physics/physics_types.hpp"
#include <limits>

namespace voxy::app_detail {

// Retired slots remain in the allocation after the renderer's live index range
// shrinks. Reset must inspect those old generations, so draw population is not
// a valid bound for a lifetime metadata copy.
[[nodiscard]] inline bool salvageMetadataRangeReadable(
    WGPUBuffer metadataBuffer, uint32_t bodyCount) noexcept {
    return metadataBuffer && bodyCount != 0u
        && bodyCount <= std::numeric_limits<size_t>::max() / sizeof(glm::uvec4)
        && uint64_t{bodyCount} * sizeof(glm::uvec4) <= wgpuBufferGetSize(metadataBuffer);
}

// Physics allocates this buffer once at backend initialization; Reset/Leave
// never resize or replace it. Capture while fixtures exist because renderView()
// deliberately returns an empty view when the last body retires. Retain the API
// handle across that empty interval, then clear before PhysicsWorld shutdown:
// the world alone owns/destroys the underlying allocation.
class SalvageMetadataReadbackSource final {
public:
    SalvageMetadataReadbackSource() = default;
    ~SalvageMetadataReadbackSource() { clear(); }
    SalvageMetadataReadbackSource(const SalvageMetadataReadbackSource&) = delete;
    SalvageMetadataReadbackSource& operator=(const SalvageMetadataReadbackSource&) = delete;

    [[nodiscard]] bool capture(WGPUBuffer buffer) noexcept {
        if (!salvageMetadataRangeReadable(buffer, 1u)) {
            clear();
            return false;
        }
#if defined(__EMSCRIPTEN__)
        wgpuBufferAddRef(buffer);
#else
        wgpuBufferReference(buffer);
#endif
        clear();
        buffer_ = buffer;
        return true;
    }

    void clear() noexcept {
        if (buffer_) wgpuBufferRelease(buffer_);
        buffer_ = nullptr;
    }

    [[nodiscard]] WGPUBuffer readableBuffer(uint32_t bodyCount) const noexcept {
        return salvageMetadataRangeReadable(buffer_, bodyCount) ? buffer_ : nullptr;
    }

private:
    WGPUBuffer buffer_ = nullptr;
};

} // namespace voxy::app_detail
