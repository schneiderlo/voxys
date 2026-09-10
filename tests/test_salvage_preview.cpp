#include "game/expedition/salvage_preview.hpp"
#include "app/salvage_preview_readback.hpp"
#include "physics/gpu/gpu_body_metadata.hpp"
#include "physics/gpu/debug_readback_ring.hpp"
#include "physics/physics_world.hpp"
#include "gpu/context.hpp"
#include <chrono>
#include <thread>
#include <gtest/gtest.h>
#include <cstring>
#include <limits>
#include <vector>

#ifndef WGPUWrappedSubmissionIndex
struct WGPUWrappedSubmissionIndex;
#endif
extern "C" WGPUBool wgpuDevicePoll(WGPUDevice, WGPUBool, const WGPUWrappedSubmissionIndex*);

using voxy::game::expedition::SalvagePreview;
namespace {
namespace physics = voxy::physics;
struct BodyPort {
    struct Slot {
        uint32_t generation = 1;
        bool reserved = false, submitted = false, removing = false;
        physics::BodySpawnDesc desc{};
        uint32_t destroyCalls = 0;
    };
    std::vector<Slot> slots = std::vector<Slot>(128);
    uint32_t capacity = 127, spawnCalls = 0;
    uint32_t failSpawnAt = std::numeric_limits<uint32_t>::max();
    uint32_t acceptedDestroyBudget = std::numeric_limits<uint32_t>::max();
    physics::BodyHandle spawn(const physics::BodySpawnDesc& desc = {}) {
        if (spawnCalls++ >= failSpawnAt) return {};
        for (uint32_t i = 1; i <= capacity; ++i) {
            auto& slot = slots[i];
            if (!slot.reserved) {
                slot.reserved = true; slot.submitted = slot.removing = false;
                slot.desc = desc; slot.destroyCalls = 0;
                return {i, slot.generation};
            }
        }
        return {};
    }
    bool destroy(physics::BodyHandle handle) {
        auto& slot = slots.at(handle.index);
        if (!slot.reserved || slot.generation != handle.generation) return false;
        ++slot.destroyCalls;
        if (slot.removing) return false;
        // Pending spawns cancel without needing another command queue slot.
        if (!slot.submitted) { release(slot); return true; }
        if (!acceptedDestroyBudget) return false;
        --acceptedDestroyBudget;
        slot.removing = true;
        return true;
    }
    static void release(Slot& slot) {
        slot.reserved = slot.submitted = slot.removing = false;
        ++slot.generation;
    }
    void completeGpuStep() {
        for (auto& slot : slots) {
            if (slot.removing) release(slot);
            else if (slot.reserved) slot.submitted = true;
        }
    }
    std::vector<std::byte> metadata() const {
        std::vector<std::byte> bytes(slots.size() * sizeof(glm::uvec4));
        for (size_t i = 0; i < slots.size(); ++i) {
            uint32_t packed = slots[i].generation;
            if (slots[i].reserved && slots[i].submitted) packed |= physics::kGpuBodyAliveFlag;
            std::memcpy(bytes.data() + i * sizeof(glm::uvec4) + 12, &packed, 4);
        }
        return bytes;
    }
    uint32_t resident() const {
        uint32_t result = 0;
        for (const auto& slot : slots) result += slot.reserved ? 1u : 0u;
        return result;
    }
    bool owns(physics::BodyHandle handle) const {
        return slots[handle.index].reserved && slots[handle.index].generation == handle.generation;
    }
    SalvagePreview::BodyAccess access() {
        return {[this](const auto& desc) { return spawn(desc); },
                [this](auto handle) { return destroy(handle); }};
    }
};

TEST(SalvagePreview, AuthoredFixtureIsBoundedAndStatic) {
    BodyPort bodies;
    SalvagePreview preview;
    ASSERT_TRUE(preview.initialize(bodies.access(), {10, 20, 30}));
    EXPECT_EQ(preview.bodyCount(), 32u);
    EXPECT_EQ(preview.legoBodyIds().size(), 4u);
    EXPECT_LT(preview.bodyCount(), SalvagePreview::PreviewBodyBudget);
    for (const auto& slot : bodies.slots) if (slot.reserved) {
        EXPECT_EQ(slot.desc.inverseMass, 0.0f);
        EXPECT_EQ(slot.desc.shape, physics::ThrowableShape::Box);
        EXPECT_TRUE(slot.desc.material.has_value());
        EXPECT_GT(slot.desc.dimensions.x, 0.0f);
    }
    EXPECT_TRUE(preview.shutdown());
    EXPECT_EQ(bodies.resident(), 0u); // All were pending, no GPU submission needed.
}

TEST(SalvagePreview, AssetInspectionControlsOwnNoSceneryOrBodyLifetimes) {
    BodyPort bodies;
    const auto sentinel = bodies.spawn();
    bodies.completeGpuStep();
    const auto initialSpawns = bodies.spawnCalls;
    SalvagePreview preview;
    ASSERT_TRUE(preview.initialize(bodies.access(), {10,20,30}, SalvagePreview::Scenery::Inspection));
    EXPECT_EQ(preview.phase(), SalvagePreview::Phase::Ready);
    EXPECT_EQ(preview.bodyCount(), 0u);
    EXPECT_TRUE(preview.legoBodyIds().empty());
    for (uint32_t reset = 1; reset <= 8; ++reset) {
        const auto revision = preview.revision();
        ASSERT_TRUE(preview.request(SalvagePreview::Action::Reset));
        preview.update();
        EXPECT_EQ(preview.resetCount(), reset);
        EXPECT_EQ(preview.phase(), SalvagePreview::Phase::Ready);
        EXPECT_GT(preview.revision(), revision);
        EXPECT_FALSE(preview.busy());
        EXPECT_FALSE(preview.retirementSnapshot()); // No zero-byte GPU readback.
        EXPECT_EQ(preview.bodyCount(), 0u);
    }
    ASSERT_TRUE(preview.request(SalvagePreview::Action::Reset));
    ASSERT_TRUE(preview.request(SalvagePreview::Action::Leave));
    preview.update();
    EXPECT_EQ(preview.phase(), SalvagePreview::Phase::Empty);
    EXPECT_EQ(preview.resetCount(), 8u); // Leave wins the unconsumed Reset.
    EXPECT_FALSE(preview.request(SalvagePreview::Action::Reset));
    EXPECT_TRUE(preview.shutdown());
    EXPECT_EQ(bodies.spawnCalls, initialSpawns);
    EXPECT_EQ(bodies.slots[sentinel.index].destroyCalls, 0u);
    EXPECT_TRUE(bodies.owns(sentinel));
    ASSERT_TRUE(preview.initialize(bodies.access(), {}, SalvagePreview::Scenery::Cove));
    EXPECT_EQ(preview.bodyCount(), 32u); // Normal cove remains the real body path.
    EXPECT_TRUE(preview.shutdown());
    EXPECT_EQ(bodies.resident(), 1u);
}

TEST(SalvagePreview, PartialSpawnFailureRollsBackOnlyOwnedBodies) {
    BodyPort bodies;
    const auto sentinel = bodies.spawn();
    bodies.completeGpuStep();
    bodies.capacity = 8;
    SalvagePreview preview;
    EXPECT_FALSE(preview.initialize(bodies.access(), {}));
    EXPECT_EQ(preview.phase(), SalvagePreview::Phase::Failed);
    EXPECT_EQ(preview.bodyCount(), 0u);
    EXPECT_EQ(bodies.resident(), 1u);
    EXPECT_TRUE(bodies.owns(sentinel));
    EXPECT_TRUE(preview.shutdown());
    bodies.capacity = 127;
    EXPECT_TRUE(preview.initialize(bodies.access(), {}));
}

TEST(SalvagePreview, ResetRequiresCompletedMatchingGenerationSnapshot) {
    BodyPort bodies;
    const auto sentinel = bodies.spawn();
    SalvagePreview preview;
    ASSERT_TRUE(preview.initialize(bodies.access(), {}));
    bodies.completeGpuStep();
    for (uint32_t reset = 1; reset <= 8; ++reset) {
        ASSERT_TRUE(preview.request(SalvagePreview::Action::Reset));
        ASSERT_TRUE(preview.request(SalvagePreview::Action::Reset));
        preview.update();
        const auto request = preview.retirementSnapshot();
        ASSERT_TRUE(request);
        preview.observeRetirement(request->revision, bodies.metadata());
        EXPECT_EQ(preview.resetCount(), reset - 1u);
        EXPECT_TRUE(preview.busy());
        bodies.completeGpuStep();
        preview.observeRetirement(request->revision - 1u, bodies.metadata());
        EXPECT_TRUE(preview.busy());
        preview.observeRetirement(request->revision, std::span<const std::byte>{});
        EXPECT_TRUE(preview.busy());
        preview.observeRetirement(request->revision, bodies.metadata());
        EXPECT_EQ(preview.resetCount(), reset);
        EXPECT_EQ(preview.bodyCount(), 32u);
        EXPECT_EQ(bodies.resident(), 33u);
        EXPECT_TRUE(bodies.owns(sentinel));
        bodies.completeGpuStep();
    }
    EXPECT_EQ(bodies.slots[sentinel.index].destroyCalls, 0u);
}

TEST(SalvagePreview, AcceptedDestroysAreNotRequeuedAndLeaveWinsReset) {
    BodyPort bodies;
    const auto sentinel = bodies.spawn();
    SalvagePreview preview;
    ASSERT_TRUE(preview.initialize(bodies.access(), {}));
    bodies.completeGpuStep();
    bodies.acceptedDestroyBudget = 2;
    ASSERT_TRUE(preview.request(SalvagePreview::Action::Reset));
    preview.update();
    EXPECT_EQ(preview.phase(), SalvagePreview::Phase::Removing);
    EXPECT_FALSE(preview.retirementSnapshot());
    ASSERT_TRUE(preview.request(SalvagePreview::Action::Leave));
    ASSERT_TRUE(preview.request(SalvagePreview::Action::Reset));
    bodies.acceptedDestroyBudget = 64;
    preview.update();
    EXPECT_EQ(bodies.slots[2].destroyCalls, 1u);
    EXPECT_EQ(bodies.slots[3].destroyCalls, 1u);
    const auto request = preview.retirementSnapshot();
    ASSERT_TRUE(request);
    bodies.completeGpuStep();
    preview.observeRetirement(request->revision, bodies.metadata());
    EXPECT_EQ(preview.phase(), SalvagePreview::Phase::Empty);
    EXPECT_EQ(preview.resetCount(), 0u);
    EXPECT_EQ(bodies.resident(), 1u);
    EXPECT_TRUE(bodies.owns(sentinel));
    EXPECT_FALSE(preview.request(SalvagePreview::Action::Reset));
}

TEST(SalvagePreview, ReusedIndicesDoNotRetainOrDeleteAnotherGeneration) {
    BodyPort bodies;
    SalvagePreview preview;
    ASSERT_TRUE(preview.initialize(bodies.access(), {}));
    bodies.completeGpuStep();
    ASSERT_TRUE(preview.request(SalvagePreview::Action::Reset));
    preview.update();
    const auto request = preview.retirementSnapshot();
    ASSERT_TRUE(request);
    bodies.completeGpuStep();
    const auto replacement = bodies.spawn();
    bodies.completeGpuStep();
    preview.observeRetirement(request->revision, bodies.metadata());
    EXPECT_EQ(preview.phase(), SalvagePreview::Phase::Ready);
    EXPECT_TRUE(preview.shutdown());
    EXPECT_TRUE(bodies.owns(replacement));
    EXPECT_EQ(bodies.resident(), 1u);
}

TEST(SalvagePreview, FailedRespawnRollsBackNewGeneration) {
    BodyPort bodies;
    SalvagePreview preview;
    ASSERT_TRUE(preview.initialize(bodies.access(), {}));
    bodies.completeGpuStep();
    ASSERT_TRUE(preview.request(SalvagePreview::Action::Reset));
    preview.update();
    const auto request = preview.retirementSnapshot();
    ASSERT_TRUE(request);
    bodies.completeGpuStep();
    bodies.failSpawnAt = bodies.spawnCalls + 4u;
    preview.observeRetirement(request->revision, bodies.metadata());
    EXPECT_EQ(preview.phase(), SalvagePreview::Phase::Failed);
    EXPECT_EQ(preview.resetCount(), 0u);
    EXPECT_EQ(bodies.resident(), 0u);
}

TEST(SalvagePreview, InvalidPortsAndActionsHaveNoSideEffects) {
    BodyPort bodies;
    SalvagePreview preview;
    EXPECT_FALSE(preview.initialize({}, {}));
    EXPECT_FALSE(preview.initialize(bodies.access(), {std::numeric_limits<float>::quiet_NaN(), 0, 0}));
    EXPECT_FALSE(preview.initialize(bodies.access(), {}, static_cast<SalvagePreview::Scenery>(99)));
    EXPECT_EQ(bodies.spawnCalls, 0u);
    EXPECT_TRUE(preview.initialize(bodies.access(), {}));
    EXPECT_FALSE(preview.request(static_cast<SalvagePreview::Action>(99)));
    preview.update();
    EXPECT_EQ(preview.phase(), SalvagePreview::Phase::Ready);
}
TEST(SalvagePreview, ShutdownReportsBlockedRemovalAndCanRetry) {
    BodyPort bodies;
    SalvagePreview preview;
    ASSERT_TRUE(preview.initialize(bodies.access(), {}));
    const auto initialRevision = preview.revision();
    bodies.completeGpuStep();
    bodies.acceptedDestroyBudget = 0;
    EXPECT_FALSE(preview.shutdown());
    EXPECT_EQ(preview.bodyCount(), 32u);
    EXPECT_EQ(preview.phase(), SalvagePreview::Phase::Failed);
    bodies.acceptedDestroyBudget = 64;
    EXPECT_TRUE(preview.shutdown());
    bodies.completeGpuStep();
    EXPECT_EQ(bodies.resident(), 0u);
    ASSERT_TRUE(preview.initialize(bodies.access(), {}));
    EXPECT_GT(preview.revision(), initialRevision);
}

TEST(SalvagePreviewGpu, CompletedMetadataAllowsBoundedResetAndPreservesUnownedBody) {
    voxy::gpu::Context gpu;
    voxy::gpu::ContextConfig config;
    ASSERT_TRUE(gpu.initHeadless(config));
    gpu.setErrorCallback([](WGPUErrorType, const std::string& message) {
        ADD_FAILURE() << "GPU validation: " << message;
    });
    physics::PhysicsInitContext context;
    context.requestedBackend = physics::BackendType::WebGpuSoft;
    context.device = gpu.getDevice(); context.queue = gpu.getQueue();
    context.maxBodies = context.maxActiveBodies = 128u;
    context.maxPairs = context.maxContacts = context.maxManifolds = 1024u;
    context.gpu.commandCapacity = 512u;
    physics::PhysicsWorld world;
    ASSERT_TRUE(world.initialize(context));
    physics::BodySpawnDesc sentinelDesc;
    sentinelDesc.position = {50, 0, 50}; sentinelDesc.inverseMass = 0.0f;
    const auto sentinel = world.spawnBody(sentinelDesc);
    ASSERT_TRUE(sentinel.valid());
    SalvagePreview preview;
    ASSERT_TRUE(preview.initialize(world, {}));
    voxy::app_detail::SalvageMetadataReadbackSource metadataSource;
    EXPECT_FALSE(metadataSource.capture(nullptr));
    const auto metadataBuffer = world.renderView().metadataBuffer;
    ASSERT_TRUE(metadataSource.capture(metadataBuffer));
    physics::DebugReadbackRing readback;
    const uint32_t capacity = static_cast<uint32_t>(wgpuBufferGetSize(metadataBuffer) / sizeof(glm::uvec4));
    const size_t bytes = size_t{capacity} * sizeof(glm::uvec4);
    ASSERT_TRUE(readback.initialize(gpu.getDevice(), 1u, bytes));
    const auto frame = [&]() -> std::optional<physics::RawDebugReadback> {
        preview.update();
        world.update(1.0f / 60.0f);
        WGPUCommandEncoderDescriptor encoderDesc{};
        auto encoder = wgpuDeviceCreateCommandEncoder(gpu.getDevice(), &encoderDesc);
        if (!encoder) return std::nullopt;
        const auto report = world.encodeGpuStepChecked(encoder);
        EXPECT_TRUE(report.succeeded());
        // The same capture/read path as Application must remain usable when
        // renderView() hides every buffer after the final body is removed.
        const auto readableMetadata = metadataSource.readableBuffer(capacity);
        EXPECT_EQ(readableMetadata, metadataBuffer);
        const bool copied = readback.encodeCopy(encoder, readableMetadata,
            0u, bytes, preview.revision(), 0u, capacity);
        EXPECT_TRUE(copied);
        WGPUCommandBufferDescriptor commandDesc{};
        auto command = wgpuCommandEncoderFinish(encoder, &commandDesc);
        wgpuCommandEncoderRelease(encoder);
        if (!command) return std::nullopt;
        wgpuQueueSubmit(gpu.getQueue(), 1u, &command);
        wgpuCommandBufferRelease(command);
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
        do {
            static_cast<void>(wgpuDevicePoll(gpu.getDevice(), false, nullptr));
            if (auto completed = readback.poll()) return completed;
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        } while (std::chrono::steady_clock::now() < deadline);
        return std::nullopt;
    };
    auto completed = frame();
    ASSERT_TRUE(completed);
    for (uint32_t reset = 1; reset <= 3; ++reset) {
        ASSERT_TRUE(preview.request(SalvagePreview::Action::Reset));
        completed = frame();
        ASSERT_TRUE(completed);
        EXPECT_TRUE(preview.busy()); // Completion must be explicitly observed.
        const auto oldRange = preview.retirementSnapshot();
        ASSERT_TRUE(oldRange);
        const auto retiredView = world.renderView();
        ASSERT_LT(retiredView.residentBodyCapacity, oldRange->bodyCount);
        // Exercise the exact Application boundary: the renderer's now-smaller
        // live range cannot reject the still-allocated old lifetime metadata.
        EXPECT_TRUE(voxy::app_detail::salvageMetadataRangeReadable(
            retiredView.metadataBuffer, oldRange->bodyCount));
        EXPECT_FALSE(voxy::app_detail::salvageMetadataRangeReadable(nullptr, oldRange->bodyCount));
        EXPECT_FALSE(voxy::app_detail::salvageMetadataRangeReadable(
            retiredView.metadataBuffer, std::numeric_limits<uint32_t>::max()));
        EXPECT_EQ(preview.resetCount(), reset - 1u);
        preview.observeRetirement(completed->tick, completed->bytes);
        ASSERT_EQ(preview.phase(), SalvagePreview::Phase::Ready);
        EXPECT_EQ(preview.resetCount(), reset);
        EXPECT_EQ(world.stats().residentBodies, 33u);
        completed = frame();
        ASSERT_TRUE(completed);
    }
    ASSERT_TRUE(preview.request(SalvagePreview::Action::Leave));
    completed = frame();
    ASSERT_TRUE(completed);
    preview.observeRetirement(completed->tick, completed->bytes);
    EXPECT_EQ(preview.phase(), SalvagePreview::Phase::Empty);
    EXPECT_EQ(world.stats().residentBodies, 1u);
    uint32_t packed = 0;
    std::memcpy(&packed, completed->bytes.data() + size_t{sentinel.index} * sizeof(glm::uvec4) + 12, 4);
    EXPECT_EQ(packed & physics::kGpuBodyGenerationMask, sentinel.generation);
    EXPECT_NE(packed & physics::kGpuBodyAliveFlag, 0u);

    // Fill most of the same world's host slots, then fail fixture creation
    // before submitting. Rollback must preserve all 101 unrelated lifetimes.
    std::vector<physics::BodyHandle> unrelated;
    for (uint32_t i = 0; i < 100u; ++i) {
        const auto handle = world.spawnBody(sentinelDesc);
        ASSERT_TRUE(handle.valid());
        unrelated.push_back(handle);
    }
    EXPECT_FALSE(preview.initialize(world, {}));
    EXPECT_EQ(preview.bodyCount(), 0u);
    EXPECT_EQ(world.stats().residentBodies, 101u);
    for (const auto handle : unrelated) EXPECT_TRUE(world.destroyBody(handle));
    EXPECT_TRUE(world.destroyBody(sentinel));
    completed = frame();
    ASSERT_TRUE(completed);
    ASSERT_EQ(world.stats().residentBodies, 0u);
    ASSERT_EQ(world.renderView().metadataBuffer, nullptr);
    ASSERT_TRUE(preview.shutdown());
    ASSERT_TRUE(preview.initialize(world, {}));
    ASSERT_EQ(world.renderView().metadataBuffer, metadataBuffer);
    completed = frame();
    ASSERT_TRUE(completed);
    const uint32_t resetBase = preview.resetCount();
    for (uint32_t reset = 1; reset <= 3; ++reset) {
        ASSERT_TRUE(preview.request(SalvagePreview::Action::Reset));
        completed = frame();
        ASSERT_TRUE(completed);
        ASSERT_EQ(world.stats().residentBodies, 0u);
        ASSERT_EQ(world.renderView().metadataBuffer, nullptr);
        const auto request = preview.retirementSnapshot();
        ASSERT_TRUE(request);
        EXPECT_EQ(metadataSource.readableBuffer(request->bodyCount), metadataBuffer);
        // A second completed frame while empty exercises the asynchronous
        // interval too: a newly queried renderer view cannot supply this copy.
        completed = frame();
        ASSERT_TRUE(completed);
        preview.observeRetirement(completed->tick, completed->bytes);
        ASSERT_EQ(preview.phase(), SalvagePreview::Phase::Ready);
        EXPECT_EQ(preview.resetCount(), resetBase + reset);
        EXPECT_EQ(world.stats().residentBodies, 32u);
        EXPECT_EQ(world.renderView().metadataBuffer, metadataBuffer);
        completed = frame();
        ASSERT_TRUE(completed);
    }
    ASSERT_TRUE(preview.request(SalvagePreview::Action::Leave));
    completed = frame();
    ASSERT_TRUE(completed);
    ASSERT_EQ(world.stats().residentBodies, 0u);
    ASSERT_EQ(world.renderView().metadataBuffer, nullptr);
    preview.observeRetirement(completed->tick, completed->bytes);
    EXPECT_EQ(preview.phase(), SalvagePreview::Phase::Empty);
    readback.shutdown();
    metadataSource.clear();
    EXPECT_EQ(metadataSource.readableBuffer(1u), nullptr);
    // Releasing the retained reference must not destroy physics' allocation.
    const auto finalBody = world.spawnBody(sentinelDesc);
    ASSERT_TRUE(finalBody.valid());
    EXPECT_EQ(world.renderView().metadataBuffer, metadataBuffer);
    EXPECT_TRUE(metadataSource.capture(world.renderView().metadataBuffer));
    metadataSource.clear();
    EXPECT_TRUE(world.destroyBody(finalBody));
}
} // namespace
