// ═══════════════════════════════════════════════════════════════════════════════
// triangle_path.cpp - Triangle Terrain Rendering Path Implementation
// ═══════════════════════════════════════════════════════════════════════════════

#include "render/triangle_path.hpp"
#include "physics/terrain_topology.hpp"
#include "gpu/resources.hpp"
#include "core/log.hpp"
#include "render/frustum.hpp"

#include <glm/gtc/matrix_inverse.hpp>
#include <algorithm>
#include <cmath>
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

constexpr uint32_t kMaximumTerrainDimension = 8'192u;

bool finiteVec(const glm::vec2& value) noexcept {
    return std::isfinite(value.x) && std::isfinite(value.y);
}

bool finiteVec(const glm::vec3& value) noexcept {
    return std::isfinite(value.x) && std::isfinite(value.y)
        && std::isfinite(value.z);
}

bool finiteVec(const glm::vec4& value) noexcept {
    return std::isfinite(value.x) && std::isfinite(value.y)
        && std::isfinite(value.z) && std::isfinite(value.w);
}

bool finiteMat(const glm::mat4& value) noexcept {
    for (glm::length_t column = 0; column < 4; ++column) {
        if (!finiteVec(value[column])) return false;
    }
    return true;
}

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
    waterParams = glm::vec4(-230.0f, 1.0f, 1.0f, 0.05f);
    waterColorA = glm::vec4(0.12f, 0.46f, 0.50f, 0.42f);
    waterColorB = glm::vec4(0.0f, 0.28f, 0.42f, 30.0f);
    waterMotion = glm::vec4(0.0f);
    lightingColor = glm::vec4(1.0f, 0.95f, 0.9f, 1.0f);
    ambientExposure = glm::vec4(0.1f, 0.12f, 0.15f, 1.0f);
    fogColor = glm::vec4(0.36f, 0.58f, 0.64f, 0.0f);
    waterOptics = glm::vec4(1.31f, 0.20f, 1.0f, 1.0f);
    waterFoam = glm::vec4(261.0f, 0.30f, 0.21f, 1500.0f);
    waterSpectrum = glm::vec4(1949.0f, 326.0f, 0.0f, 0.0f);

    // Default frustum (all zeros)
    std::memset(frustumPlanes, 0, sizeof(frustumPlanes));
}

bool CameraUniforms::setTerrain(
    uint32_t width, uint32_t height, float heightScale, float cellScale,
    float step, float fogDensity) {
    if (width == 0u || height == 0u
        || width > kMaximumTerrainDimension
        || height > kMaximumTerrainDimension
        || !std::isfinite(heightScale) || heightScale <= 0.0f
        || !std::isfinite(cellScale) || cellScale <= 0.0f
        || !std::isfinite(step) || step <= 0.0f
        || !std::isfinite(fogDensity) || fogDensity < 0.0f) {
        return false;
    }
    terrainSize = glm::vec2(static_cast<float>(width), static_cast<float>(height));
    invTerrainSize = glm::vec2(1.0f / terrainSize.x, 1.0f / terrainSize.y);
    metrics = glm::vec4(heightScale, cellScale, step, fogDensity);
    return true;
}

bool CameraUniforms::setCamera(
    const glm::mat4& view, const glm::mat4& proj,
    const glm::vec3& position) {
    if (!finiteMat(view) || !finiteMat(proj) || !finiteVec(position)) {
        return false;
    }
    const glm::mat4 nextViewProj = proj * view;
    // Invert view and projection separately, then compose. Inverting the combined
    // view-projection directly in fp32 amplifies rounding error from the non-uniform
    // Z scaling, which shows up as jitter for raycast elements at far distances.
    // inverse(proj * view) == inverse(view) * inverse(proj).
    const glm::mat4 nextInvView = glm::inverse(view);
    const glm::mat4 invProj = glm::inverse(proj);
    const glm::mat4 nextInvViewProj = nextInvView * invProj;
    if (!finiteMat(nextViewProj) || !finiteMat(nextInvView)
        || !finiteMat(invProj) || !finiteMat(nextInvViewProj)) {
        return false;
    }

    const Frustum frustum = Frustum::fromViewProj(nextViewProj);
    if (!frustum.valid()) return false;

    // Compute inverse projection parameters for ray generation
    // invProjParams.xy = tan(fov/2) * aspect, tan(fov/2) for NDC to view-space ray
    // Extract from inverse projection matrix
    viewProj = nextViewProj;
    invView = nextInvView;
    invViewProj = nextInvViewProj;
    cameraPos = glm::vec4(position, 1.0f);
    invProjParams.x = invProj[0][0];  // Scale for X
    invProjParams.y = invProj[1][1];  // Scale for Y

    // Update Frustum planes
    for(size_t i=0; i<6; ++i) {
        frustumPlanes[i] = glm::vec4(frustum.planes[i].normal, frustum.planes[i].distance);
    }
    return true;
}

bool CameraUniforms::setLightDirection(
    const glm::vec3& worldDir, const glm::mat4& view, float ambient) {
    if (!finiteVec(worldDir) || !finiteMat(view)
        || !std::isfinite(ambient) || ambient < 0.0f
        || glm::dot(worldDir, worldDir)
            <= std::numeric_limits<float>::min()) {
        return false;
    }
    // Transform world-space direction to view-space
    const glm::vec3 viewDir = glm::vec3(view * glm::vec4(worldDir, 0.0f));
    if (!finiteVec(viewDir)
        || glm::dot(viewDir, viewDir)
            <= std::numeric_limits<float>::min()) {
        return false;
    }
    lightDirVS = glm::vec4(glm::normalize(viewDir), ambient);
    lightDirWS = glm::vec4(glm::normalize(worldDir), 0.0f); // Store world-space dir
    return true;
}

bool CameraUniforms::setWater(
    bool enabled, float height, const glm::vec3& shallowColor,
    const glm::vec3& deepColor, float roughness, float waveStrength,
    float reflectionStrength, float shoreFade) {
    if (!std::isfinite(height) || !finiteVec(shallowColor)
        || !finiteVec(deepColor) || !std::isfinite(roughness)
        || !std::isfinite(waveStrength)
        || !std::isfinite(reflectionStrength)
        || !std::isfinite(shoreFade)) {
        return false;
    }
    waterParams = glm::vec4(height, enabled ? 1.0f : 0.0f,
                            std::max(waveStrength, 0.0f),
                            std::clamp(roughness, 0.02f, 1.0f));
    waterColorA = glm::vec4(glm::clamp(shallowColor, glm::vec3(0.0f), glm::vec3(1.0f)),
                            std::clamp(reflectionStrength, 0.0f, 1.0f));
    waterColorB = glm::vec4(glm::clamp(deepColor, glm::vec3(0.0f), glm::vec3(1.0f)),
                            std::max(shoreFade, 0.001f));
    return true;
}

bool CameraUniforms::setRendererMaterial(
    const glm::vec3& sunColor, float sunIntensity,
    const glm::vec3& ambientColor, const glm::vec3& atmosphericFogColor,
    float exposure, float waterIor, float waterDistortion,
    float waterAbsorptionScale, float waterScatterStrength, float foamSize,
    float foamOpacity, float foamCoverage, float reflectionDistance,
    const glm::vec2& spectrumPatchLengths) {
    if (!finiteVec(sunColor) || !std::isfinite(sunIntensity)
        || !finiteVec(ambientColor) || !finiteVec(atmosphericFogColor)
        || !std::isfinite(exposure) || !std::isfinite(waterIor)
        || !std::isfinite(waterDistortion)
        || !std::isfinite(waterAbsorptionScale)
        || !std::isfinite(waterScatterStrength)
        || !std::isfinite(foamSize) || !std::isfinite(foamOpacity)
        || !std::isfinite(foamCoverage)
        || !std::isfinite(reflectionDistance)
        || !finiteVec(spectrumPatchLengths)) {
        return false;
    }
    lightingColor = glm::vec4(glm::max(sunColor, glm::vec3(0.0f)),
                              std::max(sunIntensity, 0.0f));
    ambientExposure = glm::vec4(glm::max(ambientColor, glm::vec3(0.0f)),
                                std::max(exposure, 0.0f));
    fogColor = glm::vec4(glm::max(atmosphericFogColor, glm::vec3(0.0f)), 0.0f);
    waterOptics = glm::vec4(std::clamp(waterIor, 1.0f, 2.0f),
                            std::max(waterDistortion, 0.0f),
                            std::max(waterAbsorptionScale, 0.0f),
                            std::max(waterScatterStrength, 0.0f));
    waterFoam = glm::vec4(std::max(foamSize, 1.0f),
                          std::clamp(foamOpacity, 0.0f, 1.0f),
                          std::clamp(foamCoverage, 0.0f, 1.0f),
                          std::max(reflectionDistance, 1.0f));
    waterSpectrum = glm::vec4(glm::max(spectrumPatchLengths,
                                      glm::vec2(1.0f)),
                              0.0f, 0.0f);
    return true;
}

bool CameraUniforms::setWaterTime(float seconds) {
    if (!std::isfinite(seconds)) return false;
    waterMotion.x = seconds;
    return true;
}

bool CameraUniforms::setCameraWaterSurfaceOffset(float offset) {
    if (!std::isfinite(offset)) return false;
    waterMotion.y = offset;
    waterMotion.z = waterParams.y > 0.5f
        && cameraPos.y < waterParams.x + offset ? 1.0f : 0.0f;
    return true;
}

bool CameraUniforms::isValid() const noexcept {
    if (!finiteMat(viewProj) || !finiteMat(invViewProj)
        || !finiteMat(invView) || !finiteVec(terrainSize)
        || !finiteVec(invTerrainSize) || !finiteVec(metrics)
        || !finiteVec(cameraPos) || !finiteVec(invProjParams)
        || !finiteVec(lightDirVS) || !finiteVec(lightDirWS)
        || !finiteVec(waterParams) || !finiteVec(waterColorA)
        || !finiteVec(waterColorB) || !finiteVec(waterMotion)
        || !finiteVec(lightingColor) || !finiteVec(ambientExposure)
        || !finiteVec(fogColor) || !finiteVec(waterOptics)
        || !finiteVec(waterFoam) || !finiteVec(waterSpectrum)) {
        return false;
    }
    for (const glm::vec4& plane : frustumPlanes) {
        if (!finiteVec(plane)) return false;
    }
    return terrainSize.x > 0.0f && terrainSize.y > 0.0f
        && invTerrainSize.x > 0.0f && invTerrainSize.y > 0.0f
        && metrics.x > 0.0f && metrics.y > 0.0f
        && metrics.z > 0.0f && metrics.w >= 0.0f
        && std::abs(glm::determinant(viewProj))
            > std::numeric_limits<float>::min()
        && std::abs(glm::determinant(invViewProj))
            > std::numeric_limits<float>::min()
        && std::abs(glm::determinant(invView))
            > std::numeric_limits<float>::min();
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
    if (!std::isfinite(config.heightScale) || config.heightScale <= 0.0f
        || !std::isfinite(config.cellScale) || config.cellScale <= 0.0f
        || !std::isfinite(config.fogDensity) || config.fogDensity < 0.0f
        || config.lodStep == 0u) {
        LOG_ERROR("TrianglePath::init: invalid renderer configuration");
        return false;
    }
    
    device_ = device;
    queue_ = queue;
    config_ = config;
    
    // Initialize uniforms with config values
    if (!uniforms_.setTerrain(
            256, 256, config.heightScale, config.cellScale,
            static_cast<float>(config.lodStep), config.fogDensity)) {
        shutdown();
        return false;
    }
    
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
            
            const std::array<uint16_t, 4> corners{
                topLeft, topRight, bottomLeft, bottomRight};
            for (const auto& triangle :
                 physics::terrain_topology::kCellTriangles) {
                for (const auto corner : triangle) {
                    indices.push_back(corners[static_cast<size_t>(corner)]);
                }
            }
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
    if (!updateUniformBuffer()) return false;
    
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
    
    WGPUBindGroup nextBindGroup = gpu::createBindGroup(
        device_, bindGroupLayout_, entries, "terrain_bind_group");
    
    if (!nextBindGroup) {
        LOG_ERROR("Failed to create terrain bind group");
        return false;
    }

    if (bindGroup_) wgpuBindGroupRelease(bindGroup_);
    bindGroup_ = nextBindGroup;
    
    bindGroupDirty_ = false;
    LOG_DEBUG("Created terrain bind group");
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Heightmap Binding
// ─────────────────────────────────────────────────────────────────────────────

bool TrianglePath::setHeightmap(
    WGPUTextureView heightmapView, uint32_t width, uint32_t height,
    std::span<const uint16_t> heightData) {
    if (!device_ || !heightmapView || width == 0u || height == 0u
        || width > kMaximumTerrainDimension
        || height > kMaximumTerrainDimension) {
        LOG_ERROR("TrianglePath::setHeightmap: invalid terrain binding");
        return false;
    }
    const size_t sampleCount =
        static_cast<size_t>(width) * static_cast<size_t>(height);
    if (!heightData.empty() && heightData.size() != sampleCount) {
        LOG_ERROR("TrianglePath::setHeightmap: expected {} CPU samples, got {}",
                  sampleCount, heightData.size());
        return false;
    }

    const WGPUTextureView oldHeightmapView = heightmapView_;
    const uint32_t oldWidth = heightmapWidth_;
    const uint32_t oldHeight = heightmapHeight_;
    const CameraUniforms oldUniforms = uniforms_;
    const uint32_t oldTilesX = tilesX_;
    const uint32_t oldTilesY = tilesY_;
    const auto oldLevelTilesX = levelTilesX_;
    const auto oldLevelTilesY = levelTilesY_;
    const auto oldLevelCandidateOffsets = levelCandidateOffsets_;
    const uint32_t oldCandidateCount = totalCandidateCount_;
    const bool oldUniformsDirty = uniformsDirty_;
    const bool oldCullUniformsDirty = cullUniformsDirty_;
    const bool oldBindGroupDirty = bindGroupDirty_;

    heightmapView_ = heightmapView;
    heightmapWidth_ = width;
    heightmapHeight_ = height;
    
    // Update uniforms with terrain size
    if (!uniforms_.setTerrain(
            width, height, config_.heightScale, config_.cellScale,
            static_cast<float>(config_.lodStep), config_.fogDensity)) {
        heightmapView_ = oldHeightmapView;
        heightmapWidth_ = oldWidth;
        heightmapHeight_ = oldHeight;
        uniforms_ = oldUniforms;
        return false;
    }
    uniformsDirty_ = true;
    
    // Need to recreate bind group
    bindGroupDirty_ = true;
    
    // Recalculate tile count
    if (!calculateTileCount() || !rebuildTerrainBuffers(heightData)) {
        LOG_ERROR("Failed to build adaptive terrain buffers");
        heightmapView_ = oldHeightmapView;
        heightmapWidth_ = oldWidth;
        heightmapHeight_ = oldHeight;
        uniforms_ = oldUniforms;
        tilesX_ = oldTilesX;
        tilesY_ = oldTilesY;
        levelTilesX_ = oldLevelTilesX;
        levelTilesY_ = oldLevelTilesY;
        levelCandidateOffsets_ = oldLevelCandidateOffsets;
        totalCandidateCount_ = oldCandidateCount;
        uniformsDirty_ = oldUniformsDirty;
        cullUniformsDirty_ = oldCullUniformsDirty;
        bindGroupDirty_ = oldBindGroupDirty;
        return false;
    }
    
    LOG_DEBUG("Set heightmap: {}x{}, tiles: {}x{}", width, height, tilesX_, tilesY_);
    return true;
}

bool TrianglePath::calculateTileCount() {
    if (heightmapWidth_ == 0 || heightmapHeight_ == 0) {
        tilesX_ = 0;
        tilesY_ = 0;
        levelTilesX_.fill(0u);
        levelTilesY_.fill(0u);
        levelCandidateOffsets_.fill(0u);
        totalCandidateCount_ = 0u;
        return true;
    }
    
    const uint64_t cellsX = std::max(heightmapWidth_ - 1, 1u);
    const uint64_t cellsY = std::max(heightmapHeight_ - 1, 1u);
    const uint64_t baseStep = std::max(config_.lodStep, 1u);
    std::array<uint32_t, LOD_COUNT> nextTilesX{};
    std::array<uint32_t, LOD_COUNT> nextTilesY{};
    std::array<uint32_t, LOD_COUNT> nextOffsets{};
    uint64_t nextCandidateCount = 0u;
    for (uint32_t lod = 0; lod < LOD_COUNT; ++lod) {
        const uint64_t step = baseStep << lod;
        const uint64_t tileSpan = static_cast<uint64_t>(TILE_QUADS) * step;
        const uint64_t levelX = std::max(
            (cellsX + tileSpan - 1u) / tileSpan, uint64_t{1});
        const uint64_t levelY = std::max(
            (cellsY + tileSpan - 1u) / tileSpan, uint64_t{1});
        const uint64_t levelCount = levelX * levelY;
        if (levelX > std::numeric_limits<uint32_t>::max()
            || levelY > std::numeric_limits<uint32_t>::max()
            || nextCandidateCount > std::numeric_limits<uint32_t>::max()
            || levelCount > std::numeric_limits<uint32_t>::max()
            || nextCandidateCount + levelCount
                > std::numeric_limits<uint32_t>::max()) {
            LOG_ERROR("Triangle terrain tile layout is not representable");
            return false;
        }
        nextOffsets[lod] = static_cast<uint32_t>(nextCandidateCount);
        nextTilesX[lod] = static_cast<uint32_t>(levelX);
        nextTilesY[lod] = static_cast<uint32_t>(levelY);
        nextCandidateCount += levelCount;
    }

    levelTilesX_ = nextTilesX;
    levelTilesY_ = nextTilesY;
    levelCandidateOffsets_ = nextOffsets;
    totalCandidateCount_ = static_cast<uint32_t>(nextCandidateCount);
    tilesX_ = nextTilesX[0];
    tilesY_ = nextTilesY[0];
    cullUniformsDirty_ = true;
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Camera Updates
// ─────────────────────────────────────────────────────────────────────────────

void TrianglePath::updateCamera(const glm::mat4& view, const glm::mat4& proj, 
                                 const glm::vec3& cameraPos, float ambientIntensity) {
    CameraUniforms next = uniforms_;
    if (!next.setCamera(view, proj, cameraPos)) return;
    
    // Update light direction in view space (using hardcoded world direction)
    glm::vec3 worldLightDir = glm::normalize(glm::vec3(0.3f, 0.8f, 0.4f));
    if (!next.setLightDirection(worldLightDir, view, ambientIntensity)) return;
    
    uniforms_ = next;
    uniformsDirty_ = true;
}

void TrianglePath::setCameraUniforms(const CameraUniforms& uniforms) {
    if (!uniforms.isValid()) {
        LOG_ERROR("TrianglePath::setCameraUniforms: invalid uniform block");
        return;
    }
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
        (void)calculateTileCount();
    }
}

uint32_t TrianglePath::getLODStep() const noexcept {
    return config_.lodStep;
}

bool TrianglePath::updateUniformBuffer() {
    if (!uniformBuffer_ || !queue_) return false;
    
    if (!gpu::writeBuffer(queue_, uniformBuffer_, 0, uniforms_)) return false;
    uniformsDirty_ = false;
    return true;
}

bool TrianglePath::updateCullUniformBuffer() {
    if (!cullUniformBuffer_ || !queue_) return false;

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

    if (!gpu::writeBuffer(queue_, cullUniformBuffer_, 0, uniforms)) {
        return false;
    }
    cullUniformsDirty_ = false;
    return true;
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
    if (!encoder || !colorView || !depthView) {
        LOG_ERROR("TrianglePath::render: invalid command encoder or attachment");
        return;
    }
    
    if (!heightmapView_) {
        LOG_WARN("TrianglePath::render: no heightmap set");
        return;
    }
    
    // Update uniform buffer if dirty
    if (uniformsDirty_ && !updateUniformBuffer()) {
        return;
    }

    if (cullUniformsDirty_ && !updateCullUniformBuffer()) {
        return;
    }
    
    // Update Compute Bind Group (if needed) and Main Bind Group
    if (bindGroupDirty_ || !bindGroup_) {
        if (!createBindGroup()) {
            LOG_ERROR("Failed to create bind group during render");
            return;
        }
        if (!computeBindGroup_) {
            LOG_ERROR("Failed to create terrain compute bind group");
            return;
        }
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
    if (!computePass) {
        LOG_ERROR("TrianglePath::render: failed to begin culling pass");
        return;
    }
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
    if (!renderPass) {
        LOG_ERROR("TrianglePath::render: failed to begin terrain pass");
        return;
    }
    
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
    if (!updateCullUniformBuffer()) return false;

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
    visibilitySegmentCapacity_ = 0u;
}

bool TrianglePath::rebuildTerrainBuffers(std::span<const uint16_t> heightData) {
    if (totalCandidateCount_ == 0) {
        return false;
    }

    // Every draw gets one 256-byte-aligned visibility segment. Indirect draws
    // can then keep firstInstance at zero and remain valid on baseline WebGPU.
    const uint64_t largestLevel =
        static_cast<uint64_t>(levelTilesX_[0]) * levelTilesY_[0];
    constexpr uint32_t entriesPerAlignment = 256u / sizeof(uint32_t);
    const uint64_t alignedCapacity =
        (largestLevel + entriesPerAlignment - 1u)
        & ~static_cast<uint64_t>(entriesPerAlignment - 1u);
    if (alignedCapacity == 0u
        || alignedCapacity > std::numeric_limits<uint32_t>::max()) {
        LOG_ERROR("Triangle visibility segment is not representable");
        return false;
    }
    const uint32_t nextVisibilitySegmentCapacity =
        static_cast<uint32_t>(alignedCapacity);
    const uint64_t visibleEntryCount =
        alignedCapacity * INDIRECT_DRAW_COUNT;

    WGPUBuffer nextVisibleIndicesBuffer = nullptr;
    WGPUBuffer nextTileBoundsBuffer = nullptr;
    WGPUBuffer nextIndirectBuffer = nullptr;
    WGPUBuffer nextIndirectTemplateBuffer = nullptr;
    WGPUBindGroup nextComputeBindGroup = nullptr;
    WGPUBindGroup nextRenderBindGroup = nullptr;
    const auto cleanup = [&]() {
        if (nextRenderBindGroup) wgpuBindGroupRelease(nextRenderBindGroup);
        if (nextComputeBindGroup) wgpuBindGroupRelease(nextComputeBindGroup);
        if (nextIndirectTemplateBuffer) {
            wgpuBufferRelease(nextIndirectTemplateBuffer);
        }
        if (nextIndirectBuffer) wgpuBufferRelease(nextIndirectBuffer);
        if (nextTileBoundsBuffer) wgpuBufferRelease(nextTileBoundsBuffer);
        if (nextVisibleIndicesBuffer) wgpuBufferRelease(nextVisibleIndicesBuffer);
    };

    gpu::BufferDesc visibleDesc = gpu::BufferDesc::storage(
        visibleEntryCount * sizeof(uint32_t), false, "terrain_visible_indices_buffer");
    nextVisibleIndicesBuffer = gpu::createBuffer(device_, visibleDesc);
    if (!nextVisibleIndicesBuffer) {
        return false;
    }

    // Build exact min/max bounds. Level zero scans all source samples covered by
    // each patch; coarser levels combine four children, preserving conservatism.
    std::vector<uint32_t> packedBounds(totalCandidateCount_, 0xffff0000u);
    const bool hasHeightData = heightData.size() ==
        static_cast<size_t>(heightmapWidth_) * static_cast<size_t>(heightmapHeight_);

    if (hasHeightData) {
        const uint32_t baseStep = std::max(config_.lodStep, 1u);
        const uint64_t baseSpan =
            static_cast<uint64_t>(TILE_QUADS) * baseStep;
        for (uint32_t tileY = 0; tileY < levelTilesY_[0]; ++tileY) {
            for (uint32_t tileX = 0; tileX < levelTilesX_[0]; ++tileX) {
                const uint64_t startX64 =
                    static_cast<uint64_t>(tileX) * baseSpan;
                const uint64_t startY64 =
                    static_cast<uint64_t>(tileY) * baseSpan;
                const uint32_t startX = static_cast<uint32_t>(
                    std::min(startX64,
                             static_cast<uint64_t>(heightmapWidth_ - 1u)));
                const uint32_t startY = static_cast<uint32_t>(
                    std::min(startY64,
                             static_cast<uint64_t>(heightmapHeight_ - 1u)));
                const uint32_t endX = static_cast<uint32_t>(
                    std::min(startX64 + baseSpan,
                             static_cast<uint64_t>(heightmapWidth_ - 1u)));
                const uint32_t endY = static_cast<uint32_t>(
                    std::min(startY64 + baseSpan,
                             static_cast<uint64_t>(heightmapHeight_ - 1u)));
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
    nextTileBoundsBuffer = gpu::createBufferWithData(
        device_, queue_, boundsDesc, std::span<const uint32_t>(packedBounds));
    if (!nextTileBoundsBuffer) {
        cleanup();
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
    nextIndirectBuffer = gpu::createBufferWithData(
        device_, queue_, indirectDesc, std::span<const uint32_t>(indirectData));

    gpu::BufferDesc templateDesc;
    templateDesc.label = "terrain_indirect_template_buffer";
    templateDesc.size = indirectData.size() * sizeof(uint32_t);
    templateDesc.usage = WGPUBufferUsage_CopySrc;
    nextIndirectTemplateBuffer = gpu::createBufferWithData(
        device_, queue_, templateDesc, std::span<const uint32_t>(indirectData));

    if (!nextIndirectBuffer || !nextIndirectTemplateBuffer) {
        cleanup();
        return false;
    }

    const std::array<gpu::BindGroupEntry, 5> computeEntries = {
        gpu::BindGroupEntry(0).buffer(
            uniformBuffer_, 0, sizeof(CameraUniforms)),
        gpu::BindGroupEntry(1).buffer(
            nextIndirectBuffer, 0,
            INDIRECT_DRAW_COUNT * 5u * sizeof(uint32_t)),
        gpu::BindGroupEntry(2).buffer(
            nextVisibleIndicesBuffer, 0, WGPU_WHOLE_SIZE),
        gpu::BindGroupEntry(3).buffer(
            cullUniformBuffer_, 0, sizeof(CullUniforms)),
        gpu::BindGroupEntry(4).buffer(
            nextTileBoundsBuffer, 0, WGPU_WHOLE_SIZE)
    };
    nextComputeBindGroup = gpu::createBindGroup(
        device_, computeBindGroupLayout_, computeEntries, "cull_bind_group");
    if (!nextComputeBindGroup) {
        cleanup();
        return false;
    }

    // A live renderer must replace the render binding in the same transaction:
    // its visible-index buffer must match the newly-created culling buffers.
    if (bindGroup_) {
        const std::array<gpu::BindGroupEntry, 6> renderEntries = {
            gpu::BindGroupEntry(0).buffer(
                uniformBuffer_, 0, sizeof(CameraUniforms)),
            gpu::BindGroupEntry(1).textureView(heightmapView_),
            gpu::BindGroupEntry(2).textureView(albedoView_),
            gpu::BindGroupEntry(3).textureView(lightmapView_),
            gpu::BindGroupEntry(4).sampler(sampler_),
            gpu::BindGroupEntry(5).buffer(
                nextVisibleIndicesBuffer, 0,
                static_cast<uint64_t>(nextVisibilitySegmentCapacity)
                    * sizeof(uint32_t))
        };
        nextRenderBindGroup = gpu::createBindGroup(
            device_, bindGroupLayout_, renderEntries, "terrain_bind_group");
        if (!nextRenderBindGroup) {
            cleanup();
            return false;
        }
    }

    releaseTerrainBuffers();
    visibleIndicesBuffer_ = nextVisibleIndicesBuffer;
    tileBoundsBuffer_ = nextTileBoundsBuffer;
    indirectBuffer_ = nextIndirectBuffer;
    indirectTemplateBuffer_ = nextIndirectTemplateBuffer;
    computeBindGroup_ = nextComputeBindGroup;
    bindGroup_ = nextRenderBindGroup;
    visibilitySegmentCapacity_ = nextVisibilitySegmentCapacity;

    nextVisibleIndicesBuffer = nullptr;
    nextTileBoundsBuffer = nullptr;
    nextIndirectBuffer = nullptr;
    nextIndirectTemplateBuffer = nullptr;
    nextComputeBindGroup = nullptr;
    nextRenderBindGroup = nullptr;

    cullUniformsDirty_ = true;
    bindGroupDirty_ = bindGroup_ == nullptr;
    return true;
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
