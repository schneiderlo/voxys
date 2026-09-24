#pragma once

#include "gpu/webgpu_compat.hpp"

#if defined(VOXY_WASM)
#include <emscripten.h>
#include <cstdio>
#endif

namespace voxy::gpu {

// Only startup may suspend the WASM stack. Runtime settings and rendering keep
// their synchronous API contract. Async compilation lets the browser service
// its GPU thread while the driver compiles expensive physics/render shaders.
inline bool asynchronousStartupPipelines = false;

class StartupPipelineCompilation {
public:
    StartupPipelineCompilation() : previous_(asynchronousStartupPipelines) {
        asynchronousStartupPipelines = true;
#if defined(VOXY_WASM)
        EM_ASM({ Module['setStatus']?.('Preparing graphics...'); });
#endif
    }
    ~StartupPipelineCompilation() { asynchronousStartupPipelines = previous_; }
    StartupPipelineCompilation(const StartupPipelineCompilation&) = delete;
    StartupPipelineCompilation& operator=(const StartupPipelineCompilation&) = delete;
private:
    bool previous_;
};

#if defined(VOXY_WASM)
namespace detail {
template<class Pipeline>
struct PipelineResult {
    Pipeline pipeline = nullptr;
    bool done = false;
};

template<class Pipeline>
void pipelineCompiled(WGPUCreatePipelineAsyncStatus status, Pipeline pipeline,
                      WGPUStringView message, void* userdata, void*) {
    auto& result = *static_cast<PipelineResult<Pipeline>*>(userdata);
    if (status == WGPUCreatePipelineAsyncStatus_Success) {
        result.pipeline = pipeline;
    } else {
        const auto length = message.data
            ? (message.length == WGPU_STRLEN ? std::strlen(message.data) : message.length)
            : 0;
        std::fprintf(stderr, "WebGPU pipeline compilation failed: %.*s\n",
                     static_cast<int>(length), message.data ? message.data : "");
    }
    result.done = true;
}
} // namespace detail
#endif

inline WGPUComputePipeline createComputePipeline(
    WGPUDevice device, const WGPUComputePipelineDescriptor* descriptor) {
#if defined(VOXY_WASM)
    if (asynchronousStartupPipelines) {
        detail::PipelineResult<WGPUComputePipeline> result;
        WGPUCreateComputePipelineAsyncCallbackInfo callback =
            WGPU_CREATE_COMPUTE_PIPELINE_ASYNC_CALLBACK_INFO_INIT;
        callback.mode = WGPUCallbackMode_AllowSpontaneous;
        callback.callback = detail::pipelineCompiled<WGPUComputePipeline>;
        callback.userdata1 = &result;
        static_cast<void>(wgpuDeviceCreateComputePipelineAsync(device, descriptor, callback));
        // Yield to JS so the completion callback can run. Keep the descriptor
        // and callback state alive until completion, including device loss.
        while (!result.done) emscripten_sleep(10);
        return result.pipeline;
    }
#endif
    return wgpuDeviceCreateComputePipeline(device, descriptor);
}

inline WGPURenderPipeline createRenderPipeline(
    WGPUDevice device, const WGPURenderPipelineDescriptor* descriptor) {
#if defined(VOXY_WASM)
    if (asynchronousStartupPipelines) {
        detail::PipelineResult<WGPURenderPipeline> result;
        WGPUCreateRenderPipelineAsyncCallbackInfo callback =
            WGPU_CREATE_RENDER_PIPELINE_ASYNC_CALLBACK_INFO_INIT;
        callback.mode = WGPUCallbackMode_AllowSpontaneous;
        callback.callback = detail::pipelineCompiled<WGPURenderPipeline>;
        callback.userdata1 = &result;
        static_cast<void>(wgpuDeviceCreateRenderPipelineAsync(device, descriptor, callback));
        while (!result.done) emscripten_sleep(10);
        return result.pipeline;
    }
#endif
    return wgpuDeviceCreateRenderPipeline(device, descriptor);
}

} // namespace voxy::gpu
