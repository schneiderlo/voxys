#include "game/adventure/frontier_combat.hpp"
#include "game/adventure/frontier_interactions.hpp"
#include <gtest/gtest.h>

namespace voxy::game::adventure {
TEST(FrontierCombat, ScoutDartsAndRetreatsWhileBruteStillTelegraphsItsCommittedCharge) {
    const auto scout=frontierCombatProfile(FrontierEnemyArchetype::Scout),brute=frontierCombatProfile(FrontierEnemyArchetype::Brute);
    FrontierAttackState dart,charge;
    ASSERT_TRUE(frontierBeginAttack(dart,scout,{1,0}));ASSERT_TRUE(frontierBeginAttack(charge,brute,{1,0}));
    for(int tick=0;tick<48;++tick){frontierAdvanceAttack(dart,scout,1./60.);frontierAdvanceAttack(charge,brute,1./60.);}
    EXPECT_EQ(dart.phase,FrontierAttackPhase::Recovery);EXPECT_GT(scout.retreatSpeed,0);
    EXPECT_EQ(charge.phase,FrontierAttackPhase::Windup);EXPECT_EQ(brute.retreatSpeed,0);
    EXPECT_GT(scout.approachSpeed,brute.approachSpeed);EXPECT_GT(brute.damage,scout.damage);
    EXPECT_FALSE(frontierBeginAttack(charge,brute,{0,1}));EXPECT_EQ(charge.direction,glm::dvec2(1,0));
    for(int tick=0;tick<12;++tick)frontierAdvanceAttack(charge,brute,1./60.);
    EXPECT_EQ(charge.phase,FrontierAttackPhase::Strike);EXPECT_EQ(charge.direction,glm::dvec2(1,0));
}
TEST(FrontierCombat, HitsInterruptScoutsButDoNotCancelBruteCommitmentOrShortenRecovery) {
    const auto scout=frontierCombatProfile(FrontierEnemyArchetype::Scout),brute=frontierCombatProfile(FrontierEnemyArchetype::Brute);
    FrontierAttackState dart,charge;
    ASSERT_TRUE(frontierBeginAttack(dart,scout,{1,0}));ASSERT_TRUE(frontierBeginAttack(charge,brute,{1,0}));
    EXPECT_TRUE(frontierInterruptAttack(dart,scout));EXPECT_EQ(dart.phase,FrontierAttackPhase::Recovery);
    EXPECT_FALSE(frontierInterruptAttack(charge,brute));EXPECT_EQ(charge.phase,FrontierAttackPhase::Windup);
    frontierAdvanceAttack(charge,brute,brute.windup);EXPECT_FALSE(frontierInterruptAttack(charge,brute));
    frontierAdvanceAttack(charge,brute,brute.strike+.1);EXPECT_EQ(charge.phase,FrontierAttackPhase::Recovery);
    const double exposed=charge.seconds;EXPECT_TRUE(frontierInterruptAttack(charge,brute));EXPECT_DOUBLE_EQ(charge.seconds,exposed);
}
TEST(FrontierCombat, SuccessfulDodgeSpendsContactForEntireChargeAndEveryStrikeHitsAtMostOnce) {
    const auto profile=frontierCombatProfile(FrontierEnemyArchetype::Brute);FrontierAttackState attack;
    ASSERT_TRUE(frontierBeginAttack(attack,profile,{0,-1}));
    EXPECT_FALSE(frontierSpendStrikeContact(attack,true,false));EXPECT_FALSE(attack.hitSpent);
    frontierAdvanceAttack(attack,profile,profile.windup);
    EXPECT_FALSE(frontierSpendStrikeContact(attack,false,false));EXPECT_FALSE(attack.hitSpent);
    EXPECT_FALSE(frontierSpendStrikeContact(attack,true,true));EXPECT_TRUE(attack.hitSpent);
    frontierAdvanceAttack(attack,profile,.3);EXPECT_FALSE(frontierSpendStrikeContact(attack,true,false));
    frontierAdvanceAttack(attack,profile,profile.strike+profile.recovery);
    ASSERT_TRUE(frontierBeginAttack(attack,profile,{0,-1}));frontierAdvanceAttack(attack,profile,profile.windup);
    EXPECT_TRUE(frontierSpendStrikeContact(attack,true,false));EXPECT_FALSE(frontierSpendStrikeContact(attack,true,false));
}
TEST(FrontierCombat, CommittedChargeCannotTurnToFollowSideStepOrPassThroughBuiltCover) {
    std::vector<uint16_t> samples(64*64,32768);terrain::lego::Surface terrain{samples,64,64,600,1};
    AdventureSpatialQueries queries;ASSERT_TRUE(queries.bindTerrain(terrain));ASSERT_TRUE(queries.publish({},1));
    const double y=queries.supportHeight({0,0},1.12,1000)+.005;ASSERT_TRUE(std::isfinite(y));
    construction::WorldNamespace world;world.bytes[0]=71;
    std::vector<AdventureSpatialQueries::Solid> solids{{{world,3},{world,4},{3,y-.005,-5},{3.5,y+8,5}}};
    ASSERT_TRUE(queries.publish(solids,2));
    AdventurePlayer actor;ASSERT_TRUE(actor.initialize(queries,{0,y,0},-200,0,2.8,.4));
    const auto profile=frontierCombatProfile(FrontierEnemyArchetype::Brute);FrontierAttackState attack;
    ASSERT_TRUE(frontierBeginAttack(attack,profile,{1,0}));frontierAdvanceAttack(attack,profile,profile.windup);
    for(int tick=0;tick<28;++tick)actor.advance(1./60.,{attack.direction,false,profile.strikeSpeed,0});
    EXPECT_LT(actor.feet().x,2.0);EXPECT_NEAR(actor.feet().z,0,1e-6);
    const PlayerPose attacker{actor.feet().x,actor.feet().y,actor.feet().z,actor.facingYaw()};
    EXPECT_FALSE(frontierStrikeReachable(queries,attacker,{5,y,0,0},profile.strikeRange,profile.strikeCone));
    EXPECT_FALSE(frontierStrikeReachable(queries,attacker,{actor.feet().x,y,4,0},profile.strikeRange,profile.strikeCone));
    EXPECT_EQ(attack.direction,glm::dvec2(1,0));
}
TEST(FrontierCombat, BothAttackProfilesCanActuallyReachAStationaryTargetFromTheirTriggerDistance) {
    std::vector<uint16_t> samples(64*64,32768);terrain::lego::Surface terrain{samples,64,64,600,1};
    AdventureSpatialQueries queries;ASSERT_TRUE(queries.bindTerrain(terrain));ASSERT_TRUE(queries.publish({},1));
    const double y=queries.supportHeight({0,0},1.12,1000)+.005;ASSERT_TRUE(std::isfinite(y));
    for(const auto kind:{FrontierEnemyArchetype::Scout,FrontierEnemyArchetype::Brute}) {
        const auto profile=frontierCombatProfile(kind);FrontierAttackState attack;
        AdventurePlayer actor;ASSERT_TRUE(actor.initialize(queries,{0,y,0},-200,0,2.8,.4));
        ASSERT_TRUE(frontierBeginAttack(attack,profile,{1,0}));frontierAdvanceAttack(attack,profile,profile.windup);
        const PlayerPose target{profile.triggerRange-.1,y,0,0};unsigned hits=0;
        while(attack.phase==FrontierAttackPhase::Strike) {
            actor.advance(1./60.,{attack.direction,false,profile.strikeSpeed,0});
            const auto p=actor.feet();const PlayerPose from{p.x,p.y,p.z,actor.facingYaw()};
            if(frontierSpendStrikeContact(attack,frontierStrikeReachable(queries,from,target,profile.strikeRange,profile.strikeCone),false))++hits;
            frontierAdvanceAttack(attack,profile,1./60.);
        }
        EXPECT_EQ(hits,1u)<<"archetype="<<static_cast<int>(kind);
    }
}
} // namespace voxy::game::adventure
