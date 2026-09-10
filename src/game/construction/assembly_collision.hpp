#pragma once

#include "game/construction/assembly_compiler.hpp"
#include "game/construction/orthogonal_union.hpp"

#include <utility>

namespace voxy::game::construction {
struct AssemblyCollisionSource {
    DurableId part{};
    ContentKey definition{};
    ProxyId proxy{};
    uint32_t root = 0;
    GridBox bounds{};
    [[nodiscard]] bool operator==(const AssemblyCollisionSource&) const = default;
};
struct AssemblyCollisionRoot {
    uint32_t massRoot = 0;
    BoxUnion shape;
};
struct AssemblyCollisionIssue {
    AssemblyIssue assembly{};
    BoxUnionIssue geometry{};
    DurableId root{};
};

// Intermediate immutable collision preparation. A backend must support the
// exterior patches; treating the cells as ordinary unmasked boxes is incorrect.
// Buoyancy, module/joint frames and final cache identity remain separate stages.
class AssemblyCollisionPlan {
public:
    [[nodiscard]] static std::optional<AssemblyCollisionPlan> compile(
        const BuildSnapshot&, const PartCatalog&, AssemblyCollisionIssue&,
        AssemblyMassLimits = {}, BoxUnionLimits = {});
    [[nodiscard]] const AssemblyMassPlan& massPlan() const noexcept { return mass_; }
    [[nodiscard]] std::span<const AssemblyCollisionRoot> roots() const noexcept { return roots_; }
    [[nodiscard]] std::span<const AssemblyCollisionSource> sources() const noexcept { return sources_; }
    [[nodiscard]] const AssemblyCollisionSource* source(uint32_t label) const noexcept {
        return label > 0 && label <= sources_.size() ? &sources_[label - 1] : nullptr;
    }
    [[nodiscard]] BoxUnionStats stats() const noexcept { return stats_; }

private:
    explicit AssemblyCollisionPlan(AssemblyMassPlan mass) : mass_(std::move(mass)) {}
    AssemblyMassPlan mass_;
    std::vector<AssemblyCollisionRoot> roots_{};
    std::vector<AssemblyCollisionSource> sources_{};
    BoxUnionStats stats_{};
};
} // namespace voxy::game::construction
