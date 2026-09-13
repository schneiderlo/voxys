#include "game/adventure/world_definition.hpp"
#include <cmath>

namespace voxy::game::adventure {
const WorldDefinition& installedWorld() noexcept {
    static const WorldDefinition world{
        "voxys.adventure.main.v1", "meadow-home-r01", "full-main-unmodified-lego-r01",
        "data/generated/td_seed_1234_8192.ldh",
        "09fdfe368187cb2214bab5ceaf93c1c6abb79b492d9c3b8704378c5c40188dc7",
        "2a0ae88395e6d6e59d8853540c875d834f0f04015ee953d76faf68abae610965",
        8192,8192,600,1,-200,
        {-63,-895}, {-85,-895}, {-41,-895}, {-63,-975},
        {{{-63,-895},{-63,-917},{-63,-945},{-63,-975}}},5,2
    };
    return world;
}
bool matchesInstalledTerrain(const terrain::lego::Surface& surface) noexcept {
    const auto& w=installedWorld();
    return surface.valid()&&surface.width==w.width&&surface.height==w.height
        &&surface.heightScale==w.heightScale&&surface.cellScale==w.cellScale;
}
glm::dvec3 townSpawn(const terrain::lego::Surface& surface) noexcept {
    const auto p=installedWorld().town;
    if(!surface.valid())return {p.x,std::numeric_limits<double>::infinity(),p.y};
    return {p.x,double(terrain::lego::supportHeight(surface,glm::vec2(p),.3f))+.005,p.y};
}
bool protectedConstruction(glm::dvec3 lo,glm::dvec3 hi) noexcept {
    for(int axis=0;axis<3;++axis)if(!std::isfinite(lo[axis])||!std::isfinite(hi[axis])||lo[axis]>=hi[axis])return true;
    const auto& w=installedWorld();
    for(const auto& site:std::array<std::pair<glm::dvec2,double>,2>{{{w.town,w.townProtectedRadius},{w.landmark,w.landmarkProtectedRadius}}}) {
        const auto nearest=glm::clamp(site.first,glm::dvec2(lo.x,lo.z),glm::dvec2(hi.x,hi.z));
        if(glm::length(nearest-site.first)<site.second)return true;
    }
    return false;
}
} // namespace voxy::game::adventure
