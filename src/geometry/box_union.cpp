#include "geometry/box_union.hpp"
#include "geometry/orthogonal_geometry.hpp"

#include <algorithm>
#include <new>
#include <tuple>

namespace voxy::geometry {
namespace {
using namespace detail::orthogonal;
auto order(const UnionBox& box) noexcept {
    return std::tuple{box.source, box.bounds.minimum.x, box.bounds.minimum.y, box.bounds.minimum.z,
        box.bounds.maximum.x, box.bounds.maximum.y, box.bounds.maximum.z};
}
} // namespace

bool BoxUnion::validLimits(BoxUnionLimits limits) noexcept {
    return limits.inputBoxes > 0 && limits.inputBoxes <= kMaximumUnionInputBoxes
        && limits.cells > 0 && limits.cells <= kMaximumUnionCells
        && limits.faces > 0 && limits.faces <= kMaximumUnionFaces
        && limits.scratchPieces > 0 && limits.scratchPieces <= kMaximumUnionCells
        && limits.clipTests <= kMaximumUnionClipTests
        && limits.radiusTicks > 0 && limits.radiusTicks <= kMaximumUnionRadiusTicks;
}
std::optional<BoxUnion> BoxUnion::compile(std::span<const UnionBox> input, BoxUnionIssue& issue, BoxUnionLimits limits) {
    const auto refuse = [&](BoxUnionError error, uint32_t source = 0) -> std::optional<BoxUnion> {
        issue = {error, source}; return std::nullopt;
    };
    if (!validLimits(limits)) return refuse(BoxUnionError::InvalidProfile);
    if (input.empty()) return refuse(BoxUnionError::EmptyInput);
    if (input.size() > limits.inputBoxes) return refuse(BoxUnionError::Capacity);
    for (const auto& box : input) {
        if (box.source == 0) return refuse(BoxUnionError::InvalidSource);
        if (!positive(box.bounds)) return refuse(BoxUnionError::InvalidBox, box.source);
        for (size_t axis = 0; axis < 3; ++axis)
            if (component(box.bounds.minimum, axis) < -limits.radiusTicks
                || component(box.bounds.maximum, axis) > limits.radiusTicks) return refuse(BoxUnionError::InvalidBox, box.source);
    }
    try {
        std::vector<UnionBox> sources(input.begin(), input.end());
        std::sort(sources.begin(), sources.end(), [](const auto& a, const auto& b) { return order(a) < order(b); });
        for (size_t i = 1; i < sources.size(); ++i)
            if (sources[i - 1].source == sources[i].source) return refuse(BoxUnionError::InvalidSource, sources[i].source);
        BoxUnion result;
        const auto charge = [&]() { if (result.stats_.clipTests == limits.clipTests) return false; ++result.stats_.clipTests; return true; };
        std::vector<GridBox> pieces, nextPieces;
        for (const auto& source : sources) {
            pieces.clear(); if (!append(pieces, source.bounds, limits.scratchPieces)) return refuse(BoxUnionError::Capacity, source.source);
            for (const auto& retained : result.cells_) {
                if (pieces.empty()) break;
                nextPieces.clear();
                for (auto piece : pieces) {
                    if (!charge()) return refuse(BoxUnionError::WorkLimit, source.source);
                    const auto split = subtract(piece, retained.bounds);
                    for (size_t i = 0; i < split.count; ++i)
                        if (!append(nextPieces, split.boxes[i], limits.scratchPieces)) return refuse(BoxUnionError::Capacity, source.source);
                }
                pieces.swap(nextPieces);
            }
            for (auto piece : pieces)
                if (!append(result.cells_, UnionBox{piece, source.source}, limits.cells)) return refuse(BoxUnionError::Capacity, source.source);
        }
        std::sort(result.cells_.begin(), result.cells_.end(), [](const auto& a, const auto& b) { return order(a) < order(b); });
        // Faces can only be hidden by cells with an opposite face on the
        // exact same plane. Index those planes once, retaining source/cell
        // order within each plane so the canonical decomposition is unchanged.
        std::array<std::vector<size_t>,6> planes;
        for(size_t axis=0;axis<3;++axis)for(size_t side=0;side<2;++side) {
            auto& indices=planes[2*axis+side];indices.reserve(result.cells_.size());
            for(size_t i=0;i<result.cells_.size();++i)indices.push_back(i);
            const auto coordinate=[&](size_t i){return component(side?result.cells_[i].bounds.minimum:result.cells_[i].bounds.maximum,axis);};
            std::stable_sort(indices.begin(),indices.end(),[&](size_t a,size_t b){return coordinate(a)<coordinate(b);});
        }
        for (size_t cell = 0; cell < result.cells_.size(); ++cell) {
            const auto& source = result.cells_[cell];
            result.stats_.volumeTicks3 += extent(source.bounds, 0) * extent(source.bounds, 1) * extent(source.bounds, 2);
            for (uint8_t axis = 0; axis < 3; ++axis) for (int8_t sign : {int8_t{-1}, int8_t{1}}) {
                const int32_t plane = component(sign < 0 ? source.bounds.minimum : source.bounds.maximum, axis);
                auto face = source.bounds; component(face.minimum, axis, plane); component(face.maximum, axis, plane);
                pieces.clear(); if (!append(pieces, face, limits.scratchPieces)) return refuse(BoxUnionError::Capacity, source.source);
                const auto& indices=planes[2*axis+(sign>0?1u:0u)];
                const auto opposite=[&](size_t i){return component(sign<0?result.cells_[i].bounds.maximum:result.cells_[i].bounds.minimum,axis);};
                auto candidate=std::lower_bound(indices.begin(),indices.end(),plane,[&](size_t i,int32_t p){return opposite(i)<p;});
                for (;candidate!=indices.end()&&opposite(*candidate)==plane;++candidate) {
                    if (pieces.empty()) break;
                    const size_t other=*candidate;
                    if (cell == other) continue;
                    if (!charge()) return refuse(BoxUnionError::WorkLimit, source.source);
                    const auto& adjacent = result.cells_[other].bounds;
                    nextPieces.clear();
                    for (auto piece : pieces) {
                        if (!charge()) return refuse(BoxUnionError::WorkLimit, source.source);
                        const auto split = subtractFace(piece, adjacent, axis);
                        for (size_t i = 0; i < split.count; ++i)
                            if (!append(nextPieces, split.boxes[i], limits.scratchPieces)) return refuse(BoxUnionError::Capacity, source.source);
                    }
                    pieces.swap(nextPieces);
                }
                for (auto piece : pieces) {
                    if (!append(result.faces_, UnionFace{piece, source.source, static_cast<uint32_t>(cell), axis, sign}, limits.faces))
                        return refuse(BoxUnionError::Capacity, source.source);
                    result.stats_.surfaceAreaTicks2 += extent(piece, (axis + 1u) % 3u) * extent(piece, (axis + 2u) % 3u);
                }
            }
        }
        // All positive boxes have nonempty union. A binary tree has exactly
        // 2*N-1 nodes; reserve once and never retain references across recursion.
        result.bvh_.reserve(result.cells_.size() * 2 - 1);
        std::vector<uint32_t> indices; indices.reserve(result.cells_.size());
        for (size_t i = 0; i < result.cells_.size(); ++i) indices.push_back(static_cast<uint32_t>(i));
        const auto buildNode = [&](auto&& self, size_t first, size_t last) -> void {
            const auto node = result.bvh_.size();
            auto bounds = result.cells_[indices[first]].bounds;
            for (size_t i = first + 1; i < last; ++i) bounds = enclosing(bounds, result.cells_[indices[i]].bounds);
            result.bvh_.push_back({bounds, kUnionInternalNode, 0});
            if (last - first == 1) result.bvh_[node].cell = indices[first];
            else {
                size_t axis = 0;
                for (size_t candidate = 1; candidate < 3; ++candidate)
                    if (extent(bounds, candidate) > extent(bounds, axis)) axis = candidate;
                const size_t middle = first + (last - first) / 2;
                const auto byCenter = [&](uint32_t a, uint32_t b) {
                    const auto& ab = result.cells_[a].bounds; const auto& bb = result.cells_[b].bounds;
                    const auto ac = component(ab.minimum, axis) + component(ab.maximum, axis);
                    const auto bc = component(bb.minimum, axis) + component(bb.maximum, axis);
                    return std::pair{ac, a} < std::pair{bc, b};
                };
                std::nth_element(indices.begin() + static_cast<std::ptrdiff_t>(first), indices.begin() + static_cast<std::ptrdiff_t>(middle),
                    indices.begin() + static_cast<std::ptrdiff_t>(last), byCenter);
                self(self, first, middle); self(self, middle, last);
            }
            result.bvh_[node].escape = static_cast<uint32_t>(result.bvh_.size());
        };
        buildNode(buildNode, 0, result.cells_.size());
        issue = {}; return result;
    } catch (const std::bad_alloc&) { return refuse(BoxUnionError::Capacity); }
}

UnionQueryResult BoxUnion::query(GridBox bounds, std::span<uint32_t> output) const noexcept {
    if (!isRotatableGridPosition(bounds.minimum) || !isRotatableGridPosition(bounds.maximum)
        || bounds.minimum.x > bounds.maximum.x || bounds.minimum.y > bounds.maximum.y || bounds.minimum.z > bounds.maximum.z)
        return {UnionQueryStatus::InvalidBounds, 0, 0};
    UnionQueryResult result;
    const auto traverse = [&](bool copy) {
        size_t count = 0;
        for (uint32_t node = 0; node < bvh_.size();) {
            const auto& value = bvh_[node]; ++result.visitedNodes;
            if (!closedOverlap(bounds, value.bounds)) { node = value.escape; continue; }
            if (value.cell != kUnionInternalNode) {
                if (copy) output[count] = value.cell;
                ++count;
            }
            ++node;
        }
        return count;
    };
    result.count = traverse(false);
    if (result.count > output.size()) { result.status = UnionQueryStatus::NeedCapacity; return result; }
    if (result.count) static_cast<void>(traverse(true));
    return result;
}
} // namespace voxy::geometry
