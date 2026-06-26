// ═══════════════════════════════════════════════════════════════════════════════
// blit_path.cpp - Fullscreen Blit/Lighting Rendering Path Implementation
// ═══════════════════════════════════════════════════════════════════════════════

#include "render/blit_path.hpp"
#include "render/triangle_path.hpp"  // For CameraUniforms
#include "gpu/resources.hpp"
#include "core/log.hpp"

#include <glm/gtc/matrix_inverse.hpp>
#include <cstring>

namespace voxy::render {

// ─────────────────────────────────────────────────────────────────────────────
// Debug Uniforms Structure (matches shader)
// ─────────────────────────────────────────────────────────────────────────────

struct DebugUniforms {
    uint32_t mode = 0;           // 0=none, 1=depth, 2=normals, 3=mip_levels
    float maxDepth = 5000.0f;    // Max depth for depth visualization
    float padding0 = 0.0f;
    float padding1 = 0.0f;
};

static_assert(sizeof(DebugUniforms) == 16, "DebugUniforms must be 16 bytes");

// ═══════════════════════════════════════════════════════════════════════════════
// BlitPath Implementation
// ═══════════════════════════════════════════════════════════════════════════════

BlitPath::~BlitPath() {
    shutdown();
}

BlitPath::BlitPath(BlitPath&& other) noexcept
    : device_(other.device_)
    , queue_(other.queue_)
    , shaderModule_(other.shaderModule_)
    , pipelineLayout_(other.pipelineLayout_)
    , pipeline_(other.pipeline_)
    , bindGroupLayout_(other.bindGroupLayout_)
    , bindGroup_(other.bindGroup_)
    , uniformBuffer_(other.uniformBuffer_)
    , debugUniformBuffer_(other.debugUniformBuffer_)
    , sampler_(other.sampler_)
    , depthView_(other.depthView_)
    , shadowView_(other.shadowView_)
    , terrainView_(other.terrainView_)
    , lightmapView_(other.lightmapView_)
    , terrainWidth_(other.terrainWidth_)
    , terrainHeight_(other.terrainHeight_)
    , uniforms_(other.uniforms_)
    , config_(other.config_)
    , uniformsDirty_(other.uniformsDirty_)
    , bindGroupDirty_(other.bindGroupDirty_)
    , debugMode_(other.debugMode_)
    , debugMaxDepth_(other.debugMaxDepth_)
    , debugUniformsDirty_(other.debugUniformsDirty_)
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
    other.debugUniformBuffer_ = nullptr;
    other.sampler_ = nullptr;
    other.depthView_ = nullptr;
    other.shadowView_ = nullptr;
    other.terrainView_ = nullptr;
    other.lightmapView_ = nullptr;
    other.uniforms_ = nullptr;
}

BlitPath& BlitPath::operator=(BlitPath&& other) noexcept {
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
        debugUniformBuffer_ = other.debugUniformBuffer_;
        sampler_ = other.sampler_;
        depthView_ = other.depthView_;
        shadowView_ = other.shadowView_;
        terrainView_ = other.terrainView_;
        lightmapView_ = other.lightmapView_;
        terrainWidth_ = other.terrainWidth_;
        terrainHeight_ = other.terrainHeight_;
        uniforms_ = other.uniforms_;
        config_ = other.config_;
        uniformsDirty_ = other.uniformsDirty_;
        bindGroupDirty_ = other.bindGroupDirty_;
        debugMode_ = other.debugMode_;
        debugMaxDepth_ = other.debugMaxDepth_;
        debugUniformsDirty_ = other.debugUniformsDirty_;
        
        other.device_ = nullptr;
        other.queue_ = nullptr;
        other.shaderModule_ = nullptr;
        other.pipelineLayout_ = nullptr;
        other.pipeline_ = nullptr;
        other.bindGroupLayout_ = nullptr;
        other.bindGroup_ = nullptr;
        other.uniformBuffer_ = nullptr;
        other.debugUniformBuffer_ = nullptr;
        other.sampler_ = nullptr;
        other.depthView_ = nullptr;
        other.shadowView_ = nullptr;
        other.terrainView_ = nullptr;
        other.lightmapView_ = nullptr;
        other.uniforms_ = nullptr;
    }
    return *this;
}

void BlitPath::shutdown() {
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
    if (debugUniformBuffer_) {
        wgpuBufferRelease(debugUniformBuffer_);
        debugUniformBuffer_ = nullptr;
    }
    if (sampler_) {
        wgpuSamplerRelease(sampler_);
        sampler_ = nullptr;
    }
    
    // Free heap-allocated uniforms
    delete uniforms_;
    uniforms_ = nullptr;
    
    // Note: We don't own texture views, so don't release them
    depthView_ = nullptr;
    shadowView_ = nullptr;
    terrainView_ = nullptr;
    lightmapView_ = nullptr;
    device_ = nullptr;
    queue_ = nullptr;
}

// ─────────────────────────────────────────────────────────────────────────────
// Initialization
// ─────────────────────────────────────────────────────────────────────────────

bool BlitPath::init(WGPUDevice device, WGPUQueue queue, const BlitPathConfig& config) {
    LOG_SCOPE("BlitPath::init");
    
    if (isInitialized()) {
        LOG_ERROR("BlitPath::init: already initialized");
        return false;
    }
    
    if (!device || !queue) {
        LOG_ERROR("BlitPath::init: device or queue is null");
        return false;
    }
    
    device_ = device;
    queue_ = queue;
    config_ = config;
    
    // Allocate uniforms on heap (reuses CameraUniforms from triangle_path)
    uniforms_ = new CameraUniforms();
    uniforms_->setTerrain(terrainWidth_, terrainHeight_, config.heightScale, 
                          config.cellScale, 1.0f, config.fogDensity);
    
    // Create resources in order
    if (!createUniformBuffer()) {
        LOG_ERROR("Failed to create uniform buffer");
        shutdown();
        return false;
    }
    
    if (!createSampler()) {
        LOG_ERROR("Failed to create sampler");
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
    
    LOG_INFO("BlitPath initialized successfully");
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Uniform Buffer Creation
// ─────────────────────────────────────────────────────────────────────────────

bool BlitPath::createUniformBuffer() {
    // Create camera uniform buffer (aligned to 256 bytes for WebGPU)
    uint64_t alignedSize = gpu::alignUniformBufferSize(sizeof(CameraUniforms));
    
    gpu::BufferDesc bufferDesc = gpu::BufferDesc::uniform(
        alignedSize,
        "blit_camera_uniforms"
    );
    
    uniformBuffer_ = gpu::createBuffer(device_, bufferDesc);
    
    if (!uniformBuffer_) {
        LOG_ERROR("Failed to create uniform buffer");
        return false;
    }
    
    // Create debug uniform buffer
    uint64_t debugAlignedSize = gpu::alignUniformBufferSize(sizeof(DebugUniforms));
    
    gpu::BufferDesc debugBufferDesc = gpu::BufferDesc::uniform(
        debugAlignedSize,
        "blit_debug_uniforms"
    );
    
    debugUniformBuffer_ = gpu::createBuffer(device_, debugBufferDesc);
    
    if (!debugUniformBuffer_) {
        LOG_ERROR("Failed to create debug uniform buffer");
        return false;
    }
    
    // Upload initial data
    updateUniformBuffer();
    
    // Upload initial debug uniforms
    DebugUniforms debugUniforms;
    debugUniforms.mode = debugMode_;
    debugUniforms.maxDepth = debugMaxDepth_;
    gpu::writeBuffer(queue_, debugUniformBuffer_, 0, debugUniforms);
    debugUniformsDirty_ = false;
    
    LOG_DEBUG("Created blit uniform buffers: camera {} bytes, debug {} bytes",
              alignedSize, debugAlignedSize);
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Sampler Creation
// ─────────────────────────────────────────────────────────────────────────────

bool BlitPath::createSampler() {
    // Create linear filtering sampler for terrain and lightmap textures
    gpu::SamplerDesc samplerDesc = gpu::SamplerDesc::linear("blit_terrain_sampler");
    
    sampler_ = gpu::createSampler(device_, samplerDesc);
    
    if (!sampler_) {
        LOG_ERROR("Failed to create sampler");
        return false;
    }
    
    LOG_DEBUG("Created blit sampler");
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Bind Group Layout Creation
// ─────────────────────────────────────────────────────────────────────────────

bool BlitPath::createBindGroupLayout() {
    // Layout matches ray_blit.wgsl:
    // @group(0) @binding(0) var<uniform> camera : CameraUniforms;
    // @group(0) @binding(1) var depthTex : texture_2d<f32>;
    // @group(0) @binding(2) var shadowTex : texture_2d<f32>;
    // @group(0) @binding(3) var terrainTex : texture_2d<f32>;
    // @group(0) @binding(4) var lightmapTex : texture_2d<f32>;
    // @group(0) @binding(5) var terrainSampler : sampler;
    // @group(0) @binding(6) var<uniform> debug : DebugUniforms;
    
    std::array<gpu::BindGroupLayoutEntry, 7> entries = {
        gpu::BindGroupLayoutEntry(0)
            .vertexVisible()
            .fragmentVisible()
            .uniformBuffer(false, sizeof(CameraUniforms)),
        gpu::BindGroupLayoutEntry(1)
            .fragmentVisible()
            .texture(WGPUTextureSampleType_UnfilterableFloat, WGPUTextureViewDimension_2D, false),
        gpu::BindGroupLayoutEntry(2)
            .fragmentVisible()
            .texture(WGPUTextureSampleType_UnfilterableFloat, WGPUTextureViewDimension_2D, false),
        gpu::BindGroupLayoutEntry(3)
            .fragmentVisible()
            .texture(WGPUTextureSampleType_Float, WGPUTextureViewDimension_2D, false),
        gpu::BindGroupLayoutEntry(4)
            .fragmentVisible()
            .texture(WGPUTextureSampleType_Float, WGPUTextureViewDimension_2D, false),
        gpu::BindGroupLayoutEntry(5)
            .fragmentVisible()
            .sampler(WGPUSamplerBindingType_Filtering),
        gpu::BindGroupLayoutEntry(6)
            .fragmentVisible()
            .uniformBuffer(false, sizeof(DebugUniforms))
    };
    
    bindGroupLayout_ = gpu::createBindGroupLayout(device_, entries, "blit_bind_group_layout");
    
    if (!bindGroupLayout_) {
        LOG_ERROR("Failed to create bind group layout");
        return false;
    }
    
    LOG_DEBUG("Created blit bind group layout");
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Pipeline Creation
// ─────────────────────────────────────────────────────────────────────────────

bool BlitPath::createPipeline(const BlitPathConfig& config) {
    // Load shader module
    shaderModule_ = gpu::loadShaderModule(device_, config.shaderPath, "ray_blit.wgsl");
    
    if (!shaderModule_) {
        LOG_ERROR("Failed to load blit shader from: {}", config.shaderPath.string());
        return false;
    }
    
    // Create pipeline layout
    std::array<WGPUBindGroupLayout, 1> bindGroupLayouts = { bindGroupLayout_ };
    pipelineLayout_ = gpu::createPipelineLayout(device_, bindGroupLayouts, "blit_pipeline_layout");
    
    if (!pipelineLayout_) {
        LOG_ERROR("Failed to create pipeline layout");
        return false;
    }
    
    // Create render pipeline
    // Vertex state (no buffers - fullscreen triangle generated in shader)
    WGPUVertexState vertexState{};
    vertexState.module = shaderModule_;
    WGPU_SET_ENTRY_POINT(vertexState, "vs");
    vertexState.bufferCount = 0;
    vertexState.buffers = nullptr;
    
    // Fragment state
    WGPUColorTargetState colorTarget{};
    colorTarget.format = config.colorFormat;
    colorTarget.writeMask = WGPUColorWriteMask_All;
    
    WGPUFragmentState fragmentState{};
    fragmentState.module = shaderModule_;
    WGPU_SET_ENTRY_POINT(fragmentState, "fs");
    fragmentState.targetCount = 1;
    fragmentState.targets = &colorTarget;
    
    // Primitive state
    WGPUPrimitiveState primitiveState{};
    primitiveState.topology = WGPUPrimitiveTopology_TriangleList;
    primitiveState.frontFace = WGPUFrontFace_CCW;
    primitiveState.cullMode = WGPUCullMode_None;  // No culling for fullscreen triangle
    
    // Multisample state
    WGPUMultisampleState multisampleState{};
    multisampleState.count = 1;
    multisampleState.mask = ~0u;
    
    // Create the pipeline
    WGPURenderPipelineDescriptor pipelineDesc{};
    WGPU_SET_LABEL(pipelineDesc, "blit_pipeline");
    pipelineDesc.layout = pipelineLayout_;
    pipelineDesc.vertex = vertexState;
    pipelineDesc.fragment = &fragmentState;
    pipelineDesc.primitive = primitiveState;
    pipelineDesc.multisample = multisampleState;
    // No depth stencil - depth is handled by ray-caster
    
    pipeline_ = wgpuDeviceCreateRenderPipeline(device_, &pipelineDesc);
    
    if (!pipeline_) {
        LOG_ERROR("Failed to create blit render pipeline");
        return false;
    }
    
    LOG_DEBUG("Created blit render pipeline");
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Bind Group Creation
// ─────────────────────────────────────────────────────────────────────────────

bool BlitPath::createBindGroup() {
    if (!depthView_) {
        LOG_ERROR("Cannot create bind group: no depth view set");
        return false;
    }

    if (!shadowView_) {
        LOG_ERROR("Cannot create bind group: no shadow view set");
        return false;
    }
    
    if (!terrainView_) {
        LOG_ERROR("Cannot create bind group: no terrain view set");
        return false;
    }
    
    if (!lightmapView_) {
        LOG_ERROR("Cannot create bind group: no lightmap view set");
        return false;
    }
    
    // Release old bind group if exists
    if (bindGroup_) {
        wgpuBindGroupRelease(bindGroup_);
        bindGroup_ = nullptr;
    }
    
    std::array<gpu::BindGroupEntry, 7> entries = {
        gpu::BindGroupEntry(0).buffer(uniformBuffer_, 0, sizeof(CameraUniforms)),
        gpu::BindGroupEntry(1).textureView(depthView_),
        gpu::BindGroupEntry(2).textureView(shadowView_),
        gpu::BindGroupEntry(3).textureView(terrainView_),
        gpu::BindGroupEntry(4).textureView(lightmapView_),
        gpu::BindGroupEntry(5).sampler(sampler_),
        gpu::BindGroupEntry(6).buffer(debugUniformBuffer_, 0, sizeof(DebugUniforms))
    };
    
    bindGroup_ = gpu::createBindGroup(device_, bindGroupLayout_, entries, "blit_bind_group");
    
    if (!bindGroup_) {
        LOG_ERROR("Failed to create blit bind group");
        return false;
    }
    
    bindGroupDirty_ = false;
    LOG_DEBUG("Created blit bind group");
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Input Bindings
// ─────────────────────────────────────────────────────────────────────────────

void BlitPath::setDepthTexture(WGPUTextureView depthView) {
    depthView_ = depthView;
    bindGroupDirty_ = true;
    LOG_DEBUG("Set depth texture view");
}

void BlitPath::setShadowTexture(WGPUTextureView shadowView) {
    shadowView_ = shadowView;
    bindGroupDirty_ = true;
    LOG_DEBUG("Set shadow texture view");
}

void BlitPath::setTerrainTexture(WGPUTextureView terrainView) {
    terrainView_ = terrainView;
    bindGroupDirty_ = true;
    LOG_DEBUG("Set terrain texture view");
}

void BlitPath::setLightmapTexture(WGPUTextureView lightmapView) {
    lightmapView_ = lightmapView;
    bindGroupDirty_ = true;
    LOG_DEBUG("Set lightmap texture view");
}

void BlitPath::setTerrainSize(uint32_t width, uint32_t height) {
    terrainWidth_ = width;
    terrainHeight_ = height;
    
    if (uniforms_) {
        uniforms_->setTerrain(width, height, config_.heightScale, config_.cellScale,
                              1.0f, config_.fogDensity);
        uniformsDirty_ = true;
    }
    
    LOG_DEBUG("Set terrain size: {}x{}", width, height);
}

// ─────────────────────────────────────────────────────────────────────────────
// Camera Updates
// ─────────────────────────────────────────────────────────────────────────────

void BlitPath::updateCamera(const glm::mat4& view, const glm::mat4& proj, 
                            const glm::vec3& cameraPos, float ambientIntensity) {
    if (!uniforms_) return;
    
    uniforms_->setCamera(view, proj, cameraPos);
    
    // Update light direction in view space (using hardcoded world direction)
    glm::vec3 worldLightDir = glm::normalize(glm::vec3(0.3f, 0.8f, 0.4f));
    uniforms_->setLightDirection(worldLightDir, view, ambientIntensity);
    
    uniformsDirty_ = true;
}

void BlitPath::setLegoMode(bool enabled) {
    if (uniforms_) {
        uniforms_->setLegoMode(enabled);
        uniformsDirty_ = true;
    }
}

void BlitPath::setCameraUniforms(const CameraUniforms& uniforms) {
    if (!uniforms_) return;

    *uniforms_ = uniforms;
    uniformsDirty_ = true;
}

void BlitPath::updateUniformBuffer() {
    if (!uniformBuffer_ || !queue_ || !uniforms_) return;
    
    gpu::writeBuffer(queue_, uniformBuffer_, 0, *uniforms_);
    uniformsDirty_ = false;
}

// ─────────────────────────────────────────────────────────────────────────────
// Rendering
// ─────────────────────────────────────────────────────────────────────────────

void BlitPath::render(WGPUCommandEncoder encoder, WGPUTextureView colorView) {
    if (!pipeline_) {
        LOG_WARN("BlitPath::render: not initialized");
        return;
    }
    
    if (!depthView_ || !shadowView_ || !terrainView_ || !lightmapView_) {
        LOG_WARN("BlitPath::render: missing required texture bindings");
        return;
    }
    
    // Update uniform buffer if dirty
    if (uniformsDirty_) {
        updateUniformBuffer();
    }
    
    // Update debug uniform buffer if dirty
    if (debugUniformsDirty_ && debugUniformBuffer_) {
        DebugUniforms debugUniforms;
        debugUniforms.mode = debugMode_;
        debugUniforms.maxDepth = debugMaxDepth_;
        gpu::writeBuffer(queue_, debugUniformBuffer_, 0, debugUniforms);
        debugUniformsDirty_ = false;
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
    colorAttachment.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
    colorAttachment.loadOp = WGPULoadOp_Clear;
    colorAttachment.storeOp = WGPUStoreOp_Store;
    colorAttachment.clearValue = {0.0, 0.0, 0.0, 1.0};
    
    WGPURenderPassDescriptor renderPassDesc{};
    WGPU_SET_LABEL(renderPassDesc, "blit_render_pass");
    renderPassDesc.colorAttachmentCount = 1;
    renderPassDesc.colorAttachments = &colorAttachment;
    // No depth attachment - depth was computed by ray-caster
    
    WGPURenderPassEncoder renderPass = wgpuCommandEncoderBeginRenderPass(encoder, &renderPassDesc);
    
    // Set pipeline and bind group
    wgpuRenderPassEncoderSetPipeline(renderPass, pipeline_);
    wgpuRenderPassEncoderSetBindGroup(renderPass, 0, bindGroup_, 0, nullptr);
    
    // Draw fullscreen triangle (3 vertices, no index buffer)
    wgpuRenderPassEncoderDraw(renderPass, 3, 1, 0, 0);
    
    wgpuRenderPassEncoderEnd(renderPass);
    wgpuRenderPassEncoderRelease(renderPass);
}

// ─────────────────────────────────────────────────────────────────────────────
// Debug Visualization
// ─────────────────────────────────────────────────────────────────────────────

void BlitPath::setDebugMode(uint32_t mode) {
    if (debugMode_ != mode) {
        debugMode_ = mode;
        debugUniformsDirty_ = true;
    }
}

void BlitPath::setDebugMaxDepth(float maxDepth) {
    if (debugMaxDepth_ != maxDepth) {
        debugMaxDepth_ = maxDepth;
        debugUniformsDirty_ = true;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Accessors
// ─────────────────────────────────────────────────────────────────────────────

const CameraUniforms& BlitPath::getUniforms() const noexcept {
    static CameraUniforms defaultUniforms;
    return uniforms_ ? *uniforms_ : defaultUniforms;
}

} // namespace voxy::render
