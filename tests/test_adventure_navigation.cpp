#include "game/adventure/layered_navigation.hpp"
#include "game/adventure/construction_policy.hpp"
#include <gtest/gtest.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

using namespace voxy::game::adventure;
namespace {
using Status=LayeredNavigation::SearchStatus;
using FollowStatus=NavigationFollower::FollowStatus;
struct Scene {
    std::vector<uint16_t> samples=std::vector<uint16_t>(128*128,32768);
    voxy::terrain::lego::Surface terrain{samples,128,128,8.f,1.f};
    AdventureState state;
    AdventureSpatialQueries queries;
    uint64_t revision=0,nextPart=3;
    Scene() {
        state.world.bytes[0]=31;state.structures.push_back({2,1,0,{}, {}});
        EXPECT_TRUE(queries.bindTerrain(terrain));EXPECT_TRUE(publish());
    }
    double ground() const {
        return double(voxy::terrain::lego::supportHeight(terrain,{0,0},.3f));
    }
    double base() const {return std::round(ground()*50)/50;}
    uint64_t piece(PieceKind kind,glm::dvec3 position,uint8_t yaw=0) {
        const auto id=nextPart++;
        state.structures.front().parts.push_back({id,kind,
            {int32_t(std::round(position.x*50)),int32_t(std::round(position.y*50)),int32_t(std::round(position.z*50))},yaw,0});
        return id;
    }
    bool publish() {
        std::string error;std::vector<AdventureSpatialQueries::Solid> solids;
        if(!compileSolids(state,solids,error)){ADD_FAILURE()<<error;return false;}
        return queries.publish(solids,++revision);
    }
    glm::dvec3 feet(double x,double z,double maximum) const {
        return {x,queries.supportHeight({x,z},AdventurePlayer::radius,maximum)+.005,z};
    }
    void bridge(bool stairs=false) {
        // Eight actual catalog floor pieces form a 4 m wide elevated deck.
        // Piers sit at its sides, leaving the central ground route walkable.
        for(double x:{-3.,-1.,1.,3.})for(double z:{-1.,1.})piece(PieceKind::Floor,{x,base()+2.56,z});
        for(double x:{-3.,3.})for(double z:{-1.76,1.76})for(int level=0;level<3;++level)
            piece(PieceKind::Pier,{x,base()+double(level)*.96,z});
        if(stairs)for(int level=0;level<3;++level)
            piece(PieceKind::Stair,{-9.+double(level)*2,base()+double(level)*.96,0},3);
        EXPECT_TRUE(publish());
    }
};
bool ready(LayeredNavigation& navigation,const AdventureSpatialQueries& queries,size_t columns=64) {
    for(size_t work=0;work<4096;++work) {
        if(navigation.status()!=Status::Building)return navigation.status()==Status::Ready;
        const auto result=navigation.advance(queries,columns,4);
        EXPECT_LE(result.columns,std::min(columns,size_t{64}));EXPECT_LE(result.expansions,4u);
    }
    ADD_FAILURE()<<"Navigation did not finish its bounded columns";return false;
}
Status find(LayeredNavigation& navigation,const AdventureSpatialQueries& queries,glm::dvec3 from,glm::dvec3 to) {
    auto status=navigation.request(queries,from,to);
    for(size_t work=0;status==Status::Searching&&work<2048;++work) {
        const auto result=navigation.advance(queries,16,8);
        EXPECT_LE(result.columns,16u);EXPECT_LE(result.expansions+result.endpointChecks,8u);
        EXPECT_LE(result.controllerTicks,(result.endpointChecks+result.expansions*8*LayeredNavigation::maximumLayers)*24);
        EXPECT_LE(result.totalExpansions,LayeredNavigation::maximumExpansions);
        status=result.status;
    }
    return status;
}
bool follow(const AdventureSpatialQueries& queries,const LayeredNavigation& navigation,
            AdventurePlayer& player,std::vector<glm::dvec3>& trace) {
    NavigationFollower follower;
    if(!follower.begin(queries,navigation,navigation.path(),player))return false;
    for(int tick=0;tick<1600;++tick) {
        const auto before=player.tick();const auto result=follower.advance(queries,navigation,player);
        EXPECT_LE(player.tick(),before+1);trace.push_back(player.feet());
        EXPECT_TRUE(queries.clearCapsule(player.feet()));
        if(result==FollowStatus::Arrived)return true;
        if(result!=FollowStatus::Walking)return false;
        EXPECT_EQ(player.tick(),before+1);
    }
    return false;
}
}

TEST(AdventureNavigation, RealFollowerCrossesBridgeAndGroundBelowWithoutChangingLayers) {
    Scene scene;scene.bridge();LayeredNavigation navigation;
    ASSERT_TRUE(navigation.configure({-2,-1,4,2,-1,8,-200}));
    ASSERT_TRUE(navigation.synchronize(scene.queries));ASSERT_TRUE(ready(navigation,scene.queries));
    ASSERT_GE(navigation.layerCount({.25,.25}),2u);
    for(const double maximum:{1.,5.}) {
        const auto start=scene.feet(-3.25,.25,maximum),goal=scene.feet(3.25,.25,maximum);
        ASSERT_EQ(find(navigation,scene.queries,start,goal),Status::Found);
        AdventurePlayer actor;ASSERT_TRUE(actor.initialize(scene.queries,start,-200));
        std::vector<glm::dvec3> trace;ASSERT_TRUE(follow(scene.queries,navigation,actor,trace));
        EXPECT_LT(glm::length(actor.feet()-goal),.12);
        for(const auto point:trace) {
            if(maximum<2) {
                const double support=double(voxy::terrain::lego::supportHeight(
                    scene.terrain,glm::vec2(glm::dvec2(point.x,point.z)),.3f));
                EXPECT_NEAR(point.y,support+.005,1e-5);
                EXPECT_LT(point.y+AdventurePlayer::height,scene.base()+2.56);
            } else EXPECT_NEAR(point.y,start.y,1e-5);
        }
        EXPECT_EQ(actor.mode(),AdventurePlayer::Mode::Walking);
    }
}

TEST(AdventureNavigation, RealFollowerClimbsAndDescendsAllEighteenCatalogStairTreads) {
    Scene scene;scene.bridge(true);LayeredNavigation navigation;
    ASSERT_TRUE(navigation.configure({-3,-1,5,2,-1,8,-200}));
    ASSERT_TRUE(navigation.synchronize(scene.queries));ASSERT_TRUE(ready(navigation,scene.queries));
    const auto ground=scene.feet(-10.75,.25,1),deck=scene.feet(2.75,.25,5);
    AdventurePlayer actor;ASSERT_TRUE(actor.initialize(scene.queries,ground,-200));
    ASSERT_EQ(find(navigation,scene.queries,ground,deck),Status::Found);
    std::vector<glm::dvec3> uphill;ASSERT_TRUE(follow(scene.queries,navigation,actor,uphill));
    EXPECT_LT(glm::length(actor.feet()-deck),.12);EXPECT_GT(actor.feet().y-ground.y,2.8);
    for(double height:{.5,1.5,2.5})EXPECT_TRUE(std::any_of(uphill.begin(),uphill.end(),[&](auto p){return std::abs(p.y-ground.y-height)<.2;}));
    ASSERT_EQ(find(navigation,scene.queries,actor.feet(),ground),Status::Found);
    std::vector<glm::dvec3> downhill;ASSERT_TRUE(follow(scene.queries,navigation,actor,downhill));
    EXPECT_LT(glm::length(actor.feet()-ground),.12);EXPECT_EQ(actor.mode(),AdventurePlayer::Mode::Walking);
}

TEST(AdventureNavigation, NewWallStopsTheOldPathBeforeMovementAndRepathWalksAroundIt) {
    Scene scene;LayeredNavigation navigation;
    ASSERT_TRUE(navigation.configure({-2,-2,4,4,-1,6,-200}));
    ASSERT_TRUE(navigation.synchronize(scene.queries));ASSERT_TRUE(ready(navigation,scene.queries));
    const auto start=scene.feet(-2.75,.25,1),goal=scene.feet(2.75,.25,1);
    ASSERT_EQ(find(navigation,scene.queries,start,goal),Status::Found);
    const auto oldPath=navigation.path();AdventurePlayer actor;ASSERT_TRUE(actor.initialize(scene.queries,start,-200));
    NavigationFollower follower;ASSERT_TRUE(follower.begin(scene.queries,navigation,oldPath,actor));
    scene.piece(PieceKind::Wall,{0,scene.base(),0},1);ASSERT_TRUE(scene.publish());
    EXPECT_FALSE(navigation.valid(scene.queries,oldPath));
    ASSERT_TRUE(navigation.synchronize(scene.queries));EXPECT_FALSE(navigation.valid(scene.queries,oldPath));
    const auto before=actor.state();
    EXPECT_EQ(follower.advance(scene.queries,navigation,actor),FollowStatus::Stale);
    EXPECT_EQ(actor.feet(),before.feet);EXPECT_EQ(actor.tick(),before.tick);
    ASSERT_TRUE(ready(navigation,scene.queries));
    ASSERT_EQ(find(navigation,scene.queries,start,goal),Status::Found);
    std::vector<glm::dvec3> trace;ASSERT_TRUE(follow(scene.queries,navigation,actor,trace));
    EXPECT_LT(glm::length(actor.feet()-goal),.12);
    EXPECT_TRUE(std::any_of(trace.begin(),trace.end(),[](auto point){return std::abs(point.z)>1.3;}));
}

TEST(AdventureNavigation, RemovingBridgeDeckRefusesCrossingAndRestoringItRestoresTraversal) {
    Scene scene;scene.bridge();LayeredNavigation navigation;
    ASSERT_TRUE(navigation.configure({-2,-1,4,2,-1,8,-200}));
    ASSERT_TRUE(navigation.synchronize(scene.queries));ASSERT_TRUE(ready(navigation,scene.queries));
    const auto start=scene.feet(-3.25,.25,5),goal=scene.feet(3.25,.25,5);
    ASSERT_EQ(find(navigation,scene.queries,start,goal),Status::Found);const auto path=navigation.path();
    const auto complete=scene.state;
    auto& parts=scene.state.structures.front().parts;
    std::erase_if(parts,[](const auto& part){return part.kind==PieceKind::Floor&&std::abs(part.position.x)==50;});
    ASSERT_TRUE(scene.publish());ASSERT_TRUE(navigation.synchronize(scene.queries));
    EXPECT_FALSE(navigation.valid(scene.queries,path));ASSERT_TRUE(ready(navigation,scene.queries));
    EXPECT_EQ(find(navigation,scene.queries,start,goal),Status::NoPath);
    scene.state=complete;ASSERT_TRUE(scene.publish());ASSERT_TRUE(navigation.synchronize(scene.queries));
    ASSERT_TRUE(ready(navigation,scene.queries));ASSERT_EQ(find(navigation,scene.queries,start,goal),Status::Found);
    AdventurePlayer actor;ASSERT_TRUE(actor.initialize(scene.queries,start,-200));
    std::vector<glm::dvec3> trace;ASSERT_TRUE(follow(scene.queries,navigation,actor,trace));
    EXPECT_LT(glm::length(actor.feet()-goal),.12);
}

TEST(AdventureNavigation, DistantEditedTilePreservesTheAcceptedPathAndItsTileGenerations) {
    Scene scene;LayeredNavigation navigation;
    ASSERT_TRUE(navigation.configure({-4,-2,8,4,-1,6,-200}));
    ASSERT_TRUE(navigation.synchronize(scene.queries));ASSERT_TRUE(ready(navigation,scene.queries));
    const auto start=scene.feet(-10.75,.25,1),goal=scene.feet(-5.25,.25,1);
    ASSERT_EQ(find(navigation,scene.queries,start,goal),Status::Found);
    const auto path=navigation.path();ASSERT_FALSE(path.tiles.empty());
    AdventurePlayer actor;ASSERT_TRUE(actor.initialize(scene.queries,start,-200));
    NavigationFollower follower;ASSERT_TRUE(follower.begin(scene.queries,navigation,path,actor));
    scene.piece(PieceKind::Wall,{12,scene.base(),4},1);ASSERT_TRUE(scene.publish());
    ASSERT_TRUE(navigation.synchronize(scene.queries));
    EXPECT_GT(navigation.dirtyTileCount(),0u);EXPECT_LT(navigation.dirtyTileCount(),32u);
    ASSERT_TRUE(navigation.valid(scene.queries,path));
    for(const auto stamp:path.tiles)EXPECT_EQ(navigation.tileGeneration(stamp.tile),stamp.generation);
    const auto before=actor.tick();EXPECT_EQ(follower.advance(scene.queries,navigation,actor),FollowStatus::Walking);
    EXPECT_EQ(actor.tick(),before+1);
    ASSERT_TRUE(ready(navigation,scene.queries));EXPECT_TRUE(navigation.valid(scene.queries,path));
    for(const auto stamp:path.tiles)EXPECT_EQ(navigation.tileGeneration(stamp.tile),stamp.generation);
    FollowStatus result=FollowStatus::Walking;
    for(int tick=0;result==FollowStatus::Walking&&tick<400;++tick)result=follower.advance(scene.queries,navigation,actor);
    EXPECT_EQ(result,FollowStatus::Arrived);EXPECT_LT(glm::length(actor.feet()-goal),.12);
}

TEST(AdventureNavigation, WorkBudgetsAreBoundedAndInvalidConfigurationOrStaleQueriesCannotMoveAnActor) {
    Scene scene;LayeredNavigation navigation;
    const LayeredNavigation::Region region{-1,-1,2,2,-1,6,-200};
    ASSERT_TRUE(navigation.configure(region));ASSERT_TRUE(navigation.synchronize(scene.queries));
    const auto first=navigation.advance(scene.queries,3,1);
    EXPECT_EQ(first.columns,3u);EXPECT_LE(first.expansions,1u);EXPECT_GT(first.dirtyTiles,0u);
    const auto capped=navigation.advance(scene.queries,10000,10000);
    EXPECT_LE(capped.columns,64u);EXPECT_LE(capped.expansions,32u);
    ASSERT_TRUE(ready(navigation,scene.queries,7));
    const auto start=scene.feet(-2.75,.25,1),goal=scene.feet(2.75,.25,1);
    ASSERT_EQ(navigation.request(scene.queries,start,goal),Status::Searching);
    const auto endpoint=navigation.advance(scene.queries,0,1);
    EXPECT_EQ(endpoint.endpointChecks,1u);EXPECT_EQ(endpoint.expansions,0u);
    EXPECT_LE(endpoint.controllerTicks,24u);
    ASSERT_EQ(find(navigation,scene.queries,start,goal),Status::Found);const auto path=navigation.path();
    auto invalid=region;invalid.tilesX=0;EXPECT_FALSE(navigation.configure(invalid));
    invalid=region;invalid.tilesX=9;invalid.tilesZ=8;EXPECT_FALSE(navigation.configure(invalid));
    invalid=region;invalid.minimumFeet=7;EXPECT_FALSE(navigation.configure(invalid));
    invalid=region;invalid.waterHeight=std::numeric_limits<double>::infinity();EXPECT_FALSE(navigation.configure(invalid));
    EXPECT_TRUE(navigation.valid(scene.queries,path));
    for(const auto stamp:path.tiles)EXPECT_EQ(navigation.tileGeneration(stamp.tile),stamp.generation);
    LayeredNavigation otherNavigation;ASSERT_TRUE(otherNavigation.configure(region));
    ASSERT_TRUE(otherNavigation.synchronize(scene.queries));ASSERT_TRUE(ready(otherNavigation,scene.queries));
    EXPECT_FALSE(otherNavigation.valid(scene.queries,path));
    AdventureSpatialQueries other;ASSERT_TRUE(other.bindTerrain(scene.terrain));ASSERT_TRUE(other.publish({},1));
    EXPECT_FALSE(navigation.valid(other,path));
    AdventurePlayer actor;ASSERT_TRUE(actor.initialize(scene.queries,start,-200));NavigationFollower follower;
    EXPECT_FALSE(follower.begin(other,navigation,path,actor));EXPECT_EQ(actor.tick(),0u);EXPECT_EQ(actor.feet(),start);
}

TEST(AdventureNavigation, MoreThanEightRealWalkableLayersRefuseCapacityInsteadOfDroppingAFloor) {
    Scene scene;
    for(int level=0;level<8;++level)scene.piece(PieceKind::Floor,{0,scene.base()+2.56+double(level)*2.88,0});
    ASSERT_TRUE(scene.publish());
    // Ground plus eight separate catalog floors all fit a real standing actor.
    for(int level=0;level<8;++level) {
        const auto feet=scene.feet(.25,.25,scene.base()+2.89+double(level)*2.88);
        EXPECT_TRUE(scene.queries.clearCapsule(feet));
    }
    LayeredNavigation navigation;ASSERT_TRUE(navigation.configure({0,0,1,1,-1,30,-200}));
    ASSERT_TRUE(navigation.synchronize(scene.queries));
    ASSERT_TRUE(ready(navigation,scene.queries,4));
    const auto ground=scene.feet(.25,.25,1),top=scene.feet(.25,.25,30);
    EXPECT_EQ(navigation.request(scene.queries,ground,top),Status::Capacity);
    EXPECT_TRUE(navigation.path().points.empty());
}

TEST(AdventureNavigation, TerrainRebindAtTheSameRevisionStopsAnExistingFollowerBeforeItsNextTick) {
    Scene scene;LayeredNavigation navigation;
    ASSERT_TRUE(navigation.configure({-1,-1,2,2,-1,6,-200}));
    ASSERT_TRUE(navigation.synchronize(scene.queries));ASSERT_TRUE(ready(navigation,scene.queries));
    const auto start=scene.feet(-2.75,.25,1),goal=scene.feet(2.75,.25,1);
    ASSERT_EQ(find(navigation,scene.queries,start,goal),Status::Found);const auto path=navigation.path();
    AdventurePlayer actor;ASSERT_TRUE(actor.initialize(scene.queries,start,-200));
    NavigationFollower follower;ASSERT_TRUE(follower.begin(scene.queries,navigation,path,actor));

    // bindTerrain accepts a new borrowed heightfield without publishing solids
    // or increasing the existing static-packet revision. The buffer stays alive.
    std::vector<uint16_t> replacement(scene.samples.size(),34000);
    auto terrain=scene.terrain;terrain.samples=replacement;
    const auto revision=scene.queries.revision();const auto before=actor.state();
    ASSERT_TRUE(scene.queries.bindTerrain(terrain));EXPECT_EQ(scene.queries.revision(),revision);
    EXPECT_FALSE(navigation.valid(scene.queries,path));
    EXPECT_EQ(follower.advance(scene.queries,navigation,actor),FollowStatus::Stale);
    EXPECT_EQ(actor.tick(),before.tick);EXPECT_EQ(actor.feet(),before.feet);
    EXPECT_EQ(navigation.request(scene.queries,start,goal),Status::Stale);
    const auto work=navigation.advance(scene.queries,64,32);
    EXPECT_EQ(work.status,Status::Stale);EXPECT_EQ(work.columns,0u);EXPECT_EQ(work.expansions,0u);
    EXPECT_EQ(work.controllerTicks,0u);

    ASSERT_TRUE(navigation.synchronize(scene.queries));ASSERT_TRUE(ready(navigation,scene.queries));
    EXPECT_FALSE(navigation.valid(scene.queries,path));
    EXPECT_EQ(actor.tick(),before.tick);EXPECT_EQ(actor.feet(),before.feet);
}

TEST(AdventureNavigation, FollowerRejectsAnotherActorsGeometryOrWaterPolicyEvenWithAValidNavQuery) {
    Scene scene;LayeredNavigation navigation;
    ASSERT_TRUE(navigation.configure({-1,-1,2,2,-1,6,-200}));
    ASSERT_TRUE(navigation.synchronize(scene.queries));ASSERT_TRUE(ready(navigation,scene.queries));
    const auto start=scene.feet(-2.75,.25,1),goal=scene.feet(2.75,.25,1);
    ASSERT_EQ(find(navigation,scene.queries,start,goal),Status::Found);const auto path=navigation.path();
    ASSERT_TRUE(navigation.valid(scene.queries,path));
    AdventureSpatialQueries other;ASSERT_TRUE(other.bindTerrain(scene.terrain));ASSERT_TRUE(other.publish({},1));

    for(const bool differentGeometry:{true,false}) {
        auto& actorQueries=differentGeometry?other:scene.queries;
        const double actorWater=differentGeometry?-200:-199;
        AdventurePlayer actor;ASSERT_TRUE(actor.initialize(actorQueries,start,actorWater));
        const auto before=actor.state();NavigationFollower refused;
        // The navigation's query argument is deliberately correct. The mismatch
        // lives inside AdventurePlayer and must be checked separately.
        EXPECT_FALSE(refused.begin(scene.queries,navigation,path,actor));
        EXPECT_EQ(actor.tick(),before.tick);EXPECT_EQ(actor.feet(),before.feet);

        ASSERT_TRUE(actor.initialize(scene.queries,start,-200));NavigationFollower follower;
        ASSERT_TRUE(follower.begin(scene.queries,navigation,path,actor));
        ASSERT_TRUE(actor.initialize(actorQueries,start,actorWater));const auto rebound=actor.state();
        EXPECT_EQ(follower.advance(scene.queries,navigation,actor),FollowStatus::Stale);
        EXPECT_EQ(actor.tick(),rebound.tick);EXPECT_EQ(actor.feet(),rebound.feet);
        EXPECT_TRUE(navigation.valid(scene.queries,path));
    }
}
