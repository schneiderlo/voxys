#include "game/construction/assembly_buoyancy.hpp"

#include <gtest/gtest.h>
#include <algorithm>
#include <map>
#include <set>
#include <stdexcept>
#include <type_traits>

namespace {
using namespace voxy::game::construction;
using Point = std::array<int32_t,3>;
using Oracle = std::map<Point, std::set<uint32_t>>;
UnionBox box(GridPosition lo, GridPosition hi, uint32_t source) { return {{lo, hi}, source}; }
uint64_t volume(GridBox b) {
    return static_cast<uint64_t>(b.maximum.x-b.minimum.x) * static_cast<uint64_t>(b.maximum.y-b.minimum.y)
        * static_cast<uint64_t>(b.maximum.z-b.minimum.z);
}
BoxCoverage compile(const std::vector<UnionBox>& input) {
    BoxCoverageIssue issue; auto value = BoxCoverage::compile(input, issue);
    if (!value) throw std::runtime_error("coverage fixture: " + std::to_string(static_cast<int>(issue.error)));
    return std::move(*value);
}
Oracle oracle(const std::vector<UnionBox>& input) {
    Oracle result;
    for (const auto& b : input)
        for (int32_t x=b.bounds.minimum.x; x<b.bounds.maximum.x; ++x)
            for (int32_t y=b.bounds.minimum.y; y<b.bounds.maximum.y; ++y)
                for (int32_t z=b.bounds.minimum.z; z<b.bounds.maximum.z; ++z) result[{x,y,z}].insert(b.source);
    return result;
}
void check(const std::vector<UnionBox>& input, const BoxCoverage& coverage, bool activeSubsets = false) {
    const auto expected = oracle(input); Oracle actual;
    size_t referenceCount = 0;
    for (size_t i=0; i<coverage.cells().size(); ++i) {
        const auto& cell = coverage.cells()[i]; const auto labels = coverage.contributors(i);
        ASSERT_FALSE(labels.empty()); ASSERT_EQ(cell.firstContributor, referenceCount);
        referenceCount += labels.size();
        EXPECT_TRUE(std::ranges::is_sorted(labels));
        const std::set<uint32_t> set(labels.begin(), labels.end()); EXPECT_EQ(set.size(), labels.size());
        for (int32_t x=cell.bounds.minimum.x; x<cell.bounds.maximum.x; ++x)
            for (int32_t y=cell.bounds.minimum.y; y<cell.bounds.maximum.y; ++y)
                for (int32_t z=cell.bounds.minimum.z; z<cell.bounds.maximum.z; ++z)
                    ASSERT_TRUE(actual.emplace(Point{x,y,z}, set).second);
    }
    EXPECT_EQ(actual, expected); EXPECT_EQ(coverage.stats().volumeTicks3, expected.size());
    EXPECT_EQ(referenceCount, coverage.references().size()); EXPECT_TRUE(coverage.contributors(coverage.cells().size()).empty());
    if (!activeSubsets) return;
    ASSERT_LE(input.size(), 6u);
    // Hypothetical source activation, not a shipping flooding/force API.
    for (uint32_t mask=0; mask<(1u<<input.size()); ++mask) {
        std::set<uint32_t> enabled;
        for (size_t i=0; i<input.size(); ++i) if (mask&(1u<<i)) enabled.insert(input[i].source);
        uint64_t wanted=0, observed=0;
        for (const auto& [point, labels] : expected) {
            static_cast<void>(point);
            if (std::ranges::any_of(labels,[&](auto label){ return enabled.contains(label); })) ++wanted;
        }
        for (size_t i=0; i<coverage.cells().size(); ++i)
            if (std::ranges::any_of(coverage.contributors(i),[&](auto label){ return enabled.contains(label); }))
                observed += volume(coverage.cells()[i].bounds);
        EXPECT_EQ(observed, wanted) << mask;
    }
}
void same(const BoxCoverage& a, const BoxCoverage& b) {
    EXPECT_TRUE(std::ranges::equal(a.cells(),b.cells())); EXPECT_TRUE(std::ranges::equal(a.references(),b.references()));
    EXPECT_EQ(a.stats(),b.stats());
}
static_assert(!std::is_default_constructible_v<BoxCoverage>);
static_assert(std::is_same_v<decltype(std::declval<BoxCoverage&>().references()),std::span<const uint32_t>>);

TEST(BoxCoverage, EmptyDisplacementAndZeroAvailableOutputBudgetsAreValid) {
    BoxCoverageIssue issue; BoxCoverageLimits limits;
    limits.cells=0; limits.references=0; limits.inputBoxes=0; limits.clipTests=0; limits.referenceWrites=0;
    auto result=BoxCoverage::compile({},issue,limits); ASSERT_TRUE(result);
    EXPECT_TRUE(result->cells().empty()); EXPECT_TRUE(result->references().empty()); EXPECT_EQ(result->stats(),BoxCoverageStats{});
    EXPECT_TRUE(result->contributors(0).empty());
}

TEST(BoxCoverage, OverlapsRetainAllContributorsForEveryActiveSubset) {
    const std::vector<UnionBox> input{box({-2,-1,-2},{3,2,2},1),box({0,-2,-1},{4,3,1},7),box({-1,0,0},{1,4,3},21)};
    const auto result=compile(input); check(input,result,true);
    EXPECT_EQ(result.stats().maximumContributors,3u);
}

TEST(BoxCoverage, NestedAndIdenticalRegionsNeverEraseLargerOrSmallerSources) {
    const std::vector<UnionBox> input{box({0,0,0},{5,5,5},10),box({1,1,1},{4,4,4},20),box({1,1,1},{4,4,4},30),box({2,2,2},{3,3,3},40)};
    const auto result=compile(input); check(input,result,true);
    EXPECT_EQ(result.stats().volumeTicks3,125u); EXPECT_EQ(result.stats().maximumContributors,4u);
}

TEST(BoxCoverage, DeterministicMixedFixturesAndInputReversalMatchTheUnitOracle) {
    uint32_t state=337;
    const auto random=[&]() { state=state*1664525u+1013904223u; return state; };
    for (int fixture=0; fixture<24; ++fixture) {
        std::vector<UnionBox> input;
        for (uint32_t i=0;i<6;++i) {
            const GridPosition lo{static_cast<int32_t>(random()%5)-2,static_cast<int32_t>(random()%5)-2,static_cast<int32_t>(random()%5)-2};
            const GridPosition hi{lo.x+1+static_cast<int32_t>(random()%4),lo.y+1+static_cast<int32_t>(random()%4),lo.z+1+static_cast<int32_t>(random()%4)};
            input.push_back(box(lo,hi,i*11+3));
        }
        const auto result=compile(input); check(input,result,true);
        std::reverse(input.begin(),input.end()); same(result,compile(input));
    }
}

TEST(BoxCoverage, AllRotationsPreserveCoverageAndCavityVolume) {
    const std::vector<UnionBox> shell{box({0,0,0},{1,5,5},1),box({4,0,0},{5,5,5},2),box({1,0,0},{4,1,5},3),
        box({1,4,0},{4,5,5},4),box({1,1,0},{4,4,1},5),box({1,1,4},{4,4,5},6)};
    for (uint8_t rotation=0;rotation<24;++rotation) {
        auto input=shell;
        for (auto& b:input) b.bounds=*transformBounds({{2,-1,3},{rotation}},b.bounds);
        const auto result=compile(input); check(input,result); EXPECT_EQ(result.stats().volumeTicks3,98u);
    }
}

TEST(BoxCoverage, AllCapacityWorkAndInputErrorsAreExplicit) {
    std::vector<UnionBox> input{box({1,1,1},{3,3,3},1),box({0,0,0},{4,4,4},2)};
    BoxCoverageIssue issue; BoxCoverageLimits limits;
    limits.inputBoxes=1; EXPECT_FALSE(BoxCoverage::compile(input,issue,limits)); EXPECT_EQ(issue.error,BoxCoverageError::Capacity);
    limits={}; limits.cells=6; EXPECT_FALSE(BoxCoverage::compile(input,issue,limits)); EXPECT_EQ(issue.error,BoxCoverageError::Capacity);
    limits={}; limits.references=7; EXPECT_FALSE(BoxCoverage::compile(input,issue,limits)); EXPECT_EQ(issue.error,BoxCoverageError::Capacity);
    limits={}; limits.scratchPieces=5; EXPECT_FALSE(BoxCoverage::compile(input,issue,limits)); EXPECT_EQ(issue.error,BoxCoverageError::Capacity);
    limits={}; limits.clipTests=0; EXPECT_FALSE(BoxCoverage::compile(input,issue,limits)); EXPECT_EQ(issue.error,BoxCoverageError::WorkLimit);
    limits={}; limits.referenceWrites=1; EXPECT_FALSE(BoxCoverage::compile(std::span{input}.first(1),issue,limits));
    EXPECT_EQ(issue.error,BoxCoverageError::WorkLimit); // Final canonical packing is counted too.
    limits={}; limits.scratchPieces=0; EXPECT_FALSE(BoxCoverage::compile(input,issue,limits)); EXPECT_EQ(issue.error,BoxCoverageError::InvalidProfile);
    input[1].source=1; EXPECT_FALSE(BoxCoverage::compile(input,issue)); EXPECT_EQ(issue.error,BoxCoverageError::InvalidSource);
    input[1].source=0; EXPECT_FALSE(BoxCoverage::compile(input,issue)); EXPECT_EQ(issue.error,BoxCoverageError::InvalidSource);
    input.resize(1); input[0].bounds.maximum.x=input[0].bounds.minimum.x;
    EXPECT_FALSE(BoxCoverage::compile(input,issue)); EXPECT_EQ(issue.error,BoxCoverageError::InvalidBox);
    input[0]=box({-12801,0,0},{1,1,1},1); EXPECT_FALSE(BoxCoverage::compile(input,issue)); EXPECT_EQ(issue.error,BoxCoverageError::InvalidBox);
}

TEST(BoxCoverage, OutputAndCopiesOwnTheirContributorStorage) {
    const auto result=[] { std::vector<UnionBox> input{box({0,0,0},{4,4,4},1),box({1,1,1},{3,3,3},2)}; return compile(input); }();
    const auto copy=result; same(result,copy); EXPECT_EQ(copy.stats().volumeTicks3,64u);
    EXPECT_NE(result.references().data(),copy.references().data());
}

constexpr WorldNamespace kWorld{{'b','u','o','y','a','n','c','y','-','p','l','a','n','-','0','1'}};
DurableId id(uint64_t n) { return {kWorld,n}; }
PartCatalog validated(PartCatalogDraft draft) {
    CatalogIssue issue; auto result=PartCatalog::create(draft,issue);
    if (!result) throw std::runtime_error("catalog fixture: "+std::to_string(static_cast<int>(issue.error)));
    return std::move(*result);
}
const PartCatalog& catalog() { static const auto value=validated(makeStarterCatalogDraft()); return value; }
BuildSnapshot stack(size_t count=2) {
    BuildSnapshot result; result.id=id(1); result.owner=id(2);
    for (size_t i=0;i<count;++i) {
        PartInstance part; part.id=id(10+i); part.owningBuild=result.id; part.definition=starterPartKey(StarterPart::Beam);
        part.placement.translation.y=static_cast<int32_t>(i)*48; result.parts.push_back(part);
        if (i) { Connection link; link.id=id(1000+i); link.a={id(9+i),SocketId{1}}; link.b={part.id,SocketId{2}};
            link.strength={1000,1000,1000,1000}; result.connections.push_back(link); }
    }
    return result;
}
PartCatalogDraft overlappingDraft() {
    auto draft=makeStarterCatalogDraft(); auto& shell=draft.definitions.front();
    // Synthetic hollow occupancy allows two physically described regions to
    // overlap without canonical solid overlap or a forged live capture.
    shell.solidOccupancy={{ProxyId{1},{{-60,0,0},{}},{5,5,5}}};
    shell.buoyancy={{shell.collision.front(),BuoyancyKind::SealedCompartment}};
    auto core=shell; core.key.id.counter=500; core.nameKey="salvage.part.coverage_core";
    core.solidOccupancy[0].frame.translation.x=60;
    core.buoyancy={{{ProxyId{1},{},{10,10,10}},BuoyancyKind::SolidMaterial}};
    core.sockets[1].frame.translation.y=24; draft.definitions.push_back(core);
    return draft;
}
BuildSnapshot overlappingBuild() {
    auto build=stack(); build.parts[1].placement.translation={};
    build.parts[1].definition=overlappingDraft().definitions.back().key; return build;
}
AssemblyBuoyancyPlan compileBuild(const BuildSnapshot& input,const PartCatalog& definitions=catalog()) {
    AssemblyBuoyancyIssue issue; auto result=AssemblyBuoyancyPlan::compile(input,definitions,issue);
    if (!result) throw std::runtime_error("buoyancy fixture: "+std::to_string(static_cast<int>(issue.collision.assembly.error))
        +"/"+std::to_string(static_cast<int>(issue.collision.assembly.build.error))+"/"+std::to_string(static_cast<int>(issue.coverage.error)));
    return std::move(*result);
}

TEST(AssemblyBuoyancy, InactiveSealStillLeavesItsOverlappingSolidContributor) {
    const auto result=compileBuild(overlappingBuild(),validated(overlappingDraft())); ASSERT_EQ(result.roots().size(),1u);
    const auto& coverage=result.roots()[0].coverage;
    EXPECT_EQ(coverage.stats().volumeTicks3,480000u); EXPECT_EQ(coverage.stats().maximumContributors,2u);
    uint64_t solidOnly=0;
    for (size_t i=0;i<coverage.cells().size();++i) {
        bool solid=false;
        for (auto label:coverage.contributors(i)) {
            const auto* source=result.source(label); ASSERT_NE(source,nullptr);
            EXPECT_EQ(source->root,0u); EXPECT_EQ(source->region,ProxyId{1});
            if (source->kind==BuoyancyKind::SolidMaterial) { solid=true; EXPECT_EQ(source->part,id(11)); }
            else EXPECT_EQ(source->part,id(10));
        }
        if (solid) solidOnly+=volume(coverage.cells()[i].bounds);
    }
    EXPECT_EQ(solidOnly,8000u); EXPECT_EQ(result.collisionPlan().massPlan().roots()[0].mass.dryMassKg,180);
    EXPECT_EQ(result.source(0),nullptr); EXPECT_EQ(result.source(3),nullptr);
}

TEST(AssemblyBuoyancy, WholeBuildAndAuthoredRegionFramesComposeOnceAtEveryRotation) {
    const auto definitions=validated(overlappingDraft());
    for (uint8_t r=0;r<24;++r) {
        auto build=overlappingBuild(); const GridTransform whole{{137,-29,83},{r}};
        for (auto& part:build.parts) part.placement=*compose(whole,part.placement);
        const auto result=compileBuild(build,definitions); EXPECT_EQ(result.stats().volumeTicks3,480000u);
        for (const auto& source:result.sources()) {
            EXPECT_EQ(source.rootFromRegion.rotation,CubeRotation{r}); EXPECT_EQ(source.rootFromRegion.translation,GridPosition{});
            const GridBox local{{-source.halfExtents.x,-source.halfExtents.y,-source.halfExtents.z},source.halfExtents};
            EXPECT_EQ(source.bounds,*transformBounds({{},{r}},local));
        }
    }
    auto draft=makeStarterCatalogDraft(); draft.definitions[0].buoyancy={{{ProxyId{9},{{10,3,-4},{4}},{10,5,3}},BuoyancyKind::SolidMaterial}};
    auto build=stack(1); build.parts[0].placement={{100,200,300},{1}};
    const auto result=compileBuild(build,validated(draft)); ASSERT_EQ(result.sources().size(),1u);
    EXPECT_EQ(result.sources()[0].bounds,(GridBox{{5,1,-7},{15,7,13}})); EXPECT_EQ(result.stats().volumeTicks3,1200u);
}

TEST(AssemblyBuoyancy, AbsentDisplacementIsValidAndDoesNotInventAFullCollisionVolume) {
    auto draft=makeStarterCatalogDraft(); draft.definitions[0].buoyancy.clear();
    const auto result=compileBuild(stack(),validated(draft)); ASSERT_EQ(result.roots().size(),1u);
    EXPECT_TRUE(result.sources().empty()); EXPECT_TRUE(result.roots()[0].coverage.cells().empty()); EXPECT_EQ(result.stats().volumeTicks3,0u);
    EXPECT_EQ(result.collisionPlan().stats().volumeTicks3,960000u);
}

TEST(AssemblyBuoyancy, FullPartBudgetAndInsertionOrderPreserveExactDisplacement) {
    auto build=stack(256); const auto result=compileBuild(build);
    ASSERT_EQ(result.roots().size(),1u); EXPECT_EQ(result.sources().size(),256u);
    EXPECT_EQ(result.roots()[0].coverage.cells().size(),256u); EXPECT_EQ(result.roots()[0].coverage.references().size(),256u);
    const auto definition=catalog().lookup(starterPartKey(StarterPart::Beam)); ASSERT_TRUE(definition);
    const auto& region=definition.definition->buoyancy.front().box;
    const uint64_t single=static_cast<uint64_t>(region.halfExtents.x*2)*static_cast<uint64_t>(region.halfExtents.y*2)*static_cast<uint64_t>(region.halfExtents.z*2);
    EXPECT_EQ(result.stats().volumeTicks3,256*single);
    std::reverse(build.parts.begin(),build.parts.end()); std::reverse(build.connections.begin(),build.connections.end());
    const auto other=compileBuild(build); same(result.roots()[0].coverage,other.roots()[0].coverage);
    EXPECT_TRUE(std::ranges::equal(result.sources(),other.sources()));
}

TEST(AssemblyBuoyancy, RegionCellReferenceAndWorkBudgetsAreSharedAcrossRoots) {
    auto build=stack(4); build.connections[1].enabled=false;
    const auto result=compileBuild(build); ASSERT_EQ(result.roots().size(),2u);
    AssemblyBuoyancyIssue issue; BoxCoverageLimits limits; limits.inputBoxes=3;
    EXPECT_FALSE(AssemblyBuoyancyPlan::compile(build,catalog(),issue,{},{},limits)); EXPECT_EQ(issue.coverage.error,BoxCoverageError::Capacity);
    limits={}; limits.cells=3;
    EXPECT_FALSE(AssemblyBuoyancyPlan::compile(build,catalog(),issue,{},{},limits)); EXPECT_EQ(issue.coverage.error,BoxCoverageError::Capacity);
    limits={}; limits.references=3;
    EXPECT_FALSE(AssemblyBuoyancyPlan::compile(build,catalog(),issue,{},{},limits)); EXPECT_EQ(issue.coverage.error,BoxCoverageError::Capacity);
    limits={}; limits.clipTests=result.roots()[0].coverage.stats().clipTests;
    EXPECT_FALSE(AssemblyBuoyancyPlan::compile(build,catalog(),issue,{},{},limits)); EXPECT_EQ(issue.coverage.error,BoxCoverageError::WorkLimit); EXPECT_EQ(issue.root,id(12));
    limits={}; limits.referenceWrites=result.roots()[0].coverage.stats().referenceWrites;
    EXPECT_FALSE(AssemblyBuoyancyPlan::compile(build,catalog(),issue,{},{},limits)); EXPECT_EQ(issue.coverage.error,BoxCoverageError::WorkLimit);
}

TEST(AssemblyBuoyancy, LoanAndConditionPreserveMetadataWhileCurrentCatalogChangesAreUsed) {
    auto build=stack(); const auto initial=compileBuild(build);
    build.parts[0].health=0; build.parts[1].provenance={PartOrigin::StarterLoan,id(500)};
    const auto changed=compileBuild(build); same(initial.roots()[0].coverage,changed.roots()[0].coverage);
    auto draft=makeStarterCatalogDraft(); draft.definitions[0].buoyancy.clear();
    const auto empty=compileBuild(build,validated(draft)); EXPECT_EQ(empty.stats().volumeTicks3,0u);
    EXPECT_EQ(empty.collisionPlan().massPlan().roots()[0].mass.dryMassKg,initial.collisionPlan().massPlan().roots()[0].mass.dryMassKg);
}
} // namespace
