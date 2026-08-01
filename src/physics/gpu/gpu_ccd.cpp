#include "physics/gpu/gpu_ccd.hpp"

#include "gpu/resources.hpp"
#include "physics/gpu/deterministic_primitives.hpp"
#include "physics/terrain_topology.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <string>
#include <utility>
#include <vector>

namespace voxy::physics {
namespace {

constexpr uint32_t kTelemetryWords = GpuCcd::kTelemetryWordCount;

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

WGPUComputePipeline makePipeline(WGPUDevice device, WGPUPipelineLayout layout,
                                 WGPUShaderModule shader,
                                 const std::string& entryPoint,
                                 const char* label) {
    WGPUComputePipelineDescriptor desc{};
    WGPU_SET_LABEL(desc, label);
    desc.layout = layout;
    desc.compute.module = shader;
    WGPU_SET_ENTRY_POINT(desc.compute, entryPoint.c_str());
    return wgpuDeviceCreateComputePipeline(device, &desc);
}

} // namespace

class GpuCcd::Impl {
public:
    struct alignas(16) Params {
        // body capacity, bullet capacity, coarse steps, workgroup size.
        std::array<uint32_t, 4> counts{};
        // terrain width, height, bisection iterations, flags.
        std::array<uint32_t, 4> terrain{};
        // centered XZ origin, cell scale, height scale.
        std::array<float, 4> terrainOriginCellHeight{};
        // dt, fast-distance ratio, linear slop, reserved.
        std::array<float, 4> tuning{};
        // Signed sector containing the terrain heightfield.
        std::array<int32_t, 4> worldSector{};
    };

    ~Impl() { shutdown(); }

    bool initialize(WGPUDevice device, WGPUQueue queue,
                    const Config& config) {
        shutdown();
        if (!device || !queue || config.bodyCapacity == 0
            || config.bulletCapacity == 0
            || config.bulletCapacity > config.bodyCapacity
            || (config.workgroupSize != 64 && config.workgroupSize != 128
                && config.workgroupSize != 256)
            || config.coarseSteps == 0 || config.coarseSteps > 32
            || config.bisectionIterations == 0
            || config.bisectionIterations > 16
            || !std::isfinite(config.fastDistanceRatio)
            || config.fastDistanceRatio < 0.0f
            || !std::isfinite(config.linearSlop)
            || config.linearSlop < 0.0f) {
            return false;
        }
        device_ = device;
        queue_ = queue;
        config_ = config;

        DeterministicGpuPrimitives::Config primitiveConfig;
        primitiveConfig.capacity = config_.bodyCapacity;
        primitiveConfig.workgroupSize = config_.workgroupSize;
        primitiveConfig.shaderPath = config_.primitivesShaderPath;
        primitiveConfig.shaderSources = config_.shaderSources;
        if (!primitives_.initialize(device_, queue_, primitiveConfig)) {
            shutdown();
            return false;
        }

        const auto makeStorage = [this](uint64_t bytes, const char* label) {
            return gpu::createBuffer(device_, gpu::BufferDesc{
                .label = label,
                .size = bytes,
                .usage = WGPUBufferUsage_Storage | WGPUBufferUsage_CopyDst
                       | WGPUBufferUsage_CopySrc,
            });
        };
        bodyValues_ = makeStorage(
            uint64_t{config_.bodyCapacity} * sizeof(uint32_t),
            "ccd_body_values");
        bulletPredicates_ = makeStorage(
            uint64_t{config_.bodyCapacity} * sizeof(uint32_t),
            "ccd_bullet_predicates");
        bulletIds_ = makeStorage(
            uint64_t{config_.bodyCapacity} * sizeof(uint32_t),
            "ccd_bullet_ids");
        telemetry_ = makeStorage(kTelemetryWords * sizeof(uint32_t),
                                 "ccd_telemetry");
        parameterBuffer_ = gpu::createBuffer(device_, gpu::BufferDesc{
            .label = "ccd_params",
            .size = gpu::alignUniformBufferSize(sizeof(Params)),
            .usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst,
        });
        if (!bodyValues_ || !bulletPredicates_ || !bulletIds_
            || !telemetry_ || !parameterBuffer_) {
            shutdown();
            return false;
        }
        const std::array<uint32_t, kTelemetryWords> zeros{};
        if (!gpu::writeBuffer(queue_, telemetry_, 0, zeros)) {
            shutdown();
            return false;
        }

        shader_ = gpu::loadShaderModule(
            device_, config_.shaderPath, "physics_ccd.wgsl",
            config_.shaderSources);
        if (!shader_ || !createPipelines()) {
            shutdown();
            return false;
        }
        allocatedBytes_ = primitives_.scratchBytes()
            + size_t{config_.bodyCapacity} * 3u * sizeof(uint32_t)
            + kTelemetryWords * sizeof(uint32_t)
            + gpu::alignUniformBufferSize(sizeof(Params));
        return true;
    }

    bool createPipelines() {
        using LE = gpu::BindGroupLayoutEntry;
        std::vector<LE> entries;
        entries.emplace_back(3).computeVisible().storageBuffer(false);
        entries.emplace_back(4).computeVisible().storageBuffer(false);
        entries.emplace_back(5).computeVisible().storageBuffer(false);
        entries.emplace_back(9).computeVisible().uniformBuffer(
            false, sizeof(Params));
        markLayout_ = gpu::createBindGroupLayout(
            device_, entries, "ccd_mark_layout");

        entries.clear();
        entries.emplace_back(7).computeVisible().storageBuffer(true);
        entries.emplace_back(8).computeVisible().storageBuffer(false);
        entries.emplace_back(9).computeVisible().uniformBuffer(
            false, sizeof(Params));
        telemetryLayout_ = gpu::createBindGroupLayout(
            device_, entries, "ccd_telemetry_layout");

        entries.clear();
        entries.emplace_back(0).computeVisible().storageBuffer(false);
        entries.emplace_back(1).computeVisible().storageBuffer(false);
        entries.emplace_back(2).computeVisible().storageBuffer(true);
        entries.emplace_back(3).computeVisible().storageBuffer(false);
        entries.emplace_back(6).computeVisible().storageBuffer(true);
        entries.emplace_back(7).computeVisible().storageBuffer(true);
        entries.emplace_back(8).computeVisible().storageBuffer(false);
        entries.emplace_back(9).computeVisible().uniformBuffer(
            false, sizeof(Params));
        entries.emplace_back(10).computeVisible().texture(
            WGPUTextureSampleType_Uint, WGPUTextureViewDimension_2D, false);
        executeLayout_ = gpu::createBindGroupLayout(
            device_, entries, "ccd_execute_layout");
        if (!markLayout_ || !telemetryLayout_ || !executeLayout_) return false;

        markPipelineLayout_ = gpu::createPipelineLayout(
            device_, std::array{markLayout_}, "ccd_mark_pipeline_layout");
        telemetryPipelineLayout_ = gpu::createPipelineLayout(
            device_, std::array{telemetryLayout_},
            "ccd_telemetry_pipeline_layout");
        executePipelineLayout_ = gpu::createPipelineLayout(
            device_, std::array{executeLayout_},
            "ccd_execute_pipeline_layout");
        if (!markPipelineLayout_ || !telemetryPipelineLayout_
            || !executePipelineLayout_) return false;

        const std::string suffix = std::to_string(config_.workgroupSize);
        markPipeline_ = makePipeline(
            device_, markPipelineLayout_, shader_, "mark_bullets_" + suffix,
            "ccd_mark_bullets");
        preparePipeline_ = makePipeline(
            device_, telemetryPipelineLayout_, shader_, "prepare_ccd",
            "ccd_prepare");
        executePipeline_ = makePipeline(
            device_, executePipelineLayout_, shader_,
            "execute_ccd_" + suffix, "ccd_execute");
        finalizePipeline_ = makePipeline(
            device_, telemetryPipelineLayout_, shader_, "finalize_ccd",
            "ccd_finalize");
        return markPipeline_ && preparePipeline_ && executePipeline_
            && finalizePipeline_;
    }

    void setInput(const GpuCcdInput& input) {
        input_ = input.valid() && input.bodyCapacity <= config_.bodyCapacity
            ? input : GpuCcdInput{};
    }

    bool encode(WGPUCommandEncoder encoder, float deltaTime) {
        if (!encoder || !input_.valid() || !std::isfinite(deltaTime)
            || deltaTime <= 0.0f) return false;
        const auto origin = terrain_topology::centeredOrigin(
            input_.terrainWidth, input_.terrainHeight,
            input_.terrainCellScale);
        const Params params{
            .counts = {input_.bodyCapacity, config_.bulletCapacity,
                       config_.coarseSteps, config_.workgroupSize},
            .terrain = {input_.terrainWidth, input_.terrainHeight,
                        config_.bisectionIterations, 1u},
            .terrainOriginCellHeight = {
                origin.x, origin.y, input_.terrainCellScale,
                input_.terrainHeightScale},
            .tuning = {deltaTime, config_.fastDistanceRatio,
                       config_.linearSlop, 0.0f},
            .worldSector = {input_.terrainSector[0], input_.terrainSector[1],
                            input_.terrainSector[2], 0},
        };
        if (!gpu::writeBuffer(queue_, parameterBuffer_, 0, params))
            return false;

        const std::array<gpu::BindGroupEntry, 4> markEntries = {
            gpu::BindGroupEntry(3).buffer(input_.metadataBuffer),
            gpu::BindGroupEntry(4).buffer(bodyValues_),
            gpu::BindGroupEntry(5).buffer(bulletPredicates_),
            gpu::BindGroupEntry(9).buffer(parameterBuffer_),
        };
        WGPUBindGroup markGroup = gpu::createBindGroup(
            device_, markLayout_, markEntries, "ccd_mark_bind_group");
        if (!markGroup) return false;
        WGPUComputePassDescriptor passDesc{};
        WGPUComputePassEncoder markPass =
            wgpuCommandEncoderBeginComputePass(encoder, &passDesc);
        if (!markPass) {
            wgpuBindGroupRelease(markGroup);
            return false;
        }
        wgpuComputePassEncoderSetPipeline(markPass, markPipeline_);
        wgpuComputePassEncoderSetBindGroup(markPass, 0, markGroup, 0, nullptr);
        const uint32_t groups = (input_.bodyCapacity
            + config_.workgroupSize - 1u) / config_.workgroupSize;
        wgpuComputePassEncoderDispatchWorkgroups(markPass, groups, 1, 1);
        wgpuComputePassEncoderEnd(markPass);
        wgpuComputePassEncoderRelease(markPass);
        wgpuBindGroupRelease(markGroup);

        if (!primitives_.encodeStableCompactU32(
                encoder, bodyValues_, bulletPredicates_, bulletIds_,
                input_.bodyCapacity)) return false;

        const std::array<gpu::BindGroupEntry, 3> telemetryEntries = {
            gpu::BindGroupEntry(7).buffer(primitives_.resultBuffer()),
            gpu::BindGroupEntry(8).buffer(telemetry_),
            gpu::BindGroupEntry(9).buffer(parameterBuffer_),
        };
        WGPUBindGroup telemetryGroup = gpu::createBindGroup(
            device_, telemetryLayout_, telemetryEntries,
            "ccd_telemetry_bind_group");
        const std::array<gpu::BindGroupEntry, 9> executeEntries = {
            gpu::BindGroupEntry(0).buffer(input_.poseBuffer),
            gpu::BindGroupEntry(1).buffer(input_.motionBuffer),
            gpu::BindGroupEntry(2).buffer(input_.shapeBuffer),
            gpu::BindGroupEntry(3).buffer(input_.metadataBuffer),
            gpu::BindGroupEntry(6).buffer(bulletIds_),
            gpu::BindGroupEntry(7).buffer(primitives_.resultBuffer()),
            gpu::BindGroupEntry(8).buffer(telemetry_),
            gpu::BindGroupEntry(9).buffer(parameterBuffer_),
            gpu::BindGroupEntry(10).textureView(input_.terrainTexture),
        };
        WGPUBindGroup executeGroup = gpu::createBindGroup(
            device_, executeLayout_, executeEntries, "ccd_execute_bind_group");
        if (!telemetryGroup || !executeGroup) {
            if (telemetryGroup) wgpuBindGroupRelease(telemetryGroup);
            if (executeGroup) wgpuBindGroupRelease(executeGroup);
            return false;
        }

        WGPUComputePassEncoder pass =
            wgpuCommandEncoderBeginComputePass(encoder, &passDesc);
        if (!pass) {
            wgpuBindGroupRelease(executeGroup);
            wgpuBindGroupRelease(telemetryGroup);
            return false;
        }
        wgpuComputePassEncoderSetBindGroup(
            pass, 0, telemetryGroup, 0, nullptr);
        wgpuComputePassEncoderSetPipeline(pass, preparePipeline_);
        wgpuComputePassEncoderDispatchWorkgroups(pass, 1, 1, 1);
        wgpuComputePassEncoderSetBindGroup(pass, 0, executeGroup, 0, nullptr);
        wgpuComputePassEncoderSetPipeline(pass, executePipeline_);
        wgpuComputePassEncoderDispatchWorkgroups(pass, groups, 1, 1);
        wgpuComputePassEncoderSetBindGroup(
            pass, 0, telemetryGroup, 0, nullptr);
        wgpuComputePassEncoderSetPipeline(pass, finalizePipeline_);
        wgpuComputePassEncoderDispatchWorkgroups(pass, 1, 1, 1);
        wgpuComputePassEncoderEnd(pass);
        wgpuComputePassEncoderRelease(pass);
        wgpuBindGroupRelease(executeGroup);
        wgpuBindGroupRelease(telemetryGroup);
        return true;
    }

    void shutdown() {
        releaseHandle(markPipeline_, wgpuComputePipelineRelease);
        releaseHandle(preparePipeline_, wgpuComputePipelineRelease);
        releaseHandle(executePipeline_, wgpuComputePipelineRelease);
        releaseHandle(finalizePipeline_, wgpuComputePipelineRelease);
        releaseHandle(markPipelineLayout_, wgpuPipelineLayoutRelease);
        releaseHandle(telemetryPipelineLayout_, wgpuPipelineLayoutRelease);
        releaseHandle(executePipelineLayout_, wgpuPipelineLayoutRelease);
        releaseHandle(markLayout_, wgpuBindGroupLayoutRelease);
        releaseHandle(telemetryLayout_, wgpuBindGroupLayoutRelease);
        releaseHandle(executeLayout_, wgpuBindGroupLayoutRelease);
        releaseHandle(shader_, wgpuShaderModuleRelease);
        primitives_.shutdown();
        releaseBuffer(parameterBuffer_);
        releaseBuffer(bodyValues_);
        releaseBuffer(bulletPredicates_);
        releaseBuffer(bulletIds_);
        releaseBuffer(telemetry_);
        device_ = nullptr;
        queue_ = nullptr;
        config_ = {};
        input_ = {};
        allocatedBytes_ = 0;
    }

    WGPUDevice device_ = nullptr;
    WGPUQueue queue_ = nullptr;
    Config config_{};
    GpuCcdInput input_{};
    size_t allocatedBytes_ = 0;
    DeterministicGpuPrimitives primitives_{};
    WGPUBuffer bodyValues_ = nullptr;
    WGPUBuffer bulletPredicates_ = nullptr;
    WGPUBuffer bulletIds_ = nullptr;
    WGPUBuffer telemetry_ = nullptr;
    WGPUBuffer parameterBuffer_ = nullptr;
    WGPUShaderModule shader_ = nullptr;
    WGPUBindGroupLayout markLayout_ = nullptr;
    WGPUBindGroupLayout telemetryLayout_ = nullptr;
    WGPUBindGroupLayout executeLayout_ = nullptr;
    WGPUPipelineLayout markPipelineLayout_ = nullptr;
    WGPUPipelineLayout telemetryPipelineLayout_ = nullptr;
    WGPUPipelineLayout executePipelineLayout_ = nullptr;
    WGPUComputePipeline markPipeline_ = nullptr;
    WGPUComputePipeline preparePipeline_ = nullptr;
    WGPUComputePipeline executePipeline_ = nullptr;
    WGPUComputePipeline finalizePipeline_ = nullptr;
};

GpuCcd::GpuCcd() : impl_(std::make_unique<Impl>()) {}
GpuCcd::~GpuCcd() = default;
GpuCcd::GpuCcd(GpuCcd&&) noexcept = default;
GpuCcd& GpuCcd::operator=(GpuCcd&&) noexcept = default;

bool GpuCcd::initialize(WGPUDevice device, WGPUQueue queue,
                        const Config& config) {
    auto replacement = std::make_unique<Impl>();
    if (!replacement->initialize(device, queue, config)) return false;
    impl_ = std::move(replacement);
    return true;
}
void GpuCcd::shutdown() {
    if (impl_) impl_->shutdown();
}
void GpuCcd::setInput(const GpuCcdInput& input) {
    if (impl_) impl_->setInput(input);
}
bool GpuCcd::encode(WGPUCommandEncoder encoder, float deltaTime) {
    return impl_ && impl_->encode(encoder, deltaTime);
}
WGPUBuffer GpuCcd::bulletBodyIds() const noexcept {
    return impl_ ? impl_->bulletIds_ : nullptr;
}
WGPUBuffer GpuCcd::telemetryBuffer() const noexcept {
    return impl_ ? impl_->telemetry_ : nullptr;
}
size_t GpuCcd::allocatedBytes() const noexcept {
    return impl_ ? impl_->allocatedBytes_ : 0u;
}

GpuCcdTelemetry GpuCcd::decodeTelemetry(
    std::span<const uint32_t> words) noexcept {
    GpuCcdTelemetry result;
    if (words.size() < 15u) return result;
    result.fastCandidates = words[0];
    result.bulletRequested = words[1];
    result.bulletProcessed = words[2];
    result.bulletOverflow = words[3];
    result.hits = words[4];
    result.stalls = words[5];
    result.failures = words[6];
    result.maximumIterations = words[7];
    result.highFastCandidates = words[8];
    result.highBulletRequested = words[9];
    result.highBulletOverflow = words[10];
    result.highHits = words[11];
    result.highStalls = words[12];
    result.highFailures = words[13];
    result.tick = words[14];
    return result;
}

} // namespace voxy::physics
