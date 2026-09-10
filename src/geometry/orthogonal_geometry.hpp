#pragma once

#include "geometry/grid_types.hpp"
#include <array>
#include <cstddef>
#include <optional>
#include <span>
#include <vector>
#include <algorithm>

// Internal exact-grid primitives. Callers validate bounded positive boxes
// before subtraction/extent arithmetic; face subtraction permits one zero axis.
namespace voxy::geometry::detail::orthogonal {
inline int32_t component(GridPosition p, size_t axis) noexcept { return axis == 0 ? p.x : axis == 1 ? p.y : p.z; }
inline void component(GridPosition& p, size_t axis, int32_t value) noexcept {
    if (axis == 0) p.x = value; else if (axis == 1) p.y = value; else p.z = value;
}
inline bool positive(GridBox box) noexcept {
    return box.minimum.x < box.maximum.x && box.minimum.y < box.maximum.y && box.minimum.z < box.maximum.z;
}
inline GridBox intersection(GridBox a, GridBox b) noexcept {
    for (size_t axis = 0; axis < 3; ++axis) {
        component(a.minimum, axis, std::max(component(a.minimum, axis), component(b.minimum, axis)));
        component(a.maximum, axis, std::min(component(a.maximum, axis), component(b.maximum, axis)));
    }
    return a;
}
inline GridBox enclosing(GridBox a, GridBox b) noexcept {
    for (size_t axis = 0; axis < 3; ++axis) {
        component(a.minimum, axis, std::min(component(a.minimum, axis), component(b.minimum, axis)));
        component(a.maximum, axis, std::max(component(a.maximum, axis), component(b.maximum, axis)));
    }
    return a;
}
inline bool closedOverlap(GridBox a, GridBox b) noexcept {
    return a.minimum.x <= b.maximum.x && b.minimum.x <= a.maximum.x
        && a.minimum.y <= b.maximum.y && b.minimum.y <= a.maximum.y
        && a.minimum.z <= b.maximum.z && b.minimum.z <= a.maximum.z;
}
template<class T> bool append(std::vector<T>& values, const T& value, size_t capacity) {
    if (values.size() == capacity) return false;
    // Explicit growth keeps even non-power-of-two capacity profiles bounded.
    if (values.size() == values.capacity()) values.reserve(std::min(capacity, std::max(size_t{1}, values.capacity() * 2)));
    values.push_back(value); return true;
}
struct Pieces { std::array<GridBox, 6> boxes{}; size_t count = 0; };
inline Pieces subtract(GridBox box, GridBox cutter) noexcept {
    Pieces result; const auto common = intersection(box, cutter);
    if (!positive(common)) { result.boxes[result.count++] = box; return result; }
    for (size_t axis = 0; axis < 3; ++axis) {
        if (component(box.minimum, axis) < component(common.minimum, axis)) {
            auto slab = box; component(slab.maximum, axis, component(common.minimum, axis));
            result.boxes[result.count++] = slab; component(box.minimum, axis, component(common.minimum, axis));
        }
        if (component(box.maximum, axis) > component(common.maximum, axis)) {
            auto slab = box; component(slab.minimum, axis, component(common.maximum, axis));
            result.boxes[result.count++] = slab; component(box.maximum, axis, component(common.maximum, axis));
        }
    }
    return result;
}
inline Pieces subtractFace(GridBox face, GridBox cutter, size_t normalAxis) noexcept {
    Pieces result; auto common = intersection(face, cutter);
    const size_t u = (normalAxis + 1) % 3, v = (normalAxis + 2) % 3;
    if (component(common.minimum, u) >= component(common.maximum, u)
        || component(common.minimum, v) >= component(common.maximum, v)) {
        result.boxes[result.count++] = face; return result;
    }
    for (size_t axis : {u, v}) {
        if (component(face.minimum, axis) < component(common.minimum, axis)) {
            auto slab = face; component(slab.maximum, axis, component(common.minimum, axis));
            result.boxes[result.count++] = slab; component(face.minimum, axis, component(common.minimum, axis));
        }
        if (component(face.maximum, axis) > component(common.maximum, axis)) {
            auto slab = face; component(slab.minimum, axis, component(common.maximum, axis));
            result.boxes[result.count++] = slab; component(face.maximum, axis, component(common.maximum, axis));
        }
    }
    return result;
}
inline uint64_t extent(GridBox box, size_t axis) noexcept {
    return static_cast<uint64_t>(component(box.maximum, axis) - component(box.minimum, axis));
}
} // namespace voxy::geometry::detail::orthogonal
