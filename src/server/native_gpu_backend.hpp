#pragma once

#include "gpu/webgpu_compat.hpp"
#include "physics/gpu/gpu_lockstep.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <vector>

namespace voxy::server {

struct ServerWorldHandle {
    uint32_t index = 0;
    uint32_t generation = 0;
    [[nodiscard]] auto operator<=>(const ServerWorldHandle&) const = default;
};

struct ServerWorldDescriptor {
    uint64_t worldId = 0;
    uint64_t islandId = 0;
    physics::deterministic::GpuLockstepWorld::Config physics{};
};

struct NativeServerGpuTelemetry {
    uint64_t encodedBatches = 0;
    uint64_t worldDispatches = 0;
    uint64_t isolatedSubmissionEquivalent = 0;
    uint64_t avoidedSubmissionIntents = 0;
    uint64_t rejectedBatches = 0;
    uint64_t uploadedBodies = 0;
    uint32_t activeWorlds = 0;
    uint32_t worldHighWater = 0;
    size_t allocatedBytes = 0;
    size_t memoryHighWater = 0;
};

class NativeServerGpuBackend {
public:
    struct Config {
        uint32_t maximumWorlds = 256;
    };

    NativeServerGpuBackend();
    ~NativeServerGpuBackend();
    NativeServerGpuBackend(const NativeServerGpuBackend&) = delete;
    NativeServerGpuBackend& operator=(const NativeServerGpuBackend&) = delete;
    NativeServerGpuBackend(NativeServerGpuBackend&&) noexcept;
    NativeServerGpuBackend& operator=(NativeServerGpuBackend&&) noexcept;

    [[nodiscard]] bool initialize(
        WGPUDevice device, WGPUQueue queue, const Config& config);
    void shutdown();
    [[nodiscard]] std::optional<ServerWorldHandle> createWorld(
        const ServerWorldDescriptor& descriptor);
    [[nodiscard]] bool destroyWorld(ServerWorldHandle handle);
    [[nodiscard]] bool uploadBodies(
        ServerWorldHandle handle,
        std::span<const physics::deterministic::LockstepBody> bodies);
    // All dispatches are appended to one caller-owned encoder and therefore
    // can retire in one queue submission.
    [[nodiscard]] bool encodeBatch(
        WGPUCommandEncoder encoder, uint32_t tick,
        std::span<const ServerWorldHandle> worlds);

    [[nodiscard]] std::optional<ServerWorldDescriptor> descriptor(
        ServerWorldHandle handle) const;
    [[nodiscard]] WGPUBuffer bodyBuffer(ServerWorldHandle handle) const noexcept;
    [[nodiscard]] WGPUBuffer telemetryBuffer(
        ServerWorldHandle handle) const noexcept;
    [[nodiscard]] const NativeServerGpuTelemetry& telemetry() const noexcept;
    [[nodiscard]] static uint32_t arithmeticSchemaVersion() noexcept;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace voxy::server
