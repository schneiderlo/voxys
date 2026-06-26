// ═══════════════════════════════════════════════════════════════════════════════
// triangle_path.cpp - Triangle Terrain Rendering Path Implementation
// ═══════════════════════════════════════════════════════════════════════════════

#include "render/triangle_path.hpp"
#include "gpu/resources.hpp"
#include "core/log.hpp"
#include "render/frustum.hpp"

#include <glm/gtc/matrix_inverse.hpp>
#include <vector>
#include <cstring>
#include <array>

namespace voxy::render {

// ═══════════════════════════════════════════════════════════════════════════════
// CameraUniforms Implementation
// ═══════════════════════════════════════════════════════════════════════════════

CameraUniforms::CameraUniforms() {
    // Initialize with identity matrices
    viewProj = glm::mat4(1.0f);
    invViewProj = glm::mat4(1.0f);
    invView = glm::mat4(1.0f);
    
    // Default terrain parameters
    terrainSize = glm::vec2(256.0f, 256.0f);
    invTerrainSize = glm::vec2(1.0f / 256.0f, 1.0f / 256.0f);
    
    // Default metrics: heightScale=500, cellScale=1, step=1, fogDensity=0.0001
    metrics = glm::vec4(500.0f, 1.0f, 1.0f, 0.0001f);
    
    // Default camera at origin
    cameraPos = glm::vec4(0.0f, 100.0f, 0.0f, 1.0f);
    
    // Default inverse projection parameters (assume 90 degree FOV)
    invProjParams = glm::vec4(1.0f, 1.0f, 0.0f, 0.0f);
    
    // Default light direction (sun from upper-front-right)
    glm::vec3 defaultLightDir = glm::normalize(glm::vec3(0.3f, 0.8f, 0.4f));
    lightDirVS = glm::vec4(defaultLightDir, 0.0f);
    lightDirWS = glm::vec4(defaultLightDir, 0.0f);

    // Default frustum (all zeros)
    std::memset(frustumPlanes, 0, sizeof(frustumPlanes));
}

void CameraUniforms::setTerrain(uint32_t width, uint32_t height, float heightScale,
                                 float cellScale, float step, float fogDensity) {
    terrainSize = glm::vec2(static_cast<float>(width), static_cast<float>(height));
    invTerrainSize = glm::vec2(1.0f / terrainSize.x, 1.0f / terrainSize.y);
    metrics = glm::vec4(heightScale, cellScale, step, fogDensity);
}

void CameraUniforms::setCamera(const glm::mat4& view, const glm::mat4& proj, 
                                const glm::vec3& position) {
    viewProj = proj * view;
    invViewProj = glm::inverse(viewProj);
    invView = glm::inverse(view);
    cameraPos = glm::vec4(position, 1.0f);
    
    // Compute inverse projection parameters for ray generation
    // invProjParams.xy = tan(fov/2) * aspect, tan(fov/2) for NDC to view-space ray
    glm::mat4 invProj = glm::inverse(proj);
    // Extract from inverse projection matrix
    invProjParams.x = invProj[0][0];  // Scale for X
    invProjParams.y = invProj[1][1];  // Scale for Y

    // Update Frustum planes
    Frustum frustum = Frustum::fromViewProj(viewProj);
    for(size_t i=0; i<6; ++i) {
        frustumPlanes[i] = glm::vec4(frustum.planes[i].normal, frustum.planes[i].distance);
    }
}

void CameraUniforms::setLightDirection(const glm::vec3& worldDir, const glm::mat4& view, float ambient) {
    // Transform world-space direction to view-space
    glm::vec3 viewDir = glm::vec3(view * glm::vec4(worldDir, 0.0f));
    lightDirVS = glm::vec4(glm::normalize(viewDir), ambient);
    lightDirWS = glm::vec4(glm::normalize(worldDir), 0.0f); // Store world-space dir
}

// ═══════════════════════════════════════════════════════════════════════════════
// TrianglePath Implementation
// ═══════════════════════════════════════════════════════════════════════════════

TrianglePath::~TrianglePath() {
    shutdown();
}

TrianglePath::TrianglePath(TrianglePath&& other) noexcept
    : device_(other.device_)
    , queue_(other.queue_)
    , shaderModule_(other.shaderModule_)
    , pipelineLayout_(other.pipelineLayout_)
    , pipeline_(other.pipeline_)
    , wireframePipeline_(other.wireframePipeline_)
    , computeModule_(other.computeModule_)
    , computePipelineLayout_(other.computePipelineLayout_)
    , computePipeline_(other.computePipeline_)
    , computeBindGroupLayout_(other.computeBindGroupLayout_)
    , computeBindGroup_(other.computeBindGroup_)
    , bindGroupLayout_(other.bindGroupLayout_)
    , bindGroup_(other.bindGroup_)
    , indexBuffer_(other.indexBuffer_)
    , uniformBuffer_(other.uniformBuffer_)
    , indirectBuffer_(other.indirectBuffer_)
    , visibleIndicesBuffer_(other.visibleIndicesBuffer_)
    , heightmapView_(other.heightmapView_)
    , heightmapWidth_(other.heightmapWidth_)
    , heightmapHeight_(other.heightmapHeight_)
    , albedoView_(other.albedoView_)
    , lightmapView_(other.lightmapView_)
    , sampler_(other.sampler_)
    , uniforms_(other.uniforms_)
    , config_(other.config_)
    , tilesX_(other.tilesX_)
    , tilesY_(other.tilesY_)
    , uniformsDirty_(other.uniformsDirty_)
    , bindGroupDirty_(other.bindGroupDirty_)
    , wireframeEnabled_(other.wireframeEnabled_)
{
    // Null out the source
    other.device_ = nullptr;
    other.queue_ = nullptr;
    other.shaderModule_ = nullptr;
    other.pipelineLayout_ = nullptr;
    other.pipeline_ = nullptr;
    other.wireframePipeline_ = nullptr;
    other.computeModule_ = nullptr;
    other.computePipelineLayout_ = nullptr;
    other.computePipeline_ = nullptr;
    other.computeBindGroupLayout_ = nullptr;
    other.computeBindGroup_ = nullptr;
    other.bindGroupLayout_ = nullptr;
    other.bindGroup_ = nullptr;
    other.indexBuffer_ = nullptr;
    other.uniformBuffer_ = nullptr;
    other.indirectBuffer_ = nullptr;
    other.visibleIndicesBuffer_ = nullptr;
    other.heightmapView_ = nullptr;
    other.albedoView_ = nullptr;
    other.lightmapView_ = nullptr;
    other.sampler_ = nullptr;
}

TrianglePath& TrianglePath::operator=(TrianglePath&& other) noexcept {
    if (this != &other) {
        shutdown();
        
        device_ = other.device_;
        queue_ = other.queue_;
        shaderModule_ = other.shaderModule_;
        pipelineLayout_ = other.pipelineLayout_;
        pipeline_ = other.pipeline_;
        wireframePipeline_ = other.wireframePipeline_;
        computeModule_ = other.computeModule_;
        computePipelineLayout_ = other.computePipelineLayout_;
        computePipeline_ = other.computePipeline_;
        computeBindGroupLayout_ = other.computeBindGroupLayout_;
        computeBindGroup_ = other.computeBindGroup_;
        bindGroupLayout_ = other.bindGroupLayout_;
        bindGroup_ = other.bindGroup_;
        indexBuffer_ = other.indexBuffer_;
        uniformBuffer_ = other.uniformBuffer_;
        indirectBuffer_ = other.indirectBuffer_;
        visibleIndicesBuffer_ = other.visibleIndicesBuffer_;
        heightmapView_ = other.heightmapView_;
        heightmapWidth_ = other.heightmapWidth_;
        heightmapHeight_ = other.heightmapHeight_;
        albedoView_ = other.albedoView_;
        lightmapView_ = other.lightmapView_;
        sampler_ = other.sampler_;
        uniforms_ = other.uniforms_;
        config_ = other.config_;
        tilesX_ = other.tilesX_;
        tilesY_ = other.tilesY_;
        uniformsDirty_ = other.uniformsDirty_;
        bindGroupDirty_ = other.bindGroupDirty_;
        wireframeEnabled_ = other.wireframeEnabled_;
        
        other.device_ = nullptr;
        other.queue_ = nullptr;
        other.shaderModule_ = nullptr;
        other.pipelineLayout_ = nullptr;
        other.pipeline_ = nullptr;
        other.wireframePipeline_ = nullptr;
        other.computeModule_ = nullptr;
        other.computePipelineLayout_ = nullptr;
        other.computePipeline_ = nullptr;
        other.computeBindGroupLayout_ = nullptr;
        other.computeBindGroup_ = nullptr;
        other.bindGroupLayout_ = nullptr;
        other.bindGroup_ = nullptr;
        other.indexBuffer_ = nullptr;
        other.uniformBuffer_ = nullptr;
        other.indirectBuffer_ = nullptr;
        other.visibleIndicesBuffer_ = nullptr;
        other.heightmapView_ = nullptr;
        other.albedoView_ = nullptr;
        other.lightmapView_ = nullptr;
        other.sampler_ = nullptr;
    }
    return *this;
}

void TrianglePath::shutdown() {
    if (bindGroup_) {
        wgpuBindGroupRelease(bindGroup_);
        bindGroup_ = nullptr;
    }
    if (computeBindGroup_) {
        wgpuBindGroupRelease(computeBindGroup_);
        computeBindGroup_ = nullptr;
    }
    if (bindGroupLayout_) {
        wgpuBindGroupLayoutRelease(bindGroupLayout_);
        bindGroupLayout_ = nullptr;
    }
    if (computeBindGroupLayout_) {
        wgpuBindGroupLayoutRelease(computeBindGroupLayout_);
        computeBindGroupLayout_ = nullptr;
    }
    if (pipeline_) {
        wgpuRenderPipelineRelease(pipeline_);
        pipeline_ = nullptr;
    }
    if (wireframePipeline_) {
        wgpuRenderPipelineRelease(wireframePipeline_);
        wireframePipeline_ = nullptr;
    }
    if (computePipeline_) {
        wgpuComputePipelineRelease(computePipeline_);
        computePipeline_ = nullptr;
    }
    if (pipelineLayout_) {
        wgpuPipelineLayoutRelease(pipelineLayout_);
        pipelineLayout_ = nullptr;
    }
    if (computePipelineLayout_) {
        wgpuPipelineLayoutRelease(computePipelineLayout_);
        computePipelineLayout_ = nullptr;
    }
    if (shaderModule_) {
        wgpuShaderModuleRelease(shaderModule_);
        shaderModule_ = nullptr;
    }
    if (computeModule_) {
        wgpuShaderModuleRelease(computeModule_);
        computeModule_ = nullptr;
    }
    if (indexBuffer_) {
        wgpuBufferRelease(indexBuffer_);
        indexBuffer_ = nullptr;
    }
    if (uniformBuffer_) {
        wgpuBufferRelease(uniformBuffer_);
        uniformBuffer_ = nullptr;
    }
    if (indirectBuffer_) {
        wgpuBufferRelease(indirectBuffer_);
        indirectBuffer_ = nullptr;
    }
    if (visibleIndicesBuffer_) {
        wgpuBufferRelease(visibleIndicesBuffer_);
        visibleIndicesBuffer_ = nullptr;
    }
    
    // Note: We don't own these views/samplers, so don't release them.
    heightmapView_ = nullptr;
    albedoView_ = nullptr;
    lightmapView_ = nullptr;
    sampler_ = nullptr;
    device_ = nullptr;
    queue_ = nullptr;
}

// ─────────────────────────────────────────────────────────────────────────────
// Initialization
// ─────────────────────────────────────────────────────────────────────────────

bool TrianglePath::init(WGPUDevice device, WGPUQueue queue, const TrianglePathConfig& config) {
    LOG_SCOPE("TrianglePath::init");
    
    if (!device || !queue) {
        LOG_ERROR("TrianglePath::init: device or queue is null");
        return false;
    }
    
    device_ = device;
    queue_ = queue;
    config_ = config;
    
    // Initialize uniforms with config values
    uniforms_.setTerrain(256, 256, config.heightScale, config.cellScale, 
                         static_cast<float>(config.lodStep), config.fogDensity);
    
    // Create resources in order
    if (!createIndexBuffer()) {
        LOG_ERROR("Failed to create index buffer");
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
        LOG_ERROR("Failed to create render pipeline");
        shutdown();
        return false;
    }
    
    // Initialize compute resources
    if (!createComputeResources(config)) {
        LOG_ERROR("Failed to create compute resources");
        shutdown();
        return false;
    }

    LOG_INFO("TrianglePath initialized successfully");
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Index Buffer Creation
// ─────────────────────────────────────────────────────────────────────────────

bool TrianglePath::createIndexBuffer() {
    // Generate indices for a 64×64 quad grid
    // Each quad = 2 triangles = 6 indices
    // Total indices = 64 * 64 * 6 = 24576
    
    std::vector<uint32_t> indices;
    indices.reserve(INDICES_PER_TILE);
    
    for (uint32_t y = 0; y < TILE_QUADS; ++y) {
        for (uint32_t x = 0; x < TILE_QUADS; ++x) {
            // Vertices for this quad (row-major order)
            uint32_t topLeft = y * TILE_VERTS + x;
            uint32_t topRight = topLeft + 1;
            uint32_t bottomLeft = topLeft + TILE_VERTS;
            uint32_t bottomRight = bottomLeft + 1;
            
            // First triangle (top-left, bottom-right, bottom-left) - CCW when viewed from above
            indices.push_back(topLeft);
            indices.push_back(bottomRight);
            indices.push_back(bottomLeft);
            
            // Second triangle (top-left, top-right, bottom-right) - CCW when viewed from above
            indices.push_back(topLeft);
            indices.push_back(topRight);
            indices.push_back(bottomRight);
        }
    }
    
    // Create index buffer
    gpu::BufferDesc bufferDesc = gpu::BufferDesc::index(
        indices.size() * sizeof(uint32_t),
        "terrain_index_buffer"
    );
    
    indexBuffer_ = gpu::createBufferWithData(
        device_, queue_, bufferDesc,
        std::span<const uint32_t>(indices)
    );
    
    if (!indexBuffer_) {
        LOG_ERROR("Failed to create terrain index buffer");
        return false;
    }
    
    LOG_DEBUG("Created terrain index buffer: {} indices ({} bytes)",
              indices.size(), indices.size() * sizeof(uint32_t));
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Uniform Buffer Creation
// ─────────────────────────────────────────────────────────────────────────────

bool TrianglePath::createUniformBuffer() {
    // Create uniform buffer (aligned to 256 bytes for WebGPU)
    uint64_t alignedSize = gpu::alignUniformBufferSize(sizeof(CameraUniforms));
    
    gpu::BufferDesc bufferDesc = gpu::BufferDesc::uniform(
        alignedSize,
        "camera_uniforms"
    );
    
    uniformBuffer_ = gpu::createBuffer(device_, bufferDesc);
    
    if (!uniformBuffer_) {
        LOG_ERROR("Failed to create uniform buffer");
        return false;
    }
    
    // Upload initial data
    updateUniformBuffer();
    
    LOG_DEBUG("Created uniform buffer: {} bytes (aligned from {})",
              alignedSize, sizeof(CameraUniforms));
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Bind Group Layout Creation
// ─────────────────────────────────────────────────────────────────────────────

bool TrianglePath::createBindGroupLayout() {
    // Layout matches terrain.wgsl:
    // @group(0) @binding(0) var<uniform> camera : CameraUniforms;
    // @group(0) @binding(1) var heightTex : texture_2d<u32>;
    // @group(0) @binding(2) var albedoTex : texture_2d<f32>;
    // @group(0) @binding(3) var lightmapTex : texture_2d<f32>;
    // @group(0) @binding(4) var texSampler : sampler;
    // @group(0) @binding(5) var<storage> visibleIndices : array<u32>;
    
    std::array<gpu::BindGroupLayoutEntry, 6> entries = {
        gpu::BindGroupLayoutEntry(0)
            .vertexVisible()
            .fragmentVisible()
            .computeVisible() // Shared with compute shader
            .uniformBuffer(false, sizeof(CameraUniforms)),
        gpu::BindGroupLayoutEntry(1)
            .vertexVisible()
            .fragmentVisible()
            .texture(WGPUTextureSampleType_Uint, WGPUTextureViewDimension_2D, false),
        gpu::BindGroupLayoutEntry(2)
            .fragmentVisible()
            .texture(WGPUTextureSampleType_Float, WGPUTextureViewDimension_2D, false),
        gpu::BindGroupLayoutEntry(3)
            .fragmentVisible()
            .texture(WGPUTextureSampleType_Float, WGPUTextureViewDimension_2D, false),
        gpu::BindGroupLayoutEntry(4)
            .fragmentVisible()
            .sampler(WGPUSamplerBindingType_Filtering),
        gpu::BindGroupLayoutEntry(5)
            .vertexVisible() // Read by Vertex Shader
            .storageBuffer(true) // ReadOnly Storage
    };
    
    bindGroupLayout_ = gpu::createBindGroupLayout(device_, entries, "terrain_bind_group_layout");
    
    if (!bindGroupLayout_) {
        LOG_ERROR("Failed to create bind group layout");
        return false;
    }
    
    LOG_DEBUG("Created terrain bind group layout");
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Pipeline Creation
// ─────────────────────────────────────────────────────────────────────────────

bool TrianglePath::createPipeline(const TrianglePathConfig& config) {
    // Load shader module
    shaderModule_ = gpu::loadShaderModule(device_, config.shaderPath, "terrain.wgsl");
    
    if (!shaderModule_) {
        LOG_ERROR("Failed to load terrain shader from: {}", config.shaderPath.string());
        return false;
    }
    
    // Create pipeline layout
    std::array<WGPUBindGroupLayout, 1> bindGroupLayouts = { bindGroupLayout_ };
    pipelineLayout_ = gpu::createPipelineLayout(device_, bindGroupLayouts, "terrain_pipeline_layout");
    
    if (!pipelineLayout_) {
        LOG_ERROR("Failed to create pipeline layout");
        return false;
    }
    
    // Create render pipeline
    WGPURenderPipelineDescriptor pipelineDesc{};
    WGPU_SET_LABEL(pipelineDesc, "terrain_pipeline");
    pipelineDesc.layout = pipelineLayout_;
    
    // Vertex state - no vertex buffers (procedural generation)
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
    primitiveState.cullMode = WGPUCullMode_Back;
    pipelineDesc.primitive = primitiveState;
    
    // Depth stencil state
    WGPUDepthStencilState depthStencilState{};
    depthStencilState.format = config.depthFormat;
    depthStencilState.depthWriteEnabled = gpu::toOptionalBool(true);
    depthStencilState.depthCompare = WGPUCompareFunction_Less;
    depthStencilState.stencilFront.compare = WGPUCompareFunction_Always;
    depthStencilState.stencilFront.failOp = WGPUStencilOperation_Keep;
    depthStencilState.stencilFront.depthFailOp = WGPUStencilOperation_Keep;
    depthStencilState.stencilFront.passOp = WGPUStencilOperation_Keep;
    depthStencilState.stencilBack = depthStencilState.stencilFront;
    depthStencilState.stencilReadMask = 0xFFFFFFFF;
    depthStencilState.stencilWriteMask = 0xFFFFFFFF;
    pipelineDesc.depthStencil = &depthStencilState;
    
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
    // No blending for opaque terrain
    colorTarget.blend = nullptr;
    
    WGPUFragmentState fragmentState{};
    fragmentState.module = shaderModule_;
    WGPU_SET_ENTRY_POINT(fragmentState, "fs");
    fragmentState.targetCount = 1;
    fragmentState.targets = &colorTarget;
    pipelineDesc.fragment = &fragmentState;
    
    pipeline_ = wgpuDeviceCreateRenderPipeline(device_, &pipelineDesc);
    
    if (!pipeline_) {
        LOG_ERROR("Failed to create terrain render pipeline");
        return false;
    }
    
    // Create wireframe pipeline (same as above but with LineList topology)
    WGPU_SET_LABEL(pipelineDesc, "terrain_wireframe_pipeline");
    primitiveState.topology = WGPUPrimitiveTopology_LineList;
    pipelineDesc.primitive = primitiveState;
    
    wireframePipeline_ = wgpuDeviceCreateRenderPipeline(device_, &pipelineDesc);
    
    if (!wireframePipeline_) {
        LOG_WARN("Failed to create terrain wireframe pipeline (wireframe mode will be unavailable)");
        // Don't fail - wireframe is optional
    } else {
        LOG_DEBUG("Created terrain wireframe pipeline");
    }
    
    LOG_DEBUG("Created terrain render pipeline");
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Bind Group Creation
// ─────────────────────────────────────────────────────────────────────────────

bool TrianglePath::createBindGroup() {
    if (!heightmapView_) {
        LOG_ERROR("Cannot create bind group: no heightmap view set");
        return false;
    }
    
    if (!albedoView_ || !lightmapView_ || !sampler_) {
        // Only warn once to avoid spamming logs every frame if rendering fails
        static bool warned = false;
        if (!warned) {
             LOG_WARN("Cannot create bind group: missing texture/sampler bindings. Albedo: {}, Lightmap: {}, Sampler: {}",
                      albedoView_ ? "OK" : "MISSING",
                      lightmapView_ ? "OK" : "MISSING",
                      sampler_ ? "OK" : "MISSING");
             warned = true;
        }
        return false;
    }
    
    // Release old bind group if exists
    if (bindGroup_) {
        wgpuBindGroupRelease(bindGroup_);
        bindGroup_ = nullptr;
    }
    
    std::array<gpu::BindGroupEntry, 6> entries = {
        gpu::BindGroupEntry(0).buffer(uniformBuffer_, 0, sizeof(CameraUniforms)),
        gpu::BindGroupEntry(1).textureView(heightmapView_),
        gpu::BindGroupEntry(2).textureView(albedoView_),
        gpu::BindGroupEntry(3).textureView(lightmapView_),
        gpu::BindGroupEntry(4).sampler(sampler_),
        gpu::BindGroupEntry(5).buffer(visibleIndicesBuffer_, 0, WGPU_WHOLE_SIZE)
    };
    
    bindGroup_ = gpu::createBindGroup(device_, bindGroupLayout_, entries, "terrain_bind_group");
    
    if (!bindGroup_) {
        LOG_ERROR("Failed to create terrain bind group");
        return false;
    }
    
    bindGroupDirty_ = false;
    LOG_DEBUG("Created terrain bind group");
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Heightmap Binding
// ─────────────────────────────────────────────────────────────────────────────

void TrianglePath::setHeightmap(WGPUTextureView heightmapView, uint32_t width, uint32_t height) {
    heightmapView_ = heightmapView;
    heightmapWidth_ = width;
    heightmapHeight_ = height;
    
    // Update uniforms with terrain size
    uniforms_.setTerrain(width, height, config_.heightScale, config_.cellScale,
                         static_cast<float>(config_.lodStep), config_.fogDensity);
    uniformsDirty_ = true;
    
    // Need to recreate bind group
    bindGroupDirty_ = true;
    
    // Recalculate tile count
    calculateTileCount();
    
    LOG_DEBUG("Set heightmap: {}x{}, tiles: {}x{}", width, height, tilesX_, tilesY_);
}

void TrianglePath::calculateTileCount() {
    if (heightmapWidth_ == 0 || heightmapHeight_ == 0) {
        tilesX_ = 0;
        tilesY_ = 0;
        return;
    }
    
    uint32_t step = config_.lodStep;
    
    // Calculate number of quads after LOD reduction
    uint32_t quadsX = ((heightmapWidth_ - 1) + step - 1) / step;
    uint32_t quadsY = ((heightmapHeight_ - 1) + step - 1) / step;
    
    // Calculate number of tiles needed
    tilesX_ = (quadsX + TILE_QUADS - 1) / TILE_QUADS;
    tilesY_ = (quadsY + TILE_QUADS - 1) / TILE_QUADS;
    
    // Ensure at least 1 tile
    tilesX_ = std::max(tilesX_, 1u);
    tilesY_ = std::max(tilesY_, 1u);
}

// ─────────────────────────────────────────────────────────────────────────────
// Camera Updates
// ─────────────────────────────────────────────────────────────────────────────

void TrianglePath::updateCamera(const glm::mat4& view, const glm::mat4& proj, 
                                 const glm::vec3& cameraPos, float ambientIntensity) {
    uniforms_.setCamera(view, proj, cameraPos);
    
    // Update light direction in view space (using hardcoded world direction)
    glm::vec3 worldLightDir = glm::normalize(glm::vec3(0.3f, 0.8f, 0.4f));
    uniforms_.setLightDirection(worldLightDir, view, ambientIntensity);
    
    uniformsDirty_ = true;
}

void TrianglePath::setLODStep(uint32_t step) {
    step = std::max(step, 1u);
    if (config_.lodStep != step) {
        config_.lodStep = step;
        uniforms_.metrics.z = static_cast<float>(step);
        uniformsDirty_ = true;
        calculateTileCount();
    }
}

uint32_t TrianglePath::getLODStep() const noexcept {
    return config_.lodStep;
}

void TrianglePath::updateUniformBuffer() {
    if (!uniformBuffer_ || !queue_) return;
    
    gpu::writeBuffer(queue_, uniformBuffer_, 0, uniforms_);
    uniformsDirty_ = false;
}

// ─────────────────────────────────────────────────────────────────────────────
// Rendering
// ─────────────────────────────────────────────────────────────────────────────

void TrianglePath::render(WGPUCommandEncoder encoder, WGPUTextureView colorView, 
                          WGPUTextureView depthView) {
    if (!pipeline_ || !indexBuffer_ || !computePipeline_) {
        LOG_WARN("TrianglePath::render: not initialized");
        return;
    }
    
    if (!heightmapView_) {
        LOG_WARN("TrianglePath::render: no heightmap set");
        return;
    }
    
    // Update uniform buffer if dirty
    if (uniformsDirty_) {
        updateUniformBuffer();
    }
    
    // Update Compute Bind Group (if needed) and Main Bind Group
    if (bindGroupDirty_ || !bindGroup_) {
        if (!createBindGroup()) {
            LOG_ERROR("Failed to create bind group during render");
            return;
        }
        updateComputeBindGroup();
    }
    
    // ─────────────────────────────────────────────────────────────────────────
    // 1. Reset Indirect Buffer
    // ─────────────────────────────────────────────────────────────────────────

    // Reset instance count (offset 4) to 0.
    // We can use WriteBuffer for this 4-byte update.
    uint32_t zero = 0;
    wgpuQueueWriteBuffer(queue_, indirectBuffer_, 4, &zero, sizeof(uint32_t));

    // ─────────────────────────────────────────────────────────────────────────
    // 2. Compute Culling Pass
    // ─────────────────────────────────────────────────────────────────────────

    WGPUComputePassDescriptor computePassDesc{};
    WGPU_SET_LABEL(computePassDesc, "terrain_cull_pass");

    WGPUComputePassEncoder computePass = wgpuCommandEncoderBeginComputePass(encoder, &computePassDesc);
    wgpuComputePassEncoderSetPipeline(computePass, computePipeline_);
    wgpuComputePassEncoderSetBindGroup(computePass, 0, computeBindGroup_, 0, nullptr);

    uint32_t totalTiles = tilesX_ * tilesY_;
    uint32_t workgroups = (totalTiles + 63) / 64;
    wgpuComputePassEncoderDispatchWorkgroups(computePass, workgroups, 1, 1);

    wgpuComputePassEncoderEnd(computePass);
    wgpuComputePassEncoderRelease(computePass);

    // ─────────────────────────────────────────────────────────────────────────
    // 3. Indirect Draw Pass
    // ─────────────────────────────────────────────────────────────────────────

    // Create render pass
    WGPURenderPassColorAttachment colorAttachment{};
    colorAttachment.view = colorView;
    colorAttachment.loadOp = WGPULoadOp_Clear;
    colorAttachment.storeOp = WGPUStoreOp_Store;
    colorAttachment.clearValue = {0.6, 0.68, 0.76, 1.0};  // Fog color
    colorAttachment.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
    
    WGPURenderPassDepthStencilAttachment depthAttachment{};
    depthAttachment.view = depthView;
    depthAttachment.depthLoadOp = WGPULoadOp_Clear;
    depthAttachment.depthStoreOp = WGPUStoreOp_Store;
    depthAttachment.depthClearValue = 1.0f;
    depthAttachment.stencilLoadOp = WGPULoadOp_Undefined;
    depthAttachment.stencilStoreOp = WGPUStoreOp_Undefined;
    depthAttachment.depthReadOnly = false;
    depthAttachment.stencilReadOnly = true;
    
    WGPURenderPassDescriptor renderPassDesc{};
    WGPU_SET_LABEL(renderPassDesc, "terrain_render_pass");
    renderPassDesc.colorAttachmentCount = 1;
    renderPassDesc.colorAttachments = &colorAttachment;
    renderPassDesc.depthStencilAttachment = &depthAttachment;
    
    WGPURenderPassEncoder renderPass = wgpuCommandEncoderBeginRenderPass(encoder, &renderPassDesc);
    
    // Set pipeline and bind group (use wireframe pipeline if enabled and available)
    WGPURenderPipeline activePipeline = (wireframeEnabled_ && wireframePipeline_) ? wireframePipeline_ : pipeline_;
    wgpuRenderPassEncoderSetPipeline(renderPass, activePipeline);
    wgpuRenderPassEncoderSetBindGroup(renderPass, 0, bindGroup_, 0, nullptr);
    
    // Set index buffer
    wgpuRenderPassEncoderSetIndexBuffer(renderPass, indexBuffer_, WGPUIndexFormat_Uint32, 
                                        0, INDICES_PER_TILE * sizeof(uint32_t));
    
    // Draw using indirect buffer
    wgpuRenderPassEncoderDrawIndexedIndirect(renderPass, indirectBuffer_, 0);
    
    wgpuRenderPassEncoderEnd(renderPass);
    wgpuRenderPassEncoderRelease(renderPass);
}

// ─────────────────────────────────────────────────────────────────────────────
// Compute Resource Creation
// ─────────────────────────────────────────────────────────────────────────────

bool TrianglePath::createComputeResources(const TrianglePathConfig& config) {
    // 1. Create Indirect Buffer
    // Struct: { indexCount, instanceCount, firstIndex, baseVertex, firstInstance } (5 * u32)
    // Usage: INDIRECT | STORAGE | COPY_DST
    std::array<uint32_t, 5> indirectData = {
        INDICES_PER_TILE, // indexCount
        0,                // instanceCount (starts at 0)
        0,                // firstIndex
        0,                // baseVertex
        0                 // firstInstance
    };

    // Manual setup because gpu::BufferDesc::indirect helper might not exist or lacks flags
    gpu::BufferDesc indirectDesc;
    indirectDesc.label = "terrain_indirect_buffer";
    indirectDesc.size = sizeof(indirectData);
    indirectDesc.usage = WGPUBufferUsage_Indirect | WGPUBufferUsage_Storage | WGPUBufferUsage_CopyDst;

    indirectBuffer_ = gpu::createBufferWithData(
        device_, queue_, indirectDesc,
        std::span<const uint32_t>(indirectData)
    );

    if (!indirectBuffer_) return false;

    // 2. Create Visible Indices Buffer
    // Max tiles = 256x256 / 64x64 quads? No, calculated dynamically.
    // Assume worst case: 8192x8192 heightmap / 64 step = 128x128 tiles = 16k tiles.
    // Let's allocate enough for a reasonable large terrain.
    // 1024x1024 tiles = 1M tiles * 4 bytes = 4MB. Safe upper bound.
    uint32_t maxTiles = 1024 * 1024;

    // Explicitly casting bool to string_view was causing issues, let's use the default param or explicit string
    gpu::BufferDesc visibleDesc = gpu::BufferDesc::storage(
        maxTiles * sizeof(uint32_t),
        false, // readOnly = false
        "terrain_visible_indices_buffer"
    );

    visibleIndicesBuffer_ = gpu::createBuffer(device_, visibleDesc);
    if (!visibleIndicesBuffer_) return false;

    // 3. Load Compute Shader
    std::filesystem::path cullShaderPath = config.shaderPath.parent_path() / "cull_terrain.wgsl";
    computeModule_ = gpu::loadShaderModule(device_, cullShaderPath, "cull_terrain.wgsl");
    if (!computeModule_) return false;

    // 4. Create Compute Bind Group Layout
    // @group(0) @binding(0) var<uniform> camera : CameraUniforms;
    // @group(0) @binding(1) var<storage, read_write> indirectArgs : IndirectArgs;
    // @group(0) @binding(2) var<storage, read_write> visibleIndices : array<u32>;
    std::array<gpu::BindGroupLayoutEntry, 3> entries = {
        gpu::BindGroupLayoutEntry(0)
            .computeVisible()
            .uniformBuffer(false, sizeof(CameraUniforms)),
        gpu::BindGroupLayoutEntry(1)
            .computeVisible()
            .storageBuffer(false), // read_write
        gpu::BindGroupLayoutEntry(2)
            .computeVisible()
            .storageBuffer(false)  // read_write
    };

    computeBindGroupLayout_ = gpu::createBindGroupLayout(device_, entries, "cull_compute_layout");
    if (!computeBindGroupLayout_) return false;

    // 5. Create Compute Pipeline
    std::array<WGPUBindGroupLayout, 1> layouts = { computeBindGroupLayout_ };
    computePipelineLayout_ = gpu::createPipelineLayout(device_, layouts, "cull_pipeline_layout");
    if (!computePipelineLayout_) return false;

    WGPUComputePipelineDescriptor computeDesc{};
    WGPU_SET_LABEL(computeDesc, "cull_pipeline");
    computeDesc.layout = computePipelineLayout_;
    computeDesc.compute.module = computeModule_;
    WGPU_SET_ENTRY_POINT(computeDesc.compute, "main");

    computePipeline_ = wgpuDeviceCreateComputePipeline(device_, &computeDesc);
    if (!computePipeline_) return false;

    // 6. Create initial Bind Group
    updateComputeBindGroup();

    return true;
}

void TrianglePath::updateComputeBindGroup() {
    if (!computeBindGroupLayout_ || !indirectBuffer_ || !visibleIndicesBuffer_ || !uniformBuffer_) return;

    if (computeBindGroup_) {
        wgpuBindGroupRelease(computeBindGroup_);
        computeBindGroup_ = nullptr;
    }

    std::array<gpu::BindGroupEntry, 3> entries = {
        gpu::BindGroupEntry(0).buffer(uniformBuffer_, 0, sizeof(CameraUniforms)),
        gpu::BindGroupEntry(1).buffer(indirectBuffer_, 0, sizeof(uint32_t) * 5),
        gpu::BindGroupEntry(2).buffer(visibleIndicesBuffer_, 0, WGPU_WHOLE_SIZE)
    };

    computeBindGroup_ = gpu::createBindGroup(device_, computeBindGroupLayout_, entries, "cull_bind_group");
}

// ─────────────────────────────────────────────────────────────────────────────
// Accessors
// ─────────────────────────────────────────────────────────────────────────────

uint32_t TrianglePath::getTileCount() const noexcept {
    return tilesX_ * tilesY_;
}

uint32_t TrianglePath::getTriangleCount() const noexcept {
    // Each tile has TILE_QUADS * TILE_QUADS * 2 triangles
    return getTileCount() * TILE_QUADS * TILE_QUADS * 2;
}

void TrianglePath::setWireframe(bool enabled) {
    wireframeEnabled_ = enabled;
}

} // namespace voxy::render

