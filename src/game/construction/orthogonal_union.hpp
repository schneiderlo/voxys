#pragma once

#include "game/construction/part_catalog.hpp"
#include "geometry/box_union.hpp"

// Compatibility names; exact union preparation is shared with physics.
namespace voxy::game::construction {
using geometry::kMaximumUnionInputBoxes;
using geometry::kMaximumUnionCells;
using geometry::kMaximumUnionFaces;
using geometry::kMaximumUnionClipTests;
using geometry::kMaximumUnionRadiusTicks;
using geometry::BoxUnionLimits;
using geometry::UnionBox;
using geometry::UnionFace;
using geometry::kUnionInternalNode;
using geometry::UnionBvhNode;
using geometry::BoxUnionStats;
using geometry::BoxUnionError;
using geometry::BoxUnionIssue;
using geometry::UnionQueryStatus;
using geometry::UnionQueryResult;
using geometry::BoxUnion;
} // namespace voxy::game::construction
