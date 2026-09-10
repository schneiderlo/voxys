#include "game/construction/orthogonal_coverage.hpp"
#include "game/construction/orthogonal_geometry.hpp"

#include <algorithm>
#include <new>
#include <tuple>

namespace voxy::game::construction {
namespace {
using namespace detail::orthogonal;
struct WorkingCoverage {
    std::vector<CoverageCell> cells;
    std::vector<uint32_t> references;
    void clear() noexcept { cells.clear(); references.clear(); }
    [[nodiscard]] std::span<const uint32_t> contributors(const CoverageCell& cell) const noexcept {
        return std::span{references}.subspan(cell.firstContributor, cell.contributorCount);
    }
};
auto order(GridBox box) noexcept {
    return std::tuple{box.minimum.x, box.minimum.y, box.minimum.z, box.maximum.x, box.maximum.y, box.maximum.z};
}
} // namespace

bool BoxCoverage::validLimits(BoxCoverageLimits limits) noexcept {
    return limits.inputBoxes <= kMaximumUnionInputBoxes && limits.cells <= kMaximumUnionCells
        && limits.references <= kMaximumCoverageReferences && limits.scratchPieces > 0
        && limits.scratchPieces <= kMaximumUnionCells && limits.clipTests <= kMaximumUnionClipTests
        && limits.referenceWrites <= kMaximumCoverageReferenceWrites
        && limits.radiusTicks > 0 && limits.radiusTicks <= kMaximumUnionRadiusTicks;
}
std::optional<BoxCoverage> BoxCoverage::compile(std::span<const UnionBox> input, BoxCoverageIssue& issue, BoxCoverageLimits limits) {
    const auto refuse = [&](BoxCoverageError error, uint32_t source = 0) -> std::optional<BoxCoverage> {
        issue = {error, source}; return std::nullopt;
    };
    if (!validLimits(limits)) return refuse(BoxCoverageError::InvalidProfile);
    if (input.size() > limits.inputBoxes) return refuse(BoxCoverageError::Capacity);
    if (input.empty()) { issue = {}; return BoxCoverage{}; }
    if (limits.cells == 0 || limits.references == 0) return refuse(BoxCoverageError::Capacity);
    for (const auto& box : input) {
        if (box.source == 0) return refuse(BoxCoverageError::InvalidSource);
        if (!positive(box.bounds)) return refuse(BoxCoverageError::InvalidBox, box.source);
        for (size_t axis = 0; axis < 3; ++axis)
            if (component(box.bounds.minimum, axis) < -limits.radiusTicks
                || component(box.bounds.maximum, axis) > limits.radiusTicks) return refuse(BoxCoverageError::InvalidBox, box.source);
    }
    try {
        std::vector<UnionBox> sources(input.begin(), input.end());
        std::sort(sources.begin(), sources.end(), [](const auto& a, const auto& b) { return a.source < b.source; });
        for (size_t i = 1; i < sources.size(); ++i)
            if (sources[i - 1].source == sources[i].source) return refuse(BoxCoverageError::InvalidSource, sources[i].source);
        BoxCoverage result;
        WorkingCoverage current, next;
        std::vector<GridBox> pieces, nextPieces;
        const auto chargeClip = [&]() {
            if (result.stats_.clipTests == limits.clipTests) return false;
            ++result.stats_.clipTests; return true;
        };
        BoxCoverageError appendError = BoxCoverageError::None;
        const auto addCell = [&](WorkingCoverage& destination, GridBox bounds, std::span<const uint32_t> contributors, uint32_t extra) {
            const size_t count = contributors.size() + (extra != 0 ? size_t{1} : size_t{0});
            if (destination.cells.size() == limits.cells || count > limits.references - destination.references.size()) {
                appendError = BoxCoverageError::Capacity; return false;
            }
            if (count > limits.referenceWrites - result.stats_.referenceWrites) { appendError = BoxCoverageError::WorkLimit; return false; }
            const auto first = static_cast<uint32_t>(destination.references.size());
            result.stats_.referenceWrites += count;
            for (auto source : contributors) static_cast<void>(append(destination.references, source, limits.references));
            if (extra != 0) static_cast<void>(append(destination.references, extra, limits.references));
            static_cast<void>(append(destination.cells, CoverageCell{bounds, first, static_cast<uint32_t>(count)}, limits.cells));
            return true;
        };
        for (const auto& source : sources) {
            next.clear(); pieces.clear();
            if (!append(pieces, source.bounds, limits.scratchPieces)) return refuse(BoxCoverageError::Capacity, source.source);
            for (const auto& cell : current.cells) {
                if (!chargeClip()) return refuse(BoxCoverageError::WorkLimit, source.source);
                const auto common = intersection(cell.bounds, source.bounds);
                const auto labels = current.contributors(cell);
                if (!positive(common)) {
                    if (!addCell(next, cell.bounds, labels, 0)) return refuse(appendError, source.source);
                } else {
                    const auto residual = subtract(cell.bounds, source.bounds);
                    for (size_t i = 0; i < residual.count; ++i)
                        if (!addCell(next, residual.boxes[i], labels, 0)) return refuse(appendError, source.source);
                    if (!addCell(next, common, labels, source.source)) return refuse(appendError, source.source);
                }
                if (pieces.empty()) continue;
                nextPieces.clear();
                for (const auto& piece : pieces) {
                    if (!chargeClip()) return refuse(BoxCoverageError::WorkLimit, source.source);
                    const auto residual = subtract(piece, cell.bounds);
                    for (size_t i = 0; i < residual.count; ++i)
                        if (!append(nextPieces, residual.boxes[i], limits.scratchPieces)) return refuse(BoxCoverageError::Capacity, source.source);
                }
                pieces.swap(nextPieces);
            }
            for (auto piece : pieces)
                if (!addCell(next, piece, {}, source.source)) return refuse(appendError, source.source);
            std::swap(current, next);
        }
        std::sort(current.cells.begin(), current.cells.end(), [](const auto& a, const auto& b) { return order(a.bounds) < order(b.bounds); });
        next.clear();
        for (const auto& cell : current.cells) {
            const auto labels = current.contributors(cell);
            if (!addCell(next, cell.bounds, labels, 0)) return refuse(appendError);
            result.stats_.volumeTicks3 += extent(cell.bounds, 0) * extent(cell.bounds, 1) * extent(cell.bounds, 2);
            result.stats_.maximumContributors = std::max(result.stats_.maximumContributors, cell.contributorCount);
        }
        result.cells_ = std::move(next.cells); result.references_ = std::move(next.references);
        issue = {}; return result;
    } catch (const std::bad_alloc&) { return refuse(BoxCoverageError::Capacity); }
}
} // namespace voxy::game::construction
