#include "game/adventure/frontier_interactions.hpp"
#include "game/adventure/adventure_player.hpp"
#include <gtest/gtest.h>
#include <cmath>

namespace voxy::game::adventure {
namespace {
GridPosition grid(glm::dvec3 p){return {int32_t(std::lround(p.x*50)),int32_t(std::lround(p.y*50)),int32_t(std::lround(p.z*50))};}
struct InteractionGeometry {
    std::vector<uint16_t> samples=std::vector<uint16_t>(64*64,32768);
    terrain::lego::Surface terrain{samples,64,64,600,1};
    AdventureState state;AdventureContent content;AdventureSpatialQueries queries;std::string error;
    uint64_t nextId=4;
    InteractionGeometry() {
        state.world.bytes[0]=71;state.player={0,2.325,0,0};content.frontier=true;
        WorldStructure home;home.id=3;state.structures.push_back(home);
    }
    uint64_t part(PieceKind kind,glm::dvec3 p,uint8_t yaw=0) {
        const auto id=nextId++;state.structures[0].parts.push_back({id,kind,grid(p),yaw,0});return id;
    }
    uint64_t furniture(PieceKind kind,glm::dvec3 p) {
        const auto piece=part(kind,p);StructureComponent c;c.id=nextId++;c.structure=3;c.part=piece;c.kind=buildingDefinition(kind)->furniture;
        state.components.push_back(c);return c.id;
    }
    bool publish() {
        std::vector<AdventureSpatialQueries::Solid> solids;
        return compileSolids(state,solids,error)&&queries.bindTerrain(terrain)&&queries.publish(solids,queries.revision()+1);
    }
};
}
TEST(FrontierInteractions, BeaconServicePointWinsOverNearbyBenchButBenchWorksOutsideService) {
    InteractionGeometry f;
    const auto bench=f.furniture(PieceKind::FrontierWorkbench,{4,2.32,0});
    f.content.frontierSites={{20,{0,2.325,0,0},FrontierSiteKind::Beacon,{}}};f.state.frontier.sites={{20,0,0,0}};
    ASSERT_TRUE(f.publish())<<f.error;
    auto selected=frontierNearbyTarget(f.state,f.content,f.queries,{100,0,100});
    EXPECT_EQ(selected.kind,FrontierUseKind::Site);EXPECT_EQ(selected.id,20u);
    f.state.player={5,2.325,-4,0};
    selected=frontierNearbyTarget(f.state,f.content,f.queries,{100,0,100});
    EXPECT_EQ(selected.kind,FrontierUseKind::Furniture);EXPECT_EQ(selected.id,bench);
}
TEST(FrontierInteractions, HighCacheRequiresAnActualRouteToItsElevation) {
    InteractionGeometry f;f.content.frontierSites={{1,{0,8.085,0,0},FrontierSiteKind::Cache,{ItemKind::Scrap,18}}};
    f.state.frontier.sites={{1,0,0,0}};ASSERT_TRUE(f.publish())<<f.error;
    EXPECT_EQ(frontierNearbyTarget(f.state,f.content,f.queries,{100,0,100}).kind,FrontierUseKind::None);
    // Nearness under the platform, including the old two-stud vertical band,
    // cannot offer a reward. Discovery is separate from this physical claim.
    f.state.player={2,6.085,0,0};
    EXPECT_EQ(frontierNearbyTarget(f.state,f.content,f.queries,{100,0,100}).kind,FrontierUseKind::None);
    f.state.player={2,6.985,0,0};
    EXPECT_EQ(frontierNearbyTarget(f.state,f.content,f.queries,{100,0,100}).kind,FrontierUseKind::None);
    f.state.player={2,7.585,0,0};
    EXPECT_EQ(frontierNearbyTarget(f.state,f.content,f.queries,{100,0,100}).kind,FrontierUseKind::Site);
    f.state.player={2,8.085,0,0};
    const auto selected=frontierNearbyTarget(f.state,f.content,f.queries,{100,0,100});
    EXPECT_EQ(selected.kind,FrontierUseKind::Site);EXPECT_EQ(selected.id,1u);
}
TEST(FrontierInteractions, ScaledFurnitureReachDoesNotAllowUseThroughCover) {
    InteractionGeometry f;const auto bench=f.furniture(PieceKind::FrontierWorkbench,{6,2.32,0});
    ASSERT_TRUE(f.publish())<<f.error;
    EXPECT_TRUE(reachableComponent(f.state,bench,f.queries,f.error,true))<<f.error;
    EXPECT_FALSE(reachableComponent(f.state,bench,f.queries,f.error,false));
    f.part(PieceKind::FrontierWall,{3,2.32,0},1);ASSERT_TRUE(f.publish())<<f.error;
    EXPECT_FALSE(reachableComponent(f.state,bench,f.queries,f.error,true));
    EXPECT_EQ(frontierNearbyTarget(f.state,f.content,f.queries,{100,0,100}).kind,FrontierUseKind::None);
}
TEST(FrontierInteractions, HarvestAllowsItsOwnColliderButRefusesInterveningPlayerWall) {
    InteractionGeometry f;f.content.resourceNodes={{1,{6,2.325,0,0},{ItemKind::Wood,16}}};
    std::vector<AdventureSpatialQueries::Solid> solids{{{f.state.world,frontierSceneryId},{f.state.world,FrontierWorld::resourcePart(1)},{5.5,2.32,-.5},{6.5,6.32,.5}}};
    ASSERT_TRUE(f.queries.bindTerrain(f.terrain));ASSERT_TRUE(f.queries.publish(solids,1));
    EXPECT_EQ(frontierNearbyTarget(f.state,f.content,f.queries,{100,0,100}).kind,FrontierUseKind::Resource);
    solids.push_back({{f.state.world,3},{f.state.world,8},{2.8,2.32,-3},{3.2,8.08,3}});ASSERT_TRUE(f.queries.publish(solids,2));
    EXPECT_EQ(frontierNearbyTarget(f.state,f.content,f.queries,{100,0,100}).kind,FrontierUseKind::None);
}
TEST(FrontierInteractions, ResourcePromptAndAuthoritativeGatherAgreeAtLowCoverEdge) {
    InteractionGeometry f;f.content.resourceNodes={{1,{6,2.325,0,0},{ItemKind::Scrap,12}}};
    std::vector<AdventureSpatialQueries::Solid> solids{
        {{f.state.world,frontierSceneryId},{f.state.world,FrontierWorld::resourcePart(1)},{5.5,2.32,-.5},{6.5,6.32,.5}},
        {{f.state.world,3},{f.state.world,8},{4.65,2.32,-1},{4.85,4.23,1}}};
    ASSERT_TRUE(f.queries.bindTerrain(f.terrain));ASSERT_TRUE(f.queries.publish(solids,1));
    // This lower former prompt eye sees over the cover; the real gather line
    // intersects it. The player must receive no actionable resource prompt.
    EXPECT_TRUE(frontierInteractionSight(f.queries,{0,6.325,0},{6,3.825,0},FrontierWorld::resourcePart(1)));
    auto gathered=f.state;gathered.depletedNodes={1};
    EXPECT_FALSE(validateInteractions(f.state,gathered,f.content,f.queries,f.error));
    EXPECT_EQ(frontierNearbyTarget(f.state,f.content,f.queries,{100,0,100}).kind,FrontierUseKind::None);
    solids.pop_back();ASSERT_TRUE(f.queries.publish(solids,2));
    EXPECT_TRUE(validateInteractions(f.state,gathered,f.content,f.queries,f.error))<<f.error;
    const auto target=frontierNearbyTarget(f.state,f.content,f.queries,{100,0,100});
    EXPECT_EQ(target.kind,FrontierUseKind::Resource);EXPECT_EQ(target.id,1u);
}
TEST(FrontierInteractions, AcceptedWallsBlockMeleeAndRemovingThemRestoresTheAttackLine) {
    InteractionGeometry f;ASSERT_TRUE(f.publish())<<f.error;
    const PlayerPose player{0,2.325,0,-1.5707963267948966},enemy{6,2.325,0,1.5707963267948966};
    EXPECT_TRUE(frontierStrikeReachable(f.queries,player,enemy,8));
    f.part(PieceKind::FrontierWall,{3,2.32,0},1);ASSERT_TRUE(f.publish())<<f.error;
    EXPECT_FALSE(frontierStrikeReachable(f.queries,player,enemy,8));EXPECT_FALSE(frontierStrikeReachable(f.queries,enemy,player,8));
    f.state.structures.front().parts.clear();ASSERT_TRUE(f.publish())<<f.error;
    EXPECT_TRUE(frontierStrikeReachable(f.queries,player,enemy,8));
    EXPECT_FALSE(frontierStrikeReachable(f.queries,player,{6,7,0,0},8));
    EXPECT_FALSE(frontierStrikeReachable(f.queries,player,{-6,2.325,0,0},8));
}
TEST(FrontierInteractions, ShelteredScaledBedReturnsARealClearRecoveryCapsule) {
    InteractionGeometry f;
    for(double x:{-3.,3.})for(double z:{-3.,3.})f.part(PieceKind::FrontierFoundation,{x,2,z});
    for(double x:{-3.,3.})f.part(PieceKind::FrontierWall,{x,2.32,-6});
    for(double z:{-3.,3.}){f.part(PieceKind::FrontierWall,{-6,2.32,z},1);f.part(PieceKind::FrontierWall,{6,2.32,z},1);}
    for(double x:{-3.,3.})for(double z:{-3.,3.})f.part(PieceKind::FrontierRoof,{x,8.08,z});
    const auto bed=f.furniture(PieceKind::FrontierBed,{-1,2.32,0});
    ASSERT_TRUE(f.publish())<<f.error;
    PlayerPose recovery;ASSERT_TRUE(usableBed(f.state,bed,f.queries,recovery,f.error))<<f.error;
    EXPECT_TRUE(f.queries.clearCapsule({recovery.x,recovery.y,recovery.z},1.12,4.76));
    AdventurePlayer player;EXPECT_TRUE(player.initialize(f.queries,{recovery.x,recovery.y,recovery.z},-200,0,2.8,.4));
    std::erase_if(f.state.structures.front().parts,[](const auto& part){return part.kind==PieceKind::FrontierRoof;});
    ASSERT_TRUE(f.publish())<<f.error;EXPECT_FALSE(usableBed(f.state,bed,f.queries,recovery,f.error));
}
TEST(FrontierInteractions, RecoveryCampRejectsBlockingSolidsWhileAllowingNearbyBuilding) {
    std::vector<uint16_t> samples(4096*4096,32768);terrain::lego::Surface surface{samples,4096,4096,600,1};
    FrontierWorld world;AdventureContent content;std::string error;ASSERT_TRUE(world.initialize(surface,content,error))<<error;
    AdventureState before,after;WorldStructure home;home.id=3;
    home.parts.push_back({4,PieceKind::FrontierWall,grid(world.start()),0,0});after.structures.push_back(home);
    EXPECT_FALSE(world.protectedEdit(before,after,error));EXPECT_NE(error.find("recovery"),std::string::npos);
    after.structures[0].parts[0].position=grid(world.start()+glm::dvec3(10,0,0));
    EXPECT_TRUE(world.protectedEdit(before,after,error))<<error;
    after.structures[0].parts[0].position=grid(world.start()+glm::dvec3(0,0,3));after.structures[0].parts[0].yawQuarterTurns=1;
    EXPECT_FALSE(world.protectedEdit(before,after,error));
}
} // namespace voxy::game::adventure
