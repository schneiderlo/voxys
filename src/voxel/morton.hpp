// ═══════════════════════════════════════════════════════════════════════════════
// morton.hpp - Morton Code (Z-Order Curve) Utilities (C++20)
// ═══════════════════════════════════════════════════════════════════════════════
// Provides Morton code encoding/decoding for optimal spatial locality in the SVO.
// Morton codes interleave the bits of 3D coordinates to create a linear index
// that preserves spatial proximity, improving cache performance during traversal.
//
// Functions:
// - expandBits / compactBits: Bit manipulation primitives
// - morton3D / inverseMorton3D: Global 3D coordinates (10 bits each → 30 bits)
// - brickMorton / inverseBrickMorton: Brick-local coordinates (2 bits each → 6 bits)
// ═══════════════════════════════════════════════════════════════════════════════

#pragma once

#include <cstdint>
#include <glm/glm.hpp>

namespace voxy::voxel {

// ─────────────────────────────────────────────────────────────────────────────
// Bit Manipulation Primitives
// ─────────────────────────────────────────────────────────────────────────────

/// Expand 10 bits to 30 bits with 2 zeros between each bit
/// Input:   ........ ..XXXXXX XXXXXXXX XX
/// Output:  ..X..X.. X..X..X. .X..X..X ..X..X..
[[nodiscard]] constexpr uint32_t expandBits(uint32_t v) noexcept {
    // Spread bits using magic numbers
    v = (v * 0x00010001u) & 0xFF0000FFu;
    v = (v * 0x00000101u) & 0x0F00F00Fu;
    v = (v * 0x00000011u) & 0xC30C30C3u;
    v = (v * 0x00000005u) & 0x49249249u;
    return v;
}

/// Compact 30 bits to 10 bits (inverse of expandBits)
/// Input:   ..X..X.. X..X..X. .X..X..X ..X..X..
/// Output:  ........ ..XXXXXX XXXXXXXX XX
[[nodiscard]] constexpr uint32_t compactBits(uint32_t v) noexcept {
    v &= 0x49249249u;
    v = (v ^ (v >> 2)) & 0xC30C30C3u;
    v = (v ^ (v >> 4)) & 0x0F00F00Fu;
    v = (v ^ (v >> 8)) & 0xFF0000FFu;
    v = (v ^ (v >> 16)) & 0x000003FFu;
    return v;
}

// ─────────────────────────────────────────────────────────────────────────────
// Global Morton Codes (30-bit, for 1024³ coordinates)
// ─────────────────────────────────────────────────────────────────────────────

/// Encode 3D coordinate to 30-bit Morton code
/// Each coordinate can be 0-1023 (10 bits)
/// Result is a 30-bit value with interleaved bits: z2y2x2z1y1x1z0y0x0...
[[nodiscard]] constexpr uint32_t morton3D(uint32_t x, uint32_t y, uint32_t z) noexcept {
    return expandBits(x) | (expandBits(y) << 1) | (expandBits(z) << 2);
}

/// Decode 30-bit Morton code to 3D coordinate
/// Returns coordinates in range 0-1023 each
[[nodiscard]] constexpr glm::uvec3 inverseMorton3D(uint32_t code) noexcept {
    return {
        compactBits(code),
        compactBits(code >> 1),
        compactBits(code >> 2)
    };
}

/// Overload with separate output parameters
constexpr void inverseMorton3D(uint32_t code, uint32_t& x, uint32_t& y, uint32_t& z) noexcept {
    x = compactBits(code);
    y = compactBits(code >> 1);
    z = compactBits(code >> 2);
}

// ─────────────────────────────────────────────────────────────────────────────
// Brick-Local Morton Codes (6-bit, for 4×4×4 coordinates)
// ─────────────────────────────────────────────────────────────────────────────

/// Encode brick-local 3D coordinate to 6-bit Morton code
/// Each coordinate can be 0-3 (2 bits)
/// Result is a 6-bit value: z1y1x1z0y0x0
[[nodiscard]] constexpr uint32_t brickMorton(uint32_t x, uint32_t y, uint32_t z) noexcept {
    // For 2-bit coordinates (0-3), we interleave: z1y1x1z0y0x0
    return (x & 1u) | ((y & 1u) << 1u) | ((z & 1u) << 2u)
         | ((x & 2u) << 2u) | ((y & 2u) << 3u) | ((z & 2u) << 4u);
}

/// Decode 6-bit Morton code to brick-local 3D coordinate
/// Returns coordinates in range 0-3 each
[[nodiscard]] constexpr glm::uvec3 inverseBrickMorton(uint32_t code) noexcept {
    return {
        (code & 1u) | ((code >> 2) & 2u),
        ((code >> 1) & 1u) | ((code >> 3) & 2u),
        ((code >> 2) & 1u) | ((code >> 4) & 2u)
    };
}

/// Overload with glm::uvec3 input
[[nodiscard]] constexpr uint32_t brickMorton(const glm::uvec3& coord) noexcept {
    return brickMorton(coord.x, coord.y, coord.z);
}

// ─────────────────────────────────────────────────────────────────────────────
// 64-bit Morton Codes (for larger worlds)
// ─────────────────────────────────────────────────────────────────────────────

/// Expand 21 bits to 63 bits (for 64-bit Morton codes)
[[nodiscard]] constexpr uint64_t expandBits64(uint64_t v) noexcept {
    v = (v | (v << 32)) & 0x1F00000000FFFFull;
    v = (v | (v << 16)) & 0x1F0000FF0000FFull;
    v = (v | (v << 8))  & 0x100F00F00F00F00Full;
    v = (v | (v << 4))  & 0x10C30C30C30C30C3ull;
    v = (v | (v << 2))  & 0x1249249249249249ull;
    return v;
}

/// Compact 63 bits to 21 bits (inverse of expandBits64)
[[nodiscard]] constexpr uint64_t compactBits64(uint64_t v) noexcept {
    v &= 0x1249249249249249ull;
    v = (v ^ (v >> 2))  & 0x10C30C30C30C30C3ull;
    v = (v ^ (v >> 4))  & 0x100F00F00F00F00Full;
    v = (v ^ (v >> 8))  & 0x1F0000FF0000FFull;
    v = (v ^ (v >> 16)) & 0x1F00000000FFFFull;
    v = (v ^ (v >> 32)) & 0x1FFFFFull;
    return v;
}

/// Encode 3D coordinate to 63-bit Morton code
/// Each coordinate can be 0-2097151 (21 bits)
[[nodiscard]] constexpr uint64_t morton3D64(uint64_t x, uint64_t y, uint64_t z) noexcept {
    return expandBits64(x) | (expandBits64(y) << 1) | (expandBits64(z) << 2);
}

/// Decode 63-bit Morton code to 3D coordinate
[[nodiscard]] constexpr glm::u64vec3 inverseMorton3D64(uint64_t code) noexcept {
    return {
        compactBits64(code),
        compactBits64(code >> 1),
        compactBits64(code >> 2)
    };
}

// ─────────────────────────────────────────────────────────────────────────────
// Utility Functions
// ─────────────────────────────────────────────────────────────────────────────

/// Get the Morton code for a voxel within a brick at global coordinates
/// Global coordinates are divided by 4 to get brick coordinates,
/// and the remainder gives the local (0-3) coordinates within the brick
[[nodiscard]] constexpr uint32_t voxelMortonInBrick(uint32_t globalX, uint32_t globalY, uint32_t globalZ) noexcept {
    return brickMorton(globalX & 3u, globalY & 3u, globalZ & 3u);
}

/// Get the brick coordinates from global voxel coordinates
[[nodiscard]] constexpr glm::uvec3 globalToBrickCoord(uint32_t globalX, uint32_t globalY, uint32_t globalZ) noexcept {
    return {globalX >> 2, globalY >> 2, globalZ >> 2};
}

/// Get the brick coordinates from global voxel coordinates (vec3 version)
[[nodiscard]] constexpr glm::uvec3 globalToBrickCoord(const glm::uvec3& global) noexcept {
    return global >> 2u;
}

/// Get the local coordinates within a brick from global voxel coordinates
[[nodiscard]] constexpr glm::uvec3 globalToLocalCoord(uint32_t globalX, uint32_t globalY, uint32_t globalZ) noexcept {
    return {globalX & 3u, globalY & 3u, globalZ & 3u};
}

/// Get the local coordinates within a brick from global voxel coordinates (vec3 version)
[[nodiscard]] constexpr glm::uvec3 globalToLocalCoord(const glm::uvec3& global) noexcept {
    return global & 3u;
}

/// Convert brick coordinate and local coordinate back to global
[[nodiscard]] constexpr glm::uvec3 brickLocalToGlobal(const glm::uvec3& brickCoord, const glm::uvec3& local) noexcept {
    return (brickCoord << 2u) | local;
}

} // namespace voxy::voxel
