#include "game/adventure/construction_policy.hpp"
#include "game/adventure/adventure_player.hpp"
#include <gtest/gtest.h>
#include <vector>

using namespace voxy::game::adventure;
namespace {
struct Fixture {
    std::vector<uint16_t> samples=std::vector<uint16_t>(64*64,32768);
    voxy::terrain::lego::Surface surface{samples,64,64,8.f,1.f};
    AdventureSpatialQueries queries;
    AdventureState state;
    uint64_t next=2;
    Fixture() {
        state.world.bytes[0]=17;state.player={0,.185,8,0};state.structures.push_back({1,1,1,{},{}});
        EXPECT_TRUE(queries.bindTerrain(surface));EXPECT_TRUE(queries.publish({},1));
    }
    uint64_t add(PieceKind kind,double x,double y,double z,uint8_t yaw=0) {
        const auto id=next++;state.structures[0].parts.push_back({id,kind,{int32_t(std::round(x*50)),int32_t(std::round(y*50)),int32_t(std::round(z*50))},yaw,0});return id;
    }
    bool publish() {std::vector<AdventureSpatialQueries::Solid> solids;std::string error;return compileSolids(state,solids,error)&&queries.publish(solids,queries.revision()+1);}
};
}
TEST(AdventureConstructionPolicy, GroundedPiecesMustStaySupportedAndRefusalsDoNotMutate) {
    Fixture f;const auto foundation=f.add(PieceKind::Foundation,0,0,0);f.add(PieceKind::Wall,0,.32,0);
    AdventureState empty=f.state;empty.structures.clear();std::string error;
    ASSERT_TRUE(validateConstruction(empty,f.state,f.queries,error))<<error;
    auto removed=f.state;std::erase_if(removed.structures[0].parts,[&](const auto& p){return p.id==foundation;});
    EXPECT_FALSE(validateConstruction(f.state,removed,f.queries,error));EXPECT_EQ(error,"Connect every piece to a grounded foundation");
    auto overlap=f.state;overlap.structures[0].parts.push_back({15,PieceKind::Wall,{0,16,0},0,0});
    EXPECT_FALSE(validateConstruction(f.state,overlap,f.queries,error));EXPECT_EQ(error,"Building pieces overlap");
    EXPECT_EQ(f.state.structures[0].parts.size(),2u);
}
TEST(AdventureConstructionPolicy, TerrainProtectedAccessPlayerAndReachFailBeforePublication) {
    Fixture f;AdventureState empty=f.state;empty.structures.clear();std::string error;
    f.add(PieceKind::Floor,0,0,0);EXPECT_FALSE(validateConstruction(empty,f.state,f.queries,error));EXPECT_EQ(error,"This piece intersects the terrain");
    f.state.structures[0].parts.clear();f.add(PieceKind::Foundation,0,0,8);
    EXPECT_FALSE(validateConstruction(empty,f.state,f.queries,error));EXPECT_EQ(error,"Move out of the building preview");
    f.state.structures[0].parts.clear();f.add(PieceKind::Foundation,0,0,-20);
    EXPECT_FALSE(validateConstruction(empty,f.state,f.queries,error));EXPECT_EQ(error,"Move closer to build");
}
TEST(AdventureConstructionPolicy, QuarterTurnsPreserveOpeningAndActualFurnitureBounds) {
    Fixture f;f.add(PieceKind::Doorway,0,.32,0,1);const auto bed=f.add(PieceKind::Bed,3,.32,0,1);
    ASSERT_TRUE(f.publish());EXPECT_TRUE(f.queries.clearCapsule({0,.325,0}));
    EXPECT_FALSE(f.queries.clearCapsule({0,.325,1}));EXPECT_FALSE(f.queries.clearCapsule({3,.325,0}));
    std::vector<AdventureSpatialQueries::Solid> solids;std::string error;ASSERT_TRUE(compileSolids(f.state,solids,error));
    double xmin=100,xmax=-100;for(const auto& s:solids)if(s.part.counter==bed){xmin=std::min(xmin,s.minimum.x);xmax=std::max(xmax,s.maximum.x);}
    EXPECT_NEAR(xmax-xmin,2,1e-9);
}
TEST(AdventureConstructionPolicy, BedNeedsRoofWallsAndClearRecoveryBesideFurniture) {
    Fixture f;
    // A4x4 room with a centered bed; the open south side is a valid doorway.
    for(double x:{-1.,1.})for(double z:{-1.,1.})f.add(PieceKind::Foundation,x,0,z);
    for(double x:{-1.,1.})f.add(PieceKind::Wall,x,.32,-1.84);
    for(double z:{-1.,1.}) {f.add(PieceKind::Wall,-2.16,.32,z,1);f.add(PieceKind::Wall,2.16,.32,z,1);}
    for(double x:{-1.,1.})for(double z:{-1.,1.})f.add(PieceKind::Roof,x,3.2,z);
    const auto bed=f.add(PieceKind::Bed,0,.32,0);f.state.components.push_back({100,1,bed,1,1,FurnitureKind::Bed,{}});
    std::string error;auto empty=f.state;empty.structures.clear();empty.components.clear();
    ASSERT_TRUE(validateConstruction(empty,f.state,f.queries,error))<<error;
    ASSERT_TRUE(f.publish());PlayerPose recovery;
    ASSERT_TRUE(usableBed(f.state,100,f.queries,recovery,error))<<error;
    EXPECT_TRUE(f.queries.clearCapsule({recovery.x,recovery.y,recovery.z}));EXPECT_NEAR(recovery.y,.325,1e-5);
    const auto good=f.state;std::erase_if(f.state.structures[0].parts,[](const auto& p){return p.kind==PieceKind::Roof;});ASSERT_TRUE(f.publish());
    EXPECT_FALSE(usableBed(f.state,100,f.queries,recovery,error));EXPECT_EQ(error,"Add a roof with room to stand above the bed");
    f.state=good;f.add(PieceKind::Brick2x4,0,.32,1);f.add(PieceKind::Brick2x4,0,.32,-1);ASSERT_TRUE(f.publish());
    EXPECT_FALSE(usableBed(f.state,100,f.queries,recovery,error));
}

TEST(AdventureConstructionPolicy, LargerHouseAndRotatedBedUseTheSameShelterRule) {
    Fixture f;
    for(double x:{-1.,1.})for(double z:{-2.,0.,2.})f.add(PieceKind::Foundation,x,0,z);
    for(double x:{-1.,1.})f.add(PieceKind::Wall,x,.32,-2.84);
    for(double z:{-2.,0.,2.}) {f.add(PieceKind::Wall,-2.16,.32,z,1);f.add(PieceKind::Wall,2.16,.32,z,1);}
    for(double x:{-1.,1.})for(double z:{-2.,0.,2.})f.add(PieceKind::Roof,x,3.2,z);
    const auto bed=f.add(PieceKind::Bed,0,.32,1,1);f.state.components.push_back({100,1,bed,1,1,FurnitureKind::Bed,{}});
    auto empty=f.state;empty.structures.clear();empty.components.clear();std::string error;
    ASSERT_TRUE(validateConstruction(empty,f.state,f.queries,error))<<error;ASSERT_TRUE(f.publish());
    PlayerPose recovery;ASSERT_TRUE(usableBed(f.state,100,f.queries,recovery,error))<<error;
    EXPECT_TRUE(f.queries.clearCapsule({recovery.x,recovery.y,recovery.z}));
}
TEST(AdventureConstructionPolicy, TransfersAndCraftingRequireReachableUnblockedFurniture) {
    Fixture f;const auto chest=f.add(PieceKind::Chest,0,.32,0);
    f.state.player={0,.185,2.5,0};f.state.components.push_back({100,1,chest,1,1,FurnitureKind::Chest,{}});
    ASSERT_TRUE(f.publish());auto after=f.state;after.components[0].slots[0]={ItemKind::Wood,1};
    AdventureContent content;std::string error;
    EXPECT_TRUE(validateInteractions(f.state,after,content,f.queries,error))<<error;
    f.add(PieceKind::Wall,0,.32,1.5);ASSERT_TRUE(f.publish());after=f.state;after.components[0].slots[0]={ItemKind::Wood,1};
    EXPECT_FALSE(validateInteractions(f.state,after,content,f.queries,error));EXPECT_EQ(error,"The furniture is blocked");
    after=f.state;after.backpack[0]={ItemKind::FieldHammer,1};
    EXPECT_FALSE(validateInteractions(f.state,after,content,f.queries,error));EXPECT_EQ(error,"Use a nearby accessible workbench");
}
TEST(AdventureConstructionPolicy, GatherRequiresRealNodeReachAndUnblockedSight) {
    Fixture f;f.state.player={0,.185,2.5,0};AdventureContent content;
    content.resourceNodes.push_back({1,{0,.185,0,0},{ItemKind::Wood,8}});
    auto after=f.state;after.depletedNodes.push_back(1);std::string error;
    EXPECT_TRUE(validateInteractions(f.state,after,content,f.queries,error))<<error;
    f.add(PieceKind::Wall,0,.32,1.5);ASSERT_TRUE(f.publish());after=f.state;after.depletedNodes.push_back(1);
    EXPECT_FALSE(validateInteractions(f.state,after,content,f.queries,error));EXPECT_EQ(error,"The supplies are blocked");
    f.state.player.z=8;after.player=f.state.player;
    EXPECT_FALSE(validateInteractions(f.state,after,content,f.queries,error));EXPECT_EQ(error,"Move closer to gather supplies");
}
TEST(AdventureConstructionPolicy, TerrainPlacementIncludesAllRectangularCorners) {
    Fixture f;
    f.samples[32*64+33]=uint16_t(32768+1310); // A raised cell near the4x2 brick's long edge.
    const auto y=terrainPlacementHeight(PieceKind::Foundation,0,{1,0},f.surface);
    ASSERT_TRUE(y);f.add(PieceKind::Foundation,1,*y,0);
    auto empty=f.state;empty.structures.clear();std::string error;
    EXPECT_TRUE(validateConstruction(empty,f.state,f.queries,error))<<error;
    EXPECT_FALSE(terrainPlacementHeight(PieceKind::Foundation,0,{100,0},f.surface));
}
TEST(AdventureConstructionPolicy, OptionalStarterRoomIsAChargedLayoutWithRealDoorAndUsefulFurniture) {
    for(uint8_t yaw=0;yaw<4;++yaw) {
        Fixture f;std::string error;const auto layout=starterRoomLayout({},yaw,error);ASSERT_EQ(layout.size(),19u)<<error;
        uint64_t bed=0;
        for(const auto& placement:layout) {
            const auto id=f.next++;f.state.structures[0].parts.push_back({id,placement.kind,placement.position,placement.yawQuarterTurns,0});
            if(placement.kind==PieceKind::Bed)bed=id;
        }
        f.state.components.push_back({100,1,bed,1,1,FurnitureKind::Bed,{}});
        auto empty=f.state;empty.structures.clear();empty.components.clear();
        ASSERT_TRUE(validateConstruction(empty,f.state,f.queries,error))<<"yaw "<<int(yaw)<<": "<<error;
        ASSERT_TRUE(f.publish());PlayerPose recovery;
        ASSERT_TRUE(usableBed(f.state,100,f.queries,recovery,error))<<"yaw "<<int(yaw)<<": "<<error;
        EXPECT_TRUE(f.queries.clearCapsule({recovery.x,recovery.y,recovery.z}));
    }
}

TEST(AdventureConstructionPolicy, PlayerClimbsCatalogStairsAndTraversesTwoRoofedRoomLayouts) {
    for(const int depth:{4,6}) {
        SCOPED_TRACE(depth);
        Fixture f;
        const double edge=double(depth)*.5;
        // Four layers raise the room above the ground; the actual six-tread
        // catalog stair provides access. The second room is longer and has a
        // rotated bed, so the walking route is not merely a translated copy.
        for(const double x:{-1.,1.})for(double z=1-edge;z<edge;z+=2) {
            for(const double y:{0.,.32,.64,.96})f.add(PieceKind::Foundation,x,y,z);
            f.add(PieceKind::Roof,x,4.16,z);
        }
        for(const double x:{-1.,1.})f.add(PieceKind::Wall,x,1.28,.16-edge);
        for(double z=1-edge;z<edge;z+=2) {
            f.add(PieceKind::Wall,-2.16,1.28,z,1);
            f.add(PieceKind::Wall,2.16,1.28,z,1);
        }
        f.add(PieceKind::Doorway,-1,1.28,edge-.16);
        f.add(PieceKind::Wall,1,1.28,edge-.16);
        f.add(PieceKind::Foundation,-1,0,edge+1);
        f.add(PieceKind::Stair,-1,.32,edge+1);
        const auto bed=depth==4?f.add(PieceKind::Bed,.76,1.28,-.5)
            :f.add(PieceKind::Bed,0,1.28,-1.2,1);
        f.state.components.push_back({1000,1,bed,1,1,FurnitureKind::Bed,{}});
        auto empty=f.state;empty.structures.clear();empty.components.clear();
        std::string error;
        ASSERT_TRUE(validateConstruction(empty,f.state,f.queries,error))<<error;
        ASSERT_TRUE(f.publish());
        PlayerPose recovery;
        ASSERT_TRUE(usableBed(f.state,1000,f.queries,recovery,error))<<error;
        const auto ground=double(voxy::terrain::lego::supportHeight(f.surface,{-1.f,float(edge+3)},.3f));
        AdventurePlayer player;
        ASSERT_TRUE(player.initialize(f.queries,{-1,ground+.005,edge+3},-200));
        const auto walk=[&](glm::dvec2 target) {
            for(int tick=0;tick<240;++tick) {
                const glm::dvec2 delta=target-glm::dvec2(player.feet().x,player.feet().z);
                if(glm::length(delta)<.065)return true;
                player.advance(AdventurePlayer::fixedStep,{glm::normalize(delta),false});
            }
            return false;
        };
        ASSERT_TRUE(walk({-1,edge-.9}))<<player.feet().x<<","<<player.feet().z;
        EXPECT_NEAR(player.feet().y,1.285,.01);
        EXPECT_EQ(player.mode(),AdventurePlayer::Mode::Walking);
        ASSERT_TRUE(walk({-1.5,edge-.9}));
        ASSERT_TRUE(walk({-1.5,.65-edge}));
        EXPECT_NEAR(player.feet().y,1.285,.01);
        EXPECT_TRUE(f.queries.clearCapsule(player.feet()));
        ASSERT_TRUE(walk({-1.5,edge-.9}));
        ASSERT_TRUE(walk({-1,edge-.9}));
        ASSERT_TRUE(walk({-1,edge+3}));
        for(int tick=0;tick<30;++tick)player.advance(AdventurePlayer::fixedStep,{});
        EXPECT_NEAR(player.feet().y,ground+.005,.01);
        EXPECT_EQ(player.mode(),AdventurePlayer::Mode::Walking);
        EXPECT_GT(player.tick(),200u);
    }
}
