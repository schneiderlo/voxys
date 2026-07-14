#pragma once

#include "gpu/webgpu_compat.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <vector>

namespace voxy::physics {

enum class GpuPhysicsEventType : uint32_t {
    ContactBegin = 1,
    ContactEnd = 2,
    ContactHit = 3,
    IslandSleep = 4,
    IslandWake = 5,
};

struct alignas(16) GpuPhysicsEvent {
    // tick low word, event type, body/root A, body B or UINT_MAX.
    std::array<uint32_t, 4> header{};
    // feature ID, source/contact ID, auxiliary count, flags.
    std::array<uint32_t, 4> detail{};
};

struct GpuEventSources {
    WGPUBuffer contactEvents = nullptr;
    WGPUBuffer contactTelemetry = nullptr;
    uint32_t contactCapacity = 0;
    WGPUBuffer islandEvents = nullptr;
    WGPUBuffer islandTelemetry = nullptr;
    uint32_t islandEventCapacity = 0;
    WGPUBuffer manifolds = nullptr;
    WGPUBuffer narrowPhaseTelemetry = nullptr;
    uint32_t manifoldCapacity = 0;

    [[nodiscard]] bool hasContacts() const noexcept {
        return contactEvents && contactTelemetry && contactCapacity != 0;
    }
    [[nodiscard]] bool hasIslands() const noexcept {
        return islandEvents && islandTelemetry && islandEventCapacity != 0;
    }
    [[nodiscard]] bool hasHits() const noexcept {
        return manifolds && narrowPhaseTelemetry && manifoldCapacity != 0;
    }
    [[nodiscard]] bool valid() const noexcept {
        return hasContacts() || hasIslands() || hasHits();
    }
};

struct GpuEventBatch {
    uint64_t tick = 0;
    bool overflow = false;
    std::vector<GpuPhysicsEvent> events;
};

class GpuEventReadbackRing {
public:
    struct Config {
        uint32_t eventCapacity = 16'384;
        uint32_t readbackSlots = 3;
        std::filesystem::path shaderPath =
            "shaders/physics_event_readback.wgsl";
    };

    GpuEventReadbackRing();
    ~GpuEventReadbackRing();
    GpuEventReadbackRing(const GpuEventReadbackRing&) = delete;
    GpuEventReadbackRing& operator=(const GpuEventReadbackRing&) = delete;
    GpuEventReadbackRing(GpuEventReadbackRing&&) noexcept;
    GpuEventReadbackRing& operator=(GpuEventReadbackRing&&) noexcept;

    [[nodiscard]] bool initialize(WGPUDevice device, WGPUQueue queue,
                                  const Config& config);
    void shutdown();
    void setSources(const GpuEventSources& sources);
    [[nodiscard]] bool encodeReadback(WGPUCommandEncoder encoder,
                                      uint64_t tick);
    [[nodiscard]] std::optional<GpuEventBatch> poll();

    [[nodiscard]] WGPUBuffer packedEventBuffer() const noexcept;
    [[nodiscard]] size_t allocatedBytes() const noexcept;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

static_assert(sizeof(GpuPhysicsEvent) == 32);

} // namespace voxy::physics
