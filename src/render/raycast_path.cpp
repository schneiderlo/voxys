// ═══════════════════════════════════════════════════════════════════════════════
// raycast_path.cpp - Compute Ray-Caster Rendering Path Implementation
// ═══════════════════════════════════════════════════════════════════════════════

#include "gpu/pipeline.hpp"
#include "render/raycast_path.hpp"
#include "render/triangle_path.hpp"  // For CameraUniforms
#include "gpu/resources.hpp"
#include "gpu/webgpu_compat.hpp"
#include "core/log.hpp"

#include <glm/gtc/matrix_inverse.hpp>
#include <cmath>
#include <cstring>
#include <span>

namespace voxy::render {
namespace {

constexpr uint32_t kMaximumTextureDimension2D = 8'192u;

} // namespace

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
    , compositeShaderModule_(other.compositeShaderModule_)
    , compositePipelineLayout_(other.compositePipelineLayout_)
    , compositePipeline_(other.compositePipeline_)
    , bindGroupLayout_(other.bindGroupLayout_)
    , bindGroup_(other.bindGroup_)
    , staticBindGroup_(other.staticBindGroup_)
    , compositeBindGroupLayout_(other.compositeBindGroupLayout_)
    , compositeBindGroup_(other.compositeBindGroup_)
    , uniformBuffer_(other.uniformBuffer_)
    , staticUniformBuffer_(other.staticUniformBuffer_)
    , depthOutputTexture_(other.depthOutputTexture_)
    , depthOutputView_(other.depthOutputView_)
    , shadowOutputTexture_(other.shadowOutputTexture_)
    , shadowOutputView_(other.shadowOutputView_)
    , materialOutputTexture_(other.materialOutputTexture_)
    , materialOutputView_(other.materialOutputView_)
    , terrainDepthCacheTexture_(other.terrainDepthCacheTexture_)
    , terrainDepthCacheView_(other.terrainDepthCacheView_)
    , terrainShadowCacheTexture_(other.terrainShadowCacheTexture_)
    , terrainShadowCacheView_(other.terrainShadowCacheView_)
    , outputWidth_(other.outputWidth_)
    , outputHeight_(other.outputHeight_)
    , heightmapView_(other.heightmapView_)
    , heightmapWidth_(other.heightmapWidth_)
    , heightmapHeight_(other.heightmapHeight_)
    , shadowMapView_(other.shadowMapView_)
    , fallbackShadowTexture_(other.fallbackShadowTexture_)
    , fallbackShadowView_(other.fallbackShadowView_)
    , waterDisplacementView_(other.waterDisplacementView_)
    , waterCoastView_(other.waterCoastView_)
    , waterDisplacementSampler_(other.waterDisplacementSampler_)
    , uniforms_(other.uniforms_)
    , staticUniforms_(other.staticUniforms_)
    , config_(other.config_)
    , uniformsDirty_(other.uniformsDirty_)
    , staticUniformsDirty_(other.staticUniformsDirty_)
    , staticCacheDirty_(other.staticCacheDirty_)
    , staticStateChangedSinceDispatch_(other.staticStateChangedSinceDispatch_)
    , usingStaticCache_(other.usingStaticCache_)
    , staticCacheRefreshed_(other.staticCacheRefreshed_)
    , bindGroupDirty_(other.bindGroupDirty_)
{
    // Null out the source
    other.device_ = nullptr;
    other.queue_ = nullptr;
    other.shaderModule_ = nullptr;
    other.pipelineLayout_ = nullptr;
    other.pipeline_ = nullptr;
    other.compositeShaderModule_ = nullptr;
    other.compositePipelineLayout_ = nullptr;
    other.compositePipeline_ = nullptr;
    other.bindGroupLayout_ = nullptr;
    other.bindGroup_ = nullptr;
    other.staticBindGroup_ = nullptr;
    other.compositeBindGroupLayout_ = nullptr;
    other.compositeBindGroup_ = nullptr;
    other.uniformBuffer_ = nullptr;
    other.staticUniformBuffer_ = nullptr;
    other.depthOutputTexture_ = nullptr;
    other.depthOutputView_ = nullptr;
    other.shadowOutputTexture_ = nullptr;
    other.shadowOutputView_ = nullptr;
    other.materialOutputTexture_ = nullptr;
    other.materialOutputView_ = nullptr;
    other.terrainDepthCacheTexture_ = nullptr;
    other.terrainDepthCacheView_ = nullptr;
    other.terrainShadowCacheTexture_ = nullptr;
    other.terrainShadowCacheView_ = nullptr;
    other.heightmapView_ = nullptr;
    other.shadowMapView_ = nullptr;
    other.fallbackShadowTexture_ = nullptr;
    other.fallbackShadowView_ = nullptr;
    other.waterDisplacementView_ = nullptr;
    other.waterCoastView_ = nullptr;
    other.waterDisplacementSampler_ = nullptr;
    other.uniforms_ = nullptr;
    other.staticUniforms_ = nullptr;
}

RaycastPath& RaycastPath::operator=(RaycastPath&& other) noexcept {
    if (this != &other) {
        shutdown();
        
        device_ = other.device_;
        queue_ = other.queue_;
        shaderModule_ = other.shaderModule_;
        pipelineLayout_ = other.pipelineLayout_;
        pipeline_ = other.pipeline_;
        compositeShaderModule_ = other.compositeShaderModule_;
        compositePipelineLayout_ = other.compositePipelineLayout_;
        compositePipeline_ = other.compositePipeline_;
        bindGroupLayout_ = other.bindGroupLayout_;
        bindGroup_ = other.bindGroup_;
        staticBindGroup_ = other.staticBindGroup_;
        compositeBindGroupLayout_ = other.compositeBindGroupLayout_;
        compositeBindGroup_ = other.compositeBindGroup_;
        uniformBuffer_ = other.uniformBuffer_;
        staticUniformBuffer_ = other.staticUniformBuffer_;
        depthOutputTexture_ = other.depthOutputTexture_;
        depthOutputView_ = other.depthOutputView_;
        shadowOutputTexture_ = other.shadowOutputTexture_;
        shadowOutputView_ = other.shadowOutputView_;
        materialOutputTexture_ = other.materialOutputTexture_;
        materialOutputView_ = other.materialOutputView_;
        terrainDepthCacheTexture_ = other.terrainDepthCacheTexture_;
        terrainDepthCacheView_ = other.terrainDepthCacheView_;
        terrainShadowCacheTexture_ = other.terrainShadowCacheTexture_;
        terrainShadowCacheView_ = other.terrainShadowCacheView_;
        outputWidth_ = other.outputWidth_;
        outputHeight_ = other.outputHeight_;
        heightmapView_ = other.heightmapView_;
        heightmapWidth_ = other.heightmapWidth_;
        heightmapHeight_ = other.heightmapHeight_;
        shadowMapView_ = other.shadowMapView_;
        fallbackShadowTexture_ = other.fallbackShadowTexture_;
        fallbackShadowView_ = other.fallbackShadowView_;
        waterDisplacementView_ = other.waterDisplacementView_;
        waterCoastView_ = other.waterCoastView_;
        waterDisplacementSampler_ = other.waterDisplacementSampler_;
        uniforms_ = other.uniforms_;
        staticUniforms_ = other.staticUniforms_;
        config_ = other.config_;
        uniformsDirty_ = other.uniformsDirty_;
        staticUniformsDirty_ = other.staticUniformsDirty_;
        staticCacheDirty_ = other.staticCacheDirty_;
        staticStateChangedSinceDispatch_ = other.staticStateChangedSinceDispatch_;
        usingStaticCache_ = other.usingStaticCache_;
        staticCacheRefreshed_ = other.staticCacheRefreshed_;
        bindGroupDirty_ = other.bindGroupDirty_;
        
        other.device_ = nullptr;
        other.queue_ = nullptr;
        other.shaderModule_ = nullptr;
        other.pipelineLayout_ = nullptr;
        other.pipeline_ = nullptr;
        other.compositeShaderModule_ = nullptr;
        other.compositePipelineLayout_ = nullptr;
        other.compositePipeline_ = nullptr;
        other.bindGroupLayout_ = nullptr;
        other.bindGroup_ = nullptr;
        other.staticBindGroup_ = nullptr;
        other.compositeBindGroupLayout_ = nullptr;
        other.compositeBindGroup_ = nullptr;
        other.uniformBuffer_ = nullptr;
        other.staticUniformBuffer_ = nullptr;
        other.depthOutputTexture_ = nullptr;
        other.depthOutputView_ = nullptr;
        other.shadowOutputTexture_ = nullptr;
        other.shadowOutputView_ = nullptr;
        other.materialOutputTexture_ = nullptr;
        other.materialOutputView_ = nullptr;
        other.terrainDepthCacheTexture_ = nullptr;
        other.terrainDepthCacheView_ = nullptr;
        other.terrainShadowCacheTexture_ = nullptr;
        other.terrainShadowCacheView_ = nullptr;
        other.heightmapView_ = nullptr;
        other.shadowMapView_ = nullptr;
        other.fallbackShadowTexture_ = nullptr;
        other.fallbackShadowView_ = nullptr;
        other.waterDisplacementView_ = nullptr;
        other.waterCoastView_ = nullptr;
        other.waterDisplacementSampler_ = nullptr;
        other.uniforms_ = nullptr;
        other.staticUniforms_ = nullptr;
    }
    return *this;
}

void RaycastPath::shutdown() {
    if (compositeBindGroup_) {
        wgpuBindGroupRelease(compositeBindGroup_);
        compositeBindGroup_ = nullptr;
    }
    if (staticBindGroup_) {
        wgpuBindGroupRelease(staticBindGroup_);
        staticBindGroup_ = nullptr;
    }
    if (bindGroup_) {
        wgpuBindGroupRelease(bindGroup_);
        bindGroup_ = nullptr;
    }
    if (compositeBindGroupLayout_) {
        wgpuBindGroupLayoutRelease(compositeBindGroupLayout_);
        compositeBindGroupLayout_ = nullptr;
    }
    if (bindGroupLayout_) {
        wgpuBindGroupLayoutRelease(bindGroupLayout_);
        bindGroupLayout_ = nullptr;
    }
    if (pipeline_) {
        wgpuComputePipelineRelease(pipeline_);
        pipeline_ = nullptr;
    }
    if (compositePipeline_) {
        wgpuComputePipelineRelease(compositePipeline_);
        compositePipeline_ = nullptr;
    }
    if (compositePipelineLayout_) {
        wgpuPipelineLayoutRelease(compositePipelineLayout_);
        compositePipelineLayout_ = nullptr;
    }
    if (pipelineLayout_) {
        wgpuPipelineLayoutRelease(pipelineLayout_);
        pipelineLayout_ = nullptr;
    }
    if (shaderModule_) {
        wgpuShaderModuleRelease(shaderModule_);
        shaderModule_ = nullptr;
    }
    if (compositeShaderModule_) {
        wgpuShaderModuleRelease(compositeShaderModule_);
        compositeShaderModule_ = nullptr;
    }
    if (uniformBuffer_) {
        wgpuBufferRelease(uniformBuffer_);
        uniformBuffer_ = nullptr;
    }
    if (staticUniformBuffer_) {
        wgpuBufferRelease(staticUniformBuffer_);
        staticUniformBuffer_ = nullptr;
    }
    if (depthOutputView_) {
        wgpuTextureViewRelease(depthOutputView_);
        depthOutputView_ = nullptr;
    }
    if (depthOutputTexture_) {
        wgpuTextureRelease(depthOutputTexture_);
        depthOutputTexture_ = nullptr;
    }
    if (shadowOutputView_) {
        wgpuTextureViewRelease(shadowOutputView_);
        shadowOutputView_ = nullptr;
    }
    if (shadowOutputTexture_) {
        wgpuTextureRelease(shadowOutputTexture_);
        shadowOutputTexture_ = nullptr;
    }
    if (materialOutputView_) {
        wgpuTextureViewRelease(materialOutputView_);
        materialOutputView_ = nullptr;
    }
    if (materialOutputTexture_) {
        wgpuTextureRelease(materialOutputTexture_);
        materialOutputTexture_ = nullptr;
    }
    if (terrainDepthCacheView_) {
        wgpuTextureViewRelease(terrainDepthCacheView_);
        terrainDepthCacheView_ = nullptr;
    }
    if (terrainDepthCacheTexture_) {
        wgpuTextureRelease(terrainDepthCacheTexture_);
        terrainDepthCacheTexture_ = nullptr;
    }
    if (terrainShadowCacheView_) {
        wgpuTextureViewRelease(terrainShadowCacheView_);
        terrainShadowCacheView_ = nullptr;
    }
    if (terrainShadowCacheTexture_) {
        wgpuTextureRelease(terrainShadowCacheTexture_);
        terrainShadowCacheTexture_ = nullptr;
    }
    
    if (fallbackShadowView_) {
        wgpuTextureViewRelease(fallbackShadowView_);
        fallbackShadowView_ = nullptr;
    }
    if (fallbackShadowTexture_) {
        wgpuTextureRelease(fallbackShadowTexture_);
        fallbackShadowTexture_ = nullptr;
    }

    // Free heap-allocated uniforms
    delete uniforms_;
    uniforms_ = nullptr;
    delete staticUniforms_;
    staticUniforms_ = nullptr;

    // Note: We don't own heightmapView_ or shadowMapView_, so don't release them
    heightmapView_ = nullptr;
    shadowMapView_ = nullptr;
    waterDisplacementView_ = nullptr;
    waterCoastView_ = nullptr;
    waterDisplacementSampler_ = nullptr;
    device_ = nullptr;
    queue_ = nullptr;
    uniformsDirty_ = true;
    staticUniformsDirty_ = true;
    staticCacheDirty_ = true;
    staticStateChangedSinceDispatch_ = true;
    usingStaticCache_ = false;
    staticCacheRefreshed_ = false;
    bindGroupDirty_ = true;
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
    
    if (outputWidth == 0 || outputHeight == 0
        || outputWidth > kMaximumTextureDimension2D
        || outputHeight > kMaximumTextureDimension2D) {
        LOG_ERROR("RaycastPath::init: invalid output dimensions");
        return false;
    }
    if (!std::isfinite(config.heightScale) || config.heightScale <= 0.0f
        || !std::isfinite(config.cellScale) || config.cellScale <= 0.0f
        || !std::isfinite(config.fogDensity) || config.fogDensity < 0.0f) {
        LOG_ERROR("RaycastPath::init: invalid renderer configuration");
        return false;
    }
    
    device_ = device;
    queue_ = queue;
    config_ = config;
    
    // Allocate uniforms on heap (reuses CameraUniforms from triangle_path)
    uniforms_ = new CameraUniforms();
    if (!uniforms_->setTerrain(
            256, 256, config.heightScale, config.cellScale, 1.0f,
            config.fogDensity)) {
        shutdown();
        return false;
    }
    staticUniforms_ = new CameraUniforms(*uniforms_);
    updateStaticUniforms();
    
    // Create resources in order
    if (!createDepthOutputTexture(outputWidth, outputHeight)) {
        LOG_ERROR("Failed to create depth output texture");
        shutdown();
        return false;
    }
    
    if (!createUniformBuffer()) {
        LOG_ERROR("Failed to create uniform buffer");
        shutdown();
        return false;
    }

    // 1x1 zero fallback for the baked shadow map: boundary 0 means every
    // point above the terrain floor is lit, so rendering works (shadowless)
    // before/without a bake.
    {
        const uint16_t zero = 0;
        gpu::TextureDesc desc = gpu::TextureDesc::tex2D(
            1, 1, WGPUTextureFormat_R16Uint,
            WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst,
            "raycast_shadow_fallback");
        fallbackShadowTexture_ = gpu::createTextureWithData(
            device_, queue_, desc,
            std::as_bytes(std::span<const uint16_t>(&zero, 1)), sizeof(uint16_t));
        if (!fallbackShadowTexture_) {
            LOG_ERROR("Failed to create fallback shadow texture");
            shutdown();
            return false;
        }
        fallbackShadowView_ = gpu::createTextureView(fallbackShadowTexture_);
        if (!fallbackShadowView_) {
            LOG_ERROR("Failed to create fallback shadow texture view");
            shutdown();
            return false;
        }
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

bool RaycastPath::createDepthOutputTexture(uint32_t width, uint32_t height) {
    WGPUTexture nextDepthTexture = nullptr;
    WGPUTextureView nextDepthView = nullptr;
    WGPUTexture nextShadowTexture = nullptr;
    WGPUTextureView nextShadowView = nullptr;
    WGPUTexture nextMaterialTexture = nullptr;
    WGPUTextureView nextMaterialView = nullptr;
    WGPUTexture nextTerrainDepthTexture = nullptr;
    WGPUTextureView nextTerrainDepthView = nullptr;
    WGPUTexture nextTerrainShadowTexture = nullptr;
    WGPUTextureView nextTerrainShadowView = nullptr;
    const auto releaseView = [](WGPUTextureView& view) {
        if (!view) return;
        wgpuTextureViewRelease(view);
        view = nullptr;
    };
    const auto releaseTexture = [](WGPUTexture& texture) {
        if (!texture) return;
        wgpuTextureRelease(texture);
        texture = nullptr;
    };
    const auto cleanup = [&]() {
        releaseView(nextTerrainShadowView);
        releaseTexture(nextTerrainShadowTexture);
        releaseView(nextTerrainDepthView);
        releaseTexture(nextTerrainDepthTexture);
        releaseView(nextMaterialView);
        releaseTexture(nextMaterialTexture);
        releaseView(nextShadowView);
        releaseTexture(nextShadowTexture);
        releaseView(nextDepthView);
        releaseTexture(nextDepthTexture);
    };
    const auto fail = [&](const char* message) {
        LOG_ERROR("{}", message);
        cleanup();
        return false;
    };

    // Create separate R32Float storage textures for ray-cast depth and shadow data.
    gpu::TextureDesc depthDesc = gpu::TextureDesc::storage(
        width, height,
        WGPUTextureFormat_R32Float,
        "raycast_depth_output"
    );
    // The fused settled-camera pass writes linear scene depth as a second
    // color attachment.  Keep storage usage for the direct/Lego compute path
    // and texture usage for primitive occlusion.
    depthDesc.usage |= WGPUTextureUsage_RenderAttachment;
    nextDepthTexture = gpu::createTexture(device_, depthDesc);
    
    if (!nextDepthTexture) return fail("Failed to create depth output texture");
    
    // Create texture view
    gpu::TextureViewDesc viewDesc{};
    viewDesc.label = "raycast_depth_output_view";
    viewDesc.format = WGPUTextureFormat_R32Float;
    
    nextDepthView = gpu::createTextureView(nextDepthTexture, viewDesc);
    
    if (!nextDepthView) return fail("Failed to create depth output texture view");

    // Shadow output packs terrain shadow or signed water depth into one float.
    // See terrain_raycast.wgsl for the water depth/shadow sign encoding.
    gpu::TextureDesc shadowDesc = gpu::TextureDesc::storage(
        width, height,
        WGPUTextureFormat_R32Float,
        "raycast_shadow_output"
    );

    nextShadowTexture = gpu::createTexture(device_, shadowDesc);

    if (!nextShadowTexture) return fail("Failed to create shadow output texture");

    gpu::TextureViewDesc shadowViewDesc{};
    shadowViewDesc.label = "raycast_shadow_output_view";
    shadowViewDesc.format = WGPUTextureFormat_R32Float;

    nextShadowView = gpu::createTextureView(nextShadowTexture, shadowViewDesc);

    if (!nextShadowView) return fail("Failed to create shadow output texture view");

    gpu::TextureDesc materialDesc = gpu::TextureDesc::storage(
        width, height,
        WGPUTextureFormat_RGBA16Float,
        "raycast_material_output"
    );

    nextMaterialTexture = gpu::createTexture(device_, materialDesc);

    if (!nextMaterialTexture) return fail("Failed to create material output texture");

    gpu::TextureViewDesc materialViewDesc{};
    materialViewDesc.label = "raycast_material_output_view";
    materialViewDesc.format = WGPUTextureFormat_RGBA16Float;

    nextMaterialView = gpu::createTextureView(
        nextMaterialTexture, materialViewDesc);

    if (!nextMaterialView) return fail("Failed to create material output texture view");

    // The expensive terrain traversal is invariant while the camera and terrain
    // stay fixed. Cache its exact per-pixel depth so only animated water needs
    // to be recomputed on settled frames.
    gpu::TextureDesc cacheDesc = gpu::TextureDesc::storage(
        width, height,
        WGPUTextureFormat_R32Float,
        "raycast_terrain_depth_cache"
    );

    nextTerrainDepthTexture = gpu::createTexture(device_, cacheDesc);
    if (!nextTerrainDepthTexture) {
        return fail("Failed to create terrain depth cache texture");
    }

    gpu::TextureViewDesc cacheViewDesc{};
    cacheViewDesc.label = "raycast_terrain_depth_cache_view";
    cacheViewDesc.format = WGPUTextureFormat_R32Float;
    nextTerrainDepthView = gpu::createTextureView(
        nextTerrainDepthTexture, cacheViewDesc);
    if (!nextTerrainDepthView) {
        return fail("Failed to create terrain depth cache texture view");
    }

    gpu::TextureDesc shadowCacheDesc = gpu::TextureDesc::storage(
        width, height,
        WGPUTextureFormat_R32Float,
        "raycast_terrain_shadow_cache"
    );
    nextTerrainShadowTexture = gpu::createTexture(device_, shadowCacheDesc);
    if (!nextTerrainShadowTexture) {
        return fail("Failed to create terrain shadow cache texture");
    }

    gpu::TextureViewDesc shadowCacheViewDesc{};
    shadowCacheViewDesc.label = "raycast_terrain_shadow_cache_view";
    shadowCacheViewDesc.format = WGPUTextureFormat_R32Float;
    nextTerrainShadowView = gpu::createTextureView(
        nextTerrainShadowTexture, shadowCacheViewDesc);
    if (!nextTerrainShadowView) {
        return fail("Failed to create terrain shadow cache texture view");
    }

    releaseView(terrainShadowCacheView_);
    releaseTexture(terrainShadowCacheTexture_);
    releaseView(terrainDepthCacheView_);
    releaseTexture(terrainDepthCacheTexture_);
    releaseView(materialOutputView_);
    releaseTexture(materialOutputTexture_);
    releaseView(shadowOutputView_);
    releaseTexture(shadowOutputTexture_);
    releaseView(depthOutputView_);
    releaseTexture(depthOutputTexture_);

    depthOutputTexture_ = nextDepthTexture;
    depthOutputView_ = nextDepthView;
    shadowOutputTexture_ = nextShadowTexture;
    shadowOutputView_ = nextShadowView;
    materialOutputTexture_ = nextMaterialTexture;
    materialOutputView_ = nextMaterialView;
    terrainDepthCacheTexture_ = nextTerrainDepthTexture;
    terrainDepthCacheView_ = nextTerrainDepthView;
    terrainShadowCacheTexture_ = nextTerrainShadowTexture;
    terrainShadowCacheView_ = nextTerrainShadowView;
    nextDepthTexture = nullptr;
    nextDepthView = nullptr;
    nextShadowTexture = nullptr;
    nextShadowView = nullptr;
    nextMaterialTexture = nullptr;
    nextMaterialView = nullptr;
    nextTerrainDepthTexture = nullptr;
    nextTerrainDepthView = nullptr;
    nextTerrainShadowTexture = nullptr;
    nextTerrainShadowView = nullptr;
    outputWidth_ = width;
    outputHeight_ = height;
    
    LOG_DEBUG("Created raycast output textures: {}x{} depth/shadow R32Float, water data RGBA16Float",
              width, height);
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

    bufferDesc.label = "raycast_static_camera_uniforms";
    staticUniformBuffer_ = gpu::createBuffer(device_, bufferDesc);
    if (!staticUniformBuffer_) {
        LOG_ERROR("Failed to create static terrain uniform buffer");
        return false;
    }
    
    // Upload initial data
    if (!updateUniformBuffer()
        || !gpu::writeBuffer(
            queue_, staticUniformBuffer_, 0, *staticUniforms_)) {
        return false;
    }
    staticUniformsDirty_ = false;
    
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
    // @group(0) @binding(2) var outDepth : texture_storage_2d<r32float, write>;
    // @group(0) @binding(3) var outShadow : texture_storage_2d<r32float, write>;
    // @group(0) @binding(4) var outMaterial : texture_storage_2d<rgba16float, write>;
    // @group(0) @binding(5) var shadowHeightTex : texture_2d<u32>;
    // @group(0) @binding(6) var waterDisplacementTex : texture_2d_array<f32>;
    // @group(0) @binding(7) var waterDisplacementSampler : sampler;
    // @group(0) @binding(8) var waterCoastFieldTex : texture_2d<f32>;

    std::array<gpu::BindGroupLayoutEntry, 9> entries = {
        gpu::BindGroupLayoutEntry(0)
            .computeVisible()
            .uniformBuffer(false, sizeof(CameraUniforms)),
        gpu::BindGroupLayoutEntry(1)
            .computeVisible()
            .texture(WGPUTextureSampleType_Uint, WGPUTextureViewDimension_2D, false),
        gpu::BindGroupLayoutEntry(2)
            .computeVisible()
            .storageTexture(WGPUStorageTextureAccess_WriteOnly,
                           WGPUTextureFormat_R32Float,
                           WGPUTextureViewDimension_2D),
        gpu::BindGroupLayoutEntry(3)
            .computeVisible()
            .storageTexture(WGPUStorageTextureAccess_WriteOnly,
                           WGPUTextureFormat_R32Float,
                           WGPUTextureViewDimension_2D),
        gpu::BindGroupLayoutEntry(4)
            .computeVisible()
            .storageTexture(WGPUStorageTextureAccess_WriteOnly,
                           WGPUTextureFormat_RGBA16Float,
                           WGPUTextureViewDimension_2D),
        gpu::BindGroupLayoutEntry(5)
            .computeVisible()
            .texture(WGPUTextureSampleType_Uint, WGPUTextureViewDimension_2D, false),
        gpu::BindGroupLayoutEntry(6)
            .computeVisible()
            .texture(WGPUTextureSampleType_Float, WGPUTextureViewDimension_2DArray, false),
        gpu::BindGroupLayoutEntry(7)
            .computeVisible()
            .sampler(WGPUSamplerBindingType_Filtering),
        gpu::BindGroupLayoutEntry(8)
            .computeVisible()
            .texture(WGPUTextureSampleType_Float, WGPUTextureViewDimension_2D, false)
    };
    
    bindGroupLayout_ = gpu::createBindGroupLayout(device_, entries, "raycast_bind_group_layout");
    
    if (!bindGroupLayout_) {
        LOG_ERROR("Failed to create bind group layout");
        return false;
    }

    // Layout for water_composite.wgsl. The cached R32Float terrain depth is a
    // read-only texture here; final depth, shadow, and material remain the same
    // full-resolution storage outputs consumed by RayBlitPath.
    std::array<gpu::BindGroupLayoutEntry, 11> compositeEntries = {
        gpu::BindGroupLayoutEntry(0)
            .computeVisible()
            .uniformBuffer(false, sizeof(CameraUniforms)),
        gpu::BindGroupLayoutEntry(1)
            .computeVisible()
            .texture(WGPUTextureSampleType_Uint, WGPUTextureViewDimension_2D, false),
        gpu::BindGroupLayoutEntry(2)
            .computeVisible()
            .texture(WGPUTextureSampleType_UnfilterableFloat,
                     WGPUTextureViewDimension_2D, false),
        gpu::BindGroupLayoutEntry(3)
            .computeVisible()
            .texture(WGPUTextureSampleType_UnfilterableFloat,
                     WGPUTextureViewDimension_2D, false),
        gpu::BindGroupLayoutEntry(4)
            .computeVisible()
            .storageTexture(WGPUStorageTextureAccess_WriteOnly,
                           WGPUTextureFormat_R32Float,
                           WGPUTextureViewDimension_2D),
        gpu::BindGroupLayoutEntry(5)
            .computeVisible()
            .storageTexture(WGPUStorageTextureAccess_WriteOnly,
                           WGPUTextureFormat_R32Float,
                           WGPUTextureViewDimension_2D),
        gpu::BindGroupLayoutEntry(6)
            .computeVisible()
            .storageTexture(WGPUStorageTextureAccess_WriteOnly,
                           WGPUTextureFormat_RGBA16Float,
                           WGPUTextureViewDimension_2D),
        gpu::BindGroupLayoutEntry(7)
            .computeVisible()
            .texture(WGPUTextureSampleType_Uint, WGPUTextureViewDimension_2D, false),
        gpu::BindGroupLayoutEntry(8)
            .computeVisible()
            .texture(WGPUTextureSampleType_Float,
                     WGPUTextureViewDimension_2DArray, false),
        gpu::BindGroupLayoutEntry(9)
            .computeVisible()
            .sampler(WGPUSamplerBindingType_Filtering),
        gpu::BindGroupLayoutEntry(10)
            .computeVisible()
            .texture(WGPUTextureSampleType_Float, WGPUTextureViewDimension_2D, false)
    };

    compositeBindGroupLayout_ = gpu::createBindGroupLayout(
        device_, compositeEntries, "water_composite_bind_group_layout");
    if (!compositeBindGroupLayout_) {
        LOG_ERROR("Failed to create water composite bind group layout");
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
    
    pipeline_ = ::voxy::gpu::createComputePipeline(device_, &pipelineDesc);
    
    if (!pipeline_) {
        LOG_ERROR("Failed to create raycast compute pipeline");
        return false;
    }

    const std::filesystem::path compositeShaderPath =
        config.shaderPath.parent_path() / "water_composite.wgsl";
    compositeShaderModule_ = gpu::loadShaderModule(
        device_, compositeShaderPath, "water_composite.wgsl");
    if (!compositeShaderModule_) {
        LOG_ERROR("Failed to load water composite shader from: {}",
                  compositeShaderPath.string());
        return false;
    }

    std::array<WGPUBindGroupLayout, 1> compositeLayouts = {
        compositeBindGroupLayout_
    };
    compositePipelineLayout_ = gpu::createPipelineLayout(
        device_, compositeLayouts, "water_composite_pipeline_layout");
    if (!compositePipelineLayout_) {
        LOG_ERROR("Failed to create water composite pipeline layout");
        return false;
    }

    WGPUComputePipelineDescriptor compositePipelineDesc{};
    WGPU_SET_LABEL(compositePipelineDesc, "water_composite_pipeline");
    compositePipelineDesc.layout = compositePipelineLayout_;
    compositePipelineDesc.compute.module = compositeShaderModule_;
    WGPU_SET_ENTRY_POINT(compositePipelineDesc.compute, "main");
    compositePipeline_ = ::voxy::gpu::createComputePipeline(
        device_, &compositePipelineDesc);
    if (!compositePipeline_) {
        LOG_ERROR("Failed to create water composite compute pipeline");
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

    if (!shadowOutputView_) {
        LOG_ERROR("Cannot create bind group: no shadow output view");
        return false;
    }

    if (!materialOutputView_) {
        LOG_ERROR("Cannot create bind group: no material output view");
        return false;
    }
    if (!terrainDepthCacheView_) {
        LOG_ERROR("Cannot create bind group: no terrain depth cache view");
        return false;
    }
    if (!terrainShadowCacheView_) {
        LOG_ERROR("Cannot create bind group: no terrain shadow cache view");
        return false;
    }
    if (!waterDisplacementView_ || !waterCoastView_ || !waterDisplacementSampler_) {
        LOG_ERROR("Cannot create bind group: no FFT water simulation");
        return false;
    }
    
    std::array<gpu::BindGroupEntry, 9> entries = {
        gpu::BindGroupEntry(0).buffer(uniformBuffer_, 0, sizeof(CameraUniforms)),
        gpu::BindGroupEntry(1).textureView(heightmapView_),
        gpu::BindGroupEntry(2).textureView(depthOutputView_),
        gpu::BindGroupEntry(3).textureView(shadowOutputView_),
        gpu::BindGroupEntry(4).textureView(materialOutputView_),
        gpu::BindGroupEntry(5).textureView(shadowMapView_ ? shadowMapView_
                                                          : fallbackShadowView_),
        gpu::BindGroupEntry(6).textureView(waterDisplacementView_),
        gpu::BindGroupEntry(7).sampler(waterDisplacementSampler_),
        gpu::BindGroupEntry(8).textureView(waterCoastView_)
    };
    
    WGPUBindGroup nextBindGroup = gpu::createBindGroup(
        device_, bindGroupLayout_, entries, "raycast_bind_group");
    
    if (!nextBindGroup) {
        LOG_ERROR("Failed to create raycast bind group");
        return false;
    }

    std::array<gpu::BindGroupEntry, 9> staticEntries = {
        gpu::BindGroupEntry(0).buffer(staticUniformBuffer_, 0, sizeof(CameraUniforms)),
        gpu::BindGroupEntry(1).textureView(heightmapView_),
        gpu::BindGroupEntry(2).textureView(terrainDepthCacheView_),
        gpu::BindGroupEntry(3).textureView(terrainShadowCacheView_),
        gpu::BindGroupEntry(4).textureView(materialOutputView_),
        gpu::BindGroupEntry(5).textureView(shadowMapView_ ? shadowMapView_
                                                          : fallbackShadowView_),
        gpu::BindGroupEntry(6).textureView(waterDisplacementView_),
        gpu::BindGroupEntry(7).sampler(waterDisplacementSampler_),
        gpu::BindGroupEntry(8).textureView(waterCoastView_)
    };
    WGPUBindGroup nextStaticBindGroup = gpu::createBindGroup(
        device_, bindGroupLayout_, staticEntries, "raycast_static_bind_group");
    if (!nextStaticBindGroup) {
        LOG_ERROR("Failed to create static terrain bind group");
        wgpuBindGroupRelease(nextBindGroup);
        return false;
    }

    std::array<gpu::BindGroupEntry, 11> compositeEntries = {
        gpu::BindGroupEntry(0).buffer(uniformBuffer_, 0, sizeof(CameraUniforms)),
        gpu::BindGroupEntry(1).textureView(heightmapView_),
        gpu::BindGroupEntry(2).textureView(terrainDepthCacheView_),
        gpu::BindGroupEntry(3).textureView(terrainShadowCacheView_),
        gpu::BindGroupEntry(4).textureView(depthOutputView_),
        gpu::BindGroupEntry(5).textureView(shadowOutputView_),
        gpu::BindGroupEntry(6).textureView(materialOutputView_),
        gpu::BindGroupEntry(7).textureView(shadowMapView_ ? shadowMapView_
                                                          : fallbackShadowView_),
        gpu::BindGroupEntry(8).textureView(waterDisplacementView_),
        gpu::BindGroupEntry(9).sampler(waterDisplacementSampler_),
        gpu::BindGroupEntry(10).textureView(waterCoastView_)
    };
    WGPUBindGroup nextCompositeBindGroup = gpu::createBindGroup(
        device_, compositeBindGroupLayout_, compositeEntries,
        "water_composite_bind_group");
    if (!nextCompositeBindGroup) {
        LOG_ERROR("Failed to create water composite bind group");
        wgpuBindGroupRelease(nextStaticBindGroup);
        wgpuBindGroupRelease(nextBindGroup);
        return false;
    }

    if (compositeBindGroup_) wgpuBindGroupRelease(compositeBindGroup_);
    if (staticBindGroup_) wgpuBindGroupRelease(staticBindGroup_);
    if (bindGroup_) wgpuBindGroupRelease(bindGroup_);
    bindGroup_ = nextBindGroup;
    staticBindGroup_ = nextStaticBindGroup;
    compositeBindGroup_ = nextCompositeBindGroup;
    
    bindGroupDirty_ = false;
    LOG_DEBUG("Created raycast bind group");
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Resize
// ─────────────────────────────────────────────────────────────────────────────

bool RaycastPath::resize(uint32_t width, uint32_t height) {
    if (width == 0 || height == 0
        || width > kMaximumTextureDimension2D
        || height > kMaximumTextureDimension2D) {
        LOG_ERROR("RaycastPath::resize: invalid dimensions");
        return false;
    }
    
    if (width == outputWidth_ && height == outputHeight_) {
        return true;  // No change needed
    }
    
    LOG_DEBUG("Resizing raycast output: {}x{} -> {}x{}", 
              outputWidth_, outputHeight_, width, height);
    
    // Build every replacement before disturbing the working output set.
    if (!createDepthOutputTexture(width, height)) {
        LOG_ERROR("Failed to recreate depth output texture after resize");
        return false;
    }

    // Keep the old complete group set alive until createBindGroup() has built
    // every replacement. The groups retain the old views, so a failed rebuild
    // remains retryable without leaving this object half initialized.
    bindGroupDirty_ = true;
    staticCacheDirty_ = true;
    
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Heightmap Binding
// ─────────────────────────────────────────────────────────────────────────────

bool RaycastPath::setHeightmap(
    WGPUTextureView heightmapView, uint32_t width, uint32_t height) {
    if (!heightmapView || width < 2u || height < 2u
        || width > kMaximumTextureDimension2D
        || height > kMaximumTextureDimension2D) {
        LOG_ERROR("RaycastPath::setHeightmap: invalid terrain binding");
        return false;
    }
    // Update uniforms with terrain size
    if (uniforms_) {
        CameraUniforms next = *uniforms_;
        if (!next.setTerrain(
                width, height, config_.heightScale, config_.cellScale,
                1.0f, config_.fogDensity)) {
            return false;
        }
        *uniforms_ = next;
        uniformsDirty_ = true;
        updateStaticUniforms();
    }

    heightmapView_ = heightmapView;
    heightmapWidth_ = width;
    heightmapHeight_ = height;
    
    // Need to recreate bind group
    bindGroupDirty_ = true;
    staticCacheDirty_ = true;

    LOG_DEBUG("Set heightmap: {}x{}", width, height);
    return true;
}

void RaycastPath::setShadowMap(WGPUTextureView shadowMapView) {
    shadowMapView_ = shadowMapView;
    bindGroupDirty_ = true;
    staticCacheDirty_ = true;
    staticStateChangedSinceDispatch_ = true;
    LOG_DEBUG("Set baked shadow map");
}

void RaycastPath::setWaterSimulation(WGPUTextureView displacementView,
                                     WGPUTextureView coastView,
                                     WGPUSampler sampler) {
    waterDisplacementView_ = displacementView;
    waterCoastView_ = coastView;
    waterDisplacementSampler_ = sampler;
    bindGroupDirty_ = true;
    LOG_DEBUG("Set FFT water displacement cascades");
}

// ─────────────────────────────────────────────────────────────────────────────
// Camera Updates
// ─────────────────────────────────────────────────────────────────────────────

void RaycastPath::updateCamera(const glm::mat4& view, const glm::mat4& proj, 
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

void RaycastPath::setLegoMode(bool enabled) {
    if (uniforms_) {
        uniforms_->setLegoMode(enabled);
        uniformsDirty_ = true;
        updateStaticUniforms();
    }
}

void RaycastPath::setCameraUniforms(const CameraUniforms& uniforms) {
    if (!uniforms_) return;
    if (!uniforms.isValid()) {
        LOG_ERROR("RaycastPath::setCameraUniforms: invalid uniform block");
        return;
    }

    *uniforms_ = uniforms;
    uniformsDirty_ = true;
    updateStaticUniforms();
}

bool RaycastPath::updateUniformBuffer() {
    if (!uniformBuffer_ || !queue_ || !uniforms_) return false;
    
    if (!gpu::writeBuffer(queue_, uniformBuffer_, 0, *uniforms_)) {
        return false;
    }
    uniformsDirty_ = false;
    return true;
}

void RaycastPath::updateStaticUniforms() {
    if (!uniforms_ || !staticUniforms_) return;

    CameraUniforms next = *uniforms_;

    // The cached pass renders terrain only. Canonicalizing every water field
    // prevents animated simulation time and art-control edits from invalidating
    // terrain depth that they cannot affect.
    next.waterParams = glm::vec4(0.0f);
    next.waterColorA = glm::vec4(0.0f);
    next.waterColorB = glm::vec4(0.0f);
    next.waterMotion = glm::vec4(0.0f);
    next.waterOptics = glm::vec4(0.0f);
    next.waterFoam = glm::vec4(0.0f);
    next.waterSpectrum = glm::vec4(0.0f);
    // These fields are consumed only by the later lighting/material pass.
    // Keeping them out of the terrain snapshot lets colour grading, fog and
    // light colour scrub at uniform speed without relaunching the ray caster.
    next.metrics.z = 0.0f;
    next.metrics.w = 0.0f;
    next.lightDirVS = glm::vec4(0.0f);
    next.lightingColor = glm::vec4(0.0f);
    next.ambientExposure = glm::vec4(0.0f);
    next.fogColor = glm::vec4(0.0f);
    if (next.invProjParams.z <= 0.5f && next.lightDirWS.w == 0.0f) {
        // Normal mode samples the baked shadow texture. Sun direction only
        // affects this pass in Lego mode, where shadows are traced directly.
        next.lightDirWS = glm::vec4(0.0f);
    }

    if (std::memcmp(staticUniforms_, &next, sizeof(CameraUniforms)) == 0) {
        return;
    }

    *staticUniforms_ = next;
    staticUniformsDirty_ = true;
    staticCacheDirty_ = true;
    staticStateChangedSinceDispatch_ = true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Dispatch
// ─────────────────────────────────────────────────────────────────────────────

void RaycastPath::dispatch(WGPUCommandEncoder encoder,
                           WGPUQuerySet timestampQuerySet,
                           uint32_t timestampBegin,
                           uint32_t timestampEnd,
                           bool deferWaterComposite) {
    if (!pipeline_ || !compositePipeline_) {
        LOG_WARN("RaycastPath::dispatch: not initialized");
        return;
    }
    if (!encoder) {
        LOG_ERROR("RaycastPath::dispatch: null command encoder");
        return;
    }
    
    if (!heightmapView_) {
        LOG_WARN("RaycastPath::dispatch: no heightmap set");
        return;
    }
    
    // Update uniform buffer if dirty
    if (uniformsDirty_ && !updateUniformBuffer()) {
        return;
    }
    
    // Create bind group if dirty
    if (bindGroupDirty_ || !bindGroup_) {
        if (!createBindGroup()) {
            LOG_ERROR("Failed to create bind group during dispatch");
            return;
        }
    }
    
    const bool legoMode = uniforms_ && uniforms_->invProjParams.z > 0.5f;
    // Water must use the same scene-color/depth composition while the camera
    // moves and while it is still.  Refreshing the terrain-only buffers on a
    // view change lets the lightweight water pass and HDR refraction remain
    // authoritative for every non-Lego frame.
    // Physical LEGO scenes retain exact normals in materialOutputView_ while
    // the cached terrain-only depth feeds the existing animated water pass.
    const bool legoStudy = legoMode && uniforms_->invProjParams.z >= 1.5f;
    const bool useDirectPath = legoMode && (!legoStudy || !deferWaterComposite);
    usingStaticCache_ = !useDirectPath;
    staticCacheRefreshed_ = false;

    // Upload the terrain-only snapshot only when it will actually be consumed.
    if (!useDirectPath && staticCacheDirty_ && staticUniformsDirty_) {
        if (!gpu::writeBuffer(
                queue_, staticUniformBuffer_, 0, *staticUniforms_)) {
            return;
        }
        staticUniformsDirty_ = false;
    }

    // A changed view refreshes terrain once. The fused lighting path consumes
    // that cache directly and writes final color plus linear depth in one
    // render pass, so settled frames need no raycast compute work at all.
    if (!useDirectPath && deferWaterComposite && !staticCacheDirty_
        && !timestampQuerySet) {
        staticStateChangedSinceDispatch_ = false;
        return;
    }

    WGPUComputePassDescriptor computePassDesc{};
    WGPU_SET_LABEL(computePassDesc, "raycast_compute_pass");
    gpu::CompatPassTimestampWrites timestampWrites{};
    if (timestampQuerySet) {
        timestampWrites.querySet = timestampQuerySet;
        timestampWrites.beginningOfPassWriteIndex = timestampBegin;
        timestampWrites.endOfPassWriteIndex = timestampEnd;
        computePassDesc.timestampWrites = &timestampWrites;
    }
    
    WGPUComputePassEncoder computePass = wgpuCommandEncoderBeginComputePass(encoder, &computePassDesc);
    if (!computePass) {
        LOG_ERROR("RaycastPath::dispatch: failed to begin compute pass");
        return;
    }
    
    const uint32_t workgroupsX = getWorkgroupCountX();
    const uint32_t workgroupsY = getWorkgroupCountY();

    if (useDirectPath) {
        wgpuComputePassEncoderSetPipeline(computePass, pipeline_);
        wgpuComputePassEncoderSetBindGroup(
            computePass, 0, bindGroup_, 0, nullptr);
        wgpuComputePassEncoderDispatchWorkgroups(
            computePass, workgroupsX, workgroupsY, 1);
        staticStateChangedSinceDispatch_ = false;
    } else {
        if (staticCacheDirty_) {
            wgpuComputePassEncoderSetPipeline(computePass, pipeline_);
            wgpuComputePassEncoderSetBindGroup(
                computePass, 0, staticBindGroup_, 0, nullptr);
            wgpuComputePassEncoderDispatchWorkgroups(
                computePass, workgroupsX, workgroupsY, 1);
            staticCacheDirty_ = false;
            staticCacheRefreshed_ = true;
        }

        if (!deferWaterComposite) {
            wgpuComputePassEncoderSetPipeline(computePass, compositePipeline_);
            wgpuComputePassEncoderSetBindGroup(
                computePass, 0, compositeBindGroup_, 0, nullptr);
            wgpuComputePassEncoderDispatchWorkgroups(
                computePass, workgroupsX, workgroupsY, 1);
        }
        staticStateChangedSinceDispatch_ = false;
    }
    
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
