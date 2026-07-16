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

inline constexpr uint32_t kGpuNarrowPhasePairClassCount = 10;

enum class GpuNarrowPairClass : uint32_t {
    SphereSphere = 0,
    SphereCapsule,
    CapsuleCapsule,
    SphereBox,
    CapsuleBox,
    BoxBox,
    SphereCylinder,
    CapsuleCylinder,
    BoxCylinder,
    CylinderCylinder,
};

struct GpuNarrowPhaseInput {
    WGPUBuffer poseBuffer = nullptr;
    WGPUBuffer shapeBuffer = nullptr;
    WGPUBuffer uniquePairBuffer = nullptr;
    WGPUBuffer broadPhaseTelemetryBuffer = nullptr;
    uint32_t bodyCapacity = 0;
    uint32_t pairCapacity = 0;
    // Appended to preserve the positional layout of the original input API.
    WGPUBuffer metadataBuffer = nullptr;

    [[nodiscard]] bool valid() const noexcept {
        return poseBuffer && shapeBuffer && metadataBuffer && uniquePairBuffer
            && broadPhaseTelemetryBuffer && bodyCapacity != 0
            && pairCapacity != 0;
    }
};

struct alignas(16) GpuManifoldPoint {
    // xyz: body-local anchor; w: base separation.
    std::array<float, 4> localAnchorASeparation{};
    // xyz: body-local anchor; w: accumulated normal impulse.
    std::array<float, 4> localAnchorBNormalImpulse{};
    // feature A, feature B, lifetime, flags.
    std::array<uint32_t, 4> features{};
    // total normal impulse and reserved solver state.
    std::array<float, 4> impulses{};
};

struct alignas(16) GpuContactManifold {
    GpuKeyValue pair{};
    // point count, pair class, flags, manifold lifetime.
    std::array<uint32_t, 4> state{};
    // xyz vectors. The w lanes hold aggregate solver state.
    std::array<float, 4> normal{};
    std::array<float, 4> tangent1{};
    std::array<float, 4> tangent2{};
    std::array<float, 4> frictionAnchorA{};
    std::array<float, 4> frictionAnchorB{};
    std::array<float, 4> rollingImpulse{};
    std::array<GpuManifoldPoint, 4> points{};
};

struct GpuNarrowPhaseTelemetry {
    std::array<uint32_t, kGpuNarrowPhasePairClassCount> pairClasses{};
    uint32_t inputPairs = 0;
    uint32_t manifolds = 0;
    uint32_t manifoldPoints = 0;
    uint32_t speculativeManifolds = 0;
    uint32_t matchedFeaturePoints = 0;
    uint32_t recycledAnchorPoints = 0;
    uint32_t invalidManifolds = 0;
    uint32_t highInputPairs = 0;
    uint32_t highManifolds = 0;
    uint32_t highManifoldPoints = 0;
    uint32_t tick = 0;
    bool pairOverflow = false;
};

class GpuNarrowPhase {
public:
    static constexpr uint32_t kTelemetryWordCount = 32;
    static constexpr uint32_t kProfilingInternalBoundaryCount = 1;
    static constexpr uint64_t kActiveContactDispatchOffset =
        uint64_t{kGpuNarrowPhasePairClassCount} * 4u * sizeof(uint32_t);

    struct ProfilingBoundary {
        void (*callback)(const void*) = nullptr;
        const void* userData = nullptr;
    };

    struct Config {
        uint32_t pairCapacity = 65'536;
        // Zero preserves the historical behavior: one manifold slot per pair.
        uint32_t manifoldCapacity = 0;
        // Zero uses manifoldCapacity. The composed backend caps this to the
        // downstream solver's contact capacity.
        uint32_t dispatchContactCapacity = 0;
        uint32_t workgroupSize = 128;
        float linearSlop = 0.005f;
        float speculativeDistance = 0.02f;
        float recycleDistance = 0.05f;
        std::filesystem::path shaderPath = "shaders/physics_narrow_phase.wgsl";
    };

    GpuNarrowPhase();
    ~GpuNarrowPhase();
    GpuNarrowPhase(const GpuNarrowPhase&) = delete;
    GpuNarrowPhase& operator=(const GpuNarrowPhase&) = delete;
    GpuNarrowPhase(GpuNarrowPhase&&) noexcept;
    GpuNarrowPhase& operator=(GpuNarrowPhase&&) noexcept;

    [[nodiscard]] bool initialize(WGPUDevice device, WGPUQueue queue,
                                  const Config& config);
    void shutdown();
    void setInput(const GpuNarrowPhaseInput& input);
    [[nodiscard]] bool encode(WGPUCommandEncoder encoder);
    [[nodiscard]] bool encode(
        WGPUCommandEncoder encoder,
        const ProfilingBoundary& profilingBoundary);

    [[nodiscard]] WGPUBuffer manifolds() const noexcept;
    [[nodiscard]] WGPUBuffer pairBuckets() const noexcept;
    [[nodiscard]] WGPUBuffer pairClassTable() const noexcept;
    [[nodiscard]] WGPUBuffer activeContactDispatchBuffer() const noexcept;
    [[nodiscard]] WGPUBuffer telemetryBuffer() const noexcept;
    [[nodiscard]] uint32_t capacity() const noexcept;
    [[nodiscard]] size_t scratchBytes() const noexcept;
    // Monotonic since initialization; used to guard steady-state cache reuse.
    [[nodiscard]] size_t inputBindGroupCacheMisses() const noexcept;

    [[nodiscard]] static GpuNarrowPhaseTelemetry decodeTelemetry(
        std::span<const uint32_t> words) noexcept;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

static_assert(sizeof(GpuManifoldPoint) == 64);
static_assert(sizeof(GpuContactManifold) == 384);

} // namespace voxy::physics
