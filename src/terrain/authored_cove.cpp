#include "terrain/authored_cove.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <vector>

#include <glm/geometric.hpp>

namespace voxy::terrain {
namespace {

constexpr float kMinimumScale = 1.0e-5f;

float saturate(float value) noexcept {
    return std::clamp(value, 0.0f, 1.0f);
}

float smoothstep(float edge0, float edge1, float value) noexcept {
    if (!(edge1 > edge0)) return value >= edge1 ? 1.0f : 0.0f;
    const float t = saturate((value - edge0) / (edge1 - edge0));
    return t * t * (3.0f - 2.0f * t);
}

float gaussian(float value, float radius) noexcept {
    const float normalized = value / std::max(radius, kMinimumScale);
    return std::exp(-normalized * normalized);
}

float superGaussian(float value, float radius) noexcept {
    const float normalized = value / std::max(radius, kMinimumScale);
    const float squared = normalized * normalized;
    return std::exp(-squared * squared);
}

struct LocalCove {
    float inland = 0.0f;
    float alongshore = 0.0f;
    float shoreDistance = 0.0f;
};

LocalCove toLocal(
    glm::vec2 worldPosition,
    const AuthoredCoveConfig& config) noexcept {
    const glm::vec2 offset = worldPosition - config.center;
    const float inland = glm::dot(offset, config.inlandNormal);
    const float alongshore = glm::dot(
        offset, config.alongshoreTangent);

    // The middle is cut inland while two horns remain seaward. Two
    // incommensurate low-amplitude terms keep the waterline from reading as a
    // perfect spline or a constant-width contour.
    const float bayCut = 27.0f * superGaussian(alongshore, 30.0f);
    const float erosion =
        1.45f * std::sin(alongshore * 0.157f + 0.35f) +
        0.62f * std::sin(alongshore * 0.391f - 0.80f);
    return {
        inland,
        alongshore,
        inland - bayCut - erosion,
    };
}

float authoredBlend(
    const LocalCove& local,
    const AuthoredCoveConfig& config) noexcept {
    // A 100 m diameter core avoids advertising the authoring footprint as a
    // rotated square. The surrounding annulus only restores continuity with
    // the source tile.
    const float outside =
        std::sqrt(local.inland * local.inland +
                  local.alongshore * local.alongshore) -
        config.authoredHalfExtent;
    return 1.0f - smoothstep(0.0f, config.feather, outside);
}

float baseShoreProfile(
    float shoreDistance,
    float waterHeight) noexcept {
    if (shoreDistance < 0.0f) {
        const float offshore = -shoreDistance;
        // Shallow swash shelf, then a steeper subtidal channel.
        const float swash = std::min(offshore, 11.0f) * 0.095f;
        const float shelf =
            std::clamp(offshore - 11.0f, 0.0f, 24.0f) * 0.145f;
        const float channel =
            std::max(offshore - 35.0f, 0.0f) * 0.225f;
        return waterHeight - swash - shelf - channel;
    }

    const float foreshore = std::min(shoreDistance, 12.0f) * 0.085f;
    const float berm =
        1.35f * smoothstep(7.0f, 15.0f, shoreDistance);
    const float backshore =
        std::clamp(shoreDistance - 12.0f, 0.0f, 19.0f) * 0.13f;
    const float bluff =
        6.0f * smoothstep(23.0f, 45.0f, shoreDistance);
    return waterHeight + foreshore + berm + backshore + bluff;
}

float drainageAndErosion(const LocalCove& local) noexcept {
    const float landMask =
        smoothstep(9.0f, 18.0f, local.shoreDistance) *
        (1.0f - smoothstep(
            57.0f, 68.0f, local.shoreDistance));
    const float drainageCenter =
        13.0f + 3.2f *
        std::sin(local.shoreDistance * 0.105f);
    const float drainage =
        2.35f * gaussian(
            local.alongshore - drainageCenter, 3.6f) *
        landMask;

    constexpr std::array<float, 3> cutCenters{
        -10.0f, 27.0f, 41.0f};
    float cuts = 0.0f;
    for (size_t index = 0; index < cutCenters.size(); ++index) {
        const float center = cutCenters[index] +
            1.4f * std::sin(
                local.shoreDistance *
                (0.13f + 0.017f * static_cast<float>(index)));
        cuts += (0.72f + 0.18f * static_cast<float>(index)) *
                gaussian(local.alongshore - center, 4.2f) *
                smoothstep(
                    18.0f, 27.0f, local.shoreDistance) *
                (1.0f - smoothstep(
                    56.0f, 66.0f, local.shoreDistance));
    }

    // Two narrow tributaries join the main wash instead of ending as isolated
    // dimples. Their moving centers make the drainage read as erosion through
    // the bluff, not as heightfield-aligned grooves.
    const float tributaryMask =
        smoothstep(27.0f, 35.0f, local.shoreDistance) *
        (1.0f - smoothstep(
            57.0f, 66.0f, local.shoreDistance));
    const float tributaryA =
        0.82f * gaussian(
            local.alongshore -
                (26.0f - 0.33f * (local.shoreDistance - 31.0f)),
            2.5f) * tributaryMask;
    const float tributaryB =
        0.64f * gaussian(
            local.alongshore -
                (-4.0f + 0.29f * (local.shoreDistance - 31.0f)),
            2.2f) * tributaryMask;
    return drainage + cuts + tributaryA + tributaryB;
}

float debrisRelief(const LocalCove& local) noexcept {
    struct Debris {
        float inland;
        float alongshore;
        float inlandRadius;
        float alongshoreRadius;
        float cosine;
        float sine;
        float height;
    };
    constexpr std::array<Debris, 4> debris{{
        {-63.0f,   8.0f, 4.4f, 2.5f,  0.906f,  0.423f, 2.80f},
        {-58.0f, -11.0f, 5.2f, 2.9f,  0.731f, -0.682f, 3.15f},
        {-51.0f,  16.0f, 3.9f, 2.0f,  0.500f,  0.866f, 2.25f},
        {-47.0f, -21.0f, 3.1f, 1.8f,  0.819f,  0.574f, 1.75f},
    }};

    float relief = 0.0f;
    for (const Debris& piece : debris) {
        const float dx = local.inland - piece.inland;
        const float dz =
            local.alongshore - piece.alongshore;
        const float rotatedInland =
            dx * piece.cosine + dz * piece.sine;
        const float rotatedAlongshore =
            -dx * piece.sine + dz * piece.cosine;
        const float normalizedInland =
            rotatedInland / piece.inlandRadius;
        const float normalizedAlongshore =
            rotatedAlongshore / piece.alongshoreRadius;
        const float radiusSquared =
            normalizedInland * normalizedInland +
            normalizedAlongshore * normalizedAlongshore;
        const float facet =
            0.90f + 0.10f *
            std::sin(rotatedInland * 1.37f +
                     rotatedAlongshore * 0.91f);
        relief += piece.height *
            std::exp(-radiusSquared) * facet;
    }
    return relief;
}

float shoreRockRelief(const LocalCove& local) noexcept {
    struct Outcrop {
        float inland;
        float alongshore;
        float inlandRadius;
        float alongshoreRadius;
        float cosine;
        float sine;
        float height;
    };
    constexpr std::array<Outcrop, 2> outcrops{{
        {18.0f, -47.0f, 27.0f, 17.0f,  0.906f,  0.423f, 0.9f},
        {19.0f,  47.0f, 24.0f, 13.0f,  0.766f, -0.643f, 1.2f},
    }};

    float relief = 0.0f;
    for (size_t index = 0; index < outcrops.size(); ++index) {
        const Outcrop& outcrop = outcrops[index];
        const float dx = local.inland - outcrop.inland;
        const float dz = local.alongshore - outcrop.alongshore;
        const float rotatedInland =
            dx * outcrop.cosine + dz * outcrop.sine;
        const float rotatedAlongshore =
            -dx * outcrop.sine + dz * outcrop.cosine;
        const float radiusSquared =
            std::pow(rotatedInland / outcrop.inlandRadius, 2.0f) +
            std::pow(rotatedAlongshore / outcrop.alongshoreRadius, 2.0f);
        const float mound =
            std::pow(std::max(1.0f - radiusSquared, 0.0f), 1.35f);
        const float indexValue = static_cast<float>(index);
        const float facets =
            0.88f +
            0.08f * std::sin(
                rotatedInland * (1.21f + 0.11f * indexValue)) +
            0.06f * std::sin(
                rotatedAlongshore * (1.73f - 0.09f * indexValue));
        relief = std::max(
            relief,
            outcrop.height * mound * std::max(facets, 0.68f));
    }
    return relief;
}

float shoreMesoRelief(const LocalCove& local) noexcept {
    const float shoreMask =
        smoothstep(-3.0f, 2.0f, local.shoreDistance) *
        (1.0f - smoothstep(
            31.0f, 43.0f, local.shoreDistance));
    const float broad =
        0.62f *
            std::sin(
                local.inland * 0.173f +
                std::sin(local.alongshore * 0.089f) * 1.25f) *
            std::sin(local.alongshore * 0.137f - 0.4f) +
        0.31f *
            std::sin(
                local.inland * 0.311f -
                local.alongshore * 0.227f + 1.1f);

    constexpr std::array<float, 3> channelCenters{
        -18.0f, 7.0f, 26.0f};
    float channels = 0.0f;
    for (size_t index = 0; index < channelCenters.size(); ++index) {
        const float center =
            channelCenters[index] +
            1.8f * std::sin(
                local.shoreDistance *
                    (0.115f + 0.021f * static_cast<float>(index)) +
                static_cast<float>(index));
        channels +=
            (0.58f + 0.16f * static_cast<float>(index)) *
            gaussian(
                local.alongshore - center,
                1.55f + 0.24f * static_cast<float>(index));
    }
    return (broad - channels) * shoreMask;
}

float cliffBenchRelief(const LocalCove& local) noexcept {
    // Two offset benches expose short rock faces on the cove shoulders.
    // Their inland limits and heights differ, preventing a single contour
    // stripe from running across the full authored patch.
    const float leftMask =
        gaussian(local.alongshore + 30.0f, 12.5f);
    const float leftFront =
        15.5f +
        1.7f * std::sin(local.alongshore * 0.17f);
    const float leftLower =
        smoothstep(leftFront, leftFront + 5.2f, local.inland) *
        (1.0f - smoothstep(45.0f, 51.0f, local.inland));
    const float leftUpper =
        smoothstep(
            leftFront + 8.5f, leftFront + 12.2f,
            local.inland) *
        (1.0f - smoothstep(39.0f, 46.0f, local.inland));

    const float rightMask =
        gaussian(local.alongshore - 29.0f, 10.8f);
    const float rightFront =
        19.0f +
        2.1f * std::sin(
            local.alongshore * 0.14f + 1.2f);
    const float rightLower =
        smoothstep(rightFront, rightFront + 5.8f, local.inland) *
        (1.0f - smoothstep(47.0f, 54.0f, local.inland));
    const float rightUpper =
        smoothstep(
            rightFront + 10.0f, rightFront + 14.1f,
            local.inland) *
        (1.0f - smoothstep(42.0f, 49.0f, local.inland));

    const float ledgeMask =
        smoothstep(14.0f, 22.0f, local.inland) *
        (1.0f - smoothstep(48.0f, 58.0f, local.inland)) *
        std::max(leftMask, rightMask);
    const float ledgeFacets =
        0.09f *
        std::atan(
            3.2f *
            std::sin(
                local.inland * 0.79f +
                local.alongshore * 0.083f)) /
        std::atan(3.2f) * ledgeMask;
    return leftMask *
               (0.72f * leftLower + 0.38f * leftUpper) +
           rightMask *
               (0.64f * rightLower + 0.32f * rightUpper) +
           ledgeFacets;
}

float landBoulderRelief(const LocalCove& local) noexcept {
    struct Boulder {
        float inland;
        float alongshore;
        float inlandRadius;
        float alongshoreRadius;
        float cosine;
        float sine;
        float height;
    };
    // Unequal paired groups frame the beach without filling its central
    // gameplay lane. A sub-linear cap produces a harder shoulder than a
    // Gaussian mound, while the skewed facet term avoids round domes.
    constexpr std::array<Boulder, 6> boulders{{
        {11.0f, -30.0f, 5.4f, 3.7f,  0.819f,  0.574f, 1.48f},
        {15.5f, -24.5f, 3.9f, 2.8f,  0.500f, -0.866f, 1.08f},
        {27.0f, -34.0f, 4.9f, 3.2f,  0.906f,  0.423f, 1.72f},
        {19.0f,  27.0f, 5.7f, 3.5f,  0.766f, -0.643f, 1.62f},
        {25.5f,  32.5f, 3.8f, 2.6f,  0.574f,  0.819f, 1.15f},
        {34.0f,  23.0f, 4.5f, 3.0f,  0.866f, -0.500f, 1.38f},
    }};

    float relief = 0.0f;
    for (size_t index = 0; index < boulders.size(); ++index) {
        const Boulder& boulder = boulders[index];
        const float dx = local.inland - boulder.inland;
        const float dz =
            local.alongshore - boulder.alongshore;
        const float rotatedInland =
            dx * boulder.cosine + dz * boulder.sine;
        const float rotatedAlongshore =
            -dx * boulder.sine + dz * boulder.cosine;
        const float radiusSquared =
            std::pow(
                rotatedInland / boulder.inlandRadius, 2.0f) +
            std::pow(
                rotatedAlongshore / boulder.alongshoreRadius, 2.0f);
        const float cap = std::pow(
            std::max(1.0f - radiusSquared, 0.0f), 0.72f);
        const float facets =
            0.86f +
            0.10f * std::sin(
                rotatedInland *
                    (1.47f + 0.07f * static_cast<float>(index))) +
            0.08f * std::sin(
                rotatedAlongshore *
                    (1.91f - 0.05f * static_cast<float>(index)));
        relief = std::max(
            relief,
            boulder.height * cap * std::max(facets, 0.62f));
    }
    return relief;
}

float wreckSandDrift(const LocalCove& local) noexcept {
    // The hull is centred at approximately (20.2, -5.7) in cove space.
    // A smooth landward berm overlaps the curved lower side while the seaward
    // face remains in the swash. The low central skirt only closes the keel
    // contact; it must not raise the whole beach.
    const float hullInland = local.inland - 20.2f;
    const float hullAlongshore = local.alongshore + 5.73f;
    const float broad =
        std::exp(
            -std::pow(hullInland / 5.0f, 2.0f) -
            std::pow(hullAlongshore / 14.0f, 4.0f));
    const float lee =
        std::exp(
            -std::pow((hullInland - 2.6f) / 3.6f, 2.0f) -
            std::pow(hullAlongshore / 10.0f, 2.0f));
    const float landwardBurial =
        std::exp(
            -std::pow((hullInland - 4.6f) / 6.2f, 2.0f) -
            std::pow(hullAlongshore / 15.5f, 4.0f));
    const float brokenEdge =
        0.94f +
        0.06f *
        std::sin(
            hullAlongshore * 0.43f +
            std::sin(hullInland * 0.37f));
    return (0.25f * broad +
            0.12f * lee +
            3.20f * landwardBurial) * brokenEdge;
}

float heroWashCut(const LocalCove& local) noexcept {
    const float center =
        7.0f +
        4.1f * std::sin((local.inland - 12.0f) * 0.086f) +
        1.2f * std::sin((local.inland + 9.0f) * 0.217f);
    const float lengthMask =
        smoothstep(9.0f, 18.0f, local.inland) *
        (1.0f - smoothstep(57.0f, 68.0f, local.inland));
    const float width =
        4.8f + 0.75f * std::sin(local.inland * 0.19f);
    return 1.42f *
        gaussian(local.alongshore - center, width) *
        lengthMask;
}

float encodedToWorld(
    uint16_t encoded,
    float heightScale) noexcept {
    return ((static_cast<float>(encoded) / 65535.0f) *
            2.0f - 1.0f) * heightScale;
}

uint16_t worldToEncoded(
    float worldHeight,
    float heightScale) noexcept {
    const float normalized =
        saturate(worldHeight / (2.0f * heightScale) + 0.5f);
    return static_cast<uint16_t>(
        std::lround(normalized * 65535.0f));
}

} // namespace

CoveMaterialWeights classifyCoveMaterial(
    float relativeElevation,
    float upFacing,
    float authoredRockCue) noexcept {
    const float up = saturate(upFacing);
    const float steepness = 1.0f - up;
    float rock = std::max(
        smoothstep(0.12f, 0.43f, steepness),
        saturate(authoredRockCue));
    const float beach =
        (1.0f - smoothstep(
            2.8f, 5.2f, relativeElevation)) *
        smoothstep(0.48f, 0.82f, up) *
        (1.0f - rock);
    const float upland =
        std::max(1.0f - rock - beach, 0.0f);
    float grass = upland *
        smoothstep(0.78f, 0.91f, up) *
        smoothstep(8.0f, 14.0f, relativeElevation);
    float soil = std::max(upland - grass, 0.0f);
    float sand = beach;

    const float total =
        std::max(sand + soil + grass + rock, kMinimumScale);
    sand /= total;
    soil /= total;
    grass /= total;
    rock /= total;
    const float wetHistory =
        (1.0f - smoothstep(
            0.55f, 2.8f, relativeElevation)) *
        sand;
    return {sand, soil, grass, rock, wetHistory};
}

AuthoredCoveSample sampleAuthoredCove(
    glm::vec2 worldPosition,
    const AuthoredCoveConfig& config) noexcept {
    const LocalCove local =
        toLocal(worldPosition, config);
    float height = baseShoreProfile(
        local.shoreDistance, config.waterHeight);
    height -= drainageAndErosion(local);
    height += debrisRelief(local);
    height += cliffBenchRelief(local);
    height += landBoulderRelief(local);
    height += wreckSandDrift(local);

    // Low non-directional relief breaks a mathematically perfect profile
    // without restoring the source tile's diagonal one-metre noise.
    const float relief =
        std::sin(local.inland * 0.119f + 0.7f) *
        std::sin(local.alongshore * 0.173f - 0.2f);
    const float reliefMask =
        smoothstep(-25.0f, -4.0f, local.shoreDistance) *
        (1.0f - smoothstep(
            48.0f, 62.0f, local.shoreDistance));
    height += relief * reliefMask * 0.22f;

    const CoveMaterialWeights material =
        classifyCoveMaterial(
            height - config.waterHeight, 0.94f);
    return {
        height,
        local.shoreDistance,
        authoredBlend(local, config),
        material,
    };
}

bool applyAuthoredCove(
    std::span<uint16_t> heights,
    uint32_t width,
    uint32_t height,
    float heightScale,
    float cellScale,
    const AuthoredCoveConfig& config,
    AuthoredCoveStats* stats) noexcept {
    if (stats) *stats = {};
    if (width < 2u || height < 2u ||
        width > static_cast<uint32_t>(std::numeric_limits<int32_t>::max()) ||
        height > static_cast<uint32_t>(std::numeric_limits<int32_t>::max()) ||
        static_cast<size_t>(width) >
            std::numeric_limits<size_t>::max() / height ||
        heights.size() !=
            static_cast<size_t>(width) * height ||
        !std::isfinite(heightScale) || heightScale <= 0.0f ||
        !std::isfinite(cellScale) || cellScale <= 0.0f ||
        !std::isfinite(config.waterHeight) ||
        !std::isfinite(config.authoredHalfExtent) ||
        !std::isfinite(config.feather) ||
        !std::isfinite(config.center.x) ||
        !std::isfinite(config.center.y) ||
        !std::isfinite(config.inlandNormal.x) ||
        !std::isfinite(config.inlandNormal.y) ||
        !std::isfinite(config.alongshoreTangent.x) ||
        !std::isfinite(config.alongshoreTangent.y) ||
        config.authoredHalfExtent <= 0.0f ||
        config.feather < 0.0f) {
        return false;
    }

    AuthoredCoveStats result;
    result.minimumAuthoredHeight =
        std::numeric_limits<float>::infinity();
    result.maximumAuthoredHeight =
        -std::numeric_limits<float>::infinity();

    const glm::vec2 terrainOrigin{
        0.5f * static_cast<float>(width - 1u) * cellScale,
        0.5f * static_cast<float>(height - 1u) * cellScale,
    };
    if (!std::isfinite(terrainOrigin.x) ||
        !std::isfinite(terrainOrigin.y)) {
        return false;
    }
    // Clip in double precision before converting to sample indices. An
    // off-terrain center or tiny cell scale can exceed the integer range;
    // an empty intersection must never become an unsigned allocation size.
    const double radius =
        static_cast<double>(config.authoredHalfExtent) +
        static_cast<double>(config.feather) + 2.0;
    const double sampleScale = static_cast<double>(cellScale);
    const double sampleRadius = std::ceil(radius / sampleScale);
    const double centerX = std::floor(
        (static_cast<double>(config.center.x) +
         static_cast<double>(terrainOrigin.x)) / sampleScale);
    const double centerY = std::floor(
        (static_cast<double>(config.center.y) +
         static_cast<double>(terrainOrigin.y)) / sampleScale);
    const double clippedMinimumX = std::max(centerX - sampleRadius, 0.0);
    const double clippedMaximumX = std::min(
        centerX + sampleRadius, static_cast<double>(width - 1u));
    const double clippedMinimumY = std::max(centerY - sampleRadius, 0.0);
    const double clippedMaximumY = std::min(
        centerY + sampleRadius, static_cast<double>(height - 1u));
    if (clippedMinimumX > clippedMaximumX ||
        clippedMinimumY > clippedMaximumY) {
        return false;
    }
    const int32_t minimumX = static_cast<int32_t>(clippedMinimumX);
    const int32_t maximumX = static_cast<int32_t>(clippedMaximumX);
    const int32_t minimumY = static_cast<int32_t>(clippedMinimumY);
    const int32_t maximumY = static_cast<int32_t>(clippedMaximumY);

    const uint32_t regionWidth =
        static_cast<uint32_t>(maximumX - minimumX + 1);
    const uint32_t regionHeight =
        static_cast<uint32_t>(maximumY - minimumY + 1);
    const size_t regionSize =
        static_cast<size_t>(regionWidth) * regionHeight;
    std::vector<float> sourceHeights(regionSize);
    std::vector<float> horizontal(regionSize);
    std::vector<float> smoothed(regionSize);
    const auto regionIndex = [regionWidth](
        uint32_t x, uint32_t y) noexcept {
        return static_cast<size_t>(y) * regionWidth + x;
    };
    for (uint32_t y = 0; y < regionHeight; ++y) {
        for (uint32_t x = 0; x < regionWidth; ++x) {
            const size_t sourceIndex =
                static_cast<size_t>(
                    minimumY + static_cast<int32_t>(y)) * width +
                static_cast<uint32_t>(
                    minimumX + static_cast<int32_t>(x));
            sourceHeights[regionIndex(x, y)] =
                encodedToWorld(heights[sourceIndex], heightScale);
        }
    }

    // A compact separable Gaussian removes the diffusion tile's inflated
    // one-metre diagonal noise while preserving its low-frequency coast. The
    // authored relief below is added to this source-aware base, so the 100 m
    // patch cannot turn into a rectangular island at its feather.
    constexpr std::array<float, 17> kernel{
        1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f, 9.0f,
        8.0f, 7.0f, 6.0f, 5.0f, 4.0f, 3.0f, 2.0f, 1.0f};
    constexpr float kernelSum = 81.0f;
    for (uint32_t y = 0; y < regionHeight; ++y) {
        for (uint32_t x = 0; x < regionWidth; ++x) {
            float sum = 0.0f;
            for (int32_t tap = -8; tap <= 8; ++tap) {
                const uint32_t sampleX = static_cast<uint32_t>(
                    std::clamp(
                        static_cast<int32_t>(x) + tap,
                        0,
                        static_cast<int32_t>(regionWidth) - 1));
                sum += sourceHeights[regionIndex(sampleX, y)] *
                       kernel[static_cast<size_t>(tap + 8)];
            }
            horizontal[regionIndex(x, y)] = sum / kernelSum;
        }
    }
    for (uint32_t y = 0; y < regionHeight; ++y) {
        for (uint32_t x = 0; x < regionWidth; ++x) {
            float sum = 0.0f;
            for (int32_t tap = -8; tap <= 8; ++tap) {
                const uint32_t sampleY = static_cast<uint32_t>(
                    std::clamp(
                        static_cast<int32_t>(y) + tap,
                        0,
                        static_cast<int32_t>(regionHeight) - 1));
                sum += horizontal[regionIndex(x, sampleY)] *
                       kernel[static_cast<size_t>(tap + 8)];
            }
            smoothed[regionIndex(x, y)] = sum / kernelSum;
        }
    }

    for (int32_t y = minimumY; y <= maximumY; ++y) {
        for (int32_t x = minimumX; x <= maximumX; ++x) {
            const glm::vec2 worldPosition{
                static_cast<float>(x) * cellScale -
                    terrainOrigin.x,
                static_cast<float>(y) * cellScale -
                    terrainOrigin.y,
            };
            const AuthoredCoveSample authored =
                sampleAuthoredCove(worldPosition, config);
            if (authored.blend <= 0.0f) continue;

            const size_t index =
                static_cast<size_t>(y) * width +
                static_cast<uint32_t>(x);
            const float source =
                encodedToWorld(heights[index], heightScale);
            const uint32_t localX =
                static_cast<uint32_t>(x - minimumX);
            const uint32_t localY =
                static_cast<uint32_t>(y - minimumY);
            const float base =
                smoothed[regionIndex(localX, localY)];

            // Preserve the source coast while relaxing its near-water slope
            // into a readable subtidal shelf, foreshore, and backshore.
            const float relative = base - config.waterHeight;
            const float shoreZone =
                1.0f - smoothstep(
                    11.0f, 34.0f, std::abs(relative));
            const float relaxedRelative =
                relative * (relative < 0.0f ? 0.78f : 0.72f);
            const float filteredTarget =
                config.waterHeight + std::lerp(
                relative, relaxedRelative, shoreZone * 0.72f);
            float target = std::lerp(
                source, filteredTarget, shoreZone);

            const LocalCove local =
                toLocal(worldPosition, config);
            const float coveBowl =
                gaussian(local.alongshore + 2.0f, 25.0f) *
                gaussian(local.inland - 14.0f, 34.0f);
            const float erodedShoulder =
                0.54f *
                gaussian(local.alongshore - 19.0f, 13.0f) *
                gaussian(local.inland - 9.0f, 29.0f);
            const float coveMask =
                std::max(coveBowl, erodedShoulder);
            // Move the existing contour inland without replacing the steep
            // source coast with an incompatible absolute profile. The result
            // remains a connected eroded inlet instead of ending in a dark
            // artificial wall where two height regimes meet.
            target -= 10.6f * coveMask *
                smoothstep(-10.0f, 6.0f, relative);
            target += 0.82f *
                gaussian(relative - 3.0f, 3.8f) *
                gaussian(local.alongshore, 39.0f);

            // A source-relative backwash groove interrupts the sand ribbon
            // where the sculpted coast is already close to sea level.
            const float channelCenter =
                5.0f + (local.inland - 6.0f) * 0.29f +
                1.35f * std::sin(local.inland * 0.19f);
            const float channelLength =
                smoothstep(1.0f, 7.0f, local.inland) *
                (1.0f - smoothstep(
                    33.0f, 40.0f, local.inland));
            const float channel =
                gaussian(
                    local.alongshore - channelCenter, 2.35f) *
                channelLength;
            const float tidalCut =
                channel * coveMask;
            const float nearSculptedShore =
                1.0f - smoothstep(
                    3.0f, 8.0f,
                    std::abs(target - config.waterHeight));
            target -= 1.15f * tidalCut * nearSculptedShore;
            target -= drainageAndErosion(local) * 0.58f;
            target += debrisRelief(local) * 0.74f;
            target += shoreMesoRelief(local) *
                (0.45f + 0.55f * coveMask);
            target -= heroWashCut(local) * coveMask;
            target += shoreRockRelief(local);
            target += cliffBenchRelief(local);
            target += landBoulderRelief(local);
            target += wreckSandDrift(local);

            const float blend =
                authored.blend * authored.blend *
                (3.0f - 2.0f * authored.blend);
            const float sculpted =
                std::lerp(source, target, blend);
            heights[index] =
                worldToEncoded(sculpted, heightScale);

            ++result.touchedSamples;
            if (authored.blend >= 0.999f) {
                ++result.fullyAuthoredSamples;
            }
            result.minimumAuthoredHeight = std::min(
                result.minimumAuthoredHeight, target);
            result.maximumAuthoredHeight = std::max(
                result.maximumAuthoredHeight, target);
        }
    }

    if (result.touchedSamples == 0u) return false;
    if (stats) *stats = result;
    return true;
}

} // namespace voxy::terrain

namespace voxy::terrain {
bool applySalvageBerth(std::span<uint16_t> heights,uint32_t width,uint32_t height,
                      float heightScale,float cellScale,float waterHeight) noexcept {
    if (width<2 || height<2 || uint64_t(width)*height!=heights.size()
        || !std::isfinite(heightScale) || heightScale<=0 || !std::isfinite(cellScale) || cellScale<=0
        || !std::isfinite(waterHeight) || waterHeight-4 < -heightScale || waterHeight > heightScale) return false;
    for (uint32_t z=0;z<height;++z) for(uint32_t x=0;x<width;++x) {
        const float wx=(float(x)-float(width)*.5f)*cellScale;
        const float wz=(float(z)-float(height)*.5f)*cellScale;
        const float distance=std::max({-28.f-wx,wx+11.f,-114.f-wz,wz+84.f,0.f});
        if (distance>=4) continue;
        auto& sample=heights[size_t(z)*width+x];
        const float original=heightScale*(2*float(sample)/65535-1);
        if (original>=waterHeight || original<=waterHeight-4) continue;
        const float blend=1-smoothstep(0,4,distance);
        const float target=original+(waterHeight-4-original)*blend;
        const auto encoded=static_cast<uint16_t>(std::clamp(std::lround((target/heightScale+1)*.5f*65535),0l,65535l));
        sample=std::min(sample,encoded);
    }
    return true;
}
} // namespace voxy::terrain
