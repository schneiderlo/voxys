#include "game/adventure/builder_cannon.hpp"
#include "game/adventure/brick_thrower.hpp"
#include <gtest/gtest.h>

namespace voxy::game::adventure {
TEST(BrickThrower, ClicksDoNotRepeatAndDraggingOrLosingInputCancelsBurst) {
    BrickThrower thrower;
    EXPECT_EQ(thrower.input(true,true,false,false,{}),1u);
    EXPECT_EQ(thrower.input(true,false,false,false,{}),0u);
    EXPECT_EQ(thrower.input(true,false,true,false,{}),0u);
    EXPECT_EQ(thrower.input(true,false,false,true,{}),100u);
    EXPECT_EQ(thrower.input(true,false,false,true,{}),0u);
    EXPECT_EQ(thrower.input(true,false,true,true,{}),100u); // Complete tap in one frame.
    EXPECT_EQ(thrower.input(true,false,true,false,{3,0}),0u);
    EXPECT_EQ(thrower.input(true,false,false,false,{-3,0}),0u);
    EXPECT_EQ(thrower.input(true,false,false,true,{}),0u); // Dragged back to its start.
    (void)thrower.input(true,false,true,false,{});
    EXPECT_EQ(thrower.input(false,true,false,false,{}),0u); // Build/menu/focus boundary.
    EXPECT_EQ(thrower.input(true,false,false,true,{}),0u);
}
TEST(BrickThrower, BurstHasOneHundredSeparateStuddedBodiesInWorldCoordinates) {
    for(double yaw:{0.,.7,3.1}) {
        std::array<glm::dvec3,100> positions;
        for(uint32_t i=0;i<100;++i) {
            const auto desc=BrickThrower::projectile({1023,4,-1025},yaw,.32,i,100);
            positions[i]=physics::worldPositionToAbsolute({desc.sector,desc.position});
            EXPECT_EQ(desc.shape,physics::ThrowableShape::Box);
            EXPECT_EQ(desc.dimensions,glm::vec3(1.96f,1.14f,.96f));
            ASSERT_TRUE(desc.material);
            EXPECT_EQ(desc.material->flags&0xf0000000u,physics::kLegoBrickMaterial);
            EXPECT_TRUE(desc.bullet);EXPECT_NEAR(glm::length(desc.linearVelocity),28.f,1e-4f);
            EXPECT_GT(desc.linearVelocity.y,0);EXPECT_GE(positions[i].y,4.);
            for(uint32_t j=0;j<i;++j)EXPECT_GT(glm::length(positions[i]-positions[j]),2.59);
        }
    }
}
TEST(BuilderCannon, BarrelHingeAndMuzzleStayAlignedAcrossAimRange) {
    BuilderCannon cannon;
    const glm::dvec3 feet(1208,.005,-1080);
    const glm::dvec3 originalMuzzle(0,2.705861092,3.782148838);
    for(double yaw:{-.7,0.,.7})for(double elevation:{.02,BuilderCannon::sourceElevation,BuilderCannon::maximumElevation}) {
        cannon.yaw=yaw;cannon.elevation=elevation;
        const auto hinge=glm::dvec3(cannon.barrelMatrix(feet)*glm::dvec4(BuilderCannon::pivot,1));
        EXPECT_LT(glm::length(hinge-glm::dvec3(cannon.baseMatrix(feet)*glm::dvec4(BuilderCannon::pivot,1))),1e-8);
        const auto muzzle=glm::dvec3(cannon.barrelMatrix(feet)*glm::dvec4(originalMuzzle,1));
        EXPECT_LT(glm::length(muzzle-cannon.muzzle(feet)),1e-6);
        const auto projectile=cannon.projectile(feet);
        const auto start=physics::worldPositionToAbsolute({projectile.sector,projectile.position});
        const auto clearance=start-muzzle;
        EXPECT_NEAR(glm::length(clearance),double(BuilderCannon::ballRadius)+.12,2e-5);
        EXPECT_GT(glm::dot(glm::normalize(clearance),cannon.direction()),.999999);
        EXPECT_NEAR(glm::length(projectile.linearVelocity),BuilderCannon::shotSpeed,1e-5);
        EXPECT_TRUE(projectile.bullet);
    }
}
TEST(BuilderCannon, AimBoundsFiniteInputAndRecoilDoNotMoveTheBase) {
    BuilderCannon cannon;
    for(int i=0;i<100;++i)cannon.aim(1,1,.1);
    EXPECT_DOUBLE_EQ(cannon.yaw,.7);EXPECT_DOUBLE_EQ(cannon.elevation,BuilderCannon::maximumElevation);
    const auto before=cannon.baseMatrix({0,0,0});cannon.recoil=.18;
    EXPECT_EQ(cannon.baseMatrix({0,0,0}),before);
    cannon.aim(NAN,0,.1);EXPECT_DOUBLE_EQ(cannon.recoil,.18);
    cannon.aim(0,0,.1);EXPECT_LT(cannon.recoil,.18);
    for(int i=0;i<100;++i)cannon.aim(-1,-1,.1);
    EXPECT_DOUBLE_EQ(cannon.yaw,-.7);EXPECT_DOUBLE_EQ(cannon.elevation,.02);
}
TEST(BuilderCannon, FixedHeadingRotatesTheCompleteCannonAndProjectileTogether) {
    BuilderCannon cannon;cannon.heading=-std::numbers::pi/2;
    const auto direction=cannon.direction();
    EXPECT_LT(direction.x,-.9);EXPECT_NEAR(direction.z,0,1e-12);
    const auto desc=cannon.projectile({1240,.185,-1027});
    EXPECT_LT(desc.linearVelocity.x,-40);EXPECT_NEAR(desc.linearVelocity.z,0,1e-5);
    const auto muzzle=cannon.muzzle({1240,.185,-1027});
    EXPECT_LT(muzzle.x,1237);EXPECT_NEAR(muzzle.z,-1027,1e-8);
    for(int i=0;i<100;++i)cannon.aim(1,0,.1);
    EXPECT_DOUBLE_EQ(cannon.yaw,.7);EXPECT_DOUBLE_EQ(cannon.heading,-std::numbers::pi/2);
}
} // namespace voxy::game::adventure
