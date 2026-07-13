// ═══════════════════════════════════════════════════════════════════════════════
// blit_path.cpp - Fullscreen Blit/Lighting Rendering Path Implementation
// ═══════════════════════════════════════════════════════════════════════════════

#include "render/blit_path.hpp"
#include "render/triangle_path.hpp"  // For CameraUniforms
#include "gpu/resources.hpp"
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
    , bindGroupLayout_(other.bindGroupLayout_)
    , bindGroup_(other.bindGroup_)
    , uniformBuffer_(other.uniformBuffer_)
    , debugUniformBuffer_(other.debugUniformBuffer_)
    , sampler_(other.sampler_)
    , skyLutShaderModule_(other.skyLutShaderModule_)
    , skyLutPipelineLayout_(other.skyLutPipelineLayout_)
    , skyLutPipeline_(other.skyLutPipeline_)
    , skyLutBindGroupLayout_(other.skyLutBindGroupLayout_)
    , skyLutBindGroup_(other.skyLutBindGroup_)
    , skyLutTexture_(other.skyLutTexture_)
    , skyLutView_(other.skyLutView_)
    , skyLutBaked_(other.skyLutBaked_)
    , waterNoiseTexture_(other.waterNoiseTexture_)
    , waterNoiseView_(other.waterNoiseView_)
    , noiseSampler_(other.noiseSampler_)
    , depthView_(other.depthView_)
    , shadowView_(other.shadowView_)
    , materialView_(other.materialView_)
    , terrainView_(other.terrainView_)
    , lightmapView_(other.lightmapView_)
    , waterDisplacementView_(other.waterDisplacementView_)
    , waterFoamView_(other.waterFoamView_)
    , waterDisplacementSampler_(other.waterDisplacementSampler_)
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
    other.skyLutShaderModule_ = nullptr;
    other.skyLutPipelineLayout_ = nullptr;
    other.skyLutPipeline_ = nullptr;
    other.skyLutBindGroupLayout_ = nullptr;
    other.skyLutBindGroup_ = nullptr;
    other.skyLutTexture_ = nullptr;
    other.skyLutView_ = nullptr;
    other.waterNoiseTexture_ = nullptr;
    other.waterNoiseView_ = nullptr;
    other.noiseSampler_ = nullptr;
    other.depthView_ = nullptr;
    other.shadowView_ = nullptr;
    other.materialView_ = nullptr;
    other.terrainView_ = nullptr;
    other.lightmapView_ = nullptr;
    other.waterDisplacementView_ = nullptr;
    other.waterFoamView_ = nullptr;
    other.waterDisplacementSampler_ = nullptr;
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
        skyLutShaderModule_ = other.skyLutShaderModule_;
        skyLutPipelineLayout_ = other.skyLutPipelineLayout_;
        skyLutPipeline_ = other.skyLutPipeline_;
        skyLutBindGroupLayout_ = other.skyLutBindGroupLayout_;
        skyLutBindGroup_ = other.skyLutBindGroup_;
        skyLutTexture_ = other.skyLutTexture_;
        skyLutView_ = other.skyLutView_;
        skyLutBaked_ = other.skyLutBaked_;
        waterNoiseTexture_ = other.waterNoiseTexture_;
        waterNoiseView_ = other.waterNoiseView_;
        noiseSampler_ = other.noiseSampler_;
        depthView_ = other.depthView_;
        shadowView_ = other.shadowView_;
        materialView_ = other.materialView_;
        terrainView_ = other.terrainView_;
        lightmapView_ = other.lightmapView_;
        waterDisplacementView_ = other.waterDisplacementView_;
        waterFoamView_ = other.waterFoamView_;
        waterDisplacementSampler_ = other.waterDisplacementSampler_;
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
        other.skyLutShaderModule_ = nullptr;
        other.skyLutPipelineLayout_ = nullptr;
        other.skyLutPipeline_ = nullptr;
        other.skyLutBindGroupLayout_ = nullptr;
        other.skyLutBindGroup_ = nullptr;
        other.skyLutTexture_ = nullptr;
        other.skyLutView_ = nullptr;
        other.waterNoiseTexture_ = nullptr;
        other.waterNoiseView_ = nullptr;
        other.noiseSampler_ = nullptr;
        other.depthView_ = nullptr;
        other.shadowView_ = nullptr;
        other.materialView_ = nullptr;
        other.terrainView_ = nullptr;
        other.lightmapView_ = nullptr;
        other.waterDisplacementView_ = nullptr;
        other.waterFoamView_ = nullptr;
        other.waterDisplacementSampler_ = nullptr;
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
    if (skyLutBindGroup_) {
        wgpuBindGroupRelease(skyLutBindGroup_);
        skyLutBindGroup_ = nullptr;
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
    if (skyLutTexture_) {
        wgpuTextureRelease(skyLutTexture_);
        skyLutTexture_ = nullptr;
    }
    skyLutBaked_ = false;
    if (waterNoiseView_) {
        wgpuTextureViewRelease(waterNoiseView_);
        waterNoiseView_ = nullptr;
    }
    if (waterNoiseTexture_) {
        wgpuTextureRelease(waterNoiseTexture_);
        waterNoiseTexture_ = nullptr;
    }
    if (noiseSampler_) {
        wgpuSamplerRelease(noiseSampler_);
        noiseSampler_ = nullptr;
    }

    // Free heap-allocated uniforms
    delete uniforms_;
    uniforms_ = nullptr;
    
    // Note: We don't own texture views, so don't release them
    depthView_ = nullptr;
    shadowView_ = nullptr;
    materialView_ = nullptr;
    terrainView_ = nullptr;
    lightmapView_ = nullptr;
    waterDisplacementView_ = nullptr;
    waterFoamView_ = nullptr;
    waterDisplacementSampler_ = nullptr;
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

    if (!createSkyLut(config)) {
        LOG_ERROR("Failed to create sky LUT resources");
        shutdown();
        return false;
    }

    if (!createWaterNoise()) {
        LOG_ERROR("Failed to create water noise texture");
        shutdown();
        return false;
    }

    LOG_INFO("BlitPath initialized successfully");
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Water Detail Noise Creation
// ─────────────────────────────────────────────────────────────────────────────

namespace {

/// One tile of the water noise texture covers this many world units.
constexpr float kWaterNoiseTileWorld = 1024.0f;
constexpr uint32_t kWaterNoiseSize = 512;

/// Gradient direction on a wrapped lattice (periodic Perlin noise).
glm::vec2 latticeGradient(uint32_t ix, uint32_t iy, uint32_t period, uint32_t seed) {
    uint32_t h = (ix % period) * 374761393u + (iy % period) * 668265263u +
                 seed * 2246822519u;
    h = (h ^ (h >> 13)) * 1274126177u;
    h ^= h >> 16;
    const float angle = static_cast<float>(h & 0xffffu) *
                        (6.2831853f / 65536.0f);
    return {std::cos(angle), std::sin(angle)};
}

/// Periodic Perlin noise, p in lattice units, output roughly [-1, 1].
float periodicPerlin(glm::vec2 p, uint32_t period, uint32_t seed) {
    const glm::vec2 cell = glm::floor(p);
    const glm::vec2 f = p - cell;
    const auto ix = static_cast<uint32_t>(cell.x);
    const auto iy = static_cast<uint32_t>(cell.y);

    const auto corner = [&](uint32_t cx, uint32_t cy) {
        const glm::vec2 g = latticeGradient(ix + cx, iy + cy, period, seed);
        return glm::dot(g, f - glm::vec2(static_cast<float>(cx), static_cast<float>(cy)));
    };

    // Quintic fade for C2-continuous interpolation
    const glm::vec2 u = f * f * f * (f * (f * 6.0f - 15.0f) + 10.0f);
    const float top = corner(0, 0) + (corner(1, 0) - corner(0, 0)) * u.x;
    const float bottom = corner(0, 1) + (corner(1, 1) - corner(0, 1)) * u.x;
    return (top + (bottom - top) * u.y) * 1.6f;
}

} // namespace

bool BlitPath::createWaterNoise() {
    // R,G: gradient (dx, dz) of the three high-frequency detail waves that
    //      used to be evaluated as cosines per pixel. Wave vectors are
    //      snapped to whole periods of the tile so the texture repeats.
    // B:   flow noise (period 8)  — sampled at two scales for the bands.
    // A:   ripple noise (period 16) — drives the shore foam.
    const float base = 6.2831853f / kWaterNoiseTileWorld;
    const glm::vec2 waveK[3] = {
        glm::vec2(10.0f, 4.0f) * base,   // ~ (0.060, 0.022)
        glm::vec2(-3.0f, 8.0f) * base,   // ~ (-0.018, 0.052)
        glm::vec2(6.0f, -7.0f) * base,   // ~ (0.035, -0.041)
    };
    const float wavePhase[3] = {0.0f, 1.7f, 3.1f};

    std::vector<uint8_t> pixels(static_cast<size_t>(kWaterNoiseSize) * kWaterNoiseSize * 4);
    for (uint32_t y = 0; y < kWaterNoiseSize; ++y) {
        for (uint32_t x = 0; x < kWaterNoiseSize; ++x) {
            const glm::vec2 world = {
                (static_cast<float>(x) + 0.5f) / kWaterNoiseSize * kWaterNoiseTileWorld,
                (static_cast<float>(y) + 0.5f) / kWaterNoiseSize * kWaterNoiseTileWorld,
            };

            glm::vec2 grad{0.0f};
            for (int i = 0; i < 3; ++i) {
                grad += waveK[i] * std::cos(glm::dot(waveK[i], world) + wavePhase[i]);
            }

            const glm::vec2 latticeUV = {
                static_cast<float>(x) / kWaterNoiseSize,
                static_cast<float>(y) / kWaterNoiseSize,
            };
            const float flow = periodicPerlin(latticeUV * 8.0f, 8, 101);
            const float ripple = periodicPerlin(latticeUV * 16.0f, 16, 202);

            const auto encode = [](float v) {
                return static_cast<uint8_t>(std::clamp(v, 0.0f, 1.0f) * 255.0f + 0.5f);
            };
            const size_t idx = (static_cast<size_t>(y) * kWaterNoiseSize + x) * 4;
            pixels[idx + 0] = encode(grad.x * 4.0f + 0.5f);   // decode: (v-0.5)/4
            pixels[idx + 1] = encode(grad.y * 4.0f + 0.5f);
            pixels[idx + 2] = encode(flow * 0.5f + 0.5f);     // decode: v*2-1
            pixels[idx + 3] = encode(ripple * 0.5f + 0.5f);
        }
    }

    gpu::TextureDesc desc = gpu::TextureDesc::tex2D(
        kWaterNoiseSize, kWaterNoiseSize, WGPUTextureFormat_RGBA8Unorm,
        WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst,
        "water_noise");
    waterNoiseTexture_ = gpu::createTextureWithData(
        device_, queue_, desc,
        std::as_bytes(std::span<const uint8_t>(pixels)),
        kWaterNoiseSize * 4);
    if (!waterNoiseTexture_) {
        LOG_ERROR("Failed to create water noise texture");
        return false;
    }
    waterNoiseView_ = gpu::createTextureView(waterNoiseTexture_);
    if (!waterNoiseView_) {
        LOG_ERROR("Failed to create water noise texture view");
        return false;
    }

    gpu::SamplerDesc samplerDesc = gpu::SamplerDesc::linear("water_noise_sampler");
    samplerDesc.addressModeU = WGPUAddressMode_Repeat;
    samplerDesc.addressModeV = WGPUAddressMode_Repeat;
    noiseSampler_ = gpu::createSampler(device_, samplerDesc);
    if (!noiseSampler_) {
        LOG_ERROR("Failed to create water noise sampler");
        return false;
    }

    LOG_DEBUG("Created water noise texture ({}x{})", kWaterNoiseSize, kWaterNoiseSize);
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Sky LUT Creation
// ─────────────────────────────────────────────────────────────────────────────

namespace {
/// Resolution of the baked paraboloid sky map. The sky is low-frequency
/// (the sharp sun disc stays analytic in the blit shader), so 512 is plenty.
constexpr uint32_t kSkyLutSize = 512;
} // namespace

bool BlitPath::createSkyLut(const BlitPathConfig& config) {
    // Output texture: storage write for the bake, sampled read for the blit.
    gpu::TextureDesc lutDesc = gpu::TextureDesc::storage(
        kSkyLutSize, kSkyLutSize, WGPUTextureFormat_RGBA16Float, "sky_lut");
    skyLutTexture_ = gpu::createTexture(device_, lutDesc);
    if (!skyLutTexture_) {
        LOG_ERROR("Failed to create sky LUT texture");
        return false;
    }
    skyLutView_ = gpu::createTextureView(skyLutTexture_);
    if (!skyLutView_) {
        LOG_ERROR("Failed to create sky LUT texture view");
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
        gpu::BindGroupEntry(1).textureView(skyLutView_)
    };
    skyLutBindGroup_ =
        gpu::createBindGroup(device_, skyLutBindGroupLayout_, groupEntries, "sky_lut_bind_group");
    if (!skyLutBindGroup_) {
        LOG_ERROR("Failed to create sky LUT bind group");
        return false;
    }

    LOG_DEBUG("Created sky LUT resources ({}x{})", kSkyLutSize, kSkyLutSize);
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
    // @group(0) @binding(3) var materialTex : texture_2d<f32>;
    // @group(0) @binding(4) var terrainTex : texture_2d<f32>;
    // @group(0) @binding(5) var lightmapTex : texture_2d<f32>;
    // @group(0) @binding(6) var terrainSampler : sampler;
    // @group(0) @binding(7) var<uniform> debug : DebugUniforms;
    // @group(0) @binding(8) var skyLUT : texture_2d<f32>;
    // @group(0) @binding(9) var waterNoiseTex : texture_2d<f32>;
    // @group(0) @binding(10) var waterNoiseSampler : sampler;
    // @group(0) @binding(11) var waterDisplacementTex : texture_2d_array<f32>;
    // @group(0) @binding(12) var waterDisplacementSampler : sampler;
    // @group(0) @binding(13) var waterFoamTex : texture_2d<f32>;

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
            .texture(WGPUTextureSampleType_UnfilterableFloat, WGPUTextureViewDimension_2D, false),
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
        gpu::BindGroupLayoutEntry(11)
            .fragmentVisible()
            .texture(WGPUTextureSampleType_Float, WGPUTextureViewDimension_2DArray, false),
        gpu::BindGroupLayoutEntry(12)
            .fragmentVisible()
            .sampler(WGPUSamplerBindingType_Filtering),
        gpu::BindGroupLayoutEntry(13)
            .fragmentVisible()
            .texture(WGPUTextureSampleType_Float, WGPUTextureViewDimension_2D, false)
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
    if (!waterDisplacementView_ || !waterFoamView_ || !waterDisplacementSampler_) {
        LOG_ERROR("Cannot create bind group: no FFT water simulation");
        return false;
    }
    
    // Release old bind group if exists
    if (bindGroup_) {
        wgpuBindGroupRelease(bindGroup_);
        bindGroup_ = nullptr;
    }
    
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
        gpu::BindGroupEntry(9).textureView(waterNoiseView_),
        gpu::BindGroupEntry(10).sampler(noiseSampler_),
        gpu::BindGroupEntry(11).textureView(waterDisplacementView_),
        gpu::BindGroupEntry(12).sampler(waterDisplacementSampler_),
        gpu::BindGroupEntry(13).textureView(waterFoamView_)
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

void BlitPath::setMaterialTexture(WGPUTextureView materialView) {
    materialView_ = materialView;
    bindGroupDirty_ = true;
    LOG_DEBUG("Set material texture view");
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

void BlitPath::setWaterSimulation(WGPUTextureView displacementView,
                                  WGPUTextureView foamView,
                                  WGPUSampler sampler) {
    waterDisplacementView_ = displacementView;
    waterFoamView_ = foamView;
    waterDisplacementSampler_ = sampler;
    bindGroupDirty_ = true;
    LOG_DEBUG("Set FFT water displacement cascades");
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
        skyLutBaked_ = true;
        LOG_DEBUG("Baked sky LUT");
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
