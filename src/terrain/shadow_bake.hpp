// ═══════════════════════════════════════════════════════════════════════════════
// shadow_bake.hpp - Static Sun Shadow Height Field Baking (C++20)
// ═══════════════════════════════════════════════════════════════════════════════
// The sun and the terrain are both static, so terrain self-shadowing can be
// precomputed once instead of ray-marched per pixel per frame.
//
// For every heightmap cell this bake stores the "shadow boundary": the height
// (in the 0-65535 heightmap encoding) below which a point standing at that
// cell is in shadow. A shader shadow test is then a single texture load and
// one compare, replacing the hierarchical shadow DDA.
//
// The boundary deliberately EXCLUDES the cell's own terrain height, so a
// point on the surface never shadows itself (no acne, no bias needed).
//
// Algorithm: scan-line sweep along the sun azimuth. Rows are processed from
// the sun side; each cell takes the previous row's boundary (sampled with
// linear interpolation at the sun-ray offset), lowered by the sun elevation
// drop per step, then raised by that cell's terrain for downstream rows.
// O(N) total.
// ═══════════════════════════════════════════════════════════════════════════════

#pragma once

#include <cstdint>
#include <span>
#include <vector>

#include <glm/glm.hpp>

namespace voxy::terrain {

/// Configuration for the shadow height field bake
struct ShadowBakeConfig {
    glm::vec3 lightDir = {0.3f, 0.8f, 0.4f}; ///< World-space direction toward the sun
    float heightScale = 500.0f;              ///< World-space height range (matches renderer)
    float cellScale = 1.0f;                  ///< World-space size per heightmap cell
    uint32_t downsample = 2;                 ///< Output resolution divisor (min-filtered)
};

/// Result of the shadow bake
struct ShadowBakeResult {
    std::vector<uint16_t> data; ///< Shadow boundary per cell, heightmap encoding
    uint32_t width = 0;         ///< Output width  (heightmap width  / downsample)
    uint32_t height = 0;        ///< Output height (heightmap height / downsample)
};

/// Bake the shadow boundary height field for a heightmap and a fixed sun.
/// @param heights Heightmap samples (16-bit, row-major)
/// @param width Heightmap width in samples
/// @param height Heightmap height in samples
/// @param config Bake configuration
/// @return Baked field; empty data if inputs are invalid
[[nodiscard]] ShadowBakeResult bakeShadowHeightField(std::span<const uint16_t> heights,
                                                     uint32_t width, uint32_t height,
                                                     const ShadowBakeConfig& config = {});

} // namespace voxy::terrain
