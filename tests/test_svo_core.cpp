// ═══════════════════════════════════════════════════════════════════════════════
// test_svo_core.cpp - Unit tests for SVO Core Data Structures
// ═══════════════════════════════════════════════════════════════════════════════
// Tests Morton code utilities, SVO types (SVOInteriorNode, SVOLeafBrick), and
// SVO buffer data containers.
// ═══════════════════════════════════════════════════════════════════════════════

#include <gtest/gtest.h>

#include "voxel/morton.hpp"
#include "voxel/svo_types.hpp"

// Only include buffer utilities when full voxy_core is available (has GPU deps)
#ifdef VOXY_HAS_BUFFERS
#include "voxel/svo_buffers.hpp"
#endif

#include <algorithm>
#include <vector>
#include <random>

namespace voxy::voxel {

// ═══════════════════════════════════════════════════════════════════════════════
// Morton Code Tests
// ═══════════════════════════════════════════════════════════════════════════════

TEST(MortonTest, ExpandBitsBasic) {
    // expandBits(1) should give 1 (0b1 → 0b1)
    EXPECT_EQ(expandBits(0), 0u);
    EXPECT_EQ(expandBits(1), 1u);
    // expandBits(2) = 0b10 → 0b001000 = 8
    EXPECT_EQ(expandBits(2), 8u);
    // expandBits(3) = 0b11 → 0b001001 = 9
    EXPECT_EQ(expandBits(3), 9u);
}

TEST(MortonTest, CompactBitsBasic) {
    EXPECT_EQ(compactBits(0), 0u);
    EXPECT_EQ(compactBits(1), 1u);
    EXPECT_EQ(compactBits(8), 2u);
    EXPECT_EQ(compactBits(9), 3u);
}

TEST(MortonTest, ExpandCompactRoundtrip) {
    // Test roundtrip for various 10-bit values
    for (uint32_t i = 0; i < 1024; i += 7) {
        uint32_t expanded = expandBits(i);
        uint32_t compacted = compactBits(expanded);
        EXPECT_EQ(compacted, i) << "Failed for i = " << i;
    }
}

TEST(MortonTest, Morton3DOrigin) {
    // (0, 0, 0) should encode to 0
    EXPECT_EQ(morton3D(0, 0, 0), 0u);
}

TEST(MortonTest, Morton3DAxesIndependent) {
    // X bit pattern: expandBits(x)
    // Y bit pattern: expandBits(y) << 1
    // Z bit pattern: expandBits(z) << 2
    EXPECT_EQ(morton3D(1, 0, 0), 1u);
    EXPECT_EQ(morton3D(0, 1, 0), 2u);
    EXPECT_EQ(morton3D(0, 0, 1), 4u);
}

TEST(MortonTest, Morton3DCorner) {
    // (1, 1, 1) in child index terms = 0b111 = 7
    EXPECT_EQ(morton3D(1, 1, 1), 7u);
}

TEST(MortonTest, InverseMorton3DRoundtrip) {
    // Test roundtrip for various coordinates
    for (uint32_t x = 0; x < 64; x += 7) {
        for (uint32_t y = 0; y < 64; y += 7) {
            for (uint32_t z = 0; z < 64; z += 7) {
                uint32_t code = morton3D(x, y, z);
                glm::uvec3 result = inverseMorton3D(code);
                EXPECT_EQ(result.x, x);
                EXPECT_EQ(result.y, y);
                EXPECT_EQ(result.z, z);
            }
        }
    }
}

TEST(MortonTest, BrickMortonBasic) {
    // All corners of 4×4×4 brick
    EXPECT_EQ(brickMorton(0, 0, 0), 0u);
    EXPECT_EQ(brickMorton(1, 0, 0), 1u);
    EXPECT_EQ(brickMorton(0, 1, 0), 2u);
    EXPECT_EQ(brickMorton(1, 1, 0), 3u);
    EXPECT_EQ(brickMorton(0, 0, 1), 4u);
    EXPECT_EQ(brickMorton(3, 3, 3), 63u);  // Max value for 4×4×4
}

TEST(MortonTest, BrickMortonRange) {
    // All 64 voxels in a 4×4×4 brick should map to unique indices 0-63
    std::vector<bool> seen(64, false);
    for (uint32_t z = 0; z < 4; z++) {
        for (uint32_t y = 0; y < 4; y++) {
            for (uint32_t x = 0; x < 4; x++) {
                uint32_t idx = brickMorton(x, y, z);
                ASSERT_LT(idx, 64u) << "Index out of range at (" << x << "," << y << "," << z << ")";
                EXPECT_FALSE(seen[idx]) << "Duplicate index " << idx;
                seen[idx] = true;
            }
        }
    }
    // All 64 indices should be covered
    for (uint32_t i = 0; i < 64; i++) {
        EXPECT_TRUE(seen[i]) << "Missing index " << i;
    }
}

TEST(MortonTest, InverseBrickMortonRoundtrip) {
    for (uint32_t z = 0; z < 4; z++) {
        for (uint32_t y = 0; y < 4; y++) {
            for (uint32_t x = 0; x < 4; x++) {
                uint32_t code = brickMorton(x, y, z);
                glm::uvec3 result = inverseBrickMorton(code);
                EXPECT_EQ(result.x, x);
                EXPECT_EQ(result.y, y);
                EXPECT_EQ(result.z, z);
            }
        }
    }
}

TEST(MortonTest, GlobalToBrickAndLocal) {
    // Test conversion from global to brick + local coordinates
    glm::uvec3 global{17, 23, 45};  // Arbitrary point
    glm::uvec3 brick = globalToBrickCoord(global);
    glm::uvec3 local = globalToLocalCoord(global);
    
    EXPECT_EQ(brick, glm::uvec3(4, 5, 11));  // 17/4=4, 23/4=5, 45/4=11
    EXPECT_EQ(local, glm::uvec3(1, 3, 1));   // 17%4=1, 23%4=3, 45%4=1
    
    // Reconstruct global
    glm::uvec3 reconstructed = brickLocalToGlobal(brick, local);
    EXPECT_EQ(reconstructed, global);
}

TEST(MortonTest, VoxelMortonInBrick) {
    // voxelMortonInBrick should give same result as brickMorton with local coords
    uint32_t gx = 17, gy = 23, gz = 45;
    uint32_t expected = brickMorton(gx & 3, gy & 3, gz & 3);
    EXPECT_EQ(voxelMortonInBrick(gx, gy, gz), expected);
}

// ═══════════════════════════════════════════════════════════════════════════════
// SVOInteriorNode Tests
// ═══════════════════════════════════════════════════════════════════════════════

TEST(SVOInteriorNodeTest, DefaultConstruction) {
    SVOInteriorNode node;
    EXPECT_EQ(node.childMask, 0);
    EXPECT_EQ(node.leafMask, 0);
    EXPECT_EQ(node.parentIndex, 0xFFFF);  // Root marker
    EXPECT_EQ(node.childOffset, 0u);
    EXPECT_TRUE(node.isRoot());
}

TEST(SVOInteriorNodeTest, FullConstruction) {
    SVOInteriorNode node(0b10101010, 0b01010101, 100, 50);
    EXPECT_EQ(node.childMask, 0b10101010);
    EXPECT_EQ(node.leafMask, 0b01010101);
    EXPECT_EQ(node.parentIndex, 100);
    EXPECT_EQ(node.childOffset, 50u);
    EXPECT_FALSE(node.isRoot());
}

TEST(SVOInteriorNodeTest, HasChild) {
    SVOInteriorNode node(0b10101010, 0, 0, 0);
    EXPECT_FALSE(node.hasChild(0));
    EXPECT_TRUE(node.hasChild(1));
    EXPECT_FALSE(node.hasChild(2));
    EXPECT_TRUE(node.hasChild(3));
    EXPECT_FALSE(node.hasChild(4));
    EXPECT_TRUE(node.hasChild(5));
    EXPECT_FALSE(node.hasChild(6));
    EXPECT_TRUE(node.hasChild(7));
}

TEST(SVOInteriorNodeTest, IsChildLeaf) {
    SVOInteriorNode node(0xFF, 0b11110000, 0, 0);
    EXPECT_FALSE(node.isChildLeaf(0));
    EXPECT_FALSE(node.isChildLeaf(3));
    EXPECT_TRUE(node.isChildLeaf(4));
    EXPECT_TRUE(node.isChildLeaf(7));
}

TEST(SVOInteriorNodeTest, SetChildExists) {
    SVOInteriorNode node;
    node.setChildExists(3, true);
    node.setChildExists(7, true);
    EXPECT_EQ(node.childMask, 0b10001000);
    
    node.setChildExists(3, false);
    EXPECT_EQ(node.childMask, 0b10000000);
}

TEST(SVOInteriorNodeTest, ChildCount) {
    SVOInteriorNode node(0b10101010, 0, 0, 0);  // 4 children set
    EXPECT_EQ(node.childCount(), 4u);
    
    node.childMask = 0xFF;  // All 8 children
    EXPECT_EQ(node.childCount(), 8u);
    
    node.childMask = 0;
    EXPECT_EQ(node.childCount(), 0u);
}

TEST(SVOInteriorNodeTest, ChildrenBefore) {
    // Children at positions 1, 3, 5, 7 (mask = 0b10101010)
    SVOInteriorNode node(0b10101010, 0, 0, 100);
    
    EXPECT_EQ(node.childrenBefore(0), 0u);
    EXPECT_EQ(node.childrenBefore(1), 0u);
    EXPECT_EQ(node.childrenBefore(2), 1u);  // 1 child (at pos 1) before pos 2
    EXPECT_EQ(node.childrenBefore(3), 1u);
    EXPECT_EQ(node.childrenBefore(4), 2u);  // 2 children before pos 4
    EXPECT_EQ(node.childrenBefore(5), 2u);
    EXPECT_EQ(node.childrenBefore(6), 3u);
    EXPECT_EQ(node.childrenBefore(7), 3u);
}

TEST(SVOInteriorNodeTest, GetChildIndex) {
    SVOInteriorNode node(0b10101010, 0, 0, 100);  // Children at 1,3,5,7
    
    // Child at logical position 1 is the first child → offset 100
    EXPECT_EQ(node.getChildIndex(1), 100u);
    // Child at position 3 is second → offset 101
    EXPECT_EQ(node.getChildIndex(3), 101u);
    // Child at position 5 is third → offset 102
    EXPECT_EQ(node.getChildIndex(5), 102u);
    // Child at position 7 is fourth → offset 103
    EXPECT_EQ(node.getChildIndex(7), 103u);
}

TEST(SVOInteriorNodeTest, PackUnpackMasks) {
    SVOInteriorNode original(0xAB, 0xCD, 12345, 98765);
    
    uint32_t packed = original.packMasks();
    SVOInteriorNode unpacked = SVOInteriorNode::unpackMasks(packed, 98765);
    
    EXPECT_EQ(unpacked.childMask, original.childMask);
    EXPECT_EQ(unpacked.leafMask, original.leafMask);
    EXPECT_EQ(unpacked.parentIndex, original.parentIndex);
    EXPECT_EQ(unpacked.childOffset, original.childOffset);
}

TEST(SVOInteriorNodeTest, SizeIs8Bytes) {
    EXPECT_EQ(sizeof(SVOInteriorNode), 8u);
}

// ═══════════════════════════════════════════════════════════════════════════════
// SVOLeafBrick Tests
// ═══════════════════════════════════════════════════════════════════════════════

TEST(SVOLeafBrickTest, DefaultConstruction) {
    SVOLeafBrick brick;
    EXPECT_EQ(brick.occupancy, 0u);
    EXPECT_EQ(brick.materialBase, 0u);
    EXPECT_EQ(brick.flags, BrickFlags::None);
    EXPECT_TRUE(brick.isEmpty());
}

TEST(SVOLeafBrickTest, FullConstruction) {
    SVOLeafBrick brick(0xDEADBEEFCAFEBABEull, 42, BrickFlags::IsUniform);
    EXPECT_EQ(brick.occupancy, 0xDEADBEEFCAFEBABEull);
    EXPECT_EQ(brick.materialBase, 42);
    EXPECT_EQ(brick.flags, BrickFlags::IsUniform);
}

TEST(SVOLeafBrickTest, IsOccupied) {
    SVOLeafBrick brick(0b1010101010101010ull, 0);
    
    EXPECT_FALSE(brick.isOccupied(0));
    EXPECT_TRUE(brick.isOccupied(1));
    EXPECT_FALSE(brick.isOccupied(2));
    EXPECT_TRUE(brick.isOccupied(3));
}

TEST(SVOLeafBrickTest, SetOccupied) {
    SVOLeafBrick brick;
    
    brick.setOccupied(0, true);
    brick.setOccupied(63, true);
    
    EXPECT_TRUE(brick.isOccupied(0));
    EXPECT_TRUE(brick.isOccupied(63));
    EXPECT_FALSE(brick.isOccupied(1));
    EXPECT_FALSE(brick.isOccupied(32));
    
    brick.setOccupied(0, false);
    EXPECT_FALSE(brick.isOccupied(0));
}

TEST(SVOLeafBrickTest, IsEmptyAndIsFull) {
    SVOLeafBrick brick;
    EXPECT_TRUE(brick.isEmpty());
    EXPECT_FALSE(brick.isFull());
    
    brick.occupancy = ~0ULL;
    EXPECT_FALSE(brick.isEmpty());
    EXPECT_TRUE(brick.isFull());
    
    brick.occupancy = 1;
    EXPECT_FALSE(brick.isEmpty());
    EXPECT_FALSE(brick.isFull());
}

TEST(SVOLeafBrickTest, OccupiedCount) {
    SVOLeafBrick brick(0b1111000011110000ull, 0);
    EXPECT_EQ(brick.occupiedCount(), 8u);
    
    brick.occupancy = ~0ULL;
    EXPECT_EQ(brick.occupiedCount(), 64u);
    
    brick.occupancy = 0;
    EXPECT_EQ(brick.occupiedCount(), 0u);
}

TEST(SVOLeafBrickTest, OccupancyLoHi) {
    uint64_t occ = 0xDEADBEEFCAFEBABEull;
    SVOLeafBrick brick(occ, 0);
    
    EXPECT_EQ(brick.occupancyLo(), 0xCAFEBABEu);
    EXPECT_EQ(brick.occupancyHi(), 0xDEADBEEFu);
    
    // Reconstruct
    uint64_t reconstructed = SVOLeafBrick::makeOccupancy(brick.occupancyLo(), brick.occupancyHi());
    EXPECT_EQ(reconstructed, occ);
}

TEST(SVOLeafBrickTest, PackMeta) {
    SVOLeafBrick brick(0, 0x1234, BrickFlags::HasContour);
    
    uint32_t packed = brick.packMeta();
    EXPECT_EQ(packed & 0xFFFF, 0x1234u);
    EXPECT_EQ(packed >> 16, static_cast<uint16_t>(BrickFlags::HasContour));
}

TEST(SVOLeafBrickTest, SizeIs16Bytes) {
    EXPECT_EQ(sizeof(SVOLeafBrick), 16u);
}

// ═══════════════════════════════════════════════════════════════════════════════
// SVOUniforms Tests
// ═══════════════════════════════════════════════════════════════════════════════

TEST(SVOUniformsTest, DefaultConstruction) {
    SVOUniforms uniforms;
    EXPECT_EQ(uniforms.rootNodeIndex, 0u);
    EXPECT_EQ(uniforms.maxDepth, 12u);
    EXPECT_EQ(uniforms.worldScale, 4096.0f);
    EXPECT_EQ(uniforms.lodBias, 1.5f);
}

TEST(SVOUniformsTest, FullConstruction) {
    SVOUniforms uniforms(5, 10, 1024.0f, glm::vec3(100.0f, 200.0f, 300.0f), 2.0f);
    
    EXPECT_EQ(uniforms.rootNodeIndex, 5u);
    EXPECT_EQ(uniforms.maxDepth, 10u);
    EXPECT_EQ(uniforms.worldScale, 1024.0f);
    EXPECT_EQ(uniforms.worldOrigin, glm::vec3(100.0f, 200.0f, 300.0f));
    EXPECT_EQ(uniforms.lodBias, 2.0f);
    
    // Check brickScale calculation: worldScale / 2^maxDepth * BRICK_SIZE
    float expectedBrickScale = 1024.0f / static_cast<float>(1u << 10) * 4.0f;
    EXPECT_FLOAT_EQ(uniforms.brickScale, expectedBrickScale);
}

TEST(SVOUniformsTest, SizeIs48Bytes) {
    EXPECT_EQ(sizeof(SVOUniforms), 48u);
}

// ═══════════════════════════════════════════════════════════════════════════════
// BrickFlags Tests
// ═══════════════════════════════════════════════════════════════════════════════

TEST(BrickFlagsTest, BitwiseOr) {
    BrickFlags combined = BrickFlags::IsUniform | BrickFlags::HasContour;
    EXPECT_TRUE(hasFlag(combined, BrickFlags::IsUniform));
    EXPECT_TRUE(hasFlag(combined, BrickFlags::HasContour));
    EXPECT_FALSE(hasFlag(combined, BrickFlags::IsDirty));
}

TEST(BrickFlagsTest, BitwiseAnd) {
    BrickFlags combined = BrickFlags::IsUniform | BrickFlags::HasContour;
    EXPECT_EQ(combined & BrickFlags::IsUniform, BrickFlags::IsUniform);
    EXPECT_EQ(static_cast<uint16_t>(combined & BrickFlags::IsDirty), 0);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Child Index Tests
// ═══════════════════════════════════════════════════════════════════════════════

TEST(ChildIndexTest, AllCorners) {
    // Child ordering: (z << 2) | (y << 1) | x
    EXPECT_EQ(childIndex(0, 0, 0), 0);
    EXPECT_EQ(childIndex(1, 0, 0), 1);
    EXPECT_EQ(childIndex(0, 1, 0), 2);
    EXPECT_EQ(childIndex(1, 1, 0), 3);
    EXPECT_EQ(childIndex(0, 0, 1), 4);
    EXPECT_EQ(childIndex(1, 0, 1), 5);
    EXPECT_EQ(childIndex(0, 1, 1), 6);
    EXPECT_EQ(childIndex(1, 1, 1), 7);
}

TEST(ChildIndexTest, ChildCoordRoundtrip) {
    for (uint8_t idx = 0; idx < 8; idx++) {
        glm::uvec3 coord = childCoord(idx);
        EXPECT_EQ(childIndex(coord.x, coord.y, coord.z), idx);
    }
}

// ═══════════════════════════════════════════════════════════════════════════════
// SVOBufferData Tests (requires svo_buffers.hpp - GPU deps)
// ═══════════════════════════════════════════════════════════════════════════════

#ifdef VOXY_HAS_BUFFERS

TEST(SVOBufferDataTest, DefaultConstruction) {
    SVOBufferData data;
    EXPECT_EQ(data.nodeCount, 0u);
    EXPECT_EQ(data.brickCount, 0u);
    EXPECT_TRUE(data.nodeMasks.empty());
    EXPECT_TRUE(data.brickOccupancyLo.empty());
}

TEST(SVOBufferDataTest, AddNode) {
    SVOBufferData data;
    
    SVOInteriorNode node(0xFF, 0x0F, 100, 50);
    uint32_t idx = data.addNode(node);
    
    EXPECT_EQ(idx, 0u);
    EXPECT_EQ(data.nodeCount, 1u);
    EXPECT_EQ(data.nodeMasks.size(), 1u);
    EXPECT_EQ(data.nodeChildPtrs.size(), 1u);
    
    // Verify packing
    SVOInteriorNode retrieved = data.getNode(0);
    EXPECT_EQ(retrieved.childMask, node.childMask);
    EXPECT_EQ(retrieved.leafMask, node.leafMask);
    EXPECT_EQ(retrieved.parentIndex, node.parentIndex);
    EXPECT_EQ(retrieved.childOffset, node.childOffset);
}

TEST(SVOBufferDataTest, AddBrick) {
    SVOBufferData data;
    
    SVOLeafBrick brick(0xCAFEBABEDEADBEEFull, 42, BrickFlags::IsUniform);
    uint32_t idx = data.addBrick(brick);
    
    EXPECT_EQ(idx, 0u);
    EXPECT_EQ(data.brickCount, 1u);
    
    SVOLeafBrick retrieved = data.getBrick(0);
    EXPECT_EQ(retrieved.occupancy, brick.occupancy);
    EXPECT_EQ(retrieved.materialBase, brick.materialBase);
    EXPECT_EQ(retrieved.flags, brick.flags);
}

TEST(SVOBufferDataTest, AddBrickWithContour) {
    SVOBufferData data;
    
    SVOLeafBrick brick(1, 1);
    ContourData contour(glm::vec3(0.0f, 1.0f, 0.0f), 0.5f);
    
    data.addBrick(brick, &contour);
    
    EXPECT_TRUE(data.hasContours());
    EXPECT_EQ(data.contourNormals.size(), 1u);
    EXPECT_EQ(data.contourNormals[0].xyz(), contour.normal);
    EXPECT_EQ(data.contourNormals[0].w, contour.intersectOffset);
}

TEST(SVOBufferDataTest, Clear) {
    SVOBufferData data;
    data.addNode(SVOInteriorNode(1, 1, 1, 1));
    data.addBrick(SVOLeafBrick(1, 1));
    
    data.clear();
    
    EXPECT_EQ(data.nodeCount, 0u);
    EXPECT_EQ(data.brickCount, 0u);
    EXPECT_TRUE(data.nodeMasks.empty());
    EXPECT_TRUE(data.brickOccupancyLo.empty());
}

TEST(SVOBufferDataTest, MemoryUsage) {
    SVOBufferData data;
    
    // Add 10 nodes and 20 bricks
    for (int i = 0; i < 10; i++) {
        data.addNode(SVOInteriorNode());
    }
    for (int i = 0; i < 20; i++) {
        data.addBrick(SVOLeafBrick());
    }
    
    // Expected: 10*4 + 10*4 (node arrays) + 20*4*3 (brick arrays) = 80 + 240 = 320, no contours
    size_t expected = 10 * sizeof(uint32_t) * 2 +  // nodeMasks + nodeChildPtrs
                      20 * sizeof(uint32_t) * 3;   // brickOccLo/Hi + brickMeta
    EXPECT_EQ(data.memoryUsage(), expected);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Alignment Tests
// ═══════════════════════════════════════════════════════════════════════════════

TEST(AlignmentTest, UniformBufferAlignment) {
    EXPECT_EQ(alignToUniformBuffer(1), 256u);
    EXPECT_EQ(alignToUniformBuffer(256), 256u);
    EXPECT_EQ(alignToUniformBuffer(257), 512u);
    EXPECT_EQ(alignToUniformBuffer(1000), 1024u);
}

TEST(AlignmentTest, StorageBufferAlignment) {
    EXPECT_EQ(alignToStorageBuffer(1), 16u);
    EXPECT_EQ(alignToStorageBuffer(16), 16u);
    EXPECT_EQ(alignToStorageBuffer(17), 32u);
    EXPECT_EQ(alignToStorageBuffer(100), 112u);
}

#endif // VOXY_HAS_BUFFERS

// ═══════════════════════════════════════════════════════════════════════════════
// 64-bit Morton Tests (for larger worlds)
// ═══════════════════════════════════════════════════════════════════════════════

TEST(Morton64Test, ExpandCompact64Roundtrip) {
    // Test roundtrip for various 21-bit values
    std::mt19937_64 rng(42);
    for (int i = 0; i < 100; i++) {
        uint64_t val = rng() & 0x1FFFFF;  // 21-bit max
        uint64_t expanded = expandBits64(val);
        uint64_t compacted = compactBits64(expanded);
        EXPECT_EQ(compacted, val) << "Failed for value = " << val;
    }
}

TEST(Morton64Test, Morton3D64Roundtrip) {
    std::mt19937 rng(42);
    for (int i = 0; i < 100; i++) {
        uint64_t x = rng() % 2048;  // Use smaller values for faster test
        uint64_t y = rng() % 2048;
        uint64_t z = rng() % 2048;
        
        uint64_t code = morton3D64(x, y, z);
        glm::u64vec3 result = inverseMorton3D64(code);
        
        EXPECT_EQ(result.x, x);
        EXPECT_EQ(result.y, y);
        EXPECT_EQ(result.z, z);
    }
}

} // namespace voxy::voxel
