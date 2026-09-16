#include "game/adventure/adventure_combat.hpp"
#include "game/adventure/world_definition.hpp"
#include <gtest/gtest.h>
#include <cmath>
#include <numbers>

using namespace voxy::game::adventure;
namespace {
glm::dvec3 feet(PlayerPose p){return {p.x,p.y,p.z};}
struct CombatScene {
    std::vector<uint16_t> samples=std::vector<uint16_t>(256*2048,32768);
    voxy::terrain::lego::Surface terrain{samples,256,2048,8.f,1.f};
    AdventureSpatialQueries world,collisions;
    std::vector<AdventureSpatialQueries::Solid> solids;
    AdventureContent content;
    AdventureState state;
    AdventurePlayer player;
    AdventureCombat combat;
    uint64_t publication=0;
    CombatScene() {
        state.world.bytes[0]=47;const auto town=townSpawn(terrain);state.player={town.x,town.y,town.z,0};
        content.town=state.player;EXPECT_TRUE(world.bindTerrain(terrain));EXPECT_TRUE(world.publish({},1));
        EXPECT_TRUE(collisions.bindTerrain(terrain));std::string error;
        EXPECT_TRUE(defaultEncounterContent(world,content.encounters,error))<<error;
        initializeAdventureProgress(state,content);state.equippedTool={ItemKind::TrailStaff,1};
        EXPECT_TRUE(combat.initialize(content,world,-200));EXPECT_TRUE(publishActors());
        EXPECT_TRUE(player.initialize(collisions,feet(state.player),-200,state.player.yaw));
    }
    PlayerPose grounded(double x,double z,double yaw=0) const {
        return {x,double(voxy::terrain::lego::supportHeight(terrain,{float(x),float(z)},.3f))+.005,z,yaw};
    }
    bool publishActors() {
        auto packet=solids;const auto admitted=AdventureEncounters::admit(state,world);
        return admitted.appendSolids(state.world,packet)&&collisions.publish(packet,++publication);
    }
    void placePlayer(double x,double z,double yaw=0) {
        state.player=grounded(x,z,yaw);ASSERT_TRUE(publishActors());
        ASSERT_TRUE(player.initialize(collisions,feet(state.player),-200,yaw));
    }
    void setEnemy(size_t index,EnemyPhase phase,uint16_t health=0) {
        auto& enemy=state.combat.encounters[index].checkpoint;enemy.positioned=true;enemy.phase=phase;enemy.phaseTicks=0;
        if(health)enemy.health=health;
    }
    CombatTick tick(AdventureCombat::Input input={}) {
        EXPECT_TRUE(publishActors());const auto admitted=AdventureEncounters::admit(state,world);
        const auto next=combat.step(state,content,admitted,world,player,input);
        for(const auto& work:combat.work()) {
            EXPECT_LE(work.columns,16u);EXPECT_LE(work.expansions+work.endpointChecks,4u);
            EXPECT_LE(work.totalExpansions,LayeredNavigation::maximumExpansions);
        }
        state.player=next.player;state.health=next.health;state.combat.tick=next.tick;state.combat.player=next.playerCombat;
        for(size_t i=0;i<next.enemies.size();++i)state.combat.encounters[i].checkpoint=next.enemies[i];
        return next;
    }
    void wall(glm::dvec3 minimum,glm::dvec3 maximum) {
        solids.push_back({{state.world,30},{state.world,31},minimum,maximum});
        ASSERT_TRUE(world.publish(solids,world.revision()+1));ASSERT_TRUE(publishActors());
    }
};
}

TEST(AdventureCombat, VisibleWindupDealsOneHitThenHonorsRecovery) {
    CombatScene scene;scene.setEnemy(0,EnemyPhase::Chase);const auto spawn=scene.content.encounters[0].spawn;
    scene.placePlayer(spawn.x,spawn.z+1.2);auto next=scene.tick();
    ASSERT_EQ(next.enemies[0].phase,EnemyPhase::Windup);EXPECT_EQ(next.enemies[0].phaseTicks,0);
    const auto frozenYaw=next.enemies[0].pose.yaw;
    for(int tick=1;tick<42;++tick) {
        next=scene.tick();EXPECT_EQ(next.health,100);EXPECT_EQ(next.enemies[0].phase,EnemyPhase::Windup);
        EXPECT_DOUBLE_EQ(next.enemies[0].pose.yaw,frozenYaw);EXPECT_EQ(next.enemies[0].attackSerial,0u);
    }
    next=scene.tick();EXPECT_EQ(next.health,88);EXPECT_EQ(next.enemies[0].phase,EnemyPhase::Attack);EXPECT_EQ(next.enemies[0].attackSerial,1u);
    next=scene.tick();ASSERT_EQ(next.enemies[0].phase,EnemyPhase::Recover);
    for(int tick=0;tick<54;++tick){next=scene.tick();EXPECT_EQ(next.health,88);EXPECT_EQ(next.enemies[0].attackSerial,1u);}
    EXPECT_EQ(next.enemies[0].phase,EnemyPhase::Chase);
}

TEST(AdventureCombat, SidestepEscapesTheFrozenArcWhileRemainingWithinReach) {
    CombatScene scene;scene.setEnemy(0,EnemyPhase::Chase);const auto spawn=scene.content.encounters[0].spawn;
    scene.placePlayer(spawn.x,spawn.z+1.2);auto next=scene.tick();ASSERT_EQ(next.enemies[0].phase,EnemyPhase::Windup);
    const auto yaw=next.enemies[0].pose.yaw;
    // Ordinary movement follows a quarter circle during the windup, ending at
    // the raider's side at the same 1.2 m distance, not outside its reach.
    for(int tick=1;tick<=42;++tick) {
        const double angle=std::min(double(tick)/32.,1.)*std::numbers::pi/2;
        const glm::dvec2 target(spawn.x+1.2*std::sin(angle),spawn.z+1.2*std::cos(angle));
        const auto delta=target-glm::dvec2(scene.state.player.x,scene.state.player.z);const double distance=glm::length(delta);
        const auto movement=distance>1e-8?delta*(std::min(1.,distance/.06)/distance):glm::dvec2(0);
        next=scene.tick({{movement,false},false,false});EXPECT_EQ(next.health,100);EXPECT_DOUBLE_EQ(next.enemies[0].pose.yaw,yaw);
    }
    EXPECT_EQ(next.enemies[0].attackSerial,1u);EXPECT_LT(glm::length(feet(next.player)-feet(next.enemies[0].pose)),1.6);
    EXPECT_FALSE(AdventureCombat::hitArc(scene.world,next.enemies[0].pose,next.player,1.6));
}

TEST(AdventureCombat, StaticWallsBlockNoticeAndBothAttackDirections) {
    CombatScene scene;const auto spawn=scene.content.encounters[0].spawn;scene.setEnemy(0,EnemyPhase::Idle);
    scene.placePlayer(spawn.x,spawn.z+1.2);
    scene.wall({spawn.x-2,0,spawn.z+.5},{spawn.x+2,3,spawn.z+.7});
    EXPECT_FALSE(AdventureCombat::sight(scene.world,scene.state.player,spawn));
    EXPECT_FALSE(AdventureCombat::hitArc(scene.world,scene.state.player,spawn,1.8));
    for(int tick=0;tick<30;++tick)EXPECT_EQ(scene.tick().enemies[0].phase,EnemyPhase::Idle);
    scene.setEnemy(0,EnemyPhase::Windup);scene.state.combat.encounters[0].checkpoint.pose.yaw=std::numbers::pi;
    scene.state.combat.encounters[0].checkpoint.phaseTicks=41;
    const auto next=scene.tick();EXPECT_EQ(next.enemies[0].attackSerial,1u);EXPECT_EQ(next.health,100);
}

TEST(AdventureCombat, DodgeUsesRealMovementAndCollisionAndCannotRestartBeforeCooldown) {
    CombatScene scene;scene.placePlayer(-80,-940);const auto start=scene.state.player;
    scene.wall({-78.8,0,-941},{-78.6,3,-939});
    auto next=scene.tick({{{1,0},false},false,true});const auto ready=next.playerCombat.dodgeReadyTick;
    EXPECT_EQ(next.playerCombat.dodgeUntilTick,13u);EXPECT_EQ(next.playerCombat.invulnerableUntilTick,13u);
    for(int tick=0;tick<11;++tick)next=scene.tick({{},false,true});
    EXPECT_GT(next.player.x-start.x,.7);EXPECT_LE(next.player.x,-79.1+1e-4);
    EXPECT_TRUE(scene.world.clearCapsule(feet(next.player)));EXPECT_EQ(next.playerCombat.dodgeReadyTick,ready);
    next=scene.tick({{},false,true});EXPECT_EQ(next.playerCombat.dodgeUntilTick,0u);EXPECT_EQ(next.playerCombat.invulnerableUntilTick,0u);
    EXPECT_EQ(next.playerCombat.dodgeReadyTick,ready);
}

TEST(AdventureCombat, DodgeAvoidsARealEnemyImpactAndStaffNeedsEquippedTool) {
    CombatScene scene;scene.setEnemy(0,EnemyPhase::Windup);const auto spawn=scene.content.encounters[0].spawn;
    scene.state.combat.encounters[0].checkpoint.phaseTicks=41;
    scene.state.combat.encounters[0].checkpoint.pose.yaw=std::numbers::pi;
    scene.placePlayer(spawn.x,spawn.z+1.2);auto next=scene.tick({{{1,0},false},false,true});
    EXPECT_EQ(next.enemies[0].attackSerial,1u);EXPECT_EQ(next.health,100);EXPECT_GT(next.player.x,spawn.x);
    scene.state.equippedTool={};next=scene.tick({{},true,false});EXPECT_EQ(next.playerCombat.attackSerial,0u);
}

TEST(AdventureCombat, StaffHitsOncePerSerialAndDeadEnemyNeverReturnsOrRevives) {
    CombatScene scene;const auto spawn=scene.content.encounters[0].spawn;scene.setEnemy(0,EnemyPhase::Recover,25);
    scene.placePlayer(spawn.x,spawn.z+1.2,0);auto next=scene.tick({{},true,false});
    ASSERT_EQ(next.playerCombat.attackSerial,1u);EXPECT_EQ(next.playerCombat.attackImpactTick,13u);
    for(int tick=0;tick<11;++tick){next=scene.tick({{},true,false});EXPECT_EQ(next.enemies[0].health,25);}
    next=scene.tick();EXPECT_EQ(next.enemies[0].health,0);EXPECT_EQ(next.enemies[0].phase,EnemyPhase::Dead);
    EXPECT_EQ(next.enemies[0].lastPlayerAttackSerial,1u);EXPECT_EQ(next.playerCombat.attackImpactTick,0u);
    const auto deadPose=next.enemies[0].pose;
    scene.placePlayer(-80,-940);
    for(int tick=0;tick<120;++tick) {
        next=scene.tick({{},true,false});EXPECT_EQ(next.enemies[0].health,0);EXPECT_EQ(next.enemies[0].phase,EnemyPhase::Dead);
        EXPECT_EQ(next.enemies[0].pose,deadPose);EXPECT_EQ(next.enemies[0].lastPlayerAttackSerial,1u);
    }
    EXPECT_FALSE(AdventureEncounters::admit(scene.state,scene.world).find(1)->available);
}

TEST(AdventureCombat, NavigationChasesThenReturnsWithinLeashWithBoundedWork) {
    CombatScene scene;
    // Warm only the bounded work budget while the player remains at town.
    for(int tick=0;tick<260;++tick)scene.tick();
    const auto spawn=scene.content.encounters[0].spawn;scene.placePlayer(spawn.x+5,spawn.z);
    bool moved=false,windup=false;
    for(int tick=0;tick<700;++tick) {
        const auto next=scene.tick();const auto& enemy=next.enemies[0];
        EXPECT_LE(glm::length(glm::dvec2(enemy.pose.x-spawn.x,enemy.pose.z-spawn.z)),12);
        EXPECT_TRUE(scene.world.clearCapsule(feet(enemy.pose)));
        moved=moved||glm::length(feet(enemy.pose)-feet(spawn))>.5;
        if(enemy.phase==EnemyPhase::Windup){windup=true;break;}
    }
    ASSERT_TRUE(moved);ASSERT_TRUE(windup);
    scene.placePlayer(spawn.x+14,spawn.z);bool returned=false;
    for(int tick=0;tick<700;++tick) {
        const auto next=scene.tick();const auto& enemy=next.enemies[0];
        EXPECT_LE(glm::length(glm::dvec2(enemy.pose.x-spawn.x,enemy.pose.z-spawn.z)),12);
        if(enemy.phase==EnemyPhase::Idle&&glm::length(feet(enemy.pose)-feet(spawn))<.12){returned=true;break;}
    }
    EXPECT_TRUE(returned);
}

TEST(AdventureCombat, LongIdleThenWalkingChaseAndImpactRemainAcceptedBySessionAuthority) {
    CombatScene scene;scene.content.identity.bytes[0]=std::byte{79};std::string error;
    auto session=AdventureSession::create(scene.state.world,scene.content,error);ASSERT_TRUE(session)<<error;
    scene.state=session->state();scene.combat.reset();ASSERT_TRUE(scene.publishActors());
    ASSERT_TRUE(scene.player.initialize(scene.collisions,feet(scene.state.player),-200));
    const CandidateValidator geometry=[&](const AdventureState& before,const AdventureState& after,std::string& reason) {
        if(!scene.world.clearCapsule(feet(after.player))){reason="The player overlaps static geometry.";return false;}
        const auto old=AdventureEncounters::admit(before,scene.world),next=AdventureEncounters::admit(after,scene.world);
        for(const auto& actor:old.entries())if(actor.available&&after.combat.encounters[actor.id-1].checkpoint.health
            &&!next.find(actor.id)->available){reason="An accepted actor lost its safe body.";return false;}
        auto solids=scene.solids;AdventureSpatialQueries complete;
        if(!next.appendSolids(after.world,solids)||!complete.bindTerrain(scene.terrain)||!complete.publish(solids,1)
            ||!complete.clearCapsule(feet(after.player))){reason="The accepted actor packet blocks the player.";return false;}
        return true;
    };
    const auto accept=[&](AdventureCombat::Input input=AdventureCombat::Input{}) {
        const auto request=scene.tick(input);const auto& before=session->state();
        auto change=session->prepareCombatTick({before.revision,before.lastRequestSequence+1,1},request,geometry,error);
        if(!change){ADD_FAILURE()<<"Rejected combat tick "<<request.tick<<": "<<error;return false;}
        if(!session->commit(std::move(*change),error)){ADD_FAILURE()<<error;return false;}
        scene.state=session->state();return true;
    };
    for(int tick=0;tick<1201;++tick)ASSERT_TRUE(accept());
    EXPECT_EQ(scene.state.combat.tick,1201u);EXPECT_EQ(scene.state.health,100);
    for(const auto& encounter:scene.state.combat.encounters) {
        EXPECT_EQ(encounter.checkpoint.phase,EnemyPhase::Idle);
        EXPECT_EQ(encounter.checkpoint.phaseTicks,kMaximumCombatDeadlineTicks);
    }
    // Walk from town using the actual player controller. No fixture teleport
    // starts combat: the notice/chase/windup transitions all cross the session.
    const auto spawn=scene.content.encounters[0].spawn;const glm::dvec2 goal(spawn.x+5,spawn.z);
    bool chased=false,windup=false;
    for(int tick=0;tick<1200;++tick) {
        const auto delta=goal-glm::dvec2(scene.state.player.x,scene.state.player.z);const double distance=glm::length(delta);
        const auto movement=distance>.008?delta*(std::min(1.,distance/.06)/distance):glm::dvec2(0);
        ASSERT_TRUE(accept({{movement,false},false,false}));
        const auto phase=scene.state.combat.encounters[0].checkpoint.phase;
        chased=chased||phase==EnemyPhase::Chase;
        if(phase==EnemyPhase::Windup){windup=true;break;}
    }
    ASSERT_TRUE(chased);ASSERT_TRUE(windup);
    for(int tick=0;tick<42;++tick)ASSERT_TRUE(accept());
    EXPECT_EQ(scene.state.combat.encounters[0].checkpoint.phase,EnemyPhase::Attack);
    EXPECT_EQ(scene.state.combat.encounters[0].checkpoint.attackSerial,1u);EXPECT_EQ(scene.state.health,88);
    EXPECT_EQ(scene.state.combat.encounters[1].checkpoint.phaseTicks,kMaximumCombatDeadlineTicks);
}
