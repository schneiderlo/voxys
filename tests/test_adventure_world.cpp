#include "game/adventure/adventure_player.hpp"
#include "game/adventure/adventure_input.hpp"
#include "game/adventure/world_definition.hpp"
#include <gtest/gtest.h>
#include <chrono>
#include <algorithm>
#include <array>
#include <cmath>
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
TEST(AdventureSpatialQueries, PartCounterMembershipMatchesAcceptedSolidsThroughReplacementCopyAndMove) {
    Scene scene;
    const std::array<uint64_t,6> counters{0,1,2,3,55,UINT64_MAX};
    const auto check=[&](const AdventureSpatialQueries& queries) {
        for(const auto structure:counters)for(const auto part:counters) {
            const bool expected=std::any_of(queries.solids().begin(),queries.solids().end(),[&](const auto& solid){
                return solid.structure.counter==structure&&solid.part.counter==part;
            });
            EXPECT_EQ(queries.containsPartCounters(structure,part),expected)<<structure<<":"<<part;
        }
    };
    check(scene.queries);
    std::vector<AdventureSpatialQueries::Solid> solids{box(2,{-1,0,-1},{1,1,1}),box(UINT64_MAX,{2,0,2},{3,1,3}),box(55,{4,0,4},{5,1,5})};
    solids[1].structure.counter=3;solids[2].structure.counter=2;
    solids.push_back(solids[0]); // Several boxes may belong to the same part.
    solids.back().structure.world.bytes[1]=23;solids.back().part.world=solids.back().structure.world;
    for(size_t order=0;order<solids.size();++order) {
        ASSERT_TRUE(scene.queries.publish(solids,scene.queries.revision()+1));check(scene.queries);
        for(size_t i=0;i<solids.size();++i) {
            EXPECT_EQ(scene.queries.solids()[i].structure,solids[i].structure);
            EXPECT_EQ(scene.queries.solids()[i].part,solids[i].part);
            EXPECT_EQ(scene.queries.solids()[i].minimum,solids[i].minimum);
            EXPECT_EQ(scene.queries.solids()[i].maximum,solids[i].maximum);
        }
        std::rotate(solids.begin(),solids.begin()+1,solids.end());
    }
    auto invalid=box(3,{0,0,0},{1,1,1});invalid.maximum.x=invalid.minimum.x;
    const std::array replacement{box(55,{0,0,0},{1,1,1}),invalid};
    const auto revision=scene.queries.revision();
    EXPECT_FALSE(scene.queries.publish(replacement,revision+1));
    EXPECT_FALSE(scene.queries.publish(std::span(replacement).first(1),revision));
    EXPECT_EQ(scene.queries.revision(),revision);check(scene.queries);
    EXPECT_TRUE(scene.queries.containsPartCounters(1,2));EXPECT_FALSE(scene.queries.containsPartCounters(1,55));
    auto copy=scene.queries;check(copy);auto moved=std::move(copy);check(moved);
    copy=scene.queries;check(copy);copy=std::move(moved);check(copy);
    ASSERT_TRUE(copy.publish({},revision+1));check(copy);
    EXPECT_TRUE(scene.queries.containsPartCounters(1,2));
    ASSERT_TRUE(scene.queries.publish(std::span(replacement).first(1),revision+1));check(scene.queries);
    EXPECT_FALSE(scene.queries.containsPartCounters(1,2));EXPECT_TRUE(scene.queries.containsPartCounters(1,55));
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
TEST(AdventureSpatialQueries, FullLengthWideDiagonalSweepsRemainCompleteWithDenseLocalBuckets) {
    Scene scene;
    // The widest accepted nearly-128-unit diagonal visits exactly 8x8 of the
    // smaller buckets. Keep it valid under the existing 64-bucket query limit.
    for(double sign:{-1.,1.}) {
        const glm::dvec3 a(sign*19.9,20,sign*19.9),b(sign*110.4,20,sign*110.4);
        const auto minimum=glm::min(a,b)+glm::dvec3(80,-5,80);
        const auto wall=box(2,minimum,minimum+glm::dvec3(1,10,1));
        ASSERT_TRUE(scene.queries.publish(std::span(&wall,1),scene.queries.revision()+1));
        const auto sweep=scene.queries.sweepSphere(a,b,4,scene.queries.revision());
        ASSERT_TRUE(sweep.complete);EXPECT_TRUE(sweep.hit);EXPECT_FALSE(sweep.startOverlapped);
        EXPECT_GT(sweep.distance,0);EXPECT_LT(sweep.distance,glm::length(b-a));
    }
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
TEST(AdventurePlayer, BrickImpulsesMoveFigureStopAtWallsAndClearOnRestore) {
    Scene scene;const auto wall=box(2,{1,-1,-8},{1.2,5,8});ASSERT_TRUE(scene.queries.publish(std::span(&wall,1),2));
    AdventurePlayer player;ASSERT_TRUE(player.initialize(scene.queries,{0,scene.ground()+.005,0},-10));
    const auto start=player.state();
    player.addContactImpulse({8,0,0});
    for(int i=0;i<60;++i)player.advance(1./60,{});
    EXPECT_GT(player.feet().x,.5);EXPECT_LT(player.feet().x,.71);
    EXPECT_TRUE(scene.queries.clearCapsule(player.feet(),player.bodyRadius(),player.bodyHeight()));
    player.addContactImpulse({8,0,0});ASSERT_TRUE(player.restore(start));
    player.advance(1./60,{});EXPECT_NEAR(player.feet().x,start.feet.x,1e-8);
    player.addContactImpulse({std::numeric_limits<double>::infinity(),0,0});
    player.advance(1./60,{});EXPECT_NEAR(player.feet().x,start.feet.x,1e-8);
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

namespace {
struct SwimmingScene : Scene {
    double water=ground()+12;
    AdventureSpatialQueries::Solid dock=box(90,{-12,ground(),-4},{-5,water+1,4});
    bool start(AdventurePlayer& player,double scale=1) {
        if(!queries.publish(std::span(&dock,1),2)
            ||!player.initialize(queries,{-8,water+1.005,0},water,0,scale,
                scale==1?AdventurePlayer::radius:AdventurePlayer::creativeRadius))return false;
        auto pose=player.state();pose.feet={0,player.swimSurfaceHeight(),0};pose.mode=AdventurePlayer::Mode::Swimming;
        return player.restore(pose);
    }
};
}

TEST(AdventureSwimming, DiveRiseSurfaceAndUnderwaterRestoreKeepAcceptedDepth) {
    for(double scale:{1.,AdventurePlayer::creativeScale}) {
        SwimmingScene scene;AdventurePlayer player;ASSERT_TRUE(scene.start(player,scale));
        const auto surface=player.swimSurfaceHeight();
        for(int i=0;i<60;++i)player.advance(1./60,{{},false,1,-1});
        EXPECT_LT(player.feet().y,surface-2*std::sqrt(scale));
        EXPECT_EQ(player.mode(),AdventurePlayer::Mode::Swimming);
        for(int i=0;i<30;++i)player.advance(1./60,{});
        const auto stopped=player.state();
        for(int i=0;i<60;++i)player.advance(1./60,{});
        EXPECT_NEAR(player.feet().y,stopped.feet.y,1e-9);EXPECT_NEAR(player.worldVelocity().y,0,1e-9);
        // Saves store position; reconstructing the airborne mode must not
        // teleport a submerged character back up to the surface.
        auto loaded=stopped;loaded.mode=AdventurePlayer::Mode::Airborne;
        ASSERT_TRUE(player.restore(loaded));player.advance(1./60,{});
        EXPECT_EQ(player.mode(),AdventurePlayer::Mode::Swimming);EXPECT_NEAR(player.feet().y,stopped.feet.y,1e-9);
        AdventurePlayer reloaded;
        ASSERT_TRUE(reloaded.initialize(scene.queries,stopped.feet,scene.water,0,scale,player.bodyRadius()/scale));
        EXPECT_EQ(reloaded.mode(),AdventurePlayer::Mode::Swimming);EXPECT_EQ(reloaded.feet(),stopped.feet);
        for(int i=0;i<150;++i)player.advance(1./60,{{},false,1,1});
        EXPECT_NEAR(player.feet().y,surface,1e-8);
        EXPECT_EQ(player.mode(),AdventurePlayer::Mode::Swimming);
        for(int i=0;i<30;++i)player.advance(1./60,{{1,0},false,1,1});
        EXPECT_NEAR(player.feet().y,surface,1e-8);
        auto invalid=player.state();invalid.feet.y=surface+1;
        EXPECT_FALSE(player.restore(invalid));EXPECT_NEAR(player.feet().y,surface,1e-8);
    }
}

TEST(AdventureSwimming, SurfaceTravelIsLevelAndThreeDimensionalSpeedIsBounded) {
    SwimmingScene scene;AdventurePlayer player;ASSERT_TRUE(scene.start(player));
    const auto start=player.state();
    for(int i=0;i<90;++i)player.advance(1./60,{{0,1},false,1});
    EXPECT_NEAR(player.feet().y,start.feet.y,1e-9);EXPECT_GT(player.feet().z,3.2);
    const auto normal=player.feet();ASSERT_TRUE(player.restore(start));
    for(int i=0;i<90;++i)player.advance(1./60,{{0,1},false,1.75});
    EXPECT_EQ(player.feet(),normal); // Running never accelerates swimming.
    ASSERT_TRUE(player.restore(start));
    for(int i=0;i<60;++i) {
        player.advance(1./60,{{1,1},false,1,-1});
        EXPECT_LE(glm::length(player.worldVelocity()),AdventurePlayer::swimSpeed+1e-8);
    }
    EXPECT_GT(player.feet().x,1.2);EXPECT_LT(player.feet().y,start.feet.y-1.2);
    const auto diagonal=player.state();ASSERT_TRUE(player.restore(start));
    for(int i=0;i<30;++i)player.advance(1./30,{{1,1},false,1,-1});
    EXPECT_EQ(player.feet(),diagonal.feet);EXPECT_EQ(player.tick(),diagonal.tick);
    const auto before=player.state();player.advance(.2,{{1,0},false,1,NAN});
    EXPECT_EQ(player.feet(),before.feet);EXPECT_EQ(player.tick(),before.tick);
}

TEST(AdventureSwimming, BottomWallsAndSubmergedCeilingsBlockEveryAxis) {
    SwimmingScene scene;AdventurePlayer player;ASSERT_TRUE(scene.start(player));
    const auto surface=player.swimSurfaceHeight();
    const auto wall=box(91,{3,scene.ground(),-5},{3.08,scene.water+3,5});
    ASSERT_TRUE(scene.queries.publish(std::array{scene.dock,wall},3));
    for(int i=0;i<180;++i)player.advance(1./60,{{1,0},false,1});
    EXPECT_LE(player.feet().x+player.bodyRadius(),3.);
    EXPECT_TRUE(scene.queries.clearCapsule(player.feet(),player.bodyRadius(),player.bodyHeight()));
    auto underwater=player.state();underwater.feet={0,surface-4,0};underwater.velocity={};
    ASSERT_TRUE(player.restore(underwater));
    const auto roof=box(92,{-4,surface-1,-5},{2,surface-.8,5});
    ASSERT_TRUE(scene.queries.publish(std::array{scene.dock,wall,roof},4));
    for(int i=0;i<180;++i)player.advance(1./60,{{},false,1,1});
    EXPECT_LE(player.feet().y+player.bodyHeight(),roof.minimum.y);
    EXPECT_TRUE(scene.queries.clearCapsule(player.feet(),player.bodyRadius(),player.bodyHeight()));
    for(int i=0;i<360;++i)player.advance(1./60,{{},false,1,-1});
    EXPECT_NEAR(player.feet().y,scene.ground()+.005,.02);
    EXPECT_EQ(player.mode(),AdventurePlayer::Mode::Swimming);
    EXPECT_TRUE(scene.queries.clearCapsule(player.feet(),player.bodyRadius(),player.bodyHeight()));
}

TEST(AdventureSwimming, WalksOutOntoShoreAndBackIntoWaterWithoutOscillating) {
    for(double scale:{1.,AdventurePlayer::creativeScale}) {
        SCOPED_TRACE(scale);
        SwimmingScene scene;AdventurePlayer player;ASSERT_TRUE(scene.start(player,scale));
        const double surface=player.swimSurfaceHeight();
        const auto bank=box(91,{2,scene.ground(),-5},{12,surface+.25*scale,5});
        ASSERT_TRUE(scene.queries.publish(std::array{scene.dock,bank},3));
        for(int i=0;i<70;++i)player.advance(1./60,{{1,0},false});
        ASSERT_EQ(player.mode(),AdventurePlayer::Mode::Walking)<<player.feet().x<<","<<player.feet().y-surface;
        EXPECT_NEAR(player.feet().y,bank.maximum.y+.005,1e-7);
        for(int i=0;i<15;++i){player.advance(1./60,{});EXPECT_EQ(player.mode(),AdventurePlayer::Mode::Walking);}
        for(int i=0;i<100;++i)player.advance(1./60,{{-1,0},false});
        EXPECT_EQ(player.mode(),AdventurePlayer::Mode::Swimming);
        EXPECT_NEAR(player.feet().y,surface,1e-7);
        EXPECT_TRUE(scene.queries.clearCapsule(player.feet(),player.bodyRadius(),player.bodyHeight()));
    }
}
TEST(AdventurePlayer, CreativeFigureUsesStudScaleForCollisionMovementAndHeadroom) {
    Scene scene;
    constexpr double scale=AdventurePlayer::creativeScale;
    const double ground=double(voxy::terrain::lego::supportHeight(scene.surface,{0,0},float(AdventurePlayer::creativeRadius*scale)))+.005;
    AdventurePlayer player;
    ASSERT_TRUE(player.initialize(scene.queries,{0,ground,0},-200,0,scale,AdventurePlayer::creativeRadius));
    EXPECT_NEAR(player.bodyHeight(),4.76,1e-12);
    EXPECT_NEAR(player.bodyRadius(),1.12,1e-12);
    const auto wall=box(2,{3,ground-.01,-8},{3.32,ground+8,8});
    ASSERT_TRUE(scene.queries.publish(std::span(&wall,1),2));
    for(int i=0;i<90;++i)player.advance(1./60,{{1,0},false});
    EXPECT_GT(player.feet().x,1.8);
    EXPECT_LE(player.feet().x+player.bodyRadius(),3.001);
    EXPECT_TRUE(scene.queries.clearCapsule(player.feet(),player.bodyRadius(),player.bodyHeight()));
    auto bad=player.state();bad.feet.x=2.6;
    EXPECT_FALSE(player.restore(bad));
    const auto roof=box(3,{-3,ground+2.56,-3},{3,ground+2.88,3});
    ASSERT_TRUE(scene.queries.publish(std::span(&roof,1),3));
    AdventurePlayer legacy,creative;
    ASSERT_TRUE(legacy.initialize(scene.queries,{0,ground,0},-200));
    EXPECT_FALSE(creative.initialize(scene.queries,{0,ground,0},-200,0,scale,AdventurePlayer::creativeRadius));
}
TEST(AdventurePlayer, CreativeScaleKeepsJumpLandingAndInvalidProfileAtomic) {
    Scene scene;
    const double scale=AdventurePlayer::creativeScale;
    const double ground=double(voxy::terrain::lego::supportHeight(scene.surface,{0,0},float(AdventurePlayer::creativeRadius*scale)))+.005;
    AdventurePlayer player;ASSERT_TRUE(player.initialize(scene.queries,{0,ground,0},-200,0,scale,AdventurePlayer::creativeRadius));
    const auto before=player.state();
    EXPECT_FALSE(player.initialize(scene.queries,{0,ground,0},-200,0,4));
    EXPECT_EQ(player.feet(),before.feet);EXPECT_DOUBLE_EQ(player.bodyScale(),scale);
    player.advance(1./60,{{},true});double highest=player.feet().y;
    for(int i=0;i<120;++i){player.advance(1./60,{});highest=std::max(highest,player.feet().y);}
    EXPECT_GT(highest-ground,2.5);EXPECT_LT(highest-ground,3);
    EXPECT_EQ(player.mode(),AdventurePlayer::Mode::Walking);EXPECT_NEAR(player.feet().y,ground,.01);
    for(int i=0;i<60;++i)player.advance(1./60,{{0,1},false});
    EXPECT_GT(player.feet().z,5.5);EXPECT_LT(player.feet().z,6.5);
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

// Vehicle checks use the actual shared terrain and swept building queries.
#include "game/adventure/builder_motorbike.hpp"
TEST(BuilderMotorbike, FixedStepAccelerationBrakesSteeringAndSafeDismount) {
    Scene scene;BuilderMotorbike a,b;
    ASSERT_TRUE(a.place(scene.queries,{0,scene.ground()+.01,0},0,-200));b=a;
    for(int i=0;i<60;++i)a.advance(scene.queries,1./60.,{1,0,false},-200);
    for(int i=0;i<30;++i)b.advance(scene.queries,1./30.,{1,0,false},-200);
    EXPECT_NEAR(a.state().feet.z,b.state().feet.z,1e-9);
    EXPECT_GT(a.state().speed,8);EXPECT_LT(a.state().feet.z,-3);
    EXPECT_FALSE(a.dismount(scene.queries,-200));
    for(int i=0;i<40;++i)a.advance(scene.queries,1./60.,{0,.6,true},-200);
    EXPECT_NEAR(a.state().speed,0,1e-9);EXPECT_GT(a.state().yaw,0);
    EXPECT_GT(a.state().lean,0); // Positive yaw turns toward -X; bank into that turn.
    ASSERT_TRUE(a.dismount(scene.queries,-200));
    const auto stopped=a.state();a.advance(scene.queries,1.,{NAN,0,false},-200);
    EXPECT_EQ(a.state().feet,stopped.feet);
}
TEST(BuilderMotorbike, CannotTunnelThroughThinWallOrMountUnderRoof) {
    Scene scene;BuilderMotorbike bike;
    const auto ground=scene.ground();
    std::array solids{box(90,{-20,ground,-12.05},{20,ground+8,-12.0})};
    ASSERT_TRUE(scene.queries.publish(solids,2));
    ASSERT_TRUE(bike.place(scene.queries,{0,ground+.01,0},0,-200));
    for(int i=0;i<240;++i)bike.advance(scene.queries,1./60.,{1,0,false},-200);
    EXPECT_GT(bike.state().feet.z,-10.0);EXPECT_NEAR(bike.state().speed,0,1e-9);
    EXPECT_TRUE(scene.queries.clearCapsule(bike.state().feet,1.12,5.65));
    solids[0]=box(91,{-4,ground+3,-4},{4,ground+3.3,4});
    ASSERT_TRUE(scene.queries.publish(solids,3));
    EXPECT_FALSE(bike.place(scene.queries,{0,ground+.01,0},0,-200));
    EXPECT_FALSE(bike.place(scene.queries,{20,ground+.01,20},0,ground+1));
}
TEST(BuilderMotorbike, ParkPauseDoesNotCarryBufferedThrottle) {
    Scene scene;BuilderMotorbike bike;
    ASSERT_TRUE(bike.place(scene.queries,{0,scene.ground()+.01,0},0,-200));
    bike.advance(scene.queries,1./120.,{1,0,false},-200);bike.pause();
    const auto p=bike.state().feet;
    bike.advance(scene.queries,1./120.,{},-200);
    EXPECT_EQ(bike.state().feet,p);EXPECT_EQ(bike.state().speed,0);
}

namespace {
void ramp(Scene& scene) {
    for(size_t z=0;z<64;++z)for(size_t x=0;x<64;++x) {
        const double height=-(double(z)-31.5)*.24;
        scene.samples[z*64+x]=uint16_t((height+8.)/16.*65535.);
    }
}
void checkTires(const Scene& scene,const BuilderMotorbike::State& state) {
    constexpr double radius=BuilderMotorbike::wheelRadius,half=BuilderMotorbike::halfWheelbase;
    for(size_t i=0;i<2;++i) {
        const double direction=i?1.:-1.;
        const auto p=state.feet+glm::dvec3(-std::sin(state.yaw),0,-std::cos(state.yaw))
            *(direction*half*std::cos(state.pitch));
        const double renderedAxle=state.feet.y+state.groundOffset+radius+direction*half*std::sin(state.pitch);
        const double support=scene.queries.supportHeight({p.x,p.z},radius,state.feet.y+.85)+radius+.008;
        EXPECT_NEAR(renderedAxle,state.axleHeight[i],1e-6); // Pitch solve tolerance at mounting.
        EXPECT_GE(renderedAxle-support,-.004);
        if(state.wheelGrounded[i]) {EXPECT_NEAR(renderedAxle,support,.004);}
    }
}
}
TEST(BuilderMotorbike, TiresFollowStuddedSlopeAndBrakeHolds) {
    Scene scene;ramp(scene);BuilderMotorbike bike;
    ASSERT_TRUE(bike.place(scene.queries,{0,scene.ground()+.01,0},0,-200));
    checkTires(scene,bike.state());
    for(int i=0;i<120;++i) {
        bike.advance(scene.queries,1./60.,{1,0,false},-200);
        SCOPED_TRACE(i);checkTires(scene,bike.state());
    }
    EXPECT_LT(bike.state().feet.z,-5);EXPECT_GT(bike.state().pitch,.1);
    for(int i=0;i<120;++i)bike.advance(scene.queries,1./60.,{0,0,true},-200);
    auto stopped=bike.state().feet;
    for(int i=0;i<120;++i)bike.advance(scene.queries,1./60.,{0,0,true},-200);
    EXPECT_NEAR(bike.state().speed,0,1e-9);EXPECT_EQ(bike.state().feet,stopped);
}
TEST(BuilderMotorbike, SlopeGravityChangesSpeedAndFixedStepIsStableOnBumps) {
    Scene scene;ramp(scene);BuilderMotorbike up,down,fastFrames;
    ASSERT_TRUE(up.place(scene.queries,{0,scene.ground()+.01,0},0,-200));
    ASSERT_TRUE(down.place(scene.queries,{0,scene.ground()+.01,0},std::acos(-1.),-200));
    fastFrames=up;
    for(int i=0;i<40;++i)up.advance(scene.queries,1./30.,{1,0,false},-200);
    for(int i=0;i<160;++i)fastFrames.advance(scene.queries,1./120.,{1,0,false},-200);
    for(int i=0;i<80;++i)down.advance(scene.queries,1./60.,{1,0,false},-200);
    EXPECT_NEAR(up.state().feet.z,fastFrames.state().feet.z,1e-9);
    EXPECT_NEAR(up.state().pitch,fastFrames.state().pitch,1e-9);
    EXPECT_GT(up.state().speed,3);EXPECT_GT(down.state().speed,up.state().speed+2);
}
TEST(BuilderMotorbike, RollsOffLedgeAndLandsWithoutFreezingOrSnapping) {
    Scene scene;BuilderMotorbike bike;
    const std::array solids{box(120,{-10,0,0},{10,3,15})};
    ASSERT_TRUE(scene.queries.publish(solids,2));
    ASSERT_TRUE(bike.place(scene.queries,{0,3.01,7},0,-200));
    bool airborne=false,landed=false;double largestDrop=0;
    for(int i=0;i<190;++i) {
        const double previousBody=bike.state().feet.y+bike.state().groundOffset;
        bike.advance(scene.queries,1./60.,{1,0,false},-200);
        const auto& state=bike.state();
        largestDrop=std::max(largestDrop,previousBody-state.feet.y-state.groundOffset);
        airborne|=!state.wheelGrounded[0]||!state.wheelGrounded[1];
        if(airborne&&state.feet.z<-4&&state.wheelGrounded[0]&&state.wheelGrounded[1])landed=true;
        SCOPED_TRACE(i);checkTires(scene,state);
    }
    EXPECT_TRUE(airborne);EXPECT_TRUE(landed);EXPECT_LT(bike.state().feet.z,-6);
    EXPECT_LT(largestDrop,.45); // Gravity over time, never the full three-stud drop in one tick.
}
TEST(BuilderMotorbike, CoastsDownhillButBrakeHoldsAndCenterProbeDoesNotLiftTires) {
    Scene slope;ramp(slope);BuilderMotorbike coast,held;
    ASSERT_TRUE(coast.place(slope.queries,{0,slope.ground()+.01,0},std::acos(-1.),-200));held=coast;
    const auto start=held.state().feet;
    for(int i=0;i<90;++i) {
        coast.advance(slope.queries,1./60.,{},-200);
        held.advance(slope.queries,1./60.,{0,0,true},-200);
    }
    EXPECT_GT(coast.state().feet.z,start.z+.4);EXPECT_GT(coast.state().speed,.5);
    EXPECT_EQ(held.state().feet,start);
    Scene flat;BuilderMotorbike bike;
    const std::array solids{box(130,{-4,0,-.1},{4,.48,.1})};
    ASSERT_TRUE(flat.queries.publish(solids,2));
    ASSERT_TRUE(bike.place(flat.queries,{0,.49,0},0,-200));
    EXPECT_GT(bike.state().feet.y,.48); // Broad collision guard clears the middle obstacle.
    EXPECT_LT(bike.state().feet.y+bike.state().groundOffset,.20); // Actual tires stay on ground.
    checkTires(flat,bike.state());
    EXPECT_FALSE(bike.place(flat.queries,{31,0,0},0,-200)); // No fabricated support outside the map.
}
TEST(BuilderMotorbike, TurningAcrossElevationKeepsMomentumAndSteering) {
    int unloadedFrames=0;
    for(double yaw:{0.,.8,1.6,2.4})for(double steer:{-1.,1.}) {
        Scene scene;ramp(scene);BuilderMotorbike bike;
        ASSERT_TRUE(bike.place(scene.queries,{0,scene.ground()+.01,0},yaw,-200));
        SCOPED_TRACE(yaw);
        SCOPED_TRACE(steer);
        int stops=0,lostSteering=0;double worstDrop=0,largestTurnChange=0;
        for(int i=0;i<140;++i) {
            const auto before=bike.state();
            bike.advance(scene.queries,1./60.,{1,i>=40?steer:0.,false},-200);
            const auto& after=bike.state();
            if(before.speed>3&&after.speed<.01)++stops;
            if(i>45&&after.speed>3&&std::abs(after.yaw-before.yaw)<1e-7)++lostSteering;
            worstDrop=std::max(worstDrop,before.speed-after.speed);
            largestTurnChange=std::max(largestTurnChange,std::abs(after.yawRate-before.yawRate));
            if(!after.wheelGrounded[1])++unloadedFrames;
            SCOPED_TRACE(i);checkTires(scene,after);
        }

        EXPECT_EQ(stops,0);EXPECT_EQ(lostSteering,0);EXPECT_LT(worstDrop,1.);
        EXPECT_LT(largestTurnChange,.15);
    }
    EXPECT_GT(unloadedFrames,0); // This route actually exercises loss of front tire contact.
}

TEST(BuilderMotorbike, BrakingACompletedTurnDoesNotRotateInPlace) {
    Scene scene;BuilderMotorbike bike;
    ASSERT_TRUE(bike.place(scene.queries,{0,scene.ground()+.01,0},0,-200));
    for(int i=0;i<65;++i)bike.advance(scene.queries,1./60.,{1,1,false},-200);
    for(int i=0;i<80;++i)bike.advance(scene.queries,1./60.,{0,1,true},-200);
    ASSERT_NEAR(bike.state().speed,0,1e-9);
    const double yaw=bike.state().yaw;
    for(int i=0;i<20;++i)bike.advance(scene.queries,1./60.,{0,1,true},-200);
    EXPECT_EQ(bike.state().yaw,yaw);EXPECT_EQ(bike.state().yawRate,0);
}

#include "game/adventure/creative_scenery.hpp"
#include <fstream>
#include <cstdlib>
namespace {
struct CreativeScene {
    std::vector<uint16_t> samples=std::vector<uint16_t>(3072*3072,32768);
    voxy::terrain::lego::Surface surface{samples,3072,3072,8.f,1.f};
    AdventureState state;
    CreativeScene(){state.world=id(1).world;const auto p=creativeSpawn(surface,1.12);state.player={p.x,p.y,p.z,creativeStartYaw};}
};
}
TEST(CreativeScenery, DeterministicClearSpawnAndSharedPhysicalGeometry) {
    CreativeScene s;const auto first=CreativeScenery::admit(s.state,s.surface,{}),second=CreativeScenery::admit(s.state,s.surface,{});
    ASSERT_GE(first.props().size(),30u);ASSERT_EQ(first.props().size(),second.props().size());
    std::array<int,6> kinds{};std::vector<AdventureSpatialQueries::Solid> solids;
    ASSERT_TRUE(first.appendSolids(s.state.world,solids));AdventureSpatialQueries queries;
    ASSERT_TRUE(queries.bindTerrain(s.surface));ASSERT_TRUE(queries.publish(solids,1));
    EXPECT_TRUE(queries.clearCapsule({s.state.player.x,s.state.player.y,s.state.player.z},1.12,4.76));
    BuilderMotorbike bike;ASSERT_TRUE(bike.place(queries,{s.state.player.x+3.2,s.state.player.y,s.state.player.z},creativeStartYaw,-200));
    for(int i=0;i<60;++i)bike.advance(queries,1./60.,{1,0,false},-200);
    EXPECT_GT(bike.state().speed,3);
    for(size_t i=0;i<first.props().size();++i) {
        const auto& p=first.props()[i];++kinds[size_t(p.kind)];EXPECT_EQ(p.feet,second.props()[i].feet);EXPECT_EQ(p.id,second.props()[i].id);
        if(p.kind!=CreativePropKind::Flowers){EXPECT_GE(glm::length(glm::dvec2(p.feet.x,p.feet.z)-creativeStart),13);}
        if(p.kind==CreativePropKind::Broadleaf) {
            EXPECT_FALSE(queries.clearCapsule(p.feet,1.12,4.76));
            const auto ray=queries.raycast(p.feet+glm::dvec3(-3,2,0),{1,0,0},6);
            ASSERT_TRUE(ray.hit);EXPECT_TRUE(isCreativeScenerySolid({ray.structure,ray.part,{},{}}));
        }
    }
    for(int count:kinds)EXPECT_GT(count,0);
}
TEST(CreativeScenery, VillageAdmissionEnvelopesDoNotConsumePhysicalSlots) {
    CreativeScene s;const auto baseline=CreativeScenery::admit(s.state,s.surface,{});
    std::vector<AdventureSpatialQueries::Solid> village;
    ASSERT_TRUE(baseline.village().appendSolids(village));
    constexpr size_t spareSlots=32;
    ASSERT_GT(baseline.village().groups().size(),spareSlots);
    ASSERT_LT(village.size()+spareSlots,AdventureSpatialQueries::maximumSolids);
    // Remote owned solids consume real slots without overlapping any scenery.
    std::vector<AdventureSpatialQueries::Solid> owned(AdventureSpatialQueries::maximumSolids-village.size()-spareSlots,
        box(55,{9000,0,9000},{9001,1,9001}));
    const auto tight=CreativeScenery::admit(s.state,s.surface,owned);
    EXPECT_EQ(tight.village().groups().size(),baseline.village().groups().size());
    EXPECT_FALSE(tight.props().empty());
    auto published=owned;ASSERT_TRUE(tight.appendSolids(s.state.world,published));
    EXPECT_LE(published.size(),AdventureSpatialQueries::maximumSolids);
}
TEST(CreativeScenery, TemporaryCollisionCapacityDoesNotPermanentlySuppressForest) {
    CreativeScene s;
    std::vector<AdventureSpatialQueries::Solid> full(AdventureSpatialQueries::maximumSolids,
        box(55,{9000,0,9000},{9001,1,9001}));
    const auto deferred=CreativeScenery::admit(s.state,s.surface,full);
    ASSERT_TRUE(std::none_of(deferred.props().begin(),deferred.props().end(),[](const auto& p){
        return p.kind==CreativePropKind::Broadleaf||p.kind==CreativePropKind::Pine;
    }));
    const auto recovered=CreativeScenery::admit(s.state,s.surface,{},&deferred);
    EXPECT_TRUE(std::any_of(recovered.props().begin(),recovered.props().end(),[](const auto& p){
        return p.id>=10000&&p.id<1100000&&(p.kind==CreativePropKind::Broadleaf||p.kind==CreativePropKind::Pine);
    }));
    std::vector<AdventureSpatialQueries::Solid> solids;ASSERT_TRUE(recovered.appendSolids(s.state.world,solids));
    EXPECT_LE(solids.size(),AdventureSpatialQueries::maximumSolids);
}
TEST(CreativeScenery, ConstructionAndDoorReservationsYieldWholePropWithoutRespawn) {
    CreativeScene s;const auto original=CreativeScenery::admit(s.state,s.surface,{});
    ASSERT_FALSE(original.props().empty());const auto p=original.props().front();
    const auto blocker=box(55,p.minimum,p.maximum);
    const auto hidden=CreativeScenery::admit(s.state,s.surface,std::span(&blocker,1),&original);
    EXPECT_LT(hidden.props().size(),original.props().size());
    const auto stillHidden=CreativeScenery::admit(s.state,s.surface,{},&hidden);
    EXPECT_EQ(stillHidden.props().size(),hidden.props().size());
    const auto restored=CreativeScenery::admit(s.state,s.surface,std::span(&blocker,1));
    for(const auto& q:restored.props())EXPECT_NE(q.id,p.id);
    std::vector<AdventureSpatialQueries::Solid> solids;ASSERT_TRUE(hidden.appendSolids(s.state.world,solids));
    for(const auto& b:solids)EXPECT_NE(b.part.counter,creativeSceneryPartBase-p.id);
}
TEST(CreativeScenery, SavedPlayerAndLegacyIdentityTakePriority) {
    CreativeScene s;const auto original=CreativeScenery::admit(s.state,s.surface,{});
    ASSERT_FALSE(original.props().empty());const auto p=original.props().front();
    s.state.player={p.feet.x,p.feet.y+.18,p.feet.z,0};
    auto deferred=CreativeScenery::admit(s.state,s.surface,{});
    for(const auto& q:deferred.props())EXPECT_NE(q.id,p.id);
    s.state.structures.push_back({creativeSceneryStructureId,1,0,{}, {}});
    EXPECT_TRUE(CreativeScenery::admit(s.state,s.surface,{}).props().empty());
    voxy::terrain::lego::Surface invalid;EXPECT_TRUE(CreativeScenery::admit(s.state,invalid,{}).props().empty());
}
TEST(CreativeScenery, NearbyReservationsMatchStrictLinearOverlapAcrossBucketsAndLargeBounds) {
    CreativeScene s;
    for(const auto focus:std::array{creativeStart,glm::dvec2(-128,-128)}) {
        s.state.player.x=focus.x;s.state.player.z=focus.y;
        const auto original=CreativeScenery::admit(s.state,s.surface,{});
        ASSERT_FALSE(original.props().empty());const auto p=original.props().front();
        // Include remote reservations, duplicate references, strict margin
        // boundaries, negative cells, and the broad phase's large-box fallback.
        std::vector<AdventureSpatialQueries::Solid> probes{box(55,p.minimum,p.maximum)};
        const double boundary=p.maximum.x+.8;
        for(const double x:std::array{std::nextafter(boundary,-INFINITY),boundary,std::nextafter(boundary,INFINITY)})
            probes.push_back(box(55,{x,p.minimum.y,p.minimum.z},{x+1,p.maximum.y,p.maximum.z}));
        const double cell=std::floor(p.minimum.x/64)*64;
        probes.push_back(box(55,{cell-1,p.minimum.y,p.minimum.z},{cell+.8,p.maximum.y,p.maximum.z}));
        probes.push_back(box(55,{-9000,p.minimum.y,-9000},{9000,p.maximum.y,9000}));
        probes.push_back(box(55,{-9000,p.maximum.y+1,-9000},{9000,p.maximum.y+2,9000}));
        for(const auto& probe:probes) {
            std::vector<AdventureSpatialQueries::Solid> reserved(512,box(55,{9000,0,9000},{9001,1,9001}));
            reserved.push_back(probe);reserved.push_back(probe);
            const bool blocked=std::any_of(reserved.begin(),reserved.end(),[&](const auto& b){
                return glm::all(glm::lessThan(p.minimum,b.maximum+glm::dvec3(.8)))
                    &&glm::all(glm::greaterThan(p.maximum,b.minimum-glm::dvec3(.8)));
            });
            for(int order=0;order<2;++order) {
                SCOPED_TRACE(::testing::Message()<<"focus="<<focus.x<<","<<focus.y<<" probe="<<probe.minimum.x<<" order="<<order);
                const auto next=CreativeScenery::admit(s.state,s.surface,reserved,&original);
                const auto found=std::find_if(next.props().begin(),next.props().end(),[&](const auto& q){return q.id==p.id;});
                ASSERT_EQ(found==next.props().end(),blocked);
                if(found!=next.props().end()) {
                    EXPECT_EQ(found->feet,p.feet);EXPECT_EQ(found->minimum,p.minimum);EXPECT_EQ(found->maximum,p.maximum);
                    EXPECT_EQ(found->kind,p.kind);EXPECT_EQ(found->yawQuarterTurns,p.yawQuarterTurns);EXPECT_EQ(found->forestVariant,p.forestVariant);
                } else {
                    const auto cleared=CreativeScenery::admit(s.state,s.surface,{},&next);
                    EXPECT_TRUE(std::none_of(cleared.props().begin(),cleared.props().end(),[&](const auto& q){return q.id==p.id;}));
                }
                std::reverse(reserved.begin(),reserved.end());
            }
        }
    }
}
TEST(CreativeScenery, ActualStartingShelfSupportsAllSixKinds) {
    const char* path=std::getenv("VOXY_ADVENTURE_TEST_TERRAIN");if(!path)GTEST_SKIP()<<"Set terrain path for actual shelf admission";
    std::vector<uint16_t> samples(8192*8192);std::ifstream file(path,std::ios::binary);
    ASSERT_TRUE(file.read(reinterpret_cast<char*>(samples.data()),static_cast<std::streamsize>(samples.size()*2)));
    voxy::terrain::lego::Surface surface{samples,8192,8192,600.f,1.f};
    AdventureState state;state.world=id(1).world;const auto spawn=creativeSpawn(surface,1.12);state.player={spawn.x,spawn.y,spawn.z,creativeStartYaw};
    const auto scenery=CreativeScenery::admit(state,surface,{});std::array<int,6> kinds{};
    for(const auto& p:scenery.props())++kinds[size_t(p.kind)];
    EXPECT_GE(scenery.props().size(),24u);for(size_t i=0;i<6;++i)EXPECT_GT(kinds[i],0)<<"Missing kind "<<i;
    std::vector<AdventureSpatialQueries::Solid> solids;ASSERT_TRUE(scenery.appendSolids(state.world,solids));
    AdventureSpatialQueries queries;ASSERT_TRUE(queries.bindTerrain(surface));ASSERT_TRUE(queries.publish(solids,1));
    EXPECT_TRUE(queries.clearCapsule(spawn,1.12,4.76));
    std::cout<<"Scenery admitted "<<scenery.props().size()<<" props; kinds ";for(auto count:kinds)std::cout<<count<<' ';std::cout<<'\n';
}

TEST(CreativeScenery, StreamsStableWorldCellsAndRemembersDisplacedPropsAcrossTravel) {
    CreativeScene s;s.state.player.x=800;s.state.player.z=-800;
    auto first=CreativeScenery::admit(s.state,s.surface,{});
    ASSERT_GT(first.props().size(),100u);ASSERT_LE(first.props().size(),CreativeScenery::maximumProps);
    EXPECT_FALSE(first.needsRefresh({s.state.player.x,s.state.player.z}));
    const auto found=std::find_if(first.props().begin(),first.props().end(),[](const auto& p){return p.id>1024;});
    ASSERT_NE(found,first.props().end());const auto displaced=*found;
    auto blocked=box(555,displaced.minimum,displaced.maximum);
    auto hidden=CreativeScenery::admit(s.state,s.surface,std::span(&blocked,1),&first);
    const auto start=s.state.player;s.state.player.x-=750;
    EXPECT_TRUE(hidden.needsRefresh({s.state.player.x,s.state.player.z}));
    auto far=CreativeScenery::admit(s.state,s.surface,{},&hidden);
    ASSERT_GT(far.props().size(),100u);
    for(const auto& p:far.props()){EXPECT_GT(p.id,1024u);EXPECT_LT(glm::length(glm::dvec2(p.feet.x-s.state.player.x,p.feet.z-s.state.player.z)),350);}
    s.state.player=start;auto returned=CreativeScenery::admit(s.state,s.surface,{},&far);
    for(const auto& p:returned.props()) {
        EXPECT_NE(p.id,displaced.id);
        const auto old=std::find_if(first.props().begin(),first.props().end(),[&](const auto& q){return p.id==q.id;});
        ASSERT_NE(old,first.props().end());EXPECT_EQ(p.feet,old->feet);
    }
    // Newly streamed geometry respects the parked bike's entire envelope.
    auto farAgain=CreativeScenery::admit(s.state,s.surface,{},nullptr,std::span(&blocked,1));
    for(const auto& p:farAgain.props())EXPECT_NE(p.id,displaced.id);
}
TEST(CreativeScenery, RealWorldCoverageExtendsBeyondStartingClearing) {
    const char* path=std::getenv("VOXY_ADVENTURE_TEST_TERRAIN");if(!path)GTEST_SKIP();
    std::vector<uint16_t> samples(8192*8192);std::ifstream file(path,std::ios::binary);
    ASSERT_TRUE(file.read(reinterpret_cast<char*>(samples.data()),static_cast<std::streamsize>(samples.size()*2)));
    voxy::terrain::lego::Surface surface{samples,8192,8192,600.f,1.f};
    AdventureState state;state.world=id(1).world;
    for(const glm::dvec2 point:std::array{creativeStart,glm::dvec2(1500,-1100),glm::dvec2(900,-1000),glm::dvec2(-63,-895)}) {
        state.player={point.x,double(voxy::terrain::lego::supportHeight(surface,glm::vec2(point),1.12f))+.005,point.y,0};
        const auto scenery=CreativeScenery::admit(state,surface,{});
        // The collision neighbourhood is now 160 studs; preserve this older
        // 320-stud visual coverage check across both near and distant lists.
        size_t nearby=scenery.props().size();
        for(const auto& p:scenery.distantTrees())nearby+=glm::length(glm::dvec2(p.feet.x,p.feet.z)-point)<320;
        EXPECT_GT(nearby,40u)<<point.x<<","<<point.y;
        EXPECT_LE(scenery.props().size(),CreativeScenery::maximumProps);
        std::cout<<"World scenery at "<<point.x<<","<<point.y<<": "<<scenery.props().size()<<" props\n";
        std::vector<AdventureSpatialQueries::Solid> solids;ASSERT_TRUE(scenery.appendSolids(state.world,solids));
        AdventureSpatialQueries queries;ASSERT_TRUE(queries.bindTerrain(surface));ASSERT_TRUE(queries.publish(solids,1));
        EXPECT_TRUE(queries.clearCapsule({state.player.x,state.player.y,state.player.z},1.12,4.76));
    }
}

namespace {
class CreativeVillageRealTerrain : public ::testing::Test {
protected:
    std::vector<uint16_t> samples;
    voxy::terrain::lego::Surface surface;
    AdventureState state;
    CreativeScenery scenery;
    AdventureSpatialQueries queries;
    void SetUp() override {
        const char* path=std::getenv("VOXY_ADVENTURE_TEST_TERRAIN");
        if(!path)GTEST_SKIP()<<"Set terrain path for village traversal";
        samples.resize(8192*8192);std::ifstream file(path,std::ios::binary);
        ASSERT_TRUE(file.read(reinterpret_cast<char*>(samples.data()),static_cast<std::streamsize>(samples.size()*2)));
        surface={samples,8192,8192,600.f,1.f};state.world=id(1).world;
        const auto spawn=creativeSpawn(surface,1.12);state.player={spawn.x,spawn.y,spawn.z,creativeStartYaw};
        scenery=CreativeScenery::admit(state,surface,{});
        std::vector<AdventureSpatialQueries::Solid> solids;ASSERT_TRUE(scenery.appendSolids(state.world,solids));
        ASSERT_TRUE(queries.bindTerrain(surface));ASSERT_TRUE(queries.publish(solids,1));
    }
    glm::dvec3 floorAt(glm::dvec2 p) const {
        const double terrain=voxy::terrain::lego::supportHeight(surface,glm::vec2(p),1.12f);
        return {p.x,queries.supportHeight(p,1.12,terrain+3)+.005,p.y};
    }
    bool walkTo(AdventurePlayer& player,glm::dvec2 target,int ticks=600) const {
        for(int i=0;i<ticks;++i) {
            const glm::dvec2 delta=target-glm::dvec2(player.feet().x,player.feet().z);
            if(glm::length(delta)<.16)return true;
            player.advance(AdventurePlayer::fixedStep,{glm::normalize(delta),false});
        }
        ADD_FAILURE()<<"Walk stalled at "<<player.feet().x<<","<<player.feet().y<<","<<player.feet().z
            <<" toward "<<target.x<<","<<target.y;
        return false;
    }
};
}
TEST_F(CreativeVillageRealTerrain, AllBuildingsAdmitAndCreativeFigureWalksConnectedApproachAndEveryCottage) {
    std::array<size_t,15> meshes{};
    for(const auto& group:scenery.village().groups())for(const auto& piece:group.pieces)++meshes[piece.mesh];
    EXPECT_EQ(meshes[0]+meshes[1]+meshes[8]+meshes[9],11u);EXPECT_EQ(meshes[2],1u);EXPECT_EQ(meshes[3],1u);
    EXPECT_GE(meshes[4],12u);EXPECT_EQ(meshes[6],1u);EXPECT_GT(meshes[5],80u);
    AdventurePlayer walker;
    ASSERT_TRUE(walker.initialize(queries,{state.player.x,state.player.y,state.player.z},-200,creativeStartYaw,AdventurePlayer::creativeScale,AdventurePlayer::creativeRadius));
    for(const glm::dvec2 point:std::array{glm::dvec2(1194,-1114),glm::dvec2(1180,-1090),glm::dvec2(1158,-1070),glm::dvec2(1154,-1070),glm::dvec2(1154,-1044)}) {
        ASSERT_TRUE(walkTo(walker,point));EXPECT_EQ(walker.mode(),AdventurePlayer::Mode::Walking);
    }
    for(const auto& group:scenery.village().groups())for(const auto& piece:group.pieces)if(piece.mesh<2||piece.mesh==8||piece.mesh==9) {
        SCOPED_TRACE(group.id);
        const glm::dvec2 center(piece.feet.x,piece.feet.z),outside=center+glm::dvec2(0,piece.yaw==0?-10:10);
        AdventurePlayer visitor;
        ASSERT_TRUE(visitor.initialize(queries,floorAt(outside),-200,0,AdventurePlayer::creativeScale,AdventurePlayer::creativeRadius));
        ASSERT_TRUE(walkTo(visitor,center));
        EXPECT_EQ(visitor.mode(),AdventurePlayer::Mode::Walking);
        EXPECT_NEAR(visitor.feet().y,piece.feet.y+.325,.03);
        ASSERT_TRUE(walkTo(visitor,outside));
    }
}
TEST_F(CreativeVillageRealTerrain, MotorcycleTraversesVillageLanePastWellWithoutSnagging) {
    BuilderMotorbike bike;const glm::dvec2 start(1158,-1075);
    ASSERT_TRUE(bike.place(queries,floorAt(start),std::acos(-1.)*.5,-200));
    int ticks=0,stops=0;double worstDrop=0;
    for(;ticks<600&&bike.state().feet.x>1060;++ticks) {
        const auto before=bike.state();bike.advance(queries,BuilderMotorbike::fixedStep,{1,0,false},-200);
        const auto& after=bike.state();
        if(before.speed>3&&after.speed<.01)++stops;
        worstDrop=std::max(worstDrop,before.speed-after.speed);
        ASSERT_TRUE(std::isfinite(after.feet.y));EXPECT_NEAR(after.feet.z,start.y,.01);
    }
    EXPECT_LT(ticks,600)<<"Bike stalled at "<<bike.state().feet.x<<","<<bike.state().feet.y<<","<<bike.state().feet.z;
    EXPECT_EQ(stops,0);EXPECT_LT(worstDrop,3.0);
}
TEST_F(CreativeVillageRealTerrain, OwnedConstructionRemovesWholeBuildingAndDoesNotRespawnItDuringSession) {
    const auto& village=scenery.village();
    const auto found=std::find_if(village.groups().begin(),village.groups().end(),[](const auto& group){return group.id==1;});
    ASSERT_NE(found,village.groups().end());ASSERT_GT(found->pieces.size(),1u);
    const auto& wall=found->solids[2];const auto blocker=box(7100,wall.minimum,wall.maximum);
    const auto hidden=CreativeScenery::admit(state,surface,std::span(&blocker,1),&scenery);
    const auto stillHidden=CreativeScenery::admit(state,surface,{},&hidden);
    const auto restored=CreativeScenery::admit(state,surface,std::span(&blocker,1));
    for(const auto* result:{&hidden,&stillHidden,&restored}) {
        EXPECT_EQ(std::count_if(result->village().groups().begin(),result->village().groups().end(),[](const auto& group){return group.id==1;}),0);
        // The adjacent cottage remains; displacement must not wipe the village.
        EXPECT_EQ(std::count_if(result->village().groups().begin(),result->village().groups().end(),[](const auto& group){return group.id==2;}),1);
        std::vector<AdventureSpatialQueries::Solid> solids;ASSERT_TRUE(result->appendSolids(state.world,solids));
        for(const auto& solid:solids)EXPECT_NE(solid.part.counter,wall.part.counter);
    }
}

TEST(AdventurePlayer, RunningJumpKeepsPaceAndFastMovementStillStopsAtWalls) {
    Scene scene;AdventurePlayer runner;
    const auto ground=scene.ground()+.005;
    ASSERT_TRUE(runner.initialize(scene.queries,{0,ground,0},-200,0,AdventurePlayer::creativeScale,AdventurePlayer::creativeRadius));
    runner.advance(1./60.,{{1,0},true,1.75});
    EXPECT_EQ(runner.mode(),AdventurePlayer::Mode::Airborne);
    EXPECT_NEAR(runner.worldVelocity().x,3.6*std::sqrt(2.8)*1.75,.01);
    const auto wall=box(990,{4,-10,-10},{4.1,20,10});
    ASSERT_TRUE(scene.queries.publish(std::span(&wall,1),2));
    for(int i=0;i<90;++i)runner.advance(1./60.,{{1,0},false,1.75});
    EXPECT_LE(runner.feet().x,4.-runner.bodyRadius());
    EXPECT_TRUE(scene.queries.clearCapsule(runner.feet(),runner.bodyRadius(),runner.bodyHeight()));
}

TEST(AdventurePlayer, CreativeWalkAndRunClimbAndDescendLDrawBrickStairsWithoutJumping) {
    for(const double pace:{1.,1.75}) {
        SCOPED_TRACE(pace);
        Scene scene;
        const double base=scene.ground();
        // One LEGO brick = 24 LDraw units, one stud spacing = 20 units.
        // Three full-brick risers lead to a supported upper landing.
        const std::array stairs{
            box(2,{3,base,-3},{6,base+1.2,3}),
            box(3,{6,base,-3},{9,base+2.4,3}),
            box(4,{9,base,-3},{12,base+3.6,3}),
            box(5,{12,base,-3},{20,base+3.6,3})};
        ASSERT_TRUE(scene.queries.publish(stairs,2));
        AdventurePlayer player;
        const double floor=scene.queries.supportHeight({0,0},AdventurePlayer::creativeRadius*AdventurePlayer::creativeScale,base+.01);
        ASSERT_TRUE(player.initialize(scene.queries,{0,floor+.005,0},-200,0,
            AdventurePlayer::creativeScale,AdventurePlayer::creativeRadius));
        const auto traverse=[&](double destination) {
            const double perTick=3.6*std::sqrt(AdventurePlayer::creativeScale)*AdventurePlayer::fixedStep*pace;
            int tick=0;
            for(;tick<360&&std::abs(player.feet().x-destination)>.001;++tick) {
                const auto before=player.feet();
                const double input=std::clamp((destination-before.x)/perTick,-1.,1.);
                player.advance(AdventurePlayer::fixedStep,{{input,0},false,pace});
                EXPECT_EQ(player.mode(),AdventurePlayer::Mode::Walking)<<"tick "<<tick;
                EXPECT_TRUE(scene.queries.clearCapsule(player.feet(),player.bodyRadius(),player.bodyHeight()));
                const double support=scene.queries.supportHeight({player.feet().x,player.feet().z},player.bodyRadius(),player.feet().y+.01);
                EXPECT_NEAR(player.feet().y,support+.005,.001)<<"tick "<<tick;
                EXPECT_LE(std::abs(player.feet().y-before.y),1.21)<<"tick "<<tick;
            }
            EXPECT_LT(tick,360);
            EXPECT_NEAR(player.feet().x,destination,.001);
        };
        traverse(14);
        EXPECT_NEAR(player.feet().y,base+3.605,.001);
        traverse(0);
        EXPECT_NEAR(player.feet().y,floor+.005,.001);
    }
}

TEST(AdventurePlayer, CreativeWalkAndRunCannotAutoClimbTwoBrickWall) {
    for(const double pace:{1.,1.75}) {
        SCOPED_TRACE(pace);
        Scene scene;const double base=scene.ground();
        const auto wall=box(2,{3,base,-4},{8,base+2.4,4});
        ASSERT_TRUE(scene.queries.publish(std::span(&wall,1),2));
        AdventurePlayer player;
        const double floor=scene.queries.supportHeight({0,0},AdventurePlayer::creativeRadius*AdventurePlayer::creativeScale,base+.01);
        ASSERT_TRUE(player.initialize(scene.queries,{0,floor+.005,0},-200,0,
            AdventurePlayer::creativeScale,AdventurePlayer::creativeRadius));
        for(int i=0;i<120;++i)player.advance(AdventurePlayer::fixedStep,{{1,0},false,pace});
        EXPECT_GT(player.feet().x,1.7);
        EXPECT_LT(player.feet().x,3-player.bodyRadius());
        EXPECT_NEAR(player.feet().y,floor+.005,.001);
        EXPECT_EQ(player.mode(),AdventurePlayer::Mode::Walking);
        EXPECT_TRUE(scene.queries.clearCapsule(player.feet(),player.bodyRadius(),player.bodyHeight()));
    }
}

TEST(AdventurePlayer, NormalAdventureRetainsItsOriginalStepAllowance) {
    Scene scene;const double base=scene.ground();
    // This exceeds the original .36 step plus the .3 rounded capsule radius,
    // while remaining below the creative 1.26 allowance.
    const auto ledge=box(2,{2,base,-3},{7,base+.8,3});
    ASSERT_TRUE(scene.queries.publish(std::span(&ledge,1),2));
    AdventurePlayer player;ASSERT_TRUE(player.initialize(scene.queries,{0,base+.005,0},-200));
    for(int i=0;i<120;++i)player.advance(AdventurePlayer::fixedStep,{{1,0},false});
    EXPECT_GT(player.feet().x,1.6);
    EXPECT_LT(player.feet().x,1.7);
    EXPECT_NEAR(player.feet().y,base+.005,.001);
    EXPECT_EQ(player.mode(),AdventurePlayer::Mode::Walking);
}

namespace {
std::map<uint32_t,CreativeProp> forestInventory(const CreativeScenery& scenery) {
    std::map<uint32_t,CreativeProp> result;
    for(const auto& p:scenery.props()) {
        if(p.kind==CreativePropKind::Broadleaf||p.kind==CreativePropKind::Pine) {
            EXPECT_TRUE(result.emplace(p.id,p).second);
        }
    }
    for(const auto& p:scenery.distantTrees())EXPECT_TRUE(result.emplace(p.id,p).second);
    return result;
}
}
TEST(CreativeForest, TwoKilometreCoverageHasNoDistantCollisionOrDuplicateTrees) {
    CreativeScene s;const auto scene=CreativeScenery::admit(s.state,s.surface,{});
    const auto trees=forestInventory(scene);ASSERT_GT(trees.size(),2000u);
    size_t horizon=0;
    for(const auto& [key,p]:trees) {
        const double d=glm::length(glm::dvec2(p.feet.x-s.state.player.x,p.feet.z-s.state.player.z));
        horizon+=d>=1900&&d<=2000;
        EXPECT_LT(key,1100000u);
        EXPECT_TRUE(isCreativeScenerySolid({{s.state.world,creativeSceneryStructureId},{s.state.world,creativeSceneryPartBase-key},{},{}}));
    }
    EXPECT_GT(horizon,100u);
    std::vector<AdventureSpatialQueries::Solid> solids;ASSERT_TRUE(scene.appendSolids(s.state.world,solids));
    std::set<uint64_t> physical;for(const auto& b:solids)physical.insert(b.part.counter);
    for(const auto& p:scene.distantTrees())EXPECT_FALSE(physical.contains(creativeSceneryPartBase-p.id));
    EXPECT_LT(solids.size(),AdventureSpatialQueries::maximumSolids);
}
TEST(CreativeForest, SettlementMeadowProtectsEveryVillageEdgeDuringTravel) {
    CreativeScene s;
    // Independent landmarks from the village layout, including the outlying
    // cottage, blacksmith and cannon, rather than only the starting position.
    const std::array landmarks{creativeStart,glm::dvec2(1042,-1068),glm::dvec2(1044,-1091),
        glm::dvec2(1088,-1092),glm::dvec2(1110,-1011),glm::dvec2(1208,-1032),glm::dvec2(1240,-1027)};
    auto scene=CreativeScenery::admit(s.state,s.surface,{});
    for(const auto focus:std::array{creativeStart,glm::dvec2(1400,-1000),glm::dvec2(900,-1100)}) {
        s.state.player.x=focus.x;s.state.player.z=focus.y;
        scene=CreativeScenery::admit(s.state,s.surface,{},&scene);
        const auto trees=forestInventory(scene);size_t surrounding=0;
        for(const auto& [id,p]:trees)if(p.forestVariant<6) {
            const glm::dvec2 tree(p.feet.x,p.feet.z);
            for(const auto landmark:landmarks)EXPECT_GE(glm::length(tree-landmark),60.);
            surrounding+=glm::length(tree-glm::dvec2(1134,-1065))<500;
        }
        EXPECT_GT(surrounding,1000u); // The town has a meadow, not an empty landscape.
    }
    EXPECT_TRUE(std::any_of(scene.props().begin(),scene.props().end(),[](const auto& p){return p.id>=100&&p.id<114;}));
}
TEST(CreativeForest, StreamedTilesMatchColdGenerationAndReuseCollisionOnlyRefreshes) {
    CreativeScene s;auto scene=CreativeScenery::admit(s.state,s.surface,{});
    ASSERT_TRUE(scene.forestSource());
    const auto source=scene.forestSource();
    s.state.player.x+=16;
    scene=CreativeScenery::admit(s.state,s.surface,{},&scene);
    EXPECT_EQ(scene.forestSource(),source);
    for(const auto delta:std::array{glm::dvec2(128,0),glm::dvec2(0,-128),glm::dvec2(-256,256)}) {
        s.state.player.x+=delta.x;s.state.player.z+=delta.y;
        const auto begin=std::chrono::steady_clock::now();
        scene=CreativeScenery::admit(s.state,s.surface,{},&scene);
        const auto warm=std::chrono::steady_clock::now();
        const auto cold=CreativeScenery::admit(s.state,s.surface,{});
        const auto finish=std::chrono::steady_clock::now();
        ASSERT_EQ(scene.forestSource()->size(),cold.forestSource()->size());
        for(size_t i=0;i<scene.forestSource()->size();++i) {
            const auto& a=(*scene.forestSource())[i];const auto& b=(*cold.forestSource())[i];
            EXPECT_EQ(a.id,b.id);EXPECT_EQ(a.feet,b.feet);EXPECT_EQ(a.minimum,b.minimum);EXPECT_EQ(a.maximum,b.maximum);
            EXPECT_EQ(a.forestVariant,b.forestVariant);EXPECT_EQ(a.yawQuarterTurns,b.yawQuarterTurns);
        }
        std::cout<<"Forest region refresh: reused="<<std::chrono::duration<double,std::milli>(warm-begin).count()
                 <<" ms cold="<<std::chrono::duration<double,std::milli>(finish-warm).count()<<" ms\n";
    }
}

TEST(CreativeForest, ApproachAndDifferentRegionOrderKeepTreeIdentityAndGeometry) {
    CreativeScene s;const auto original=CreativeScenery::admit(s.state,s.surface,{});
    const auto before=forestInventory(original);
    const auto selected=std::find_if(original.distantTrees().begin(),original.distantTrees().end(),[&](const auto& p){
        const double d=glm::length(glm::dvec2(p.feet.x-s.state.player.x,p.feet.z-s.state.player.z));return d>500&&d<700;
    });ASSERT_NE(selected,original.distantTrees().end());const auto target=*selected;
    s.state.player.x=target.feet.x+20;s.state.player.z=target.feet.z;
    const auto approached=CreativeScenery::admit(s.state,s.surface,{},&original);
    const auto fresh=CreativeScenery::admit(s.state,s.surface,{});
    const auto nearby=std::find_if(approached.props().begin(),approached.props().end(),[&](const auto& p){return p.id==target.id;});
    ASSERT_NE(nearby,approached.props().end());EXPECT_EQ(nearby->feet,target.feet);EXPECT_EQ(nearby->kind,target.kind);
    const auto after=forestInventory(approached),reloaded=forestInventory(fresh);
    for(const auto& [key,p]:after) {
        const auto old=before.find(key);if(old!=before.end()) {
            EXPECT_EQ(old->second.feet,p.feet);EXPECT_EQ(old->second.kind,p.kind);EXPECT_EQ(old->second.yawQuarterTurns,p.yawQuarterTurns);
            EXPECT_EQ(old->second.forestVariant,p.forestVariant);
        }
        // Trees overlapping the actor or authored local ground detail may be
        // suppressed, but every common identity has precisely the same pose.
        const auto reload=reloaded.find(key);if(reload!=reloaded.end()){EXPECT_EQ(reload->second.feet,p.feet);}
    }
    std::vector<AdventureSpatialQueries::Solid> solids;ASSERT_TRUE(approached.appendSolids(s.state.world,solids));
    EXPECT_TRUE(std::any_of(solids.begin(),solids.end(),[&](const auto& b){return b.part.counter==creativeSceneryPartBase-target.id;}));
}
TEST(CreativeForest, DistantConstructionSuppressesWholeTreeAcrossApproachAndReload) {
    CreativeScene s;const auto original=CreativeScenery::admit(s.state,s.surface,{});
    ASSERT_FALSE(original.distantTrees().empty());const auto p=original.distantTrees().front();
    const auto blocker=box(991,p.minimum,p.maximum);
    const auto hidden=CreativeScenery::admit(s.state,s.surface,std::span(&blocker,1),&original);
    EXPECT_FALSE(forestInventory(hidden).contains(p.id));
    s.state.player.x=p.feet.x+20;s.state.player.z=p.feet.z;
    const auto approached=CreativeScenery::admit(s.state,s.surface,{},&hidden);
    EXPECT_FALSE(forestInventory(approached).contains(p.id));
    const auto loaded=CreativeScenery::admit(s.state,s.surface,std::span(&blocker,1));
    EXPECT_FALSE(forestInventory(loaded).contains(p.id));
}
TEST(CreativeForest, PlayerCanStandUnderCanopyButStreamingReservesTrunkSpace) {
    CreativeScene s;const auto original=CreativeScenery::admit(s.state,s.surface,{});
    const auto selected=std::find_if(original.distantTrees().begin(),original.distantTrees().end(),[](const auto& p){
        return p.forestVariant==0;
    });ASSERT_NE(selected,original.distantTrees().end());const auto tree=*selected;
    s.state.player.x=tree.feet.x+3;s.state.player.y=tree.feet.y;s.state.player.z=tree.feet.z;
    const auto underneath=CreativeScenery::admit(s.state,s.surface,{});
    ASSERT_TRUE(forestInventory(underneath).contains(tree.id));
    std::vector<AdventureSpatialQueries::Solid> solids;
    ASSERT_TRUE(underneath.appendSolids(s.state.world,solids));
    const auto trunk=std::find_if(solids.begin(),solids.end(),[&](const auto& b){
        return b.part.counter==creativeSceneryPartBase-tree.id;
    });ASSERT_NE(trunk,solids.end());
    EXPECT_LT(trunk->maximum.x-trunk->minimum.x,1.5);
    EXPECT_GT(tree.maximum.x-tree.minimum.x,10.);
    s.state.player.x=tree.feet.x;
    EXPECT_FALSE(forestInventory(CreativeScenery::admit(s.state,s.surface,{})).contains(tree.id));
}
TEST(CreativeForest, DetailedVillageBudgetDoesNotLeaveAnEmptyCircleAroundPlayer) {
    CreativeScene s;s.state.player.x=0;s.state.player.z=0;
    // Imported parts and their reserved capacity share this packet with village
    // paving, user builds and forest. Reproduce a crowded installed scene.
    std::vector<AdventureSpatialQueries::Solid> installed;
    for(uint64_t i=0;i<8000;++i)installed.push_back(box(i+100,{4000,0,4000},{4001,1,4001}));
    const auto scene=CreativeScenery::admit(s.state,s.surface,installed);
    size_t trunks=0;
    for(const auto& p:scene.props())trunks+=p.forestVariant<6;
    EXPECT_GT(trunks,100u);
    EXPECT_TRUE(scene.appendSolids(s.state.world,installed));
    EXPECT_LT(installed.size(),AdventureSpatialQueries::maximumSolids);
}
TEST(CreativeForest, MixedAgeGrovesHaveOverlappingCrownsAndWalkableTrunkSpacing) {
    CreativeScene s;s.state.player.x=0;s.state.player.z=0;
    const auto trees=forestInventory(CreativeScenery::admit(s.state,s.surface,{}));
    std::array<size_t,6> variants{};
    std::map<std::pair<int,int>,const CreativeProp*> cells;
    for(const auto& [id,p]:trees)if(p.forestVariant<variants.size()) {
        ++variants[p.forestVariant];
        cells[{int(std::floor(p.feet.x/8)),int(std::floor(p.feet.z/8))}]=&p;
    }
    size_t touching=0;
    for(const auto& [cell,p]:cells) {
        bool crownTouches=false;
        for(int dz=-1;dz<=1;++dz)for(int dx=-1;dx<=1;++dx) {
            if(!dx&&!dz)continue;
            const auto neighbor=cells.find({cell.first+dx,cell.second+dz});
            if(neighbor==cells.end())continue;
            const auto* q=neighbor->second;
            EXPECT_GE(glm::length(glm::dvec2(p->feet.x-q->feet.x,p->feet.z-q->feet.z)),3.);
            crownTouches|=p->minimum.x<q->maximum.x&&p->maximum.x>q->minimum.x&&
                          p->minimum.z<q->maximum.z&&p->maximum.z>q->minimum.z;
        }
        touching+=crownTouches;
    }
    for(size_t n:variants)EXPECT_GT(n,50u);
    EXPECT_GT(touching,cells.size()/2);
    std::cout<<"Forest canopy neighbours: "<<touching<<" / "<<cells.size()<<'\n';
}
TEST(CreativeForest, RealTerrainKeepsDistantWoodlandAndBoundedCollision) {
    const char* path=std::getenv("VOXY_ADVENTURE_TEST_TERRAIN");if(!path)GTEST_SKIP();
    std::vector<uint16_t> samples(8192*8192);std::ifstream file(path,std::ios::binary);
    ASSERT_TRUE(file.read(reinterpret_cast<char*>(samples.data()),static_cast<std::streamsize>(samples.size()*2)));
    voxy::terrain::lego::Surface surface{samples,8192,8192,600.f,1.f};
    AdventureState state;state.world=id(1).world;
    for(const glm::dvec2 point:std::array{creativeStart,glm::dvec2(800,-800),glm::dvec2(-63,-895)}) {
        state.player={point.x,double(voxy::terrain::lego::supportHeight(surface,glm::vec2(point),1.12f))+.005,point.y,0};
        const auto scenery=CreativeScenery::admit(state,surface,{});size_t horizon=0;
        for(const auto& p:scenery.distantTrees()) {
            const double distance=glm::length(glm::dvec2(p.feet.x,p.feet.z)-point);
            horizon+=distance>=1800&&distance<=2000;
            EXPECT_GT(p.feet.y,installedWorld().waterHeight+4);
        }
        EXPECT_GT(horizon,100u);EXPECT_GT(scenery.distantTrees().size(),1000u);
        std::vector<AdventureSpatialQueries::Solid> solids;ASSERT_TRUE(scenery.appendSolids(state.world,solids));
        EXPECT_LE(solids.size(),AdventureSpatialQueries::maximumSolids);
        std::cout<<"Forest at "<<point.x<<","<<point.y<<": near="<<scenery.props().size()<<" distant="<<scenery.distantTrees().size()<<" horizon="<<horizon<<" solids="<<solids.size()<<'\n';
    }
}
