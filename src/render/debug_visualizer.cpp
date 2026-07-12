// ═══════════════════════════════════════════════════════════════════════════════
// debug_visualizer.cpp - Debug Visualization Renderer Implementation
// ═══════════════════════════════════════════════════════════════════════════════

#include "render/debug_visualizer.hpp"
#include "gpu/resources.hpp"
#include "core/log.hpp"

namespace voxy::render {

// ═══════════════════════════════════════════════════════════════════════════════
// DebugVisualizer Implementation
// ═══════════════════════════════════════════════════════════════════════════════

DebugVisualizer::~DebugVisualizer() {
    shutdown();
}

DebugVisualizer::DebugVisualizer(DebugVisualizer&& other) noexcept
    : device_(other.device_)
    , queue_(other.queue_)
    , shaderModule_(other.shaderModule_)
    , pipelineLayout_(other.pipelineLayout_)
    , pipeline_(other.pipeline_)
    , bindGroupLayout_(other.bindGroupLayout_)
    , bindGroup_(other.bindGroup_)
    , uniformBuffer_(other.uniformBuffer_)
    , depthView_(other.depthView_)
    , params_(other.params_)
    , config_(other.config_)
    , uniformsDirty_(other.uniformsDirty_)
    , bindGroupDirty_(other.bindGroupDirty_)
{
    // Null out the source
    other.device_ = nullptr;
    other.queue_ = nullptr;
    other.shaderModule_ = nullptr;
    other.pipelineLayout_ = nullptr;
    other.pipeline_ = nullptr;
    other.bindGroupLayout_ = nullptr;
    other.bindGroup_ = nullptr;
    other.uniformBuffer_ = nullptr;
    other.depthView_ = nullptr;
}

DebugVisualizer& DebugVisualizer::operator=(DebugVisualizer&& other) noexcept {
    if (this != &other) {
        shutdown();
        
        device_ = other.device_;
        queue_ = other.queue_;
        shaderModule_ = other.shaderModule_;
        pipelineLayout_ = other.pipelineLayout_;
        pipeline_ = other.pipeline_;
        bindGroupLayout_ = other.bindGroupLayout_;
        bindGroup_ = other.bindGroup_;
        uniformBuffer_ = other.uniformBuffer_;
        depthView_ = other.depthView_;
        params_ = other.params_;
        config_ = other.config_;
        uniformsDirty_ = other.uniformsDirty_;
        bindGroupDirty_ = other.bindGroupDirty_;
        
        other.device_ = nullptr;
        other.queue_ = nullptr;
        other.shaderModule_ = nullptr;
        other.pipelineLayout_ = nullptr;
        other.pipeline_ = nullptr;
        other.bindGroupLayout_ = nullptr;
        other.bindGroup_ = nullptr;
        other.uniformBuffer_ = nullptr;
        other.depthView_ = nullptr;
    }
    return *this;
}

void DebugVisualizer::shutdown() {
    if (bindGroup_) {
        wgpuBindGroupRelease(bindGroup_);
        bindGroup_ = nullptr;
    }
    if (bindGroupLayout_) {
        wgpuBindGroupLayoutRelease(bindGroupLayout_);
        bindGroupLayout_ = nullptr;
    }
    if (pipeline_) {
        wgpuRenderPipelineRelease(pipeline_);
        pipeline_ = nullptr;
    }
    if (pipelineLayout_) {
        wgpuPipelineLayoutRelease(pipelineLayout_);
        pipelineLayout_ = nullptr;
    }
    if (shaderModule_) {
        wgpuShaderModuleRelease(shaderModule_);
        shaderModule_ = nullptr;
    }
    if (uniformBuffer_) {
        wgpuBufferRelease(uniformBuffer_);
        uniformBuffer_ = nullptr;
    }
    
    // Note: We don't own depthView_, so don't release it
    depthView_ = nullptr;
    device_ = nullptr;
    queue_ = nullptr;
}

// ─────────────────────────────────────────────────────────────────────────────
// Initialization
// ─────────────────────────────────────────────────────────────────────────────

bool DebugVisualizer::init(WGPUDevice device, WGPUQueue queue,
                           const DebugVisualizerConfig& config) {
    LOG_SCOPE("DebugVisualizer::init");
    
    if (!device || !queue) {
        LOG_ERROR("DebugVisualizer::init: device or queue is null");
        return false;
    }
    
    device_ = device;
    queue_ = queue;
    config_ = config;
    
    // Initialize params from config
    params_.nearDist = config.nearDist;
    params_.farDist = config.farDist;
    params_.mode = static_cast<uint32_t>(config.mode);
    params_.padding = 0;
    
    // Create resources in order
    if (!createUniformBuffer()) {
        LOG_ERROR("Failed to create uniform buffer");
        shutdown();
        return false;
    }
    
    if (!createBindGroupLayout()) {
        LOG_ERROR("Failed to create bind group layout");
        shutdown();
        return false;
    }
    
    if (!createPipeline(config)) {
        LOG_ERROR("Failed to create render pipeline");
        shutdown();
        return false;
    }
    
    LOG_INFO("DebugVisualizer initialized successfully");
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Uniform Buffer Creation
// ─────────────────────────────────────────────────────────────────────────────

bool DebugVisualizer::createUniformBuffer() {
    uint64_t alignedSize = gpu::alignUniformBufferSize(sizeof(DebugParams));
    
    gpu::BufferDesc bufferDesc = gpu::BufferDesc::uniform(
        alignedSize,
        "debug_params"
    );
    
    uniformBuffer_ = gpu::createBuffer(device_, bufferDesc);
    
    if (!uniformBuffer_) {
        LOG_ERROR("Failed to create debug uniform buffer");
        return false;
    }
    
    // Upload initial data
    updateUniformBuffer();
    
    LOG_DEBUG("Created debug uniform buffer: {} bytes", alignedSize);
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Bind Group Layout Creation
// ─────────────────────────────────────────────────────────────────────────────

bool DebugVisualizer::createBindGroupLayout() {
    // Layout matches debug_depth.wgsl:
    // @group(0) @binding(0) var<uniform> params : DebugParams;
    // @group(0) @binding(1) var depthTex : texture_2d<f32>;
    
    std::array<gpu::BindGroupLayoutEntry, 2> entries = {
        gpu::BindGroupLayoutEntry(0)
            .fragmentVisible()
            .uniformBuffer(false, sizeof(DebugParams)),
        gpu::BindGroupLayoutEntry(1)
            .fragmentVisible()
            .texture(WGPUTextureSampleType_UnfilterableFloat, WGPUTextureViewDimension_2D, false)
    };
    
    bindGroupLayout_ = gpu::createBindGroupLayout(device_, entries, "debug_bind_group_layout");
    
    if (!bindGroupLayout_) {
        LOG_ERROR("Failed to create debug bind group layout");
        return false;
    }
    
    LOG_DEBUG("Created debug bind group layout");
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Pipeline Creation
// ─────────────────────────────────────────────────────────────────────────────

bool DebugVisualizer::createPipeline(const DebugVisualizerConfig& config) {
    // Load shader module
    shaderModule_ = gpu::loadShaderModule(device_, config.shaderPath, "debug_depth.wgsl");
    
    if (!shaderModule_) {
        LOG_ERROR("Failed to load debug shader from: {}", config.shaderPath.string());
        return false;
    }
    
    // Create pipeline layout
    std::array<WGPUBindGroupLayout, 1> bindGroupLayouts = { bindGroupLayout_ };
    pipelineLayout_ = gpu::createPipelineLayout(device_, bindGroupLayouts, "debug_pipeline_layout");
    
    if (!pipelineLayout_) {
        LOG_ERROR("Failed to create debug pipeline layout");
        return false;
    }
    
    // Create render pipeline
    WGPURenderPipelineDescriptor pipelineDesc{};
    WGPU_SET_LABEL(pipelineDesc, "debug_pipeline");
    pipelineDesc.layout = pipelineLayout_;
    
    // Vertex state - no vertex buffers (fullscreen triangle)
    WGPUVertexState vertexState{};
    vertexState.module = shaderModule_;
    WGPU_SET_ENTRY_POINT(vertexState, "vs");
    vertexState.bufferCount = 0;
    vertexState.buffers = nullptr;
    pipelineDesc.vertex = vertexState;
    
    // Primitive state
    WGPUPrimitiveState primitiveState{};
    primitiveState.topology = WGPUPrimitiveTopology_TriangleList;
    primitiveState.stripIndexFormat = WGPUIndexFormat_Undefined;
    primitiveState.frontFace = WGPUFrontFace_CCW;
    primitiveState.cullMode = WGPUCullMode_None;  // No culling for fullscreen
    pipelineDesc.primitive = primitiveState;
    
    // No depth stencil (we're just blitting)
    pipelineDesc.depthStencil = nullptr;
    
    // Multisample state
    WGPUMultisampleState multisampleState{};
    multisampleState.count = 1;
    multisampleState.mask = 0xFFFFFFFF;
    multisampleState.alphaToCoverageEnabled = false;
    pipelineDesc.multisample = multisampleState;
    
    // Fragment state
    WGPUColorTargetState colorTarget{};
    colorTarget.format = config.colorFormat;
    colorTarget.writeMask = WGPUColorWriteMask_All;
    colorTarget.blend = nullptr;  // No blending
    
    WGPUFragmentState fragmentState{};
    fragmentState.module = shaderModule_;
    WGPU_SET_ENTRY_POINT(fragmentState, "fs");
    fragmentState.targetCount = 1;
    fragmentState.targets = &colorTarget;
    pipelineDesc.fragment = &fragmentState;
    
    pipeline_ = wgpuDeviceCreateRenderPipeline(device_, &pipelineDesc);
    
    if (!pipeline_) {
        LOG_ERROR("Failed to create debug render pipeline");
        return false;
    }
    
    LOG_DEBUG("Created debug render pipeline");
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Bind Group Creation
// ─────────────────────────────────────────────────────────────────────────────

bool DebugVisualizer::createBindGroup() {
    if (!depthView_) {
        LOG_ERROR("Cannot create debug bind group: no depth view set");
        return false;
    }
    
    // Release old bind group if exists
    if (bindGroup_) {
        wgpuBindGroupRelease(bindGroup_);
        bindGroup_ = nullptr;
    }
    
    std::array<gpu::BindGroupEntry, 2> entries = {
        gpu::BindGroupEntry(0).buffer(uniformBuffer_, 0, sizeof(DebugParams)),
        gpu::BindGroupEntry(1).textureView(depthView_)
    };
    
    bindGroup_ = gpu::createBindGroup(device_, bindGroupLayout_, entries, "debug_bind_group");
    
    if (!bindGroup_) {
        LOG_ERROR("Failed to create debug bind group");
        return false;
    }
    
    bindGroupDirty_ = false;
    LOG_DEBUG("Created debug bind group");
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Depth Texture Binding
// ─────────────────────────────────────────────────────────────────────────────

void DebugVisualizer::setDepthTexture(WGPUTextureView depthView) {
    depthView_ = depthView;
    bindGroupDirty_ = true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Visualization Settings
// ─────────────────────────────────────────────────────────────────────────────

void DebugVisualizer::setDepthRange(float near, float far) {
    if (params_.nearDist != near || params_.farDist != far) {
        params_.nearDist = near;
        params_.farDist = far;
        uniformsDirty_ = true;
    }
}

void DebugVisualizer::setMode(DebugVisMode mode) {
    uint32_t modeVal = static_cast<uint32_t>(mode);
    if (params_.mode != modeVal) {
        params_.mode = modeVal;
        uniformsDirty_ = true;
    }
}

DebugVisMode DebugVisualizer::getMode() const noexcept {
    return static_cast<DebugVisMode>(params_.mode);
}

void DebugVisualizer::updateUniformBuffer() {
    if (!uniformBuffer_ || !queue_) return;
    
    gpu::writeBuffer(queue_, uniformBuffer_, 0, params_);
    uniformsDirty_ = false;
}

// ─────────────────────────────────────────────────────────────────────────────
// Rendering
// ─────────────────────────────────────────────────────────────────────────────

void DebugVisualizer::render(WGPUCommandEncoder encoder, WGPUTextureView colorView) {
    if (!pipeline_) {
        LOG_WARN("DebugVisualizer::render: not initialized");
        return;
    }
    
    if (!depthView_) {
        LOG_WARN("DebugVisualizer::render: no depth texture set");
        return;
    }
    
    // Update uniform buffer if dirty
    if (uniformsDirty_) {
        updateUniformBuffer();
    }
    
    // Create bind group if dirty
    if (bindGroupDirty_ || !bindGroup_) {
        if (!createBindGroup()) {
            LOG_ERROR("Failed to create bind group during render");
            return;
        }
    }
    
    // Create render pass
    WGPURenderPassColorAttachment colorAttachment{};
    colorAttachment.view = colorView;
    colorAttachment.loadOp = WGPULoadOp_Clear;
    colorAttachment.storeOp = WGPUStoreOp_Store;
    colorAttachment.clearValue = {0.0, 0.0, 0.0, 1.0};  // Black background
    colorAttachment.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
    
    WGPURenderPassDescriptor renderPassDesc{};
    WGPU_SET_LABEL(renderPassDesc, "debug_render_pass");
    renderPassDesc.colorAttachmentCount = 1;
    renderPassDesc.colorAttachments = &colorAttachment;
    renderPassDesc.depthStencilAttachment = nullptr;
    
    WGPURenderPassEncoder renderPass = wgpuCommandEncoderBeginRenderPass(encoder, &renderPassDesc);
    
    // Set pipeline and bind group
    wgpuRenderPassEncoderSetPipeline(renderPass, pipeline_);
    wgpuRenderPassEncoderSetBindGroup(renderPass, 0, bindGroup_, 0, nullptr);
    
    // Draw fullscreen triangle (3 vertices, 1 instance)
    wgpuRenderPassEncoderDraw(renderPass, 3, 1, 0, 0);
    
    wgpuRenderPassEncoderEnd(renderPass);
    wgpuRenderPassEncoderRelease(renderPass);
}

} // namespace voxy::render


