#include "game/adventure/cannon_physics_scene.hpp"
#include "game/adventure/ldraw_blacksmith_geometry.hpp"

#include <gtest/gtest.h>
#include <cmath>
#include <limits>
#include <set>

namespace voxy::game::adventure {
namespace {
using Solid=AdventureSpatialQueries::Solid;
bool contains(const CannonPhysicsScene::Packet& packet, glm::dvec3 point) {
    for(const auto& partition:packet.partitions) {
        const auto p=(point-partition.origin)/physics::kAuthoredShapeTickMetres;
        for(const auto& cell:partition.shape.cells())
            if(p.x>=cell.minimum[0]&&p.x<cell.maximum[0]
                &&p.y>=cell.minimum[1]&&p.y<cell.maximum[1]
                &&p.z>=cell.minimum[2]&&p.z<cell.maximum[2])return true;
    }
    return false;
}
TEST(CannonPhysicsScene, CompleteBlacksmithFitsBoundedPartitionsWithoutDroppingSolids) {
    const glm::dvec3 origin(1208,187.005,-1032);
    std::vector<Solid> solids;
    for(const auto& box:blacksmithMeshSolids)
        solids.push_back({{}, {}, origin+box.minimum,origin+box.maximum});
    std::string error;
    const auto packet=CannonPhysicsScene::compile(solids,origin,73,error);
    ASSERT_TRUE(packet)<<error;
    EXPECT_EQ(packet->revision,73u);
    EXPECT_EQ(packet->solidCount,4054u);
    EXPECT_GT(packet->partitions.size(),1u);
    EXPECT_LE(packet->partitions.size(),CannonPhysicsScene::maximumPartitions);
    EXPECT_LE(packet->cost.cells,CannonPhysicsScene::maximumCells);
    std::set<uint32_t> labels;
    for(const auto& partition:packet->partitions)
        for(const auto& cell:partition.shape.cells())labels.insert(cell.source);
    EXPECT_EQ(labels.size(),solids.size());
    // Every actual source box centre is represented by GPU collision, including
    // wall ornament and roof pieces missed by the earlier whole-house proxy.
    for(const auto& box:solids) ASSERT_TRUE(contains(*packet,(box.minimum+box.maximum)*.5));
    EXPECT_FALSE(contains(*packet,origin+glm::dvec3(50,1,0)));
    RecordProperty("blacksmith_partitions",int(packet->partitions.size()));
    RecordProperty("blacksmith_cells",int(packet->cost.cells));
    RecordProperty("blacksmith_faces",int(packet->cost.faces));
    RecordProperty("blacksmith_bytes",int(packet->cost.bytes));
}
TEST(CannonPhysicsScene, DistantLocalFramePreservesDoorAndConservativeBounds) {
    const glm::dvec3 origin(100000008,.005,-100000032);
    const std::array<Solid,3> solids{{
        {{},{},origin+glm::dvec3(-4,0,0),origin+glm::dvec3(-1,6,.2)},
        {{},{},origin+glm::dvec3(1,0,0),origin+glm::dvec3(4,6,.2)},
        {{},{},origin+glm::dvec3(-1,5,0),origin+glm::dvec3(1,6,.2)}}};
    std::string error;const auto packet=CannonPhysicsScene::compile(solids,origin,1,error);
    ASSERT_TRUE(packet)<<error;
    EXPECT_TRUE(contains(*packet,origin+glm::dvec3(-2,3,.1)));
    EXPECT_TRUE(contains(*packet,origin+glm::dvec3(2,3,.1)));
    EXPECT_TRUE(contains(*packet,origin+glm::dvec3(0,5.5,.1)));
    EXPECT_FALSE(contains(*packet,origin+glm::dvec3(0,2,.1)));
    EXPECT_FALSE(contains(*packet,origin+glm::dvec3(0,2,.4)));
}
TEST(CannonPhysicsScene, InvalidAndOverBudgetPacketsDoNotBecomePartialScenes) {
    const Solid valid{{},{},{0,0,0},{1,1,1}};
    std::string error;CannonPhysicsScene bridge;
    EXPECT_FALSE(bridge.prepare(std::span(&valid,1),{},0,error));
    EXPECT_TRUE(bridge.empty());EXPECT_FALSE(bridge.ready(0));
    auto invalid=valid;invalid.maximum.x=std::numeric_limits<double>::infinity();
    EXPECT_FALSE(bridge.prepare(std::span(&invalid,1),{},1,error));
    EXPECT_TRUE(bridge.empty());
    invalid=valid;invalid.maximum.y=invalid.minimum.y;
    EXPECT_FALSE(bridge.prepare(std::span(&invalid,1),{},1,error));
    std::vector<Solid> tooMany(AdventureSpatialQueries::maximumSolids+1,valid);
    EXPECT_FALSE(bridge.prepare(tooMany,{},1,error));
    EXPECT_TRUE(bridge.empty());
    EXPECT_TRUE(bridge.prepare(std::span(&valid,1),{},1,error))<<error;
    EXPECT_EQ(bridge.pendingRevision(),1u);EXPECT_TRUE(bridge.needsQuiescentBoundary());
    EXPECT_FALSE(bridge.ready(1));EXPECT_FALSE(bridge.waitingForExecution());
    EXPECT_FALSE(bridge.prepare({}, {},2,error));
    EXPECT_EQ(bridge.pendingRevision(),1u);
    bridge.abandonAfterWorldShutdown();EXPECT_TRUE(bridge.empty());
}
TEST(CannonPhysicsScene, InitialMotionAndTransferredProjectileAreValidatedBeforeOwnership) {
    const Solid solid{{},{},{0,0,0},{1,1,1}};std::string error;
    auto packet=CannonPhysicsScene::compile(std::span(&solid,1),{},1,error);ASSERT_TRUE(packet);
    packet->partitions[0].originVelocity={2,0,0};CannonPhysicsScene scene;
    EXPECT_FALSE(scene.prepareAuthored(*packet,error));EXPECT_TRUE(scene.empty());
    packet->partitions[0].dynamic=true;packet->partitions[0].angularVelocity={0,NAN,0};
    EXPECT_FALSE(scene.prepareAuthored(*packet,error));EXPECT_TRUE(scene.empty());
    packet->partitions[0].angularVelocity={0,1,0};packet->consumeBody=physics::BodyHandle{};
    EXPECT_FALSE(scene.prepareAuthored(*packet,error));EXPECT_TRUE(scene.empty());
    packet->consumeBody=physics::BodyHandle{7,3};
    EXPECT_TRUE(scene.prepareAuthored(*packet,error))<<error;EXPECT_FALSE(scene.empty());
    scene.abandonAfterWorldShutdown();
}
TEST(CannonPhysicsScene, EmptyRevisionIsValidAndLabelsRemainInputScoped) {
    std::string error;const auto empty=CannonPhysicsScene::compile({}, {},4,error);
    ASSERT_TRUE(empty)<<error;EXPECT_TRUE(empty->partitions.empty());EXPECT_EQ(empty->cost.cells,0u);
    const std::array<Solid,2> touching{{{{},{},{0,0,0},{1,1,1}},{{},{},{1,0,0},{2,1,1}}}};
    const auto packet=CannonPhysicsScene::compile(touching,{},5,error);ASSERT_TRUE(packet)<<error;
    ASSERT_EQ(packet->partitions.size(),1u);
    const auto origin=packet->partitions[0].origin;
    const auto join=int32_t(std::round((1-origin.x)/physics::kAuthoredShapeTickMetres));
    for(const auto& face:packet->partitions[0].shape.faces())
        EXPECT_FALSE(face.axis==0 && face.minimum[0]==join);
}
} // namespace
} // namespace voxy::game::adventure
