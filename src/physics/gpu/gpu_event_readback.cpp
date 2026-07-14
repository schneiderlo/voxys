#include "physics/gpu/gpu_event_readback.hpp"

#include "gpu/resources.hpp"
#include "physics/gpu/debug_readback_ring.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <utility>
#include <vector>

namespace voxy::physics {
namespace {

constexpr size_t kPacketHeaderBytes = 16;

template <typename T>
void releaseHandle(T& handle, void (*release)(T)) {
    if (!handle) return;
    release(handle);
    handle = nullptr;
}

void releaseBuffer(WGPUBuffer& buffer) {
    if (!buffer) return;
    wgpuBufferDestroy(buffer);
    wgpuBufferRelease(buffer);
    buffer = nullptr;
}

} // namespace

class GpuEventReadbackRing::Impl {
public:
    struct alignas(16) Params {
        // contact capacity, island capacity, output capacity, source flags.
        std::array<uint32_t, 4> counts{};
        // 64-bit tick split into low/high words.
        std::array<uint32_t, 4> tick{};
    };

    ~Impl() { shutdown(); }

    bool initialize(WGPUDevice device, WGPUQueue queue,
                    const Config& config) {
        shutdown();
        if (!device || !queue || config.eventCapacity == 0
            || config.readbackSlots == 0) return false;
        device_ = device;
        queue_ = queue;
        config_ = config;
        packetBytes_ = kPacketHeaderBytes
            + size_t{config_.eventCapacity} * sizeof(GpuPhysicsEvent);
        packedEvents_ = gpu::createBuffer(device_, gpu::BufferDesc{
            .label = "physics_event_packet",
            .size = packetBytes_,
            .usage = WGPUBufferUsage_Storage | WGPUBufferUsage_CopySrc,
        });
        parameterBuffer_ = gpu::createBuffer(device_, gpu::BufferDesc{
            .label = "physics_event_params",
            .size = gpu::alignUniformBufferSize(sizeof(Params)),
            .usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst,
        });
        fallbackBuffer_ = gpu::createBuffer(device_, gpu::BufferDesc{
            .label = "physics_event_empty_source",
            .size = 128,
            .usage = WGPUBufferUsage_Storage | WGPUBufferUsage_CopyDst,
        });
        if (!packedEvents_ || !parameterBuffer_ || !fallbackBuffer_
            || !readback_.initialize(
                device_, config_.readbackSlots, packetBytes_)) {
            shutdown();
            return false;
        }
        const std::array<uint32_t, 32> zeros{};
        gpu::writeBuffer(queue_, fallbackBuffer_, 0, zeros);

        shader_ = gpu::loadShaderModule(
            device_, config_.shaderPath, "physics_event_readback.wgsl");
        if (!shader_) {
            shutdown();
            return false;
        }
        using LE = gpu::BindGroupLayoutEntry;
        std::vector<LE> entries;
        for (uint32_t binding = 0; binding < 7; ++binding)
            entries.emplace_back(binding).computeVisible().storageBuffer(true);
        entries[4] = LE(4).computeVisible().storageBuffer(false);
        entries.emplace_back(7).computeVisible().uniformBuffer(
            false, sizeof(Params));
        bindGroupLayout_ = gpu::createBindGroupLayout(
            device_, entries, "physics_event_readback_layout");
        pipelineLayout_ = gpu::createPipelineLayout(
            device_, std::array{bindGroupLayout_},
            "physics_event_readback_pipeline_layout");
        if (!bindGroupLayout_ || !pipelineLayout_) {
            shutdown();
            return false;
        }
        WGPUComputePipelineDescriptor desc{};
        WGPU_SET_LABEL(desc, "physics_pack_deterministic_events");
        desc.layout = pipelineLayout_;
        desc.compute.module = shader_;
        WGPU_SET_ENTRY_POINT(desc.compute, "pack_events");
        pipeline_ = wgpuDeviceCreateComputePipeline(device_, &desc);
        if (!pipeline_) {
            shutdown();
            return false;
        }
        allocatedBytes_ = packetBytes_ + gpu::alignUniformBufferSize(
            sizeof(Params)) + 128 + readback_.allocatedBytes();
        return true;
    }

    void shutdown() {
        readback_.shutdown();
        releaseHandle(pipeline_, wgpuComputePipelineRelease);
        releaseHandle(pipelineLayout_, wgpuPipelineLayoutRelease);
        releaseHandle(bindGroupLayout_, wgpuBindGroupLayoutRelease);
        releaseHandle(shader_, wgpuShaderModuleRelease);
        releaseBuffer(fallbackBuffer_);
        releaseBuffer(parameterBuffer_);
        releaseBuffer(packedEvents_);
        device_ = nullptr;
        queue_ = nullptr;
        config_ = {};
        sources_ = {};
        packetBytes_ = 0;
        allocatedBytes_ = 0;
    }

    void setSources(const GpuEventSources& sources) {
        sources_ = sources.valid() ? sources : GpuEventSources{};
    }

    bool encodeReadback(WGPUCommandEncoder encoder, uint64_t tick) {
        if (!encoder || !sources_.valid()) return false;
        uint32_t sourceFlags = 0;
        if (sources_.hasContacts()) sourceFlags |= 1u;
        if (sources_.hasIslands()) sourceFlags |= 2u;
        if (sources_.hasHits()) sourceFlags |= 4u;
        const Params params{
            .counts = {sources_.hasContacts() ? sources_.contactCapacity : 0u,
                       sources_.hasIslands()
                           ? sources_.islandEventCapacity : 0u,
                       config_.eventCapacity, sourceFlags},
            .tick = {static_cast<uint32_t>(tick),
                     static_cast<uint32_t>(tick >> 32u),
                     sources_.hasHits() ? sources_.manifoldCapacity : 0u, 0u},
        };
        gpu::writeBuffer(queue_, parameterBuffer_, 0, params);
        const std::array<gpu::BindGroupEntry, 8> entries = {
            gpu::BindGroupEntry(0).buffer(sources_.hasContacts()
                ? sources_.contactEvents : fallbackBuffer_),
            gpu::BindGroupEntry(1).buffer(sources_.hasContacts()
                ? sources_.contactTelemetry : fallbackBuffer_),
            gpu::BindGroupEntry(2).buffer(sources_.hasIslands()
                ? sources_.islandEvents : fallbackBuffer_),
            gpu::BindGroupEntry(3).buffer(sources_.hasIslands()
                ? sources_.islandTelemetry : fallbackBuffer_),
            gpu::BindGroupEntry(4).buffer(packedEvents_),
            gpu::BindGroupEntry(5).buffer(sources_.hasHits()
                ? sources_.manifolds : fallbackBuffer_),
            gpu::BindGroupEntry(6).buffer(sources_.hasHits()
                ? sources_.narrowPhaseTelemetry : fallbackBuffer_),
            gpu::BindGroupEntry(7).buffer(parameterBuffer_),
        };
        WGPUBindGroup group = gpu::createBindGroup(
            device_, bindGroupLayout_, entries,
            "physics_event_readback_bind_group");
        if (!group) return false;
        WGPUComputePassDescriptor passDesc{};
        WGPUComputePassEncoder pass =
            wgpuCommandEncoderBeginComputePass(encoder, &passDesc);
        wgpuComputePassEncoderSetPipeline(pass, pipeline_);
        wgpuComputePassEncoderSetBindGroup(pass, 0, group, 0, nullptr);
        wgpuComputePassEncoderDispatchWorkgroups(pass, 1, 1, 1);
        wgpuComputePassEncoderEnd(pass);
        wgpuComputePassEncoderRelease(pass);
        wgpuBindGroupRelease(group);
        return readback_.encodeCopy(
            encoder, packedEvents_, 0, packetBytes_, tick, 0,
            config_.eventCapacity);
    }

    std::optional<GpuEventBatch> poll() {
        auto raw = readback_.poll();
        if (!raw || raw->bytes.size() != packetBytes_) return std::nullopt;
        std::array<uint32_t, 4> header{};
        std::memcpy(header.data(), raw->bytes.data(), kPacketHeaderBytes);
        const uint32_t count = std::min(header[0], config_.eventCapacity);
        GpuEventBatch result;
        result.tick = raw->tick;
        result.overflow = header[1] != 0u;
        result.events.resize(count);
        if (count != 0) {
            std::memcpy(result.events.data(),
                        raw->bytes.data() + kPacketHeaderBytes,
                        size_t{count} * sizeof(GpuPhysicsEvent));
        }
        return result;
    }

    WGPUDevice device_ = nullptr;
    WGPUQueue queue_ = nullptr;
    Config config_{};
    GpuEventSources sources_{};
    size_t packetBytes_ = 0;
    size_t allocatedBytes_ = 0;
    WGPUBuffer packedEvents_ = nullptr;
    WGPUBuffer parameterBuffer_ = nullptr;
    WGPUBuffer fallbackBuffer_ = nullptr;
    WGPUShaderModule shader_ = nullptr;
    WGPUBindGroupLayout bindGroupLayout_ = nullptr;
    WGPUPipelineLayout pipelineLayout_ = nullptr;
    WGPUComputePipeline pipeline_ = nullptr;
    DebugReadbackRing readback_{};
};

GpuEventReadbackRing::GpuEventReadbackRing()
    : impl_(std::make_unique<Impl>()) {}
GpuEventReadbackRing::~GpuEventReadbackRing() = default;
GpuEventReadbackRing::GpuEventReadbackRing(
    GpuEventReadbackRing&&) noexcept = default;
GpuEventReadbackRing& GpuEventReadbackRing::operator=(
    GpuEventReadbackRing&&) noexcept = default;

bool GpuEventReadbackRing::initialize(
    WGPUDevice device, WGPUQueue queue, const Config& config) {
    return impl_->initialize(device, queue, config);
}
void GpuEventReadbackRing::shutdown() { impl_->shutdown(); }
void GpuEventReadbackRing::setSources(const GpuEventSources& sources) {
    impl_->setSources(sources);
}
bool GpuEventReadbackRing::encodeReadback(
    WGPUCommandEncoder encoder, uint64_t tick) {
    return impl_->encodeReadback(encoder, tick);
}
std::optional<GpuEventBatch> GpuEventReadbackRing::poll() {
    return impl_->poll();
}
WGPUBuffer GpuEventReadbackRing::packedEventBuffer() const noexcept {
    return impl_->packedEvents_;
}
size_t GpuEventReadbackRing::allocatedBytes() const noexcept {
    return impl_->allocatedBytes_;
}

} // namespace voxy::physics
