#pragma once

#include "game/adventure/adventure_trail_content.hpp"
#include "game/adventure/trail_sites.hpp"
#include <array>
#include <optional>
#include <string>

namespace voxy::game::adventure {
// Presentation IDs are explicit. They are not TrailSiteId or a saved unlock.
enum class CompassTarget : uint8_t { Home=0,Relay=1,SignalTerrace=2,SurveyOverlook=3,WatchArch=4 };
enum class TrailDialogueChoice : uint8_t { Close,Accept,Complete };
struct TrailDialogue {
    uint8_t questId=0;
    TrailDialogueChoice choice=TrailDialogueChoice::Close;
    std::string text,choiceLabel;
};
struct TrailQuestReadout {
    uint8_t questId=0;
    QuestPhase phase=QuestPhase::NotAccepted;
    bool unlocked=false,ready=false;
    std::string title,objective,status;
    std::optional<CompassTarget> waypoint;
};
struct TrailCompassReadout {
    CompassTarget target=CompassTarget::Relay;
    bool revealed=false,equipped=false,available=false;
    std::optional<uint8_t> backpackSlot;
    double bearing=0,distance=0;
    std::string label,direction,reason;
};
struct TrailDiscoveryReadout {
    uint8_t discoveryId=0;
    bool found=false,rewardClaimed=false,available=false;
    CompassTarget waypoint=CompassTarget::SignalTerrace;
    std::string title,objective,reward;
};

// Inputs are accepted state and current physical readiness. These functions
// produce no session commands, inventory changes or durable unlocks. NPC/quest
// pairs are semantic menu identities; the host must publish revision-bound
// intents and revalidate physical NPC reach before accepting a choice.
// Moss returns nullopt until first-home completion, preserving that dialogue;
// afterwards he owns optional quest5. Unknown NPCs always return nullopt.
[[nodiscard]] std::optional<TrailDialogue> trailDialogue(uint8_t npc,const AdventureState&,
    const FirstHomeReadiness& fieldHome);
[[nodiscard]] std::optional<TrailQuestReadout> trailQuestReadout(uint8_t questId,
    const AdventureState&,const FirstHomeReadiness& fieldHome);
// Exactly main IDs2..4; rows select a questId, never remotely turn it in.
[[nodiscard]] std::array<TrailQuestReadout,3> trailJournal(const AdventureState&,
    const FirstHomeReadiness& fieldHome);
// The optional survey entry is separate from the main three-row journal and
// main tracker. Hidden until first-home completion, including on old saves.
[[nodiscard]] std::optional<TrailQuestReadout> trailSideQuest(const AdventureState&);
[[nodiscard]] std::string trailObjective(const AdventureState&,
    const FirstHomeReadiness& firstHome,const FirstHomeReadiness& fieldHome);
// Bearings point to admitted physical sites, never raw fallback coordinates.
// Home follows the current usable registered bed. Watch Arch remains hidden
// until the permanent relay activation receipt exists. Equipment is mandatory.
[[nodiscard]] TrailCompassReadout trailCompassReadout(const AdventureState&,
    const AdventureSpatialQueries&,const TrailSites&,CompassTarget);
// Material rewards are described only after the real discovery receipt. The
// passed immutable installed records provide quantities; no recipe is invented.
[[nodiscard]] std::array<TrailDiscoveryReadout,2> trailDiscoveries(const AdventureState&,
    std::span<const DiscoveryContent>,const TrailSites&);
} // namespace voxy::game::adventure
