#include "game/adventure/imported_wall_physics.hpp"
#include "gpu/context.hpp"
#include <gtest/gtest.h>
#include <chrono>
#include <thread>

#ifndef VOXY_WASM
struct WGPUWrappedSubmissionIndex;
extern "C" WGPUBool wgpuDevicePoll(WGPUDevice,WGPUBool,const WGPUWrappedSubmissionIndex*);
#endif
namespace voxy::game::adventure {
namespace {
ImportedAssemblySource source() {
    ImportedAssemblySource s;s.assetId="wall-owner-probe";s.sourceSha256=std::string(64,'a');
    s.parts={{101,"wall/anchored","3004.dat",4,1,{0,4,0},{1,0,0,0}},
        {102,"wall/unsupported","3004.dat",4,2,{4,4,0},{1,0,0,0}}};s.anchors={101};return s;
}
TEST(ImportedWallPhysics, InitialQueriesExistBeforeGpuAdmissionAndFailedInitializationDoesNotActivate) {
    ImportedWallPhysics wall;std::string error;
    EXPECT_FALSE(wall.initialize(source(),{NAN,0,0},{1,0,0,0},error));EXPECT_FALSE(wall.active());
    ASSERT_TRUE(wall.initialize(source(),{1000,20,-1000},{1,0,0,0},error))<<error;
    EXPECT_TRUE(wall.active());EXPECT_FALSE(wall.ready());EXPECT_TRUE(wall.needsQuiescentBoundary());
    EXPECT_FALSE(wall.initialSolids().empty());EXPECT_TRUE(wall.settledSolids().empty());
    for(const auto& cell:wall.initialSolids()) {
        EXPECT_TRUE(cell.sourceId==101||cell.sourceId==102);
        EXPECT_GT(cell.minimum.y,22.);EXPECT_LT(cell.maximum.y,25.);
    }
    EXPECT_FALSE(wall.release(error));EXPECT_FALSE(wall.reset(error));EXPECT_FALSE(wall.released());
}
#ifndef VOXY_WASM
class ImportedWallGpu: public testing::Test {
protected:
    gpu::Context context;physics::PhysicsWorld world;ImportedWallPhysics wall;CannonPhysicsScene floor;
    std::string error;
    void SetUp() override {
        ASSERT_TRUE(context.initHeadless());
        physics::PhysicsInitContext init;init.requestedBackend=physics::BackendType::WebGpuSoft;
        init.device=context.getDevice();init.queue=context.getQueue();init.maxBodies=32;init.maxActiveBodies=32;
        init.maxPairs=256;init.maxContacts=256;init.maxManifolds=256;init.gpu.gravity={0,-9.81f,0};
        init.gpu.commandCapacity=128;init.gpu.debugReadbackBodyCapacity=32;
        ASSERT_TRUE(world.initialize(init));
    }
    void TearDown() override {world.shutdown();wall.abandonAfterWorldShutdown();floor.abandonAfterWorldShutdown();}
    template<class F>bool wait(F f) {
        const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(10);
        do {static_cast<void>(wgpuDevicePoll(context.getDevice(),false,nullptr));world.update(0);
            if(f())return true;
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }while(std::chrono::steady_clock::now()<end);return false;
    }
    bool submit(uint32_t ticks) {
        if(ticks&&!world.scheduleFixedTicks(ticks))return false;
        physics::ShapeResourceError issue;const auto ticket=world.prepareGpuSubmission(issue);if(!ticket.valid())return false;
        WGPUCommandEncoderDescriptor ed{};const auto encoder=wgpuDeviceCreateCommandEncoder(context.getDevice(),&ed);
        const auto report=world.encodeGpuStepChecked(encoder);WGPUCommandBufferDescriptor cd{};
        const auto command=report.succeeded()?wgpuCommandEncoderFinish(encoder,&cd):nullptr;
        wgpuCommandEncoderRelease(encoder);if(!command){static_cast<void>(world.discardGpuSubmission(ticket));return false;}
        const auto result=world.submitGpuSubmission(ticket,std::span(&command,1));wgpuCommandBufferRelease(command);
        if(result!=physics::ShapeResourceError::None)return false;
        const auto target=world.tickFrontier().submitted;
        return wait([&]{const auto* pool=world.authoredShapeResources();
            return world.tickFrontier().completed==target&&pool
                &&pool->stats().cpu.completed>=ticket.serial;});
    }
    bool step(uint32_t ticks=1) {
        static_cast<void>(wgpuDevicePoll(context.getDevice(),false,nullptr));world.update(0);
        if(!wall.update(world,error))return false;
        if(floor.update(world,error)==CannonPhysicsScene::Progress::Failed)return false;
        const auto* resources=world.authoredShapeResources();
        if(!resources||resources->stats().phase!=physics::ShapeResourcePhase::Ready)return true;
        return submit(!wall.needsQuiescentBoundary()&&!floor.needsQuiescentBoundary()?ticks:0);
    }
    bool install(bool ground=true) {
        AdventureSpatialQueries::Solid plane{{},{},{-20,-1,-20},{20,0,20}};
        if(!floor.prepare(ground?std::span(&plane,1):std::span<AdventureSpatialQueries::Solid>{},{},1,error)||!wall.initialize(source(),{},{1,0,0,0},error))return false;
        for(int i=0;i<300;++i){if(!step())return false;if(wall.ready()&&floor.ready(1))return true;std::this_thread::sleep_for(std::chrono::milliseconds(1));}
        return false;
    }
};
TEST_F(ImportedWallGpu, OnlyKnownSupportedPartsFallThenCertifiedStaticQueriesMatchAndResetRestores) {
    ASSERT_TRUE(install())<<error;ASSERT_EQ(wall.bindings().size(),2u);
    const auto initialRevision=wall.geometryRevision();
    ASSERT_TRUE(wall.release(error))<<error;EXPECT_TRUE(wall.released());EXPECT_TRUE(wall.busy());
    EXPECT_TRUE(wall.settledSolids().empty());
    bool sawFalling=false;
    for(int i=0;i<1400&&!wall.ready();++i) {
        ASSERT_TRUE(step())<<error; sawFalling|=wall.phase()==ImportedWallPhysics::Phase::Falling;
    }
    ASSERT_TRUE(wall.ready())<<int(wall.phase())<<" "<<error;
    EXPECT_TRUE(sawFalling);EXPECT_EQ(wall.phase(),ImportedWallPhysics::Phase::Settled);
    EXPECT_GT(wall.geometryRevision(),initialRevision);EXPECT_TRUE(wall.released());
    double fallingTop=-100,pinnedBottom=100;
    for(const auto& cell:wall.settledSolids()) {
        if(cell.sourceId==101)fallingTop=std::max(fallingTop,cell.maximum.y);
        if(cell.sourceId==102)pinnedBottom=std::min(pinnedBottom,cell.minimum.y);
    }
    EXPECT_LT(fallingTop,2.);EXPECT_GT(fallingTop,.5);EXPECT_GT(pinnedBottom,2.5);
    const auto settledRevision=wall.geometryRevision();
    for(int i=0;i<4;++i)ASSERT_TRUE(step())<<error;
    EXPECT_EQ(wall.geometryRevision(),settledRevision);
    ASSERT_TRUE(wall.reset(error))<<error;
    for(int i=0;i<200&&!wall.ready();++i)ASSERT_TRUE(step())<<error;
    ASSERT_TRUE(wall.ready());EXPECT_FALSE(wall.released());EXPECT_EQ(wall.phase(),ImportedWallPhysics::Phase::Intact);
    for(const auto& cell:wall.settledSolids())EXPECT_GT(cell.minimum.y,2.5);
    wall.requestClear();for(int i=0;i<200&&!wall.empty();++i)ASSERT_TRUE(step())<<error;
    EXPECT_TRUE(wall.empty());EXPECT_TRUE(wall.bindings().empty());
}
TEST_F(ImportedWallGpu, CertifiedImpactNeighborhoodPreservesSourcesAndBoundsEnergyAcrossRepeatedHits) {
    auto section=source();section.parts.push_back({103,"wall/connected-upper","3004.dat",4,3,{0,5.2,0},{1,0,0,0}});
    section.bonds={{1001,101,103,0,0,true}};
    const AdventureSpatialQueries::Solid plane{{},{},{-20,-1,-20},{20,0,20}};
    ASSERT_TRUE(floor.prepare(std::span(&plane,1),{},1,error));
    ASSERT_TRUE(wall.initialize(section,{},{1,0,0,0},error))<<error;
    for(int i=0;i<300&&(!wall.ready()||!floor.ready(1));++i) {
        ASSERT_TRUE(step())<<error;std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    ASSERT_TRUE(wall.ready());ASSERT_EQ(wall.bindings().size(),3u);
    EXPECT_LT(wall.maximumPartDisplacement(),1e-5);
    const auto initialCellCount=wall.initialSolids().size();
    auto makeHit=[&] {
        ImportedWallPhysics::Impact hit;hit.geometryRevision=wall.geometryRevision();
        for(const auto& binding:wall.bindings())if(binding.sourceId==101)hit.target=binding.body;
        for(uint32_t face=0;face<4096;++face)if(wall.contactPart(hit.target,0x80000000u|face)==101u) {
            hit.targetFeature=0x80000000u|face;break;
        }
        physics::BodySpawnDesc desc;desc.position={100,100,100};hit.projectile=world.spawnBody(desc);
        hit.direction={0,0,1};hit.normalImpulse=4;hit.closingSpeed=10;hit.projectileMass=.1;hit.projectileEnergy=5;
        return hit;
    };
    auto hit=makeHit();ASSERT_TRUE(hit.projectile.valid());ASSERT_TRUE(submit(1));
    ASSERT_EQ(wall.contactPart(hit.target,hit.targetFeature),101u);
    auto invalid=hit;invalid.geometryRevision++;EXPECT_FALSE(wall.impact(invalid,error));
    invalid=hit;invalid.target.generation++;EXPECT_FALSE(wall.impact(invalid,error));
    invalid=hit;invalid.targetFeature=0;EXPECT_FALSE(wall.impact(invalid,error));
    invalid=hit;invalid.direction.x=NAN;EXPECT_FALSE(wall.impact(invalid,error));
    EXPECT_EQ(wall.impactStats().count,0u);EXPECT_FALSE(wall.released());
    ASSERT_TRUE(wall.impact(hit,error))<<error;EXPECT_EQ(wall.impactStats().count,1u);
    EXPECT_EQ(wall.impactStats().releasedParts,2u);EXPECT_EQ(wall.impactStats().sourceId,101u);
    EXPECT_GT(wall.impactStats().addedEnergy,0);EXPECT_LE(wall.impactStats().addedEnergy,wall.impactStats().energyBudget+1e-6);
    EXPECT_LE(wall.impactStats().energyBudget,1.);EXPECT_TRUE(wall.settledSolids().empty());
    EXPECT_DOUBLE_EQ(wall.maximumPartDisplacement(),0.); // No uncertified pose is published.
    EXPECT_FALSE(wall.impact(hit,error));
    for(int i=0;i<1400&&!wall.ready();++i)ASSERT_TRUE(step())<<error;
    ASSERT_TRUE(wall.ready())<<int(wall.phase())<<" "<<error;ASSERT_EQ(wall.bindings().size(),3u);
    EXPECT_EQ(world.stats().residentBodies,4u); // Floor + all three source parts, no projectile.
    EXPECT_GT(wall.maximumPartDisplacement(),1.);
    double top=-100,pinnedBottom=100;
    for(const auto& cell:wall.settledSolids()) {
        if(cell.sourceId==101)top=std::max(top,cell.maximum.y);
        if(cell.sourceId==102)pinnedBottom=std::min(pinnedBottom,cell.minimum.y);
    }
    EXPECT_LT(top,2.);EXPECT_GT(pinnedBottom,2.5);
    const auto previousContact=wall.impactStats().worldPoint;
    hit=makeHit();ASSERT_TRUE(hit.projectile.valid());ASSERT_TRUE(submit(1));
    ASSERT_TRUE(wall.impact(hit,error))<<error;EXPECT_EQ(wall.impactStats().count,2u);
    // The current settled pose is the second impact origin, never the original
    // floating source layout. This is a component API test; runtime tests supply
    // the actual certified GPU ContactHit evidence.
    EXPECT_LT(wall.impactStats().worldPoint.y,previousContact.y-1.);
    EXPECT_LE(wall.impactStats().addedEnergy,wall.impactStats().energyBudget+1e-6);
    for(int i=0;i<1400&&!wall.ready();++i)ASSERT_TRUE(step())<<error;
    ASSERT_TRUE(wall.ready());ASSERT_EQ(wall.bindings().size(),3u);
    ASSERT_TRUE(wall.reset(error))<<error;
    for(int i=0;i<300&&!wall.ready();++i){ASSERT_TRUE(step())<<error;std::this_thread::sleep_for(std::chrono::milliseconds(1));}
    ASSERT_TRUE(wall.ready());EXPECT_FALSE(wall.released());
    EXPECT_LT(wall.maximumPartDisplacement(),1e-5);
    EXPECT_EQ(wall.settledSolids().size(),initialCellCount); // Original stud collision is restored.
    for(const auto& cell:wall.settledSolids())EXPECT_GT(cell.minimum.y,2.5);
}
TEST_F(ImportedWallGpu, CenteredImpactDoesNotInventTorqueInReleasedNeighbors) {
    auto section=source();section.parts.push_back({103,"wall/connected-upper","3004.dat",4,3,{0,5.2,0},{1,0,0,0}});
    section.bonds={{1001,101,103,0,0,true}};
    ASSERT_TRUE(floor.prepare({}, {},1,error));
    ASSERT_TRUE(wall.initialize(section,{},{1,0,0,0},error))<<error;
    for(int i=0;i<300&&(!wall.ready()||!floor.ready(1));++i) {
        ASSERT_TRUE(step())<<error;std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    ASSERT_TRUE(wall.ready());
    const auto compound=ImportedAssembly::prepare(section,error);ASSERT_TRUE(compound)<<error;
    auto struckSource=section;struckSource.parts.resize(1);struckSource.bonds.clear();struckSource.anchors={101};
    const auto struck=ImportedAssembly::prepare(struckSource,error);ASSERT_TRUE(struck)<<error;
    const auto& mass=struck->roots()[0].shape.packedMass();
    // The contact normal passes through the struck brick's own COM. The upper
    // neighbor is released but has no direct contact impulse or artificial spin.
    const auto contact=struck->roots()[0].origin+glm::dvec3(mass.centerInverseMass[0],mass.centerInverseMass[1],-.5);
    const auto r=*compound->rootForPart(101);physics::AuthoredFrameError issue;
    const auto anchor=physics::AuthoredBodyFrame(compound->roots()[r].shape)
        .bodyPoint(glm::vec3(contact-compound->roots()[r].origin),issue);
    ASSERT_TRUE(anchor);
    ImportedWallPhysics::Impact hit;hit.geometryRevision=wall.geometryRevision();hit.targetLocalPoint=*anchor;
    for(const auto& binding:wall.bindings())if(binding.sourceId==101)hit.target=binding.body;
    for(uint32_t face=0;face<4096;++face)if(wall.contactPart(hit.target,0x80000000u|face)==101u) {
        hit.targetFeature=0x80000000u|face;break;
    }
    physics::BodySpawnDesc projectile;projectile.position={100,100,100};hit.projectile=world.spawnBody(projectile);
    ASSERT_TRUE(hit.projectile.valid());ASSERT_TRUE(submit(1));
    hit.direction={0,0,1};hit.normalImpulse=100;hit.closingSpeed=48;hit.projectileMass=1;hit.projectileEnergy=500;
    ASSERT_TRUE(wall.impact(hit,error))<<error;
    EXPECT_EQ(wall.impactStats().releasedParts,2u);
    EXPECT_NEAR(wall.impactStats().rotationalEnergy,0.,1e-9);
    EXPECT_NEAR(wall.impactStats().addedEnergy,.5*struck->roots()[0].mass.massKg*12.*12.,.0001);
    EXPECT_LE(wall.impactStats().addedEnergy,wall.impactStats().energyBudget);
}
TEST_F(ImportedWallGpu, OffCenterImpactUsesPackedInertiaAndBoundsLinearPlusAngularEnergy) {
    ASSERT_TRUE(install())<<error;
    const auto graph=ImportedAssembly::prepare(source(),error);ASSERT_TRUE(graph);
    const auto r=*graph->rootForPart(101);
    ImportedWallPhysics::Impact hit;hit.geometryRevision=wall.geometryRevision();
    for(const auto& binding:wall.bindings())if(binding.sourceId==101)hit.target=binding.body;
    for(uint32_t face=0;face<4096;++face)if(wall.contactPart(hit.target,0x80000000u|face)==101u) {
        hit.targetFeature=0x80000000u|face;break;
    }
    physics::AuthoredFrameError issue;
    const auto anchor=physics::AuthoredBodyFrame(graph->roots()[r].shape)
        .bodyPoint(glm::vec3(glm::dvec3(.9,3.4,-.5)-graph->roots()[r].origin),issue);
    ASSERT_TRUE(anchor);hit.targetLocalPoint=*anchor;
    physics::BodySpawnDesc projectile;projectile.position={100,100,100};hit.projectile=world.spawnBody(projectile);
    ASSERT_TRUE(hit.projectile.valid());ASSERT_TRUE(submit(1));
    hit.direction={0,0,1};hit.normalImpulse=100;hit.closingSpeed=48;hit.projectileMass=1;hit.projectileEnergy=500;
    ASSERT_TRUE(wall.impact(hit,error))<<error;
    const auto stats=wall.impactStats();EXPECT_DOUBLE_EQ(stats.energyBudget,36.);
    EXPECT_GT(stats.rotationalEnergy,1.);EXPECT_GT(stats.addedEnergy,stats.rotationalEnergy);
    EXPECT_LE(stats.addedEnergy,stats.energyBudget);EXPECT_EQ(stats.releasedParts,1u);
    physics::BodyHandle moving;
    for(int i=0;i<300&&!moving.valid();++i) {
        ASSERT_TRUE(step())<<error;
        for(const auto& binding:wall.bindingsForEncodedTick(world.encodedTick()))
            if(binding.sourceId==101&&binding.body!=hit.target)moving=binding.body;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    ASSERT_TRUE(moving.valid());
    world.requestDebugSnapshot({moving.index,1});ASSERT_TRUE(submit(0));
    std::optional<physics::DebugSnapshot> snapshot;
    ASSERT_TRUE(wait([&]{snapshot=world.pollDebugSnapshot();return snapshot.has_value();}));
    ASSERT_EQ(snapshot->bodies.size(),1u);const auto& body=snapshot->bodies[0];ASSERT_TRUE(body.alive);
    EXPECT_LT(body.angularVelocity.y,-1.f);EXPECT_LE(glm::length(body.angularVelocity),10.001f);
    EXPECT_GT(body.linearVelocity.z,1.f);EXPECT_LT(glm::length(body.linearVelocity),12.01f);
    // The hollow brick COM is not the rectangular envelope's midpoint. Use
    // its packed COM and full principal inertia, including the real X torque.
    // The free body rotates during admission, so compare WORLD angular momentum
    // (conserved by the gyroscopic step), not fixed component angular velocity.
    const auto& mass=graph->roots()[r].shape.packedMass();
    const glm::dvec3 center(mass.centerInverseMass[0],mass.centerInverseMass[1],mass.centerInverseMass[2]);
    const auto lever=glm::dvec3(.9,3.4,-.5)-graph->roots()[r].origin-center;
    const auto expectedMomentumPerSpeed=glm::cross(lever,glm::dvec3(0,0,1./double(mass.centerInverseMass[3])));
    const auto principal=glm::inverse(glm::dquat(body.orientation))*glm::dvec3(body.angularVelocity);
    const auto worldMomentum=glm::dquat(body.orientation)*(principal/
        glm::dvec3(mass.inverseInertiaRadius[0],mass.inverseInertiaRadius[1],mass.inverseInertiaRadius[2]));
    EXPECT_LT(glm::length(worldMomentum/double(body.linearVelocity.z)-expectedMomentumPerSpeed),.005);

}
TEST_F(ImportedWallGpu, ImpactIncludesNearbyEndOfRotatedLongSupportingPlate) {
    auto section=source();const auto partRotation=glm::angleAxis(glm::radians(90.),glm::dvec3(0,1,0));
    section.parts[0].partNumber="3460.dat";section.parts[0].rotation=partRotation;
    section.parts[1].translation.x=20;
    section.parts.push_back({103,"wall/end-brick","3005.dat",4,3,
        glm::dvec3(0,5.2,0)+partRotation*glm::dvec3(3.5,0,0),partRotation});
    section.bonds={{1001,101,103,7,0,true}};
    const glm::dvec3 worldOrigin(100,20,-50);const auto wallRotation=glm::angleAxis(.6f,glm::vec3(0,1,0));
    ASSERT_TRUE(floor.prepare({}, {},1,error));
    ASSERT_TRUE(wall.initialize(section,worldOrigin,wallRotation,error))<<error;
    for(int i=0;i<300&&(!wall.ready()||!floor.ready(1));++i) {
        ASSERT_TRUE(step())<<error;std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    ASSERT_TRUE(wall.ready());
    const auto graph=ImportedAssembly::prepare(section,error);ASSERT_TRUE(graph)<<error;
    const auto r=*graph->rootForPart(103);
    ImportedWallPhysics::Impact hit;hit.geometryRevision=wall.geometryRevision();
    for(const auto& binding:wall.bindings())if(binding.sourceId==103)hit.target=binding.body;
    for(uint32_t face=0;face<4096;++face)if(wall.contactPart(hit.target,0x80000000u|face)==103u) {
        hit.targetFeature=0x80000000u|face;break;
    }
    const auto center=section.parts.back().translation+partRotation*glm::dvec3(0,-.6,0);
    physics::AuthoredFrameError frameError;
    const auto anchor=physics::AuthoredBodyFrame(graph->roots()[r].shape)
        .bodyPoint(glm::vec3(center-graph->roots()[r].origin),frameError);
    ASSERT_TRUE(anchor);hit.targetLocalPoint=*anchor;
    physics::BodySpawnDesc projectile;projectile.position={100,100,100};hit.projectile=world.spawnBody(projectile);
    ASSERT_TRUE(hit.projectile.valid());ASSERT_TRUE(submit(1));
    hit.direction=wallRotation*glm::vec3(0,0,1);hit.normalImpulse=4;hit.closingSpeed=10;
    hit.projectileMass=.1;hit.projectileEnergy=5;
    ASSERT_TRUE(wall.impact(hit,error))<<error;
    EXPECT_EQ(wall.impactStats().sourceId,103u);
    EXPECT_EQ(wall.impactStats().releasedParts,2u)
        <<"Plate centre is >3.5 studs away, but its supporting end is only .6 away";
    EXPECT_LT(glm::length(wall.impactStats().worldPoint-(worldOrigin+glm::dquat(wallRotation)*center)),1e-5);
}
TEST_F(ImportedWallGpu, ManualSupportRemovalDropsRemainingPartAndResetRestoresSourceBindings) {
    auto section=source();section.parts[0].partNumber="3005.dat";section.parts[0].translation={0,1.2,0};
    section.parts.push_back({103,"wall/above-support","3005.dat",4,1,{0,2.4,0},{1,0,0,0}});
    section.bonds={{1001,101,103,0,0,true}};
    const AdventureSpatialQueries::Solid plane{{},{},{-20,-1,-20},{20,0,20}};
    ASSERT_TRUE(floor.prepare(std::span(&plane,1),{},1,error));
    ASSERT_TRUE(wall.initialize(section,{},{1,0,0,0},error))<<error;
    for(int i=0;i<300&&(!wall.ready()||!floor.ready(1));++i) {
        ASSERT_TRUE(step())<<error;std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    ASSERT_TRUE(wall.ready());ASSERT_EQ(wall.bindings().size(),3u);
    const auto revision=wall.geometryRevision();
    EXPECT_FALSE(wall.release(error,102));EXPECT_FALSE(wall.release(error,999));
    EXPECT_EQ(wall.geometryRevision(),revision);EXPECT_FALSE(wall.released());EXPECT_EQ(wall.bindings().size(),3u);
    ASSERT_TRUE(wall.release(error,101))<<error;EXPECT_TRUE(wall.released());EXPECT_TRUE(wall.settledSolids().empty());
    EXPECT_FALSE(wall.release(error,101));
    for(int i=0;i<1400&&!wall.ready();++i)ASSERT_TRUE(step())<<error;
    ASSERT_TRUE(wall.ready());ASSERT_EQ(wall.phase(),ImportedWallPhysics::Phase::Settled);
    ASSERT_EQ(wall.bindings().size(),2u);
    bool sawUpper=false;
    for(const auto& binding:wall.bindings()) {
        EXPECT_NE(binding.sourceId,101u);
        if(binding.sourceId==103){sawUpper=true;EXPECT_EQ(binding.meshNode,1u);}
    }
    EXPECT_TRUE(sawUpper);double upperTop=-100;
    for(const auto& cell:wall.settledSolids()) {
        EXPECT_NE(cell.sourceId,101u);
        if(cell.sourceId==103)upperTop=std::max(upperTop,cell.maximum.y);
    }
    EXPECT_GT(upperTop,1.);EXPECT_LT(upperTop,1.5); // Fell about one full brick height under gravity.
    EXPECT_FALSE(wall.release(error,103));
    ASSERT_TRUE(wall.reset(error))<<error;
    for(int i=0;i<300&&!wall.ready();++i) {
        ASSERT_TRUE(step())<<error;std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    ASSERT_TRUE(wall.ready());EXPECT_FALSE(wall.released());ASSERT_EQ(wall.bindings().size(),3u);
    EXPECT_EQ(wall.phase(),ImportedWallPhysics::Phase::Intact);
    size_t sharedMeshInstances=0;for(const auto& binding:wall.bindings())if(binding.meshNode==1)++sharedMeshInstances;
    EXPECT_EQ(sharedMeshInstances,2u);
    upperTop=-100;bool restoredSupport=false;
    for(const auto& cell:wall.settledSolids()) {
        restoredSupport|=cell.sourceId==101;
        if(cell.sourceId==103)upperTop=std::max(upperTop,cell.maximum.y);
    }
    EXPECT_TRUE(restoredSupport);EXPECT_GT(upperTop,2.5);
}
TEST_F(ImportedWallGpu, ResetDuringFallDiscardsOldObservationAndRetainsEverySourcePart) {
    ASSERT_TRUE(install())<<error;ASSERT_TRUE(wall.release(error));
    for(int i=0;i<100&&wall.phase()!=ImportedWallPhysics::Phase::Falling;++i)ASSERT_TRUE(step())<<error;
    ASSERT_EQ(wall.phase(),ImportedWallPhysics::Phase::Falling);ASSERT_TRUE(step(8));
    ASSERT_TRUE(wall.reset(error))<<error;
    for(int i=0;i<200&&!wall.ready();++i)ASSERT_TRUE(step())<<error;
    ASSERT_TRUE(wall.ready());EXPECT_FALSE(wall.released());ASSERT_EQ(wall.bindings().size(),2u);
    for(const auto& cell:wall.settledSolids())EXPECT_GT(cell.minimum.y,2.5);
    ASSERT_TRUE(wall.release(error));
    for(int i=0;i<1400&&!wall.ready();++i)ASSERT_TRUE(step())<<error;
    ASSERT_EQ(wall.phase(),ImportedWallPhysics::Phase::Settled);
}
TEST_F(ImportedWallGpu, RenderSwitchesHandlesOnEncodedMutationBeforeGameplayPublication) {
    ASSERT_TRUE(install())<<error;const auto old=wall.bindings()[0].body;
    ASSERT_TRUE(wall.release(error));
    ASSERT_TRUE(wait([&]{
        if(!wall.update(world,error))return true;
        return wall.phase()==ImportedWallPhysics::Phase::Releasing&&!wall.needsQuiescentBoundary();
    }))<<error;
    ASSERT_EQ(wall.phase(),ImportedWallPhysics::Phase::Releasing);
    const auto before=world.encodedTick();
    ASSERT_EQ(wall.bindingsForEncodedTick(before).size(),2u);
    EXPECT_EQ(wall.bindingsForEncodedTick(before)[0].body,old);
    ASSERT_TRUE(submit(1));
    // No owner update/publish occurred after encoding. The render view must use
    // the handles admitted by that physics pass, not the just-destroyed roots.
    EXPECT_EQ(wall.bindings()[0].body,old);EXPECT_FALSE(wall.ready());
    ASSERT_EQ(wall.bindingsForEncodedTick(world.encodedTick()).size(),2u);
    const auto next=wall.bindingsForEncodedTick(world.encodedTick())[0].body;
    EXPECT_TRUE(next.valid());EXPECT_NE(next,old);
    ASSERT_TRUE(wall.update(world,error));EXPECT_EQ(wall.bindings()[0].body,next);
}

TEST_F(ImportedWallGpu, AccumulatorWorldCertifiesAtJoinedTickWithoutChangingClock) {
    const AdventureSpatialQueries::Solid plane{{},{},{-20,-1,-20},{20,0,20}};
    ASSERT_TRUE(floor.prepare(std::span(&plane,1),{},1,error));
    ASSERT_TRUE(wall.initialize(source(),{},{1,0,0,0},error));
    bool sawCertification=false;
    uint64_t certificationTick=0;
    const auto accumulatorStep=[&]() -> bool {
        world.update(0);
        if(!wall.update(world,error))return false;
        if(floor.update(world,error)==CannonPhysicsScene::Progress::Failed)return false;
        const auto* pool=world.authoredShapeResources();
        if(!pool||pool->stats().phase!=physics::ShapeResourcePhase::Ready) {
            return wait([&]{const auto* current=world.authoredShapeResources();
                return current&&current->stats().phase==physics::ShapeResourcePhase::Ready;});
        }
        // The application's host controls scheduling; the wall only requests a
        // quiescent boundary. The initial empty submission activates ownership.
        if(!world.tickFrontier().supported&&!submit(0))return false;
        const bool paused=wall.needsQuiescentBoundary()||floor.needsQuiescentBoundary();
        if(!world.setSchedulingPaused(paused))return false;
        world.update(1.f/60.f);
        const bool certifying=wall.phase()==ImportedWallPhysics::Phase::Certifying;
        if(certifying) {
            sawCertification=true;certificationTick=world.tickFrontier().completed;
            EXPECT_TRUE(paused);
            EXPECT_EQ(world.encodedTick(),certificationTick);
        }
        if(!submit(0))return false;
        if(certifying){EXPECT_EQ(world.encodedTick(),certificationTick);}
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        return true;
    };
    for(int i=0;i<300&&(!wall.ready()||!floor.ready(1));++i)ASSERT_TRUE(accumulatorStep())<<error;
    ASSERT_TRUE(wall.ready());ASSERT_TRUE(floor.ready(1));
    ASSERT_TRUE(wall.release(error));
    for(int i=0;i<1000&&!wall.ready();++i)ASSERT_TRUE(accumulatorStep())<<error;
    ASSERT_TRUE(wall.ready())<<"phase="<<int(wall.phase())<<" "<<error;
    EXPECT_EQ(wall.phase(),ImportedWallPhysics::Phase::Settled);
    EXPECT_TRUE(sawCertification);EXPECT_GT(certificationTick,0u);
    EXPECT_FALSE(wall.settledSolids().empty());
    ASSERT_TRUE(world.setSchedulingPaused(false));
    EXPECT_FALSE(world.scheduleFixedTicks(1)); // The host still owns accumulator mode.
    const auto before=world.encodedTick();
    world.update(1.f/60.f);ASSERT_TRUE(submit(0));
    EXPECT_GT(world.encodedTick(),before);
}

TEST_F(ImportedWallGpu, FailedPartialReplacementCanDrainAndRebuildAfterCapacityReturns) {
    ASSERT_TRUE(install())<<error;
    // Floor + two wall roots occupy three slots. Leave one slot free so the
    // replacement admits one root and then must roll that reservation back.
    std::vector<physics::BodyHandle> fillers;
    for(uint32_t i=0;i<28;++i) {
        physics::BodySpawnDesc desc;desc.position={1000.f+float(i)*4.f,10,1000};
        const auto body=world.spawnBody(desc);ASSERT_TRUE(body.valid());fillers.push_back(body);
    }
    ASSERT_TRUE(submit(1));ASSERT_TRUE(wall.release(error));
    bool failed=false;
    for(int i=0;i<100&&!failed;++i)failed=!step();
    ASSERT_TRUE(failed);ASSERT_EQ(wall.phase(),ImportedWallPhysics::Phase::Failed);
    EXPECT_TRUE(wall.released());EXPECT_TRUE(wall.busy());EXPECT_EQ(wall.bindings().size(),2u);
    for(const auto body:fillers)ASSERT_TRUE(world.destroyBody(body));
    ASSERT_TRUE(wall.reset(error))<<error;
    EXPECT_EQ(wall.phase(),ImportedWallPhysics::Phase::Recovering);
    for(int i=0;i<300&&!wall.ready();++i)ASSERT_TRUE(step())<<error;
    ASSERT_TRUE(wall.ready())<<error;EXPECT_FALSE(wall.released());
    ASSERT_EQ(wall.bindings().size(),2u);EXPECT_EQ(world.stats().residentBodies,3u);
    for(const auto& cell:wall.settledSolids())EXPECT_GT(cell.minimum.y,2.5);
}

TEST_F(ImportedWallGpu, NeverSleepingFallTimesOutAndStillAllowsRebuild) {
    ASSERT_TRUE(install(false))<<error;ASSERT_TRUE(wall.release(error));
    bool failed=false;
    for(int i=0;i<400&&!failed;++i)failed=!step(8);
    ASSERT_TRUE(failed);EXPECT_EQ(wall.phase(),ImportedWallPhysics::Phase::Failed);
    EXPECT_NE(error.find("haven't settled"),std::string::npos)<<error;
    EXPECT_TRUE(wall.released());EXPECT_TRUE(wall.busy());EXPECT_TRUE(wall.settledSolids().empty());
    ASSERT_TRUE(wall.reset(error))<<error;
    for(int i=0;i<200&&!wall.ready();++i)ASSERT_TRUE(step())<<error;
    EXPECT_TRUE(wall.ready());EXPECT_FALSE(wall.released());
}

#endif
} // namespace
} // namespace voxy::game::adventure
