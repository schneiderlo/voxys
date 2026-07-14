#pragma once

#include "gpu/webgpu_compat.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>

namespace voxy::physics {

inline constexpr uint32_t kGpuBodyBulletFlag = 1u << 0u;
inline constexpr uint32_t kGpuBodyCcdHitFlag = 1u << 1u;
inline constexpr uint32_t kGpuBodyCcdFailureFlag = 1u << 2u;

struct GpuCcdInput {
    WGPUBuffer poseBuffer = nullptr;
    WGPUBuffer motionBuffer = nullptr;
    WGPUBuffer shapeBuffer = nullptr;
    WGPUBuffer metadataBuffer = nullptr;
    WGPUTextureView terrainTexture = nullptr;
    uint32_t bodyCapacity = 0;
    uint32_t terrainWidth = 0;
    uint32_t terrainHeight = 0;
    float terrainHeightScale = 0.0f;
    float terrainCellScale = 0.0f;

    [[nodiscard]] bool valid() const noexcept {
        return poseBuffer && motionBuffer && shapeBuffer && metadataBuffer
            && terrainTexture && bodyCapacity != 0 && terrainWidth >= 2
            && terrainHeight >= 2 && terrainHeightScale > 0.0f
            && terrainCellScale > 0.0f;
    }
};

struct GpuCcdTelemetry {
    uint32_t fastCandidates = 0;
    uint32_t bulletRequested = 0;
    uint32_t bulletProcessed = 0;
    uint32_t bulletOverflow = 0;
    uint32_t hits = 0;
    uint32_t stalls = 0;
    uint32_t failures = 0;
    uint32_t maximumIterations = 0;
    uint32_t highFastCandidates = 0;
    uint32_t highBulletRequested = 0;
    uint32_t highBulletOverflow = 0;
    uint32_t highHits = 0;
    uint32_t highStalls = 0;
    uint32_t highFailures = 0;
    uint32_t tick = 0;
};

class GpuCcd {
public:
    static constexpr uint32_t kTelemetryWordCount = 32;

    struct Config {
        uint32_t bodyCapacity = 16'384;
        uint32_t bulletCapacity = 1'024;
        uint32_t workgroupSize = 128;
        uint32_t coarseSteps = 16;
        uint32_t bisectionIterations = 8;
        float fastDistanceRatio = 0.5f;
        float linearSlop = 0.005f;
        std::filesystem::path shaderPath = "shaders/physics_ccd.wgsl";
        std::filesystem::path primitivesShaderPath =
            "shaders/physics_deterministic_primitives.wgsl";
    };

    GpuCcd();
    ~GpuCcd();
    GpuCcd(const GpuCcd&) = delete;
    GpuCcd& operator=(const GpuCcd&) = delete;
    GpuCcd(GpuCcd&&) noexcept;
    GpuCcd& operator=(GpuCcd&&) noexcept;

    [[nodiscard]] bool initialize(WGPUDevice device, WGPUQueue queue,
                                  const Config& config);
    void shutdown();
    void setInput(const GpuCcdInput& input);
    [[nodiscard]] bool encode(WGPUCommandEncoder encoder, float deltaTime);

    [[nodiscard]] WGPUBuffer bulletBodyIds() const noexcept;
    [[nodiscard]] WGPUBuffer telemetryBuffer() const noexcept;
    [[nodiscard]] size_t allocatedBytes() const noexcept;

    [[nodiscard]] static GpuCcdTelemetry decodeTelemetry(
        std::span<const uint32_t> words) noexcept;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace voxy::physics
