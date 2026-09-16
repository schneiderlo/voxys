#pragma once

#include <cstdint>
#include <string>

namespace voxy::game::adventure {
struct AdventureState;
class AdventureSpatialQueries;

// Installed IDs and versions are part of the canonical save contract.
enum class QuestId : uint8_t { FirstHome = 1 };
inline constexpr uint16_t kFirstHomeDefinitionVersion = 1;
enum class QuestPhase : uint8_t { NotAccepted = 0, Active = 1, Completed = 2 };

struct FirstHomeProgress {
    QuestPhase phase = QuestPhase::NotAccepted;
    // Zero until completion; otherwise the accepted completion revision.
    // This receipt is permanent, even when the qualifying home is later removed.
    uint64_t rewardRevision = 0;
    bool operator==(const FirstHomeProgress&) const = default;
};

[[nodiscard]] bool validFirstHomeProgress(const FirstHomeProgress&, uint64_t acceptedRevision) noexcept;
// A validated completed quest owns the recipe independently of backpack space.
// This helper does not grant a compass item or accept a quest transition.
[[nodiscard]] bool trailCompassRecipeUnlocked(const FirstHomeProgress&) noexcept;

enum class FirstHomeStep : uint8_t {
    Ready,
    RegisterBed,
    MakeBedUsable,
    AddChest,
    AddWorkbench,
};
struct FirstHomeReadiness {
    FirstHomeStep nextStep = FirstHomeStep::RegisterBed;
    uint64_t structure = 0;
    std::string message;
    [[nodiscard]] bool ready() const noexcept { return nextStep == FirstHomeStep::Ready; }
};

// Current-state objective, including work done before quest acceptance/schema 2.
// The state must already pass AdventureSession::validate, and the queries must
// represent that accepted world's geometry. This read-only result never grants
// rewards. Turn-in must re-evaluate it inside the trusted transaction validator.
// Chest contents, hammer ownership and historical transfers/crafting do not
// belong to the first-home predicate.
[[nodiscard]] FirstHomeReadiness firstHomeReadiness(
    const AdventureState&, const AdventureSpatialQueries&);
} // namespace voxy::game::adventure
