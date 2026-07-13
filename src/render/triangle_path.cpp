// ═══════════════════════════════════════════════════════════════════════════════
// triangle_path.cpp - Triangle Terrain Rendering Path Implementation
// ═══════════════════════════════════════════════════════════════════════════════

#include "render/triangle_path.hpp"
#include "gpu/resources.hpp"
#include "core/log.hpp"
#include "render/frustum.hpp"

#include <glm/gtc/matrix_inverse.hpp>
#include <algorithm>
#include <vector>
#include <cstring>
#include <array>
#include <limits>

namespace voxy::render {

namespace {

struct CullUniforms {
    std::array<glm::uvec4, TrianglePath::LOD_COUNT> levels{};
    // Splitting thresholds for levels 0..3. Level 0 is never split.
    glm::vec4 splitDistances = glm::vec4(0.0f);
    glm::vec4 originAndCellScale = glm::vec4(0.0f);
    // x = total candidate count, y = base LOD step, z/w reserved.
    glm::uvec4 metadata = glm::uvec4(0u);
};

static_assert(sizeof(CullUniforms) == 112, "CullUniforms must be 112 bytes");

} // namespace

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
    waterParams = glm::vec4(-230.0f, 1.0f, 0.08f, 0.05f);
    waterColorA = glm::vec4(0.12f, 0.46f, 0.50f, 0.42f);
    waterColorB = glm::vec4(0.0f, 0.28f, 0.42f, 30.0f);
    waterMotion = glm::vec4(0.0f);

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
    // Invert view and projection separately, then compose. Inverting the combined
    // view-projection directly in fp32 amplifies rounding error from the non-uniform
    // Z scaling, which shows up as jitter for raycast elements at far distances.
    // inverse(proj * view) == inverse(view) * inverse(proj).
    invView = glm::inverse(view);
    glm::mat4 invProj = glm::inverse(proj);
    invViewProj = invView * invProj;
    cameraPos = glm::vec4(position, 1.0f);

    // Compute inverse projection parameters for ray generation
    // invProjParams.xy = tan(fov/2) * aspect, tan(fov/2) for NDC to view-space ray
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

void CameraUniforms::setWater(bool enabled, float height, const glm::vec3& shallowColor,
                              const glm::vec3& deepColor, float roughness,
                              float waveStrength, float reflectionStrength, float shoreFade) {
    waterParams = glm::vec4(height, enabled ? 1.0f : 0.0f,
                            std::max(waveStrength, 0.0f),
                            std::clamp(roughness, 0.02f, 1.0f));
    waterColorA = glm::vec4(glm::clamp(shallowColor, glm::vec3(0.0f), glm::vec3(1.0f)),
                            std::clamp(reflectionStrength, 0.0f, 1.0f));
    waterColorB = glm::vec4(glm::clamp(deepColor, glm::vec3(0.0f), glm::vec3(1.0f)),
                            std::max(shoreFade, 0.001f));
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
    , indirectTemplateBuffer_(other.indirectTemplateBuffer_)
    , visibleIndicesBuffer_(other.visibleIndicesBuffer_)
    , tileBoundsBuffer_(other.tileBoundsBuffer_)
    , cullUniformBuffer_(other.cullUniformBuffer_)
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
    , levelTilesX_(other.levelTilesX_)
    , levelTilesY_(other.levelTilesY_)
    , levelCandidateOffsets_(other.levelCandidateOffsets_)
    , totalCandidateCount_(other.totalCandidateCount_)
    , visibilitySegmentCapacity_(other.visibilitySegmentCapacity_)
    , uniformsDirty_(other.uniformsDirty_)
    , cullUniformsDirty_(other.cullUniformsDirty_)
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
    other.indirectTemplateBuffer_ = nullptr;
    other.visibleIndicesBuffer_ = nullptr;
    other.tileBoundsBuffer_ = nullptr;
    other.cullUniformBuffer_ = nullptr;
    other.heightmapView_ = nullptr;
    other.albedoView_ = nullptr;
    other.lightmapView_ = nullptr;
    other.sampler_ = nullptr;
    other.visibilitySegmentCapacity_ = 0;
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
        indirectTemplateBuffer_ = other.indirectTemplateBuffer_;
        visibleIndicesBuffer_ = other.visibleIndicesBuffer_;
        tileBoundsBuffer_ = other.tileBoundsBuffer_;
        cullUniformBuffer_ = other.cullUniformBuffer_;
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
        levelTilesX_ = other.levelTilesX_;
        levelTilesY_ = other.levelTilesY_;
        levelCandidateOffsets_ = other.levelCandidateOffsets_;
        totalCandidateCount_ = other.totalCandidateCount_;
        visibilitySegmentCapacity_ = other.visibilitySegmentCapacity_;
        uniformsDirty_ = other.uniformsDirty_;
        cullUniformsDirty_ = other.cullUniformsDirty_;
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
        other.indirectTemplateBuffer_ = nullptr;
        other.visibleIndicesBuffer_ = nullptr;
        other.tileBoundsBuffer_ = nullptr;
        other.cullUniformBuffer_ = nullptr;
        other.heightmapView_ = nullptr;
        other.albedoView_ = nullptr;
        other.lightmapView_ = nullptr;
        other.sampler_ = nullptr;
        other.visibilitySegmentCapacity_ = 0;
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
    if (indirectTemplateBuffer_) {
        wgpuBufferRelease(indirectTemplateBuffer_);
        indirectTemplateBuffer_ = nullptr;
    }
    if (visibleIndicesBuffer_) {
        wgpuBufferRelease(visibleIndicesBuffer_);
        visibleIndicesBuffer_ = nullptr;
    }
    if (tileBoundsBuffer_) {
        wgpuBufferRelease(tileBoundsBuffer_);
        tileBoundsBuffer_ = nullptr;
    }
    if (cullUniformBuffer_) {
        wgpuBufferRelease(cullUniformBuffer_);
        cullUniformBuffer_ = nullptr;
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
    // A 32x32 patch uses 16-bit indices and fits in a small reusable index stream.
    // Four downward skirts hide T-junctions between adjacent adaptive LODs.
    std::vector<uint16_t> indices;
    indices.reserve(INDICES_PER_TILE);
    
    for (uint32_t y = 0; y < TILE_QUADS; ++y) {
        for (uint32_t x = 0; x < TILE_QUADS; ++x) {
            // Vertices for this quad (row-major order)
            uint16_t topLeft = static_cast<uint16_t>(y * TILE_VERTS + x);
            uint16_t topRight = static_cast<uint16_t>(topLeft + 1);
            uint16_t bottomLeft = static_cast<uint16_t>(topLeft + TILE_VERTS);
            uint16_t bottomRight = static_cast<uint16_t>(bottomLeft + 1);
            
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

    const uint16_t skirtStart = static_cast<uint16_t>(BASE_VERTICES_PER_TILE);
    auto appendSkirtEdge = [&](uint32_t edge, auto baseVertexAt) {
        const uint16_t edgeSkirtStart = static_cast<uint16_t>(skirtStart + edge * TILE_VERTS);
        for (uint32_t i = 0; i < TILE_QUADS; ++i) {
            const uint16_t base0 = static_cast<uint16_t>(baseVertexAt(i));
            const uint16_t base1 = static_cast<uint16_t>(baseVertexAt(i + 1));
            const uint16_t skirt0 = static_cast<uint16_t>(edgeSkirtStart + i);
            const uint16_t skirt1 = static_cast<uint16_t>(edgeSkirtStart + i + 1);

            // Edge vertices are supplied clockwise, so this winding faces outward.
            indices.push_back(base0);
            indices.push_back(skirt1);
            indices.push_back(skirt0);
            indices.push_back(base0);
            indices.push_back(base1);
            indices.push_back(skirt1);
        }
    };

    appendSkirtEdge(0, [](uint32_t i) { return i; });
    appendSkirtEdge(1, [](uint32_t i) { return i * TILE_VERTS + TILE_QUADS; });
    appendSkirtEdge(2, [](uint32_t i) { return TILE_QUADS * TILE_VERTS + (TILE_QUADS - i); });
    appendSkirtEdge(3, [](uint32_t i) { return (TILE_QUADS - i) * TILE_VERTS; });

    if (indices.size() != INDICES_PER_TILE) {
        LOG_ERROR("Terrain index count mismatch: expected {}, generated {}",
                  INDICES_PER_TILE, indices.size());
        return false;
    }
    
    // Create index buffer
    gpu::BufferDesc bufferDesc = gpu::BufferDesc::index(
        indices.size() * sizeof(uint16_t),
        "terrain_index_buffer"
    );
    
    indexBuffer_ = gpu::createBufferWithData(
        device_, queue_, bufferDesc,
        std::span<const uint16_t>(indices)
    );
    
    if (!indexBuffer_) {
        LOG_ERROR("Failed to create terrain index buffer");
        return false;
    }
    
    LOG_DEBUG("Created terrain index buffer: {} indices ({} bytes)",
              indices.size(), indices.size() * sizeof(uint16_t));
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
            // Per-draw dynamic offsets avoid the optional WebGPU
            // indirect-first-instance feature.
            .storageBuffer(true, true, sizeof(uint32_t)) // ReadOnly Storage
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
        gpu::BindGroupEntry(5).buffer(
            visibleIndicesBuffer_, 0,
            static_cast<uint64_t>(visibilitySegmentCapacity_) * sizeof(uint32_t))
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

void TrianglePath::setHeightmap(WGPUTextureView heightmapView, uint32_t width, uint32_t height,
                                std::span<const uint16_t> heightData) {
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

    if (!rebuildTerrainBuffers(heightData)) {
        LOG_ERROR("Failed to build adaptive terrain buffers");
    }
    
    LOG_DEBUG("Set heightmap: {}x{}, tiles: {}x{}", width, height, tilesX_, tilesY_);
}

void TrianglePath::calculateTileCount() {
    if (heightmapWidth_ == 0 || heightmapHeight_ == 0) {
        tilesX_ = 0;
        tilesY_ = 0;
        return;
    }
    
    const uint32_t cellsX = std::max(heightmapWidth_ - 1, 1u);
    const uint32_t cellsY = std::max(heightmapHeight_ - 1, 1u);
    const uint32_t baseStep = std::max(config_.lodStep, 1u);

    totalCandidateCount_ = 0;
    for (uint32_t lod = 0; lod < LOD_COUNT; ++lod) {
        const uint32_t step = baseStep << lod;
        const uint32_t tileSpan = TILE_QUADS * step;
        levelCandidateOffsets_[lod] = totalCandidateCount_;
        levelTilesX_[lod] = std::max((cellsX + tileSpan - 1) / tileSpan, 1u);
        levelTilesY_[lod] = std::max((cellsY + tileSpan - 1) / tileSpan, 1u);
        totalCandidateCount_ += levelTilesX_[lod] * levelTilesY_[lod];
    }

    tilesX_ = levelTilesX_[0];
    tilesY_ = levelTilesY_[0];
    cullUniformsDirty_ = true;
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

void TrianglePath::setCameraUniforms(const CameraUniforms& uniforms) {
    uniforms_ = uniforms;
    uniformsDirty_ = true;
}

void TrianglePath::setLODStep(uint32_t step) {
    step = std::max(step, 1u);
    if (config_.lodStep != step) {
        if (heightmapWidth_ != 0 || heightmapHeight_ != 0) {
            LOG_WARN("Base triangle LOD step must be configured before binding the heightmap");
            return;
        }
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

void TrianglePath::updateCullUniformBuffer() {
    if (!cullUniformBuffer_ || !queue_) return;

    const uint32_t terrainWidth = std::max(heightmapWidth_, 1u);
    const uint32_t terrainHeight = std::max(heightmapHeight_, 1u);
    const uint32_t step = std::max(config_.lodStep, 1u);
    const float cellScale = config_.cellScale;
    const glm::vec2 terrainSize(static_cast<float>(terrainWidth),
                                static_cast<float>(terrainHeight));
    const glm::vec2 origin = 0.5f * (terrainSize - glm::vec2(1.0f, 1.0f)) * cellScale;

    CullUniforms uniforms;
    for (uint32_t lod = 0; lod < LOD_COUNT; ++lod) {
        uniforms.levels[lod] = glm::uvec4(
            levelTilesX_[lod], levelTilesY_[lod], levelCandidateOffsets_[lod], 0u);
    }
    // A one-metre heightfield cell is about one pixel at each transition for a
    // 720p, 60-degree view. Distances scale with world-space cell size.
    uniforms.splitDistances = glm::vec4(
        0.0f,
        512.0f * cellScale,
        1536.0f * cellScale,
        4096.0f * cellScale);
    uniforms.originAndCellScale = glm::vec4(origin.x, origin.y, cellScale, 0.0f);
    uniforms.metadata = glm::uvec4(
        totalCandidateCount_, step, visibilitySegmentCapacity_, 0u);

    gpu::writeBuffer(queue_, cullUniformBuffer_, 0, uniforms);
    cullUniformsDirty_ = false;
}

// ─────────────────────────────────────────────────────────────────────────────
// Rendering
// ─────────────────────────────────────────────────────────────────────────────

void TrianglePath::render(WGPUCommandEncoder encoder, WGPUTextureView colorView, 
                          WGPUTextureView depthView) {
    if (!pipeline_ || !indexBuffer_ || !computePipeline_ || !indirectBuffer_ ||
        !indirectTemplateBuffer_ || !visibleIndicesBuffer_ || !tileBoundsBuffer_) {
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

    if (cullUniformsDirty_) {
        updateCullUniformBuffer();
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

    // Restore all 32 draw records in one copy. This resets atomic instance counts;
    // visibility segments are selected later with dynamic storage offsets.
    constexpr uint64_t indirectBytes = INDIRECT_DRAW_COUNT * 5u * sizeof(uint32_t);
    wgpuCommandEncoderCopyBufferToBuffer(
        encoder, indirectTemplateBuffer_, 0, indirectBuffer_, 0, indirectBytes);

    // ─────────────────────────────────────────────────────────────────────────
    // 2. Compute Culling Pass
    // ─────────────────────────────────────────────────────────────────────────

    WGPUComputePassDescriptor computePassDesc{};
    WGPU_SET_LABEL(computePassDesc, "terrain_cull_pass");

    WGPUComputePassEncoder computePass = wgpuCommandEncoderBeginComputePass(encoder, &computePassDesc);
    wgpuComputePassEncoderSetPipeline(computePass, computePipeline_);
    wgpuComputePassEncoderSetBindGroup(computePass, 0, computeBindGroup_, 0, nullptr);

    uint32_t workgroups = (totalCandidateCount_ + 63) / 64;
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

    // Set index buffer
    wgpuRenderPassEncoderSetIndexBuffer(renderPass, indexBuffer_, WGPUIndexFormat_Uint16,
                                        0, INDICES_PER_TILE * sizeof(uint16_t));

    // Distance bins are emitted nearest first. Each bin contains four adaptive
    // LOD draws, producing coarse front-to-back ordering without a GPU sort.
    for (uint32_t draw = 0; draw < INDIRECT_DRAW_COUNT; ++draw) {
        const uint32_t visibilityOffset =
            draw * visibilitySegmentCapacity_ * sizeof(uint32_t);
        wgpuRenderPassEncoderSetBindGroup(
            renderPass, 0, bindGroup_, 1, &visibilityOffset);
        wgpuRenderPassEncoderDrawIndexedIndirect(
            renderPass, indirectBuffer_, draw * 5u * sizeof(uint32_t));
    }
    
    wgpuRenderPassEncoderEnd(renderPass);
    wgpuRenderPassEncoderRelease(renderPass);
}

// ─────────────────────────────────────────────────────────────────────────────
// Compute Resource Creation
// ─────────────────────────────────────────────────────────────────────────────

bool TrianglePath::createComputeResources(const TrianglePathConfig& config) {
    // Terrain-sized indirect, visibility, and bounds buffers are created after
    // the heightmap dimensions and CPU samples are supplied.

    // 1. Create Cull Constants Uniform Buffer
    gpu::BufferDesc cullDesc = gpu::BufferDesc::uniform(
        gpu::alignUniformBufferSize(sizeof(CullUniforms)),
        "terrain_cull_uniforms"
    );

    cullUniformBuffer_ = gpu::createBuffer(device_, cullDesc);
    if (!cullUniformBuffer_) return false;
    updateCullUniformBuffer();

    // 2. Load Compute Shader
    std::filesystem::path cullShaderPath = config.shaderPath.parent_path() / "cull_terrain.wgsl";
    computeModule_ = gpu::loadShaderModule(device_, cullShaderPath, "cull_terrain.wgsl");
    if (!computeModule_) return false;

    // 3. Create Compute Bind Group Layout
    // @group(0) @binding(0) var<uniform> camera : CameraUniforms;
    // @group(0) @binding(1) var<storage, read_write> indirectArgs : IndirectArgs;
    // @group(0) @binding(2) var<storage, read_write> visibleIndices : array<u32>;
    // @group(0) @binding(3) var<uniform> cull : CullUniforms;
    // @group(0) @binding(4) var<storage, read> tileBounds : array<u32>;
    std::array<gpu::BindGroupLayoutEntry, 5> entries = {
        gpu::BindGroupLayoutEntry(0)
            .computeVisible()
            .uniformBuffer(false, sizeof(CameraUniforms)),
        gpu::BindGroupLayoutEntry(1)
            .computeVisible()
            .storageBuffer(false), // read_write
        gpu::BindGroupLayoutEntry(2)
            .computeVisible()
            .storageBuffer(false), // read_write
        gpu::BindGroupLayoutEntry(3)
            .computeVisible()
            .uniformBuffer(false, sizeof(CullUniforms)),
        gpu::BindGroupLayoutEntry(4)
            .computeVisible()
            .storageBuffer(true)
    };

    computeBindGroupLayout_ = gpu::createBindGroupLayout(device_, entries, "cull_compute_layout");
    if (!computeBindGroupLayout_) return false;

    // 4. Create Compute Pipeline
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

    return true;
}

void TrianglePath::releaseTerrainBuffers() {
    if (bindGroup_) {
        wgpuBindGroupRelease(bindGroup_);
        bindGroup_ = nullptr;
    }
    if (computeBindGroup_) {
        wgpuBindGroupRelease(computeBindGroup_);
        computeBindGroup_ = nullptr;
    }
    if (indirectBuffer_) {
        wgpuBufferRelease(indirectBuffer_);
        indirectBuffer_ = nullptr;
    }
    if (indirectTemplateBuffer_) {
        wgpuBufferRelease(indirectTemplateBuffer_);
        indirectTemplateBuffer_ = nullptr;
    }
    if (visibleIndicesBuffer_) {
        wgpuBufferRelease(visibleIndicesBuffer_);
        visibleIndicesBuffer_ = nullptr;
    }
    if (tileBoundsBuffer_) {
        wgpuBufferRelease(tileBoundsBuffer_);
        tileBoundsBuffer_ = nullptr;
    }
}

bool TrianglePath::rebuildTerrainBuffers(std::span<const uint16_t> heightData) {
    releaseTerrainBuffers();

    if (totalCandidateCount_ == 0) {
        return false;
    }

    // Every draw gets one 256-byte-aligned visibility segment. Indirect draws
    // can then keep firstInstance at zero and remain valid on baseline WebGPU.
    const uint32_t largestLevel = levelTilesX_[0] * levelTilesY_[0];
    constexpr uint32_t entriesPerAlignment = 256u / sizeof(uint32_t);
    visibilitySegmentCapacity_ =
        (largestLevel + entriesPerAlignment - 1u) & ~(entriesPerAlignment - 1u);
    const uint64_t visibleEntryCount =
        static_cast<uint64_t>(visibilitySegmentCapacity_) * INDIRECT_DRAW_COUNT;
    gpu::BufferDesc visibleDesc = gpu::BufferDesc::storage(
        visibleEntryCount * sizeof(uint32_t), false, "terrain_visible_indices_buffer");
    visibleIndicesBuffer_ = gpu::createBuffer(device_, visibleDesc);
    if (!visibleIndicesBuffer_) {
        releaseTerrainBuffers();
        return false;
    }

    // Build exact min/max bounds. Level zero scans all source samples covered by
    // each patch; coarser levels combine four children, preserving conservatism.
    std::vector<uint32_t> packedBounds(totalCandidateCount_, 0xffff0000u);
    const bool hasHeightData = heightData.size() ==
        static_cast<size_t>(heightmapWidth_) * static_cast<size_t>(heightmapHeight_);

    if (hasHeightData) {
        const uint32_t baseStep = std::max(config_.lodStep, 1u);
        const uint32_t baseSpan = TILE_QUADS * baseStep;
        for (uint32_t tileY = 0; tileY < levelTilesY_[0]; ++tileY) {
            for (uint32_t tileX = 0; tileX < levelTilesX_[0]; ++tileX) {
                const uint32_t startX = tileX * baseSpan;
                const uint32_t startY = tileY * baseSpan;
                const uint32_t endX = std::min(startX + baseSpan, heightmapWidth_ - 1);
                const uint32_t endY = std::min(startY + baseSpan, heightmapHeight_ - 1);
                uint16_t minHeight = std::numeric_limits<uint16_t>::max();
                uint16_t maxHeight = 0;

                for (uint32_t y = startY; y <= endY; ++y) {
                    const size_t row = static_cast<size_t>(y) * heightmapWidth_;
                    for (uint32_t x = startX; x <= endX; ++x) {
                        const uint16_t h = heightData[row + x];
                        minHeight = std::min(minHeight, h);
                        maxHeight = std::max(maxHeight, h);
                    }
                }

                const uint32_t index = levelCandidateOffsets_[0] +
                    tileY * levelTilesX_[0] + tileX;
                packedBounds[index] = static_cast<uint32_t>(minHeight) |
                    (static_cast<uint32_t>(maxHeight) << 16);
            }
        }

        for (uint32_t lod = 1; lod < LOD_COUNT; ++lod) {
            for (uint32_t tileY = 0; tileY < levelTilesY_[lod]; ++tileY) {
                for (uint32_t tileX = 0; tileX < levelTilesX_[lod]; ++tileX) {
                    uint16_t minHeight = std::numeric_limits<uint16_t>::max();
                    uint16_t maxHeight = 0;
                    for (uint32_t childY = 0; childY < 2; ++childY) {
                        for (uint32_t childX = 0; childX < 2; ++childX) {
                            const uint32_t x = tileX * 2 + childX;
                            const uint32_t y = tileY * 2 + childY;
                            if (x >= levelTilesX_[lod - 1] || y >= levelTilesY_[lod - 1]) {
                                continue;
                            }
                            const uint32_t childIndex = levelCandidateOffsets_[lod - 1] +
                                y * levelTilesX_[lod - 1] + x;
                            const uint32_t bounds = packedBounds[childIndex];
                            minHeight = std::min(minHeight, static_cast<uint16_t>(bounds & 0xffffu));
                            maxHeight = std::max(maxHeight, static_cast<uint16_t>(bounds >> 16));
                        }
                    }
                    const uint32_t index = levelCandidateOffsets_[lod] +
                        tileY * levelTilesX_[lod] + tileX;
                    packedBounds[index] = static_cast<uint32_t>(minHeight) |
                        (static_cast<uint32_t>(maxHeight) << 16);
                }
            }
        }
    } else {
        LOG_WARN("Triangle path did not receive CPU height samples; using conservative bounds");
    }

    gpu::BufferDesc boundsDesc = gpu::BufferDesc::storage(
        packedBounds.size() * sizeof(uint32_t), true, "terrain_tile_bounds_buffer");
    tileBoundsBuffer_ = gpu::createBufferWithData(
        device_, queue_, boundsDesc, std::span<const uint32_t>(packedBounds));
    if (!tileBoundsBuffer_) {
        releaseTerrainBuffers();
        return false;
    }

    std::vector<uint32_t> indirectData(INDIRECT_DRAW_COUNT * 5u, 0u);
    for (uint32_t bin = 0; bin < DISTANCE_BIN_COUNT; ++bin) {
        for (uint32_t lod = 0; lod < LOD_COUNT; ++lod) {
            const uint32_t draw = bin * LOD_COUNT + lod;
            // Draw only the grid. Skirts are intentionally disabled because a
            // free-fly camera below the heightfield can intersect their walls.
            indirectData[draw * 5 + 0] = BASE_INDICES_PER_TILE;
            // firstInstance stays zero: non-zero values require the optional
            // WebGPU indirect-first-instance feature.
            indirectData[draw * 5 + 4] = 0u;
        }
    }

    gpu::BufferDesc indirectDesc;
    indirectDesc.label = "terrain_indirect_buffer";
    indirectDesc.size = indirectData.size() * sizeof(uint32_t);
    indirectDesc.usage = WGPUBufferUsage_Indirect | WGPUBufferUsage_Storage |
                         WGPUBufferUsage_CopyDst;
    indirectBuffer_ = gpu::createBufferWithData(
        device_, queue_, indirectDesc, std::span<const uint32_t>(indirectData));

    gpu::BufferDesc templateDesc;
    templateDesc.label = "terrain_indirect_template_buffer";
    templateDesc.size = indirectData.size() * sizeof(uint32_t);
    templateDesc.usage = WGPUBufferUsage_CopySrc;
    indirectTemplateBuffer_ = gpu::createBufferWithData(
        device_, queue_, templateDesc, std::span<const uint32_t>(indirectData));

    if (!indirectBuffer_ || !indirectTemplateBuffer_) {
        releaseTerrainBuffers();
        return false;
    }

    cullUniformsDirty_ = true;
    bindGroupDirty_ = true;
    updateCullUniformBuffer();
    updateComputeBindGroup();
    return computeBindGroup_ != nullptr;
}

void TrianglePath::updateComputeBindGroup() {
    if (!computeBindGroupLayout_ || !indirectBuffer_ || !visibleIndicesBuffer_ ||
        !tileBoundsBuffer_ || !uniformBuffer_ || !cullUniformBuffer_) return;

    if (computeBindGroup_) {
        wgpuBindGroupRelease(computeBindGroup_);
        computeBindGroup_ = nullptr;
    }

    std::array<gpu::BindGroupEntry, 5> entries = {
        gpu::BindGroupEntry(0).buffer(uniformBuffer_, 0, sizeof(CameraUniforms)),
        gpu::BindGroupEntry(1).buffer(
            indirectBuffer_, 0, INDIRECT_DRAW_COUNT * 5u * sizeof(uint32_t)),
        gpu::BindGroupEntry(2).buffer(visibleIndicesBuffer_, 0, WGPU_WHOLE_SIZE),
        gpu::BindGroupEntry(3).buffer(cullUniformBuffer_, 0, sizeof(CullUniforms)),
        gpu::BindGroupEntry(4).buffer(tileBoundsBuffer_, 0, WGPU_WHOLE_SIZE)
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
