#pragma once

#include <glm/vec2.hpp>
#include <cmath>
#include <optional>

namespace voxy::game::adventure {

struct AdventurePointer {
    glm::dvec2 framebuffer{};
    glm::dvec2 ndc{};
};

// Input stores logical window/CSS coordinates. Rendering and HUD bounds use
// framebuffer pixels. Use the actual two extents, since browser resolution
// scaling and rounding need not match devicePixelRatio or be uniform per axis.
// Preserve outside positions: clamping would turn a miss into an edge hit.
[[nodiscard]] inline std::optional<AdventurePointer> adventurePointer(
    glm::dvec2 raw,glm::dvec2 logicalExtent,glm::uvec2 framebufferExtent) noexcept {
    if(!std::isfinite(raw.x)||!std::isfinite(raw.y)||
       !std::isfinite(logicalExtent.x)||!std::isfinite(logicalExtent.y)||
       logicalExtent.x<=0||logicalExtent.y<=0||
       framebufferExtent.x==0||framebufferExtent.y==0)return std::nullopt;
    const auto normalized=raw/logicalExtent;
    const AdventurePointer result{
        normalized*glm::dvec2(framebufferExtent),
        {normalized.x*2-1,1-normalized.y*2}};
    if(!std::isfinite(result.framebuffer.x)||!std::isfinite(result.framebuffer.y)||
       !std::isfinite(result.ndc.x)||!std::isfinite(result.ndc.y))return std::nullopt;
    return result;
}

} // namespace voxy::game::adventure
