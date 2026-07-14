#include "render/primitive_gpu_culling.hpp"

#include "core/log.hpp"
#include "gpu/resources.hpp"
#include "render/frustum.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <vector>

namespace voxy::render {
namespace {

constexpr uint32_t kWorkgroupSize = 256;
constexpr uint32_t kStorageOffsetAlignmentWords = 64;
constexpr int32_t kMaximumRenderSectorDelta = 4096;

struct alignas(16) CullUniforms {
    std::array<glm::vec4, 6> planes{};
    glm::uvec4 counts{};
    glm::ivec4 cameraSector{};
};

static_assert(sizeof(CullUniforms) == 128);

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

} // namespace

PrimitiveGpuCulling::~PrimitiveGpuCulling() { shutdown(); }

bool PrimitiveGpuCulling::initialize(
    WGPUDevice device, WGPUQueue queue, const std::filesystem::path& shaderPath,
    const std::array<PrimitiveDrawGeometry, kShapeCount>& geometry) {
    shutdown();
    if (!device || !queue) return false;
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
    const std::array<gpu::BindGroupLayoutEntry, 4> rebaseEntries = {
        gpu::BindGroupLayoutEntry(0).computeVisible().storageBuffer(true),
        gpu::BindGroupLayoutEntry(8).computeVisible().uniformBuffer(
            false, sizeof(CullUniforms)),
        gpu::BindGroupLayoutEntry(9).computeVisible().storageBuffer(true),
        gpu::BindGroupLayoutEntry(10).computeVisible().storageBuffer(false),
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

void PrimitiveGpuCulling::setBodyView(const physics::PhysicsRenderView& view) {
    if (bodyView_.poseBuffer != view.poseBuffer
        || bodyView_.shapeBuffer != view.shapeBuffer
        || bodyView_.metadataBuffer != view.metadataBuffer) {
        bindGroupDirty_ = true;
        rebaseBindGroupDirty_ = true;
    }
    bodyView_ = view;
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
    uint32_t newCapacity = std::max(256u, allocatedBodyCapacity_);
    while (newCapacity < bodyCapacity) {
        if (newCapacity > std::numeric_limits<uint32_t>::max() / 2u)
            return false;
        newCapacity *= 2u;
    }
    releaseCapacityBuffers();
    allocatedBodyCapacity_ = newCapacity;
    segmentCapacity_ = (newCapacity + kStorageOffsetAlignmentWords - 1u)
                     & ~(kStorageOffsetAlignmentWords - 1u);
    blockCapacity_ = (newCapacity + kWorkgroupSize - 1u) / kWorkgroupSize;

    const gpu::BufferDesc scratchDesc{
        .label = "physics_primitive_cull_scratch",
        .size = uint64_t{newCapacity} * sizeof(uint32_t),
        .usage = WGPUBufferUsage_Storage | WGPUBufferUsage_CopyDst,
    };
    visibility_ = gpu::createBuffer(device_, scratchDesc);
    localOffsets_ = gpu::createBuffer(device_, scratchDesc);
    cameraRelativePoses_ = gpu::createBuffer(
        device_, gpu::BufferDesc::storage(
            uint64_t{newCapacity} * 2u * sizeof(glm::vec4), false,
            "physics_primitive_camera_relative_poses"));
    const gpu::BufferDesc blockDesc{
        .label = "physics_primitive_cull_blocks",
        .size = uint64_t{blockCapacity_} * kShapeCount * sizeof(uint32_t),
        .usage = WGPUBufferUsage_Storage | WGPUBufferUsage_CopyDst,
    };
    blockSums_ = gpu::createBuffer(device_, blockDesc);
    blockPrefix_ = gpu::createBuffer(device_, blockDesc);
    const gpu::BufferDesc visibleDesc{
        .label = "physics_primitive_visible_body_ids",
        .size = uint64_t{segmentCapacity_} * kShapeCount * sizeof(uint32_t),
        .usage = WGPUBufferUsage_Storage | WGPUBufferUsage_CopyDst
               | WGPUBufferUsage_CopySrc,
    };
    visibleBodyIds_ = gpu::createBuffer(device_, visibleDesc);

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
    indirectDrawArgs_ = gpu::createBufferWithData(
        device_, queue_, indirectDesc,
        std::span<const IndirectDrawArgs>(indirect));
    bindGroupDirty_ = true;
    return cameraRelativePoses_ && visibility_ && localOffsets_
        && blockSums_ && blockPrefix_
        && visibleBodyIds_ && indirectDrawArgs_;
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
    const std::array<gpu::BindGroupEntry, 4> rebaseEntries = {
        gpu::BindGroupEntry(0).buffer(bodyView_.poseBuffer),
        gpu::BindGroupEntry(8).buffer(
            uniformBuffer_, 0, sizeof(CullUniforms)),
        gpu::BindGroupEntry(9).buffer(bodyView_.metadataBuffer),
        gpu::BindGroupEntry(10).buffer(cameraRelativePoses_),
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
    gpu::writeBuffer(queue_, uniformBuffer_, 0, uniforms);

    WGPUComputePassDescriptor passDesc{};
    WGPU_SET_LABEL(passDesc, "physics_primitive_culling");
    WGPUComputePassEncoder pass =
        wgpuCommandEncoderBeginComputePass(encoder, &passDesc);
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
