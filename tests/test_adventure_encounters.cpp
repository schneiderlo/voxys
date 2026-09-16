#include "game/adventure/adventure_encounters.hpp"
#include "game/adventure/adventure_player.hpp"
#include "game/adventure/world_definition.hpp"
#include <gtest/gtest.h>
#include <algorithm>
#include <bit>
#include <cstdlib>
#include <fstream>
#include <limits>

using namespace voxy::game::adventure;
namespace {
using Solid=AdventureSpatialQueries::Solid;
glm::dvec3 position(PlayerPose p){return {p.x,p.y,p.z};}
struct Scene {
    std::vector<uint16_t> samples=std::vector<uint16_t>(256*2048,32768);
    voxy::terrain::lego::Surface terrain{samples,256,2048,8.f,1.f};
    AdventureSpatialQueries base;
    AdventureContent content;
    AdventureState state;
    std::vector<Solid> solids;
    Scene() {
        state.world.bytes[0]=29;const auto town=townSpawn(terrain);
        state.player={town.x,town.y,town.z,.7};content.town=state.player;
        EXPECT_TRUE(base.bindTerrain(terrain));EXPECT_TRUE(base.publish({},1));
        std::string error;EXPECT_TRUE(defaultEncounterContent(base,content.encounters,error))<<error;
        initializeAdventureProgress(state,content);
    }
    void publish(){ASSERT_TRUE(base.publish(solids,base.revision()+1));}
    PlayerPose ground(double x,double z,double yaw=0) const {
        return {x,double(voxy::terrain::lego::supportHeight(terrain,{float(x),float(z)},.3f))+.005,z,yaw};
    }
    void positioned(size_t index,PlayerPose p) {
        auto& checkpoint=state.combat.encounters[index].checkpoint;
        checkpoint.positioned=true;checkpoint.pose=p;checkpoint.phase=EnemyPhase::Idle;
    }
    void homeAt(PlayerPose p,double halfWidth=2) {
        state.structures.push_back({9,1,0,{},{{10,PieceKind::Wall,{},0,0}}});
        solids.push_back({{state.world,9},{state.world,10},position(p)-glm::dvec3(halfWidth,.1,halfWidth),
            position(p)+glm::dvec3(halfWidth,2,halfWidth)});publish();
    }
};
bool walk(const AdventureSpatialQueries& queries,AdventurePlayer& player,glm::dvec2 target,int limit=200) {
    for(int tick=0;tick<limit;++tick) {
        const auto p=player.feet();const auto delta=target-glm::dvec2(p.x,p.z);const double distance=glm::length(delta);
        if(distance<.008)return player.mode()==AdventurePlayer::Mode::Walking;
        const double amount=std::min(1.,distance/(3.6*AdventurePlayer::fixedStep));
        player.advance(AdventurePlayer::fixedStep,{delta*(amount/distance),false});
        if(!queries.clearCapsule(player.feet())||player.mode()!=AdventurePlayer::Mode::Walking)return false;
    }
    return false;
}
}

TEST(AdventureEncounters, CanonicalRaidersUseAgreedCombatContractAndRealWalkingBodies) {
    Scene scene;const auto before=scene.state;const auto actors=AdventureEncounters::admit(scene.state,scene.base);
    ASSERT_EQ(encounterDefinitions().size(),2u);EXPECT_EQ(encounterDefinition(0),nullptr);EXPECT_EQ(encounterDefinition(3),nullptr);
    EXPECT_EQ(encounterDefinition(1)->maximumHealth,60);EXPECT_EQ(encounterDefinition(2)->maximumHealth,100);
    EXPECT_EQ(encounterDefinition(1)->damage,12);EXPECT_EQ(encounterDefinition(2)->damage,18);
    EXPECT_EQ(encounterDefinition(1)->loot,(ItemStack{ItemKind::Scrap,8}));
    EXPECT_EQ(encounterDefinition(2)->loot,(ItemStack{ItemKind::RelayCore,1}));EXPECT_TRUE(encounterDefinition(2)->grantsRelayCore);
    auto solids=scene.solids;ASSERT_TRUE(actors.appendSolids(scene.state.world,solids));ASSERT_EQ(solids.size(),2u);
    AdventureSpatialQueries published;ASSERT_TRUE(published.bindTerrain(scene.terrain));ASSERT_TRUE(published.publish(solids,1));
    for(const auto& actor:actors.entries()) {
        const auto* definition=encounterDefinition(actor.id);ASSERT_NE(definition,nullptr);
        EXPECT_EQ(definition->noticeRadius,7);EXPECT_EQ(definition->leashRadius,12);EXPECT_EQ(definition->reach,1.6);
        EXPECT_EQ(definition->attackWindup,.7);EXPECT_EQ(definition->recovery,.9);
        ASSERT_TRUE(actor.available);EXPECT_TRUE(actor.needsPositionCheckpoint);EXPECT_EQ(actor.health,definition->maximumHealth);
        EXPECT_EQ(glm::dvec2(actor.pose.x,actor.pose.z),definition->anchor);
        EXPECT_TRUE(scene.base.clearCapsule(position(actor.pose)));EXPECT_FALSE(published.clearCapsule(position(actor.pose)));
        EXPECT_EQ(actors.find(actor.id),&actor);EXPECT_TRUE(actors.occupied(position(actor.pose)));
        EXPECT_FALSE(actors.occupied(position(actor.pose),.3,1.7,actor.id));
        const auto& solid=solids[size_t(actor.id)-1];EXPECT_EQ(solid.part.counter,encounterPartId(actor.id));
        EXPECT_EQ(solid.structure.counter,encounterStructureId(actor.id));
        EXPECT_EQ(solid.minimum,position(actor.pose)-glm::dvec3(.3,0,.3));
        EXPECT_EQ(solid.maximum,position(actor.pose)+glm::dvec3(.3,1.7,.3));
        EXPECT_FALSE(protectedConstruction(solid.minimum,solid.maximum));
        AdventurePlayer player;ASSERT_TRUE(player.initialize(scene.base,position(actor.pose),-200));
        EXPECT_TRUE(walk(scene.base,player,{actor.pose.x+1,actor.pose.z}));
        EXPECT_TRUE(walk(scene.base,player,{actor.pose.x,actor.pose.z}));
    }
    EXPECT_EQ(scene.state,before);
}

TEST(AdventureEncounters, FreshActorUsesBoundedFallbackButSavedPoseNeverMovesAroundOldPlayer) {
    for(size_t index=0;index<kAdventureEncounterCount;++index) {
        Scene scene;scene.state.player=scene.content.encounters[index].spawn;
        const auto before=scene.state;const auto fresh=AdventureEncounters::admit(scene.state,scene.base);
        const auto* moved=fresh.find(uint32_t(index+1));ASSERT_TRUE(moved->available);
        const double offset=glm::length(position(moved->pose)-position(scene.state.player));
        EXPECT_GE(offset,.75);EXPECT_LT(offset,1.2);EXPECT_EQ(scene.state,before);
        std::vector<Solid> solids;ASSERT_TRUE(fresh.appendSolids(scene.state.world,solids));
        AdventureSpatialQueries queries;ASSERT_TRUE(queries.bindTerrain(scene.terrain));ASSERT_TRUE(queries.publish(solids,1));
        EXPECT_TRUE(queries.clearCapsule(position(scene.state.player)));EXPECT_TRUE(queries.clearCapsule(townSpawn(scene.terrain)));
        scene.positioned(index,scene.state.player);const auto saved=scene.state;
        const auto restored=AdventureEncounters::admit(scene.state,scene.base);
        EXPECT_FALSE(restored.find(uint32_t(index+1))->available);EXPECT_EQ(scene.state,saved);
        EXPECT_EQ(restored.find(uint32_t(index+1))->pose,scene.state.player);
    }
}

TEST(AdventureEncounters, OldHomesAndBedRecoveryHavePriorityOverNewOrSavedActors) {
    Scene scene;const auto original=scene.content.encounters[0].spawn;
    scene.homeAt(original);const auto before=scene.state;
    auto actors=AdventureEncounters::admit(scene.state,scene.base);
    EXPECT_FALSE(actors.find(1)->available);EXPECT_TRUE(actors.find(2)->available);EXPECT_EQ(scene.state,before);
    scene.positioned(0,original);const auto saved=scene.state;actors=AdventureEncounters::admit(scene.state,scene.base);
    EXPECT_FALSE(actors.find(1)->available);EXPECT_EQ(actors.find(1)->pose,original);EXPECT_EQ(scene.state,saved);
    scene.solids.clear();scene.publish();scene.state.registeredBed=11;scene.state.recovery=original;
    EXPECT_FALSE(AdventureEncounters::admit(scene.state,scene.base).find(1)->available);
    scene.state.combat.encounters[0].checkpoint.positioned=false;scene.state.combat.encounters[0].checkpoint.phase=EnemyPhase::Dormant;
    actors=AdventureEncounters::admit(scene.state,scene.base);ASSERT_TRUE(actors.find(1)->available);
    EXPECT_FALSE(actors.occupied(position(original)));EXPECT_EQ(scene.state.recovery,original);
}

TEST(AdventureEncounters, SavedSupportedPoseAndHealthAreExactAndBlockedOrDeadActorHasNoBody) {
    Scene scene;const auto p=scene.ground(-57.25,-944.25,1.23456789);scene.positioned(0,p);
    scene.state.combat.encounters[0].checkpoint.health=23;const auto before=scene.state;
    auto actors=AdventureEncounters::admit(scene.state,scene.base);ASSERT_TRUE(actors.find(1)->available);
    EXPECT_EQ(actors.find(1)->pose,p);EXPECT_EQ(actors.find(1)->health,23);EXPECT_FALSE(actors.find(1)->needsPositionCheckpoint);
    EXPECT_EQ(scene.state,before);
    scene.solids.push_back({{scene.state.world,90},{scene.state.world,91},position(p)+glm::dvec3(-1,.8,-1),position(p)+glm::dvec3(1,1,1)});
    scene.publish();EXPECT_FALSE(AdventureEncounters::admit(scene.state,scene.base).find(1)->available);
    scene.solids.clear();scene.publish();auto& dead=scene.state.combat.encounters[0];
    dead.checkpoint.health=0;dead.checkpoint.phase=EnemyPhase::Dead;dead.deathRevision=1;
    actors=AdventureEncounters::admit(scene.state,scene.base);EXPECT_FALSE(actors.find(1)->available);
    EXPECT_FALSE(actors.occupied(position(p)));std::vector<Solid> solids;ASSERT_TRUE(actors.appendSolids(scene.state.world,solids));
    ASSERT_EQ(solids.size(),1u);EXPECT_EQ(solids.front().part.counter,encounterPartId(2));
}

TEST(AdventureEncounters, StandingCloseToAnEnemyDoesNotRemoveItsAcceptedBody) {
    Scene scene;const auto p=scene.content.encounters[0].spawn;scene.positioned(0,p);
    const auto admitted=AdventureEncounters::admit(scene.state,scene.base);
    std::vector<Solid> solids;ASSERT_TRUE(admitted.appendSolids(scene.state.world,solids));
    AdventureSpatialQueries published;ASSERT_TRUE(published.bindTerrain(scene.terrain));ASSERT_TRUE(published.publish(solids,1));
    // The diagonal capsule is clear even though its enclosing AABB overlaps the
    // enemy box. The .61 m case is closer than the new-spawn .05 m safety margin.
    for(const auto delta:std::array<glm::dvec2,4>{{{.61,0},{.6,0},{.54,.54},{.59,0}}}) {
        scene.state.player=scene.ground(p.x+delta.x,p.z+delta.y);
        const bool clear=published.clearCapsule(position(scene.state.player));
        EXPECT_EQ(clear,delta.x!=.59);
        const auto before=scene.state;const auto restored=AdventureEncounters::admit(scene.state,scene.base);
        EXPECT_EQ(restored.find(1)->available,clear);EXPECT_EQ(restored.find(1)->pose,p);EXPECT_EQ(scene.state,before);
    }
}

TEST(AdventureEncounters, ReservedLegacyIdentitiesDeferOnlyTheirActorAndAppendIsAtomic) {
    for(const auto id:{encounterPartId(1),encounterStructureId(1)}) {
        Scene scene;scene.state.structures.push_back({9,1,0,{},{{id,PieceKind::Floor,{},0,0}}});
        const auto before=scene.state;const auto actors=AdventureEncounters::admit(scene.state,scene.base);
        EXPECT_FALSE(actors.find(1)->available);EXPECT_TRUE(actors.find(2)->available);EXPECT_EQ(scene.state,before);
    }
    Scene scene;const auto actors=AdventureEncounters::admit(scene.state,scene.base);
    std::vector<Solid> solids;ASSERT_TRUE(actors.appendSolids(scene.state.world,solids));const auto count=solids.size();
    EXPECT_FALSE(actors.appendSolids(scene.state.world,solids));EXPECT_EQ(solids.size(),count);
    std::vector<Solid> full(AdventureSpatialQueries::maximumSolids,solids.front());
    EXPECT_FALSE(actors.appendSolids(scene.state.world,full));EXPECT_EQ(full.size(),AdventureSpatialQueries::maximumSolids);
    auto otherWorld=scene.state.world;otherWorld.bytes[1]=1;
    EXPECT_FALSE(actors.appendSolids(otherWorld,solids));EXPECT_EQ(solids.size(),count);
}

TEST(AdventureEncounters, MovementPreservesLeashProtectedBeaconAndHeadroomWhileRestRequiresSafety) {
    Scene scene;const auto actors=AdventureEncounters::admit(scene.state,scene.base);
    const auto p=actors.find(2)->pose;ASSERT_TRUE(actors.find(2)->available);
    EXPECT_TRUE(actors.movementAllowed(2,position(p),scene.base));
    EXPECT_FALSE(actors.movementAllowed(2,position(scene.ground(p.x+13,p.z)),scene.base));
    EXPECT_FALSE(actors.movementAllowed(2,position(scene.ground(-63,-975)),scene.base));
    EXPECT_FALSE(actors.movementAllowed(2,position(p)+glm::dvec3(0,1,0),scene.base));
    EXPECT_FALSE(actors.movementAllowed(0,position(p),scene.base));
    EXPECT_TRUE(actors.safeRest(scene.state.player,scene.base));
    EXPECT_FALSE(actors.safeRest(scene.ground(p.x+2,p.z),scene.base));
    EXPECT_TRUE(actors.safeRest(scene.ground(p.x+8,p.z,.2),scene.base));
    scene.solids.push_back({{scene.state.world,90},{scene.state.world,91},position(p)+glm::dvec3(-1,.8,-1),position(p)+glm::dvec3(1,1,1)});
    scene.publish();EXPECT_FALSE(actors.movementAllowed(2,position(p),scene.base));
    EXPECT_TRUE(actors.occupied({std::numeric_limits<double>::quiet_NaN(),0,0}));
}

TEST(AdventureEncounters, InvalidInstalledGroundLeavesContentOutputUntouched) {
    Scene scene;const auto before=scene.content.encounters;scene.homeAt(before[0].spawn);
    std::string error;EXPECT_FALSE(defaultEncounterContent(scene.base,scene.content.encounters,error));
    EXPECT_EQ(scene.content.encounters,before);EXPECT_FALSE(error.empty());
    AdventureSpatialQueries invalid;EXPECT_FALSE(defaultEncounterContent(invalid,scene.content.encounters,error));
    EXPECT_EQ(scene.content.encounters,before);
}

TEST(AdventureEncounters, InstalledFullTerrainHasBothWalkingEncountersAndPreservesLegacyPoses) {
    const char* path=std::getenv("VOXY_ADVENTURE_TEST_TERRAIN");
    if(!path||!*path)GTEST_SKIP()<<"Supply the installed full raw terrain via VOXY_ADVENTURE_TEST_TERRAIN for this CPU preflight.";
    std::vector<uint16_t> samples(8192*8192);std::ifstream input(path,std::ios::binary);
    ASSERT_TRUE(input.read(reinterpret_cast<char*>(samples.data()),static_cast<std::streamsize>(samples.size()*sizeof(uint16_t))));
    EXPECT_EQ(input.peek(),std::char_traits<char>::eof());
    ASSERT_EQ(voxy::core::sha256Hex(voxy::core::sha256(std::as_bytes(std::span(samples)))),installedWorld().samplesSha256);
    if constexpr(std::endian::native==std::endian::big)for(auto& sample:samples)sample=uint16_t((sample>>8)|(sample<<8));
    const voxy::terrain::lego::Surface terrain{samples,8192,8192,600.f,1.f};
    AdventureSpatialQueries base;ASSERT_TRUE(base.bindTerrain(terrain));
    AdventureState state;state.world.bytes[0]=29;const auto town=townSpawn(terrain);state.player={town.x,town.y,town.z,0};
    std::vector<Solid> markers;
    const std::array points{installedWorld().town+glm::dvec2(0,-3),installedWorld().landmark};
    for(size_t i=0;i<points.size();++i) {
        const auto p=points[i];const double y=double(voxy::terrain::lego::supportHeight(terrain,glm::vec2(p),.5f));
        markers.push_back({{state.world,i+1},{state.world,i+1},{p.x-.5,y,p.y-.5},{p.x+.5,y+(i?4.:2.),p.y+.5}});
    }
    ASSERT_TRUE(base.publish(markers,1));AdventureContent content;content.town=state.player;std::string error;
    ASSERT_TRUE(defaultEncounterContent(base,content.encounters,error))<<error;initializeAdventureProgress(state,content);
    const auto original=state;const auto actors=AdventureEncounters::admit(state,base);
    const std::array<double,2> expectedFeet{{-146.375,-147.655}};
    for(size_t i=0;i<actors.entries().size();++i) {
        state=original;
        const auto& actor=actors.entries()[i];ASSERT_TRUE(actor.available)<<int(actor.id);
        EXPECT_NEAR(actor.pose.y,expectedFeet[i],.0001);EXPECT_EQ(state,original);
        RecordProperty("encounter_"+std::to_string(actor.id)+"_feet_y",std::to_string(actor.pose.y));
        AdventurePlayer walker;ASSERT_TRUE(walker.initialize(base,position(actor.pose),-200));
        for(const auto delta:std::array<glm::dvec2,4>{{{1,0},{-1,0},{0,1},{0,-1}}}) {
            ASSERT_TRUE(walk(base,walker,glm::dvec2(actor.pose.x,actor.pose.z)+delta));
            ASSERT_TRUE(walk(base,walker,{actor.pose.x,actor.pose.z}));
        }
        state=original;state.player=actor.pose;const auto savedPlayer=state;
        const auto fresh=AdventureEncounters::admit(state,base);EXPECT_EQ(state,savedPlayer);
        EXPECT_FALSE(fresh.occupied(position(state.player)));
        auto& checkpoint=state.combat.encounters[i].checkpoint;checkpoint.pose=actor.pose;
        checkpoint.positioned=true;checkpoint.phase=EnemyPhase::Idle;const auto saved=state;
        const auto restored=AdventureEncounters::admit(state,base);EXPECT_FALSE(restored.find(actor.id)->available);EXPECT_EQ(state,saved);
    }
    state=original;const auto p=actors.find(1)->pose;
    state.combat.encounters[0].checkpoint.positioned=true;state.combat.encounters[0].checkpoint.phase=EnemyPhase::Idle;
    state.structures.push_back({9,1,0,{},{{10,PieceKind::Wall,{},0,0}}});
    auto blocked=markers;blocked.push_back({{state.world,9},{state.world,10},position(p)-glm::dvec3(2,.1,2),position(p)+glm::dvec3(2,2,2)});
    ASSERT_TRUE(base.publish(blocked,2));const auto oldHome=state;
    const auto deferred=AdventureEncounters::admit(state,base);EXPECT_FALSE(deferred.find(1)->available);EXPECT_TRUE(deferred.find(2)->available);
    EXPECT_EQ(state,oldHome);EXPECT_EQ(deferred.find(1)->pose,p);EXPECT_TRUE(base.clearCapsule(town));
}
