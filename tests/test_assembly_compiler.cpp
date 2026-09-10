#include "game/construction/assembly_compiler.hpp"

#include <gtest/gtest.h>
#include <algorithm>
#include <stdexcept>
#include <type_traits>

namespace {
using namespace voxy::game::construction;
constexpr WorldNamespace kWorld{{'a','s','s','e','m','b','l','y','-','m','a','s','s','-','v','1'}};
DurableId id(uint64_t value) { return {kWorld, value}; }
PartCatalog validated(PartCatalogDraft draft) {
    CatalogIssue issue;
    auto result = PartCatalog::create(draft, issue);
    if (!result) throw std::runtime_error("catalog fixture: " + std::to_string(static_cast<int>(issue.error)));
    return std::move(*result);
}
const PartCatalog& catalog() { static const auto result = validated(makeStarterCatalogDraft()); return result; }
PartInstance part(uint64_t value, GridPosition where = {}, StarterPart kind = StarterPart::Beam) {
    PartInstance result;
    result.id = id(value); result.owningBuild = id(1); result.definition = starterPartKey(kind);
    result.placement.translation = where;
    result.settings = defaultModuleSettings(*catalog().lookup(result.definition).definition);
    return result;
}
Connection weld(uint64_t value, uint64_t a, uint64_t b, uint64_t socketA = 1, uint64_t socketB = 2) {
    Connection result;
    result.id = id(value); result.a = {id(a), SocketId{socketA}}; result.b = {id(b), SocketId{socketB}};
    result.strength = {1000, 1000, 1000, 1000}; return result;
}
BuildSnapshot stack(size_t count = 2) {
    BuildSnapshot result; result.id = id(1); result.owner = id(2); result.revision = TopologyRevision{7};
    for (size_t i = 0; i < count; ++i) {
        result.parts.push_back(part(10 + i, {0, static_cast<int32_t>(i) * kBrickBodyTicks, 0}));
        if (i) result.connections.push_back(weld(1000 + i, 9 + i, 10 + i));
    }
    return result;
}
AssemblyMassPlan compile(const BuildSnapshot& build, const PartCatalog& definitions = catalog()) {
    AssemblyIssue issue;
    auto result = AssemblyMassPlan::compile(build, definitions, issue);
    if (!result) throw std::runtime_error("assembly fixture: " + std::to_string(static_cast<int>(issue.error))
        + "/" + std::to_string(static_cast<int>(issue.build.error)) + "/" + std::string(issue.build.field));
    return std::move(*result);
}
PartCatalogDraft asymmetricDraft() {
    auto draft = makeStarterCatalogDraft();
    auto& first = draft.definitions.front();
    first.mass = {2, {.1, -.1, .2}, {{.05, .004, -.003, .004, .08, .006, -.003, .006, .1}}};
    auto second = first; second.key.id.counter = 500; second.nameKey = "salvage.part.mass_probe";
    second.mass = {3, {-.2, .1, -.1}, {{.09, -.005, .002, -.005, .12, .003, .002, .003, .13}}};
    draft.definitions.push_back(second); return draft;
}
BuildSnapshot asymmetricBuild() {
    auto build = stack(); build.parts[1].definition = asymmetricDraft().definitions.back().key; return build;
}
void same(const AssemblyMassPlan& a, const AssemblyMassPlan& b) {
    EXPECT_EQ(a.build(), b.build()); EXPECT_EQ(a.revision(), b.revision());
    EXPECT_TRUE(std::ranges::equal(a.roots(), b.roots())); EXPECT_TRUE(std::ranges::equal(a.parts(), b.parts()));
}
std::array<double, 3> vector(MetresPosition p) { return {p.x, p.y, p.z}; }
static_assert(!std::is_default_constructible_v<AssemblyMassPlan>);
static_assert(std::is_same_v<decltype(std::declval<AssemblyMassPlan&>().roots()), std::span<const AssemblyMassRoot>>);

TEST(AssemblyMass, AsymmetricFullTensorMatchesRationalGolden) {
    const auto plan = compile(asymmetricBuild(), validated(asymmetricDraft()));
    ASSERT_EQ(plan.roots().size(), 1u); ASSERT_EQ(plan.parts().size(), 2u);
    const auto& mass = plan.roots()[0].mass;
    EXPECT_EQ(mass.dryMassKg, 5); EXPECT_NEAR(mass.localCenterOfMass.x, -.08, 1e-14);
    EXPECT_NEAR(mass.localCenterOfMass.y, .596, 1e-14); EXPECT_NEAR(mass.localCenterOfMass.z, .02, 1e-14);
    // Independent Fraction golden in SIM-01/stage-a1/asymmetric-golden.json.
    const std::array<double, 9> expected{1.86272, .4166, -.109, .4166, .416, .4266, -.109, .4266, 1.95272};
    for (size_t i = 0; i < 9; ++i) EXPECT_NEAR(mass.inertia.elements[i], expected[i], 1e-13) << i;
    EXPECT_EQ(plan.roots()[0].key, id(10)); EXPECT_EQ(plan.roots()[0].partCount, 2u);
    EXPECT_EQ(plan.parts()[1].rootFromPart.translation, (GridPosition{0, 48, 0}));
}

TEST(AssemblyMass, AllProperRotationsTransformTheFullTensorAndFramesOnce) {
    const auto definitions = validated(asymmetricDraft());
    const auto reference = compile(asymmetricBuild(), definitions); const auto& mass = reference.roots()[0].mass;
    for (uint8_t r = 0; r < 24; ++r) {
        auto build = asymmetricBuild(); const GridTransform whole{{137, -291, 500}, {r}};
        for (auto& instance : build.parts) instance.placement = *compose(whole, instance.placement);
        const auto plan = compile(build, definitions); const auto& actual = plan.roots()[0].mass;
        const auto R = *rotationMatrix({r}); const auto center = vector(mass.localCenterOfMass);
        const auto observed = vector(actual.localCenterOfMass);
        // Independent dense long-double evaluation, unlike production's
        // signed-axis lookup and compensated component accumulation.
        for (size_t a = 0; a < 3; ++a) {
            long double expected = 0;
            for (size_t b = 0; b < 3; ++b) expected += R.elements[a * 3 + b] * static_cast<long double>(center[b]);
            EXPECT_NEAR(observed[a], static_cast<double>(expected), 1e-13);
            for (size_t b = 0; b < 3; ++b) {
                long double tensor = 0;
                for (size_t c = 0; c < 3; ++c) for (size_t d = 0; d < 3; ++d)
                    tensor += R.elements[a * 3 + c] * static_cast<long double>(mass.inertia.elements[c * 3 + d]) * R.elements[b * 3 + d];
                EXPECT_NEAR(actual.inertia.elements[a * 3 + b], static_cast<double>(tensor), 1e-12);
            }
        }
        EXPECT_EQ(plan.roots()[0].buildFromRoot.translation, whole.translation);
        EXPECT_EQ(plan.roots()[0].buildFromRoot.rotation, CubeRotation{});
        for (size_t i = 0; i < plan.parts().size(); ++i)
            EXPECT_EQ(compose(plan.roots()[0].buildFromRoot, plan.parts()[i].rootFromPart), build.parts[i].placement);
    }
}

TEST(AssemblyMass, RelocatedBallastChangesBalanceWithoutChangingMass) {
    auto build = stack(); build.parts[1] = part(11, {-75, 48, 0}, StarterPart::Ballast);
    build.connections = {weld(1000, 10, 11, 100, 2)};
    const auto left = compile(build);
    build.parts[1].placement.translation.x = 75; build.connections[0].a.socket = SocketId{106};
    const auto right = compile(build);
    const auto& a = left.roots()[0].mass; const auto& b = right.roots()[0].mass;
    EXPECT_EQ(a.dryMassKg, 690); EXPECT_EQ(a.dryMassKg, b.dryMassKg);
    EXPECT_NEAR(a.localCenterOfMass.x, -600.0 * 1.5 / 690.0, 1e-13);
    EXPECT_NEAR(b.localCenterOfMass.x, -a.localCenterOfMass.x, 1e-13);
    EXPECT_EQ(a.localCenterOfMass.y, b.localCenterOfMass.y);
    EXPECT_NEAR(a.inertia.elements[1], -b.inertia.elements[1], 1e-12);
}

TEST(AssemblyMass, EquivalentInsertionOrdersAndEndpointDirectionsHaveIdenticalOutput) {
    auto build = stack(4); build.connections[1].enabled = false;
    const auto reference = compile(build); ASSERT_EQ(reference.roots().size(), 2u);
    EXPECT_EQ(reference.roots()[0].key, id(10)); EXPECT_EQ(reference.roots()[1].key, id(12));
    std::reverse(build.parts.begin(), build.parts.end()); std::reverse(build.connections.begin(), build.connections.end());
    for (auto& link : build.connections) std::swap(link.a, link.b);
    same(reference, compile(build));
}

TEST(AssemblyMass, CosmeticConditionAndProvenanceDoNotDestroyDryMass) {
    auto build = stack(); const auto reference = compile(build);
    build.parts[0].health = 0; build.parts[0].paint = {0, 1, 2, 3}; build.parts[0].settings.enabled = false;
    build.parts[1].provenance = {PartOrigin::StarterLoan, id(100)};
    build.connections[0].damage = kFullHealth;
    same(reference, compile(build)); // A later accepted fracture changes topology explicitly.
    build.connections[0].enabled = false;
    const auto split = compile(build); ASSERT_EQ(split.roots().size(), 2u);
    EXPECT_EQ(split.roots()[0].mass.dryMassKg + split.roots()[1].mass.dryMassKg, reference.roots()[0].mass.dryMassKg);
}

TEST(AssemblyMass, RopeAndAuthoredLatchNeverPretendToBeRigidCapture) {
    auto build = stack(); build.parts = {part(10, {}, StarterPart::Winch), part(11, {400, 60, 50}, StarterPart::TowEye)};
    auto link = weld(1000, 10, 11, 10, 10); link.kind = ConnectionKind::Rope;
    link.minimumLengthMillimetres = 500; link.maximumLengthMillimetres = 40000; link.restLengthMillimetres = 5000;
    build.connections = {link}; EXPECT_EQ(compile(build).roots().size(), 2u);
    auto draft = makeStarterCatalogDraft(); auto cargo = draft.definitions.front();
    cargo.key.id.counter = 500; cargo.nameKey = "salvage.part.latch_probe";
    cargo.sockets[0].family = SocketFamily::CargoLatch; draft.definitions.push_back(cargo);
    build.parts = {part(10, {}, StarterPart::CargoCradle), part(11, {1000, 0, 0})};
    build.parts[1].definition = cargo.key;
    build.connections = {weld(1000, 10, 11, 10, 1)}; build.connections[0].kind = ConnectionKind::Latch;
    EXPECT_EQ(compile(build, validated(draft)).roots().size(), 2u);
}

TEST(AssemblyMass, LargeCommonTranslationsPreserveExactLocalMassAndMappings) {
    const auto definitions = validated(asymmetricDraft()); const auto reference = compile(asymmetricBuild(), definitions);
    for (const int32_t distance : {-2000000000, 2000000000}) {
        auto build = asymmetricBuild(); const GridPosition shift{distance, distance, distance};
        for (auto& instance : build.parts) instance.placement.translation = *checkedAdd(instance.placement.translation, shift);
        const auto plan = compile(build, definitions);
        auto root = plan.roots()[0]; root.buildFromRoot.translation = {};
        EXPECT_EQ(root, reference.roots()[0]); EXPECT_TRUE(std::ranges::equal(plan.parts(), reference.parts()));
    }
}

TEST(AssemblyMass, CurrentCatalogIsRevalidatedAndActuallyUsed) {
    const auto build = stack(); const auto reference = compile(build);
    auto draft = makeStarterCatalogDraft(); draft.definitions[0].mass.dryMassKg *= 2;
    for (double& value : draft.definitions[0].mass.inertia.elements) value *= 2;
    const auto changed = compile(build, validated(draft));
    EXPECT_EQ(changed.roots()[0].mass.dryMassKg, reference.roots()[0].mass.dryMassKg * 2);
    draft.definitions[0].sockets[0].frame.translation.x = 1;
    AssemblyIssue issue;
    EXPECT_FALSE(AssemblyMassPlan::compile(build, validated(draft), issue));
    EXPECT_EQ(issue.error, AssemblyError::InvalidBuild); EXPECT_EQ(issue.build.error, BuildError::MisalignedWeld);
}

TEST(AssemblyMass, InvalidEmptyAndOverLimitInputsReturnTypedFailures) {
    auto build = stack(); AssemblyIssue issue;
    EXPECT_FALSE(AssemblyMassPlan::compile(build, catalog(), issue, {0, 64, 12800})); EXPECT_EQ(issue.error, AssemblyError::InvalidProfile);
    EXPECT_FALSE(AssemblyMassPlan::compile(build, catalog(), issue, {1, 64, 12800})); EXPECT_EQ(issue.error, AssemblyError::Capacity);
    EXPECT_FALSE(AssemblyMassPlan::compile(build, catalog(), issue, {256, 64, 99})); EXPECT_EQ(issue.error, AssemblyError::Extent);
    EXPECT_EQ(issue.object, id(10));
    build.parts[1].id = build.parts[0].id;
    EXPECT_FALSE(AssemblyMassPlan::compile(build, catalog(), issue)); EXPECT_EQ(issue.error, AssemblyError::InvalidBuild);
    build = stack(0);
    EXPECT_FALSE(AssemblyMassPlan::compile(build, catalog(), issue)); EXPECT_EQ(issue.error, AssemblyError::EmptyBuild);
}

TEST(AssemblyMass, RootAndFullBuildCapacitiesAreEnforcedWithoutTruncation) {
    auto build = stack(64); build.connections.clear();
    EXPECT_EQ(compile(build).roots().size(), 64u);
    build = stack(65); build.connections.clear(); AssemblyIssue issue;
    EXPECT_FALSE(AssemblyMassPlan::compile(build, catalog(), issue)); EXPECT_EQ(issue.error, AssemblyError::Capacity);
    build = stack(256); const auto full = compile(build);
    ASSERT_EQ(full.roots().size(), 1u); EXPECT_EQ(full.roots()[0].partCount, 256u);
    EXPECT_EQ(full.parts().size(), 256u); EXPECT_EQ(full.roots()[0].mass.dryMassKg, 256 * 90.0);
    build.parts.push_back(part(900, {0, 256 * 48, 0}));
    EXPECT_FALSE(AssemblyMassPlan::compile(build, catalog(), issue)); EXPECT_EQ(issue.error, AssemblyError::Capacity);
}

TEST(AssemblyMass, OutputOwnsItsValuesAfterInputsAndCatalogAreDestroyed) {
    const auto output = [] { auto build = stack(); auto definitions = validated(makeStarterCatalogDraft()); return compile(build, definitions); }();
    const auto copy = output; same(output, copy);
    EXPECT_EQ(output.roots()[0].mass.dryMassKg, 180); EXPECT_EQ(output.parts()[1].part, id(11));
}
} // namespace
