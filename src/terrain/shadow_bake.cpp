// ═══════════════════════════════════════════════════════════════════════════════
// shadow_bake.cpp - Static Sun Shadow Height Field Baking Implementation
// ═══════════════════════════════════════════════════════════════════════════════

#include "terrain/shadow_bake.hpp"

#include <algorithm>
#include <cmath>

namespace voxy::terrain {

namespace {

/// Min-filter 2x2-style downsample by an arbitrary integer factor.
/// Min (not max) is used so any residual quantization errs toward light
/// leaks of under a cell instead of shadow acne on slopes.
ShadowBakeResult downsampleMin(const std::vector<float>& src,
                               uint32_t width, uint32_t height,
                               uint32_t factor) {
    ShadowBakeResult result;
    result.width = std::max(width / factor, 1u);
    result.height = std::max(height / factor, 1u);
    result.data.resize(static_cast<size_t>(result.width) * result.height);

    for (uint32_t oy = 0; oy < result.height; ++oy) {
        for (uint32_t ox = 0; ox < result.width; ++ox) {
            float minVal = 65535.0f;
            for (uint32_t sy = 0; sy < factor; ++sy) {
                const uint32_t y = std::min(oy * factor + sy, height - 1);
                for (uint32_t sx = 0; sx < factor; ++sx) {
                    const uint32_t x = std::min(ox * factor + sx, width - 1);
                    minVal = std::min(minVal, src[static_cast<size_t>(y) * width + x]);
                }
            }
            result.data[static_cast<size_t>(oy) * result.width + ox] =
                static_cast<uint16_t>(std::clamp(minVal, 0.0f, 65535.0f));
        }
    }
    return result;
}

} // namespace

ShadowBakeResult bakeShadowHeightField(std::span<const uint16_t> heights,
                                       uint32_t width, uint32_t height,
                                       const ShadowBakeConfig& config) {
    ShadowBakeResult empty;
    if (width == 0 || height == 0 ||
        heights.size() < static_cast<size_t>(width) * height ||
        !std::isfinite(config.heightScale) || config.heightScale <= 0.0f ||
        !std::isfinite(config.cellScale) || config.cellScale <= 0.0f ||
        config.downsample == 0) {
        return empty;
    }

    const float lightLength = glm::length(config.lightDir);
    if (!std::isfinite(lightLength) || lightLength < 1e-5f) {
        return empty;
    }

    const glm::vec3 light = config.lightDir / lightLength;
    const float horizontal = std::sqrt(light.x * light.x + light.z * light.z);

    std::vector<float> boundary(static_cast<size_t>(width) * height, 0.0f);

    // Sun straight overhead (or below the horizon): nothing casts a shadow.
    if (horizontal < 1e-5f || light.y <= 0.0f) {
        return downsampleMin(boundary, width, height, config.downsample);
    }

    // Shadow propagates opposite to the direction toward the sun.
    const float px = -light.x / horizontal;
    const float pz = -light.z / horizontal;
    const float tanElev = light.y / horizontal;

    // Sweep along the dominant propagation axis, one cell per step.
    // "major" is the swept axis, "minor" the perpendicular one.
    const bool majorIsZ = std::abs(pz) >= std::abs(px);
    const uint32_t majorCount = majorIsZ ? height : width;
    const uint32_t minorCount = majorIsZ ? width : height;
    const float majorDir = majorIsZ ? pz : px;
    // Minor-axis drift of the sun ray per major step, in cells.
    const float drift = (majorIsZ ? px : pz) / std::abs(majorDir);

    // Height the sun ray descends per major step, in heightmap units.
    const float stepWorld = config.cellScale * std::sqrt(1.0f + drift * drift);
    const float drop = tanElev * stepWorld * 65535.0f / (2.0f * config.heightScale);

    const auto cellIndex = [&](uint32_t major, uint32_t minor) -> size_t {
        return majorIsZ ? static_cast<size_t>(major) * width + minor
                        : static_cast<size_t>(minor) * width + major;
    };

    // Rows processed from the sun side toward the shadowed side.
    const bool forward = majorDir > 0.0f;

    // Running boundary INCLUDING terrain, used to propagate to the next row.
    std::vector<float> carry(minorCount);
    for (uint32_t minor = 0; minor < minorCount; ++minor) {
        carry[minor] = static_cast<float>(heights[cellIndex(forward ? 0 : majorCount - 1, minor)]);
    }

    std::vector<float> next(minorCount);
    for (uint32_t step = 1; step < majorCount; ++step) {
        const uint32_t major = forward ? step : majorCount - 1 - step;

        for (uint32_t minor = 0; minor < minorCount; ++minor) {
            // Where the sun ray through this cell crossed the previous row.
            const float up = std::clamp(static_cast<float>(minor) - drift,
                                        0.0f, static_cast<float>(minorCount - 1));
            const uint32_t i0 = static_cast<uint32_t>(up);
            const uint32_t i1 = std::min(i0 + 1, minorCount - 1);
            const float frac = up - static_cast<float>(i0);
            const float upstream = carry[i0] + (carry[i1] - carry[i0]) * frac;

            // Boundary excluding this cell's own terrain: no self-shadowing.
            const float excl = std::max(upstream - drop, 0.0f);
            const size_t idx = cellIndex(major, minor);
            boundary[idx] = excl;
            next[minor] = std::max(excl, static_cast<float>(heights[idx]));
        }
        carry.swap(next);
    }

    return downsampleMin(boundary, width, height, config.downsample);
}

} // namespace voxy::terrain
