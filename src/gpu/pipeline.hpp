#pragma once

#include "gpu/webgpu_compat.hpp"

#if defined(VOXY_WASM)
#include <emscripten.h>
#include <emscripten/promise.h>
#include <cstdio>
#include <memory>
#include <vector>
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
        if (!previous_) {
            EM_ASM({
                Module['voxyStartupCompletedPipelines'] = 0;
                Module['setStatus']?.('Preparing graphics...');
            });
        }
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
    em_promise_t completion = emscripten_promise_create();

    Pipeline await() {
        // Resume from the completion callback, not a timer that Chrome can
        // throttle when the loading tab is in the background. Await also keeps
        // the callback state and caller's pipeline descriptor alive.
        static_cast<void>(emscripten_promise_await(completion));
        emscripten_promise_destroy(completion);
        return pipeline;
    }
};

template<class Pipeline>
void pipelineCompiled(WGPUCreatePipelineAsyncStatus status, Pipeline pipeline,
                      WGPUStringView message, void* userdata, void*) {
    auto& result = *static_cast<PipelineResult<Pipeline>*>(userdata);
    if (status == WGPUCreatePipelineAsyncStatus_Success) {
        result.pipeline = pipeline;
        EM_ASM({
            const count = ++Module['voxyStartupCompletedPipelines'];
            Module['setStatus']?.('Preparing graphics... ' + count
                + (count === 1 ? ' step complete' : ' steps complete'));
        });
    } else {
        const auto length = message.data
            ? (message.length == WGPU_STRLEN ? std::strlen(message.data) : message.length)
            : 0;
        std::fprintf(stderr, "WebGPU pipeline compilation failed: %.*s\n",
                     static_cast<int>(length), message.data ? message.data : "");
    }
    // Failure also wakes the caller so normal initialization error handling runs.
    emscripten_promise_resolve(result.completion, EM_PROMISE_FULFILL, nullptr);
}
} // namespace detail
#endif

// Queue independent pipelines before waiting for any of them. Destinations
// must remain alive until wait() (or this batch's destructor) has completed.
// Native builds and runtime reconfiguration retain synchronous creation.
class ComputePipelineBatch {
public:
    ComputePipelineBatch() = default;
    ~ComputePipelineBatch() { wait(); }
    ComputePipelineBatch(const ComputePipelineBatch&) = delete;
    ComputePipelineBatch& operator=(const ComputePipelineBatch&) = delete;

    void add(WGPUDevice device, const WGPUComputePipelineDescriptor& descriptor,
             WGPUComputePipeline& destination) {
#if defined(VOXY_WASM)
        if (asynchronousStartupPipelines) {
            auto pending = std::make_unique<Pending>();
            pending->destination = &destination;
            // Store callback state before submitting; vector growth must never
            // relocate the state handed to WebGPU.
            auto* result = &pending->result;
            pending_.push_back(std::move(pending));
            WGPUCreateComputePipelineAsyncCallbackInfo callback =
                WGPU_CREATE_COMPUTE_PIPELINE_ASYNC_CALLBACK_INFO_INIT;
            callback.mode = WGPUCallbackMode_AllowSpontaneous;
            callback.callback = detail::pipelineCompiled<WGPUComputePipeline>;
            callback.userdata1 = result;
            static_cast<void>(wgpuDeviceCreateComputePipelineAsync(
                device, &descriptor, callback));
            return;
        }
#endif
        destination = wgpuDeviceCreateComputePipeline(device, &descriptor);
    }

    void wait() {
#if defined(VOXY_WASM)
        // Drain every callback, including when one compilation failed. No
        // pending callback may outlive its state or the destination handles.
        for (auto& pending : pending_) {
            *pending->destination = pending->result.await();
        }
        pending_.clear();
#endif
    }

private:
#if defined(VOXY_WASM)
    struct Pending {
        WGPUComputePipeline* destination = nullptr;
        detail::PipelineResult<WGPUComputePipeline> result;
    };
    std::vector<std::unique_ptr<Pending>> pending_;
#endif
};

// Render descriptors are consumed by WebGPU when add() submits them, just like
// compute descriptors. Each destination must survive until the batch drains.
class RenderPipelineBatch {
public:
    RenderPipelineBatch() = default;
    ~RenderPipelineBatch() { wait(); }
    RenderPipelineBatch(const RenderPipelineBatch&) = delete;
    RenderPipelineBatch& operator=(const RenderPipelineBatch&) = delete;

    void add(WGPUDevice device, const WGPURenderPipelineDescriptor& descriptor,
             WGPURenderPipeline& destination) {
#if defined(VOXY_WASM)
        if (asynchronousStartupPipelines) {
            auto pending = std::make_unique<Pending>();
            pending->destination = &destination;
            auto* result = &pending->result;
            pending_.push_back(std::move(pending));
            WGPUCreateRenderPipelineAsyncCallbackInfo callback =
                WGPU_CREATE_RENDER_PIPELINE_ASYNC_CALLBACK_INFO_INIT;
            callback.mode = WGPUCallbackMode_AllowSpontaneous;
            callback.callback = detail::pipelineCompiled<WGPURenderPipeline>;
            callback.userdata1 = result;
            static_cast<void>(wgpuDeviceCreateRenderPipelineAsync(
                device, &descriptor, callback));
            return;
        }
#endif
        destination = wgpuDeviceCreateRenderPipeline(device, &descriptor);
    }

    void wait() {
#if defined(VOXY_WASM)
        // Even a failed pipeline must settle before callback state is freed.
        for (auto& pending : pending_) {
            *pending->destination = pending->result.await();
        }
        pending_.clear();
#endif
    }

private:
#if defined(VOXY_WASM)
    struct Pending {
        WGPURenderPipeline* destination = nullptr;
        detail::PipelineResult<WGPURenderPipeline> result;
    };
    std::vector<std::unique_ptr<Pending>> pending_;
#endif
};

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
        return result.await();
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
        return result.await();
    }
#endif
    return wgpuDeviceCreateRenderPipeline(device, descriptor);
}

} // namespace voxy::gpu
