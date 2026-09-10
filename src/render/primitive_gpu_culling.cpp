#include "render/primitive_gpu_culling.hpp"

#include "core/log.hpp"
#include "gpu/resources.hpp"
#include "physics/gpu/gpu_body_shape.hpp"
#include "render/frustum.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <limits>
#include <utility>
#include <vector>

namespace voxy::render {
namespace {

constexpr uint32_t kWorkgroupSize = 256;
constexpr uint32_t kStorageOffsetAlignmentWords = 64;
constexpr int32_t kMaximumRenderSectorDelta = 4096;
constexpr uint64_t kPoseStride = 32u;
constexpr uint64_t kShapeStride = sizeof(physics::GpuBodyShape);
constexpr uint64_t kMetadataStride = 16u;

struct alignas(16) CullUniforms {
    std::array<glm::vec4, 6> planes{};
    glm::uvec4 counts{};
    glm::ivec4 cameraSector{};
    glm::vec4 interpolation{1.0f, 0.0f, 0.0f, 0.0f};
};

static_assert(sizeof(CullUniforms) == 144);

struct IndirectDrawArgs {
    uint32_t indexCount = 0;
    uint32_t instanceCount = 0;
    uint32_t firstIndex = 0;
    int32_t baseVertex = 0;
    uint32_t firstInstance = 0;
};

static_assert(sizeof(IndirectDrawArgs) == 20);

WGPUComputePipeline createPipeline(WGPUDevice device, WGPUPipelineLayout layout,
                                   WGPUShaderModule module,
                                   const char* entryPoint,
                                   const char* label) {
    WGPUComputePipelineDescriptor desc{};
    WGPU_SET_LABEL(desc, label);
    desc.layout = layout;
    desc.compute.module = module;
    WGPU_SET_ENTRY_POINT(desc.compute, entryPoint);
    return wgpuDeviceCreateComputePipeline(device, &desc);
}

template <typename T>
void releaseHandle(T& handle, void (*release)(T)) {
    if (!handle) return;
    release(handle);
    handle = nullptr;
}

void destroyBuffer(WGPUBuffer& buffer) {
    if (!buffer) return;
    wgpuBufferDestroy(buffer);
    wgpuBufferRelease(buffer);
    buffer = nullptr;
}

bool validStorageBuffer(
    WGPUBuffer buffer, uint64_t requiredSize) noexcept {
    return buffer
        && wgpuBufferGetSize(buffer) >= requiredSize
        && (wgpuBufferGetUsage(buffer) & WGPUBufferUsage_Storage) != 0;
}

bool emptyBodyView(const physics::PhysicsRenderView& view) noexcept {
    return !view.poseBuffer && !view.shapeBuffer && !view.metadataBuffer
        && !view.previousPoseBuffer && !view.previousMetadataBuffer
        && !view.activeBodyIds && !view.visibleBodyIds
        && !view.perShapeRanges && !view.indirectDrawArgs
        && view.residentBodyCapacity == 0u && view.shapeCount == 0u;
}

} // namespace

PrimitiveGpuCulling::~PrimitiveGpuCulling() { shutdown(); }

bool PrimitiveGpuCulling::initialize(
    WGPUDevice device, WGPUQueue queue, const std::filesystem::path& shaderPath,
    const std::array<PrimitiveDrawGeometry, kShapeCount>& geometry) {
    if (!device || !queue
        || std::any_of(
            geometry.begin(), geometry.end(),
            [](const PrimitiveDrawGeometry& draw) {
                return draw.indexCount == 0u
                    || draw.indexCount - 1u
                        > std::numeric_limits<uint32_t>::max()
                            - draw.firstIndex;
            })) {
        return false;
    }
    PrimitiveGpuCulling replacement;
    if (!replacement.initializeFresh(
            device, queue, shaderPath, geometry)) {
        return false;
    }
    swap(replacement);
    return true;
}

bool PrimitiveGpuCulling::initializeFresh(
    WGPUDevice device, WGPUQueue queue, const std::filesystem::path& shaderPath,
    const std::array<PrimitiveDrawGeometry, kShapeCount>& geometry) {
    device_ = device;
    queue_ = queue;
    geometry_ = geometry;
    uniformBuffer_ = gpu::createBuffer(
        device_, gpu::BufferDesc::uniform(
            gpu::alignUniformBufferSize(sizeof(CullUniforms)),
            "physics_primitive_cull_uniforms"));
    shaderModule_ = gpu::loadShaderModule(
        device_, shaderPath, "physics_primitive_cull.wgsl");
    if (!uniformBuffer_ || !shaderModule_) {
        shutdown();
        return false;
    }

    std::array<gpu::BindGroupLayoutEntry, 9> entries = {
        gpu::BindGroupLayoutEntry(0).computeVisible().storageBuffer(true),
        gpu::BindGroupLayoutEntry(1).computeVisible().storageBuffer(true),
        gpu::BindGroupLayoutEntry(2).computeVisible().storageBuffer(false),
        gpu::BindGroupLayoutEntry(3).computeVisible().storageBuffer(false),
        gpu::BindGroupLayoutEntry(4).computeVisible().storageBuffer(false),
        gpu::BindGroupLayoutEntry(5).computeVisible().storageBuffer(false),
        gpu::BindGroupLayoutEntry(6).computeVisible().storageBuffer(false),
        gpu::BindGroupLayoutEntry(7).computeVisible().storageBuffer(false),
        gpu::BindGroupLayoutEntry(8).computeVisible().uniformBuffer(
            false, sizeof(CullUniforms)),
    };
    bindGroupLayout_ = gpu::createBindGroupLayout(
        device_, entries, "physics_primitive_cull_layout");
    if (!bindGroupLayout_) {
        shutdown();
        return false;
    }
    const std::array<WGPUBindGroupLayout, 1> layouts{bindGroupLayout_};
    pipelineLayout_ = gpu::createPipelineLayout(
        device_, layouts, "physics_primitive_cull_pipeline_layout");
    if (!pipelineLayout_) {
        shutdown();
        return false;
    }
    const std::array<gpu::BindGroupLayoutEntry, 6> rebaseEntries = {
        gpu::BindGroupLayoutEntry(0).computeVisible().storageBuffer(true),
        gpu::BindGroupLayoutEntry(8).computeVisible().uniformBuffer(
            false, sizeof(CullUniforms)),
        gpu::BindGroupLayoutEntry(9).computeVisible().storageBuffer(true),
        gpu::BindGroupLayoutEntry(10).computeVisible().storageBuffer(false),
        gpu::BindGroupLayoutEntry(11).computeVisible().storageBuffer(true),
        gpu::BindGroupLayoutEntry(12).computeVisible().storageBuffer(true),
    };
    rebaseBindGroupLayout_ = gpu::createBindGroupLayout(
        device_, rebaseEntries, "physics_primitive_rebase_layout");
    if (!rebaseBindGroupLayout_) {
        shutdown();
        return false;
    }
    const std::array<WGPUBindGroupLayout, 1> rebaseLayouts{
        rebaseBindGroupLayout_};
    rebasePipelineLayout_ = gpu::createPipelineLayout(
        device_, rebaseLayouts, "physics_primitive_rebase_pipeline_layout");
    if (!rebasePipelineLayout_) {
        shutdown();
        return false;
    }
    rebasePipeline_ = createPipeline(
        device_, rebasePipelineLayout_, shaderModule_, "rebase_poses",
        "physics_primitive_rebase_poses");
    blocksPipeline_ = createPipeline(
        device_, pipelineLayout_, shaderModule_, "cull_blocks",
        "physics_primitive_cull_blocks");
    scanPipeline_ = createPipeline(
        device_, pipelineLayout_, shaderModule_, "scan_blocks",
        "physics_primitive_cull_scan");
    scatterPipeline_ = createPipeline(
        device_, pipelineLayout_, shaderModule_, "scatter_visible",
        "physics_primitive_cull_scatter");
    if (!rebasePipeline_ || !blocksPipeline_ || !scanPipeline_
        || !scatterPipeline_) {
        shutdown();
        return false;
    }
    return true;
}

void PrimitiveGpuCulling::swap(PrimitiveGpuCulling& other) noexcept {
    using std::swap;
    swap(device_, other.device_);
    swap(queue_, other.queue_);
    swap(bodyView_, other.bodyView_);
    swap(geometry_, other.geometry_);
    swap(allocatedBodyCapacity_, other.allocatedBodyCapacity_);
    swap(segmentCapacity_, other.segmentCapacity_);
    swap(blockCapacity_, other.blockCapacity_);
    swap(bindGroupDirty_, other.bindGroupDirty_);
    swap(rebaseBindGroupDirty_, other.rebaseBindGroupDirty_);
    swap(shaderModule_, other.shaderModule_);
    swap(bindGroupLayout_, other.bindGroupLayout_);
    swap(pipelineLayout_, other.pipelineLayout_);
    swap(blocksPipeline_, other.blocksPipeline_);
    swap(scanPipeline_, other.scanPipeline_);
    swap(scatterPipeline_, other.scatterPipeline_);
    swap(bindGroup_, other.bindGroup_);
    swap(rebaseBindGroupLayout_, other.rebaseBindGroupLayout_);
    swap(rebasePipelineLayout_, other.rebasePipelineLayout_);
    swap(rebasePipeline_, other.rebasePipeline_);
    swap(rebaseBindGroup_, other.rebaseBindGroup_);
    swap(uniformBuffer_, other.uniformBuffer_);
    swap(cameraRelativePoses_, other.cameraRelativePoses_);
    swap(visibility_, other.visibility_);
    swap(localOffsets_, other.localOffsets_);
    swap(blockSums_, other.blockSums_);
    swap(blockPrefix_, other.blockPrefix_);
    swap(visibleBodyIds_, other.visibleBodyIds_);
    swap(indirectDrawArgs_, other.indirectDrawArgs_);
}

void PrimitiveGpuCulling::shutdown() {
    releaseHandle(rebaseBindGroup_, wgpuBindGroupRelease);
    releaseHandle(bindGroup_, wgpuBindGroupRelease);
    releaseCapacityBuffers();
    destroyBuffer(uniformBuffer_);
    releaseHandle(blocksPipeline_, wgpuComputePipelineRelease);
    releaseHandle(scanPipeline_, wgpuComputePipelineRelease);
    releaseHandle(scatterPipeline_, wgpuComputePipelineRelease);
    releaseHandle(rebasePipeline_, wgpuComputePipelineRelease);
    releaseHandle(pipelineLayout_, wgpuPipelineLayoutRelease);
    releaseHandle(rebasePipelineLayout_, wgpuPipelineLayoutRelease);
    releaseHandle(bindGroupLayout_, wgpuBindGroupLayoutRelease);
    releaseHandle(rebaseBindGroupLayout_, wgpuBindGroupLayoutRelease);
    releaseHandle(shaderModule_, wgpuShaderModuleRelease);
    device_ = nullptr;
    queue_ = nullptr;
    bodyView_ = {};
    allocatedBodyCapacity_ = 0;
    segmentCapacity_ = 0;
    blockCapacity_ = 0;
    bindGroupDirty_ = true;
    rebaseBindGroupDirty_ = true;
}

bool PrimitiveGpuCulling::setBodyView(
    const physics::PhysicsRenderView& view) {
    if (!emptyBodyView(view)) {
        const uint64_t capacity = view.residentBodyCapacity;
        const bool hasHistory = view.previousPoseBuffer
            || view.previousMetadataBuffer;
        if (!view.valid() || view.shapeCount != kShapeCount
            || !std::isfinite(view.interpolationAlpha)
            || view.interpolationAlpha < 0.0f || view.interpolationAlpha > 1.0f
            || !std::isfinite(view.maximumInterpolationDistance)
            || view.maximumInterpolationDistance < 0.0f
            || (hasHistory && (!view.metadataBuffer
                || !validStorageBuffer(view.previousPoseBuffer, capacity * kPoseStride)
                || !validStorageBuffer(view.previousMetadataBuffer, capacity * kMetadataStride)))
            || !validStorageBuffer(
                view.poseBuffer, capacity * kPoseStride)
            || !validStorageBuffer(
                view.shapeBuffer, capacity * kShapeStride)
            || (view.metadataBuffer
                && !validStorageBuffer(
                    view.metadataBuffer, capacity * kMetadataStride))) {
            LOG_ERROR("PrimitiveGpuCulling: invalid physics render view");
            return false;
        }
    }
    if (bodyView_.poseBuffer != view.poseBuffer
        || bodyView_.shapeBuffer != view.shapeBuffer
        || bodyView_.metadataBuffer != view.metadataBuffer
        || bodyView_.previousPoseBuffer != view.previousPoseBuffer
        || bodyView_.previousMetadataBuffer != view.previousMetadataBuffer) {
        bindGroupDirty_ = true;
        rebaseBindGroupDirty_ = true;
    }
    bodyView_ = view;
    return true;
}

void PrimitiveGpuCulling::releaseCapacityBuffers() {
    releaseHandle(rebaseBindGroup_, wgpuBindGroupRelease);
    releaseHandle(bindGroup_, wgpuBindGroupRelease);
    destroyBuffer(cameraRelativePoses_);
    destroyBuffer(visibility_);
    destroyBuffer(localOffsets_);
    destroyBuffer(blockSums_);
    destroyBuffer(blockPrefix_);
    destroyBuffer(visibleBodyIds_);
    destroyBuffer(indirectDrawArgs_);
    bindGroupDirty_ = true;
    rebaseBindGroupDirty_ = true;
}

bool PrimitiveGpuCulling::ensureCapacity(uint32_t bodyCapacity) {
    if (bodyCapacity <= allocatedBodyCapacity_ && visibleBodyIds_)
        return true;
    uint64_t nextCapacity = std::max(256u, allocatedBodyCapacity_);
    while (nextCapacity < bodyCapacity) {
        if (nextCapacity > std::numeric_limits<uint32_t>::max() / 2u)
            return false;
        nextCapacity *= 2u;
    }
    const uint64_t nextSegmentCapacity =
        (nextCapacity + kStorageOffsetAlignmentWords - 1u)
        & ~uint64_t{kStorageOffsetAlignmentWords - 1u};
    const uint64_t nextBlockCapacity =
        (nextCapacity + kWorkgroupSize - 1u) / kWorkgroupSize;
    if (nextCapacity > std::numeric_limits<uint32_t>::max()
        || nextSegmentCapacity > std::numeric_limits<uint32_t>::max()
        || nextBlockCapacity > std::numeric_limits<uint32_t>::max()) {
        return false;
    }
    const uint32_t newCapacity = static_cast<uint32_t>(nextCapacity);
    const uint32_t newSegmentCapacity =
        static_cast<uint32_t>(nextSegmentCapacity);
    const uint32_t newBlockCapacity =
        static_cast<uint32_t>(nextBlockCapacity);

    const gpu::BufferDesc scratchDesc{
        .label = "physics_primitive_cull_scratch",
        .size = uint64_t{newCapacity} * sizeof(uint32_t),
        .usage = WGPUBufferUsage_Storage | WGPUBufferUsage_CopyDst,
    };
    WGPUBuffer nextVisibility = gpu::createBuffer(device_, scratchDesc);
    WGPUBuffer nextLocalOffsets = gpu::createBuffer(device_, scratchDesc);
    WGPUBuffer nextCameraRelativePoses = gpu::createBuffer(
        device_, gpu::BufferDesc::storage(
            uint64_t{newCapacity} * 2u * sizeof(glm::vec4), false,
            "physics_primitive_camera_relative_poses"));
    const gpu::BufferDesc blockDesc{
        .label = "physics_primitive_cull_blocks",
        .size = uint64_t{newBlockCapacity}
              * kShapeCount * sizeof(uint32_t),
        .usage = WGPUBufferUsage_Storage | WGPUBufferUsage_CopyDst,
    };
    WGPUBuffer nextBlockSums = gpu::createBuffer(device_, blockDesc);
    WGPUBuffer nextBlockPrefix = gpu::createBuffer(device_, blockDesc);
    const gpu::BufferDesc visibleDesc{
        .label = "physics_primitive_visible_body_ids",
        .size = uint64_t{newSegmentCapacity}
              * kShapeCount * sizeof(uint32_t),
        .usage = WGPUBufferUsage_Storage | WGPUBufferUsage_CopyDst
               | WGPUBufferUsage_CopySrc,
    };
    WGPUBuffer nextVisibleBodyIds = gpu::createBuffer(device_, visibleDesc);

    std::array<IndirectDrawArgs, kShapeCount> indirect{};
    for (uint32_t shape = 0; shape < kShapeCount; ++shape) {
        indirect[shape].indexCount = geometry_[shape].indexCount;
        indirect[shape].firstIndex = geometry_[shape].firstIndex;
    }
    const gpu::BufferDesc indirectDesc{
        .label = "physics_primitive_indirect_draws",
        .size = sizeof(indirect),
        .usage = WGPUBufferUsage_Storage | WGPUBufferUsage_Indirect
               | WGPUBufferUsage_CopyDst | WGPUBufferUsage_CopySrc,
    };
    WGPUBuffer nextIndirectDrawArgs = gpu::createBufferWithData(
        device_, queue_, indirectDesc,
        std::span<const IndirectDrawArgs>(indirect));
    if (!nextCameraRelativePoses || !nextVisibility || !nextLocalOffsets
        || !nextBlockSums || !nextBlockPrefix || !nextVisibleBodyIds
        || !nextIndirectDrawArgs) {
        destroyBuffer(nextCameraRelativePoses);
        destroyBuffer(nextVisibility);
        destroyBuffer(nextLocalOffsets);
        destroyBuffer(nextBlockSums);
        destroyBuffer(nextBlockPrefix);
        destroyBuffer(nextVisibleBodyIds);
        destroyBuffer(nextIndirectDrawArgs);
        return false;
    }

    releaseCapacityBuffers();
    cameraRelativePoses_ = nextCameraRelativePoses;
    visibility_ = nextVisibility;
    localOffsets_ = nextLocalOffsets;
    blockSums_ = nextBlockSums;
    blockPrefix_ = nextBlockPrefix;
    visibleBodyIds_ = nextVisibleBodyIds;
    indirectDrawArgs_ = nextIndirectDrawArgs;
    allocatedBodyCapacity_ = newCapacity;
    segmentCapacity_ = newSegmentCapacity;
    blockCapacity_ = newBlockCapacity;
    bindGroupDirty_ = true;
    return true;
}

bool PrimitiveGpuCulling::updateBindGroup() {
    if (bindGroupDirty_) {
        releaseHandle(bindGroup_, wgpuBindGroupRelease);
        if (!bodyView_.valid() || !visibleBodyIds_ || !renderPoseBuffer())
            return false;
        const std::array<gpu::BindGroupEntry, 9> entries = {
            gpu::BindGroupEntry(0).buffer(renderPoseBuffer()),
            gpu::BindGroupEntry(1).buffer(bodyView_.shapeBuffer),
            gpu::BindGroupEntry(2).buffer(visibility_),
            gpu::BindGroupEntry(3).buffer(localOffsets_),
            gpu::BindGroupEntry(4).buffer(blockSums_),
            gpu::BindGroupEntry(5).buffer(blockPrefix_),
            gpu::BindGroupEntry(6).buffer(visibleBodyIds_),
            gpu::BindGroupEntry(7).buffer(indirectDrawArgs_),
            gpu::BindGroupEntry(8).buffer(
                uniformBuffer_, 0, sizeof(CullUniforms)),
        };
        bindGroup_ = gpu::createBindGroup(
            device_, bindGroupLayout_, entries,
            "physics_primitive_cull_bind_group");
        bindGroupDirty_ = false;
    }
    if (!bindGroup_) return false;

    if (!bodyView_.metadataBuffer) {
        releaseHandle(rebaseBindGroup_, wgpuBindGroupRelease);
        rebaseBindGroupDirty_ = false;
        return true;
    }
    if (!rebaseBindGroupDirty_) return rebaseBindGroup_ != nullptr;
    releaseHandle(rebaseBindGroup_, wgpuBindGroupRelease);
    const std::array<gpu::BindGroupEntry, 6> rebaseEntries = {
        gpu::BindGroupEntry(0).buffer(bodyView_.poseBuffer),
        gpu::BindGroupEntry(8).buffer(
            uniformBuffer_, 0, sizeof(CullUniforms)),
        gpu::BindGroupEntry(9).buffer(bodyView_.metadataBuffer),
        gpu::BindGroupEntry(10).buffer(cameraRelativePoses_),
        gpu::BindGroupEntry(11).buffer(bodyView_.previousPoseBuffer
            ? bodyView_.previousPoseBuffer : bodyView_.poseBuffer),
        gpu::BindGroupEntry(12).buffer(bodyView_.previousMetadataBuffer
            ? bodyView_.previousMetadataBuffer : bodyView_.metadataBuffer),
    };
    rebaseBindGroup_ = gpu::createBindGroup(
        device_, rebaseBindGroupLayout_, rebaseEntries,
        "physics_primitive_rebase_bind_group");
    rebaseBindGroupDirty_ = false;
    return rebaseBindGroup_ != nullptr;
}

bool PrimitiveGpuCulling::encode(WGPUCommandEncoder encoder,
                                 const glm::mat4& viewProjection,
                                 const glm::ivec3& cameraSector) {
    if (!encoder || !bodyView_.valid()
        || bodyView_.residentBodyCapacity == 0
        || !ensureCapacity(bodyView_.residentBodyCapacity)
        || !updateBindGroup()) return false;

    CullUniforms uniforms;
    const Frustum frustum = Frustum::fromViewProj(viewProjection);
    if (!frustum.valid()) return false;
    for (size_t index = 0; index < frustum.planes.size(); ++index) {
        uniforms.planes[index] = glm::vec4(
            frustum.planes[index].normal, frustum.planes[index].distance);
    }
    const uint32_t blockCount =
        (bodyView_.residentBodyCapacity + kWorkgroupSize - 1u)
        / kWorkgroupSize;
    uniforms.counts = glm::uvec4(
        bodyView_.residentBodyCapacity, blockCount, segmentCapacity_,
        kShapeCount);
    uniforms.cameraSector = glm::ivec4(
        cameraSector, kMaximumRenderSectorDelta);
    uniforms.interpolation = glm::vec4(
        bodyView_.previousPoseBuffer ? bodyView_.interpolationAlpha : 1.0f,
        bodyView_.maximumInterpolationDistance, 0.0f, 0.0f);
    if (!gpu::writeBuffer(queue_, uniformBuffer_, 0, uniforms)) {
        return false;
    }

    WGPUComputePassDescriptor passDesc{};
    WGPU_SET_LABEL(passDesc, "physics_primitive_culling");
    WGPUComputePassEncoder pass =
        wgpuCommandEncoderBeginComputePass(encoder, &passDesc);
    if (!pass) {
        LOG_ERROR("PrimitiveGpuCulling: failed to begin compute pass");
        return false;
    }
    if (bodyView_.metadataBuffer) {
        wgpuComputePassEncoderSetBindGroup(
            pass, 0, rebaseBindGroup_, 0, nullptr);
        wgpuComputePassEncoderSetPipeline(pass, rebasePipeline_);
        wgpuComputePassEncoderDispatchWorkgroups(pass, blockCount, 1, 1);
    }
    wgpuComputePassEncoderSetBindGroup(pass, 0, bindGroup_, 0, nullptr);
    wgpuComputePassEncoderSetPipeline(pass, blocksPipeline_);
    wgpuComputePassEncoderDispatchWorkgroups(pass, blockCount, 1, 1);
    wgpuComputePassEncoderSetPipeline(pass, scanPipeline_);
    wgpuComputePassEncoderDispatchWorkgroups(pass, 1, 1, 1);
    wgpuComputePassEncoderSetPipeline(pass, scatterPipeline_);
    wgpuComputePassEncoderDispatchWorkgroups(pass, blockCount, 1, 1);
    wgpuComputePassEncoderEnd(pass);
    wgpuComputePassEncoderRelease(pass);
    return true;
}

} // namespace voxy::render
