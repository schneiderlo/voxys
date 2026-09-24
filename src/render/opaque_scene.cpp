#include "gpu/pipeline.hpp"
#include "render/opaque_scene.hpp"

#include <array>

namespace voxy::render {

struct OpaqueScene::Impl {
    WGPUDevice device = nullptr;
    WGPUShaderModule shader = nullptr;
    WGPUBindGroupLayout bindings = nullptr;
    WGPUPipelineLayout layout = nullptr;
    WGPURenderPipeline pipeline = nullptr;
    WGPUBindGroup inputs = nullptr;
    WGPUTextureView sourceColor = nullptr, sourceDepth = nullptr; // inputs retains both
    struct Targets {
        WGPUTexture color = nullptr, depth = nullptr;
        WGPUTextureView colorView = nullptr, depthView = nullptr;
        uint32_t width = 0, height = 0;
        ~Targets() {
            if (colorView) wgpuTextureViewRelease(colorView);
            if (depthView) wgpuTextureViewRelease(depthView);
            if (color) wgpuTextureRelease(color);
            if (depth) wgpuTextureRelease(depth);
        }
    };
    std::unique_ptr<Targets> targets;
    ~Impl() {
        // Release, never Destroy: encoded commands may retain these resources.
        targets.reset();
        if (inputs) wgpuBindGroupRelease(inputs);
        if (pipeline) wgpuRenderPipelineRelease(pipeline);
        if (layout) wgpuPipelineLayoutRelease(layout);
        if (bindings) wgpuBindGroupLayoutRelease(bindings);
        if (shader) wgpuShaderModuleRelease(shader);
        if (device) wgpuDeviceRelease(device);
    }
};

OpaqueScene::OpaqueScene() = default;
OpaqueScene::~OpaqueScene() = default;

bool OpaqueScene::init(WGPUDevice device, const std::filesystem::path& shader) {
    if (!device || shader.empty()) return false;
    auto next = std::make_unique<Impl>();
    next->device = device;
#if defined(VOXY_WASM)
    wgpuDeviceAddRef(device);
#else
    wgpuDeviceReference(device);
#endif
    next->shader = gpu::loadShaderModule(device, shader, "opaque_scene_seed");
    if (!next->shader) return false;
    const std::array entries{
        gpu::BindGroupLayoutEntry(0).fragmentVisible().texture(WGPUTextureSampleType_UnfilterableFloat),
        gpu::BindGroupLayoutEntry(1).fragmentVisible().texture(WGPUTextureSampleType_UnfilterableFloat)};
    next->bindings = gpu::createBindGroupLayout(device, entries, "opaque_scene_seed_inputs");
    if (!next->bindings) return false;
    next->layout = gpu::createPipelineLayout(device,
        std::span<const WGPUBindGroupLayout>(&next->bindings, 1), "opaque_scene_seed_layout");
    if (!next->layout) return false;
    std::array<WGPUColorTargetState, 2> targets{};
    targets[0].format = WGPUTextureFormat_RGBA16Float;
    targets[1].format = WGPUTextureFormat_R32Float;
    for (auto& target : targets) target.writeMask = WGPUColorWriteMask_All;
    WGPUFragmentState fragment{};
    fragment.module = next->shader;
    WGPU_SET_ENTRY_POINT(fragment, "fs");
    fragment.targetCount = targets.size();
    fragment.targets = targets.data();
    WGPURenderPipelineDescriptor pipeline{};
    WGPU_SET_LABEL(pipeline, "opaque_scene_seed");
    pipeline.layout = next->layout;
    pipeline.vertex.module = next->shader;
    WGPU_SET_ENTRY_POINT(pipeline.vertex, "vs");
    pipeline.fragment = &fragment;
    pipeline.primitive.topology = WGPUPrimitiveTopology_TriangleList;
    pipeline.primitive.frontFace = WGPUFrontFace_CCW;
    pipeline.primitive.cullMode = WGPUCullMode_None;
    pipeline.multisample.count = 1;
    pipeline.multisample.mask = 0xffffffffu;
    next->pipeline = ::voxy::gpu::createRenderPipeline(device, &pipeline);
    if (!next->pipeline) return false;
    impl_ = std::move(next);
    return true;
}

bool OpaqueScene::resize(uint32_t width, uint32_t height) {
    if (!impl_ || !width || !height || width > 8192 || height > 8192
        || uint64_t{width} * height > maximumBytes / bytesPerPixel) return false;
    if (impl_->targets && impl_->targets->width == width && impl_->targets->height == height) return true;
    auto next = std::make_unique<Impl::Targets>();
    next->width = width; next->height = height;
    auto desc = gpu::TextureDesc::renderTarget(width, height,
        WGPUTextureFormat_RGBA16Float, "opaque_scene_color");
    desc.usage |= WGPUTextureUsage_CopySrc;
    next->color = gpu::createTexture(impl_->device, desc);
    desc.format = WGPUTextureFormat_R32Float;
    desc.label = "opaque_scene_radial_depth";
    next->depth = gpu::createTexture(impl_->device, desc);
    if (!next->color || !next->depth) return false;
    next->colorView = gpu::createTextureView(next->color, {});
    next->depthView = gpu::createTextureView(next->depth, {});
    if (!next->colorView || !next->depthView) return false;
    impl_->targets = std::move(next);
    return true;
}

bool OpaqueScene::seed(WGPUCommandEncoder encoder, WGPUTextureView terrainColor,
                       WGPUTextureView terrainDepth, WGPUQuerySet query, uint32_t beginQuery) {
    if (!impl_ || !impl_->targets || !encoder || !terrainColor || !terrainDepth) return false;
    const auto& target = *impl_->targets;
    if (terrainColor == target.colorView || terrainColor == target.depthView
        || terrainDepth == target.colorView || terrainDepth == target.depthView) return false;
    // Callers supply their full-sized cache views. WebGPU also validates their
    // usage/format and rejects aliasing through different views of a texture.
    if (terrainColor != impl_->sourceColor || terrainDepth != impl_->sourceDepth) {
        const std::array entries{gpu::BindGroupEntry(0).textureView(terrainColor),
                                 gpu::BindGroupEntry(1).textureView(terrainDepth)};
        auto inputs = gpu::createBindGroup(impl_->device, impl_->bindings, entries, "opaque_scene_seed_inputs");
        if (!inputs) return false;
        if (impl_->inputs) wgpuBindGroupRelease(impl_->inputs);
        impl_->inputs = inputs;
        impl_->sourceColor = terrainColor; impl_->sourceDepth = terrainDepth;
    }
    std::array<WGPURenderPassColorAttachment, 2> attachments{};
    attachments[0].view = target.colorView;
    attachments[1].view = target.depthView;
    for (auto& attachment : attachments) {
        attachment.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
        attachment.loadOp = WGPULoadOp_Clear;
        attachment.storeOp = WGPUStoreOp_Store;
    }
    WGPURenderPassDescriptor desc{};
    WGPU_SET_LABEL(desc, "opaque_scene_seed");
    desc.colorAttachmentCount = attachments.size();
    desc.colorAttachments = attachments.data();
    gpu::CompatRenderPassTimestampWrites timestamps{};
    if (query && beginQuery != WGPU_QUERY_SET_INDEX_UNDEFINED) {
        timestamps.querySet = query;
        timestamps.beginningOfPassWriteIndex = beginQuery;
        timestamps.endOfPassWriteIndex = WGPU_QUERY_SET_INDEX_UNDEFINED;
        desc.timestampWrites = &timestamps;
    }
    auto pass = wgpuCommandEncoderBeginRenderPass(encoder, &desc);
    if (!pass) return false;
    wgpuRenderPassEncoderSetPipeline(pass, impl_->pipeline);
    wgpuRenderPassEncoderSetBindGroup(pass, 0, impl_->inputs, 0, nullptr);
    wgpuRenderPassEncoderDraw(pass, 3, 1, 0, 0);
    wgpuRenderPassEncoderEnd(pass);
    wgpuRenderPassEncoderRelease(pass);
    return true;
}

WGPUTextureView OpaqueScene::colorView() const noexcept { return impl_ && impl_->targets ? impl_->targets->colorView : nullptr; }
WGPUTextureView OpaqueScene::depthView() const noexcept { return impl_ && impl_->targets ? impl_->targets->depthView : nullptr; }
WGPUTexture OpaqueScene::colorTexture() const noexcept { return impl_ && impl_->targets ? impl_->targets->color : nullptr; }
WGPUTexture OpaqueScene::depthTexture() const noexcept { return impl_ && impl_->targets ? impl_->targets->depth : nullptr; }
uint64_t OpaqueScene::requestedBytes() const noexcept {
    return impl_ && impl_->targets ? uint64_t{impl_->targets->width} * impl_->targets->height * bytesPerPixel : 0;
}

} // namespace voxy::render
