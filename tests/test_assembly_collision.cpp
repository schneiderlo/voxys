#include "game/construction/assembly_collision.hpp"

#include <gtest/gtest.h>
#include <algorithm>
#include <map>
#include <set>
#include <stdexcept>
#include <tuple>

namespace {
using namespace voxy::game::construction;
UnionBox box(GridPosition lo, GridPosition hi, uint32_t source) { return {{lo, hi}, source}; }
BoxUnion compile(std::vector<UnionBox> input) {
    BoxUnionIssue issue; auto result = BoxUnion::compile(input, issue);
    if (!result) throw std::runtime_error("union fixture: " + std::to_string(static_cast<int>(issue.error)));
    return std::move(*result);
}
using Point = std::array<int32_t, 3>;
Point point(GridPosition p) { return {p.x, p.y, p.z}; }
using FaceKey = std::tuple<Point, size_t, int>;
// Independent finite unit-cell oracle. No clipping, BVH, surface extraction
// or geometry helpers from the implementation are used here.
void checkOracle(const std::vector<UnionBox>& input, const BoxUnion& result) {
    std::map<Point, uint32_t> expected, actual;
    const auto fill = [](const UnionBox& value, auto&& visitor) {
        for (int32_t x = value.bounds.minimum.x; x < value.bounds.maximum.x; ++x)
            for (int32_t y = value.bounds.minimum.y; y < value.bounds.maximum.y; ++y)
                for (int32_t z = value.bounds.minimum.z; z < value.bounds.maximum.z; ++z) visitor(Point{x,y,z});
    };
    for (const auto& value : input) fill(value, [&](Point p) {
        const auto found = expected.find(p);
        if (found == expected.end() || value.source < found->second) expected[p] = value.source;
    });
    for (const auto& value : result.cells()) fill(value, [&](Point p) { EXPECT_TRUE(actual.emplace(p, value.source).second); });
    EXPECT_EQ(actual, expected); EXPECT_EQ(result.stats().volumeTicks3, expected.size());
    std::map<FaceKey, uint32_t> wantedFaces, actualFaces;
    for (const auto& [p, source] : expected) for (size_t axis = 0; axis < 3; ++axis) for (int sign : {-1, 1}) {
        auto neighbor = p; neighbor[axis] += sign;
        if (expected.contains(neighbor)) continue;
        auto origin = p; if (sign > 0) ++origin[axis];
        wantedFaces.emplace(FaceKey{origin, axis, sign}, source);
    }
    for (const auto& face : result.faces()) {
        ASSERT_LT(face.axis, 3u); ASSERT_LT(face.cell, result.cells().size());
        EXPECT_EQ(face.source, result.cells()[face.cell].source);
        const auto lo = point(face.bounds.minimum), hi = point(face.bounds.maximum);
        ASSERT_EQ(lo[face.axis], hi[face.axis]); EXPECT_TRUE(face.sign == 1 || face.sign == -1);
        const auto u = (face.axis + 1u) % 3u, v = (face.axis + 2u) % 3u;
        ASSERT_LT(lo[u], hi[u]); ASSERT_LT(lo[v], hi[v]);
        for (int32_t a = lo[u]; a < hi[u]; ++a) for (int32_t b = lo[v]; b < hi[v]; ++b) {
            auto p = lo; p[u] = a; p[v] = b;
            EXPECT_TRUE(actualFaces.emplace(FaceKey{p, face.axis, face.sign}, face.source).second);
        }
    }
    EXPECT_EQ(actualFaces, wantedFaces); EXPECT_EQ(result.stats().surfaceAreaTicks2, wantedFaces.size());
}
void equivalent(const BoxUnion& a, const BoxUnion& b) {
    EXPECT_TRUE(std::ranges::equal(a.cells(), b.cells())); EXPECT_TRUE(std::ranges::equal(a.faces(), b.faces()));
    EXPECT_TRUE(std::ranges::equal(a.bvh(), b.bvh())); EXPECT_EQ(a.stats(), b.stats());
}
bool touches(GridBox a, GridBox b) {
    const auto al = point(a.minimum), ah = point(a.maximum), bl = point(b.minimum), bh = point(b.maximum);
    for (size_t axis = 0; axis < 3; ++axis) if (al[axis] > bh[axis] || bl[axis] > ah[axis]) return false;
    return true;
}

TEST(BoxUnion, PartialOverlapContainmentAndDuplicateBoundsHaveOneOwner) {
    std::vector<UnionBox> input{box({-2,-2,-2},{2,2,2},10), box({-1,0,-1},{3,3,3},20),
        box({-1,-1,-1},{1,1,1},30), box({-2,-2,-2},{2,2,2},40)};
    const auto result = compile(input); checkOracle(input, result);
    EXPECT_FALSE(std::ranges::any_of(result.cells(), [](const auto& cell) { return cell.source == 30 || cell.source == 40; }));
}

TEST(BoxUnion, PartialMatingFacesKeepEveryExposedRectangle) {
    std::vector<UnionBox> input{box({0,0,0},{4,4,4},1), box({4,1,1},{6,3,3},2)};
    const auto result = compile(input); checkOracle(input, result);
    EXPECT_EQ(result.stats().volumeTicks3, 72u); EXPECT_EQ(result.stats().surfaceAreaTicks2, 112u);
    const auto patches = std::ranges::count_if(result.faces(), [](const auto& face) {
        return face.axis == 0 && face.sign == 1 && face.bounds.minimum.x == 4;
    });
    EXPECT_EQ(patches, 4); // Exposed frame around the smaller box, not a full contact face.
}

TEST(BoxUnion, ClosedCavityRetainsItsInnerBoundaryAndExcludesItsVolume) {
    std::vector<UnionBox> shell{box({0,0,0},{1,5,5},1), box({4,0,0},{5,5,5},2),
        box({1,0,0},{4,1,5},3), box({1,4,0},{4,5,5},4), box({1,1,0},{4,4,1},5), box({1,1,4},{4,4,5},6)};
    const auto result = compile(shell); checkOracle(shell, result);
    EXPECT_EQ(result.stats().volumeTicks3, 98u); EXPECT_EQ(result.stats().surfaceAreaTicks2, 204u);
    std::array<uint32_t, 8> output{};
    EXPECT_EQ(result.query({{2,2,2},{3,3,3}}, output).count, 0u);
}

TEST(BoxUnion, EveryProperRotationMatchesIndependentOccupancyAndSurfaceOracle) {
    const std::vector<UnionBox> input{box({-3,-1,-2},{1,2,1},1), box({-1,0,0},{3,4,2},2)};
    const auto reference = compile(input);
    for (uint8_t rotation = 0; rotation < 24; ++rotation) {
        auto rotated = input;
        for (auto& value : rotated) value.bounds = *transformBounds({{1,-2,3},{rotation}}, value.bounds);
        const auto result = compile(rotated); checkOracle(rotated, result);
        EXPECT_EQ(result.stats().volumeTicks3, reference.stats().volumeTicks3);
        EXPECT_EQ(result.stats().surfaceAreaTicks2, reference.stats().surfaceAreaTicks2);
    }
}

TEST(BoxUnion, BoundedMixedFixturesAndInputPermutationsRemainExact) {
    uint32_t state = 731;
    const auto next = [&]() { state = state * 1664525u + 1013904223u; return state; };
    for (int fixture = 0; fixture < 32; ++fixture) {
        std::vector<UnionBox> input;
        for (uint32_t i = 0; i < 6; ++i) {
            const GridPosition lo{static_cast<int32_t>(next() % 7) - 3, static_cast<int32_t>(next() % 7) - 3, static_cast<int32_t>(next() % 7) - 3};
            const GridPosition hi{lo.x + 1 + static_cast<int32_t>(next() % 4), lo.y + 1 + static_cast<int32_t>(next() % 4), lo.z + 1 + static_cast<int32_t>(next() % 4)};
            input.push_back(box(lo, hi, i * 7 + 3));
        }
        const auto result = compile(input); checkOracle(input, result);
        std::reverse(input.begin(), input.end()); equivalent(result, compile(input));
    }
}

TEST(BoxUnion, BvhQueriesMatchLinearCellsAndNeverPartiallyWriteOnFailure) {
    const auto result = compile({box({0,0,0},{4,4,4},1), box({2,2,-2},{6,6,2},2), box({-3,0,1},{1,2,5},3)});
    ASSERT_EQ(result.bvh().size(), result.cells().size() * 2 - 1);
    ASSERT_EQ(result.bvh()[0].escape, result.bvh().size());
    std::array<uint32_t, 128> output{};
    for (int32_t x = -4; x <= 7; ++x) for (int32_t y = -2; y <= 7; ++y) for (int32_t z = -3; z <= 6; ++z) {
        const GridBox bounds{{x,y,z},{x+1,y+1,z+1}};
        std::set<uint32_t> expected;
        for (size_t i = 0; i < result.cells().size(); ++i) if (touches(bounds, result.cells()[i].bounds)) expected.insert(static_cast<uint32_t>(i));
        output.fill(99999); const auto query = result.query(bounds, output);
        ASSERT_EQ(query.status, UnionQueryStatus::Complete); ASSERT_EQ(query.count, expected.size());
        const std::set<uint32_t> actual(output.begin(), output.begin() + static_cast<std::ptrdiff_t>(query.count));
        EXPECT_EQ(actual, expected); EXPECT_EQ(output[query.count], 99999u);
        EXPECT_LE(query.visitedNodes, result.bvh().size() * 2);
    }
    const GridBox all{{-10,-10,-10},{10,10,10}};
    output.fill(888); auto small = result.query(all, std::span{output}.first(1));
    EXPECT_EQ(small.status, UnionQueryStatus::NeedCapacity); EXPECT_EQ(small.count, result.cells().size());
    EXPECT_TRUE(std::ranges::all_of(output, [](auto value) { return value == 888; }));
    small = result.query({{1,0,0},{0,0,0}}, output); EXPECT_EQ(small.status, UnionQueryStatus::InvalidBounds);
    EXPECT_TRUE(std::ranges::all_of(output, [](auto value) { return value == 888; }));
    const auto pointQuery = result.query({{4,4,4},{4,4,4}}, output);
    EXPECT_EQ(pointQuery.status, UnionQueryStatus::Complete); EXPECT_GT(pointQuery.count, 0u);
}

TEST(BoxUnion, InvalidInputsAndEveryIndependentBudgetRefuseWithoutTruncation) {
    std::vector<UnionBox> input{box({1,1,1},{3,3,3},1), box({0,0,0},{4,4,4},2)};
    BoxUnionIssue issue; BoxUnionLimits limits;
    limits.inputBoxes = 1; EXPECT_FALSE(BoxUnion::compile(input, issue, limits)); EXPECT_EQ(issue.error, BoxUnionError::Capacity);
    limits = {}; limits.cells = 6; EXPECT_FALSE(BoxUnion::compile(input, issue, limits)); EXPECT_EQ(issue.error, BoxUnionError::Capacity);
    limits = {}; limits.faces = 5; EXPECT_FALSE(BoxUnion::compile(input, issue, limits)); EXPECT_EQ(issue.error, BoxUnionError::Capacity);
    limits = {}; limits.scratchPieces = 5; EXPECT_FALSE(BoxUnion::compile(input, issue, limits)); EXPECT_EQ(issue.error, BoxUnionError::Capacity);
    limits = {}; limits.clipTests = 0; EXPECT_FALSE(BoxUnion::compile(input, issue, limits)); EXPECT_EQ(issue.error, BoxUnionError::WorkLimit);
    EXPECT_TRUE(BoxUnion::compile(std::span{input}.first(1), issue, limits));
    limits = {}; limits.radiusTicks = kMaximumUnionRadiusTicks + 1;
    EXPECT_FALSE(BoxUnion::compile(input, issue, limits)); EXPECT_EQ(issue.error, BoxUnionError::InvalidProfile);
    EXPECT_FALSE(BoxUnion::compile({}, issue)); EXPECT_EQ(issue.error, BoxUnionError::EmptyInput);
    input[1].source = 1; EXPECT_FALSE(BoxUnion::compile(input, issue)); EXPECT_EQ(issue.error, BoxUnionError::InvalidSource);
    input[1].source = 0; EXPECT_FALSE(BoxUnion::compile(input, issue)); EXPECT_EQ(issue.error, BoxUnionError::InvalidSource);
    input.resize(1); input[0].bounds.maximum.x = input[0].bounds.minimum.x;
    EXPECT_FALSE(BoxUnion::compile(input, issue)); EXPECT_EQ(issue.error, BoxUnionError::InvalidBox);
    input[0] = box({-12801,0,0},{1,1,1},1); EXPECT_FALSE(BoxUnion::compile(input, issue)); EXPECT_EQ(issue.error, BoxUnionError::InvalidBox);
}

constexpr WorldNamespace kWorld{{'c','o','l','l','i','s','i','o','n','-','p','l','a','n','0','1'}};
DurableId id(uint64_t n) { return {kWorld, n}; }
PartCatalog validated(PartCatalogDraft draft) {
    CatalogIssue issue; auto result = PartCatalog::create(draft, issue);
    if (!result) throw std::runtime_error("catalog fixture: " + std::to_string(static_cast<int>(issue.error)));
    return std::move(*result);
}
const PartCatalog& catalog() { static const auto value = validated(makeStarterCatalogDraft()); return value; }
BuildSnapshot stack(size_t count = 2) {
    BuildSnapshot result; result.id = id(1); result.owner = id(2);
    for (size_t i = 0; i < count; ++i) {
        PartInstance part; part.id = id(10 + i); part.owningBuild = result.id; part.definition = starterPartKey(StarterPart::Beam);
        part.placement.translation.y = static_cast<int32_t>(i) * 48; result.parts.push_back(part);
        if (i) {
            Connection link; link.id = id(1000 + i); link.a = {id(9 + i), SocketId{1}}; link.b = {part.id, SocketId{2}};
            link.strength = {1000,1000,1000,1000}; result.connections.push_back(link);
        }
    }
    return result;
}
AssemblyCollisionPlan compileBuild(const BuildSnapshot& build, const PartCatalog& definitions = catalog()) {
    AssemblyCollisionIssue issue; auto result = AssemblyCollisionPlan::compile(build, definitions, issue);
    if (!result) throw std::runtime_error("collision fixture: " + std::to_string(static_cast<int>(issue.assembly.error))
        + "/" + std::to_string(static_cast<int>(issue.geometry.error)));
    return std::move(*result);
}

TEST(AssemblyCollision, RealWeldedStackHasNoInternalMatingFacesAndStablePartLookup) {
    const auto result = compileBuild(stack()); ASSERT_EQ(result.roots().size(), 1u);
    const auto& shape = result.roots()[0].shape;
    EXPECT_EQ(shape.stats().volumeTicks3, 960000u); EXPECT_EQ(shape.stats().surfaceAreaTicks2, 68000u);
    EXPECT_EQ(shape.cells().size(), 2u); EXPECT_EQ(shape.faces().size(), 10u);
    EXPECT_EQ(result.massPlan().roots()[0].mass.dryMassKg, 180);
    for (const auto& face : shape.faces()) {
        const auto* source = result.source(face.source); ASSERT_NE(source, nullptr);
        EXPECT_EQ(source->root, 0u); EXPECT_EQ(source->proxy, ProxyId{1});
        EXPECT_EQ(source->part, id(9 + face.source));
        EXPECT_FALSE(face.axis == 1 && face.bounds.minimum.y == 24);
    }
    EXPECT_EQ(result.source(0), nullptr); EXPECT_EQ(result.source(3), nullptr);
}

TEST(AssemblyCollision, Full256PartProfileCompilesWithoutDecorativeContacts) {
    auto build = stack(256); const auto result = compileBuild(build);
    ASSERT_EQ(result.roots().size(), 1u); EXPECT_EQ(result.sources().size(), 256u);
    const auto& shape = result.roots()[0].shape;
    EXPECT_EQ(shape.cells().size(), 256u); EXPECT_EQ(shape.faces().size(), 1026u); EXPECT_EQ(shape.bvh().size(), 511u);
    EXPECT_EQ(shape.stats().volumeTicks3, 256u * 480000u); EXPECT_EQ(shape.stats().surfaceAreaTicks2, 6164000u);
    std::reverse(build.parts.begin(), build.parts.end()); std::reverse(build.connections.begin(), build.connections.end());
    const auto reordered = compileBuild(build); equivalent(shape, reordered.roots()[0].shape);
    EXPECT_TRUE(std::ranges::equal(result.sources(), reordered.sources()));
}

TEST(AssemblyCollision, DisabledWeldRootsStaySeparateAndBudgetsApplyAcrossAllRoots) {
    auto build = stack(4); build.connections[1].enabled = false;
    const auto result = compileBuild(build); ASSERT_EQ(result.roots().size(), 2u);
    EXPECT_EQ(result.roots()[0].shape.faces().size(), 10u); EXPECT_EQ(result.roots()[1].shape.faces().size(), 10u);
    AssemblyCollisionIssue issue; BoxUnionLimits limits; limits.cells = 3;
    EXPECT_FALSE(AssemblyCollisionPlan::compile(build, catalog(), issue, {}, limits)); EXPECT_EQ(issue.geometry.error, BoxUnionError::Capacity);
    limits = {}; limits.faces = 19;
    EXPECT_FALSE(AssemblyCollisionPlan::compile(build, catalog(), issue, {}, limits)); EXPECT_EQ(issue.geometry.error, BoxUnionError::Capacity);
    limits = {}; limits.clipTests = result.roots()[0].shape.stats().clipTests;
    EXPECT_FALSE(AssemblyCollisionPlan::compile(build, catalog(), issue, {}, limits)); EXPECT_EQ(issue.geometry.error, BoxUnionError::WorkLimit);
    EXPECT_EQ(issue.root, id(12));
}

TEST(AssemblyCollision, OverlappingAuthoredProxiesDoNotDoubleVolumeOrMass) {
    auto draft = makeStarterCatalogDraft(); auto& beam = draft.definitions[0];
    beam.collision.push_back({ProxyId{2}, {}, {50, 12, 12}});
    const auto result = compileBuild(stack(1), validated(draft));
    EXPECT_EQ(result.sources().size(), 2u); EXPECT_EQ(result.roots()[0].shape.cells().size(), 1u);
    EXPECT_EQ(result.stats().volumeTicks3, 480000u); EXPECT_EQ(result.massPlan().roots()[0].mass.dryMassKg, 90);
    for (const auto& face : result.roots()[0].shape.faces()) {
        const auto* source = result.source(face.source); ASSERT_NE(source, nullptr);
        EXPECT_EQ(source->proxy, ProxyId{1});
    }
}

TEST(AssemblyCollision, AuthoredProxyAndPartRotationComposeExactlyOnce) {
    auto draft = makeStarterCatalogDraft(); draft.definitions[0].collision = {{ProxyId{9}, {{10,3,-4},{4}}, {10,5,3}}};
    auto build = stack(1); build.parts[0].placement = {{100,200,300},{1}};
    const auto result = compileBuild(build, validated(draft)); ASSERT_EQ(result.sources().size(), 1u);
    EXPECT_EQ(result.sources()[0].bounds, (GridBox{{5,1,-7},{15,7,13}}));
    EXPECT_EQ(result.massPlan().roots()[0].buildFromRoot.translation, (GridPosition{100,200,300}));
    EXPECT_EQ(result.stats().volumeTicks3, 1200u);
}
} // namespace
