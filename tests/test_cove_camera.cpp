#include "game/expedition/cove_camera.hpp"
#include <gtest/gtest.h>
#include <array>
#include <cmath>
#include <limits>
#include <numbers>

using namespace voxy::game::expedition;
namespace {
struct Scene {
    bool complete=true,overlap=false;
    std::optional<double> hit;
    mutable unsigned calls=0;
    mutable double radius=0;
    mutable uint64_t tick=0;
    mutable glm::dvec3 from{},to{};
    static CoveCamera::SweepResult cast(const void* context,glm::dvec3 start,glm::dvec3 end,
                                        double radius,uint64_t tick) noexcept {
        const auto& self=*static_cast<const Scene*>(context);
        ++self.calls;self.radius=radius;self.tick=tick;self.from=start;self.to=end;
        const double length=glm::length(end-start);
        const bool hit=self.hit&&*self.hit<=length;
        return {self.complete,hit,self.overlap,hit?*self.hit:length,{0,0,-1}};
    }
    CoveCamera::Sweep query() const {return {this,cast};}
};
struct Terrain {
    std::array<uint16_t,25> samples{};
    voxy::terrain::lego::Surface surface{samples,5,5,1.f,1.f};
    Terrain(){samples.fill(32768);}
};
}

TEST(CoveCamera, NearPlaneSphereEnclosesEveryCornerAcrossActualViewports) {
    for(double aspect:{.35,1.,16./9,3.5})for(double fov:{.65,1.05,1.7}) {
        CoveCamera camera;Scene scene;CoveCamera::Projection p{fov,aspect,.1};
        ASSERT_EQ(camera.update({{0,2,0},0,71},{},p,scene.query(),1./60),CoveCamera::Result::Ready);
        const auto& pose=camera.pose();const auto right=glm::normalize(glm::cross(pose.forward,glm::dvec3(0,1,0)));
        const auto up=glm::cross(right,pose.forward);const double halfHeight=.1*std::tan(fov*.5);
        for(double sx:{-1.,1.})for(double sy:{-1.,1.}) {
            const auto corner=pose.forward*.1+right*(sx*halfHeight*aspect)+up*(sy*halfHeight);
            EXPECT_LE(glm::length(corner)+CoveCamera::presentationSkin,scene.radius+1e-12);
        }
        EXPECT_EQ(scene.tick,71u);EXPECT_EQ(scene.from,glm::dvec3(0,2,0));
    }
    EXPECT_FALSE(CoveCamera::nearPlaneRadius({1.,0,.1}));
    EXPECT_FALSE(CoveCamera::nearPlaneRadius({1.,1.,0}));
}

TEST(CoveCamera, NewlyMovingObstructionPullsInImmediatelyAndClearanceReleasesGradually) {
    CoveCamera camera;Scene scene;CoveCamera::Target target{{0,2,0},0,10};
    ASSERT_EQ(camera.update(target,{},{},scene.query(),1./60),CoveCamera::Result::Ready);
    const double clear=camera.pose().distance;EXPECT_DOUBLE_EQ(clear,4.8);
    scene.hit=.24;target.geometryTick=11;
    EXPECT_EQ(camera.update(target,{},{},scene.query(),1./60),CoveCamera::Result::Obstructed);
    EXPECT_NEAR(camera.pose().distance,.238,1e-12);EXPECT_TRUE(camera.pose().hideAvatar);
    EXPECT_EQ(scene.calls,2u);EXPECT_EQ(scene.tick,11u);
    scene.hit.reset();target.geometryTick=12;
    EXPECT_EQ(camera.update(target,{},{},scene.query(),1./60),CoveCamera::Result::Ready);
    EXPECT_GT(camera.pose().distance,.238);EXPECT_LT(camera.pose().distance,clear);
    // Release stays on the newly checked ray; it never interpolates an old
    // world-space eye sideways through the moving obstacle.
    const auto certified=glm::normalize(scene.to-scene.from);
    EXPECT_LT(glm::length(camera.pose().eye-scene.from-certified*camera.pose().distance),1e-12);
}

TEST(CoveCamera, ZeroDistanceAndUnknownOrOverlappedGeometryNeverUseAnUnsafeMinimumBoom) {
    CoveCamera camera;Scene scene;CoveCamera::Target target{{0,2,0},0,5};
    scene.hit=0;
    ASSERT_EQ(camera.update(target,{},{},scene.query(),1./60),CoveCamera::Result::Obstructed);
    EXPECT_TRUE(camera.pose().valid);EXPECT_EQ(camera.pose().eye,target.anchor);
    EXPECT_GT(glm::length(camera.pose().viewTarget-camera.pose().eye),.99);
    scene.complete=false;
    EXPECT_EQ(camera.update(target,{},{},scene.query(),1./60),CoveCamera::Result::GeometryUnavailable);
    EXPECT_FALSE(camera.pose().valid);EXPECT_EQ(camera.pose().distance,0);
    scene.complete=true;scene.overlap=true;
    EXPECT_EQ(camera.update(target,{},{},scene.query(),1./60),CoveCamera::Result::AnchorOverlapped);
    EXPECT_FALSE(camera.pose().valid);
    scene.overlap=false;scene.hit=-1;
    EXPECT_EQ(camera.update(target,{},{},scene.query(),1./60),CoveCamera::Result::GeometryUnavailable);
    EXPECT_FALSE(camera.pose().valid);
}

TEST(CoveCamera, OrbitChaseAndMenuHandoffsPreserveHeadingAndReducedMotionStopsPassiveSwing) {
    CoveCamera camera;Scene scene;CoveCamera::Target target{{0,2,0},0,1};
    ASSERT_TRUE(camera.settings({CoveCamera::Mode::Orbit,false,true}));
    ASSERT_EQ(camera.update(target,{{.9,.2}},{},scene.query(),1./60),CoveCamera::Result::Ready);
    const double yaw=camera.pose().yaw,pitch=camera.pose().elevation;
    target.facingYaw=-2.8; // Boarding / a new helm facing is not a new camera.
    CoveCamera::Input blocked{{2,1},10,true,false};
    for(unsigned i=0;i<20;++i)(void)camera.update(target,blocked,{},scene.query(),.1);
    EXPECT_DOUBLE_EQ(camera.pose().yaw,yaw);EXPECT_DOUBLE_EQ(camera.pose().elevation,pitch);
    EXPECT_DOUBLE_EQ(camera.userDistance(),4.8);
    ASSERT_TRUE(camera.settings({CoveCamera::Mode::Chase,false,true}));
    (void)camera.update(target,{},{},scene.query(),1./60);
    EXPECT_DOUBLE_EQ(camera.pose().yaw,yaw); // Resume rearms quiet time.
    for(unsigned i=0;i<30;++i) {
        const double before=camera.pose().yaw;
        (void)camera.update(target,{},{},scene.query(),.1);
        EXPECT_LE(std::abs(std::remainder(camera.pose().yaw-before,2*std::numbers::pi)),.15000001);
    }
    EXPECT_NE(camera.pose().yaw,yaw);
    ASSERT_TRUE(camera.settings({CoveCamera::Mode::Chase,true,true}));
    const double reducedYaw=camera.pose().yaw;target.facingYaw=1.8;
    for(unsigned i=0;i<50;++i)(void)camera.update(target,{},{},scene.query(),.1);
    EXPECT_DOUBLE_EQ(camera.pose().yaw,reducedYaw);
    CoveCamera::Input recenter;recenter.recenter=true;
    (void)camera.update(target,recenter,{},scene.query(),.1);
    EXPECT_NE(camera.pose().yaw,reducedYaw); // Explicit choice still works.
    (void)camera.update(target,blocked,{},scene.query(),.1);
    const double menuYaw=camera.pose().yaw;
    (void)camera.update(target,{},{},scene.query(),.1);
    EXPECT_DOUBLE_EQ(camera.pose().yaw,menuYaw); // No queued recenter after close.
}

TEST(CoveCamera, EventAnglesSurviveLongFramesAndDiscontinuityNeverSweepsFromTheOldWorldPoint) {
    CoveCamera camera;Scene scene;CoveCamera::Target target{{127.99,2,-30},.4,1};
    (void)camera.update(target,{},{},scene.query(),1./60);
    const double oldYaw=camera.pose().yaw;
    target.anchor={1024.01,20,64};target.discontinuity=true;
    (void)camera.update(target,{{.75,-.1}},{},scene.query(),3.);
    EXPECT_NEAR(camera.pose().yaw,oldYaw+.75,1e-12);
    EXPECT_EQ(scene.from,target.anchor);EXPECT_DOUBLE_EQ(camera.pose().distance,4.8);
    EXPECT_GT(glm::length(camera.pose().eye-glm::dvec3(127.99,2,-30)),800);
    const auto before=camera.pose();const auto calls=scene.calls;
    CoveCamera::Input invalid;invalid.orbitRadians.x=std::numeric_limits<double>::quiet_NaN();
    EXPECT_EQ(camera.update(target,invalid,{},scene.query(),1./60),CoveCamera::Result::InvalidInput);
    EXPECT_EQ(camera.pose().eye,before.eye);EXPECT_EQ(scene.calls,calls);
    ASSERT_TRUE(camera.restoreOrbit(-2.,.5,7.));
    EXPECT_FALSE(camera.pose().valid); // Restoring intent does not certify collision geometry.
    EXPECT_DOUBLE_EQ(camera.pose().yaw,-2.);EXPECT_DOUBLE_EQ(camera.pose().elevation,.5);
    EXPECT_DOUBLE_EQ(camera.pose().requestedDistance,7.);
    ASSERT_EQ(camera.update(target,{},{},scene.query(),0),CoveCamera::Result::Ready);
    EXPECT_DOUBLE_EQ(camera.pose().yaw,-2.);EXPECT_DOUBLE_EQ(camera.pose().elevation,.5);
    EXPECT_DOUBLE_EQ(camera.pose().distance,7.);
    EXPECT_FALSE(camera.restoreOrbit(0,2.,7.));EXPECT_FALSE(camera.restoreOrbit(0,.3,13.));
    EXPECT_DOUBLE_EQ(camera.pose().yaw,-2.);
}

TEST(CoveCamera, LoadFramingFitsActualCornersAndReportsOcclusionOrDistanceLimits) {
    CoveCamera camera;Scene scene;CoveCamera::Target target{{0,2,0},0,1};
    target.load=CoveCamera::Bounds{{-3,-1,-4},{3,3,-2}};
    ASSERT_TRUE(camera.settings({CoveCamera::Mode::Orbit,false,true}));
    CoveCamera::Projection projection{1.,1.,.1};
    ASSERT_EQ(camera.update(target,{},{projection},scene.query(),1./60),CoveCamera::Result::Ready);
    EXPECT_TRUE(camera.pose().loadFramed);EXPECT_GT(camera.pose().requestedDistance,4.8);
    const auto& pose=camera.pose();const auto right=glm::normalize(glm::cross(pose.forward,glm::dvec3(0,1,0)));
    const auto up=glm::cross(right,pose.forward);const double scale=1/std::tan(.5);
    for(unsigned i=0;i<8;++i) {
        const auto& b=*target.load;const glm::dvec3 corner{i&1?b.maximum.x:b.minimum.x,
            i&2?b.maximum.y:b.minimum.y,i&4?b.maximum.z:b.minimum.z};
        const auto p=corner-pose.eye;const double depth=glm::dot(p,pose.forward);
        ASSERT_GE(depth,.1);EXPECT_LE(std::abs(glm::dot(p,right)*scale/depth),.85000001);
        EXPECT_LE(std::abs(glm::dot(p,up)*scale/depth),.85000001);
    }
    scene.hit=.5;(void)camera.update(target,{},{projection},scene.query(),1./60);
    EXPECT_FALSE(camera.pose().loadFramed);
    scene.hit.reset();target.load=CoveCamera::Bounds{{-50,0,-1},{50,2,1}};
    target.discontinuity=true;(void)camera.update(target,{},{projection},scene.query(),1./60);
    EXPECT_EQ(camera.pose().requestedDistance,CoveCamera::maximumDistance);EXPECT_FALSE(camera.pose().loadFramed);
    EXPECT_FALSE(camera.setUserDistance(0));EXPECT_FALSE(camera.setUserDistance(12.01));
    ASSERT_TRUE(camera.setUserDistance(7));EXPECT_DOUBLE_EQ(camera.userDistance(),7);
}

TEST(CoveCamera, ExtendedZoomKeepsCollisionSweepAndDefaultCoveLimit) {
    CoveCamera camera;Scene scene;CoveCamera::Target target{{0,2,0},0,1};
    EXPECT_FALSE(camera.setUserDistance(64));
    auto settings=camera.settings();settings.distanceLimit=64;
    ASSERT_TRUE(camera.settings(settings));
    ASSERT_EQ(camera.update(target,{{},-100},{},scene.query(),.1),CoveCamera::Result::Ready);
    EXPECT_DOUBLE_EQ(camera.userDistance(),64);
    EXPECT_DOUBLE_EQ(camera.pose().distance,64);
    EXPECT_NEAR(glm::length(scene.to-scene.from),64,1e-10);
    scene.hit=7;
    EXPECT_EQ(camera.update(target,{},{},scene.query(),.1),CoveCamera::Result::Obstructed);
    EXPECT_LT(camera.pose().distance,7);EXPECT_DOUBLE_EQ(camera.userDistance(),64);
    ASSERT_TRUE(camera.restoreOrbit(.5,.3,64));
    settings.distanceLimit=65;EXPECT_FALSE(camera.settings(settings));
    settings.distanceLimit=NAN;EXPECT_FALSE(camera.settings(settings));
    settings.distanceLimit=12;ASSERT_TRUE(camera.settings(settings));
    EXPECT_DOUBLE_EQ(camera.userDistance(),12);
}

TEST(CoveCameraTerrain, CircularStudAndColumnEdgesCannotBeSkippedByALongSweep) {
    Terrain terrain;const double top=terrain.surface.cellTop(1,1);
    const auto stud=sweepCoveTerrainSphere(terrain.surface,{-1.1,top+.10,-.5},{.1,top+.10,-.5},.05);
    ASSERT_TRUE(stud.complete);ASSERT_TRUE(stud.hit);EXPECT_FALSE(stud.startOverlapped);
    EXPECT_NEAR(stud.distance,.25,2e-7); // center -.5, radius .30+.05.
    // Real circular studs, not square bounding boxes: this near-corner
    // segment stays outside a .35 m dilated circle.
    const auto corner=sweepCoveTerrainSphere(terrain.surface,{-.82,top+.10,-.82},{-.78,top+.10,-.82},.05);
    ASSERT_TRUE(corner.complete);EXPECT_FALSE(corner.hit);
    terrain.samples[1*5+2]=65535;
    const auto cliff=sweepCoveTerrainSphere(terrain.surface,{-1.1,.6,-.8},{1.1,.6,-.8},.1);
    ASSERT_TRUE(cliff.complete);ASSERT_TRUE(cliff.hit);EXPECT_NEAR(cliff.distance,1.,2e-7);
    EXPECT_LT(cliff.normal.x,-.99);
}

TEST(CoveCameraTerrain, ExactStartingOverlapTangencyAndFiniteBoundaryAreReported) {
    Terrain terrain;const double top=terrain.surface.cellTop(1,1);
    const auto overlap=sweepCoveTerrainSphere(terrain.surface,{-.5,top+.1,-.5},{-.5,2,-.5},.05);
    ASSERT_TRUE(overlap.complete);EXPECT_TRUE(overlap.hit);EXPECT_TRUE(overlap.startOverlapped);
    EXPECT_DOUBLE_EQ(overlap.distance,0);
    const glm::dvec3 tangentPoint{-.5,top+double(voxy::terrain::lego::kStudHeight)+.05,-.5};
    const auto tangent=sweepCoveTerrainSphere(terrain.surface,tangentPoint,tangentPoint+glm::dvec3(0,.2,0),.05);
    ASSERT_TRUE(tangent.complete);EXPECT_TRUE(tangent.hit);EXPECT_FALSE(tangent.startOverlapped);
    EXPECT_DOUBLE_EQ(tangent.distance,0);
    const auto boundary=sweepCoveTerrainSphere(terrain.surface,{-3,top-.1,-.8},{-1,top-.1,-.8},.1);
    ASSERT_TRUE(boundary.complete);EXPECT_TRUE(boundary.hit);EXPECT_NEAR(boundary.distance,.9,2e-7);
    const auto outside=sweepCoveTerrainSphere(terrain.surface,{-20,2,-20},{-18,2,-20},.1);
    ASSERT_TRUE(outside.complete);EXPECT_FALSE(outside.hit);
    const auto floor=sweepCoveTerrainSphere(terrain.surface,{-20,0,-20},{-20,-2,-20},.1);
    ASSERT_TRUE(floor.complete);EXPECT_TRUE(floor.hit);EXPECT_NEAR(floor.distance,.9,1e-12);
}

TEST(CoveCameraTerrain, SmoothFallbackAndWorkLimitsNeverPretendMissingCoverageIsClear) {
    Terrain terrain;terrain.samples[2*5+2]=65535;
    const auto hill=sweepCoveTerrainSphere(terrain.surface,{-1.5,.7,-.5},{.5,.7,-.5},.1,false);
    ASSERT_TRUE(hill.complete);EXPECT_TRUE(hill.hit);EXPECT_LE(hill.distance,.4+1e-8);
    const auto limited=sweepCoveTerrainSphere(terrain.surface,{-1.8,3,-1.8},{1.8,3,1.8},.1,true,1);
    EXPECT_FALSE(limited.complete);EXPECT_FALSE(limited.hit);
    EXPECT_FALSE(sweepCoveTerrainSphere(terrain.surface,{0,3,0},{0,4,0},.1,true,4097).complete);
    auto broken=terrain.surface;broken.samples={};
    EXPECT_FALSE(sweepCoveTerrainSphere(broken,{0,3,0},{0,4,0},.1).complete);
    broken.width=broken.height=65536; // Product wraps size_t on WASM32; no fake empty terrain.
    EXPECT_FALSE(sweepCoveTerrainSphere(broken,{0,3,0},{0,4,0},.1).complete);
    EXPECT_FALSE(sweepCoveTerrainSphere(terrain.surface,{0,3,0},{0,4,0},0).complete);
    EXPECT_FALSE(sweepCoveTerrainSphere(terrain.surface,{0,3,0},{0,4,0},std::numeric_limits<double>::infinity()).complete);
}
