#pragma once

#include "gpu/webgpu_compat.hpp"
#include "physics/deterministic/lockstep_types.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>

namespace voxy::physics::deterministic {

class GpuLockstepWorld {
public:
    struct Config {
        uint32_t bodyCapacity = 1'024;
        uint32_t contactCapacity = 4'096;
        uint32_t tickRateHz = kLockstepDefaultTickRateHz;
        uint32_t substeps = kLockstepDefaultSubsteps;
        uint32_t solverIterations = 4;
        int32_t gravityPerSubstepQ16 =
            kLockstepDefaultGravityPerSubstepQ16;
        std::filesystem::path shaderPath = "shaders/physics_lockstep.wgsl";
    };

    GpuLockstepWorld();
    ~GpuLockstepWorld();
    GpuLockstepWorld(const GpuLockstepWorld&) = delete;
    GpuLockstepWorld& operator=(const GpuLockstepWorld&) = delete;
    GpuLockstepWorld(GpuLockstepWorld&&) noexcept;
    GpuLockstepWorld& operator=(GpuLockstepWorld&&) noexcept;

    [[nodiscard]] bool initialize(WGPUDevice device, WGPUQueue queue,
                                  const Config& config);
    void shutdown();
    [[nodiscard]] bool uploadBodies(std::span<const LockstepBody> bodies);
    [[nodiscard]] bool encode(WGPUCommandEncoder encoder, uint32_t tick);

    [[nodiscard]] WGPUBuffer bodyBuffer() const noexcept;
    [[nodiscard]] WGPUBuffer contactBuffer() const noexcept;
    [[nodiscard]] WGPUBuffer rootBuffer() const noexcept;
    [[nodiscard]] WGPUBuffer bodyHashBuffer() const noexcept;
    [[nodiscard]] WGPUBuffer contactHashBuffer() const noexcept;
    [[nodiscard]] WGPUBuffer islandHashBuffer() const noexcept;
    [[nodiscard]] WGPUBuffer telemetryBuffer() const noexcept;
    [[nodiscard]] size_t allocatedBytes() const noexcept;

    [[nodiscard]] static LockstepTelemetry decodeTelemetry(
        std::span<const uint32_t> words);

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace voxy::physics::deterministic
