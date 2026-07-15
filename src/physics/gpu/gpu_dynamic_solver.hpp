#pragma once

#include "gpu/webgpu_compat.hpp"
#include "physics/gpu/gpu_narrow_phase.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>

namespace voxy::physics {

inline constexpr uint32_t kGpuSolverMaximumColors = 32;

struct GpuDynamicSolverInput {
    WGPUBuffer poseBuffer = nullptr;
    WGPUBuffer motionBuffer = nullptr;
    WGPUBuffer shapeBuffer = nullptr;
    WGPUBuffer metadataBuffer = nullptr;
    WGPUBuffer manifoldBuffer = nullptr;
    WGPUBuffer narrowPhaseTelemetryBuffer = nullptr;
    uint32_t bodyCapacity = 0;
    uint32_t contactCapacity = 0;

    [[nodiscard]] bool valid() const noexcept {
        return poseBuffer && motionBuffer && shapeBuffer && metadataBuffer
            && manifoldBuffer && narrowPhaseTelemetryBuffer
            && bodyCapacity != 0 && contactCapacity != 0;
    }
};

struct alignas(16) GpuConstraintCache {
    std::array<float, 4> normalMass{};
    std::array<float, 4> preImpactVelocity{};
    // Inverse 2x2 tangent effective mass: m00, m01, m11, unused.
    std::array<float, 4> tangentMass{};
    // Twist mass, rolling mass, friction radius, unused.
    std::array<float, 4> angularMass{};
    // Bias rate, mass scale, impulse scale, unused.
    std::array<float, 4> softness{};
    // Constant world-sector offset from body A's local frame to body B's.
    // Local centers still advance between Soft Step substeps.
    std::array<float, 4> sectorOffset{};
};

struct alignas(16) GpuEndpointDelta {
    std::array<float, 4> linear{};
    std::array<float, 4> angular{};
};

struct GpuDynamicSolverTelemetry {
    std::array<uint32_t, kGpuSolverMaximumColors> colorCounts{};
    uint32_t contactCount = 0;
    uint32_t coloredContacts = 0;
    uint32_t overflowContacts = 0;
    uint32_t persistentColorsRetained = 0;
    uint32_t maximumBodyDegree = 0;
    uint32_t conflictErrors = 0;
    uint32_t overflowIterations = 0;
    uint32_t highContacts = 0;
    uint32_t highOverflow = 0;
    uint32_t tick = 0;
    bool contactOverflow = false;
    uint32_t smallIslandContacts = 0;
    uint32_t smallIslandBodies = 0;
};

class GpuDynamicSolver {
public:
    static constexpr uint32_t kTelemetryWordCount = 64;

    struct Config {
        uint32_t bodyCapacity = 16'384;
        uint32_t contactCapacity = 65'536;
        uint32_t colorCount = 32;
        uint32_t workgroupSize = 128;
        uint32_t substeps = 4;
        uint32_t overflowIterations = 2;
        // Deterministic two-body/one-contact island specialization. Larger
        // islands fall back to the global color solver.
        bool enableSmallIslandFastPath = true;
        float tickSeconds = 1.0f / 60.0f;
        std::array<float, 3> gravity{0.0f, -9.81f, 0.0f};
        float linearDamping = 0.05f;
        float angularDamping = 0.05f;
        float linearSlop = 0.005f;
        float speculativeDistance = 0.02f;
        float biasRate = 0.2f;
        float maximumPushSpeed = 3.0f;
        float friction = 0.65f;
        float restitution = 0.2f;
        float rollingResistance = 0.01f;
        float restitutionThreshold = 1.0f;
        std::filesystem::path shaderPath = "shaders/physics_dynamic_solver.wgsl";
        std::filesystem::path primitivesShaderPath =
            "shaders/physics_deterministic_primitives.wgsl";
    };

    GpuDynamicSolver();
    ~GpuDynamicSolver();
    GpuDynamicSolver(const GpuDynamicSolver&) = delete;
    GpuDynamicSolver& operator=(const GpuDynamicSolver&) = delete;
    GpuDynamicSolver(GpuDynamicSolver&&) noexcept;
    GpuDynamicSolver& operator=(GpuDynamicSolver&&) noexcept;

    [[nodiscard]] bool initialize(WGPUDevice device, WGPUQueue queue,
                                  const Config& config);
    void shutdown();
    void setInput(const GpuDynamicSolverInput& input);
    [[nodiscard]] bool encode(WGPUCommandEncoder encoder,
                              bool compactColorSolve = false);

    [[nodiscard]] WGPUBuffer colors() const noexcept;
    [[nodiscard]] WGPUBuffer sortedColorRecords() const noexcept;
    [[nodiscard]] WGPUBuffer colorRanges() const noexcept;
    [[nodiscard]] WGPUBuffer constraintCache() const noexcept;
    [[nodiscard]] WGPUBuffer telemetryBuffer() const noexcept;
    [[nodiscard]] uint32_t colorCount() const noexcept;
    [[nodiscard]] size_t scratchBytes() const noexcept;

    [[nodiscard]] static GpuDynamicSolverTelemetry decodeTelemetry(
        std::span<const uint32_t> words) noexcept;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

static_assert(sizeof(GpuConstraintCache) == 96);
static_assert(sizeof(GpuEndpointDelta) == 32);

} // namespace voxy::physics
