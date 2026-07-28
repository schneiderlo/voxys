#include "server/native_gpu_backend.hpp"

#include <algorithm>
#include <limits>
#include <tuple>
#include <utility>

namespace voxy::server {

class NativeServerGpuBackend::Impl {
public:
    struct Slot {
        uint32_t generation = 1;
        bool active = false;
        bool retired = false;
        ServerWorldDescriptor descriptor{};
        std::unique_ptr<physics::deterministic::GpuLockstepWorld> world;
    };

    bool initialize(WGPUDevice device, WGPUQueue queue, const Config& config) {
        constexpr uint32_t maximumSlotCount = 65'536u;
        if (!device || !queue || config.maximumWorlds == 0u
            || config.maximumWorlds > maximumSlotCount) {
            return false;
        }
        std::vector<Slot> replacement(config.maximumWorlds);
        shutdown();
        device_ = device;
        queue_ = queue;
        config_ = config;
        slots_ = std::move(replacement);
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
            const size_t bytes = slot.world->allocatedBytes();
            telemetry_.allocatedBytes =
                telemetry_.allocatedBytes
                    > std::numeric_limits<size_t>::max() - bytes
                ? std::numeric_limits<size_t>::max()
                : telemetry_.allocatedBytes + bytes;
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
    if (!impl_) impl_ = std::make_unique<Impl>();
    return impl_->initialize(device, queue, config);
}

void NativeServerGpuBackend::shutdown() {
    if (impl_) impl_->shutdown();
}

std::optional<ServerWorldHandle> NativeServerGpuBackend::createWorld(
    const ServerWorldDescriptor& descriptor) {
    if (!impl_ || !impl_->initialized_ || descriptor.worldId == 0u
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
        [](const Impl::Slot& slot) {
            return !slot.active && !slot.retired;
        });
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
    if (!impl_) return false;
    Impl::Slot* slot = impl_->slot(handle);
    if (slot == nullptr) return false;
    slot->world->shutdown();
    slot->world.reset();
    slot->descriptor = {};
    slot->active = false;
    if (slot->generation == std::numeric_limits<uint32_t>::max())
        slot->retired = true;
    else
        ++slot->generation;
    impl_->refreshTelemetry();
    return true;
}

bool NativeServerGpuBackend::uploadBodies(
    ServerWorldHandle handle,
    std::span<const physics::deterministic::LockstepBody> bodies) {
    if (!impl_) return false;
    Impl::Slot* slot = impl_->slot(handle);
    if (slot == nullptr || !slot->world->uploadBodies(bodies)) return false;
    const uint64_t count = bodies.size();
    impl_->telemetry_.uploadedBodies =
        impl_->telemetry_.uploadedBodies
                > std::numeric_limits<uint64_t>::max() - count
            ? std::numeric_limits<uint64_t>::max()
            : impl_->telemetry_.uploadedBodies + count;
    return true;
}

bool NativeServerGpuBackend::encodeBatch(
    WGPUCommandEncoder encoder, uint32_t tick,
    std::span<const ServerWorldHandle> input) {
    if (!impl_ || !impl_->initialized_ || !encoder || input.empty()
        || input.size() > impl_->config_.maximumWorlds) {
        if (!impl_) return false;
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
    if (!impl_) return std::nullopt;
    const Impl::Slot* slot = impl_->slot(handle);
    return slot != nullptr
        ? std::optional<ServerWorldDescriptor>(slot->descriptor) : std::nullopt;
}

WGPUBuffer NativeServerGpuBackend::bodyBuffer(
    ServerWorldHandle handle) const noexcept {
    if (!impl_) return nullptr;
    const Impl::Slot* slot = impl_->slot(handle);
    return slot != nullptr ? slot->world->bodyBuffer() : nullptr;
}

WGPUBuffer NativeServerGpuBackend::telemetryBuffer(
    ServerWorldHandle handle) const noexcept {
    if (!impl_) return nullptr;
    const Impl::Slot* slot = impl_->slot(handle);
    return slot != nullptr ? slot->world->telemetryBuffer() : nullptr;
}

const NativeServerGpuTelemetry& NativeServerGpuBackend::telemetry() const noexcept {
    static const NativeServerGpuTelemetry empty{};
    if (!impl_) return empty;
    return impl_->telemetry_;
}

uint32_t NativeServerGpuBackend::arithmeticSchemaVersion() noexcept {
    return physics::deterministic::kLockstepSchemaVersion;
}

} // namespace voxy::server
