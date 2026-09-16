#include "game/adventure/adventure_preferences.hpp"
#include "game/adventure/adventure_input.hpp"
#include <json.hpp>
#include <algorithm>
#include <cmath>
#include <limits>
#include <set>
#include <stdexcept>
#include <vector>

namespace voxy::game::adventure {
namespace {
constexpr std::array keys{0,48,49,50,51,52,53,54,55,56,57,
    67,70,72,73,74,75,76,77,78,79,80,81,84,85,86,88,89,90};
constexpr std::array mice{-1,0,2};
constexpr std::array pads{-1,1,3,4,5,6,7,10,12,13,14,15};
constexpr std::array<std::string_view,2> actionIds{"attack","dodge"};
constexpr std::array<std::string_view,2> actionLabels{"Attack","Dodge"};
bool contains(std::span<const int> choices,int value) noexcept {
    return std::find(choices.begin(),choices.end(),value)!=choices.end();
}
std::string mouseLabel(int mouse) {
    return mouse==0?"Left mouse":mouse==2?"Middle mouse":"Unbound";
}
bool exactFields(const nlohmann::json& object,std::initializer_list<std::string_view> names) {
    return object.is_object()&&object.size()==names.size()
        &&std::all_of(names.begin(),names.end(),[&](auto name){return object.contains(name);});
}
bool integer(const nlohmann::json& value,int& output) {
    if(!value.is_number_integer())return false;
    if(value.is_number_unsigned()) {
        const auto number=value.get<uint64_t>();
        if(number>static_cast<uint64_t>(std::numeric_limits<int>::max()))return false;
        output=static_cast<int>(number);
    } else {
        const auto number=value.get<int64_t>();
        if(number<std::numeric_limits<int>::min()||number>std::numeric_limits<int>::max())return false;
        output=static_cast<int>(number);
    }
    return true;
}
}
std::span<const int> combatKeyChoices() noexcept{return keys;}
std::span<const int> combatMouseChoices() noexcept{return mice;}
std::span<const int> combatPadChoices() noexcept{return pads;}
bool validateAdventurePreferences(const AdventurePreferences& p,std::string& error) {
    error="These adventure settings are invalid.";
    const auto range=[](double value,double low,double high){return std::isfinite(value)&&value>=low&&value<=high;};
    if((p.textScale!=1&&p.textScale!=1.25&&p.textScale!=1.5)
        ||!range(p.mouseSensitivity,.25,3)||!range(p.padSensitivity,.25,3)
        ||!range(p.moveDeadzone,.05,.45)||!range(p.lookDeadzone,.05,.45))return false;
    for(size_t i=0;i<p.combat.size();++i) {
        const auto& b=p.combat[i];
        if(!contains(keys,b.key)||!contains(mice,b.mouse)||!contains(pads,b.pad)) {
            error=std::string(actionLabels[i])+": choose an available key, mouse button or controller button.";return false;
        }
    }
    const auto& attack=p.combat[0];const auto& dodge=p.combat[1];
    std::string conflict;
    if(attack.key&&attack.key==dodge.key)conflict=expedition::coveKeyLabel(attack.key);
    else if(attack.mouse>=0&&attack.mouse==dodge.mouse)conflict=mouseLabel(attack.mouse);
    else if(attack.pad>=0&&attack.pad==dodge.pad)conflict=expedition::covePadLabel(attack.pad);
    if(!conflict.empty()){error="Dodge conflicts with Attack: "+conflict+" is already used.";return false;}
    error.clear();return true;
}
bool parseAdventurePreferences(std::string_view text,AdventurePreferences& output,std::string& error) {
    error="Adventure settings are damaged or incompatible. Current controls were kept.";
    if(text.empty()||text.size()>kMaximumAdventurePreferencesBytes)return false;
    try {
        bool duplicate=false;std::vector<std::set<std::string>> objectKeys;
        const auto callback=[&](int depth,nlohmann::json::parse_event_t event,nlohmann::json& value) {
            if(depth>8)throw std::runtime_error("Adventure settings nesting limit");
            if(event==nlohmann::json::parse_event_t::object_start)objectKeys.emplace_back();
            else if(event==nlohmann::json::parse_event_t::key) {
                if(objectKeys.empty()||!objectKeys.back().insert(value.get<std::string>()).second)duplicate=true;
            } else if(event==nlohmann::json::parse_event_t::object_end)objectKeys.pop_back();
            return true;
        };
        const auto json=nlohmann::json::parse(text,callback);
        if(duplicate||!exactFields(json,{"version","textScale","mouseSensitivity","padSensitivity","moveDeadzone","lookDeadzone",
            "highContrast","reducedMotion","invertX","invertY","orbitToggle","combat"}))return false;
        int version=0;if(!integer(json.at("version"),version)||version!=1)return false;
        AdventurePreferences p;
        for(const auto& [name,destination]:std::array<std::pair<std::string_view,double*>,5>{{
            {"textScale",&p.textScale},{"mouseSensitivity",&p.mouseSensitivity},{"padSensitivity",&p.padSensitivity},
            {"moveDeadzone",&p.moveDeadzone},{"lookDeadzone",&p.lookDeadzone}}}) {
            const auto& value=json.at(std::string(name));if(!value.is_number())return false;
            *destination=value.get<double>();
        }
        for(const auto& [name,destination]:std::array<std::pair<std::string_view,bool*>,5>{{
            {"highContrast",&p.highContrast},{"reducedMotion",&p.reducedMotion},{"invertX",&p.invertX},
            {"invertY",&p.invertY},{"orbitToggle",&p.orbitToggle}}}) {
            const auto& value=json.at(std::string(name));if(!value.is_boolean())return false;
            *destination=value.get<bool>();
        }
        const auto& bindings=json.at("combat");if(!bindings.is_array()||bindings.size()!=p.combat.size())return false;
        std::array<bool,2> seen{};
        for(const auto& row:bindings) {
            if(!exactFields(row,{"action","key","mouse","pad"})||!row.at("action").is_string())return false;
            const auto id=row.at("action").get<std::string>();
            const auto found=std::find(actionIds.begin(),actionIds.end(),id);if(found==actionIds.end())return false;
            const size_t i=static_cast<size_t>(found-actionIds.begin());if(seen[i])return false;seen[i]=true;
            auto& b=p.combat[i];
            if(!integer(row.at("key"),b.key)||!integer(row.at("mouse"),b.mouse)||!integer(row.at("pad"),b.pad))return false;
        }
        if(!validateAdventurePreferences(p,error))return false;
        output=p;error.clear();return true;
    } catch(const std::exception&){return false;}
}
bool encodeAdventurePreferences(const AdventurePreferences& p,std::string& output,std::string& error) {
    if(!validateAdventurePreferences(p,error))return false;
    try {
        auto combat=nlohmann::json::array();
        for(size_t i=0;i<p.combat.size();++i) {
            const auto& b=p.combat[i];combat.push_back({{"action",actionIds[i]},{"key",b.key},{"mouse",b.mouse},{"pad",b.pad}});
        }
        const nlohmann::json json{{"version",kAdventurePreferencesVersion},{"textScale",p.textScale},
            {"mouseSensitivity",p.mouseSensitivity},{"padSensitivity",p.padSensitivity},
            {"moveDeadzone",p.moveDeadzone},{"lookDeadzone",p.lookDeadzone},{"highContrast",p.highContrast},
            {"reducedMotion",p.reducedMotion},{"invertX",p.invertX},{"invertY",p.invertY},
            {"orbitToggle",p.orbitToggle},{"combat",std::move(combat)}};
        auto text=json.dump();
        if(text.size()>kMaximumAdventurePreferencesBytes){error="Adventure settings are too large.";return false;}
        output=std::move(text);error.clear();return true;
    } catch(const std::exception&){error="Adventure settings could not be encoded.";return false;}
}
expedition::CoveInputPreferences adventureRoutingPreferences(const AdventurePreferences& p) {
    auto routing=adventureInputDefaults();
    routing.textScale=p.textScale;routing.mouseSensitivity=p.mouseSensitivity;routing.padSensitivity=p.padSensitivity;
    routing.moveDeadzone=p.moveDeadzone;routing.lookDeadzone=p.lookDeadzone;routing.highContrast=p.highContrast;
    routing.invertX=p.invertX;routing.invertY=p.invertY;routing.orbitToggle=p.orbitToggle;
    return routing;
}
std::string combatBindingLabel(const AdventurePreferences& p,CombatAction action,bool gamepad) {
    const auto i=static_cast<size_t>(action);if(i>=p.combat.size())return "Unbound";
    const auto& b=p.combat[i];if(gamepad)return expedition::covePadLabel(b.pad);
    std::string label;if(b.key)label=expedition::coveKeyLabel(b.key);
    if(b.mouse>=0){if(!label.empty())label+=" / ";label+=mouseLabel(b.mouse);}
    return label.empty()?"Unbound":label;
}
void AdventureCombatInputRouter::reset() noexcept{*this=AdventureCombatInputRouter{};}
bool AdventureCombatInputRouter::pressed(CombatAction action) const noexcept {
    const auto i=static_cast<size_t>(action);return i<pressed_.size()&&pressed_[i];
}
void AdventureCombatInputRouter::tick(const expedition::CoveInputSample& s,bool active,const AdventurePreferences& p,
    std::array<bool,3> mousePressed) {
    pressed_.fill(false);
    const bool preferencesChanged=!initialized_||preferences_!=p;
    const bool changed=preferencesChanged||active!=active_||s.focused!=focused_||s.resetSerial!=resetSerial_;
    const bool padChanged=!initialized_||s.padSerial!=padSerial_||s.padConnected!=padConnected_||s.padArmed!=padArmed_;
    if(preferencesChanged){std::string error;valid_=validateAdventurePreferences(p,error);preferences_=p;}
    if(changed) {
        for(size_t i=0;i<blockedKeys_.size();++i)blockedKeys_[i]=s.physicalKeys[i]||s.keys[i]||s.pressed[i];
        blockedPad_=true;blockedMouse_=true;
    }
    if(padChanged)blockedPad_=true;
    initialized_=true;active_=active;focused_=s.focused;resetSerial_=s.resetSerial;
    padSerial_=s.padSerial;padConnected_=s.padConnected;padArmed_=s.padArmed;
    for(size_t i=0;i<blockedKeys_.size();++i)
        if(!s.physicalKeys[i]&&!s.keys[i]&&!s.pressed[i])blockedKeys_[i]=false;
    const std::array mouse{s.mouseLeft,s.mouseRight,s.mouseMiddle};
    if(std::none_of(mouse.begin(),mouse.end(),[](bool held){return held;})
        &&std::none_of(mousePressed.begin(),mousePressed.end(),[](bool edge){return edge;})&&!s.rightPressed)blockedMouse_=false;
    bool padNeutral=std::none_of(s.padDown.begin(),s.padDown.end(),[](bool held){return held;})
        &&std::none_of(s.padPressed.begin(),s.padPressed.end(),[](bool edge){return edge;});
    for(float axis:s.axes)padNeutral=padNeutral&&std::isfinite(axis)&&axis==0;
    if(s.padConnected&&s.padArmed&&padNeutral)blockedPad_=false;
    if(active&&s.focused&&valid_)for(size_t i=0;i<p.combat.size();++i) {
        const auto& b=p.combat[i];
        // Use the captured event modifiers, not modifiers at frame end. A key
        // pressed and released within one frame remains one valid short tap.
        if(b.key>0) {
            const auto key=static_cast<size_t>(b.key);
            pressed_[i]=!blockedKeys_[key]&&s.pressed[key]&&s.pressModifiers[key]==0;
        }
        if(b.mouse>=0&&!blockedMouse_) {
            const auto button=static_cast<size_t>(b.mouse);
            pressed_[i]=pressed_[i]||mousePressed[button]||(mouse[button]&&!previousMouse_[button]);
        }
        if(b.pad>=0&&s.padConnected&&s.padArmed&&!blockedPad_)
            pressed_[i]=pressed_[i]||s.padPressed[static_cast<size_t>(b.pad)];
    }
    previousMouse_=mouse;
}
} // namespace voxy::game::adventure
