#include "game/construction/assembly_collision.hpp"

#include <new>

namespace voxy::game::construction {
std::optional<AssemblyCollisionPlan> AssemblyCollisionPlan::compile(
    const BuildSnapshot& input, const PartCatalog& catalog, AssemblyCollisionIssue& issue,
    AssemblyMassLimits massLimits, BoxUnionLimits geometryLimits) {
    const auto refuse = [&](BoxUnionError error, DurableId root = {}) -> std::optional<AssemblyCollisionPlan> {
        issue = {{}, {error, 0}, root}; return std::nullopt;
    };
    if (!BoxUnion::validLimits(geometryLimits)) return refuse(BoxUnionError::InvalidProfile);
    try {
        AssemblyIssue massIssue;
        auto mass = AssemblyMassPlan::compile(input, catalog, massIssue, massLimits);
        if (!mass) { issue = {massIssue, {}, {}}; return std::nullopt; }
        size_t count = 0;
        for (const auto& part : mass->parts()) {
            const auto& definition = *catalog.lookup(part.definition).definition;
            if (definition.collision.size() > geometryLimits.inputBoxes - count) return refuse(BoxUnionError::Capacity);
            count += definition.collision.size();
        }
        AssemblyCollisionPlan result{std::move(*mass)};
        result.sources_.reserve(count); result.roots_.reserve(result.mass_.roots().size());
        for (const auto& part : result.mass_.parts()) {
            const auto& definition = *catalog.lookup(part.definition).definition;
            for (const auto& proxy : definition.collision) {
                const auto bounds = transformBounds(part.rootFromPart, *boxBounds(proxy));
                if (!bounds) return refuse(BoxUnionError::InvalidBox, result.mass_.roots()[part.root].key);
                result.sources_.push_back({part.part, part.definition, proxy.id, part.root, *bounds});
            }
        }
        auto remaining = geometryLimits;
        std::vector<UnionBox> rootSources; rootSources.reserve(result.sources_.size());
        for (size_t root = 0; root < result.mass_.roots().size(); ++root) {
            const auto key = result.mass_.roots()[root].key;
            if (remaining.cells == 0 || remaining.faces == 0) return refuse(BoxUnionError::Capacity, key);
            rootSources.clear();
            for (size_t i = 0; i < result.sources_.size(); ++i)
                if (result.sources_[i].root == root) rootSources.push_back({result.sources_[i].bounds, static_cast<uint32_t>(i + 1)});
            BoxUnionIssue unionIssue;
            auto shape = BoxUnion::compile(rootSources, unionIssue, remaining);
            if (!shape) { issue = {{}, unionIssue, key}; return std::nullopt; }
            remaining.cells -= shape->cells().size(); remaining.faces -= shape->faces().size();
            remaining.clipTests -= shape->stats().clipTests;
            result.stats_.clipTests += shape->stats().clipTests;
            result.stats_.volumeTicks3 += shape->stats().volumeTicks3;
            result.stats_.surfaceAreaTicks2 += shape->stats().surfaceAreaTicks2;
            result.roots_.push_back({static_cast<uint32_t>(root), std::move(*shape)});
        }
        issue = {}; return result;
    } catch (const std::bad_alloc&) { return refuse(BoxUnionError::Capacity); }
}
} // namespace voxy::game::construction
