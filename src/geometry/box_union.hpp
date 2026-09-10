#pragma once

#include "geometry/grid_types.hpp"
#include <array>
#include <cstddef>
#include <optional>
#include <span>
#include <vector>

namespace voxy::geometry {

inline constexpr size_t kMaximumUnionInputBoxes = 2048;
inline constexpr size_t kMaximumUnionCells = 4096;
inline constexpr size_t kMaximumUnionFaces = 24576;
inline constexpr uint64_t kMaximumUnionClipTests = 8388608;
inline constexpr int32_t kMaximumUnionRadiusTicks = 12800;

struct BoxUnionLimits {
    size_t inputBoxes = kMaximumUnionInputBoxes;
    size_t cells = kMaximumUnionCells;
    size_t faces = kMaximumUnionFaces;
    size_t scratchPieces = kMaximumUnionCells;
    uint64_t clipTests = kMaximumUnionClipTests;
    int32_t radiusTicks = kMaximumUnionRadiusTicks;
};
struct UnionBox {
    GridBox bounds{}; // Positive half-open volume in root-local lattice ticks.
    uint32_t source = 0; // Nonzero unique input label; retained cells may share it.
    [[nodiscard]] bool operator==(const UnionBox&) const = default;
};
struct UnionFace {
    GridBox bounds{}; // Collapsed on axis; positive extents on both other axes.
    uint32_t source = 0;
    uint32_t cell = 0;
    uint8_t axis = 0;
    int8_t sign = 1; // Outward normal; never a solver impulse.
    [[nodiscard]] bool operator==(const UnionFace&) const = default;
};
inline constexpr uint32_t kUnionInternalNode = UINT32_MAX;
struct UnionBvhNode {
    GridBox bounds{};
    uint32_t cell = kUnionInternalNode;
    uint32_t escape = 0; // First node after this preorder subtree.
    [[nodiscard]] bool operator==(const UnionBvhNode&) const = default;
};
struct BoxUnionStats {
    uint64_t clipTests = 0;
    uint64_t volumeTicks3 = 0;
    uint64_t surfaceAreaTicks2 = 0;
    [[nodiscard]] bool operator==(const BoxUnionStats&) const = default;
};
enum class BoxUnionError : uint8_t { None, InvalidProfile, EmptyInput, InvalidBox, InvalidSource, Capacity, WorkLimit };
struct BoxUnionIssue { BoxUnionError error = BoxUnionError::None; uint32_t source = 0; };
enum class UnionQueryStatus : uint8_t { Complete, NeedCapacity, InvalidBounds };
struct UnionQueryResult {
    UnionQueryStatus status = UnionQueryStatus::Complete;
    size_t count = 0; // Required capacity, also valid on NeedCapacity.
    uint32_t visitedNodes = 0;
};

// Exact disjoint volumes plus exterior patches. Raw cells alone are NOT safe
// unmasked compound contacts: the solver must honor the exterior boundary.
class BoxUnion {
public:
    [[nodiscard]] static bool validLimits(BoxUnionLimits) noexcept;
    [[nodiscard]] static std::optional<BoxUnion> compile(std::span<const UnionBox>, BoxUnionIssue&, BoxUnionLimits = {});
    [[nodiscard]] std::span<const UnionBox> cells() const noexcept { return cells_; }
    [[nodiscard]] std::span<const UnionFace> faces() const noexcept { return faces_; }
    [[nodiscard]] std::span<const UnionBvhNode> bvh() const noexcept { return bvh_; }
    [[nodiscard]] BoxUnionStats stats() const noexcept { return stats_; }
    // Closed AABB overlap (including touching/point bounds), broad-phase only.
    // On failure output is untouched. Successful output is in BVH traversal order.
    [[nodiscard]] UnionQueryResult query(GridBox bounds, std::span<uint32_t> output) const noexcept;

private:
    BoxUnion() = default;
    std::vector<UnionBox> cells_{};
    std::vector<UnionFace> faces_{};
    std::vector<UnionBvhNode> bvh_{};
    BoxUnionStats stats_{};
};
} // namespace voxy::geometry
