#include "game/adventure/imported_assembly.hpp"
#include "game/adventure/cannon_physics_scene.hpp"
#include "game/adventure/ldraw_blacksmith_ground_remainder.hpp"
#include <gtest/gtest.h>
#include <algorithm>
#include <array>
#include <numbers>
#include <set>
#include <map>
#include <fstream>
#include <chrono>
#include <json.hpp>

namespace voxy::game::adventure {
namespace {
ImportedAssemblySource wall() {
    ImportedAssemblySource source;source.assetId="actual-selected-wall";source.sourceSha256=std::string(64,'a');
    // 32 catalog bricks with explicit, independently named, staggered stud
    // mates. No bounding-box overlap or proximity operation creates bonds.
    for(uint64_t row=0;row<8;++row)for(uint64_t column=0;column<4;++column) {
        const uint64_t id=1000+row*4+column;
        source.parts.push_back({id,"house/wall/"+std::to_string(id),"3004.dat",4,uint32_t(row*4+column+1),
            {double(column)*2+double(row%2),double(row+1)*1.2,0},{1,0,0,0}});
        if(row==0)source.anchors.push_back(id);
    }
    const auto* spec=importedPartCatalog("3004.dat");
    for(uint64_t row=1;row<8;++row)for(uint64_t up=0;up<4;++up)for(uint16_t us=0;us<2;++us) {
        const auto& upper=source.parts[row*4+up];const auto top=upper.translation+spec->connector(us,true);
        for(uint64_t low=0;low<4;++low)for(uint16_t ls=0;ls<2;++ls) {
            const auto& lower=source.parts[(row-1)*4+low];const auto bottom=lower.translation+spec->connector(ls,false);
            if(glm::length(top-bottom)<1e-8)
                source.bonds.push_back({10000+source.bonds.size(),lower.sourceId,upper.sourceId,ls,us,true});
        }
    }
    return source;
}
TEST(ImportedAssembly, ExplicitCatalogConnectionsAndEveryAnchorDriveSupport) {
    auto source=wall();std::string error;
    auto before=ImportedAssembly::prepare(source,error);ASSERT_TRUE(before)<<error;
    ASSERT_EQ(before->roots().size(),1u);EXPECT_TRUE(before->roots()[0].anchored);
    EXPECT_EQ(before->roots()[0].partIndices.size(),32u);
    EXPECT_NEAR(before->roots()[0].mass.massKg,8.,1e-10);
    std::vector<uint64_t> cuts;
    for(const auto& bond:source.bonds)if(bond.lowerPart<1004)cuts.push_back(bond.id);
    auto after=before->prepareCut(1,cuts,{},error);ASSERT_TRUE(after)<<error;
    EXPECT_EQ(after->source().revision,2u);EXPECT_EQ(before->source().revision,1u);
    ASSERT_EQ(after->roots().size(),5u);
    std::set<uint64_t> membership;double mass=0;size_t loose=0;
    for(const auto& root:after->roots()) {
        mass+=root.mass.massKg;
        if(root.key<1004){EXPECT_TRUE(root.anchored);EXPECT_EQ(root.partIndices.size(),1u);}
        else {EXPECT_FALSE(root.anchored);EXPECT_EQ(root.partIndices.size(),28u);++loose;}
        for(const auto index:root.partIndices)EXPECT_TRUE(membership.insert(after->source().parts[index].sourceId).second);
        for(const auto& face:root.shape.faces()) {
            const auto id=after->partForFeature(face.source);ASSERT_TRUE(id);
            EXPECT_EQ(after->rootForPart(*id),after->rootForPart(root.key));
        }
    }
    EXPECT_EQ(loose,1u);EXPECT_EQ(membership.size(),32u);EXPECT_NEAR(mass,8.,1e-10);
    // The old graph's least-node "keel" is deliberately irrelevant to support:
    // removing all named anchors makes even the component containing it movable.
    auto released=before->prepareCut(1,{},source.anchors,error);ASSERT_TRUE(released)<<error;
    ASSERT_EQ(released->roots().size(),1u);EXPECT_FALSE(released->roots()[0].anchored);
}
TEST(ImportedAssembly, CutsAndPhysicalPreparationAreAtomicAndDeterministic) {
    auto source=wall();std::string error;
    const auto first=ImportedAssembly::prepare(source,error);ASSERT_TRUE(first)<<error;
    std::reverse(source.parts.begin(),source.parts.end());std::reverse(source.bonds.begin(),source.bonds.end());
    std::reverse(source.anchors.begin(),source.anchors.end());
    const auto permuted=ImportedAssembly::prepare(source,error);ASSERT_TRUE(permuted)<<error;
    EXPECT_EQ(first->roots()[0].key,permuted->roots()[0].key);
    EXPECT_EQ(first->roots()[0].origin,permuted->roots()[0].origin);
    EXPECT_EQ(first->roots()[0].shape.cost(),permuted->roots()[0].shape.cost());
    EXPECT_EQ(first->roots()[0].mass.rootCenterOfMass,permuted->roots()[0].mass.rootCenterOfMass);
    const std::array<uint64_t,1> cut{source.bonds[0].id},unknown{999999};
    EXPECT_FALSE(first->prepareCut(2,cut,{},error));
    EXPECT_FALSE(first->prepareCut(1,unknown,{},error));
    const std::array<uint64_t,2> duplicate{cut[0],cut[0]};
    EXPECT_FALSE(first->prepareCut(1,duplicate,{},error));
    auto after=first->prepareCut(1,cut,{},error);ASSERT_TRUE(after)<<error;
    EXPECT_FALSE(after->prepareCut(2,cut,{},error));
    EXPECT_EQ(first->source().revision,1u);EXPECT_EQ(first->roots()[0].partIndices.size(),32u);
}
TEST(ImportedAssembly, ManualPartRemovalDeletesOwnedTopologyAndPreservesOtherIdentitiesAtomically) {
    auto source=wall();std::string error;const auto original=ImportedAssembly::prepare(source,error);ASSERT_TRUE(original)<<error;
    const uint64_t removed=1000;
    EXPECT_FALSE(original->prepareRemovePart(2,removed,error));
    EXPECT_FALSE(original->prepareRemovePart(1,999999,error));
    const auto next=original->prepareRemovePart(1,removed,error);ASSERT_TRUE(next)<<error;
    ASSERT_EQ(next->source().parts.size(),31u);EXPECT_EQ(next->source().revision,2u);
    EXPECT_FALSE(next->rootForPart(removed));EXPECT_FALSE(next->prepareRemovePart(2,removed,error));
    EXPECT_EQ(std::count(next->source().anchors.begin(),next->source().anchors.end(),removed),0);
    for(const auto& bond:next->source().bonds) {EXPECT_NE(bond.lowerPart,removed);EXPECT_NE(bond.upperPart,removed);}
    double mass=0;std::set<uint64_t> geometryIds;
    for(const auto& root:next->roots()) {
        mass+=root.mass.massKg;
        for(const auto& cell:root.shape.cells()) {
            const auto id=next->partForFeature(cell.source);ASSERT_TRUE(id);EXPECT_NE(*id,removed);geometryIds.insert(*id);
        }
    }
    EXPECT_NEAR(mass,7.75,1e-10);EXPECT_EQ(geometryIds.size(),31u);
    for(const auto& part:next->source().parts) {
        const auto old=std::find_if(source.parts.begin(),source.parts.end(),[&](const auto& p){return p.sourceId==part.sourceId;});
        ASSERT_NE(old,source.parts.end());EXPECT_EQ(part.meshNode,old->meshNode);EXPECT_EQ(part.sourcePath,old->sourcePath);
        EXPECT_EQ(part.translation,old->translation);EXPECT_EQ(part.rotation,old->rotation);
    }
    EXPECT_EQ(original->source().parts.size(),32u);EXPECT_EQ(original->source().revision,1u);EXPECT_TRUE(original->rootForPart(removed));
}
TEST(ImportedAssembly, ActualSupportRemovalDropsOnlyNamedSourceAndBothIncidentConnections) {
    std::string error;const auto source=loadImportedSection("data/adventure/ldraw-blacksmith-parts-r01/wall.json",error);
    ASSERT_TRUE(source)<<error;const auto original=ImportedAssembly::prepare(*source,error);ASSERT_TRUE(original)<<error;
    constexpr uint64_t removed=415964676630277730ull,above=409735568841264200ull;
    const auto next=original->prepareRemovePart(source->revision,removed,error);ASSERT_TRUE(next)<<error;
    ASSERT_EQ(next->source().parts.size(),38u);ASSERT_TRUE(next->rootForPart(above));EXPECT_FALSE(next->rootForPart(removed));
    EXPECT_EQ(next->source().bonds.size()+2,source->bonds.size());
    const auto upper=std::find_if(next->source().parts.begin(),next->source().parts.end(),[](const auto& part){return part.sourceId==above;});
    ASSERT_NE(upper,next->source().parts.end());EXPECT_EQ(upper->meshNode,2u);
    for(const auto& bond:next->source().bonds) {EXPECT_NE(bond.lowerPart,removed);EXPECT_NE(bond.upperPart,removed);}
    std::set<uint64_t> eligible;
    for(const auto& root:original->roots())if(root.anchored)
        for(const auto index:root.partIndices)eligible.insert(original->source().parts[index].sourceId);
    ASSERT_TRUE(eligible.contains(removed));ASSERT_TRUE(eligible.contains(above));
    std::vector<uint64_t> cuts;for(const auto& bond:next->source().bonds)
        if(bond.active&&eligible.contains(bond.lowerPart)&&eligible.contains(bond.upperPart))cuts.push_back(bond.id);
    const auto released=next->prepareCut(next->source().revision,cuts,next->source().anchors,error);ASSERT_TRUE(released)<<error;
    const auto upperRoot=released->rootForPart(above);ASSERT_TRUE(upperRoot);
    EXPECT_FALSE(released->roots()[*upperRoot].anchored);EXPECT_EQ(released->roots()[*upperRoot].partIndices.size(),1u);
    EXPECT_EQ(original->source().parts.size(),39u);EXPECT_TRUE(original->rootForPart(removed));
}
TEST(ImportedAssembly, MetadataPreservesArbitraryRotationsButCollisionRefusesUnsupportedOnes) {
    auto source=wall();source.bonds.clear();source.parts.resize(1);source.anchors={source.parts[0].sourceId};
    source.parts[0].rotation=glm::angleAxis(.37,glm::dvec3(0,1,0));std::string error;
    EXPECT_TRUE(validateImportedSource(source,error))<<error;
    const auto original=source.parts[0].rotation;
    EXPECT_FALSE(ImportedAssembly::prepare(source,error));EXPECT_EQ(source.parts[0].rotation,original);
    source.parts[0].rotation=glm::angleAxis(std::numbers::pi/2,glm::dvec3(0,1,0));
    EXPECT_TRUE(ImportedAssembly::prepare(source,error))<<error;
    source.parts[0].partNumber="unsupported-decorative-roof.dat";
    EXPECT_TRUE(validateImportedSource(source,error));EXPECT_FALSE(ImportedAssembly::prepare(source,error));
}
TEST(ImportedAssembly, SourceSchemaDoesNotSilentlyTruncateToTheSectionBudget) {
    auto source=wall();source.bonds.clear();source.anchors.clear();
    const auto part=source.parts[0];source.parts.clear();
    for(uint64_t i=0;i<300;++i){auto p=part;p.sourceId=i+1;p.sourcePath="source/"+std::to_string(i);source.parts.push_back(p);}
    std::string error;EXPECT_TRUE(validateImportedSource(source,error))<<error;
    EXPECT_FALSE(ImportedAssembly::prepare(source,error));EXPECT_EQ(source.parts.size(),300u);
    source.schemaVersion=2;EXPECT_FALSE(validateImportedSource(source,error));
    source.schemaVersion=1;source.parts[1].sourceId=source.parts[0].sourceId;
    EXPECT_FALSE(validateImportedSource(source,error));
}
TEST(ImportedAssembly, InvalidStudMatesAndImplicitProximityNeverCreateConnections) {
    auto source=wall();std::string error;
    source.bonds[0].upperSlot=99;EXPECT_FALSE(ImportedAssembly::prepare(source,error));
    source=wall();source.parts[4].translation.x+=.1;EXPECT_FALSE(ImportedAssembly::prepare(source,error));
    source=wall();auto duplicate=source.bonds[0];duplicate.id+=100000;source.bonds.push_back(duplicate);
    EXPECT_FALSE(ImportedAssembly::prepare(source,error));
    source=wall();source.parts.resize(2);source.bonds.clear();source.anchors={1001};
    const auto separate=ImportedAssembly::prepare(source,error);ASSERT_TRUE(separate)<<error;
    ASSERT_EQ(separate->roots().size(),2u);EXPECT_FALSE(separate->roots()[0].anchored);EXPECT_TRUE(separate->roots()[1].anchored);
}
TEST(ImportedAssembly, ExcessiveFragmentationRefusesTheWholeCut) {
    ImportedAssemblySource source;source.assetId="fragment-budget";source.sourceSha256=std::string(64,'b');
    std::vector<uint64_t> cuts;
    for(uint64_t i=0;i<65;++i) {
        source.parts.push_back({i+1,"stack/"+std::to_string(i),"3005.dat",4,uint32_t(i+1),
            {0,double(i)*1.2,0},{1,0,0,0}});
        if(i){source.bonds.push_back({1000+i,i,i+1,0,0,true});cuts.push_back(1000+i);}
    }
    source.anchors={1};std::string error;
    const auto before=ImportedAssembly::prepare(source,error);ASSERT_TRUE(before)<<error;
    ASSERT_EQ(before->roots().size(),1u);
    EXPECT_FALSE(before->prepareCut(1,cuts,{},error));
    EXPECT_NE(error.find("root budget"),std::string::npos)<<error;
    EXPECT_EQ(before->source().revision,1u);EXPECT_EQ(before->roots()[0].partIndices.size(),65u);
    EXPECT_TRUE(std::all_of(before->source().bonds.begin(),before->source().bonds.end(),[](const auto& bond){return bond.active;}));
}
TEST(ImportedAssembly, HollowBodyAndStudProxyPreserveOpenUnderside) {
    auto source=wall();source.parts.resize(1);source.bonds.clear();source.anchors={1000};
    std::string error;const auto value=ImportedAssembly::prepare(source,error);ASSERT_TRUE(value)<<error;
    const auto& shape=value->roots()[0].shape;
    const auto inside=[&](glm::dvec3 p){
        p/=physics::kAuthoredShapeTickMetres;
        return std::any_of(shape.cells().begin(),shape.cells().end(),[&](const auto& c){
            return p.x>=c.minimum[0]&&p.x<c.maximum[0]&&p.y>=c.minimum[1]&&p.y<c.maximum[1]&&p.z>=c.minimum[2]&&p.z<c.maximum[2];});
    };
    EXPECT_FALSE(inside({0,-.6,0}));EXPECT_TRUE(inside({.9,-.6,0}));
    EXPECT_TRUE(inside({0,-.1,0}));EXPECT_TRUE(inside({.5,.1,0}));
    EXPECT_FALSE(inside({0,.1,0}));EXPECT_FALSE(inside({.5,.3,0}));
    EXPECT_TRUE(inside({.5,.1,.27}));EXPECT_FALSE(inside({.5,.1,.29}));
    // Official stud.dat radius=6LDU;3004s01 underside half-width=6LDU.
    // Keep the source's .2 wall and .3 cavity, while an inscribed stud proxy
    // leaves .02 side clearance for the graph-owned clutch after release.
    double studHalfWidth=0;size_t studCells=0;
    for(const auto& cell:shape.cells())if(cell.minimum[1]>=0) {
        ++studCells;
        for(unsigned corner=0;corner<4;++corner) {
            const double x=double(corner&1?cell.maximum[0]:cell.minimum[0])*physics::kAuthoredShapeTickMetres;
            const double z=double(corner&2?cell.maximum[2]:cell.minimum[2])*physics::kAuthoredShapeTickMetres;
            EXPECT_LE(std::min(std::hypot(x-.5,z),std::hypot(x+.5,z)),.3000001);
            studHalfWidth=std::max(studHalfWidth,std::abs(z));
        }
    }
    EXPECT_GT(studCells,0u);EXPECT_NEAR(.3-studHalfWidth,.02,1e-8);
}
TEST(ImportedAssembly, ChildMotionPreservesRigidVelocityWithoutInventedImpactImpulse) {
    auto source=wall();std::string error;const auto before=ImportedAssembly::prepare(source,error);ASSERT_TRUE(before)<<error;
    std::vector<uint64_t> cuts;for(const auto& bond:source.bonds)if(bond.lowerPart<1004)cuts.push_back(bond.id);
    const auto after=before->prepareCut(1,cuts,{},error);ASSERT_TRUE(after)<<error;
    physics::AuthoredRootMotion parent;parent.position=physics::worldPositionFromAbsolute({100000000,200,300});
    parent.orientation=glm::angleAxis(.5f,glm::vec3(0,1,0));parent.originVelocity={1,2,3};parent.angularVelocity={0,2,0};
    const auto motions=after->inheritMotion(*before,std::span(&parent,1),error);ASSERT_TRUE(motions)<<error;
    ASSERT_EQ(motions->size(),after->roots().size());
    for(size_t i=0;i<motions->size();++i) {
        const auto offset=glm::dquat(parent.orientation)*(after->roots()[i].origin-before->roots()[0].origin);
        const auto expected=glm::dvec3(parent.originVelocity)+glm::cross(glm::dvec3(parent.angularVelocity),offset);
        EXPECT_NEAR(glm::length(glm::dvec3((*motions)[i].originVelocity)-expected),0,1e-5);
    }
    parent.originVelocity={};parent.angularVelocity={};
    const auto stationary=after->inheritMotion(*before,std::span(&parent,1),error);ASSERT_TRUE(stationary)<<error;
    for(const auto& motion:*stationary){EXPECT_EQ(motion.originVelocity,glm::vec3(0));EXPECT_EQ(motion.angularVelocity,glm::vec3(0));}
    auto altered=after->source();altered.parts[0].meshNode+=100;
    const auto wrong=ImportedAssembly::prepare(altered,error);ASSERT_TRUE(wrong)<<error;
    EXPECT_FALSE(wrong->inheritMotion(*before,std::span(&parent,1),error));
}

TEST(ImportedAssembly, ReleasedShellKeepsEveryShellSurfaceAndFullPartMassButOmitsStudCollision) {
    auto source=wall();source.parts.resize(1);source.bonds.clear();source.anchors={1000};
    std::string error;const auto detailed=ImportedAssembly::prepare(source,error);ASSERT_TRUE(detailed)<<error;
    EXPECT_EQ(detailed->collisionProfile(),ImportedAssembly::CollisionProfile::Detailed);
    EXPECT_FALSE(detailed->prepareReleasedShell(2,error));
    const auto shell=detailed->prepareReleasedShell(1,error);ASSERT_TRUE(shell)<<error;
    EXPECT_EQ(shell->collisionProfile(),ImportedAssembly::CollisionProfile::ReleasedShell);
    EXPECT_EQ(shell->source().revision,2u);EXPECT_EQ(detailed->source().revision,1u);
    EXPECT_FALSE(shell->prepareReleasedShell(2,error));
    const auto& before=detailed->roots()[0];const auto& after=shell->roots()[0];
    EXPECT_EQ(before.mass.massKg,after.mass.massKg);
    EXPECT_EQ(before.mass.rootCenterOfMass,after.mass.rootCenterOfMass);
    EXPECT_EQ(before.mass.inertiaAboutCenter,after.mass.inertiaAboutCenter);
    EXPECT_EQ(before.origin,after.origin);EXPECT_EQ(before.partIndices,after.partIndices);
    const auto inside=[](const auto& shape,glm::dvec3 p) {
        p/=physics::kAuthoredShapeTickMetres;
        return std::any_of(shape.cells().begin(),shape.cells().end(),[&](const auto& c) {
            return p.x>=c.minimum[0]&&p.x<c.maximum[0]&&p.y>=c.minimum[1]&&p.y<c.maximum[1]&&p.z>=c.minimum[2]&&p.z<c.maximum[2];
        });
    };
    // Sample off boundaries over the complete hollow brick. Every pre-fracture
    // body point remains identical, including the cavity and all four walls.
    for(int x=-11;x<=11;++x)for(int y=-13;y<=-1;++y)for(int z=-6;z<=6;++z) {
        const glm::dvec3 p(double(x)*.1+.013,double(y)*.1+.017,double(z)*.1+.011);
        EXPECT_EQ(inside(before.shape,p),inside(after.shape,p));
    }
    EXPECT_TRUE(inside(before.shape,{.5,.1,0}));EXPECT_FALSE(inside(after.shape,{.5,.1,0}));
    EXPECT_FALSE(inside(after.shape,{0,-.6,0}));
    for(const auto& cell:after.shape.cells()) {
        EXPECT_LE(cell.maximum[1],0);EXPECT_LE(cell.source,5u);
        EXPECT_EQ(shell->partForFeature(cell.source),1000u);
    }
    EXPECT_FALSE(shell->partForFeature(6));EXPECT_TRUE(detailed->partForFeature(6));
    EXPECT_FALSE(ImportedAssembly::prepare(source,error,static_cast<ImportedAssembly::CollisionProfile>(99)));
}

TEST(ImportedAssembly, ReleasedShellPreservesSourcePosesMotionAndProfileAcrossTopologyChanges) {
    auto source=wall();std::string error;const auto detailed=ImportedAssembly::prepare(source,error);ASSERT_TRUE(detailed)<<error;
    const auto shell=detailed->prepareReleasedShell(1,error);ASSERT_TRUE(shell)<<error;
    physics::AuthoredRootMotion parent;parent.position=physics::worldPositionFromAbsolute({50,20,30});
    parent.orientation=glm::angleAxis(.5f,glm::vec3(0,1,0));parent.originVelocity={1,2,3};parent.angularVelocity={0,2,0};
    const auto motion=shell->inheritMotion(*detailed,std::span(&parent,1),error);ASSERT_TRUE(motion)<<error;
    ASSERT_EQ(motion->size(),1u);
    EXPECT_EQ(physics::worldPositionToAbsolute((*motion)[0].position),physics::worldPositionToAbsolute(parent.position));
    EXPECT_EQ((*motion)[0].orientation,parent.orientation);EXPECT_EQ((*motion)[0].originVelocity,parent.originVelocity);
    EXPECT_EQ((*motion)[0].angularVelocity,parent.angularVelocity);
    for(uint32_t i=0;i<shell->source().parts.size();++i) {
        const auto& a=shell->source().parts[i];const auto& b=detailed->source().parts[i];
        EXPECT_EQ(a.sourceId,b.sourceId);EXPECT_EQ(a.sourcePath,b.sourcePath);EXPECT_EQ(a.partNumber,b.partNumber);
        EXPECT_EQ(a.colour,b.colour);EXPECT_EQ(a.meshNode,b.meshNode);EXPECT_EQ(a.translation,b.translation);
        EXPECT_EQ(a.rotation,b.rotation);EXPECT_EQ(shell->partMatrix(i),detailed->partMatrix(i));
    }
    EXPECT_EQ(shell->source().anchors,detailed->source().anchors);
    std::vector<uint64_t> cuts;for(const auto& bond:source.bonds)if(bond.lowerPart<1004)cuts.push_back(bond.id);
    const auto cut=shell->prepareCut(2,cuts,{},error);ASSERT_TRUE(cut)<<error;
    EXPECT_EQ(cut->collisionProfile(),ImportedAssembly::CollisionProfile::ReleasedShell);
    EXPECT_TRUE(cut->inheritMotion(*shell,*motion,error))<<error;
    const auto removed=cut->prepareRemovePart(3,1000,error);ASSERT_TRUE(removed)<<error;
    EXPECT_EQ(removed->collisionProfile(),ImportedAssembly::CollisionProfile::ReleasedShell);
    for(const auto& root:removed->roots())for(const auto& cell:root.shape.cells())EXPECT_LE((cell.source-1)%256,4u);
    auto restoredSource=shell->source();++restoredSource.revision;
    const auto restored=ImportedAssembly::prepare(restoredSource,error);ASSERT_TRUE(restored)<<error;
    EXPECT_FALSE(restored->inheritMotion(*shell,*motion,error));
    EXPECT_EQ(detailed->collisionProfile(),ImportedAssembly::CollisionProfile::Detailed);
}

TEST(ImportedAssembly, GroundSectionShellDoesNotIntersectCompiledRetainedHouse) {
    std::string error;const auto source=loadImportedSection("data/adventure/ldraw-blacksmith-ground-r01/wall.json",error);
    ASSERT_TRUE(source)<<error;
    const auto graph=ImportedAssembly::prepare(*source,error,ImportedAssembly::CollisionProfile::ReleasedShell);
    ASSERT_TRUE(graph)<<error;
    const glm::dvec3 house(1208,.185,-1032),cannon(1240,.185,-1027);
    std::vector<AdventureSpatialQueries::Solid> solids;
    for(const auto& box:blacksmithRemainderSolids)solids.push_back({{},{},
        house+glm::dvec3(-box.maximum.x,box.minimum.y,-box.maximum.z),
        house+glm::dvec3(-box.minimum.x,box.maximum.y,-box.minimum.z)});
    const auto packet=CannonPhysicsScene::compile(solids,cannon,1,error);ASSERT_TRUE(packet)<<error;
    double maximumDepth=0;size_t overlaps=0,targetOverlaps=0;
    for(const auto& root:graph->roots())for(const auto& cell:root.shape.cells()) {
        glm::dvec3 lo,hi;
        for(int axis=0;axis<3;++axis) {
            lo[axis]=double(cell.minimum[size_t(axis)])*physics::kAuthoredShapeTickMetres+root.origin[axis];
            hi[axis]=double(cell.maximum[size_t(axis)])*physics::kAuthoredShapeTickMetres+root.origin[axis];
        }
        const auto minimum=house+glm::dvec3(-hi.x,lo.y,-hi.z),maximum=house+glm::dvec3(-lo.x,hi.y,-lo.z);
        for(const auto& partition:packet->partitions)for(const auto& retained:partition.shape.cells()) {
            glm::dvec3 retainedMin,retainedMax;
            for(int axis=0;axis<3;++axis) {
                retainedMin[axis]=double(retained.minimum[size_t(axis)])*physics::kAuthoredShapeTickMetres+partition.origin[axis];
                retainedMax[axis]=double(retained.maximum[size_t(axis)])*physics::kAuthoredShapeTickMetres+partition.origin[axis];
            }
            const auto depth=glm::min(maximum,retainedMax)-glm::max(minimum,retainedMin);
            const double minimumDepth=std::min({depth.x,depth.y,depth.z});
            if(minimumDepth>1e-5) {
                ++overlaps;maximumDepth=std::max(maximumDepth,minimumDepth);
                if(graph->partForFeature(cell.source)==93361846531299384ull)++targetOverlaps;
            }
        }
    }
    RecordProperty("compiledRetainedOverlaps",std::to_string(overlaps));
    RecordProperty("compiledTargetOverlaps",std::to_string(targetOverlaps));
    RecordProperty("compiledMaximumDepth",std::to_string(maximumDepth));
    EXPECT_EQ(overlaps,0u)<<maximumDepth;EXPECT_EQ(targetOverlaps,0u);
}

TEST(ImportedAssembly, ActualBlacksmithWallPreservesAllSourcePartsAndSupportEvidence) {
    const std::filesystem::path path="data/adventure/ldraw-blacksmith-parts-r01/wall.json";
    std::string error;const auto source=loadImportedSection(path,error);ASSERT_TRUE(source)<<error;
    EXPECT_EQ(source->parts.size(),39u);EXPECT_EQ(source->bonds.size(),46u);EXPECT_EQ(source->anchors.size(),4u);
    EXPECT_EQ(source->sourceSha256,"0a7f53680569172309ebfe7053dfaccb04ea941643f1e3cbf811cf44ffd5fcee");
    EXPECT_TRUE(std::any_of(source->parts.begin(),source->parts.end(),[](const auto& p){return p.sourceId>(uint64_t(1)<<53);}));
    const auto assembly=ImportedAssembly::prepare(*source,error);ASSERT_TRUE(assembly)<<error;
    size_t anchoredParts=0,unknownParts=0;double mass=0;std::set<uint64_t> identities;
    for(const auto& root:assembly->roots()) {
        (root.anchored?anchoredParts:unknownParts)+=root.partIndices.size();mass+=root.mass.massKg;
        for(const auto index:root.partIndices)EXPECT_TRUE(identities.insert(assembly->source().parts[index].sourceId).second);
    }
    EXPECT_EQ(identities.size(),39u);EXPECT_EQ(anchoredParts,29u);EXPECT_EQ(unknownParts,10u);
    // Lack of an explicit boundary witness is not proof of no real support.
    // Runtime keeps these ten unknown-support parts static in this milestone.
    const auto released=assembly->prepareCut(1,{},source->anchors,error);ASSERT_TRUE(released)<<error;
    EXPECT_EQ(released->source().parts.size(),39u);double releasedMass=0;
    for(const auto& root:released->roots()){EXPECT_FALSE(root.anchored);releasedMass+=root.mass.massKg;}
    EXPECT_NEAR(mass,releasedMass,1e-10);
    RecordProperty("actualWallParts",39);RecordProperty("knownBoundarySupportedParts",int(anchoredParts));
    RecordProperty("unknownSupportParts",int(unknownParts));RecordProperty("physicalRoots",int(assembly->roots().size()));
    EXPECT_FALSE(loadImportedSection(path,error,std::string(64,'f')));
}
TEST(ImportedAssembly, ActualSelectedPartCellsDoNotInitiallyInterpenetrateThroughBodyFrames) {
    std::string error;
    const auto source=loadImportedSection("data/adventure/ldraw-blacksmith-parts-r01/wall.json",error);
    ASSERT_TRUE(source)<<error;
    const auto assembled=ImportedAssembly::prepare(*source,error);ASSERT_TRUE(assembled)<<error;
    std::vector<uint64_t> cuts;for(const auto& bond:source->bonds)if(bond.active)cuts.push_back(bond.id);
    // Compile every part independently: a union of a bonded component would
    // erase overlapping interiors and could conceal a bad source placement.
    const auto separate=assembled->prepareCut(source->revision,cuts,source->anchors,error);
    ASSERT_TRUE(separate)<<error;ASSERT_EQ(separate->roots().size(),39u);
    struct Bounds {glm::dvec3 minimum{INFINITY},maximum{-INFINITY};};
    const auto axisRotation=glm::angleAxis(float(std::numbers::pi),glm::vec3(0,1,0));
    for(const bool worldPlacement:{false,true}) {
        SCOPED_TRACE(worldPlacement?"distant yaw-pi body frame":"source body frame");
        const glm::dvec3 offset=worldPlacement?glm::dvec3(1200,7,-1040):glm::dvec3(0);
        const glm::quat orientation=worldPlacement?axisRotation:glm::quat(1,0,0,0);
        std::map<uint64_t,std::vector<Bounds>> members;
        for(const auto& root:separate->roots()) {
            ASSERT_EQ(root.partIndices.size(),1u);
            const physics::AuthoredBodyFrame frame(root.shape);physics::AuthoredFrameError frameError;
            const physics::AuthoredRootMotion motion{
                .position=physics::worldPositionFromAbsolute(offset+glm::dquat(orientation)*root.origin),
                .orientation=orientation};
            const auto body=frame.bodyMotion(motion,frameError);ASSERT_TRUE(body);
            const auto center=physics::worldPositionToAbsolute(body->centerPosition);
            for(const auto& cell:root.shape.cells()) {
                const auto id=separate->partForFeature(cell.source);ASSERT_TRUE(id);
                Bounds bounds;
                for(unsigned corner=0;corner<8;++corner) {
                    glm::vec3 local;
                    for(unsigned axis=0;axis<3;++axis)local[int(axis)]=float(corner&(1u<<axis)?cell.maximum[axis]:cell.minimum[axis])*float(physics::kAuthoredShapeTickMetres);
                    const auto principal=frame.bodyPoint(local,frameError);ASSERT_TRUE(principal);
                    const auto actual=center+glm::dquat(body->orientation)*glm::dvec3(*principal);
                    const auto expected=physics::worldPositionToAbsolute(motion.position)+glm::dquat(orientation)*glm::dvec3(local);
                    for(int axis=0;axis<3;++axis)EXPECT_NEAR(actual[axis],expected[axis],.00005);
                    bounds.minimum=glm::min(bounds.minimum,actual);bounds.maximum=glm::max(bounds.maximum,actual);
                }
                members[*id].push_back(bounds);
            }
        }
        ASSERT_EQ(members.size(),39u);size_t pairs=0;double worst=0;uint64_t firstId=0,secondId=0;
        for(auto first=members.begin();first!=members.end();++first)for(auto second=std::next(first);second!=members.end();++second) {
            ++pairs;
            for(const auto& a:first->second)for(const auto& b:second->second) {
                const auto depth=glm::min(a.maximum,b.maximum)-glm::max(a.minimum,b.minimum);
                const double penetration=std::max(0.,std::min({depth.x,depth.y,depth.z}));
                if(penetration>worst){worst=penetration;firstId=first->first;secondId=second->first;}
            }
        }
        EXPECT_EQ(pairs,741u);
        // Allow source-export/f32-frame roundoff, far below a .02 authored tick.
        EXPECT_LE(worst,.002)<<"overlapping source IDs "<<firstId<<" and "<<secondId;
        RecordProperty(worldPlacement?"worldMaximumPairPenetration":"sourceMaximumPairPenetration",worst);
    }
}

TEST(ImportedAssembly, LoaderRejectsForgedCatalogAndUnwitnessedAnchors) {
    const std::filesystem::path original="data/adventure/ldraw-blacksmith-parts-r01/wall.json";
    std::ifstream file(original);ASSERT_TRUE(file);auto packet=nlohmann::json::parse(file);
    // Keep this copied fixture self-contained, even if an older export stores
    // the MPD hash only in the adjacent full assembly manifest.
    packet["sourceMPDSha256"]="0a7f53680569172309ebfe7053dfaccb04ea941643f1e3cbf811cf44ffd5fcee";
    const auto path=std::filesystem::path(testing::TempDir())/("imported-wall-"+
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+".json");
    struct Remove {std::filesystem::path path;~Remove(){std::error_code error;std::filesystem::remove(path,error);}} cleanup{path};
    const auto write=[&](const nlohmann::json& value){std::ofstream output(path);output<<value;return bool(output);};
    std::string error;ASSERT_TRUE(write(packet));ASSERT_TRUE(loadImportedSection(path,error))<<error;
    auto bad=packet;bad["parts"][0]["studs"][0][0]=.25;ASSERT_TRUE(write(bad));EXPECT_FALSE(loadImportedSection(path,error));
    bad=packet;bad["parts"][0]["scale"]={1.,2.,1.};ASSERT_TRUE(write(bad));EXPECT_FALSE(loadImportedSection(path,error));
    bad=packet;bad["parts"][0]["sourceId"]="18446744073709551616";ASSERT_TRUE(write(bad));EXPECT_FALSE(loadImportedSection(path,error));
    bad=packet;bad["boundaryBonds"]=nlohmann::json::array();ASSERT_TRUE(write(bad));EXPECT_FALSE(loadImportedSection(path,error));
    bad=packet;bad["schema"]=uint64_t(4294967297);ASSERT_TRUE(write(bad));EXPECT_FALSE(loadImportedSection(path,error));
}
TEST(ImportedAssembly, ActualRemainderDoesNotPenetrateAnySelectedPartProxy) {
    const std::filesystem::path directory="data/adventure/ldraw-blacksmith-parts-r01";
    std::string error;const auto source=loadImportedSection(directory/"wall.json",error);ASSERT_TRUE(source)<<error;
    const auto graph=ImportedAssembly::prepare(*source,error);ASSERT_TRUE(graph)<<error;
    std::ifstream file(directory/"remainder-collision.json");ASSERT_TRUE(file);
    const auto json=nlohmann::json::parse(file);const auto& boxes=json.at("boxes");ASSERT_TRUE(boxes.is_array());
    std::vector<std::pair<glm::dvec3,glm::dvec3>> remainder;
    for(const auto& box:boxes) {
        ASSERT_EQ(box.size(),2u);ASSERT_EQ(box[0].size(),3u);ASSERT_EQ(box[1].size(),3u);
        remainder.push_back({{box[0][0].get<double>(),box[0][1].get<double>(),box[0][2].get<double>()},
            {box[1][0].get<double>(),box[1][1].get<double>(),box[1][2].get<double>()}});
    }
    size_t penetratingCells=0,worstBox=0;uint64_t worstPart=0;double worstVolume=0;
    for(const auto& root:graph->roots())for(const auto& cell:root.shape.cells()) {
        const auto lo=root.origin+glm::dvec3(cell.minimum[0],cell.minimum[1],cell.minimum[2])*physics::kAuthoredShapeTickMetres;
        const auto hi=root.origin+glm::dvec3(cell.maximum[0],cell.maximum[1],cell.maximum[2])*physics::kAuthoredShapeTickMetres;
        for(size_t i=0;i<remainder.size();++i) {
            const auto overlap=glm::min(hi,remainder[i].second)-glm::max(lo,remainder[i].first);
            if(glm::all(glm::greaterThan(overlap,glm::dvec3(.002)))) {
                ++penetratingCells;const double volume=overlap.x*overlap.y*overlap.z;
                if(volume>worstVolume){worstVolume=volume;worstBox=i;worstPart=graph->partForFeature(cell.source).value_or(0);}
            }
        }
    }
    EXPECT_EQ(penetratingCells,0u)<<"Static remainder penetrates selected source "<<worstPart
        <<", remainder box "<<worstBox<<", worst single-cell overlap volume "<<worstVolume;
    // Run the real D1 compiler: its outward .02 lattice expansion must not
    // recreate intersections removed from the source collision artifact.
    // Both landmarks use the same level plot; remove their common world offset
    // but preserve the installed house yaw and cannon-relative lattice origin.
    std::vector<AdventureSpatialQueries::Solid> worldRemainder;
    for(const auto& [lo,hi]:remainder)worldRemainder.push_back({{}, {},
        {-hi.x,lo.y,-hi.z},{-lo.x,hi.y,-lo.z}});
    const auto packet=CannonPhysicsScene::compile(worldRemainder,{-4,0,-42},1,error);ASSERT_TRUE(packet)<<error;
    std::vector<std::pair<glm::dvec3,glm::dvec3>> compiledRemainder;
    for(const auto& partition:packet->partitions)for(const auto& cell:partition.shape.cells()) {
        const auto lo=partition.origin+glm::dvec3(cell.minimum[0],cell.minimum[1],cell.minimum[2])*physics::kAuthoredShapeTickMetres;
        const auto hi=partition.origin+glm::dvec3(cell.maximum[0],cell.maximum[1],cell.maximum[2])*physics::kAuthoredShapeTickMetres;
        compiledRemainder.push_back({{-hi.x,lo.y,-hi.z},{-lo.x,hi.y,-lo.z}});
    }
    size_t compiledPenetrations=0;
    for(const auto& root:graph->roots())for(const auto& cell:root.shape.cells()) {
        const auto lo=root.origin+glm::dvec3(cell.minimum[0],cell.minimum[1],cell.minimum[2])*physics::kAuthoredShapeTickMetres;
        const auto hi=root.origin+glm::dvec3(cell.maximum[0],cell.maximum[1],cell.maximum[2])*physics::kAuthoredShapeTickMetres;
        for(const auto& [otherLo,otherHi]:compiledRemainder)
            if(glm::all(glm::greaterThan(glm::min(hi,otherHi)-glm::max(lo,otherLo),glm::dvec3(.002))))++compiledPenetrations;
    }
    EXPECT_EQ(compiledPenetrations,0u)<<"GPU static-scene lattice expansion recreates selected-part overlap.";
    RecordProperty("compiledRemainderCells",int(packet->cost.cells));
}
} // namespace
} // namespace voxy::game::adventure
