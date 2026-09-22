#include "game/adventure/imported_wall_physics.hpp"
#include "gpu/context.hpp"
#include <gtest/gtest.h>
#include <chrono>
#include <thread>
#include <sstream>
#include <set>
#include <cstdlib>
#include <fstream>

#ifndef VOXY_WASM
struct WGPUWrappedSubmissionIndex;
extern "C" WGPUBool wgpuDevicePoll(WGPUDevice,WGPUBool,const WGPUWrappedSubmissionIndex*);

namespace voxy::game::adventure {
namespace {
class ImportedStackGpu: public testing::TestWithParam<const char*> {
protected:
    gpu::Context context;
    physics::PhysicsWorld world;
    ImportedWallPhysics stack;
    CannonPhysicsScene floor;
    std::string error;
    void SetUp() override {
        ASSERT_TRUE(context.initHeadless());
        physics::PhysicsInitContext init;
        init.requestedBackend=physics::BackendType::WebGpuSoft;
        init.device=context.getDevice();init.queue=context.getQueue();
        init.maxBodies=32;init.maxActiveBodies=32;init.maxPairs=256;
        init.maxContacts=256;init.maxManifolds=256;
        init.gpu.authoredContactPatches=8;
        init.gpu.commandCapacity=128;init.gpu.debugReadbackBodyCapacity=32;
        ASSERT_TRUE(world.initialize(init));
    }
    void TearDown() override {
        world.shutdown();stack.abandonAfterWorldShutdown();floor.abandonAfterWorldShutdown();
    }
    template<class F> bool wait(F condition) {
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(10);
        do {
            static_cast<void>(wgpuDevicePoll(context.getDevice(),false,nullptr));world.update(0);
            if(condition())return true;
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        } while(std::chrono::steady_clock::now()<deadline);
        return false;
    }
    bool submit(uint32_t ticks) {
        if(ticks&&!world.scheduleFixedTicks(ticks)){error="schedule failed";return false;}
        physics::ShapeResourceError issue;
        auto ticket=world.prepareGpuSubmission(issue);
        // Larger source sections can still have asynchronous shape uploads.
        // Retry preparation without scheduling the same simulation tick twice.
        if(!ticket.valid()&&issue==physics::ShapeResourceError::Busy) {
            if(!wait([&]{ticket=world.prepareGpuSubmission(issue);return ticket.valid()||issue!=physics::ShapeResourceError::Busy;})) {
                error="submission preparation stayed busy";return false;
            }
        }
        if(!ticket.valid()){error="prepare submission failed: "+std::to_string(int(issue));return false;}
        WGPUCommandEncoderDescriptor ed{};
        const auto encoder=wgpuDeviceCreateCommandEncoder(context.getDevice(),&ed);
        const auto report=world.encodeGpuStepChecked(encoder);WGPUCommandBufferDescriptor cd{};
        const auto command=report.succeeded()?wgpuCommandEncoderFinish(encoder,&cd):nullptr;
        wgpuCommandEncoderRelease(encoder);
        if(!command){static_cast<void>(world.discardGpuSubmission(ticket));error="encode failed: "+std::to_string(int(report.status));return false;}
        const auto result=world.submitGpuSubmission(ticket,std::span(&command,1));wgpuCommandBufferRelease(command);
        if(result!=physics::ShapeResourceError::None){error="submit failed: "+std::to_string(int(result));return false;}
        const auto tick=world.encodedTick();
        const bool observed=wait([&]{const auto frontier=world.tickFrontier();
            const auto* pool=world.authoredShapeResources();
            return frontier.failed||(frontier.completed>=tick&&pool
                &&pool->stats().cpu.completed>=ticket.serial);});
        const auto frontier=world.tickFrontier();
        if(frontier.failed) {
            const auto stats=world.stats();
            error="GPU proof failed: encoded="+std::to_string(tick)
                +" completed="+std::to_string(frontier.completed)
                +" highContacts="+std::to_string(stats.highContacts)
                +" contactCapacity="+std::to_string(stats.contactCapacity)
                +" highManifolds="+std::to_string(stats.highManifolds);
            return false;
        }
        if(!observed)error="GPU completion timed out at tick "+std::to_string(tick);
        return observed;
    }
    bool step() {
        if(!stack.update(world,error))return false;
        if(floor.update(world,error)==CannonPhysicsScene::Progress::Failed)return false;
        const auto* resources=world.authoredShapeResources();
        if(!resources||resources->stats().phase!=physics::ShapeResourcePhase::Ready) {
            return wait([&]{return world.authoredShapeResources()&&world.authoredShapeResources()->stats().phase==physics::ShapeResourcePhase::Ready;});
        }
        return submit(stack.needsQuiescentBoundary()||floor.needsQuiescentBoundary()?0:1);
    }
};

TEST_P(ImportedStackGpu, ThreeSourceBricksReleasedAtRestSettleWithoutArtificialImpulse) {
    const bool distant=std::string_view(GetParam()).starts_with("distant-");
    const char* partName=GetParam()+(distant?8:0);
    const auto* part=importedPartCatalog(partName);ASSERT_NE(part,nullptr);
    const glm::dvec3 origin=distant?glm::dvec3(1208,.185,-1032):glm::dvec3(0);
    const glm::dquat rotation=distant?glm::angleAxis(glm::pi<double>(),glm::dvec3(0,1,0)):glm::dquat(1,0,0,0);
    ImportedAssemblySource source;source.assetId="three-stud-stack";source.sourceSha256=std::string(64,'a');
    for(uint64_t i=0;i<3;++i) {
        source.parts.push_back({i+1,"stack/"+std::to_string(i),partName,4,uint32_t(i+1),
            {0,1+double(i+1)*part->height,0},{1,0,0,0}});
        if(i)for(uint16_t slot=0;slot<part->connectorCount();++slot)
            source.bonds.push_back({100+i*16+slot,i,i+1,slot,slot,true});
    }
    source.anchors={1};
    ASSERT_TRUE(stack.initialize(source,origin,rotation,error))<<error;
    const AdventureSpatialQueries::Solid ground{{},{},origin+glm::dvec3(-10,0,-10),origin+glm::dvec3(10,1,10)};
    ASSERT_TRUE(floor.prepare(std::span(&ground,1),{},1,error))<<error;
    for(int i=0;i<300&&(!stack.ready()||!floor.ready(1));++i)ASSERT_TRUE(step())<<error;
    ASSERT_TRUE(stack.ready());ASSERT_TRUE(floor.ready(1));
    ASSERT_TRUE(stack.release(error))<<error;
    const auto start=world.encodedTick();
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(30);
    while(!stack.ready()&&world.encodedTick()-start<1200&&std::chrono::steady_clock::now()<deadline)
        ASSERT_TRUE(step())<<error;
    std::string diagnostic;
    if(!stack.ready()) {
        world.requestDebugSnapshot({1,32});ASSERT_TRUE(submit(1));
        const auto requested=world.encodedTick();
        ASSERT_TRUE(wait([&] {
            const auto snapshot=world.pollDebugSnapshot();if(!snapshot||snapshot->tick<requested)return false;
            std::ostringstream out;
            for(const auto& body:snapshot->bodies)if(body.alive&&glm::length(body.inverseInertia)>0) {
                const auto p=physics::worldPositionToAbsolute({body.sector,body.position});
                out<<" body="<<body.handle.index<<" awake="<<body.awake<<" p="<<p.x<<','<<p.y<<','<<p.z
                    <<" v="<<body.linearVelocity.x<<','<<body.linearVelocity.y<<','<<body.linearVelocity.z
                    <<" omega="<<body.angularVelocity.x<<','<<body.angularVelocity.y<<','<<body.angularVelocity.z;
            }
            diagnostic=out.str();return true;
        }));
    }
    RecordProperty("simulatedTicks",std::to_string(world.encodedTick()-start));
    EXPECT_TRUE(stack.ready())<<"part="<<GetParam()<<" phase="<<int(stack.phase())<<diagnostic;
    if(stack.ready()) {
        EXPECT_EQ(stack.bindings().size(),3u);
        for(const auto& cell:stack.settledSolids())EXPECT_GT(cell.minimum.y,origin.y+.95);
    }
}
INSTANTIATE_TEST_SUITE_P(Catalog,ImportedStackGpu,testing::Values("3005.dat","3004.dat","3023.dat","distant-3005.dat"));

// Isolation fixture: real source wall topology over a synthetic flat support.
// This deliberately excludes the house remainder and is not world acceptance.
class ImportedWallFlatFloorGpu: public ImportedStackGpu {
protected:
    void SetUp() override {
        gpu::ContextConfig contextConfig;
        contextConfig.enableTimestamps=std::getenv("VOXY_IMPORTED_WALL_PROFILE")!=nullptr;
        ASSERT_TRUE(context.initHeadless(contextConfig));
        physics::PhysicsInitContext init;init.requestedBackend=physics::BackendType::WebGpuSoft;
        init.device=context.getDevice();init.queue=context.getQueue();
        init.maxBodies=128;init.maxActiveBodies=128;init.maxPairs=4096;
        init.maxContacts=4096;init.maxManifolds=4096;
        init.gpu.authoredContactPatches=8;
        init.gpu.commandCapacity=128;init.gpu.debugReadbackBodyCapacity=128;
        init.gpu.substeps=16;
        init.gpu.enableStageProfiling=contextConfig.enableTimestamps;
        init.gpu.stageProfilingIntervalTicks=1;
        if(contextConfig.enableTimestamps) {
            ASSERT_TRUE(wgpuDeviceHasFeature(context.getDevice(),WGPUFeatureName_TimestampQuery));
        }
        ASSERT_TRUE(world.initialize(init));
    }
};
TEST_F(ImportedWallFlatFloorGpu, ActualSourceWallSettlesOnSyntheticFlatFloor) {
    const auto source=loadImportedSection("data/adventure/ldraw-blacksmith-parts-r01/wall.json",error);
    ASSERT_TRUE(source)<<error;ASSERT_EQ(source->parts.size(),39u);
    const auto graph=ImportedAssembly::prepare(*source,error);ASSERT_TRUE(graph)<<error;
    std::set<uint64_t> eligible;
    for(const auto& root:graph->roots())if(root.anchored)
        for(const auto index:root.partIndices)eligible.insert(graph->source().parts[index].sourceId);
    ASSERT_EQ(eligible.size(),29u);
    ASSERT_TRUE(stack.initialize(*source,{},{1,0,0,0},error))<<error;
    glm::dvec3 minimum(INFINITY),maximum(-INFINITY);double support=INFINITY;
    for(const auto& cell:stack.initialSolids()) {
        minimum=glm::min(minimum,cell.minimum);maximum=glm::max(maximum,cell.maximum);
        if(eligible.contains(cell.sourceId))support=std::min(support,cell.minimum.y);
    }
    ASSERT_TRUE(std::isfinite(support));
    // Wide enough for the eight-unit wall to topple without falling off an
    // artificial narrow platform; still inside the96-unit partition bound.
    const auto center=(minimum+maximum)*.5;
    const AdventureSpatialQueries::Solid ground{{},{},
        {center.x-40,support-1,center.z-40},{center.x+40,support,center.z+40}};
    ASSERT_TRUE(floor.prepare(std::span(&ground,1),{},1,error))<<error;
    for(int i=0;i<300&&(!stack.ready()||!floor.ready(1));++i)ASSERT_TRUE(step())<<error;
    ASSERT_TRUE(stack.ready());ASSERT_TRUE(floor.ready(1));
    ASSERT_TRUE(stack.release(error))<<error;
    const auto start=world.encodedTick();
    std::vector<physics::PhysicsGpuStageTiming> stageSamples;
    const char* profilePath=std::getenv("VOXY_IMPORTED_WALL_PROFILE");
    const auto collectProfile=[&] {
        while(auto sample=world.pollGpuStageTimings())
            if(sample->tick>=start+120&&sample->tick<start+240)stageSamples.push_back(*sample);
    };
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(60);
    while(!stack.ready()&&world.encodedTick()-start<1200&&std::chrono::steady_clock::now()<deadline) {
        ASSERT_TRUE(step())<<"phase="<<int(stack.phase())<<" error="<<error;
        if(profilePath)collectProfile();
    }
    if(profilePath) {
        collectProfile();
        ASSERT_EQ(stageSamples.size(),120u)<<"Require the complete identical awake window";
        std::filesystem::path outputPath(profilePath);
        if(const char* artifacts=std::getenv("TEST_UNDECLARED_OUTPUTS_DIR"))
            outputPath=std::filesystem::path(artifacts)/outputPath.filename();
        std::ofstream output(outputPath);ASSERT_TRUE(output.good());
        output<<"{\"units\":\"uncalibrated GPU timestamp ticks\",\"substeps\":16,\"samples\":[";
        for(size_t index=0;index<stageSamples.size();++index) {
            const auto& sample=stageSamples[index];uint64_t total=0;
            for(const auto ticks:sample.timestampTicks)total+=ticks;
            if(index)output<<',';
            output<<"{\"tick\":"<<sample.tick-start<<",\"solver\":"
                <<sample.timestampTicks[static_cast<size_t>(physics::PhysicsGpuStage::DynamicSolverSolve)]
                <<",\"total\":"<<total<<'}';
        }
        output<<"]}\n";ASSERT_TRUE(output.good());
    }
    RecordProperty("syntheticFloorTop",support);
    RecordProperty("simulatedTicks",std::to_string(world.encodedTick()-start));
    std::string diagnostic;
    if(!stack.ready()) {
        world.requestDebugSnapshot({1,127});ASSERT_TRUE(submit(1));const auto requested=world.encodedTick();
        ASSERT_TRUE(wait([&]{
            const auto snapshot=world.pollDebugSnapshot();if(!snapshot||snapshot->tick<requested)return false;
            std::ostringstream out,positions;size_t awake=0,dynamic=0;float linear=0,angular=0;
            for(const auto& body:snapshot->bodies)if(body.alive&&glm::length(body.inverseInertia)>0) {
                ++dynamic;awake+=body.awake?1u:0u;
                linear=std::max(linear,glm::length(body.linearVelocity));
                angular=std::max(angular,glm::length(body.angularVelocity));
                const auto p=physics::worldPositionToAbsolute({body.sector,body.position});
                positions<<"\n body="<<body.handle.index<<" awake="<<body.awake
                    <<" position="<<p.x<<","<<p.y<<","<<p.z
                    <<" speed="<<glm::length(body.linearVelocity)<<" angular="<<glm::length(body.angularVelocity)
                    <<" velocity="<<body.linearVelocity.x<<","<<body.linearVelocity.y<<","<<body.linearVelocity.z
                    <<" omega="<<body.angularVelocity.x<<","<<body.angularVelocity.y<<","<<body.angularVelocity.z
                    <<" orientation(wxyz)="<<body.orientation.w<<","<<body.orientation.x<<","<<body.orientation.y<<","<<body.orientation.z;
            }
            out<<" dynamic="<<dynamic<<" awake="<<awake<<" maxLinear="<<linear<<" maxAngular="<<angular;
            diagnostic=out.str()+positions.str();return true;
        }));
    }
    EXPECT_TRUE(stack.ready())<<"Synthetic floor only; phase="<<int(stack.phase())<<" "<<error<<diagnostic;
    if(stack.ready()) {
        EXPECT_EQ(stack.bindings().size(),39u);
        EXPECT_EQ(stack.phase(),ImportedWallPhysics::Phase::Settled);
        for(const auto& cell:stack.settledSolids()) {
            if(eligible.contains(cell.sourceId)) { EXPECT_GT(cell.minimum.y,support-.05); }
        }
    }
}

class RawImportedStackGpu: public ImportedStackGpu {};
TEST_P(RawImportedStackGpu, ThreePieceSleepsWithoutBias) {
    const bool solidBox=std::string_view(GetParam())=="solid-3005.dat";
    const char* partName=solidBox?"3005.dat":GetParam();
    const auto* part=importedPartCatalog(partName);ASSERT_NE(part,nullptr);
    ImportedAssemblySource source;source.assetId="three-piece-activity";source.sourceSha256=std::string(64,'a');
    for(uint64_t i=0;i<3;++i)source.parts.push_back({i+1,"activity/"+std::to_string(i),partName,4,uint32_t(i+1),
        {0,1+double(i+1)*part->height,0},{1,0,0,0}});
    const auto graph=ImportedAssembly::prepare(source,error);ASSERT_TRUE(graph)<<error;
    CannonPhysicsScene pile;CannonPhysicsScene::Packet packet;packet.revision=1;packet.solidCount=3;
    for(const auto& root:graph->roots()) {
        auto shape=root.shape;
        if(solidBox) {
            // Keep the catalog's mass, COM and inertia exactly: only replace
            // the hollow/studded collision surface with its solid body box.
            const geometry::UnionBox box{{{-25,-60,-25},{25,0,25}},1};
            geometry::BoxUnionIssue unionIssue;
            const auto volume=geometry::BoxUnion::compile(std::span(&box,1),unionIssue);ASSERT_TRUE(volume);
            physics::AuthoredShapeIssue shapeIssue;
            const auto plain=physics::AuthoredShape::prepare(*volume,root.mass,shapeIssue);ASSERT_TRUE(plain);
            shape=*plain;
        }
        packet.partitions.push_back({.origin=root.origin,.shape=std::move(shape),.dynamic=true});
    }
    ASSERT_TRUE(pile.prepareAuthored(std::move(packet),error))<<error;
    const AdventureSpatialQueries::Solid ground{{},{},{-10,0,-10},{10,1,10}};
    ASSERT_TRUE(floor.prepare(std::span(&ground,1),{},1,error))<<error;
    for(int i=0;i<300&&(!pile.ready(1)||!floor.ready(1));++i) {
        ASSERT_NE(pile.update(world,error),CannonPhysicsScene::Progress::Failed)<<error;
        ASSERT_NE(floor.update(world,error),CannonPhysicsScene::Progress::Failed)<<error;
        ASSERT_TRUE(wait([&]{const auto* pool=world.authoredShapeResources();return pool&&pool->stats().phase==physics::ShapeResourcePhase::Ready;}));
        ASSERT_TRUE(submit(pile.needsQuiescentBoundary()||floor.needsQuiescentBoundary()?0:1));
    }
    ASSERT_TRUE(pile.ready(1));ASSERT_TRUE(floor.ready(1));
    size_t slowRun=0,longestSlowRun=0,fastTicks=0;float maximumLinear=0,maximumAngular=0;
    std::array<glm::dvec3,3> previousPositions{};std::array<glm::dquat,3> previousRotations{};
    bool slept=false;std::ostringstream trace;trace<<"tick,maxLinear,maxAngular,awake,slowRun,poseLinear,poseAngular\n";
    for(unsigned tick=0;tick<300&&!slept;++tick) {
        world.requestDebugSnapshot({1,32});ASSERT_TRUE(submit(1));const auto expected=world.encodedTick();
        std::optional<physics::DebugSnapshot> captured;
        ASSERT_TRUE(wait([&]{auto read=world.pollDebugSnapshot();if(!read||read->tick<expected)return false;captured=std::move(read);return true;}));
        float linear=0,angular=0;double poseLinear=0,poseAngular=0;size_t count=0,awake=0;
        for(const auto& body:captured->bodies) {
            size_t member=3;for(size_t i=0;i<3;++i)if(body.handle==pile.body(i))member=i;
            if(member==3)continue;
            ASSERT_TRUE(body.alive);++count;awake+=body.awake?1u:0u;
            linear=std::max(linear,glm::length(body.linearVelocity));angular=std::max(angular,glm::length(body.angularVelocity));
            const auto position=physics::worldPositionToAbsolute({body.sector,body.position});
            const auto rotation=glm::normalize(glm::dquat(body.orientation));
            if(tick) {
                poseLinear=std::max(poseLinear,60*glm::length(position-previousPositions[member]));
                const auto delta=rotation*glm::inverse(previousRotations[member]);
                poseAngular=std::max(poseAngular,120*std::atan2(glm::length(glm::dvec3(delta.x,delta.y,delta.z)),std::abs(delta.w)));
            }
            previousPositions[member]=position;previousRotations[member]=rotation;
        }
        ASSERT_EQ(count,3u);
        maximumLinear=std::max(maximumLinear,linear);maximumAngular=std::max(maximumAngular,angular);
        if(linear<=.05f&&angular<=.05f)++slowRun;else{slowRun=0;++fastTicks;}
        longestSlowRun=std::max(longestSlowRun,slowRun);slept=awake==0;
        trace<<expected<<','<<linear<<','<<angular<<','<<awake<<','<<slowRun<<','<<poseLinear<<','<<poseAngular<<'\n';
    }
    if(!slept)RecordProperty("activityTrace",trace.str());
    RecordProperty("longestSlowRun",int(longestSlowRun));
    RecordProperty("fastTicks",int(fastTicks));RecordProperty("maxLinear",maximumLinear);RecordProperty("maxAngular",maximumAngular);
    EXPECT_TRUE(slept)<<"part="<<GetParam()<<" longest all-body slow run="<<longestSlowRun
        <<" fast ticks="<<fastTicks<<" peak linear="<<maximumLinear<<" angular="<<maximumAngular;
    world.shutdown();pile.abandonAfterWorldShutdown();
}
INSTANTIATE_TEST_SUITE_P(Activity,RawImportedStackGpu,testing::Values("3005.dat","solid-3005.dat"));
} // namespace
} // namespace voxy::game::adventure
#endif
