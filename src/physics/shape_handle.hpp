#pragma once

#include <compare>
#include <cstdint>

namespace voxy::physics {
// Runtime identity only. The owner assigns a fresh nonzero pool identity on
// each world/backend incarnation; it must never reuse one while handles exist.
// GPU descriptors use index/generation inside the already selected pool.
struct ShapeHandle {
    uint32_t index = 0;
    uint32_t generation = 0;
    uint64_t pool = 0;
    [[nodiscard]] constexpr bool valid() const noexcept {
        return index != 0 && generation != 0 && pool != 0;
    }
    [[nodiscard]] constexpr auto operator<=>(const ShapeHandle&) const noexcept = default;
};
} // namespace voxy::physics
