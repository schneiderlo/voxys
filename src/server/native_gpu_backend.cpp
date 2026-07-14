#include "server/native_gpu_backend.hpp"

#include <algorithm>
#include <tuple>
#include <utility>

namespace voxy::server {

class NativeServerGpuBackend::Impl {
public:
    struct Slot {
        uint32_t generation = 1;
        bool active = false;
        ServerWorldDescriptor descriptor{};
        std::unique_ptr<physics::deterministic::GpuLockstepWorld> world;
    };

    bool initialize(WGPUDevice device, WGPUQueue queue, const Config& config) {
        shutdown();
        if (!device || !queue || config.maximumWorlds == 0u) return false;
        device_ = device;
        queue_ = queue;
        config_ = config;
        slots_.resize(config_.maximumWorlds);
        initialized_ = true;
        return true;
    }

    void shutdown() {
        for (auto& slot : slots_) {
            if (slot.world) slot.world->shutdown();
        }
        slots_.clear();
        device_ = nullptr;
        queue_ = nullptr;
        config_ = {};
        telemetry_ = {};
        initialized_ = false;
    }

    Slot* slot(ServerWorldHandle handle) noexcept {
        if (handle.index == 0u || handle.index > slots_.size()) return nullptr;
        Slot& result = slots_[handle.index - 1u];
        return result.active && result.generation == handle.generation
            ? &result : nullptr;
    }

    const Slot* slot(ServerWorldHandle handle) const noexcept {
        if (handle.index == 0u || handle.index > slots_.size()) return nullptr;
        const Slot& result = slots_[handle.index - 1u];
        return result.active && result.generation == handle.generation
            ? &result : nullptr;
    }

    void refreshTelemetry() {
        telemetry_.activeWorlds = 0;
        telemetry_.allocatedBytes = 0;
        for (const auto& slot : slots_) {
            if (!slot.active || !slot.world) continue;
            ++telemetry_.activeWorlds;
            telemetry_.allocatedBytes += slot.world->allocatedBytes();
        }
        telemetry_.worldHighWater = std::max(
            telemetry_.worldHighWater, telemetry_.activeWorlds);
        telemetry_.memoryHighWater = std::max(
            telemetry_.memoryHighWater, telemetry_.allocatedBytes);
    }

    WGPUDevice device_ = nullptr;
    WGPUQueue queue_ = nullptr;
    Config config_{};
    std::vector<Slot> slots_;
    NativeServerGpuTelemetry telemetry_{};
    bool initialized_ = false;
};

NativeServerGpuBackend::NativeServerGpuBackend()
    : impl_(std::make_unique<Impl>()) {}
NativeServerGpuBackend::~NativeServerGpuBackend() = default;
NativeServerGpuBackend::NativeServerGpuBackend(
    NativeServerGpuBackend&&) noexcept = default;
NativeServerGpuBackend& NativeServerGpuBackend::operator=(
    NativeServerGpuBackend&&) noexcept = default;

bool NativeServerGpuBackend::initialize(
    WGPUDevice device, WGPUQueue queue, const Config& config) {
    return impl_->initialize(device, queue, config);
}

void NativeServerGpuBackend::shutdown() { impl_->shutdown(); }

std::optional<ServerWorldHandle> NativeServerGpuBackend::createWorld(
    const ServerWorldDescriptor& descriptor) {
    if (!impl_->initialized_ || descriptor.worldId == 0u
        || descriptor.islandId == 0u) return std::nullopt;
    const bool duplicate = std::any_of(
        impl_->slots_.begin(), impl_->slots_.end(),
        [&descriptor](const Impl::Slot& slot) {
            return slot.active
                && slot.descriptor.worldId == descriptor.worldId
                && slot.descriptor.islandId == descriptor.islandId;
        });
    if (duplicate) return std::nullopt;
    auto iterator = std::find_if(
        impl_->slots_.begin(), impl_->slots_.end(),
        [](const Impl::Slot& slot) { return !slot.active; });
    if (iterator == impl_->slots_.end()) return std::nullopt;
    auto world = std::make_unique<physics::deterministic::GpuLockstepWorld>();
    if (!world->initialize(
            impl_->device_, impl_->queue_, descriptor.physics)) {
        return std::nullopt;
    }
    iterator->active = true;
    iterator->descriptor = descriptor;
    iterator->world = std::move(world);
    const ServerWorldHandle handle{
        static_cast<uint32_t>(iterator - impl_->slots_.begin()) + 1u,
        iterator->generation,
    };
    impl_->refreshTelemetry();
    return handle;
}

bool NativeServerGpuBackend::destroyWorld(ServerWorldHandle handle) {
    Impl::Slot* slot = impl_->slot(handle);
    if (slot == nullptr) return false;
    slot->world->shutdown();
    slot->world.reset();
    slot->descriptor = {};
    slot->active = false;
    ++slot->generation;
    if (slot->generation == 0u) slot->generation = 1u;
    impl_->refreshTelemetry();
    return true;
}

bool NativeServerGpuBackend::uploadBodies(
    ServerWorldHandle handle,
    std::span<const physics::deterministic::LockstepBody> bodies) {
    Impl::Slot* slot = impl_->slot(handle);
    if (slot == nullptr || !slot->world->uploadBodies(bodies)) return false;
    impl_->telemetry_.uploadedBodies += bodies.size();
    return true;
}

bool NativeServerGpuBackend::encodeBatch(
    WGPUCommandEncoder encoder, uint32_t tick,
    std::span<const ServerWorldHandle> input) {
    if (!encoder || input.empty()) {
        ++impl_->telemetry_.rejectedBatches;
        return false;
    }
    std::vector<ServerWorldHandle> worlds(input.begin(), input.end());
    for (const auto handle : worlds) {
        if (impl_->slot(handle) == nullptr) {
            ++impl_->telemetry_.rejectedBatches;
            return false;
        }
    }
    std::stable_sort(worlds.begin(), worlds.end(),
        [this](ServerWorldHandle lhs, ServerWorldHandle rhs) {
            const auto* left = impl_->slot(lhs);
            const auto* right = impl_->slot(rhs);
            return std::tie(left->descriptor.worldId,
                            left->descriptor.islandId, lhs.index)
                 < std::tie(right->descriptor.worldId,
                            right->descriptor.islandId, rhs.index);
        });
    if (std::adjacent_find(worlds.begin(), worlds.end()) != worlds.end()) {
        ++impl_->telemetry_.rejectedBatches;
        return false;
    }
    for (const auto handle : worlds) {
        if (!impl_->slot(handle)->world->encode(encoder, tick)) {
            ++impl_->telemetry_.rejectedBatches;
            return false;
        }
    }
    ++impl_->telemetry_.encodedBatches;
    impl_->telemetry_.worldDispatches += worlds.size();
    impl_->telemetry_.isolatedSubmissionEquivalent += worlds.size();
    impl_->telemetry_.avoidedSubmissionIntents += worlds.size() - 1u;
    return true;
}

std::optional<ServerWorldDescriptor> NativeServerGpuBackend::descriptor(
    ServerWorldHandle handle) const {
    const Impl::Slot* slot = impl_->slot(handle);
    return slot != nullptr
        ? std::optional<ServerWorldDescriptor>(slot->descriptor) : std::nullopt;
}

WGPUBuffer NativeServerGpuBackend::bodyBuffer(
    ServerWorldHandle handle) const noexcept {
    const Impl::Slot* slot = impl_->slot(handle);
    return slot != nullptr ? slot->world->bodyBuffer() : nullptr;
}

WGPUBuffer NativeServerGpuBackend::telemetryBuffer(
    ServerWorldHandle handle) const noexcept {
    const Impl::Slot* slot = impl_->slot(handle);
    return slot != nullptr ? slot->world->telemetryBuffer() : nullptr;
}

const NativeServerGpuTelemetry& NativeServerGpuBackend::telemetry() const noexcept {
    return impl_->telemetry_;
}

uint32_t NativeServerGpuBackend::arithmeticSchemaVersion() noexcept {
    return physics::deterministic::kLockstepSchemaVersion;
}

} // namespace voxy::server
