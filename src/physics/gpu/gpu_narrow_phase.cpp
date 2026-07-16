#include "physics/gpu/gpu_narrow_phase.hpp"

#include "gpu/resources.hpp"

#include <algorithm>
#include <array>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace voxy::physics {
namespace {

constexpr uint32_t kTelemetryWords = GpuNarrowPhase::kTelemetryWordCount;
constexpr uint32_t kClassTableWords = 32;

template <typename T>
void releaseHandle(T& handle, void (*release)(T)) {
    if (handle) {
        release(handle);
        handle = nullptr;
    }
}

void releaseBuffer(WGPUBuffer& buffer) {
    if (buffer) {
        wgpuBufferDestroy(buffer);
        wgpuBufferRelease(buffer);
        buffer = nullptr;
    }
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

class GpuNarrowPhase::Impl {
public:
    struct alignas(16) Params {
        std::array<uint32_t, 4> capacities{};
        std::array<float, 4> tolerances{};
        std::array<uint32_t, 4> dispatch{};
    };

    ~Impl() { shutdown(); }

    bool initialize(WGPUDevice device, WGPUQueue queue, const Config& config) {
        if (device_ || !device || !queue || config.pairCapacity == 0
            || config.linearSlop <= 0.0f || config.speculativeDistance < 0.0f
            || config.recycleDistance < config.linearSlop
            || (config.workgroupSize != 64 && config.workgroupSize != 128
                && config.workgroupSize != 256)) {
            return false;
        }
        device_ = device;
        queue_ = queue;
        config_ = config;
        if (config_.manifoldCapacity == 0u) {
            config_.manifoldCapacity = config_.pairCapacity;
        }
        if (config_.dispatchContactCapacity == 0u) {
            config_.dispatchContactCapacity = config_.manifoldCapacity;
        }
        config_.dispatchContactCapacity = std::min(
            config_.dispatchContactCapacity, config_.manifoldCapacity);

        const auto makeStorage = [this](uint64_t bytes, const char* label) {
            return gpu::createBuffer(device_, gpu::BufferDesc{
                .label = label,
                .size = bytes,
                .usage = WGPUBufferUsage_Storage | WGPUBufferUsage_CopyDst
                       | WGPUBufferUsage_CopySrc,
            });
        };
        parameterBuffer_ = gpu::createBuffer(device_, gpu::BufferDesc{
            .label = "narrow_phase_params",
            .size = sizeof(Params),
            .usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst,
        });
        bucketedPairs_ = makeStorage(
            uint64_t{config.pairCapacity} * sizeof(GpuKeyValue),
            "narrow_phase_pair_buckets");
        classTable_ = makeStorage(kClassTableWords * sizeof(uint32_t),
                                  "narrow_phase_class_table");
        classDispatchArgs_ = gpu::createBuffer(device_, gpu::BufferDesc{
            .label = "narrow_phase_class_dispatch_args",
            .size = uint64_t{kGpuNarrowPhasePairClassCount + 1u} * 4u
                  * sizeof(uint32_t),
            .usage = WGPUBufferUsage_Storage | WGPUBufferUsage_CopyDst
                   | WGPUBufferUsage_Indirect,
        });
        manifoldsA_ = makeStorage(
            uint64_t{config_.manifoldCapacity} * sizeof(GpuContactManifold),
            "narrow_phase_manifolds_a");
        manifoldsB_ = makeStorage(
            uint64_t{config_.manifoldCapacity} * sizeof(GpuContactManifold),
            "narrow_phase_manifolds_b");
        telemetry_ = makeStorage(kTelemetryWords * sizeof(uint32_t),
                                 "narrow_phase_telemetry");
        if (!parameterBuffer_ || !bucketedPairs_ || !classTable_
            || !classDispatchArgs_
            || !manifoldsA_ || !manifoldsB_ || !telemetry_) {
            shutdown();
            return false;
        }
        const std::array<uint32_t, kTelemetryWords> zeroTelemetry{};
        gpu::writeBuffer(queue_, telemetry_, 0, zeroTelemetry);

        scratchBytes_ = gpu::saturatingSize(
            sizeof(Params)
            + uint64_t{config.pairCapacity} * sizeof(GpuKeyValue)
            + kClassTableWords * sizeof(uint32_t)
            + uint64_t{kGpuNarrowPhasePairClassCount + 1u} * 4u
                * sizeof(uint32_t)
            + uint64_t{config_.manifoldCapacity} * 2u
                * sizeof(GpuContactManifold)
            + kTelemetryWords * sizeof(uint32_t));

        shaderModule_ = gpu::loadShaderModule(
            device_, config.shaderPath, "physics_narrow_phase.wgsl");
        if (!shaderModule_ || !createPipelines()) {
            shutdown();
            return false;
        }
        return true;
    }

    bool createPipelines() {
        using LE = gpu::BindGroupLayoutEntry;
        const auto storage = [](std::vector<LE>& entries, uint32_t binding,
                                bool readOnly) {
            entries.emplace_back(binding).computeVisible().storageBuffer(readOnly);
        };
        const auto uniform = [](std::vector<LE>& entries) {
            entries.emplace_back(10).computeVisible().uniformBuffer(
                false, sizeof(Params));
        };

        std::vector<LE> bucketEntries;
        storage(bucketEntries, 1, true);
        storage(bucketEntries, 2, true);
        storage(bucketEntries, 3, true);
        storage(bucketEntries, 4, false);
        storage(bucketEntries, 5, false);
        storage(bucketEntries, 9, false);
        storage(bucketEntries, 11, false);
        uniform(bucketEntries);
        bucketLayout_ = gpu::createBindGroupLayout(
            device_, bucketEntries, "narrow_phase_bucket_layout");

        std::vector<LE> narrowEntries;
        storage(narrowEntries, 0, true);
        storage(narrowEntries, 1, true);
        storage(narrowEntries, 4, false);
        storage(narrowEntries, 5, false);
        storage(narrowEntries, 6, true);
        storage(narrowEntries, 7, false);
        storage(narrowEntries, 8, true);
        storage(narrowEntries, 9, false);
        uniform(narrowEntries);
        narrowLayout_ = gpu::createBindGroupLayout(
            device_, narrowEntries, "narrow_phase_collision_layout");

        std::vector<LE> finalizeEntries;
        storage(finalizeEntries, 3, true);
        storage(finalizeEntries, 9, false);
        uniform(finalizeEntries);
        finalizeLayout_ = gpu::createBindGroupLayout(
            device_, finalizeEntries, "narrow_phase_finalize_layout");
        if (!bucketLayout_ || !narrowLayout_ || !finalizeLayout_) return false;

        bucketPipelineLayout_ = gpu::createPipelineLayout(
            device_, std::array{bucketLayout_},
            "narrow_phase_bucket_pipeline_layout");
        narrowPipelineLayout_ = gpu::createPipelineLayout(
            device_, std::array{narrowLayout_},
            "narrow_phase_collision_pipeline_layout");
        finalizePipelineLayout_ = gpu::createPipelineLayout(
            device_, std::array{finalizeLayout_},
            "narrow_phase_finalize_pipeline_layout");
        if (!bucketPipelineLayout_ || !narrowPipelineLayout_
            || !finalizePipelineLayout_) return false;

        const std::string suffix = std::to_string(config_.workgroupSize);
        resetBucketsPipeline_ = makePipeline(
            device_, bucketPipelineLayout_, shaderModule_,
            "reset_pair_buckets", "narrow_phase_reset_buckets");
        countBucketsPipeline_ = makePipeline(
            device_, bucketPipelineLayout_, shaderModule_,
            "count_pair_classes_" + suffix,
            "narrow_phase_count_buckets");
        finalizeBucketsPipeline_ = makePipeline(
            device_, bucketPipelineLayout_, shaderModule_,
            "finalize_pair_buckets", "narrow_phase_finalize_buckets");
        scatterBucketsPipeline_ = makePipeline(
            device_, bucketPipelineLayout_, shaderModule_,
            "scatter_pair_classes_" + suffix,
            "narrow_phase_scatter_buckets");
        finalizePipeline_ = makePipeline(
            device_, finalizePipelineLayout_, shaderModule_, "finalize_narrow",
            "narrow_phase_finalize");
        constexpr std::array<const char*, kGpuNarrowPhasePairClassCount> names = {
            "sphere_sphere", "sphere_capsule", "capsule_capsule",
            "sphere_box", "capsule_box", "box_box", "sphere_cylinder",
            "capsule_cylinder", "box_cylinder", "cylinder_cylinder"};
        for (uint32_t index = 0; index < names.size(); ++index) {
            classPipelines_[index] = makePipeline(
                device_, narrowPipelineLayout_, shaderModule_,
                std::string("narrow_") + names[index] + "_" + suffix,
                "narrow_phase_pair_class");
        }
        if (!resetBucketsPipeline_ || !countBucketsPipeline_
            || !finalizeBucketsPipeline_ || !scatterBucketsPipeline_
            || !finalizePipeline_) return false;
        for (WGPUComputePipeline pipeline : classPipelines_) {
            if (!pipeline) return false;
        }
        return true;
    }

    static bool sameInputBindings(const GpuNarrowPhaseInput& lhs,
                                  const GpuNarrowPhaseInput& rhs) {
        return lhs.poseBuffer == rhs.poseBuffer
            && lhs.shapeBuffer == rhs.shapeBuffer
            && lhs.uniquePairBuffer == rhs.uniquePairBuffer
            && lhs.broadPhaseTelemetryBuffer
                == rhs.broadPhaseTelemetryBuffer
            && lhs.metadataBuffer == rhs.metadataBuffer;
    }

    void releaseCachedInputGroups() {
        for (WGPUBindGroup& group : cachedInputGroups_) {
            releaseHandle(group, wgpuBindGroupRelease);
        }
    }

    void setInput(const GpuNarrowPhaseInput& input) {
        const GpuNarrowPhaseInput next = input.bodyCapacity != 0
              && input.pairCapacity <= config_.pairCapacity
            ? input : GpuNarrowPhaseInput{};
        if (!next.valid() || !sameInputBindings(input_, next)) {
            releaseCachedInputGroups();
        }
        input_ = next;
    }

    bool ensureCachedInputGroups() {
        if (!cachedInputGroups_[0]) {
            const std::array<gpu::BindGroupEntry, 8> entries = {
                gpu::BindGroupEntry(1).buffer(input_.shapeBuffer),
                gpu::BindGroupEntry(2).buffer(input_.uniquePairBuffer),
                gpu::BindGroupEntry(3).buffer(
                    input_.broadPhaseTelemetryBuffer),
                gpu::BindGroupEntry(4).buffer(bucketedPairs_),
                gpu::BindGroupEntry(5).buffer(classTable_),
                gpu::BindGroupEntry(9).buffer(telemetry_),
                gpu::BindGroupEntry(10).buffer(parameterBuffer_),
                gpu::BindGroupEntry(11).buffer(classDispatchArgs_),
            };
            cachedInputGroups_[0] = gpu::createBindGroup(
                device_, bucketLayout_, entries,
                "narrow_phase_bucket_bind_group");
            ++inputBindGroupCacheMisses_;
        }
        if (!cachedInputGroups_[1]) {
            const std::array<gpu::BindGroupEntry, 3> entries = {
                gpu::BindGroupEntry(3).buffer(
                    input_.broadPhaseTelemetryBuffer),
                gpu::BindGroupEntry(9).buffer(telemetry_),
                gpu::BindGroupEntry(10).buffer(parameterBuffer_),
            };
            cachedInputGroups_[1] = gpu::createBindGroup(
                device_, finalizeLayout_, entries,
                "narrow_phase_finalize_bind_group");
            ++inputBindGroupCacheMisses_;
        }

        const uint32_t parity = manifoldsAreB_ ? 1u : 0u;
        WGPUBindGroup& narrowGroup = cachedInputGroups_[2u + parity];
        if (!narrowGroup) {
            WGPUBuffer previous = manifoldsAreB_ ? manifoldsB_ : manifoldsA_;
            WGPUBuffer next = manifoldsAreB_ ? manifoldsA_ : manifoldsB_;
            const std::array<gpu::BindGroupEntry, 9> entries = {
                gpu::BindGroupEntry(0).buffer(input_.poseBuffer),
                gpu::BindGroupEntry(1).buffer(input_.shapeBuffer),
                gpu::BindGroupEntry(4).buffer(bucketedPairs_),
                gpu::BindGroupEntry(5).buffer(classTable_),
                gpu::BindGroupEntry(6).buffer(previous),
                gpu::BindGroupEntry(7).buffer(next),
                gpu::BindGroupEntry(8).buffer(input_.metadataBuffer),
                gpu::BindGroupEntry(9).buffer(telemetry_),
                gpu::BindGroupEntry(10).buffer(parameterBuffer_),
            };
            narrowGroup = gpu::createBindGroup(
                device_, narrowLayout_, entries,
                "narrow_phase_collision_bind_group");
            ++inputBindGroupCacheMisses_;
        }
        if (!cachedInputGroups_[0] || !cachedInputGroups_[1] || !narrowGroup) {
            releaseCachedInputGroups();
            return false;
        }
        return true;
    }

    bool encode(
        WGPUCommandEncoder encoder,
        const GpuNarrowPhase::ProfilingBoundary& profilingBoundary) {
        if (!encoder || !input_.valid()) return false;
        const auto writeProfilingBoundary = [&] {
            if (profilingBoundary.callback) {
                profilingBoundary.callback(profilingBoundary.userData);
            }
        };
        const Params params{
            .capacities = {input_.bodyCapacity, input_.pairCapacity,
                           config_.manifoldCapacity, config_.workgroupSize},
            .tolerances = {config_.linearSlop, config_.speculativeDistance,
                           config_.recycleDistance, 0.0f},
            .dispatch = {config_.dispatchContactCapacity, 0u, 0u, 0u},
        };
        gpu::writeBuffer(queue_, parameterBuffer_, 0, params);
        if (!ensureCachedInputGroups()) return false;
        const uint32_t parity = manifoldsAreB_ ? 1u : 0u;
        WGPUBindGroup bucketGroup = cachedInputGroups_[0];
        WGPUBindGroup finalizeGroup = cachedInputGroups_[1];
        WGPUBindGroup narrowGroup = cachedInputGroups_[2u + parity];

        WGPUComputePassDescriptor passDesc{};
        WGPUComputePassEncoder pass =
            wgpuCommandEncoderBeginComputePass(encoder, &passDesc);
        wgpuComputePassEncoderSetBindGroup(pass, 0, bucketGroup, 0, nullptr);
        const uint32_t groups =
            (input_.pairCapacity + config_.workgroupSize - 1u)
            / config_.workgroupSize;
        wgpuComputePassEncoderSetPipeline(pass, resetBucketsPipeline_);
        wgpuComputePassEncoderDispatchWorkgroups(pass, 1, 1, 1);
        wgpuComputePassEncoderSetPipeline(pass, countBucketsPipeline_);
        wgpuComputePassEncoderDispatchWorkgroups(pass, groups, 1, 1);
        wgpuComputePassEncoderSetPipeline(pass, finalizeBucketsPipeline_);
        wgpuComputePassEncoderDispatchWorkgroups(pass, 1, 1, 1);
        wgpuComputePassEncoderSetPipeline(pass, scatterBucketsPipeline_);
        wgpuComputePassEncoderDispatchWorkgroups(pass, groups, 1, 1);
        wgpuComputePassEncoderEnd(pass);
        wgpuComputePassEncoderRelease(pass);
        writeProfilingBoundary();

        pass = wgpuCommandEncoderBeginComputePass(encoder, &passDesc);
        wgpuComputePassEncoderSetBindGroup(pass, 0, narrowGroup, 0, nullptr);
        for (uint32_t index = 0u; index < classPipelines_.size(); ++index) {
            wgpuComputePassEncoderSetPipeline(pass, classPipelines_[index]);
            wgpuComputePassEncoderDispatchWorkgroupsIndirect(
                pass, classDispatchArgs_,
                uint64_t{index} * 4u * sizeof(uint32_t));
        }
        wgpuComputePassEncoderSetBindGroup(pass, 0, finalizeGroup, 0, nullptr);
        wgpuComputePassEncoderSetPipeline(pass, finalizePipeline_);
        wgpuComputePassEncoderDispatchWorkgroups(pass, 1, 1, 1);
        wgpuComputePassEncoderEnd(pass);
        wgpuComputePassEncoderRelease(pass);
        manifoldsAreB_ = !manifoldsAreB_;
        return true;
    }

    void shutdown() {
        releaseCachedInputGroups();
        releaseHandle(resetBucketsPipeline_, wgpuComputePipelineRelease);
        releaseHandle(countBucketsPipeline_, wgpuComputePipelineRelease);
        releaseHandle(finalizeBucketsPipeline_, wgpuComputePipelineRelease);
        releaseHandle(scatterBucketsPipeline_, wgpuComputePipelineRelease);
        for (auto& pipeline : classPipelines_) {
            releaseHandle(pipeline, wgpuComputePipelineRelease);
        }
        releaseHandle(finalizePipeline_, wgpuComputePipelineRelease);
        releaseHandle(bucketPipelineLayout_, wgpuPipelineLayoutRelease);
        releaseHandle(narrowPipelineLayout_, wgpuPipelineLayoutRelease);
        releaseHandle(finalizePipelineLayout_, wgpuPipelineLayoutRelease);
        releaseHandle(bucketLayout_, wgpuBindGroupLayoutRelease);
        releaseHandle(narrowLayout_, wgpuBindGroupLayoutRelease);
        releaseHandle(finalizeLayout_, wgpuBindGroupLayoutRelease);
        releaseHandle(shaderModule_, wgpuShaderModuleRelease);
        releaseBuffer(parameterBuffer_);
        releaseBuffer(bucketedPairs_);
        releaseBuffer(classTable_);
        releaseBuffer(classDispatchArgs_);
        releaseBuffer(manifoldsA_);
        releaseBuffer(manifoldsB_);
        releaseBuffer(telemetry_);
        device_ = nullptr;
        queue_ = nullptr;
        config_ = {};
        input_ = {};
        manifoldsAreB_ = false;
        inputBindGroupCacheMisses_ = 0;
        scratchBytes_ = 0;
    }

    WGPUDevice device_ = nullptr;
    WGPUQueue queue_ = nullptr;
    Config config_{};
    GpuNarrowPhaseInput input_{};
    size_t scratchBytes_ = 0;
    size_t inputBindGroupCacheMisses_ = 0;
    bool manifoldsAreB_ = false;
    std::array<WGPUBindGroup, 4> cachedInputGroups_{};
    WGPUBuffer parameterBuffer_ = nullptr;
    WGPUBuffer bucketedPairs_ = nullptr;
    WGPUBuffer classTable_ = nullptr;
    WGPUBuffer classDispatchArgs_ = nullptr;
    WGPUBuffer manifoldsA_ = nullptr;
    WGPUBuffer manifoldsB_ = nullptr;
    WGPUBuffer telemetry_ = nullptr;
    WGPUShaderModule shaderModule_ = nullptr;
    WGPUBindGroupLayout bucketLayout_ = nullptr;
    WGPUBindGroupLayout narrowLayout_ = nullptr;
    WGPUBindGroupLayout finalizeLayout_ = nullptr;
    WGPUPipelineLayout bucketPipelineLayout_ = nullptr;
    WGPUPipelineLayout narrowPipelineLayout_ = nullptr;
    WGPUPipelineLayout finalizePipelineLayout_ = nullptr;
    WGPUComputePipeline resetBucketsPipeline_ = nullptr;
    WGPUComputePipeline countBucketsPipeline_ = nullptr;
    WGPUComputePipeline finalizeBucketsPipeline_ = nullptr;
    WGPUComputePipeline scatterBucketsPipeline_ = nullptr;
    std::array<WGPUComputePipeline, kGpuNarrowPhasePairClassCount>
        classPipelines_{};
    WGPUComputePipeline finalizePipeline_ = nullptr;
};

GpuNarrowPhase::GpuNarrowPhase() : impl_(std::make_unique<Impl>()) {}
GpuNarrowPhase::~GpuNarrowPhase() = default;
GpuNarrowPhase::GpuNarrowPhase(GpuNarrowPhase&&) noexcept = default;
GpuNarrowPhase& GpuNarrowPhase::operator=(GpuNarrowPhase&&) noexcept = default;

bool GpuNarrowPhase::initialize(WGPUDevice device, WGPUQueue queue,
                                const Config& config) {
    return impl_->initialize(device, queue, config);
}
void GpuNarrowPhase::shutdown() { impl_->shutdown(); }
void GpuNarrowPhase::setInput(const GpuNarrowPhaseInput& input) {
    impl_->setInput(input);
}
bool GpuNarrowPhase::encode(WGPUCommandEncoder encoder) {
    return impl_->encode(encoder, ProfilingBoundary{});
}
bool GpuNarrowPhase::encode(
    WGPUCommandEncoder encoder,
    const ProfilingBoundary& profilingBoundary) {
    return impl_->encode(encoder, profilingBoundary);
}
WGPUBuffer GpuNarrowPhase::manifolds() const noexcept {
    return impl_->manifoldsAreB_ ? impl_->manifoldsB_ : impl_->manifoldsA_;
}
WGPUBuffer GpuNarrowPhase::pairBuckets() const noexcept {
    return impl_->bucketedPairs_;
}
WGPUBuffer GpuNarrowPhase::pairClassTable() const noexcept {
    return impl_->classTable_;
}
WGPUBuffer GpuNarrowPhase::activeContactDispatchBuffer() const noexcept {
    return impl_->classDispatchArgs_;
}
WGPUBuffer GpuNarrowPhase::telemetryBuffer() const noexcept {
    return impl_->telemetry_;
}
uint32_t GpuNarrowPhase::capacity() const noexcept {
    return impl_->config_.manifoldCapacity;
}
size_t GpuNarrowPhase::scratchBytes() const noexcept {
    return impl_->scratchBytes_;
}
size_t GpuNarrowPhase::inputBindGroupCacheMisses() const noexcept {
    return impl_->inputBindGroupCacheMisses_;
}

GpuNarrowPhaseTelemetry GpuNarrowPhase::decodeTelemetry(
    std::span<const uint32_t> words) noexcept {
    GpuNarrowPhaseTelemetry result;
    if (words.size() < 24) return result;
    for (uint32_t index = 0; index < result.pairClasses.size(); ++index) {
        result.pairClasses[index] = words[index];
    }
    result.inputPairs = words[10];
    result.manifolds = words[11];
    result.manifoldPoints = words[12];
    result.speculativeManifolds = words[13];
    result.matchedFeaturePoints = words[14];
    result.recycledAnchorPoints = words[15];
    result.invalidManifolds = words[16];
    result.pairOverflow = words[17] != 0u;
    result.highInputPairs = words[18];
    result.highManifolds = words[19];
    result.highManifoldPoints = words[20];
    result.tick = words[21];
    return result;
}

} // namespace voxy::physics
