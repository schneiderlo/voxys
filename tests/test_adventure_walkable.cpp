#include "game/adventure/spatial_queries.hpp"
#include <gtest/gtest.h>
#include <array>
#include <cmath>
#include <limits>
#include <type_traits>
#include <utility>

using namespace voxy::game::adventure;
namespace {
using Queries=AdventureSpatialQueries;
using Id=voxy::game::construction::DurableId;
Id id(uint64_t counter) {voxy::game::construction::WorldNamespace world{};world.bytes[0]=31;return {world,counter};}
Queries::Solid box(uint64_t part,glm::dvec3 low,glm::dvec3 high) {return {id(1),id(part),low,high};}
struct ColumnScene {
    std::vector<uint16_t> samples=std::vector<uint16_t>(64*64,32768);
    voxy::terrain::lego::Surface terrain{samples,64,64,8.f,1.f};
    Queries queries;
    ColumnScene(){EXPECT_TRUE(queries.bindTerrain(terrain));EXPECT_TRUE(queries.publish({},1));}
    double ground() const {return double(voxy::terrain::lego::supportHeight(terrain,{0,0},.3f));}
};
static_assert(std::is_same_v<decltype(std::declval<const Queries&>().solids()),std::span<const Queries::Solid>>);
}

TEST(AdventureWalkable, GroundAndRaisedBridgeAreDistinctDescendingPhysicalSpans) {
    ColumnScene scene;const std::array bridge{box(2,{-2,3,-2},{2,3.32,2}),box(3,{-2,3,-2},{2,3.32,2})};
    ASSERT_TRUE(scene.queries.publish(bridge,2));ASSERT_EQ(scene.queries.solids().size(),2u);
    EXPECT_EQ(scene.queries.solids()[0].part,id(2));
    std::array<double,8> feet{};const auto column=scene.queries.walkableFeet({0,0},0,5,feet);
    ASSERT_TRUE(column.complete);ASSERT_EQ(column.count,2u);
    EXPECT_DOUBLE_EQ(feet[0],3.32+.005);EXPECT_DOUBLE_EQ(feet[1],scene.ground()+.005);
    EXPECT_NEAR(scene.ground(),.18,1e-6);
    for(size_t i=0;i<column.count;++i) {
        EXPECT_TRUE(scene.queries.clearCapsule({0,feet[i],0}));
        EXPECT_NEAR(scene.queries.supportHeight({0,0},.3,feet[i]-.005),feet[i]-.005,1e-12);
    }
}
TEST(AdventureWalkable, LowBridgeCeilingBlocksGroundButItsDeckRemainsUsable) {
    ColumnScene scene;const auto ceiling=box(2,{-2,1,-2},{2,1.32,2});
    ASSERT_TRUE(scene.queries.publish(std::span(&ceiling,1),2));
    std::array<double,8> feet{};const auto column=scene.queries.walkableFeet({0,0},0,4,feet);
    ASSERT_TRUE(column.complete);ASSERT_EQ(column.count,1u);EXPECT_DOUBLE_EQ(feet[0],1.32+.005);
    EXPECT_FALSE(scene.queries.clearCapsule({0,scene.ground()+.005,0}));
}
TEST(AdventureWalkable, PhysicalStairTreadsUseTheirActualSupportHeights) {
    ColumnScene scene;const std::array stairs{
        box(2,{1,0,-1},{2,.32,1}),box(3,{2,0,-1},{3,.64,1}),box(4,{3,0,-1},{4,.96,1})};
    ASSERT_TRUE(scene.queries.publish(stairs,2));
    for(size_t step=0;step<stairs.size();++step) {
        std::array<double,8> feet{};const glm::dvec2 point(1.5+double(step),0);
        const auto column=scene.queries.walkableFeet(point,0,2,feet);
        ASSERT_TRUE(column.complete);ASSERT_EQ(column.count,1u);
        EXPECT_DOUBLE_EQ(feet[0],stairs[step].maximum.y+.005);
        EXPECT_TRUE(scene.queries.clearCapsule({point.x,feet[0],point.y}));
    }
}
TEST(AdventureWalkable, InclusiveFeetLimitsExcludeOutOfRangeSupportAndPreserveEmptyResults) {
    ColumnScene scene;const double height=scene.ground()+.005;std::array<double,1> feet{-123};
    auto column=scene.queries.walkableFeet({0,0},height,height,feet);
    ASSERT_TRUE(column.complete);ASSERT_EQ(column.count,1u);EXPECT_DOUBLE_EQ(feet[0],height);
    feet[0]=-123;
    column=scene.queries.walkableFeet({0,0},std::nextafter(height,1.),1,feet);
    EXPECT_TRUE(column.complete);EXPECT_EQ(column.count,0u);EXPECT_EQ(feet[0],-123);
    column=scene.queries.walkableFeet({0,0},-1,std::nextafter(height,0.),feet);
    EXPECT_TRUE(column.complete);EXPECT_EQ(column.count,0u);EXPECT_EQ(feet[0],-123);
    column=scene.queries.walkableFeet({0,0},1,2,{});EXPECT_TRUE(column.complete);EXPECT_EQ(column.count,0u);
    column=scene.queries.walkableFeet({0,0},0,2,{});EXPECT_FALSE(column.complete);EXPECT_EQ(column.count,0u);
}
TEST(AdventureWalkable, ExactDistinctLevelsAreNotLostToTheSupportQueryTolerance) {
    ColumnScene scene;const std::array layers{box(2,{-2,2,-2},{2,3,2}),box(3,{-2,2,-2},{2,3.0000005,2})};
    ASSERT_TRUE(scene.queries.publish(layers,2));std::array<double,8> feet{};
    const auto column=scene.queries.walkableFeet({0,0},0,4,feet);
    ASSERT_TRUE(column.complete);ASSERT_EQ(column.count,3u);
    EXPECT_DOUBLE_EQ(feet[0],3.0000005+.005);EXPECT_DOUBLE_EQ(feet[1],3.+.005);
    EXPECT_DOUBLE_EQ(feet[2],scene.ground()+.005);
}
TEST(AdventureWalkable, OutputOverflowIsIncompleteAndCannotPublishPartialLayers) {
    ColumnScene scene;std::vector<Queries::Solid> layers;
    for(int level=1;level<=3;++level)layers.push_back(box(uint64_t(level+1),{-2,level*3.-.2,-2},{2,level*3.,2}));
    ASSERT_TRUE(scene.queries.publish(layers,2));std::array<double,2> feet{-123,-456};
    const auto column=scene.queries.walkableFeet({0,0},0,10,feet);
    EXPECT_FALSE(column.complete);EXPECT_EQ(column.count,0u);EXPECT_EQ(feet[0],-123);EXPECT_EQ(feet[1],-456);
    std::array<double,4> complete{};const auto enough=scene.queries.walkableFeet({0,0},0,10,complete);
    ASSERT_TRUE(enough.complete);ASSERT_EQ(enough.count,4u);EXPECT_DOUBLE_EQ(complete[3],scene.ground()+.005);
}
TEST(AdventureWalkable, SixtyFourDistinctSupportsFitButTheNextOneMakesTheColumnIncomplete) {
    ColumnScene scene;std::vector<Queries::Solid> layers;
    for(int level=1;level<=63;++level)layers.push_back(box(uint64_t(level+1),{-2,level*3.-.2,-2},{2,level*3.,2}));
    ASSERT_TRUE(scene.queries.publish(layers,2));std::array<double,128> feet{};
    const auto allowed=scene.queries.walkableFeet({0,0},0,200,feet);
    ASSERT_TRUE(allowed.complete);ASSERT_EQ(allowed.count,64u);EXPECT_DOUBLE_EQ(feet[63],scene.ground()+.005);
    layers.push_back(box(65,{-2,191.8,-2},{2,192,2}));ASSERT_TRUE(scene.queries.publish(layers,3));feet.fill(-123);
    const auto overflow=scene.queries.walkableFeet({0,0},0,200,feet);
    EXPECT_FALSE(overflow.complete);EXPECT_EQ(overflow.count,0u);for(double value:feet)EXPECT_EQ(value,-123);
}
TEST(AdventureWalkable, DuplicatePrimitivesDoNotSpendTheDistinctSurfaceBudget) {
    ColumnScene scene;std::vector<Queries::Solid> copies;
    for(uint64_t i=0;i<70;++i)copies.push_back(box(i+2,{-2,3,-2},{2,3.32,2}));
    ASSERT_TRUE(scene.queries.publish(copies,2));std::array<double,2> feet{};
    const auto column=scene.queries.walkableFeet({0,0},0,5,feet);
    ASSERT_TRUE(column.complete);ASSERT_EQ(column.count,2u);
}
TEST(AdventureWalkable, InvalidArgumentsAndUnsupportedTerrainExtentsNeverPublishFeet) {
    ColumnScene scene;std::array<double,8> feet;feet.fill(-123);
    const double nan=std::numeric_limits<double>::quiet_NaN(),inf=std::numeric_limits<double>::infinity();
    const auto invalid=[&](glm::dvec2 point,double low,double high,double radius=.3,double height=1.7){
        const auto column=scene.queries.walkableFeet(point,low,high,feet,radius,height);
        EXPECT_FALSE(column.complete);EXPECT_EQ(column.count,0u);for(double value:feet)EXPECT_EQ(value,-123);
    };
    invalid({nan,0},0,1);invalid({0,inf},0,1);invalid({0,0},nan,1);invalid({0,0},0,inf);
    invalid({0,0},1,0);invalid({0,0},0,1,0);invalid({0,0},0,1,-.3);invalid({0,0},0,1,nan);
    invalid({0,0},0,1,4.01,9);invalid({0,0},0,1,.3,.59);invalid({0,0},0,1,.3,10.01);invalid({0,0},0,1,.3,inf);
    invalid({100001,0},0,1);invalid({0,0},-100000,0);invalid({0,0},0,100000);
    invalid({31.4,0},0,1);invalid({-31.4,0},0,1);invalid({0,31.4},0,1);invalid({0,-31.4},0,1);
    Queries unbound;const auto missing=unbound.walkableFeet({0,0},0,1,feet);
    EXPECT_FALSE(missing.complete);EXPECT_EQ(missing.count,0u);
}
