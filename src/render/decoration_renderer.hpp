// ═══════════════════════════════════════════════════════════════════════════════
// decoration_renderer.hpp - Instanced Terrain Decoration Renderer (C++20)
// ═══════════════════════════════════════════════════════════════════════════════

#pragma once

#include <cstdint>
#include <filesystem>
#include <span>
#include <vector>

#include <glm/glm.hpp>

#if defined(VOXY_WASM)
    #include <webgpu/webgpu.h>
#else
    #include <webgpu.h>
#endif

#include "terrain/decorations.hpp"

namespace voxy::render {

struct CameraUniforms;

struct DecorationRendererConfig {
    std::filesystem::path shaderPath;
    WGPUTextureFormat colorFormat = WGPUTextureFormat_BGRA8Unorm;
    WGPUTextureFormat depthFormat = WGPUTextureFormat_Depth32Float;
    uint32_t maxInstances = 22000;
    uint32_t maxVisibleInstances = 1800;
    float visibleDistance = 650.0f;
    float rayDepthBias = 0.9f;
    float fadeStart = 420.0f;
    float fadeEnd = 650.0f;

    static DecorationRendererConfig defaults() {
        return DecorationRendererConfig{
            .shaderPath = "shaders/decorations.wgsl",
        };
    }
};

class DecorationRenderer {
public:
    DecorationRenderer() = default;
    ~DecorationRenderer();

    DecorationRenderer(const DecorationRenderer&) = delete;
    DecorationRenderer& operator=(const DecorationRenderer&) = delete;

    DecorationRenderer(DecorationRenderer&& other) noexcept;
    DecorationRenderer& operator=(DecorationRenderer&& other) noexcept;

    [[nodiscard]] bool init(WGPUDevice device, WGPUQueue queue,
                            const DecorationRendererConfig& config = DecorationRendererConfig::defaults());
    void shutdown();

    [[nodiscard]] bool isInitialized() const noexcept { return pipelineRayDepth_ != nullptr; }
    [[nodiscard]] uint32_t getInstanceCount() const noexcept { return instanceCount_; }

    [[nodiscard]] bool uploadDecorations(std::span<const terrain::TerrainDecoration> decorations);
    [[nodiscard]] bool uploadTrees(std::span<const terrain::TreeDecoration> trees) {
        return uploadDecorations(trees);
    }

    void setCameraUniforms(const CameraUniforms& uniforms);
    void setRaycastDepthTexture(WGPUTextureView depthView);

    void renderRaycast(WGPUCommandEncoder encoder, WGPUTextureView colorView);
    void renderWithDepth(WGPUCommandEncoder encoder, WGPUTextureView colorView, WGPUTextureView depthView);

private:
    struct GPUInstance {
        glm::vec4 baseRadius;
        glm::vec4 colorHeight;
        glm::vec4 variant;
    };

    struct DecorationParams {
        uint32_t useRayDepth = 0;
        float rayDepthBias = 0.9f;
        float fadeStart = 900.0f;
        float fadeEnd = 1700.0f;
    };

    static_assert(sizeof(GPUInstance) == 48, "Decoration GPU instance must match WGSL layout");
    static_assert(sizeof(DecorationParams) == 16, "Decoration params must match WGSL layout");

    bool createUniformBuffers();
    bool createFallbackDepthTexture();
    bool createBindGroupLayout();
    bool createPipelines(const DecorationRendererConfig& config);
    bool createBindGroup();
    void updateCameraBuffer();
    void updateParamsBuffer(uint32_t useRayDepth);
    void rebuildVisibleInstances();
    void releasePipelines();

    WGPUDevice device_ = nullptr;
    WGPUQueue queue_ = nullptr;

    WGPUShaderModule shaderModule_ = nullptr;
    WGPUPipelineLayout pipelineLayout_ = nullptr;
    WGPURenderPipeline pipelineRayDepth_ = nullptr;
    WGPURenderPipeline pipelineHardwareDepth_ = nullptr;

    WGPUBindGroupLayout bindGroupLayout_ = nullptr;
    WGPUBindGroup bindGroup_ = nullptr;

    WGPUBuffer cameraBuffer_ = nullptr;
    WGPUBuffer paramsBuffer_ = nullptr;
    WGPUBuffer instanceBuffer_ = nullptr;

    WGPUTexture fallbackDepthTexture_ = nullptr;
    WGPUTextureView fallbackDepthView_ = nullptr;
    WGPUTextureView raycastDepthView_ = nullptr;

    CameraUniforms* cameraUniforms_ = nullptr;
    DecorationParams params_{};
    DecorationRendererConfig config_ = DecorationRendererConfig::defaults();
    std::vector<GPUInstance> allInstances_;
    std::vector<GPUInstance> visibleInstances_;
    uint32_t instanceCount_ = 0;
    uint32_t visibleInstanceCount_ = 0;
    uint32_t instanceBufferCapacity_ = 0;
    bool cameraDirty_ = true;
    bool paramsDirty_ = true;
    bool bindGroupDirty_ = true;
};

} // namespace voxy::render
