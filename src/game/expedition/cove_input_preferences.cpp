#include "game/expedition/cove_input_preferences.hpp"
#include "engine/platform/input.hpp"
#include <json.hpp>
#include <algorithm>
#include <bit>
#include <cmath>
#include <set>
#include <vector>

namespace voxy::game::expedition {
namespace {
constexpr uint8_t world=1,workshop=2,both=3;
constexpr CoveActionInfo action(CoveAction id,std::string_view name,std::string_view label,uint8_t contexts,
    int key=0,int pad=-1,int alternate=0,uint8_t modifiers=0,uint8_t alternateModifiers=0) {
    return {id,name,label,contexts,{key,alternate,modifiers,alternateModifiers,pad}};
}
using A=CoveAction;
constexpr std::array actions{
    action(A::MoveForward,"move_forward","Forward / throttle",world,87,-1,265),
    action(A::MoveBack,"move_back","Back / reverse",world,83,-1,264),
    action(A::MoveLeft,"move_left","Left / steer left",world,65,-1,263),
    action(A::MoveRight,"move_right","Right / steer right",world,68,-1,262),
    action(A::Interact,"interact","Interact",world,69,0),
    action(A::Jump,"jump","Jump",world,32,1),
    action(A::Workshop,"workshop","Open / close workshop",both,66,8),
    action(A::CameraMenu,"camera_menu","Camera / settings menu",world,291,3),
    action(A::ToolsMenu,"tools_menu","Workshop tools menu",workshop,291,9),
    action(A::Pause,"pause","Pause / resume",world,80,9),
    action(A::Save,"save","Save expedition",world,299),
    action(A::Rescue,"rescue","Rescue",world,82),
    action(A::Hook,"hook","Hook / release",world,70,2),
    action(A::ReelIn,"reel_in","Reel in / raise",world,81,7),
    action(A::PayOut,"pay_out","Pay out / lower",world,90,6),
    action(A::AcceptJob,"accept_job","Accept job",world,74,12),
    action(A::Deliver,"deliver","Deliver cargo",world,72,13),
    action(A::PowerHarbor,"power_harbor","Power harbor",world,75,14),
    action(A::Cut,"cut","Cut nearby weld",world,67),
    action(A::CameraMode,"camera_mode","Orbit / chase camera",world,86,10),
    action(A::Recenter,"recenter","Recenter camera",world,71,11),
    action(A::FrameLoad,"frame_load","Frame towed load",world,77),
    action(A::ReducedMotion,"reduced_motion","Reduced motion",world,76),
    action(A::PreviousPart,"previous_part","Previous part",workshop,0,4),
    action(A::NextPart,"next_part","Next part",workshop,258,5),
    action(A::PartLeft,"part_left","Move part left",workshop,263,14),
    action(A::PartRight,"part_right","Move part right",workshop,262,15),
    action(A::PartForward,"part_forward","Move part forward",workshop,265,12),
    action(A::PartBack,"part_back","Move part back",workshop,264,13),
    action(A::PartUp,"part_up","Raise part",workshop,81,7),
    action(A::PartDown,"part_down","Lower part",workshop,90,6),
    action(A::RotateY,"rotate_y","Rotate part Y",workshop,82,2),
    action(A::RotateX,"rotate_x","Rotate part X",workshop,82,-1,0,1),
    action(A::RotateZ,"rotate_z","Rotate part Z",workshop,82,-1,0,4),
    action(A::Snap,"snap","Snap to socket",workshop,84,3),
    action(A::Keep,"keep","Keep / place brick",workshop,69,0),
    action(A::Undo,"undo","Undo draft",workshop,85,-1,90,0,2),
    action(A::Redo,"redo","Redo draft",workshop,89,-1,0,2),
    action(A::Cancel,"cancel","Cancel draft / stop brick",workshop,259,1),
    action(A::Remove,"remove","Remove part",workshop,261),
    action(A::Launch,"launch","Launch expedition",workshop,257),
    action(A::UndoLaunch,"undo_launch","Undo launch",workshop,73),
    action(A::RedoLaunch,"redo_launch","Redo launch",workshop,79),
    action(A::NextType,"next_type","Next part type",workshop,67),
    action(A::AddPart,"add_part","Add selected type",workshop,86),
    action(A::TogglePart,"toggle_part","Enable / disable part",workshop,88),
    action(A::LimitPart,"limit_part","Cycle part limit",workshop,76),
    action(A::ReversePart,"reverse_part","Reverse part drive",workshop,78),
    action(A::RebuildStarter,"rebuild_starter","Rebuild starter",workshop,72),
    action(A::LoadRecovery,"load_recovery","Load recovered design",workshop,75),
    action(A::NextRecovery,"next_recovery","Next recovered design",workshop,74),
    action(A::RemoveRecovery,"remove_recovery","Remove recovery backup",workshop,70),
    action(A::FocusPart,"focus_part","Focus selected part",workshop,71),
    action(A::WholeBoat,"whole_boat","Frame whole boat",workshop,77),
    action(A::Paint,"paint","Next brick color",workshop,89),
    action(A::BrickOne,"brick_one","Build 1 x 2 brick",workshop,49),
    action(A::BrickTwo,"brick_two","Build 2 x 2 brick",workshop,50),
    action(A::BrickThree,"brick_three","Build 2 x 4 brick",workshop,51),
    action(A::SelectAll,"select_all","Select all parts",workshop,65,-1,0,2),
    action(A::OnlyPrimary,"only_primary","Select primary only",workshop),
    action(A::Duplicate,"duplicate","Copy selected parts",workshop,68,-1,0,2),
    action(A::MirrorX,"mirror_x","Mirror across X",workshop),
    action(A::MirrorZ,"mirror_z","Mirror across Z",workshop),
    action(A::Replace,"replace","Replace selected part",workshop,82,-1,0,2),
    action(A::ToggleNext,"toggle_next","Toggle next selection",workshop),
    action(A::OrbitLeft,"orbit_left","Orbit left",workshop,65),
    action(A::OrbitRight,"orbit_right","Orbit right",workshop,68),
    action(A::ZoomIn,"zoom_in","Zoom in",workshop,87),
    action(A::ZoomOut,"zoom_out","Zoom out",workshop,83),
    action(A::PanMode,"pan_mode","Hold for camera pan",workshop,0,11),
    action(A::ZoomMode,"zoom_mode","Hold for camera zoom",workshop,0,10)
};
static_assert(actions.size()==kCoveActionCount);
constexpr size_t index(A actionId) noexcept {return static_cast<size_t>(actionId);}
bool validKey(int key) noexcept {
    return key==0||key==32||(key>=48&&key<=57)||(key>=65&&key<=90)
        ||(key>=257&&key<=265)||(key>=290&&key<=301);
}
bool chordConflict(int key,uint8_t modifiers,const CoveBinding& b) noexcept {
    return key!=0&&((key==b.key&&modifiers==b.modifiers)||(key==b.alternate&&modifiers==b.alternateModifiers));
}
bool exactFields(const nlohmann::json& value,std::initializer_list<std::string_view> names) {
    if(!value.is_object()||value.size()!=names.size())return false;
    return std::all_of(names.begin(),names.end(),[&](auto name){return value.contains(name);});
}
bool integer(const nlohmann::json& object,std::string_view name,int& result) {
    const auto& value=object.at(std::string(name));
    if(!value.is_number_integer())return false;
    if(value.is_number_unsigned()) {
        const auto v=value.get<uint64_t>();if(v>65535)return false;result=static_cast<int>(v);
    }else {const auto v=value.get<int64_t>();if(v< -1||v>65535)return false;result=static_cast<int>(v);}
    return true;
}
uint64_t signature(const CoveInputPreferences& p) noexcept {
    uint64_t hash=1469598103934665603ull;
    const auto add=[&](uint64_t value){hash=(hash^value)*1099511628211ull;};
    for(const auto& b:p.bindings) {add(static_cast<uint64_t>(b.key));add(static_cast<uint64_t>(b.alternate));
        add(b.modifiers);add(b.alternateModifiers);add(static_cast<uint64_t>(b.pad+1));}
    add(std::bit_cast<uint64_t>(p.padSensitivity));add(std::bit_cast<uint64_t>(p.mouseSensitivity));
    add(std::bit_cast<uint64_t>(p.moveDeadzone));add(std::bit_cast<uint64_t>(p.lookDeadzone));
    add(p.invertX);add(p.invertY);add(p.reelToggle);add(p.orbitToggle);return hash;
}
}
std::span<const CoveActionInfo> coveActionList() noexcept {return actions;}
CoveInputPreferences::CoveInputPreferences() {
    for(const auto& a:actions)bindings[index(a.action)]=a.defaults;
}
std::string coveKeyLabel(int key,uint8_t modifiers) {
    std::string result;
    if(key==0)return "Unbound";
    if(modifiers&2)result+="Ctrl + ";
    if(modifiers&1)result+="Shift + ";
    if(modifiers&4)result+="Alt + ";
    if((key>=48&&key<=57)||(key>=65&&key<=90))return result+static_cast<char>(key);
    if(key>=290&&key<=301)return result+"F"+std::to_string(key-289);
    switch(key) {
    case 32:return result+"Space";case 257:return result+"Enter";case 258:return result+"Tab";
    case 259:return result+"Backspace";case 260:return result+"Insert";case 261:return result+"Delete";
    case 262:return result+"Right";case 263:return result+"Left";case 264:return result+"Down";
    case 265:return result+"Up";default:return "Unknown";
    }
}
std::string covePadLabel(int button) {
    constexpr std::array<std::string_view,17> labels{"A / Cross","B / Circle","X / Square","Y / Triangle",
        "LB / L1","RB / R1","LT / L2","RT / R2","View","Menu","L3","R3","D-pad Up","D-pad Down","D-pad Left","D-pad Right","Home"};
    return button>=0&&button<static_cast<int>(labels.size())?std::string(labels[static_cast<size_t>(button)]):"Unbound";
}
bool validateCoveInputPreferences(const CoveInputPreferences& p,std::string& error) {
    error="These control settings are invalid.";
    const auto range=[](double value,double low,double high){return std::isfinite(value)&&value>=low&&value<=high;};
    if(!range(p.mouseSensitivity,.25,3)||!range(p.padSensitivity,.25,3)
        ||!range(p.moveDeadzone,.05,.45)||!range(p.lookDeadzone,.05,.45)
        ||(p.textScale!=1&&p.textScale!=1.25&&p.textScale!=1.5)||p.locale!="en")return false;
    for(size_t i=0;i<p.bindings.size();++i) {
        const auto& b=p.bindings[i];
        if(!validKey(b.key)||!validKey(b.alternate)||b.modifiers>7||b.alternateModifiers>7||b.pad< -1||b.pad>=16
            ||(!b.key&&b.modifiers)||(!b.alternate&&b.alternateModifiers)
            ||(b.key&&b.key==b.alternate&&b.modifiers==b.alternateModifiers))return false;
        // F2 remains a fixed way into menus even after their primary rebind.
        if((b.key==291||b.alternate==291)&&actions[i].action!=A::CameraMenu&&actions[i].action!=A::ToolsMenu) {
            error="F2 is reserved for the menu.";return false;
        }
        // F9 remains the engine presentation toggle; no gameplay binding steals it.
        if(b.key==298||b.alternate==298){error="F9 is reserved for display pacing.";return false;}
        if(b.pad==9&&actions[i].action!=CoveAction::Pause&&actions[i].action!=CoveAction::ToolsMenu){
            error="Menu is reserved for the pause or tools menu.";return false;
        }
        for(size_t j=0;j<i;++j)if((actions[i].contexts&actions[j].contexts)!=0) {
            const auto& other=p.bindings[j];
            if(chordConflict(b.key,b.modifiers,other)||chordConflict(b.alternate,b.alternateModifiers,other)
                ||(b.pad>=0&&b.pad==other.pad)) {
                error="Already used by "+std::string(actions[j].label)+".";return false;
            }
        }
    }
    error.clear();return true;
}
bool rebindCoveAction(CoveInputPreferences& p,A a,CoveBinding binding,std::string& error) {
    if(index(a)>=kCoveActionCount){error="Unknown control.";return false;}
    auto candidate=p;candidate.bindings[index(a)]=binding;
    if(!validateCoveInputPreferences(candidate,error))return false;
    p=std::move(candidate);return true;
}
bool parseCoveInputPreferences(std::string_view text,CoveInputPreferences& output,std::string& error) {
    error="Settings are damaged or incompatible. Current controls were kept.";
    if(text.empty()||text.size()>kMaximumCovePreferencesBytes)return false;
    try {
        bool duplicate=false;std::vector<std::set<std::string>> keys;
        auto callback=[&](int depth,nlohmann::json::parse_event_t event,nlohmann::json& value) {
            if(depth>8)throw std::runtime_error("Settings nesting limit");
            if(event==nlohmann::json::parse_event_t::object_start)keys.emplace_back();
            else if(event==nlohmann::json::parse_event_t::key&&!keys.back().insert(value.get<std::string>()).second)duplicate=true;
            else if(event==nlohmann::json::parse_event_t::object_end)keys.pop_back();
            return true;
        };
        const auto j=nlohmann::json::parse(text,callback);
        if(duplicate||!exactFields(j,{"version","mouseSensitivity","padSensitivity","moveDeadzone","lookDeadzone",
            "invertX","invertY","reelToggle","orbitToggle","textScale","highContrast","tutorialsEnabled","captionsEnabled","locale","bindings"}))return false;
        int version=0;if(!integer(j,"version",version)||version!=1)return false;
        CoveInputPreferences p;
        for(const auto& [name,destination]:std::array<std::pair<std::string_view,double*>,5>{{
            {"mouseSensitivity",&p.mouseSensitivity},{"padSensitivity",&p.padSensitivity},{"moveDeadzone",&p.moveDeadzone},
            {"lookDeadzone",&p.lookDeadzone},{"textScale",&p.textScale}}}) {
            const auto& value=j.at(std::string(name));
            if(!value.is_number())return false;
            *destination=value.get<double>();
        }
        for(const auto& [name,destination]:std::array<std::pair<std::string_view,bool*>,7>{{
            {"invertX",&p.invertX},{"invertY",&p.invertY},{"reelToggle",&p.reelToggle},{"orbitToggle",&p.orbitToggle},
            {"highContrast",&p.highContrast},{"tutorialsEnabled",&p.tutorialsEnabled},{"captionsEnabled",&p.captionsEnabled}}}) {
            const auto& value=j.at(std::string(name));
            if(!value.is_boolean())return false;
            *destination=value.get<bool>();
        }
        if(!j["locale"].is_string())return false;
        p.locale=j["locale"].get<std::string>();
        const auto& bindings=j["bindings"];
        if(!bindings.is_array()||bindings.size()!=kCoveActionCount)return false;
        std::array<bool,kCoveActionCount> seen{};
        for(const auto& row:bindings) {
            if(!exactFields(row,{"action","key","modifiers","alternate","alternateModifiers","pad"})||!row["action"].is_string())return false;
            const auto id=row["action"].get<std::string>();
            const auto found=std::find_if(actions.begin(),actions.end(),[&](const auto& a){return a.id==id;});
            if(found==actions.end())return false;
            const size_t i=index(found->action);
            if(seen[i])return false;
            seen[i]=true;
            CoveBinding b;int mods=0,altMods=0;
            if(!integer(row,"key",b.key)||!integer(row,"alternate",b.alternate)||!integer(row,"pad",b.pad)
                ||!integer(row,"modifiers",mods)||!integer(row,"alternateModifiers",altMods)||mods<0||mods>7||altMods<0||altMods>7)return false;
            b.modifiers=static_cast<uint8_t>(mods);b.alternateModifiers=static_cast<uint8_t>(altMods);p.bindings[i]=b;
        }
        if(!validateCoveInputPreferences(p,error))return false;
        output=std::move(p);error.clear();return true;
    }catch(const std::exception&){return false;}
}
bool encodeCoveInputPreferences(const CoveInputPreferences& p,std::string& output,std::string& error) {
    if(!validateCoveInputPreferences(p,error))return false;
    try {
        auto bindings=nlohmann::json::array();
        for(const auto& a:actions) {
            const auto& b=p.bindings[index(a.action)];
            bindings.push_back({{"action",a.id},{"key",b.key},{"modifiers",b.modifiers},{"alternate",b.alternate},
                {"alternateModifiers",b.alternateModifiers},{"pad",b.pad}});
        }
        nlohmann::json j{{"version",1},{"mouseSensitivity",p.mouseSensitivity},{"padSensitivity",p.padSensitivity},
            {"moveDeadzone",p.moveDeadzone},{"lookDeadzone",p.lookDeadzone},{"invertX",p.invertX},{"invertY",p.invertY},
            {"reelToggle",p.reelToggle},{"orbitToggle",p.orbitToggle},{"textScale",p.textScale},{"highContrast",p.highContrast},
            {"tutorialsEnabled",p.tutorialsEnabled},{"captionsEnabled",p.captionsEnabled},{"locale",p.locale},{"bindings",std::move(bindings)}};
        auto text=j.dump();if(text.size()>kMaximumCovePreferencesBytes)return false;
        output=std::move(text);error.clear();return true;
    }catch(const std::exception&){error="Settings could not be encoded.";return false;}
}
CoveInputSample sampleCoveInput(const voxy::Input& input) {
    CoveInputSample s;s.focused=input.focused();s.resetSerial=input.resetSerial();
    s.modifiers=static_cast<uint8_t>(((input.isKeyDown(Key::LeftShift)||input.isKeyDown(Key::RightShift))?1:0)
        |((input.isKeyDown(Key::LeftControl)||input.isKeyDown(Key::RightControl))?2:0)
        |((input.isKeyDown(Key::LeftAlt)||input.isKeyDown(Key::RightAlt))?4:0));
    for(size_t i=0;i<s.keys.size();++i) {
        const auto key=static_cast<Key>(i);s.keys[i]=input.isKeyDown(key);s.pressed[i]=input.wasKeyPressed(key);
        s.physicalKeys[i]=input.physicalKeyDown(key);s.pressModifiers[i]=input.keyPressModifiers(key);
    }
    const auto& pad=input.gamepad();s.padConnected=pad.connected();s.padArmed=pad.armed();s.padSerial=pad.serial();
    for(size_t i=0;i<s.padDown.size();++i) {const auto b=static_cast<PadButton>(i);s.padDown[i]=pad.down(b);s.padPressed[i]=pad.pressed(b);}
    for(size_t i=0;i<s.axes.size();++i)s.axes[i]=pad.axis(i);
    s.mouseLeft=input.physicalMouseDown(MouseButton::Left);s.mouseRight=input.physicalMouseDown(MouseButton::Right);
    s.mouseMiddle=input.physicalMouseDown(MouseButton::Middle);s.rightPressed=input.wasMouseButtonPressed(MouseButton::Right);return s;
}
void CoveInputRouter::reset() noexcept {*this=CoveInputRouter{};}
CoveActionState CoveInputRouter::state(A a) const noexcept {return index(a)<states_.size()?states_[index(a)]:CoveActionState{};}
void CoveInputRouter::tick(const CoveInputSample& s,CoveInputContext context,const CoveInputPreferences& p) {
    const auto previous=states_;states_.fill({});movement_={};look_={};
    const auto currentSignature=signature(p);
    const bool changed=!initialized_||context!=context_||s.resetSerial!=resetSerial_||s.focused!=focused_||currentSignature!=preferenceSignature_;
    const bool padChanged=s.padSerial!=padSerial_||s.padConnected!=padConnected_||s.padArmed!=padArmed_;
    if(changed) {
        for(size_t i=0;i<blockedKeys_.size();++i)blockedKeys_[i]=s.physicalKeys[i]||s.keys[i]||s.pressed[i];
        blockedPad_=true;blockedMouse_=true;reelDirection_=0;orbitDrag_=false;
    }
    if(padChanged){blockedPad_=true;reelDirection_=0;}
    const bool mouseWasArmed=!blockedMouse_;
    initialized_=true;context_=context;resetSerial_=s.resetSerial;focused_=s.focused;preferenceSignature_=currentSignature;
    padSerial_=s.padSerial;padConnected_=s.padConnected;padArmed_=s.padArmed;
    bool padNeutral=std::none_of(s.padDown.begin(),s.padDown.end(),[](bool b){return b;});
    for(float axis:s.axes)padNeutral=padNeutral&&std::isfinite(axis)&&axis==0;
    if(padNeutral&&!std::any_of(s.padPressed.begin(),s.padPressed.end(),[](bool b){return b;}))blockedPad_=false;
    for(size_t i=0;i<blockedKeys_.size();++i)if(!s.physicalKeys[i]&&!s.keys[i]&&!s.pressed[i])blockedKeys_[i]=false;
    if(!s.mouseLeft&&!s.mouseRight&&!s.mouseMiddle&&!s.rightPressed)blockedMouse_=false;
    const bool allowed=s.focused&&context!=CoveInputContext::Menu;
    mouseGesturesAllowed_=allowed&&mouseWasArmed;
    const bool padAllowed=allowed&&s.padConnected&&s.padArmed&&!blockedPad_;
    if(allowed)for(const auto& a:actions)if((a.contexts&static_cast<uint8_t>(context))!=0) {
        const auto& b=p.bindings[index(a.action)];auto& out=states_[index(a.action)];
        const auto key=[&](int code,uint8_t mods) {
            if(code<=0||code>=512||blockedKeys_[static_cast<size_t>(code)])return;
            const auto i=static_cast<size_t>(code);
            out.down=out.down||(s.keys[i]&&s.modifiers==mods);
            out.pressed=out.pressed||(s.pressed[i]&&s.pressModifiers[i]==mods);
        };
        key(b.key,b.modifiers);key(b.alternate,b.alternateModifiers);
        if(padAllowed&&b.pad>=0&&b.pad<17) {
            const auto i=static_cast<size_t>(b.pad);out.down=out.down||s.padDown[i];out.pressed=out.pressed||s.padPressed[i];
        }
    }
    if(allowed&&context==CoveInputContext::World&&p.reelToggle) {
        const bool in=pressed(A::ReelIn),out=pressed(A::PayOut);
        if(in&&out)reelDirection_=0;
        else if(in)reelDirection_=reelDirection_==1?0:1;
        else if(out)reelDirection_=reelDirection_== -1?0:-1;
        states_[index(A::ReelIn)].down=reelDirection_==1;states_[index(A::PayOut)].down=reelDirection_== -1;
        // A toggled motor emits a start edge only when it starts. A second press
        // is a stop/release, never another motor start command.
    }
    if(down(A::ReelIn)&&down(A::PayOut)){states_[index(A::ReelIn)].down=false;states_[index(A::PayOut)].down=false;}
    for(const auto a:{A::ReelIn,A::PayOut})states_[index(a)].pressed=down(a)&&!previous[index(a)].down;
    if(!allowed)reelDirection_=0;
    if(allowed&&!blockedMouse_) {
        if(p.orbitToggle) {if(s.rightPressed)orbitDrag_=!orbitDrag_;}
        else orbitDrag_=s.mouseRight;
    }else orbitDrag_=false;
    for(size_t i=0;i<states_.size();++i)states_[i].released=previous[i].down&&!states_[i].down;
    if(allowed&&context==CoveInputContext::World) {
        movement_={double(down(A::MoveRight))-double(down(A::MoveLeft)),double(down(A::MoveForward))-double(down(A::MoveBack))};
        if(padAllowed){movement_[0]+=double(s.axes[0]);movement_[1]-=double(s.axes[1]);}
        for(double& value:movement_)value=std::isfinite(value)?std::clamp(value,-1.,1.):0.;
    }
    if(padAllowed) {
        look_={double(s.axes[2])*p.padSensitivity*(p.invertX?-1:1),double(s.axes[3])*p.padSensitivity*(p.invertY?-1:1)};
        for(double& value:look_)if(!std::isfinite(value))value=0;
    }
}
} // namespace voxy::game::expedition
