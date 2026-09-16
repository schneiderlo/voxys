#include "game/adventure/field_home_access.hpp"
#include "game/adventure/adventure_player.hpp"
#include "game/adventure/construction_policy.hpp"
#include <gtest/gtest.h>
#include <algorithm>
#include <cmath>

using namespace voxy::game::adventure;
namespace {
using Solid=AdventureSpatialQueries::Solid;
glm::dvec3 feet(PlayerPose pose){return {pose.x,pose.y,pose.z};}
GridPosition grid(double x,double y,double z){return {int32_t(std::round(x*50)),int32_t(std::round(y*50)),int32_t(std::round(z*50))};}
struct Home {
    std::vector<uint16_t> samples=std::vector<uint16_t>(64*64,32768);
    voxy::terrain::lego::Surface terrain{samples,64,64,8.f,1.f};
    AdventureSpatialQueries queries;
    std::unique_ptr<AdventureSession> session;
    std::string error;
    uint64_t structure=0,bed=0;
    CommandStamp stamp() const {return {session->state().revision,session->state().lastRequestSequence+1,1};}
    CandidateValidator validator() {
        return [&](const auto& before,const auto& after,std::string& reason){return validateConstruction(before,after,queries,reason);};
    }
    bool publish() {
        std::vector<Solid> solids;return compileSolids(session->state(),solids,error)&&queries.publish(solids,queries.revision()+1);
    }
    bool create() {
        if(!queries.bindTerrain(terrain)||!queries.publish({},1))return false;
        voxy::game::construction::WorldNamespace world;world.bytes[0]=71;
        AdventureContent content;content.identity.bytes[0]=std::byte{13};content.town={0,.185,8,0};
        session=AdventureSession::create(world,content,error);if(!session)return false;
        const auto layout=starterRoomLayout({},0,error);
        auto change=session->prepareBlueprint(stamp(),layout,validator(),error);
        if(!change||!session->commit(std::move(*change),error)||!publish())return false;
        structure=session->state().structures.front().id;
        for(const auto& component:session->state().components)if(component.kind==FurnitureKind::Bed)bed=component.id;
        return bed!=0;
    }
    bool remove(uint64_t part) {
        auto change=session->prepareRemove(stamp(),part,validator(),error);
        return change&&session->commit(std::move(*change),error)&&publish();
    }
    bool annex(bool sealed) {
        std::vector<uint64_t> removed;
        for(const auto& component:session->state().components)if(component.kind!=FurnitureKind::Bed)removed.push_back(component.part);
        for(const auto part:removed)if(!remove(part))return false;
        std::vector<PlacePart> layout;
        const auto add=[&](PieceKind kind,double x,double y,double z,uint8_t yaw=0) {
            layout.push_back({structure,kind,grid(x,y,z),yaw,0});
        };
        // A real connected annex: these foundations touch the paid starter
        // floor. The only change between the two layouts is one catalog wall
        // versus an aligned doorway at the existing room's southern door.
        for(double x:{-1.,1.})for(double z:{3.,5.})add(PieceKind::Foundation,x,0,z);
        for(double z:{3.,5.})for(double x:{-2.16,2.16})add(PieceKind::Wall,x,.32,z,1);
        for(double x:{-1.,1.})add(PieceKind::Wall,x,.32,5.84);
        add(sealed?PieceKind::Wall:PieceKind::Doorway,-1,.32,2.16);
        add(PieceKind::Wall,1,.32,2.16);
        add(PieceKind::Chest,-1,.32,5);add(PieceKind::Workbench,1,.32,5);
        auto change=session->prepareBlueprint(stamp(),layout,validator(),error);
        return change&&session->commit(std::move(*change),error)&&publish();
    }
    AdventureState selectedBed() const {
        // This is the same private selection allowed by the helper contract.
        // The player's real saved recovery registration stays unchanged.
        auto state=session->state();state.registeredBed=bed;return state;
    }
};
bool walk(AdventurePlayer& actor,glm::dvec2 target) {
    for(int tick=0;tick<240;++tick) {
        const auto before=actor.feet();const auto delta=target-glm::dvec2(before.x,before.z);const double distance=glm::length(delta);
        if(distance<.008)return actor.mode()==AdventurePlayer::Mode::Walking;
        actor.advance(AdventurePlayer::fixedStep,{delta*(std::min(1.,distance/.06)/distance),false});
        if(actor.mode()!=AdventurePlayer::Mode::Walking||glm::length(actor.feet()-before)<1e-8)return false;
    }
    return false;
}
uint64_t furniture(const AdventureState& state,FurnitureKind kind) {
    for(const auto& component:state.components)if(component.kind==kind)return component.id;
    return 0;
}
bool observedCall(const AdventureState& state,const AdventureSpatialQueries& queries,uint64_t structure,
    FurnitureKind kind,FieldHomeAccessWork& work) {
    const auto before=state;const auto revision=queries.revision();const auto terrain=queries.terrain();
    const std::vector<uint16_t> samples(terrain.samples.begin(),terrain.samples.end());
    const std::vector<Solid> solids(queries.solids().begin(),queries.solids().end());
    const bool result=fieldFurnitureAccessible(state,queries,structure,kind,&work);
    EXPECT_EQ(state,before);EXPECT_EQ(queries.revision(),revision);EXPECT_EQ(queries.terrain().samples.data(),terrain.samples.data());
    EXPECT_TRUE(std::equal(samples.begin(),samples.end(),queries.terrain().samples.begin()));
    EXPECT_EQ(queries.solidCount(),solids.size());
    for(size_t i=0;i<std::min(solids.size(),queries.solidCount());++i) {
        EXPECT_EQ(queries.solids()[i].structure,solids[i].structure);EXPECT_EQ(queries.solids()[i].part,solids[i].part);
        EXPECT_EQ(queries.solids()[i].minimum,solids[i].minimum);EXPECT_EQ(queries.solids()[i].maximum,solids[i].maximum);
    }
    EXPECT_LE(work.bedChecks,work.maximumBedChecks);EXPECT_LE(work.furnitureChecks,work.maximumFurnitureChecks);
    EXPECT_LE(work.approaches,work.maximumApproaches);EXPECT_LE(work.sampledColumns,work.maximumSampledColumns);
    EXPECT_LE(work.controllerTicks,work.maximumControllerTicks);EXPECT_LE(work.navigationSteps,work.maximumNavigationSteps);
    return result;
}
}

TEST(AdventureFieldHomeAccess, PaidStarterRoomConnectsTheSameUsableBedToChestAndWorkbench) {
    Home home;ASSERT_TRUE(home.create())<<home.error;
    EXPECT_EQ(home.session->state().structures.front().parts.size(),19u);
    EXPECT_EQ(itemCount(home.session->state().backpack,ItemKind::Wood),570u);
    const auto state=home.selectedBed();PlayerPose recovery;std::string error;
    ASSERT_TRUE(usableBed(state,home.bed,home.queries,recovery,error))<<error;
    AdventurePlayer actor;ASSERT_TRUE(actor.initialize(home.queries,feet(recovery),-200));
    EXPECT_TRUE(home.queries.clearCapsule(actor.feet()));FieldHomeAccessWork work;
    EXPECT_TRUE(observedCall(state,home.queries,home.structure,FurnitureKind::Chest,work));
    EXPECT_TRUE(observedCall(state,home.queries,home.structure,FurnitureKind::Workbench,work));
    EXPECT_FALSE(work.exhausted);EXPECT_EQ(work.bedChecks,2u);EXPECT_EQ(home.session->state().registeredBed,0u);
}

TEST(AdventureFieldHomeAccess, OpenPaidAnnexRequiresContinuousWalkingThroughTheActualDoorway) {
    Home home;ASSERT_TRUE(home.create())<<home.error;ASSERT_TRUE(home.annex(false))<<home.error;
    auto state=home.selectedBed();PlayerPose recovery;std::string error;
    ASSERT_TRUE(usableBed(state,home.bed,home.queries,recovery,error))<<error;
    auto observer=state;observer.player=recovery;
    EXPECT_FALSE(reachableComponent(observer,furniture(state,FurnitureKind::Chest),home.queries,error));
    EXPECT_FALSE(reachableComponent(observer,furniture(state,FurnitureKind::Workbench),home.queries,error));
    AdventurePlayer actor;ASSERT_TRUE(actor.initialize(home.queries,feet(recovery),-200));
    ASSERT_TRUE(walk(actor,{-1,.5}));ASSERT_TRUE(walk(actor,{-1,3.5}));
    observer.player={actor.feet().x,actor.feet().y,actor.feet().z,actor.facingYaw()};
    EXPECT_TRUE(reachableComponent(observer,furniture(state,FurnitureKind::Chest),home.queries,error))<<error;
    EXPECT_TRUE(reachableComponent(observer,furniture(state,FurnitureKind::Workbench),home.queries,error))<<error;
    FieldHomeAccessWork work;
    EXPECT_TRUE(observedCall(state,home.queries,home.structure,FurnitureKind::Chest,work));
    EXPECT_TRUE(observedCall(state,home.queries,home.structure,FurnitureKind::Workbench,work));
    EXPECT_GT(work.controllerTicks,0u);EXPECT_FALSE(work.exhausted);
}

TEST(AdventureFieldHomeAccess, SealedPaidAnnexDoesNotBecomeUsableFromHypotheticalFarSidePoses) {
    Home home;ASSERT_TRUE(home.create())<<home.error;ASSERT_TRUE(home.annex(true))<<home.error;
    const auto state=home.selectedBed();PlayerPose recovery;std::string error;
    ASSERT_TRUE(usableBed(state,home.bed,home.queries,recovery,error))<<error;
    AdventurePlayer actor;ASSERT_TRUE(actor.initialize(home.queries,feet(recovery),-200));
    ASSERT_TRUE(walk(actor,{-1,.5}));EXPECT_FALSE(walk(actor,{-1,3.5}));EXPECT_LT(actor.feet().z,2.);
    FieldHomeAccessWork work;
    EXPECT_FALSE(observedCall(state,home.queries,home.structure,FurnitureKind::Chest,work));
    EXPECT_FALSE(observedCall(state,home.queries,home.structure,FurnitureKind::Workbench,work));
    // Geometry still contains both actual furniture components and the bed
    // remains sheltered; refusal must come from physical access, not absence.
    EXPECT_NE(furniture(state,FurnitureKind::Chest),0u);EXPECT_NE(furniture(state,FurnitureKind::Workbench),0u);
    EXPECT_TRUE(usableBed(state,home.bed,home.queries,recovery,error));
}

TEST(AdventureFieldHomeAccess, ForeignFurnitureAndStalePublishedShelterCannotBorrowReadiness) {
    Home home;ASSERT_TRUE(home.create())<<home.error;const auto owned=home.selectedBed();
    auto foreign=owned;
    for(auto& component:foreign.components)if(component.kind==FurnitureKind::Chest)component.owner=2;
    FieldHomeAccessWork foreignWork;
    EXPECT_FALSE(observedCall(foreign,home.queries,home.structure,FurnitureKind::Chest,foreignWork));
    EXPECT_TRUE(observedCall(foreign,home.queries,home.structure,FurnitureKind::Workbench,foreignWork));
    foreign=owned;foreign.structures.front().owner=2;FieldHomeAccessWork otherHome;
    EXPECT_FALSE(observedCall(foreign,home.queries,home.structure,FurnitureKind::Chest,otherHome));
    foreign=owned;
    for(auto& component:foreign.components)if(component.kind==FurnitureKind::Bed)component.owner=2;
    FieldHomeAccessWork otherBed;
    EXPECT_FALSE(observedCall(foreign,home.queries,home.structure,FurnitureKind::Workbench,otherBed));
    const auto roof=std::find_if(owned.structures.front().parts.begin(),owned.structures.front().parts.end(),[](const auto& part){return part.kind==PieceKind::Roof;});
    ASSERT_NE(roof,owned.structures.front().parts.end());ASSERT_TRUE(home.remove(roof->id))<<home.error;
    FieldHomeAccessWork stale;
    EXPECT_FALSE(observedCall(owned,home.queries,home.structure,FurnitureKind::Chest,stale));
}

TEST(AdventureFieldHomeAccess, OneSharedAllowanceAccumulatesAcrossFurnitureAndFailsClosedAtExhaustion) {
    Home home;ASSERT_TRUE(home.create())<<home.error;const auto state=home.selectedBed();FieldHomeAccessWork shared;
    for(size_t pair=0;pair<FieldHomeAccessWork::maximumBedChecks/2;++pair) {
        ASSERT_TRUE(observedCall(state,home.queries,home.structure,FurnitureKind::Chest,shared));
        ASSERT_TRUE(observedCall(state,home.queries,home.structure,FurnitureKind::Workbench,shared));
    }
    EXPECT_EQ(shared.bedChecks,shared.maximumBedChecks);EXPECT_FALSE(shared.exhausted);
    EXPECT_FALSE(observedCall(state,home.queries,home.structure,FurnitureKind::Chest,shared));EXPECT_TRUE(shared.exhausted);
    const auto spent=shared.controllerTicks;
    EXPECT_FALSE(observedCall(state,home.queries,home.structure,FurnitureKind::Workbench,shared));EXPECT_EQ(shared.controllerTicks,spent);
    ASSERT_TRUE(home.annex(false))<<home.error;const auto remote=home.selectedBed();FieldHomeAccessWork limited;
    limited.controllerTicks=limited.maximumControllerTicks-1;
    EXPECT_FALSE(observedCall(remote,home.queries,home.structure,FurnitureKind::Chest,limited));
    EXPECT_TRUE(limited.exhausted);EXPECT_EQ(limited.controllerTicks,limited.maximumControllerTicks);
    EXPECT_FALSE(observedCall(remote,home.queries,home.structure,FurnitureKind::Workbench,limited));
}
