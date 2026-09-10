#include "physics/rigid_mass_frame.hpp"
#include "game/construction/compiled_assembly.hpp"

#include <gtest/gtest.h>
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace voxy::physics {
namespace {
using namespace voxy::game::construction;
constexpr MassMatrix kR{2.0/15.0,-2.0/3.0,11.0/15.0,14.0/15.0,1.0/3.0,2.0/15.0,-1.0/3.0,2.0/3.0,2.0/3.0};
constexpr MassVector kD{2,3,4};
MassMatrix tensor(MassMatrix r,MassVector d) {
    MassMatrix result{};
    for (size_t i=0;i<3;++i) for (size_t j=0;j<3;++j) for (size_t k=0;k<3;++k) result[i*3+j]+=r[i*3+k]*d[k]*r[j*3+k];
    return result;
}
RigidMassInput golden() { return {5,{.25,-.5,.75},tensor(kR,kD)}; }
RigidMassFrame prepare(RigidMassInput input=golden()) {
    MassFrameError error; auto result=RigidMassFrame::prepare(input,error);
    if (!result) throw std::runtime_error("mass frame fixture "+std::to_string(static_cast<int>(error)));
    return *result;
}
void near(MassVector actual,MassVector wanted,double tolerance=1.0e-12) {
    for (size_t i=0;i<3;++i) EXPECT_NEAR(actual[i],wanted[i],tolerance);
}
MassVector multiply(const MassMatrix& a,MassVector b) {
    return {a[0]*b[0]+a[1]*b[1]+a[2]*b[2],a[3]*b[0]+a[4]*b[1]+a[5]*b[2],a[6]*b[0]+a[7]*b[1]+a[8]*b[2]};
}
double dot(MassVector a,MassVector b) { return a[0]*b[0]+a[1]*b[1]+a[2]*b[2]; }
MassMatrix product(const MassMatrix& a,const MassMatrix& b) {
    MassMatrix result{};
    for(size_t i=0;i<3;++i) for(size_t j=0;j<3;++j) for(size_t k=0;k<3;++k) result[i*3+j]+=a[i*3+k]*b[k*3+j];
    return result;
}
MassMatrix transpose(const MassMatrix& a) { return {a[0],a[3],a[6],a[1],a[4],a[7],a[2],a[5],a[8]}; }
void checkFrame(const RigidMassInput& source,const RigidMassFrame& frame,double relativeTolerance=1.0e-12) {
    const auto& r=frame.rootFromBodyRotation(); const auto composed=tensor(r,frame.principalInertia());
    const double scale=*std::max_element(source.inertiaAboutCenter.begin(),source.inertiaAboutCenter.end(),
        [](double a,double b){return std::abs(a)<std::abs(b);});
    for (size_t i=0;i<9;++i) EXPECT_NEAR(composed[i],source.inertiaAboutCenter[i],std::abs(scale)*relativeTolerance);
    const double determinant=r[0]*(r[4]*r[8]-r[5]*r[7])-r[1]*(r[3]*r[8]-r[5]*r[6])+r[2]*(r[3]*r[7]-r[4]*r[6]);
    EXPECT_NEAR(determinant,1.0,1.0e-12); EXPECT_LE(frame.diagnostics().rotations,48u);
    EXPECT_LE(frame.diagnostics().normalizedReconstructionError,1.0e-12); EXPECT_LE(frame.diagnostics().orthogonalityError,1.0e-12);
    EXPECT_TRUE(std::ranges::is_sorted(frame.principalInertia()));
}
static_assert(!std::is_default_constructible_v<RigidMassFrame>);
static_assert(std::is_trivially_copyable_v<RigidMassFrame>);

TEST(RigidMassFrame, RationalAsymmetricTensorAndOffCenterImpulseMatchIndependentValues) {
    // R comes from q=(1,2,3,4)/sqrt(30); exact Fraction calculation lives in
    // SIM-02/stage-a1/golden.py. None of these reference inverses use Jacobi.
    auto input=golden();
    input.inertiaAboutCenter={88.0/25.0,-2.0/75.0,8.0/15.0,-2.0/75.0,161.0/75.0,2.0/5.0,8.0/15.0,2.0/5.0,10.0/3.0};
    const auto frame=prepare(input); checkFrame(input,frame); near(frame.principalInertia(),kD); EXPECT_EQ(frame.inverseMass(),.2);
    const auto response=frame.impulseAtRootPoint({2,3,-4},{1,-2,3});
    near(response.linear,{.4,.6,-.8});
    // Exact inverse applied to r×J = (-3/4,15/2,21/4).
    near(response.angular,{-1357.0/3600.0,5863.0/1800.0,56.0/45.0});
    const auto centered=frame.impulseAtRootPoint({2,3,-4},input.rootCenterOfMass); near(centered.angular,{0,0,0});
    near(frame.angularMomentumRoot(response.angular),{-.75,7.5,5.25});
}

TEST(RigidMassFrame, AllProperRotationsPreserveTensorEnergyAndTorqueResponse) {
    const auto input=golden(); const MassVector velocity{1.25,-2.0,.75},torque{-3,4,7};
    const auto base=prepare(input); const double energy=dot(velocity,multiply(input.inertiaAboutCenter,velocity));
    for (uint8_t rotation=0;rotation<24;++rotation) {
        MassMatrix turn{}; const auto entries=rotationMatrix({rotation})->elements;
        for (size_t i=0;i<9;++i) turn[i]=entries[i];
        auto moved=input; moved.inertiaAboutCenter=product(product(turn,input.inertiaAboutCenter),transpose(turn));
        moved.rootCenterOfMass=multiply(turn,input.rootCenterOfMass);
        const auto frame=prepare(moved); checkFrame(moved,frame); near(frame.principalInertia(),kD);
        const auto rotatedVelocity=multiply(turn,velocity);
        EXPECT_NEAR(dot(rotatedVelocity,frame.angularMomentumRoot(rotatedVelocity)),energy,1.0e-11);
        near(frame.angularResponseRoot(multiply(turn,torque)),multiply(turn,base.angularResponseRoot(torque)),1.0e-11);
        near(frame.rootPoint(frame.bodyPoint({2,-5,7})),{2,-5,7});
    }
}

TEST(RigidMassFrame, IsotropicRepeatedAndPermutedMomentsUseProperStableFrames) {
    for (const auto values:std::array<MassVector,5>{{{3,3,3},{2,2,3},{3,2,2},{4,3,2},{1.0e-10,1,1}}}) {
        for (const auto& orientation:std::array<MassMatrix,2>{{{1,0,0,0,1,0,0,0,1},kR}}) {
            auto input=golden(); input.inertiaAboutCenter=tensor(orientation,values);
            const auto a=prepare(input),b=prepare(input); checkFrame(input,a);
            EXPECT_EQ(a.rootFromBodyRotation(),b.rootFromBodyRotation()); EXPECT_EQ(a.principalInertia(),b.principalInertia());
        }
    }
    RigidMassInput isotropic{2,{0,0,0},{3,0,0,0,3,0,0,0,3}};
    EXPECT_EQ(prepare(isotropic).rootFromBodyRotation(),(MassMatrix{1,0,0,0,1,0,0,0,1}));
}

TEST(RigidMassFrame, FiniteExtremeScalesPreserveNormalizedFramesWithoutOverflow) {
    for (double factor:{1.0e-200,1.0e-100,1.0,1.0e100,1.0e200}) {
        auto input=golden(); input.massKg*=factor; for (auto& value:input.inertiaAboutCenter) value*=factor;
        const auto frame=prepare(input); checkFrame(input,frame);
        for (size_t i=0;i<3;++i) { EXPECT_NEAR(frame.principalInertia()[i]/factor,kD[i],1.0e-12);
            EXPECT_NEAR(frame.principalInverseInertia()[i]*factor,1.0/kD[i],1.0e-12); }
        EXPECT_NEAR(frame.inverseMass()*factor,.2,1.0e-15);
    }
}

TEST(RigidMassFrame, InvalidInputIsExplicitAndDoesNotChangeAnExistingFrame) {
    const auto existing=prepare(); const auto saved=existing.rootFromBodyRotation(); MassFrameError error;
    auto input=golden(); input.massKg=std::numeric_limits<double>::infinity(); EXPECT_FALSE(RigidMassFrame::prepare(input,error)); EXPECT_EQ(error,MassFrameError::NonFinite);
    for (double mass:{0.0,-1.0,std::numeric_limits<double>::denorm_min()}) {
        input=golden(); input.massKg=mass; EXPECT_FALSE(RigidMassFrame::prepare(input,error)); EXPECT_EQ(error,MassFrameError::InvalidMass);
    }
    input=golden(); input.rootCenterOfMass[1]=std::numeric_limits<double>::quiet_NaN(); EXPECT_FALSE(RigidMassFrame::prepare(input,error)); EXPECT_EQ(error,MassFrameError::NonFinite);
    input=golden(); input.inertiaAboutCenter[7]=std::numeric_limits<double>::infinity(); EXPECT_FALSE(RigidMassFrame::prepare(input,error)); EXPECT_EQ(error,MassFrameError::NonFinite);
    input=golden(); input.inertiaAboutCenter[1]+=.001; EXPECT_FALSE(RigidMassFrame::prepare(input,error)); EXPECT_EQ(error,MassFrameError::Asymmetric);
    input=golden(); input.inertiaAboutCenter={1,0,0,0,1,0,0,0,3}; EXPECT_FALSE(RigidMassFrame::prepare(input,error)); EXPECT_EQ(error,MassFrameError::NonPhysical);
    input.inertiaAboutCenter={-1,0,0,0,1,0,0,0,1}; EXPECT_FALSE(RigidMassFrame::prepare(input,error)); EXPECT_EQ(error,MassFrameError::NonPhysical);
    input.inertiaAboutCenter={1.0e-14,0,0,0,1,0,0,0,1}; EXPECT_FALSE(RigidMassFrame::prepare(input,error)); EXPECT_EQ(error,MassFrameError::IllConditioned);
    input.inertiaAboutCenter={}; EXPECT_FALSE(RigidMassFrame::prepare(input,error)); EXPECT_EQ(error,MassFrameError::IllConditioned);
    EXPECT_EQ(existing.rootFromBodyRotation(),saved); ASSERT_TRUE(RigidMassFrame::prepare(golden(),error)); EXPECT_EQ(error,MassFrameError::None);
}

TEST(RigidMassFrame, PoseAndVelocityRoundTripsRetainAuthoredOriginAndCenterOfMass) {
    const auto frame=prepare(); MassPose root{{10,20,30},{0,-1,0,1,0,0,0,0,1}};
    const auto body=frame.bodyPose(root); near(body.position,{10.5,20.25,30.75});
    const auto restored=frame.rootPose(body); near(restored.position,root.position);
    for (size_t i=0;i<9;++i) EXPECT_NEAR(restored.orientation[i],root.orientation[i],1.0e-12);
    const auto centerVelocity=frame.centerVelocity({3,4,5},{0,0,2},root.orientation); near(centerVelocity,{2.5,5,5});
    near(frame.rootVelocity(centerVelocity,{0,0,2},root.orientation),{3,4,5});
    // Analytically advance the COM and world orientation. The reconstructed
    // authored origin must orbit COM, not integrate its old origin linearly.
    const double dt=.125,angle=.25,c=std::cos(angle),s=std::sin(angle);
    const MassMatrix step{c,-s,0,s,c,0,0,0,1}; auto next=body;
    for (size_t i=0;i<3;++i) next.position[i]+=centerVelocity[i]*dt;
    next.orientation=product(step,body.orientation); const auto nextRoot=frame.rootPose(next);
    const auto newOffset=multiply(product(step,root.orientation),golden().rootCenterOfMass);
    MassVector expected{}; for (size_t i=0;i<3;++i) expected[i]=body.position[i]+centerVelocity[i]*dt-newOffset[i];
    near(nextRoot.position,expected); near(frame.bodyPose(nextRoot).position,next.position);
}

TEST(RigidMassFrame, ThousandsOfDensePhysicalFixturesRespectBoundedIteration) {
    uint32_t state=991; const auto random=[&](){state=state*1664525u+1013904223u;return static_cast<double>(state&65535u)/65535.0;};
    for (size_t fixture=0;fixture<2000;++fixture) {
        MassVector axis{random()-.5,random()-.5,random()-.5}; const double length=std::sqrt(dot(axis,axis));
        for(auto& value:axis) value/=length;
        const double angle=random()*6.0,c=std::cos(angle),s=std::sin(angle),v=1-c,x=axis[0],y=axis[1],z=axis[2];
        const MassMatrix rotation{c+x*x*v,x*y*v-z*s,x*z*v+y*s,y*x*v+z*s,c+y*y*v,y*z*v-x*s,z*x*v-y*s,z*y*v+x*s,c+z*z*v};
        const MassVector variance{.01+random(),.01+random(),.01+random()};
        const MassVector moments{variance[1]+variance[2],variance[0]+variance[2],variance[0]+variance[1]};
        RigidMassInput input{1.0+random()*1000,{random(),random(),random()},tensor(rotation,moments)};
        const auto frame=prepare(input); checkFrame(input,frame);
        const MassVector omega{random(),random(),random()};
        near(frame.angularMomentumRoot(omega),multiply(input.inertiaAboutCenter,omega),1.0e-11);
        near(frame.angularResponseRoot(frame.angularMomentumRoot(omega)),omega,1.0e-10);
    }
}

TEST(RigidMassFrame, ActualCompiledOffsetBallastRetainsFullTensorAndPointPlacement) {
    const WorldNamespace world{{'m','a','s','s','-','b','a','c','k','e','n','d','-','0','0','1'}};
    const auto id=[&](uint64_t n){return DurableId{world,n};};
    CatalogIssue catalogIssue; const auto catalog=PartCatalog::create(makeStarterCatalogDraft(),catalogIssue); ASSERT_TRUE(catalog);
    BuildSnapshot input; input.id=id(1); input.owner=id(2);
    PartInstance beam; beam.id=id(10); beam.owningBuild=input.id; beam.definition=starterPartKey(StarterPart::Beam);
    PartInstance ballast=beam; ballast.id=id(11); ballast.definition=starterPartKey(StarterPart::Ballast); ballast.placement.translation={-75,48,0};
    input.parts={beam,ballast}; Connection link; link.id=id(20); link.a={beam.id,SocketId{100}}; link.b={ballast.id,SocketId{2}};
    link.strength={1000,900,800,700}; input.connections={link}; AssemblyFunctionIssue issue;
    const auto compiled=CompiledAssembly::compile(input,*catalog,issue); ASSERT_TRUE(compiled); ASSERT_EQ(compiled->mass().roots().size(),1u);
    const auto& mass=compiled->mass().roots()[0].mass;
    RigidMassInput properties{mass.dryMassKg,{mass.localCenterOfMass.x,mass.localCenterOfMass.y,mass.localCenterOfMass.z},mass.inertia.elements};
    const auto frame=prepare(properties); checkFrame(properties,frame);
    EXPECT_NE(properties.inertiaAboutCenter[1],0.0); EXPECT_LT(frame.center()[0],-1.0);
    const auto source=compiled->functions().sockets()[0].rootFromSocket.translation;
    const MassVector point{static_cast<double>(source.x)/50.0,static_cast<double>(source.y)/50.0,static_cast<double>(source.z)/50.0};
    near(frame.rootPoint(frame.bodyPoint(point)),point);
    near(frame.angularMomentumRoot(frame.angularResponseRoot({0,0,1000})),{0,0,1000},1.0e-9);
}
} // namespace
} // namespace voxy::physics
