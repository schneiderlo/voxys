// ═══════════════════════════════════════════════════════════════════════════════
// mesh_path.cpp - RIDGEBREAK static VMESH PBR renderer
// ═══════════════════════════════════════════════════════════════════════════════

#include "gpu/pipeline.hpp"
#include "render/mesh_path.hpp"

#include "core/log.hpp"
#include "gpu/resources.hpp"
#include "gpu/webgpu_compat.hpp"
#include "render/primitive_path.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <fstream>
#include <limits>
#include <span>
#include <string>
#include <vector>
#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>

namespace voxy::render {
namespace {

constexpr std::streamoff kMaximumVmeshFileBytes =
    512ll * 1024ll * 1024ll;
constexpr uint32_t kMaximumRenderExtent = 8'192u;

SunShadowUniforms sunShadowFrame(const glm::vec3& camera, const glm::vec3& direction,
    const glm::vec3& worldOrigin, bool farEnabled) {
    const auto light = glm::normalize(direction);
    const auto up = std::abs(light.y) > 0.95f ? glm::vec3(0,0,1) : glm::vec3(0,1,0);
    const auto projection = [&](float halfWidth, float depthRange, uint32_t resolution) {
        auto matrix = glm::orthoLH_ZO(-halfWidth, halfWidth, -halfWidth, halfWidth, 0.0f, depthRange)
            * glm::lookAtLH(camera + light * (depthRange * 0.5f), camera, up);
        const auto origin = matrix * glm::vec4(0,0,0,1);
        const float scale = float(resolution) * 0.5f;
        matrix[3].x += (std::round(origin.x * scale) - origin.x * scale) / scale;
        matrix[3].y += (std::round(origin.y * scale) - origin.y * scale) / scale;
        return matrix;
    };
    SunShadowUniforms result;
    result.viewProj = projection(24.0f,128.0f,MeshPath::sunShadowResolution);
    result.params = {1,48.0f / MeshPath::sunShadowResolution,1.0f / 128.0f,0};
    result.worldOrigin = glm::vec4(worldOrigin,0);
    // Architectural self-shadow and ground contact must survive village views.
    // Keep the near texel footprint unchanged for studs, hands and wheel contact.
    result.farViewProj = projection(192.0f,768.0f,MeshPath::farSunShadowResolution);
    result.farParams = {farEnabled ? 1.0f : 0.0f,384.0f / MeshPath::farSunShadowResolution,1.0f / 768.0f,0};
    return result;
}

struct alignas(16) MeshUniforms {
    glm::mat4 viewProj{1.0f};
    glm::vec4 cameraPosition{0.0f};
    glm::vec4 lightDirectionFogDensity{0.0f, 1.0f, 0.0f, 0.0f};
    glm::vec4 sunColorIntensity{1.0f};
    glm::vec4 ambientColorIntensity{0.0f};
    glm::vec4 fogColorExposure{0.0f};
    glm::vec4 environmentParams{0.0f};
};

struct alignas(16) GpuMaterial {
    glm::vec4 baseColorFactor{1.0f};
    glm::vec4 emissiveFactorAlphaCutoff{0.0f};
    glm::vec4 metallicRoughnessNormalOcclusion{0.0f, 1.0f, 1.0f, 1.0f};
    glm::uvec4 flags{0u};
};

struct alignas(16) GpuDrawInstance {
    glm::mat4 modelMatrix{1.0f};
    glm::vec4 tintColor{1.0f};
    float emissiveBoost = 0.0f;
    uint32_t materialIndex = 0u;
    uint32_t padding[2] = {};
    glm::vec4 baseColorOverride{0.0f};
    glm::vec4 surface{0.0f};
};

static_assert(sizeof(MeshUniforms) == 160u);
static_assert(alignof(MeshUniforms) == 16u);
static_assert(offsetof(MeshUniforms, viewProj) == 0u);
static_assert(offsetof(MeshUniforms, cameraPosition) == 64u);
static_assert(offsetof(MeshUniforms, lightDirectionFogDensity) == 80u);
static_assert(offsetof(MeshUniforms, sunColorIntensity) == 96u);
static_assert(offsetof(MeshUniforms, ambientColorIntensity) == 112u);
static_assert(offsetof(MeshUniforms, fogColorExposure) == 128u);
static_assert(offsetof(MeshUniforms, environmentParams) == 144u);
static_assert(sizeof(GpuMaterial) == 64u);
static_assert(alignof(GpuMaterial) == 16u);
static_assert(offsetof(GpuMaterial, baseColorFactor) == 0u);
static_assert(offsetof(GpuMaterial, emissiveFactorAlphaCutoff) == 16u);
static_assert(offsetof(GpuMaterial, metallicRoughnessNormalOcclusion) == 32u);
static_assert(offsetof(GpuMaterial, flags) == 48u);
static_assert(sizeof(GpuDrawInstance) == MeshPath::gpuInstanceBytes);
static_assert(alignof(GpuDrawInstance) == 16u);
static_assert(offsetof(GpuDrawInstance, modelMatrix) == 0u);
static_assert(offsetof(GpuDrawInstance, tintColor) == 64u);
static_assert(offsetof(GpuDrawInstance, emissiveBoost) == 80u);
static_assert(offsetof(GpuDrawInstance, materialIndex) == 84u);
static_assert(offsetof(GpuDrawInstance, padding) == 88u);
static_assert(offsetof(GpuDrawInstance, baseColorOverride) == 96u);
static_assert(offsetof(GpuDrawInstance, surface) == 112u);
static_assert(sizeof(moto::VmeshVertex) == 72u);

struct PendingDraw {
    uint32_t assetIndex = 0u;
    uint32_t submeshIndex = 0u;
    uint32_t firstInstance = 0u;
    uint32_t materialIndex = 0u;
    uint8_t alphaMode = moto::VmeshAlphaOpaque;
    float distanceSquared = 0.0f;
    bool castsSunShadow = true;
    uint32_t instanceCount = 1u;
    uint8_t visibility = 7u; // Color, near shadow, far shadow.
};

[[nodiscard]] bool finiteFloat(float value) noexcept {
    return std::isfinite(value);
}

[[nodiscard]] bool finiteVec3(const glm::vec3& value) noexcept {
    return finiteFloat(value.x) && finiteFloat(value.y)
        && finiteFloat(value.z);
}

[[nodiscard]] bool finiteVec4(const glm::vec4& value) noexcept {
    return finiteFloat(value.x) && finiteFloat(value.y)
        && finiteFloat(value.z) && finiteFloat(value.w);
}

[[nodiscard]] bool finiteMatrix(const glm::mat4& value) noexcept {
    for (glm::length_t column = 0; column < 4; ++column) {
        for (glm::length_t row = 0; row < 4; ++row) {
            if (!finiteFloat(value[column][row])) return false;
        }
    }
    return true;
}

[[nodiscard]] bool validModelMatrix(const glm::mat4& value) noexcept {
    if (!finiteMatrix(value)
        || std::abs(value[0][3]) > 1.0e-5f
        || std::abs(value[1][3]) > 1.0e-5f
        || std::abs(value[2][3]) > 1.0e-5f
        || std::abs(value[3][3] - 1.0f) > 1.0e-5f) {
        return false;
    }
    const float determinant = glm::determinant(glm::mat3(value));
    return finiteFloat(determinant) && std::abs(determinant) > 1.0e-12f;
}

[[nodiscard]] bool validLighting(const PrimitiveLighting& lighting) noexcept {
    const float directionLengthSquared =
        glm::dot(lighting.direction, lighting.direction);
    return finiteVec3(lighting.direction)
        && finiteFloat(directionLengthSquared)
        && directionLengthSquared > std::numeric_limits<float>::min()
        && finiteVec3(lighting.sunColor)
        && finiteFloat(lighting.sunIntensity)
        && finiteVec3(lighting.ambientColor)
        && finiteFloat(lighting.ambientIntensity)
        && finiteVec3(lighting.fogColor)
        && finiteFloat(lighting.fogDensity)
        && finiteFloat(lighting.exposure)
        && finiteFloat(lighting.dayNightHour);
}

[[nodiscard]] bool validMaterial(const moto::VmeshMaterial& material) noexcept {
    for (float factor : material.baseColorFactor) {
        if (!finiteFloat(factor)) return false;
    }
    for (float factor : material.emissiveFactor) {
        if (!finiteFloat(factor)) return false;
    }
    return finiteFloat(material.metallicFactor)
        && finiteFloat(material.roughnessFactor)
        && finiteFloat(material.normalScale)
        && finiteFloat(material.occlusionStrength)
        && finiteFloat(material.alphaCutoff)
        && material.alphaMode <= moto::VmeshAlphaBlend
        && material.doubleSided <= 1u && material.unlit <= 1u;
}

[[nodiscard]] bool boundsVisible(const glm::mat4& localToClip,
                                 const glm::vec3& minimum,
                                 const glm::vec3& maximum) noexcept {
    std::array<glm::vec4, 8> corners{};
    size_t corner = 0u;
    for (uint32_t z = 0u; z < 2u; ++z) {
        for (uint32_t y = 0u; y < 2u; ++y) {
            for (uint32_t x = 0u; x < 2u; ++x) {
                corners[corner++] = localToClip * glm::vec4(
                    x ? maximum.x : minimum.x,
                    y ? maximum.y : minimum.y,
                    z ? maximum.z : minimum.z, 1.0f);
            }
        }
    }
    const auto allOutside = [&](auto predicate) {
        return std::all_of(corners.begin(), corners.end(), predicate);
    };
    // WebGPU clip space is -w..w in X/Y and 0..w in Z.
    return !allOutside([](const glm::vec4& p) { return p.x < -p.w; })
        && !allOutside([](const glm::vec4& p) { return p.x > p.w; })
        && !allOutside([](const glm::vec4& p) { return p.y < -p.w; })
        && !allOutside([](const glm::vec4& p) { return p.y > p.w; })
        && !allOutside([](const glm::vec4& p) { return p.z < 0.0f; })
        && !allOutside([](const glm::vec4& p) { return p.z > p.w; });
}

[[nodiscard]] float srgbToLinear(float value) noexcept {
    return value <= 0.04045f ? value / 12.92f
                             : std::pow((value + 0.055f) / 1.055f, 2.4f);
}

[[nodiscard]] float linearToSrgb(float value) noexcept {
    value = std::clamp(value, 0.0f, 1.0f);
    return value <= 0.0031308f ? value * 12.92f
                              : 1.055f * std::pow(value, 1.0f / 2.4f) - 0.055f;
}

[[nodiscard]] std::vector<uint8_t> downsampleRgba(
    std::span<const uint8_t> source, uint32_t width, uint32_t height,
    moto::VmeshTextureSlot slot, bool srgb) {
    const uint32_t nextWidth = std::max(width / 2u, 1u);
    const uint32_t nextHeight = std::max(height / 2u, 1u);
    std::vector<uint8_t> result(
        static_cast<size_t>(nextWidth) * nextHeight * 4u);
    for (uint32_t y = 0u; y < nextHeight; ++y) {
        for (uint32_t x = 0u; x < nextWidth; ++x) {
            glm::vec4 sum(0.0f);
            for (uint32_t oy = 0u; oy < 2u; ++oy) {
                for (uint32_t ox = 0u; ox < 2u; ++ox) {
                    const uint32_t sx = std::min(x * 2u + ox, width - 1u);
                    const uint32_t sy = std::min(y * 2u + oy, height - 1u);
                    const size_t offset =
                        (static_cast<size_t>(sy) * width + sx) * 4u;
                    glm::vec4 sample(
                        source[offset + 0u] / 255.0f,
                        source[offset + 1u] / 255.0f,
                        source[offset + 2u] / 255.0f,
                        source[offset + 3u] / 255.0f);
                    if (srgb) {
                        sample.r = srgbToLinear(sample.r);
                        sample.g = srgbToLinear(sample.g);
                        sample.b = srgbToLinear(sample.b);
                    }
                    sum += sample;
                }
            }
            glm::vec4 value = sum * 0.25f;
            if (slot == moto::VmeshTextureNormal) {
                glm::vec3 normal = glm::vec3(value) * 2.0f - 1.0f;
                const float lengthSquared = glm::dot(normal, normal);
                normal = lengthSquared > 1.0e-12f
                    ? normal * glm::inversesqrt(lengthSquared)
                    : glm::vec3(0.0f, 0.0f, 1.0f);
                value = glm::vec4(normal * 0.5f + 0.5f, value.a);
            } else if (srgb) {
                value.r = linearToSrgb(value.r);
                value.g = linearToSrgb(value.g);
                value.b = linearToSrgb(value.b);
            }
            const size_t outputOffset =
                (static_cast<size_t>(y) * nextWidth + x) * 4u;
            for (uint32_t channel = 0u; channel < 4u; ++channel) {
                const auto component = static_cast<glm::length_t>(channel);
                result[outputOffset + channel] = static_cast<uint8_t>(
                    std::lround(std::clamp(value[component], 0.0f, 1.0f)
                                * 255.0f));
            }
        }
    }
    return result;
}

[[nodiscard]] WGPUTexture createMaterialTexture(
    WGPUDevice device, WGPUQueue queue, const moto::VmeshData& data,
    const moto::VmeshMaterial& material, moto::VmeshTextureSlot slot) {
    if (material.hasTexture[slot] == 0u) return nullptr;
    const uint32_t width = material.textureWidth[slot];
    const uint32_t height = material.textureHeight[slot];
    const uint32_t offset = material.textureOffset[slot];
    const uint32_t size = material.textureSize[slot];
    const bool srgb = material.textureIsSrgb[slot] != 0u;
    gpu::TextureDesc descriptor = gpu::TextureDesc::tex2DMipmapped(
        width, height,
        srgb ? WGPUTextureFormat_RGBA8UnormSrgb
             : WGPUTextureFormat_RGBA8Unorm,
        WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst,
        "mesh_path_material_texture");
    WGPUTexture texture = gpu::createTexture(device, descriptor);
    if (!texture) return nullptr;
    struct TextureGuard {
        WGPUTexture texture;
        bool published = false;
        ~TextureGuard() {
            if (!published) {
                // Earlier mip writes may still be queued. Release ownership
                // without invalidating the queue's internal references.
                wgpuTextureRelease(texture);
            }
        }
    } textureGuard{texture};
    const size_t imageOffset = static_cast<size_t>(offset);
    const size_t imageSize = static_cast<size_t>(size);
    std::vector<uint8_t> level(
        data.images.data() + imageOffset,
        data.images.data() + imageOffset + imageSize);
    uint32_t levelWidth = width;
    uint32_t levelHeight = height;
    for (uint32_t mip = 0u; mip < descriptor.mipLevelCount; ++mip) {
        if (!gpu::writeTexture(
                queue, texture, std::as_bytes(std::span(level)), levelWidth,
                levelHeight, levelWidth * 4u, mip)) {
            return nullptr;
        }
        if (mip + 1u < descriptor.mipLevelCount) {
            level = downsampleRgba(level, levelWidth, levelHeight, slot, srgb);
            levelWidth = std::max(levelWidth / 2u, 1u);
            levelHeight = std::max(levelHeight / 2u, 1u);
        }
    }
    textureGuard.published = true;
    return texture;
}

[[nodiscard]] WGPUTextureView selectedEnvironment(
    WGPUTextureView environment, WGPUTextureView fallback) noexcept {
    return environment ? environment : fallback;
}

void releaseBuffer(WGPUBuffer& buffer) noexcept {
    if (!buffer) return;
    wgpuBufferRelease(buffer);
    buffer = nullptr;
}

template<typename Asset>
void releaseAsset(Asset& asset, bool destroyResources = true) noexcept {
    for (auto& material : asset.materials) {
        if (material.bindGroup) {
            wgpuBindGroupRelease(material.bindGroup);
            material.bindGroup = nullptr;
        }
        for (WGPUTextureView& view : material.views) {
            if (view) wgpuTextureViewRelease(view);
            view = nullptr;
        }
        for (WGPUTexture& texture : material.textures) {
            if (texture) {
                if (destroyResources) wgpuTextureDestroy(texture);
                wgpuTextureRelease(texture);
            }
            texture = nullptr;
        }
    }
    asset.materials.clear();
    releaseBuffer(asset.materialBuffer);
    releaseBuffer(asset.indexBuffer);
    releaseBuffer(asset.vertexBuffer);
}

}  // namespace

struct MeshPath::StaticCache {
    struct Record { uint32_t batch; };
    std::vector<Record> records;
    std::vector<std::pair<uint32_t,uint32_t>> ranges;
    std::vector<PendingDraw> templates,draws;
    std::vector<std::vector<uint32_t>> buckets;
    std::vector<uint32_t> selected;
    std::vector<GpuDrawInstance> resident;
    std::vector<uint32_t> packed;
    bool upload=true,selectionUpload=true;
};

struct MeshPath::FrameScratch {
    std::vector<GpuDrawInstance> gpuInstances, sortedInstances;
    std::vector<PendingDraw> draws, batches;
    std::vector<uint32_t> liveIndices;
};

uint64_t MeshPath::frameScratchCapacityBytes() const noexcept {
    if (!frameScratch_) return 0;
    const auto& scratch = *frameScratch_;
    return (scratch.gpuInstances.capacity() + scratch.sortedInstances.capacity()) * sizeof(GpuDrawInstance)
        + (scratch.draws.capacity() + scratch.batches.capacity()) * sizeof(PendingDraw)
        + scratch.liveIndices.capacity() * sizeof(uint32_t);
}

glm::vec4 opaqueSrgbPaintOverride(const std::array<uint8_t, 4>& rgba) noexcept {
    return {srgbToLinear(float(rgba[0]) / 255.0f),
        srgbToLinear(float(rgba[1]) / 255.0f),
        srgbToLinear(float(rgba[2]) / 255.0f), 1.0f};
}

MeshPath::~MeshPath() { shutdown(); }

bool MeshPath::init(WGPUDevice device, WGPUQueue queue,
                    const MeshPathConfig& config) {
    shutdown();
    if (!device || !queue || config.maxInstances == 0u
        || config.maxDrawsPerFrame == 0u
        || (config.linearHdrOutput && config.colorFormat != WGPUTextureFormat_RGBA16Float)
        || (config.frontFace != WGPUFrontFace_CCW && config.frontFace != WGPUFrontFace_CW)
        || config.maxInstances
            > std::numeric_limits<size_t>::max() / sizeof(GpuDrawInstance)) {
        return false;
    }

    device_ = device;
    queue_ = queue;
    colorFormat_ = config.colorFormat;
    linearHdrOutput_ = config.linearHdrOutput;
    toySkyGroundFill_ = config.toySkyGroundFill;
    depthFormat_ = config.depthFormat;
    maxDrawsPerFrame_ = config.maxDrawsPerFrame;
    sunShadows_ = config.sunShadows;
    farSunShadows_ = config.sunShadows && config.farSunShadows;
    instanceCapacity_ = config.maxInstances;

    uniformBuffer_ = gpu::createBuffer(
        device_, gpu::BufferDesc::uniform(
                     sizeof(MeshUniforms), "mesh_path_uniforms"));
    instanceBuffer_ = gpu::createBuffer(
        device_, gpu::BufferDesc::storage(
                     instanceCapacity_ * sizeof(GpuDrawInstance), true,
                     "mesh_path_draw_instances"));
    instanceIndexBuffer_=gpu::createBuffer(device_,gpu::BufferDesc::storage(
        instanceCapacity_*sizeof(uint32_t),true,"mesh_path_instance_indices"));
    gpu::SamplerDesc samplerDesc =
        gpu::SamplerDesc::linear("mesh_path_environment_sampler");
    samplerDesc.addressModeU = WGPUAddressMode_Repeat;
    sampler_ = gpu::createSampler(device_, samplerDesc);
    filteredSampler_ = gpu::createSampler(device_, gpu::SamplerDesc::linear("mesh_filtered_environment_sampler"));
    gpu::SamplerDesc materialSamplerDesc =
        gpu::SamplerDesc::linear("mesh_path_material_sampler");
    materialSamplerDesc.addressModeU = WGPUAddressMode_Repeat;
    materialSamplerDesc.addressModeV = WGPUAddressMode_Repeat;
    materialSamplerDesc.maxAnisotropy = 4u;
    materialSampler_ = gpu::createSampler(device_, materialSamplerDesc);

    const std::array<uint8_t, 4> fallbackPixel = {255u, 255u, 255u, 255u};
    const gpu::TextureDesc fallbackDesc = gpu::TextureDesc::tex2D(
        1u, 1u, WGPUTextureFormat_RGBA8Unorm,
        WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst,
        "mesh_path_fallback_environment");
    fallbackEnvironmentTexture_ = gpu::createTextureWithData(
        device_, queue_, fallbackDesc,
        std::as_bytes(std::span<const uint8_t>(fallbackPixel)), 4u);
    if (fallbackEnvironmentTexture_) {
        gpu::TextureViewDesc viewDesc{};
        viewDesc.label = "mesh_path_fallback_environment_view";
        fallbackEnvironmentView_ =
            gpu::createTextureView(fallbackEnvironmentTexture_, viewDesc);
    }
    auto cubeDesc = fallbackDesc;
    cubeDesc.label = "mesh_path_fallback_cube";
    cubeDesc.depthOrArrayLayers = 6;
    fallbackCubeTexture_ = gpu::createTexture(device_, cubeDesc);
    if (fallbackCubeTexture_) {
        // WebGPU initializes this dormant binding to zero. Legacy shading
        // never samples it; filtered shading binds the fully baked cubes.
        fallbackCubeView_ = gpu::createTextureView(fallbackCubeTexture_, {
            .dimension = WGPUTextureViewDimension_Cube, .arrayLayerCount = 6});
    }
    if (config.filteredEnvironment) {
        EnvironmentLightingConfig environmentConfig;
        environmentConfig.shaderPath = config.shaderPath.parent_path() / "environment_lighting.wgsl";
        if (!filteredEnvironment_.init(device_, queue_, environmentConfig)
            || filteredEnvironment_.requestedBytes() != filteredEnvironmentReservationBytes) {
            releaseHandles();
            return false;
        }
    }

    const float fallbackDepth = -1.0f;
    const gpu::TextureDesc fallbackDepthDesc = gpu::TextureDesc::tex2D(
        1u, 1u, WGPUTextureFormat_R32Float,
        WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst,
        "mesh_path_fallback_ray_depth");
    fallbackRayDepthTexture_ = gpu::createTextureWithData(
        device_, queue_, fallbackDepthDesc,
        std::as_bytes(std::span<const float>(&fallbackDepth, 1u)),
        sizeof(float));
    if (fallbackRayDepthTexture_) {
        fallbackRayDepthView_ = gpu::createTextureView(fallbackRayDepthTexture_);
    }

    if (!uniformBuffer_ || !instanceBuffer_ || !instanceIndexBuffer_ || !sampler_ || !materialSampler_
        || !filteredSampler_ || !fallbackCubeTexture_ || !fallbackCubeView_
        || !fallbackEnvironmentTexture_ || !fallbackEnvironmentView_
        || !fallbackRayDepthTexture_ || !fallbackRayDepthView_
        || !createPipeline(config)) {
        releaseHandles();
        return false;
    }
    boundEnvironmentView_ = fallbackEnvironmentView_;
    boundRayDepthView_ = fallbackRayDepthView_;
    instancesValid_ = false;
    return true;
}

void MeshPath::shutdown() {
    teardown(true);
}

void MeshPath::releaseHandles() {
    teardown(false);
}

void MeshPath::teardown(bool destroyResources) {
    frameScratch_.reset();
    lastSubmeshVisitCount_ = 0;
    staticCache_.reset();
    for (GpuAsset& asset : assets_) releaseAsset(asset, destroyResources);
    assets_.clear();
    instances_.clear();

    if (sunCasterPipeline_) { wgpuRenderPipelineRelease(sunCasterPipeline_); sunCasterPipeline_ = nullptr; }
    if (sunCasterCulledPipeline_) { wgpuRenderPipelineRelease(sunCasterCulledPipeline_); sunCasterCulledPipeline_ = nullptr; }
    if (sunCasterPipelineLayout_) { wgpuPipelineLayoutRelease(sunCasterPipelineLayout_); sunCasterPipelineLayout_ = nullptr; }
    if (sunShadowBinding_) { wgpuBindGroupRelease(sunShadowBinding_); sunShadowBinding_ = nullptr; }
    if (farSunCasterBinding_) { wgpuBindGroupRelease(farSunCasterBinding_); farSunCasterBinding_ = nullptr; }
    if (sunCasterBinding_) { wgpuBindGroupRelease(sunCasterBinding_); sunCasterBinding_ = nullptr; }
    if (sunShadowLayout_) { wgpuBindGroupLayoutRelease(sunShadowLayout_); sunShadowLayout_ = nullptr; }
    if (sunCasterLayout_) { wgpuBindGroupLayoutRelease(sunCasterLayout_); sunCasterLayout_ = nullptr; }
    if (sunShadowSampler_) { wgpuSamplerRelease(sunShadowSampler_); sunShadowSampler_ = nullptr; }
    if (sunShadowView_) { wgpuTextureViewRelease(sunShadowView_); sunShadowView_ = nullptr; }
    if (sunShadowTexture_) {
        if (destroyResources) wgpuTextureDestroy(sunShadowTexture_);
        wgpuTextureRelease(sunShadowTexture_); sunShadowTexture_ = nullptr;
    }
    if (farSunShadowUniform_) {
        if (destroyResources) wgpuBufferDestroy(farSunShadowUniform_);
        wgpuBufferRelease(farSunShadowUniform_); farSunShadowUniform_ = nullptr;
    }
    if (sunShadowUniform_) {
        if (destroyResources) wgpuBufferDestroy(sunShadowUniform_);
        wgpuBufferRelease(sunShadowUniform_); sunShadowUniform_ = nullptr;
    }
    sunShadows_ = false; farSunShadows_ = false; toySkyGroundFill_ = false;

    if (blendPipeline_) {
        wgpuRenderPipelineRelease(blendPipeline_);
        blendPipeline_ = nullptr;
    }
    if (opaqueCulledPipeline_) {
        wgpuRenderPipelineRelease(opaqueCulledPipeline_);
        opaqueCulledPipeline_ = nullptr;
    }
    if (opaquePipeline_) {
        wgpuRenderPipelineRelease(opaquePipeline_);
        opaquePipeline_ = nullptr;
    }
    if (pipelineLayout_) {
        wgpuPipelineLayoutRelease(pipelineLayout_);
        pipelineLayout_ = nullptr;
    }
    if (bindGroupLayout_) {
        wgpuBindGroupLayoutRelease(bindGroupLayout_);
        bindGroupLayout_ = nullptr;
    }
    if (shaderModule_) {
        wgpuShaderModuleRelease(shaderModule_);
        shaderModule_ = nullptr;
    }
    if (sampler_) {
        wgpuSamplerRelease(sampler_);
        sampler_ = nullptr;
    }
    if (materialSampler_) {
        wgpuSamplerRelease(materialSampler_);
        materialSampler_ = nullptr;
    }
    if (filteredSampler_) { wgpuSamplerRelease(filteredSampler_); filteredSampler_ = nullptr; }
    filteredEnvironment_.releaseHandles();
    filteredEnvironmentReady_ = false;
    filteredEnvironmentEncoded_ = false;
    environmentBakeCount_ = 0;
    if (fallbackCubeView_) { wgpuTextureViewRelease(fallbackCubeView_); fallbackCubeView_ = nullptr; }
    if (fallbackCubeTexture_) {
        if (destroyResources) wgpuTextureDestroy(fallbackCubeTexture_);
        wgpuTextureRelease(fallbackCubeTexture_);
        fallbackCubeTexture_ = nullptr;
    }
    if (fallbackEnvironmentView_) {
        wgpuTextureViewRelease(fallbackEnvironmentView_);
        fallbackEnvironmentView_ = nullptr;
    }
    if (fallbackEnvironmentTexture_) {
        if (destroyResources) wgpuTextureDestroy(fallbackEnvironmentTexture_);
        wgpuTextureRelease(fallbackEnvironmentTexture_);
        fallbackEnvironmentTexture_ = nullptr;
    }
    if (fallbackRayDepthView_) {
        wgpuTextureViewRelease(fallbackRayDepthView_);
        fallbackRayDepthView_ = nullptr;
    }
    if (fallbackRayDepthTexture_) {
        if (destroyResources) wgpuTextureDestroy(fallbackRayDepthTexture_);
        wgpuTextureRelease(fallbackRayDepthTexture_);
        fallbackRayDepthTexture_ = nullptr;
    }
    releaseBuffer(instanceBuffer_);
    releaseBuffer(instanceIndexBuffer_);
    releaseBuffer(uniformBuffer_);
    if (bodyBinding_) { wgpuBindGroupRelease(bodyBinding_); bodyBinding_=nullptr; }
    if (bodyLayout_) { wgpuBindGroupLayoutRelease(bodyLayout_); bodyLayout_=nullptr; }
    releaseBuffer(bodyFallback_); releaseBuffer(bodyCamera_);

    device_ = nullptr;
    queue_ = nullptr;
    environmentView_ = nullptr;
    boundEnvironmentView_ = nullptr;
    rayDepthView_ = nullptr;
    boundRayDepthView_ = nullptr;
    instanceCapacity_ = 0u;
    maxDrawsPerFrame_ = 512u;
    colorFormat_ = WGPUTextureFormat_BGRA8Unorm;
    depthFormat_ = WGPUTextureFormat_Depth32Float;
    instancesValid_ = false;
    lastSubmittedDrawCount_ = 0u;
    lastCulledInstanceCount_ = 0u;
}

bool MeshPath::createPipeline(const MeshPathConfig& config) {
    std::array<gpu::BindGroupLayoutEntry, 16> entries = {
        gpu::BindGroupLayoutEntry(0).vertexVisible().fragmentVisible()
            .uniformBuffer(false, sizeof(MeshUniforms)),
        gpu::BindGroupLayoutEntry(1).vertexVisible()
            .storageBuffer(true, false, sizeof(GpuDrawInstance)),
        gpu::BindGroupLayoutEntry(2).fragmentVisible()
            .storageBuffer(true, false, sizeof(GpuMaterial)),
        gpu::BindGroupLayoutEntry(3).fragmentVisible()
            .texture(WGPUTextureSampleType_Float,
                     WGPUTextureViewDimension_2D, false),
        gpu::BindGroupLayoutEntry(4).fragmentVisible()
            .sampler(WGPUSamplerBindingType_Filtering),
        gpu::BindGroupLayoutEntry(5).fragmentVisible()
            .texture(WGPUTextureSampleType_UnfilterableFloat,
                     WGPUTextureViewDimension_2D, false),
        gpu::BindGroupLayoutEntry(6).fragmentVisible()
            .texture(WGPUTextureSampleType_Float,
                     WGPUTextureViewDimension_2D, false),
        gpu::BindGroupLayoutEntry(7).fragmentVisible()
            .texture(WGPUTextureSampleType_Float,
                     WGPUTextureViewDimension_2D, false),
        gpu::BindGroupLayoutEntry(8).fragmentVisible()
            .texture(WGPUTextureSampleType_Float,
                     WGPUTextureViewDimension_2D, false),
        gpu::BindGroupLayoutEntry(9).fragmentVisible()
            .texture(WGPUTextureSampleType_Float,
                     WGPUTextureViewDimension_2D, false),
        gpu::BindGroupLayoutEntry(10).fragmentVisible()
            .sampler(WGPUSamplerBindingType_Filtering),
        gpu::BindGroupLayoutEntry(11).fragmentVisible().texture(WGPUTextureSampleType_Float, WGPUTextureViewDimension_Cube),
        gpu::BindGroupLayoutEntry(12).fragmentVisible().texture(WGPUTextureSampleType_Float, WGPUTextureViewDimension_Cube),
        gpu::BindGroupLayoutEntry(13).fragmentVisible().texture(),
        gpu::BindGroupLayoutEntry(14).fragmentVisible().sampler(),
        gpu::BindGroupLayoutEntry(15).vertexVisible().storageBuffer(true,false,sizeof(uint32_t)),
    };
    bindGroupLayout_ = gpu::createBindGroupLayout(
        device_, entries, "mesh_path_bind_group_layout");
    if (!bindGroupLayout_) return false;

    using LE=gpu::BindGroupLayoutEntry;
    const std::array bodyEntries{LE(0).vertexVisible().storageBuffer(true),LE(1).vertexVisible().storageBuffer(true),
        LE(2).vertexVisible().storageBuffer(true),LE(3).vertexVisible().storageBuffer(true),
        LE(4).vertexVisible().uniformBuffer(false,32)};
    bodyLayout_=gpu::createBindGroupLayout(device_,bodyEntries,"mesh_body_layout");
    bodyFallback_=gpu::createBuffer(device_,gpu::BufferDesc::storage(64,true,"mesh_body_empty"));
    bodyCamera_=gpu::createBuffer(device_,gpu::BufferDesc::uniform(32,"mesh_body_camera"));
    if (!bodyLayout_ || !bodyFallback_ || !bodyCamera_ || !setAuthoredBodyView({},{})) return false;
    const auto shadowEntries = sceneShadowLayoutEntries();
    sunShadowLayout_ = gpu::createBindGroupLayout(device_,shadowEntries,"mesh_sun_receiver_layout");
    sunCasterLayout_ = gpu::createBindGroupLayout(device_,std::span(shadowEntries).first(1),"mesh_sun_caster_layout");
    sunShadowUniform_ = gpu::createBuffer(device_,gpu::BufferDesc::uniform(sizeof(SunShadowUniforms),"mesh_sun_uniform"));
    farSunShadowUniform_ = gpu::createBuffer(device_,gpu::BufferDesc::uniform(sizeof(SunShadowUniforms),"mesh_far_sun_uniform"));
    const uint32_t shadowWidth = sunShadows_ ? sunShadowResolution + (farSunShadows_ ? farSunShadowResolution : 0u) : 1u;
    const uint32_t shadowHeight = sunShadows_ ? (farSunShadows_ ? farSunShadowResolution : sunShadowResolution) : 1u;
    sunShadowTexture_ = gpu::createTexture(device_,gpu::TextureDesc::depth(shadowWidth,shadowHeight,
        WGPUTextureFormat_Depth32Float,"mesh_live_sun_depth"));
    if (sunShadowTexture_) sunShadowView_ = gpu::createTextureView(sunShadowTexture_);
    sunShadowSampler_ = gpu::createSampler(device_,gpu::SamplerDesc::comparison(WGPUCompareFunction_LessEqual,"mesh_sun_pcf"));
    if (!sunShadowLayout_ || !sunCasterLayout_ || !sunShadowUniform_ || !farSunShadowUniform_ || !sunShadowView_ || !sunShadowSampler_) return false;
    using BE = gpu::BindGroupEntry;
    const std::array shadowBindings{BE(0).buffer(sunShadowUniform_),BE(1).textureView(sunShadowView_),BE(2).sampler(sunShadowSampler_)};
    sunShadowBinding_ = gpu::createBindGroup(device_,sunShadowLayout_,shadowBindings,"mesh_sun_receiver");
    sunCasterBinding_ = gpu::createBindGroup(device_,sunCasterLayout_,std::span(shadowBindings).first(1),"mesh_sun_caster");
    const std::array farCasterBindings{BE(0).buffer(farSunShadowUniform_)};
    farSunCasterBinding_ = gpu::createBindGroup(device_,sunCasterLayout_,farCasterBindings,"mesh_far_sun_caster");
    if (!sunShadowBinding_ || !sunCasterBinding_ || !farSunCasterBinding_) return false;
    const std::array<WGPUBindGroupLayout, 3> layouts = {bindGroupLayout_,bodyLayout_,sunShadowLayout_};
    pipelineLayout_ = gpu::createPipelineLayout(
        device_, layouts, "mesh_path_pipeline_layout");
    shaderModule_ = gpu::loadShaderModule(
        device_, config.shaderPath, "mesh_path.wgsl");
    if (!pipelineLayout_ || !shaderModule_) return false;

    std::array<WGPUVertexAttribute, 6> attributes{};
    attributes[0].format = WGPUVertexFormat_Float32x3;
    attributes[0].offset = 0u;
    attributes[0].shaderLocation = 0u;
    attributes[1].format = WGPUVertexFormat_Float32x3;
    attributes[1].offset = 12u;
    attributes[1].shaderLocation = 1u;
    attributes[2].format = WGPUVertexFormat_Float32x4;
    attributes[2].offset = 24u;
    attributes[2].shaderLocation = 2u;
    attributes[3].format = WGPUVertexFormat_Float32x2;
    attributes[3].offset = 40u;
    attributes[3].shaderLocation = 3u;
    attributes[4].format = WGPUVertexFormat_Uint16x4;
    attributes[4].offset = 48u;
    attributes[4].shaderLocation = 4u;
    attributes[5].format = WGPUVertexFormat_Float32x4;
    attributes[5].offset = 56u;
    attributes[5].shaderLocation = 5u;

    WGPUVertexBufferLayout vertexBufferLayout{};
    vertexBufferLayout.arrayStride = sizeof(moto::VmeshVertex);
    vertexBufferLayout.stepMode = WGPUVertexStepMode_Vertex;
    vertexBufferLayout.attributeCount = attributes.size();
    vertexBufferLayout.attributes = attributes.data();

    WGPUVertexState vertexState{};
    vertexState.module = shaderModule_;
    WGPU_SET_ENTRY_POINT(vertexState, "vs");
    vertexState.bufferCount = 1u;
    vertexState.buffers = &vertexBufferLayout;

    std::array<WGPUColorTargetState, 2> colorTargets{};
    auto& colorTarget = colorTargets[0];
    colorTarget.format = config.colorFormat;
    colorTarget.writeMask = WGPUColorWriteMask_All;
    colorTargets[1].format = WGPUTextureFormat_R32Float;
    colorTargets[1].writeMask = WGPUColorWriteMask_All;
    WGPUFragmentState fragmentState{};
    fragmentState.module = shaderModule_;
    WGPU_SET_ENTRY_POINT(fragmentState, config.linearHdrOutput ? "fsOpaqueHdr" : "fs");
    fragmentState.targetCount = config.linearHdrOutput ? 2u : 1u;
    fragmentState.targets = colorTargets.data();

    WGPUPrimitiveState primitiveState{};
    primitiveState.topology = WGPUPrimitiveTopology_TriangleList;
    primitiveState.frontFace = config.frontFace;
    primitiveState.cullMode = WGPUCullMode_None;

    WGPUDepthStencilState depthState{};
    depthState.format = config.depthFormat;
    depthState.depthWriteEnabled = gpu::toOptionalBool(true);
    depthState.depthCompare = WGPUCompareFunction_LessEqual;
    depthState.stencilFront.compare = WGPUCompareFunction_Always;
    depthState.stencilFront.failOp = WGPUStencilOperation_Keep;
    depthState.stencilFront.depthFailOp = WGPUStencilOperation_Keep;
    depthState.stencilFront.passOp = WGPUStencilOperation_Keep;
    depthState.stencilBack = depthState.stencilFront;
    depthState.stencilReadMask = 0xFFFFFFFFu;
    depthState.stencilWriteMask = 0xFFFFFFFFu;

    WGPUMultisampleState multisample{};
    multisample.count = 1u;
    multisample.mask = 0xFFFFFFFFu;

    auto create = [&](const char* label, bool blendEnabled,
                      bool depthWrite) -> WGPURenderPipeline {
        WGPUBlendState blend{};
        if (blendEnabled) {
            blend.color.operation = WGPUBlendOperation_Add;
            blend.color.srcFactor = WGPUBlendFactor_SrcAlpha;
            blend.color.dstFactor = WGPUBlendFactor_OneMinusSrcAlpha;
            blend.alpha.operation = WGPUBlendOperation_Add;
            blend.alpha.srcFactor = WGPUBlendFactor_One;
            blend.alpha.dstFactor = WGPUBlendFactor_OneMinusSrcAlpha;
            colorTarget.blend = &blend;
        } else {
            colorTarget.blend = nullptr;
        }
        depthState.depthWriteEnabled = gpu::toOptionalBool(depthWrite && !config.depthOverlay);
        if (config.depthOverlay) depthState.depthCompare = WGPUCompareFunction_Always;
        WGPURenderPipelineDescriptor descriptor{};
        WGPU_SET_LABEL(descriptor, label);
        descriptor.layout = pipelineLayout_;
        descriptor.vertex = vertexState;
        descriptor.fragment = &fragmentState;
        descriptor.primitive = primitiveState;
        descriptor.depthStencil = &depthState;
        descriptor.multisample = multisample;
        return ::voxy::gpu::createRenderPipeline(device_, &descriptor);
    };
    opaquePipeline_ = create("mesh_path_opaque_pipeline", false, true);
    // The shader discards back faces of single-sided materials; culling them
    // in hardware drops exactly those fragments before they are rasterized.
    primitiveState.cullMode = WGPUCullMode_Back;
    opaqueCulledPipeline_ = create("mesh_path_opaque_culled_pipeline", false, true);
    primitiveState.cullMode = WGPUCullMode_None;
    blendPipeline_ = create("mesh_path_blend_pipeline", !config.linearHdrOutput, config.linearHdrOutput);
    if (sunShadows_) {
        // The caster binds only its matrix, never the depth texture currently
        // being written. Color receivers bind the finished map in a later pass.
        const std::array<WGPUBindGroupLayout,3> casterLayouts{bindGroupLayout_,bodyLayout_,sunCasterLayout_};
        sunCasterPipelineLayout_ = gpu::createPipelineLayout(device_,casterLayouts,"mesh_sun_pipeline_layout");
        if (!sunCasterPipelineLayout_) return false;
        WGPU_SET_ENTRY_POINT(vertexState,"vsSunShadow");
        WGPU_SET_ENTRY_POINT(fragmentState,"fsSunShadow");
        fragmentState.targetCount = 0; fragmentState.targets = nullptr;
        depthState.format = WGPUTextureFormat_Depth32Float;
        depthState.depthWriteEnabled = gpu::toOptionalBool(true);
        depthState.depthCompare = WGPUCompareFunction_LessEqual;
        depthState.depthBias = 1; depthState.depthBiasSlopeScale = 1.0f;
        WGPURenderPipelineDescriptor descriptor{};
        WGPU_SET_LABEL(descriptor,"mesh_live_sun_casters");
        descriptor.layout = sunCasterPipelineLayout_; descriptor.vertex = vertexState;
        descriptor.fragment = &fragmentState; descriptor.primitive = primitiveState;
        descriptor.depthStencil = &depthState; descriptor.multisample = multisample;
        sunCasterPipeline_ = ::voxy::gpu::createRenderPipeline(device_,&descriptor);
        descriptor.primitive.cullMode = WGPUCullMode_Back;
        WGPU_SET_LABEL(descriptor,"mesh_live_sun_casters_culled");
        sunCasterCulledPipeline_ = ::voxy::gpu::createRenderPipeline(device_,&descriptor);
    }
    return opaquePipeline_ && opaqueCulledPipeline_ && blendPipeline_
        && (!sunShadows_ || (sunCasterPipeline_ && sunCasterCulledPipeline_));
}

bool MeshPath::loadMesh(const std::filesystem::path& path) {
    if (!isInitialized() || path.empty()) return false;
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        LOG_ERROR("MeshPath: failed to open VMESH file '{}'", path.string());
        return false;
    }
    const auto fileSize = file.tellg();
    if (fileSize <= 0
        || fileSize > std::numeric_limits<std::streamsize>::max()
        || fileSize > kMaximumVmeshFileBytes) {
        LOG_ERROR("MeshPath: VMESH file '{}' has an invalid size", path.string());
        return false;
    }
    const auto size = static_cast<std::streamsize>(fileSize);
    file.seekg(0, std::ios::beg);
    std::vector<uint8_t> bytes(static_cast<size_t>(size));
    if (!file.read(reinterpret_cast<char*>(bytes.data()), size)) {
        LOG_ERROR("MeshPath: failed to read VMESH file '{}'", path.string());
        return false;
    }
    return loadMeshFromBytes(bytes);
}

bool MeshPath::loadMeshFromBytes(const std::vector<uint8_t>& bytes) {
    if (!isInitialized() || bytes.size() < sizeof(moto::VmeshHeader)
        || bytes.size() > static_cast<size_t>(kMaximumVmeshFileBytes)) {
        return false;
    }
    moto::VmeshData data;
    std::string error;
    if (!moto::readVmesh(bytes.data(), bytes.size(), &data, &error)) {
        LOG_ERROR("MeshPath: invalid VMESH data: {}", error);
        return false;
    }
    return loadMeshData(data);
}

bool MeshPath::loadMeshData(const moto::VmeshData& data) {
    if (!isInitialized()) return false;
    if (linearHdrOutput_ && std::any_of(data.materials.begin(), data.materials.end(),
        [](const auto& material) { return material.alphaMode == moto::VmeshAlphaBlend; })) {
        LOG_ERROR("MeshPath: blended materials require a later transparency pass");
        return false;
    }
    const auto& header = data.header;
    // This entry point bypasses serialized offset validation. Prove owned
    // vector/count relations in u64 before any platform-size multiplication.
    if (header.version != moto::kVmeshVersion
        || header.vertexStride != sizeof(moto::VmeshVertex)
        || (header.indexStride != 2u && header.indexStride != 4u)
        || uint64_t{header.vertexCount} * sizeof(moto::VmeshVertex) != data.vertices.size()
        || uint64_t{header.indexCount} * header.indexStride != data.indices.size()
        || header.submeshCount != data.submeshes.size() || header.materialCount != data.materials.size()
        || header.nodeCount != data.nodes.size() || header.skinCount != data.skins.size()
        || header.jointCount != data.joints.size() || header.animCount != data.anims.size()
        || header.animChannelCount != data.animChannels.size()
        || header.meshCount == 0u || header.meshCount > 65'536u
        || header.meshCount > data.submeshes.size()) return false;
    uint64_t ownedBytes = 0u;
    const auto reserve = [&ownedBytes](size_t count, size_t stride) {
        constexpr uint64_t maximum = static_cast<uint64_t>(kMaximumVmeshFileBytes);
        if (count > (maximum - ownedBytes) / stride) return false;
        ownedBytes += uint64_t{count} * stride;
        return true;
    };
    if (!reserve(data.vertices.size(), 1u) || !reserve(data.indices.size(), 1u)
        || !reserve(data.images.size(), 1u) || !reserve(data.stringBlob.size(), 1u)
        || !reserve(data.channelData.size(), 1u) || !reserve(data.submeshes.size(), sizeof(moto::VmeshSubmesh))
        || !reserve(data.materials.size(), sizeof(moto::VmeshMaterial)) || !reserve(data.nodes.size(), sizeof(moto::VmeshNode))
        || !reserve(data.skins.size(), sizeof(moto::VmeshSkin)) || !reserve(data.joints.size(), sizeof(moto::VmeshJoint))
        || !reserve(data.anims.size(), sizeof(moto::VmeshAnim)) || !reserve(data.animChannels.size(), sizeof(moto::VmeshAnimChannel))) return false;
    if (!uploadMesh(data)) {
        LOG_ERROR("MeshPath: failed to upload VMESH data");
        return false;
    }
    return true;
}

bool MeshPath::uploadMesh(const moto::VmeshData& data) {
    if (!device_ || !queue_ || !instanceBuffer_ || !uniformBuffer_
        || data.header.vertexStride != sizeof(moto::VmeshVertex)
        || (data.header.indexStride != 2u && data.header.indexStride != 4u)
        || data.header.vertexCount == 0u || data.header.indexCount == 0u
        || data.header.meshCount == 0u || data.submeshes.empty()
        || data.materials.empty()
        || data.vertices.size()
            != uint64_t{data.header.vertexCount}
                * sizeof(moto::VmeshVertex)
        || data.indices.size()
            != uint64_t{data.header.indexCount}
                * data.header.indexStride) {
        return false;
    }

    std::vector<glm::vec3> positions(data.header.vertexCount);
    for (uint32_t index = 0u; index < data.header.vertexCount; ++index) {
        moto::VmeshVertex vertex{};
        std::memcpy(&vertex,
                    data.vertices.data()
                        + static_cast<size_t>(index) * sizeof(vertex),
                    sizeof(vertex));
        const glm::vec3 position(
            vertex.position[0], vertex.position[1], vertex.position[2]);
        const glm::vec3 normal(
            vertex.normal[0], vertex.normal[1], vertex.normal[2]);
        if (!finiteVec3(position) || !finiteVec3(normal)
            || glm::dot(normal, normal) <= std::numeric_limits<float>::min()) {
            return false;
        }
        for (float component : vertex.tangent) {
            if (!finiteFloat(component)) return false;
        }
        for (float component : vertex.texCoord) {
            if (!finiteFloat(component)) return false;
        }
        for (float component : vertex.weight) {
            if (!finiteFloat(component)) return false;
        }
        positions[index] = position;
    }

    std::vector<uint32_t> indices(data.header.indexCount);
    for (uint32_t index = 0u; index < data.header.indexCount; ++index) {
        const uint8_t* source = data.indices.data()
            + static_cast<size_t>(index) * data.header.indexStride;
        if (data.header.indexStride == 2u) {
            uint16_t value = 0u;
            std::memcpy(&value, source, sizeof(value));
            indices[index] = value;
        } else {
            std::memcpy(&indices[index], source, sizeof(indices[index]));
        }
        if (indices[index] >= data.header.vertexCount) return false;
    }

    std::vector<moto::VmeshSubmesh> submeshes = data.submeshes;
    std::vector<MeshBounds> meshBounds(data.header.meshCount);
    for (uint32_t submeshIndex = 0; submeshIndex < submeshes.size(); ++submeshIndex) {
        auto& submesh = submeshes[submeshIndex];
        if (submesh.indexCount == 0u || (submesh.indexCount % 3u) != 0u
            || submesh.materialIndex >= data.materials.size()
            || submesh.meshIndex >= data.header.meshCount
            || (submesh.indexOffset % data.header.indexStride) != 0u) {
            return false;
        }
        const uint32_t firstIndex =
            submesh.indexOffset / data.header.indexStride;
        if (firstIndex > data.header.indexCount
            || submesh.indexCount > data.header.indexCount - firstIndex
            || firstIndex > std::numeric_limits<uint32_t>::max() / 4u) {
            return false;
        }
        MeshBounds& bounds = meshBounds[submesh.meshIndex];
        bounds.submeshIndices.push_back(submeshIndex);
        for (uint32_t i = 0u; i < submesh.indexCount; ++i) {
            const glm::vec3& position = positions[indices[firstIndex + i]];
            if (!bounds.valid) {
                bounds.minimum = position;
                bounds.maximum = position;
                bounds.valid = true;
            } else {
                bounds.minimum = glm::min(bounds.minimum, position);
                bounds.maximum = glm::max(bounds.maximum, position);
            }
        }
        submesh.indexOffset = firstIndex * 4u;
    }

    std::vector<GpuMaterial> materials;
    materials.reserve(data.materials.size());
    std::vector<uint8_t> materialAlphaModes;
    materialAlphaModes.reserve(data.materials.size());
    std::vector<uint8_t> materialDoubleSided;
    materialDoubleSided.reserve(data.materials.size());
    for (const moto::VmeshMaterial& material : data.materials) {
        if (!validMaterial(material)) return false;
        // Byte loading proves these through readVmesh. The public parsed-data
        // entry point must prove them too before createMaterialTexture reads.
        for (uint32_t slot = 0u; slot < moto::VmeshTextureCount; ++slot) {
            if (material.hasTexture[slot] > 1u || material.textureIsSrgb[slot] > 1u) return false;
            if (material.hasTexture[slot] == 0u) continue;
            const uint64_t bytes = uint64_t{material.textureWidth[slot]}
                * material.textureHeight[slot] * 4u;
            const size_t offset = material.textureOffset[slot];
            if (bytes == 0u || bytes != material.textureSize[slot]
                || offset > data.images.size() || bytes > data.images.size() - offset) return false;
        }
        GpuMaterial gpuMaterial;
        gpuMaterial.baseColorFactor = glm::vec4(
            material.baseColorFactor[0], material.baseColorFactor[1],
            material.baseColorFactor[2], material.baseColorFactor[3]);
        gpuMaterial.emissiveFactorAlphaCutoff = glm::vec4(
            material.emissiveFactor[0], material.emissiveFactor[1],
            material.emissiveFactor[2], material.alphaCutoff);
        gpuMaterial.metallicRoughnessNormalOcclusion = glm::vec4(
            material.metallicFactor, material.roughnessFactor,
            material.normalScale, material.occlusionStrength);
        uint32_t textureMask = 0u;
        for (uint32_t slot = 0u; slot < moto::VmeshTextureCount; ++slot) {
            if (material.hasTexture[slot] != 0u) textureMask |= 1u << slot;
        }
        gpuMaterial.flags = glm::uvec4(
            material.alphaMode, material.doubleSided, material.unlit,
            textureMask);
        materials.push_back(gpuMaterial);
        materialAlphaModes.push_back(material.alphaMode);
        materialDoubleSided.push_back(material.doubleSided);
    }

    GpuAsset pending;
    struct PendingGuard {
        GpuAsset& asset;
        bool published = false;
        ~PendingGuard() { if (!published) releaseAsset(asset, false); }
    } pendingGuard{pending};
    pending.vertexBuffer = gpu::createBufferWithData(
        device_, queue_,
        gpu::BufferDesc::vertex(data.vertices.size(), "mesh_path_vertices"),
        std::span<const uint8_t>(data.vertices));
    pending.indexBuffer = gpu::createBufferWithData(
        device_, queue_,
        gpu::BufferDesc::index(indices.size() * sizeof(uint32_t),
                               "mesh_path_indices_u32"),
        std::span<const uint32_t>(indices));
    pending.materialBuffer = gpu::createBufferWithData(
        device_, queue_,
        gpu::BufferDesc::storage(materials.size() * sizeof(GpuMaterial), true,
                                 "mesh_path_materials"),
        std::span<const GpuMaterial>(materials));
    pending.submeshes = std::move(submeshes);
    pending.meshBounds = std::move(meshBounds);
    pending.materialAlphaModes = std::move(materialAlphaModes);
    pending.materialDoubleSided = std::move(materialDoubleSided);
    pending.vertexCount = data.header.vertexCount;
    pending.indexCount = data.header.indexCount;

    if (pending.vertexBuffer && pending.indexBuffer && pending.materialBuffer) {
        pending.materials.resize(data.materials.size());
        const WGPUTextureView environment = boundEnvironmentView_
            ? boundEnvironmentView_ : fallbackEnvironmentView_;
        const WGPUTextureView rayDepth = boundRayDepthView_
            ? boundRayDepthView_ : fallbackRayDepthView_;
        for (size_t materialIndex = 0u;
             materialIndex < data.materials.size(); ++materialIndex) {
            GpuMaterialResources& resources = pending.materials[materialIndex];
            const moto::VmeshMaterial& material = data.materials[materialIndex];
            bool textureFailure = false;
            for (uint32_t slot = 0u; slot < moto::VmeshTextureCount; ++slot) {
                if (material.hasTexture[slot] == 0u) continue;
                resources.textures[slot] = createMaterialTexture(
                    device_, queue_, data, material,
                    static_cast<moto::VmeshTextureSlot>(slot));
                if (!resources.textures[slot]) {
                    textureFailure = true;
                    break;
                }
                gpu::TextureViewDesc viewDesc{};
                viewDesc.mipLevelCount =
                    wgpuTextureGetMipLevelCount(resources.textures[slot]);
                resources.views[slot] = gpu::createTextureView(
                    resources.textures[slot], viewDesc);
                if (!resources.views[slot]) {
                    textureFailure = true;
                    break;
                }
            }
            if (textureFailure) break;
            const auto textureView = [&](uint32_t slot) {
                return resources.views[slot] ? resources.views[slot]
                                             : fallbackEnvironmentView_;
            };
            const auto filtered = filteredEnvironment_.views();
            const std::array<gpu::BindGroupEntry, 16> entries = {
                gpu::BindGroupEntry(0).buffer(
                    uniformBuffer_, 0u, sizeof(MeshUniforms)),
                gpu::BindGroupEntry(1).buffer(
                    instanceBuffer_, 0u,
                    instanceCapacity_ * sizeof(GpuDrawInstance)),
                gpu::BindGroupEntry(2).buffer(
                    pending.materialBuffer, 0u,
                    materials.size() * sizeof(GpuMaterial)),
                gpu::BindGroupEntry(3).textureView(environment),
                gpu::BindGroupEntry(4).sampler(sampler_),
                gpu::BindGroupEntry(5).textureView(rayDepth),
                gpu::BindGroupEntry(6).textureView(textureView(0u)),
                gpu::BindGroupEntry(7).textureView(textureView(1u)),
                gpu::BindGroupEntry(8).textureView(textureView(2u)),
                gpu::BindGroupEntry(9).textureView(textureView(3u)),
                gpu::BindGroupEntry(10).sampler(materialSampler_),
                gpu::BindGroupEntry(11).textureView(filtered.specular ? filtered.specular : fallbackCubeView_),
                gpu::BindGroupEntry(12).textureView(filtered.diffuse ? filtered.diffuse : fallbackCubeView_),
                gpu::BindGroupEntry(13).textureView(filtered.brdf ? filtered.brdf : fallbackEnvironmentView_),
                gpu::BindGroupEntry(14).sampler(filteredSampler_),
                gpu::BindGroupEntry(15).buffer(instanceIndexBuffer_,0u,instanceCapacity_*sizeof(uint32_t)),
            };
            resources.bindGroup = gpu::createBindGroup(
                device_, bindGroupLayout_, entries,
                "mesh_path_material_bind_group");
            if (!resources.bindGroup) break;
        }
    }
    if (!pending.vertexBuffer || !pending.indexBuffer
        || !pending.materialBuffer
        || pending.materials.size() != data.materials.size()
        || std::any_of(pending.materials.begin(), pending.materials.end(),
                       [](const GpuMaterialResources& material) {
                           return material.bindGroup == nullptr;
                       })) {
        return false;
    }

    assets_.push_back(std::move(pending));
    pendingGuard.published = true;
    instancesValid_ = false;
    return true;
}

void MeshPath::clearInstances() {
    instances_.clear();
    instancesValid_ = false;
}

void MeshPath::addInstance(const MeshDrawInstance& instance) {
    instances_.push_back(instance);
    instancesValid_ = false;
}

bool MeshPath::setStaticInstances(std::span<const MeshDrawInstance> instances) {
    if(!isInitialized())return false;
    auto next=std::make_shared<StaticCache>();
    for(const auto& instance:instances) {
        if(instance.assetIndex>=assets_.size() || instance.castsSunShadow || instance.physicsBody.valid()
            || !validModelMatrix(instance.modelMatrix) || !finiteVec4(instance.tintColor)
            || instance.tintColor.a!=1 || !finiteFloat(instance.emissiveBoost)
            || !finiteVec4(instance.baseColorOverride) || !finiteVec4(instance.surface)
            || glm::any(glm::lessThan(instance.baseColorOverride,glm::vec4(0)))
            || glm::any(glm::greaterThan(instance.baseColorOverride,glm::vec4(1)))
            || (instance.baseColorOverride.w!=0 && instance.baseColorOverride.w!=1)
            || glm::any(glm::lessThan(instance.surface,glm::vec4(0)))
            || glm::any(glm::greaterThan(instance.surface,glm::vec4(1)))
            || (instance.surface.z!=0 && instance.surface.z!=1)
            || (instance.surface.z==0 && instance.surface!=glm::vec4(0))) return false;
        const auto& asset=assets_[instance.assetIndex];
        if(instance.meshIndex>=asset.meshBounds.size() || !asset.meshBounds[instance.meshIndex].valid)return false;
        const uint32_t first=static_cast<uint32_t>(next->records.size());
        for(const uint32_t sub:asset.meshBounds[instance.meshIndex].submeshIndices) {
            const auto& mesh=asset.submeshes[sub];
            if(asset.materialAlphaModes[mesh.materialIndex]!=moto::VmeshAlphaOpaque
                || next->records.size()>=maxDrawsPerFrame_)return false;
            auto batch=std::find_if(next->templates.begin(),next->templates.end(),[&](const auto& b){return b.assetIndex==instance.assetIndex&&b.submeshIndex==sub;});
            const uint32_t index=static_cast<uint32_t>(batch-next->templates.begin());
            if(batch==next->templates.end())next->templates.push_back({instance.assetIndex,sub,0,mesh.materialIndex,moto::VmeshAlphaOpaque,0,false,0,1});
            GpuDrawInstance gpu{};gpu.modelMatrix=instance.modelMatrix;gpu.tintColor=instance.tintColor;
            gpu.emissiveBoost=instance.emissiveBoost;gpu.materialIndex=mesh.materialIndex;
            gpu.baseColorOverride=instance.baseColorOverride;gpu.surface=instance.surface;
            next->records.push_back({index});next->resident.push_back(gpu);
        }
        const auto count=static_cast<uint32_t>(next->records.size())-first;
        if(count==0)return false;
        next->ranges.emplace_back(first,count);
    }
    next->buckets.resize(next->templates.size());
    staticCache_=std::move(next);
    return true;
}

bool MeshPath::selectStaticInstances(std::span<const uint32_t> selected) {
    if(!staticCache_)return selected.empty();
    auto& cache=*staticCache_;
    if(std::equal(selected.begin(),selected.end(),cache.selected.begin(),cache.selected.end()))return true;
    size_t total=0;
    for(auto index:selected) {
        if(index>=cache.ranges.size())return false;
        total+=cache.ranges[index].second;
        if(total>maxDrawsPerFrame_)return false;
    }
    for(auto& bucket:cache.buckets)bucket.clear();
    for(auto index:selected) {
        const auto [first,count]=cache.ranges[index];
        for(uint32_t r=first;r<first+count;++r)cache.buckets[cache.records[r].batch].push_back(r);
    }
    cache.packed.clear();cache.packed.reserve(total);cache.draws.clear();
    for(size_t b=0;b<cache.buckets.size();++b) {
        const auto& bucket=cache.buckets[b];if(bucket.empty())continue;
        auto draw=cache.templates[b];draw.firstInstance=static_cast<uint32_t>(cache.packed.size());
        draw.instanceCount=static_cast<uint32_t>(bucket.size());cache.draws.push_back(draw);
        cache.packed.insert(cache.packed.end(),bucket.begin(),bucket.end());
    }
    cache.selected.assign(selected.begin(),selected.end());cache.selectionUpload=true;
    return true;
}

void MeshPath::setEnvironmentTexture(WGPUTextureView view) {
    static_cast<void>(setSceneTextures(view, rayDepthView_));
}

void MeshPath::setRayDepthTexture(WGPUTextureView view) {
    static_cast<void>(setSceneTextures(environmentView_, view));
}

bool MeshPath::setSceneTextures(WGPUTextureView environment, WGPUTextureView rayDepth) {
    if (!isInitialized()) return false;
    if (filteredEnvironmentEncoded_) return false;
    if (environmentView_ == environment && rayDepthView_ == rayDepth) return true;
    const auto oldEnvironment = environmentView_;
    const auto oldDepth = rayDepthView_;
    environmentView_ = environment;
    rayDepthView_ = rayDepth;
    try {
        if (rebuildBindGroups()) {
            if (oldEnvironment != environment) filteredEnvironmentReady_ = false;
            return true;
        }
    } catch (...) {
        environmentView_ = oldEnvironment;
        rayDepthView_ = oldDepth;
        throw;
    }
    environmentView_ = oldEnvironment;
    rayDepthView_ = oldDepth;
    return false;
}

bool MeshPath::encodeEnvironmentLighting(WGPUCommandEncoder encoder) {
    if (!isInitialized() || !encoder || filteredEnvironmentEncoded_) return false;
    if (filteredEnvironment_.requestedBytes() == 0 || filteredEnvironmentReady_) return true;
    if (!filteredEnvironment_.encodeBake(encoder, boundEnvironmentView_)) return false;
    filteredEnvironmentEncoded_ = true;
    if (environmentBakeCount_ != std::numeric_limits<uint32_t>::max()) ++environmentBakeCount_;
    return true;
}

void MeshPath::acknowledgeEnvironmentSubmission() noexcept {
    if (filteredEnvironmentEncoded_) filteredEnvironmentReady_ = true;
    filteredEnvironmentEncoded_ = false;
}

void MeshPath::discardEnvironmentEncoding() noexcept { filteredEnvironmentEncoded_ = false; }

bool MeshPath::invalidateEnvironmentLighting() noexcept {
    if (filteredEnvironmentEncoded_) return false;
    filteredEnvironmentReady_ = false;
    return true;
}

bool MeshPath::rebuildBindGroups() {
    if (!device_ || !bindGroupLayout_ || !instanceBuffer_ || !uniformBuffer_
        || !sampler_ || !materialSampler_ || !fallbackEnvironmentView_
        || !fallbackRayDepthView_) {
        return false;
    }
    const WGPUTextureView environment = selectedEnvironment(
        environmentView_, fallbackEnvironmentView_);
    const WGPUTextureView rayDepth = selectedEnvironment(
        rayDepthView_, fallbackRayDepthView_);
    std::vector<std::vector<WGPUBindGroup>> replacements(assets_.size());
    struct ReplacementGuard {
        std::vector<std::vector<WGPUBindGroup>>& groups;
        bool published = false;
        ~ReplacementGuard() {
            if (!published) for (const auto& row : groups) for (auto group : row) {
                if (group) wgpuBindGroupRelease(group);
            }
        }
    } replacementGuard{replacements};
    for (size_t assetIndex = 0u; assetIndex < assets_.size(); ++assetIndex) {
        const GpuAsset& asset = assets_[assetIndex];
        replacements[assetIndex].resize(asset.materials.size(), nullptr);
        for (size_t materialIndex = 0u;
             materialIndex < asset.materials.size(); ++materialIndex) {
            const GpuMaterialResources& material =
                asset.materials[materialIndex];
            const auto textureView = [&](uint32_t slot) {
                return material.views[slot] ? material.views[slot]
                                            : fallbackEnvironmentView_;
            };
            const auto filtered = filteredEnvironment_.views();
            const std::array<gpu::BindGroupEntry, 16> entries = {
                gpu::BindGroupEntry(0).buffer(
                    uniformBuffer_, 0u, sizeof(MeshUniforms)),
                gpu::BindGroupEntry(1).buffer(
                    instanceBuffer_, 0u,
                    instanceCapacity_ * sizeof(GpuDrawInstance)),
                gpu::BindGroupEntry(2).buffer(asset.materialBuffer),
                gpu::BindGroupEntry(3).textureView(environment),
                gpu::BindGroupEntry(4).sampler(sampler_),
                gpu::BindGroupEntry(5).textureView(rayDepth),
                gpu::BindGroupEntry(6).textureView(textureView(0u)),
                gpu::BindGroupEntry(7).textureView(textureView(1u)),
                gpu::BindGroupEntry(8).textureView(textureView(2u)),
                gpu::BindGroupEntry(9).textureView(textureView(3u)),
                gpu::BindGroupEntry(10).sampler(materialSampler_),
                gpu::BindGroupEntry(11).textureView(filtered.specular ? filtered.specular : fallbackCubeView_),
                gpu::BindGroupEntry(12).textureView(filtered.diffuse ? filtered.diffuse : fallbackCubeView_),
                gpu::BindGroupEntry(13).textureView(filtered.brdf ? filtered.brdf : fallbackEnvironmentView_),
                gpu::BindGroupEntry(14).sampler(filteredSampler_),
                gpu::BindGroupEntry(15).buffer(instanceIndexBuffer_,0u,instanceCapacity_*sizeof(uint32_t)),
            };
            replacements[assetIndex][materialIndex] = gpu::createBindGroup(
                device_, bindGroupLayout_, entries,
                "mesh_path_material_bind_group");
            if (!replacements[assetIndex][materialIndex]) {
                LOG_ERROR("MeshPath: failed to rebuild material bind groups");
                return false;
            }
        }
    }
    for (size_t assetIndex = 0u; assetIndex < assets_.size(); ++assetIndex) {
        for (size_t materialIndex = 0u;
             materialIndex < assets_[assetIndex].materials.size();
             ++materialIndex) {
            GpuMaterialResources& material =
                assets_[assetIndex].materials[materialIndex];
            if (material.bindGroup) wgpuBindGroupRelease(material.bindGroup);
            material.bindGroup = replacements[assetIndex][materialIndex];
        }
    }
    boundEnvironmentView_ = environment;
    boundRayDepthView_ = rayDepth;
    replacementGuard.published = true;
    return true;
}

bool MeshPath::ensureInstanceCapacity(size_t required) {
    if (required <= instanceCapacity_) return true;
    if (!device_ || required > std::numeric_limits<uint32_t>::max()) return false;

    size_t newCapacity = std::max<size_t>(instanceCapacity_, 1u);
    while (newCapacity < required) {
        if (newCapacity > std::numeric_limits<size_t>::max() / 2u) return false;
        newCapacity *= 2u;
    }
    if (newCapacity > std::numeric_limits<size_t>::max()
                           / sizeof(GpuDrawInstance)) {
        return false;
    }

    WGPUBuffer replacementBuffer = gpu::createBuffer(
        device_, gpu::BufferDesc::storage(
                     newCapacity * sizeof(GpuDrawInstance), true,
                     "mesh_path_draw_instances"));
    if (!replacementBuffer) return false;
    WGPUBuffer replacementIndices=gpu::createBuffer(device_,gpu::BufferDesc::storage(
        newCapacity*sizeof(uint32_t),true,"mesh_path_instance_indices"));
    if(!replacementIndices){releaseBuffer(replacementBuffer);return false;}

    std::vector<std::vector<WGPUBindGroup>> replacementGroups(assets_.size());
    const WGPUTextureView environment = boundEnvironmentView_
        ? boundEnvironmentView_ : fallbackEnvironmentView_;
    const WGPUTextureView rayDepth = boundRayDepthView_
        ? boundRayDepthView_ : fallbackRayDepthView_;
    for (size_t assetIndex = 0u; assetIndex < assets_.size(); ++assetIndex) {
        GpuAsset& asset = assets_[assetIndex];
        replacementGroups[assetIndex].resize(asset.materials.size(), nullptr);
        for (size_t materialIndex = 0u;
             materialIndex < asset.materials.size(); ++materialIndex) {
            const GpuMaterialResources& material = asset.materials[materialIndex];
            const auto textureView = [&](uint32_t slot) {
                return material.views[slot] ? material.views[slot]
                                            : fallbackEnvironmentView_;
            };
            const auto filtered = filteredEnvironment_.views();
            const std::array<gpu::BindGroupEntry, 16> entries = {
                gpu::BindGroupEntry(0).buffer(
                    uniformBuffer_, 0u, sizeof(MeshUniforms)),
                gpu::BindGroupEntry(1).buffer(
                    replacementBuffer, 0u,
                    newCapacity * sizeof(GpuDrawInstance)),
                gpu::BindGroupEntry(2).buffer(asset.materialBuffer),
                gpu::BindGroupEntry(3).textureView(environment),
                gpu::BindGroupEntry(4).sampler(sampler_),
                gpu::BindGroupEntry(5).textureView(rayDepth),
                gpu::BindGroupEntry(6).textureView(textureView(0u)),
                gpu::BindGroupEntry(7).textureView(textureView(1u)),
                gpu::BindGroupEntry(8).textureView(textureView(2u)),
                gpu::BindGroupEntry(9).textureView(textureView(3u)),
                gpu::BindGroupEntry(10).sampler(materialSampler_),
                gpu::BindGroupEntry(11).textureView(filtered.specular ? filtered.specular : fallbackCubeView_),
                gpu::BindGroupEntry(12).textureView(filtered.diffuse ? filtered.diffuse : fallbackCubeView_),
                gpu::BindGroupEntry(13).textureView(filtered.brdf ? filtered.brdf : fallbackEnvironmentView_),
                gpu::BindGroupEntry(14).sampler(filteredSampler_),
                gpu::BindGroupEntry(15).buffer(replacementIndices,0u,newCapacity*sizeof(uint32_t)),
            };
            replacementGroups[assetIndex][materialIndex] =
                gpu::createBindGroup(device_, bindGroupLayout_, entries,
                                     "mesh_path_material_bind_group");
            if (!replacementGroups[assetIndex][materialIndex]) {
                for (auto& groups : replacementGroups) {
                    for (WGPUBindGroup group : groups) {
                        if (group) wgpuBindGroupRelease(group);
                    }
                }
                releaseBuffer(replacementBuffer);releaseBuffer(replacementIndices);
                return false;
            }
        }
    }

    for (size_t assetIndex = 0u; assetIndex < assets_.size(); ++assetIndex) {
        for (size_t materialIndex = 0u;
             materialIndex < assets_[assetIndex].materials.size();
             ++materialIndex) {
            GpuMaterialResources& material =
                assets_[assetIndex].materials[materialIndex];
            if (material.bindGroup) wgpuBindGroupRelease(material.bindGroup);
            material.bindGroup = replacementGroups[assetIndex][materialIndex];
        }
    }
    releaseBuffer(instanceBuffer_);
    instanceBuffer_ = replacementBuffer;
    releaseBuffer(instanceIndexBuffer_);instanceIndexBuffer_=replacementIndices;
    instanceCapacity_ = newCapacity;
    instancesValid_ = false;
    return true;
}

void MeshPath::render(WGPUCommandEncoder encoder, WGPUTextureView colorView,
                      WGPUTextureView depthView, const glm::mat4& view,
                      const glm::mat4& projection,
                      const glm::vec3& cameraPosition,
                      const glm::vec3& lightDirection, uint32_t width,
                      uint32_t height, bool useRayDepth) {
    PrimitiveLighting lighting;
    lighting.direction = lightDirection;
    render(encoder, colorView, depthView, view, projection, cameraPosition,
           lighting, width, height, useRayDepth);
}

bool MeshPath::render(WGPUCommandEncoder encoder, WGPUTextureView colorView,
                      WGPUTextureView depthView, const glm::mat4& view,
                      const glm::mat4& projection,
                      const glm::vec3& cameraPosition,
                      const PrimitiveLighting& lighting, uint32_t width,
                      uint32_t height, bool useRayDepth, WGPUTextureView linearDepthOutput,
                      SceneShadowConsumer beforeColor, glm::vec3 shadowFrameWorldOrigin, FootContacts footContacts) {
    lastSubmittedDrawCount_ = 0u;
    lastCulledInstanceCount_ = 0u;
    lastInstanceUploadBytes_=lastColorTriangles_=lastShadowTriangles_=0;
    lastSubmeshVisitCount_=0;
    for (auto& asset : assets_) asset.encodedColorDraws=0;
    if (!isInitialized() || !encoder || !colorView || !depthView
        || linearHdrOutput_ != (linearDepthOutput != nullptr)) return false;
    const glm::mat4 viewProj = projection * view;
    if (width == 0u || height == 0u || width > kMaximumRenderExtent
        || height > kMaximumRenderExtent || !finiteMatrix(view)
        || !finiteMatrix(projection) || !finiteMatrix(viewProj)
        || !finiteVec3(cameraPosition) || !finiteVec3(shadowFrameWorldOrigin) || !validLighting(lighting)) {
        LOG_ERROR("MeshPath::render: invalid frame matrices, lighting, or extent");
        return false;
    }
    const size_t staticCount=staticCache_ ? staticCache_->resident.size() : 0;
    const size_t staticVisible=staticCache_ ? staticCache_->packed.size() : 0;
    if (instances_.empty() && staticVisible==0) return beforeColor(encoder, nullptr);
    for (const auto& foot : footContacts)
        if (!finiteVec3(glm::vec3(foot)) || !std::isfinite(foot.w) || foot.w < 0 || foot.w > 4) return false;

    auto shadow = sunShadows_ ? sunShadowFrame(cameraPosition,lighting.direction,shadowFrameWorldOrigin,farSunShadows_) : SunShadowUniforms{};
    shadow.footContacts = footContacts;

    if (!frameScratch_) frameScratch_ = std::make_shared<FrameScratch>();
    auto& gpuInstances = frameScratch_->gpuInstances;
    auto& draws = frameScratch_->draws;
    gpuInstances.clear();
    draws.clear();
    gpuInstances.reserve(std::min<size_t>(instances_.size(), maxDrawsPerFrame_));
    draws.reserve(std::min<size_t>(instances_.size(), maxDrawsPerFrame_));
    for (const MeshDrawInstance& instance : instances_) {
        if (instance.assetIndex >= assets_.size()
            || !validModelMatrix(instance.modelMatrix)
            || !finiteVec4(instance.tintColor)
            || !finiteVec4(instance.baseColorOverride)
            || glm::any(glm::lessThan(instance.baseColorOverride, glm::vec4(0.0f)))
            || glm::any(glm::greaterThan(instance.baseColorOverride, glm::vec4(1.0f)))
            || (instance.baseColorOverride.w != 0.0f && instance.baseColorOverride.w != 1.0f)
            || !finiteVec4(instance.surface)
            || glm::any(glm::lessThan(instance.surface, glm::vec4(0.0f)))
            || glm::any(glm::greaterThan(instance.surface, glm::vec4(1.0f)))
            || (instance.surface.z != 0.0f && instance.surface.z != 1.0f)
            || (instance.surface.z == 0.0f && instance.surface != glm::vec4(0.0f))
            || instance.shadowRegions > 3u
            || !finiteFloat(instance.emissiveBoost)) {
            LOG_ERROR("MeshPath::render: invalid mesh instance");
            instancesValid_ = false;
            return false;
        }
        const GpuAsset& asset = assets_[instance.assetIndex];
        if (!asset.vertexBuffer || !asset.indexBuffer
            || asset.materials.empty()
            || asset.materialAlphaModes.size() != asset.materials.size()
            || asset.materialDoubleSided.size() != asset.materials.size()
            || instance.meshIndex >= asset.meshBounds.size()
            || !asset.meshBounds[instance.meshIndex].valid) {
            LOG_ERROR("MeshPath::render: invalid mesh asset");
            instancesValid_ = false;
            return false;
        }

        const MeshBounds& bounds = asset.meshBounds[instance.meshIndex];
        if (!finiteVec3(bounds.minimum) || !finiteVec3(bounds.maximum)
            || glm::any(glm::greaterThan(bounds.minimum, bounds.maximum))) {
            LOG_ERROR("MeshPath::render: invalid mesh bounds");
            instancesValid_ = false;
            return false;
        }

        if (bounds.submeshIndices.empty()) {
            LOG_ERROR("MeshPath::render: asset {} has no logical mesh {}",
                      instance.assetIndex, instance.meshIndex);
            instancesValid_ = false;
            return false;
        }
        const bool live=instance.physicsBody.valid();
        uint8_t visibility=instance.colorVisible &&
            (live || boundsVisible(viewProj*instance.modelMatrix,bounds.minimum,bounds.maximum)) ? 1u : 0u;
        if(sunShadows_ && instance.castsSunShadow && instance.tintColor.a>=.99f) {
            if((instance.shadowRegions&1u) && (live || boundsVisible(shadow.viewProj*instance.modelMatrix,bounds.minimum,bounds.maximum)))visibility|=2u;
            if(farSunShadows_ && (instance.shadowRegions&2u) && (live || boundsVisible(shadow.farViewProj*instance.modelMatrix,bounds.minimum,bounds.maximum)))visibility|=4u;
        }
        if(!visibility) { ++lastCulledInstanceCount_;continue; }

        const glm::vec3 localCenter =
            (bounds.minimum + bounds.maximum) * 0.5f;
        const glm::vec3 worldCenter = glm::vec3(
            instance.modelMatrix * glm::vec4(localCenter, 1.0f));
        const glm::vec3 cameraDelta = cameraPosition - worldCenter;
        const float distanceSquared = glm::dot(cameraDelta, cameraDelta);
        for (const uint32_t submeshIndex : bounds.submeshIndices) {
            ++lastSubmeshVisitCount_;
            const moto::VmeshSubmesh& submesh = asset.submeshes[submeshIndex];
            if (gpuInstances.size() >= maxDrawsPerFrame_
                || gpuInstances.size()
                    >= std::numeric_limits<uint32_t>::max()) {
                LOG_ERROR("MeshPath::render: expanded draw count exceeds {}",
                          maxDrawsPerFrame_);
                instancesValid_ = false;
                return false;
            }
            GpuDrawInstance gpuInstance;
            gpuInstance.modelMatrix = instance.modelMatrix;
            gpuInstance.tintColor = instance.tintColor;
            gpuInstance.baseColorOverride = instance.baseColorOverride;
            gpuInstance.surface = instance.surface;
            gpuInstance.emissiveBoost = instance.emissiveBoost;
            gpuInstance.materialIndex = submesh.materialIndex;
            gpuInstance.padding[0] = instance.physicsBody.index;
            gpuInstance.padding[1] = instance.physicsBody.generation;
            const uint32_t firstInstance =
                static_cast<uint32_t>(gpuInstances.size());
            gpuInstances.push_back(gpuInstance);
            draws.push_back({
                instance.assetIndex, submeshIndex, firstInstance,
                submesh.materialIndex,
                asset.materialAlphaModes[submesh.materialIndex],
                distanceSquared, instance.castsSunShadow && instance.tintColor.a >= 0.99f
                    && asset.materialAlphaModes[submesh.materialIndex] != moto::VmeshAlphaBlend,1u,visibility});
        }
    }

    if (gpuInstances.empty() && staticVisible==0) return beforeColor(encoder,nullptr);
    std::sort(draws.begin(), draws.end(), [](const PendingDraw& left,
                                             const PendingDraw& right) {
        const bool leftBlend = left.alphaMode == moto::VmeshAlphaBlend;
        const bool rightBlend = right.alphaMode == moto::VmeshAlphaBlend;
        if (leftBlend != rightBlend) return !leftBlend;
        if (leftBlend && left.distanceSquared != right.distanceSquared) {
            return left.distanceSquared > right.distanceSquared;
        }
        if (left.assetIndex != right.assetIndex) {
            return left.assetIndex < right.assetIndex;
        }
        if (left.materialIndex != right.materialIndex) {
            return left.materialIndex < right.materialIndex;
        }
        if(left.submeshIndex != right.submeshIndex)return left.submeshIndex < right.submeshIndex;
        if(!leftBlend && left.castsSunShadow!=right.castsSunShadow)return left.castsSunShadow<right.castsSunShadow;
        if(!leftBlend)return left.visibility<right.visibility;
        return false;
    });

    // Shader instance_index includes firstInstance. Pack records in the existing
    // material/depth order, then coalesce compatible consecutive draws. Each
    // instance keeps its transform, paint, live-body handle and surface state.
    // Blended records retain back-to-front order; no new transparency sorting.
    auto& sortedInstances = frameScratch_->sortedInstances;
    sortedInstances.clear();
    sortedInstances.reserve(gpuInstances.size());
    auto& batches = frameScratch_->batches;
    batches.clear();
    batches.reserve(draws.size());
    for (auto draw : draws) {
        sortedInstances.push_back(gpuInstances[draw.firstInstance]);
        draw.firstInstance = static_cast<uint32_t>(sortedInstances.size()-1u);
        if (!batches.empty()) {
            auto& previous = batches.back();
            if (previous.assetIndex == draw.assetIndex && previous.submeshIndex == draw.submeshIndex
                && previous.materialIndex == draw.materialIndex && previous.alphaMode == draw.alphaMode
                && previous.castsSunShadow == draw.castsSunShadow && previous.visibility == draw.visibility) {
                ++previous.instanceCount;
                continue;
            }
        }
        batches.push_back(draw);
    }
    gpuInstances.swap(sortedInstances);
    draws.swap(batches);

    const auto oldBuffer=instanceBuffer_;
    const size_t required=std::max(staticCount,staticVisible)+gpuInstances.size();
    if (required>maxDrawsPerFrame_ || !ensureInstanceCapacity(required)) {
        instancesValid_ = false;
        return false;
    }
    if(staticCount && (staticCache_->upload || oldBuffer!=instanceBuffer_)) {
        if(!gpu::writeBuffer(queue_,instanceBuffer_,0u,std::span<const GpuDrawInstance>(staticCache_->resident)))return false;
        lastInstanceUploadBytes_+=staticCount*sizeof(GpuDrawInstance);staticCache_->upload=false;
    }
    if (!gpuInstances.empty() && !gpu::writeBuffer(queue_, instanceBuffer_, staticCount*sizeof(GpuDrawInstance),
                          std::span<const GpuDrawInstance>(gpuInstances))) {
        instancesValid_ = false;
        return false;
    }
    lastInstanceUploadBytes_+=gpuInstances.size()*sizeof(GpuDrawInstance);
    if(staticCount) {
        if(staticVisible && (staticCache_->selectionUpload || oldBuffer!=instanceBuffer_)) {
            if(!gpu::writeBuffer(queue_,instanceIndexBuffer_,0u,std::span<const uint32_t>(staticCache_->packed)))return false;
            lastInstanceUploadBytes_+=staticVisible*sizeof(uint32_t);
        }
        staticCache_->selectionUpload=false;
        auto& liveIndices=frameScratch_->liveIndices;
        liveIndices.clear();liveIndices.reserve(gpuInstances.size());
        for(uint32_t i=0;i<gpuInstances.size();++i)liveIndices.push_back(static_cast<uint32_t>(staticCount)+i);
        if(!liveIndices.empty() && !gpu::writeBuffer(queue_,instanceIndexBuffer_,staticVisible*sizeof(uint32_t),std::span<const uint32_t>(liveIndices)))return false;
        lastInstanceUploadBytes_+=liveIndices.size()*sizeof(uint32_t);
        for(auto& draw:draws)draw.firstInstance+=static_cast<uint32_t>(staticVisible);
    }
    // Static records are opaque. Draw before live opaque/transparent records,
    // preserving the latter's back-to-front blending order.
    if(staticCount)draws.insert(draws.begin(),staticCache_->draws.begin(),staticCache_->draws.end());

    MeshUniforms uniforms;
    uniforms.viewProj = viewProj;
    uniforms.cameraPosition = glm::vec4(
        cameraPosition, useRayDepth ? 1.0f : 0.0f);
    uniforms.lightDirectionFogDensity = glm::vec4(
        glm::normalize(lighting.direction),
        std::max(lighting.fogDensity, 0.0f));
    uniforms.sunColorIntensity = glm::vec4(
        glm::max(lighting.sunColor, glm::vec3(0.0f)),
        std::max(lighting.sunIntensity, 0.0f));
    uniforms.ambientColorIntensity = glm::vec4(
        glm::max(lighting.ambientColor, glm::vec3(0.0f)),
        std::max(lighting.ambientIntensity, 0.0f));
    uniforms.fogColorExposure = glm::vec4(
        glm::max(lighting.fogColor, glm::vec3(0.0f)),
        std::max(lighting.exposure, 0.0f));
    if (filteredEnvironment_.requestedBytes() != 0) {
        if (!filteredEnvironmentReady_ && !filteredEnvironmentEncoded_) return false;
        uniforms.environmentParams.x = 1.0f;
    }
    uniforms.environmentParams.y = toySkyGroundFill_ ? 1.0f : 0.0f;
    uniforms.environmentParams.z = lighting.dayNightHour;
    uniforms.environmentParams.w = staticCount ? 1.f : 0.f;
    if (!gpu::writeBuffer(queue_, uniformBuffer_, 0u, uniforms)) {
        instancesValid_ = false;
        return false;
    }
    instancesValid_ = true;

    auto farShadow = shadow;
    farShadow.viewProj = shadow.farViewProj;
    if (!gpu::writeBuffer(queue_,sunShadowUniform_,0u,shadow)
        || !gpu::writeBuffer(queue_,farSunShadowUniform_,0u,farShadow)) return false;
    if (sunShadows_) {
        WGPURenderPassDepthStencilAttachment target{};
        target.view = sunShadowView_; target.depthClearValue = 1.0f;
        target.depthLoadOp = WGPULoadOp_Clear; target.depthStoreOp = WGPUStoreOp_Store;
        target.stencilReadOnly = true;
        WGPURenderPassDescriptor descriptor{};
        WGPU_SET_LABEL(descriptor,"mesh_live_sun_shadows");
        descriptor.depthStencilAttachment = &target;
        auto pass = wgpuCommandEncoderBeginRenderPass(encoder,&descriptor);
        if (!pass) return false;
        WGPURenderPipeline boundCaster = sunCasterPipeline_;
        wgpuRenderPassEncoderSetPipeline(pass,boundCaster);
        wgpuRenderPassEncoderSetBindGroup(pass,1u,bodyBinding_,0u,nullptr);
        uint32_t boundShadowAsset = std::numeric_limits<uint32_t>::max();
        uint32_t boundShadowMaterial = std::numeric_limits<uint32_t>::max();
        for (uint32_t region=0;region<(farSunShadows_ ? 2u : 1u);++region) {
            const uint32_t size = region ? farSunShadowResolution : sunShadowResolution;
            const uint32_t x = region ? sunShadowResolution : 0;
            wgpuRenderPassEncoderSetViewport(pass,float(x),0,float(size),float(size),0,1);
            wgpuRenderPassEncoderSetScissorRect(pass,x,0,size,size);
            wgpuRenderPassEncoderSetBindGroup(pass,2u,region ? farSunCasterBinding_ : sunCasterBinding_,0u,nullptr);
            for (const auto& draw : draws) {
                // Translucent material/ghosts cannot cast an opaque silhouette.
                if (!draw.castsSunShadow || !(draw.visibility & (2u<<region))) continue;
                const auto& asset = assets_[draw.assetIndex];
                const auto& submesh = asset.submeshes[draw.submeshIndex];
                if (boundShadowAsset != draw.assetIndex) {
                    boundShadowAsset = draw.assetIndex;
                    boundShadowMaterial = std::numeric_limits<uint32_t>::max();
                    wgpuRenderPassEncoderSetVertexBuffer(pass,0u,asset.vertexBuffer,0u,WGPU_WHOLE_SIZE);
                    wgpuRenderPassEncoderSetIndexBuffer(pass,asset.indexBuffer,WGPUIndexFormat_Uint32,0u,WGPU_WHOLE_SIZE);
                }
                // Same layout: bind groups survive the pipeline switch.
                const auto caster = asset.materialDoubleSided[draw.materialIndex]
                    ? sunCasterPipeline_ : sunCasterCulledPipeline_;
                if (boundCaster != caster) {
                    boundCaster = caster;
                    wgpuRenderPassEncoderSetPipeline(pass,boundCaster);
                }
                if (boundShadowMaterial != draw.materialIndex) {
                    boundShadowMaterial = draw.materialIndex;
                    wgpuRenderPassEncoderSetBindGroup(pass,0u,asset.materials[draw.materialIndex].bindGroup,0u,nullptr);
                }
                wgpuRenderPassEncoderDrawIndexed(pass,submesh.indexCount,draw.instanceCount,submesh.indexOffset/4u,0,draw.firstInstance);
                lastShadowTriangles_+=uint64_t(submesh.indexCount/3)*draw.instanceCount;
            }
        }
        wgpuRenderPassEncoderEnd(pass); wgpuRenderPassEncoderRelease(pass);
    }

    if (!beforeColor(encoder, sunShadows_ ? sunShadowBinding_ : nullptr)) return false;

    std::array<WGPURenderPassColorAttachment, 2> colorAttachments{};
    auto& colorAttachment = colorAttachments[0];
    colorAttachment.view = colorView;
    colorAttachment.loadOp = WGPULoadOp_Load;
    colorAttachment.storeOp = WGPUStoreOp_Store;
    colorAttachment.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;

    WGPURenderPassDepthStencilAttachment depthAttachment{};
    depthAttachment.view = depthView;
    depthAttachment.depthLoadOp = WGPULoadOp_Load;
    depthAttachment.depthStoreOp = WGPUStoreOp_Store;
    depthAttachment.depthClearValue = 1.0f;
    depthAttachment.stencilLoadOp = WGPULoadOp_Undefined;
    depthAttachment.stencilStoreOp = WGPUStoreOp_Undefined;
    depthAttachment.depthReadOnly = false;
    depthAttachment.stencilReadOnly = true;

    WGPURenderPassDescriptor passDescriptor{};
    WGPU_SET_LABEL(passDescriptor, "mesh_path_pass");
    colorAttachments[1] = colorAttachment;
    colorAttachments[1].view = linearDepthOutput;
    passDescriptor.colorAttachmentCount = linearHdrOutput_ ? 2u : 1u;
    passDescriptor.colorAttachments = colorAttachments.data();
    passDescriptor.depthStencilAttachment = &depthAttachment;
    WGPURenderPassEncoder pass =
        wgpuCommandEncoderBeginRenderPass(encoder, &passDescriptor);
    if (!pass) {
        LOG_ERROR("MeshPath::render: failed to begin render pass");
        return false;
    }

    WGPURenderPipeline boundPipeline = nullptr;
    uint32_t boundAsset = std::numeric_limits<uint32_t>::max();
    uint32_t boundMaterial = std::numeric_limits<uint32_t>::max();
    for (const PendingDraw& draw : draws) {
        if(!(draw.visibility&1u))continue;
        const GpuAsset& asset = assets_[draw.assetIndex];
        const moto::VmeshSubmesh& submesh =
            asset.submeshes[draw.submeshIndex];
        WGPURenderPipeline desiredPipeline =
            draw.alphaMode == moto::VmeshAlphaBlend ? blendPipeline_
            : asset.materialDoubleSided[draw.materialIndex] ? opaquePipeline_
            : opaqueCulledPipeline_;
        if (boundPipeline != desiredPipeline) {
            boundPipeline = desiredPipeline;
            wgpuRenderPassEncoderSetPipeline(pass, boundPipeline);
            wgpuRenderPassEncoderSetBindGroup(pass,1u,bodyBinding_,0u,nullptr);
            wgpuRenderPassEncoderSetBindGroup(pass,2u,sunShadowBinding_,0u,nullptr);
        }
        if (boundAsset != draw.assetIndex) {
            boundAsset = draw.assetIndex;
            boundMaterial = std::numeric_limits<uint32_t>::max();
            wgpuRenderPassEncoderSetVertexBuffer(
                pass, 0u, asset.vertexBuffer, 0u, WGPU_WHOLE_SIZE);
            wgpuRenderPassEncoderSetIndexBuffer(
                pass, asset.indexBuffer, WGPUIndexFormat_Uint32,
                0u, WGPU_WHOLE_SIZE);
        }
        if (boundMaterial != draw.materialIndex) {
            boundMaterial = draw.materialIndex;
            wgpuRenderPassEncoderSetBindGroup(
                pass, 0u, asset.materials[draw.materialIndex].bindGroup,
                0u, nullptr);
        }
        wgpuRenderPassEncoderDrawIndexed(
            pass, submesh.indexCount, draw.instanceCount, submesh.indexOffset / 4u,
            0, draw.firstInstance);
        ++assets_[draw.assetIndex].encodedColorDraws;
        ++lastSubmittedDrawCount_;
        lastColorTriangles_+=uint64_t(submesh.indexCount/3)*draw.instanceCount;
    }
    wgpuRenderPassEncoderEnd(pass);
    wgpuRenderPassEncoderRelease(pass);
    return true;
}

}  // namespace voxy::render

namespace voxy::render {
bool MeshPath::setAuthoredBodyView(const physics::PhysicsRenderView& view,physics::WorldPosition camera) {
    if (!bodyLayout_ || !bodyFallback_ || !bodyCamera_ || !physics::isValidWorldPosition(camera)) return false;
    const bool active=view.authoredShapeBuffer != nullptr;
    if (active && (!view.poseBuffer || !view.metadataBuffer || !view.shapeBuffer)) return false;
    struct alignas(16) Camera { glm::ivec4 sector; glm::vec4 local; };
    if (!gpu::writeBuffer(queue_,bodyCamera_,0,Camera{glm::ivec4(camera.sector,0),glm::vec4(camera.local,0)})) return false;
    using BE=gpu::BindGroupEntry;
    const std::array entries{BE(0).buffer(active?view.poseBuffer:bodyFallback_),
        BE(1).buffer(active?view.metadataBuffer:bodyFallback_),BE(2).buffer(active?view.shapeBuffer:bodyFallback_),
        BE(3).buffer(active?view.authoredShapeBuffer:bodyFallback_),BE(4).buffer(bodyCamera_)};
    auto binding=gpu::createBindGroup(device_,bodyLayout_,entries,"mesh_live_body");
    if (!binding) return false;
    if(bodyBinding_) wgpuBindGroupRelease(bodyBinding_);
    bodyBinding_=binding; return true;
}
} // namespace voxy::render
