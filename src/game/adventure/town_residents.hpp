#pragma once

#include "game/adventure/adventure_session.hpp"
#include "game/adventure/spatial_queries.hpp"
#include <array>
#include <limits>
#include <span>
#include <string_view>

namespace voxy::game::adventure {

inline constexpr size_t kTownResidentCount=3;
struct TownResidentDefinition {
    uint32_t id=0;
    std::string_view name,role;
    glm::dvec2 anchor{},facingTarget{};
    glm::vec3 tint{1};
};
struct TownResidentPose {
    uint32_t id=0;
    glm::dvec3 feet{},approach{};
    double yaw=0;
    bool available=false;
};
[[nodiscard]] std::span<const TownResidentDefinition> residentDefinitions() noexcept;
[[nodiscard]] const TownResidentDefinition* residentDefinition(uint32_t id) noexcept;
[[nodiscard]] constexpr uint64_t residentPartId(uint32_t id) noexcept {
    return id>=1&&id<=kTownResidentCount?std::numeric_limits<uint64_t>::max()-id:0;
}

// Derived installed actors, never saved player parts. The caller first validates
// and restores the old player/house without these new actors. Base queries must
// contain accepted buildings/markers but no resident colliders. On initial load
// or restore, pass previous=nullptr. During that same load, retain the returned
// poses; passing previous retries only deferred actors and never moves an
// admitted actor back toward its authored anchor. Retry at most4Hz after an
// actual player/geometry change. Failure defers visual and collider together.
class TownResidents {
public:
    static constexpr double bodyRadius=.3,bodyHeight=1.7,interactionRange=3;
    [[nodiscard]] static TownResidents admit(const AdventureState&,
        const AdventureSpatialQueries& base,const TownResidents* previous=nullptr);
    [[nodiscard]] const std::array<TownResidentPose,kTownResidentCount>& entries() const noexcept {return entries_;}
    [[nodiscard]] const TownResidentPose* find(uint32_t id) const noexcept;
    // Append only after admission; capacity refusal leaves the vector unchanged.
    [[nodiscard]] bool appendSolids(construction::WorldNamespace,std::vector<AdventureSpatialQueries::Solid>&) const;
    [[nodiscard]] bool interactable(uint32_t id,const PlayerPose&,
        const AdventureSpatialQueries& published,std::string& error) const;
    [[nodiscard]] uint32_t nearestInteractable(const PlayerPose&,
        const AdventureSpatialQueries& published,uint64_t aimedPart=0) const;
private:
    construction::WorldNamespace world_{};
    std::array<TownResidentPose,kTownResidentCount> entries_{};
};

} // namespace voxy::game::adventure
