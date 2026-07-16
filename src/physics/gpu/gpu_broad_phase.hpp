#pragma once

#include "gpu/webgpu_compat.hpp"
#include "physics/gpu/deterministic_primitives.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>

namespace voxy::physics {

struct BroadPhaseBodyView {
    WGPUBuffer poseBuffer = nullptr;
    WGPUBuffer shapeBuffer = nullptr;
    WGPUBuffer metadataBuffer = nullptr;
    uint32_t bodyCapacity = 0;

    [[nodiscard]] bool valid() const noexcept {
        return poseBuffer && shapeBuffer && metadataBuffer && bodyCapacity != 0;
    }
};

struct alignas(16) GpuCellRange {
    uint32_t keyLow = 0;
    uint32_t keyHigh = 0;
    uint32_t firstEntry = 0;
    uint32_t entryCount = 0;
};

struct alignas(16) GpuPersistentContact {
    GpuKeyValue pair{};
    std::array<uint32_t, 4> state{};
};

struct alignas(16) GpuContactEvent {
    uint32_t pairLow = 0;
    uint32_t pairHigh = 0;
    uint32_t type = 0;
    uint32_t contactId = 0;
};

enum class ContactEventType : uint32_t {
    Begin = 1,
    End = 2,
    Hit = 3,
};

struct GpuBroadPhaseTelemetry {
    uint32_t gridEntries = 0;
    uint32_t occupiedCells = 0;
    uint32_t candidatePairs = 0;
    uint32_t uniquePairs = 0;
    uint32_t activeSleepingPairs = 0;
    uint32_t oversizedBodies = 0;
    uint32_t persistentContacts = 0;
    uint32_t beginEvents = 0;
    uint32_t endEvents = 0;
    bool candidateOverflow = false;
    bool pairOverflow = false;
    bool contactOverflow = false;
    bool eventOverflow = false;
    uint32_t highGridEntries = 0;
    uint32_t highOccupiedCells = 0;
    uint32_t highCandidatePairs = 0;
    uint32_t highUniquePairs = 0;
    uint32_t highContacts = 0;
    uint32_t highEvents = 0;
    uint32_t tick = 0;
};

class GpuBroadPhase {
public:
    static constexpr uint32_t kTelemetryWordCount = 32;
    static constexpr uint32_t kProfilingInternalBoundaryCount = 5;

    struct ProfilingBoundary {
        void (*callback)(const void*) = nullptr;
        const void* userData = nullptr;
    };

    struct Config {
        uint32_t bodyCapacity = 16'384;
        uint32_t candidatePairCapacity = 262'144;
        uint32_t pairCapacity = 65'536;
        uint32_t contactCapacity = 65'536;
        float cellSize = 4.0f;
        float speculativeMargin = 0.02f;
        uint32_t workgroupSize = 128;
        std::filesystem::path shaderPath = "shaders/physics_broad_phase.wgsl";
        std::filesystem::path primitivesShaderPath =
            "shaders/physics_deterministic_primitives.wgsl";
    };

    GpuBroadPhase();
    ~GpuBroadPhase();
    GpuBroadPhase(const GpuBroadPhase&) = delete;
    GpuBroadPhase& operator=(const GpuBroadPhase&) = delete;
    GpuBroadPhase(GpuBroadPhase&&) noexcept;
    GpuBroadPhase& operator=(GpuBroadPhase&&) noexcept;

    [[nodiscard]] bool initialize(WGPUDevice device, WGPUQueue queue,
                                  const Config& config);
    void shutdown();
    void setBodyView(const BroadPhaseBodyView& view);
    // Selects between equivalent canonical pair enumerators using delayed
    // occupancy telemetry. Threshold hysteresis prevents route thrashing.
    void updateMediumPairPath(uint32_t gridEntries,
                              uint32_t occupiedCells) noexcept;
    [[nodiscard]] bool encode(WGPUCommandEncoder encoder);
    [[nodiscard]] bool encode(
        WGPUCommandEncoder encoder,
        const ProfilingBoundary& profilingBoundary);

    [[nodiscard]] WGPUBuffer sortedGridEntries() const noexcept;
    [[nodiscard]] WGPUBuffer occupiedCellRanges() const noexcept;
    [[nodiscard]] WGPUBuffer uniquePairs() const noexcept;
    [[nodiscard]] WGPUBuffer persistentContacts() const noexcept;
    [[nodiscard]] WGPUBuffer contactEvents() const noexcept;
    [[nodiscard]] WGPUBuffer telemetryBuffer() const noexcept;
    [[nodiscard]] uint32_t gridEntryCapacity() const noexcept;
    [[nodiscard]] uint32_t candidatePairCapacity() const noexcept;
    [[nodiscard]] uint32_t pairCapacity() const noexcept;
    [[nodiscard]] uint32_t contactCapacity() const noexcept;
    [[nodiscard]] size_t scratchBytes() const noexcept;

    [[nodiscard]] static GpuBroadPhaseTelemetry decodeTelemetry(
        std::span<const uint32_t> words) noexcept;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

static_assert(sizeof(GpuCellRange) == 16);
static_assert(sizeof(GpuPersistentContact) == 32);
static_assert(sizeof(GpuContactEvent) == 16);

} // namespace voxy::physics
