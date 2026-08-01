#pragma once

#include "physics/physics_world.hpp"
#include "render/primitive_gpu_culling.hpp"
#include "render/primitive_instance_packing.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <vector>

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#if defined(VOXY_WASM)
    #include <webgpu/webgpu.h>
#else
    #include <webgpu.h>
#endif

namespace voxy::render {

namespace detail {

// Exact CPU mirror of BodyShape in physics_primitives_compact.wgsl and the
// persistent WebGpuSoft shape buffer. Keeping the upload typed prevents a
// packed array of two vec4s from silently walking a three-vec4 GPU stride.
struct alignas(16) CompactPrimitiveShapeGpu {
    glm::vec4 dimensionsType{0.0f};
    glm::vec4 inverseInertiaMaterial{0.0f};
    glm::vec4 materialCoefficients{-1.0f, -1.0f, -1.0f, 1.0f};
};

static_assert(sizeof(CompactPrimitiveShapeGpu) == 48u);
static_assert(alignof(CompactPrimitiveShapeGpu) == 16u);

} // namespace detail

struct PrimitivePathConfig {
    std::filesystem::path shaderPath = "shaders/physics_primitives.wgsl";
    std::filesystem::path compactShaderPath;
    std::filesystem::path cullShaderPath;
    WGPUTextureFormat colorFormat = WGPUTextureFormat_BGRA8Unorm;
    WGPUTextureFormat depthFormat = WGPUTextureFormat_Depth32Float;
};

struct PrimitiveUploadStats {
    size_t bytesUploaded = 0;
    uint32_t writeCalls = 0;
    bool fullUpload = false;
};

struct PrimitiveCpuTimings {
    double packingMs = 0.0;
    double uploadMs = 0.0;
};

/// Frame-wide lighting shared with terrain and water presentation.
struct PrimitiveLighting {
    glm::vec3 direction{0.3f, 0.8f, 0.4f};
    glm::vec3 sunColor{1.0f, 0.95f, 0.9f};
    float sunIntensity = 1.0f;
    glm::vec3 ambientColor{0.1f, 0.12f, 0.15f};
    float ambientIntensity = 1.3f;
    glm::vec3 fogColor{0.36f, 0.58f, 0.64f};
    float fogDensity = 0.0001f;
    float exposure = 1.0f;
};

class PrimitivePath {
public:
    PrimitivePath() = default;
    ~PrimitivePath();

    PrimitivePath(const PrimitivePath&) = delete;
    PrimitivePath& operator=(const PrimitivePath&) = delete;

    [[nodiscard]] bool init(WGPUDevice device, WGPUQueue queue,
                            const PrimitivePathConfig& config = {});
    void shutdown();
    [[nodiscard]] bool isInitialized() const noexcept { return pipeline_ != nullptr; }

    void setRayDepthTexture(WGPUTextureView view);
    void setInstances(std::span<const physics::PhysicsWorld::DynamicBodySnapshot> bodies);
    void setCompactPhysicsInstances(
        std::span<const physics::PhysicsWorld::DynamicBodySnapshot> bodies);
    void setPhysicsRenderView(const physics::PhysicsRenderView& view);
    void clearPhysicsRenderView();
    [[nodiscard]] const PrimitiveUploadStats& lastUploadStats() const noexcept {
        return lastUploadStats_;
    }
    [[nodiscard]] const PrimitiveCpuTimings& lastCpuTimings() const noexcept {
        return lastCpuTimings_;
    }
    [[nodiscard]] const PrimitiveUploadStats& lastCompactUploadStats() const noexcept {
        return lastCompactUploadStats_;
    }

    void render(WGPUCommandEncoder encoder, WGPUTextureView colorView,
                WGPUTextureView depthView, const glm::mat4& view,
                const glm::mat4& projection, const glm::vec3& cameraPosition,
                const glm::vec3& lightDirection, uint32_t width, uint32_t height,
                bool useRayDepth,
                const glm::ivec3& cameraSector = glm::ivec3(0),
                WGPUQuerySet timestampQuerySet = nullptr,
                uint32_t timestampBegin = WGPU_QUERY_SET_INDEX_UNDEFINED,
                uint32_t timestampEnd = WGPU_QUERY_SET_INDEX_UNDEFINED);

    void render(WGPUCommandEncoder encoder, WGPUTextureView colorView,
                WGPUTextureView depthView, const glm::mat4& view,
                const glm::mat4& projection, const glm::vec3& cameraPosition,
                const PrimitiveLighting& lighting, uint32_t width,
                uint32_t height, bool useRayDepth,
                const glm::ivec3& cameraSector = glm::ivec3(0),
                WGPUQuerySet timestampQuerySet = nullptr,
                uint32_t timestampBegin = WGPU_QUERY_SET_INDEX_UNDEFINED,
                uint32_t timestampEnd = WGPU_QUERY_SET_INDEX_UNDEFINED);

private:
    struct DrawRange {
        uint32_t firstIndex = 0;
        uint32_t indexCount = 0;
        uint32_t firstInstance = 0;
        uint32_t instanceCount = 0;
    };

    [[nodiscard]] bool createGeometry();
    [[nodiscard]] bool createBuffers();
    [[nodiscard]] bool createLayoutAndPipeline(const PrimitivePathConfig& config);
    [[nodiscard]] bool createCompactLayoutAndPipeline(
        const PrimitivePathConfig& config);
    [[nodiscard]] bool ensureInstanceCapacity(size_t requiredCapacity);
    [[nodiscard]] bool ensureCompactInstanceCapacity(size_t requiredCapacity);
    void updateBindGroup();
    void updateCompactBindGroup();
    [[nodiscard]] bool rebuildCompactRenderBundle();

    WGPUDevice device_ = nullptr;
    WGPUQueue queue_ = nullptr;
    WGPUShaderModule shaderModule_ = nullptr;
    WGPUBindGroupLayout bindGroupLayout_ = nullptr;
    WGPUPipelineLayout pipelineLayout_ = nullptr;
    WGPURenderPipeline pipeline_ = nullptr;
    WGPUBindGroup bindGroup_ = nullptr;
    WGPUBuffer vertexBuffer_ = nullptr;
    WGPUBuffer indexBuffer_ = nullptr;
    WGPUBuffer uniformBuffer_ = nullptr;
    WGPUBuffer instanceBuffer_ = nullptr;
    WGPUTextureView rayDepthView_ = nullptr;
    WGPUTextureView boundRayDepthView_ = nullptr;
    WGPUShaderModule compactShaderModule_ = nullptr;
    WGPUBindGroupLayout compactBindGroupLayout_ = nullptr;
    WGPUPipelineLayout compactPipelineLayout_ = nullptr;
    WGPURenderPipeline compactPipeline_ = nullptr;
    WGPUBindGroup compactBindGroup_ = nullptr;
    WGPURenderBundle compactRenderBundle_ = nullptr;
    WGPUBuffer cpuPoseBuffer_ = nullptr;
    WGPUBuffer cpuShapeBuffer_ = nullptr;
    WGPUBuffer compactBoundPoseBuffer_ = nullptr;
    WGPUBuffer compactBoundShapeBuffer_ = nullptr;
    WGPUBuffer compactBoundVisibleBuffer_ = nullptr;
    uint32_t compactBoundVisibleSegmentCapacity_ = 0;
    WGPUTextureView compactBoundRayDepthView_ = nullptr;
    physics::PhysicsRenderView physicsRenderView_{};
    PrimitiveGpuCulling gpuCulling_;
    std::array<DrawRange, static_cast<size_t>(physics::PhysicsWorld::ThrowableShape::Count)> ranges_{};
    detail::PrimitiveInstanceCache instanceCache_;
    std::vector<uint64_t> uploadedInstanceCacheTokens_;
    // Reused CPU fallback staging. Poses change every frame; shapes usually do
    // not, so their uploaded copy is tracked independently.
    std::vector<glm::vec4> cpuPoseUpload_;
    std::vector<detail::CompactPrimitiveShapeGpu> cpuShapeUpload_;
    std::vector<glm::vec4> uploadedCpuShapeDimensions_;
    PrimitiveUploadStats lastUploadStats_;
    PrimitiveUploadStats lastCompactUploadStats_;
    PrimitiveCpuTimings lastCpuTimings_;
    size_t instanceCapacity_ = 0;
    size_t compactInstanceCapacity_ = 0;
    uint32_t instanceCount_ = 0;
    bool instanceBufferContentsValid_ = false;
    bool cpuShapeBufferContentsValid_ = false;
    WGPUTextureFormat colorFormat_ = WGPUTextureFormat_BGRA8Unorm;
    WGPUTextureFormat depthFormat_ = WGPUTextureFormat_Depth32Float;
};

} // namespace voxy::render
