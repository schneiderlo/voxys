#pragma once

#include <cstdint>

namespace voxy::geometry {
// Integer coordinates carry no unit or game identity. Each caller owns its
// scale and range contract; construction uses 50 ticks per metre.
struct GridPosition {
    int32_t x = 0;
    int32_t y = 0;
    int32_t z = 0;
    [[nodiscard]] bool operator==(const GridPosition&) const = default;
};
// The exact union query contract excludes INT32_MIN, preserving its former
// construction range and safe negation during proper rotations.
[[nodiscard]] constexpr bool isRotatableGridPosition(GridPosition p) noexcept {
    return p.x != INT32_MIN && p.y != INT32_MIN && p.z != INT32_MIN;
}
struct GridBox {
    GridPosition minimum{};
    GridPosition maximum{};
    [[nodiscard]] bool operator==(const GridBox&) const = default;
};
} // namespace voxy::geometry
