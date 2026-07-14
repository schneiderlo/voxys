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

#if defined(VOXY_WASM)
    #include <webgpu/webgpu.h>
#else
    #include <webgpu.h>
#endif

namespace voxy::render {

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
                bool useRayDepth);

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
    WGPUBuffer cpuPoseBuffer_ = nullptr;
    WGPUBuffer cpuShapeBuffer_ = nullptr;
    WGPUBuffer compactBoundPoseBuffer_ = nullptr;
    WGPUBuffer compactBoundShapeBuffer_ = nullptr;
    WGPUBuffer compactBoundVisibleBuffer_ = nullptr;
    WGPUTextureView compactBoundRayDepthView_ = nullptr;
    physics::PhysicsRenderView physicsRenderView_{};
    PrimitiveGpuCulling gpuCulling_;
    std::array<DrawRange, static_cast<size_t>(physics::PhysicsWorld::ThrowableShape::Count)> ranges_{};
    detail::PrimitiveInstanceCache instanceCache_;
    std::vector<uint64_t> uploadedInstanceCacheTokens_;
    PrimitiveUploadStats lastUploadStats_;
    PrimitiveUploadStats lastCompactUploadStats_;
    PrimitiveCpuTimings lastCpuTimings_;
    size_t instanceCapacity_ = 0;
    size_t compactInstanceCapacity_ = 0;
    uint32_t instanceCount_ = 0;
    bool instanceBufferContentsValid_ = false;
};

} // namespace voxy::render
