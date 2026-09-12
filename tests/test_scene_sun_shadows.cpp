#include "gpu/context.hpp"
#include "gpu/resources.hpp"
#include "render/scene_shadows.hpp"
#include <gtest/gtest.h>
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#if defined(VOXY_NATIVE)
namespace voxy::render {
namespace {
struct ShadowResources {
    WGPUTexture depth = nullptr;
    WGPUTextureView view = nullptr;
    WGPUSampler sampler = nullptr;
    std::array<WGPUBuffer, 5> buffers{};
    std::array<WGPUBindGroupLayout, 3> layouts{};
    std::array<WGPUBindGroup, 3> groups{};
    std::array<WGPUPipelineLayout, 2> pipelineLayouts{};
    std::array<WGPUShaderModule, 2> shaders{};
    WGPURenderPipeline caster = nullptr;
    WGPUComputePipeline receiver = nullptr;
    WGPUCommandEncoder encoder = nullptr;
    WGPUCommandBuffer command = nullptr;
    ~ShadowResources() {
        if (command) wgpuCommandBufferRelease(command);
        if (encoder) wgpuCommandEncoderRelease(encoder);
        if (receiver) wgpuComputePipelineRelease(receiver);
        if (caster) wgpuRenderPipelineRelease(caster);
        for (auto value : shaders) if (value) wgpuShaderModuleRelease(value);
        for (auto value : pipelineLayouts) if (value) wgpuPipelineLayoutRelease(value);
        for (auto value : groups) if (value) wgpuBindGroupRelease(value);
        for (auto value : layouts) if (value) wgpuBindGroupLayoutRelease(value);
        for (auto value : buffers) if (value) { wgpuBufferDestroy(value); wgpuBufferRelease(value); }
        if (sampler) wgpuSamplerRelease(sampler);
        if (view) wgpuTextureViewRelease(view);
        if (depth) { wgpuTextureDestroy(depth); wgpuTextureRelease(depth); }
    }
};

// Independent geometric oracle: intersect a light ray with a world-space plane.
// This does not reproduce the shader's receiver-plane depth-gradient formula.
glm::vec3 planePoint(const glm::mat4& inverse, glm::vec2 uv, float height) {
    const auto ndc = (uv - glm::vec2(.5f)) * glm::vec2(2, -2);
    const auto a = inverse * glm::vec4(ndc, 0, 1);
    const auto b = inverse * glm::vec4(ndc, 1, 1);
    const auto start = glm::vec3(a) / a.w;
    const auto end = glm::vec3(b) / b.w;
    return start + (end - start) * ((height - start.y) / (end.y - start.y));
}
} // namespace

TEST(SceneSunShadowsGPU, ReceiverPlanePcfRemovesFlatDeckAcneWithoutLosingNearContact) {
    gpu::Context context;
    ASSERT_TRUE(context.initHeadless());
    context.setErrorCallback([](WGPUErrorType, const char* message) { ADD_FAILURE() << message; });
    const auto device = context.getDevice();
    const auto queue = context.getQueue();
    ShadowResources resources;
    // Same resolution, orthographic extent, sun direction, caster raster bias,
    // receiver bias and bilinear 3x3 comparison kernel as the playable Cove.
    constexpr uint32_t resolution = 1024;
    constexpr size_t clearCount = 256;
    constexpr size_t contactCount = 64;
    constexpr size_t count = clearCount + contactCount;
    constexpr size_t outputBytes = count * sizeof(glm::vec2);
    const auto light = glm::normalize(glm::vec3(.4f, .8f, -.4f));
    const glm::vec3 center(0, 3, 0);
    SunShadowUniforms uniforms;
    uniforms.viewProj = glm::orthoLH_ZO(-24.0f, 24.0f, -24.0f, 24.0f, 0.0f, 128.0f)
        * glm::lookAtLH(center + light * 64.0f, center, glm::vec3(0, 1, 0));
    const auto origin = uniforms.viewProj * glm::vec4(0, 0, 0, 1);
    constexpr float snapScale = static_cast<float>(resolution) * .5f;
    uniforms.viewProj[3].x += (std::round(origin.x * snapScale) - origin.x * snapScale) / snapScale;
    uniforms.viewProj[3].y += (std::round(origin.y * snapScale) - origin.y * snapScale) / snapScale;
    uniforms.params = {1, 48.0f / resolution, 1.0f / 128.0f, 0};
    const auto inverse = glm::inverse(uniforms.viewProj);
    std::vector<glm::vec4> vertices;
    // Full-screen receiver plus an actual raised 6 cm caster, not a fabricated
    // depth texture. Raster interpolation and hardware depth bias stay active.
    for (const auto uv : std::array<glm::vec2, 3>{{{0, 0}, {2, 0}, {0, 2}}}) {
        vertices.push_back(uniforms.viewProj * glm::vec4(planePoint(inverse, uv, 0), 1));
    }
    for (const auto uv : std::array<glm::vec2, 6>{{{.44f,.44f},{.56f,.44f},{.44f,.56f},
                                                 {.44f,.56f},{.56f,.44f},{.56f,.56f}}}) {
        vertices.push_back(uniforms.viewProj * glm::vec4(planePoint(inverse, uv, .06f), 1));
    }
    std::array<glm::vec4, count> samples{};
    for (size_t i = 0; i < count; ++i) {
        const auto phase = glm::vec2(static_cast<float>(i % 16) / 16.0f,
                                    static_cast<float>((i / 16) % 16) / 16.0f);
        const float base = i < clearCount ? .30f : .50f;
        const auto uv = glm::vec2(base) + phase / static_cast<float>(resolution);
        samples[i] = glm::vec4(planePoint(inverse, uv, 0), 1);
    }
    resources.buffers[0] = gpu::createBuffer(device, gpu::BufferDesc::uniform(sizeof(uniforms)));
    resources.buffers[1] = gpu::createBuffer(device, gpu::BufferDesc::storage(vertices.size() * sizeof(glm::vec4), true));
    resources.buffers[2] = gpu::createBuffer(device, gpu::BufferDesc::storage(sizeof(samples), true));
    resources.buffers[3] = gpu::createBuffer(device, gpu::BufferDesc::storage(outputBytes));
    resources.buffers[4] = gpu::createBuffer(device, gpu::BufferDesc{
        .size=outputBytes, .usage=WGPUBufferUsage_CopyDst | WGPUBufferUsage_MapRead});
    for (auto buffer : resources.buffers) ASSERT_NE(buffer, nullptr);
    ASSERT_TRUE(gpu::writeBuffer(queue, resources.buffers[0], 0, uniforms));
    ASSERT_TRUE(gpu::writeBuffer(queue, resources.buffers[1], 0, std::as_bytes(std::span(vertices))));
    ASSERT_TRUE(gpu::writeBuffer(queue, resources.buffers[2], 0, samples));
    resources.depth = gpu::createTexture(device, gpu::TextureDesc::depth(resolution, resolution));
    ASSERT_NE(resources.depth, nullptr);
    resources.view = gpu::createTextureView(resources.depth);
    WGPUSamplerDescriptor sampler{};
    sampler.addressModeU = sampler.addressModeV = sampler.addressModeW = WGPUAddressMode_ClampToEdge;
    sampler.minFilter = sampler.magFilter = WGPUFilterMode_Linear;
    sampler.mipmapFilter = WGPUMipmapFilterMode_Nearest;
    sampler.compare = WGPUCompareFunction_LessEqual;
    sampler.lodMaxClamp = 32;
    sampler.maxAnisotropy = 1;
    resources.sampler = wgpuDeviceCreateSampler(device, &sampler);
    ASSERT_NE(resources.view, nullptr); ASSERT_NE(resources.sampler, nullptr);

    using Layout = gpu::BindGroupLayoutEntry;
    using Entry = gpu::BindGroupEntry;
    resources.layouts[0] = gpu::createBindGroupLayout(device, std::array{
        Layout(0).computeVisible().uniformBuffer(false, sizeof(uniforms)),
        Layout(1).computeVisible().texture(WGPUTextureSampleType_Depth),
        Layout(2).computeVisible().sampler(WGPUSamplerBindingType_Comparison)});
    resources.layouts[1] = gpu::createBindGroupLayout(device, std::array{
        Layout(0).computeVisible().storageBuffer(true), Layout(1).computeVisible().storageBuffer(false)});
    resources.layouts[2] = gpu::createBindGroupLayout(device, std::array{Layout(0).vertexVisible().storageBuffer(true)});
    resources.groups[0] = gpu::createBindGroup(device, resources.layouts[0], std::array{
        Entry(0).buffer(resources.buffers[0]), Entry(1).textureView(resources.view), Entry(2).sampler(resources.sampler)});
    resources.groups[1] = gpu::createBindGroup(device, resources.layouts[1], std::array{
        Entry(0).buffer(resources.buffers[2]), Entry(1).buffer(resources.buffers[3])});
    resources.groups[2] = gpu::createBindGroup(device, resources.layouts[2], std::array{Entry(0).buffer(resources.buffers[1])});
    resources.pipelineLayouts[0] = gpu::createPipelineLayout(device, std::span(resources.layouts).first<2>());
    resources.pipelineLayouts[1] = gpu::createPipelineLayout(device, std::span(resources.layouts).last<1>());
    for (auto group : resources.groups) ASSERT_NE(group, nullptr);

    std::ifstream sourceFile("shaders/scene_sun_shadow.wgsl.in");
    ASSERT_TRUE(sourceFile.good());
    std::string source{std::istreambuf_iterator<char>(sourceFile), {}};
    for (auto at = source.find("SHADOW_GROUP"); at != std::string::npos; at = source.find("SHADOW_GROUP")) {
        source.replace(at, std::string("SHADOW_GROUP").size(), "0");
    }
    // A mutation control proves the observed flat-receiver defect on the same
    // hardware. Only this test variant restores the old constant tap reference.
    const std::string correction = "reference + dot(depthGradient, vec2<f32>(f32(x), f32(y)) * texel)";
    ASSERT_NE(source.find(correction), std::string::npos);
    auto old = source.substr(source.find("fn sunVisibility"));
    old.replace(old.find(correction), correction.size(), "reference");
    for (auto at = old.find("sunVisibility"); at != std::string::npos; at = old.find("sunVisibility", at + 16)) {
        old.replace(at, std::string("sunVisibility").size(), "oldSunVisibility");
    }
    old.replace(old.find("sceneSunVisibility"), std::string("sceneSunVisibility").size(), "oldSceneSunVisibility");
    source += old + R"(
@group(1) @binding(0) var<storage, read> points: array<vec4<f32>>;
@group(1) @binding(1) var<storage, read_write> result: array<vec2<f32>>;
@compute @workgroup_size(64)
fn sampleReceiver(@builtin(global_invocation_id) id: vec3<u32>) {
    if (id.x >= arrayLength(&points)) { return; }
    let light = normalize(vec3<f32>(.4, .8, -.4));
    result[id.x] = vec2<f32>(sunVisibility(points[id.x].xyz, vec3<f32>(0,1,0), light),
        oldSunVisibility(points[id.x].xyz, vec3<f32>(0,1,0), light));
}
)";
    resources.shaders[0] = gpu::createShaderModule(device, source, "receiver_plane_pcf_oracle");
    resources.shaders[1] = gpu::createShaderModule(device, R"(
@group(0) @binding(0) var<storage, read> vertices: array<vec4<f32>>;
@vertex fn castPlane(@builtin(vertex_index) index: u32) -> @builtin(position) vec4<f32> {
    return vertices[index];
}
)", "receiver_plane_caster");
    WGPUComputePipelineDescriptor compute{};
    compute.layout = resources.pipelineLayouts[0];
    compute.compute.module = resources.shaders[0];
    WGPU_SET_ENTRY_POINT(compute.compute, "sampleReceiver");
    resources.receiver = wgpuDeviceCreateComputePipeline(device, &compute);
    WGPUDepthStencilState depthState{};
    depthState.format = WGPUTextureFormat_Depth32Float;
    depthState.depthWriteEnabled = gpu::toOptionalBool(true);
    depthState.depthCompare = WGPUCompareFunction_LessEqual;
    depthState.depthBias = 1; depthState.depthBiasSlopeScale = 1.0f;
    depthState.stencilFront.compare = WGPUCompareFunction_Always;
    depthState.stencilFront.failOp = depthState.stencilFront.depthFailOp = depthState.stencilFront.passOp = WGPUStencilOperation_Keep;
    depthState.stencilBack = depthState.stencilFront;
    depthState.stencilReadMask = depthState.stencilWriteMask = 0xff;
    WGPURenderPipelineDescriptor render{};
    render.layout = resources.pipelineLayouts[1];
    render.vertex.module = resources.shaders[1];
    WGPU_SET_ENTRY_POINT(render.vertex, "castPlane");
    render.primitive.topology = WGPUPrimitiveTopology_TriangleList;
    render.primitive.frontFace = WGPUFrontFace_CCW;
    render.primitive.cullMode = WGPUCullMode_None;
    render.depthStencil = &depthState;
    render.multisample.count = 1; render.multisample.mask = ~0u;
    resources.caster = wgpuDeviceCreateRenderPipeline(device, &render);
    ASSERT_NE(resources.receiver, nullptr); ASSERT_NE(resources.caster, nullptr);
    WGPUCommandEncoderDescriptor encoderDescriptor{};
    resources.encoder = wgpuDeviceCreateCommandEncoder(device, &encoderDescriptor);
    WGPURenderPassDepthStencilAttachment depth{};
    depth.view = resources.view; depth.depthClearValue = 1;
    depth.depthLoadOp = WGPULoadOp_Clear; depth.depthStoreOp = WGPUStoreOp_Store;
    depth.stencilReadOnly = true;
    WGPURenderPassDescriptor passDescriptor{};
    passDescriptor.depthStencilAttachment = &depth;
    auto pass = wgpuCommandEncoderBeginRenderPass(resources.encoder, &passDescriptor);
    wgpuRenderPassEncoderSetPipeline(pass, resources.caster);
    wgpuRenderPassEncoderSetBindGroup(pass, 0, resources.groups[2], 0, nullptr);
    wgpuRenderPassEncoderDraw(pass, static_cast<uint32_t>(vertices.size()), 1, 0, 0);
    wgpuRenderPassEncoderEnd(pass); wgpuRenderPassEncoderRelease(pass);
    WGPUComputePassDescriptor computePassDescriptor{};
    auto computePass = wgpuCommandEncoderBeginComputePass(resources.encoder, &computePassDescriptor);
    wgpuComputePassEncoderSetPipeline(computePass, resources.receiver);
    wgpuComputePassEncoderSetBindGroup(computePass, 0, resources.groups[0], 0, nullptr);
    wgpuComputePassEncoderSetBindGroup(computePass, 1, resources.groups[1], 0, nullptr);
    wgpuComputePassEncoderDispatchWorkgroups(computePass, static_cast<uint32_t>((count + 63) / 64), 1, 1);
    wgpuComputePassEncoderEnd(computePass); wgpuComputePassEncoderRelease(computePass);
    wgpuCommandEncoderCopyBufferToBuffer(resources.encoder, resources.buffers[3], 0, resources.buffers[4], 0, outputBytes);
    WGPUCommandBufferDescriptor commandDescriptor{};
    resources.command = wgpuCommandEncoderFinish(resources.encoder, &commandDescriptor);
    ASSERT_NE(resources.command, nullptr);
    wgpuQueueSubmit(queue, 1, &resources.command);
    const auto state = std::make_shared<std::atomic<int>>(0);
    using Payload = std::shared_ptr<std::atomic<int>>;
    wgpuBufferMapAsync(resources.buffers[4], WGPUMapMode_Read, 0, outputBytes,
        [](WGPUBufferMapAsyncStatus status, void* data) {
            const std::unique_ptr<Payload> completion(static_cast<Payload*>(data));
            (*completion)->store(status == WGPUBufferMapAsyncStatus_Success ? 1 : 2, std::memory_order_release);
        }, new Payload(state));
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
    while (!state->load(std::memory_order_acquire) && std::chrono::steady_clock::now() < deadline) {
        context.tick();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    ASSERT_EQ(state->load(std::memory_order_acquire), 1);
    const auto* values = static_cast<const glm::vec2*>(wgpuBufferGetConstMappedRange(resources.buffers[4], 0, outputBytes));
    ASSERT_NE(values, nullptr);
    float clearMinimum = 1, oldMinimum = 1, contactMaximum = 0;
    size_t oldAcneSamples = 0;
    for (size_t i = 0; i < count; ++i) {
        EXPECT_TRUE(std::isfinite(values[i].x));
        EXPECT_GE(values[i].x, 0); EXPECT_LE(values[i].x, 1);
        if (i < clearCount) {
            clearMinimum = std::min(clearMinimum, values[i].x);
            oldMinimum = std::min(oldMinimum, values[i].y);
            if (values[i].y < .995f) ++oldAcneSamples;
        } else {
            contactMaximum = std::max(contactMaximum, values[i].x);
        }
    }
    wgpuBufferUnmap(resources.buffers[4]);
    RecordProperty("clearMinimum", clearMinimum);
    RecordProperty("oldMinimum", oldMinimum);
    RecordProperty("oldAcneSamples", static_cast<int>(oldAcneSamples));
    RecordProperty("contactMaximum", contactMaximum);
    EXPECT_GE(clearMinimum, .9999f);
    EXPECT_GT(oldAcneSamples, 0u) << "Mutation control must reproduce constant-reference self-shadowing";
    EXPECT_LT(contactMaximum, .05f) << "The 6 cm occluder must retain a near-contact shadow";
}
} // namespace voxy::render
#endif
