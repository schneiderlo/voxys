#pragma once

#include "geometry/grid_types.hpp"
#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>
#include <span>

namespace voxy::game::adventure {
// .02 m lattice, in the unchanged doorway's bottom-centred local coordinates.
// The separately authored leaf mesh uses the CLOSED box directly. Opening is
// a discrete accepted -90 degree Y pose, extending toward local +Z.
inline constexpr geometry::GridPosition kDoorHinge{-32,2,0};
inline constexpr geometry::GridBox kClosedDoorLeaf{{-32,2,-2},{32,110,2}};
inline constexpr geometry::GridBox kOpenDoorLeaf{{-34,2,0},{-30,110,64}};
[[nodiscard]] const geometry::GridBox& doorLeafBox(bool open) noexcept;
[[nodiscard]] glm::dmat4 doorLeafTransform(bool open) noexcept;
[[nodiscard]] glm::dvec3 doorHandlePoint(bool open) noexcept;
// Sixteen outward-rounded AABBs contain the complete continuous quarter-turn,
// not just sampled poses. Used for conservative clearance, never as walking
// colliders or support. They reserve future opening when a door is built.
[[nodiscard]] std::span<const geometry::GridBox> doorSweepBoxes() noexcept;
} // namespace voxy::game::adventure
