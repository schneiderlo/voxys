#include "game/expedition/workshop_camera.hpp"
#include <gtest/gtest.h>
#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <limits>

using namespace voxy::game::expedition;

TEST(WorkshopCamera, RealProjectionContainsTallAndAsymmetricBuildsOutsideTheUi) {
    for(double aspect:{16./9,4./3,.6})for(double yaw:{-2.,0.,1.5})for(double elevation:{-.7,.4,1.2}) {
        WorkshopCamera camera;camera.orbit(yaw-camera.yaw(),elevation-camera.elevation());
        const auto projection=glm::perspectiveLH_ZO(.9,aspect,.1,10000.);
        const glm::dvec4 area=aspect<1?glm::dvec4{-.92,.06,.92,.92}:glm::dvec4{-.92,-.8,.25,.92};
        ASSERT_TRUE(camera.viewport({projection[0][0],projection[1][1]},area));
        for(const auto bounds:{WorkshopBounds{{-5,-2,-56},{3,63,-48}},WorkshopBounds{{-27,-1,-63},{18,5,-47}}}) {
            ASSERT_TRUE(camera.frame(bounds));
            const auto view=glm::lookAtLH(camera.eye(),camera.viewTarget(),glm::dvec3(0,1,0));
            for(unsigned corner=0;corner<8;++corner) {
                const glm::dvec3 p{corner&1?bounds.maximum.x:bounds.minimum.x,
                    corner&2?bounds.maximum.y:bounds.minimum.y,corner&4?bounds.maximum.z:bounds.minimum.z};
                const auto clip=projection*view*glm::dvec4(p,1);ASSERT_GT(clip.w,.1);
                const auto ndc=glm::dvec3(clip)/clip.w;
                EXPECT_GE(ndc.x,area.x-1e-10);EXPECT_LE(ndc.x,area.z+1e-10);
                EXPECT_GE(ndc.y,area.y-1e-10);EXPECT_LE(ndc.y,area.w+1e-10);
                EXPECT_GE(ndc.z,0);EXPECT_LE(ndc.z,1);
            }
        }
    }
}

TEST(WorkshopCamera, PanTracksThePointerAndFramingRecoversAfterExtremeZoom) {
    WorkshopCamera camera;const auto projection=glm::perspectiveLH_ZO(1.,1.6,.1,10000.);
    ASSERT_TRUE(camera.viewport({projection[0][0],projection[1][1]},{-.9,-.8,.3,.9}));
    const WorkshopBounds bounds{{-5,-2,-56},{3,8,-48}};ASSERT_TRUE(camera.frame(bounds));
    const auto point=camera.target();
    const auto project=[&]{auto clip=projection*glm::lookAtLH(camera.eye(),camera.viewTarget(),glm::dvec3(0,1,0))*glm::dvec4(point,1);return glm::dvec2(clip)/clip.w;};
    const auto before=project();camera.pan({64,30},600);const auto after=project();
    EXPECT_NEAR(after.x-before.x,128./960,1e-10);EXPECT_NEAR(after.y-before.y,-60./600,1e-10);
    camera.zoom(10000);EXPECT_EQ(camera.distance(),WorkshopCamera::minimumDistance);
    camera.zoom(-10000);EXPECT_EQ(camera.distance(),WorkshopCamera::maximumDistance);
    ASSERT_TRUE(camera.frame(bounds));EXPECT_EQ(camera.target(),point);
}

TEST(WorkshopCamera, InvalidFramingLeavesTheCurrentViewIntact) {
    WorkshopCamera camera;ASSERT_TRUE(camera.frame({{-2,-1,-5},{2,4,5}}));
    const auto eye=camera.eye(),target=camera.target();const auto distance=camera.distance();
    const double nan=std::numeric_limits<double>::quiet_NaN();
    EXPECT_FALSE(camera.frame({{nan,0,0},{1,1,1}}));
    EXPECT_FALSE(camera.frame({{2,0,0},{1,1,1}}));
    EXPECT_FALSE(camera.frame({{-100000,0,0},{100000,100000,100000}}));
    EXPECT_FALSE(camera.viewport({0,1},{-.9,-.9,.9,.9}));
    camera.pan({nan,0},600);camera.zoom(nan);camera.orbit(nan,0);
    EXPECT_EQ(camera.eye(),eye);EXPECT_EQ(camera.target(),target);EXPECT_EQ(camera.distance(),distance);
}
