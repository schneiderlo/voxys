#pragma once

#include "game/expedition/cove_input_preferences.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

namespace voxy::game::adventure {
inline constexpr size_t kMaximumAdventurePreferencesBytes=4096;
inline constexpr uint32_t kAdventurePreferencesVersion=1;
enum class CombatAction : uint8_t { Attack=0,Dodge=1 };
struct CombatBinding {
    int key=0,mouse=-1,pad=-1;
    bool operator==(const CombatBinding&) const=default;
};
// Independent local profile. These fields are never world save payload fields.
struct AdventurePreferences {
    double textScale=1,mouseSensitivity=1,padSensitivity=1;
    double moveDeadzone=.2,lookDeadzone=.2;
    bool highContrast=false,reducedMotion=false,invertX=false,invertY=false,orbitToggle=false;
    std::array<CombatBinding,2> combat{{{0,0,5},{81,-1,1}}};
    bool operator==(const AdventurePreferences&) const=default;
};
[[nodiscard]] bool validateAdventurePreferences(const AdventurePreferences&,std::string& error);
// Complete strict JSON v1: duplicate, unknown, missing, malformed and conflicting
// fields refuse without changing output. Combat rows identify attack/dodge.
[[nodiscard]] bool parseAdventurePreferences(std::string_view,AdventurePreferences&,std::string& error);
[[nodiscard]] bool encodeAdventurePreferences(const AdventurePreferences&,std::string&,std::string& error);
// Reduced motion belongs to the camera settings; no equivalent Cove input field
// exists. All Adventure movement/build bindings remain adventureInputDefaults().
[[nodiscard]] expedition::CoveInputPreferences adventureRoutingPreferences(const AdventurePreferences&);
[[nodiscard]] std::span<const int> combatKeyChoices() noexcept;
[[nodiscard]] std::span<const int> combatMouseChoices() noexcept;
[[nodiscard]] std::span<const int> combatPadChoices() noexcept;
[[nodiscard]] std::string combatBindingLabel(const AdventurePreferences&,CombatAction,bool gamepad);

// Once per displayed input frame; active only while Explore owns world input.
// Context/focus/reset/rebind boundaries require physical release before rearm.
// Gamepad axes are already filtered by the platform and are never filtered here.
class AdventureCombatInputRouter {
public:
    // Accumulated mouse press edges preserve clicks that begin and end between
    // frames. Standard indices: left0, right1, middle2.
    void tick(const expedition::CoveInputSample&,bool active,const AdventurePreferences&,
        std::array<bool,3> mousePressed={});
    void reset() noexcept;
    [[nodiscard]] bool pressed(CombatAction) const noexcept;
private:
    AdventurePreferences preferences_{};
    bool initialized_=false,active_=false,focused_=false,valid_=false;
    bool padConnected_=false,padArmed_=false,blockedPad_=true,blockedMouse_=true;
    uint64_t resetSerial_=0,padSerial_=0;
    std::array<bool,512> blockedKeys_{};
    std::array<bool,3> previousMouse_{};
    std::array<bool,2> pressed_{};
};
} // namespace voxy::game::adventure
