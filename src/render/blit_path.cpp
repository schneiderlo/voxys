// ═══════════════════════════════════════════════════════════════════════════════
// blit_path.cpp - Fullscreen Blit/Lighting Rendering Path Implementation
// ═══════════════════════════════════════════════════════════════════════════════

#include "render/blit_path.hpp"
#include "render/triangle_path.hpp"  // For CameraUniforms
#include "gpu/resources.hpp"
#include "gpu/webgpu_compat.hpp"
#include "core/log.hpp"

#include <glm/gtc/matrix_inverse.hpp>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <span>
#include <vector>

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
    , cachedPipelineLayout_(other.cachedPipelineLayout_)
    , cachedPipeline_(other.cachedPipeline_)
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
    , backgroundTexture_(other.backgroundTexture_)
    , backgroundView_(other.backgroundView_)
    , outputWidth_(other.outputWidth_)
    , outputHeight_(other.outputHeight_)
    , depthView_(other.depthView_)
    , shadowView_(other.shadowView_)
    , materialView_(other.materialView_)
    , staticDepthView_(other.staticDepthView_)
    , staticShadowView_(other.staticShadowView_)
    , terrainView_(other.terrainView_)
    , lightmapView_(other.lightmapView_)
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
    other.cachedPipelineLayout_ = nullptr;
    other.cachedPipeline_ = nullptr;
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
    other.backgroundTexture_ = nullptr;
    other.backgroundView_ = nullptr;
    other.depthView_ = nullptr;
    other.shadowView_ = nullptr;
    other.materialView_ = nullptr;
    other.staticDepthView_ = nullptr;
    other.staticShadowView_ = nullptr;
    other.terrainView_ = nullptr;
    other.lightmapView_ = nullptr;
    other.uniforms_ = nullptr;
    other.staticUniforms_ = nullptr;
}

BlitPath& BlitPath::operator=(BlitPath&& other) noexcept {
    if (this != &other) {
        shutdown();
        
        device_ = other.device_;
        queue_ = other.queue_;
        shaderModule_ = other.shaderModule_;
        pipelineLayout_ = other.pipelineLayout_;
        pipeline_ = other.pipeline_;
        cachedPipelineLayout_ = other.cachedPipelineLayout_;
        cachedPipeline_ = other.cachedPipeline_;
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
        backgroundTexture_ = other.backgroundTexture_;
        backgroundView_ = other.backgroundView_;
        outputWidth_ = other.outputWidth_;
        outputHeight_ = other.outputHeight_;
        depthView_ = other.depthView_;
        shadowView_ = other.shadowView_;
        materialView_ = other.materialView_;
        staticDepthView_ = other.staticDepthView_;
        staticShadowView_ = other.staticShadowView_;
        terrainView_ = other.terrainView_;
        lightmapView_ = other.lightmapView_;
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
        debugMode_ = other.debugMode_;
        debugMaxDepth_ = other.debugMaxDepth_;
        debugUniformsDirty_ = other.debugUniformsDirty_;
        
        other.device_ = nullptr;
        other.queue_ = nullptr;
        other.shaderModule_ = nullptr;
        other.pipelineLayout_ = nullptr;
        other.pipeline_ = nullptr;
        other.cachedPipelineLayout_ = nullptr;
        other.cachedPipeline_ = nullptr;
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
        other.backgroundTexture_ = nullptr;
        other.backgroundView_ = nullptr;
        other.depthView_ = nullptr;
        other.shadowView_ = nullptr;
        other.materialView_ = nullptr;
        other.staticDepthView_ = nullptr;
        other.staticShadowView_ = nullptr;
        other.terrainView_ = nullptr;
        other.lightmapView_ = nullptr;
        other.uniforms_ = nullptr;
        other.staticUniforms_ = nullptr;
    }
    return *this;
}

void BlitPath::shutdown() {
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
    if (cachedPipelineLayout_) {
        wgpuPipelineLayoutRelease(cachedPipelineLayout_);
        cachedPipelineLayout_ = nullptr;
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
    terrainView_ = nullptr;
    lightmapView_ = nullptr;
    device_ = nullptr;
    queue_ = nullptr;
    outputWidth_ = 0;
    outputHeight_ = 0;
    staticCacheActive_ = false;
    backgroundValid_ = false;
    backgroundDirty_ = true;
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
    staticUniforms_ = new CameraUniforms(*uniforms_);
    updateStaticUniforms();
    
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

    LOG_INFO("BlitPath initialized successfully");
    return true;
}

bool BlitPath::resize(uint32_t width, uint32_t height) {
    if (!device_ || width == 0 || height == 0) {
        LOG_ERROR("BlitPath::resize: invalid device or dimensions");
        return false;
    }
    if (width == outputWidth_ && height == outputHeight_ && backgroundView_) {
        return true;
    }

    // These groups retain the old framebuffer-sized cache views.
    if (staticBindGroup_) {
        wgpuBindGroupRelease(staticBindGroup_);
        staticBindGroup_ = nullptr;
    }
    if (cachedBindGroup_) {
        wgpuBindGroupRelease(cachedBindGroup_);
        cachedBindGroup_ = nullptr;
    }
    if (backgroundView_) {
        wgpuTextureViewRelease(backgroundView_);
        backgroundView_ = nullptr;
    }
    if (backgroundTexture_) {
        wgpuTextureRelease(backgroundTexture_);
        backgroundTexture_ = nullptr;
    }

    outputWidth_ = width;
    outputHeight_ = height;
    backgroundValid_ = false;
    backgroundDirty_ = true;
    bindGroupDirty_ = true;
    return createBackgroundTexture();
}

bool BlitPath::createBackgroundTexture() {
    gpu::TextureDesc desc = gpu::TextureDesc::renderTarget(
        outputWidth_, outputHeight_, config_.colorFormat,
        "blit_static_background");
    backgroundTexture_ = gpu::createTexture(device_, desc);
    if (!backgroundTexture_) {
        LOG_ERROR("Failed to create static background texture");
        return false;
    }

    gpu::TextureViewDesc viewDesc{};
    viewDesc.label = "blit_static_background_view";
    viewDesc.format = config_.colorFormat;
    backgroundView_ = gpu::createTextureView(backgroundTexture_, viewDesc);
    if (!backgroundView_) {
        LOG_ERROR("Failed to create static background texture view");
        return false;
    }

    LOG_DEBUG("Created static background cache: {}x{}", outputWidth_,
              outputHeight_);
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

[[nodiscard]] uint8_t proceduralFoamTexel(uint32_t x, uint32_t y) noexcept {
    const float u = (static_cast<float>(x) + 0.5f) /
                    static_cast<float>(kSurfaceFoamSize);
    const float v = (static_cast<float>(y) + 0.5f) /
                    static_cast<float>(kSurfaceFoamSize);

    // A seamless vector warp prevents the iso-lines below from exposing their
    // underlying noise lattice. Integer periods keep both tile edges exact.
    const float warpX = periodicGradientNoise(u, v, 3, 0x37d4f12bu) +
        0.35f * periodicGradientNoise(u, v, 7, 0x7f4a7c15u);
    const float warpY = periodicGradientNoise(u, v, 3, 0xb49a85d1u) +
        0.35f * periodicGradientNoise(u, v, 7, 0x94d049bbu);
    const float warpedU = u + warpX * 0.085f;
    const float warpedV = v + warpY * 0.085f;

    const float broadField =
        periodicGradientNoise(warpedU, warpedV, 7, 0x6c8e9cf5u) * 0.64f +
        periodicGradientNoise(warpedU, warpedV, 14, 0x1f123bb5u) * 0.25f +
        periodicGradientNoise(warpedU, warpedV, 28, 0xc2b2ae35u) * 0.11f;
    const float broadRidge = 1.0f - foamSmoothstep(
        0.008f, 0.070f, std::abs(broadField));

    // An integer torus transform changes orientation without breaking tiling.
    const float detailU = warpedU + warpedV;
    const float detailV = -warpedU + 2.0f * warpedV;
    const float detailField =
        periodicGradientNoise(detailU, detailV, 13, 0x85ebca6bu) * 0.72f +
        periodicGradientNoise(detailU, detailV, 26, 0x27d4eb2fu) * 0.28f;
    const float detailRidge = 1.0f - foamSmoothstep(
        0.006f, 0.045f, std::abs(detailField));

    // Independent fractal fields break the contours into foam fragments and
    // vary their width. Only the bright cores survive the shader threshold.
    const float breakup = periodicFbm(u, v, 4, 0x165667b1u);
    const float detailBreakup = periodicFbm(
        u + v, -u + 2.0f * v, 6, 0xd3a2646cu);
    const float broadStrands = broadRidge *
        (0.30f + 0.82f * foamSmoothstep(0.40f, 0.68f, breakup));
    const float detailStrands = detailRidge *
        (0.26f + 0.78f * foamSmoothstep(
            0.44f, 0.72f, detailBreakup));
    float mask = std::max(broadStrands, detailStrands * 0.90f);
    mask = foamSmoothstep(0.18f, 0.98f, mask);
    return static_cast<uint8_t>(
        std::lround(std::clamp(mask, 0.0f, 1.0f) * 255.0f));
}

} // namespace

bool BlitPath::createSurfaceFoamTexture() {
    uint32_t width = kSurfaceFoamSize;
    uint32_t height = kSurfaceFoamSize;
    std::vector<uint8_t> mip(static_cast<size_t>(width) * height);
    for (uint32_t y = 0; y < height; ++y) {
        for (uint32_t x = 0; x < width; ++x) {
            mip[static_cast<size_t>(y) * width + x] =
                proceduralFoamTexel(x, y);
        }
    }

    gpu::TextureDesc desc = gpu::TextureDesc::tex2DMipmapped(
        width, height, WGPUTextureFormat_R8Unorm,
        WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst,
        "ocean_procedural_foam");
    surfaceFoamTexture_ = gpu::createTexture(device_, desc);
    if (!surfaceFoamTexture_) return false;

    for (uint32_t level = 0; level < desc.mipLevelCount; ++level) {
        gpu::writeTexture(queue_, surfaceFoamTexture_,
                          std::as_bytes(std::span<const uint8_t>(mip)),
                          width, height, width, level);
        if (width == 1 && height == 1) break;

        const uint32_t nextWidth = std::max(width / 2, 1u);
        const uint32_t nextHeight = std::max(height / 2, 1u);
        std::vector<uint8_t> next(
            static_cast<size_t>(nextWidth) * nextHeight);
        for (uint32_t y = 0; y < nextHeight; ++y) {
            for (uint32_t x = 0; x < nextWidth; ++x) {
                const uint32_t x0 = std::min(x * 2, width - 1);
                const uint32_t x1 = std::min(x0 + 1, width - 1);
                const uint32_t y0 = std::min(y * 2, height - 1);
                const uint32_t y1 = std::min(y0 + 1, height - 1);
                const uint32_t sum =
                    static_cast<uint32_t>(
                        mip[static_cast<size_t>(y0) * width + x0]) +
                    static_cast<uint32_t>(
                        mip[static_cast<size_t>(y0) * width + x1]) +
                    static_cast<uint32_t>(
                        mip[static_cast<size_t>(y1) * width + x0]) +
                    static_cast<uint32_t>(
                        mip[static_cast<size_t>(y1) * width + x1]);
                next[static_cast<size_t>(y) * nextWidth + x] =
                    static_cast<uint8_t>((sum + 2u) / 4u);
            }
        }
        mip = std::move(next);
        width = nextWidth;
        height = nextHeight;
    }

    gpu::TextureViewDesc viewDesc{};
    viewDesc.label = "ocean_procedural_foam_view";
    viewDesc.format = WGPUTextureFormat_R8Unorm;
    viewDesc.mipLevelCount = desc.mipLevelCount;
    surfaceFoamView_ = gpu::createTextureView(surfaceFoamTexture_, viewDesc);
    if (!surfaceFoamView_) return false;

    gpu::SamplerDesc samplerDesc =
        gpu::SamplerDesc::linear("ocean_procedural_foam_sampler");
    samplerDesc.addressModeU = WGPUAddressMode_Repeat;
    samplerDesc.addressModeV = WGPUAddressMode_Repeat;
    surfaceFoamSampler_ = gpu::createSampler(device_, samplerDesc);
    if (!surfaceFoamSampler_) return false;

    LOG_DEBUG("Generated procedural ocean foam ({}x{}, {} mips)",
              kSurfaceFoamSize, kSurfaceFoamSize, desc.mipLevelCount);
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Sky LUT Creation
// ─────────────────────────────────────────────────────────────────────────────

namespace {
/// Resolution of the baked paraboloid sky map. The sky is low-frequency
/// (the sharp sun disc stays analytic in the blit shader), so 512 is plenty.
constexpr uint32_t kSkyLutSize = 512;
constexpr uint32_t kSkyLutMipCount =
    gpu::calculateMipLevelCount(kSkyLutSize, kSkyLutSize);
} // namespace

bool BlitPath::createSkyLut(const BlitPathConfig& config) {
    // Output texture: storage write for the bake, sampled read for the blit.
    gpu::TextureDesc lutDesc = gpu::TextureDesc::storage(
        kSkyLutSize, kSkyLutSize, WGPUTextureFormat_RGBA16Float, "sky_lut");
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
              kSkyLutSize, kSkyLutSize, kSkyLutMipCount);
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
    updateUniformBuffer();
    gpu::writeBuffer(queue_, staticUniformBuffer_, 0, *staticUniforms_);
    staticUniformsDirty_ = false;
    
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
    // @group(0) @binding(3) var materialTex : texture_2d<f32>;
    // @group(0) @binding(4) var terrainTex : texture_2d<f32>;
    // @group(0) @binding(5) var lightmapTex : texture_2d<f32>;
    // @group(0) @binding(6) var terrainSampler : sampler;
    // @group(0) @binding(7) var<uniform> debug : DebugUniforms;
    // @group(0) @binding(8) var skyLUT : texture_2d<f32>;
    // @group(0) @binding(9) var oceanFoamTex : texture_2d<f32>;
    // @group(0) @binding(10) var oceanFoamSampler : sampler;

    std::array<gpu::BindGroupLayoutEntry, 11> entries = {
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
            .sampler(WGPUSamplerBindingType_Filtering)
    };
    
    bindGroupLayout_ = gpu::createBindGroupLayout(device_, entries, "blit_bind_group_layout");
    
    if (!bindGroupLayout_) {
        LOG_ERROR("Failed to create bind group layout");
        return false;
    }

    // The settled-camera pipeline has one extra input: the exact terrain/sky
    // color rendered when the static ray cache was refreshed.
    std::array<gpu::BindGroupLayoutEntry, 12> cachedEntries = {
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

    std::array<WGPUBindGroupLayout, 1> cachedLayouts = {
        cachedBindGroupLayout_
    };
    cachedPipelineLayout_ = gpu::createPipelineLayout(
        device_, cachedLayouts, "blit_cached_pipeline_layout");
    if (!cachedPipelineLayout_) {
        LOG_ERROR("Failed to create cached blit pipeline layout");
        return false;
    }

    WGPU_SET_ENTRY_POINT(fragmentState, "fsCached");
    pipelineDesc.layout = cachedPipelineLayout_;
    WGPU_SET_LABEL(pipelineDesc, "blit_cached_pipeline");
    cachedPipeline_ = wgpuDeviceCreateRenderPipeline(device_, &pipelineDesc);
    if (!cachedPipeline_) {
        LOG_ERROR("Failed to create cached blit render pipeline");
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
    if (!surfaceFoamView_ || !surfaceFoamSampler_) {
        LOG_ERROR("Cannot create bind group: no procedural ocean foam texture");
        return false;
    }
    
    // Release old bind group if exists
    if (bindGroup_) {
        wgpuBindGroupRelease(bindGroup_);
        bindGroup_ = nullptr;
    }
    if (staticBindGroup_) {
        wgpuBindGroupRelease(staticBindGroup_);
        staticBindGroup_ = nullptr;
    }
    if (cachedBindGroup_) {
        wgpuBindGroupRelease(cachedBindGroup_);
        cachedBindGroup_ = nullptr;
    }
    std::array<gpu::BindGroupEntry, 11> entries = {
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
        gpu::BindGroupEntry(10).sampler(surfaceFoamSampler_)
    };
    
    bindGroup_ = gpu::createBindGroup(device_, bindGroupLayout_, entries, "blit_bind_group");
    
    if (!bindGroup_) {
        LOG_ERROR("Failed to create blit bind group");
        return false;
    }

    if (staticDepthView_ && staticShadowView_ && backgroundView_) {
        std::array<gpu::BindGroupEntry, 11> staticEntries = {
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
            gpu::BindGroupEntry(10).sampler(surfaceFoamSampler_)
        };
        staticBindGroup_ = gpu::createBindGroup(
            device_, bindGroupLayout_, staticEntries,
            "blit_static_bind_group");
        if (!staticBindGroup_) {
            LOG_ERROR("Failed to create static blit bind group");
            return false;
        }

        std::array<gpu::BindGroupEntry, 12> cachedEntries = {
            gpu::BindGroupEntry(0).buffer(
                uniformBuffer_, 0, sizeof(CameraUniforms)),
            gpu::BindGroupEntry(1).textureView(depthView_),
            gpu::BindGroupEntry(2).textureView(shadowView_),
            gpu::BindGroupEntry(3).textureView(materialView_),
            gpu::BindGroupEntry(4).textureView(terrainView_),
            gpu::BindGroupEntry(5).textureView(lightmapView_),
            gpu::BindGroupEntry(6).sampler(sampler_),
            gpu::BindGroupEntry(7).buffer(
                debugUniformBuffer_, 0, sizeof(DebugUniforms)),
            gpu::BindGroupEntry(8).textureView(skyLutView_),
            gpu::BindGroupEntry(9).textureView(surfaceFoamView_),
            gpu::BindGroupEntry(10).sampler(surfaceFoamSampler_),
            gpu::BindGroupEntry(11).textureView(backgroundView_)
        };
        cachedBindGroup_ = gpu::createBindGroup(
            device_, cachedBindGroupLayout_, cachedEntries,
            "blit_cached_bind_group");
        if (!cachedBindGroup_) {
            LOG_ERROR("Failed to create cached blit bind group");
            return false;
        }

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

void BlitPath::setLightmapTexture(WGPUTextureView lightmapView) {
    lightmapView_ = lightmapView;
    bindGroupDirty_ = true;
    backgroundDirty_ = true;
    LOG_DEBUG("Set lightmap texture view");
}

void BlitPath::setTerrainSize(uint32_t width, uint32_t height) {
    terrainWidth_ = width;
    terrainHeight_ = height;
    
    if (uniforms_) {
        uniforms_->setTerrain(width, height, config_.heightScale, config_.cellScale,
                              1.0f, config_.fogDensity);
        uniformsDirty_ = true;
        updateStaticUniforms();
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

    *uniforms_ = uniforms;
    uniformsDirty_ = true;
    updateStaticUniforms();
}

void BlitPath::updateUniformBuffer() {
    if (!uniformBuffer_ || !queue_ || !uniforms_) return;
    
    gpu::writeBuffer(queue_, uniformBuffer_, 0, *uniforms_);
    uniformsDirty_ = false;
}

void BlitPath::updateStaticUniforms() {
    if (!uniforms_ || !staticUniforms_) return;

    CameraUniforms next = *uniforms_;
    // Simulation time changes every frame but cannot affect static terrain or
    // sky. All other fields remain exact so lighting/config edits invalidate.
    next.waterMotion = glm::vec4(0.0f);
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
    if (!pipeline_) {
        LOG_WARN("BlitPath::render: not initialized");
        return;
    }
    
    if (!depthView_ || !shadowView_ || !materialView_ || !terrainView_ || !lightmapView_) {
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

    // Bake the static sky once, on the first frame (uniforms are current by
    // now, unlike at init time).
    if (!skyLutBaked_ && skyLutPipeline_ && skyLutBindGroup_) {
        WGPUComputePassDescriptor computePassDesc{};
        WGPU_SET_LABEL(computePassDesc, "sky_lut_bake_pass");
        WGPUComputePassEncoder computePass =
            wgpuCommandEncoderBeginComputePass(encoder, &computePassDesc);
        wgpuComputePassEncoderSetPipeline(computePass, skyLutPipeline_);
        wgpuComputePassEncoderSetBindGroup(computePass, 0, skyLutBindGroup_, 0, nullptr);
        const uint32_t groups = (kSkyLutSize + 7) / 8;
        wgpuComputePassEncoderDispatchWorkgroups(computePass, groups, groups, 1);
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
                wgpuComputePassEncoderSetPipeline(mipPass,
                                                  skyLutMipPipeline_);
                wgpuComputePassEncoderSetBindGroup(
                    mipPass, 0, skyLutMipBindGroups_[level - 1], 0, nullptr);
                const uint32_t mipSize =
                    std::max(kSkyLutSize >> level, 1u);
                wgpuComputePassEncoderDispatchWorkgroups(
                    mipPass, (mipSize + 7) / 8, (mipSize + 7) / 8, 1);
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
                                    bool writeTimestamps) {
        WGPURenderPassColorAttachment colorAttachment{};
        colorAttachment.view = target;
        colorAttachment.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
        colorAttachment.loadOp = WGPULoadOp_Clear;
        colorAttachment.storeOp = WGPUStoreOp_Store;
        colorAttachment.clearValue = {0.0, 0.0, 0.0, 1.0};

        WGPURenderPassDescriptor renderPassDesc{};
        WGPU_SET_LABEL(renderPassDesc, label);
        renderPassDesc.colorAttachmentCount = 1;
        renderPassDesc.colorAttachments = &colorAttachment;
        gpu::CompatRenderPassTimestampWrites timestampWrites{};
        if (writeTimestamps && timestampQuerySet) {
            timestampWrites.querySet = timestampQuerySet;
            timestampWrites.beginningOfPassWriteIndex = timestampBegin;
            timestampWrites.endOfPassWriteIndex = timestampEnd;
            renderPassDesc.timestampWrites = &timestampWrites;
        }

        WGPURenderPassEncoder renderPass =
            wgpuCommandEncoderBeginRenderPass(encoder, &renderPassDesc);
        wgpuRenderPassEncoderSetPipeline(renderPass, selectedPipeline);
        wgpuRenderPassEncoderSetBindGroup(
            renderPass, 0, selectedBindGroup, 0, nullptr);
        wgpuRenderPassEncoderDraw(renderPass, 3, 1, 0, 0);
        wgpuRenderPassEncoderEnd(renderPass);
        wgpuRenderPassEncoderRelease(renderPass);
    };

    const bool useCachedPath = staticCacheActive_ && backgroundView_ &&
        staticBindGroup_ && cachedBindGroup_ && cachedPipeline_;
    if (useCachedPath && (!backgroundValid_ || backgroundDirty_)) {
        if (staticUniformsDirty_) {
            gpu::writeBuffer(queue_, staticUniformBuffer_, 0,
                             *staticUniforms_);
            staticUniformsDirty_ = false;
        }
        drawFullscreen(backgroundView_, pipeline_, staticBindGroup_,
                       "blit_static_background_pass", false);
        backgroundValid_ = true;
        backgroundDirty_ = false;
    }

    if (useCachedPath && backgroundValid_) {
        drawFullscreen(colorView, cachedPipeline_, cachedBindGroup_,
                       "blit_cached_water_pass", true);
    } else {
        drawFullscreen(colorView, pipeline_, bindGroup_,
                       "blit_render_pass", true);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Debug Visualization
// ─────────────────────────────────────────────────────────────────────────────

void BlitPath::setDebugMode(uint32_t mode) {
    if (debugMode_ != mode) {
        debugMode_ = mode;
        debugUniformsDirty_ = true;
        backgroundDirty_ = true;
    }
}

void BlitPath::setDebugMaxDepth(float maxDepth) {
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
