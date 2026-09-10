#include "physics/authored_body_frame.hpp"

#include <gtest/gtest.h>
#include <bit>
#include <limits>
#include <stdexcept>
#include <type_traits>

namespace voxy::physics {
namespace {

AuthoredShape shape(MassVector center = {.25, -.5, .75}) {
    const std::array<geometry::UnionBox, 1> boxes{{{{{-50,-50,-50},{50,50,50}},1}}};
    geometry::BoxUnionIssue issue;
    const auto geometry = geometry::BoxUnion::compile(boxes, issue);
    if (!geometry) throw std::runtime_error("motion geometry fixture");
    const RigidMassInput mass{5,center,{88.0/25,-2.0/75,8.0/15,-2.0/75,161.0/75,2.0/5,8.0/15,2.0/5,10.0/3}};
    AuthoredShapeIssue error;
    auto result = AuthoredShape::prepare(*geometry, mass, error);
    if (!result) throw std::runtime_error("motion mass fixture");
    return std::move(*result);
}
void near(glm::vec3 actual, glm::dvec3 expected, double tolerance = .00002) {
    for (int i = 0; i < 3; ++i) EXPECT_NEAR(static_cast<double>(actual[i]), expected[i], tolerance);
}
glm::dvec3 relative(const WorldPosition& position, glm::ivec3 sector) {
    glm::dvec3 result;
    for (int i = 0; i < 3; ++i) result[i] = static_cast<double>(int64_t{position.sector[i]} - sector[i]) * 256.0
        + static_cast<double>(position.local[i]);
    return result;
}
void nearPosition(const WorldPosition& actual, const WorldPosition& expected, double tolerance = .00002) {
    const auto a = relative(actual, expected.sector);
    for (int i = 0; i < 3; ++i) EXPECT_NEAR(a[i], static_cast<double>(expected.local[i]), tolerance);
    EXPECT_TRUE(isValidWorldPosition(actual));
}
glm::quat quarterTurn() { const float half = std::sqrt(.5f); return {half,0,0,half}; }

static_assert(std::is_trivially_copyable_v<AuthoredBodyFrame>);
static_assert(!std::is_convertible_v<AuthoredRootMotion, AuthoredBodyMotion>);

TEST(AuthoredBodyFrame, RotatedOriginMapsToAnalyticalCenterAndVelocity) {
    const AuthoredBodyFrame frame(shape());
    AuthoredRootMotion root;
    root.position.local = {10,20,30}; root.orientation = quarterTurn();
    root.originVelocity = {3,4,5}; root.angularVelocity = {0,0,2};
    AuthoredFrameError error;
    const auto body = frame.bodyMotion(root,error); ASSERT_TRUE(body); EXPECT_EQ(error,AuthoredFrameError::None);
    near(body->centerPosition.local,{10.5,20.25,30.75}); near(body->centerVelocity,{2.5,5,5});
    near(body->angularVelocity,{0,0,2});
    const auto restored = frame.rootMotion(*body,error); ASSERT_TRUE(restored); EXPECT_EQ(error,AuthoredFrameError::None);
    nearPosition(restored->position,root.position); near(restored->originVelocity,{3,4,5});
    near(restored->angularVelocity,{0,0,2});
    near(restored->orientation * glm::vec3(1,0,0),{0,1,0},.000001);
}

TEST(AuthoredBodyFrame, SectorTransitionsRetainIdenticalNearAndFarPrecision) {
    const AuthoredBodyFrame frame(shape()); AuthoredFrameError error;
    for (glm::ivec3 sector : {glm::ivec3(0),glm::ivec3(INT32_MAX-3,INT32_MIN+3,2'000'000'000)}) {
        AuthoredRootMotion root; root.position = {sector,{127.875f,-127.875f,127.5f}};
        const auto body=frame.bodyMotion(root,error); ASSERT_TRUE(body);
        EXPECT_EQ(body->centerPosition.sector,sector+glm::ivec3(1,-1,1));
        near(body->centerPosition.local,{-127.875,127.625,-127.75},0);
        const auto restored=frame.rootMotion(*body,error); ASSERT_TRUE(restored);
        nearPosition(restored->position,root.position);
    }
}

TEST(AuthoredBodyFrame, RoundingAtPositiveLocalLimitCarriesOrRejectsWorldOverflow) {
    const AuthoredBodyFrame frame(shape({.000005,0,0})); AuthoredFrameError error;
    AuthoredRootMotion root; root.position.local.x=std::nextafter(128.0f,0.0f);
    const auto body=frame.bodyMotion(root,error); ASSERT_TRUE(body);
    EXPECT_EQ(body->centerPosition.sector.x,1); EXPECT_EQ(body->centerPosition.local.x,-128.0f);
    EXPECT_TRUE(isValidWorldPosition(body->centerPosition));
    root.position.sector.x=INT32_MAX;
    EXPECT_FALSE(frame.bodyMotion(root,error)); EXPECT_EQ(error,AuthoredFrameError::WorldOverflow);
    EXPECT_EQ(root.position.local.x,std::nextafter(128.0f,0.0f));
    // The reverse conversion has the same carry obligation.
    const AuthoredBodyFrame opposite(shape({-.000005,0,0}));
    AuthoredBodyMotion input; input.centerPosition={{INT32_MAX,0,0},{std::nextafter(128.0f,0.0f),0,0}};
    input.orientation=opposite.bodyMotion(AuthoredRootMotion{},error)->orientation;
    EXPECT_FALSE(opposite.rootMotion(input,error)); EXPECT_EQ(error,AuthoredFrameError::WorldOverflow);
}

TEST(AuthoredBodyFrame, FragmentTranslationPreservesLocalPrecisionAndNeverClampsWorldLimits) {
    AuthoredFrameError error;
    const WorldPosition near{{0,0,0},{.000013f,-127.875f,127.5f}};
    const WorldPosition far{{INT32_MAX-3,INT32_MIN+3,2'000'000'000},near.local};
    const glm::dvec3 offset{.000017,-.5,.75};
    const auto a=translateAuthoredPosition(near,offset,error);ASSERT_TRUE(a);EXPECT_EQ(error,AuthoredFrameError::None);
    const auto b=translateAuthoredPosition(far,offset,error);ASSERT_TRUE(b);EXPECT_EQ(error,AuthoredFrameError::None);
    EXPECT_EQ(a->local,b->local);EXPECT_EQ(b->sector,far.sector+a->sector);
    EXPECT_GT(b->local.x,0.000029f);EXPECT_LT(b->local.x,0.000031f);
    const WorldPosition edge{{INT32_MAX,INT32_MIN,0},{std::nextafter(128.f,0.f),-128.f,0}};
    for(const auto delta:{glm::dvec3(.000005,0,0),glm::dvec3(0,-.000005,0),
        glm::dvec3(std::numeric_limits<double>::infinity(),0,0),glm::dvec3(0,std::numeric_limits<double>::quiet_NaN(),0)}) {
        EXPECT_FALSE(translateAuthoredPosition(edge,delta,error));EXPECT_EQ(error,AuthoredFrameError::WorldOverflow);
    }
    EXPECT_FALSE(translateAuthoredPosition({{0,0,0},{128,0,0}},glm::dvec3(0),error));
    EXPECT_EQ(error,AuthoredFrameError::InvalidPosition);
}

TEST(AuthoredBodyFrame, BothWorldEndsRejectWithoutSaturatingOrMutatingState) {
    const AuthoredBodyFrame frame(shape()); AuthoredFrameError error;
    AuthoredRootMotion root; root.position={{INT32_MAX,0,0},{127.875f,0,0}};
    EXPECT_FALSE(frame.bodyMotion(root,error)); EXPECT_EQ(error,AuthoredFrameError::WorldOverflow);
    EXPECT_EQ(root.position.sector.x,INT32_MAX); EXPECT_EQ(root.position.local.x,127.875f);
    root.position={{0,INT32_MIN,0},{0,-127.875f,0}};
    EXPECT_FALSE(frame.bodyMotion(root,error)); EXPECT_EQ(error,AuthoredFrameError::WorldOverflow);
    EXPECT_EQ(root.position.sector.y,INT32_MIN); EXPECT_EQ(root.position.local.y,-127.875f);
    const auto body=frame.bodyMotion(AuthoredRootMotion{},error); ASSERT_TRUE(body);
    auto reverse=*body; reverse.centerPosition={{INT32_MIN,0,0},{-128,0,0}};
    EXPECT_FALSE(frame.rootMotion(reverse,error)); EXPECT_EQ(error,AuthoredFrameError::WorldOverflow);
}

TEST(AuthoredBodyFrame, InvalidPositionsAndOrientationsAreNeverRepaired) {
    const AuthoredBodyFrame frame(shape()); AuthoredFrameError error;
    const float nan=std::numeric_limits<float>::quiet_NaN(),inf=std::numeric_limits<float>::infinity();
    for (float invalid : {128.0f,-129.0f,nan,inf}) for (int axis=0;axis<3;++axis) {
        AuthoredRootMotion root; root.position.local[axis]=invalid;
        EXPECT_FALSE(frame.bodyMotion(root,error)); EXPECT_EQ(error,AuthoredFrameError::InvalidPosition);
        AuthoredBodyMotion body; body.centerPosition.local[axis]=invalid;
        EXPECT_FALSE(frame.rootMotion(body,error)); EXPECT_EQ(error,AuthoredFrameError::InvalidPosition);
    }
    for (glm::quat invalid : {glm::quat(0,0,0,0),glm::quat(2,0,0,0),glm::quat(nan,0,0,0),glm::quat(0,0,inf,0)}) {
        AuthoredRootMotion root; root.orientation=invalid;
        EXPECT_FALSE(frame.bodyMotion(root,error)); EXPECT_EQ(error,AuthoredFrameError::InvalidOrientation);
        AuthoredBodyMotion body; body.orientation=invalid;
        EXPECT_FALSE(frame.rootMotion(body,error)); EXPECT_EQ(error,AuthoredFrameError::InvalidOrientation);
    }
}

TEST(AuthoredBodyFrame, NonfiniteVelocityAndPointsReturnTypedRefusals) {
    const AuthoredBodyFrame frame(shape()); AuthoredFrameError error;
    for (float invalid : {std::numeric_limits<float>::quiet_NaN(),std::numeric_limits<float>::infinity()}) {
        for (int axis=0;axis<3;++axis) {
            AuthoredRootMotion root; root.originVelocity[axis]=invalid;
            EXPECT_FALSE(frame.bodyMotion(root,error)); EXPECT_EQ(error,AuthoredFrameError::InvalidVelocity);
            root.originVelocity={0,0,0}; root.angularVelocity[axis]=invalid;
            EXPECT_FALSE(frame.bodyMotion(root,error)); EXPECT_EQ(error,AuthoredFrameError::InvalidVelocity);
            AuthoredBodyMotion body; body.centerVelocity[axis]=invalid;
            EXPECT_FALSE(frame.rootMotion(body,error)); EXPECT_EQ(error,AuthoredFrameError::InvalidVelocity);
            body.centerVelocity={0,0,0}; body.angularVelocity[axis]=invalid;
            EXPECT_FALSE(frame.rootMotion(body,error)); EXPECT_EQ(error,AuthoredFrameError::InvalidVelocity);
            glm::vec3 point(0); point[axis]=invalid;
            EXPECT_FALSE(frame.bodyPoint(point,error)); EXPECT_EQ(error,AuthoredFrameError::InvalidPoint);
            EXPECT_FALSE(frame.rootPoint(point,error)); EXPECT_EQ(error,AuthoredFrameError::InvalidPoint);
        }
    }
    ASSERT_TRUE(frame.bodyPoint({1,2,3},error)); EXPECT_EQ(error,AuthoredFrameError::None);
}

TEST(AuthoredBodyFrame, FiniteButUnrepresentableVelocityAndPointsDoNotClamp) {
    const AuthoredBodyFrame frame(shape({1,2,3})); AuthoredFrameError error;
    constexpr float maximum=std::numeric_limits<float>::max();
    AuthoredRootMotion root; root.originVelocity={-maximum,0,0}; root.angularVelocity={0,0,maximum};
    EXPECT_FALSE(frame.bodyMotion(root,error)); EXPECT_EQ(error,AuthoredFrameError::Unrepresentable);
    auto body=frame.bodyMotion(AuthoredRootMotion{},error); ASSERT_TRUE(body);
    body->centerVelocity={maximum,0,0}; body->angularVelocity={0,0,maximum};
    EXPECT_FALSE(frame.rootMotion(*body,error)); EXPECT_EQ(error,AuthoredFrameError::Unrepresentable);
    // A corner of the f32 cube cannot remain representable under every rotation.
    unsigned rejected=0;
    for (float x : {-maximum,maximum}) for (float y : {-maximum,maximum}) for (float z : {-maximum,maximum}) {
        if (!frame.bodyPoint({x,y,z},error)) { ++rejected; EXPECT_EQ(error,AuthoredFrameError::Unrepresentable); }
    }
    EXPECT_GT(rejected,0u);
}

TEST(AuthoredBodyFrame, PointMappingAppliesPackedCenterAndPrincipalRotationOnce) {
    const AuthoredBodyFrame frame(shape()); AuthoredFrameError error;
    AuthoredRootMotion root; root.orientation=quarterTurn();
    const auto body=frame.bodyMotion(root,error); ASSERT_TRUE(body);
    const auto point=frame.bodyPoint({1,-2,3},error); ASSERT_TRUE(point);
    // Rz(90°) * (point - COM) = (1.5, .75, 2.25), independent of eigenvector signs.
    near(body->orientation * *point,{1.5,.75,2.25},.000002);
    const auto restored=frame.rootPoint(*point,error); ASSERT_TRUE(restored);
    near(*restored,{1,-2,3},.000002);
    const auto atCenter=frame.bodyPoint({.25f,-.5f,.75f},error); ASSERT_TRUE(atCenter);
    near(*atCenter,{0,0,0},0);
}

TEST(AuthoredBodyFrame, EquivalentQuaternionSignsAndSmallNormDriftCanonicalize) {
    const AuthoredBodyFrame frame(shape({0,0,0})); AuthoredFrameError error;
    for (glm::quat q : {glm::quat(1,0,0,0),glm::quat(0,1,0,0),glm::quat(0,0,1,0),quarterTurn()}) {
        AuthoredRootMotion a; a.orientation=q; auto b=a; b.orientation=-q;
        const auto first=frame.bodyMotion(a,error),second=frame.bodyMotion(b,error); ASSERT_TRUE(first); ASSERT_TRUE(second);
        for (int i=0;i<4;++i) EXPECT_EQ(std::bit_cast<uint32_t>(first->orientation[i]),std::bit_cast<uint32_t>(second->orientation[i]));
        auto drift=a; drift.orientation*=1.0001f;
        const auto normalized=frame.bodyMotion(drift,error); ASSERT_TRUE(normalized);
        EXPECT_NEAR(glm::dot(first->orientation,normalized->orientation),1.0f,.000001f);
        const auto restored=frame.rootMotion(*first,error); ASSERT_TRUE(restored);
        EXPECT_GE(restored->orientation.w,0.0f);
        for (int i=0;i<4;++i) {
            if (restored->orientation[i]==0) { EXPECT_FALSE(std::signbit(restored->orientation[i])); }
        }
    }
}

TEST(AuthoredBodyFrame, DenseWorldOrientationsPreserveLocalPrecisionAndVelocityField) {
    const AuthoredBodyFrame frame(shape()); AuthoredFrameError error;
    uint32_t state=991;
    const auto random=[&](){state=state*1664525u+1013904223u; return static_cast<double>(state&65535u)/65535.0;};
    for (int iteration=0;iteration<1000;++iteration) {
        const glm::dvec3 axis=glm::normalize(glm::dvec3(random()-.5,random()-.5,random()-.5));
        const double angle=random()*6.0,half=angle*.5,c=std::cos(angle),s=std::sin(angle);
        AuthoredRootMotion root; root.position={{2'000'000'000,-2'000'000'000,999'999'999},{127,-127,15.25f}};
        root.orientation=glm::quat(glm::dquat(std::cos(half),axis*std::sin(half)));
        root.originVelocity={3,4,5}; root.angularVelocity={-1,2,3};
        const auto body=frame.bodyMotion(root,error); ASSERT_TRUE(body);
        // Independent Rodrigues reference; do not reconstruct a far absolute coordinate.
        const glm::dvec3 center(.25,-.5,.75);
        const glm::dvec3 offset=center*c+glm::cross(axis,center)*s+axis*glm::dot(axis,center)*(1-c);
        const auto measured=relative(body->centerPosition,root.position.sector)-glm::dvec3(root.position.local);
        for (int i=0;i<3;++i) EXPECT_NEAR(measured[i],offset[i],.00001);
        near(body->centerVelocity,glm::dvec3(3,4,5)+glm::cross(glm::dvec3(-1,2,3),offset),.000002);
        const auto restored=frame.rootMotion(*body,error); ASSERT_TRUE(restored);
        nearPosition(restored->position,root.position); near(restored->originVelocity,{3,4,5},.000003);
    }
}

} // namespace
} // namespace voxy::physics
