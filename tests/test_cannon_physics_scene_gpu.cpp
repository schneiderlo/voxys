#include "game/adventure/cannon_physics_scene.hpp"
#include "gpu/context.hpp"
#include "game/adventure/builder_cannon.hpp"
#include "game/adventure/brick_thrower.hpp"
#include "game/adventure/player_physics_proxy.hpp"

#include <gtest/gtest.h>
#include <chrono>
#include <thread>

#ifndef VOXY_WASM
struct WGPUWrappedSubmissionIndex;
extern "C" WGPUBool wgpuDevicePoll(WGPUDevice,WGPUBool,const WGPUWrappedSubmissionIndex*);

namespace voxy::game::adventure {
namespace {
class CannonPhysicsSceneGpu : public testing::Test {
protected:
    gpu::Context context;
    physics::PhysicsWorld world;
    CannonPhysicsScene scene;
    std::string error;
    void SetUp() override { if(!context.initHeadless())GTEST_SKIP()<<"No headless WebGPU adapter"; }
    void TearDown() override {
        if(auto* resources=world.authoredShapeResources();resources && resources->stats().cpu.references==0) {
            resources->close();
            EXPECT_TRUE(wait([&]{resources->poll();return resources->stats().phase==physics::ShapeResourcePhase::Closed;}));
        }
        world.shutdown();scene.abandonAfterWorldShutdown();
    }
    bool initialize(uint32_t bodies=16,glm::vec3 gravity={0,0,0},uint32_t substeps=4) {
        physics::PhysicsInitContext init;init.requestedBackend=physics::BackendType::WebGpuSoft;
        init.device=context.getDevice();init.queue=context.getQueue();
        init.maxBodies=bodies;init.maxActiveBodies=bodies;init.maxPairs=bodies>16?8192:64;
        init.maxContacts=bodies>16?4096:32;init.maxManifolds=init.maxPairs;init.gpu.gravity=gravity;init.gpu.substeps=substeps;
        init.gpu.commandCapacity=512;init.gpu.attachmentCapacity=8;
        init.gpu.attachmentCommandCapacity=16;init.gpu.asyncQueryCapacity=4;
        init.gpu.debugReadbackBodyCapacity=bodies;
        return world.initialize(init);
    }
    template<class Predicate> bool wait(Predicate predicate) {
        const auto limit=std::chrono::steady_clock::now()+std::chrono::seconds(20);
        do {
            static_cast<void>(wgpuDevicePoll(context.getDevice(),false,nullptr));
            world.update(0);
            if(predicate())return true;
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        } while(std::chrono::steady_clock::now()<limit);
        return false;
    }
    bool bootstrap() {
        if(!wait([&] {
            static_cast<void>(scene.update(world,error));
            const auto* r=world.authoredShapeResources();
            return r && r->stats().phase==physics::ShapeResourcePhase::Ready;
        }))return false;
        physics::ShapeResourceError status;
        const auto ticket=world.prepareGpuSubmission(status);
        return ticket.valid() && world.discardGpuSubmission(ticket)==physics::ShapeResourceError::None;
    }
    bool execute(uint32_t ticks=1) {
        if(!world.scheduleFixedTicks(ticks))return false;
        physics::ShapeResourceError status;
        const auto ticket=world.prepareGpuSubmission(status);if(!ticket.valid())return false;
        WGPUCommandEncoderDescriptor ed{};
        const auto encoder=wgpuDeviceCreateCommandEncoder(context.getDevice(),&ed);
        if(!encoder) {static_cast<void>(world.discardGpuSubmission(ticket));return false;}
        const auto encoded=world.encodeGpuStepChecked(encoder);
        WGPUCommandBufferDescriptor cd{};
        const auto command=encoded.succeeded()?wgpuCommandEncoderFinish(encoder,&cd):nullptr;
        wgpuCommandEncoderRelease(encoder);
        if(!command){static_cast<void>(world.discardGpuSubmission(ticket));return false;}
        const auto submitted=world.submitGpuSubmission(ticket,std::span(&command,1));
        wgpuCommandBufferRelease(command);
        if(submitted!=physics::ShapeResourceError::None)return false;
        const auto tick=world.tickFrontier().submitted;
        return wait([&]{return world.tickFrontier().completed==tick;});
    }
};
TEST_F(CannonPhysicsSceneGpu, ThrownBricksExecuteOnGpuFallCollideAndRetire) {
    ASSERT_TRUE(initialize(256,{0,-9.81f,0}));ASSERT_TRUE(bootstrap());
    physics::BodySpawnDesc ground;
    ground.shape=physics::ThrowableShape::Box;ground.position={0,-.5f,0};
    ground.dimensions={200,1,200};ground.inverseMass=0;
    const auto floor=world.spawnBody(ground);ASSERT_TRUE(floor.valid());
    BrickThrower thrower;
    ASSERT_EQ(thrower.launch(world,{0,4,0},0,.32,1),1u);
    ASSERT_EQ(thrower.launch(world,{0,4,-5},0,.32,100),100u);
    ASSERT_EQ(thrower.live(),101u);EXPECT_EQ(thrower.thrown,101u);
    ASSERT_EQ(thrower.bodyIds().size(),101u);
    EXPECT_EQ(thrower.bodyIds().front(),thrower.bricks.front().body.index);
    EXPECT_EQ(thrower.bodyIds().back(),thrower.bricks[100].body.index);
    world.requestDebugSnapshot({1,256});ASSERT_TRUE(execute());
    std::optional<physics::DebugSnapshot> initial;
    ASSERT_TRUE(wait([&]{initial=world.pollDebugSnapshot();return initial.has_value();}));
    const auto first=thrower.bricks.front().body;
    const auto findFirst=[&](const auto& snapshot) {
        return std::find_if(snapshot.bodies.begin(),snapshot.bodies.end(),[&](const auto& body){return body.handle==first;});
    };
    const auto start=findFirst(*initial);ASSERT_NE(start,initial->bodies.end());
    EXPECT_TRUE(start->alive);EXPECT_GT(start->linearVelocity.y,0);
    // Observe after landing, before the fastest bricks can leave the finite floor.
    for(int i=0;i<36;++i)ASSERT_TRUE(execute(4));
    world.requestDebugSnapshot({2,256});ASSERT_TRUE(execute());
    std::optional<physics::DebugSnapshot> settled;
    ASSERT_TRUE(wait([&]{settled=world.pollDebugSnapshot();return settled.has_value();}));
    const auto end=findFirst(*settled);ASSERT_NE(end,settled->bodies.end());
    EXPECT_LT(end->position.y,start->position.y);EXPECT_LT(end->position.z,start->position.z-5);
    size_t live=0;
    for(const auto& body:settled->bodies)if(body.alive&&body.handle!=floor) {
        ++live;EXPECT_TRUE(std::isfinite(body.position.y));
        EXPECT_GT(body.position.y,.2f); // Supported by the GPU floor, not free-falling through it.
    }
    EXPECT_EQ(live,101u);
    thrower.retire(world,true);EXPECT_EQ(thrower.live(),0u);
    EXPECT_TRUE(thrower.bodyIds().empty());
    ASSERT_TRUE(execute());EXPECT_EQ(world.stats().residentBodies,1u);
    ASSERT_EQ(thrower.launch(world,{0,4,0},0,.32,100),100u); // Freed slots can be reused.
    ASSERT_TRUE(execute());thrower.retire(world,true);ASSERT_TRUE(world.destroyBody(floor));ASSERT_TRUE(execute());
}
TEST_F(CannonPhysicsSceneGpu, ThrownBrickFlightBounceAndStackMatchJolt) {
    ASSERT_TRUE(initialize(64,{0,-9.81f,0},16));ASSERT_TRUE(bootstrap());
    physics::PhysicsWorld jolt;
    physics::PhysicsInitContext reference;reference.requestedBackend=physics::BackendType::JoltLegacy;
    reference.maxBodies=64;reference.maxActiveBodies=64;reference.maxPairs=256;
    reference.maxContacts=128;reference.maxManifolds=256;
    reference.joltJobSystem=physics::JoltJobSystemMode::SingleThreaded;
    ASSERT_TRUE(jolt.initialize(reference));
    const std::vector<uint16_t> terrain(128*128,32768);
    ASSERT_TRUE(world.setTerrain(terrain,128,128,10,1));
    ASSERT_TRUE(jolt.setTerrain(terrain,128,128,10,1));
    auto flying=BrickThrower::projectile({0,30,30},0,.32,0,1);
    auto falling=flying;falling.position={-10,4,0};falling.linearVelocity={};falling.angularVelocity={};
    const auto spawn=[&](physics::BodySpawnDesc desc) {
        const auto handle=world.spawnBody(desc);EXPECT_TRUE(handle.valid());
        // The reference uses the enclosing box. Flight is shape independent;
        // upright drop/stack contacts have the same bottom and top heights.
        desc.material=physics::PhysicsMaterial{.friction=.65f,.restitution=.25f,.rollingResistance=0};
        EXPECT_TRUE(jolt.spawnBody(desc).valid());return handle;
    };
    const auto flight=spawn(flying),drop=spawn(falling);
    std::array<physics::BodyHandle,4> stack;
    for(size_t i=0;i<stack.size();++i) {
        auto brick=falling;brick.position={10,.58f+1.14f*float(i),0};stack[i]=spawn(brick);
    }
    float maximumFlightError=0,maximumDropError=0,maximumFlightRotationError=0;
    float reboundGpu=0,reboundJolt=0;
    std::optional<physics::DebugSnapshot> snapshot;
    for(uint32_t tick=1;tick<=360;++tick) {
        jolt.update(1.f/60);
        world.requestDebugSnapshot({1,64});ASSERT_TRUE(execute());
        snapshot.reset();ASSERT_TRUE(wait([&]{snapshot=world.pollDebugSnapshot();return snapshot.has_value();}));
        const auto at=[&](physics::BodyHandle handle)->const physics::DebugBodyState& {
            const auto& body=snapshot->bodies.at(handle.index-1);
            EXPECT_EQ(body.handle,handle);return body;
        };
        const auto expected=jolt.dynamicBodies();ASSERT_EQ(expected.size(),6u);
        if(std::getenv("VOXY_TRACE_BRICKS")&&(tick<=4||tick==10||tick==30||tick==60))for(const auto handle:stack) {
            const auto& b=at(handle);
            std::cerr<<"stack "<<tick<<" "<<handle.index<<" p "<<b.position.x<<","<<b.position.y<<","<<b.position.z
                <<" v "<<b.linearVelocity.x<<","<<b.linearVelocity.y<<","<<b.linearVelocity.z
                <<" w "<<b.angularVelocity.x<<","<<b.angularVelocity.y<<","<<b.angularVelocity.z<<"\n";
        }
        if(tick<=60) {
            maximumFlightError=std::max(maximumFlightError,glm::length(at(flight).position-expected[0].position));
            maximumFlightRotationError=std::max(maximumFlightRotationError,
                2.f*std::acos(std::clamp(std::abs(glm::dot(at(flight).orientation,expected[0].rotation)),0.f,1.f)));
        }
        if(tick<=120)maximumDropError=std::max(maximumDropError,std::abs(at(drop).position.y-expected[1].position.y));
        if(tick>=53&&tick<=100) {
            reboundGpu=std::max(reboundGpu,at(drop).position.y);
            reboundJolt=std::max(reboundJolt,expected[1].position.y);
        }
        if(tick==360)for(size_t i=0;i<stack.size();++i) {
            EXPECT_NEAR(at(stack[i]).position.y,expected[i+2].position.y,.10f);
            EXPECT_LT(glm::length(at(stack[i]).linearVelocity),.10f);
            EXPECT_LT(glm::length(at(stack[i]).angularVelocity),.10f);
        }
    }
    RecordProperty("maximumFlightError",maximumFlightError);
    RecordProperty("maximumFlightRotationError",maximumFlightRotationError);
    RecordProperty("maximumDropError",maximumDropError);
    RecordProperty("reboundGpu",reboundGpu);RecordProperty("reboundJolt",reboundJolt);
    EXPECT_LT(maximumFlightError,.05f);
    EXPECT_LT(maximumFlightRotationError,.05f);
    EXPECT_LT(maximumDropError,.15f);
    EXPECT_NEAR(reboundGpu,reboundJolt,.10f);
}
TEST_F(CannonPhysicsSceneGpu, FastBrickReachesItsSurfaceAndRetainsBounceMomentum) {
    ASSERT_TRUE(initialize(16,{0,-9.81f,0},16));ASSERT_TRUE(bootstrap());
    const std::vector<uint16_t> terrain(64*64,32768);
    ASSERT_TRUE(world.setTerrain(terrain,64,64,10,1));
    auto desc=BrickThrower::projectile({0,0,0},0,0,0,1);
    desc.position={0,5,0};desc.linearVelocity={0,-90,0};desc.angularVelocity={};
    const auto body=world.spawnBody(desc);ASSERT_TRUE(body.valid());
    float lowest=5,upward=0;
    for(int tick=0;tick<10;++tick) {
        world.requestDebugSnapshot({body.index,1});ASSERT_TRUE(execute());
        std::optional<physics::DebugSnapshot> snapshot;
        ASSERT_TRUE(wait([&]{snapshot=world.pollDebugSnapshot();return snapshot.has_value();}));
        ASSERT_EQ(snapshot->bodies.size(),1u);
        lowest=std::min(lowest,snapshot->bodies[0].position.y);
        upward=std::max(upward,snapshot->bodies[0].linearVelocity.y);
    }
    EXPECT_GT(lowest,.50f);EXPECT_LT(lowest,.65f);
    EXPECT_GT(upward,18.f);EXPECT_LT(upward,26.f);
}
TEST_F(CannonPhysicsSceneGpu, BricksBounceOffPlayerAndWalkingPushesBricks) {
    ASSERT_TRUE(initialize(16,{0,0,0},16));ASSERT_TRUE(world.setEventReadbackEnabled(true));ASSERT_TRUE(bootstrap());
    PlayerPhysicsProxy player;BrickThrower thrower;
    player.sync(world,{0,0,0},1.12,4.76);
    ASSERT_TRUE(player.body.valid());
    auto desc=BrickThrower::projectile({0,0,0},0,0,0,1);
    desc.position={0,2.38f,6};desc.linearVelocity={0,0,-15};desc.angularVelocity={};
    const auto brick=world.spawnBody(desc);ASSERT_TRUE(brick.valid());
    thrower.bricks[0]={brick,1800};
    glm::dvec3 impulse{};
    for(int tick=0;tick<40;++tick) {
        player.sync(world,{0,0,0},1.12,4.76);ASSERT_TRUE(execute());
        while(auto batch=world.pollEvents()) {
            ASSERT_FALSE(batch->overflow);
            for(const auto& event:batch->events)impulse+=player.contactImpulse(event,thrower);
        }
    }
    world.requestDebugSnapshot({1,16});ASSERT_TRUE(execute());
    std::optional<physics::DebugSnapshot> bounce;
    ASSERT_TRUE(wait([&]{bounce=world.pollDebugSnapshot();return bounce.has_value();}));
    const auto find=[&](const auto& snapshot,physics::BodyHandle handle) {
        return std::find_if(snapshot.bodies.begin(),snapshot.bodies.end(),[&](const auto& b){return b.handle==handle;});
    };
    const auto after=find(*bounce,brick);ASSERT_NE(after,bounce->bodies.end());
    EXPECT_GT(after->position.z,1.5f);EXPECT_GT(after->linearVelocity.z,0);
    EXPECT_LT(impulse.z,-.1);EXPECT_GT(player.contacts,0u);
    EXPECT_TRUE(find(*bounce,player.body)->kinematic);
    ASSERT_TRUE(world.destroyBody(brick));ASSERT_TRUE(execute());
    desc.position={0,2.38f,2};desc.linearVelocity={};
    const auto parked=world.spawnBody(desc);ASSERT_TRUE(parked.valid());
    for(int tick=1;tick<=60;++tick) {
        player.sync(world,{0,0,double(tick)*.05},1.12,4.76);ASSERT_TRUE(execute());
        while(world.pollEvents()){}
    }
    world.requestDebugSnapshot({1,16});ASSERT_TRUE(execute());
    std::optional<physics::DebugSnapshot> pushed;
    ASSERT_TRUE(wait([&]{pushed=world.pollDebugSnapshot();return pushed.has_value();}));
    const auto moved=find(*pushed,parked);ASSERT_NE(moved,pushed->bodies.end());
    EXPECT_GT(moved->position.z,4.f);
    // Recovery is not a 500-unit/s player strike.
    player.sync(world,{40,0,40},1.12,4.76);
    world.requestDebugSnapshot({1,16});ASSERT_TRUE(execute());
    std::optional<physics::DebugSnapshot> teleported;
    ASSERT_TRUE(wait([&]{teleported=world.pollDebugSnapshot();return teleported.has_value();}));
    EXPECT_LT(glm::length(find(*teleported,player.body)->linearVelocity),1e-5f);
    player.clear(world);ASSERT_FALSE(player.body.valid());ASSERT_TRUE(execute());
}
TEST_F(CannonPhysicsSceneGpu, ReadinessRequiresExecutedAdmissionAndReplacementRetiresShapes) {
    ASSERT_TRUE(initialize());
    AdventureSpatialQueries::Solid solid{{},{},{0,0,0},{1,2,1}};
    ASSERT_TRUE(scene.prepare(std::span(&solid,1),{},1,error))<<error;
    ASSERT_TRUE(bootstrap())<<error;
    ASSERT_TRUE(wait([&]{static_cast<void>(scene.update(world,error));return scene.waitingForExecution();}))<<error;
    EXPECT_FALSE(scene.ready(1));EXPECT_EQ(scene.bodyCount(),0u);
    ASSERT_TRUE(execute());
    EXPECT_EQ(scene.update(world,error),CannonPhysicsScene::Progress::Ready)<<error;
    EXPECT_TRUE(scene.ready(1));EXPECT_EQ(scene.bodyCount(),1u);
    solid.minimum.x+=10;solid.maximum.x+=10;
    ASSERT_TRUE(scene.prepare(std::span(&solid,1),{},2,error))<<error;
    EXPECT_FALSE(scene.ready(2));
    ASSERT_TRUE(wait([&]{static_cast<void>(scene.update(world,error));return scene.waitingForExecution();}))<<error;
    EXPECT_EQ(scene.acceptedRevision(),1u);EXPECT_FALSE(scene.ready(2));
    ASSERT_TRUE(execute());
    EXPECT_EQ(scene.update(world,error),CannonPhysicsScene::Progress::Ready)<<error;
    EXPECT_TRUE(scene.ready(2));EXPECT_EQ(scene.bodyCount(),1u);
    // The GPU query path sees the replacement and the old position is clear.
    const std::array<physics::PhysicsQueryRequest,2> rays{{
        {.requestId=1,.origin={.5f,1,-2},.direction={0,0,1},.maximumDistance=4},
        {.requestId=2,.origin={10.5f,1,-2},.direction={0,0,1},.maximumDistance=4}}};
    ASSERT_TRUE(world.submitQueries(rays));ASSERT_TRUE(execute());
    std::optional<physics::PhysicsQueryBatch> hits;
    ASSERT_TRUE(wait([&]{hits=world.pollQueryResults();return hits.has_value();}));
    ASSERT_EQ(hits->outputs.size(),2u);
    EXPECT_FALSE(hits->outputs[0].overflow);EXPECT_TRUE(hits->outputs[0].hits.empty());
    EXPECT_FALSE(hits->outputs[1].overflow);ASSERT_EQ(hits->outputs[1].hits.size(),1u);
    EXPECT_NEAR(hits->outputs[1].hits[0].distance,2,1e-4);
    ASSERT_TRUE(wait([&]{world.authoredShapeResources()->poll();return world.authoredShapeResources()->stats().cpu.resident==1;}));
    EXPECT_FALSE(scene.prepare({}, {},1,error));EXPECT_TRUE(scene.ready(2));
    scene.requestClear();EXPECT_FALSE(scene.ready(2));
    EXPECT_EQ(scene.update(world,error),CannonPhysicsScene::Progress::Waiting)<<error;
    ASSERT_TRUE(execute());
    EXPECT_EQ(scene.update(world,error),CannonPhysicsScene::Progress::Ready)<<error;
    EXPECT_TRUE(scene.empty());
    ASSERT_TRUE(wait([&]{world.authoredShapeResources()->poll();return world.authoredShapeResources()->stats().cpu.resident==0;}));
    EXPECT_EQ(world.authoredShapeResources()->stats().cpu.references,0u);
}
TEST_F(CannonPhysicsSceneGpu, FullBodyPoolRollsBackEveryUnexecutedStaticSpawn) {
    ASSERT_TRUE(initialize(2));
    // Wide separations deliberately require four bounded partitions. A two-body
    // pool must not admit the first two and silently leave the other walls open.
    const std::array<AdventureSpatialQueries::Solid,4> solids{{
        {{},{},{0,0,0},{1,1,1}},{{},{},{200,0,0},{201,1,1}},
        {{},{},{400,0,0},{401,1,1}},{{},{},{600,0,0},{601,1,1}}}};
    ASSERT_TRUE(scene.prepare(solids,{},1,error))<<error;
    ASSERT_TRUE(bootstrap())<<error;
    ASSERT_TRUE(wait([&]{return scene.update(world,error)==CannonPhysicsScene::Progress::Failed;}));
    EXPECT_FALSE(scene.ready(1));EXPECT_FALSE(scene.waitingForExecution());
    EXPECT_FALSE(scene.needsQuiescentBoundary()); // Refusal must not freeze unrelated physics.
    EXPECT_EQ(scene.bodyCount(),0u);EXPECT_EQ(world.stats().residentBodies,0u);
    scene.requestClear();
    EXPECT_EQ(scene.update(world,error),CannonPhysicsScene::Progress::Ready)<<error;
    EXPECT_TRUE(scene.empty());
    ASSERT_TRUE(wait([&]{world.authoredShapeResources()->poll();return world.authoredShapeResources()->stats().cpu.resident==0;}));
}
TEST_F(CannonPhysicsSceneGpu, JoinedStaticSectionReplacementFallsUnderGravityAndClearsOldOpening) {
    ASSERT_TRUE(initialize(16,{0,-9.81f,0}));
    const std::array<AdventureSpatialQueries::Solid,2> source{{
        {{},{},{0,4,0},{1,5,1}},{{},{},{0,5.2,0},{1,6.2,1}}}};
    ASSERT_TRUE(scene.prepare(source,{},1,error));ASSERT_TRUE(bootstrap());
    ASSERT_TRUE(wait([&]{static_cast<void>(scene.update(world,error));return scene.waitingForExecution();}));
    ASSERT_TRUE(execute());ASSERT_EQ(scene.update(world,error),CannonPhysicsScene::Progress::Ready);
    const auto old=scene.body(0);ASSERT_TRUE(old.valid());
    CannonPhysicsScene::Packet released;released.revision=2;released.solidCount=2;
    for(const auto& solid:source) {
        auto one=CannonPhysicsScene::compile(std::span(&solid,1),{},2,error);ASSERT_TRUE(one)<<error;
        ASSERT_EQ(one->partitions.size(),1u);one->partitions[0].dynamic=true;
        released.partitions.push_back(std::move(one->partitions[0]));
    }
    ASSERT_TRUE(scene.prepareAuthored(std::move(released),error));
    ASSERT_TRUE(wait([&]{static_cast<void>(scene.update(world,error));return scene.waitingForExecution();}));
    EXPECT_EQ(scene.acceptedRevision(),1u);EXPECT_EQ(scene.body(0),old);
    ASSERT_TRUE(execute());ASSERT_EQ(scene.update(world,error),CannonPhysicsScene::Progress::Ready);
    ASSERT_EQ(scene.bodyCount(),2u);EXPECT_NE(scene.body(0),old);
    for(int i=0;i<8;++i)ASSERT_TRUE(execute(8));
    world.requestDebugSnapshot({1,16});ASSERT_TRUE(execute());
    std::optional<physics::DebugSnapshot> snapshot;
    ASSERT_TRUE(wait([&]{snapshot=world.pollDebugSnapshot();return snapshot.has_value();}));
    EXPECT_EQ(snapshot->confirmedIncarnation,world.tickFrontier().incarnation);
    for(size_t i=0;i<2;++i) {
        const auto found=std::find_if(snapshot->bodies.begin(),snapshot->bodies.end(),[&](const auto& b){return b.handle==scene.body(i);});
        ASSERT_NE(found,snapshot->bodies.end());EXPECT_TRUE(found->alive);
        EXPECT_LT(found->position.y,2.f);EXPECT_LT(found->linearVelocity.y,-5.f);
    }
    const physics::PhysicsQueryRequest ray{.requestId=11,.origin={.5f,4.5f,-2},.direction={0,0,1},.maximumDistance=4};
    ASSERT_TRUE(world.submitQueries(std::span(&ray,1)));ASSERT_TRUE(execute());
    std::optional<physics::PhysicsQueryBatch> hits;
    ASSERT_TRUE(wait([&]{hits=world.pollQueryResults();return hits.has_value();}));
    ASSERT_EQ(hits->outputs.size(),1u);EXPECT_FALSE(hits->outputs[0].overflow);EXPECT_TRUE(hits->outputs[0].hits.empty());
    scene.requestClear();static_cast<void>(scene.update(world,error));ASSERT_TRUE(execute());
    ASSERT_EQ(scene.update(world,error),CannonPhysicsScene::Progress::Ready);EXPECT_TRUE(scene.empty());
}
TEST_F(CannonPhysicsSceneGpu, ReplacementTransfersRootMotionAndConsumesProjectileAtSameTick) {
    ASSERT_TRUE(initialize());
    const AdventureSpatialQueries::Solid solid{{},{},{0,4,0},{2,5,1}};
    ASSERT_TRUE(scene.prepare(std::span(&solid,1),{},1,error));ASSERT_TRUE(bootstrap());
    ASSERT_TRUE(wait([&]{static_cast<void>(scene.update(world,error));return scene.waitingForExecution();}));
    ASSERT_TRUE(execute());ASSERT_EQ(scene.update(world,error),CannonPhysicsScene::Progress::Ready);
    const auto old=scene.body(0);
    physics::BodySpawnDesc desc;desc.position={100,100,100};
    const auto projectile=world.spawnBody(desc);ASSERT_TRUE(projectile.valid());ASSERT_TRUE(execute());
    auto packet=CannonPhysicsScene::compile(std::span(&solid,1),{},2,error);ASSERT_TRUE(packet);
    auto& partition=packet->partitions[0];partition.dynamic=true;
    partition.orientation=glm::angleAxis(.4f,glm::vec3(0,1,0));
    partition.originVelocity={3,0,1};partition.angularVelocity={0,1,0};packet->consumeBody=projectile;
    physics::AuthoredFrameError issue;
    const auto expected=physics::AuthoredBodyFrame(partition.shape).bodyMotion({
        .position=physics::worldPositionFromAbsolute(partition.origin),.orientation=partition.orientation,
        .originVelocity=partition.originVelocity,.angularVelocity=partition.angularVelocity},issue);
    ASSERT_TRUE(expected);
    ASSERT_TRUE(scene.prepareAuthored(std::move(*packet),error))<<error;
    ASSERT_TRUE(wait([&]{static_cast<void>(scene.update(world,error));return scene.waitingForExecution();}))<<error;
    EXPECT_EQ(scene.body(0),old);EXPECT_EQ(world.stats().residentBodies,3u); // Includes the reserved replacement.
    world.requestDebugSnapshot({1,16});ASSERT_TRUE(execute());
    ASSERT_EQ(scene.update(world,error),CannonPhysicsScene::Progress::Ready)<<error;
    std::optional<physics::DebugSnapshot> snapshot;
    ASSERT_TRUE(wait([&]{snapshot=world.pollDebugSnapshot();return snapshot.has_value();}));
    EXPECT_EQ(world.stats().residentBodies,1u);bool found=false;
    for(const auto& body:snapshot->bodies)if(body.alive) {
        EXPECT_NE(body.handle,old);EXPECT_NE(body.handle,projectile);
        if(body.handle==scene.body(0)) {
            found=true;EXPECT_LT(glm::length(body.linearVelocity-expected->centerVelocity),.05f);
            EXPECT_LT(glm::length(body.angularVelocity-expected->angularVelocity),.05f);
        }
    }
    EXPECT_TRUE(found);
    // Clear before a staged replacement is admitted also owns its projectile.
    const auto second=world.spawnBody(desc);ASSERT_TRUE(second.valid());ASSERT_TRUE(execute());
    auto cancelled=CannonPhysicsScene::compile(std::span(&solid,1),{},3,error);ASSERT_TRUE(cancelled);
    cancelled->consumeBody=second;ASSERT_TRUE(scene.prepareAuthored(std::move(*cancelled),error));
    scene.requestClear();static_cast<void>(scene.update(world,error));ASSERT_TRUE(execute());
    ASSERT_EQ(scene.update(world,error),CannonPhysicsScene::Progress::Ready)<<error;
    EXPECT_TRUE(scene.empty());EXPECT_EQ(world.stats().residentBodies,0u);
}
TEST_F(CannonPhysicsSceneGpu, CannonCooldownPauseExpiryAndBusyClearKeepExactBodyOwnership) {
    ASSERT_TRUE(initialize());ASSERT_TRUE(scene.prepare({}, {},1,error));
    ASSERT_TRUE(bootstrap());
    ASSERT_TRUE(wait([&]{return scene.update(world,error)==CannonPhysicsScene::Progress::Ready;}))<<error;
    BuilderCannon cannon;
    const glm::dvec3 feet(1208,180,-1080);
    ASSERT_TRUE(cannon.fire(world,feet));EXPECT_EQ(cannon.liveShots(),1u);
    EXPECT_FALSE(cannon.fire(world,feet));EXPECT_EQ(cannon.shotsFired,1u);
    EXPECT_EQ(cannon.shots[0].expires,150u);
    for(int i=0;i<7;++i)ASSERT_TRUE(execute(8));
    ASSERT_TRUE(execute(3));EXPECT_EQ(world.encodedTick(),59u);
    EXPECT_FALSE(cannon.fire(world,feet));ASSERT_TRUE(execute());
    ASSERT_TRUE(cannon.fire(world,feet));EXPECT_EQ(cannon.liveShots(),2u);
    const auto pausedTick=world.encodedTick();
    for(int i=0;i<10;++i){world.update(0);cannon.retire(world);}
    EXPECT_EQ(world.encodedTick(),pausedTick);EXPECT_EQ(cannon.liveShots(),2u);
    for(int i=0;i<11;++i)ASSERT_TRUE(execute(8));
    ASSERT_TRUE(execute(2));EXPECT_EQ(world.encodedTick(),150u);
    cannon.retire(world);EXPECT_EQ(cannon.liveShots(),1u);
    ASSERT_TRUE(execute());EXPECT_EQ(world.stats().residentBodies,1u);
    // A submission temporarily forbids mutation. clear must retain ownership
    // for a later retry instead of forgetting the still-live projectile.
    physics::ShapeResourceError status;const auto ticket=world.prepareGpuSubmission(status);
    ASSERT_TRUE(ticket.valid());EXPECT_FALSE(cannon.clear(world));EXPECT_EQ(cannon.liveShots(),1u);
    ASSERT_EQ(world.discardGpuSubmission(ticket),physics::ShapeResourceError::None);
    EXPECT_TRUE(cannon.clear(world));EXPECT_EQ(cannon.liveShots(),0u);
    ASSERT_TRUE(execute());EXPECT_EQ(world.stats().residentBodies,0u);
    EXPECT_EQ(cannon.nextShotTick,0u);EXPECT_DOUBLE_EQ(cannon.recoil,0);
    scene.requestClear();EXPECT_EQ(scene.update(world,error),CannonPhysicsScene::Progress::Ready);
    EXPECT_TRUE(scene.empty());
}
} // namespace
} // namespace voxy::game::adventure
#endif
