#include "game/construction/assembly_buoyancy.hpp"

#include <algorithm>
#include <new>

namespace voxy::game::construction {
std::optional<AssemblyBuoyancyPlan> AssemblyBuoyancyPlan::compile(
    const BuildSnapshot& input, const PartCatalog& catalog, AssemblyBuoyancyIssue& issue,
    AssemblyMassLimits massLimits, BoxUnionLimits collisionLimits, BoxCoverageLimits coverageLimits) {
    const auto refuse = [&](BoxCoverageError error, DurableId root = {}) -> std::optional<AssemblyBuoyancyPlan> {
        issue = {{}, {error, 0}, root}; return std::nullopt;
    };
    if (!BoxCoverage::validLimits(coverageLimits)) return refuse(BoxCoverageError::InvalidProfile);
    try {
        AssemblyCollisionIssue collisionIssue;
        auto collision = AssemblyCollisionPlan::compile(input, catalog, collisionIssue, massLimits, collisionLimits);
        if (!collision) { issue = {collisionIssue, {}, {}}; return std::nullopt; }
        size_t count = 0;
        for (const auto& part : collision->massPlan().parts()) {
            const auto& definition = *catalog.lookup(part.definition).definition;
            if (definition.buoyancy.size() > coverageLimits.inputBoxes - count) return refuse(BoxCoverageError::Capacity);
            count += definition.buoyancy.size();
        }
        AssemblyBuoyancyPlan result{std::move(*collision)};
        const auto& mass = result.collision_.massPlan();
        result.sources_.reserve(count); result.roots_.reserve(mass.roots().size());
        for (const auto& part : mass.parts()) {
            const auto& definition = *catalog.lookup(part.definition).definition;
            for (const auto& region : definition.buoyancy) {
                const auto frame = compose(part.rootFromPart, region.box.frame);
                const auto bounds = transformBounds(part.rootFromPart, *boxBounds(region.box));
                if (!frame || !bounds) return refuse(BoxCoverageError::InvalidBox, mass.roots()[part.root].key);
                result.sources_.push_back({part.part, part.definition, region.box.id, part.root, region.kind,
                    *frame, region.box.halfExtents, *bounds});
            }
        }
        auto remaining = coverageLimits;
        std::vector<UnionBox> rootSources; rootSources.reserve(result.sources_.size());
        for (size_t root = 0; root < mass.roots().size(); ++root) {
            rootSources.clear();
            for (size_t i = 0; i < result.sources_.size(); ++i)
                if (result.sources_[i].root == root) rootSources.push_back({result.sources_[i].bounds, static_cast<uint32_t>(i + 1)});
            BoxCoverageIssue coverageIssue;
            auto coverage = BoxCoverage::compile(rootSources, coverageIssue, remaining);
            if (!coverage) { issue = {{}, coverageIssue, mass.roots()[root].key}; return std::nullopt; }
            remaining.cells -= coverage->cells().size(); remaining.references -= coverage->references().size();
            remaining.clipTests -= coverage->stats().clipTests; remaining.referenceWrites -= coverage->stats().referenceWrites;
            result.stats_.clipTests += coverage->stats().clipTests;
            result.stats_.referenceWrites += coverage->stats().referenceWrites;
            result.stats_.volumeTicks3 += coverage->stats().volumeTicks3;
            result.stats_.maximumContributors = std::max(result.stats_.maximumContributors, coverage->stats().maximumContributors);
            result.roots_.push_back({static_cast<uint32_t>(root), std::move(*coverage)});
        }
        issue = {}; return result;
    } catch (const std::bad_alloc&) { return refuse(BoxCoverageError::Capacity); }
}
} // namespace voxy::game::construction
