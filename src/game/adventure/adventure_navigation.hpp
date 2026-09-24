#pragma once

#include "terrain/lego_surface.hpp"
#include <cstdint>
#include <span>
#include <vector>

namespace voxy::game::adventure {
// North is world -Z. This map is sampled from the accepted LEGO terrain, not
// a second world simulation. Geometry overlays contain accepted footprints only.
struct AdventureNavigationFootprint {
    glm::dvec2 minimum{},maximum{};
    uint32_t rgb=0xb89568;
};
[[nodiscard]] float adventureNavigationBearing(glm::dvec2 forward) noexcept;
class AdventureNavigationCache {
public:
    static constexpr uint32_t size=256;
    static constexpr double span=1536,centerStep=128;
    [[nodiscard]] bool needsUpdate(const terrain::lego::Surface&,float waterHeight,
        glm::dvec2 player,uint64_t geometryEpoch) const noexcept;
    [[nodiscard]] std::vector<uint8_t> rasterize(const terrain::lego::Surface&,float waterHeight,
        glm::dvec2 player,uint64_t geometryEpoch,std::span<const AdventureNavigationFootprint>);
    [[nodiscard]] glm::vec2 playerUv(glm::dvec2 player) const noexcept;
    [[nodiscard]] uint64_t terrainRasterizations() const noexcept {return terrainRasterizations_;}
private:
    static glm::dvec2 quantizedCenter(glm::dvec2) noexcept;
    bool sameTerrain(const terrain::lego::Surface&,float waterHeight) const noexcept;
    terrain::lego::Surface source_{};
    glm::dvec2 center_{};
    float waterHeight_=0;
    uint64_t geometryEpoch_=0,terrainRasterizations_=0;
    std::vector<uint8_t> terrainRgba_;
};
}
