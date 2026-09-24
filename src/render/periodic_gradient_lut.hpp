#pragma once

#include "gpu/pipeline.hpp"
#include "gpu/resources.hpp"
#include "gpu/webgpu_compat.hpp"

#include <array>
#include <string_view>
#include <utility>

namespace voxy::render {

// This is the original periodicGradientHash, evaluated on the rendering device.
// Do not replace it with CPU sin(): small transcendental differences are
// amplified by the fract(sin(...) * 43758...) hash.
inline constexpr std::string_view kPeriodicGradientBakeShader = R"wgsl(
@group(0) @binding(0)
var gradients : texture_storage_2d<rgba32float, write>;

fn periodicGradientHash(cellIn : vec2<f32>) -> vec2<f32> {
    let cell = cellIn - floor(cellIn / 16.0) * 16.0;
    let phase = vec2<f32>(dot(cell, vec2<f32>(127.1, 311.7)),
                          dot(cell, vec2<f32>(269.5, 183.3)));
    return fract(sin(phase) * 43758.5453123) * 2.0 - vec2<f32>(1.0);
}

@compute @workgroup_size(8, 8)
fn main(@builtin(global_invocation_id) id : vec3<u32>) {
    if (any(id.xy >= vec2<u32>(16u))) { return; }
    let cell = vec2<f32>(id.xy);
    let coordinate = vec2<i32>(i32(id.x * 2u), i32(id.y));
    // Two adjacent RGBA texels hold all four corner gradients. Noise then
    // needs two texture loads, rather than four hash evaluations/four loads.
    textureStore(gradients, coordinate, vec4<f32>(
        periodicGradientHash(cell),
        periodicGradientHash(cell + vec2<f32>(1.0, 0.0))));
    textureStore(gradients, coordinate + vec2<i32>(1, 0), vec4<f32>(
        periodicGradientHash(cell + vec2<f32>(0.0, 1.0)),
        periodicGradientHash(cell + vec2<f32>(1.0))));
}
)wgsl";

namespace detail {
template <typename T, void (*Release)(T)>
class GradientBakeHandle {
public:
    explicit GradientBakeHandle(T value) noexcept : value_(value) {}
    ~GradientBakeHandle() { if (value_) Release(value_); }
    GradientBakeHandle(const GradientBakeHandle&) = delete;
    GradientBakeHandle& operator=(const GradientBakeHandle&) = delete;
    [[nodiscard]] T get() const noexcept { return value_; }
    [[nodiscard]] T release() noexcept {
        return std::exchange(value_, nullptr);
    }
    explicit operator bool() const noexcept { return value_ != nullptr; }
private:
    T value_;
};
} // namespace detail

// 512 RGBA32Float texels (8 KiB of texel data), one initialization submission,
// no per-frame updates and no CPU readback. Reads use textureLoad: no sampler,
// filtering, half-float conversion, or change to the noise interpolation.
class PeriodicGradientLut {
public:
    PeriodicGradientLut() = default;
    ~PeriodicGradientLut() { reset(); }
    PeriodicGradientLut(const PeriodicGradientLut&) = delete;
    PeriodicGradientLut& operator=(const PeriodicGradientLut&) = delete;
    PeriodicGradientLut(PeriodicGradientLut&& other) noexcept {
        *this = std::move(other);
    }
    PeriodicGradientLut& operator=(PeriodicGradientLut&& other) noexcept {
        if (this != &other) {
            reset();
            device_ = std::exchange(other.device_, nullptr);
            texture_ = std::exchange(other.texture_, nullptr);
            view_ = std::exchange(other.view_, nullptr);
        }
        return *this;
    }

    void reset() noexcept {
        if (view_) wgpuTextureViewRelease(std::exchange(view_, nullptr));
        // Release, rather than destroy: already submitted commands and old
        // bind groups are allowed to retain this texture until they retire.
        if (texture_) wgpuTextureRelease(std::exchange(texture_, nullptr));
        device_ = nullptr;
    }

    [[nodiscard]] WGPUTextureView view() const noexcept { return view_; }
    [[nodiscard]] WGPUTexture texture() const noexcept { return texture_; }

    [[nodiscard]] bool initialize(WGPUDevice device, WGPUQueue queue) {
        if (!device || !queue) return false;
        if (view_) return device_ == device;
        using namespace detail;

        const auto descriptor = gpu::TextureDesc::tex2D(
            32u, 16u, WGPUTextureFormat_RGBA32Float,
            WGPUTextureUsage_TextureBinding | WGPUTextureUsage_StorageBinding
                | WGPUTextureUsage_CopySrc,
            "periodic_gradient_lut");
        GradientBakeHandle<WGPUTexture, wgpuTextureRelease> texture(
            gpu::createTexture(device, descriptor));
        if (!texture) return false;
        GradientBakeHandle<WGPUTextureView, wgpuTextureViewRelease> view(
            gpu::createTextureView(texture.get()));
        if (!view) return false;
        GradientBakeHandle<WGPUShaderModule, wgpuShaderModuleRelease> module(
            gpu::createShaderModule(device, kPeriodicGradientBakeShader,
                                    "periodic_gradient_bake"));
        if (!module) return false;

        const std::array<gpu::BindGroupLayoutEntry, 1> entries = {
            gpu::BindGroupLayoutEntry(0).computeVisible().storageTexture(
                WGPUStorageTextureAccess_WriteOnly,
                WGPUTextureFormat_RGBA32Float)
        };
        GradientBakeHandle<WGPUBindGroupLayout, wgpuBindGroupLayoutRelease>
            layout(gpu::createBindGroupLayout(
                device, entries, "periodic_gradient_bake_layout"));
        if (!layout) return false;
        const std::array<WGPUBindGroupLayout, 1> layouts = {layout.get()};
        GradientBakeHandle<WGPUPipelineLayout, wgpuPipelineLayoutRelease>
            pipelineLayout(gpu::createPipelineLayout(
                device, layouts, "periodic_gradient_bake_pipeline_layout"));
        if (!pipelineLayout) return false;
        WGPUComputePipelineDescriptor pipelineDescriptor{};
        WGPU_SET_LABEL(pipelineDescriptor, "periodic_gradient_bake_pipeline");
        pipelineDescriptor.layout = pipelineLayout.get();
        pipelineDescriptor.compute.module = module.get();
        WGPU_SET_ENTRY_POINT(pipelineDescriptor.compute, "main");
        GradientBakeHandle<WGPUComputePipeline, wgpuComputePipelineRelease>
            pipeline(::voxy::gpu::createComputePipeline(device, &pipelineDescriptor));
        if (!pipeline) return false;
        const std::array<gpu::BindGroupEntry, 1> resources = {
            gpu::BindGroupEntry(0).textureView(view.get())
        };
        GradientBakeHandle<WGPUBindGroup, wgpuBindGroupRelease> group(
            gpu::createBindGroup(device, layout.get(), resources,
                                 "periodic_gradient_bake_group"));
        if (!group) return false;

        WGPUCommandEncoderDescriptor encoderDescriptor{};
        WGPU_SET_LABEL(encoderDescriptor, "periodic_gradient_bake_encoder");
        GradientBakeHandle<WGPUCommandEncoder, wgpuCommandEncoderRelease>
            encoder(wgpuDeviceCreateCommandEncoder(device, &encoderDescriptor));
        if (!encoder) return false;
        WGPUComputePassDescriptor passDescriptor{};
        WGPU_SET_LABEL(passDescriptor, "periodic_gradient_bake_pass");
        GradientBakeHandle<WGPUComputePassEncoder, wgpuComputePassEncoderRelease>
            pass(wgpuCommandEncoderBeginComputePass(encoder.get(), &passDescriptor));
        if (!pass) return false;
        wgpuComputePassEncoderSetPipeline(pass.get(), pipeline.get());
        wgpuComputePassEncoderSetBindGroup(pass.get(), 0u, group.get(), 0u, nullptr);
        wgpuComputePassEncoderDispatchWorkgroups(pass.get(), 2u, 2u, 1u);
        wgpuComputePassEncoderEnd(pass.get());
        // wgpu-native retains the command buffer through a live pass handle.
        // End recording AND release that handle before finish/submit.
        // This is host object lifetime management, not a GPU wait.
        wgpuComputePassEncoderRelease(pass.release());
        WGPUCommandBufferDescriptor commandsDescriptor{};
        GradientBakeHandle<WGPUCommandBuffer, wgpuCommandBufferRelease> commands(
            wgpuCommandEncoderFinish(encoder.get(), &commandsDescriptor));
        if (!commands) return false;
        const WGPUCommandBuffer submitted = commands.get();
        wgpuQueueSubmit(queue, 1u, &submitted);
        // The queue orders the bake before subsequent renderer submissions.
        // No onSubmittedWorkDone/mapAsync/poll/wait is needed here.
        device_ = device;
        texture_ = texture.release();
        view_ = view.release();
        return true;
    }
private:
    WGPUDevice device_ = nullptr; // identity only; owned by the renderer context
    WGPUTexture texture_ = nullptr;
    WGPUTextureView view_ = nullptr;
};

} // namespace voxy::render
