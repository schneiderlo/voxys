#pragma once

#include "game/construction/assembly_collision.hpp"
#include "game/construction/orthogonal_coverage.hpp"

namespace voxy::game::construction {
struct AssemblyBuoyancySource {
    DurableId part{};
    ContentKey definition{};
    ProxyId region{}; // Scoped by definition's buoyancy collection.
    uint32_t root = 0;
    BuoyancyKind kind = BuoyancyKind::SolidMaterial;
    GridTransform rootFromRegion{};
    GridPosition halfExtents{};
    GridBox bounds{};
    [[nodiscard]] bool operator==(const AssemblyBuoyancySource&) const = default;
};
struct AssemblyBuoyancyRoot { uint32_t massRoot = 0; BoxCoverage coverage; };
struct AssemblyBuoyancyIssue {
    AssemblyCollisionIssue collision{};
    BoxCoverageIssue coverage{};
    DurableId root{};
};

// Owned geometry/provenance preparation only. This does not sample water,
// choose a flood approximation, apply a force or mutate accepted scene state.
class AssemblyBuoyancyPlan {
public:
    [[nodiscard]] static std::optional<AssemblyBuoyancyPlan> compile(
        const BuildSnapshot&, const PartCatalog&, AssemblyBuoyancyIssue&,
        AssemblyMassLimits = {}, BoxUnionLimits = {}, BoxCoverageLimits = {});
    [[nodiscard]] const AssemblyCollisionPlan& collisionPlan() const noexcept { return collision_; }
    [[nodiscard]] std::span<const AssemblyBuoyancyRoot> roots() const noexcept { return roots_; }
    [[nodiscard]] std::span<const AssemblyBuoyancySource> sources() const noexcept { return sources_; }
    [[nodiscard]] const AssemblyBuoyancySource* source(uint32_t label) const noexcept {
        return label > 0 && label <= sources_.size() ? &sources_[label - 1] : nullptr;
    }
    [[nodiscard]] BoxCoverageStats stats() const noexcept { return stats_; }
private:
    explicit AssemblyBuoyancyPlan(AssemblyCollisionPlan collision) : collision_(std::move(collision)) {}
    AssemblyCollisionPlan collision_;
    std::vector<AssemblyBuoyancyRoot> roots_{};
    std::vector<AssemblyBuoyancySource> sources_{};
    BoxCoverageStats stats_{};
};
} // namespace voxy::game::construction
