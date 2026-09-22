#include "camera/camera.hpp"
#include "game/adventure/adventure_input.hpp"
#include "game/adventure/adventure_swim_animation.hpp"
#include "game/expedition/cove_camera.hpp"

#include <gtest/gtest.h>
#include <array>
#include <cmath>
#include <numbers>

namespace {
using namespace voxy::game::adventure;
using namespace voxy::game::expedition;

CoveCamera::SweepResult clearCameraSweep(const void*,glm::dvec3 from,glm::dvec3 to,
                                       double,uint64_t) noexcept {
    return {true,false,false,glm::length(to-from),{}};
}

std::array<double,2> routedMovement(bool controller,double right,double forward) {
    CoveInputRouter router;
    const auto preferences=adventureInputDefaults();
    CoveInputSample sample;
    sample.padConnected=controller;sample.padArmed=controller;
    // Establish the same neutral-input handoff required by the live router.
    router.tick(sample,CoveInputContext::World,preferences);
    if(controller) {
        sample.axes[0]=static_cast<float>(right);
        sample.axes[1]=static_cast<float>(-forward);
    } else {
        sample.keys['D']=right>0;sample.keys['A']=right<0;
        sample.keys['W']=forward>0;sample.keys['S']=forward<0;
        sample.physicalKeys=sample.keys;
    }
    router.tick(sample,CoveInputContext::World,preferences);
    return router.movement();
}
} // namespace

TEST(AdventureMovement, KeyboardAndControllerDirectionsMatchRealCameraProjection) {
    const glm::dvec3 anchor(14,2,-9);
    const std::array<std::array<double,2>,8> directions{{
        {1,0},{-1,0},{0,1},{0,-1},{1,1},{-1,1},{1,-1},{-1,-1}}};
    for(int quadrant=0;quadrant<8;++quadrant)for(double elevation:{-.25,.32,1.1})
    for(float aspect:{.8f,1.6f}) {
        const double yaw=double(quadrant)*std::numbers::pi/4.;
        SCOPED_TRACE(::testing::Message()<<"yaw="<<yaw<<" elevation="<<elevation<<" aspect="<<aspect);
        CoveCamera orbit;
        ASSERT_TRUE(orbit.restoreOrbit(yaw,elevation,4.8));
        ASSERT_EQ(orbit.update({anchor,yaw,1},{},{1.05,double(aspect),.1},
                              {nullptr,clearCameraSweep},1./60),CoveCamera::Result::Ready);
        const auto& pose=orbit.pose();
        voxy::Camera camera(glm::vec3(pose.eye),glm::vec3(pose.viewTarget));
        camera.setAspectRatio(aspect);
        const auto project=[&](glm::dvec3 point) {
            return glm::dvec4(camera.viewProjectionMatrix()*glm::vec4(glm::vec3(point),1.f));
        };
        const auto center=project(anchor);
        ASSERT_GT(center.w,0.);
        for(bool controller:{false,true})for(const auto direction:directions) {
            SCOPED_TRACE(::testing::Message()<<"controller="<<controller<<" right="<<direction[0]<<" forward="<<direction[1]);
            const auto movement=routedMovement(controller,direction[0],direction[1]);
            ASSERT_EQ(movement,direction);
            const auto world=adventureCameraRelativeMovement(movement,pose.yaw);
            const auto moved=project(anchor+glm::dvec3(world.x,0,world.y)*.2);
            ASSERT_GT(moved.w,0.);
            const double horizontal=moved.x/moved.w-center.x/center.w;
            // Test rendered screen displacement, not another hand-written basis.
            if(direction[0]!=0)EXPECT_GT(horizontal*direction[0],.005);
            else EXPECT_NEAR(horizontal,0.,2e-6);
            // Forward moves into the visible scene; left/right leaves depth alone.
            if(direction[1]!=0)EXPECT_GT((moved.w-center.w)*direction[1],.05);
            else EXPECT_NEAR(moved.w,center.w,2e-6);
        }
    }
}

TEST(AdventureMovement, RotationPreservesAnalogMagnitudeAndLeavesDiagonalCappingToPlayer) {
    const auto analog=routedMovement(true,.375,.25);
    const auto diagonal=routedMovement(false,1,1);
    const double analogLength=std::hypot(analog[0],analog[1]);
    for(int quadrant=0;quadrant<8;++quadrant) {
        const double yaw=double(quadrant)*std::numbers::pi/4.;
        EXPECT_NEAR(glm::length(adventureCameraRelativeMovement(analog,yaw)),analogLength,1e-12);
        EXPECT_NEAR(glm::length(adventureCameraRelativeMovement(diagonal,yaw)),std::sqrt(2.),1e-12);
        EXPECT_EQ(adventureCameraRelativeMovement({0,0},yaw),glm::dvec2(0));
    }
}

TEST(AdventureMovement, RunningJumpAcceptsPressEdgesAndKeepsOtherModifiers) {
    const auto preferences=adventureInputDefaults();
    for(bool building:{false,true})for(bool allowRunning:{false,true})
    for(uint8_t modifiers:{uint8_t(1),uint8_t(3),uint8_t(5)})
    for(bool quickTap:{false,true}) {
        SCOPED_TRACE(::testing::Message()<<"building="<<building<<" running="<<allowRunning
            <<" modifiers="<<int(modifiers)<<" quickTap="<<quickTap);
        CoveInputRouter router;
        CoveInputSample sample;
        const auto tick=[&](CoveInputContext context=CoveInputContext::World) {
            router.tick(adventureMovementSample(sample,building,allowRunning),context,preferences);
        };
        tick();
        sample.modifiers=modifiers;
        sample.keys['W']=true;sample.physicalKeys['W']=true;
        tick();
        sample.pressed[32]=true;sample.pressModifiers[32]=modifiers;
        sample.keys[32]=!quickTap;sample.physicalKeys[32]=!quickTap;
        // A tap and Shift release can both arrive before the next frame.
        if(quickTap)sample.modifiers=uint8_t(modifiers&~1u);
        tick();
        const bool shouldJump=allowRunning&&modifiers==1;
        EXPECT_EQ(router.pressed(CoveAction::Jump),shouldJump);
        if(shouldJump){EXPECT_EQ(router.movement()[1],1.);}
        EXPECT_EQ(sample.pressModifiers[32],modifiers); // Workshop keeps the original chord.
        sample.pressed[32]=false;
        tick();
        EXPECT_FALSE(router.pressed(CoveAction::Jump));
    }
}

TEST(AdventureMovement, RunningJumpMustRearmAfterMenu) {
    const auto preferences=adventureInputDefaults();
    CoveInputRouter router;
    CoveInputSample sample;
    const auto tick=[&](CoveInputContext context=CoveInputContext::World) {
        router.tick(adventureMovementSample(sample,true,true),context,preferences);
    };
    tick(CoveInputContext::Menu);
    sample.modifiers=1;sample.pressModifiers[32]=1;
    sample.keys[32]=true;sample.physicalKeys[32]=true;sample.pressed[32]=true;
    tick(CoveInputContext::Menu);
    EXPECT_FALSE(router.pressed(CoveAction::Jump));
    tick();
    EXPECT_FALSE(router.pressed(CoveAction::Jump));
    sample.pressed[32]=false;
    tick();
    EXPECT_FALSE(router.pressed(CoveAction::Jump));
    sample.keys[32]=false;sample.physicalKeys[32]=false;
    tick();
    sample.keys[32]=true;sample.physicalKeys[32]=true;sample.pressed[32]=true;
    tick();
    EXPECT_TRUE(router.pressed(CoveAction::Jump));
}

TEST(AdventureMovement, SwimUsesMouseLookPitchAndExplicitRiseDiveOverrides) {
    for(double yaw:{0.,1.,-2.})for(double elevation:{-.4,.32,1.2}) {
        const auto level=adventureSwimmingMovement({0,1},yaw,elevation,false,false,false);
        EXPECT_DOUBLE_EQ(level.y,0);EXPECT_NEAR(glm::length(level),1.,1e-12);
        const auto aimed=adventureSwimmingMovement({0,1},yaw,elevation,true,false,false);
        EXPECT_NEAR(aimed.y,-std::sin(elevation),1e-12);EXPECT_NEAR(glm::length(aimed),1.,1e-12);
        EXPECT_EQ(adventureSwimmingMovement({0,-1},yaw,elevation,true,false,false),-aimed);
        EXPECT_DOUBLE_EQ(adventureSwimmingMovement({1,0},yaw,elevation,true,false,false).y,0);
        EXPECT_DOUBLE_EQ(adventureSwimmingMovement({0,1},yaw,elevation,true,true,false).y,1);
        EXPECT_DOUBLE_EQ(adventureSwimmingMovement({0,1},yaw,elevation,true,false,true).y,-1);
        EXPECT_DOUBLE_EQ(adventureSwimmingMovement({0,1},yaw,elevation,true,true,true).y,0);
    }
}

TEST(AdventureMovement, SwimRiseDiveAreHeldActionsWithFocusAndMenuRearming) {
    const auto preferences=adventureInputDefaults();
    for(bool building:{false,true})for(bool shift:{false,true}) {
        CoveInputRouter router;CoveInputSample sample;
        const auto tick=[&](CoveInputContext context=CoveInputContext::World) {
            router.tick(adventureMovementSample(sample,building,true),context,preferences);
        };
        tick();sample.modifiers=shift?1:0;
        sample.keys[32]=sample.physicalKeys[32]=true;
        for(int i=0;i<3;++i){tick();EXPECT_TRUE(router.down(CoveAction::Jump));}
        sample.keys[32]=sample.physicalKeys[32]=false;
        sample.keys['X']=sample.physicalKeys['X']=true;
        for(int i=0;i<3;++i){tick();EXPECT_TRUE(router.down(CoveAction::PayOut));}
        tick(CoveInputContext::Menu);EXPECT_FALSE(router.down(CoveAction::PayOut));
        tick();EXPECT_FALSE(router.down(CoveAction::PayOut));
        sample.keys['X']=sample.physicalKeys['X']=false;tick();
        sample.keys['X']=sample.physicalKeys['X']=true;tick();EXPECT_TRUE(router.down(CoveAction::PayOut));
        sample.focused=false;tick();EXPECT_FALSE(router.down(CoveAction::PayOut));
        sample.focused=true;tick();EXPECT_FALSE(router.down(CoveAction::PayOut));
    }
}

TEST(AdventureSwimmingAnimation, LeansIntoTravelAndPausesWithoutMovingCollisionOrigin) {
    AdventureSwimAnimation moving,treading;
    for(int i=0;i<90;++i) {
        moving.update(1./60,true,{0,0,-2.4},1,false);
        treading.update(1./60,true,{},1,false);
    }
    const glm::dvec4 head(0,1.55,0,1);
    EXPECT_LT((moving.body()*head).z,-.45);
    EXPECT_GT((treading.body()*head).z,-.15);
    const glm::dvec3 shoulder(-.3,1.3,0),hip(-.15,.6,0);
    EXPECT_NEAR(glm::length(glm::dvec3(moving.arm(shoulder,true)*glm::dvec4(shoulder,1))-shoulder),0,1e-12);
    EXPECT_NEAR(glm::length(glm::dvec3(moving.leg(hip,true)*glm::dvec4(hip,1))-hip),0,1e-12);
    const auto body=moving.body(),arm=moving.arm(shoulder,true);const auto phase=moving.phase();
    moving.update(0,false,{},1,false);
    EXPECT_EQ(moving.body(),body);EXPECT_EQ(moving.arm(shoulder,true),arm);EXPECT_DOUBLE_EQ(moving.phase(),phase);
    moving.update(.1,true,{0,0,-2.4},1,false);EXPECT_NE(moving.arm(shoulder,true),arm);
    for(int i=0;i<90;++i)moving.update(1./60,false,{},1,false);
    EXPECT_FALSE(moving.active());EXPECT_EQ(moving.body(),glm::dmat4(1));
}
