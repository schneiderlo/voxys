#pragma once

#include "gpu/webgpu_compat.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>

namespace voxy::physics {

struct GpuIslandInput {
    WGPUBuffer poseBuffer = nullptr;
    WGPUBuffer motionBuffer = nullptr;
    WGPUBuffer metadataBuffer = nullptr;
    WGPUBuffer manifoldBuffer = nullptr;
    WGPUBuffer narrowPhaseTelemetryBuffer = nullptr;
    uint32_t bodyCapacity = 0;
    uint32_t contactCapacity = 0;

    [[nodiscard]] bool valid() const noexcept {
        return poseBuffer && motionBuffer && metadataBuffer && manifoldBuffer
            && narrowPhaseTelemetryBuffer && bodyCapacity != 0
            && contactCapacity != 0;
    }
};

struct alignas(16) GpuIslandRecord {
    uint32_t rootBody = 0;
    uint32_t firstBodyRecord = 0;
    uint32_t bodyCount = 0;
    uint32_t state = 0;
};

struct alignas(16) GpuSleepingCellRange {
    uint32_t keyLow = 0;
    uint32_t keyHigh = 0;
    uint32_t firstEntry = 0;
    uint32_t entryCount = 0;
};

enum class GpuIslandEventType : uint32_t {
    Sleep = 1,
    Wake = 2,
};

struct alignas(16) GpuIslandEvent {
    uint32_t rootBody = 0;
    GpuIslandEventType type = GpuIslandEventType::Sleep;
    uint32_t tick = 0;
    uint32_t bodyCount = 0;
};

struct GpuIslandTelemetry {
    uint32_t islandCount = 0;
    uint32_t awakeIslands = 0;
    uint32_t sleepingIslands = 0;
    uint32_t awakeBodies = 0;
    uint32_t sleepingBodies = 0;
    uint32_t maximumIslandBodies = 0;
    uint32_t sleepTransitions = 0;
    uint32_t wakeTransitions = 0;
    uint32_t events = 0;
    uint32_t sleepingGridEntries = 0;
    uint32_t sleepingGridCells = 0;
    uint32_t rootErrors = 0;
    uint32_t unionRounds = 0;
    uint32_t executedGlobalUnionRounds = 0;
    uint32_t tick = 0;
    uint32_t highIslands = 0;
    uint32_t highSleepingBodies = 0;
    uint32_t highSleepingGridEntries = 0;
    uint32_t highEvents = 0;
    bool eventOverflow = false;
    bool gridOverflow = false;
};

class GpuIslandManager {
public:
    static constexpr uint32_t kTelemetryWordCount = 36;

    struct Config {
        uint32_t bodyCapacity = 16'384;
        uint32_t contactCapacity = 65'536;
        uint32_t eventCapacity = 16'384;
        uint32_t workgroupSize = 128;
        uint32_t unionRounds = 32;
        uint32_t sleepTicks = 30;
        float linearSleepThreshold = 0.05f;
        float angularSleepThreshold = 0.05f;
        float sleepingCellSize = 4.0f;
        std::filesystem::path shaderPath = "shaders/physics_islands.wgsl";
        std::filesystem::path primitivesShaderPath =
            "shaders/physics_deterministic_primitives.wgsl";
    };

    GpuIslandManager();
    ~GpuIslandManager();
    GpuIslandManager(const GpuIslandManager&) = delete;
    GpuIslandManager& operator=(const GpuIslandManager&) = delete;
    GpuIslandManager(GpuIslandManager&&) noexcept;
    GpuIslandManager& operator=(GpuIslandManager&&) noexcept;

    [[nodiscard]] bool initialize(WGPUDevice device, WGPUQueue queue,
                                  const Config& config);
    void shutdown();
    void setInput(const GpuIslandInput& input);
    [[nodiscard]] bool encode(WGPUCommandEncoder encoder,
                              bool compactSmallWorld = false);

    [[nodiscard]] WGPUBuffer bodyRoots() const noexcept;
    [[nodiscard]] WGPUBuffer sortedBodyRecords() const noexcept;
    [[nodiscard]] WGPUBuffer islands() const noexcept;
    [[nodiscard]] WGPUBuffer sleepingGridEntries() const noexcept;
    [[nodiscard]] WGPUBuffer sleepingCellRanges() const noexcept;
    [[nodiscard]] WGPUBuffer events() const noexcept;
    [[nodiscard]] WGPUBuffer telemetryBuffer() const noexcept;
    [[nodiscard]] size_t scratchBytes() const noexcept;
    // Monotonic since initialization; used to guard steady-state cache reuse.
    [[nodiscard]] size_t inputBindGroupCacheMisses() const noexcept;

    [[nodiscard]] static GpuIslandTelemetry decodeTelemetry(
        std::span<const uint32_t> words) noexcept;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

static_assert(sizeof(GpuIslandRecord) == 16);
static_assert(sizeof(GpuSleepingCellRange) == 16);
static_assert(sizeof(GpuIslandEvent) == 16);

} // namespace voxy::physics
