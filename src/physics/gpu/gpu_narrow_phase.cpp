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
                                 const char* label, int authoredPass = -1) {
    WGPUComputePipelineDescriptor desc{};
    WGPU_SET_LABEL(desc, label);
    desc.layout = layout;
    desc.compute.module = shader;
    WGPU_SET_ENTRY_POINT(desc.compute, entryPoint.c_str());
    WGPUConstantEntry authored{};
    if (authoredPass >= 0) {
        authored.key = gpu::toStringView("AUTHORED_PAIR_PASS");
        authored.value = static_cast<double>(authoredPass);
        desc.compute.constantCount = 1;
        desc.compute.constants = &authored;
    }
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
            || config.normalPatchesPerPair == 0 || config.normalPatchesPerPair > 8
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
        if (uint64_t{config_.manifoldCapacity} * config_.normalPatchesPerPair > UINT32_MAX) {
            shutdown(); return false;
        }
        config_.manifoldCapacity *= config_.normalPatchesPerPair;
        if (config_.dispatchContactCapacity == 0u) {
            config_.dispatchContactCapacity = config_.manifoldCapacity;
        }
        config_.dispatchContactCapacity = std::min(
            config_.dispatchContactCapacity, config_.manifoldCapacity);

        rawScanCapacity_ = config_.normalPatchesPerPair == 1
            ? config_.dispatchContactCapacity : config_.manifoldCapacity;

        WGPULimits deviceLimits{};
        const uint64_t historyBytes = uint64_t{config_.manifoldCapacity}
            * sizeof(GpuContactManifold);
        if (!gpu::getDeviceLimits(device_, deviceLimits)
            || historyBytes > deviceLimits.maxStorageBufferBindingSize
            || historyBytes > deviceLimits.maxBufferSize
            || (uint64_t{rawScanCapacity_} + config_.workgroupSize - 1u)
                    / config_.workgroupSize > deviceLimits.maxComputeWorkgroupsPerDimension) {
            shutdown(); return false;
        }

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
        // Collision dispatch only reads the class counts/offsets. A GPU copy
        // to a uniform frees a storage binding for the compound atlas while
        // staying within WebGPU's guaranteed eight-storage-buffer limit.
        collisionClasses_ = gpu::createBuffer(device_, gpu::BufferDesc{
            .label = "narrow_phase_collision_classes",
            .size = kClassTableWords * sizeof(uint32_t),
            .usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst,
        });
        const std::array<uint32_t, 8> emptyShapeHeap{};
        emptyShapeHeap_ = gpu::createBufferWithData(device_, queue_,
            gpu::BufferDesc::storage(sizeof(emptyShapeHeap), false,
                                    "narrow_phase_empty_shape_heap"),
            std::span<const uint32_t>(emptyShapeHeap));
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
        activePredicates_ = makeStorage(
            uint64_t{rawScanCapacity_} * sizeof(uint32_t),
            "narrow_phase_active_predicates");
        activeOffsets_ = makeStorage(
            uint64_t{rawScanCapacity_} * sizeof(uint32_t),
            "narrow_phase_active_offsets");
        if (!parameterBuffer_ || !bucketedPairs_ || !classTable_
            || !classDispatchArgs_
            || !manifoldsA_ || !manifoldsB_ || !telemetry_
            || !activePredicates_ || !activeOffsets_ || !collisionClasses_
            || !emptyShapeHeap_) {
            shutdown();
            return false;
        }
        const std::array<uint32_t, kTelemetryWords> zeroTelemetry{};
        if (!gpu::writeBuffer(queue_, telemetry_, 0, zeroTelemetry)) {
            shutdown();
            return false;
        }

        DeterministicGpuPrimitives::Config primitiveConfig;
        primitiveConfig.capacity = rawScanCapacity_;
        primitiveConfig.workgroupSize = config_.workgroupSize;
        primitiveConfig.shaderPath = config_.primitivesShaderPath;
        primitiveConfig.shaderSources = config_.shaderSources;
        if (!primitives_.initialize(device_, queue_, primitiveConfig)) {
            shutdown();
            return false;
        }
        scratchBytes_ = gpu::saturatingSize(
            primitives_.scratchBytes() + sizeof(Params)
            + uint64_t{config.pairCapacity} * sizeof(GpuKeyValue)
            + kClassTableWords * sizeof(uint32_t)
            + uint64_t{kGpuNarrowPhasePairClassCount + 1u} * 4u
                * sizeof(uint32_t)
            + uint64_t{config_.manifoldCapacity} * 2u
                * sizeof(GpuContactManifold)
            + uint64_t{rawScanCapacity_} * 2u
                * sizeof(uint32_t)
            + kTelemetryWords * sizeof(uint32_t)
            + kClassTableWords * sizeof(uint32_t) + sizeof(emptyShapeHeap));

        shaderModule_ = gpu::loadShaderModule(
            device_, config.shaderPath, "physics_narrow_phase.wgsl",
            config.shaderSources);
        if (!shaderModule_ || !createPipelines()
            || !createCompactionGroups()) {
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

        std::vector<LE> countEntries;
        storage(countEntries, 0, true);
        storage(countEntries, 1, true);
        storage(countEntries, 2, true);
        storage(countEntries, 3, true);
        storage(countEntries, 5, false);
        storage(countEntries, 7, false);
        storage(countEntries, 8, true);
        storage(countEntries, 9, false);
        uniform(countEntries);
        countLayout_ = gpu::createBindGroupLayout(
            device_, countEntries, "narrow_phase_count_layout");

        std::vector<LE> scatterEntries;
        storage(scatterEntries, 0, true);
        storage(scatterEntries, 1, true);
        storage(scatterEntries, 2, true);
        storage(scatterEntries, 3, true);
        storage(scatterEntries, 4, false);
        storage(scatterEntries, 5, false);
        storage(scatterEntries, 8, true);
        uniform(scatterEntries);
        scatterLayout_ = gpu::createBindGroupLayout(
            device_, scatterEntries, "narrow_phase_scatter_layout");

        std::vector<LE> narrowEntries;
        storage(narrowEntries, 0, true);
        storage(narrowEntries, 1, true);
        storage(narrowEntries, 4, false);
        storage(narrowEntries, 6, true);
        storage(narrowEntries, 7, false);
        storage(narrowEntries, 8, true);
        storage(narrowEntries, 9, false);
        storage(narrowEntries, 15, true);
        narrowEntries.emplace_back(16).computeVisible().uniformBuffer(
            false, kClassTableWords * sizeof(uint32_t));
        uniform(narrowEntries);
        narrowLayout_ = gpu::createBindGroupLayout(
            device_, narrowEntries, "narrow_phase_collision_layout");

        std::vector<LE> finalizeEntries;
        storage(finalizeEntries, 3, true);
        storage(finalizeEntries, 9, false);
        uniform(finalizeEntries);
        finalizeLayout_ = gpu::createBindGroupLayout(
            device_, finalizeEntries, "narrow_phase_finalize_layout");

        std::vector<LE> compactEntries;
        storage(compactEntries, 7, false);
        storage(compactEntries, 9, false);
        storage(compactEntries, 11, false);
        storage(compactEntries, 12, false);
        storage(compactEntries, 13, false);
        storage(compactEntries, 14, true);
        uniform(compactEntries);
        compactLayout_ = gpu::createBindGroupLayout(
            device_, compactEntries, "narrow_phase_compact_layout");
        if (!bucketLayout_ || !countLayout_ || !scatterLayout_
            || !narrowLayout_ || !finalizeLayout_ || !compactLayout_) {
            return false;
        }

        bucketPipelineLayout_ = gpu::createPipelineLayout(
            device_, std::array{bucketLayout_},
            "narrow_phase_bucket_pipeline_layout");
        countPipelineLayout_ = gpu::createPipelineLayout(
            device_, std::array{countLayout_},
            "narrow_phase_count_pipeline_layout");
        scatterPipelineLayout_ = gpu::createPipelineLayout(
            device_, std::array{scatterLayout_},
            "narrow_phase_scatter_pipeline_layout");
        narrowPipelineLayout_ = gpu::createPipelineLayout(
            device_, std::array{narrowLayout_},
            "narrow_phase_collision_pipeline_layout");
        finalizePipelineLayout_ = gpu::createPipelineLayout(
            device_, std::array{finalizeLayout_},
            "narrow_phase_finalize_pipeline_layout");
        compactPipelineLayout_ = gpu::createPipelineLayout(
            device_, std::array{compactLayout_},
            "narrow_phase_compact_pipeline_layout");
        if (!bucketPipelineLayout_ || !countPipelineLayout_
            || !scatterPipelineLayout_ || !narrowPipelineLayout_
            || !finalizePipelineLayout_ || !compactPipelineLayout_) {
            return false;
        }

        const std::string suffix = std::to_string(config_.workgroupSize);
        resetBucketsPipeline_ = makePipeline(
            device_, bucketPipelineLayout_, shaderModule_,
            "reset_pair_buckets", "narrow_phase_reset_buckets");
        countBucketsPipeline_ = makePipeline(
            device_, countPipelineLayout_, shaderModule_,
            "count_pair_classes_" + suffix,
            "narrow_phase_count_buckets");
        finalizeBucketsPipeline_ = makePipeline(
            device_, bucketPipelineLayout_, shaderModule_,
            "finalize_pair_buckets", "narrow_phase_finalize_buckets");
        scatterBucketsPipeline_ = makePipeline(
            device_, scatterPipelineLayout_, shaderModule_,
            "scatter_pair_classes_" + suffix,
            "narrow_phase_scatter_buckets");
        finalizePipeline_ = makePipeline(
            device_, finalizePipelineLayout_, shaderModule_, "finalize_narrow",
            "narrow_phase_finalize");
        markActivePipeline_ = makePipeline(
            device_, compactPipelineLayout_, shaderModule_,
            "mark_active_manifolds_" + suffix,
            "narrow_phase_mark_active_manifolds");
        scatterActivePipeline_ = makePipeline(
            device_, compactPipelineLayout_, shaderModule_,
            "scatter_active_manifolds_" + suffix,
            "narrow_phase_scatter_active_manifolds");
        finalizeActivePipeline_ = makePipeline(
            device_, compactPipelineLayout_, shaderModule_,
            "finalize_active_manifolds",
            "narrow_phase_finalize_active_manifolds");
        commitActivePipeline_ = makePipeline(
            device_, compactPipelineLayout_, shaderModule_,
            "commit_active_manifolds_" + suffix,
            "narrow_phase_commit_active_manifolds");
        constexpr std::array<const char*, kGpuNarrowPhasePairClassCount> names = {
            "sphere_sphere", "sphere_capsule", "capsule_capsule",
            "sphere_box", "capsule_box", "box_box", "sphere_cylinder",
            "capsule_cylinder", "box_cylinder", "cylinder_cylinder"};
        for (uint32_t index = 0; index < names.size(); ++index) {
            const bool canContainAuthored = index == 3u || index == 4u
                || index == 5u || index == 8u;
            classPipelines_[index] = makePipeline(
                device_, narrowPipelineLayout_, shaderModule_,
                std::string("narrow_") + names[index] + "_" + suffix,
                "narrow_phase_pair_class", canContainAuthored ? 0 : -1);
            if (canContainAuthored) {
                authoredClassPipelines_[index] = makePipeline(
                    device_, narrowPipelineLayout_, shaderModule_,
                    std::string("narrow_") + names[index] + "_" + suffix,
                    "narrow_phase_authored_pair_class", 1);
                if (!authoredClassPipelines_[index]) return false;
            }
        }
        if (!resetBucketsPipeline_ || !countBucketsPipeline_
            || !finalizeBucketsPipeline_ || !scatterBucketsPipeline_
            || !finalizePipeline_ || !markActivePipeline_
            || !scatterActivePipeline_ || !finalizeActivePipeline_
            || !commitActivePipeline_) return false;
        for (WGPUComputePipeline pipeline : classPipelines_) {
            if (!pipeline) return false;
        }
        return true;
    }

    bool createCompactionGroups() {
        for (uint32_t parity = 0u; parity < compactGroups_.size(); ++parity) {
            WGPUBuffer current = parity == 0u ? manifoldsB_ : manifoldsA_;
            WGPUBuffer active = parity == 0u ? manifoldsA_ : manifoldsB_;
            const std::array<gpu::BindGroupEntry, 7> entries = {
                gpu::BindGroupEntry(7).buffer(current),
                gpu::BindGroupEntry(9).buffer(telemetry_),
                gpu::BindGroupEntry(10).buffer(parameterBuffer_),
                gpu::BindGroupEntry(11).buffer(classDispatchArgs_),
                gpu::BindGroupEntry(12).buffer(active),
                gpu::BindGroupEntry(13).buffer(activePredicates_),
                gpu::BindGroupEntry(14).buffer(activeOffsets_),
            };
            compactGroups_[parity] = gpu::createBindGroup(
                device_, compactLayout_, entries,
                "narrow_phase_compact_bind_group");
            if (!compactGroups_[parity]) return false;
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
            && lhs.metadataBuffer == rhs.metadataBuffer
            && lhs.authoredShapeBuffer == rhs.authoredShapeBuffer;
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
        const uint32_t parity = manifoldsAreB_ ? 1u : 0u;
        WGPUBuffer previous = manifoldsAreB_ ? manifoldsB_ : manifoldsA_;
        WGPUBuffer next = manifoldsAreB_ ? manifoldsA_ : manifoldsB_;
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

        WGPUBindGroup& narrowGroup = cachedInputGroups_[2u + parity];
        if (!narrowGroup) {
            const std::array<gpu::BindGroupEntry, 10> entries = {
                gpu::BindGroupEntry(0).buffer(input_.poseBuffer),
                gpu::BindGroupEntry(1).buffer(input_.shapeBuffer),
                gpu::BindGroupEntry(4).buffer(bucketedPairs_),
                gpu::BindGroupEntry(6).buffer(previous),
                gpu::BindGroupEntry(7).buffer(next),
                gpu::BindGroupEntry(8).buffer(input_.metadataBuffer),
                gpu::BindGroupEntry(9).buffer(telemetry_),
                gpu::BindGroupEntry(10).buffer(parameterBuffer_),
                gpu::BindGroupEntry(15).buffer(input_.authoredShapeBuffer
                    ? input_.authoredShapeBuffer : emptyShapeHeap_),
                gpu::BindGroupEntry(16).buffer(collisionClasses_),
            };
            narrowGroup = gpu::createBindGroup(
                device_, narrowLayout_, entries,
                "narrow_phase_collision_bind_group");
            ++inputBindGroupCacheMisses_;
        }
        WGPUBindGroup& countGroup = cachedInputGroups_[4u + parity];
        if (!countGroup) {
            const std::array<gpu::BindGroupEntry, 9> entries = {
                gpu::BindGroupEntry(0).buffer(input_.poseBuffer),
                gpu::BindGroupEntry(1).buffer(input_.shapeBuffer),
                gpu::BindGroupEntry(2).buffer(input_.uniquePairBuffer),
                gpu::BindGroupEntry(3).buffer(
                    input_.broadPhaseTelemetryBuffer),
                gpu::BindGroupEntry(5).buffer(classTable_),
                gpu::BindGroupEntry(7).buffer(next),
                gpu::BindGroupEntry(8).buffer(input_.metadataBuffer),
                gpu::BindGroupEntry(9).buffer(telemetry_),
                gpu::BindGroupEntry(10).buffer(parameterBuffer_),
            };
            countGroup = gpu::createBindGroup(
                device_, countLayout_, entries,
                "narrow_phase_count_bind_group");
            ++inputBindGroupCacheMisses_;
        }
        if (!cachedInputGroups_[6]) {
            const std::array<gpu::BindGroupEntry, 8> entries = {
                gpu::BindGroupEntry(0).buffer(input_.poseBuffer),
                gpu::BindGroupEntry(1).buffer(input_.shapeBuffer),
                gpu::BindGroupEntry(2).buffer(input_.uniquePairBuffer),
                gpu::BindGroupEntry(3).buffer(
                    input_.broadPhaseTelemetryBuffer),
                gpu::BindGroupEntry(4).buffer(bucketedPairs_),
                gpu::BindGroupEntry(5).buffer(classTable_),
                gpu::BindGroupEntry(8).buffer(input_.metadataBuffer),
                gpu::BindGroupEntry(10).buffer(parameterBuffer_),
            };
            cachedInputGroups_[6] = gpu::createBindGroup(
                device_, scatterLayout_, entries,
                "narrow_phase_scatter_bind_group");
            ++inputBindGroupCacheMisses_;
        }
        if (!cachedInputGroups_[0] || !cachedInputGroups_[1]
            || !narrowGroup || !countGroup || !cachedInputGroups_[6]) {
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
            .dispatch = {rawScanCapacity_, config_.normalPatchesPerPair, config_.dispatchContactCapacity, 0u},
        };
        if (!gpu::writeBuffer(queue_, parameterBuffer_, 0, params))
            return false;
        if (!ensureCachedInputGroups()) return false;
        const uint32_t parity = manifoldsAreB_ ? 1u : 0u;
        WGPUBindGroup bucketGroup = cachedInputGroups_[0];
        WGPUBindGroup finalizeGroup = cachedInputGroups_[1];
        WGPUBindGroup narrowGroup = cachedInputGroups_[2u + parity];
        WGPUBindGroup countGroup = cachedInputGroups_[4u + parity];
        WGPUBindGroup scatterGroup = cachedInputGroups_[6];

        WGPUComputePassDescriptor passDesc{};
        WGPUComputePassEncoder pass =
            wgpuCommandEncoderBeginComputePass(encoder, &passDesc);
        if (!pass) return false;
        wgpuComputePassEncoderSetBindGroup(pass, 0, bucketGroup, 0, nullptr);
        const uint32_t groups =
            (input_.pairCapacity + config_.workgroupSize - 1u)
            / config_.workgroupSize;
        wgpuComputePassEncoderSetPipeline(pass, resetBucketsPipeline_);
        wgpuComputePassEncoderDispatchWorkgroups(pass, 1, 1, 1);
        wgpuComputePassEncoderSetPipeline(pass, countBucketsPipeline_);
        wgpuComputePassEncoderSetBindGroup(
            pass, 0, countGroup, 0, nullptr);
        wgpuComputePassEncoderDispatchWorkgroups(pass, groups, 1, 1);
        wgpuComputePassEncoderSetPipeline(pass, finalizeBucketsPipeline_);
        wgpuComputePassEncoderSetBindGroup(pass, 0, bucketGroup, 0, nullptr);
        wgpuComputePassEncoderDispatchWorkgroups(pass, 1, 1, 1);
        wgpuComputePassEncoderSetPipeline(pass, scatterBucketsPipeline_);
        wgpuComputePassEncoderSetBindGroup(
            pass, 0, scatterGroup, 0, nullptr);
        wgpuComputePassEncoderDispatchWorkgroups(pass, groups, 1, 1);
        wgpuComputePassEncoderEnd(pass);
        wgpuComputePassEncoderRelease(pass);
        writeProfilingBoundary();

        wgpuCommandEncoderCopyBufferToBuffer(encoder, classTable_, 0,
            collisionClasses_, 0, kClassTableWords * sizeof(uint32_t));
        pass = wgpuCommandEncoderBeginComputePass(encoder, &passDesc);
        if (!pass) return false;
        wgpuComputePassEncoderSetBindGroup(pass, 0, narrowGroup, 0, nullptr);
        for (uint32_t index = 0u; index < classPipelines_.size(); ++index) {
            wgpuComputePassEncoderSetPipeline(pass, classPipelines_[index]);
            wgpuComputePassEncoderDispatchWorkgroupsIndirect(
                pass, classDispatchArgs_,
                uint64_t{index} * 4u * sizeof(uint32_t));
            if (authoredClassPipelines_[index]) {
                wgpuComputePassEncoderSetPipeline(pass, authoredClassPipelines_[index]);
                wgpuComputePassEncoderDispatchWorkgroupsIndirect(
                    pass, classDispatchArgs_,
                    uint64_t{index} * 4u * sizeof(uint32_t));
            }
        }
        wgpuComputePassEncoderSetBindGroup(pass, 0, finalizeGroup, 0, nullptr);
        wgpuComputePassEncoderSetPipeline(pass, finalizePipeline_);
        wgpuComputePassEncoderDispatchWorkgroups(pass, 1, 1, 1);
        wgpuComputePassEncoderEnd(pass);
        wgpuComputePassEncoderRelease(pass);

        WGPUBindGroup compactGroup = compactGroups_[parity];
        const uint32_t compactGroups =
            (rawScanCapacity_ + config_.workgroupSize - 1u)
            / config_.workgroupSize;
        pass = wgpuCommandEncoderBeginComputePass(encoder, &passDesc);
        if (!pass) return false;
        wgpuComputePassEncoderSetBindGroup(
            pass, 0, compactGroup, 0, nullptr);
        wgpuComputePassEncoderSetPipeline(pass, markActivePipeline_);
        wgpuComputePassEncoderDispatchWorkgroups(
            pass, compactGroups, 1, 1);
        wgpuComputePassEncoderEnd(pass);
        wgpuComputePassEncoderRelease(pass);
        if (!primitives_.encodeScanU32(
                encoder, activePredicates_, activeOffsets_,
                rawScanCapacity_)) {
            return false;
        }
        pass = wgpuCommandEncoderBeginComputePass(encoder, &passDesc);
        if (!pass) return false;
        wgpuComputePassEncoderSetBindGroup(
            pass, 0, compactGroup, 0, nullptr);
        wgpuComputePassEncoderSetPipeline(pass, scatterActivePipeline_);
        wgpuComputePassEncoderDispatchWorkgroups(
            pass, compactGroups, 1, 1);
        wgpuComputePassEncoderSetPipeline(pass, finalizeActivePipeline_);
        wgpuComputePassEncoderDispatchWorkgroups(pass, 1, 1, 1);
        wgpuComputePassEncoderEnd(pass);
        wgpuComputePassEncoderRelease(pass);
        manifoldsAreB_ = !manifoldsAreB_;
        return true;
    }

    bool encodeCommitActiveManifolds(WGPUCommandEncoder encoder) {
        if (!encoder || !input_.valid()) return false;
        const uint32_t compactParity = manifoldsAreB_ ? 0u : 1u;
        WGPUComputePassDescriptor passDesc{};
        WGPUComputePassEncoder pass =
            wgpuCommandEncoderBeginComputePass(encoder, &passDesc);
        if (!pass) return false;
        wgpuComputePassEncoderSetBindGroup(
            pass, 0, compactGroups_[compactParity], 0, nullptr);
        wgpuComputePassEncoderSetPipeline(pass, commitActivePipeline_);
        wgpuComputePassEncoderDispatchWorkgroups(
            pass,
            (config_.dispatchContactCapacity + config_.workgroupSize - 1u)
                / config_.workgroupSize,
            1, 1);
        wgpuComputePassEncoderEnd(pass);
        wgpuComputePassEncoderRelease(pass);
        return true;
    }

    void shutdown() {
        releaseCachedInputGroups();
        for (WGPUBindGroup& group : compactGroups_) {
            releaseHandle(group, wgpuBindGroupRelease);
        }
        releaseHandle(resetBucketsPipeline_, wgpuComputePipelineRelease);
        releaseHandle(countBucketsPipeline_, wgpuComputePipelineRelease);
        releaseHandle(finalizeBucketsPipeline_, wgpuComputePipelineRelease);
        releaseHandle(scatterBucketsPipeline_, wgpuComputePipelineRelease);
        for (auto& pipeline : classPipelines_) {
            releaseHandle(pipeline, wgpuComputePipelineRelease);
        }
        for (auto& pipeline : authoredClassPipelines_) {
            releaseHandle(pipeline, wgpuComputePipelineRelease);
        }
        releaseHandle(finalizePipeline_, wgpuComputePipelineRelease);
        releaseHandle(markActivePipeline_, wgpuComputePipelineRelease);
        releaseHandle(scatterActivePipeline_, wgpuComputePipelineRelease);
        releaseHandle(finalizeActivePipeline_, wgpuComputePipelineRelease);
        releaseHandle(commitActivePipeline_, wgpuComputePipelineRelease);
        releaseHandle(bucketPipelineLayout_, wgpuPipelineLayoutRelease);
        releaseHandle(countPipelineLayout_, wgpuPipelineLayoutRelease);
        releaseHandle(scatterPipelineLayout_, wgpuPipelineLayoutRelease);
        releaseHandle(narrowPipelineLayout_, wgpuPipelineLayoutRelease);
        releaseHandle(finalizePipelineLayout_, wgpuPipelineLayoutRelease);
        releaseHandle(compactPipelineLayout_, wgpuPipelineLayoutRelease);
        releaseHandle(bucketLayout_, wgpuBindGroupLayoutRelease);
        releaseHandle(countLayout_, wgpuBindGroupLayoutRelease);
        releaseHandle(scatterLayout_, wgpuBindGroupLayoutRelease);
        releaseHandle(narrowLayout_, wgpuBindGroupLayoutRelease);
        releaseHandle(finalizeLayout_, wgpuBindGroupLayoutRelease);
        releaseHandle(compactLayout_, wgpuBindGroupLayoutRelease);
        releaseHandle(shaderModule_, wgpuShaderModuleRelease);
        primitives_.shutdown();
        releaseBuffer(parameterBuffer_);
        releaseBuffer(bucketedPairs_);
        releaseBuffer(classTable_);
        releaseBuffer(collisionClasses_);
        releaseBuffer(emptyShapeHeap_);
        releaseBuffer(classDispatchArgs_);
        releaseBuffer(manifoldsA_);
        releaseBuffer(manifoldsB_);
        releaseBuffer(telemetry_);
        releaseBuffer(activePredicates_);
        releaseBuffer(activeOffsets_);
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
    uint32_t rawScanCapacity_ = 0;
    GpuNarrowPhaseInput input_{};
    size_t scratchBytes_ = 0;
    size_t inputBindGroupCacheMisses_ = 0;
    bool manifoldsAreB_ = false;
    std::array<WGPUBindGroup, 7> cachedInputGroups_{};
    std::array<WGPUBindGroup, 2> compactGroups_{};
    DeterministicGpuPrimitives primitives_;
    WGPUBuffer parameterBuffer_ = nullptr;
    WGPUBuffer bucketedPairs_ = nullptr;
    WGPUBuffer classTable_ = nullptr;
    WGPUBuffer collisionClasses_ = nullptr;
    WGPUBuffer emptyShapeHeap_ = nullptr;
    WGPUBuffer classDispatchArgs_ = nullptr;
    WGPUBuffer manifoldsA_ = nullptr;
    WGPUBuffer manifoldsB_ = nullptr;
    WGPUBuffer telemetry_ = nullptr;
    WGPUBuffer activePredicates_ = nullptr;
    WGPUBuffer activeOffsets_ = nullptr;
    WGPUShaderModule shaderModule_ = nullptr;
    WGPUBindGroupLayout bucketLayout_ = nullptr;
    WGPUBindGroupLayout countLayout_ = nullptr;
    WGPUBindGroupLayout scatterLayout_ = nullptr;
    WGPUBindGroupLayout narrowLayout_ = nullptr;
    WGPUBindGroupLayout finalizeLayout_ = nullptr;
    WGPUBindGroupLayout compactLayout_ = nullptr;
    WGPUPipelineLayout bucketPipelineLayout_ = nullptr;
    WGPUPipelineLayout countPipelineLayout_ = nullptr;
    WGPUPipelineLayout scatterPipelineLayout_ = nullptr;
    WGPUPipelineLayout narrowPipelineLayout_ = nullptr;
    WGPUPipelineLayout finalizePipelineLayout_ = nullptr;
    WGPUPipelineLayout compactPipelineLayout_ = nullptr;
    WGPUComputePipeline resetBucketsPipeline_ = nullptr;
    WGPUComputePipeline countBucketsPipeline_ = nullptr;
    WGPUComputePipeline finalizeBucketsPipeline_ = nullptr;
    WGPUComputePipeline scatterBucketsPipeline_ = nullptr;
    std::array<WGPUComputePipeline, kGpuNarrowPhasePairClassCount>
        classPipelines_{};
    std::array<WGPUComputePipeline, kGpuNarrowPhasePairClassCount>
        authoredClassPipelines_{};
    WGPUComputePipeline finalizePipeline_ = nullptr;
    WGPUComputePipeline markActivePipeline_ = nullptr;
    WGPUComputePipeline scatterActivePipeline_ = nullptr;
    WGPUComputePipeline finalizeActivePipeline_ = nullptr;
    WGPUComputePipeline commitActivePipeline_ = nullptr;
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
bool GpuNarrowPhase::encodeCommitActiveManifolds(
    WGPUCommandEncoder encoder) {
    return impl_->encodeCommitActiveManifolds(encoder);
}
WGPUBuffer GpuNarrowPhase::manifolds() const noexcept {
    return impl_->manifoldsAreB_ ? impl_->manifoldsB_ : impl_->manifoldsA_;
}
WGPUBuffer GpuNarrowPhase::activeManifolds() const noexcept {
    return impl_->manifoldsAreB_ ? impl_->manifoldsA_ : impl_->manifoldsB_;
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
    std::span<const uint32_t> words,
    std::span<const uint32_t> collisionPairClasses) noexcept {
    GpuNarrowPhaseTelemetry result;
    if (words.size() < kTelemetryWords) return result;
    for (uint32_t index = 0; index < result.pairClasses.size(); ++index) {
        result.pairClasses[index] = words[index];
        if (index < collisionPairClasses.size()) {
            result.collisionPairClasses[index] = collisionPairClasses[index];
        }
    }
    result.inputPairs = words[10];
    result.manifolds = words[11];
    result.manifoldPoints = words[12];
    result.speculativeManifolds = words[13];
    result.matchedFeaturePoints = words[14];
    result.recycledAnchorPoints = words[15];
    result.invalidManifolds = words[16];
    result.patchOverflow = words[25] != 0u;
    result.activeContactOverflow = words[26] != 0u;
    result.requiredPatchHighWater = words[28];
    result.pairOverflow = words[17] != 0u;
    result.highInputPairs = words[18];
    result.highManifolds = words[19];
    result.highManifoldPoints = words[20];
    result.tick = words[21];
    return result;
}

} // namespace voxy::physics
