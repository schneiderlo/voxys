#include "game/lego_playground.hpp"
#include "gpu/context.hpp"
#include "physics/physics_world.hpp"
#include <gtest/gtest.h>
#include <chrono>
#include <limits>
#include <thread>
#ifndef WGPUWrappedSubmissionIndex
struct WGPUWrappedSubmissionIndex;
#endif
extern "C" WGPUBool wgpuDevicePoll(WGPUDevice, WGPUBool,
                                   const WGPUWrappedSubmissionIndex *);
using voxy::game::LegoPlayground;
using namespace voxy;
namespace {
class LegoPlaygroundGpu : public ::testing::Test {
protected:
  gpu::Context gpu;
  physics::PhysicsWorld world;
  LegoPlayground p;
  bool waitForGpu() {
    // wgpu-native 22's blocking poll ignores a 60-second fence timeout and
    // retires command buffers that can still be executing. Cold LLVMpipe
    // compilation can cross that limit. Poll the actual completion fence;
    // retain the surrounding CTest timeout as the hang guard.
    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::seconds(110);
    while (!wgpuDevicePoll(gpu.getDevice(), false, nullptr)) {
      if (std::chrono::steady_clock::now() >= deadline)
        return false;
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return true;
  }
  void SetUp() override {
    gpu::ContextConfig config;
    config.enableValidation = true;
    ASSERT_TRUE(gpu.initHeadless(config)) << "LEGO validation requires a GPU";
    gpu.setErrorCallback([](WGPUErrorType, const std::string &message) {
      ADD_FAILURE() << "GPU validation: " << message;
    });
    physics::PhysicsInitContext context;
    context.requestedBackend = physics::BackendType::WebGpuSoft;
    context.device = gpu.getDevice();
    context.queue = gpu.getQueue();
    context.maxBodies = 128;
    context.maxActiveBodies = 128;
    context.maxPairs = 1024;
    context.maxContacts = 1024;
    context.maxManifolds = 1024;
    context.gpu.commandCapacity = 512;
    context.gpu.debugReadbackBodyCapacity = 128;
    context.gpu.maximumCatchUpTicks = 4;
    ASSERT_TRUE(world.initialize(context));
    ASSERT_TRUE(p.initialize(world, {0, 0, 0}));
  }
  void settle(float seconds = 3.f) {
    for (int i = 0; i < int(seconds * 60); ++i) {
      p.step(1.f / 60);
      world.update(1.f / 60);
      WGPUCommandEncoderDescriptor desc{};
      auto enc = wgpuDeviceCreateCommandEncoder(gpu.getDevice(), &desc);
      const auto report = world.encodeGpuStepChecked(enc);
      ASSERT_TRUE(report.succeeded());
      WGPUCommandBufferDescriptor cb{};
      auto cmd = wgpuCommandEncoderFinish(enc, &cb);
      wgpuQueueSubmit(gpu.getQueue(), 1, &cmd);
      wgpuCommandBufferRelease(cmd);
      wgpuCommandEncoderRelease(enc);
      ASSERT_TRUE(waitForGpu()) << "GPU submission did not complete";
    }
    p.step(1e-6f);
  }
  void tower() {
    for (int i = 0; i < 3; ++i) {
      ASSERT_TRUE(p.placeAt(0, 0)) << i;
      settle();
    }
  }
};
TEST_F(LegoPlaygroundGpu, CompoundStudsSupportRealStackAndSleep) {
  ASSERT_TRUE(p.placeAt(0, 0));
  EXPECT_FALSE(p.placeAt(0, 0));
  settle();
  EXPECT_NEAR(p.brickPosition(0).y, .57f, .03f);
  ASSERT_TRUE(p.placeAt(0, 0));
  settle();
  // The parent origin includes the studs. Ignoring them would lose .18 m.
  EXPECT_NEAR(p.brickPosition(1).y, 1.71f, .04f);
  EXPECT_EQ(p.state().sleeping, 2u);
  EXPECT_EQ(p.state().awake, 0u);
  EXPECT_GE(p.state().sleepTransitions, 2u);
}
TEST_F(LegoPlaygroundGpu, BoxFootprintSupportsEdgeAndRejectsInvalidPlacement) {
  EXPECT_FALSE(p.placeAt(8, 8));
  EXPECT_FALSE(p.placeAt(std::numeric_limits<float>::quiet_NaN(), 0));
  ASSERT_TRUE(p.placeAt(0, 0));
  settle();
  p.select(0);
  ASSERT_TRUE(p.placeAt(0, 1));
  settle();
  EXPECT_GT(p.brickPosition(1).y, 1.4f);
  EXPECT_NEAR(p.brickPosition(1).z, 1.5f, .1f);
}
TEST_F(LegoPlaygroundGpu,
       BallImpactWakesStackAndCompletesChallengeThenResetReplays) {
  tower();
  settle();
  ASSERT_EQ(p.state().phase, 1u);
  // Strike the upper brick: the target must fall through tower destruction.
  auto target = p.state().target;
  auto aim = target - glm::vec3(0, 1.1f, 0);
  ASSERT_TRUE(p.launch(aim + glm::vec3(0, 0, 8), glm::vec3(0, .07f, -1)));
  settle(5);
  EXPECT_EQ(p.state().phase, 2u)
      << "target displacement " << glm::length(p.state().target - target)
      << " target " << p.state().target.y << " bricks " << p.state().bricks
      << " awake " << p.state().awake << " impacts " << p.state().impacts;
  EXPECT_GT(p.state().impacts, 0u);
  EXPECT_GT(p.state().wakeTransitions, 0u);
  p.reset();
  EXPECT_EQ(p.state().bricks, 0u);
  EXPECT_EQ(p.state().balls, 0u);
  EXPECT_EQ(p.state().dust, 0u);
  EXPECT_EQ(p.state().phase, 0u);
  settle(.2f);
  tower();
  settle();
  EXPECT_EQ(p.state().phase, 1u);
}
TEST_F(LegoPlaygroundGpu, PoolsStayBoundedAcrossImpactsAndReset) {
  p.select(0);
  for (int z = -3; z < 3; ++z)
    for (int x = -4; x < 4; ++x)
      ASSERT_TRUE(p.placeAt(float(x) + .5f, float(z) + .5f));
  EXPECT_EQ(p.state().bricks, LegoPlayground::MaxBricks);
  EXPECT_FALSE(p.placeAt(6, 6));
  for (int i = 0; i < 16; ++i) {
    static_cast<void>(p.launch({0, 5, 10}, {0, 0, -1}));
    settle(.35f);
  }
  p.step(1000);
  EXPECT_LE(p.state().bricks, LegoPlayground::MaxBricks);
  EXPECT_LE(p.state().balls, LegoPlayground::MaxBalls);
  EXPECT_LE(p.state().dust, LegoPlayground::MaxDust);
  EXPECT_LE(p.instances().size(), 56u);
  p.reset();
  settle();
  EXPECT_EQ(p.state().awake, 0u);
  EXPECT_EQ(p.state().sleeping, 0u);
  EXPECT_EQ(p.bodyIds().size(), 0u);
  EXPECT_EQ(p.residentCount(), 1u);
}
TEST_F(LegoPlaygroundGpu, RaysDistinguishStudCapsFromGapsOnRotatedBrick) {
  physics::BodySpawnDesc brick;
  brick.shape = physics::ThrowableShape::Box;
  brick.dimensions = {1.96f, 1.14f, 3.96f};
  brick.position = {0, .57f, 0};
  brick.inverseMass = 0;
  brick.orientation = glm::angleAxis(.4f, glm::vec3(0, 1, 0));
  physics::PhysicsMaterial material;
  material.flags = physics::kLegoBrickMaterial;
  brick.material = material;
  const auto id = world.spawnBody(brick);
  ASSERT_TRUE(id.valid());
  settle(.1f);
  std::array<physics::PhysicsQueryRequest, 2> rays{};
  for (uint32_t i = 0; i < 2; ++i) {
    auto &ray = rays[i];
    ray.requestId = i;
    ray.origin =
        brick.orientation * glm::vec3(i == 0 ? .5f : 0, 5, i == 0 ? .5f : 0);
    ray.direction = {0, -1, 0};
    ray.maximumDistance = 10;
  }
  ASSERT_TRUE(world.submitQueries(rays));
  settle(.05f);
  auto hits = world.pollQueryResults();
  for (int i = 0; !hits && i < 8; ++i) {
    ASSERT_TRUE(waitForGpu()) << "GPU query did not complete";
    hits = world.pollQueryResults();
  }
  ASSERT_TRUE(hits);
  ASSERT_EQ(hits->outputs.size(), 2u);
  for (uint32_t i = 0; i < 2; ++i) {
    ASSERT_EQ(hits->outputs[i].hits.size(), 1u);
    EXPECT_EQ(hits->outputs[i].hits[0].bodyHandle(), id);
    EXPECT_NEAR(hits->outputs[i].hits[0].distance, i == 0 ? 3.86f : 4.04f,
                .002f);
  }
}

TEST_F(LegoPlaygroundGpu, RejectsUnboundedCompoundDescriptions) {
  physics::BodySpawnDesc brick;
  brick.shape = physics::ThrowableShape::Box;
  physics::PhysicsMaterial material;
  material.flags = physics::kLegoBrickMaterial;
  brick.material = material;
  brick.dimensions = {4000, 1.14f, 4000};
  EXPECT_FALSE(world.spawnBody(brick).valid());
  brick.dimensions = {3.96f, 1.14f, 3.96f};
  EXPECT_FALSE(world.spawnBody(brick).valid());
  brick.dimensions = {1.96f, 1.14f, 3.96f};
  brick.shape = physics::ThrowableShape::Sphere;
  EXPECT_FALSE(world.spawnBody(brick).valid());
}

} // namespace
