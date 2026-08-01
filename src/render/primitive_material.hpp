#pragma once

#include <glm/vec3.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace voxy::render {

// PhysicsMaterial::flags is already resident beside every GPU body. This
// compact encoding turns those otherwise opaque bits into a render material
// without adding another storage buffer, upload, or draw call.
//
//   0..15  linear base color, RGB 5:6:5
//  16..21  perceptual roughness
//  22..26  metallic
//      27  dielectric clear coat
//  28..31  0xA marker (all other flag values retain the shape fallback)
inline constexpr uint32_t kPrimitiveMaterialMarker = 0xa000'0000u;
inline constexpr uint32_t kPrimitiveMaterialMarkerMask = 0xf000'0000u;

struct PrimitiveMaterial {
    glm::vec3 baseColor{0.5f};
    float roughness = 0.5f;
    float metallic = 0.0f;
    bool clearcoat = false;
};

[[nodiscard]] inline bool hasPrimitiveMaterial(uint32_t flags) noexcept {
    return (flags & kPrimitiveMaterialMarkerMask) == kPrimitiveMaterialMarker;
}

[[nodiscard]] inline uint32_t packPrimitiveMaterial(
    const PrimitiveMaterial& material) noexcept {
    const auto finiteUnit = [](float value, float fallback) noexcept {
        return std::clamp(std::isfinite(value) ? value : fallback, 0.0f, 1.0f);
    };
    const auto quantize = [](float value, uint32_t maximum) noexcept {
        return static_cast<uint32_t>(
            std::lround(value * static_cast<float>(maximum)));
    };

    const uint32_t red = quantize(finiteUnit(material.baseColor.r, 0.5f), 31u);
    const uint32_t green =
        quantize(finiteUnit(material.baseColor.g, 0.5f), 63u);
    const uint32_t blue = quantize(finiteUnit(material.baseColor.b, 0.5f), 31u);
    const uint32_t roughness =
        quantize(finiteUnit(material.roughness, 0.5f), 63u);
    const uint32_t metallic =
        quantize(finiteUnit(material.metallic, 0.0f), 31u);

    return kPrimitiveMaterialMarker
        | red
        | (green << 5u)
        | (blue << 11u)
        | (roughness << 16u)
        | (metallic << 22u)
        | (material.clearcoat ? (1u << 27u) : 0u);
}

[[nodiscard]] inline PrimitiveMaterial unpackPrimitiveMaterial(
    uint32_t flags) noexcept {
    if (!hasPrimitiveMaterial(flags)) return {};
    return {
        .baseColor = {
            static_cast<float>(flags & 31u) / 31.0f,
            static_cast<float>((flags >> 5u) & 63u) / 63.0f,
            static_cast<float>((flags >> 11u) & 31u) / 31.0f,
        },
        .roughness =
            static_cast<float>((flags >> 16u) & 63u) / 63.0f,
        .metallic =
            static_cast<float>((flags >> 22u) & 31u) / 31.0f,
        .clearcoat = ((flags >> 27u) & 1u) != 0u,
    };
}

} // namespace voxy::render
