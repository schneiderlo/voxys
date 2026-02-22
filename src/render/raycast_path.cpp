// ═══════════════════════════════════════════════════════════════════════════════
// raycast_path.cpp - Compute Ray-Caster Rendering Path Implementation
// ═══════════════════════════════════════════════════════════════════════════════

#include "render/raycast_path.hpp"
#include "render/triangle_path.hpp"  // For CameraUniforms
#include "gpu/resources.hpp"
#include "core/log.hpp"

#include <glm/gtc/matrix_inverse.hpp>
#include <cstring>

namespace voxy::render {

// ═══════════════════════════════════════════════════════════════════════════════
// RaycastPath Implementation
// ═══════════════════════════════════════════════════════════════════════════════

RaycastPath::~RaycastPath() {
    shutdown();
}

RaycastPath::RaycastPath(RaycastPath&& other) noexcept
    : device_(other.device_)
    , queue_(other.queue_)
    , shaderModule_(other.shaderModule_)
    , pipelineLayout_(other.pipelineLayout_)
    , pipeline_(other.pipeline_)
    , bindGroupLayout_(other.bindGroupLayout_)
    , bindGroup_(other.bindGroup_)
    , uniformBuffer_(other.uniformBuffer_)
    , depthOutputTexture_(other.depthOutputTexture_)
    , depthOutputView_(other.depthOutputView_)
    , outputWidth_(other.outputWidth_)
    , outputHeight_(other.outputHeight_)
    , heightmapView_(other.heightmapView_)
    , heightmapWidth_(other.heightmapWidth_)
    , heightmapHeight_(other.heightmapHeight_)
    , uniforms_(other.uniforms_)
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
    other.depthOutputTexture_ = nullptr;
    other.depthOutputView_ = nullptr;
    other.heightmapView_ = nullptr;
    other.uniforms_ = nullptr;
}

RaycastPath& RaycastPath::operator=(RaycastPath&& other) noexcept {
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
        depthOutputTexture_ = other.depthOutputTexture_;
        depthOutputView_ = other.depthOutputView_;
        outputWidth_ = other.outputWidth_;
        outputHeight_ = other.outputHeight_;
        heightmapView_ = other.heightmapView_;
        heightmapWidth_ = other.heightmapWidth_;
        heightmapHeight_ = other.heightmapHeight_;
        uniforms_ = other.uniforms_;
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
        other.depthOutputTexture_ = nullptr;
        other.depthOutputView_ = nullptr;
        other.heightmapView_ = nullptr;
        other.uniforms_ = nullptr;
    }
    return *this;
}

void RaycastPath::shutdown() {
    if (bindGroup_) {
        wgpuBindGroupRelease(bindGroup_);
        bindGroup_ = nullptr;
    }
    if (bindGroupLayout_) {
        wgpuBindGroupLayoutRelease(bindGroupLayout_);
        bindGroupLayout_ = nullptr;
    }
    if (pipeline_) {
        wgpuComputePipelineRelease(pipeline_);
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
    if (depthOutputView_) {
        wgpuTextureViewRelease(depthOutputView_);
        depthOutputView_ = nullptr;
    }
    if (depthOutputTexture_) {
        wgpuTextureRelease(depthOutputTexture_);
        depthOutputTexture_ = nullptr;
    }
    
    // Free heap-allocated uniforms
    delete uniforms_;
    uniforms_ = nullptr;
    
    // Note: We don't own heightmapView_, so don't release it
    heightmapView_ = nullptr;
    device_ = nullptr;
    queue_ = nullptr;
}

// ─────────────────────────────────────────────────────────────────────────────
// Initialization
// ─────────────────────────────────────────────────────────────────────────────

bool RaycastPath::init(WGPUDevice device, WGPUQueue queue,
                       uint32_t outputWidth, uint32_t outputHeight,
                       const RaycastPathConfig& config) {
    LOG_SCOPE("RaycastPath::init");
    
    if (!device || !queue) {
        LOG_ERROR("RaycastPath::init: device or queue is null");
        return false;
    }
    
    if (outputWidth == 0 || outputHeight == 0) {
        LOG_ERROR("RaycastPath::init: output dimensions cannot be zero");
        return false;
    }
    
    device_ = device;
    queue_ = queue;
    config_ = config;
    outputWidth_ = outputWidth;
    outputHeight_ = outputHeight;
    
    // Allocate uniforms on heap (reuses CameraUniforms from triangle_path)
    uniforms_ = new CameraUniforms();
    uniforms_->setTerrain(256, 256, config.heightScale, config.cellScale, 1.0f, config.fogDensity);
    
    // Create resources in order
    if (!createDepthOutputTexture()) {
        LOG_ERROR("Failed to create depth output texture");
        shutdown();
        return false;
    }
    
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
        LOG_ERROR("Failed to create compute pipeline");
        shutdown();
        return false;
    }
    
    LOG_INFO("RaycastPath initialized successfully ({}x{} output)", outputWidth, outputHeight);
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Depth Output Texture Creation
// ─────────────────────────────────────────────────────────────────────────────

bool RaycastPath::createDepthOutputTexture() {
    // Create RG32Float storage texture for ray-cast depth output and shadow data
    gpu::TextureDesc texDesc = gpu::TextureDesc::storage(
        outputWidth_, outputHeight_,
        WGPUTextureFormat_RG32Float,
        "raycast_depth_output"
    );
    
    depthOutputTexture_ = gpu::createTexture(device_, texDesc);
    
    if (!depthOutputTexture_) {
        LOG_ERROR("Failed to create depth output texture");
        return false;
    }
    
    // Create texture view
    gpu::TextureViewDesc viewDesc{};
    viewDesc.label = "raycast_depth_output_view";
    viewDesc.format = WGPUTextureFormat_RG32Float;
    
    depthOutputView_ = gpu::createTextureView(depthOutputTexture_, viewDesc);
    
    if (!depthOutputView_) {
        LOG_ERROR("Failed to create depth output texture view");
        return false;
    }
    
    LOG_DEBUG("Created depth output texture: {}x{} R32Float", outputWidth_, outputHeight_);
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Uniform Buffer Creation
// ─────────────────────────────────────────────────────────────────────────────

bool RaycastPath::createUniformBuffer() {
    // Create uniform buffer (aligned to 256 bytes for WebGPU)
    uint64_t alignedSize = gpu::alignUniformBufferSize(sizeof(CameraUniforms));
    
    gpu::BufferDesc bufferDesc = gpu::BufferDesc::uniform(
        alignedSize,
        "raycast_camera_uniforms"
    );
    
    uniformBuffer_ = gpu::createBuffer(device_, bufferDesc);
    
    if (!uniformBuffer_) {
        LOG_ERROR("Failed to create uniform buffer");
        return false;
    }
    
    // Upload initial data
    updateUniformBuffer();
    
    LOG_DEBUG("Created raycast uniform buffer: {} bytes (aligned from {})",
              alignedSize, sizeof(CameraUniforms));
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Bind Group Layout Creation
// ─────────────────────────────────────────────────────────────────────────────

bool RaycastPath::createBindGroupLayout() {
    // Layout matches terrain_raycast.wgsl:
    // @group(0) @binding(0) var<uniform> camera : CameraUniforms;
    // @group(0) @binding(1) var heightTex : texture_2d<u32>;
    // @group(0) @binding(2) var outDepth : texture_storage_2d<rg32float, write>;
    
    std::array<gpu::BindGroupLayoutEntry, 3> entries = {
        gpu::BindGroupLayoutEntry(0)
            .computeVisible()
            .uniformBuffer(false, sizeof(CameraUniforms)),
        gpu::BindGroupLayoutEntry(1)
            .computeVisible()
            .texture(WGPUTextureSampleType_Uint, WGPUTextureViewDimension_2D, false),
        gpu::BindGroupLayoutEntry(2)
            .computeVisible()
            .storageTexture(WGPUStorageTextureAccess_WriteOnly, 
                           WGPUTextureFormat_RG32Float,
                           WGPUTextureViewDimension_2D)
    };
    
    bindGroupLayout_ = gpu::createBindGroupLayout(device_, entries, "raycast_bind_group_layout");
    
    if (!bindGroupLayout_) {
        LOG_ERROR("Failed to create bind group layout");
        return false;
    }
    
    LOG_DEBUG("Created raycast bind group layout");
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Pipeline Creation
// ─────────────────────────────────────────────────────────────────────────────

bool RaycastPath::createPipeline(const RaycastPathConfig& config) {
    // Load shader module
    shaderModule_ = gpu::loadShaderModule(device_, config.shaderPath, "terrain_raycast.wgsl");
    
    if (!shaderModule_) {
        LOG_ERROR("Failed to load raycast shader from: {}", config.shaderPath.string());
        return false;
    }
    
    // Create pipeline layout
    std::array<WGPUBindGroupLayout, 1> bindGroupLayouts = { bindGroupLayout_ };
    pipelineLayout_ = gpu::createPipelineLayout(device_, bindGroupLayouts, "raycast_pipeline_layout");
    
    if (!pipelineLayout_) {
        LOG_ERROR("Failed to create pipeline layout");
        return false;
    }
    
    // Create compute pipeline
    WGPUComputePipelineDescriptor pipelineDesc{};
    WGPU_SET_LABEL(pipelineDesc, "raycast_pipeline");
    pipelineDesc.layout = pipelineLayout_;
    pipelineDesc.compute.module = shaderModule_;
    WGPU_SET_ENTRY_POINT(pipelineDesc.compute, "main");
    
    pipeline_ = wgpuDeviceCreateComputePipeline(device_, &pipelineDesc);
    
    if (!pipeline_) {
        LOG_ERROR("Failed to create raycast compute pipeline");
        return false;
    }
    
    LOG_DEBUG("Created raycast compute pipeline");
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Bind Group Creation
// ─────────────────────────────────────────────────────────────────────────────

bool RaycastPath::createBindGroup() {
    if (!heightmapView_) {
        LOG_ERROR("Cannot create bind group: no heightmap view set");
        return false;
    }
    
    if (!depthOutputView_) {
        LOG_ERROR("Cannot create bind group: no depth output view");
        return false;
    }
    
    // Release old bind group if exists
    if (bindGroup_) {
        wgpuBindGroupRelease(bindGroup_);
        bindGroup_ = nullptr;
    }
    
    std::array<gpu::BindGroupEntry, 3> entries = {
        gpu::BindGroupEntry(0).buffer(uniformBuffer_, 0, sizeof(CameraUniforms)),
        gpu::BindGroupEntry(1).textureView(heightmapView_),
        gpu::BindGroupEntry(2).textureView(depthOutputView_)
    };
    
    bindGroup_ = gpu::createBindGroup(device_, bindGroupLayout_, entries, "raycast_bind_group");
    
    if (!bindGroup_) {
        LOG_ERROR("Failed to create raycast bind group");
        return false;
    }
    
    bindGroupDirty_ = false;
    LOG_DEBUG("Created raycast bind group");
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Resize
// ─────────────────────────────────────────────────────────────────────────────

bool RaycastPath::resize(uint32_t width, uint32_t height) {
    if (width == 0 || height == 0) {
        LOG_ERROR("RaycastPath::resize: dimensions cannot be zero");
        return false;
    }
    
    if (width == outputWidth_ && height == outputHeight_) {
        return true;  // No change needed
    }
    
    LOG_DEBUG("Resizing raycast output: {}x{} -> {}x{}", 
              outputWidth_, outputHeight_, width, height);
    
    // Release old depth output resources
    if (depthOutputView_) {
        wgpuTextureViewRelease(depthOutputView_);
        depthOutputView_ = nullptr;
    }
    if (depthOutputTexture_) {
        wgpuTextureRelease(depthOutputTexture_);
        depthOutputTexture_ = nullptr;
    }
    
    outputWidth_ = width;
    outputHeight_ = height;
    
    // Create new depth output texture
    if (!createDepthOutputTexture()) {
        LOG_ERROR("Failed to recreate depth output texture after resize");
        return false;
    }
    
    // Need to recreate bind group with new depth output view
    bindGroupDirty_ = true;
    
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Heightmap Binding
// ─────────────────────────────────────────────────────────────────────────────

void RaycastPath::setHeightmap(WGPUTextureView heightmapView, uint32_t width, uint32_t height) {
    heightmapView_ = heightmapView;
    heightmapWidth_ = width;
    heightmapHeight_ = height;
    
    // Update uniforms with terrain size
    if (uniforms_) {
        uniforms_->setTerrain(width, height, config_.heightScale, config_.cellScale,
                              1.0f, config_.fogDensity);
        uniformsDirty_ = true;
    }
    
    // Need to recreate bind group
    bindGroupDirty_ = true;
    
    LOG_DEBUG("Set heightmap: {}x{}", width, height);
}

// ─────────────────────────────────────────────────────────────────────────────
// Camera Updates
// ─────────────────────────────────────────────────────────────────────────────

void RaycastPath::updateCamera(const glm::mat4& view, const glm::mat4& proj, 
                               const glm::vec3& cameraPos, float ambientIntensity) {
    if (!uniforms_) return;
    
    uniforms_->setCamera(view, proj, cameraPos);
    
    // Update light direction in view space (using hardcoded world direction)
    glm::vec3 worldLightDir = glm::normalize(glm::vec3(0.3f, 0.8f, 0.4f));
    uniforms_->setLightDirection(worldLightDir, view, ambientIntensity);
    
    uniformsDirty_ = true;
}

void RaycastPath::setLegoMode(bool enabled) {
    if (uniforms_) {
        uniforms_->setLegoMode(enabled);
        uniformsDirty_ = true;
    }
}

void RaycastPath::updateUniformBuffer() {
    if (!uniformBuffer_ || !queue_ || !uniforms_) return;
    
    gpu::writeBuffer(queue_, uniformBuffer_, 0, *uniforms_);
    uniformsDirty_ = false;
}

// ─────────────────────────────────────────────────────────────────────────────
// Dispatch
// ─────────────────────────────────────────────────────────────────────────────

void RaycastPath::dispatch(WGPUCommandEncoder encoder) {
    if (!pipeline_) {
        LOG_WARN("RaycastPath::dispatch: not initialized");
        return;
    }
    
    if (!heightmapView_) {
        LOG_WARN("RaycastPath::dispatch: no heightmap set");
        return;
    }
    
    // Update uniform buffer if dirty
    if (uniformsDirty_) {
        updateUniformBuffer();
    }
    
    // Create bind group if dirty
    if (bindGroupDirty_ || !bindGroup_) {
        if (!createBindGroup()) {
            LOG_ERROR("Failed to create bind group during dispatch");
            return;
        }
    }
    
    // Create compute pass
    WGPUComputePassDescriptor computePassDesc{};
    WGPU_SET_LABEL(computePassDesc, "raycast_compute_pass");
    
    WGPUComputePassEncoder computePass = wgpuCommandEncoderBeginComputePass(encoder, &computePassDesc);
    
    // Set pipeline and bind group
    wgpuComputePassEncoderSetPipeline(computePass, pipeline_);
    wgpuComputePassEncoderSetBindGroup(computePass, 0, bindGroup_, 0, nullptr);
    
    // Calculate workgroup counts
    uint32_t workgroupsX = (outputWidth_ + WORKGROUP_SIZE_X - 1) / WORKGROUP_SIZE_X;
    uint32_t workgroupsY = (outputHeight_ + WORKGROUP_SIZE_Y - 1) / WORKGROUP_SIZE_Y;
    
    // Dispatch
    wgpuComputePassEncoderDispatchWorkgroups(computePass, workgroupsX, workgroupsY, 1);
    
    wgpuComputePassEncoderEnd(computePass);
    wgpuComputePassEncoderRelease(computePass);
}

// ─────────────────────────────────────────────────────────────────────────────
// Accessors
// ─────────────────────────────────────────────────────────────────────────────

const CameraUniforms& RaycastPath::getUniforms() const noexcept {
    static CameraUniforms defaultUniforms;
    return uniforms_ ? *uniforms_ : defaultUniforms;
}

uint32_t RaycastPath::getWorkgroupCountX() const noexcept {
    return (outputWidth_ + WORKGROUP_SIZE_X - 1) / WORKGROUP_SIZE_X;
}

uint32_t RaycastPath::getWorkgroupCountY() const noexcept {
    return (outputHeight_ + WORKGROUP_SIZE_Y - 1) / WORKGROUP_SIZE_Y;
}

} // namespace voxy::render

