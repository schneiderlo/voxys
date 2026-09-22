#include "physics/gpu/gpu_event_readback.hpp"

#include "gpu/resources.hpp"
#include "physics/gpu/debug_readback_ring.hpp"
#include "physics/gpu/gpu_narrow_phase.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <utility>
#include <vector>

namespace voxy::physics {
namespace {

constexpr size_t kPacketHeaderBytes = 16;
// A runtime storage array must bind at least one element even when its
// source flag is disabled. ContactManifold is the largest optional element.
constexpr size_t kFallbackBytes = sizeof(GpuContactManifold);

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
        // Attachment capacity, reserved.
        std::array<uint32_t, 4> attachments{};
    };

    ~Impl() { shutdown(); }

    bool initialize(WGPUDevice device, WGPUQueue queue,
                    const Config& config) {
        shutdown();
        if (!device || !queue || config.eventCapacity == 0
            || config.readbackSlots == 0
            || config.readbackSlots > DebugReadbackRing::kMaximumSlots) return false;
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
            .size = gpu::alignUniformBufferSize(sizeof(Params))
                * config_.readbackSlots,
            .usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst,
        });
        fallbackBuffer_ = gpu::createBuffer(device_, gpu::BufferDesc{
            .label = "physics_event_empty_source",
            .size = kFallbackBytes,
            .usage = WGPUBufferUsage_Storage | WGPUBufferUsage_CopyDst,
        });
        sourceTelemetry_ = gpu::createBuffer(device_, gpu::BufferDesc{
            .label = "physics_event_source_telemetry",
            .size = 32,
            .usage = WGPUBufferUsage_Storage | WGPUBufferUsage_CopyDst,
        });
        if (!packedEvents_ || !parameterBuffer_ || !fallbackBuffer_
            || !sourceTelemetry_
            || !readback_.initialize(
                device_, config_.readbackSlots, packetBytes_)) {
            shutdown();
            return false;
        }
        const std::array<uint32_t, kFallbackBytes / sizeof(uint32_t)> zeros{};
        if (!gpu::writeBuffer(queue_, fallbackBuffer_, 0, zeros)) {
            shutdown();
            return false;
        }

        shader_ = gpu::loadShaderModule(
            device_, config_.shaderPath, "physics_event_readback.wgsl",
            config_.shaderSources);
        if (!shader_) {
            shutdown();
            return false;
        }
        using LE = gpu::BindGroupLayoutEntry;
        std::vector<LE> entries;
        entries.emplace_back(0).computeVisible().storageBuffer(true);
        entries.emplace_back(1).computeVisible().storageBuffer(true);
        entries.emplace_back(2).computeVisible().storageBuffer(true);
        entries.emplace_back(4).computeVisible().storageBuffer(false);
        entries.emplace_back(5).computeVisible().storageBuffer(true);
        entries.emplace_back(7).computeVisible().uniformBuffer(
            false, sizeof(Params));
        entries.emplace_back(8).computeVisible().storageBuffer(true);
        entries.emplace_back(9).computeVisible().storageBuffer(true);
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
            sizeof(Params)) * config_.readbackSlots
            + kFallbackBytes + 32u + readback_.allocatedBytes();
        return true;
    }

    void shutdown() {
        readback_.shutdown();
        releaseHandle(pipeline_, wgpuComputePipelineRelease);
        releaseHandle(pipelineLayout_, wgpuPipelineLayoutRelease);
        releaseHandle(bindGroupLayout_, wgpuBindGroupLayoutRelease);
        releaseHandle(shader_, wgpuShaderModuleRelease);
        releaseBuffer(sourceTelemetry_);
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

    bool encodeReadback(WGPUCommandEncoder encoder, uint64_t tick, uint64_t submissionSerial) {
        if (!encoder || !sources_.valid()) return false;
        const auto slot = readback_.nextAvailableSlot();
        if (!slot) return false;
        // Queue writes happen before the submitted command buffer. Each
        // outstanding copy needs its own parameters, including within a batch.
        const uint64_t parameterOffset =
            *slot * gpu::alignUniformBufferSize(sizeof(Params));
        uint32_t sourceFlags = 0;
        if (sources_.hasContacts()) sourceFlags |= 1u;
        if (sources_.hasIslands()) sourceFlags |= 2u;
        if (sources_.hasHits()) sourceFlags |= 4u;
        if (sources_.hasAttachments()) sourceFlags |= 8u;
        const Params params{
            .counts = {sources_.hasContacts() ? sources_.contactCapacity : 0u,
                       sources_.hasIslands()
                           ? sources_.islandEventCapacity : 0u,
                       config_.eventCapacity, sourceFlags},
            .tick = {static_cast<uint32_t>(tick),
                     static_cast<uint32_t>(tick >> 32u),
                     sources_.hasHits() ? sources_.manifoldCapacity : 0u,
                     sources_.metadata ? sources_.bodyCapacity : 0u},
            .attachments = {
                sources_.hasAttachments()
                    ? sources_.attachmentCapacity : 0u,
                0u, 0u, 0u},
        };
        if (!gpu::writeBuffer(queue_, parameterBuffer_, parameterOffset, params))
            return false;
        // Compact only the words consumed by the packet shader. This replaces
        // three telemetry bindings with one and leaves room for attachment
        // records inside WebGPU's guaranteed storage-buffer profile.
        if (sources_.hasContacts()) {
            wgpuCommandEncoderCopyBufferToBuffer(
                encoder, sources_.contactTelemetry, 7u * sizeof(uint32_t),
                sourceTelemetry_, 0u, 2u * sizeof(uint32_t));
            wgpuCommandEncoderCopyBufferToBuffer(
                encoder, sources_.contactTelemetry, 12u * sizeof(uint32_t),
                sourceTelemetry_, 2u * sizeof(uint32_t), sizeof(uint32_t));
        }
        if (sources_.hasIslands()) {
            wgpuCommandEncoderCopyBufferToBuffer(
                encoder, sources_.islandTelemetry, 8u * sizeof(uint32_t),
                sourceTelemetry_, 3u * sizeof(uint32_t), sizeof(uint32_t));
            wgpuCommandEncoderCopyBufferToBuffer(
                encoder, sources_.islandTelemetry, 17u * sizeof(uint32_t),
                sourceTelemetry_, 4u * sizeof(uint32_t), sizeof(uint32_t));
        }
        if (sources_.hasHits()) {
            wgpuCommandEncoderCopyBufferToBuffer(
                encoder, sources_.narrowPhaseTelemetry,
                (sources_.useActiveManifoldCount ? 24u : 11u) * sizeof(uint32_t), sourceTelemetry_,
                5u * sizeof(uint32_t), sizeof(uint32_t));
            wgpuCommandEncoderCopyBufferToBuffer(
                encoder, sources_.narrowPhaseTelemetry,
                17u * sizeof(uint32_t), sourceTelemetry_,
                6u * sizeof(uint32_t), sizeof(uint32_t));
        }
        const std::array<gpu::BindGroupEntry, 8> entries = {
            gpu::BindGroupEntry(0).buffer(sources_.hasContacts()
                ? sources_.contactEvents : fallbackBuffer_),
            gpu::BindGroupEntry(1).buffer(sourceTelemetry_),
            gpu::BindGroupEntry(2).buffer(sources_.hasIslands()
                ? sources_.islandEvents : fallbackBuffer_),
            gpu::BindGroupEntry(4).buffer(packedEvents_),
            gpu::BindGroupEntry(5).buffer(sources_.hasHits()
                ? sources_.manifolds : fallbackBuffer_),
            gpu::BindGroupEntry(7).buffer(
                parameterBuffer_, parameterOffset, sizeof(Params)),
            gpu::BindGroupEntry(8).buffer(sources_.metadata
                ? sources_.metadata : fallbackBuffer_),
            gpu::BindGroupEntry(9).buffer(sources_.hasAttachments()
                ? sources_.attachments : fallbackBuffer_),
        };
        WGPUBindGroup group = gpu::createBindGroup(
            device_, bindGroupLayout_, entries,
            "physics_event_readback_bind_group");
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
        wgpuComputePassEncoderDispatchWorkgroups(pass, 1, 1, 1);
        wgpuComputePassEncoderEnd(pass);
        wgpuComputePassEncoderRelease(pass);
        wgpuBindGroupRelease(group);
        return readback_.encodeCopy(
            encoder, packedEvents_, 0, packetBytes_, tick, 0,
            config_.eventCapacity, slot, submissionSerial);
    }

    std::optional<GpuEventBatch> poll() {
        auto raw = readback_.poll();
        if (!raw || raw->bytes.size() != packetBytes_) return std::nullopt;
        std::array<uint32_t, 4> header{};
        std::memcpy(header.data(), raw->bytes.data(), kPacketHeaderBytes);
        const uint32_t count = std::min(header[0], config_.eventCapacity);
        GpuEventBatch result;
        result.tick = raw->tick;
        result.submissionSerial=raw->submissionSerial;
        result.overflow = header[1] != 0u;
        result.valid=header[0]<=config_.eventCapacity
            && (uint64_t{header[2]} | (uint64_t{header[3]}<<32u))==raw->tick;
        result.events.resize(count);
        if (count != 0) {
            std::memcpy(result.events.data(),
                        raw->bytes.data() + kPacketHeaderBytes,
                        size_t{count} * sizeof(GpuPhysicsEvent));
            for(const auto& event:result.events) {
                if(event.header[0]!=static_cast<uint32_t>(raw->tick)
                    || event.header[1]<1 || event.header[1]>6) result.valid=false;
            }
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
    WGPUBuffer sourceTelemetry_ = nullptr;
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
    auto replacement = std::make_unique<Impl>();
    if (!replacement->initialize(device, queue, config)) return false;
    impl_ = std::move(replacement);
    return true;
}
void GpuEventReadbackRing::shutdown() {
    if (impl_) impl_->shutdown();
}
void GpuEventReadbackRing::setSources(const GpuEventSources& sources) {
    if (impl_) impl_->setSources(sources);
}
bool GpuEventReadbackRing::encodeReadback(
    WGPUCommandEncoder encoder, uint64_t tick, uint64_t submissionSerial) {
    return impl_ && impl_->encodeReadback(encoder, tick, submissionSerial);
}
std::optional<GpuEventBatch> GpuEventReadbackRing::poll() {
    return impl_ ? impl_->poll() : std::nullopt;
}

uint32_t GpuEventReadbackRing::availableSlots() const noexcept {
    return impl_ ? impl_->readback_.availableSlots() : 0;
}
uint64_t GpuEventReadbackRing::failedReadbacks() const noexcept {
    return impl_ ? impl_->readback_.failedReadbacks() : 0;
}
WGPUBuffer GpuEventReadbackRing::packedEventBuffer() const noexcept {
    return impl_ ? impl_->packedEvents_ : nullptr;
}
size_t GpuEventReadbackRing::allocatedBytes() const noexcept {
    return impl_ ? impl_->allocatedBytes_ : 0u;
}

} // namespace voxy::physics
