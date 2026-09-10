#pragma once

#include "game/construction/orthogonal_union.hpp"

namespace voxy::game::construction {
inline constexpr size_t kMaximumCoverageReferences = 32768;
inline constexpr uint64_t kMaximumCoverageReferenceWrites = 8388608;
struct BoxCoverageLimits {
    size_t inputBoxes = kMaximumUnionInputBoxes;
    size_t cells = kMaximumUnionCells;
    size_t references = kMaximumCoverageReferences;
    size_t scratchPieces = kMaximumUnionCells;
    uint64_t clipTests = kMaximumUnionClipTests;
    uint64_t referenceWrites = kMaximumCoverageReferenceWrites;
    int32_t radiusTicks = kMaximumUnionRadiusTicks;
};
struct CoverageCell {
    GridBox bounds{};
    uint32_t firstContributor = 0, contributorCount = 0;
    [[nodiscard]] bool operator==(const CoverageCell&) const = default;
};
struct BoxCoverageStats {
    uint64_t clipTests = 0, referenceWrites = 0, volumeTicks3 = 0;
    uint32_t maximumContributors = 0;
    [[nodiscard]] bool operator==(const BoxCoverageStats&) const = default;
};
enum class BoxCoverageError : uint8_t { None, InvalidProfile, InvalidBox, InvalidSource, Capacity, WorkLimit };
struct BoxCoverageIssue { BoxCoverageError error = BoxCoverageError::None; uint32_t source = 0; };

// Immutable disjoint arrangement with every covering source retained. Unlike
// collision union ownership, disabling one contributor cannot erase another.
// Empty input is a valid empty displacement set. No live flood/force policy.
class BoxCoverage {
public:
    [[nodiscard]] static bool validLimits(BoxCoverageLimits) noexcept;
    [[nodiscard]] static std::optional<BoxCoverage> compile(std::span<const UnionBox>, BoxCoverageIssue&, BoxCoverageLimits = {});
    [[nodiscard]] std::span<const CoverageCell> cells() const noexcept { return cells_; }
    [[nodiscard]] std::span<const uint32_t> references() const noexcept { return references_; }
    [[nodiscard]] std::span<const uint32_t> contributors(size_t cell) const noexcept {
        if (cell >= cells_.size()) return {};
        const auto& value = cells_[cell];
        return std::span{references_}.subspan(value.firstContributor, value.contributorCount);
    }
    [[nodiscard]] BoxCoverageStats stats() const noexcept { return stats_; }
private:
    BoxCoverage() = default;
    std::vector<CoverageCell> cells_{};
    std::vector<uint32_t> references_{};
    BoxCoverageStats stats_{};
};
} // namespace voxy::game::construction
