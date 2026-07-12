// ═══════════════════════════════════════════════════════════════════════════════
// svo_types.hpp - Sparse Voxel Octree Data Types (C++20)
// ═══════════════════════════════════════════════════════════════════════════════
// Defines the core data structures for the SVO terrain architecture:
// - SVOInteriorNode: 2×2×2 interior node (8 bytes)
// - SVOLeafBrick: 4×4×4 voxel brick with 64-bit occupancy mask (12 bytes)
// - SVOUniforms: GPU uniform buffer structure
// - Child ordering constants (Morton Z-order)
// ═══════════════════════════════════════════════════════════════════════════════

#pragma once

#include <cstdint>
#include <array>
#include <glm/glm.hpp>

namespace voxy::voxel {

// ─────────────────────────────────────────────────────────────────────────────
// Constants
// ─────────────────────────────────────────────────────────────────────────────

/// Brick dimension (4×4×4 voxels)
constexpr uint32_t BRICK_SIZE = 4;

/// Number of voxels per brick (4×4×4 = 64)
constexpr uint32_t VOXELS_PER_BRICK = BRICK_SIZE * BRICK_SIZE * BRICK_SIZE;

/// Number of children per interior node (2×2×2 = 8)
constexpr uint32_t CHILDREN_PER_NODE = 8;

/// Maximum depth of the SVO tree (e.g., 12 for 4096³ world)
constexpr uint32_t MAX_SVO_DEPTH = 16;

/// Maximum number of iterations for ray traversal
constexpr uint32_t MAX_TRAVERSAL_ITERATIONS = 256;

// ─────────────────────────────────────────────────────────────────────────────
// Child Ordering (Morton Z-order within 2×2×2)
// ─────────────────────────────────────────────────────────────────────────────
// Index = (z << 2) | (y << 1) | x
//   0: (0,0,0)  1: (1,0,0)  2: (0,1,0)  3: (1,1,0)
//   4: (0,0,1)  5: (1,0,1)  6: (0,1,1)  7: (1,1,1)

/// Get child index from 3D coordinates (each 0 or 1)
[[nodiscard]] constexpr uint8_t childIndex(uint32_t x, uint32_t y, uint32_t z) noexcept {
    return static_cast<uint8_t>((z << 2) | (y << 1) | x);
}

/// Get 3D coordinates from child index
[[nodiscard]] constexpr glm::uvec3 childCoord(uint8_t childIdx) noexcept {
    return {
        childIdx & 1u,
        (childIdx >> 1) & 1u,
        (childIdx >> 2) & 1u
    };
}

// ─────────────────────────────────────────────────────────────────────────────
// Leaf Brick Flags
// ─────────────────────────────────────────────────────────────────────────────

/// Flags for SVOLeafBrick
enum class BrickFlags : uint16_t {
    None        = 0,
    IsUniform   = 1 << 0,  ///< All voxels are same material (skip occupancy check)
    HasContour  = 1 << 1,  ///< Has contour normal for smooth surfaces
    IsDirty     = 1 << 2,  ///< Needs GPU buffer update
};

/// Bitwise OR for BrickFlags
[[nodiscard]] constexpr BrickFlags operator|(BrickFlags a, BrickFlags b) noexcept {
    return static_cast<BrickFlags>(static_cast<uint16_t>(a) | static_cast<uint16_t>(b));
}

/// Bitwise AND for BrickFlags
[[nodiscard]] constexpr BrickFlags operator&(BrickFlags a, BrickFlags b) noexcept {
    return static_cast<BrickFlags>(static_cast<uint16_t>(a) & static_cast<uint16_t>(b));
}

/// Check if flag is set
[[nodiscard]] constexpr bool hasFlag(BrickFlags flags, BrickFlags flag) noexcept {
    return (static_cast<uint16_t>(flags) & static_cast<uint16_t>(flag)) != 0;
}

// ─────────────────────────────────────────────────────────────────────────────
// Interior Node Structure
// ─────────────────────────────────────────────────────────────────────────────

/// Interior node: references 8 children (2×2×2)
/// Memory layout: 8 bytes, cache-aligned
struct SVOInteriorNode {
    uint8_t  childMask;     ///< 8 bits: which children exist (bit N = child N)
    uint8_t  leafMask;      ///< 8 bits: which children are leaves (vs interior)
    uint16_t parentIndex;   ///< Index to parent node (for backtracking, 0xFFFF = root)
    uint32_t childOffset;   ///< Index to first child in flat array

    /// Default constructor - empty node
    constexpr SVOInteriorNode() noexcept
        : childMask(0), leafMask(0), parentIndex(0xFFFF), childOffset(0) {}

    /// Full constructor
    constexpr SVOInteriorNode(uint8_t childMask_, uint8_t leafMask_, 
                               uint16_t parentIndex_, uint32_t childOffset_) noexcept
        : childMask(childMask_), leafMask(leafMask_), 
          parentIndex(parentIndex_), childOffset(childOffset_) {}

    /// Check if child exists at given index (0-7)
    [[nodiscard]] constexpr bool hasChild(uint8_t childIdx) const noexcept {
        return (childMask & (1u << childIdx)) != 0;
    }

    /// Check if child at given index is a leaf (vs interior)
    [[nodiscard]] constexpr bool isChildLeaf(uint8_t childIdx) const noexcept {
        return (leafMask & (1u << childIdx)) != 0;
    }

    /// Set child exists at given index
    constexpr void setChildExists(uint8_t childIdx, bool exists) noexcept {
        if (exists) {
            childMask |= (1u << childIdx);
        } else {
            childMask &= ~(1u << childIdx);
        }
    }

    /// Set whether child is a leaf
    constexpr void setChildIsLeaf(uint8_t childIdx, bool isLeaf) noexcept {
        if (isLeaf) {
            leafMask |= (1u << childIdx);
        } else {
            leafMask &= ~(1u << childIdx);
        }
    }

    /// Count number of existing children
    [[nodiscard]] constexpr uint32_t childCount() const noexcept {
        return static_cast<uint32_t>(__builtin_popcount(childMask));
    }

    /// Count children before given index (for offset calculation)
    [[nodiscard]] constexpr uint32_t childrenBefore(uint8_t childIdx) const noexcept {
        uint8_t mask = childMask & ((1u << childIdx) - 1);
        return static_cast<uint32_t>(__builtin_popcount(mask));
    }

    /// Get actual index of child at logical position childIdx
    [[nodiscard]] constexpr uint32_t getChildIndex(uint8_t childIdx) const noexcept {
        return childOffset + childrenBefore(childIdx);
    }

    /// Check if this is the root node
    [[nodiscard]] constexpr bool isRoot() const noexcept {
        return parentIndex == 0xFFFF;
    }

    /// Pack node data into a single uint32 for GPU (childMask | leafMask << 8 | parentIndex << 16)
    [[nodiscard]] constexpr uint32_t packMasks() const noexcept {
        return static_cast<uint32_t>(childMask) | 
               (static_cast<uint32_t>(leafMask) << 8) |
               (static_cast<uint32_t>(parentIndex) << 16);
    }

    /// Unpack masks from GPU format
    static constexpr SVOInteriorNode unpackMasks(uint32_t packed, uint32_t childPtr) noexcept {
        return SVOInteriorNode{
            static_cast<uint8_t>(packed & 0xFF),
            static_cast<uint8_t>((packed >> 8) & 0xFF),
            static_cast<uint16_t>(packed >> 16),
            childPtr
        };
    }
};

static_assert(sizeof(SVOInteriorNode) == 8, "SVOInteriorNode must be 8 bytes for alignment");

// ─────────────────────────────────────────────────────────────────────────────
// Leaf Brick Structure
// ─────────────────────────────────────────────────────────────────────────────

/// Leaf node: 4×4×4 voxel brick (64 voxels packed into 64 bits)
/// Memory layout: 16 bytes (8-byte aligned for uint64_t)
struct SVOLeafBrick {
    uint64_t occupancy;     ///< 1 bit per voxel (Morton Z-order), 64 total
    uint16_t materialBase;  ///< Base material index for this brick
    BrickFlags flags;       ///< IS_UNIFORM, HAS_CONTOUR, etc.
    uint32_t _pad;          ///< Padding for 16-byte alignment

    /// Default constructor - empty brick
    constexpr SVOLeafBrick() noexcept
        : occupancy(0), materialBase(0), flags(BrickFlags::None), _pad(0) {}

    /// Full constructor
    constexpr SVOLeafBrick(uint64_t occupancy_, uint16_t material_, BrickFlags flags_ = BrickFlags::None) noexcept
        : occupancy(occupancy_), materialBase(material_), flags(flags_), _pad(0) {}

    /// Check if voxel at Morton index is occupied
    [[nodiscard]] constexpr bool isOccupied(uint32_t mortonIdx) const noexcept {
        return (occupancy & (1ULL << mortonIdx)) != 0;
    }

    /// Set occupancy at Morton index
    constexpr void setOccupied(uint32_t mortonIdx, bool occupied) noexcept {
        if (occupied) {
            occupancy |= (1ULL << mortonIdx);
        } else {
            occupancy &= ~(1ULL << mortonIdx);
        }
    }

    /// Check if brick is completely empty
    [[nodiscard]] constexpr bool isEmpty() const noexcept {
        return occupancy == 0;
    }

    /// Check if brick is completely full
    [[nodiscard]] constexpr bool isFull() const noexcept {
        return occupancy == ~0ULL;
    }

    /// Count occupied voxels
    [[nodiscard]] constexpr uint32_t occupiedCount() const noexcept {
        return static_cast<uint32_t>(__builtin_popcountll(occupancy));
    }

    /// Get low 32 bits of occupancy (for GPU upload)
    [[nodiscard]] constexpr uint32_t occupancyLo() const noexcept {
        return static_cast<uint32_t>(occupancy & 0xFFFFFFFF);
    }

    /// Get high 32 bits of occupancy (for GPU upload)
    [[nodiscard]] constexpr uint32_t occupancyHi() const noexcept {
        return static_cast<uint32_t>(occupancy >> 32);
    }

    /// Pack metadata into single uint32 for GPU (materialBase | flags << 16)
    [[nodiscard]] constexpr uint32_t packMeta() const noexcept {
        return static_cast<uint32_t>(materialBase) | 
               (static_cast<uint32_t>(flags) << 16);
    }

    /// Reconstruct occupancy from lo/hi parts
    static constexpr uint64_t makeOccupancy(uint32_t lo, uint32_t hi) noexcept {
        return static_cast<uint64_t>(lo) | (static_cast<uint64_t>(hi) << 32);
    }
};

static_assert(sizeof(SVOLeafBrick) == 16, "SVOLeafBrick must be 16 bytes (8-byte aligned)");

// ─────────────────────────────────────────────────────────────────────────────
// GPU Uniforms Structure
// ─────────────────────────────────────────────────────────────────────────────

/// Uniform buffer data for SVO ray-casting shader
/// Must match WGSL struct layout (16-byte aligned)
struct SVOUniforms {
    uint32_t  rootNodeIndex;  ///< Index of root node
    uint32_t  maxDepth;       ///< Maximum tree depth (e.g., 12 for 4096³)
    float     worldScale;     ///< World units per root voxel
    float     brickScale;     ///< World units per brick voxel (worldScale / 2^maxDepth * 4)
    glm::vec3 worldOrigin;    ///< World-space origin of SVO volume
    float     _pad0;          ///< Padding for 16-byte alignment
    float     lodBias;        ///< LOD bias for distance-based termination (1.0-2.0)
    uint32_t  _pad1[3];       ///< Padding to 48 bytes

    /// Default constructor
    constexpr SVOUniforms() noexcept
        : rootNodeIndex(0), maxDepth(12), worldScale(4096.0f), brickScale(1.0f),
          worldOrigin(0.0f), _pad0(0.0f), lodBias(1.5f), _pad1{0, 0, 0} {}

    /// Full constructor
    constexpr SVOUniforms(uint32_t root, uint32_t depth, float scale, 
                           const glm::vec3& origin, float lod = 1.5f) noexcept
        : rootNodeIndex(root), maxDepth(depth), worldScale(scale),
          brickScale(scale / static_cast<float>(1u << depth) * BRICK_SIZE),
          worldOrigin(origin), _pad0(0.0f), lodBias(lod), _pad1{0, 0, 0} {}
};

static_assert(sizeof(SVOUniforms) == 48, "SVOUniforms must be 48 bytes (16-byte aligned)");

// ─────────────────────────────────────────────────────────────────────────────
// Optional Contour Normal (for smooth surfaces)
// ─────────────────────────────────────────────────────────────────────────────

/// Contour data for subvoxel surface refinement
struct ContourData {
    glm::vec3 normal;          ///< Average surface normal
    float     intersectOffset; ///< Subvoxel offset along normal

    constexpr ContourData() noexcept
        : normal(0.0f, 1.0f, 0.0f), intersectOffset(0.0f) {}

    constexpr ContourData(const glm::vec3& n, float offset) noexcept
        : normal(n), intersectOffset(offset) {}
};

static_assert(sizeof(ContourData) == 16, "ContourData must be 16 bytes");

// ─────────────────────────────────────────────────────────────────────────────
// Sparse Voxel (intermediate structure for voxelization)
// ─────────────────────────────────────────────────────────────────────────────

/// A single voxel with its 3D position and material
struct SparseVoxel {
    uint32_t x;         ///< X coordinate
    uint32_t y;         ///< Y coordinate (height)
    uint32_t z;         ///< Z coordinate
    uint16_t material;  ///< Material index

    constexpr SparseVoxel() noexcept
        : x(0), y(0), z(0), material(0) {}

    constexpr SparseVoxel(uint32_t x_, uint32_t y_, uint32_t z_, uint16_t mat = 0) noexcept
        : x(x_), y(y_), z(z_), material(mat) {}

    /// Comparison for sorting (by Morton code)
    [[nodiscard]] bool operator==(const SparseVoxel& other) const noexcept {
        return x == other.x && y == other.y && z == other.z;
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// Material Constants
// ─────────────────────────────────────────────────────────────────────────────

/// Common material indices
namespace Material {
    constexpr uint16_t Air   = 0;
    constexpr uint16_t Grass = 1;
    constexpr uint16_t Dirt  = 2;
    constexpr uint16_t Rock  = 3;
    constexpr uint16_t Sand  = 4;
    constexpr uint16_t Snow  = 5;
    constexpr uint16_t Water = 6;
}

} // namespace voxy::voxel
