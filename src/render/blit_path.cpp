// ═══════════════════════════════════════════════════════════════════════════════
// blit_path.cpp - Fullscreen Blit/Lighting Rendering Path Implementation
// ═══════════════════════════════════════════════════════════════════════════════

#include "render/blit_path.hpp"
#include "render/triangle_path.hpp"  // For CameraUniforms
#include "render/water_clipmap_mesh.hpp"
#include "gpu/resources.hpp"
#include "gpu/webgpu_compat.hpp"
#include "core/log.hpp"

#include <glm/gtc/packing.hpp>
#include <glm/gtc/matrix_inverse.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <fstream>
#include <limits>
#include <span>
#include <vector>

#if __has_include(<stb_image.h>)
    #include <stb_image.h>
#endif

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
    , periodicGradientLut_(std::move(other.periodicGradientLut_))
    , shaderModule_(other.shaderModule_)
    , pipelineLayout_(other.pipelineLayout_)
    , pipeline_(other.pipeline_)
    , backgroundPipeline_(other.backgroundPipeline_)
    , cachedPipelineLayout_(other.cachedPipelineLayout_)
    , cachedPipeline_(other.cachedPipeline_)
    , cachedColorPipeline_(other.cachedColorPipeline_)
    , waterClipmapShaderModule_(other.waterClipmapShaderModule_)
    , waterClipmapPipeline_(other.waterClipmapPipeline_)
    , waterClipmapColorPipeline_(other.waterClipmapColorPipeline_)
    , waterClipmapVertexBuffer_(other.waterClipmapVertexBuffer_)
    , waterClipmapIndexBuffer_(other.waterClipmapIndexBuffer_)
    , waterClipmapIndexCount_(other.waterClipmapIndexCount_)
    , bindGroupLayout_(other.bindGroupLayout_)
    , bindGroup_(other.bindGroup_)
    , cachedBindGroupLayout_(other.cachedBindGroupLayout_)
    , staticBindGroup_(other.staticBindGroup_)
    , cachedBindGroup_(other.cachedBindGroup_)
    , uniformBuffer_(other.uniformBuffer_)
    , staticUniformBuffer_(other.staticUniformBuffer_)
    , debugUniformBuffer_(other.debugUniformBuffer_)
    , sampler_(other.sampler_)
    , skyLutShaderModule_(other.skyLutShaderModule_)
    , skyLutPipelineLayout_(other.skyLutPipelineLayout_)
    , skyLutPipeline_(other.skyLutPipeline_)
    , skyLutBindGroupLayout_(other.skyLutBindGroupLayout_)
    , skyLutBindGroup_(other.skyLutBindGroup_)
    , skyLutTexture_(other.skyLutTexture_)
    , skyLutView_(other.skyLutView_)
    , skyLutBaseView_(other.skyLutBaseView_)
    , skyLutMipShaderModule_(other.skyLutMipShaderModule_)
    , skyLutMipPipelineLayout_(other.skyLutMipPipelineLayout_)
    , skyLutMipPipeline_(other.skyLutMipPipeline_)
    , skyLutMipBindGroupLayout_(other.skyLutMipBindGroupLayout_)
    , skyLutMipViews_(std::move(other.skyLutMipViews_))
    , skyLutMipBindGroups_(std::move(other.skyLutMipBindGroups_))
    , skyLutBaked_(other.skyLutBaked_)
    , surfaceFoamTexture_(other.surfaceFoamTexture_)
    , surfaceFoamView_(other.surfaceFoamView_)
    , surfaceFoamSampler_(other.surfaceFoamSampler_)
    , particleShaderModule_(other.particleShaderModule_)
    , particlePipelineLayout_(other.particlePipelineLayout_)
    , particlePipeline_(other.particlePipeline_)
    , particleBindGroupLayout_(other.particleBindGroupLayout_)
    , particleBindGroup_(other.particleBindGroup_)
    , particleBuffer_(other.particleBuffer_)
    , underwaterParticles_(std::move(other.underwaterParticles_))
    , particleRandomState_(other.particleRandomState_)
    , particlesInitialized_(other.particlesInitialized_)
    , backgroundTexture_(other.backgroundTexture_)
    , backgroundView_(other.backgroundView_)
    , coverageMaskTexture_(other.coverageMaskTexture_)
    , coverageMaskView_(other.coverageMaskView_)
    , outputWidth_(other.outputWidth_)
    , outputHeight_(other.outputHeight_)
    , depthView_(other.depthView_)
    , shadowView_(other.shadowView_)
    , materialView_(other.materialView_)
    , staticDepthView_(other.staticDepthView_)
    , staticShadowView_(other.staticShadowView_)
    , heightmapView_(other.heightmapView_)
    , shadowHeightView_(other.shadowHeightView_)
    , waterDisplacementView_(other.waterDisplacementView_)
    , waterDisplacementSampler_(other.waterDisplacementSampler_)
    , terrainView_(other.terrainView_)
    , lightmapView_(other.lightmapView_)
    , terrainMaterialAlbedoView_(other.terrainMaterialAlbedoView_)
    , terrainMaterialNormalRoughnessView_(
          other.terrainMaterialNormalRoughnessView_)
    , terrainWidth_(other.terrainWidth_)
    , terrainHeight_(other.terrainHeight_)
    , uniforms_(other.uniforms_)
    , staticUniforms_(other.staticUniforms_)
    , config_(other.config_)
    , uniformsDirty_(other.uniformsDirty_)
    , staticUniformsDirty_(other.staticUniformsDirty_)
    , bindGroupDirty_(other.bindGroupDirty_)
    , staticCacheActive_(other.staticCacheActive_)
    , backgroundValid_(other.backgroundValid_)
    , backgroundDirty_(other.backgroundDirty_)
    , linearDepthRequired_(other.linearDepthRequired_)
    , usedGeometryWaterPathLastRender_(
          other.usedGeometryWaterPathLastRender_)
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
    other.backgroundPipeline_ = nullptr;
    other.cachedPipelineLayout_ = nullptr;
    other.cachedPipeline_ = nullptr;
    other.cachedColorPipeline_ = nullptr;
    other.waterClipmapShaderModule_ = nullptr;
    other.waterClipmapPipeline_ = nullptr;
    other.waterClipmapColorPipeline_ = nullptr;
    other.waterClipmapVertexBuffer_ = nullptr;
    other.waterClipmapIndexBuffer_ = nullptr;
    other.waterClipmapIndexCount_ = 0u;
    other.bindGroupLayout_ = nullptr;
    other.bindGroup_ = nullptr;
    other.cachedBindGroupLayout_ = nullptr;
    other.staticBindGroup_ = nullptr;
    other.cachedBindGroup_ = nullptr;
    other.uniformBuffer_ = nullptr;
    other.staticUniformBuffer_ = nullptr;
    other.debugUniformBuffer_ = nullptr;
    other.sampler_ = nullptr;
    other.skyLutShaderModule_ = nullptr;
    other.skyLutPipelineLayout_ = nullptr;
    other.skyLutPipeline_ = nullptr;
    other.skyLutBindGroupLayout_ = nullptr;
    other.skyLutBindGroup_ = nullptr;
    other.skyLutTexture_ = nullptr;
    other.skyLutView_ = nullptr;
    other.skyLutBaseView_ = nullptr;
    other.skyLutMipShaderModule_ = nullptr;
    other.skyLutMipPipelineLayout_ = nullptr;
    other.skyLutMipPipeline_ = nullptr;
    other.skyLutMipBindGroupLayout_ = nullptr;
    other.skyLutMipViews_.clear();
    other.skyLutMipBindGroups_.clear();
    other.surfaceFoamTexture_ = nullptr;
    other.surfaceFoamView_ = nullptr;
    other.surfaceFoamSampler_ = nullptr;
    other.particleShaderModule_ = nullptr;
    other.particlePipelineLayout_ = nullptr;
    other.particlePipeline_ = nullptr;
    other.particleBindGroupLayout_ = nullptr;
    other.particleBindGroup_ = nullptr;
    other.particleBuffer_ = nullptr;
    other.underwaterParticles_.clear();
    other.particlesInitialized_ = false;
    other.backgroundTexture_ = nullptr;
    other.backgroundView_ = nullptr;
    other.coverageMaskTexture_ = nullptr;
    other.coverageMaskView_ = nullptr;
    other.depthView_ = nullptr;
    other.shadowView_ = nullptr;
    other.materialView_ = nullptr;
    other.staticDepthView_ = nullptr;
    other.staticShadowView_ = nullptr;
    other.heightmapView_ = nullptr;
    other.shadowHeightView_ = nullptr;
    other.waterDisplacementView_ = nullptr;
    other.waterDisplacementSampler_ = nullptr;
    other.terrainView_ = nullptr;
    other.lightmapView_ = nullptr;
    other.terrainMaterialAlbedoView_ = nullptr;
    other.terrainMaterialNormalRoughnessView_ = nullptr;
    other.uniforms_ = nullptr;
    other.staticUniforms_ = nullptr;
}

BlitPath& BlitPath::operator=(BlitPath&& other) noexcept {
    if (this != &other) {
        shutdown();
        
        device_ = other.device_;
        queue_ = other.queue_;
        periodicGradientLut_ = std::move(other.periodicGradientLut_);
        shaderModule_ = other.shaderModule_;
        pipelineLayout_ = other.pipelineLayout_;
        pipeline_ = other.pipeline_;
        backgroundPipeline_ = other.backgroundPipeline_;
        cachedPipelineLayout_ = other.cachedPipelineLayout_;
        cachedPipeline_ = other.cachedPipeline_;
        cachedColorPipeline_ = other.cachedColorPipeline_;
        waterClipmapShaderModule_ = other.waterClipmapShaderModule_;
        waterClipmapPipeline_ = other.waterClipmapPipeline_;
        waterClipmapColorPipeline_ = other.waterClipmapColorPipeline_;
        waterClipmapVertexBuffer_ = other.waterClipmapVertexBuffer_;
        waterClipmapIndexBuffer_ = other.waterClipmapIndexBuffer_;
        waterClipmapIndexCount_ = other.waterClipmapIndexCount_;
        bindGroupLayout_ = other.bindGroupLayout_;
        bindGroup_ = other.bindGroup_;
        cachedBindGroupLayout_ = other.cachedBindGroupLayout_;
        staticBindGroup_ = other.staticBindGroup_;
        cachedBindGroup_ = other.cachedBindGroup_;
        uniformBuffer_ = other.uniformBuffer_;
        staticUniformBuffer_ = other.staticUniformBuffer_;
        debugUniformBuffer_ = other.debugUniformBuffer_;
        sampler_ = other.sampler_;
        skyLutShaderModule_ = other.skyLutShaderModule_;
        skyLutPipelineLayout_ = other.skyLutPipelineLayout_;
        skyLutPipeline_ = other.skyLutPipeline_;
        skyLutBindGroupLayout_ = other.skyLutBindGroupLayout_;
        skyLutBindGroup_ = other.skyLutBindGroup_;
        skyLutTexture_ = other.skyLutTexture_;
        skyLutView_ = other.skyLutView_;
        skyLutBaseView_ = other.skyLutBaseView_;
        skyLutMipShaderModule_ = other.skyLutMipShaderModule_;
        skyLutMipPipelineLayout_ = other.skyLutMipPipelineLayout_;
        skyLutMipPipeline_ = other.skyLutMipPipeline_;
        skyLutMipBindGroupLayout_ = other.skyLutMipBindGroupLayout_;
        skyLutMipViews_ = std::move(other.skyLutMipViews_);
        skyLutMipBindGroups_ = std::move(other.skyLutMipBindGroups_);
        skyLutBaked_ = other.skyLutBaked_;
        surfaceFoamTexture_ = other.surfaceFoamTexture_;
        surfaceFoamView_ = other.surfaceFoamView_;
        surfaceFoamSampler_ = other.surfaceFoamSampler_;
        particleShaderModule_ = other.particleShaderModule_;
        particlePipelineLayout_ = other.particlePipelineLayout_;
        particlePipeline_ = other.particlePipeline_;
        particleBindGroupLayout_ = other.particleBindGroupLayout_;
        particleBindGroup_ = other.particleBindGroup_;
        particleBuffer_ = other.particleBuffer_;
        underwaterParticles_ = std::move(other.underwaterParticles_);
        particleRandomState_ = other.particleRandomState_;
        particlesInitialized_ = other.particlesInitialized_;
        backgroundTexture_ = other.backgroundTexture_;
        backgroundView_ = other.backgroundView_;
        coverageMaskTexture_ = other.coverageMaskTexture_;
        coverageMaskView_ = other.coverageMaskView_;
        outputWidth_ = other.outputWidth_;
        outputHeight_ = other.outputHeight_;
        depthView_ = other.depthView_;
        shadowView_ = other.shadowView_;
        materialView_ = other.materialView_;
        staticDepthView_ = other.staticDepthView_;
        staticShadowView_ = other.staticShadowView_;
        heightmapView_ = other.heightmapView_;
        shadowHeightView_ = other.shadowHeightView_;
        waterDisplacementView_ = other.waterDisplacementView_;
        waterDisplacementSampler_ = other.waterDisplacementSampler_;
        terrainView_ = other.terrainView_;
        lightmapView_ = other.lightmapView_;
        terrainMaterialAlbedoView_ = other.terrainMaterialAlbedoView_;
        terrainMaterialNormalRoughnessView_ =
            other.terrainMaterialNormalRoughnessView_;
        terrainWidth_ = other.terrainWidth_;
        terrainHeight_ = other.terrainHeight_;
        uniforms_ = other.uniforms_;
        staticUniforms_ = other.staticUniforms_;
        config_ = other.config_;
        uniformsDirty_ = other.uniformsDirty_;
        staticUniformsDirty_ = other.staticUniformsDirty_;
        bindGroupDirty_ = other.bindGroupDirty_;
        staticCacheActive_ = other.staticCacheActive_;
        backgroundValid_ = other.backgroundValid_;
        backgroundDirty_ = other.backgroundDirty_;
        linearDepthRequired_ = other.linearDepthRequired_;
        usedGeometryWaterPathLastRender_ =
            other.usedGeometryWaterPathLastRender_;
        debugMode_ = other.debugMode_;
        debugMaxDepth_ = other.debugMaxDepth_;
        debugUniformsDirty_ = other.debugUniformsDirty_;
        
        other.device_ = nullptr;
        other.queue_ = nullptr;
        other.shaderModule_ = nullptr;
        other.pipelineLayout_ = nullptr;
        other.pipeline_ = nullptr;
        other.backgroundPipeline_ = nullptr;
        other.cachedPipelineLayout_ = nullptr;
        other.cachedPipeline_ = nullptr;
        other.cachedColorPipeline_ = nullptr;
        other.waterClipmapShaderModule_ = nullptr;
        other.waterClipmapPipeline_ = nullptr;
        other.waterClipmapColorPipeline_ = nullptr;
        other.waterClipmapVertexBuffer_ = nullptr;
        other.waterClipmapIndexBuffer_ = nullptr;
        other.waterClipmapIndexCount_ = 0u;
        other.bindGroupLayout_ = nullptr;
        other.bindGroup_ = nullptr;
        other.cachedBindGroupLayout_ = nullptr;
        other.staticBindGroup_ = nullptr;
        other.cachedBindGroup_ = nullptr;
        other.uniformBuffer_ = nullptr;
        other.staticUniformBuffer_ = nullptr;
        other.debugUniformBuffer_ = nullptr;
        other.sampler_ = nullptr;
        other.skyLutShaderModule_ = nullptr;
        other.skyLutPipelineLayout_ = nullptr;
        other.skyLutPipeline_ = nullptr;
        other.skyLutBindGroupLayout_ = nullptr;
        other.skyLutBindGroup_ = nullptr;
        other.skyLutTexture_ = nullptr;
        other.skyLutView_ = nullptr;
        other.skyLutBaseView_ = nullptr;
        other.skyLutMipShaderModule_ = nullptr;
        other.skyLutMipPipelineLayout_ = nullptr;
        other.skyLutMipPipeline_ = nullptr;
        other.skyLutMipBindGroupLayout_ = nullptr;
        other.skyLutMipViews_.clear();
        other.skyLutMipBindGroups_.clear();
        other.surfaceFoamTexture_ = nullptr;
        other.surfaceFoamView_ = nullptr;
        other.surfaceFoamSampler_ = nullptr;
        other.particleShaderModule_ = nullptr;
        other.particlePipelineLayout_ = nullptr;
        other.particlePipeline_ = nullptr;
        other.particleBindGroupLayout_ = nullptr;
        other.particleBindGroup_ = nullptr;
        other.particleBuffer_ = nullptr;
        other.underwaterParticles_.clear();
        other.particlesInitialized_ = false;
        other.backgroundTexture_ = nullptr;
        other.backgroundView_ = nullptr;
        other.coverageMaskTexture_ = nullptr;
        other.coverageMaskView_ = nullptr;
        other.depthView_ = nullptr;
        other.shadowView_ = nullptr;
        other.materialView_ = nullptr;
        other.staticDepthView_ = nullptr;
        other.staticShadowView_ = nullptr;
        other.heightmapView_ = nullptr;
        other.shadowHeightView_ = nullptr;
        other.waterDisplacementView_ = nullptr;
        other.waterDisplacementSampler_ = nullptr;
        other.terrainView_ = nullptr;
        other.lightmapView_ = nullptr;
        other.terrainMaterialAlbedoView_ = nullptr;
        other.terrainMaterialNormalRoughnessView_ = nullptr;
        other.uniforms_ = nullptr;
        other.staticUniforms_ = nullptr;
    }
    return *this;
}

void BlitPath::shutdown() {
    periodicGradientLut_.reset();
    if (waterClipmapIndexBuffer_) {
        wgpuBufferRelease(waterClipmapIndexBuffer_);
        waterClipmapIndexBuffer_ = nullptr;
    }
    if (waterClipmapVertexBuffer_) {
        wgpuBufferRelease(waterClipmapVertexBuffer_);
        waterClipmapVertexBuffer_ = nullptr;
    }
    if (waterClipmapPipeline_) {
        wgpuRenderPipelineRelease(waterClipmapPipeline_);
        waterClipmapPipeline_ = nullptr;
    }
    if (waterClipmapColorPipeline_) {
        wgpuRenderPipelineRelease(waterClipmapColorPipeline_);
        waterClipmapColorPipeline_ = nullptr;
    }
    if (waterClipmapShaderModule_) {
        wgpuShaderModuleRelease(waterClipmapShaderModule_);
        waterClipmapShaderModule_ = nullptr;
    }
    waterClipmapIndexCount_ = 0u;
    if (particleBindGroup_) {
        wgpuBindGroupRelease(particleBindGroup_);
        particleBindGroup_ = nullptr;
    }
    if (particleBindGroupLayout_) {
        wgpuBindGroupLayoutRelease(particleBindGroupLayout_);
        particleBindGroupLayout_ = nullptr;
    }
    if (particlePipeline_) {
        wgpuRenderPipelineRelease(particlePipeline_);
        particlePipeline_ = nullptr;
    }
    if (particlePipelineLayout_) {
        wgpuPipelineLayoutRelease(particlePipelineLayout_);
        particlePipelineLayout_ = nullptr;
    }
    if (particleShaderModule_) {
        wgpuShaderModuleRelease(particleShaderModule_);
        particleShaderModule_ = nullptr;
    }
    if (particleBuffer_) {
        wgpuBufferRelease(particleBuffer_);
        particleBuffer_ = nullptr;
    }
    if (cachedBindGroup_) {
        wgpuBindGroupRelease(cachedBindGroup_);
        cachedBindGroup_ = nullptr;
    }
    if (staticBindGroup_) {
        wgpuBindGroupRelease(staticBindGroup_);
        staticBindGroup_ = nullptr;
    }
    if (bindGroup_) {
        wgpuBindGroupRelease(bindGroup_);
        bindGroup_ = nullptr;
    }
    if (bindGroupLayout_) {
        wgpuBindGroupLayoutRelease(bindGroupLayout_);
        bindGroupLayout_ = nullptr;
    }
    if (cachedBindGroupLayout_) {
        wgpuBindGroupLayoutRelease(cachedBindGroupLayout_);
        cachedBindGroupLayout_ = nullptr;
    }
    if (cachedPipeline_) {
        wgpuRenderPipelineRelease(cachedPipeline_);
        cachedPipeline_ = nullptr;
    }
    if (cachedColorPipeline_) {
        wgpuRenderPipelineRelease(cachedColorPipeline_);
        cachedColorPipeline_ = nullptr;
    }
    if (cachedPipelineLayout_) {
        wgpuPipelineLayoutRelease(cachedPipelineLayout_);
        cachedPipelineLayout_ = nullptr;
    }
    if (backgroundPipeline_) {
        wgpuRenderPipelineRelease(backgroundPipeline_);
        backgroundPipeline_ = nullptr;
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
    if (staticUniformBuffer_) {
        wgpuBufferRelease(staticUniformBuffer_);
        staticUniformBuffer_ = nullptr;
    }
    if (debugUniformBuffer_) {
        wgpuBufferRelease(debugUniformBuffer_);
        debugUniformBuffer_ = nullptr;
    }
    if (sampler_) {
        wgpuSamplerRelease(sampler_);
        sampler_ = nullptr;
    }
    if (skyLutBindGroup_) {
        wgpuBindGroupRelease(skyLutBindGroup_);
        skyLutBindGroup_ = nullptr;
    }
    for (WGPUBindGroup bindGroup : skyLutMipBindGroups_) {
        if (bindGroup) {
            wgpuBindGroupRelease(bindGroup);
        }
    }
    skyLutMipBindGroups_.clear();
    for (WGPUTextureView view : skyLutMipViews_) {
        if (view) {
            wgpuTextureViewRelease(view);
        }
    }
    skyLutMipViews_.clear();
    if (skyLutMipBindGroupLayout_) {
        wgpuBindGroupLayoutRelease(skyLutMipBindGroupLayout_);
        skyLutMipBindGroupLayout_ = nullptr;
    }
    if (skyLutMipPipeline_) {
        wgpuComputePipelineRelease(skyLutMipPipeline_);
        skyLutMipPipeline_ = nullptr;
    }
    if (skyLutMipPipelineLayout_) {
        wgpuPipelineLayoutRelease(skyLutMipPipelineLayout_);
        skyLutMipPipelineLayout_ = nullptr;
    }
    if (skyLutMipShaderModule_) {
        wgpuShaderModuleRelease(skyLutMipShaderModule_);
        skyLutMipShaderModule_ = nullptr;
    }
    if (skyLutBindGroupLayout_) {
        wgpuBindGroupLayoutRelease(skyLutBindGroupLayout_);
        skyLutBindGroupLayout_ = nullptr;
    }
    if (skyLutPipeline_) {
        wgpuComputePipelineRelease(skyLutPipeline_);
        skyLutPipeline_ = nullptr;
    }
    if (skyLutPipelineLayout_) {
        wgpuPipelineLayoutRelease(skyLutPipelineLayout_);
        skyLutPipelineLayout_ = nullptr;
    }
    if (skyLutShaderModule_) {
        wgpuShaderModuleRelease(skyLutShaderModule_);
        skyLutShaderModule_ = nullptr;
    }
    if (skyLutView_) {
        wgpuTextureViewRelease(skyLutView_);
        skyLutView_ = nullptr;
    }
    if (skyLutBaseView_) {
        wgpuTextureViewRelease(skyLutBaseView_);
        skyLutBaseView_ = nullptr;
    }
    if (skyLutTexture_) {
        wgpuTextureRelease(skyLutTexture_);
        skyLutTexture_ = nullptr;
    }
    skyLutBaked_ = false;
    if (surfaceFoamView_) {
        wgpuTextureViewRelease(surfaceFoamView_);
        surfaceFoamView_ = nullptr;
    }
    if (surfaceFoamTexture_) {
        wgpuTextureRelease(surfaceFoamTexture_);
        surfaceFoamTexture_ = nullptr;
    }
    if (surfaceFoamSampler_) {
        wgpuSamplerRelease(surfaceFoamSampler_);
        surfaceFoamSampler_ = nullptr;
    }
    if (backgroundView_) {
        wgpuTextureViewRelease(backgroundView_);
        backgroundView_ = nullptr;
    }
    if (backgroundTexture_) {
        wgpuTextureRelease(backgroundTexture_);
        backgroundTexture_ = nullptr;
    }
    if (coverageMaskView_) {
        wgpuTextureViewRelease(coverageMaskView_);
        coverageMaskView_ = nullptr;
    }
    if (coverageMaskTexture_) {
        wgpuTextureRelease(coverageMaskTexture_);
        coverageMaskTexture_ = nullptr;
    }
    underwaterParticles_.clear();
    particlesInitialized_ = false;

    // Free heap-allocated uniforms
    delete uniforms_;
    uniforms_ = nullptr;
    delete staticUniforms_;
    staticUniforms_ = nullptr;
    
    // Note: We don't own texture views, so don't release them
    depthView_ = nullptr;
    shadowView_ = nullptr;
    materialView_ = nullptr;
    staticDepthView_ = nullptr;
    staticShadowView_ = nullptr;
    heightmapView_ = nullptr;
    shadowHeightView_ = nullptr;
    waterDisplacementView_ = nullptr;
    waterDisplacementSampler_ = nullptr;
    terrainView_ = nullptr;
    lightmapView_ = nullptr;
    terrainMaterialAlbedoView_ = nullptr;
    terrainMaterialNormalRoughnessView_ = nullptr;
    device_ = nullptr;
    queue_ = nullptr;
    outputWidth_ = 0;
    outputHeight_ = 0;
    staticCacheActive_ = false;
    backgroundValid_ = false;
    backgroundDirty_ = true;
    linearDepthRequired_ = true;
    usedGeometryWaterPathLastRender_ = false;
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
    if (!std::isfinite(config.heightScale) || config.heightScale <= 0.0f
        || !std::isfinite(config.cellScale) || config.cellScale <= 0.0f
        || !std::isfinite(config.fogDensity) || config.fogDensity < 0.0f) {
        LOG_ERROR("BlitPath::init: invalid renderer configuration");
        return false;
    }
    
    device_ = device;
    queue_ = queue;
    config_ = config;
    
    // Allocate uniforms on heap (reuses CameraUniforms from triangle_path)
    uniforms_ = new CameraUniforms();
    if (!uniforms_->setTerrain(
            terrainWidth_, terrainHeight_, config.heightScale,
            config.cellScale, 1.0f, config.fogDensity)) {
        shutdown();
        return false;
    }
    staticUniforms_ = new CameraUniforms(*uniforms_);
    updateStaticUniforms();
    
    // Bake the original periodic hash once on this rendering device.
    if (!periodicGradientLut_.initialize(device_, queue_)) {
        LOG_ERROR("Failed to create periodic gradient lookup table");
        shutdown();
        return false;
    }

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

    if (!createWaterClipmapResources(config)) {
        LOG_ERROR("Failed to create water clipmap resources");
        shutdown();
        return false;
    }

    if (!createSkyLut(config)) {
        LOG_ERROR("Failed to create sky LUT resources");
        shutdown();
        return false;
    }

    if (!createSurfaceFoamTexture()) {
        LOG_ERROR("Failed to create procedural ocean foam texture");
        shutdown();
        return false;
    }

    if (!createUnderwaterParticleResources(config)) {
        LOG_ERROR("Failed to create underwater particle resources");
        shutdown();
        return false;
    }

    LOG_INFO("BlitPath initialized successfully");
    return true;
}

bool BlitPath::resize(uint32_t width, uint32_t height) {
    if (!device_ || width == 0 || height == 0) {
        LOG_ERROR("BlitPath::resize: invalid device or dimensions");
        return false;
    }
    if (width == outputWidth_ && height == outputHeight_ && backgroundView_ &&
        coverageMaskView_) {
        return true;
    }

    if (!createBackgroundTexture(width, height)) return false;
    // Existing groups retain the previous views. createBindGroup() swaps them
    // only after all dynamic/static/cached replacements have been created.
    backgroundValid_ = false;
    backgroundDirty_ = true;
    bindGroupDirty_ = true;
    return true;
}

bool BlitPath::createBackgroundTexture(uint32_t width, uint32_t height) {
    WGPUTexture nextBackgroundTexture = nullptr;
    WGPUTextureView nextBackgroundView = nullptr;
    WGPUTexture nextCoverageTexture = nullptr;
    WGPUTextureView nextCoverageView = nullptr;
    const auto cleanup = [&]() {
        if (nextCoverageView) wgpuTextureViewRelease(nextCoverageView);
        if (nextCoverageTexture) wgpuTextureRelease(nextCoverageTexture);
        if (nextBackgroundView) wgpuTextureViewRelease(nextBackgroundView);
        if (nextBackgroundTexture) wgpuTextureRelease(nextBackgroundTexture);
    };

    gpu::TextureDesc desc = gpu::TextureDesc::renderTarget(
        width, height, WGPUTextureFormat_RGBA16Float,
        "blit_static_background");
    nextBackgroundTexture = gpu::createTexture(device_, desc);
    if (!nextBackgroundTexture) {
        LOG_ERROR("Failed to create static background texture");
        return false;
    }

    gpu::TextureViewDesc viewDesc{};
    viewDesc.label = "blit_static_background_view";
    viewDesc.format = WGPUTextureFormat_RGBA16Float;
    nextBackgroundView = gpu::createTextureView(
        nextBackgroundTexture, viewDesc);
    if (!nextBackgroundView) {
        LOG_ERROR("Failed to create static background texture view");
        cleanup();
        return false;
    }

    gpu::TextureDesc maskDesc = gpu::TextureDesc::depth(
        width, height,
        WGPUTextureFormat_Depth24PlusStencil8,
        "blit_water_coverage_mask");
    maskDesc.usage = WGPUTextureUsage_RenderAttachment;
    nextCoverageTexture = gpu::createTexture(device_, maskDesc);
    if (!nextCoverageTexture) {
        LOG_ERROR("Failed to create water coverage mask texture");
        cleanup();
        return false;
    }
    gpu::TextureViewDesc maskViewDesc{};
    maskViewDesc.label = "blit_water_coverage_mask_view";
    maskViewDesc.format = WGPUTextureFormat_Depth24PlusStencil8;
    nextCoverageView = gpu::createTextureView(
        nextCoverageTexture, maskViewDesc);
    if (!nextCoverageView) {
        LOG_ERROR("Failed to create water coverage mask view");
        cleanup();
        return false;
    }

    if (backgroundView_) wgpuTextureViewRelease(backgroundView_);
    if (backgroundTexture_) wgpuTextureRelease(backgroundTexture_);
    if (coverageMaskView_) wgpuTextureViewRelease(coverageMaskView_);
    if (coverageMaskTexture_) wgpuTextureRelease(coverageMaskTexture_);
    backgroundTexture_ = nextBackgroundTexture;
    backgroundView_ = nextBackgroundView;
    coverageMaskTexture_ = nextCoverageTexture;
    coverageMaskView_ = nextCoverageView;
    outputWidth_ = width;
    outputHeight_ = height;

    LOG_DEBUG("Created static background cache: {}x{}", width, height);
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Asset-Free Surface Foam Creation
// ─────────────────────────────────────────────────────────────────────────────

namespace {

constexpr uint32_t kSurfaceFoamSize = 1024;

[[nodiscard]] uint32_t foamHash(uint32_t x, uint32_t y,
                                uint32_t seed) noexcept {
    uint32_t value = x * 0x9e3779b9u ^ y * 0x85ebca6bu ^ seed;
    value ^= value >> 16u;
    value *= 0x7feb352du;
    value ^= value >> 15u;
    value *= 0x846ca68bu;
    return value ^ (value >> 16u);
}

[[nodiscard]] int32_t foamWrap(int32_t value, int32_t period) noexcept {
    const int32_t remainder = value % period;
    return remainder < 0 ? remainder + period : remainder;
}

[[nodiscard]] float foamSmoothstep(float edge0, float edge1,
                                   float value) noexcept {
    const float t = std::clamp((value - edge0) / (edge1 - edge0),
                               0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

[[nodiscard]] float foamGradientDot(uint32_t hash, float x,
                                    float y) noexcept {
    constexpr float kDiagonal = 0.70710678118f;
    switch (hash & 7u) {
        case 0u: return x;
        case 1u: return -x;
        case 2u: return y;
        case 3u: return -y;
        case 4u: return (x + y) * kDiagonal;
        case 5u: return (x - y) * kDiagonal;
        case 6u: return (-x + y) * kDiagonal;
        default: return (-x - y) * kDiagonal;
    }
}

[[nodiscard]] float periodicGradientNoise(float u, float v, int32_t period,
                                          uint32_t seed) noexcept {
    const float px = u * static_cast<float>(period);
    const float py = v * static_cast<float>(period);
    const float floorX = std::floor(px);
    const float floorY = std::floor(py);
    const int32_t ix = static_cast<int32_t>(floorX);
    const int32_t iy = static_cast<int32_t>(floorY);
    const float tx = px - floorX;
    const float ty = py - floorY;
    const float sx = tx * tx * tx *
        (tx * (tx * 6.0f - 15.0f) + 10.0f);
    const float sy = ty * ty * ty *
        (ty * (ty * 6.0f - 15.0f) + 10.0f);

    const auto cornerHash = [period, seed](int32_t x, int32_t y) noexcept {
        return foamHash(
            static_cast<uint32_t>(foamWrap(x, period)),
            static_cast<uint32_t>(foamWrap(y, period)), seed);
    };
    const float n00 = foamGradientDot(cornerHash(ix, iy), tx, ty);
    const float n10 = foamGradientDot(cornerHash(ix + 1, iy), tx - 1.0f, ty);
    const float n01 = foamGradientDot(cornerHash(ix, iy + 1), tx, ty - 1.0f);
    const float n11 = foamGradientDot(
        cornerHash(ix + 1, iy + 1), tx - 1.0f, ty - 1.0f);
    const float nx0 = n00 + (n10 - n00) * sx;
    const float nx1 = n01 + (n11 - n01) * sx;
    return (nx0 + (nx1 - nx0) * sy) * 1.41421356237f;
}

[[nodiscard]] float periodicFbm(float u, float v, int32_t basePeriod,
                                uint32_t seed) noexcept {
    float result = 0.0f;
    float normalization = 0.0f;
    float weight = 0.55f;
    int32_t period = basePeriod;
    for (uint32_t octave = 0; octave < 5u; ++octave) {
        result += periodicGradientNoise(
            u, v, period, seed + octave * 0x9e3779b9u) * weight;
        normalization += weight;
        weight *= 0.5f;
        period *= 2;
    }
    return std::clamp(0.5f + 0.5f * result / normalization, 0.0f, 1.0f);
}

[[nodiscard]] float periodicCellularEdge(float u, float v, int32_t period,
                                         uint32_t seed) noexcept {
    const float px = u * static_cast<float>(period);
    const float py = v * static_cast<float>(period);
    const int32_t cellX = static_cast<int32_t>(std::floor(px));
    const int32_t cellY = static_cast<int32_t>(std::floor(py));
    float nearest = 1.0e9f;
    float second = 1.0e9f;
    for (int32_t offsetY = -1; offsetY <= 1; ++offsetY) {
        for (int32_t offsetX = -1; offsetX <= 1; ++offsetX) {
            const int32_t candidateX = cellX + offsetX;
            const int32_t candidateY = cellY + offsetY;
            const uint32_t hashX = foamHash(
                static_cast<uint32_t>(foamWrap(candidateX, period)),
                static_cast<uint32_t>(foamWrap(candidateY, period)), seed);
            const uint32_t hashY = foamHash(
                static_cast<uint32_t>(foamWrap(candidateX, period)),
                static_cast<uint32_t>(foamWrap(candidateY, period)),
                seed ^ 0x9e3779b9u);
            const float featureX = static_cast<float>(hashX & 0xffffu) /
                                   65535.0f;
            const float featureY = static_cast<float>(hashY & 0xffffu) /
                                   65535.0f;
            const float dx = static_cast<float>(candidateX) + featureX - px;
            const float dy = static_cast<float>(candidateY) + featureY - py;
            const float distanceSquared = dx * dx + dy * dy;
            if (distanceSquared < nearest) {
                second = nearest;
                nearest = distanceSquared;
            } else if (distanceSquared < second) {
                second = distanceSquared;
            }
        }
    }
    const float boundaryDistance =
        std::sqrt(second) - std::sqrt(nearest);
    return 1.0f - foamSmoothstep(0.008f, 0.072f, boundaryDistance);
}

[[nodiscard]] std::array<uint8_t, 4> proceduralOceanMaterialTexel(
    uint32_t x, uint32_t y) noexcept {
    const float u = (static_cast<float>(x) + 0.5f) /
                    static_cast<float>(kSurfaceFoamSize);
    const float v = (static_cast<float>(y) + 0.5f) /
                    static_cast<float>(kSurfaceFoamSize);

    // A seamless domain warp feeds three cellular scales. The cellular edges
    // are admitted only inside irregular low-frequency patches: an ungated
    // Voronoi edge field reads as a continuous polygon grid once projected
    // across the ocean, while real surface foam breaks into nested islands,
    // bubbles, and short branching strands.
    const float warpX = periodicGradientNoise(u, v, 3, 0x37d4f12bu) +
        0.35f * periodicGradientNoise(u, v, 7, 0x7f4a7c15u);
    const float warpY = periodicGradientNoise(u, v, 3, 0xb49a85d1u) +
        0.35f * periodicGradientNoise(u, v, 7, 0x94d049bbu);
    const float warpedU = u + warpX * 0.085f;
    const float warpedV = v + warpY * 0.085f;

    const float broad = periodicFbm(
        warpedU, warpedV, 5, 0x6c8e9cf5u);
    const float flowU = warpedU + warpedV;
    const float flowV = -warpedU + 2.0f * warpedV;
    const float erosion = periodicFbm(
        flowU, flowV, 9, 0x85ebca6bu);
    const float breakup = periodicFbm(
        2.0f * warpedU + warpedV,
        -warpedU + warpedV, 17, 0x165667b1u);
    const float foamRegion = foamSmoothstep(
        0.54f, 0.74f, broad * 0.62f + erosion * 0.38f);
    const float largeWeb = periodicCellularEdge(
        warpedU, warpedV, 13, 0x6c8e9cf5u);
    const float mediumWeb = periodicCellularEdge(
        2.0f * warpedU + warpedV, -warpedU + 2.0f * warpedV,
        29, 0x85ebca6bu);
    const float bubbleWeb = periodicCellularEdge(
        3.0f * warpedU - 2.0f * warpedV,
        2.0f * warpedU + 3.0f * warpedV, 67, 0x165667b1u);
    const float network = std::max(
        largeWeb * foamRegion,
        std::max(mediumWeb * foamRegion * breakup,
                 bubbleWeb * foamRegion * breakup * 0.72f));
    const float mask = std::clamp(
        0.46f + broad * 0.14f + erosion * 0.06f + breakup * 0.04f +
            foamRegion * 0.05f + network * 0.30f,
        0.0f, 1.0f);

    // Pack a completely generated seabed albedo into GBA. Integer domain
    // transforms keep every octave tileable while producing rock shelves,
    // shell flecks, and crossed sand ripples at independent scales. R remains
    // the whitecap mask used by the surface material.
    const float sediment = periodicFbm(
        3.0f * u + 2.0f * v, -2.0f * u + 3.0f * v,
        7, 0xd1b54a35u);
    const float stoneField = periodicFbm(
        2.0f * u - v, u + 2.0f * v, 11, 0xa24baed5u);
    const float grain = periodicGradientNoise(
        5.0f * u + 3.0f * v, -3.0f * u + 5.0f * v,
        29, 0x9fb21c65u) * 0.5f + 0.5f;
    constexpr float kTau = 6.28318530717958647692f;
    const float rippleA = 0.5f + 0.5f * std::sin(
        kTau * (23.0f * u + 7.0f * v) + sediment * 2.2f);
    const float rippleB = 0.5f + 0.5f * std::sin(
        kTau * (-5.0f * u + 19.0f * v) - erosion * 1.7f);
    const float ripples = rippleA * 0.72f + rippleB * 0.28f;
    const float rock = foamSmoothstep(
        0.58f, 0.76f, stoneField + (sediment - 0.5f) * 0.36f);
    const float shells = foamSmoothstep(0.72f, 0.93f, grain) *
        (1.0f - rock) * foamSmoothstep(0.32f, 0.66f, breakup);

    // Irregular plates and buried seams provide readable metre-scale detail
    // from an underwater camera. They are generated into the same tile as the
    // fine sand, so no external floor image or extra recurring texture lookup
    // is required.
    const float plateRegion = foamSmoothstep(
        0.46f, 0.70f, stoneField * 0.72f + sediment * 0.28f);
    const float plateEdge = periodicCellularEdge(
        2.0f * u + v, -u + 2.0f * v, 23, 0x4cf5ad43u) *
        plateRegion * foamSmoothstep(0.24f, 0.68f, erosion);
    const float buriedSeam = periodicCellularEdge(
        3.0f * u - 2.0f * v, 2.0f * u + 3.0f * v,
        41, 0x27d4eb2fu) * (1.0f - plateRegion) * 0.42f;
    const float paverRow = std::floor(v * 24.0f);
    const float paverU = u * 32.0f +
        static_cast<float>(static_cast<int32_t>(paverRow) & 1) * 0.5f;
    const float paverV = v * 24.0f;
    const float edgeDistance = std::min(
        std::min(paverU - std::floor(paverU),
                 1.0f - (paverU - std::floor(paverU))),
        std::min(paverV - std::floor(paverV),
                 1.0f - (paverV - std::floor(paverV))));
    const float paverJoint =
        (1.0f - foamSmoothstep(0.018f, 0.075f, edgeDistance)) *
        foamSmoothstep(0.40f, 0.72f,
                       stoneField * 0.64f + sediment * 0.36f);
    const float mineral = foamSmoothstep(
        0.55f, 0.82f, sediment * 0.54f + breakup * 0.46f) *
        (1.0f - plateEdge);

    const std::array<float, 3> darkSand{0.32f, 0.32f, 0.20f};
    const std::array<float, 3> lightSand{0.91f, 0.84f, 0.62f};
    const std::array<float, 3> darkStone{0.085f, 0.12f, 0.085f};
    const std::array<float, 3> litStone{0.52f, 0.56f, 0.40f};
    const std::array<float, 3> shellColor{0.96f, 0.91f, 0.72f};
    const std::array<float, 3> mineralColor{0.57f, 0.49f, 0.29f};
    std::array<uint8_t, 4> packed{};
    packed[0] = static_cast<uint8_t>(std::lround(mask * 255.0f));
    for (size_t channel = 0; channel < 3; ++channel) {
        const float sandMix = std::clamp(
            0.16f + sediment * 0.72f + ripples * 0.12f,
            0.0f, 1.0f);
        const float sand = darkSand[channel] +
            (lightSand[channel] - darkSand[channel]) * sandMix;
        const float stoneMix = std::clamp(
            0.18f + grain * 0.42f + erosion * 0.28f,
            0.0f, 1.0f);
        const float stone = darkStone[channel] +
            (litStone[channel] - darkStone[channel]) * stoneMix;
        float base = (sand + (stone - sand) * rock) *
            (0.94f + grain * 0.12f + (rippleA - 0.5f) * 0.03f);
        base += (mineralColor[channel] - base) * mineral * 0.22f;
        base *= 1.0f - plateEdge * 0.48f - buriedSeam * 0.20f -
                paverJoint * 0.38f;
        const float albedo = base + (shellColor[channel] - base) * shells;
        packed[channel + 1] = static_cast<uint8_t>(std::lround(
            std::clamp(albedo, 0.0f, 1.0f) * 255.0f));
    }
    return packed;
}

} // namespace

bool BlitPath::createSurfaceFoamTexture() {
    uint32_t width = kSurfaceFoamSize;
    uint32_t height = kSurfaceFoamSize;
    constexpr uint32_t kChannels = 4u;
    std::vector<uint8_t> mip(
        static_cast<size_t>(width) * height * kChannels);
    for (uint32_t y = 0; y < height; ++y) {
        for (uint32_t x = 0; x < width; ++x) {
            const auto texel = proceduralOceanMaterialTexel(x, y);
            const size_t offset =
                (static_cast<size_t>(y) * width + x) * kChannels;
            std::memcpy(mip.data() + offset, texel.data(), texel.size());
        }
    }

    gpu::TextureDesc desc = gpu::TextureDesc::tex2DMipmapped(
        width, height, WGPUTextureFormat_RGBA8Unorm,
        WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst,
        "ocean_procedural_material");
    surfaceFoamTexture_ = gpu::createTexture(device_, desc);
    if (!surfaceFoamTexture_) return false;

    for (uint32_t level = 0; level < desc.mipLevelCount; ++level) {
        if (!gpu::writeTexture(
                queue_, surfaceFoamTexture_,
                std::as_bytes(std::span<const uint8_t>(mip)),
                width, height, width * kChannels, level)) {
            return false;
        }
        if (width == 1 && height == 1) break;

        const uint32_t nextWidth = std::max(width / 2, 1u);
        const uint32_t nextHeight = std::max(height / 2, 1u);
        std::vector<uint8_t> next(
            static_cast<size_t>(nextWidth) * nextHeight * kChannels);
        for (uint32_t y = 0; y < nextHeight; ++y) {
            for (uint32_t x = 0; x < nextWidth; ++x) {
                const uint32_t x0 = std::min(x * 2, width - 1);
                const uint32_t x1 = std::min(x0 + 1, width - 1);
                const uint32_t y0 = std::min(y * 2, height - 1);
                const uint32_t y1 = std::min(y0 + 1, height - 1);
                for (uint32_t channel = 0; channel < kChannels; ++channel) {
                    const auto source = [&](uint32_t sx, uint32_t sy) {
                        return static_cast<uint32_t>(mip[
                            (static_cast<size_t>(sy) * width + sx) *
                                kChannels + channel]);
                    };
                    const uint32_t sum = source(x0, y0) + source(x1, y0) +
                                         source(x0, y1) + source(x1, y1);
                    next[(static_cast<size_t>(y) * nextWidth + x) *
                             kChannels + channel] =
                        static_cast<uint8_t>((sum + 2u) / 4u);
                }
            }
        }
        mip = std::move(next);
        width = nextWidth;
        height = nextHeight;
    }

    gpu::TextureViewDesc viewDesc{};
    viewDesc.label = "ocean_procedural_material_view";
    viewDesc.format = WGPUTextureFormat_RGBA8Unorm;
    viewDesc.mipLevelCount = desc.mipLevelCount;
    surfaceFoamView_ = gpu::createTextureView(surfaceFoamTexture_, viewDesc);
    if (!surfaceFoamView_) return false;

    gpu::SamplerDesc samplerDesc =
        gpu::SamplerDesc::linear("ocean_procedural_material_sampler");
    samplerDesc.addressModeU = WGPUAddressMode_Repeat;
    samplerDesc.addressModeV = WGPUAddressMode_Repeat;
    // This repeat sampler is shared by the ocean field and terrain detail
    // arrays. Anisotropy keeps world-projected material stable at shore-grazing
    // angles without consuming another portable sampler slot.
    samplerDesc.maxAnisotropy = 8u;
    surfaceFoamSampler_ = gpu::createSampler(device_, samplerDesc);
    if (!surfaceFoamSampler_) return false;

    LOG_DEBUG("Generated procedural ocean material ({}x{}, {} mips)",
              kSurfaceFoamSize, kSurfaceFoamSize, desc.mipLevelCount);
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Underwater Particle Creation
// ─────────────────────────────────────────────────────────────────────────────

namespace {
constexpr uint32_t kUnderwaterParticleCount = 7200u;
constexpr float kParticleNearDistance = 2.5f;
constexpr float kParticleFarDistance = 65.0f;
constexpr float kParticleTau = 6.28318530717958647692f;
} // namespace

bool BlitPath::createUnderwaterParticleResources(
    const BlitPathConfig& config) {
    underwaterParticles_.assign(kUnderwaterParticleCount, glm::vec4(0.0f));
    for (glm::vec4& particle : underwaterParticles_) {
        particle.w = nextParticleRandom();
    }
    const uint64_t byteSize =
        underwaterParticles_.size() * sizeof(glm::vec4);
    particleBuffer_ = gpu::createBuffer(
        device_, gpu::BufferDesc::storage(
                     byteSize, true, "underwater_particle_instances"));
    if (!particleBuffer_) return false;
    if (!gpu::writeBuffer(
            queue_, particleBuffer_, 0,
            std::span<const glm::vec4>(underwaterParticles_))) {
        return false;
    }

    std::array<gpu::BindGroupLayoutEntry, 3> entries = {
        gpu::BindGroupLayoutEntry(0)
            .vertexVisible()
            .uniformBuffer(false, sizeof(CameraUniforms)),
        gpu::BindGroupLayoutEntry(1)
            .fragmentVisible()
            .texture(WGPUTextureSampleType_UnfilterableFloat,
                     WGPUTextureViewDimension_2D, false),
        gpu::BindGroupLayoutEntry(2)
            .vertexVisible()
            .storageBuffer(true, false, sizeof(glm::vec4))
    };
    particleBindGroupLayout_ = gpu::createBindGroupLayout(
        device_, entries, "underwater_particle_bind_group_layout");
    if (!particleBindGroupLayout_) return false;

    const std::array<WGPUBindGroupLayout, 1> layouts = {
        particleBindGroupLayout_};
    particlePipelineLayout_ = gpu::createPipelineLayout(
        device_, layouts, "underwater_particle_pipeline_layout");
    if (!particlePipelineLayout_) return false;

    const std::filesystem::path shaderPath =
        config.shaderPath.parent_path() / "underwater_particles.wgsl";
    particleShaderModule_ = gpu::loadShaderModule(
        device_, shaderPath, "underwater_particles.wgsl");
    if (!particleShaderModule_) return false;

    WGPUVertexState vertexState{};
    vertexState.module = particleShaderModule_;
    WGPU_SET_ENTRY_POINT(vertexState, "vs");

    WGPUBlendState blend{};
    blend.color.operation = WGPUBlendOperation_Add;
    blend.color.srcFactor = WGPUBlendFactor_SrcAlpha;
    blend.color.dstFactor = WGPUBlendFactor_OneMinusSrcAlpha;
    blend.alpha.operation = WGPUBlendOperation_Add;
    blend.alpha.srcFactor = WGPUBlendFactor_One;
    blend.alpha.dstFactor = WGPUBlendFactor_OneMinusSrcAlpha;

    WGPUColorTargetState colorTarget{};
    colorTarget.format = config.colorFormat;
    colorTarget.blend = &blend;
    colorTarget.writeMask = WGPUColorWriteMask_All;
    WGPUFragmentState fragmentState{};
    fragmentState.module = particleShaderModule_;
    WGPU_SET_ENTRY_POINT(fragmentState, "fs");
    fragmentState.targetCount = 1;
    fragmentState.targets = &colorTarget;

    WGPUPrimitiveState primitiveState{};
    primitiveState.topology = WGPUPrimitiveTopology_TriangleList;
    primitiveState.frontFace = WGPUFrontFace_CCW;
    primitiveState.cullMode = WGPUCullMode_None;
    WGPUMultisampleState multisample{};
    multisample.count = 1;
    multisample.mask = ~0u;

    WGPURenderPipelineDescriptor descriptor{};
    WGPU_SET_LABEL(descriptor, "underwater_particle_pipeline");
    descriptor.layout = particlePipelineLayout_;
    descriptor.vertex = vertexState;
    descriptor.fragment = &fragmentState;
    descriptor.primitive = primitiveState;
    descriptor.multisample = multisample;
    particlePipeline_ = wgpuDeviceCreateRenderPipeline(device_, &descriptor);
    return particlePipeline_ != nullptr;
}

float BlitPath::nextParticleRandom() {
    // Small deterministic generator: only 24 high bits are consumed so the
    // generated values have stable fp32 precision on native and WASM.
    particleRandomState_ ^= particleRandomState_ << 13u;
    particleRandomState_ ^= particleRandomState_ >> 17u;
    particleRandomState_ ^= particleRandomState_ << 5u;
    return static_cast<float>(particleRandomState_ >> 8u) *
           (1.0f / 16777216.0f);
}

bool BlitPath::respawnUnderwaterParticle(size_t index,
                                         const glm::vec3& cameraPos,
                                         float surfaceHeight) {
    constexpr size_t kAttempts = 20;
    constexpr float kNearCubed = kParticleNearDistance *
        kParticleNearDistance * kParticleNearDistance;
    constexpr float kFarCubed = kParticleFarDistance *
        kParticleFarDistance * kParticleFarDistance;
    for (size_t attempt = 0; attempt < kAttempts; ++attempt) {
        const float angle = nextParticleRandom() * kParticleTau;
        const float polarCosine = nextParticleRandom() * 2.0f - 1.0f;
        const float polarSine = std::sqrt(
            std::max(1.0f - polarCosine * polarCosine, 0.0f));
        const glm::vec3 direction{
            polarSine * std::cos(angle), polarSine * std::sin(angle),
            polarCosine};
        const float radius = std::cbrt(
            kNearCubed + nextParticleRandom() * (kFarCubed - kNearCubed));
        const glm::vec3 position = cameraPos + direction * radius;
        if (position.y < surfaceHeight - 0.5f) {
            underwaterParticles_[index].x = position.x;
            underwaterParticles_[index].y = position.y;
            underwaterParticles_[index].z = position.z;
            return true;
        }
    }
    underwaterParticles_[index].w = 0.0f;
    return false;
}

bool BlitPath::updateUnderwaterParticles() {
    if (!uniforms_ || !particleBuffer_ || underwaterParticles_.empty()) {
        return false;
    }
    const glm::vec3 cameraPos{uniforms_->cameraPos};
    const float surfaceHeight = uniforms_->waterParams.x +
                                uniforms_->waterMotion.y;
    constexpr float kNearSquared =
        kParticleNearDistance * kParticleNearDistance;
    constexpr float kFarSquared =
        kParticleFarDistance * kParticleFarDistance;
    for (size_t index = 0; index < underwaterParticles_.size(); ++index) {
        glm::vec4& particle = underwaterParticles_[index];
        const glm::vec3 offset = glm::vec3(particle) - cameraPos;
        const float distanceSquared = glm::dot(offset, offset);
        if (!particlesInitialized_ || distanceSquared < kNearSquared ||
            distanceSquared > kFarSquared ||
            particle.y > surfaceHeight - 0.5f || particle.w <= 0.0f) {
            if (particle.w <= 0.0f) particle.w = nextParticleRandom();
            (void)respawnUnderwaterParticle(
                index, cameraPos, surfaceHeight);
        }
    }
    if (!gpu::writeBuffer(
            queue_, particleBuffer_, 0,
            std::span<const glm::vec4>(underwaterParticles_))) {
        return false;
    }
    particlesInitialized_ = true;
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Sky LUT Creation
// ─────────────────────────────────────────────────────────────────────────────

namespace {
/// The original environment is generated specifically for this renderer in a
/// full-sphere 2:1 projection. Keeping its native resolution retains the fine
/// cloud edges seen in near-field wave reflections at no recurring sample cost.
constexpr uint32_t kSkyLutWidth = 1774;
constexpr uint32_t kSkyLutHeight = 887;
constexpr uint32_t kSkyLutMipCount =
    gpu::calculateMipLevelCount(kSkyLutWidth, kSkyLutHeight);
constexpr std::streamoff kMaximumEncodedEnvironmentBytes =
    64ll * 1024ll * 1024ll;

[[nodiscard]] float skySrgbToLinear(float value) noexcept {
    return value <= 0.04045f
        ? value / 12.92f
        : std::pow((value + 0.055f) / 1.055f, 2.4f);
}

[[nodiscard]] bool uploadGeneratedEnvironment(
    WGPUQueue queue, WGPUTexture texture,
    const std::filesystem::path& path) {
    if (path.empty() || !std::filesystem::exists(path)) return false;

    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) return false;
    const auto fileSize = file.tellg();
    if (fileSize <= 0 ||
        fileSize > std::numeric_limits<std::streamsize>::max() ||
        fileSize > std::numeric_limits<int>::max() ||
        fileSize > kMaximumEncodedEnvironmentBytes) {
        return false;
    }
    const auto size = static_cast<std::streamsize>(fileSize);
    file.seekg(0, std::ios::beg);
    std::vector<uint8_t> encoded(static_cast<size_t>(size));
    if (!file.read(reinterpret_cast<char*>(encoded.data()), size)) {
        return false;
    }

    int decodedWidth = 0;
    int decodedHeight = 0;
    int channels = 0;
    if (!stbi_info_from_memory(
            encoded.data(), static_cast<int>(encoded.size()),
            &decodedWidth, &decodedHeight, &channels)
        || decodedWidth != static_cast<int>(kSkyLutWidth)
        || decodedHeight != static_cast<int>(kSkyLutHeight)) {
        LOG_ERROR("Generated environment has unexpected dimensions {}x{}; "
                  "expected {}x{}", decodedWidth, decodedHeight,
                  kSkyLutWidth, kSkyLutHeight);
        return false;
    }

    uint8_t* decoded = stbi_load_from_memory(
        encoded.data(), static_cast<int>(encoded.size()),
        &decodedWidth, &decodedHeight, &channels, 4);
    if (!decoded) return false;
    if (decodedWidth != static_cast<int>(kSkyLutWidth) ||
        decodedHeight != static_cast<int>(kSkyLutHeight)) {
        LOG_ERROR("Generated environment has unexpected dimensions {}x{}; "
                  "expected {}x{}", decodedWidth, decodedHeight,
                  kSkyLutWidth, kSkyLutHeight);
        stbi_image_free(decoded);
        return false;
    }

    uint32_t width = kSkyLutWidth;
    uint32_t height = kSkyLutHeight;
    std::vector<float> linear(
        static_cast<size_t>(width) * height * 4u, 1.0f);
    for (size_t pixel = 0;
         pixel < static_cast<size_t>(width) * height; ++pixel) {
        const float red = static_cast<float>(decoded[pixel * 4u]) / 255.0f;
        const float green =
            static_cast<float>(decoded[pixel * 4u + 1u]) / 255.0f;
        const float blue =
            static_cast<float>(decoded[pixel * 4u + 2u]) / 255.0f;
        const float luminance =
            red * 0.2126f + green * 0.7152f + blue * 0.0722f;
        const float highlight = std::clamp(
            (luminance - 0.52f) / 0.44f, 0.0f, 1.0f);
        const float radianceScale = 0.82f + highlight * highlight * 2.45f;
        linear[pixel * 4u] = skySrgbToLinear(red) * radianceScale;
        linear[pixel * 4u + 1u] = skySrgbToLinear(green) * radianceScale;
        linear[pixel * 4u + 2u] = skySrgbToLinear(blue) * radianceScale;
    }
    stbi_image_free(decoded);

    // Cross-fade paired border texels so filtering remains continuous even if
    // the generated panorama contains a small residual exposure difference at
    // its horizontal join.
    constexpr uint32_t kSeamBlendWidth = 48u;
    for (uint32_t y = 0; y < height; ++y) {
        for (uint32_t x = 0; x < kSeamBlendWidth; ++x) {
            const uint32_t oppositeX = width - 1u - x;
            const float strength = 1.0f -
                static_cast<float>(x) /
                    static_cast<float>(kSeamBlendWidth);
            for (uint32_t channel = 0; channel < 3u; ++channel) {
                const size_t left =
                    (static_cast<size_t>(y) * width + x) * 4u + channel;
                const size_t right =
                    (static_cast<size_t>(y) * width + oppositeX) * 4u +
                    channel;
                const float average = (linear[left] + linear[right]) * 0.5f;
                linear[left] += (average - linear[left]) * strength;
                linear[right] += (average - linear[right]) * strength;
            }
        }
    }

    for (uint32_t level = 0; level < kSkyLutMipCount; ++level) {
        std::vector<uint16_t> packed(linear.size());
        for (size_t component = 0; component < linear.size(); ++component) {
            packed[component] = glm::packHalf1x16(
                std::clamp(linear[component], 0.0f, 65504.0f));
        }
        if (!gpu::writeTexture(
                queue, texture,
                std::as_bytes(std::span<const uint16_t>(packed)),
                width, height, width * 8u, level)) {
            return false;
        }
        if (width == 1u && height == 1u) break;

        const uint32_t nextWidth = std::max(width / 2u, 1u);
        const uint32_t nextHeight = std::max(height / 2u, 1u);
        std::vector<float> next(
            static_cast<size_t>(nextWidth) * nextHeight * 4u, 1.0f);
        for (uint32_t y = 0; y < nextHeight; ++y) {
            for (uint32_t x = 0; x < nextWidth; ++x) {
                const uint32_t x0 = std::min(x * 2u, width - 1u);
                const uint32_t x1 = std::min(x0 + 1u, width - 1u);
                const uint32_t y0 = std::min(y * 2u, height - 1u);
                const uint32_t y1 = std::min(y0 + 1u, height - 1u);
                for (uint32_t channel = 0; channel < 4u; ++channel) {
                    const auto sample = [&](uint32_t sx, uint32_t sy) {
                        return linear[(static_cast<size_t>(sy) * width + sx) *
                                      4u + channel];
                    };
                    next[(static_cast<size_t>(y) * nextWidth + x) * 4u +
                         channel] =
                        (sample(x0, y0) + sample(x1, y0) +
                         sample(x0, y1) + sample(x1, y1)) * 0.25f;
                }
            }
        }
        linear = std::move(next);
        width = nextWidth;
        height = nextHeight;
    }
    LOG_INFO("Loaded original generated ocean environment: {}", path.string());
    return true;
}
} // namespace

bool BlitPath::createSkyLut(const BlitPathConfig& config) {
    // Output texture: storage write for the bake, sampled read for the blit.
    gpu::TextureDesc lutDesc = gpu::TextureDesc::storage(
        kSkyLutWidth, kSkyLutHeight, WGPUTextureFormat_RGBA16Float,
        "sky_lut");
    lutDesc.usage |= WGPUTextureUsage_CopyDst;
    lutDesc.mipLevelCount = kSkyLutMipCount;
    skyLutTexture_ = gpu::createTexture(device_, lutDesc);
    if (!skyLutTexture_) {
        LOG_ERROR("Failed to create sky LUT texture");
        return false;
    }
    gpu::TextureViewDesc sampledViewDesc{};
    sampledViewDesc.label = "sky_lut_sampled_view";
    sampledViewDesc.format = WGPUTextureFormat_RGBA16Float;
    sampledViewDesc.mipLevelCount = kSkyLutMipCount;
    skyLutView_ = gpu::createTextureView(skyLutTexture_, sampledViewDesc);
    if (!skyLutView_) {
        LOG_ERROR("Failed to create sky LUT texture view");
        return false;
    }

    if (uploadGeneratedEnvironment(
            queue_, skyLutTexture_, config.environmentPath)) {
        skyLutBaked_ = true;
        return true;
    }

    LOG_WARN("Generated environment unavailable; using procedural sky bake");
    skyLutBaseView_ = gpu::createMipView(
        skyLutTexture_, 0, WGPUTextureFormat_RGBA16Float);
    if (!skyLutBaseView_) {
        LOG_ERROR("Failed to create sky LUT base-mip storage view");
        return false;
    }

    // Bake compute pipeline
    const auto shaderPath = config.shaderPath.parent_path() / "sky_lut.wgsl";
    skyLutShaderModule_ = gpu::loadShaderModule(device_, shaderPath, "sky_lut.wgsl");
    if (!skyLutShaderModule_) {
        LOG_ERROR("Failed to load sky LUT shader from: {}", shaderPath.string());
        return false;
    }

    std::array<gpu::BindGroupLayoutEntry, 2> layoutEntries = {
        gpu::BindGroupLayoutEntry(0)
            .computeVisible()
            .uniformBuffer(false, sizeof(CameraUniforms)),
        gpu::BindGroupLayoutEntry(1)
            .computeVisible()
            .storageTexture(WGPUStorageTextureAccess_WriteOnly,
                            WGPUTextureFormat_RGBA16Float,
                            WGPUTextureViewDimension_2D)
    };
    skyLutBindGroupLayout_ =
        gpu::createBindGroupLayout(device_, layoutEntries, "sky_lut_bind_group_layout");
    if (!skyLutBindGroupLayout_) {
        LOG_ERROR("Failed to create sky LUT bind group layout");
        return false;
    }

    std::array<WGPUBindGroupLayout, 1> layouts = { skyLutBindGroupLayout_ };
    skyLutPipelineLayout_ = gpu::createPipelineLayout(device_, layouts, "sky_lut_pipeline_layout");
    if (!skyLutPipelineLayout_) {
        LOG_ERROR("Failed to create sky LUT pipeline layout");
        return false;
    }

    WGPUComputePipelineDescriptor pipelineDesc{};
    WGPU_SET_LABEL(pipelineDesc, "sky_lut_pipeline");
    pipelineDesc.layout = skyLutPipelineLayout_;
    pipelineDesc.compute.module = skyLutShaderModule_;
    WGPU_SET_ENTRY_POINT(pipelineDesc.compute, "main");
    skyLutPipeline_ = wgpuDeviceCreateComputePipeline(device_, &pipelineDesc);
    if (!skyLutPipeline_) {
        LOG_ERROR("Failed to create sky LUT compute pipeline");
        return false;
    }

    std::array<gpu::BindGroupEntry, 2> groupEntries = {
        gpu::BindGroupEntry(0).buffer(uniformBuffer_, 0, sizeof(CameraUniforms)),
        gpu::BindGroupEntry(1).textureView(skyLutBaseView_)
    };
    skyLutBindGroup_ =
        gpu::createBindGroup(device_, skyLutBindGroupLayout_, groupEntries, "sky_lut_bind_group");
    if (!skyLutBindGroup_) {
        LOG_ERROR("Failed to create sky LUT bind group");
        return false;
    }

    // Build every roughness level explicitly. WebGPU has no implicit mip
    // generation and the reflection shader samples a continuous mip LOD.
    const auto mipShaderPath =
        config.shaderPath.parent_path() / "sky_lut_mip.wgsl";
    skyLutMipShaderModule_ = gpu::loadShaderModule(
        device_, mipShaderPath, "sky_lut_mip.wgsl");
    if (!skyLutMipShaderModule_) {
        LOG_ERROR("Failed to load sky LUT mip shader from: {}",
                  mipShaderPath.string());
        return false;
    }

    std::array<gpu::BindGroupLayoutEntry, 2> mipLayoutEntries = {
        gpu::BindGroupLayoutEntry(0)
            .computeVisible()
            .texture(WGPUTextureSampleType_UnfilterableFloat),
        gpu::BindGroupLayoutEntry(1)
            .computeVisible()
            .storageTexture(WGPUStorageTextureAccess_WriteOnly,
                            WGPUTextureFormat_RGBA16Float,
                            WGPUTextureViewDimension_2D)
    };
    skyLutMipBindGroupLayout_ = gpu::createBindGroupLayout(
        device_, mipLayoutEntries, "sky_lut_mip_bind_group_layout");
    if (!skyLutMipBindGroupLayout_) {
        LOG_ERROR("Failed to create sky LUT mip bind group layout");
        return false;
    }

    std::array<WGPUBindGroupLayout, 1> mipLayouts = {
        skyLutMipBindGroupLayout_
    };
    skyLutMipPipelineLayout_ = gpu::createPipelineLayout(
        device_, mipLayouts, "sky_lut_mip_pipeline_layout");
    if (!skyLutMipPipelineLayout_) {
        LOG_ERROR("Failed to create sky LUT mip pipeline layout");
        return false;
    }

    WGPUComputePipelineDescriptor mipPipelineDesc{};
    WGPU_SET_LABEL(mipPipelineDesc, "sky_lut_mip_pipeline");
    mipPipelineDesc.layout = skyLutMipPipelineLayout_;
    mipPipelineDesc.compute.module = skyLutMipShaderModule_;
    WGPU_SET_ENTRY_POINT(mipPipelineDesc.compute, "main");
    skyLutMipPipeline_ =
        wgpuDeviceCreateComputePipeline(device_, &mipPipelineDesc);
    if (!skyLutMipPipeline_) {
        LOG_ERROR("Failed to create sky LUT mip compute pipeline");
        return false;
    }

    skyLutMipViews_.reserve((kSkyLutMipCount - 1) * 2);
    skyLutMipBindGroups_.reserve(kSkyLutMipCount - 1);
    for (uint32_t level = 1; level < kSkyLutMipCount; ++level) {
        WGPUTextureView sourceView = gpu::createMipView(
            skyLutTexture_, level - 1, WGPUTextureFormat_RGBA16Float);
        WGPUTextureView destinationView = gpu::createMipView(
            skyLutTexture_, level, WGPUTextureFormat_RGBA16Float);
        if (!sourceView || !destinationView) {
            if (sourceView) wgpuTextureViewRelease(sourceView);
            if (destinationView) wgpuTextureViewRelease(destinationView);
            LOG_ERROR("Failed to create sky LUT mip {} views", level);
            return false;
        }

        std::array<gpu::BindGroupEntry, 2> mipGroupEntries = {
            gpu::BindGroupEntry(0).textureView(sourceView),
            gpu::BindGroupEntry(1).textureView(destinationView)
        };
        WGPUBindGroup mipBindGroup = gpu::createBindGroup(
            device_, skyLutMipBindGroupLayout_, mipGroupEntries,
            "sky_lut_mip_bind_group");
        if (!mipBindGroup) {
            wgpuTextureViewRelease(sourceView);
            wgpuTextureViewRelease(destinationView);
            LOG_ERROR("Failed to create sky LUT mip {} bind group", level);
            return false;
        }
        skyLutMipViews_.push_back(sourceView);
        skyLutMipViews_.push_back(destinationView);
        skyLutMipBindGroups_.push_back(mipBindGroup);
    }

    LOG_DEBUG("Created sky LUT resources ({}x{}, {} mips)",
              kSkyLutWidth, kSkyLutHeight, kSkyLutMipCount);
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

    bufferDesc.label = "blit_static_camera_uniforms";
    staticUniformBuffer_ = gpu::createBuffer(device_, bufferDesc);
    if (!staticUniformBuffer_) {
        LOG_ERROR("Failed to create static camera uniform buffer");
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
    if (!updateUniformBuffer()
        || !gpu::writeBuffer(
            queue_, staticUniformBuffer_, 0, *staticUniforms_)) {
        return false;
    }
    staticUniformsDirty_ = false;
    
    // Upload initial debug uniforms
    DebugUniforms debugUniforms;
    debugUniforms.mode = debugMode_;
    debugUniforms.maxDepth = debugMaxDepth_;
    if (!gpu::writeBuffer(
            queue_, debugUniformBuffer_, 0, debugUniforms)) {
        return false;
    }
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
    // @group(0) @binding(3) var materialTex : texture_2d<f32>;
    // @group(0) @binding(4) var terrainTex : texture_2d<f32>;
    // @group(0) @binding(5) var lightmapTex : texture_2d<f32>;
    // @group(0) @binding(6) var terrainSampler : sampler;
    // @group(0) @binding(7) var<uniform> debug : DebugUniforms;
    // @group(0) @binding(8) var skyLUT : texture_2d<f32>;
    // @group(0) @binding(9) var oceanFoamTex : texture_2d<f32>;
    // @group(0) @binding(10) var oceanFoamSampler : sampler;
    // @group(0) @binding(17) var terrainMaterialAlbedo : texture_2d_array<f32>;
    // @group(0) @binding(18) var terrainMaterialNormalRoughness :
    //     texture_2d_array<f32>;

    std::array<gpu::BindGroupLayoutEntry, 14> entries = {
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
            .texture(WGPUTextureSampleType_Float, WGPUTextureViewDimension_2D, false),
        gpu::BindGroupLayoutEntry(6)
            .fragmentVisible()
            .sampler(WGPUSamplerBindingType_Filtering),
        gpu::BindGroupLayoutEntry(7)
            .fragmentVisible()
            .uniformBuffer(false, sizeof(DebugUniforms)),
        gpu::BindGroupLayoutEntry(8)
            .fragmentVisible()
            .texture(WGPUTextureSampleType_Float, WGPUTextureViewDimension_2D, false),
        gpu::BindGroupLayoutEntry(9)
            .fragmentVisible()
            .texture(WGPUTextureSampleType_Float, WGPUTextureViewDimension_2D, false),
        gpu::BindGroupLayoutEntry(10)
            .fragmentVisible()
            .sampler(WGPUSamplerBindingType_Filtering),
        gpu::BindGroupLayoutEntry(17)
            .fragmentVisible()
            .texture(WGPUTextureSampleType_Float,
                     WGPUTextureViewDimension_2DArray, false),
        gpu::BindGroupLayoutEntry(18)
            .fragmentVisible()
            .texture(WGPUTextureSampleType_Float,
                     WGPUTextureViewDimension_2DArray, false),
        gpu::BindGroupLayoutEntry(19)
            .fragmentVisible()
            .texture(WGPUTextureSampleType_UnfilterableFloat,
                     WGPUTextureViewDimension_2D, false)
    };
    
    bindGroupLayout_ = gpu::createBindGroupLayout(device_, entries, "blit_bind_group_layout");
    
    if (!bindGroupLayout_) {
        LOG_ERROR("Failed to create bind group layout");
        return false;
    }

    // The settled-camera pipeline shades animated water directly over the exact
    // cached HDR terrain/sky. Bindings 13-16 are the same geometry inputs used
    // by the old intermediate water-composite compute pass.
    std::array<gpu::BindGroupLayoutEntry, 20> cachedEntries = {
        gpu::BindGroupLayoutEntry(0)
            .vertexVisible()
            .fragmentVisible()
            .uniformBuffer(false, sizeof(CameraUniforms)),
        gpu::BindGroupLayoutEntry(1)
            .fragmentVisible()
            .texture(WGPUTextureSampleType_UnfilterableFloat,
                     WGPUTextureViewDimension_2D, false),
        gpu::BindGroupLayoutEntry(2)
            .fragmentVisible()
            .texture(WGPUTextureSampleType_UnfilterableFloat,
                     WGPUTextureViewDimension_2D, false),
        gpu::BindGroupLayoutEntry(3)
            .fragmentVisible()
            .texture(WGPUTextureSampleType_Float,
                     WGPUTextureViewDimension_2D, false),
        gpu::BindGroupLayoutEntry(4)
            .fragmentVisible()
            .texture(WGPUTextureSampleType_Float,
                     WGPUTextureViewDimension_2D, false),
        gpu::BindGroupLayoutEntry(5)
            .fragmentVisible()
            .texture(WGPUTextureSampleType_Float,
                     WGPUTextureViewDimension_2D, false),
        gpu::BindGroupLayoutEntry(6)
            .fragmentVisible()
            .sampler(WGPUSamplerBindingType_Filtering),
        gpu::BindGroupLayoutEntry(7)
            .fragmentVisible()
            .uniformBuffer(false, sizeof(DebugUniforms)),
        gpu::BindGroupLayoutEntry(8)
            .fragmentVisible()
            .texture(WGPUTextureSampleType_Float,
                     WGPUTextureViewDimension_2D, false),
        gpu::BindGroupLayoutEntry(9)
            .fragmentVisible()
            .texture(WGPUTextureSampleType_Float,
                     WGPUTextureViewDimension_2D, false),
        gpu::BindGroupLayoutEntry(10)
            .fragmentVisible()
            .sampler(WGPUSamplerBindingType_Filtering),
        gpu::BindGroupLayoutEntry(11)
            .fragmentVisible()
            .texture(WGPUTextureSampleType_Float,
                     WGPUTextureViewDimension_2D, false),
        gpu::BindGroupLayoutEntry(12)
            .fragmentVisible()
            .texture(WGPUTextureSampleType_UnfilterableFloat,
                     WGPUTextureViewDimension_2D, false),
        gpu::BindGroupLayoutEntry(13)
            .fragmentVisible()
            .texture(WGPUTextureSampleType_Uint,
                     WGPUTextureViewDimension_2D, false),
        gpu::BindGroupLayoutEntry(14)
            .fragmentVisible()
            .texture(WGPUTextureSampleType_Uint,
                     WGPUTextureViewDimension_2D, false),
        gpu::BindGroupLayoutEntry(15)
            .vertexVisible()
            .fragmentVisible()
            .texture(WGPUTextureSampleType_Float,
                     WGPUTextureViewDimension_2DArray, false),
        gpu::BindGroupLayoutEntry(16)
            .vertexVisible()
            .fragmentVisible()
            .sampler(WGPUSamplerBindingType_Filtering),
        gpu::BindGroupLayoutEntry(17)
            .fragmentVisible()
            .texture(WGPUTextureSampleType_Float,
                     WGPUTextureViewDimension_2DArray, false),
        gpu::BindGroupLayoutEntry(18)
            .fragmentVisible()
            .texture(WGPUTextureSampleType_Float,
                     WGPUTextureViewDimension_2DArray, false),
        gpu::BindGroupLayoutEntry(19)
            .fragmentVisible()
            .texture(WGPUTextureSampleType_UnfilterableFloat,
                     WGPUTextureViewDimension_2D, false)
    };
    cachedBindGroupLayout_ = gpu::createBindGroupLayout(
        device_, cachedEntries, "blit_cached_bind_group_layout");
    if (!cachedBindGroupLayout_) {
        LOG_ERROR("Failed to create cached blit bind group layout");
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

    // The cached opaque scene stays in linear HDR so water can refract it
    // before the one and only presentation transform.
    colorTarget.format = WGPUTextureFormat_RGBA16Float;
    WGPU_SET_ENTRY_POINT(fragmentState, "fsBackground");
    WGPU_SET_LABEL(pipelineDesc, "blit_background_pipeline");
    backgroundPipeline_ =
        wgpuDeviceCreateRenderPipeline(device_, &pipelineDesc);
    if (!backgroundPipeline_) {
        LOG_ERROR("Failed to create linear background pipeline");
        return false;
    }

    std::array<WGPUBindGroupLayout, 1> cachedLayouts = {
        cachedBindGroupLayout_
    };
    cachedPipelineLayout_ = gpu::createPipelineLayout(
        device_, cachedLayouts, "blit_cached_pipeline_layout");
    if (!cachedPipelineLayout_) {
        LOG_ERROR("Failed to create cached blit pipeline layout");
        return false;
    }

    std::array<WGPUColorTargetState, 2> cachedTargets{};
    cachedTargets[0].format = config.colorFormat;
    cachedTargets[0].writeMask = WGPUColorWriteMask_All;
    cachedTargets[1].format = WGPUTextureFormat_R32Float;
    cachedTargets[1].writeMask = WGPUColorWriteMask_All;
    fragmentState.targetCount = cachedTargets.size();
    fragmentState.targets = cachedTargets.data();
    WGPU_SET_ENTRY_POINT(fragmentState, "fsCachedOpaque");
    pipelineDesc.layout = cachedPipelineLayout_;
    WGPUDepthStencilState opaqueMaskState{};
    opaqueMaskState.format = WGPUTextureFormat_Depth24PlusStencil8;
    opaqueMaskState.depthWriteEnabled = gpu::toOptionalBool(false);
    opaqueMaskState.depthCompare = WGPUCompareFunction_Always;
    opaqueMaskState.stencilFront.compare = WGPUCompareFunction_Equal;
    opaqueMaskState.stencilFront.failOp = WGPUStencilOperation_Keep;
    opaqueMaskState.stencilFront.depthFailOp = WGPUStencilOperation_Keep;
    opaqueMaskState.stencilFront.passOp = WGPUStencilOperation_Keep;
    opaqueMaskState.stencilBack = opaqueMaskState.stencilFront;
    opaqueMaskState.stencilReadMask = 1u;
    opaqueMaskState.stencilWriteMask = 0u;
    pipelineDesc.depthStencil = &opaqueMaskState;
    WGPU_SET_LABEL(pipelineDesc, "blit_cached_opaque_pipeline");
    cachedPipeline_ = wgpuDeviceCreateRenderPipeline(device_, &pipelineDesc);
    if (!cachedPipeline_) {
        LOG_ERROR("Failed to create cached blit render pipeline");
        return false;
    }

    fragmentState.targetCount = 1u;
    fragmentState.targets = cachedTargets.data();
    WGPU_SET_ENTRY_POINT(fragmentState, "fsCachedOpaqueColor");
    WGPU_SET_LABEL(pipelineDesc, "blit_cached_opaque_color_pipeline");
    cachedColorPipeline_ =
        wgpuDeviceCreateRenderPipeline(device_, &pipelineDesc);
    if (!cachedColorPipeline_) {
        LOG_ERROR("Failed to create color-only cached blit pipeline");
        return false;
    }

    LOG_DEBUG("Created blit render pipeline");
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
bool BlitPath::createWaterClipmapResources(const BlitPathConfig& config) {
    const std::filesystem::path shaderPath =
        config.shaderPath.parent_path() / "water_clipmap.wgsl";
    waterClipmapShaderModule_ = gpu::loadShaderModule(
        device_, shaderPath, "water_clipmap.wgsl");
    if (!waterClipmapShaderModule_) {
        LOG_ERROR("Failed to load water clipmap shader from: {}",
                  shaderPath.string());
        return false;
    }

    WGPUVertexAttribute attribute{};
    attribute.format = WGPUVertexFormat_Float32x3;
    attribute.offset = 0u;
    attribute.shaderLocation = 0u;
    WGPUVertexBufferLayout vertexBufferLayout{};
    vertexBufferLayout.arrayStride = sizeof(detail::WaterClipmapVertex);
    vertexBufferLayout.stepMode = WGPUVertexStepMode_Vertex;
    vertexBufferLayout.attributeCount = 1u;
    vertexBufferLayout.attributes = &attribute;

    WGPUVertexState vertexState{};
    vertexState.module = waterClipmapShaderModule_;
    WGPU_SET_ENTRY_POINT(vertexState, "vs");
    vertexState.bufferCount = 1u;
    vertexState.buffers = &vertexBufferLayout;

    std::array<WGPUColorTargetState, 2> colorTargets{};
    colorTargets[0].format = config.colorFormat;
    colorTargets[0].writeMask = WGPUColorWriteMask_All;
    colorTargets[1].format = WGPUTextureFormat_R32Float;
    colorTargets[1].writeMask = WGPUColorWriteMask_All;
    WGPUFragmentState fragmentState{};
    fragmentState.module = waterClipmapShaderModule_;
    WGPU_SET_ENTRY_POINT(fragmentState, "fs");
    fragmentState.targetCount = colorTargets.size();
    fragmentState.targets = colorTargets.data();

    WGPUPrimitiveState primitiveState{};
    primitiveState.topology = WGPUPrimitiveTopology_TriangleList;
    primitiveState.frontFace = WGPUFrontFace_CCW;
    // Both faces are required when the camera crosses the live surface.
    primitiveState.cullMode = WGPUCullMode_None;
    WGPUMultisampleState multisampleState{};
    multisampleState.count = 1u;
    multisampleState.mask = ~0u;

    WGPURenderPipelineDescriptor pipelineDesc{};
    WGPU_SET_LABEL(pipelineDesc, "water_clipmap_pipeline");
    pipelineDesc.layout = cachedPipelineLayout_;
    pipelineDesc.vertex = vertexState;
    pipelineDesc.fragment = &fragmentState;
    pipelineDesc.primitive = primitiveState;
    WGPUDepthStencilState waterMaskState{};
    waterMaskState.format = WGPUTextureFormat_Depth24PlusStencil8;
    waterMaskState.depthWriteEnabled = gpu::toOptionalBool(false);
    waterMaskState.depthCompare = WGPUCompareFunction_Always;
    waterMaskState.stencilFront.compare = WGPUCompareFunction_Always;
    waterMaskState.stencilFront.failOp = WGPUStencilOperation_Keep;
    waterMaskState.stencilFront.depthFailOp = WGPUStencilOperation_Keep;
    waterMaskState.stencilFront.passOp = WGPUStencilOperation_Replace;
    waterMaskState.stencilBack = waterMaskState.stencilFront;
    waterMaskState.stencilReadMask = 1u;
    waterMaskState.stencilWriteMask = 1u;
    pipelineDesc.depthStencil = &waterMaskState;
    pipelineDesc.multisample = multisampleState;
    waterClipmapPipeline_ =
        wgpuDeviceCreateRenderPipeline(device_, &pipelineDesc);
    if (!waterClipmapPipeline_) {
        LOG_ERROR("Failed to create water clipmap render pipeline");
        return false;
    }

    fragmentState.targetCount = 1u;
    fragmentState.targets = colorTargets.data();
    WGPU_SET_ENTRY_POINT(fragmentState, "fsColor");
    WGPU_SET_LABEL(pipelineDesc, "water_clipmap_color_pipeline");
    waterClipmapColorPipeline_ =
        wgpuDeviceCreateRenderPipeline(device_, &pipelineDesc);
    if (!waterClipmapColorPipeline_) {
        LOG_ERROR("Failed to create color-only water clipmap pipeline");
        return false;
    }

    const detail::WaterClipmapMesh mesh = detail::makeWaterClipmap();
    if (mesh.vertices.empty() || mesh.indices.empty() ||
        mesh.indices.size() > std::numeric_limits<uint32_t>::max()) {
        LOG_ERROR("Generated invalid water clipmap topology");
        return false;
    }
    waterClipmapVertexBuffer_ = gpu::createBufferWithData(
        device_, queue_,
        gpu::BufferDesc::vertex(
            mesh.vertices.size() * sizeof(detail::WaterClipmapVertex),
            "water_clipmap_vertices"),
        std::span<const detail::WaterClipmapVertex>(mesh.vertices));
    waterClipmapIndexBuffer_ = gpu::createBufferWithData(
        device_, queue_,
        gpu::BufferDesc::index(
            mesh.indices.size() * sizeof(uint32_t),
            "water_clipmap_indices"),
        std::span<const uint32_t>(mesh.indices));
    if (!waterClipmapVertexBuffer_ || !waterClipmapIndexBuffer_) {
        LOG_ERROR("Failed to upload water clipmap topology");
        return false;
    }
    waterClipmapIndexCount_ = static_cast<uint32_t>(mesh.indices.size());
    LOG_INFO("Created water clipmap: {} vertices, {} triangles",
             mesh.vertices.size(), mesh.indices.size() / 3u);
    return true;
}

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

    if (!materialView_) {
        LOG_ERROR("Cannot create bind group: no material view set");
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
    if (!terrainMaterialAlbedoView_
        || !terrainMaterialNormalRoughnessView_) {
        LOG_ERROR("Cannot create bind group: terrain material arrays are missing");
        return false;
    }
    if (!surfaceFoamView_ || !surfaceFoamSampler_) {
        LOG_ERROR("Cannot create bind group: no procedural ocean foam texture");
        return false;
    }
    const bool createStaticGroups =
        staticDepthView_ && staticShadowView_ && backgroundView_;
    if (createStaticGroups
        && (!heightmapView_ || !shadowHeightView_
            || !waterDisplacementView_ || !waterDisplacementSampler_)) {
        LOG_ERROR(
            "Cannot create fused water bind group: missing geometry resources");
        return false;
    }

    WGPUBindGroup nextBindGroup = nullptr;
    WGPUBindGroup nextStaticBindGroup = nullptr;
    WGPUBindGroup nextCachedBindGroup = nullptr;
    WGPUBindGroup nextParticleBindGroup = nullptr;
    const auto cleanup = [&]() {
        if (nextParticleBindGroup) {
            wgpuBindGroupRelease(nextParticleBindGroup);
        }
        if (nextCachedBindGroup) wgpuBindGroupRelease(nextCachedBindGroup);
        if (nextStaticBindGroup) wgpuBindGroupRelease(nextStaticBindGroup);
        if (nextBindGroup) wgpuBindGroupRelease(nextBindGroup);
    };

    std::array<gpu::BindGroupEntry, 14> entries = {
        gpu::BindGroupEntry(0).buffer(uniformBuffer_, 0, sizeof(CameraUniforms)),
        gpu::BindGroupEntry(1).textureView(depthView_),
        gpu::BindGroupEntry(2).textureView(shadowView_),
        gpu::BindGroupEntry(3).textureView(materialView_),
        gpu::BindGroupEntry(4).textureView(terrainView_),
        gpu::BindGroupEntry(5).textureView(lightmapView_),
        gpu::BindGroupEntry(6).sampler(sampler_),
        gpu::BindGroupEntry(7).buffer(debugUniformBuffer_, 0, sizeof(DebugUniforms)),
        gpu::BindGroupEntry(8).textureView(skyLutView_),
        gpu::BindGroupEntry(9).textureView(surfaceFoamView_),
        gpu::BindGroupEntry(10).sampler(surfaceFoamSampler_),
        gpu::BindGroupEntry(17).textureView(terrainMaterialAlbedoView_),
        gpu::BindGroupEntry(18).textureView(
            terrainMaterialNormalRoughnessView_),
        gpu::BindGroupEntry(19).textureView(periodicGradientLut_.view())
    };
    
    nextBindGroup = gpu::createBindGroup(
        device_, bindGroupLayout_, entries, "blit_bind_group");
    
    if (!nextBindGroup) {
        LOG_ERROR("Failed to create blit bind group");
        return false;
    }

    if (particleBindGroupLayout_ && particleBuffer_) {
        const uint64_t byteSize =
            underwaterParticles_.size() * sizeof(glm::vec4);
        std::array<gpu::BindGroupEntry, 3> particleEntries = {
            gpu::BindGroupEntry(0).buffer(
                uniformBuffer_, 0, sizeof(CameraUniforms)),
            gpu::BindGroupEntry(1).textureView(depthView_),
            gpu::BindGroupEntry(2).buffer(particleBuffer_, 0, byteSize)
        };
        nextParticleBindGroup = gpu::createBindGroup(
            device_, particleBindGroupLayout_, particleEntries,
            "underwater_particle_bind_group");
        if (!nextParticleBindGroup) {
            LOG_ERROR("Failed to create underwater particle bind group");
            cleanup();
            return false;
        }
    }

    if (createStaticGroups) {
        std::array<gpu::BindGroupEntry, 14> staticEntries = {
            gpu::BindGroupEntry(0).buffer(
                staticUniformBuffer_, 0, sizeof(CameraUniforms)),
            gpu::BindGroupEntry(1).textureView(staticDepthView_),
            gpu::BindGroupEntry(2).textureView(staticShadowView_),
            gpu::BindGroupEntry(3).textureView(materialView_),
            gpu::BindGroupEntry(4).textureView(terrainView_),
            gpu::BindGroupEntry(5).textureView(lightmapView_),
            gpu::BindGroupEntry(6).sampler(sampler_),
            gpu::BindGroupEntry(7).buffer(
                debugUniformBuffer_, 0, sizeof(DebugUniforms)),
            gpu::BindGroupEntry(8).textureView(skyLutView_),
            gpu::BindGroupEntry(9).textureView(surfaceFoamView_),
            gpu::BindGroupEntry(10).sampler(surfaceFoamSampler_),
            gpu::BindGroupEntry(17).textureView(
                terrainMaterialAlbedoView_),
            gpu::BindGroupEntry(18).textureView(
                terrainMaterialNormalRoughnessView_),
            gpu::BindGroupEntry(19).textureView(periodicGradientLut_.view())
        };
        nextStaticBindGroup = gpu::createBindGroup(
            device_, bindGroupLayout_, staticEntries,
            "blit_static_bind_group");
        if (!nextStaticBindGroup) {
            LOG_ERROR("Failed to create static blit bind group");
            cleanup();
            return false;
        }

        std::array<gpu::BindGroupEntry, 20> cachedEntries = {
            gpu::BindGroupEntry(0).buffer(
                uniformBuffer_, 0, sizeof(CameraUniforms)),
            // The dynamic depth texture is a render attachment in this pass;
            // bind the static cache in unused legacy slots to avoid a WebGPU
            // read/write usage conflict.
            gpu::BindGroupEntry(1).textureView(staticDepthView_),
            gpu::BindGroupEntry(2).textureView(staticShadowView_),
            gpu::BindGroupEntry(3).textureView(materialView_),
            gpu::BindGroupEntry(4).textureView(terrainView_),
            gpu::BindGroupEntry(5).textureView(lightmapView_),
            gpu::BindGroupEntry(6).sampler(sampler_),
            gpu::BindGroupEntry(7).buffer(
                debugUniformBuffer_, 0, sizeof(DebugUniforms)),
            gpu::BindGroupEntry(8).textureView(skyLutView_),
            gpu::BindGroupEntry(9).textureView(surfaceFoamView_),
            gpu::BindGroupEntry(10).sampler(surfaceFoamSampler_),
            gpu::BindGroupEntry(11).textureView(backgroundView_),
            gpu::BindGroupEntry(12).textureView(staticDepthView_),
            gpu::BindGroupEntry(13).textureView(heightmapView_),
            gpu::BindGroupEntry(14).textureView(shadowHeightView_),
            gpu::BindGroupEntry(15).textureView(waterDisplacementView_),
            gpu::BindGroupEntry(16).sampler(waterDisplacementSampler_),
            gpu::BindGroupEntry(17).textureView(
                terrainMaterialAlbedoView_),
            gpu::BindGroupEntry(18).textureView(
                terrainMaterialNormalRoughnessView_),
            gpu::BindGroupEntry(19).textureView(periodicGradientLut_.view())
        };
        nextCachedBindGroup = gpu::createBindGroup(
            device_, cachedBindGroupLayout_, cachedEntries,
            "blit_cached_bind_group");
        if (!nextCachedBindGroup) {
            LOG_ERROR("Failed to create cached blit bind group");
            cleanup();
            return false;
        }
    }

    if (particleBindGroup_) wgpuBindGroupRelease(particleBindGroup_);
    if (cachedBindGroup_) wgpuBindGroupRelease(cachedBindGroup_);
    if (staticBindGroup_) wgpuBindGroupRelease(staticBindGroup_);
    if (bindGroup_) wgpuBindGroupRelease(bindGroup_);
    bindGroup_ = nextBindGroup;
    staticBindGroup_ = nextStaticBindGroup;
    cachedBindGroup_ = nextCachedBindGroup;
    particleBindGroup_ = nextParticleBindGroup;
    
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

void BlitPath::setMaterialTexture(WGPUTextureView materialView) {
    materialView_ = materialView;
    bindGroupDirty_ = true;
    LOG_DEBUG("Set material texture view");
}

void BlitPath::setStaticTerrainTextures(WGPUTextureView depthView,
                                        WGPUTextureView shadowView) {
    staticDepthView_ = depthView;
    staticShadowView_ = shadowView;
    bindGroupDirty_ = true;
    backgroundValid_ = false;
    backgroundDirty_ = true;
    LOG_DEBUG("Set camera-static terrain depth and shadow textures");
}

void BlitPath::setWaterCompositeResources(
    WGPUTextureView heightmapView,
    WGPUTextureView shadowHeightView,
    WGPUTextureView displacementView,
    WGPUSampler displacementSampler) {
    heightmapView_ = heightmapView;
    shadowHeightView_ = shadowHeightView;
    waterDisplacementView_ = displacementView;
    waterDisplacementSampler_ = displacementSampler;
    bindGroupDirty_ = true;
    backgroundDirty_ = true;
    LOG_DEBUG("Set fused water geometry resources");
}

void BlitPath::setStaticCacheState(bool active,
                                   bool terrainCacheRefreshed) {
    staticCacheActive_ = active;
    if (terrainCacheRefreshed) {
        backgroundDirty_ = true;
    }
}

void BlitPath::setTerrainTexture(WGPUTextureView terrainView) {
    terrainView_ = terrainView;
    bindGroupDirty_ = true;
    backgroundDirty_ = true;
    LOG_DEBUG("Set terrain texture view");
}

void BlitPath::setTerrainMaterialTextures(
    WGPUTextureView albedoView,
    WGPUTextureView normalRoughnessView) {
    terrainMaterialAlbedoView_ = albedoView;
    terrainMaterialNormalRoughnessView_ = normalRoughnessView;
    bindGroupDirty_ = true;
    backgroundDirty_ = true;
    LOG_DEBUG("Set terrain material array views");
}

void BlitPath::setLightmapTexture(WGPUTextureView lightmapView) {
    lightmapView_ = lightmapView;
    bindGroupDirty_ = true;
    backgroundDirty_ = true;
    LOG_DEBUG("Set lightmap texture view");
}

void BlitPath::setTerrainSize(uint32_t width, uint32_t height) {
    if (uniforms_) {
        CameraUniforms next = *uniforms_;
        if (!next.setTerrain(
                width, height, config_.heightScale, config_.cellScale,
                1.0f, config_.fogDensity)) {
            LOG_ERROR("BlitPath::setTerrainSize: invalid terrain size");
            return;
        }
        *uniforms_ = next;
        uniformsDirty_ = true;
        updateStaticUniforms();
    }
    terrainWidth_ = width;
    terrainHeight_ = height;
    
    LOG_DEBUG("Set terrain size: {}x{}", width, height);
}

// ─────────────────────────────────────────────────────────────────────────────
// Camera Updates
// ─────────────────────────────────────────────────────────────────────────────

void BlitPath::updateCamera(const glm::mat4& view, const glm::mat4& proj, 
                            const glm::vec3& cameraPos, float ambientIntensity) {
    if (!uniforms_) return;
    CameraUniforms next = *uniforms_;
    if (!next.setCamera(view, proj, cameraPos)) return;
    
    // Update light direction in view space (using hardcoded world direction)
    glm::vec3 worldLightDir = glm::normalize(glm::vec3(0.3f, 0.8f, 0.4f));
    if (!next.setLightDirection(worldLightDir, view, ambientIntensity)) return;
    
    *uniforms_ = next;
    uniformsDirty_ = true;
    updateStaticUniforms();
}

void BlitPath::setLegoMode(bool enabled) {
    if (uniforms_) {
        uniforms_->setLegoMode(enabled);
        uniformsDirty_ = true;
        updateStaticUniforms();
    }
}

void BlitPath::setCameraUniforms(const CameraUniforms& uniforms) {
    if (!uniforms_) return;
    if (!uniforms.isValid()) {
        LOG_ERROR("BlitPath::setCameraUniforms: invalid uniform block");
        return;
    }

    *uniforms_ = uniforms;
    uniformsDirty_ = true;
    updateStaticUniforms();
}

bool BlitPath::updateUniformBuffer() {
    if (!uniformBuffer_ || !queue_ || !uniforms_) return false;
    
    if (!gpu::writeBuffer(queue_, uniformBuffer_, 0, *uniforms_)) return false;
    uniformsDirty_ = false;
    return true;
}

void BlitPath::updateStaticUniforms() {
    if (!uniforms_ || !staticUniforms_) return;

    CameraUniforms next = *uniforms_;
    // Simulation time and the precise local wave offset change every frame,
    // but only the above/below-water transition affects the opaque backdrop.
    next.waterMotion.x = 0.0f;
    next.waterMotion.y = 0.0f;
    next.waterMotion.w = 0.0f;
    // Presentation and surface-only controls do not alter the cached opaque
    // HDR scene. Canonicalizing them keeps live material scrubbing cheap.
    next.ambientExposure.w = 0.0f;
    next.waterParams.z = 0.0f;
    next.waterParams.w = 0.0f;
    next.waterColorA = glm::vec4(0.0f);
    next.waterColorB.w = 0.0f;
    next.waterOptics.x = 0.0f;
    next.waterOptics.y = 0.0f;
    next.waterFoam = glm::vec4(0.0f);
    next.waterSpectrum = glm::vec4(0.0f);
    if (next.waterMotion.z <= 0.5f) {
        // Sea level and the enabled flag also classify the cached shoreline
        // terrain (sand, run-up wetness, soil, and grass). Keep x/y stable;
        // z/w were canonicalized above because only live water consumes them.
        next.waterColorB = glm::vec4(0.0f);
        next.waterOptics = glm::vec4(0.0f);
    }
    if (std::memcmp(staticUniforms_, &next, sizeof(CameraUniforms)) == 0) {
        return;
    }

    *staticUniforms_ = next;
    staticUniformsDirty_ = true;
    backgroundDirty_ = true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Rendering
// ─────────────────────────────────────────────────────────────────────────────

void BlitPath::render(WGPUCommandEncoder encoder, WGPUTextureView colorView,
                      WGPUQuerySet timestampQuerySet,
                      uint32_t timestampBegin,
                      uint32_t timestampEnd) {
    usedGeometryWaterPathLastRender_ = false;
    if (!pipeline_) {
        LOG_WARN("BlitPath::render: not initialized");
        return;
    }
    if (!encoder || !colorView) {
        LOG_ERROR("BlitPath::render: invalid encoder or color view");
        return;
    }
    
    if (!depthView_ || !shadowView_ || !materialView_ || !terrainView_ || !lightmapView_) {
        LOG_WARN("BlitPath::render: missing required texture bindings");
        return;
    }
    
    // Update uniform buffer if dirty
    if (uniformsDirty_ && !updateUniformBuffer()) {
        return;
    }
    
    // Update debug uniform buffer if dirty
    if (debugUniformsDirty_ && debugUniformBuffer_) {
        DebugUniforms debugUniforms;
        debugUniforms.mode = debugMode_;
        debugUniforms.maxDepth = debugMaxDepth_;
        if (!gpu::writeBuffer(
                queue_, debugUniformBuffer_, 0, debugUniforms)) {
            return;
        }
        debugUniformsDirty_ = false;
    }
    
    // Create bind group if dirty
    if (bindGroupDirty_ || !bindGroup_) {
        if (!createBindGroup()) {
            LOG_ERROR("Failed to create bind group during render");
            return;
        }
    }

    // Bake the static sky once, on the first frame (uniforms are current by
    // now, unlike at init time).
    if (!skyLutBaked_ && skyLutPipeline_ && skyLutBindGroup_) {
        WGPUComputePassDescriptor computePassDesc{};
        WGPU_SET_LABEL(computePassDesc, "sky_lut_bake_pass");
        WGPUComputePassEncoder computePass =
            wgpuCommandEncoderBeginComputePass(encoder, &computePassDesc);
        if (!computePass) {
            LOG_ERROR("BlitPath::render: failed to begin sky LUT pass");
            return;
        }
        wgpuComputePassEncoderSetPipeline(computePass, skyLutPipeline_);
        wgpuComputePassEncoderSetBindGroup(computePass, 0, skyLutBindGroup_, 0, nullptr);
        wgpuComputePassEncoderDispatchWorkgroups(
            computePass, (kSkyLutWidth + 7) / 8,
            (kSkyLutHeight + 7) / 8, 1);
        wgpuComputePassEncoderEnd(computePass);
        wgpuComputePassEncoderRelease(computePass);

        // One pass per level gives each read-after-write dependency its own
        // WebGPU usage scope. This executes only once, during the first frame.
        if (skyLutMipPipeline_ &&
            skyLutMipBindGroups_.size() == kSkyLutMipCount - 1) {
            for (uint32_t level = 1; level < kSkyLutMipCount; ++level) {
                WGPUComputePassDescriptor mipPassDesc{};
                WGPU_SET_LABEL(mipPassDesc, "sky_lut_mip_pass");
                WGPUComputePassEncoder mipPass =
                    wgpuCommandEncoderBeginComputePass(encoder, &mipPassDesc);
                if (!mipPass) {
                    LOG_ERROR("BlitPath::render: failed to begin sky LUT mip pass");
                    return;
                }
                wgpuComputePassEncoderSetPipeline(mipPass,
                                                  skyLutMipPipeline_);
                wgpuComputePassEncoderSetBindGroup(
                    mipPass, 0, skyLutMipBindGroups_[level - 1], 0, nullptr);
                const uint32_t mipWidth =
                    std::max(kSkyLutWidth >> level, 1u);
                const uint32_t mipHeight =
                    std::max(kSkyLutHeight >> level, 1u);
                wgpuComputePassEncoderDispatchWorkgroups(
                    mipPass, (mipWidth + 7) / 8,
                    (mipHeight + 7) / 8, 1);
                wgpuComputePassEncoderEnd(mipPass);
                wgpuComputePassEncoderRelease(mipPass);
            }
        }
        skyLutBaked_ = true;
        LOG_DEBUG("Baked sky LUT and roughness mip chain");
    }

    const auto drawFullscreen = [&](WGPUTextureView target,
                                    WGPURenderPipeline selectedPipeline,
                                    WGPUBindGroup selectedBindGroup,
                                    const char* label,
                                    bool writeTimestamps,
                                    bool writeLinearDepth,
                                    bool beginTimestampOnly = false,
                                    bool deferTimestampEnd = false) -> bool {
        std::array<WGPURenderPassColorAttachment, 2> colorAttachments{};
        colorAttachments[0].view = target;
        colorAttachments[0].depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
        colorAttachments[0].loadOp = WGPULoadOp_Clear;
        colorAttachments[0].storeOp = WGPUStoreOp_Store;
        colorAttachments[0].clearValue = {0.0, 0.0, 0.0, 1.0};
        if (writeLinearDepth) {
            colorAttachments[1].view = depthView_;
            colorAttachments[1].depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
            colorAttachments[1].loadOp = WGPULoadOp_Clear;
            colorAttachments[1].storeOp = WGPUStoreOp_Store;
            colorAttachments[1].clearValue = {-1.0, 0.0, 0.0, 0.0};
        }

        WGPURenderPassDescriptor renderPassDesc{};
        WGPU_SET_LABEL(renderPassDesc, label);
        renderPassDesc.colorAttachmentCount = writeLinearDepth ? 2u : 1u;
        renderPassDesc.colorAttachments = colorAttachments.data();
        gpu::CompatRenderPassTimestampWrites timestampWrites{};
        if (writeTimestamps && timestampQuerySet) {
            timestampWrites.querySet = timestampQuerySet;
            timestampWrites.beginningOfPassWriteIndex = timestampBegin;
            timestampWrites.endOfPassWriteIndex =
                (beginTimestampOnly || deferTimestampEnd)
                    ? WGPU_QUERY_SET_INDEX_UNDEFINED : timestampEnd;
            renderPassDesc.timestampWrites = &timestampWrites;
        }

        WGPURenderPassEncoder renderPass =
            wgpuCommandEncoderBeginRenderPass(encoder, &renderPassDesc);
        if (!renderPass) {
            LOG_ERROR("BlitPath::render: failed to begin '{}'", label);
            return false;
        }
        wgpuRenderPassEncoderSetPipeline(renderPass, selectedPipeline);
        wgpuRenderPassEncoderSetBindGroup(
            renderPass, 0, selectedBindGroup, 0, nullptr);
        wgpuRenderPassEncoderDraw(renderPass, 3, 1, 0, 0);
        wgpuRenderPassEncoderEnd(renderPass);
        wgpuRenderPassEncoderRelease(renderPass);
        return true;
    };

    const bool useCachedPath = staticCacheActive_ && backgroundView_ &&
        coverageMaskView_ &&
        staticBindGroup_ && cachedBindGroup_ && cachedPipeline_ &&
        cachedColorPipeline_ &&
        backgroundPipeline_ && waterClipmapPipeline_ &&
        waterClipmapColorPipeline_ &&
        waterClipmapVertexBuffer_ && waterClipmapIndexBuffer_ &&
        waterClipmapIndexCount_ != 0u;
    const bool cameraUnderwater = uniforms_->waterParams.y > 0.5f &&
                                  uniforms_->waterMotion.z > 0.5f;
    const bool drawParticles =
        cameraUnderwater && particlePipeline_ && particleBindGroup_ &&
        updateUnderwaterParticles();
    // Cover every recurring blit pass with one interval. The beginning and
    // end query indices must each be written once, even on a cache hit.
    bool lightingTimestampStarted = false;
    if (useCachedPath && (!backgroundValid_ || backgroundDirty_)) {
        if (staticUniformsDirty_) {
            if (!gpu::writeBuffer(
                    queue_, staticUniformBuffer_, 0, *staticUniforms_)) {
                return;
            }
            staticUniformsDirty_ = false;
        }
        if (!drawFullscreen(backgroundView_, backgroundPipeline_,
                            staticBindGroup_,
                            "blit_static_background_pass", true, false, true)) {
            return;
        }
        lightingTimestampStarted = timestampQuerySet != nullptr;
        backgroundValid_ = true;
        backgroundDirty_ = false;
    }

    const bool preserveLinearDepth =
        linearDepthRequired_ || cameraUnderwater;
    if (useCachedPath && backgroundValid_) {
        std::array<WGPURenderPassColorAttachment, 2> colorAttachments{};
        colorAttachments[0].view = colorView;
        colorAttachments[0].depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
        colorAttachments[0].loadOp = WGPULoadOp_Clear;
        colorAttachments[0].storeOp = WGPUStoreOp_Store;
        colorAttachments[0].clearValue = {0.0, 0.0, 0.0, 1.0};
        colorAttachments[1].view = depthView_;
        colorAttachments[1].depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
        colorAttachments[1].loadOp = WGPULoadOp_Clear;
        colorAttachments[1].storeOp = WGPUStoreOp_Store;
        colorAttachments[1].clearValue = {-1.0, 0.0, 0.0, 0.0};

        WGPURenderPassDescriptor renderPassDesc{};
        WGPU_SET_LABEL(renderPassDesc, "blit_water_clipmap_pass");
        renderPassDesc.colorAttachmentCount =
            preserveLinearDepth ? colorAttachments.size() : 1u;
        renderPassDesc.colorAttachments = colorAttachments.data();
        WGPURenderPassDepthStencilAttachment maskAttachment{};
        maskAttachment.view = coverageMaskView_;
        maskAttachment.depthLoadOp = WGPULoadOp_Clear;
        maskAttachment.depthStoreOp = WGPUStoreOp_Discard;
        maskAttachment.depthClearValue = 1.0f;
        maskAttachment.stencilLoadOp = WGPULoadOp_Clear;
        maskAttachment.stencilStoreOp = WGPUStoreOp_Discard;
        maskAttachment.stencilClearValue = 0u;
        maskAttachment.depthReadOnly = false;
        maskAttachment.stencilReadOnly = false;
        renderPassDesc.depthStencilAttachment = &maskAttachment;
        gpu::CompatRenderPassTimestampWrites timestampWrites{};
        // The middle pass has no endpoints when background and particles
        // bracket it. WebGPU requires at least one defined endpoint.
        if (timestampQuerySet && (!lightingTimestampStarted || !drawParticles)) {
            timestampWrites.querySet = timestampQuerySet;
            timestampWrites.beginningOfPassWriteIndex = lightingTimestampStarted
                ? WGPU_QUERY_SET_INDEX_UNDEFINED : timestampBegin;
            timestampWrites.endOfPassWriteIndex = drawParticles
                ? WGPU_QUERY_SET_INDEX_UNDEFINED : timestampEnd;
            renderPassDesc.timestampWrites = &timestampWrites;
        }

        WGPURenderPassEncoder renderPass =
            wgpuCommandEncoderBeginRenderPass(encoder, &renderPassDesc);
        if (!renderPass) {
            LOG_ERROR("BlitPath::render: failed to begin water clipmap pass");
            return;
        }
        usedGeometryWaterPathLastRender_ = true;
        if (uniforms_->waterParams.y > 0.5f) {
            wgpuRenderPassEncoderSetStencilReference(renderPass, 1u);
            wgpuRenderPassEncoderSetPipeline(
                renderPass, preserveLinearDepth
                    ? waterClipmapPipeline_
                    : waterClipmapColorPipeline_);
            wgpuRenderPassEncoderSetBindGroup(
                renderPass, 0, cachedBindGroup_, 0, nullptr);
            wgpuRenderPassEncoderSetVertexBuffer(
                renderPass, 0, waterClipmapVertexBuffer_, 0,
                WGPU_WHOLE_SIZE);
            wgpuRenderPassEncoderSetIndexBuffer(
                renderPass, waterClipmapIndexBuffer_,
                WGPUIndexFormat_Uint32, 0, WGPU_WHOLE_SIZE);
            wgpuRenderPassEncoderDrawIndexed(
                renderPass, waterClipmapIndexCount_, 1, 0, 0, 0);
        }
        // Fill only pixels which the water pass did not cover. The stencil
        // test rejects them before the cached opaque fragment shader, avoiding
        // a complete second presentation transform underneath the ocean.
        wgpuRenderPassEncoderSetStencilReference(renderPass, 0u);
        wgpuRenderPassEncoderSetPipeline(
            renderPass, preserveLinearDepth
                ? cachedPipeline_
                : cachedColorPipeline_);
        wgpuRenderPassEncoderSetBindGroup(
            renderPass, 0, cachedBindGroup_, 0, nullptr);
        wgpuRenderPassEncoderDraw(renderPass, 3, 1, 0, 0);
        wgpuRenderPassEncoderEnd(renderPass);
        wgpuRenderPassEncoderRelease(renderPass);
    } else {
        if (!drawFullscreen(colorView, pipeline_, bindGroup_,
                            "blit_render_pass", true, false, false,
                            drawParticles)) {
            return;
        }
    }

    if (drawParticles) {

        WGPURenderPassColorAttachment colorAttachment{};
        colorAttachment.view = colorView;
        colorAttachment.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
        colorAttachment.loadOp = WGPULoadOp_Load;
        colorAttachment.storeOp = WGPUStoreOp_Store;

        WGPURenderPassDescriptor passDescriptor{};
        WGPU_SET_LABEL(passDescriptor, "underwater_particle_pass");
        passDescriptor.colorAttachmentCount = 1;
        passDescriptor.colorAttachments = &colorAttachment;
        gpu::CompatRenderPassTimestampWrites particleTimestamps{};
        if (timestampQuerySet) {
            particleTimestamps.querySet = timestampQuerySet;
            particleTimestamps.beginningOfPassWriteIndex =
                WGPU_QUERY_SET_INDEX_UNDEFINED;
            particleTimestamps.endOfPassWriteIndex = timestampEnd;
            passDescriptor.timestampWrites = &particleTimestamps;
        }
        WGPURenderPassEncoder pass =
            wgpuCommandEncoderBeginRenderPass(encoder, &passDescriptor);
        if (!pass) {
            LOG_ERROR("BlitPath::render: failed to begin underwater particle pass");
            return;
        }
        wgpuRenderPassEncoderSetPipeline(pass, particlePipeline_);
        wgpuRenderPassEncoderSetBindGroup(
            pass, 0, particleBindGroup_, 0, nullptr);
        wgpuRenderPassEncoderDraw(
            pass, 6, kUnderwaterParticleCount, 0, 0);
        wgpuRenderPassEncoderEnd(pass);
        wgpuRenderPassEncoderRelease(pass);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Debug Visualization
// ─────────────────────────────────────────────────────────────────────────────

void BlitPath::setDebugMode(uint32_t mode) {
    if (mode > 3u) {
        LOG_ERROR("BlitPath::setDebugMode: invalid mode {}", mode);
        return;
    }
    if (debugMode_ != mode) {
        debugMode_ = mode;
        debugUniformsDirty_ = true;
        backgroundDirty_ = true;
    }
}

void BlitPath::setDebugMaxDepth(float maxDepth) {
    if (!std::isfinite(maxDepth) || maxDepth <= 0.0f) {
        LOG_ERROR("BlitPath::setDebugMaxDepth: invalid depth");
        return;
    }
    if (debugMaxDepth_ != maxDepth) {
        debugMaxDepth_ = maxDepth;
        debugUniformsDirty_ = true;
        backgroundDirty_ = true;
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
