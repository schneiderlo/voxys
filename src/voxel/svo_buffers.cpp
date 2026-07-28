// ═══════════════════════════════════════════════════════════════════════════════
// svo_buffers.cpp - SVO GPU Buffer Management Implementation
// ═══════════════════════════════════════════════════════════════════════════════

#include "voxel/svo_buffers.hpp"
#include "core/log.hpp"
#include "gpu/resources.hpp"

#include <string>
#include <utility>

namespace voxy::voxel {

// ─────────────────────────────────────────────────────────────────────────────
// SVOGPUBuffers Implementation
// ─────────────────────────────────────────────────────────────────────────────

SVOGPUBuffers::~SVOGPUBuffers() {
    release();
}

SVOGPUBuffers::SVOGPUBuffers(SVOGPUBuffers&& other) noexcept
    : uniformBuffer_(other.uniformBuffer_)
    , nodeMasksBuffer_(other.nodeMasksBuffer_)
    , nodeChildPtrsBuffer_(other.nodeChildPtrsBuffer_)
    , brickOccLoBuffer_(other.brickOccLoBuffer_)
    , brickOccHiBuffer_(other.brickOccHiBuffer_)
    , brickMetaBuffer_(other.brickMetaBuffer_)
    , contourNormalsBuffer_(other.contourNormalsBuffer_)
    , nodeCount_(other.nodeCount_)
    , brickCount_(other.brickCount_)
{
    other.uniformBuffer_ = nullptr;
    other.nodeMasksBuffer_ = nullptr;
    other.nodeChildPtrsBuffer_ = nullptr;
    other.brickOccLoBuffer_ = nullptr;
    other.brickOccHiBuffer_ = nullptr;
    other.brickMetaBuffer_ = nullptr;
    other.contourNormalsBuffer_ = nullptr;
    other.nodeCount_ = 0;
    other.brickCount_ = 0;
}

SVOGPUBuffers& SVOGPUBuffers::operator=(SVOGPUBuffers&& other) noexcept {
    if (this != &other) {
        release();
        
        uniformBuffer_ = other.uniformBuffer_;
        nodeMasksBuffer_ = other.nodeMasksBuffer_;
        nodeChildPtrsBuffer_ = other.nodeChildPtrsBuffer_;
        brickOccLoBuffer_ = other.brickOccLoBuffer_;
        brickOccHiBuffer_ = other.brickOccHiBuffer_;
        brickMetaBuffer_ = other.brickMetaBuffer_;
        contourNormalsBuffer_ = other.contourNormalsBuffer_;
        nodeCount_ = other.nodeCount_;
        brickCount_ = other.brickCount_;
        
        other.uniformBuffer_ = nullptr;
        other.nodeMasksBuffer_ = nullptr;
        other.nodeChildPtrsBuffer_ = nullptr;
        other.brickOccLoBuffer_ = nullptr;
        other.brickOccHiBuffer_ = nullptr;
        other.brickMetaBuffer_ = nullptr;
        other.contourNormalsBuffer_ = nullptr;
        other.nodeCount_ = 0;
        other.brickCount_ = 0;
    }
    return *this;
}

bool SVOGPUBuffers::create(WGPUDevice device, WGPUQueue queue,
                            const SVOBufferData& data,
                            const SVOUniforms& uniforms,
                            std::string_view label) {
    if (!device || !queue || !data.valid() || !uniforms.valid()
        || (data.nodeCount != 0u
            && uniforms.rootNodeIndex >= data.nodeCount)) {
        LOG_ERROR(
            "SVOGPUBuffers::create: invalid handles, SoA data, or uniforms");
        return false;
    }

    std::string labelStr{label};
    SVOGPUBuffers replacement;

    // Create uniform buffer
    replacement.uniformBuffer_ = createUniformBuffer(
        device, queue, &uniforms, sizeof(SVOUniforms),
        labelStr + "_uniforms");
    if (!replacement.uniformBuffer_) {
        LOG_ERROR("SVOGPUBuffers: Failed to create uniform buffer");
        return false;
    }

    // Create node buffers (only if there are nodes)
    if (!data.nodeMasks.empty()) {
        replacement.nodeMasksBuffer_ = createStorageBuffer(
            device, queue, data.nodeMasks.data(),
            data.nodeMasks.size() * sizeof(uint32_t),
            labelStr + "_nodeMasks");
        if (!replacement.nodeMasksBuffer_) {
            LOG_ERROR("SVOGPUBuffers: Failed to create nodeMasks buffer");
            return false;
        }

        replacement.nodeChildPtrsBuffer_ = createStorageBuffer(
            device, queue, data.nodeChildPtrs.data(),
            data.nodeChildPtrs.size() * sizeof(uint32_t),
            labelStr + "_nodeChildPtrs");
        if (!replacement.nodeChildPtrsBuffer_) {
            LOG_ERROR("SVOGPUBuffers: Failed to create nodeChildPtrs buffer");
            return false;
        }
    }

    // Create brick buffers (only if there are bricks)
    if (!data.brickOccupancyLo.empty()) {
        replacement.brickOccLoBuffer_ = createStorageBuffer(
            device, queue, data.brickOccupancyLo.data(),
            data.brickOccupancyLo.size() * sizeof(uint32_t),
            labelStr + "_brickOccLo");
        if (!replacement.brickOccLoBuffer_) {
            LOG_ERROR("SVOGPUBuffers: Failed to create brickOccLo buffer");
            return false;
        }

        replacement.brickOccHiBuffer_ = createStorageBuffer(
            device, queue, data.brickOccupancyHi.data(),
            data.brickOccupancyHi.size() * sizeof(uint32_t),
            labelStr + "_brickOccHi");
        if (!replacement.brickOccHiBuffer_) {
            LOG_ERROR("SVOGPUBuffers: Failed to create brickOccHi buffer");
            return false;
        }

        replacement.brickMetaBuffer_ = createStorageBuffer(
            device, queue, data.brickMeta.data(),
            data.brickMeta.size() * sizeof(uint32_t),
            labelStr + "_brickMeta");
        if (!replacement.brickMetaBuffer_) {
            LOG_ERROR("SVOGPUBuffers: Failed to create brickMeta buffer");
            return false;
        }
    }

    // Create optional contour buffer
    if (!data.contourNormals.empty()) {
        replacement.contourNormalsBuffer_ = createStorageBuffer(
            device, queue, data.contourNormals.data(),
            data.contourNormals.size() * sizeof(glm::vec4),
            labelStr + "_contourNormals");
        if (!replacement.contourNormalsBuffer_) {
            LOG_ERROR("SVOGPUBuffers: Failed to create contourNormals buffer");
            return false;
        }
    }

    replacement.nodeCount_ = data.nodeCount;
    replacement.brickCount_ = data.brickCount;
    *this = std::move(replacement);

    LOG_DEBUG("SVOGPUBuffers: Created {} nodes, {} bricks", nodeCount_, brickCount_);
    return true;
}

bool SVOGPUBuffers::updateUniforms(
    WGPUQueue queue, const SVOUniforms& uniforms) {
    if (!uniforms.valid()
        || (nodeCount_ != 0u && uniforms.rootNodeIndex >= nodeCount_)) {
        return false;
    }
    return gpu::writeBuffer(queue, uniformBuffer_, 0u, uniforms);
}

bool SVOGPUBuffers::updateBricks(
    WGPUQueue queue, uint32_t startBrick,
    std::span<const SVOLeafBrick> bricks) {
    if (!queue || !brickOccLoBuffer_ || !brickOccHiBuffer_
        || !brickMetaBuffer_ || startBrick > brickCount_
        || bricks.size() > size_t{brickCount_ - startBrick}) {
        LOG_ERROR("SVOGPUBuffers::updateBricks: range exceeds brick count");
        return false;
    }
    if (bricks.empty()) return true;
    if (!contourNormalsBuffer_
        && std::any_of(
            bricks.begin(), bricks.end(),
            [](const SVOLeafBrick& brick) {
                return hasFlag(brick.flags, BrickFlags::HasContour);
            })) {
        LOG_ERROR(
            "SVOGPUBuffers::updateBricks: contour flag has no contour buffer");
        return false;
    }

    // Prepare SoA data for update
    std::vector<uint32_t> occLo(bricks.size());
    std::vector<uint32_t> occHi(bricks.size());
    std::vector<uint32_t> meta(bricks.size());

    for (size_t i = 0; i < bricks.size(); ++i) {
        occLo[i] = bricks[i].occupancyLo();
        occHi[i] = bricks[i].occupancyHi();
        meta[i] = bricks[i].packMeta();
    }

    const uint64_t offset = uint64_t{startBrick} * sizeof(uint32_t);
    const size_t size = bricks.size() * sizeof(uint32_t);
    for (WGPUBuffer buffer : {
             brickOccLoBuffer_, brickOccHiBuffer_, brickMetaBuffer_}) {
        if (!gpu::isBufferWriteDataValid(
                wgpuBufferGetSize(buffer), wgpuBufferGetUsage(buffer),
                offset, size)) {
            return false;
        }
    }
    return gpu::writeBuffer(
               queue, brickOccLoBuffer_, offset,
               std::span<const uint32_t>{occLo})
        && gpu::writeBuffer(
               queue, brickOccHiBuffer_, offset,
               std::span<const uint32_t>{occHi})
        && gpu::writeBuffer(
               queue, brickMetaBuffer_, offset,
               std::span<const uint32_t>{meta});
}

void SVOGPUBuffers::release() {
    if (uniformBuffer_) {
        wgpuBufferDestroy(uniformBuffer_);
        wgpuBufferRelease(uniformBuffer_);
        uniformBuffer_ = nullptr;
    }
    if (nodeMasksBuffer_) {
        wgpuBufferDestroy(nodeMasksBuffer_);
        wgpuBufferRelease(nodeMasksBuffer_);
        nodeMasksBuffer_ = nullptr;
    }
    if (nodeChildPtrsBuffer_) {
        wgpuBufferDestroy(nodeChildPtrsBuffer_);
        wgpuBufferRelease(nodeChildPtrsBuffer_);
        nodeChildPtrsBuffer_ = nullptr;
    }
    if (brickOccLoBuffer_) {
        wgpuBufferDestroy(brickOccLoBuffer_);
        wgpuBufferRelease(brickOccLoBuffer_);
        brickOccLoBuffer_ = nullptr;
    }
    if (brickOccHiBuffer_) {
        wgpuBufferDestroy(brickOccHiBuffer_);
        wgpuBufferRelease(brickOccHiBuffer_);
        brickOccHiBuffer_ = nullptr;
    }
    if (brickMetaBuffer_) {
        wgpuBufferDestroy(brickMetaBuffer_);
        wgpuBufferRelease(brickMetaBuffer_);
        brickMetaBuffer_ = nullptr;
    }
    if (contourNormalsBuffer_) {
        wgpuBufferDestroy(contourNormalsBuffer_);
        wgpuBufferRelease(contourNormalsBuffer_);
        contourNormalsBuffer_ = nullptr;
    }
    nodeCount_ = 0;
    brickCount_ = 0;
}

WGPUBuffer SVOGPUBuffers::createStorageBuffer(
    WGPUDevice device, WGPUQueue queue, const void* data,
    size_t size, std::string_view label) {
    // Ensure minimum size and alignment
    const size_t alignedSize =
        alignToStorageBuffer(std::max(size, size_t{16}));
    if (alignedSize == 0u || (size != 0u && data == nullptr)) return nullptr;
    return gpu::createBufferWithData(
        device, queue,
        gpu::BufferDesc{
            .label = label,
            .size = alignedSize,
            .usage = WGPUBufferUsage_Storage | WGPUBufferUsage_CopyDst,
        },
        std::span<const std::byte>(
            static_cast<const std::byte*>(data), size));
}

WGPUBuffer SVOGPUBuffers::createUniformBuffer(
    WGPUDevice device, WGPUQueue queue, const void* data,
    size_t size, std::string_view label) {
    // Ensure minimum size and alignment for uniform buffers
    const size_t alignedSize =
        alignToUniformBuffer(std::max(size, size_t{16}));
    if (alignedSize == 0u || (size != 0u && data == nullptr)) return nullptr;
    return gpu::createBufferWithData(
        device, queue,
        gpu::BufferDesc{
            .label = label,
            .size = alignedSize,
            .usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst,
        },
        std::span<const std::byte>(
            static_cast<const std::byte*>(data), size));
}

} // namespace voxy::voxel
