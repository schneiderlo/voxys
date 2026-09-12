#include "game/expedition/cove_character.hpp"
#include <gtest/gtest.h>
#include <limits>
#include <tuple>

namespace voxy::game::expedition {
namespace {
using Mode=CovePlayer::Mode;
using Clip=CoveCharacter::Clip;

TEST(CoveCharacter, ZeroElapsedTimePreservesPoseAndDoesNotQueueAFalseLanding) {
    CoveCharacter actor;
    actor.update(.02,Mode::Airborne,0,3,false);
    ASSERT_EQ(actor.clip(),Clip::Jump);
    const double time=actor.time(),blend=actor.blend();
    for(int i=0;i<20;++i)actor.update(0,Mode::Walking,3.6,0,true);
    EXPECT_EQ(actor.clip(),Clip::Jump);EXPECT_DOUBLE_EQ(actor.time(),time);EXPECT_DOUBLE_EQ(actor.blend(),blend);
    // A paused observation cannot leave an invisible landing timer that fires
    // after the character actually swims ashore.
    actor.update(.02,Mode::Swimming,0,0,false);ASSERT_EQ(actor.clip(),Clip::Swim);
    actor.update(.02,Mode::Walking,0,0,false);EXPECT_EQ(actor.clip(),Clip::Idle);
}

TEST(CoveCharacter, WalkPhaseUsesActualRelativeSpeedAndBoundedElapsedTime) {
    CoveCharacter half,full,blocked;
    half.update(.1,Mode::Walking,1.8,0,false);
    full.update(.1,Mode::Walking,3.6,0,false);
    blocked.update(.1,Mode::Walking,0,0,false);
    EXPECT_EQ(half.clip(),Clip::Walk);EXPECT_EQ(full.clip(),Clip::Walk);EXPECT_EQ(blocked.clip(),Clip::Idle);
    EXPECT_DOUBLE_EQ(half.time(),.05);EXPECT_DOUBLE_EQ(full.time(),.1);
    half.update(.1,Mode::Walking,1.8,0,false);EXPECT_DOUBLE_EQ(half.time(),.1);
    CoveCharacter fast;fast.update(100,Mode::Walking,100,0,false);
    EXPECT_DOUBLE_EQ(fast.time(),.25*1.5);EXPECT_DOUBLE_EQ(fast.blend(),1);
    // Releasing movement selects idle even when the previous walk was fast.
    fast.update(.01,Mode::Walking,.09,0,false);EXPECT_EQ(fast.clip(),Clip::Idle);
}

TEST(CoveCharacter, JumpFallAndLandingFollowActualModeAndVerticalVelocity) {
    CoveCharacter actor;actor.update(.05,Mode::Walking,3.6,0,false);ASSERT_EQ(actor.clip(),Clip::Walk);
    actor.update(.05,Mode::Airborne,3.6,4,false);ASSERT_EQ(actor.clip(),Clip::Jump);
    EXPECT_DOUBLE_EQ(actor.time(),.05);EXPECT_NEAR(actor.blend(),.05/.14,1e-12);
    actor.update(.05,Mode::Airborne,3.6,-1,false);ASSERT_EQ(actor.clip(),Clip::Fall);
    actor.update(.02,Mode::Walking,3.6,0,false);ASSERT_EQ(actor.clip(),Clip::Land);
    actor.update(.2,Mode::Walking,3.6,0,false);EXPECT_EQ(actor.clip(),Clip::Land);
    actor.update(.02,Mode::Walking,3.6,0,false);EXPECT_EQ(actor.clip(),Clip::Walk);
    actor.update(.02,Mode::Airborne,0,0,false);EXPECT_EQ(actor.clip(),Clip::Fall); // Apex is no longer ascent.
}

TEST(CoveCharacter, SwimmingHelmAndToolHaveExplicitMovementPriority) {
    CoveCharacter actor;
    actor.update(.1,Mode::Swimming,2,0,true);EXPECT_EQ(actor.clip(),Clip::Swim);
    actor.update(.1,Mode::Helm,0,0,false);EXPECT_EQ(actor.clip(),Clip::Helm);
    actor.update(.1,Mode::Helm,0,0,true);EXPECT_EQ(actor.clip(),Clip::Tool);
    actor.update(.1,Mode::Walking,0,0,true);EXPECT_EQ(actor.clip(),Clip::Tool);
    actor.update(.1,Mode::Walking,1,0,true);EXPECT_EQ(actor.clip(),Clip::Walk);
    actor.update(.1,Mode::Airborne,1,1,true);EXPECT_EQ(actor.clip(),Clip::Jump);
}

TEST(CoveCharacter, PauseKeepsBlendedPhaseAndResetDropsAllTransientHistory) {
    CoveCharacter actor;actor.update(.02,Mode::Walking,1.8,0,false);
    const auto clip=actor.clip();const double time=actor.time(),blend=actor.blend();
    for(int i=0;i<600;++i)actor.update(0,Mode::Helm,0,0,true);
    EXPECT_EQ(actor.clip(),clip);EXPECT_DOUBLE_EQ(actor.time(),time);EXPECT_DOUBLE_EQ(actor.blend(),blend);
    actor.reset();EXPECT_EQ(actor.clip(),Clip::Idle);EXPECT_DOUBLE_EQ(actor.time(),0);EXPECT_DOUBLE_EQ(actor.blend(),1);
    actor.update(.02,Mode::Walking,0,0,false);EXPECT_EQ(actor.clip(),Clip::Idle);
}

TEST(CoveCharacter, PausedRestoreSelectsSemanticPoseWithoutAdvancingCosmeticTime) {
    CoveCharacter actor;actor.update(.02,Mode::Airborne,0,-1,false);actor.update(.02,Mode::Walking,0,0,false);
    for(const auto& [mode,velocity,clip]:std::array<std::tuple<Mode,double,Clip>,5>{{
        {Mode::Helm,0,Clip::Helm},{Mode::Airborne,3,Clip::Jump},{Mode::Airborne,-3,Clip::Fall},
        {Mode::Swimming,0,Clip::Swim},{Mode::Walking,0,Clip::Idle}}}) {
        ASSERT_TRUE(actor.restoreMode(mode,velocity));EXPECT_EQ(actor.clip(),clip);
        EXPECT_DOUBLE_EQ(actor.time(),0);EXPECT_DOUBLE_EQ(actor.blend(),1);
        actor.update(0,mode,0,velocity,false);EXPECT_EQ(actor.clip(),clip);EXPECT_DOUBLE_EQ(actor.time(),0);
    }
    actor.update(.02,Mode::Walking,0,0,false);EXPECT_EQ(actor.clip(),Clip::Idle); // No pre-restore landing timer.
    ASSERT_TRUE(actor.restoreMode(Mode::Helm,0));
    EXPECT_FALSE(actor.restoreMode(Mode::Airborne,std::numeric_limits<double>::infinity()));
    EXPECT_FALSE(actor.restoreMode(static_cast<Mode>(255),0));EXPECT_EQ(actor.clip(),Clip::Helm);
}

TEST(CoveCharacter, InvalidObservationPreservesTheLastAcceptedCosmeticState) {
    CoveCharacter actor;actor.update(.05,Mode::Walking,1.8,0,false);
    const auto clip=actor.clip();const double time=actor.time(),blend=actor.blend();
    const double nan=std::numeric_limits<double>::quiet_NaN(),infinity=std::numeric_limits<double>::infinity();
    actor.update(nan,Mode::Airborne,1,1,false);actor.update(-1,Mode::Airborne,1,1,false);
    actor.update(.1,Mode::Airborne,nan,1,false);actor.update(.1,Mode::Airborne,-1,1,false);
    actor.update(.1,Mode::Airborne,1,infinity,false);actor.update(.1,static_cast<Mode>(255),1,1,false);
    EXPECT_EQ(actor.clip(),clip);EXPECT_DOUBLE_EQ(actor.time(),time);EXPECT_DOUBLE_EQ(actor.blend(),blend);
}
} // namespace
} // namespace voxy::game::expedition
