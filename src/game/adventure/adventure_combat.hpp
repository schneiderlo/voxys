#pragma once

#include "game/adventure/adventure_encounters.hpp"
#include "game/adventure/layered_navigation.hpp"

namespace voxy::game::adventure {

// Bounded solo fixed-tick resolver. Checkpoints own attack windows, damage and
// death; navigation/animation never award health, items or progress. The host
// commits the returned packet and its collider packet together. A refused
// packet requires reset() and restoring the player's previous controller state.
class AdventureCombat {
public:
    struct Input {AdventurePlayer::Input movement{};bool attack=false,dodge=false;};
    static constexpr uint16_t staffDamage=25;
    static constexpr uint64_t staffWindupTicks=12,staffRecoveryTicks=36;
    static constexpr uint64_t dodgeTicks=12,dodgeRecoveryTicks=54;
    [[nodiscard]] bool initialize(const AdventureContent&,const AdventureSpatialQueries&,double waterHeight);
    void reset() noexcept;
    [[nodiscard]] CombatTick step(const AdventureState&,const AdventureContent&,
        const AdventureEncounters&,const AdventureSpatialQueries& staticWorld,AdventurePlayer&,Input);
    [[nodiscard]] static bool sight(const AdventureSpatialQueries&,PlayerPose from,PlayerPose to) noexcept;
    [[nodiscard]] static bool hitArc(const AdventureSpatialQueries&,PlayerPose from,PlayerPose to,double reach) noexcept;
    [[nodiscard]] static bool fighting(EnemyPhase) noexcept;
    [[nodiscard]] static std::string_view phaseLabel(EnemyPhase) noexcept;
    [[nodiscard]] const std::array<LayeredNavigation::Work,kAdventureEncounterCount>& work() const noexcept {return work_;}
private:
    struct Walker {
        LayeredNavigation graph;
        NavigationFollower follower;
        AdventurePlayer actor;
        glm::dvec3 goal{};
        uint64_t nextRequest=0;
        bool configured=false,following=false;
    };
    [[nodiscard]] bool walk(size_t,EnemyCombatCheckpoint&,glm::dvec3 target,uint64_t,
        const AdventureEncounters&,const AdventureSpatialQueries&,PlayerPose player,
        const std::array<EnemyCombatCheckpoint,kAdventureEncounterCount>&);
    std::array<Walker,kAdventureEncounterCount> walkers_;
    std::array<LayeredNavigation::Work,kAdventureEncounterCount> work_{};
    double waterHeight_=-200;
};
} // namespace voxy::game::adventure
