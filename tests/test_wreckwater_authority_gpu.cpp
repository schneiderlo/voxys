#include <gtest/gtest.h>

#include "gpu/context.hpp"
#include "server/wreckwater_authority_runtime.hpp"

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <optional>
#include <span>
#include <thread>

#ifndef WGPUWrappedSubmissionIndex
struct WGPUWrappedSubmissionIndex;
#endif

extern "C" WGPUBool wgpuDevicePoll(
    WGPUDevice device, WGPUBool wait,
    const WGPUWrappedSubmissionIndex* wrappedSubmissionIndex);

namespace voxy::server {
namespace {

class LifecycleTransport final
    : public network::IMultiplayerTransport {
public:
    void service() override {}

    bool send(
        uint32_t, network::DeliveryClass,
        std::span<const std::byte>) override {
        return true;
    }

    bool send(
        uint32_t, uint64_t, network::DeliveryClass,
        std::span<const std::byte>) override {
        return true;
    }

    std::optional<network::MultiplayerTransportFrame>
    poll() override {
        if (frames.empty()) return std::nullopt;
        network::MultiplayerTransportFrame result =
            std::move(frames.front());
        frames.pop_front();
        return result;
    }

    void close() override {}

    std::deque<network::MultiplayerTransportFrame> frames;
};

TEST(
    WreckwaterAuthorityGpuIntegration,
    RunsExactTicksAndClosesTowBreakOnRealWebGpuSoftWorld) {
    gpu::Context context;
    gpu::ContextConfig contextConfig;
    contextConfig.enableValidation = true;
    if (!context.initHeadless(contextConfig)) {
        GTEST_SKIP() << "Headless WebGPU is unavailable";
    }

    physics::PhysicsWorld world;
    physics::PhysicsInitContext physicsConfig;
    physicsConfig.requestedBackend =
        physics::BackendType::WebGpuSoft;
    physicsConfig.allowCpuFallback = false;
    physicsConfig.device = context.getDevice();
    physicsConfig.queue = context.getQueue();
    physicsConfig.maxBodies = 64u;
    physicsConfig.maxActiveBodies = 64u;
    physicsConfig.maxPairs =
        kWreckwaterAuthorityGpuPairCapacity;
    physicsConfig.maxCandidatePairs = 256u;
    physicsConfig.maxContacts = 256u;
    physicsConfig.maxManifolds =
        kWreckwaterAuthorityGpuPairCapacity;
    physicsConfig.enableValidation = true;
    physicsConfig.gpu.maximumCatchUpTicks = 1u;
    physicsConfig.gpu.commandCapacity = 256u;
    physicsConfig.gpu.attachmentCapacity = 16u;
    physicsConfig.gpu.attachmentCommandCapacity = 64u;
    physicsConfig.gpu.debugReadbackSlots =
        kWreckwaterAuthorityReadbackRingSlots;
    physicsConfig.gpu.debugReadbackBodyCapacity =
        kWreckwaterAuthorityPhysicalBodyCount;
    physicsConfig.gpu.eventReadbackSlots =
        kWreckwaterAuthorityReadbackRingSlots;
    physicsConfig.gpu.enableTelemetryReadback = false;
    ASSERT_TRUE(world.initialize(physicsConfig));
    ASSERT_EQ(
        world.backendType(), physics::BackendType::WebGpuSoft);

    auto transport = std::make_unique<LifecycleTransport>();
    LifecycleTransport* transportView = transport.get();
    for (uint32_t peer = 1u;
         peer <= kWreckwaterAuthorityPeerCount; ++peer) {
        transport->frames.push_back({
            .peerId = peer,
            .delivery = network::DeliveryClass::Realtime,
            .bytes = {},
            .type =
                network::MultiplayerTransportFrameType::Connected,
            .connectionSerial = 100u + peer,
        });
    }
    RealWreckwaterAuthorityPhysics authorityPhysics(context, world);
    WreckwaterAuthorityRuntime::Config runtimeConfig;
    runtimeConfig.replay.contentHash =
        0x5752'4750'5554'0001ull;
    runtimeConfig.match.warmupTicks = 1u;
    runtimeConfig.match.liveTicks = 300u;
    runtimeConfig.liveWorld.maximumInteractionDistance = 24.0f;
    runtimeConfig.liveWorld.towBreakForce = 1.0f;
    {
        WreckwaterAuthorityRuntime runtime(
            runtimeConfig, std::move(transport), authorityPhysics);
        ASSERT_TRUE(runtime.initialize())
            << wreckwaterAuthorityFailStopReasonName(
                runtime.failStopReason());
        const WreckwaterAuthorityTickResult tick = runtime.tick();
        EXPECT_TRUE(tick.authorityStarted);
        EXPECT_TRUE(tick.simulationAdvanced);
        EXPECT_FALSE(tick.failStopped);
        EXPECT_EQ(
            tick.physicsStatus,
            WreckwaterAuthorityPhysicsStepStatus::Submitted);
        EXPECT_EQ(runtime.match().currentTick(), 1u);
        EXPECT_EQ(world.encodedTick(), 1u);

        bool towQueued = false;
        bool sawTow = false;
        bool closedBreak = false;
        for (uint32_t iteration = 0u;
             iteration < 120u && !closedBreak; ++iteration) {
            std::this_thread::sleep_for(
                std::chrono::milliseconds(16));
            const WreckwaterAuthorityTickResult advanced =
                runtime.tick();
            ASSERT_FALSE(advanced.failStopped)
                << wreckwaterAuthorityFailStopReasonName(
                       runtime.failStopReason())
                << " live_apply="
                << game::wreckwaterLiveApplyFailureName(
                       runtime.liveWorld()
                           .lastApplyDiagnostic().failure);

            const uint64_t targetTick =
                runtime.match().currentTick() + 1u;
            const bool targetIsNetworkSnapshotTick =
                (targetTick - 1u)
                    % kWreckwaterAuthoritySnapshotIntervalTicks
                == 0u;
            if (!towQueued
                && runtime.match().phase() == game::MatchPhase::Live
                && runtime.latestCertifiedEvidenceTick() != 0u
                && !targetIsNetworkSnapshotTick) {
                network::WreckwaterActionRequest request;
                request.requestedApplicationTick = targetTick;
                request.clientRequestSequence = 1u;
                request.action = network::WreckwaterAction::Tow;
                request.cargoId = 1u;
                request.cargoGeneration = 1u;
                request.observedCargoRevision = 1u;
                const network::WreckwaterWriteResult payload =
                    network::WreckwaterActionRequestCodec::encode(
                        request);
                ASSERT_TRUE(payload);

                network::Packet packet;
                packet.header.payloadType =
                    network::PacketPayloadType::Command;
                packet.header.sessionId = 1u;
                packet.header.worldId = 1u;
                packet.header.worldEpoch = 1u;
                packet.header.authorityEpoch = 1u;
                packet.header.sequence = 1u;
                packet.header.tick = targetTick;
                packet.payload = payload.bytes;
                const network::PacketWriteResult encoded =
                    network::PacketCodec::encode(
                        packet,
                        network::DeliveryClass::ReliableEvent);
                ASSERT_TRUE(encoded.error.empty());
                transportView->frames.push_back({
                    .peerId = 2u,
                    .delivery =
                        network::DeliveryClass::ReliableEvent,
                    .bytes = encoded.bytes,
                    .type =
                        network::
                            MultiplayerTransportFrameType::Data,
                    .connectionSerial = 102u,
                });
                towQueued = true;
            }
            sawTow = sawTow || runtime.liveWorld().towCount() == 1u;
            const game::CargoState* cargo =
                runtime.match().cargo(1u);
            closedBreak =
                runtime.telemetry().attachmentBreakEvents != 0u
                && runtime.liveWorld().towCount() == 0u
                && cargo != nullptr
                && cargo->disposition
                    == game::CargoDisposition::Free;
        }
        EXPECT_TRUE(towQueued);
        EXPECT_TRUE(sawTow);
        EXPECT_TRUE(closedBreak);
        EXPECT_GT(
            runtime.telemetry().evidenceFramesCertified,
            runtime.telemetry().snapshotsBuilt);
        EXPECT_GT(runtime.lastPublishedSnapshotSequence(), 0u);
        EXPECT_FALSE(runtime.failStopped());
        runtime.close();
    }
    context.tick();
    world.shutdown();
    context.shutdown();
}

TEST(
    WreckwaterAuthorityGpuIntegration,
    FragmentDoesNotDisplaceRecycledPrimarySlotInExactGpuReadback) {
    gpu::Context context;
    gpu::ContextConfig contextConfig;
    contextConfig.enableValidation = true;
    if (!context.initHeadless(contextConfig)) {
        GTEST_SKIP() << "Headless WebGPU is unavailable";
    }

    physics::PhysicsWorld world;
    physics::PhysicsInitContext physicsConfig;
    physicsConfig.requestedBackend =
        physics::BackendType::WebGpuSoft;
    physicsConfig.allowCpuFallback = false;
    physicsConfig.device = context.getDevice();
    physicsConfig.queue = context.getQueue();
    physicsConfig.maxBodies = 16u;
    physicsConfig.maxActiveBodies = 16u;
    physicsConfig.maxPairs =
        kWreckwaterAuthorityGpuPairCapacity;
    physicsConfig.maxCandidatePairs = 256u;
    physicsConfig.maxContacts = 64u;
    physicsConfig.maxManifolds =
        kWreckwaterAuthorityGpuPairCapacity;
    physicsConfig.enableValidation = true;
    physicsConfig.gpu.maximumCatchUpTicks = 1u;
    physicsConfig.gpu.commandCapacity = 32u;
    physicsConfig.gpu.debugReadbackSlots = 8u;
    physicsConfig.gpu.debugReadbackBodyCapacity =
        kWreckwaterAuthorityPhysicalBodyCount;
    physicsConfig.gpu.eventReadbackSlots = 8u;
    physicsConfig.gpu.enableTelemetryReadback = false;
    physicsConfig.gpu.gravity = {0.0f, 0.0f, 0.0f};
    ASSERT_TRUE(world.initialize(physicsConfig));

    RealWreckwaterAuthorityPhysics authorityPhysics(context, world);
    physics::BodySpawnDesc primaryDesc;
    primaryDesc.shape = physics::ThrowableShape::Box;
    primaryDesc.dimensions = {4.0f, 1.5f, 8.0f};
    primaryDesc.inverseMass = 1.0f / 850.0f;
    std::array<physics::BodyHandle, 3u> primary{};
    for (size_t index = 0u; index < primary.size(); ++index) {
        primaryDesc.position.x =
            static_cast<float>(index) * 12.0f;
        primary[index] = world.spawnBody(primaryDesc);
        ASSERT_EQ(primary[index].index, index + 1u);
        ASSERT_EQ(primary[index].generation, 1u);
    }
    physics::BodySpawnDesc fragmentDesc = primaryDesc;
    fragmentDesc.position = {40.0f, 0.0f, 0.0f};
    fragmentDesc.dimensions = {0.55f, 0.20f, 0.85f};
    fragmentDesc.inverseMass = 1.0f / 18.0f;
    const physics::BodyHandle fragment =
        world.spawnBody(fragmentDesc);
    ASSERT_EQ(fragment, (physics::BodyHandle{4u, 1u}));

    const auto exactPrimaryReadback =
        [&](uint64_t expectedTick)
            -> std::optional<physics::DebugSnapshot> {
        const WreckwaterAuthorityPhysicsStepResult submitted =
            authorityPhysics.submitExactTick(
                expectedTick, true,
                {
                    .firstBody = primary[0].index,
                    .bodyCount =
                        kWreckwaterAuthorityPhysicalBodyCount,
                });
        EXPECT_TRUE(submitted);
        EXPECT_EQ(
            submitted.status,
            WreckwaterAuthorityPhysicsStepStatus::Submitted);
        EXPECT_EQ(submitted.encodedTick, expectedTick);
        for (uint32_t attempt = 0u; attempt < 16u; ++attempt) {
            static_cast<void>(wgpuDevicePoll(
                context.getDevice(), true, nullptr));
            authorityPhysics.serviceAsync();
            if (auto snapshot =
                    authorityPhysics.pollDebugSnapshot()) {
                return snapshot;
            }
        }
        return std::nullopt;
    };

    const auto born = exactPrimaryReadback(1u);
    ASSERT_TRUE(born.has_value());
    ASSERT_EQ(born->bodies.size(), primary.size());
    for (size_t index = 0u; index < primary.size(); ++index) {
        EXPECT_TRUE(born->bodies[index].alive);
        EXPECT_EQ(born->bodies[index].handle, primary[index]);
    }

    ASSERT_TRUE(world.destroyBody(primary[0]));
    const auto retired = exactPrimaryReadback(2u);
    ASSERT_TRUE(retired.has_value());
    ASSERT_EQ(retired->bodies.size(), primary.size());
    EXPECT_FALSE(retired->bodies[0].alive);
    EXPECT_EQ(retired->bodies[0].handle.index, primary[0].index);
    EXPECT_EQ(
        retired->bodies[0].handle.generation,
        primary[0].generation + 1u);
    EXPECT_TRUE(retired->bodies[1].alive);
    EXPECT_TRUE(retired->bodies[2].alive);

    primaryDesc.position = {0.0f, 0.0f, 0.0f};
    const physics::BodyHandle replacement =
        world.spawnBody(primaryDesc);
    ASSERT_EQ(replacement.index, primary[0].index);
    ASSERT_EQ(
        replacement.generation,
        primary[0].generation + 1u);
    primary[0] = replacement;
    const auto respawned = exactPrimaryReadback(3u);
    ASSERT_TRUE(respawned.has_value());
    ASSERT_EQ(respawned->bodies.size(), primary.size());
    EXPECT_TRUE(respawned->bodies[0].alive);
    EXPECT_EQ(respawned->bodies[0].handle, replacement);
    EXPECT_TRUE(respawned->bodies[1].alive);
    EXPECT_TRUE(respawned->bodies[2].alive);
    EXPECT_EQ(world.stats().residentBodies, 4u);
    EXPECT_TRUE(world.destroyBody(fragment));

    context.tick();
    world.shutdown();
    context.shutdown();
}

} // namespace
} // namespace voxy::server
