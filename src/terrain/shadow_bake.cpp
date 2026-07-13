// ═══════════════════════════════════════════════════════════════════════════════
// shadow_bake.cpp - Static Sun Shadow Height Field Baking Implementation
// ═══════════════════════════════════════════════════════════════════════════════

#include "terrain/shadow_bake.hpp"

#include <algorithm>
#include <cmath>

namespace voxy::terrain {

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

    ShadowBakeResult result;
    result.width = std::max(width / config.downsample, 1u);
    result.height = std::max(height / config.downsample, 1u);

    // Sun straight overhead (or below the horizon): nothing casts a shadow.
    if (horizontal < 1e-5f || light.y <= 0.0f) {
        result.data.resize(static_cast<size_t>(result.width) * result.height, 0u);
        return result;
    }

    // Fold each boundary value into its final min-filtered texel as soon as
    // the sweep produces it. This avoids a full-resolution float boundary
    // field and the second full-image downsample pass.
    result.data.resize(static_cast<size_t>(result.width) * result.height, 65535u);

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

    const uint32_t majorOutputCount = majorIsZ ? result.height : result.width;
    const uint32_t minorOutputCount = majorIsZ ? result.width : result.height;
    const uint32_t coveredMinor = static_cast<uint32_t>(std::min<size_t>(
        minorCount, static_cast<size_t>(minorOutputCount) * config.downsample));
    const auto outputIndex = [&](uint32_t outMajor, uint32_t outMinor) -> size_t {
        return majorIsZ
            ? static_cast<size_t>(outMajor) * result.width + outMinor
            : static_cast<size_t>(outMinor) * result.width + outMajor;
    };

    // Rows processed from the sun side toward the shadowed side.
    const bool forward = majorDir > 0.0f;

    // Running boundary INCLUDING terrain, used to propagate to the next row.
    std::vector<float> carry(minorCount);
    const uint32_t firstMajor = forward ? 0 : majorCount - 1;
    for (uint32_t minor = 0; minor < minorCount; ++minor) {
        carry[minor] = static_cast<float>(heights[cellIndex(firstMajor, minor)]);
    }
    const uint32_t firstOutMajor = firstMajor / config.downsample;
    if (firstOutMajor < majorOutputCount) {
        for (uint32_t outMinor = 0; outMinor < minorOutputCount; ++outMinor) {
            result.data[outputIndex(firstOutMajor, outMinor)] = 0u;
        }
    }

    std::vector<float> next(minorCount);
    result.scratchBytes = 2 * static_cast<size_t>(minorCount) * sizeof(float);
    for (uint32_t step = 1; step < majorCount; ++step) {
        const uint32_t major = forward ? step : majorCount - 1 - step;

        const auto sweepCell = [&](uint32_t minor) -> float {
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
            next[minor] = std::max(excl, static_cast<float>(heights[idx]));
            // carry is the max of terrain [0, 65535] and an already bounded
            // boundary. Interpolation preserves that range and drop is
            // positive, so excl is already in the old clamp's exact range.
            return excl;
        };

        const uint32_t outMajor = major / config.downsample;
        if (outMajor < majorOutputCount) {
            // Keep each block minimum in a register for this sweep row. This
            // avoids a division and output read/write for every input cell.
            for (uint32_t outMinor = 0; outMinor < minorOutputCount; ++outMinor) {
                auto& output = result.data[outputIndex(outMajor, outMinor)];
                const uint32_t firstMinor = outMinor * config.downsample;
                const uint32_t endMinor = static_cast<uint32_t>(std::min<size_t>(
                    minorCount, static_cast<size_t>(firstMinor) + config.downsample));
                float rowMinimum = 65535.0f;
                for (uint32_t minor = firstMinor; minor < endMinor; ++minor) {
                    rowMinimum = std::min(rowMinimum, sweepCell(minor));
                }
                output = std::min(output, static_cast<uint16_t>(rowMinimum));
            }
            // A floor-sized output intentionally omits a non-divisible tail,
            // but those cells must still participate in shadow propagation.
            for (uint32_t minor = coveredMinor; minor < minorCount; ++minor) {
                static_cast<void>(sweepCell(minor));
            }
        } else {
            for (uint32_t minor = 0; minor < minorCount; ++minor) {
                static_cast<void>(sweepCell(minor));
            }
        }
        carry.swap(next);
    }

    return result;
}

} // namespace voxy::terrain
