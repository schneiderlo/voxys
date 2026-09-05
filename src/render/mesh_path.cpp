// ═══════════════════════════════════════════════════════════════════════════════
// mesh_path.cpp - RIDGEBREAK static VMESH PBR renderer
// ═══════════════════════════════════════════════════════════════════════════════

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

namespace voxy::render {
namespace {

constexpr std::streamoff kMaximumVmeshFileBytes =
    512ll * 1024ll * 1024ll;
constexpr uint32_t kMaximumRenderExtent = 8'192u;

struct alignas(16) MeshUniforms {
    glm::mat4 viewProj{1.0f};
    glm::vec4 cameraPosition{0.0f};
    glm::vec4 lightDirectionFogDensity{0.0f, 1.0f, 0.0f, 0.0f};
    glm::vec4 sunColorIntensity{1.0f};
    glm::vec4 ambientColorIntensity{0.0f};
    glm::vec4 fogColorExposure{0.0f};
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
};

static_assert(sizeof(MeshUniforms) == 144u);
static_assert(alignof(MeshUniforms) == 16u);
static_assert(offsetof(MeshUniforms, viewProj) == 0u);
static_assert(offsetof(MeshUniforms, cameraPosition) == 64u);
static_assert(offsetof(MeshUniforms, lightDirectionFogDensity) == 80u);
static_assert(offsetof(MeshUniforms, sunColorIntensity) == 96u);
static_assert(offsetof(MeshUniforms, ambientColorIntensity) == 112u);
static_assert(offsetof(MeshUniforms, fogColorExposure) == 128u);
static_assert(sizeof(GpuMaterial) == 64u);
static_assert(alignof(GpuMaterial) == 16u);
static_assert(offsetof(GpuMaterial, baseColorFactor) == 0u);
static_assert(offsetof(GpuMaterial, emissiveFactorAlphaCutoff) == 16u);
static_assert(offsetof(GpuMaterial, metallicRoughnessNormalOcclusion) == 32u);
static_assert(offsetof(GpuMaterial, flags) == 48u);
static_assert(sizeof(GpuDrawInstance) == 96u);
static_assert(alignof(GpuDrawInstance) == 16u);
static_assert(offsetof(GpuDrawInstance, modelMatrix) == 0u);
static_assert(offsetof(GpuDrawInstance, tintColor) == 64u);
static_assert(offsetof(GpuDrawInstance, emissiveBoost) == 80u);
static_assert(offsetof(GpuDrawInstance, materialIndex) == 84u);
static_assert(offsetof(GpuDrawInstance, padding) == 88u);
static_assert(sizeof(moto::VmeshVertex) == 72u);

struct PendingDraw {
    uint32_t assetIndex = 0u;
    uint32_t submeshIndex = 0u;
    uint32_t firstInstance = 0u;
    uint32_t materialIndex = 0u;
    uint8_t alphaMode = moto::VmeshAlphaOpaque;
    float distanceSquared = 0.0f;
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
        && finiteFloat(lighting.exposure);
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
            wgpuTextureDestroy(texture);
            wgpuTextureRelease(texture);
            return nullptr;
        }
        if (mip + 1u < descriptor.mipLevelCount) {
            level = downsampleRgba(level, levelWidth, levelHeight, slot, srgb);
            levelWidth = std::max(levelWidth / 2u, 1u);
            levelHeight = std::max(levelHeight / 2u, 1u);
        }
    }
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
void releaseAsset(Asset& asset) noexcept {
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
                wgpuTextureDestroy(texture);
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

MeshPath::~MeshPath() { shutdown(); }

bool MeshPath::init(WGPUDevice device, WGPUQueue queue,
                    const MeshPathConfig& config) {
    shutdown();
    if (!device || !queue || config.maxInstances == 0u
        || config.maxDrawsPerFrame == 0u
        || config.maxInstances
            > std::numeric_limits<size_t>::max() / sizeof(GpuDrawInstance)) {
        return false;
    }

    device_ = device;
    queue_ = queue;
    colorFormat_ = config.colorFormat;
    depthFormat_ = config.depthFormat;
    maxDrawsPerFrame_ = config.maxDrawsPerFrame;
    instanceCapacity_ = config.maxInstances;

    uniformBuffer_ = gpu::createBuffer(
        device_, gpu::BufferDesc::uniform(
                     sizeof(MeshUniforms), "mesh_path_uniforms"));
    instanceBuffer_ = gpu::createBuffer(
        device_, gpu::BufferDesc::storage(
                     instanceCapacity_ * sizeof(GpuDrawInstance), true,
                     "mesh_path_draw_instances"));
    gpu::SamplerDesc samplerDesc =
        gpu::SamplerDesc::linear("mesh_path_environment_sampler");
    samplerDesc.addressModeU = WGPUAddressMode_Repeat;
    sampler_ = gpu::createSampler(device_, samplerDesc);
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

    if (!uniformBuffer_ || !instanceBuffer_ || !sampler_ || !materialSampler_
        || !fallbackEnvironmentTexture_ || !fallbackEnvironmentView_
        || !fallbackRayDepthTexture_ || !fallbackRayDepthView_
        || !createPipeline(config)) {
        shutdown();
        return false;
    }
    boundEnvironmentView_ = fallbackEnvironmentView_;
    boundRayDepthView_ = fallbackRayDepthView_;
    instancesValid_ = false;
    return true;
}

void MeshPath::shutdown() {
    for (GpuAsset& asset : assets_) releaseAsset(asset);
    assets_.clear();
    instances_.clear();

    if (blendPipeline_) {
        wgpuRenderPipelineRelease(blendPipeline_);
        blendPipeline_ = nullptr;
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
    if (fallbackEnvironmentView_) {
        wgpuTextureViewRelease(fallbackEnvironmentView_);
        fallbackEnvironmentView_ = nullptr;
    }
    if (fallbackEnvironmentTexture_) {
        wgpuTextureDestroy(fallbackEnvironmentTexture_);
        wgpuTextureRelease(fallbackEnvironmentTexture_);
        fallbackEnvironmentTexture_ = nullptr;
    }
    if (fallbackRayDepthView_) {
        wgpuTextureViewRelease(fallbackRayDepthView_);
        fallbackRayDepthView_ = nullptr;
    }
    if (fallbackRayDepthTexture_) {
        wgpuTextureDestroy(fallbackRayDepthTexture_);
        wgpuTextureRelease(fallbackRayDepthTexture_);
        fallbackRayDepthTexture_ = nullptr;
    }
    releaseBuffer(instanceBuffer_);
    releaseBuffer(uniformBuffer_);

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
    std::array<gpu::BindGroupLayoutEntry, 11> entries = {
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
    };
    bindGroupLayout_ = gpu::createBindGroupLayout(
        device_, entries, "mesh_path_bind_group_layout");
    if (!bindGroupLayout_) return false;

    const std::array<WGPUBindGroupLayout, 1> layouts = {bindGroupLayout_};
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

    WGPUColorTargetState colorTarget{};
    colorTarget.format = config.colorFormat;
    colorTarget.writeMask = WGPUColorWriteMask_All;
    WGPUFragmentState fragmentState{};
    fragmentState.module = shaderModule_;
    WGPU_SET_ENTRY_POINT(fragmentState, "fs");
    fragmentState.targetCount = 1u;
    fragmentState.targets = &colorTarget;

    WGPUPrimitiveState primitiveState{};
    primitiveState.topology = WGPUPrimitiveTopology_TriangleList;
    primitiveState.frontFace = WGPUFrontFace_CCW;
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
        depthState.depthWriteEnabled = gpu::toOptionalBool(depthWrite);
        WGPURenderPipelineDescriptor descriptor{};
        WGPU_SET_LABEL(descriptor, label);
        descriptor.layout = pipelineLayout_;
        descriptor.vertex = vertexState;
        descriptor.fragment = &fragmentState;
        descriptor.primitive = primitiveState;
        descriptor.depthStencil = &depthState;
        descriptor.multisample = multisample;
        return wgpuDeviceCreateRenderPipeline(device_, &descriptor);
    };
    opaquePipeline_ = create("mesh_path_opaque_pipeline", false, true);
    blendPipeline_ = create("mesh_path_blend_pipeline", true, false);
    return opaquePipeline_ != nullptr && blendPipeline_ != nullptr;
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
            != static_cast<size_t>(data.header.vertexCount)
                * sizeof(moto::VmeshVertex)
        || data.indices.size()
            != static_cast<size_t>(data.header.indexCount)
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
    for (moto::VmeshSubmesh& submesh : submeshes) {
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
    for (const moto::VmeshMaterial& material : data.materials) {
        if (!validMaterial(material)) return false;
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
    }

    GpuAsset pending;
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
            const std::array<gpu::BindGroupEntry, 11> entries = {
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
        releaseAsset(pending);
        return false;
    }

    assets_.push_back(std::move(pending));
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

void MeshPath::setEnvironmentTexture(WGPUTextureView view) {
    if (!isInitialized() || environmentView_ == view) return;
    const WGPUTextureView previous = environmentView_;
    environmentView_ = view;
    rebuildBindGroups();
    if (boundEnvironmentView_
        != selectedEnvironment(environmentView_, fallbackEnvironmentView_)) {
        environmentView_ = previous;
    }
}

void MeshPath::setRayDepthTexture(WGPUTextureView view) {
    if (!isInitialized() || rayDepthView_ == view) return;
    const WGPUTextureView previous = rayDepthView_;
    rayDepthView_ = view;
    rebuildBindGroups();
    if (boundRayDepthView_
        != selectedEnvironment(rayDepthView_, fallbackRayDepthView_)) {
        rayDepthView_ = previous;
    }
}

void MeshPath::rebuildBindGroups() {
    if (!device_ || !bindGroupLayout_ || !instanceBuffer_ || !uniformBuffer_
        || !sampler_ || !materialSampler_ || !fallbackEnvironmentView_
        || !fallbackRayDepthView_) {
        return;
    }
    const WGPUTextureView environment = selectedEnvironment(
        environmentView_, fallbackEnvironmentView_);
    const WGPUTextureView rayDepth = selectedEnvironment(
        rayDepthView_, fallbackRayDepthView_);
    std::vector<std::vector<WGPUBindGroup>> replacements(assets_.size());
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
            const std::array<gpu::BindGroupEntry, 11> entries = {
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
            };
            replacements[assetIndex][materialIndex] = gpu::createBindGroup(
                device_, bindGroupLayout_, entries,
                "mesh_path_material_bind_group");
            if (!replacements[assetIndex][materialIndex]) {
                for (auto& groups : replacements) {
                    for (WGPUBindGroup group : groups) {
                        if (group) wgpuBindGroupRelease(group);
                    }
                }
                LOG_ERROR("MeshPath: failed to rebuild material bind groups");
                return;
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
            const std::array<gpu::BindGroupEntry, 11> entries = {
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
                releaseBuffer(replacementBuffer);
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

void MeshPath::render(WGPUCommandEncoder encoder, WGPUTextureView colorView,
                      WGPUTextureView depthView, const glm::mat4& view,
                      const glm::mat4& projection,
                      const glm::vec3& cameraPosition,
                      const PrimitiveLighting& lighting, uint32_t width,
                      uint32_t height, bool useRayDepth) {
    lastSubmittedDrawCount_ = 0u;
    lastCulledInstanceCount_ = 0u;
    if (!isInitialized() || !encoder || !colorView || !depthView) return;
    const glm::mat4 viewProj = projection * view;
    if (width == 0u || height == 0u || width > kMaximumRenderExtent
        || height > kMaximumRenderExtent || !finiteMatrix(view)
        || !finiteMatrix(projection) || !finiteMatrix(viewProj)
        || !finiteVec3(cameraPosition) || !validLighting(lighting)) {
        LOG_ERROR("MeshPath::render: invalid frame matrices, lighting, or extent");
        return;
    }
    if (instances_.empty()) return;

    std::vector<GpuDrawInstance> gpuInstances;
    std::vector<PendingDraw> draws;
    gpuInstances.reserve(std::min<size_t>(instances_.size(), maxDrawsPerFrame_));
    draws.reserve(std::min<size_t>(instances_.size(), maxDrawsPerFrame_));
    for (const MeshDrawInstance& instance : instances_) {
        if (instance.assetIndex >= assets_.size()
            || !validModelMatrix(instance.modelMatrix)
            || !finiteVec4(instance.tintColor)
            || !finiteFloat(instance.emissiveBoost)) {
            LOG_ERROR("MeshPath::render: invalid mesh instance");
            instancesValid_ = false;
            return;
        }
        const GpuAsset& asset = assets_[instance.assetIndex];
        if (!asset.vertexBuffer || !asset.indexBuffer
            || asset.materials.empty()
            || asset.materialAlphaModes.size() != asset.materials.size()
            || instance.meshIndex >= asset.meshBounds.size()
            || !asset.meshBounds[instance.meshIndex].valid) {
            LOG_ERROR("MeshPath::render: invalid mesh asset");
            instancesValid_ = false;
            return;
        }

        const MeshBounds& bounds = asset.meshBounds[instance.meshIndex];
        if (!finiteVec3(bounds.minimum) || !finiteVec3(bounds.maximum)
            || glm::any(glm::greaterThan(bounds.minimum, bounds.maximum))) {
            LOG_ERROR("MeshPath::render: invalid mesh bounds");
            instancesValid_ = false;
            return;
        }

        bool foundMesh = false;
        for (const moto::VmeshSubmesh& submesh : asset.submeshes) {
            if (submesh.meshIndex == instance.meshIndex) {
                foundMesh = true;
                break;
            }
        }
        if (!foundMesh) {
            LOG_ERROR("MeshPath::render: asset {} has no logical mesh {}",
                      instance.assetIndex, instance.meshIndex);
            instancesValid_ = false;
            return;
        }
        if (!boundsVisible(viewProj * instance.modelMatrix,
                           bounds.minimum, bounds.maximum)) {
            ++lastCulledInstanceCount_;
            continue;
        }

        const glm::vec3 localCenter =
            (bounds.minimum + bounds.maximum) * 0.5f;
        const glm::vec3 worldCenter = glm::vec3(
            instance.modelMatrix * glm::vec4(localCenter, 1.0f));
        const glm::vec3 cameraDelta = cameraPosition - worldCenter;
        const float distanceSquared = glm::dot(cameraDelta, cameraDelta);
        for (uint32_t submeshIndex = 0u;
             submeshIndex < asset.submeshes.size(); ++submeshIndex) {
            const moto::VmeshSubmesh& submesh = asset.submeshes[submeshIndex];
            if (submesh.meshIndex != instance.meshIndex) continue;
            if (gpuInstances.size() >= maxDrawsPerFrame_
                || gpuInstances.size()
                    >= std::numeric_limits<uint32_t>::max()) {
                LOG_ERROR("MeshPath::render: expanded draw count exceeds {}",
                          maxDrawsPerFrame_);
                instancesValid_ = false;
                return;
            }
            GpuDrawInstance gpuInstance;
            gpuInstance.modelMatrix = instance.modelMatrix;
            gpuInstance.tintColor = instance.tintColor;
            gpuInstance.emissiveBoost = instance.emissiveBoost;
            gpuInstance.materialIndex = submesh.materialIndex;
            const uint32_t firstInstance =
                static_cast<uint32_t>(gpuInstances.size());
            gpuInstances.push_back(gpuInstance);
            draws.push_back({
                instance.assetIndex, submeshIndex, firstInstance,
                submesh.materialIndex,
                asset.materialAlphaModes[submesh.materialIndex],
                distanceSquared});
        }
    }

    if (gpuInstances.empty()) return;
    lastSubmittedDrawCount_ = static_cast<uint32_t>(draws.size());
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
        return left.submeshIndex < right.submeshIndex;
    });

    if (!ensureInstanceCapacity(gpuInstances.size())) {
        instancesValid_ = false;
        return;
    }
    if (!gpu::writeBuffer(queue_, instanceBuffer_, 0u,
                          std::span<const GpuDrawInstance>(gpuInstances))) {
        instancesValid_ = false;
        return;
    }

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
    if (!gpu::writeBuffer(queue_, uniformBuffer_, 0u, uniforms)) {
        instancesValid_ = false;
        return;
    }
    instancesValid_ = true;

    WGPURenderPassColorAttachment colorAttachment{};
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
    passDescriptor.colorAttachmentCount = 1u;
    passDescriptor.colorAttachments = &colorAttachment;
    passDescriptor.depthStencilAttachment = &depthAttachment;
    WGPURenderPassEncoder pass =
        wgpuCommandEncoderBeginRenderPass(encoder, &passDescriptor);
    if (!pass) {
        LOG_ERROR("MeshPath::render: failed to begin render pass");
        return;
    }

    WGPURenderPipeline boundPipeline = nullptr;
    uint32_t boundAsset = std::numeric_limits<uint32_t>::max();
    uint32_t boundMaterial = std::numeric_limits<uint32_t>::max();
    for (const PendingDraw& draw : draws) {
        const GpuAsset& asset = assets_[draw.assetIndex];
        const moto::VmeshSubmesh& submesh =
            asset.submeshes[draw.submeshIndex];
        WGPURenderPipeline desiredPipeline =
            draw.alphaMode == moto::VmeshAlphaBlend
                ? blendPipeline_ : opaquePipeline_;
        if (boundPipeline != desiredPipeline) {
            boundPipeline = desiredPipeline;
            wgpuRenderPassEncoderSetPipeline(pass, boundPipeline);
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
            pass, submesh.indexCount, 1u, submesh.indexOffset / 4u,
            0, draw.firstInstance);
    }
    wgpuRenderPassEncoderEnd(pass);
    wgpuRenderPassEncoderRelease(pass);
}

}  // namespace voxy::render
