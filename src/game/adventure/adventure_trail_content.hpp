#pragma once
#include "game/adventure/adventure_progress.hpp"
#include <span>
#include <string_view>

namespace voxy::game::adventure {
struct DiscoveryContent {
    uint8_t id=0;
    PlayerPose position{};
    ItemStack reward{};
    bool operator==(const DiscoveryContent&) const = default;
};
struct TrailQuestDefinition {
    uint8_t id=0,npcId=0,prerequisite=0;
    std::string_view title,objective;
};
[[nodiscard]] std::span<const TrailQuestDefinition> trailQuestDefinitions() noexcept;
[[nodiscard]] const TrailQuestDefinition* trailQuestDefinition(uint8_t id) noexcept;
[[nodiscard]] bool trailQuestPrerequisite(const AdventureState&,uint8_t id) noexcept;
// Current equipped items for2, historical core claim for3, activation receipt
// for4, both discovery arrival receipts for5. Read-only hints; accepting and
// finishing still use session transactions.
[[nodiscard]] bool trailQuestReady(const AdventureState&,uint8_t id) noexcept;
[[nodiscard]] bool wideStoneStepRecipeUnlocked(const AdventureState&) noexcept;
[[nodiscard]] bool validTrailContent(const AdventureContent&) noexcept;
[[nodiscard]] bool validateTrailProgress(const AdventureState&,const AdventureContent&,std::string&);
[[nodiscard]] std::string_view trailContentFingerprint() noexcept;
[[nodiscard]] std::string_view sideQuestContentFingerprint() noexcept;
} // namespace voxy::game::adventure
