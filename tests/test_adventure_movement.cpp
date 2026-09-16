#include "camera/camera.hpp"
#include "game/adventure/adventure_input.hpp"
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
