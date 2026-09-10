#include "render/environment_lighting.hpp"

#include "gpu/resources.hpp"

#include <array>
#include <bit>
#include <cstddef>
#include <utility>

namespace voxy::render {
namespace {

struct alignas(16) BakeParams {
    uint32_t size;
    uint32_t kind;
    uint32_t samples;
    float roughness;
};
static_assert(sizeof(BakeParams) == 16);
static_assert(offsetof(BakeParams, roughness) == 12);

bool validExtent(uint32_t size, uint32_t minimum, uint32_t maximum) {
    return size >= minimum && size <= maximum && std::has_single_bit(size);
}

} // namespace

struct EnvironmentLighting::Impl {
    WGPUDevice device = nullptr;
    WGPUShaderModule shader = nullptr;
    WGPUBindGroupLayout bindings = nullptr;
    WGPUPipelineLayout layout = nullptr;
    WGPUComputePipeline pipeline = nullptr;
    WGPUSampler sampler = nullptr;
    std::array<WGPUTexture, 3> textures{};
    std::array<WGPUTextureView, 3> sampled{};
    struct Pass {
        WGPUTextureView output = nullptr;
        WGPUBuffer uniform = nullptr;
        uint32_t size = 0;
        uint32_t layers = 0;
    };
    // At most ten specular mips (512..1), diffuse and BRDF.
    std::array<Pass, 12> passes{};
    size_t passCount = 0;
    uint64_t bytes = 0;

    ~Impl() {
        for (auto& pass : passes) {
            if (pass.output) wgpuTextureViewRelease(pass.output);
            if (pass.uniform) wgpuBufferRelease(pass.uniform);
        }
        for (auto view : sampled) if (view) wgpuTextureViewRelease(view);
        for (auto texture : textures) if (texture) wgpuTextureRelease(texture);
        if (sampler) wgpuSamplerRelease(sampler);
        if (pipeline) wgpuComputePipelineRelease(pipeline);
        if (layout) wgpuPipelineLayoutRelease(layout);
        if (bindings) wgpuBindGroupLayoutRelease(bindings);
        if (shader) wgpuShaderModuleRelease(shader);
        if (device) wgpuDeviceRelease(device);
    }
};

EnvironmentLighting::EnvironmentLighting() = default;
EnvironmentLighting::~EnvironmentLighting() = default;

bool EnvironmentLighting::init(WGPUDevice device, WGPUQueue queue,
                                const EnvironmentLightingConfig& config) {
    if (!device || !queue || config.shaderPath.empty()
        || !validExtent(config.specularSize, 16, 512)
        || !validExtent(config.diffuseSize, 4, 64)
        || !validExtent(config.brdfSize, 16, 256)
        || config.samples < 64 || config.samples > 4096) return false;

    auto next = std::make_unique<Impl>();
    next->device = device;
#if defined(VOXY_WASM)
    wgpuDeviceAddRef(device);
#else
    wgpuDeviceReference(device);
#endif
    next->shader = gpu::loadShaderModule(device, config.shaderPath, "environment_lighting");
    if (!next->shader) return false;
    const std::array entries{
        gpu::BindGroupLayoutEntry(0).computeVisible().uniformBuffer(false, sizeof(BakeParams)),
        gpu::BindGroupLayoutEntry(1).computeVisible().texture(),
        gpu::BindGroupLayoutEntry(2).computeVisible().sampler(),
        gpu::BindGroupLayoutEntry(3).computeVisible().storageTexture(
            WGPUStorageTextureAccess_WriteOnly, WGPUTextureFormat_RGBA16Float,
            WGPUTextureViewDimension_2DArray)};
    next->bindings = gpu::createBindGroupLayout(device, entries, "environment_bake_bindings");
    if (!next->bindings) return false;
    next->layout = gpu::createPipelineLayout(device,
        std::span<const WGPUBindGroupLayout>(&next->bindings, 1), "environment_bake_layout");
    if (!next->layout) return false;
    WGPUComputePipelineDescriptor pipelineDesc{};
    WGPU_SET_LABEL(pipelineDesc, "environment_bake");
    pipelineDesc.layout = next->layout;
    pipelineDesc.compute.module = next->shader;
    WGPU_SET_ENTRY_POINT(pipelineDesc.compute, "bake");
    next->pipeline = wgpuDeviceCreateComputePipeline(device, &pipelineDesc);
    auto samplerDesc = gpu::SamplerDesc::linear("environment_bake_source");
    samplerDesc.addressModeU = WGPUAddressMode_Repeat;
    next->sampler = gpu::createSampler(device, samplerDesc);
    if (!next->pipeline || !next->sampler) return false;

    const std::array sizes{config.specularSize, config.diffuseSize, config.brdfSize};
    for (uint32_t kind = 0; kind < 3; ++kind) {
        const uint32_t levels = kind == 0 ? static_cast<uint32_t>(std::bit_width(sizes[kind])) : 1u;
        const uint32_t layers = kind == 2 ? 1u : 6u;
        gpu::TextureDesc textureDesc{
            .label = "filtered_environment", .width = sizes[kind], .height = sizes[kind],
            .depthOrArrayLayers = layers, .mipLevelCount = levels,
            .format = WGPUTextureFormat_RGBA16Float,
            .usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_StorageBinding};
        next->textures[kind] = gpu::createTexture(device, textureDesc);
        if (!next->textures[kind]) return false;
        next->sampled[kind] = gpu::createTextureView(next->textures[kind], {
            .label = "filtered_environment_sampled",
            .dimension = kind == 2 ? WGPUTextureViewDimension_2D : WGPUTextureViewDimension_Cube,
            .mipLevelCount = levels, .arrayLayerCount = layers});
        if (!next->sampled[kind]) return false;
        for (uint32_t mip = 0; mip < levels; ++mip) {
            auto& pass = next->passes[next->passCount++];
            pass.size = sizes[kind] >> mip;
            pass.layers = layers;
            pass.output = gpu::createTextureView(next->textures[kind], {
                .label = "environment_bake_output", .dimension = WGPUTextureViewDimension_2DArray,
                .baseMipLevel = mip, .arrayLayerCount = layers});
            const BakeParams params{pass.size, kind, config.samples,
                kind == 0 ? static_cast<float>(mip) / static_cast<float>(levels - 1u) : 0.0f};
            pass.uniform = gpu::createBufferWithData(device, queue,
                gpu::BufferDesc::uniform(sizeof(params), "environment_bake_params"),
                std::as_bytes(std::span(&params, 1)));
            if (!pass.output || !pass.uniform) return false;
            next->bytes += uint64_t{pass.size} * pass.size * layers * 8u + sizeof(params);
        }
    }
    impl_ = std::move(next);
    return true;
}

bool EnvironmentLighting::encodeBake(WGPUCommandEncoder encoder, WGPUTextureView source) {
    if (!impl_ || !encoder || !source) return false;
    struct Groups {
        std::array<WGPUBindGroup, 12> handles{};
        ~Groups() { for (auto handle : handles) if (handle) wgpuBindGroupRelease(handle); }
    } groups;
    auto& state = *impl_;
    // Finish all fallible CPU setup before opening a compute pass.
    for (size_t i = 0; i < state.passCount; ++i) {
        const std::array entries{
            gpu::BindGroupEntry(0).buffer(state.passes[i].uniform, 0, sizeof(BakeParams)),
            gpu::BindGroupEntry(1).textureView(source),
            gpu::BindGroupEntry(2).sampler(state.sampler),
            gpu::BindGroupEntry(3).textureView(state.passes[i].output)};
        groups.handles[i] = gpu::createBindGroup(state.device, state.bindings, entries, "environment_bake_group");
        if (!groups.handles[i]) return false;
    }
    WGPUComputePassDescriptor desc{};
    WGPU_SET_LABEL(desc, "environment_lighting_bake");
    WGPUComputePassEncoder pass = wgpuCommandEncoderBeginComputePass(encoder, &desc);
    if (!pass) return false;
    wgpuComputePassEncoderSetPipeline(pass, state.pipeline);
    for (size_t i = 0; i < state.passCount; ++i) {
        wgpuComputePassEncoderSetBindGroup(pass, 0, groups.handles[i], 0, nullptr);
        const uint32_t groupsPerAxis = (state.passes[i].size + 7u) / 8u;
        wgpuComputePassEncoderDispatchWorkgroups(pass, groupsPerAxis, groupsPerAxis, state.passes[i].layers);
    }
    wgpuComputePassEncoderEnd(pass);
    wgpuComputePassEncoderRelease(pass);
    return true;
}

void EnvironmentLighting::releaseHandles() noexcept { impl_.reset(); }

FilteredEnvironmentViews EnvironmentLighting::views() const noexcept {
    return impl_ ? FilteredEnvironmentViews{impl_->sampled[0], impl_->sampled[1], impl_->sampled[2]}
                 : FilteredEnvironmentViews{};
}

uint64_t EnvironmentLighting::requestedBytes() const noexcept { return impl_ ? impl_->bytes : 0; }

} // namespace voxy::render
