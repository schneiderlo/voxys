#pragma once

#include "game/adventure/adventure_session.hpp"
#include "game/adventure/spatial_queries.hpp"
#include <array>
#include <string_view>

namespace voxy::game::adventure {

struct EncounterDefinition {
    uint8_t id=0;
    std::string_view name;
    glm::dvec2 anchor{};
    uint16_t maximumHealth=0,damage=0;
    double noticeRadius=7,leashRadius=12,reach=1.6,attackWindup=.7,recovery=.9;
    ItemStack loot{};
    bool grantsRelayCore=false;
    glm::vec3 tint{1};
};
struct AdmittedEncounter {
    uint8_t id=0;
    PlayerPose pose{};
    uint16_t health=0;
    bool available=false;
    // A new actor's derived pose must be accepted through the combat authority
    // before its first movement/attack. Admission itself never writes a save.
    bool needsPositionCheckpoint=false;
};
[[nodiscard]] std::span<const EncounterDefinition> encounterDefinitions() noexcept;
[[nodiscard]] const EncounterDefinition* encounterDefinition(uint32_t id) noexcept;
[[nodiscard]] constexpr uint64_t encounterStructureId(uint32_t id) noexcept {
    return id>=1&&id<=kAdventureEncounterCount?UINT64_MAX-32768u-id:0;
}
[[nodiscard]] constexpr uint64_t encounterPartId(uint32_t id) noexcept {
    return id>=1&&id<=kAdventureEncounterCount?UINT64_MAX-33024u-id:0;
}

// Initial installed content uses real terrain support and the actual walking
// controller for four one-metre approach/exit probes. Failure changes no output.
[[nodiscard]] bool defaultEncounterContent(const AdventureSpatialQueries& staticBase,
    std::array<EncounterContent,kAdventureEncounterCount>& output,std::string& error);

// staticBase contains terrain, accepted player structures and installed scenery,
// never these enemies. Save checkpoints remain authoritative: an alive saved
// pose is admitted exactly or the entire actor (visual/collider/AI) is deferred.
// Only an unpositioned new checkpoint may try the nine bounded anchor offsets.
// Call on initial restore, accepted geometry or actor checkpoint changes; an
// unavailable actor cannot attack, move or issue loot from this derived module.
class AdventureEncounters {
public:
    static constexpr double bodyRadius=.3,bodyHeight=1.7,playerMargin=.05;
    [[nodiscard]] static AdventureEncounters admit(const AdventureState&,
        const AdventureSpatialQueries& staticBase);
    [[nodiscard]] const std::array<AdmittedEncounter,kAdventureEncounterCount>& entries() const noexcept {return entries_;}
    [[nodiscard]] const AdmittedEncounter* find(uint32_t id) const noexcept;
    // Appends the exact available rendered bodies, all-or-nothing on refusal.
    [[nodiscard]] bool appendSolids(construction::WorldNamespace,
        std::vector<AdventureSpatialQueries::Solid>&) const;
    [[nodiscard]] bool occupied(glm::dvec3 feet,double radius=.3,double height=1.7,
        uint32_t exceptId=0) const noexcept;
    // Shared static walking policy; root additionally checks player/other actors
    // and follows the ordinary layered navigation/controller path.
    [[nodiscard]] bool movementAllowed(uint32_t id,glm::dvec3 feet,
        const AdventureSpatialQueries& staticBase) const noexcept;
    [[nodiscard]] bool safeRest(PlayerPose,const AdventureSpatialQueries& staticBase) const noexcept;
private:
    construction::WorldNamespace world_{};
    std::array<AdmittedEncounter,kAdventureEncounterCount> entries_{};
};

} // namespace voxy::game::adventure
