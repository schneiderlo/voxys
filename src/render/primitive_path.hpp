#pragma once

#include "physics/physics_world.hpp"
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
    WGPUTextureFormat colorFormat = WGPUTextureFormat_BGRA8Unorm;
    WGPUTextureFormat depthFormat = WGPUTextureFormat_Depth32Float;
};

struct PrimitiveUploadStats {
    size_t bytesUploaded = 0;
    uint32_t writeCalls = 0;
    bool fullUpload = false;
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
    [[nodiscard]] const PrimitiveUploadStats& lastUploadStats() const noexcept {
        return lastUploadStats_;
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
    [[nodiscard]] bool ensureInstanceCapacity(size_t requiredCapacity);
    void updateBindGroup();

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
    std::array<DrawRange, static_cast<size_t>(physics::PhysicsWorld::ThrowableShape::Count)> ranges_{};
    detail::PrimitiveInstanceCache instanceCache_;
    std::vector<uint64_t> uploadedInstanceCacheTokens_;
    PrimitiveUploadStats lastUploadStats_;
    size_t instanceCapacity_ = 0;
    uint32_t instanceCount_ = 0;
    bool instanceBufferContentsValid_ = false;
};

} // namespace voxy::render
