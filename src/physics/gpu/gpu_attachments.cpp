#include "physics/gpu/gpu_attachments.hpp"

#include "gpu/resources.hpp"

#include <array>
#include <cmath>
#include <utility>
#include <vector>

namespace voxy::physics {
namespace {

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

class GpuAttachmentSolver::Impl {
public:
    struct alignas(16) Params {
        // Attachment capacity, command count, body capacity, command capacity.
        std::array<uint32_t, 4> counts{};
        // Tick seconds, linear slop, positional bias, reserved.
        std::array<float, 4> tuning{};
        // Maximum sector delta, reserved.
        std::array<uint32_t, 4> world{};
    };

    ~Impl() { shutdown(); }

    bool initialize(WGPUDevice device, WGPUQueue queue,
                    const Config& config) {
        shutdown();
        if (!device || !queue || config.attachmentCapacity < 2u
            || config.commandCapacity == 0u
            || !std::isfinite(config.tickSeconds)
            || config.tickSeconds <= 0.0f
            || !std::isfinite(config.linearSlop)
            || config.linearSlop < 0.0f
            || !std::isfinite(config.biasFactor)
            || config.biasFactor < 0.0f
            || config.maximumSectorDelta == 0u
            || config.maximumSectorDelta > 4'096u) {
            return false;
        }
        device_ = device;
        queue_ = queue;
        config_ = config;

        const auto storageUsage = WGPUBufferUsage_Storage
                                | WGPUBufferUsage_CopyDst
                                | WGPUBufferUsage_CopySrc;
        attachments_ = gpu::createBuffer(device_, gpu::BufferDesc{
            .label = "physics_distance_attachments",
            .size = uint64_t{config_.attachmentCapacity}
                  * sizeof(GpuDistanceAttachment),
            .usage = storageUsage,
        });
        commands_ = gpu::createBuffer(device_, gpu::BufferDesc{
            .label = "physics_attachment_commands",
            .size = uint64_t{config_.commandCapacity}
                  * sizeof(GpuAttachmentCommand),
            .usage = WGPUBufferUsage_Storage | WGPUBufferUsage_CopyDst,
        });
        telemetry_ = gpu::createBuffer(device_, gpu::BufferDesc{
            .label = "physics_attachment_telemetry",
            .size = kTelemetryWordCount * sizeof(uint32_t),
            .usage = storageUsage,
        });
        params_ = gpu::createBuffer(device_, gpu::BufferDesc{
            .label = "physics_attachment_params",
            .size = gpu::alignUniformBufferSize(sizeof(Params)),
            .usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst,
        });
        if (!attachments_ || !commands_ || !telemetry_ || !params_) {
            shutdown();
            return false;
        }
        const std::vector<GpuDistanceAttachment> empty(
            config_.attachmentCapacity);
        const std::array<uint32_t, kTelemetryWordCount> zeroTelemetry{};
        if (!gpu::writeBuffer(
                queue_, attachments_, 0,
                std::span<const GpuDistanceAttachment>(empty))
            || !gpu::writeBuffer(queue_, telemetry_, 0, zeroTelemetry)) {
            shutdown();
            return false;
        }

        shader_ = gpu::loadShaderModule(
            device_, config_.shaderPath, "physics_attachments.wgsl",
            config_.shaderSources);
        if (!shader_) {
            shutdown();
            return false;
        }

        using LE = gpu::BindGroupLayoutEntry;
        std::vector<LE> entries;
        entries.emplace_back(0).computeVisible().storageBuffer(false);
        entries.emplace_back(1).computeVisible().storageBuffer(false);
        entries.emplace_back(2).computeVisible().storageBuffer(true);
        entries.emplace_back(3).computeVisible().storageBuffer(false);
        entries.emplace_back(4).computeVisible().storageBuffer(false);
        entries.emplace_back(5).computeVisible().storageBuffer(false);
        entries.emplace_back(6).computeVisible().storageBuffer(true);
        entries.emplace_back(7).computeVisible().storageBuffer(false);
        entries.emplace_back(8).computeVisible().uniformBuffer(
            false, sizeof(Params));
        layout_ = gpu::createBindGroupLayout(
            device_, entries, "physics_attachment_layout");
        pipelineLayout_ = gpu::createPipelineLayout(
            device_, std::array{layout_},
            "physics_attachment_pipeline_layout");
        if (!layout_ || !pipelineLayout_) {
            shutdown();
            return false;
        }
        WGPUComputePipelineDescriptor pipelineDesc{};
        WGPU_SET_LABEL(pipelineDesc, "physics_solve_distance_attachments");
        pipelineDesc.layout = pipelineLayout_;
        pipelineDesc.compute.module = shader_;
        WGPU_SET_ENTRY_POINT(pipelineDesc.compute, "solve_attachments");
        pipeline_ = wgpuDeviceCreateComputePipeline(device_, &pipelineDesc);
        if (!pipeline_) {
            shutdown();
            return false;
        }
        persistentBytes_ = size_t{config_.attachmentCapacity}
                         * sizeof(GpuDistanceAttachment);
        scratchBytes_ = size_t{config_.commandCapacity}
                      * sizeof(GpuAttachmentCommand)
                      + kTelemetryWordCount * sizeof(uint32_t)
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
        releaseBuffer(commands_);
        releaseBuffer(attachments_);
        device_ = nullptr;
        queue_ = nullptr;
        config_ = {};
        input_ = {};
        commandCount_ = 0u;
        persistentBytes_ = 0u;
        scratchBytes_ = 0u;
    }

    void setInput(const GpuAttachmentInput& input) {
        input_ = input.valid() ? input : GpuAttachmentInput{};
    }

    bool uploadCommands(std::span<const GpuAttachmentCommand> commands) {
        if (!queue_ || commands.size() > config_.commandCapacity) return false;
        commandCount_ = static_cast<uint32_t>(commands.size());
        return commands.empty()
            || gpu::writeBuffer(queue_, commands_, 0,
                std::as_bytes(commands));
    }

    bool encode(WGPUCommandEncoder encoder) {
        if (!encoder || !input_.valid()) return false;
        const Params values{
            .counts = {config_.attachmentCapacity, commandCount_,
                       input_.bodyCapacity, config_.commandCapacity},
            .tuning = {config_.tickSeconds, config_.linearSlop,
                       config_.biasFactor, 0.0f},
            .world = {config_.maximumSectorDelta, 0u, 0u, 0u},
        };
        if (!gpu::writeBuffer(queue_, params_, 0, values)) return false;
        const std::array<gpu::BindGroupEntry, 9> entries = {
            gpu::BindGroupEntry(0).buffer(input_.poseBuffer),
            gpu::BindGroupEntry(1).buffer(input_.motionBuffer),
            gpu::BindGroupEntry(2).buffer(input_.shapeBuffer),
            gpu::BindGroupEntry(3).buffer(input_.metadataBuffer),
            gpu::BindGroupEntry(4).buffer(input_.coreCountersBuffer),
            gpu::BindGroupEntry(5).buffer(attachments_),
            gpu::BindGroupEntry(6).buffer(commands_),
            gpu::BindGroupEntry(7).buffer(telemetry_),
            gpu::BindGroupEntry(8).buffer(params_),
        };
        WGPUBindGroup group = gpu::createBindGroup(
            device_, layout_, entries, "physics_attachment_bind_group");
        if (!group) return false;
        WGPUComputePassDescriptor passDesc{};
        WGPU_SET_LABEL(passDesc, "physics_attachment_pass");
        WGPUComputePassEncoder pass =
            wgpuCommandEncoderBeginComputePass(encoder, &passDesc);
        if (!pass) {
            wgpuBindGroupRelease(group);
            return false;
        }
        wgpuComputePassEncoderSetPipeline(pass, pipeline_);
        wgpuComputePassEncoderSetBindGroup(pass, 0, group, 0, nullptr);
        wgpuComputePassEncoderDispatchWorkgroups(pass, 1u, 1u, 1u);
        wgpuComputePassEncoderEnd(pass);
        wgpuComputePassEncoderRelease(pass);
        wgpuBindGroupRelease(group);
        return true;
    }

    WGPUDevice device_ = nullptr;
    WGPUQueue queue_ = nullptr;
    Config config_{};
    GpuAttachmentInput input_{};
    uint32_t commandCount_ = 0u;
    size_t persistentBytes_ = 0u;
    size_t scratchBytes_ = 0u;
    WGPUBuffer attachments_ = nullptr;
    WGPUBuffer commands_ = nullptr;
    WGPUBuffer telemetry_ = nullptr;
    WGPUBuffer params_ = nullptr;
    WGPUShaderModule shader_ = nullptr;
    WGPUBindGroupLayout layout_ = nullptr;
    WGPUPipelineLayout pipelineLayout_ = nullptr;
    WGPUComputePipeline pipeline_ = nullptr;
};

GpuAttachmentSolver::GpuAttachmentSolver()
    : impl_(std::make_unique<Impl>()) {}
GpuAttachmentSolver::~GpuAttachmentSolver() = default;
GpuAttachmentSolver::GpuAttachmentSolver(
    GpuAttachmentSolver&&) noexcept = default;
GpuAttachmentSolver& GpuAttachmentSolver::operator=(
    GpuAttachmentSolver&&) noexcept = default;

bool GpuAttachmentSolver::initialize(
    WGPUDevice device, WGPUQueue queue, const Config& config) {
    auto replacement = std::make_unique<Impl>();
    if (!replacement->initialize(device, queue, config)) return false;
    impl_ = std::move(replacement);
    return true;
}

void GpuAttachmentSolver::shutdown() {
    if (impl_) impl_->shutdown();
}

void GpuAttachmentSolver::setInput(const GpuAttachmentInput& input) {
    if (impl_) impl_->setInput(input);
}

bool GpuAttachmentSolver::uploadCommands(
    std::span<const GpuAttachmentCommand> commands) {
    return impl_ && impl_->uploadCommands(commands);
}

bool GpuAttachmentSolver::encode(WGPUCommandEncoder encoder) {
    return impl_ && impl_->encode(encoder);
}

WGPUBuffer GpuAttachmentSolver::attachmentBuffer() const noexcept {
    return impl_ ? impl_->attachments_ : nullptr;
}

WGPUBuffer GpuAttachmentSolver::telemetryBuffer() const noexcept {
    return impl_ ? impl_->telemetry_ : nullptr;
}

size_t GpuAttachmentSolver::persistentBytes() const noexcept {
    return impl_ ? impl_->persistentBytes_ : 0u;
}

size_t GpuAttachmentSolver::scratchBytes() const noexcept {
    return impl_ ? impl_->scratchBytes_ : 0u;
}

GpuAttachmentTelemetry GpuAttachmentSolver::decodeTelemetry(
    std::span<const uint32_t> words) noexcept {
    GpuAttachmentTelemetry result;
    if (words.size() < 13u) return result;
    result.live = words[0];
    result.taut = words[1];
    result.slack = words[2];
    result.breaks = words[3];
    result.commands = words[4];
    result.staleCommands = words[5];
    result.invalidEndpoints = words[6];
    result.tick = words[7];
    result.highLive = words[8];
    result.highTaut = words[9];
    result.highSlack = words[10];
    result.highBreaks = words[11];
    result.highCommands = words[12];
    return result;
}

} // namespace voxy::physics
