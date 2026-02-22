// ═══════════════════════════════════════════════════════════════════════════════
// svo_buffers.hpp - SVO GPU Buffer Management (C++20)
// ═══════════════════════════════════════════════════════════════════════════════
// Provides Structure-of-Arrays (SoA) buffer container for efficient GPU upload
// and cache-coherent access during ray traversal. The SoA layout separates node
// masks, child pointers, and brick data into separate arrays for better memory
// coalescing on the GPU.
//
// GPU Buffer Layout:
// - Buffer 0: Uniforms
// - Buffer 1: nodeMasks (packed u32: childMask | leafMask << 8 | parentIndex << 16)
// - Buffer 2: nodeChildPtrs (child offset indices)
// - Buffer 3: brickOccupancyLo (low 32 bits of occupancy)
// - Buffer 4: brickOccupancyHi (high 32 bits of occupancy)
// - Buffer 5: brickMeta (materialBase | flags << 16)
// - Buffer 6: contourNormals (optional, vec4 per brick)
// ═══════════════════════════════════════════════════════════════════════════════

#pragma once

#include "svo_types.hpp"

#include <cstdint>
#include <vector>
#include <span>
#include <optional>
#include <string_view>

// WebGPU header - same API for native (wgpu-native) and WASM
#if defined(VOXY_WASM)
    #include <webgpu/webgpu.h>
#else
    #include <webgpu.h>
#endif

namespace voxy::voxel {

// ─────────────────────────────────────────────────────────────────────────────
// SoA Buffer Container (CPU-side)
// ─────────────────────────────────────────────────────────────────────────────

/// Structure-of-Arrays container for SVO data
/// Separates node and brick data into cache-friendly arrays for GPU upload
struct SVOBufferData {
    // Interior node data (SoA)
    std::vector<uint32_t> nodeMasks;      ///< Packed: childMask | leafMask << 8 | parentIndex << 16
    std::vector<uint32_t> nodeChildPtrs;  ///< Child offset indices

    // Leaf brick data (SoA)
    std::vector<uint32_t> brickOccupancyLo;  ///< Low 32 bits of 64-bit occupancy
    std::vector<uint32_t> brickOccupancyHi;  ///< High 32 bits of 64-bit occupancy
    std::vector<uint32_t> brickMeta;         ///< Packed: materialBase | flags << 16

    // Optional contour data
    std::vector<glm::vec4> contourNormals;  ///< Normal + offset, vec4 aligned

    // Metadata
    uint32_t nodeCount = 0;   ///< Number of interior nodes
    uint32_t brickCount = 0;  ///< Number of leaf bricks

    /// Clear all data
    void clear() noexcept {
        nodeMasks.clear();
        nodeChildPtrs.clear();
        brickOccupancyLo.clear();
        brickOccupancyHi.clear();
        brickMeta.clear();
        contourNormals.clear();
        nodeCount = 0;
        brickCount = 0;
    }

    /// Reserve space for expected node and brick counts
    void reserve(uint32_t nodes, uint32_t bricks, bool withContours = false) {
        nodeMasks.reserve(nodes);
        nodeChildPtrs.reserve(nodes);
        brickOccupancyLo.reserve(bricks);
        brickOccupancyHi.reserve(bricks);
        brickMeta.reserve(bricks);
        if (withContours) {
            contourNormals.reserve(bricks);
        }
    }

    /// Add an interior node, returns its index
    uint32_t addNode(const SVOInteriorNode& node) {
        uint32_t idx = nodeCount++;
        nodeMasks.push_back(node.packMasks());
        nodeChildPtrs.push_back(node.childOffset);
        return idx;
    }

    /// Add a leaf brick, returns its index
    uint32_t addBrick(const SVOLeafBrick& brick, const ContourData* contour = nullptr) {
        uint32_t idx = brickCount++;
        brickOccupancyLo.push_back(brick.occupancyLo());
        brickOccupancyHi.push_back(brick.occupancyHi());
        brickMeta.push_back(brick.packMeta());
        if (contour) {
            contourNormals.push_back(glm::vec4(contour->normal, contour->intersectOffset));
        }
        return idx;
    }

    /// Get node by index
    [[nodiscard]] SVOInteriorNode getNode(uint32_t idx) const noexcept {
        return SVOInteriorNode::unpackMasks(nodeMasks[idx], nodeChildPtrs[idx]);
    }

    /// Get brick by index
    [[nodiscard]] SVOLeafBrick getBrick(uint32_t idx) const noexcept {
        uint64_t occ = SVOLeafBrick::makeOccupancy(brickOccupancyLo[idx], brickOccupancyHi[idx]);
        uint32_t meta = brickMeta[idx];
        return SVOLeafBrick(occ, 
                            static_cast<uint16_t>(meta & 0xFFFF),
                            static_cast<BrickFlags>(meta >> 16));
    }

    /// Check if contour data is present
    [[nodiscard]] bool hasContours() const noexcept {
        return !contourNormals.empty();
    }

    /// Calculate total memory usage in bytes
    [[nodiscard]] size_t memoryUsage() const noexcept {
        return nodeMasks.size() * sizeof(uint32_t) +
               nodeChildPtrs.size() * sizeof(uint32_t) +
               brickOccupancyLo.size() * sizeof(uint32_t) +
               brickOccupancyHi.size() * sizeof(uint32_t) +
               brickMeta.size() * sizeof(uint32_t) +
               contourNormals.size() * sizeof(glm::vec4);
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// GPU Buffers
// ─────────────────────────────────────────────────────────────────────────────

/// GPU buffer wrapper for SVO data
class SVOGPUBuffers {
public:
    SVOGPUBuffers() = default;
    ~SVOGPUBuffers();

    // Non-copyable
    SVOGPUBuffers(const SVOGPUBuffers&) = delete;
    SVOGPUBuffers& operator=(const SVOGPUBuffers&) = delete;

    // Movable
    SVOGPUBuffers(SVOGPUBuffers&& other) noexcept;
    SVOGPUBuffers& operator=(SVOGPUBuffers&& other) noexcept;

    /// Create GPU buffers and upload data
    /// @param device WebGPU device
    /// @param queue WebGPU queue for data upload
    /// @param data CPU-side SVO data to upload
    /// @param uniforms SVO uniforms
    /// @param label Debug label prefix
    /// @return true on success, false on failure
    [[nodiscard]] bool create(WGPUDevice device, WGPUQueue queue,
                              const SVOBufferData& data,
                              const SVOUniforms& uniforms,
                              std::string_view label = "svo");

    /// Update just the uniform buffer (for camera/LOD changes)
    void updateUniforms(WGPUQueue queue, const SVOUniforms& uniforms);

    /// Update a range of brick data (for dynamic editing)
    /// @param queue WebGPU queue
    /// @param startBrick First brick index to update
    /// @param bricks Brick data to upload
    void updateBricks(WGPUQueue queue, uint32_t startBrick,
                      std::span<const SVOLeafBrick> bricks);

    /// Release all GPU resources
    void release();

    /// Check if buffers are valid
    [[nodiscard]] bool isValid() const noexcept { return uniformBuffer_ != nullptr; }

    /// Get uniform buffer
    [[nodiscard]] WGPUBuffer getUniformBuffer() const noexcept { return uniformBuffer_; }

    /// Get node masks buffer
    [[nodiscard]] WGPUBuffer getNodeMasksBuffer() const noexcept { return nodeMasksBuffer_; }

    /// Get node child pointers buffer
    [[nodiscard]] WGPUBuffer getNodeChildPtrsBuffer() const noexcept { return nodeChildPtrsBuffer_; }

    /// Get brick occupancy low buffer
    [[nodiscard]] WGPUBuffer getBrickOccupancyLoBuffer() const noexcept { return brickOccLoBuffer_; }

    /// Get brick occupancy high buffer
    [[nodiscard]] WGPUBuffer getBrickOccupancyHiBuffer() const noexcept { return brickOccHiBuffer_; }

    /// Get brick metadata buffer
    [[nodiscard]] WGPUBuffer getBrickMetaBuffer() const noexcept { return brickMetaBuffer_; }

    /// Get contour normals buffer (may be nullptr if not present)
    [[nodiscard]] WGPUBuffer getContourNormalsBuffer() const noexcept { return contourNormalsBuffer_; }

    /// Get node count
    [[nodiscard]] uint32_t getNodeCount() const noexcept { return nodeCount_; }

    /// Get brick count
    [[nodiscard]] uint32_t getBrickCount() const noexcept { return brickCount_; }

private:
    // GPU buffers
    WGPUBuffer uniformBuffer_ = nullptr;
    WGPUBuffer nodeMasksBuffer_ = nullptr;
    WGPUBuffer nodeChildPtrsBuffer_ = nullptr;
    WGPUBuffer brickOccLoBuffer_ = nullptr;
    WGPUBuffer brickOccHiBuffer_ = nullptr;
    WGPUBuffer brickMetaBuffer_ = nullptr;
    WGPUBuffer contourNormalsBuffer_ = nullptr;

    // Cached counts
    uint32_t nodeCount_ = 0;
    uint32_t brickCount_ = 0;

    /// Helper to create a storage buffer
    [[nodiscard]] static WGPUBuffer createStorageBuffer(WGPUDevice device, 
                                                         const void* data, 
                                                         size_t size,
                                                         std::string_view label);

    /// Helper to create a uniform buffer
    [[nodiscard]] static WGPUBuffer createUniformBuffer(WGPUDevice device,
                                                         const void* data,
                                                         size_t size,
                                                         std::string_view label);
};

// ─────────────────────────────────────────────────────────────────────────────
// Buffer Alignment Utilities
// ─────────────────────────────────────────────────────────────────────────────

/// WebGPU minimum uniform buffer alignment
constexpr size_t UNIFORM_BUFFER_ALIGNMENT = 256;

/// WebGPU minimum storage buffer alignment
constexpr size_t STORAGE_BUFFER_ALIGNMENT = 16;

/// Align size to WebGPU uniform buffer requirements
[[nodiscard]] constexpr size_t alignToUniformBuffer(size_t size) noexcept {
    return (size + UNIFORM_BUFFER_ALIGNMENT - 1) & ~(UNIFORM_BUFFER_ALIGNMENT - 1);
}

/// Align size to WebGPU storage buffer requirements
[[nodiscard]] constexpr size_t alignToStorageBuffer(size_t size) noexcept {
    return (size + STORAGE_BUFFER_ALIGNMENT - 1) & ~(STORAGE_BUFFER_ALIGNMENT - 1);
}

} // namespace voxy::voxel
