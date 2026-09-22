#pragma once

#include "terrain/lego_surface.hpp"
#include <array>
#include <string_view>
#include <glm/glm.hpp>

namespace voxy::game::adventure {

// Installed content, not a recipe supplied by save bytes. Coordinates are
// centered full-heightfield metres; no Cove crop or water-relative translation.
struct WorldDefinition {
    std::string_view profile, contentRevision, terrainRecipe, heightmapPath;
    std::string_view ldhSha256, samplesSha256;
    uint32_t width=8192, height=8192;
    float heightScale=600, cellScale=1, waterHeight=-200;
    glm::dvec2 town{}, homeSuggestion{}, alternativeHome{}, landmark{};
    std::array<glm::dvec2,4> route{};
    // Visible square/sign access is protected; suggestions are not build plots.
    double townProtectedRadius=5, landmarkProtectedRadius=2;
};

[[nodiscard]] const WorldDefinition& installedWorld() noexcept;
[[nodiscard]] bool matchesInstalledTerrain(const terrain::lego::Surface&) noexcept;
[[nodiscard]] glm::dvec3 townSpawn(const terrain::lego::Surface&) noexcept;
// New creative worlds open on a sunlit, level shelf above the main bay.
// This is presentation/startup policy, not durable content or a saved home.
inline constexpr glm::dvec2 creativeStart{1200,-1120};
inline constexpr double creativeStartYaw=.75;
[[nodiscard]] glm::dvec3 creativeSpawn(const terrain::lego::Surface&,double bodyRadius) noexcept;
[[nodiscard]] bool protectedConstruction(glm::dvec3 minimum,glm::dvec3 maximum) noexcept;

} // namespace voxy::game::adventure
