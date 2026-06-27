// ═══════════════════════════════════════════════════════════════════════════════
// decoration_renderer.cpp - Instanced Terrain Decoration Renderer Implementation
// ═══════════════════════════════════════════════════════════════════════════════

#include "render/decoration_renderer.hpp"
#include "render/triangle_path.hpp"
#include "gpu/resources.hpp"
#include "core/log.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <utility>
#include <vector>

namespace voxy::render {

DecorationRenderer::~DecorationRenderer() {
    shutdown();
}

DecorationRenderer::DecorationRenderer(DecorationRenderer&& other) noexcept
    : device_(other.device_)
    , queue_(other.queue_)
    , shaderModule_(other.shaderModule_)
    , pipelineLayout_(other.pipelineLayout_)
    , pipelineRayDepth_(other.pipelineRayDepth_)
    , pipelineHardwareDepth_(other.pipelineHardwareDepth_)
    , bindGroupLayout_(other.bindGroupLayout_)
    , bindGroup_(other.bindGroup_)
    , cameraBuffer_(other.cameraBuffer_)
    , paramsBuffer_(other.paramsBuffer_)
    , instanceBuffer_(other.instanceBuffer_)
    , fallbackDepthTexture_(other.fallbackDepthTexture_)
    , fallbackDepthView_(other.fallbackDepthView_)
    , raycastDepthView_(other.raycastDepthView_)
    , cameraUniforms_(other.cameraUniforms_)
    , params_(other.params_)
    , config_(other.config_)
    , allInstances_(std::move(other.allInstances_))
    , visibleInstances_(std::move(other.visibleInstances_))
    , instanceCount_(other.instanceCount_)
    , visibleInstanceCount_(other.visibleInstanceCount_)
    , instanceBufferCapacity_(other.instanceBufferCapacity_)
    , cameraDirty_(other.cameraDirty_)
    , paramsDirty_(other.paramsDirty_)
    , bindGroupDirty_(other.bindGroupDirty_) {
    other.device_ = nullptr;
    other.queue_ = nullptr;
    other.shaderModule_ = nullptr;
    other.pipelineLayout_ = nullptr;
    other.pipelineRayDepth_ = nullptr;
    other.pipelineHardwareDepth_ = nullptr;
    other.bindGroupLayout_ = nullptr;
    other.bindGroup_ = nullptr;
    other.cameraBuffer_ = nullptr;
    other.paramsBuffer_ = nullptr;
    other.instanceBuffer_ = nullptr;
    other.fallbackDepthTexture_ = nullptr;
    other.fallbackDepthView_ = nullptr;
    other.raycastDepthView_ = nullptr;
    other.cameraUniforms_ = nullptr;
    other.instanceCount_ = 0;
    other.visibleInstanceCount_ = 0;
    other.instanceBufferCapacity_ = 0;
}

DecorationRenderer& DecorationRenderer::operator=(DecorationRenderer&& other) noexcept {
    if (this != &other) {
        shutdown();

        device_ = other.device_;
        queue_ = other.queue_;
        shaderModule_ = other.shaderModule_;
        pipelineLayout_ = other.pipelineLayout_;
        pipelineRayDepth_ = other.pipelineRayDepth_;
        pipelineHardwareDepth_ = other.pipelineHardwareDepth_;
        bindGroupLayout_ = other.bindGroupLayout_;
        bindGroup_ = other.bindGroup_;
        cameraBuffer_ = other.cameraBuffer_;
        paramsBuffer_ = other.paramsBuffer_;
        instanceBuffer_ = other.instanceBuffer_;
        fallbackDepthTexture_ = other.fallbackDepthTexture_;
        fallbackDepthView_ = other.fallbackDepthView_;
        raycastDepthView_ = other.raycastDepthView_;
        cameraUniforms_ = other.cameraUniforms_;
        params_ = other.params_;
        config_ = other.config_;
        allInstances_ = std::move(other.allInstances_);
        visibleInstances_ = std::move(other.visibleInstances_);
        instanceCount_ = other.instanceCount_;
        visibleInstanceCount_ = other.visibleInstanceCount_;
        instanceBufferCapacity_ = other.instanceBufferCapacity_;
        cameraDirty_ = other.cameraDirty_;
        paramsDirty_ = other.paramsDirty_;
        bindGroupDirty_ = other.bindGroupDirty_;

        other.device_ = nullptr;
        other.queue_ = nullptr;
        other.shaderModule_ = nullptr;
        other.pipelineLayout_ = nullptr;
        other.pipelineRayDepth_ = nullptr;
        other.pipelineHardwareDepth_ = nullptr;
        other.bindGroupLayout_ = nullptr;
        other.bindGroup_ = nullptr;
        other.cameraBuffer_ = nullptr;
        other.paramsBuffer_ = nullptr;
        other.instanceBuffer_ = nullptr;
        other.fallbackDepthTexture_ = nullptr;
        other.fallbackDepthView_ = nullptr;
        other.raycastDepthView_ = nullptr;
        other.cameraUniforms_ = nullptr;
        other.instanceCount_ = 0;
        other.visibleInstanceCount_ = 0;
        other.instanceBufferCapacity_ = 0;
    }
    return *this;
}

bool DecorationRenderer::init(WGPUDevice device, WGPUQueue queue,
                              const DecorationRendererConfig& config) {
    LOG_SCOPE("DecorationRenderer::init");

    if (!device || !queue) {
        LOG_ERROR("DecorationRenderer::init: device or queue is null");
        return false;
    }

    device_ = device;
    queue_ = queue;
    config_ = config;
    params_.rayDepthBias = config.rayDepthBias;
    params_.fadeStart = config.fadeStart;
    params_.fadeEnd = config.fadeEnd;
    cameraUniforms_ = new CameraUniforms();

    if (!createUniformBuffers() ||
        !createFallbackDepthTexture() ||
        !createBindGroupLayout() ||
        !createPipelines(config)) {
        shutdown();
        return false;
    }

    LOG_INFO("DecorationRenderer initialized");
    return true;
}

void DecorationRenderer::shutdown() {
    if (bindGroup_) {
        wgpuBindGroupRelease(bindGroup_);
        bindGroup_ = nullptr;
    }
    if (bindGroupLayout_) {
        wgpuBindGroupLayoutRelease(bindGroupLayout_);
        bindGroupLayout_ = nullptr;
    }
    releasePipelines();
    if (pipelineLayout_) {
        wgpuPipelineLayoutRelease(pipelineLayout_);
        pipelineLayout_ = nullptr;
    }
    if (shaderModule_) {
        wgpuShaderModuleRelease(shaderModule_);
        shaderModule_ = nullptr;
    }
    if (cameraBuffer_) {
        wgpuBufferRelease(cameraBuffer_);
        cameraBuffer_ = nullptr;
    }
    if (paramsBuffer_) {
        wgpuBufferRelease(paramsBuffer_);
        paramsBuffer_ = nullptr;
    }
    if (instanceBuffer_) {
        wgpuBufferRelease(instanceBuffer_);
        instanceBuffer_ = nullptr;
    }
    if (fallbackDepthView_) {
        wgpuTextureViewRelease(fallbackDepthView_);
        fallbackDepthView_ = nullptr;
    }
    if (fallbackDepthTexture_) {
        wgpuTextureDestroy(fallbackDepthTexture_);
        wgpuTextureRelease(fallbackDepthTexture_);
        fallbackDepthTexture_ = nullptr;
    }

    raycastDepthView_ = nullptr;
    delete cameraUniforms_;
    cameraUniforms_ = nullptr;
    allInstances_.clear();
    visibleInstances_.clear();
    instanceCount_ = 0;
    visibleInstanceCount_ = 0;
    instanceBufferCapacity_ = 0;
    device_ = nullptr;
    queue_ = nullptr;
}

bool DecorationRenderer::uploadTrees(std::span<const terrain::TreeDecoration> trees) {
    if (!device_ || !queue_) {
        return false;
    }

    if (instanceBuffer_) {
        wgpuBufferRelease(instanceBuffer_);
        instanceBuffer_ = nullptr;
    }
    allInstances_.clear();
    visibleInstances_.clear();
    instanceCount_ = 0;
    visibleInstanceCount_ = 0;
    instanceBufferCapacity_ = 0;

    const size_t count = std::min<size_t>(trees.size(), config_.maxInstances);
    if (count == 0) {
        bindGroupDirty_ = true;
        return true;
    }

    allInstances_.reserve(count);
    for (size_t i = 0; i < count; ++i) {
        const auto& tree = trees[i];
        allInstances_.push_back(GPUInstance{
            .baseRadius = glm::vec4(tree.x, tree.y, tree.z, tree.radius),
            .colorHeight = glm::vec4(tree.colorR, tree.colorG, tree.colorB, tree.height),
        });
    }

    instanceBufferCapacity_ = static_cast<uint32_t>(
        std::min<size_t>(allInstances_.size(), std::max(config_.maxVisibleInstances, 1u))
    );
    gpu::BufferDesc desc = gpu::BufferDesc::storage(
        static_cast<uint64_t>(instanceBufferCapacity_) * sizeof(GPUInstance),
        true,
        "decoration_visible_instances"
    );
    instanceBuffer_ = gpu::createBuffer(device_, desc);
    if (!instanceBuffer_) {
        LOG_ERROR("Failed to create decoration instance buffer");
        return false;
    }

    instanceCount_ = static_cast<uint32_t>(allInstances_.size());
    rebuildVisibleInstances();
    bindGroupDirty_ = true;
    LOG_INFO("Uploaded {} terrain decoration instances ({} visible cap)",
             instanceCount_, instanceBufferCapacity_);
    return true;
}

void DecorationRenderer::setCameraUniforms(const CameraUniforms& uniforms) {
    if (!cameraUniforms_) {
        return;
    }
    *cameraUniforms_ = uniforms;
    cameraDirty_ = true;
    rebuildVisibleInstances();
}

void DecorationRenderer::setRaycastDepthTexture(WGPUTextureView depthView) {
    raycastDepthView_ = depthView;
    bindGroupDirty_ = true;
}

bool DecorationRenderer::createUniformBuffers() {
    const uint64_t cameraSize = gpu::alignUniformBufferSize(sizeof(CameraUniforms));
    cameraBuffer_ = gpu::createBuffer(device_, gpu::BufferDesc::uniform(cameraSize, "decoration_camera_uniforms"));
    if (!cameraBuffer_) {
        return false;
    }

    const uint64_t paramsSize = gpu::alignUniformBufferSize(sizeof(DecorationParams));
    paramsBuffer_ = gpu::createBuffer(device_, gpu::BufferDesc::uniform(paramsSize, "decoration_params"));
    if (!paramsBuffer_) {
        return false;
    }

    updateCameraBuffer();
    updateParamsBuffer(0);
    return true;
}

bool DecorationRenderer::createFallbackDepthTexture() {
    gpu::TextureDesc desc = gpu::TextureDesc::tex2D(
        1,
        1,
        WGPUTextureFormat_R32Float,
        WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst,
        "decoration_fallback_depth"
    );
    fallbackDepthTexture_ = gpu::createTexture(device_, desc);
    if (!fallbackDepthTexture_) {
        return false;
    }

    const float skyDepth = -1.0f;
    gpu::writeTexture(
        queue_,
        fallbackDepthTexture_,
        std::span<const std::byte>(reinterpret_cast<const std::byte*>(&skyDepth), sizeof(skyDepth)),
        1,
        1,
        sizeof(float)
    );

    gpu::TextureViewDesc viewDesc{};
    viewDesc.label = "decoration_fallback_depth_view";
    viewDesc.format = WGPUTextureFormat_R32Float;
    fallbackDepthView_ = gpu::createTextureView(fallbackDepthTexture_, viewDesc);
    return fallbackDepthView_ != nullptr;
}

bool DecorationRenderer::createBindGroupLayout() {
    std::array<gpu::BindGroupLayoutEntry, 4> entries = {
        gpu::BindGroupLayoutEntry(0)
            .vertexVisible()
            .fragmentVisible()
            .uniformBuffer(false, sizeof(CameraUniforms)),
        gpu::BindGroupLayoutEntry(1)
            .vertexVisible()
            .storageBuffer(true),
        gpu::BindGroupLayoutEntry(2)
            .fragmentVisible()
            .texture(WGPUTextureSampleType_UnfilterableFloat, WGPUTextureViewDimension_2D, false),
        gpu::BindGroupLayoutEntry(3)
            .fragmentVisible()
            .uniformBuffer(false, sizeof(DecorationParams)),
    };

    bindGroupLayout_ = gpu::createBindGroupLayout(device_, entries, "decoration_bind_group_layout");
    return bindGroupLayout_ != nullptr;
}

bool DecorationRenderer::createPipelines(const DecorationRendererConfig& config) {
    shaderModule_ = gpu::loadShaderModule(device_, config.shaderPath, "decorations.wgsl");
    if (!shaderModule_) {
        LOG_ERROR("Failed to load decoration shader from: {}", config.shaderPath.string());
        return false;
    }

    std::array<WGPUBindGroupLayout, 1> layouts = {bindGroupLayout_};
    pipelineLayout_ = gpu::createPipelineLayout(device_, layouts, "decoration_pipeline_layout");
    if (!pipelineLayout_) {
        return false;
    }

    auto createPipeline = [&](bool useHardwareDepth, const char* label) -> WGPURenderPipeline {
        WGPUVertexState vertexState{};
        vertexState.module = shaderModule_;
        WGPU_SET_ENTRY_POINT(vertexState, "vs");
        vertexState.bufferCount = 0;
        vertexState.buffers = nullptr;

        WGPUColorTargetState colorTarget{};
        colorTarget.format = config.colorFormat;
        colorTarget.writeMask = WGPUColorWriteMask_All;
        colorTarget.blend = nullptr;

        WGPUFragmentState fragmentState{};
        fragmentState.module = shaderModule_;
        WGPU_SET_ENTRY_POINT(fragmentState, "fs");
        fragmentState.targetCount = 1;
        fragmentState.targets = &colorTarget;

        WGPUPrimitiveState primitiveState{};
        primitiveState.topology = WGPUPrimitiveTopology_TriangleList;
        primitiveState.frontFace = WGPUFrontFace_CCW;
        primitiveState.cullMode = WGPUCullMode_None;

        WGPUMultisampleState multisampleState{};
        multisampleState.count = 1;
        multisampleState.mask = ~0u;
        multisampleState.alphaToCoverageEnabled = false;

        WGPUDepthStencilState depthStencil{};
        depthStencil.format = config.depthFormat;
        depthStencil.depthWriteEnabled = gpu::toOptionalBool(false);
        depthStencil.depthCompare = WGPUCompareFunction_LessEqual;
        depthStencil.stencilFront.compare = WGPUCompareFunction_Always;
        depthStencil.stencilFront.failOp = WGPUStencilOperation_Keep;
        depthStencil.stencilFront.depthFailOp = WGPUStencilOperation_Keep;
        depthStencil.stencilFront.passOp = WGPUStencilOperation_Keep;
        depthStencil.stencilBack = depthStencil.stencilFront;
        depthStencil.stencilReadMask = 0xFFFFFFFF;
        depthStencil.stencilWriteMask = 0xFFFFFFFF;

        WGPURenderPipelineDescriptor desc{};
        WGPU_SET_LABEL(desc, label);
        desc.layout = pipelineLayout_;
        desc.vertex = vertexState;
        desc.fragment = &fragmentState;
        desc.primitive = primitiveState;
        desc.multisample = multisampleState;
        desc.depthStencil = useHardwareDepth ? &depthStencil : nullptr;

        return wgpuDeviceCreateRenderPipeline(device_, &desc);
    };

    pipelineRayDepth_ = createPipeline(false, "decoration_ray_depth_pipeline");
    pipelineHardwareDepth_ = createPipeline(true, "decoration_hardware_depth_pipeline");
    return pipelineRayDepth_ != nullptr && pipelineHardwareDepth_ != nullptr;
}

bool DecorationRenderer::createBindGroup() {
    if (!instanceBuffer_ || !cameraBuffer_ || !paramsBuffer_) {
        return false;
    }

    if (bindGroup_) {
        wgpuBindGroupRelease(bindGroup_);
        bindGroup_ = nullptr;
    }

    WGPUTextureView depthView = raycastDepthView_ ? raycastDepthView_ : fallbackDepthView_;
    std::array<gpu::BindGroupEntry, 4> entries = {
        gpu::BindGroupEntry(0).buffer(cameraBuffer_, 0, sizeof(CameraUniforms)),
        gpu::BindGroupEntry(1).buffer(instanceBuffer_, 0, WGPU_WHOLE_SIZE),
        gpu::BindGroupEntry(2).textureView(depthView),
        gpu::BindGroupEntry(3).buffer(paramsBuffer_, 0, sizeof(DecorationParams)),
    };

    bindGroup_ = gpu::createBindGroup(device_, bindGroupLayout_, entries, "decoration_bind_group");
    bindGroupDirty_ = bindGroup_ == nullptr;
    return bindGroup_ != nullptr;
}

void DecorationRenderer::updateCameraBuffer() {
    if (!queue_ || !cameraBuffer_ || !cameraUniforms_) {
        return;
    }
    gpu::writeBuffer(queue_, cameraBuffer_, 0, *cameraUniforms_);
    cameraDirty_ = false;
}

void DecorationRenderer::updateParamsBuffer(uint32_t useRayDepth) {
    if (!queue_ || !paramsBuffer_) {
        return;
    }
    params_.useRayDepth = useRayDepth;
    gpu::writeBuffer(queue_, paramsBuffer_, 0, params_);
    paramsDirty_ = false;
}

void DecorationRenderer::rebuildVisibleInstances() {
    visibleInstanceCount_ = 0;
    if (!queue_ || !instanceBuffer_ || !cameraUniforms_ || instanceBufferCapacity_ == 0) {
        return;
    }

    visibleInstances_.clear();
    visibleInstances_.reserve(instanceBufferCapacity_);

    const glm::vec3 cameraPos(cameraUniforms_->cameraPos);
    const float maxDistance = std::max(config_.visibleDistance, 1.0f);
    const float maxDistance2 = maxDistance * maxDistance;

    for (const auto& instance : allInstances_) {
        const glm::vec3 base(instance.baseRadius);
        const float height = instance.colorHeight.w;
        const float radius = std::max(instance.baseRadius.w, height * 0.45f);
        const glm::vec3 center = base + glm::vec3(0.0f, height * 0.5f, 0.0f);
        const glm::vec3 delta = center - cameraPos;
        const float distance2 = glm::dot(delta, delta);
        if (distance2 > maxDistance2) {
            continue;
        }

        bool insideFrustum = true;
        for (const auto& plane : cameraUniforms_->frustumPlanes) {
            const float signedDistance = glm::dot(glm::vec3(plane), center) + plane.w;
            if (signedDistance < -radius) {
                insideFrustum = false;
                break;
            }
        }
        if (!insideFrustum) {
            continue;
        }

        visibleInstances_.push_back(instance);
        if (visibleInstances_.size() >= instanceBufferCapacity_) {
            break;
        }
    }

    visibleInstanceCount_ = static_cast<uint32_t>(visibleInstances_.size());
    if (visibleInstances_.empty()) {
        return;
    }

    gpu::writeBuffer(
        queue_,
        instanceBuffer_,
        0,
        std::span<const std::byte>(
            reinterpret_cast<const std::byte*>(visibleInstances_.data()),
            visibleInstances_.size() * sizeof(GPUInstance)
        )
    );
}

void DecorationRenderer::releasePipelines() {
    if (pipelineRayDepth_) {
        wgpuRenderPipelineRelease(pipelineRayDepth_);
        pipelineRayDepth_ = nullptr;
    }
    if (pipelineHardwareDepth_) {
        wgpuRenderPipelineRelease(pipelineHardwareDepth_);
        pipelineHardwareDepth_ = nullptr;
    }
}

void DecorationRenderer::renderRaycast(WGPUCommandEncoder encoder, WGPUTextureView colorView) {
    if (!pipelineRayDepth_ || visibleInstanceCount_ == 0) {
        return;
    }
    if (cameraDirty_) {
        updateCameraBuffer();
    }
    if (paramsDirty_ || params_.useRayDepth != 1u) {
        updateParamsBuffer(1u);
    }
    if (bindGroupDirty_ || !bindGroup_) {
        if (!createBindGroup()) {
            LOG_WARN("DecorationRenderer::renderRaycast: bind group unavailable");
            return;
        }
    }

    WGPURenderPassColorAttachment colorAttachment{};
    colorAttachment.view = colorView;
    colorAttachment.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
    colorAttachment.loadOp = WGPULoadOp_Load;
    colorAttachment.storeOp = WGPUStoreOp_Store;

    WGPURenderPassDescriptor passDesc{};
    WGPU_SET_LABEL(passDesc, "decoration_ray_depth_pass");
    passDesc.colorAttachmentCount = 1;
    passDesc.colorAttachments = &colorAttachment;

    WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &passDesc);
    wgpuRenderPassEncoderSetPipeline(pass, pipelineRayDepth_);
    wgpuRenderPassEncoderSetBindGroup(pass, 0, bindGroup_, 0, nullptr);
    wgpuRenderPassEncoderDraw(pass, 12, visibleInstanceCount_, 0, 0);
    wgpuRenderPassEncoderEnd(pass);
    wgpuRenderPassEncoderRelease(pass);
}

void DecorationRenderer::renderWithDepth(WGPUCommandEncoder encoder, WGPUTextureView colorView,
                                         WGPUTextureView depthView) {
    if (!pipelineHardwareDepth_ || visibleInstanceCount_ == 0 || !depthView) {
        return;
    }
    if (cameraDirty_) {
        updateCameraBuffer();
    }
    if (paramsDirty_ || params_.useRayDepth != 0u) {
        updateParamsBuffer(0u);
    }
    if (bindGroupDirty_ || !bindGroup_) {
        if (!createBindGroup()) {
            LOG_WARN("DecorationRenderer::renderWithDepth: bind group unavailable");
            return;
        }
    }

    WGPURenderPassColorAttachment colorAttachment{};
    colorAttachment.view = colorView;
    colorAttachment.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
    colorAttachment.loadOp = WGPULoadOp_Load;
    colorAttachment.storeOp = WGPUStoreOp_Store;

    WGPURenderPassDepthStencilAttachment depthAttachment{};
    depthAttachment.view = depthView;
    depthAttachment.depthLoadOp = WGPULoadOp_Load;
    depthAttachment.depthStoreOp = WGPUStoreOp_Store;
    depthAttachment.stencilLoadOp = WGPULoadOp_Undefined;
    depthAttachment.stencilStoreOp = WGPUStoreOp_Undefined;
    depthAttachment.depthReadOnly = false;
    depthAttachment.stencilReadOnly = true;

    WGPURenderPassDescriptor passDesc{};
    WGPU_SET_LABEL(passDesc, "decoration_hardware_depth_pass");
    passDesc.colorAttachmentCount = 1;
    passDesc.colorAttachments = &colorAttachment;
    passDesc.depthStencilAttachment = &depthAttachment;

    WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &passDesc);
    wgpuRenderPassEncoderSetPipeline(pass, pipelineHardwareDepth_);
    wgpuRenderPassEncoderSetBindGroup(pass, 0, bindGroup_, 0, nullptr);
    wgpuRenderPassEncoderDraw(pass, 12, visibleInstanceCount_, 0, 0);
    wgpuRenderPassEncoderEnd(pass);
    wgpuRenderPassEncoderRelease(pass);
}

} // namespace voxy::render
