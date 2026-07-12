// ═══════════════════════════════════════════════════════════════════════════════
// svo_buffers.cpp - SVO GPU Buffer Management Implementation
// ═══════════════════════════════════════════════════════════════════════════════

#include "voxel/svo_buffers.hpp"
#include "core/log.hpp"

#include <cstring>
#include <string>

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

bool SVOGPUBuffers::create(WGPUDevice device, WGPUQueue /*queue*/,
                            const SVOBufferData& data,
                            const SVOUniforms& uniforms,
                            std::string_view label) {
    // Release any existing buffers
    release();

    if (!device) {
        LOG_ERROR("SVOGPUBuffers::create: device is null");
        return false;
    }

    std::string labelStr{label};

    // Create uniform buffer
    uniformBuffer_ = createUniformBuffer(device, &uniforms, sizeof(SVOUniforms),
                                          labelStr + "_uniforms");
    if (!uniformBuffer_) {
        LOG_ERROR("SVOGPUBuffers: Failed to create uniform buffer");
        release();
        return false;
    }

    // Create node buffers (only if there are nodes)
    if (!data.nodeMasks.empty()) {
        nodeMasksBuffer_ = createStorageBuffer(device, 
                                                data.nodeMasks.data(),
                                                data.nodeMasks.size() * sizeof(uint32_t),
                                                labelStr + "_nodeMasks");
        if (!nodeMasksBuffer_) {
            LOG_ERROR("SVOGPUBuffers: Failed to create nodeMasks buffer");
            release();
            return false;
        }

        nodeChildPtrsBuffer_ = createStorageBuffer(device,
                                                    data.nodeChildPtrs.data(),
                                                    data.nodeChildPtrs.size() * sizeof(uint32_t),
                                                    labelStr + "_nodeChildPtrs");
        if (!nodeChildPtrsBuffer_) {
            LOG_ERROR("SVOGPUBuffers: Failed to create nodeChildPtrs buffer");
            release();
            return false;
        }
    }

    // Create brick buffers (only if there are bricks)
    if (!data.brickOccupancyLo.empty()) {
        brickOccLoBuffer_ = createStorageBuffer(device,
                                                 data.brickOccupancyLo.data(),
                                                 data.brickOccupancyLo.size() * sizeof(uint32_t),
                                                 labelStr + "_brickOccLo");
        if (!brickOccLoBuffer_) {
            LOG_ERROR("SVOGPUBuffers: Failed to create brickOccLo buffer");
            release();
            return false;
        }

        brickOccHiBuffer_ = createStorageBuffer(device,
                                                 data.brickOccupancyHi.data(),
                                                 data.brickOccupancyHi.size() * sizeof(uint32_t),
                                                 labelStr + "_brickOccHi");
        if (!brickOccHiBuffer_) {
            LOG_ERROR("SVOGPUBuffers: Failed to create brickOccHi buffer");
            release();
            return false;
        }

        brickMetaBuffer_ = createStorageBuffer(device,
                                                data.brickMeta.data(),
                                                data.brickMeta.size() * sizeof(uint32_t),
                                                labelStr + "_brickMeta");
        if (!brickMetaBuffer_) {
            LOG_ERROR("SVOGPUBuffers: Failed to create brickMeta buffer");
            release();
            return false;
        }
    }

    // Create optional contour buffer
    if (!data.contourNormals.empty()) {
        contourNormalsBuffer_ = createStorageBuffer(device,
                                                     data.contourNormals.data(),
                                                     data.contourNormals.size() * sizeof(glm::vec4),
                                                     labelStr + "_contourNormals");
        if (!contourNormalsBuffer_) {
            LOG_WARN("SVOGPUBuffers: Failed to create contourNormals buffer (optional)");
            // Continue without contours - not a fatal error
        }
    }

    nodeCount_ = data.nodeCount;
    brickCount_ = data.brickCount;

    LOG_DEBUG("SVOGPUBuffers: Created {} nodes, {} bricks", nodeCount_, brickCount_);
    return true;
}

void SVOGPUBuffers::updateUniforms(WGPUQueue queue, const SVOUniforms& uniforms) {
    if (uniformBuffer_ && queue) {
        wgpuQueueWriteBuffer(queue, uniformBuffer_, 0, &uniforms, sizeof(SVOUniforms));
    }
}

void SVOGPUBuffers::updateBricks(WGPUQueue queue, uint32_t startBrick,
                                  std::span<const SVOLeafBrick> bricks) {
    if (!queue || bricks.empty()) return;
    
    if (startBrick + bricks.size() > brickCount_) {
        LOG_ERROR("SVOGPUBuffers::updateBricks: range exceeds brick count");
        return;
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

    size_t offset = startBrick * sizeof(uint32_t);
    size_t size = bricks.size() * sizeof(uint32_t);

    if (brickOccLoBuffer_) {
        wgpuQueueWriteBuffer(queue, brickOccLoBuffer_, offset, occLo.data(), size);
    }
    if (brickOccHiBuffer_) {
        wgpuQueueWriteBuffer(queue, brickOccHiBuffer_, offset, occHi.data(), size);
    }
    if (brickMetaBuffer_) {
        wgpuQueueWriteBuffer(queue, brickMetaBuffer_, offset, meta.data(), size);
    }
}

void SVOGPUBuffers::release() {
    if (uniformBuffer_) {
        wgpuBufferRelease(uniformBuffer_);
        uniformBuffer_ = nullptr;
    }
    if (nodeMasksBuffer_) {
        wgpuBufferRelease(nodeMasksBuffer_);
        nodeMasksBuffer_ = nullptr;
    }
    if (nodeChildPtrsBuffer_) {
        wgpuBufferRelease(nodeChildPtrsBuffer_);
        nodeChildPtrsBuffer_ = nullptr;
    }
    if (brickOccLoBuffer_) {
        wgpuBufferRelease(brickOccLoBuffer_);
        brickOccLoBuffer_ = nullptr;
    }
    if (brickOccHiBuffer_) {
        wgpuBufferRelease(brickOccHiBuffer_);
        brickOccHiBuffer_ = nullptr;
    }
    if (brickMetaBuffer_) {
        wgpuBufferRelease(brickMetaBuffer_);
        brickMetaBuffer_ = nullptr;
    }
    if (contourNormalsBuffer_) {
        wgpuBufferRelease(contourNormalsBuffer_);
        contourNormalsBuffer_ = nullptr;
    }
    nodeCount_ = 0;
    brickCount_ = 0;
}

WGPUBuffer SVOGPUBuffers::createStorageBuffer(WGPUDevice device,
                                               const void* data,
                                               size_t size,
                                               std::string_view label) {
    // Ensure minimum size and alignment
    size_t alignedSize = alignToStorageBuffer(std::max(size, size_t(16)));

    WGPUBufferDescriptor desc{};
    // Handle WGPUStringView change in newer dawn/webgpu headers
#if defined(__EMSCRIPTEN__)
    WGPUStringView labelView;
    labelView.data = label.data();
    labelView.length = label.length();
    desc.label = labelView;
#else
    desc.label = label.data();
#endif
    desc.size = alignedSize;
    desc.usage = WGPUBufferUsage_Storage | WGPUBufferUsage_CopyDst;
    desc.mappedAtCreation = false;

    WGPUBuffer buffer = wgpuDeviceCreateBuffer(device, &desc);
    if (!buffer) {
        return nullptr;
    }

    // Upload data
    if (data && size > 0) {
        WGPUQueue queue = wgpuDeviceGetQueue(device);
        if (queue) {
            wgpuQueueWriteBuffer(queue, buffer, 0, data, size);
            wgpuQueueRelease(queue);
        }
    }

    return buffer;
}

WGPUBuffer SVOGPUBuffers::createUniformBuffer(WGPUDevice device,
                                               const void* data,
                                               size_t size,
                                               std::string_view label) {
    // Ensure minimum size and alignment for uniform buffers
    size_t alignedSize = alignToUniformBuffer(std::max(size, size_t(16)));

    WGPUBufferDescriptor desc{};
#if defined(__EMSCRIPTEN__)
    WGPUStringView labelView;
    labelView.data = label.data();
    labelView.length = label.length();
    desc.label = labelView;
#else
    desc.label = label.data();
#endif
    desc.size = alignedSize;
    desc.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
    desc.mappedAtCreation = false;


    WGPUBuffer buffer = wgpuDeviceCreateBuffer(device, &desc);
    if (!buffer) {
        return nullptr;
    }

    // Upload data
    if (data && size > 0) {
        WGPUQueue queue = wgpuDeviceGetQueue(device);
        if (queue) {
            wgpuQueueWriteBuffer(queue, buffer, 0, data, size);
            wgpuQueueRelease(queue);
        }
    }

    return buffer;
}

} // namespace voxy::voxel
