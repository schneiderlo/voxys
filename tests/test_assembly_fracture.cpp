#include "game/construction/assembly_fracture.hpp"

#include <gtest/gtest.h>
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace voxy::game::construction {
namespace {
constexpr WorldNamespace world{{'f','r','a','c','t','u','r','e','-','t','e','s','t','-','0','1'}};
DurableId id(uint64_t n){return {world,n};}
PartCatalog catalog(bool redundant=false) {
    auto draft=makeStarterCatalogDraft();auto& part=draft.definitions.front();
    // Asymmetric tensor/COM exposes root-origin versus COM mistakes.
    part.mass={2,{.1,-.1,.2},{{.05,.004,-.003,.004,.08,.006,-.003,.006,.1}}};
    if(redundant) {
        auto top=part.sockets[0],bottom=part.sockets[1];top.id=SocketId{3};bottom.id=SocketId{4};
        part.sockets.push_back(top);part.sockets.push_back(bottom);
    }
    auto other=part;other.key.id.counter=500;other.nameKey="salvage.part.fracture_probe";
    other.mass={3,{-.2,.1,-.1},{{.09,-.005,.002,-.005,.12,.003,.002,.003,.13}}};
    draft.definitions.push_back(other);CatalogIssue issue;auto result=PartCatalog::create(draft,issue);
    if(!result)throw std::runtime_error("fracture catalog: "+std::string(issue.field));
    return std::move(*result);
}
BuildSnapshot stack(size_t count=2) {
    BuildSnapshot build;build.id=id(1);build.owner=id(2);build.revision=TopologyRevision{7};
    for(size_t i=0;i<count;++i) {
        PartInstance part;part.id=id(10+i);part.owningBuild=build.id;part.definition=starterPartKey(StarterPart::Beam);
        part.placement.translation={0,static_cast<int32_t>(i)*kBrickBodyTicks,0};build.parts.push_back(part);
        if(i) {
            Connection link;link.id=id(1000+i);link.a={id(9+i),SocketId{1}};link.b={id(10+i),SocketId{2}};
            link.strength={1000,1000,1000,1000};build.connections.push_back(link);
        }
    }
    return build;
}
std::vector<std::byte> encoded(const BuildSnapshot& build,const PartCatalog& definitions) {
    std::vector<std::byte> bytes;const auto issue=encodeBuild(build,definitions,bytes);
    if(issue)throw std::runtime_error("fracture build: "+std::string(issue.field));
    return bytes;
}
AssemblyFracturePlan prepare(const BuildSnapshot& build,std::span<const DurableId> cuts,const PartCatalog& definitions) {
    AssemblyFractureIssue issue;auto result=AssemblyFracturePlan::prepare(build,build.revision,cuts,definitions,issue);
    if(!result)throw std::runtime_error("fracture prepare: "+std::to_string(static_cast<int>(issue.error))
        +" / "+std::string(issue.build.field)+" / "+std::string(issue.assembly.buoyancy.collision.assembly.build.field));
    return std::move(*result);
}
using V=physics::MassVector;using M=physics::MassMatrix;
V add(V a,V b){for(size_t i=0;i<3;++i)a[i]+=b[i];return a;}
V sub(V a,V b){for(size_t i=0;i<3;++i)a[i]-=b[i];return a;}
V scale(V a,double s){for(auto& x:a)x*=s;return a;}
double dot(V a,V b){double sum=0;for(size_t i=0;i<3;++i)sum+=a[i]*b[i];return sum;}
V cross(V a,V b){return {a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]};}
V mul(const M& m,V v){V out{};for(size_t r=0;r<3;++r)for(size_t c=0;c<3;++c)out[r]+=m[3*r+c]*v[c];return out;}
M transpose(M m){for(size_t r=0;r<3;++r)for(size_t c=r+1;c<3;++c)std::swap(m[3*r+c],m[3*c+r]);return m;}
struct Totals {double mass=0,energy=0;V momentum{},angular{};};
Totals totals(const AssemblyMassPlan& mass,std::span<const AssemblyRootMotion> states,V reference={}) {
    Totals out;
    for(const auto& root:mass.roots()) {
        const auto found=std::find_if(states.begin(),states.end(),[&](const auto& s){return s.root==root.key;});
        if(found==states.end())throw std::runtime_error("missing analytical root");
        const auto& s=*found;const auto& p=root.mass;const auto& r=s.worldFromRoot.orientation;
        const auto center=mul(r,{p.localCenterOfMass.x,p.localCenterOfMass.y,p.localCenterOfMass.z});
        const auto velocity=add(s.originVelocity,cross(s.angularVelocity,center));
        const auto linear=scale(velocity,p.dryMassKg);
        const auto spin=mul(r,mul(p.inertia.elements,mul(transpose(r),s.angularVelocity)));
        out.mass+=p.dryMassKg;out.momentum=add(out.momentum,linear);
        out.angular=add(out.angular,add(spin,cross(add(sub(s.worldFromRoot.position,reference),center),linear)));
        out.energy+=.5*(p.dryMassKg*dot(velocity,velocity)+dot(s.angularVelocity,spin));
    }
    return out;
}
void conserved(const Totals& a,const Totals& b) {
    EXPECT_NEAR(a.mass,b.mass,1e-10*std::max(1.,a.mass));
    EXPECT_NEAR(a.energy,b.energy,1e-10*std::max(1.,a.energy));
    for(size_t i=0;i<3;++i) {
        EXPECT_NEAR(a.momentum[i],b.momentum[i],1e-10*std::max(1.,std::abs(a.momentum[i])));
        EXPECT_NEAR(a.angular[i],b.angular[i],1e-9*std::max(1.,std::abs(a.angular[i])));
    }
}
AssemblyMotionSource source(const AssemblyFracturePlan& plan,std::span<const AssemblyRootMotion> states) {
    return {plan.before().mass().build(),plan.before().mass().revision(),SimulationTick{71},states};
}

TEST(AssemblyFracture, CutPreservesEveryOwnedPartAndSerializesTheDisabledBond) {
    const auto definitions=catalog();auto build=stack();build.editLease=EditLease{id(3),AuthorityEpoch{2},SimulationTick{100}};
    build.parts[0].provenance={PartOrigin::StarterLoan,id(90)};build.parts[1].health=1234;
    build.parts[1].paint={12,34,56,255};const auto original=encoded(build,definitions);
    const std::array cuts{id(1001)};auto plan=prepare(build,cuts,definitions);
    EXPECT_EQ(encoded(build,definitions),original);EXPECT_EQ(plan.afterBuild().parts,build.parts);
    EXPECT_EQ(plan.afterBuild().id,build.id);EXPECT_EQ(plan.afterBuild().owner,build.owner);EXPECT_EQ(plan.afterBuild().editLease,build.editLease);
    EXPECT_EQ(plan.afterBuild().revision,TopologyRevision{8});ASSERT_EQ(plan.afterBuild().connections.size(),1u);
    EXPECT_FALSE(plan.afterBuild().connections[0].enabled);EXPECT_EQ(plan.afterBuild().connections[0].damage,kFullHealth);
    EXPECT_EQ(plan.afterBuild().connections[0].id,build.connections[0].id);
    ASSERT_EQ(plan.before().mass().roots().size(),1u);ASSERT_EQ(plan.after().mass().roots().size(),2u);
    EXPECT_EQ(plan.after().functions().sockets().size(),plan.before().functions().sockets().size());
    const auto& link=plan.after().functions().connections()[0];
    EXPECT_EQ(plan.after().functions().sockets()[link.socketA].root,0u);
    EXPECT_EQ(plan.after().functions().sockets()[link.socketB].root,1u);
    uint64_t volume=0;for(const auto& root:plan.after().buoyancy().roots())volume+=root.coverage.stats().volumeTicks3;
    EXPECT_EQ(volume,plan.before().buoyancy().roots()[0].coverage.stats().volumeTicks3);
    EXPECT_EQ(plan.after().collision().roots().size(),2u);
    std::optional<BuildModel> loaded;ASSERT_FALSE(decodeBuild(encoded(plan.afterBuild(),definitions),definitions,loaded));
    EXPECT_EQ(encoded(loaded->snapshot(),definitions),encoded(plan.afterBuild(),definitions));
    AssemblyFractureIssue issue;EXPECT_FALSE(AssemblyFracturePlan::prepare(plan.afterBuild(),TopologyRevision{8},cuts,definitions,issue));
    EXPECT_EQ(issue.error,AssemblyFractureError::InactiveConnection);
}

TEST(AssemblyFracture, AlternateWeldKeepsOneBodyUntilTheLastPathIsCut) {
    const auto definitions=catalog(true);auto build=stack();auto extra=build.connections[0];
    extra.id=id(1002);extra.a.socket=SocketId{3};extra.b.socket=SocketId{4};build.connections.push_back(extra);
    const std::array first{id(1001)},second{id(1002)};auto kept=prepare(build,first,definitions);
    EXPECT_EQ(kept.after().mass().roots().size(),1u);
    auto split=prepare(kept.afterBuild(),second,definitions);EXPECT_EQ(split.after().mass().roots().size(),2u);
    const std::array unordered{id(1002),id(1001)};auto simultaneous=prepare(build,unordered,definitions);
    EXPECT_EQ(simultaneous.cuts()[0],id(1001));EXPECT_EQ(simultaneous.cuts()[1],id(1002));
    EXPECT_EQ(simultaneous.after().mass().roots().size(),2u);
}

TEST(AssemblyFracture, ChildOriginInheritsAngularVelocityAtItsActualOffset) {
    const auto definitions=catalog();const std::array cuts{id(1001)};auto plan=prepare(stack(),cuts,definitions);
    const std::array states{AssemblyRootMotion{id(10),{{100,200,300},{1,0,0,0,1,0,0,0,1}},{1,2,3},{0,0,2}}};
    AssemblyFractureIssue issue;const auto after=plan.inheritMotion(source(plan,states),SimulationTick{71},issue);ASSERT_TRUE(after);
    ASSERT_EQ(after->size(),2u);EXPECT_EQ((*after)[0].originVelocity,states[0].originVelocity);
    EXPECT_NEAR((*after)[1].worldFromRoot.position[1],200.96,1e-12);
    EXPECT_NEAR((*after)[1].originVelocity[0],-.92,1e-12);
    EXPECT_EQ((*after)[1].originVelocity[1],2);EXPECT_EQ((*after)[1].originVelocity[2],3);
    EXPECT_EQ((*after)[1].angularVelocity,states[0].angularVelocity);
    conserved(totals(plan.before().mass(),states),totals(plan.after().mass(),*after));
}

TEST(AssemblyFracture, RotatedAsymmetricSplitConservesMassMomentumAndEnergyForEveryCubeRotation) {
    const auto definitions=catalog();auto build=stack(3);build.parts[1].definition=definitions.definitions().back().key;
    const std::array cuts{id(1001),id(1002)};auto plan=prepare(build,cuts,definitions);
    for(uint8_t rotation=0;rotation<24;++rotation) {
        const auto axes=rotationMatrix(CubeRotation{rotation});M matrix{};
        ASSERT_TRUE(axes);for(size_t i=0;i<9;++i)matrix[i]=axes->elements[i];
        const std::array states{AssemblyRootMotion{id(10),{{80,-20,60},matrix},{1.25,-3.5,7},{2.5,-.75,1.5}}};
        AssemblyFractureIssue issue;const auto after=plan.inheritMotion(source(plan,states),SimulationTick{71},issue);ASSERT_TRUE(after)<<int(rotation);
        conserved(totals(plan.before().mass(),states),totals(plan.after().mass(),*after));
    }
    const double c=std::cos(.37),s=std::sin(.37),b=std::cos(.61),a=std::sin(.61);
    const M rotation{c,-s*b,s*a,s,c*b,-c*a,0,a,b};
    const std::array states{AssemblyRootMotion{id(10),{{0,0,0},rotation},{1.25,-3.5,7},{2.5,-.75,1.5}}};
    AssemblyFractureIssue issue;const auto after=plan.inheritMotion(source(plan,states),SimulationTick{71},issue);ASSERT_TRUE(after);
    conserved(totals(plan.before().mass(),states),totals(plan.after().mass(),*after));
}

TEST(AssemblyFracture, ExistingIndependentRootsKeepTheirOwnMotionAndAcceptUnorderedObservations) {
    const auto definitions=catalog();auto build=stack(4);build.connections[1].enabled=false;
    const std::array cuts{id(1001)};auto plan=prepare(build,cuts,definitions);
    ASSERT_EQ(plan.before().mass().roots().size(),2u);ASSERT_EQ(plan.after().mass().roots().size(),3u);
    const std::array states{AssemblyRootMotion{id(12),{{-20,30,40},{}},{-4,1,2},{1,0,0}},
        AssemblyRootMotion{id(10),{{1,2,3},{}},{3,2,1},{0,0,2}}};
    auto ordered=states;for(auto& state:ordered)state.worldFromRoot.orientation={1,0,0,0,1,0,0,0,1};
    AssemblyFractureIssue issue;const auto after=plan.inheritMotion(source(plan,ordered),SimulationTick{71},issue);ASSERT_TRUE(after);
    EXPECT_EQ((*after)[2].root,id(12));EXPECT_EQ((*after)[2].originVelocity,ordered[0].originVelocity);
    EXPECT_EQ((*after)[2].worldFromRoot.position,ordered[0].worldFromRoot.position);
    conserved(totals(plan.before().mass(),ordered),totals(plan.after().mass(),*after));
}

TEST(AssemblyFracture, Full256PartBuildProduces64CompleteRootsAndRefusesToDropExcessFragments) {
    const auto definitions=catalog();auto build=stack(kMaximumBuildParts);const auto original=encoded(build,definitions);
    std::vector<DurableId> cuts;for(uint64_t i=4;i<kMaximumBuildParts;i+=4)cuts.push_back(id(1000+i));
    auto plan=prepare(build,cuts,definitions);ASSERT_EQ(plan.after().mass().roots().size(),64u);
    EXPECT_EQ(plan.after().mass().parts().size(),256u);EXPECT_EQ(plan.after().functions().modules().size(),256u);
    EXPECT_EQ(plan.after().collision().roots().size(),64u);EXPECT_EQ(plan.after().buoyancy().roots().size(),64u);
    for(const auto& root:plan.after().mass().roots())EXPECT_EQ(root.partCount,4u);
    cuts.push_back(id(1001));AssemblyFractureIssue issue;
    EXPECT_FALSE(AssemblyFracturePlan::prepare(build,build.revision,cuts,definitions,issue));
    EXPECT_EQ(issue.error,AssemblyFractureError::Compilation);
    EXPECT_EQ(issue.assembly.buoyancy.collision.assembly.error,AssemblyError::Capacity);
    EXPECT_EQ(encoded(build,definitions),original);
}

TEST(AssemblyFracture, BadCutOrInsufficientPreparedRootCapacityNeverChangesInput) {
    const auto definitions=catalog();auto build=stack();const auto original=encoded(build,definitions);
    AssemblyFractureIssue issue;const std::array cut{id(1001)},unknown{id(9999)};
    const std::array duplicates{id(1001),id(1001)};
    EXPECT_FALSE(AssemblyFracturePlan::prepare(build,TopologyRevision{6},cut,definitions,issue));EXPECT_EQ(issue.error,AssemblyFractureError::StaleRevision);
    EXPECT_FALSE(AssemblyFracturePlan::prepare(build,build.revision,{},definitions,issue));EXPECT_EQ(issue.error,AssemblyFractureError::InvalidRequest);
    EXPECT_FALSE(AssemblyFracturePlan::prepare(build,build.revision,duplicates,definitions,issue));EXPECT_EQ(issue.error,AssemblyFractureError::InvalidRequest);
    EXPECT_FALSE(AssemblyFracturePlan::prepare(build,build.revision,unknown,definitions,issue));EXPECT_EQ(issue.error,AssemblyFractureError::UnknownConnection);
    AssemblyCompileProfile profile;profile.mass.roots=1;
    EXPECT_FALSE(AssemblyFracturePlan::prepare(build,build.revision,cut,definitions,issue,profile));EXPECT_EQ(issue.error,AssemblyFractureError::Compilation);
    EXPECT_EQ(encoded(build,definitions),original);
    build.revision=TopologyRevision{UINT64_MAX};EXPECT_FALSE(AssemblyFracturePlan::prepare(build,build.revision,cut,definitions,issue));
    EXPECT_EQ(issue.error,AssemblyFractureError::RevisionExhausted);
}

TEST(AssemblyFracture, MixedOrInvalidMotionIsRejectedWithoutPartialOutput) {
    const auto definitions=catalog();const std::array cuts{id(1001)};auto plan=prepare(stack(),cuts,definitions);
    const std::array good{AssemblyRootMotion{id(10),{{1,2,3},{1,0,0,0,1,0,0,0,1}},{1,2,3},{4,5,6}}};
    AssemblyFractureIssue issue;
    for(int fault=0;fault<9;++fault) {
        auto states=good;auto observation=source(plan,states);
        switch(fault) {
        case 0:observation.build=id(9);break;
        case 1:observation.revision=TopologyRevision{8};break;
        case 2:observation.completedTick=SimulationTick{70};break;
        case 3:observation.roots={};break;
        case 4:states[0].root=id(999);break;
        case 5:states[0].worldFromRoot.orientation[0]=-1;break;
        case 6:states[0].worldFromRoot.orientation[0]=1.1;break;
        case 7:states[0].originVelocity[0]=std::numeric_limits<double>::infinity();break;
        case 8:states[0].angularVelocity[0]=std::numeric_limits<double>::quiet_NaN();break;
        }
        EXPECT_FALSE(plan.inheritMotion(observation,SimulationTick{71},issue))<<fault;
    }
    auto build=stack(3);build.connections[1].enabled=false;auto multi=prepare(build,cuts,definitions);
    const std::array duplicate{good[0],good[0]};
    EXPECT_FALSE(multi.inheritMotion(source(multi,duplicate),SimulationTick{71},issue));EXPECT_EQ(issue.error,AssemblyFractureError::MotionRoots);
    auto distant=stack(3);const std::array doubleCut{id(1001),id(1002)};auto farther=prepare(distant,doubleCut,definitions);
    auto huge=good;huge[0].angularVelocity[0]=std::numeric_limits<double>::max();
    EXPECT_FALSE(farther.inheritMotion(source(farther,huge),SimulationTick{71},issue));EXPECT_EQ(issue.error,AssemblyFractureError::InvalidMotion);
    EXPECT_TRUE(plan.inheritMotion(source(plan,good),SimulationTick{71},issue));
}
} // namespace
} // namespace voxy::game::construction
