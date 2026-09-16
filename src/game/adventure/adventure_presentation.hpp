#pragma once
#include "game/adventure/adventure_preferences.hpp"
#include "game/adventure/adventure_session.hpp"
#include "game/adventure/quests.hpp"
#include <optional>
#include <glm/mat4x4.hpp>

namespace voxy::game::adventure {
enum class AdventureGuideTopic : uint8_t { Movement,Building,Home,Quests,Combat,Saving,Count };
struct AdventureGuideCard { std::string title,text; };
// Short, read-only help. Reading a card never accepts a quest, saves a world,
// changes preferences or records tutorial completion.
[[nodiscard]] AdventureGuideCard adventureGuideCard(AdventureGuideTopic,const AdventurePreferences&,bool gamepad);
enum class HomeDialogueChoice : uint8_t { Close, Accept, Complete };
struct HomeDialogue {
    std::string text;
    std::string choiceLabel;
    HomeDialogueChoice choice=HomeDialogueChoice::Close;
};
struct CompassReadout {
    bool equipped=false, available=false, home=false;
    std::optional<uint8_t> backpackSlot;
    double bearing=0, distance=0;
    std::string label, direction, reason;
};
// Read-only presentation. Session commands still validate giver, phase,
// geometry, inventory and receipt before accepting any choice.
[[nodiscard]] HomeDialogue homeDialogue(uint8_t npc,const AdventureState&,const FirstHomeReadiness&);
[[nodiscard]] CompassReadout compassReadout(const AdventureState&,const AdventureSpatialQueries&,bool home);
[[nodiscard]] std::string homeObjective(const AdventureState&,const FirstHomeReadiness&);
// Cosmetic canonical Beam mesh in the exported right-hand anchor frame.
// The admitted rigid anchor already includes the character's basis bridge.
// Shaft: .8 m long, .0448 m square, through the C-grip's local Z aperture.
[[nodiscard]] glm::dmat4 trailStaffModel(const glm::dmat4& hand) noexcept;
}
