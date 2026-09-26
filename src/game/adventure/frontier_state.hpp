#pragma once

#include "game/adventure/adventure_progress.hpp"
#include <string>
#include <vector>

namespace voxy::game::adventure {
// Stable installed IDs, independent of a renderer slot or currently active actor.
// The first region intentionally uses a bounded snapshot, not unlimited entities.
inline constexpr size_t kMaximumFrontierEnemies=16, kMaximumFrontierSites=8;
enum class FrontierSiteKind : uint8_t { Cache, Beacon, Quarry };
enum class FrontierEnemyArchetype : uint8_t { Scout, Brute };
struct FrontierEnemyContent {
    uint32_t id=0, generation=1;
    PlayerPose spawn{};
    uint16_t maximumHealth=60;
    ItemStack loot{ItemKind::Scrap,8};
    FrontierEnemyArchetype archetype=FrontierEnemyArchetype::Scout;
    bool operator==(const FrontierEnemyContent&) const = default;
};
struct FrontierSiteContent {
    uint32_t id=0;
    PlayerPose position{};
    FrontierSiteKind kind=FrontierSiteKind::Cache;
    ItemStack reward{};
    // Installed encounter rules; durable saves retain the same stable receipts.
    std::vector<uint32_t> defenders{};
    bool requiresQuarryCharge=false;
    bool operator==(const FrontierSiteContent&) const = default;
};
struct FrontierEnemyProgress {
    uint32_t id=0, generation=1;
    PlayerPose pose{}; // Last accepted impact/death position; untouched actors use spawn.
    uint16_t health=0;
    uint64_t deathRevision=0, lootClaimRevision=0;
    bool operator==(const FrontierEnemyProgress&) const = default;
};
struct FrontierSiteProgress {
    uint32_t id=0;
    uint64_t discoveredRevision=0, restoredRevision=0, rewardClaimRevision=0;
    bool operator==(const FrontierSiteProgress&) const = default;
};
struct FrontierEnemyPose {
    uint32_t id=0;
    PlayerPose pose{};
};
struct FrontierProgress {
    std::vector<FrontierEnemyProgress> enemies;
    std::vector<FrontierSiteProgress> sites;
    uint64_t quarryUnlockRevision=0;
    bool operator==(const FrontierProgress&) const = default;
};
[[nodiscard]] bool validFrontierContent(const AdventureContent&) noexcept;
void initializeFrontierProgress(AdventureState&,const AdventureContent&);
[[nodiscard]] bool validateFrontierProgress(const AdventureState&,const AdventureContent&,std::string&);
[[nodiscard]] const FrontierEnemyProgress* frontierEnemy(const AdventureState&,uint32_t id) noexcept;
[[nodiscard]] const FrontierSiteProgress* frontierSite(const AdventureState&,uint32_t id) noexcept;
[[nodiscard]] size_t frontierRemainingDefenders(const AdventureState&,const FrontierSiteContent&) noexcept;
[[nodiscard]] bool frontierHasQuarryHarvest(const AdventureState&,const AdventureContent&) noexcept;
[[nodiscard]] bool frontierBeaconRequirements(const AdventureState&,const AdventureContent&,const FrontierSiteContent&,std::string&);
[[nodiscard]] std::string frontierContentFingerprint();
} // namespace voxy::game::adventure
