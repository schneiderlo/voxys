#include "gpu/pipeline.hpp"
#include "physics/gpu/gpu_lockstep.hpp"

#include "gpu/resources.hpp"

#include <algorithm>
#include <array>
#include <limits>
#include <utility>
#include <vector>

namespace voxy::physics::deterministic {
namespace {

constexpr uint32_t kTelemetryWords = 32;

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

class GpuLockstepWorld::Impl {
public:
    struct alignas(16) Params {
        // body capacity, contact capacity, substeps, solver iterations.
        std::array<uint32_t, 4> counts{};
        // gravity/substep Q16, velocity-to-position divisor, sector constants.
        std::array<int32_t, 4> integration{};
        std::array<uint32_t, 4> tick{};
    };

    ~Impl() { shutdown(); }

    bool initialize(WGPUDevice device, WGPUQueue queue,
                    const Config& config) {
        shutdown();
        if (!device || !queue || config.bodyCapacity == 0
            || config.contactCapacity == 0 || config.tickRateHz == 0
            || config.substeps == 0
            || config.substeps > 16 || config.solverIterations == 0
            || config.solverIterations > 32) return false;
        const uint32_t divisorScale =
            config.substeps * kLockstepVelocityToPositionScale;
        if (config.tickRateHz
            > static_cast<uint32_t>(std::numeric_limits<int32_t>::max())
                / divisorScale) return false;
        device_ = device;
        queue_ = queue;
        config_ = config;
        const auto makeStorage = [this](uint64_t bytes, const char* label) {
            return gpu::createBuffer(device_, gpu::BufferDesc{
                .label = label,
                .size = bytes,
                .usage = WGPUBufferUsage_Storage | WGPUBufferUsage_CopyDst
                       | WGPUBufferUsage_CopySrc,
            });
        };
        bodies_ = makeStorage(
            uint64_t{config_.bodyCapacity} * sizeof(LockstepBody),
            "lockstep_bodies");
        contacts_ = makeStorage(
            uint64_t{config_.contactCapacity} * sizeof(LockstepContact),
            "lockstep_contacts");
        roots_ = makeStorage(
            uint64_t{config_.bodyCapacity} * sizeof(uint32_t),
            "lockstep_roots");
        bodyHashes_ = makeStorage(
            uint64_t{config_.bodyCapacity} * sizeof(uint32_t),
            "lockstep_body_hashes");
        contactHashes_ = makeStorage(
            uint64_t{config_.contactCapacity} * sizeof(uint32_t),
            "lockstep_contact_hashes");
        islandHashes_ = makeStorage(
            uint64_t{config_.bodyCapacity} * sizeof(uint32_t),
            "lockstep_island_hashes");
        telemetry_ = makeStorage(kTelemetryWords * sizeof(uint32_t),
                                 "lockstep_telemetry");
        params_ = gpu::createBuffer(device_, gpu::BufferDesc{
            .label = "lockstep_params",
            .size = gpu::alignUniformBufferSize(sizeof(Params)),
            .usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst,
        });
        if (!bodies_ || !contacts_ || !roots_ || !bodyHashes_
            || !contactHashes_ || !islandHashes_ || !telemetry_ || !params_) {
            shutdown();
            return false;
        }
        std::vector<LockstepBody> empty(config_.bodyCapacity);
        const std::array<uint32_t, kTelemetryWords> zeros{};
        if (!gpu::writeBuffer(queue_, bodies_, 0,
                std::span<const LockstepBody>(empty))
            || !gpu::writeBuffer(queue_, telemetry_, 0, zeros)) {
            shutdown();
            return false;
        }

        shader_ = gpu::loadShaderModule(
            device_, config_.shaderPath, "physics_lockstep.wgsl");
        if (!shader_) {
            shutdown();
            return false;
        }
        using LE = gpu::BindGroupLayoutEntry;
        std::vector<LE> entries;
        for (uint32_t binding = 0; binding <= 6; ++binding)
            entries.emplace_back(binding).computeVisible().storageBuffer(false);
        entries.emplace_back(7).computeVisible().uniformBuffer(
            false, sizeof(Params));
        layout_ = gpu::createBindGroupLayout(
            device_, entries, "lockstep_layout");
        pipelineLayout_ = gpu::createPipelineLayout(
            device_, std::array{layout_}, "lockstep_pipeline_layout");
        if (!layout_ || !pipelineLayout_) {
            shutdown();
            return false;
        }
        WGPUComputePipelineDescriptor desc{};
        WGPU_SET_LABEL(desc, "physics_lockstep_step");
        desc.layout = pipelineLayout_;
        desc.compute.module = shader_;
        WGPU_SET_ENTRY_POINT(desc.compute, "step_lockstep");
        pipeline_ = ::voxy::gpu::createComputePipeline(device_, &desc);
        if (!pipeline_) {
            shutdown();
            return false;
        }
        allocatedBytes_ = size_t{config_.bodyCapacity}
                * (sizeof(LockstepBody) + 3u * sizeof(uint32_t))
            + size_t{config_.contactCapacity}
                * (sizeof(LockstepContact) + sizeof(uint32_t))
            + kTelemetryWords * sizeof(uint32_t)
            + gpu::alignUniformBufferSize(sizeof(Params));
        return true;
    }

    void shutdown() {
        releaseHandle(pipeline_, wgpuComputePipelineRelease);
        releaseHandle(pipelineLayout_, wgpuPipelineLayoutRelease);
        releaseHandle(layout_, wgpuBindGroupLayoutRelease);
        releaseHandle(shader_, wgpuShaderModuleRelease);
        releaseBuffer(params_);
        releaseBuffer(telemetry_);
        releaseBuffer(islandHashes_);
        releaseBuffer(contactHashes_);
        releaseBuffer(bodyHashes_);
        releaseBuffer(roots_);
        releaseBuffer(contacts_);
        releaseBuffer(bodies_);
        device_ = nullptr;
        queue_ = nullptr;
        config_ = {};
        allocatedBytes_ = 0;
    }

    bool uploadBodies(std::span<const LockstepBody> input) {
        if (!queue_ || input.size() > config_.bodyCapacity) return false;
        std::vector<LockstepBody> upload(config_.bodyCapacity);
        std::copy(input.begin(), input.end(), upload.begin());
        return gpu::writeBuffer(queue_, bodies_, 0,
                                std::span<const LockstepBody>(upload));
    }

    bool encode(WGPUCommandEncoder encoder, uint32_t tick) {
        if (!encoder || !pipeline_) return false;
        const Params values{
            .counts = {config_.bodyCapacity, config_.contactCapacity,
                       config_.substeps, config_.solverIterations},
            .integration = {config_.gravityPerSubstepQ16,
                            static_cast<int32_t>(uint64_t{config_.tickRateHz}
                                * config_.substeps
                                * kLockstepVelocityToPositionScale),
                            kLockstepSectorSize, kLockstepSectorHalf},
            .tick = {tick, 0u, kLockstepSchemaVersion, 0u},
        };
        if (!gpu::writeBuffer(queue_, params_, 0, values))
            return false;
        const std::array<gpu::BindGroupEntry, 8> entries = {
            gpu::BindGroupEntry(0).buffer(bodies_),
            gpu::BindGroupEntry(1).buffer(contacts_),
            gpu::BindGroupEntry(2).buffer(roots_),
            gpu::BindGroupEntry(3).buffer(bodyHashes_),
            gpu::BindGroupEntry(4).buffer(contactHashes_),
            gpu::BindGroupEntry(5).buffer(islandHashes_),
            gpu::BindGroupEntry(6).buffer(telemetry_),
            gpu::BindGroupEntry(7).buffer(params_),
        };
        WGPUBindGroup group = gpu::createBindGroup(
            device_, layout_, entries, "lockstep_bind_group");
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
        return true;
    }

    WGPUDevice device_ = nullptr;
    WGPUQueue queue_ = nullptr;
    Config config_{};
    size_t allocatedBytes_ = 0;
    WGPUBuffer bodies_ = nullptr;
    WGPUBuffer contacts_ = nullptr;
    WGPUBuffer roots_ = nullptr;
    WGPUBuffer bodyHashes_ = nullptr;
    WGPUBuffer contactHashes_ = nullptr;
    WGPUBuffer islandHashes_ = nullptr;
    WGPUBuffer telemetry_ = nullptr;
    WGPUBuffer params_ = nullptr;
    WGPUShaderModule shader_ = nullptr;
    WGPUBindGroupLayout layout_ = nullptr;
    WGPUPipelineLayout pipelineLayout_ = nullptr;
    WGPUComputePipeline pipeline_ = nullptr;
};

GpuLockstepWorld::GpuLockstepWorld() : impl_(std::make_unique<Impl>()) {}
GpuLockstepWorld::~GpuLockstepWorld() = default;
GpuLockstepWorld::GpuLockstepWorld(GpuLockstepWorld&&) noexcept = default;
GpuLockstepWorld& GpuLockstepWorld::operator=(
    GpuLockstepWorld&&) noexcept = default;

bool GpuLockstepWorld::initialize(
    WGPUDevice device, WGPUQueue queue, const Config& config) {
    auto replacement = std::make_unique<Impl>();
    if (!replacement->initialize(device, queue, config)) return false;
    impl_ = std::move(replacement);
    return true;
}
void GpuLockstepWorld::shutdown() {
    if (impl_) impl_->shutdown();
}
bool GpuLockstepWorld::uploadBodies(std::span<const LockstepBody> bodies) {
    return impl_ && impl_->uploadBodies(bodies);
}
bool GpuLockstepWorld::encode(WGPUCommandEncoder encoder, uint32_t tick) {
    return impl_ && impl_->encode(encoder, tick);
}
WGPUBuffer GpuLockstepWorld::bodyBuffer() const noexcept {
    return impl_ ? impl_->bodies_ : nullptr;
}
WGPUBuffer GpuLockstepWorld::contactBuffer() const noexcept {
    return impl_ ? impl_->contacts_ : nullptr;
}
WGPUBuffer GpuLockstepWorld::rootBuffer() const noexcept {
    return impl_ ? impl_->roots_ : nullptr;
}
WGPUBuffer GpuLockstepWorld::bodyHashBuffer() const noexcept {
    return impl_ ? impl_->bodyHashes_ : nullptr;
}
WGPUBuffer GpuLockstepWorld::contactHashBuffer() const noexcept {
    return impl_ ? impl_->contactHashes_ : nullptr;
}
WGPUBuffer GpuLockstepWorld::islandHashBuffer() const noexcept {
    return impl_ ? impl_->islandHashes_ : nullptr;
}
WGPUBuffer GpuLockstepWorld::telemetryBuffer() const noexcept {
    return impl_ ? impl_->telemetry_ : nullptr;
}
size_t GpuLockstepWorld::allocatedBytes() const noexcept {
    return impl_ ? impl_->allocatedBytes_ : 0u;
}

LockstepTelemetry GpuLockstepWorld::decodeTelemetry(
    std::span<const uint32_t> words) {
    LockstepTelemetry result;
    if (words.size() < 8u) return result;
    result.liveBodies = words[0];
    result.contacts = words[1];
    result.contactOverflow = words[2] != 0u;
    result.tick = words[3];
    result.hashes.world = words[4];
    result.hashes.bodyAggregate = words[5];
    result.hashes.contactAggregate = words[6];
    result.hashes.islandAggregate = words[7];
    return result;
}

} // namespace voxy::physics::deterministic
