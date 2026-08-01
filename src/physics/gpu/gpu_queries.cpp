#include "physics/gpu/gpu_queries.hpp"

#include "gpu/resources.hpp"
#include "physics/gpu/debug_readback_ring.hpp"

#include <array>
#include <cstring>
#include <utility>
#include <vector>

namespace voxy::physics {
namespace {

template <typename T>
void releaseHandle(T& handle, void (*release)(T)) {
    if (handle) {
        release(handle);
        handle = nullptr;
    }
}

void releaseBuffer(WGPUBuffer& buffer) {
    if (!buffer) return;
    wgpuBufferDestroy(buffer);
    wgpuBufferRelease(buffer);
    buffer = nullptr;
}

} // namespace

class GpuAsyncQuerySystem::Impl {
public:
    struct alignas(16) Params {
        // body count, query count, output hit capacity, reserved.
        std::array<uint32_t, 4> counts{};
    };

    ~Impl() { shutdown(); }

    bool initialize(WGPUDevice device, WGPUQueue queue,
                    const Config& config) {
        shutdown();
        if (!device || !queue || config.bodyCapacity == 0
            || config.requestCapacity == 0 || config.readbackSlots == 0) {
            return false;
        }
        device_ = device;
        queue_ = queue;
        config_ = config;

        requestBuffer_ = gpu::createBuffer(device_, gpu::BufferDesc{
            .label = "physics_query_requests",
            .size = uint64_t{config_.requestCapacity}
                  * sizeof(GpuQueryRequest),
            .usage = WGPUBufferUsage_Storage | WGPUBufferUsage_CopyDst,
        });
        outputBuffer_ = gpu::createBuffer(device_, gpu::BufferDesc{
            .label = "physics_query_outputs",
            .size = uint64_t{config_.requestCapacity}
                  * sizeof(GpuQueryOutput),
            .usage = WGPUBufferUsage_Storage | WGPUBufferUsage_CopySrc,
        });
        parameterBuffer_ = gpu::createBuffer(device_, gpu::BufferDesc{
            .label = "physics_query_params",
            .size = gpu::alignUniformBufferSize(sizeof(Params)),
            .usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst,
        });
        if (!requestBuffer_ || !outputBuffer_ || !parameterBuffer_
            || !readback_.initialize(
                device_, config_.readbackSlots,
                size_t{config_.requestCapacity} * sizeof(GpuQueryOutput))) {
            shutdown();
            return false;
        }

        shaderModule_ = gpu::loadShaderModule(
            device_, config_.shaderPath, "physics_queries.wgsl",
            config_.shaderSources);
        if (!shaderModule_) {
            shutdown();
            return false;
        }

        using LE = gpu::BindGroupLayoutEntry;
        std::vector<LE> entries;
        entries.emplace_back(0).computeVisible().storageBuffer(true);
        entries.emplace_back(1).computeVisible().storageBuffer(true);
        entries.emplace_back(2).computeVisible().storageBuffer(true);
        entries.emplace_back(3).computeVisible().storageBuffer(true);
        entries.emplace_back(4).computeVisible().storageBuffer(false);
        entries.emplace_back(5).computeVisible().uniformBuffer(
            false, sizeof(Params));
        bindGroupLayout_ = gpu::createBindGroupLayout(
            device_, entries, "physics_query_layout");
        if (!bindGroupLayout_) {
            shutdown();
            return false;
        }
        pipelineLayout_ = gpu::createPipelineLayout(
            device_, std::array{bindGroupLayout_},
            "physics_query_pipeline_layout");
        if (!pipelineLayout_) {
            shutdown();
            return false;
        }
        WGPUComputePipelineDescriptor pipelineDesc{};
        WGPU_SET_LABEL(pipelineDesc, "physics_async_queries");
        pipelineDesc.layout = pipelineLayout_;
        pipelineDesc.compute.module = shaderModule_;
        WGPU_SET_ENTRY_POINT(pipelineDesc.compute, "execute_queries");
        pipeline_ = wgpuDeviceCreateComputePipeline(device_, &pipelineDesc);
        if (!pipeline_) {
            shutdown();
            return false;
        }
        allocatedBytes_ = size_t{config_.requestCapacity}
                * (sizeof(GpuQueryRequest) + sizeof(GpuQueryOutput))
            + gpu::alignUniformBufferSize(sizeof(Params))
            + readback_.allocatedBytes();
        return true;
    }

    void shutdown() {
        readback_.shutdown();
        releaseHandle(pipeline_, wgpuComputePipelineRelease);
        releaseHandle(pipelineLayout_, wgpuPipelineLayoutRelease);
        releaseHandle(bindGroupLayout_, wgpuBindGroupLayoutRelease);
        releaseHandle(shaderModule_, wgpuShaderModuleRelease);
        releaseBuffer(parameterBuffer_);
        releaseBuffer(outputBuffer_);
        releaseBuffer(requestBuffer_);
        device_ = nullptr;
        queue_ = nullptr;
        config_ = {};
        bodyView_ = {};
        pendingCount_ = 0;
        pendingTick_ = 0;
        allocatedBytes_ = 0;
    }

    void setBodyView(const GpuQueryBodyView& view) {
        bodyView_ = view.valid() && view.bodyCapacity <= config_.bodyCapacity
            ? view : GpuQueryBodyView{};
    }

    bool submit(std::span<const GpuQueryRequest> requests, uint64_t tick) {
        if (!queue_ || pendingCount_ != 0 || requests.empty()
            || requests.size() > config_.requestCapacity) {
            return false;
        }
        if (!gpu::writeBuffer(queue_, requestBuffer_, 0,
                std::span<const std::byte>(
                    reinterpret_cast<const std::byte*>(requests.data()),
                    requests.size_bytes()))) {
            return false;
        }
        pendingCount_ = static_cast<uint32_t>(requests.size());
        pendingTick_ = tick;
        return true;
    }

    bool encode(WGPUCommandEncoder encoder) {
        if (!encoder || !bodyView_.valid() || pendingCount_ == 0) return false;
        const Params params{
            .counts = {bodyView_.bodyCapacity, pendingCount_,
                       kGpuQueryMaximumHits, 0u},
        };
        if (!gpu::writeBuffer(queue_, parameterBuffer_, 0, params))
            return false;
        const std::array<gpu::BindGroupEntry, 6> entries = {
            gpu::BindGroupEntry(0).buffer(bodyView_.poseBuffer),
            gpu::BindGroupEntry(1).buffer(bodyView_.shapeBuffer),
            gpu::BindGroupEntry(2).buffer(bodyView_.metadataBuffer),
            gpu::BindGroupEntry(3).buffer(requestBuffer_),
            gpu::BindGroupEntry(4).buffer(outputBuffer_),
            gpu::BindGroupEntry(5).buffer(parameterBuffer_),
        };
        WGPUBindGroup group = gpu::createBindGroup(
            device_, bindGroupLayout_, entries, "physics_query_bind_group");
        if (!group) return false;

        WGPUComputePassDescriptor passDesc{};
        WGPUComputePassEncoder pass =
            wgpuCommandEncoderBeginComputePass(encoder, &passDesc);
        if (!pass) {
            wgpuBindGroupRelease(group);
            return false;
        }
        wgpuComputePassEncoderSetPipeline(pass, pipeline_);
        wgpuComputePassEncoderSetBindGroup(pass, 0, group, 0, nullptr);
        wgpuComputePassEncoderDispatchWorkgroups(pass, pendingCount_, 1, 1);
        wgpuComputePassEncoderEnd(pass);
        wgpuComputePassEncoderRelease(pass);
        wgpuBindGroupRelease(group);

        const uint32_t count = pendingCount_;
        const uint64_t tick = pendingTick_;
        if (!readback_.encodeCopy(
                encoder, outputBuffer_, 0,
                uint64_t{count} * sizeof(GpuQueryOutput), tick, 0, count)) {
            return false;
        }
        pendingCount_ = 0;
        pendingTick_ = 0;
        return true;
    }

    std::optional<GpuQueryBatchResult> poll() {
        auto raw = readback_.poll();
        if (!raw) return std::nullopt;
        const size_t expected =
            size_t{raw->bodyCount} * sizeof(GpuQueryOutput);
        if (raw->bytes.size() != expected) return std::nullopt;
        GpuQueryBatchResult result;
        result.tick = raw->tick;
        result.outputs.resize(raw->bodyCount);
        std::memcpy(result.outputs.data(), raw->bytes.data(), expected);
        return result;
    }

    WGPUDevice device_ = nullptr;
    WGPUQueue queue_ = nullptr;
    Config config_{};
    GpuQueryBodyView bodyView_{};
    uint32_t pendingCount_ = 0;
    uint64_t pendingTick_ = 0;
    size_t allocatedBytes_ = 0;
    WGPUBuffer requestBuffer_ = nullptr;
    WGPUBuffer outputBuffer_ = nullptr;
    WGPUBuffer parameterBuffer_ = nullptr;
    WGPUShaderModule shaderModule_ = nullptr;
    WGPUBindGroupLayout bindGroupLayout_ = nullptr;
    WGPUPipelineLayout pipelineLayout_ = nullptr;
    WGPUComputePipeline pipeline_ = nullptr;
    DebugReadbackRing readback_{};
};

GpuAsyncQuerySystem::GpuAsyncQuerySystem() : impl_(std::make_unique<Impl>()) {}
GpuAsyncQuerySystem::~GpuAsyncQuerySystem() = default;
GpuAsyncQuerySystem::GpuAsyncQuerySystem(GpuAsyncQuerySystem&&) noexcept = default;
GpuAsyncQuerySystem& GpuAsyncQuerySystem::operator=(
    GpuAsyncQuerySystem&&) noexcept = default;

bool GpuAsyncQuerySystem::initialize(WGPUDevice device, WGPUQueue queue,
                                     const Config& config) {
    auto replacement = std::make_unique<Impl>();
    if (!replacement->initialize(device, queue, config)) return false;
    impl_ = std::move(replacement);
    return true;
}
void GpuAsyncQuerySystem::shutdown() {
    if (impl_) impl_->shutdown();
}
void GpuAsyncQuerySystem::setBodyView(const GpuQueryBodyView& view) {
    if (impl_) impl_->setBodyView(view);
}
bool GpuAsyncQuerySystem::submit(
    std::span<const GpuQueryRequest> requests, uint64_t tick) {
    return impl_ && impl_->submit(requests, tick);
}
bool GpuAsyncQuerySystem::encode(WGPUCommandEncoder encoder) {
    return impl_ && impl_->encode(encoder);
}
std::optional<GpuQueryBatchResult> GpuAsyncQuerySystem::poll() {
    return impl_ ? impl_->poll() : std::nullopt;
}
WGPUBuffer GpuAsyncQuerySystem::outputBuffer() const noexcept {
    return impl_ ? impl_->outputBuffer_ : nullptr;
}
size_t GpuAsyncQuerySystem::allocatedBytes() const noexcept {
    return impl_ ? impl_->allocatedBytes_ : 0u;
}

} // namespace voxy::physics
