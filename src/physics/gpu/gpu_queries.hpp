#pragma once

#include "gpu/shader_source.hpp"
#include "gpu/webgpu_compat.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <vector>

namespace voxy::physics {

inline constexpr uint32_t kGpuQueryMaximumHits = 16;
// Keeps query-frame f32 coordinates within roughly one million metres while
// still allowing explicit casts across thousands of world sectors.
inline constexpr uint32_t kGpuQueryMaximumSectorDelta = 4096;

enum class GpuQueryType : uint32_t {
    RayCast = 0,
    OverlapSphere = 1,
    SphereCast = 2,
    CapsuleCast = 3,
};

struct alignas(16) GpuQueryRequest {
    // request ID, type, maximum hits, flags.
    std::array<uint32_t, 4> ids{};
    // xyz origin/center, w overlap or cast radius.
    std::array<float, 4> originRadius{};
    // xyz normalized or unnormalized direction, w maximum distance.
    std::array<float, 4> directionDistance{};
    // Capsule local axis xyz, w capsule half-height.
    std::array<float, 4> dimensions{};
    // Query-frame world sector xyz, maximum body-sector delta.
    std::array<int32_t, 4> sector{};
};

struct alignas(16) GpuQueryHit {
    // request ID, body ID, feature ID, query type.
    std::array<uint32_t, 4> ids{};
    // sort metric/fraction, world distance, reserved, reserved.
    std::array<float, 4> metricDistance{};
    std::array<float, 4> point{};
    std::array<float, 4> normal{};
    // Query-frame sector xyz, body generation in w.
    std::array<int32_t, 4> sector{};
};

struct alignas(16) GpuQueryOutput {
    // request ID, hit count, status bits, query type.
    // bit 0: hit capacity; bit 1: invalid/missing authored shape;
    // bit 2: authored cast did not converge. Nonzero is an incomplete result.
    std::array<uint32_t, 4> header{};
    std::array<GpuQueryHit, kGpuQueryMaximumHits> hits{};
};

struct GpuQueryBodyView {
    WGPUBuffer poseBuffer = nullptr;
    WGPUBuffer shapeBuffer = nullptr;
    WGPUBuffer metadataBuffer = nullptr;
    uint32_t bodyCapacity = 0;
    // Borrow only within the shape owner's declared submission. An absent
    // atlas supports primitives only; tagged bodies report incomplete output.
    WGPUBuffer authoredShapeBuffer = nullptr;

    [[nodiscard]] bool valid() const noexcept {
        return poseBuffer && shapeBuffer && metadataBuffer && bodyCapacity != 0;
    }
};

struct GpuQueryBatchResult {
    uint64_t tick = 0;
    std::vector<GpuQueryOutput> outputs;
};

class GpuAsyncQuerySystem {
public:
    struct Config {
        uint32_t bodyCapacity = 16'384;
        uint32_t requestCapacity = 256;
        uint32_t readbackSlots = 3;
        std::filesystem::path shaderPath = "shaders/physics_queries.wgsl";
        std::span<const gpu::ShaderSource> shaderSources{};
    };

    GpuAsyncQuerySystem();
    ~GpuAsyncQuerySystem();
    GpuAsyncQuerySystem(const GpuAsyncQuerySystem&) = delete;
    GpuAsyncQuerySystem& operator=(const GpuAsyncQuerySystem&) = delete;
    GpuAsyncQuerySystem(GpuAsyncQuerySystem&&) noexcept;
    GpuAsyncQuerySystem& operator=(GpuAsyncQuerySystem&&) noexcept;

    [[nodiscard]] bool initialize(WGPUDevice device, WGPUQueue queue,
                                  const Config& config);
    void shutdown();
    void setBodyView(const GpuQueryBodyView& view);
    [[nodiscard]] bool submit(std::span<const GpuQueryRequest> requests,
                              uint64_t tick);
    [[nodiscard]] bool encode(WGPUCommandEncoder encoder);
    [[nodiscard]] std::optional<GpuQueryBatchResult> poll();

    [[nodiscard]] WGPUBuffer outputBuffer() const noexcept;
    [[nodiscard]] size_t allocatedBytes() const noexcept;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

static_assert(sizeof(GpuQueryRequest) == 80);
static_assert(sizeof(GpuQueryHit) == 80);
static_assert(sizeof(GpuQueryOutput) == 1296);

} // namespace voxy::physics
