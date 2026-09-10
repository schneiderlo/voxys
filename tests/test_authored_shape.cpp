#include "physics/authored_shape_pool.hpp"
#include "game/construction/compiled_assembly.hpp"

#include <gtest/gtest.h>
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <type_traits>

namespace {
using namespace voxy::physics;
using namespace voxy::geometry;
namespace game = voxy::game::construction;
constexpr MassMatrix kRotation{2.0/15.0,-2.0/3.0,11.0/15.0,14.0/15.0,1.0/3.0,2.0/15.0,-1.0/3.0,2.0/3.0,2.0/3.0};
MassMatrix tensor(const MassMatrix& r,MassVector d) {
    MassMatrix result{};
    for (size_t i=0;i<3;++i) for (size_t j=0;j<3;++j) for (size_t k=0;k<3;++k) result[i*3+j]+=r[i*3+k]*r[j*3+k]*d[k];
    return result;
}
RigidMassInput properties() { return {5,{.25,-.5,.75},tensor(kRotation,{2,3,4})}; }
BoxUnion geometry(std::vector<UnionBox> boxes={{{{-50,-50,-50},{50,50,50}},7}}) {
    BoxUnionIssue issue; auto result=BoxUnion::compile(boxes,issue);
    if (!result) throw std::runtime_error("shape geometry fixture");
    return std::move(*result);
}
AuthoredShape shape() {
    AuthoredShapeIssue issue; auto result=AuthoredShape::prepare(geometry(),properties(),issue);
    if (!result) throw std::runtime_error("shape fixture "+std::to_string(static_cast<int>(issue.error)));
    return std::move(*result);
}
AuthoredShapePool pool(uint64_t id=17,AuthoredShapePoolLimits limits={}) {
    ShapePoolError error; auto result=AuthoredShapePool::create(id,error,limits);
    if (!result) throw std::runtime_error("pool fixture");
    return std::move(*result);
}
ShapeHandle insert(AuthoredShapePool& target) {
    auto prepared=shape(); ShapePoolError error; const auto handle=target.insert(std::move(prepared),error);
    if (!handle.valid()) throw std::runtime_error("pool insertion fixture");
    return handle;
}
std::array<int32_t,3> point(GridPosition p) { return {p.x,p.y,p.z}; }
MassVector rotate(const std::array<float,4>& q,MassVector v,bool inverse=false) {
    // Same cross-product form as shader quaternion rotation; double reference
    // arithmetic here isolates the error from quantizing the stored values.
    const double sign=inverse ? -1 : 1, x=static_cast<double>(q[0])*sign,y=static_cast<double>(q[1])*sign,z=static_cast<double>(q[2])*sign,w=static_cast<double>(q[3]);
    const MassVector t{2*(y*v[2]-z*v[1]),2*(z*v[0]-x*v[2]),2*(x*v[1]-y*v[0])};
    return {v[0]+w*t[0]+y*t[2]-z*t[1],v[1]+w*t[1]+z*t[0]-x*t[2],v[2]+w*t[2]+x*t[1]-y*t[0]};
}
static_assert(!std::is_copy_constructible_v<AuthoredShapePool>);
static_assert(std::is_nothrow_move_constructible_v<AuthoredShapePool>);

TEST(AuthoredShape, ExactExteriorPatchesAndBvhSurvivePacking) {
    const auto source=geometry({{{{0,0,0},{4,4,4}},1},{{{4,1,1},{6,3,3}},2}});
    AuthoredShapeIssue issue; const auto result=AuthoredShape::prepare(source,properties(),issue); ASSERT_TRUE(result);
    EXPECT_EQ(issue.error,AuthoredShapeError::None);
    ASSERT_EQ(result->cells().size(),source.cells().size()); ASSERT_EQ(result->faces().size(),source.faces().size());
    ASSERT_EQ(result->nodes().size(),source.bvh().size()); EXPECT_EQ(result->rootBounds(),source.bvh()[0].bounds);
    size_t total=0,partial=0;
    for (size_t i=0;i<result->cells().size();++i) {
        const auto& cell=result->cells()[i];
        EXPECT_EQ(cell.minimum,point(source.cells()[i].bounds.minimum)); EXPECT_EQ(cell.maximum,point(source.cells()[i].bounds.maximum));
        EXPECT_EQ(cell.source,source.cells()[i].source); EXPECT_EQ(cell.reserved,(std::array<uint32_t,3>{}));
        for (uint32_t j=0;j<cell.faceCount;++j) {
            const auto& face=result->faces()[cell.firstFace+j];
            EXPECT_EQ(face.cell,i); EXPECT_EQ(face.source,cell.source);
            EXPECT_EQ(face.reserved,(std::array<uint32_t,2>{})); ++total;
            if (face.axis==0 && face.sign==1 && face.minimum[0]==4) ++partial;
        }
    }
    EXPECT_EQ(total,result->faces().size()); EXPECT_EQ(partial,4u);
    for (size_t i=0;i<source.faces().size();++i) {
        const auto& a=result->faces()[i]; const auto& b=source.faces()[i];
        EXPECT_EQ(a.minimum,point(b.bounds.minimum)); EXPECT_EQ(a.maximum,point(b.bounds.maximum));
        EXPECT_EQ(a.axis,b.axis); EXPECT_EQ(a.sign,b.sign);
    }
    for (size_t i=0;i<source.bvh().size();++i) {
        const auto& a=result->nodes()[i]; const auto& b=source.bvh()[i];
        EXPECT_EQ(a.minimum,point(b.bounds.minimum)); EXPECT_EQ(a.maximum,point(b.bounds.maximum));
        EXPECT_EQ(a.cell,b.cell); EXPECT_EQ(a.escape,b.escape);
    }
}

TEST(AuthoredShape, FullyEnclosedCellHasNoContactFaces) {
    // A small first source becomes an interior cell when a larger source fills
    // its surroundings. Contacts must not use its six raw box faces.
    const auto source=geometry({{{{-1,-1,-1},{1,1,1}},1},{{{-3,-3,-3},{3,3,3}},2}});
    AuthoredShapeIssue issue; const auto result=AuthoredShape::prepare(source,properties(),issue); ASSERT_TRUE(result);
    ASSERT_EQ(result->cells()[0].source,1u); EXPECT_EQ(result->cells()[0].faceCount,0u);
    EXPECT_FALSE(std::ranges::any_of(result->faces(),[](const auto& f){return f.source==1;}));
}

TEST(AuthoredShape, QuantizedFrameMatchesIndependentOffCenterImpulse) {
    const auto result=shape(); const auto& packed=result.packedMass();
    EXPECT_NEAR(packed.centerInverseMass[3],.2,1.0e-8);
    const MassVector angularImpulse{-.75,7.5,5.25};
    auto body=rotate(packed.rootFromBodyQuaternion,angularImpulse,true);
    for (size_t i=0;i<3;++i) body[i]*=static_cast<double>(packed.inverseInertiaRadius[i]);
    const auto response=rotate(packed.rootFromBodyQuaternion,body);
    const MassVector wanted{-1357.0/3600.0,5863.0/1800.0,56.0/45.0};
    for (size_t i=0;i<3;++i) EXPECT_NEAR(response[i],wanted[i],2.0e-6);
    const MassVector rootPoint{1,-2,3}; MassVector offset{};
    for(size_t i=0;i<3;++i) offset[i]=rootPoint[i]-static_cast<double>(packed.centerInverseMass[i]);
    const auto roundTrip=rotate(packed.rootFromBodyQuaternion,rotate(packed.rootFromBodyQuaternion,offset,true));
    for(size_t i=0;i<3;++i) EXPECT_NEAR(roundTrip[i]+static_cast<double>(packed.centerInverseMass[i]),rootPoint[i],2.0e-6);
}

TEST(AuthoredShape, RotationBranchesAndDenseFramesRemainRepresentable) {
    const auto source=geometry(); AuthoredShapeIssue issue;
    for(uint8_t rotation=0;rotation<24;++rotation) {
        const auto basis=game::rotationMatrix(game::CubeRotation{rotation}); ASSERT_TRUE(basis);
        MassMatrix r{}; for(size_t i=0;i<9;++i) r[i]=basis->elements[i];
        auto input=properties(); input.inertiaAboutCenter=tensor(r,{2,3,4});
        const auto result=AuthoredShape::prepare(source,input,issue); ASSERT_TRUE(result) << unsigned(rotation);
        const auto& q=result->packedMass().rootFromBodyQuaternion;
        for(size_t i=0;i<3;++i) {
            MassVector axis{}; axis[i]=1;
            const auto actual=rotate(q,axis),wanted=result->massFrame().rootVector(axis);
            for(size_t j=0;j<3;++j) EXPECT_NEAR(actual[j],wanted[j],1.0e-6);
        }
    }
    for(uint32_t i=0;i<400;++i) {
        const double angle=static_cast<double>(i)*.037, c=std::cos(angle),s=std::sin(angle);
        const MassMatrix r{c,-s,0,s,c,0,0,0,1};
        auto input=properties(); input.inertiaAboutCenter=tensor(r,{1,2,2.5});
        const auto result=AuthoredShape::prepare(source,input,issue); ASSERT_TRUE(result) << i;
    }
}

TEST(AuthoredShape, RadiusContainsEveryCornerRelativeToQuantizedCom) {
    const auto source=geometry({{{{-12800,-12800,-12800},{12800,12800,12800}},1}});
    auto input=properties(); input.rootCenterOfMass={255.999999,-255.123456,255.876543};
    AuthoredShapeIssue issue; const auto result=AuthoredShape::prepare(source,input,issue); ASSERT_TRUE(result);
    const auto& packed=result->packedMass();
    for(uint32_t bits=0;bits<8;++bits) {
        double distance=0;
        for(size_t i=0;i<3;++i) { const double offset=((bits&(1u<<i)) ? 256.0 : -256.0)-static_cast<double>(packed.centerInverseMass[i]); distance+=offset*offset; }
        EXPECT_GT(packed.inverseInertiaRadius[3],std::sqrt(distance));
    }
}

TEST(AuthoredShape, UnsupportedFloatRangesAndAmplifiedBasisErrorReject) {
    const auto source=geometry(); AuthoredShapeIssue issue;
    for(double scale:{1.0e-200,1.0e200}) {
        auto input=properties(); input.massKg=scale;
        EXPECT_FALSE(AuthoredShape::prepare(source,input,issue)); EXPECT_EQ(issue.error,AuthoredShapeError::Unrepresentable);
        input=properties(); for(auto& value:input.inertiaAboutCenter) value*=scale;
        EXPECT_FALSE(AuthoredShape::prepare(source,input,issue)); EXPECT_EQ(issue.error,AuthoredShapeError::Unrepresentable);
    }
    auto input=properties(); input.rootCenterOfMass[0]=256.001;
    EXPECT_FALSE(AuthoredShape::prepare(source,input,issue)); EXPECT_EQ(issue.error,AuthoredShapeError::Unrepresentable);
    input=properties(); input.inertiaAboutCenter=tensor(kRotation,{1.0e-10,3,3});
    EXPECT_FALSE(AuthoredShape::prepare(source,input,issue)); EXPECT_EQ(issue.error,AuthoredShapeError::Unrepresentable);
    input=properties(); input.massKg=std::numeric_limits<double>::quiet_NaN();
    EXPECT_FALSE(AuthoredShape::prepare(source,input,issue)); EXPECT_EQ(issue.error,AuthoredShapeError::Mass); EXPECT_EQ(issue.mass,MassFrameError::NonFinite);
}

TEST(AuthoredShape, IndependentCapacityLimitsAndMovedFromGeometryReject) {
    auto source=geometry({{{{0,0,0},{4,4,4}},1},{{{4,1,1},{6,3,3}},2}});
    AuthoredShapeIssue issue;
    for(size_t axis=0;axis<3;++axis) {
        AuthoredShapeLimits limits;
        if(axis==0) limits.cells=1; else if(axis==1) limits.faces=1; else limits.nodes=1;
        EXPECT_FALSE(AuthoredShape::prepare(source,properties(),issue,limits)); EXPECT_EQ(issue.error,AuthoredShapeError::Capacity);
    }
    auto limits=AuthoredShapeLimits{}; limits.cells=0;
    EXPECT_FALSE(AuthoredShape::prepare(source,properties(),issue,limits)); EXPECT_EQ(issue.error,AuthoredShapeError::InvalidProfile);
    auto kept=std::move(source);
    EXPECT_FALSE(AuthoredShape::prepare(source,properties(),issue)); EXPECT_EQ(issue.error,AuthoredShapeError::InvalidGeometry);
    EXPECT_TRUE(AuthoredShape::prepare(kept,properties(),issue)); EXPECT_EQ(issue.error,AuthoredShapeError::None);
}

TEST(AuthoredShape, ActualCompiledOffsetBallastPreservesContactProvenance) {
    using namespace game;
    const WorldNamespace world{{'s','h','a','p','e','-','b','a','c','k','e','n','d','0','0','1'}};
    const auto id=[&](uint64_t n){return DurableId{world,n};};
    CatalogIssue catalogIssue; const auto catalog=PartCatalog::create(makeStarterCatalogDraft(),catalogIssue); ASSERT_TRUE(catalog);
    BuildSnapshot input; input.id=id(1); input.owner=id(2);
    PartInstance beam; beam.id=id(10); beam.owningBuild=input.id; beam.definition=starterPartKey(StarterPart::Beam);
    PartInstance ballast=beam; ballast.id=id(11); ballast.definition=starterPartKey(StarterPart::Ballast); ballast.placement.translation={-75,48,0};
    input.parts={beam,ballast}; Connection link; link.id=id(20); link.a={beam.id,SocketId{100}}; link.b={ballast.id,SocketId{2}};
    link.strength={1000,900,800,700}; input.connections={link}; AssemblyFunctionIssue compileIssue;
    auto compiled=CompiledAssembly::compile(input,*catalog,compileIssue); ASSERT_TRUE(compiled); ASSERT_EQ(compiled->collision().roots().size(),1u);
    const auto& mass=compiled->mass().roots()[0].mass;
    RigidMassInput physical{mass.dryMassKg,{mass.localCenterOfMass.x,mass.localCenterOfMass.y,mass.localCenterOfMass.z},mass.inertia.elements};
    AuthoredShapeIssue issue; auto prepared=AuthoredShape::prepare(compiled->collision().roots()[0].shape,physical,issue); ASSERT_TRUE(prepared);
    EXPECT_NE(physical.inertiaAboutCenter[1],0.0); EXPECT_LT(prepared->packedMass().centerInverseMass[0],-1.0f);
    for(const auto& face:prepared->faces()) {
        const auto* provenance=compiled->collision().source(face.source); ASSERT_NE(provenance,nullptr);
        EXPECT_TRUE(provenance->part==beam.id || provenance->part==ballast.id);
        EXPECT_EQ(provenance->root,0u); EXPECT_TRUE(provenance->proxy.valid());
    }
    const auto first=prepared->faces()[0]; compiled.reset(); input.parts.clear(); input.connections.clear();
    auto registry=pool(); ShapePoolError error; const auto handle=registry.insert(std::move(*prepared),error); ASSERT_TRUE(handle.valid());
    EXPECT_EQ(registry.get(handle)->faces()[0].minimum,first.minimum);
    EXPECT_EQ(registry.get(handle)->faces()[0].source,first.source);
}

TEST(AuthoredShape, EntireStarterCatalogPreparesInEveryPermittedRotation) {
    using namespace game;
    const WorldNamespace world{{'s','h','a','p','e','-','c','a','t','a','l','o','g','0','0','1'}};
    const auto id=[&](uint64_t n){return DurableId{world,n};};
    CatalogIssue catalogIssue; const auto catalog=PartCatalog::create(makeStarterCatalogDraft(),catalogIssue); ASSERT_TRUE(catalog);
    uint32_t preparedCount=0;
    for(const auto& definition:catalog->definitions()) for(uint8_t rotation=0;rotation<24;++rotation) {
        if((definition.permittedRotationMask&(1u<<rotation))==0) continue;
        BuildSnapshot input; input.id=id(1); input.owner=id(2);
        PartInstance part; part.id=id(3); part.owningBuild=input.id; part.definition=definition.key; part.placement.rotation={rotation};
        part.settings=defaultModuleSettings(definition);
        input.parts={part}; AssemblyFunctionIssue compileIssue;
        const auto compiled=CompiledAssembly::compile(input,*catalog,compileIssue); ASSERT_TRUE(compiled) << definition.nameKey;
        ASSERT_EQ(compiled->mass().roots().size(),1u); const auto& mass=compiled->mass().roots()[0].mass;
        const RigidMassInput physical{mass.dryMassKg,{mass.localCenterOfMass.x,mass.localCenterOfMass.y,mass.localCenterOfMass.z},mass.inertia.elements};
        AuthoredShapeIssue issue;
        const auto prepared=AuthoredShape::prepare(compiled->collision().roots()[0].shape,physical,issue);
        ASSERT_TRUE(prepared) << definition.nameKey << " rotation " << unsigned(rotation); ++preparedCount;
    }
    EXPECT_EQ(catalog->definitions().size(),12u); EXPECT_EQ(preparedCount,288u);
}

TEST(AuthoredShapePool, RetirementWaitsForBothGpuCompletionAndRetainedReaders) {
    AuthoredShapePoolLimits limits; limits.slots=1; auto registry=pool(17,limits);
    const auto first=insert(registry); const auto* held=registry.get(first); ASSERT_NE(held,nullptr);
    const auto cost=held->cost(); EXPECT_EQ(registry.retain(first),ShapePoolError::None);
    const std::array used{first,first}; EXPECT_EQ(registry.submitted(5,used),ShapePoolError::None);
    EXPECT_EQ(registry.retire(first),ShapePoolError::None); EXPECT_EQ(registry.retire(first),ShapePoolError::None);
    EXPECT_EQ(registry.retain(first),ShapePoolError::Retiring); EXPECT_EQ(registry.submitted(6,used),ShapePoolError::Retiring);
    EXPECT_EQ(registry.completed(4),ShapePoolError::None); EXPECT_EQ(registry.get(first),held);
    auto replacement=shape(); ShapePoolError error;
    EXPECT_FALSE(registry.insert(std::move(replacement),error).valid()); EXPECT_EQ(error,ShapePoolError::Capacity);
    EXPECT_EQ(replacement.cost(),cost); EXPECT_EQ(registry.stats().charged,cost);
    EXPECT_EQ(registry.completed(5),ShapePoolError::None); EXPECT_EQ(registry.get(first),held);
    EXPECT_EQ(registry.release(first),ShapePoolError::None); EXPECT_EQ(registry.get(first),nullptr);
    const auto second=registry.insert(std::move(replacement),error); ASSERT_TRUE(second.valid());
    EXPECT_EQ(second.index,first.index); EXPECT_EQ(second.generation,first.generation+1);
    EXPECT_EQ(registry.get(first),nullptr); EXPECT_EQ(registry.release(first),ShapePoolError::InvalidHandle);
}

TEST(AuthoredShapePool, DelayedGpuUseAlonePreventsReuse) {
    auto registry=pool(); const auto handle=insert(registry);
    const std::array used{handle}; EXPECT_EQ(registry.submitted(3,used),ShapePoolError::None);
    EXPECT_EQ(registry.retire(handle),ShapePoolError::None); EXPECT_EQ(registry.stats().retiring,1u);
    EXPECT_EQ(registry.completed(2),ShapePoolError::None); ASSERT_NE(registry.get(handle),nullptr);
    EXPECT_EQ(registry.completed(3),ShapePoolError::None); EXPECT_EQ(registry.get(handle),nullptr);
    EXPECT_EQ(registry.stats().charged,(ShapeResourceCost{}));
}

TEST(AuthoredShapePool, InvalidSubmissionHasNoPartialEffects) {
    auto registry=pool(); const auto first=insert(registry),second=insert(registry);
    const auto before=registry.stats();
    std::array<ShapeHandle,513> oversized{}; oversized.fill(first);
    EXPECT_EQ(registry.submitted(7,oversized),ShapePoolError::Capacity); EXPECT_EQ(registry.stats(),before);
    const std::array invalid{first,ShapeHandle{second.index,second.generation,99}};
    EXPECT_EQ(registry.submitted(7,invalid),ShapePoolError::InvalidHandle); EXPECT_EQ(registry.stats(),before);
    // If first had been marked by the rejected batch, this would leak until 7.
    EXPECT_EQ(registry.retire(first),ShapePoolError::None); EXPECT_EQ(registry.get(first),nullptr);
    const std::array valid{second}; EXPECT_EQ(registry.submitted(7,valid),ShapePoolError::None);
    const auto submitted=registry.stats(); EXPECT_EQ(registry.submitted(7,valid),ShapePoolError::InvalidSubmission);
    EXPECT_EQ(registry.completed(8),ShapePoolError::InvalidSubmission); EXPECT_EQ(registry.stats(),submitted);
    EXPECT_EQ(registry.completed(7),ShapePoolError::None); EXPECT_EQ(registry.completed(6),ShapePoolError::InvalidSubmission);
}

TEST(AuthoredShapePool, EveryWorldBudgetCountsRetiringResourcesAndPreservesRejectedInput) {
    const auto cost=shape().cost();
    for(size_t budget=0;budget<5;++budget) {
        AuthoredShapePoolLimits limits;
        if(budget==0) limits.slots=1; else if(budget==1) limits.cells=cost.cells;
        else if(budget==2) limits.faces=cost.faces; else if(budget==3) limits.nodes=cost.nodes; else limits.bytes=cost.bytes;
        auto registry=pool(17,limits); const auto first=insert(registry);
        const std::array used{first}; EXPECT_EQ(registry.submitted(1,used),ShapePoolError::None); EXPECT_EQ(registry.retire(first),ShapePoolError::None);
        auto replacement=shape(); ShapePoolError error; const auto before=registry.stats();
        EXPECT_FALSE(registry.insert(std::move(replacement),error).valid()); EXPECT_EQ(error,ShapePoolError::Capacity);
        EXPECT_EQ(registry.stats(),before); EXPECT_EQ(replacement.cost(),cost);
        EXPECT_EQ(registry.completed(1),ShapePoolError::None);
        EXPECT_TRUE(registry.insert(std::move(replacement),error).valid()); EXPECT_EQ(error,ShapePoolError::None);
    }
}

TEST(AuthoredShapePool, PoolIncarnationMoveAndGenerationExhaustionCannotResurrectHandles) {
    AuthoredShapePoolLimits limits; limits.slots=1; limits.generationsPerSlot=2;
    auto registry=pool(17,limits); const auto first=insert(registry);
    auto foreign=pool(18,limits); const auto other=insert(foreign);
    EXPECT_EQ(first.index,other.index); EXPECT_EQ(first.generation,other.generation); EXPECT_EQ(foreign.get(first),nullptr);
    auto moved=std::move(registry); EXPECT_EQ(registry.identity(),0u); EXPECT_EQ(registry.get(first),nullptr); ASSERT_NE(moved.get(first),nullptr);
    EXPECT_EQ(moved.retire(first),ShapePoolError::None); const auto second=insert(moved); EXPECT_EQ(second.generation,2u);
    EXPECT_EQ(moved.retire(second),ShapePoolError::None); EXPECT_EQ(moved.stats().exhausted,1u);
    auto replacement=shape(); ShapePoolError error; EXPECT_FALSE(moved.insert(std::move(replacement),error).valid());
    EXPECT_EQ(error,ShapePoolError::GenerationExhausted); EXPECT_EQ(moved.get(first),nullptr); EXPECT_EQ(moved.get(second),nullptr);
}

TEST(AuthoredShapePool, InvalidProfilesReferencesAndSerialExhaustionAreExplicit) {
    ShapePoolError error; EXPECT_FALSE(AuthoredShapePool::create(0,error)); EXPECT_EQ(error,ShapePoolError::InvalidProfile);
    auto limits=AuthoredShapePoolLimits{}; limits.slots=513;
    EXPECT_FALSE(AuthoredShapePool::create(17,error,limits)); EXPECT_EQ(error,ShapePoolError::InvalidProfile);
    auto registry=pool(); const auto handle=insert(registry); EXPECT_EQ(registry.release(handle),ShapePoolError::NoReference);
    EXPECT_EQ(registry.retain({}),ShapePoolError::InvalidHandle); EXPECT_EQ(registry.retire({1,0,17}),ShapePoolError::InvalidHandle);
    EXPECT_FALSE((ShapeHandle{1,1,0}).valid()); EXPECT_FALSE((ShapeHandle{1,0,17}).valid());
    const std::array used{handle}; EXPECT_EQ(registry.submitted(UINT64_MAX,used),ShapePoolError::None);
    EXPECT_EQ(registry.submitted(0,used),ShapePoolError::InvalidSubmission); EXPECT_EQ(registry.retire(handle),ShapePoolError::None);
    EXPECT_EQ(registry.completed(UINT64_MAX),ShapePoolError::None); EXPECT_EQ(registry.get(handle),nullptr);
    EXPECT_EQ(registry.completed(UINT64_MAX),ShapePoolError::None);
}
} // namespace
