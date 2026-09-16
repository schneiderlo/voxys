#include "game/adventure/building_doors.hpp"
#include "game/adventure/adventure_player.hpp"
#include "game/adventure/construction_policy.hpp"
#include "game/adventure/village_layout.hpp"
#include "game/adventure/trail_sites.hpp"
#include "game/adventure/world_definition.hpp"
#include <gtest/gtest.h>
#include <algorithm>
#include <cmath>
#include <numbers>
#include <glm/gtc/matrix_transform.hpp>

namespace voxy::game::adventure {
namespace {
using Solid=AdventureSpatialQueries::Solid;
glm::dvec3 rotate(glm::dvec3 p,uint8_t yaw) {
    for(uint8_t i=0;i<yaw;++i)p={p.z,p.y,-p.x};
    return p;
}
GridPosition grid(glm::dvec3 p) {return {int32_t(std::round(p.x*50)),int32_t(std::round(p.y*50)),int32_t(std::round(p.z*50))};}
bool overlap(const Solid& a,const Solid& b) {
    return glm::all(glm::lessThan(a.minimum,b.maximum-glm::dvec3(1e-7)))
        &&glm::all(glm::greaterThan(a.maximum,b.minimum+glm::dvec3(1e-7)));
}
void sameSolids(std::span<const Solid> actual,std::span<const Solid> expected) {
    ASSERT_EQ(actual.size(),expected.size());
    for(size_t i=0;i<actual.size();++i) {
        EXPECT_EQ(actual[i].part,expected[i].part);EXPECT_EQ(actual[i].structure,expected[i].structure);
        EXPECT_EQ(actual[i].minimum,expected[i].minimum);EXPECT_EQ(actual[i].maximum,expected[i].maximum);
    }
}
bool walk(AdventurePlayer& player,glm::dvec2 goal) {
    for(int i=0;i<180;++i) {
        const auto before=player.feet();const auto delta=goal-glm::dvec2(before.x,before.z);const double distance=glm::length(delta);
        if(distance<.008)return player.mode()==AdventurePlayer::Mode::Walking;
        player.advance(AdventurePlayer::fixedStep,{delta*(std::min(1.,distance/.06)/distance),false});
        if(player.mode()!=AdventurePlayer::Mode::Walking||glm::length(player.feet()-before)<1e-8)return false;
    }
    return false;
}
struct Scene {
    std::vector<uint16_t> samples=std::vector<uint16_t>(64*64,32768);
    voxy::terrain::lego::Surface terrain{samples,64,64,8.f,1.f};
    AdventureContent content;AdventureSpatialQueries queries;
    std::unique_ptr<AdventureSession> session;std::string error;uint8_t yaw=0;
    bool create() {
        if(!queries.bindTerrain(terrain)||!queries.publish({},1))return false;
        content.identity.bytes[0]=std::byte{71};content.town={4,double(voxy::terrain::lego::supportHeight(terrain,{4,-3},.3f))+.005,-3,0};
        construction::WorldNamespace world;world.bytes[0]=94;
        session=AdventureSession::create(world,content,error);return bool(session);
    }
    CommandStamp stamp() const {return {session->state().revision,session->state().lastRequestSequence+1,1};}
    CandidateValidator validator() {return [&](const auto& before,const auto& after,std::string& reason){return validateConstruction(before,after,queries,reason);};}
    bool publish(std::span<const Solid> extra={}) {
        std::vector<Solid> solids;if(!compileSolids(session->state(),solids,error))return false;
        solids.insert(solids.end(),extra.begin(),extra.end());AdventureSpatialQueries next;
        if(!next.bindTerrain(terrain)||!next.publish(solids,queries.revision()+1))return false;
        queries=std::move(next);return true;
    }
    bool commit(std::optional<AdventureSession::PreparedChange> change) {
        if(!change)return false;
        std::vector<Solid> solids;if(!compileSolids(change->state(),solids,error))return false;
        AdventureSpatialQueries next;
        if(!next.bindTerrain(terrain)||!next.publish(solids,queries.revision()+1)||!session->commit(std::move(*change),error))return false;
        queries=std::move(next);return true;
    }
    bool build(uint8_t angle=0,PieceKind kind=PieceKind::HingedDoor) {
        yaw=angle;std::vector<PlacePart> pieces;
        for(double z:{-2.,0.,2.})pieces.push_back({0,PieceKind::Foundation,grid(rotate({0,0,z},yaw)),yaw,0});
        pieces.push_back({0,kind,{0,16,0},yaw,0});
        return commit(session->prepareBlueprint(stamp(),pieces,validator(),error));
    }
    bool player(glm::dvec3 local) {
        auto state=session->state();const auto p=rotate(local,yaw);state.player={p.x,p.y,p.z,0};
        auto next=AdventureSession::restore(state,content,error);if(!next)return false;session=std::move(next);return true;
    }
    const StructureComponent& door() const {return session->state().components.front();}
    std::optional<AdventureSession::PreparedChange> prepare(bool open) {
        const auto& component=door();return session->prepareSetDoorOpen(stamp(),component.id,open,component.revision,validator(),error);
    }
};
}

TEST(AdventureDoorGeometry, SharedRigidRecipeAndEnvelopesCoverTheEntireQuarterTurn) {
    const auto* frame=buildingDefinition(PieceKind::Doorway);const auto* door=buildingDefinition(PieceKind::HingedDoor);
    ASSERT_NE(frame,nullptr);ASSERT_NE(door,nullptr);ASSERT_EQ(frame->solids.size(),3u);ASSERT_EQ(door->solids.size(),4u);
    for(size_t i=0;i<3;++i)EXPECT_EQ(frame->solids[i],door->solids[i]);
    EXPECT_EQ(door->solids[3],kClosedDoorLeaf);EXPECT_EQ(door->cost,(MaterialCost{6,0,2}));
    EXPECT_EQ(frame->furniture,FurnitureKind::None);EXPECT_EQ(door->furniture,FurnitureKind::Door);
    EXPECT_EQ(doorSweepBoxes().size(),16u);
    const glm::dvec3 hinge=glm::dvec3(kDoorHinge.x,kDoorHinge.y,kDoorHinge.z)*.02;
    for(bool open:{false,true}) {
        glm::dvec3 minimum(INFINITY),maximum(-INFINITY);
        for(int x:{-32,32})for(int y:{2,110})for(int z:{-2,2}) {
            const auto point=glm::dvec3(doorLeafTransform(open)*glm::dvec4(double(x)*.02,double(y)*.02,double(z)*.02,1));
            minimum=glm::min(minimum,point);maximum=glm::max(maximum,point);
        }
        const auto box=doorLeafBox(open);
        for(int axis=0;axis<3;++axis) {
            const glm::dvec3 low(box.minimum.x,box.minimum.y,box.minimum.z),high(box.maximum.x,box.maximum.y,box.maximum.z);
            EXPECT_NEAR(minimum[axis],low[axis]*.02,1e-12);EXPECT_NEAR(maximum[axis],high[axis]*.02,1e-12);
        }
    }
    for(int step=0;step<=720;++step) {
        const double angle=-std::numbers::pi*double(step)/1440;
        const auto transform=glm::translate(glm::dmat4(1),hinge)*glm::rotate(glm::dmat4(1),angle,glm::dvec3(0,1,0))*glm::translate(glm::dmat4(1),-hinge);
        for(int x:{-32,32})for(int z:{-2,2}) {
            const auto point=glm::dvec3(transform*glm::dvec4(double(x)*.02,1.16,double(z)*.02,1));
            EXPECT_TRUE(std::any_of(doorSweepBoxes().begin(),doorSweepBoxes().end(),[&](const auto& box) {
                const glm::dvec3 low=glm::dvec3(box.minimum.x,box.minimum.y,box.minimum.z)*.02;
                const glm::dvec3 high=glm::dvec3(box.maximum.x,box.maximum.y,box.maximum.z)*.02;
                return glm::all(glm::greaterThanEqual(point,low-glm::dvec3(1e-10)))&&glm::all(glm::lessThanEqual(point,high+glm::dvec3(1e-10)));
            }));
        }
    }
}

TEST(AdventureDoorGeometry, PaidDoorPublishesOneLeafForWalkingCameraAndPickingAtEveryYaw) {
    for(uint8_t yaw=0;yaw<4;++yaw) {
        SCOPED_TRACE(int(yaw));Scene scene;ASSERT_TRUE(scene.create())<<scene.error;
        const auto stock=scene.session->state().backpack;ASSERT_TRUE(scene.build(yaw))<<scene.error;
        EXPECT_EQ(itemCount(scene.session->state().backpack,ItemKind::Wood)+6,itemCount(stock,ItemKind::Wood));
        EXPECT_EQ(itemCount(scene.session->state().backpack,ItemKind::Scrap)+2,itemCount(stock,ItemKind::Scrap));
        ASSERT_EQ(scene.queries.solidCount(),7u);ASSERT_TRUE(scene.player({0,.325,-1.8}))<<scene.error;
        const std::vector<Solid> closed(scene.queries.solids().begin(),scene.queries.solids().end());
        const auto from=rotate({0,.325,-1.3},yaw),to=rotate({0,.325,1.3},yaw);
        auto ray=scene.queries.raycast(from+glm::dvec3(0,1,0),to-from,2.6);
        ASSERT_TRUE(ray.complete&&ray.hit);EXPECT_EQ(ray.part.counter,scene.door().part);
        EXPECT_TRUE(scene.queries.sweepSphere(from+glm::dvec3(0,1,0),to+glm::dvec3(0,1,0),.18).hit);
        AdventurePlayer blocked;ASSERT_TRUE(blocked.initialize(scene.queries,from,-200));EXPECT_FALSE(walk(blocked,{to.x,to.z}));
        const auto paid=scene.session->state().backpack;
        ASSERT_TRUE(scene.commit(scene.prepare(true)))<<scene.error;EXPECT_TRUE(scene.door().doorOpen);
        ASSERT_EQ(scene.queries.solidCount(),7u);sameSolids(scene.queries.solids().first(6),std::span(closed).first(6));
        EXPECT_EQ(scene.session->state().backpack,paid);
        ray=scene.queries.raycast(from+glm::dvec3(0,1,0),to-from,2.6);EXPECT_TRUE(ray.complete);EXPECT_FALSE(ray.hit);
        const auto camera=scene.queries.sweepSphere(from+glm::dvec3(0,1,0),to+glm::dvec3(0,1,0),.18);
        EXPECT_TRUE(camera.complete);EXPECT_FALSE(camera.hit);
        AdventurePlayer walking;ASSERT_TRUE(walking.initialize(scene.queries,from,-200));
        EXPECT_TRUE(walk(walking,{to.x,to.z}));EXPECT_TRUE(walk(walking,{from.x,from.z}));
        ASSERT_TRUE(scene.commit(scene.prepare(false)))<<scene.error;EXPECT_FALSE(scene.door().doorOpen);
        sameSolids(scene.queries.solids(),closed);EXPECT_EQ(scene.session->state().backpack,paid);
        EXPECT_TRUE(scene.queries.sweepCapsule(from,to).hit);
    }
}

TEST(AdventureDoorGeometry, SweptOccupancyRefusesEvenWhenBothEndpointPosesAreEmpty) {
    Scene scene;ASSERT_TRUE(scene.create());ASSERT_TRUE(scene.build())<<scene.error;
    ASSERT_TRUE(scene.player({.25,.325,.65}));ASSERT_TRUE(scene.queries.clearCapsule({.25,.325,.65}));
    auto open=scene.session->state();open.components.front().doorOpen=true;std::vector<Solid> openSolids;
    ASSERT_TRUE(compileSolids(open,openSolids,scene.error));AdventureSpatialQueries endpoint;
    ASSERT_TRUE(endpoint.bindTerrain(scene.terrain));ASSERT_TRUE(endpoint.publish(openSolids,1));
    ASSERT_TRUE(endpoint.clearCapsule({.25,.325,.65}));
    const auto before=scene.session->state();const auto revision=scene.queries.revision();
    const std::vector<Solid> geometry(scene.queries.solids().begin(),scene.queries.solids().end());
    EXPECT_FALSE(scene.prepare(true));EXPECT_NE(scene.error.find("swing"),std::string::npos);
    EXPECT_EQ(scene.session->state(),before);EXPECT_EQ(scene.queries.revision(),revision);sameSolids(scene.queries.solids(),geometry);
    ASSERT_TRUE(scene.player({0,.325,-1.8}));
    const auto world=scene.session->state().world;
    const Solid actor{{world,1},{world,UINT64_MAX-90},{.20,.6,.60},{.30,1.6,.70}};
    for(const auto& solid:openSolids)EXPECT_FALSE(overlap(actor,solid));
    for(const auto& solid:geometry)EXPECT_FALSE(overlap(actor,solid));
    ASSERT_TRUE(scene.publish(std::span(&actor,1)));
    const auto occupied=scene.session->state();EXPECT_FALSE(scene.prepare(true));EXPECT_EQ(scene.session->state(),occupied);
    EXPECT_NE(scene.error.find("character or scenery"),std::string::npos);
    Scene placing;ASSERT_TRUE(placing.create());
    auto blockingActor=actor;blockingActor.part.world=blockingActor.structure.world=placing.session->state().world;
    ASSERT_TRUE(placing.publish(std::span(&blockingActor,1)));const auto unbuilt=placing.session->state();
    EXPECT_FALSE(placing.build());EXPECT_EQ(placing.session->state(),unbuilt);
    EXPECT_NE(placing.error.find("character or scenery"),std::string::npos);
    ASSERT_TRUE(scene.publish());ASSERT_TRUE(scene.commit(scene.prepare(true)))<<scene.error;
    ASSERT_TRUE(scene.player({0,.325,0}));ASSERT_TRUE(scene.queries.clearCapsule({0,.325,0}));
    const auto doorwayOccupied=scene.session->state();EXPECT_FALSE(scene.prepare(false));EXPECT_EQ(scene.session->state(),doorwayOccupied);
}

TEST(AdventureDoorGeometry, FutureSwingReservationRefusesBuildingButAllowsClearGroundedAdditions) {
    Scene scene;ASSERT_TRUE(scene.create());ASSERT_TRUE(scene.build())<<scene.error;ASSERT_TRUE(scene.player({0,.325,-1.8}));
    const auto before=scene.session->state();const uint64_t structure=before.structures.front().id;
    auto blocked=scene.session->preparePlace(scene.stamp(),{structure,PieceKind::Pier,{12,16,36},0,0},scene.validator(),scene.error);
    EXPECT_FALSE(blocked);EXPECT_NE(scene.error.find("swing"),std::string::npos);EXPECT_EQ(scene.session->state(),before);
    auto clear=scene.session->preparePlace(scene.stamp(),{structure,PieceKind::Pier,{-32,16,120},0,0},scene.validator(),scene.error);
    ASSERT_TRUE(scene.commit(std::move(clear)))<<scene.error;
    ASSERT_TRUE(scene.commit(scene.prepare(true)))<<scene.error;
    std::vector<Solid> reservation;ASSERT_TRUE(compileDoorSwingSolids(scene.session->state(),reservation,scene.error));
    ASSERT_EQ(reservation.size(),16u);for(const auto& solid:reservation)EXPECT_EQ(solid.part.counter,scene.door().part);
}

TEST(AdventureDoorGeometry, MovingLeafCannotSupportAnotherPieceAndHandleSightIsRequired) {
    Scene scene;ASSERT_TRUE(scene.create());ASSERT_TRUE(scene.build())<<scene.error;ASSERT_TRUE(scene.player({0,.325,-1.8}));
    ASSERT_TRUE(scene.commit(scene.prepare(true)))<<scene.error;
    const auto before=scene.session->state();const auto structure=before.structures.front().id;
    // This brick touches the open leaf's top, beyond the frame and any floor.
    auto floating=scene.session->preparePlace(scene.stamp(),{structure,PieceKind::Brick1x2,{-32,126,50},0,0},scene.validator(),scene.error);
    EXPECT_FALSE(floating);EXPECT_NE(scene.error.find("grounded foundation"),std::string::npos);EXPECT_EQ(scene.session->state(),before);
    ASSERT_TRUE(scene.commit(scene.prepare(false)))<<scene.error;
    const auto world=scene.session->state().world;
    const Solid wall{{world,1},{world,UINT64_MAX-91},{-1,.33,-1.1},{1,2.5,-.9}};
    ASSERT_TRUE(scene.publish(std::span(&wall,1)));
    EXPECT_FALSE(reachableComponent(scene.session->state(),scene.door().id,scene.queries,scene.error));
    EXPECT_FALSE(scene.prepare(true));EXPECT_NE(scene.error.find("blocked"),std::string::npos);
}

TEST(AdventureDoorGeometry, TerrainInsideFutureSwingRefusesPlacementBeforeSpending) {
    Scene scene;scene.samples.assign(256*256,32768);scene.terrain={scene.samples,256,256,8.f,.25f};
    const auto origin=scene.terrain.origin();
    const int x=int(std::floor((.2+double(origin.x))/.25)),z=int(std::floor((.7+double(origin.y))/.25));
    scene.samples[size_t(z)*256+size_t(x)]=38010; // A real 1.28 m column; no collider proxy.
    ASSERT_TRUE(scene.create());const auto before=scene.session->state();
    const std::array<PlacePart,3> pieces{{{0,PieceKind::Pier,{-42,0,0},0,0},{0,PieceKind::Pier,{42,0,0},0,0},
        {0,PieceKind::HingedDoor,{0,48,0},0,0}}};
    EXPECT_FALSE(scene.session->prepareBlueprint(scene.stamp(),pieces,scene.validator(),scene.error));
    EXPECT_NE(scene.error.find("level ground"),std::string::npos);EXPECT_EQ(scene.session->state(),before);
}

TEST(AdventureDoorGeometry, OldOpenDoorwaysStayHollowAndMalformedDoorStateKeepsOutputIntact) {
    Scene legacy;ASSERT_TRUE(legacy.create());ASSERT_TRUE(legacy.build(0,PieceKind::Doorway))<<legacy.error;
    EXPECT_TRUE(legacy.session->state().components.empty());EXPECT_EQ(legacy.queries.solidCount(),6u);
    EXPECT_TRUE(legacy.queries.clearCapsule({0,.325,0}));
    std::vector<Solid> reservation(1);ASSERT_TRUE(compileDoorSwingSolids(legacy.session->state(),reservation,legacy.error));EXPECT_TRUE(reservation.empty());
    Scene scene;ASSERT_TRUE(scene.create());ASSERT_TRUE(scene.build())<<scene.error;
    const std::vector<Solid> before(scene.queries.solids().begin(),scene.queries.solids().end());auto output=before;
    auto broken=scene.session->state();broken.components.clear();
    EXPECT_FALSE(compileSolids(broken,output,scene.error));sameSolids(output,before);
    broken=scene.session->state();broken.components.push_back(broken.components.front());
    EXPECT_FALSE(compileSolids(broken,output,scene.error));sameSolids(output,before);
    // A saved player in the future swing does not invalidate a closed door.
    // Only operating it requires that player to step aside.
    ASSERT_TRUE(scene.player({.25,.325,.65}));
    EXPECT_TRUE(validateInstalledGeometry(scene.session->state(),scene.queries,scene.error))<<scene.error;
}

TEST(AdventureDoorGeometry, DoorSwingPreservesVillageAndTrailApproachesAcrossChanges) {
    std::vector<uint16_t> samples(256*2304,32768);
    const voxy::terrain::lego::Surface terrain{samples,256,2304,8.f,1.f};
    AdventureState before;before.world.bytes[0]=57;const auto town=townSpawn(terrain);before.player={town.x,town.y,town.z,0};
    AdventureContent content;content.town=before.player;AdventureSpatialQueries base;
    ASSERT_TRUE(base.bindTerrain(terrain));ASSERT_TRUE(base.publish({},1));
    const auto village=VillageLayout::admit(before,content,base,{});
    const auto trail=TrailSites::admit(before,content,base);
    const auto check=[&](const auto& layout) {
        bool proved=false;std::string error;
        // At most four sides of eight existing groups. Find a real installed
        // approach where the narrow closed frame fits but its inward arc does
        // not; no fake scenery envelope or mutation of the installed layout.
        for(const auto& group:layout.groups())if(group.available&&!proved) {
            const auto center=(group.minimum+group.maximum)*.5;
            const std::array<glm::dvec3,4> anchors{{{center.x,.32,group.minimum.z-.96},
                {center.x,.32,group.maximum.z+.96},{group.minimum.x-.96,.32,center.z},
                {group.maximum.x+.96,.32,center.z}}};
            constexpr std::array<uint8_t,4> yaws{0,2,1,3};
            for(size_t i=0;i<anchors.size()&&!proved;++i) {
                auto frame=before;frame.structures.push_back({3,1,0,{},{{4,PieceKind::Doorway,grid(anchors[i]),yaws[i],0}}});frame.lastIssuedId=4;
                if(!layout.validateNewConstruction(before,frame,error))continue;
                auto closed=frame;closed.structures.front().parts.front().kind=PieceKind::HingedDoor;
                StructureComponent component;component.id=5;component.structure=3;component.part=4;component.kind=FurnitureKind::Door;
                closed.components.push_back(component);closed.lastIssuedId=5;
                if(layout.validateNewConstruction(before,closed,error))continue;
                const auto saved=closed;
                EXPECT_TRUE(layout.validateNewConstruction(closed,closed,error))<<error;EXPECT_EQ(closed,saved);
                auto opened=closed;opened.components.front().doorOpen=true;
                EXPECT_TRUE(buildingGeometryChanged(closed,opened,4));
                EXPECT_FALSE(layout.validateNewConstruction(closed,opened,error));EXPECT_EQ(closed,saved);
                proved=true;
            }
        }
        EXPECT_TRUE(proved);
    };
    check(village);check(trail);
}

TEST(FreeBuildGeometry, BricksCanStartOnTerrainButFloatingAndOverlappingPiecesAreRejected) {
    Scene scene;ASSERT_TRUE(scene.create())<<scene.error;
    scene.content.freeBuilding=true;
    scene.session=AdventureSession::create(scene.session->state().world,scene.content,scene.error);ASSERT_TRUE(scene.session);
    const CandidateValidator creative=[&](const auto& before,const auto& after,std::string& error){return validateConstruction(before,after,scene.queries,error,true);};
    const auto y=terrainPlacementHeight(PieceKind::Brick2x4,0,{0,0},scene.terrain);ASSERT_TRUE(y);
    const PlacePart place{0,PieceKind::Brick2x4,grid({0,*y,0}),0,0};
    auto change=scene.session->preparePlace(scene.stamp(),place,creative,scene.error);ASSERT_TRUE(change)<<scene.error;
    EXPECT_FALSE(validateConstruction(scene.session->state(),change->state(),scene.queries,scene.error));
    ASSERT_TRUE(scene.commit(std::move(change)))<<scene.error;
    EXPECT_FALSE(scene.session->preparePlace(scene.stamp(),place,creative,scene.error));
    auto floating=place;floating.position.y+=200;
    EXPECT_FALSE(scene.session->preparePlace(scene.stamp(),floating,creative,scene.error));
    auto stacked=place;stacked.position.y+=48;
    EXPECT_TRUE(scene.session->preparePlace(scene.stamp(),stacked,creative,scene.error))<<scene.error;
    EXPECT_TRUE(validateInstalledGeometry(scene.session->state(),scene.queries,scene.error,true))<<scene.error;
}
} // namespace voxy::game::adventure
