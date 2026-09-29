#include "game/adventure/spatial_queries.hpp"
#include <gtest/gtest.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <future>

namespace voxy::game::adventure {
namespace {
using Queries=AdventureSpatialQueries;
construction::DurableId identity(uint64_t value) {
    construction::WorldNamespace world{};world.bytes[0]=91;return {world,value};
}
Queries::Solid solid(uint64_t part,glm::dvec3 low,glm::dvec3 high) {
    return {identity(1),identity(part),low,high};
}
struct QueryScene {
    std::vector<uint16_t> samples=std::vector<uint16_t>(256u*256u,32768);
    terrain::lego::Surface terrain{samples,256,256,8.f,1.f};
    Queries queries;
    QueryScene(){EXPECT_TRUE(queries.bindTerrain(terrain));}
};
}

TEST(AdventureQueryCandidates, FirstLastAndDedupWordBoundariesRemainPresentAtFullCapacity) {
    QueryScene scene;
    std::vector<Queries::Solid> boxes(Queries::maximumSolids);
    for(size_t index=0;index<boxes.size();++index)
        boxes[index]=solid(index+2,{1000,1,1000},{1001,2,1001});
    constexpr std::array<size_t,4> indices{0,63,64,Queries::maximumSolids-1};
    for(size_t probe=0;probe<indices.size();++probe) {
        const double x=-48.+32.*double(probe);
        const auto index=indices[probe];
        // Crossing both X and Z collision-sector boundaries intentionally puts
        // the same primitive in four visited buckets.
        boxes[index]=solid(index+2,{x-1,3,-1},{x+1,4,1});
    }
    ASSERT_TRUE(scene.queries.publish(boxes,1));
    ASSERT_EQ(scene.queries.solidCount(),Queries::maximumSolids);
    for(size_t probe=0;probe<indices.size();++probe) {
        SCOPED_TRACE(probe);const double x=-48.+32.*double(probe);
        EXPECT_NEAR(scene.queries.supportHeight({x,0},.3,5),4.,1e-12);
        EXPECT_FALSE(scene.queries.clearCapsule({x,3.1,0},.3,1.7));
        EXPECT_TRUE(scene.queries.clearCapsule({x,4.005,0},.3,1.7));
        const auto ray=scene.queries.raycast({x-2,3.5,0},{1,0,0},4);
        ASSERT_TRUE(ray.complete);ASSERT_TRUE(ray.hit);EXPECT_FALSE(ray.terrain);
        EXPECT_EQ(ray.part,identity(indices[probe]+2));EXPECT_NEAR(ray.distance,1.,1e-12);
        const auto sweep=scene.queries.sweepCapsule({x-2,3.1,0},{x+2,3.1,0},.3,1.7);
        ASSERT_TRUE(sweep.complete);EXPECT_TRUE(sweep.hit);EXPECT_FALSE(sweep.startOverlapped);
        std::array<double,4> feet{};
        const auto column=scene.queries.walkableFeet({x,0},0,6,feet);
        ASSERT_TRUE(column.complete);ASSERT_EQ(column.count,2u);
        EXPECT_DOUBLE_EQ(feet[0],4.005);
    }
    auto overflow=boxes;overflow.push_back(solid(Queries::maximumSolids+2,{0,3,0},{1,4,1}));
    EXPECT_FALSE(scene.queries.publish(overflow,2));EXPECT_EQ(scene.queries.revision(),1u);
    EXPECT_EQ(scene.queries.solidCount(),Queries::maximumSolids);
}

TEST(AdventureQueryCandidates, MultiSectorEqualDistanceHitsRetainTheirPublishedTieOrder) {
    QueryScene scene;
    std::vector<Queries::Solid> boxes;
    for(size_t index=0;index<130;++index)boxes.push_back(solid(index+2,{-17,3,-17},{17,4,17}));
    ASSERT_TRUE(scene.queries.publish(boxes,1));
    // All entries span multiple buckets. Deduplication must not reinsert one later
    // and replace the last published tie with an earlier primitive.
    for(int repeat=0;repeat<20;++repeat) {
        const auto ray=scene.queries.raycast({-20,3.5,0},{1,0,0},40);
        ASSERT_TRUE(ray.complete);ASSERT_TRUE(ray.hit);
        EXPECT_EQ(ray.part,identity(131));EXPECT_DOUBLE_EQ(ray.distance,3.);
        std::array<double,3> feet{};
        const auto column=scene.queries.walkableFeet({0,0},0,6,feet);
        ASSERT_TRUE(column.complete);ASSERT_EQ(column.count,2u);
        EXPECT_DOUBLE_EQ(feet[0],4.005);
    }
    EXPECT_FALSE(scene.queries.publish({},1));
    ASSERT_TRUE(scene.queries.publish({},2));
    const auto empty=scene.queries.raycast({-20,3.5,0},{1,0,0},40);
    ASSERT_TRUE(empty.complete);EXPECT_FALSE(empty.hit);
    EXPECT_TRUE(scene.queries.clearCapsule({0,.185,0},.3,1.7));
}

TEST(AdventureQueryCandidates, SingleSectorHitsPreserveTieOrderAndNegativeSectorCoordinates) {
    QueryScene scene;
    std::vector<Queries::Solid> boxes;
    constexpr std::array<double,4> centers{-24.,-8.,8.,24.};
    for(size_t probe=0;probe<centers.size();++probe) {
        const double p=centers[probe];
        // Every ray and capsule stays inside one sector. Identical boxes
        // still use the last published primitive to break ray distance ties.
        for(size_t duplicate=0;duplicate<65;++duplicate)
            boxes.push_back(solid(probe*65+duplicate+2,{p-.5,3,p-.5},{p+.5,4,p+.5}));
    }
    ASSERT_TRUE(scene.queries.publish(boxes,1));
    for(size_t probe=0;probe<centers.size();++probe) {
        SCOPED_TRACE(probe);const double p=centers[probe];
        const auto ray=scene.queries.raycast({p-2,3.5,p},{1,0,0},4);
        ASSERT_TRUE(ray.complete);ASSERT_TRUE(ray.hit);
        EXPECT_EQ(ray.part,identity(probe*65+66));EXPECT_DOUBLE_EQ(ray.distance,1.5);
        EXPECT_DOUBLE_EQ(scene.queries.supportHeight({p,p},.3,5),4.);
        EXPECT_FALSE(scene.queries.clearCapsule({p,3.1,p},.3,1.7));
        EXPECT_TRUE(scene.queries.clearCapsule({p,4.005,p},.3,1.7));
        const auto sweep=scene.queries.sweepCapsule({p-2,3.1,p},{p+2,3.1,p},.3,1.7);
        ASSERT_TRUE(sweep.complete);EXPECT_TRUE(sweep.hit);
    }
    ASSERT_TRUE(scene.queries.publish({},2));
    const auto empty=scene.queries.raycast({6,3.5,8},{1,0,0},4);
    ASSERT_TRUE(empty.complete);EXPECT_FALSE(empty.hit);
    EXPECT_TRUE(scene.queries.clearCapsule({8,.185,8},.3,1.7));
}

TEST(AdventureQueryCandidates, IndependentConcurrentReadsDoNotShareDedupScratch) {
    QueryScene scene;
    const std::array boxes{solid(2,{-17,3,-17},{17,4,17}),solid(3,{-1,6,-1},{1,7,1})};
    ASSERT_TRUE(scene.queries.publish(boxes,1));
    const auto worker=[&] {
        bool complete=true;
        for(int repeat=0;repeat<100;++repeat) {
            const auto ray=scene.queries.raycast({-20,3.5,0},{1,0,0},40);
            complete&=ray.complete&&ray.hit&&ray.part==identity(2)&&ray.distance==3.;
            complete&=scene.queries.supportHeight({0,0},.3,10)==7.;
            complete&=!scene.queries.clearCapsule({0,6.1,0},.3,1.7);
        }
        return complete;
    };
    auto first=std::async(std::launch::async,worker);
    auto second=std::async(std::launch::async,worker);
    EXPECT_TRUE(first.get());EXPECT_TRUE(second.get());
}

TEST(AdventureQueryCandidates, EveryPublishedSolidCanMatchAtFullCapacity) {
    QueryScene scene;
    // Unlike the sparse first/last-index fixture, every output candidate slot
    // is occupied here. Exercise both direct-bucket and deduplicated traversal.
    for(const bool crossing:std::array<bool,2>{false,true}) {
        SCOPED_TRACE(crossing);
        const double center=crossing?0.:8.,extent=crossing?17.:.5;
        std::vector<Queries::Solid> boxes;
        boxes.reserve(Queries::maximumSolids);
        for(size_t index=0;index<Queries::maximumSolids;++index)
            boxes.push_back(solid(index+2,{center-extent,3,center-extent},
                {center+extent,4,center+extent}));
        ASSERT_TRUE(scene.queries.publish(boxes,scene.queries.revision()+1));
        const double start=crossing?-20.:6.,distance=crossing?40.:4.;
        const auto ray=scene.queries.raycast({start,3.5,center},{1,0,0},distance);
        ASSERT_TRUE(ray.complete);ASSERT_TRUE(ray.hit);EXPECT_FALSE(ray.terrain);
        EXPECT_EQ(ray.part,identity(Queries::maximumSolids+1));
        EXPECT_DOUBLE_EQ(ray.distance,crossing?3.:1.5);
        EXPECT_DOUBLE_EQ(scene.queries.supportHeight({center,center},.3,5),4.);
        EXPECT_FALSE(scene.queries.clearCapsule({center,3.1,center},.3,1.7));
        std::array<double,3> feet{-123,-456,-789};
        const auto column=scene.queries.walkableFeet({center,center},0,6,feet);
        ASSERT_TRUE(column.complete);ASSERT_EQ(column.count,2u);
        EXPECT_DOUBLE_EQ(feet[0],4.005);EXPECT_DOUBLE_EQ(feet[2],-789.);
    }
}

TEST(AdventureQueryCandidates, OversizedMultiSectorPublicQueriesFailClosed) {
    QueryScene scene;
    const std::array boxes{solid(2,{-17,3,-17},{17,4,17})};
    ASSERT_TRUE(scene.queries.publish(boxes,1));
    const glm::dvec3 from{-64,3.1,-64},to{64,3.1,64};
    constexpr double radius=4,height=8;
    const auto low=glm::min(from,to)-glm::dvec3(radius,0,radius);
    const auto high=glm::max(from,to)+glm::dvec3(radius,height,radius);
    const auto sectorExtent=[&](int axis) {
        return int(std::floor(high[axis]/Queries::sectorSize))
            -int(std::floor(low[axis]/Queries::sectorSize))+1;
    };
    ASSERT_GT(sectorExtent(0)*sectorExtent(2),64);
    // The public travel bounds reject this envelope before candidate scanning.
    // A valid capsule/ray cannot currently reach the private >64-sector guard.
    const auto capsule=scene.queries.sweepCapsule(from,to,radius,height);
    EXPECT_FALSE(capsule.complete);EXPECT_FALSE(capsule.hit);
    const auto sphere=scene.queries.sweepSphere(from,to,radius,scene.queries.revision());
    EXPECT_FALSE(sphere.complete);EXPECT_FALSE(sphere.hit);
    const auto ray=scene.queries.raycast(from,to-from,glm::length(to-from));
    EXPECT_FALSE(ray.complete);EXPECT_FALSE(ray.hit);
    EXPECT_EQ(scene.queries.revision(),1u);
    EXPECT_DOUBLE_EQ(scene.queries.supportHeight({0,0},.3,5),4.);
}

TEST(AdventureQueryCandidates, OverlappingSweepRetainsLastCandidateNormalPrecedence) {
    QueryScene scene;
    // The wider box resolves upward; the narrower box resolves along +X.
    // merge() intentionally accepts each start-overlap, including equal-time
    // ones. Preserve its last-candidate precedence in either bucket path.
    for(const double center:std::array<double,2>{0.,8.}) {
        auto upward=solid(2,{center-17,3,center-17},{center+17,4,center+17});
        auto sideways=solid(3,{center-17,3,center-17},{center+.25,4,center+17});
        for(const bool reversed:std::array<bool,2>{false,true}) {
            SCOPED_TRACE(center);
            SCOPED_TRACE(reversed);
            std::array boxes{upward,sideways};
            if(reversed)std::swap(boxes[0],boxes[1]);
            ASSERT_TRUE(scene.queries.publish(boxes,scene.queries.revision()+1));
            const auto result=scene.queries.sweepCapsule({center,3.1,center},{center+4,3.1,center},.3,1.7);
            ASSERT_TRUE(result.complete);ASSERT_TRUE(result.hit);EXPECT_TRUE(result.startOverlapped);
            EXPECT_DOUBLE_EQ(result.distance,0.);
            EXPECT_DOUBLE_EQ(result.normal.x,reversed?0.:1.);
            EXPECT_DOUBLE_EQ(result.normal.y,reversed?1.:0.);
            EXPECT_DOUBLE_EQ(result.normal.z,0.);
        }
    }
}
} // namespace voxy::game::adventure
