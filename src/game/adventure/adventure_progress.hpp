#pragma once

#include "game/adventure/item_catalog.hpp"
#include "game/adventure/quests.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace voxy::game::adventure {
struct AdventureState;
struct AdventureContent;
struct PlayerPose {
    double x=0,y=0,z=0,yaw=0;
    bool operator==(const PlayerPose&) const = default;
};
inline constexpr size_t kAdventureEncounterCount=2;
inline constexpr uint64_t kMaximumCombatDeadlineTicks=600;
enum class EnemyPhase : uint8_t { Dormant,Idle,Patrol,Notice,Chase,Windup,Attack,Recover,Return,Dead };
struct PlayerCombatCheckpoint {
    uint64_t attackSerial=0,attackReadyTick=0,attackImpactTick=0;
    uint64_t dodgeReadyTick=0,dodgeUntilTick=0,invulnerableUntilTick=0;
    double dodgeDirectionX=0,dodgeDirectionZ=0;
    bool operator==(const PlayerCombatCheckpoint&) const = default;
};
struct EnemyCombatCheckpoint {
    uint8_t encounterId=0;
    uint32_t generation=1;
    bool positioned=false;
    PlayerPose pose{};
    uint16_t health=0;
    EnemyPhase phase=EnemyPhase::Dormant;
    uint16_t phaseTicks=0;
    uint64_t attackSerial=0,lastPlayerAttackSerial=0;
    bool operator==(const EnemyCombatCheckpoint&) const = default;
};
struct EncounterProgress {
    EnemyCombatCheckpoint checkpoint{};
    uint64_t deathRevision=0,lootClaimRevision=0;
    bool operator==(const EncounterProgress&) const = default;
};
struct AdventureCombatProgress {
    uint64_t tick=0;
    PlayerCombatCheckpoint player{};
    std::array<EncounterProgress,kAdventureEncounterCount> encounters{};
    bool operator==(const AdventureCombatProgress&) const = default;
};
// Compact installed save/authority contract. World EncounterDefinition owns
// navigation, ranges, timings and damage and supplies these immutable values.
struct EncounterContent {
    uint8_t id=0;
    uint32_t generation=1;
    PlayerPose spawn{};
    uint16_t maximumHealth=0;
    ItemStack loot{};
    bool grantsRelayCore=false;
    bool operator==(const EncounterContent&) const = default;
};
struct DiscoveryProgress {
    uint64_t discoveredRevision=0,rewardClaimRevision=0;
    bool operator==(const DiscoveryProgress&) const = default;
};
// Schema3 reserved these records as unearned. Schema4 enabled discoveries and
// main quests2..4; schema5 enables optional quest5 in slot3 without adding fields.
struct AdventureTrailProgress {
    std::array<DiscoveryProgress,2> discoveries{};
    std::array<FirstHomeProgress,4> quests{};
    uint64_t relayActivationRevision=0;
    bool operator==(const AdventureTrailProgress&) const = default;
};
// Trusted fixed-tick resolver output, never a UI/network health-setting action.
// Session assigns death receipts itself; this packet cannot grant or claim loot.
struct CombatTick {
    uint64_t tick=0;
    PlayerPose player{};
    uint16_t health=100;
    PlayerCombatCheckpoint playerCombat{};
    std::array<EnemyCombatCheckpoint,kAdventureEncounterCount> enemies{};
};
[[nodiscard]] bool validEncounterContent(const AdventureContent&) noexcept;
void initializeAdventureProgress(AdventureState&,const AdventureContent&) noexcept;
[[nodiscard]] bool validateAdventureProgress(const AdventureState&,const AdventureContent&,std::string&);
} // namespace voxy::game::adventure
