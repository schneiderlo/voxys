#include "game/adventure/adventure_player.hpp"
#include "game/adventure/adventure_input.hpp"
#include "game/adventure/world_definition.hpp"
#include <gtest/gtest.h>
#include <algorithm>
#include <array>
#include <limits>

using namespace voxy::game::adventure;
namespace {
using Id=voxy::game::construction::DurableId;
Id id(uint64_t counter) {voxy::game::construction::WorldNamespace world{};world.bytes[0]=17;return {world,counter};}
struct Scene {
    std::vector<uint16_t> samples=std::vector<uint16_t>(64*64,32768);
    voxy::terrain::lego::Surface surface{samples,64,64,8.f,1.f};
    AdventureSpatialQueries queries;
    Scene(){EXPECT_TRUE(queries.bindTerrain(surface));EXPECT_TRUE(queries.publish({},1));}
    double ground(double x=0,double z=0) const {return voxy::terrain::lego::supportHeight(surface,{float(x),float(z)},.3f);}
};
AdventureSpatialQueries::Solid box(uint64_t part,glm::dvec3 lo,glm::dvec3 hi) {return {id(1),id(part),lo,hi};}
}
TEST(AdventureWorld, InstalledProfileUsesFullUnmodifiedTerrainAndSeparateIdentity) {
    const auto& world=installedWorld();EXPECT_EQ(world.width,8192u);EXPECT_EQ(world.height,8192u);
    EXPECT_EQ(world.terrainRecipe,"full-main-unmodified-lego-r01");EXPECT_NE(world.profile.find("adventure"),std::string_view::npos);
    EXPECT_EQ(world.samplesSha256.size(),64u);EXPECT_EQ(world.ldhSha256.size(),64u);
    EXPECT_GT(glm::length(world.homeSuggestion-world.town),world.townProtectedRadius);
    EXPECT_FALSE(protectedConstruction({-87,-147,-897},{-83,-143,-893}));
    EXPECT_TRUE(protectedConstruction({-65,-147,-897},{-61,-143,-893}));
}
TEST(AdventureSpatialQueries, PublicationIsAtomicBoundedAndRejectsStaleRevision) {
    Scene scene;const auto wall=box(2,{-1,1,-1},{1,3,1});ASSERT_TRUE(scene.queries.publish(std::span(&wall,1),2));
    auto bad=wall;bad.maximum.x=bad.minimum.x;EXPECT_FALSE(scene.queries.publish(std::span(&bad,1),3));
    EXPECT_FALSE(scene.queries.publish({},2));EXPECT_EQ(scene.queries.revision(),2u);EXPECT_EQ(scene.queries.solidCount(),1u);
    std::vector<AdventureSpatialQueries::Solid> overflow(AdventureSpatialQueries::maximumSolids+1,wall);
    EXPECT_FALSE(scene.queries.publish(overflow,3));EXPECT_EQ(scene.queries.solidCount(),1u);
    EXPECT_FALSE(scene.queries.sweepSphere({0,2,-3},{0,2,3},.2,1).complete);
}
TEST(AdventureSpatialQueries, BridgeHasDistinctGroundAndDeckSupportAndRoofBlocksHead) {
    Scene scene;const std::array solids{box(2,{-4,3,-4},{4,3.32,4}),box(3,{-4,6,-4},{4,6.32,4})};
    ASSERT_TRUE(scene.queries.publish(solids,2));
    EXPECT_NEAR(scene.queries.supportHeight({0,0},.3,1),scene.ground(),1e-5);
    EXPECT_NEAR(scene.queries.supportHeight({0,0},.3,4),3.32,1e-9);
    EXPECT_TRUE(scene.queries.clearCapsule({0,scene.ground()+.005,0}));
    EXPECT_TRUE(scene.queries.clearCapsule({0,3.325,0}));
    EXPECT_FALSE(scene.queries.clearCapsule({0,4.8,0}));
    const auto up=scene.queries.sweepCapsule({0,3.325,0},{0,5,0});
    ASSERT_TRUE(up.complete);ASSERT_TRUE(up.hit);EXPECT_NEAR(up.distance,.975,1e-5);EXPECT_LT(up.normal.y,0);
}
TEST(AdventureSpatialQueries, DoorwayPickingWalkingAndCameraReadTheSameSolids) {
    Scene scene;const std::array solids{box(2,{-2,0,-.2},{-.68,3,.2}),box(2,{.68,0,-.2},{2,3,.2}),box(2,{-.68,2.24,-.2},{.68,3,.2})};
    ASSERT_TRUE(scene.queries.publish(solids,2));
    EXPECT_TRUE(scene.queries.clearCapsule({0,.2,0}));EXPECT_FALSE(scene.queries.clearCapsule({1,.2,0}));
    auto clear=scene.queries.sweepCapsule({0,.3,-2},{0,.3,2});EXPECT_TRUE(clear.complete);EXPECT_FALSE(clear.hit);
    auto blocked=scene.queries.sweepSphere({1,1,-2},{1,1,2},.2,2);ASSERT_TRUE(blocked.complete);ASSERT_TRUE(blocked.hit);EXPECT_NEAR(blocked.distance,1.6,1e-7);
    auto ray=scene.queries.raycast({1,1,-2},{0,0,1},4);ASSERT_TRUE(ray.complete);ASSERT_TRUE(ray.hit);EXPECT_FALSE(ray.terrain);EXPECT_EQ(ray.part,id(2));EXPECT_NEAR(ray.distance,1.8,1e-7);
}
TEST(AdventureSpatialQueries, CrossSectorReferencesDoNotLoseSolidOrDuplicateQuery) {
    Scene scene;const auto solid=box(2,{255,0,255},{257,3,257});ASSERT_TRUE(scene.queries.publish(std::span(&solid,1),2));
    const auto hit=scene.queries.sweepSphere({253,2,256},{259,2,256},.3,2);
    ASSERT_TRUE(hit.complete);ASSERT_TRUE(hit.hit);EXPECT_NEAR(hit.distance,1.7,1e-7);
}
TEST(AdventurePlayer, WalkJumpAndPauseUseAcceptedTicksWithoutFlyingThroughWall) {
    Scene scene;const auto wall=box(2,{2,-1,-8},{2.2,5,8});ASSERT_TRUE(scene.queries.publish(std::span(&wall,1),2));
    AdventurePlayer player;ASSERT_TRUE(player.initialize(scene.queries,{0,scene.ground()+.005,0},-10));
    for(int i=0;i<120;++i)player.advance(1./60,{{1,0},false});
    EXPECT_GT(player.feet().x,1);EXPECT_LT(player.feet().x,1.71);
    const double y=player.feet().y;player.advance(1./60,{{},true});EXPECT_EQ(player.mode(),AdventurePlayer::Mode::Airborne);
    for(int i=0;i<15;++i)player.advance(1./60,{});
    EXPECT_GT(player.feet().y,y+.5);
    for(int i=0;i<90;++i)player.advance(1./60,{});
    EXPECT_EQ(player.mode(),AdventurePlayer::Mode::Walking);
    const auto state=player.state();player.advance(.001,{{1,0},true});player.discardPendingInput();player.advance(1./60,{});
    EXPECT_EQ(player.mode(),AdventurePlayer::Mode::Walking);EXPECT_EQ(player.tick(),state.tick+1);
}
TEST(AdventurePlayer, WalksUpSixPhysicalStairTreadsOntoFloorAndReturnsUnderBridge) {
    Scene scene;std::vector<AdventureSpatialQueries::Solid> solids;
    const double base=scene.ground()+.005;
    for(int i=0;i<6;++i)solids.push_back(box(uint64_t(i+2),{1.+i*.5,base,-1},{1.5+i*.5,base+.16*(i+1),1}));
    solids.push_back(box(8,{4,base+.64,-1},{8,base+.96,1}));ASSERT_TRUE(scene.queries.publish(solids,2));
    AdventurePlayer player;ASSERT_TRUE(player.initialize(scene.queries,{0,base,0},-10));
    for(int i=0;i<95;++i)player.advance(1./60,{{1,0},false});
    EXPECT_GT(player.feet().x,5);EXPECT_NEAR(player.feet().y,base+.965,.015);EXPECT_EQ(player.mode(),AdventurePlayer::Mode::Walking);
}
TEST(AdventurePlayer, InvalidRestorePreservesAcceptedStateAndWaterUsesWorldHeight) {
    Scene scene;AdventurePlayer player;ASSERT_TRUE(player.initialize(scene.queries,{0,scene.ground()+.005,0},-200));
    const auto before=player.state();auto invalid=before;invalid.feet.y=std::numeric_limits<double>::quiet_NaN();EXPECT_FALSE(player.restore(invalid));EXPECT_EQ(player.feet(),before.feet);
    invalid=before;invalid.mode=AdventurePlayer::Mode::Helm;EXPECT_FALSE(player.restore(invalid));
    invalid=before;invalid.mode=AdventurePlayer::Mode::Airborne;invalid.feet.y=3;ASSERT_TRUE(player.restore(invalid));
    for(int i=0;i<80;++i)player.advance(1./60,{});
    EXPECT_EQ(player.mode(),AdventurePlayer::Mode::Walking);
}
TEST(AdventurePlayer, IdleConnectedControllerDoesNotStealMouseBuildingAim) {
    EXPECT_FALSE(adventurePadOwnsAim(false,false,true,false));
    EXPECT_TRUE(adventurePadOwnsAim(false,false,true,true));
    EXPECT_TRUE(adventurePadOwnsAim(true,false,true,false));
    EXPECT_FALSE(adventurePadOwnsAim(true,true,true,false));
    EXPECT_FALSE(adventurePadOwnsAim(true,true,true,true));
    EXPECT_FALSE(adventurePadOwnsAim(true,false,false,false));
}
TEST(AdventureInput, ControllerMatchesHudAndPlacementCannotAlsoJump) {
    using A=voxy::game::expedition::CoveAction;
    using Context=voxy::game::expedition::CoveInputContext;
    using Button=voxy::PadButton;
    const auto preferences=adventureInputDefaults();
    std::string error;
    ASSERT_TRUE(voxy::game::expedition::validateCoveInputPreferences(preferences,error))<<error;
    voxy::game::expedition::CoveInputRouter movement,building;
    voxy::game::expedition::CoveInputSample sample;
    sample.padConnected=true;sample.padArmed=true;
    const auto tick=[&](bool build) {
        movement.tick(adventureMovementSample(sample,build),Context::World,preferences);
        building.tick(sample,build?Context::Workshop:Context::Menu,preferences);
    };
    const auto release=[&](bool build) {
        sample.padDown.fill(false);sample.padPressed.fill(false);
        sample.keys.fill(false);sample.pressed.fill(false);sample.physicalKeys.fill(false);
        tick(build);
    };
    const auto press=[&](Button button) {
        sample.padDown[static_cast<size_t>(button)]=true;
        sample.padPressed[static_cast<size_t>(button)]=true;
    };
    release(false);press(Button::Confirm);tick(false);
    EXPECT_TRUE(movement.pressed(A::Jump));EXPECT_FALSE(movement.pressed(A::Interact));
    release(false);press(Button::Tool);tick(false);
    EXPECT_TRUE(movement.pressed(A::Interact));EXPECT_FALSE(movement.pressed(A::Jump));
    EXPECT_FALSE(movement.pressed(A::Hook));
    release(false);press(Button::Back);tick(false);
    EXPECT_FALSE(movement.pressed(A::Jump));
    release(true);press(Button::Confirm);tick(true);
    EXPECT_TRUE(building.pressed(A::Keep));EXPECT_FALSE(movement.pressed(A::Jump));
    EXPECT_TRUE(sample.padPressed[static_cast<size_t>(Button::Confirm)]);
    release(true);press(Button::Back);tick(true);
    EXPECT_TRUE(building.pressed(A::Cancel));EXPECT_FALSE(movement.pressed(A::Jump));
    release(true);sample.keys[32]=true;sample.pressed[32]=true;sample.physicalKeys[32]=true;tick(true);
    EXPECT_TRUE(movement.pressed(A::Jump));EXPECT_FALSE(building.pressed(A::Keep));
}
