// Standalone WASM regression probe. Compile with the project's Emscripten SDK:
// em++ scripts/fixtures/startup_pipeline_probe.cpp -Isrc -DVOXY_WASM -std=c++20 \
//   --use-port=emdawnwebgpu -sASYNCIFY -sMODULARIZE -sEXPORT_NAME=PipelineProbe \
//   -sENVIRONMENT=web -o pipeline-probe.js
// Supply preinitializedWebGPUDevice. The browser harness can pause positive
// setTimeout calls after the first "Preparing graphics" status to verify that
// compilation completion does not depend on polling timers.
#include "gpu/pipeline.hpp"

#include <array>
#include <cassert>
#include <cstdio>

extern "C" WGPUDevice emscripten_webgpu_get_device();

int main() {
    const auto device = emscripten_webgpu_get_device();
    assert(device);
    WGPUShaderSourceWGSL code = WGPU_SHADER_SOURCE_WGSL_INIT;
    code.code = voxy::gpu::toStringView(
        "override SIZE: u32 = 1u;"
        "@compute @workgroup_size(SIZE) fn main() {}"
        "@vertex fn vs() -> @builtin(position) vec4<f32> {"
        "return vec4<f32>(0, 0, 0, 1); }"
        "@fragment fn fs() -> @location(0) vec4<f32> {"
        "return vec4<f32>(1, 0, 0, 1); }");
    WGPUShaderModuleDescriptor shader = WGPU_SHADER_MODULE_DESCRIPTOR_INIT;
    shader.nextInChain = &code.chain;
    const auto module = wgpuDeviceCreateShaderModule(device, &shader);
    assert(module);
    {
        voxy::gpu::StartupPipelineCompilation startup;
        std::array<WGPUComputePipeline, 24> pipelines{};
        WGPUComputePipeline invalid = nullptr;
        {
            voxy::gpu::ComputePipelineBatch batch;
            for (size_t index = 0; index < pipelines.size(); ++index) {
                // Descriptors/constants are stack locals reused on each call.
                WGPUConstantEntry constant{};
                constant.key = voxy::gpu::toStringView("SIZE");
                constant.value = static_cast<double>(index + 1);
                WGPUComputePipelineDescriptor desc = WGPU_COMPUTE_PIPELINE_DESCRIPTOR_INIT;
                desc.compute.module = module;
                desc.compute.entryPoint = voxy::gpu::toStringView("main");
                desc.compute.constantCount = 1;
                desc.compute.constants = &constant;
                batch.add(device, desc, pipelines[index]);
            }
            WGPUComputePipelineDescriptor desc = WGPU_COMPUTE_PIPELINE_DESCRIPTOR_INIT;
            desc.compute.module = module;
            desc.compute.entryPoint = voxy::gpu::toStringView("missing");
            batch.add(device, desc, invalid);
            batch.wait();
            batch.wait(); // Idempotent: no promise or handle is released twice.
        }
        assert(!invalid);
        for (const auto pipeline : pipelines) {
            assert(pipeline);
            wgpuComputePipelineRelease(pipeline);
        }
        WGPUComputePipeline drained = nullptr;
        {
            voxy::gpu::ComputePipelineBatch batch;
            WGPUComputePipelineDescriptor desc = WGPU_COMPUTE_PIPELINE_DESCRIPTOR_INIT;
            desc.compute.module = module;
            desc.compute.entryPoint = voxy::gpu::toStringView("main");
            batch.add(device, desc, drained);
            // Scope exit must drain callbacks even without an explicit wait.
        }
        assert(drained);
        wgpuComputePipelineRelease(drained);

        WGPUColorTargetState target = WGPU_COLOR_TARGET_STATE_INIT;
        target.format = WGPUTextureFormat_RGBA8Unorm;
        WGPUFragmentState fragment = WGPU_FRAGMENT_STATE_INIT;
        fragment.module = module;
        fragment.entryPoint = voxy::gpu::toStringView("fs");
        fragment.targetCount = 1;
        fragment.targets = &target;
        WGPURenderPipelineDescriptor render = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
        render.vertex.module = module;
        render.vertex.entryPoint = voxy::gpu::toStringView("vs");
        render.fragment = &fragment;
        const auto validRender = voxy::gpu::createRenderPipeline(device, &render);
        assert(validRender);
        wgpuRenderPipelineRelease(validRender);
        render.vertex.entryPoint = voxy::gpu::toStringView("missing");
        assert(!voxy::gpu::createRenderPipeline(device, &render));

        std::array<WGPURenderPipeline, 24> renders{};
        WGPURenderPipeline invalidRender = nullptr;
        {
            voxy::gpu::RenderPipelineBatch batch;
            render.vertex.entryPoint = voxy::gpu::toStringView("vs");
            for (size_t index = 0; index < renders.size(); ++index) {
                // Reuse nested descriptor storage, as the production variants
                // do for target formats, entry points and specialization data.
                target.format = index % 2 == 0 ? WGPUTextureFormat_RGBA8Unorm
                                              : WGPUTextureFormat_RGBA16Float;
                batch.add(device, render, renders[index]);
            }
            render.vertex.entryPoint = voxy::gpu::toStringView("missing");
            batch.add(device, render, invalidRender);
            batch.wait();
            batch.wait();
        }
        assert(!invalidRender);
        for (const auto pipeline : renders) {
            assert(pipeline);
            wgpuRenderPipelineRelease(pipeline);
        }
        WGPURenderPipeline drainedRender = nullptr;
        {
            voxy::gpu::RenderPipelineBatch batch;
            render.vertex.entryPoint = voxy::gpu::toStringView("vs");
            batch.add(device, render, drainedRender);
        }
        assert(drainedRender);
        wgpuRenderPipelineRelease(drainedRender);
        EM_ASM({
            if (Module['voxyStartupCompletedPipelines'] !== 51) throw Error('Unexpected completion count');
        });

        // Losing the device must also drain outstanding callbacks safely.
        std::array<WGPUComputePipeline, 2> interrupted{};
        WGPURenderPipeline interruptedRender = nullptr;
        {
            voxy::gpu::ComputePipelineBatch batch;
            voxy::gpu::RenderPipelineBatch renderBatch;
            for (auto& pipeline : interrupted) {
                WGPUComputePipelineDescriptor desc = WGPU_COMPUTE_PIPELINE_DESCRIPTOR_INIT;
                desc.compute.module = module;
                desc.compute.entryPoint = voxy::gpu::toStringView("main");
                batch.add(device, desc, pipeline);
            }
            renderBatch.add(device, render, interruptedRender);
            EM_ASM({ Module['preinitializedWebGPUDevice'].destroy(); });
            batch.wait();
            renderBatch.wait();
        }
        // A driver may finish these before loss is delivered. Both outcomes
        // are valid; callbacks must settle and any returned handles be released.
        for (const auto pipeline : interrupted) {
            if (pipeline) wgpuComputePipelineRelease(pipeline);
        }
        if (interruptedRender) wgpuRenderPipelineRelease(interruptedRender);
    }
    assert(!voxy::gpu::asynchronousStartupPipelines);
    wgpuShaderModuleRelease(module);
    EM_ASM({
        globalThis.pipelineProbePassed = true;
    });
    puts("Pipeline batch success, failure, scope drain, render and device-loss callbacks passed");
}
