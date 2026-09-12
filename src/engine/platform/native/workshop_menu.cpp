#include "engine/platform/native/workshop_menu.hpp"
#include "game/expedition/brick_paint.hpp"
#include <algorithm>
#include <cmath>

namespace voxy::platform {
using namespace game::expedition;

NativeWorkshopMenu::NativeWorkshopMenu(std::filesystem::path root, Action action, Blueprint blueprint, Preferences preferences)
    : action_(std::move(action)), blueprint_(std::move(blueprint)),preferences_(std::move(preferences)),
      library_(std::move(root), [this](std::span<const std::byte> bytes) {
          return blueprint_ && blueprint_(3, designBlueprintHex(bytes)) == "ok";
      }) {}
bool NativeWorkshopMenu::active() const noexcept { return page_ != Page::Closed; }
void NativeWorkshopMenu::page(Page value) {
    page_ = value; content_.selected = 0; content_.naming = value == Page::Naming;
    status_.clear();
}
void NativeWorkshopMenu::open() { page(Page::Main); }
void NativeWorkshopMenu::openCamera() {cameraReturn_=Page::Closed;page(Page::PlayerCamera);}
void NativeWorkshopMenu::openGame() {page(Page::Game);}
void NativeWorkshopMenu::openJob() {page(Page::Job);}
void NativeWorkshopMenu::openHelp() {helpReturn_=Page::Game;page(Page::Help);}
void NativeWorkshopMenu::dismiss() { page(Page::Closed); }
bool NativeWorkshopMenu::generalPage() const noexcept {return page_>=Page::Game;}
std::string_view NativeWorkshopMenu::pageName() const noexcept {
    switch (page_) {
    case Page::Closed: return "closed";
    case Page::Main: return "main";
    case Page::Selection: return "selection";
    case Page::Move: return "move";
    case Page::Parts: return "parts";
    case Page::Paint: return "paint";
    case Page::Settings: return "settings";
    case Page::Camera: return "camera";
    case Page::PlayerCamera: return "player-camera";
    case Page::Library: return "library";
    case Page::Design: return "design";
    case Page::Imports: return "imports";
    case Page::Naming: return "naming";
    case Page::Remove: return "remove";
    case Page::Game:return "game";
    case Page::Job:return "job";
    case Page::Map:return "map";
    case Page::Inventory:return "inventory";
    case Page::Accessibility:return "accessibility";
    case Page::Bindings:return "bindings";
    case Page::Binding:return "binding";
    case Page::CaptureKey:return "capture-key";
    case Page::ChooseKey:return "choose-key";
    case Page::ChoosePad:return "choose-pad";
    case Page::Help:return "help";
    case Page::Title:return "title";
    case Page::Worlds:return "worlds";
    case Page::Confirm:return "confirm";
    }
    return "closed";
}
const NativeDesignLibrary::Row* NativeWorkshopMenu::chosen() const {
    const auto& rows = library_.rows();
    const auto found = std::find_if(rows.begin(), rows.end(), [&](const auto& row) { return row.id == selectedId_; });
    return found == rows.end() ? nullptr : &*found;
}
void NativeWorkshopMenu::rebuild(const Facts& facts) {
    const bool enabled = facts.workshopOpen && !facts.pending;
    const bool libraryReady = enabled && library_.ready();
    content_.subtitle = facts.selectedName;
    content_.status = status_.empty() ? facts.message : status_;
    content_.naming = page_ == Page::Naming;
    choices_.clear();
    const auto add = [&](std::string label, int action, bool available = true) {
        choices_.push_back({{std::move(label), enabled && available}, action, Page::Closed, 0, 0});
    };
    const auto submenu = [&](std::string label, Page destination, bool available = true) {
        choices_.push_back({{std::move(label), available}, -1, destination, 0, 0});
    };
    const auto gameAction=[&](std::string label,int id,bool available=true) {
        choices_.push_back({{std::move(label),facts.gameAvailable&&available},id,Page::Closed,0,0});
    };
    const auto information=[&](const std::vector<std::string>& lines) {
        for(size_t i=0;i<std::min(lines.size(),size_t(64));++i)
            choices_.push_back({{lines[i],false},-1,Page::Closed,0,0});
    };
    static const CoveInputPreferences defaults;
    const auto& prefs=facts.preferences?*facts.preferences:defaults;
    switch (page_) {
    case Page::Closed: content_ = {}; return;
    case Page::Main:
        content_.title = "Workshop tools";
        submenu("Select and group", Page::Selection);
        submenu("Move and rotate", Page::Move);
        submenu("Choose parts", Page::Parts);
        submenu("Paint bricks", Page::Paint, facts.canPaint);
        submenu("Module settings", Page::Settings, facts.canConfigure);
        submenu("Camera", Page::Camera);
        submenu("Saved designs", Page::Library);
        add("Keep change", 71, facts.canKeep);
        add("Undo draft", 72, facts.canUndo); add("Redo draft", 306, facts.canRedo);
        add("Cancel change", 73); add("Stop brick tool", 96, facts.brushActive);
        add("Launch boat", 79, facts.canLaunch);
        add("Undo launch", 80, facts.canUndoLaunch); add("Redo launch", 81, facts.canRedoLaunch);
        add("Test sail (temporary copy)",340,facts.testAvailable&&!facts.testBusy);
        submenu("Controls and accessibility",Page::Accessibility,bool(preferences_));
        submenu("Help",Page::Help);
        submenu("Back to building", Page::Closed);
        break;
    case Page::Selection:
        content_.title = "Select and group";
        content_.subtitle = std::to_string(facts.selectedCount) + " selected: " + facts.selectedName;
        add("Previous part", 61); add("Next part", 62); add("Toggle next in group", 309);
        add("Select whole boat", 300); add("Select only this part", 301);
        add("Duplicate group", 302, facts.selectedCount > 0 && facts.canAdd);
        add("Mirror across X = 0", 303, facts.selectedCount > 0);
        add("Mirror across Z = 0", 304, facts.selectedCount > 0);
        add("Replace with chosen part", 305, facts.selectedCount > 0);
        add("Remove selection", 74, facts.selectedCount > 0);
        add("Keep change", 71, facts.canKeep); add("Undo draft", 72, facts.canUndo); add("Redo draft", 306, facts.canRedo);
        submenu("Back", Page::Main); break;
    case Page::Move:
        content_.title = "Move and rotate";
        add("Left", 63); add("Right", 64); add("Forward", 65); add("Back", 66);
        add("Up one plate", 67); add("Down one plate", 68);
        add("Rotate around Y", 69); add("Rotate around X", 307); add("Rotate around Z", 308);
        add("Snap to free socket", 70); add("Keep change", 71, facts.canKeep);
        add("Undo draft", 72, facts.canUndo); add("Redo draft", 306, facts.canRedo); submenu("Back", Page::Main); break;
    case Page::Parts:
        content_.title = "Choose parts";
        for (size_t index = 0; index < facts.catalogNames.size(); ++index) {
            // First choose a catalogue item without adding it; replacement uses
            // that choice. Add is explicit, preserving unsupported-type refusal.
            add((index == facts.catalogIndex ? "* " : "") + facts.catalogNames[index], -1000 - static_cast<int>(index));
        }
        add("Add chosen part", 84, facts.canAdd); submenu("Back", Page::Main); break;
    case Page::Paint:
        content_.title = "Paint bricks";
        for (size_t index = 0; index < kBrickPaintPalette.size(); ++index)
            add((facts.paintIndex && *facts.paintIndex == index ? "* " : "") + std::string(kBrickPaintPalette[index].name), 200 + static_cast<int>(index), facts.canPaint);
        add("Keep paint", 71, facts.canKeep); add("Undo draft", 72, facts.canUndo); submenu("Back", Page::Main); break;
    case Page::Settings:
        content_.title = "Module settings";
        add("Toggle enabled", 85, facts.canConfigure); add("Cycle output limit", 86, facts.hasOutputLimit);
        add("Reverse drive", 87, facts.canReverse); add("Keep settings", 71, facts.canKeep); submenu("Back", Page::Main); break;
    case Page::Camera:
        content_.title = "Camera";
        add("Focus selection", 97); add("Frame whole boat", 98); add("Orbit left", 75); add("Orbit right", 76);
        add("Zoom in", 77); add("Zoom out", 78); submenu("Back", Page::Main); break;
    case Page::PlayerCamera: {
        content_.title = "Camera options";
        content_.subtitle = "Choose your view";
        content_.status = status_;
        const bool cameraEnabled = facts.cameraAvailable && !facts.workshopOpen;
        const auto cameraAction = [&](std::string label,int action,bool available=true) {
            choices_.push_back({{std::move(label),cameraEnabled&&available},action,Page::Closed,0,0});
        };
        cameraAction(facts.chaseCamera?"View: Chase":"View: Orbit",320);
        cameraAction("Recenter behind robot",321);
        cameraAction(facts.frameLoad?"Frame load: On":"Frame load: Off",322);
        cameraAction(facts.reducedMotion?"Reduced motion: On":"Reduced motion: Off",323);
        const bool validDistance=std::isfinite(facts.cameraDistance)
            &&facts.cameraDistance>=1.5&&facts.cameraDistance<=12.;
        cameraAction("Move camera closer",324,validDistance&&facts.cameraDistance>1.5);
        cameraAction("Move camera farther",325,validDistance&&facts.cameraDistance<12.);
        submenu("Controls and accessibility",Page::Accessibility,bool(preferences_));
        submenu("Help",Page::Help);
        submenu(cameraReturn_==Page::Game?"Back":"Back to game",cameraReturn_);
        break;
    }
    case Page::Library:
        content_.title = "Saved designs"; content_.subtitle = library_.root().string();
        content_.status = status_.empty() ? library_.message() : status_;
        add("Save boat as new", -10, libraryReady);
        add("Export current boat", -14, libraryReady);
        submenu("Import a design file", Page::Imports, libraryReady);
        for (const auto& row : library_.rows()) choices_.push_back({{row.name, libraryReady}, -1, Page::Design, row.id, row.revision});
        submenu("Back", Page::Main); break;
    case Page::Design: {
        content_.title = "Saved design"; content_.subtitle = chosen() ? chosen()->name : "Choose a saved design";
        content_.status = status_.empty() ? library_.message() : status_;
        const bool exists = chosen() && chosen()->revision == selectedRevision_;
        add("Load into workshop", -20, libraryReady && exists && facts.canLoadBlueprint);
        add("Update from current boat", -11, libraryReady && exists);
        add("Copy with a new name", -12, libraryReady && exists);
        add("Rename saved design", -13, libraryReady && exists);
        add("Restore previous version", -21, libraryReady && exists && chosen()->backupAvailable);
        submenu("Remove saved design", Page::Remove, libraryReady && exists);
        submenu("Back to saved designs", Page::Library); break;
    }
    case Page::Imports:
        content_.title = "Import design"; content_.subtitle = library_.importsPath().string();
        content_.status = status_.empty() ? library_.message() : status_;
        add("Refresh file list", -22, libraryReady);
        for (size_t index = 0; index < library_.imports().size(); ++index)
            add(library_.imports()[index], -2000 - static_cast<int>(index), libraryReady);
        submenu("Back", Page::Library); break;
    case Page::Naming:
        content_.title = naming_ == Naming::Rename ? "Rename design" : naming_ == Naming::Duplicate ? "Name the copy"
            : naming_ == Naming::Export ? "Name the export" : naming_ == Naming::Update ? "Update saved design" : "Name this boat";
        content_.subtitle = "Type, or use the letter grid";
        content_.status = status_;
        add("Done", -30, libraryReady); add("Backspace", -31); add("Cancel", -32); break;
    case Page::Remove:
        content_.title = "Remove this design?"; content_.subtitle = chosen() ? chosen()->name : "";
        content_.status = "The saved design and its backup will be removed.";
        add("Remove design and backup", -23, libraryReady && chosen() && chosen()->revision == selectedRevision_);
        submenu("Cancel", Page::Design); break;
    case Page::Game:
        content_.title=facts.testActive?"Test sail":"Expedition menu";
        content_.subtitle=facts.pausePending?"Waiting for movement to stop":facts.paused?"Paused":"Game controls";
        content_.status=status_.empty()?(facts.testActive?facts.practiceMessage:facts.saveMessage):status_;
        gameAction("Resume",91,facts.canResume&&!facts.pausePending);
        gameAction("Save expedition",350,facts.canSave&&!facts.testActive);
        submenu("Job board",Page::Job);submenu("Nearby map",Page::Map);submenu("Inventory",Page::Inventory);
        gameAction(facts.workshopOpen?"Return to building":facts.paused?"Resume and open workshop":"Open workshop",-53,
            !facts.pending&&!facts.testActive&&!facts.pausePending&&(!facts.paused||facts.canResume));
        submenu("Camera",Page::PlayerCamera,facts.cameraAvailable);
        submenu("Controls and accessibility",Page::Accessibility,bool(preferences_));submenu("Help",Page::Help);
        gameAction("Return from test sail",341,facts.testActive&&facts.canReturnTest&&!facts.testBusy);
        gameAction("Rescue to dock",1,facts.canRescue&&!facts.testActive);
        submenu("Expeditions",Page::Title,facts.canChangeWorld);
        gameAction("Quit game...",-50,facts.canQuit);break;
    case Page::Job:
        content_.title=facts.jobTitle.empty()?"Job board":facts.jobTitle;content_.subtitle=facts.jobText;content_.status=status_;
        if(facts.paused)gameAction("Resume expedition",91,facts.canResume&&!facts.pausePending);
        gameAction(facts.jobActionLabel.empty()?"Continue job":facts.jobActionLabel,facts.jobAction,facts.jobActionEnabled&&facts.jobAction>0);
        submenu("Help",Page::Help);submenu("Back",Page::Game);break;
    case Page::Map:
        content_.title="Nearby map";content_.subtitle="Distances from your robot";content_.status=status_;
        information(facts.mapLines);submenu("Back",Page::Game);break;
    case Page::Inventory:
        content_.title="Inventory";content_.subtitle="Owned parts and material";content_.status=status_;
        information(facts.inventoryLines);submenu("Back",Page::Game);break;
    case Page::Accessibility: {
        content_.title="Controls and accessibility";content_.subtitle="Changes apply immediately";
        content_.status=status_.empty()?facts.preferencesMessage:status_;
        const auto option=[&](std::string label,int command){choices_.push_back({{std::move(label),bool(preferences_)},command,Page::Closed,0,0});};
        option("Text size: "+std::to_string(static_cast<int>(prefs.textScale*100))+"%",-100);
        option(prefs.highContrast?"High contrast: On":"High contrast: Off",-101);
        option(prefs.tutorialsEnabled?"Guidance: On":"Guidance: Off",-102);
        option(prefs.captionsEnabled?"Captions: On":"Captions: Off",-103);
        option("Mouse speed: "+std::to_string(static_cast<int>(prefs.mouseSensitivity*100))+"%",-104);
        option("Stick speed: "+std::to_string(static_cast<int>(prefs.padSensitivity*100))+"%",-105);
        option("Move deadzone: "+std::to_string(static_cast<int>(std::lround(prefs.moveDeadzone*100)))+"%",-106);
        option("Look deadzone: "+std::to_string(static_cast<int>(std::lround(prefs.lookDeadzone*100)))+"%",-107);
        option(prefs.invertX?"Invert camera X: On":"Invert camera X: Off",-108);
        option(prefs.invertY?"Invert camera Y: On":"Invert camera Y: Off",-109);
        option(prefs.reelToggle?"Winch: Press to start / stop":"Winch: Hold to run",-110);
        option(prefs.orbitToggle?"Mouse orbit: Press to start / stop":"Mouse orbit: Hold to turn",-111);
        submenu("Remap controls",Page::Bindings,bool(preferences_));
        choices_.push_back({{"Language: English",false},-1,Page::Closed,0,0});
        option("Restore default controls...",-112);submenu("Back",settingsReturn_);break;
    }
    case Page::Bindings:
        content_.title="Remap controls";content_.subtitle="Escape and menu navigation stay available";
        content_.status=status_;
        for(const auto& info:coveActionList()) {
            const auto& b=prefs.bindings[static_cast<size_t>(info.action)];
            choices_.push_back({{std::string(info.label)+": "+coveKeyLabel(b.key,b.modifiers),bool(preferences_)},
                -4000-static_cast<int>(info.action),Page::Closed,0,0});
        }
        submenu("Back",Page::Accessibility);break;
    case Page::Binding:
        content_.title=std::string(coveActionList()[static_cast<size_t>(bindingAction_)].label);
        content_.subtitle="Change choices, then Apply";content_.status=status_;
        choices_.push_back({{"Primary: "+coveKeyLabel(bindingDraft_.key,bindingDraft_.modifiers),true},-120,Page::Closed,0,0});
        choices_.push_back({{"Alternate: "+coveKeyLabel(bindingDraft_.alternate,bindingDraft_.alternateModifiers),true},-121,Page::Closed,0,0});
        choices_.push_back({{"Primary modifiers: "+coveKeyLabel(32,bindingDraft_.modifiers),true},-127,Page::Closed,0,0});
        choices_.push_back({{"Alternate modifiers: "+coveKeyLabel(32,bindingDraft_.alternateModifiers),true},-128,Page::Closed,0,0});
        submenu("Controller: "+covePadLabel(bindingDraft_.pad),Page::ChoosePad);
        choices_.push_back({{"Clear primary key",true},-122,Page::Closed,0,0});
        choices_.push_back({{"Clear alternate key",true},-123,Page::Closed,0,0});
        choices_.push_back({{"Apply binding",bool(preferences_)},-124,Page::Closed,0,0});
        choices_.push_back({{"Restore this control",bool(preferences_)},-125,Page::Closed,0,0});
        submenu("Back",Page::Bindings);break;
    case Page::CaptureKey:
        content_.title="Press the new key";content_.subtitle="Hold Ctrl, Shift or Alt for a shortcut";content_.status=status_;
        submenu("Choose from key list",Page::ChooseKey);submenu("Cancel",Page::Binding);break;
    case Page::ChooseKey:
        content_.title="Choose a key";content_.subtitle="Controller-friendly key list";content_.status=status_;
        for(int key=32;key<=301;++key) {
            if(!(key==32||(key>=48&&key<=57)||(key>=65&&key<=90)||(key>=257&&key<=265)||(key>=290&&key<=301))||key==298)continue;
            choices_.push_back({{coveKeyLabel(key),true},-3000-key,Page::Closed,0,0});
        }
        choices_.push_back({{"Cycle modifiers",true},-126,Page::Closed,0,0});submenu("Back",Page::Binding);break;
    case Page::ChoosePad:
        content_.title="Choose controller button";content_.subtitle="Menu navigation always keeps its standard buttons";content_.status=status_;
        for(int button=-1;button<16;++button)choices_.push_back({{covePadLabel(button),
            button!=9||bindingAction_==CoveAction::Pause||bindingAction_==CoveAction::ToolsMenu},-3500-(button+1),Page::Closed,0,0});
        submenu("Back",Page::Binding);break;
    case Page::Help:
        content_.title=facts.tutorialTitle.empty()?"Quick help":facts.tutorialTitle;
        content_.subtitle=facts.tutorialText;content_.status=status_;
        information(facts.helpLines);
        gameAction(facts.tutorialActionLabel.empty()?"Next action":facts.tutorialActionLabel,facts.tutorialAction,
            !facts.tutorialComplete&&facts.tutorialAction>0&&!facts.pending);
        gameAction("Restart guidance",352);submenu("Back",helpReturn_);break;
    case Page::Title:
        content_.title="Expeditions";content_.subtitle="Leaving keeps only confirmed saves";content_.status=status_;
        gameAction("New expedition...",-51,facts.canChangeWorld);submenu("Load saved Cove",Page::Worlds,facts.canChangeWorld&&!facts.worldNames.empty());
        submenu("Controls and accessibility",Page::Accessibility,bool(preferences_));submenu("Help",Page::Help);submenu("Back",Page::Game);break;
    case Page::Worlds:
        content_.title="Load saved Cove";content_.subtitle="Choose a confirmed saved world";content_.status=status_;
        for(size_t i=0;i<std::min(facts.worldNames.size(),size_t(64));++i)gameAction(facts.worldNames[i],-5000-static_cast<int>(i),facts.canChangeWorld);
        submenu("Back",Page::Title);break;
    case Page::Confirm:
        content_.title=confirmedAction_==354?"Quit this game?":confirmedAction_==360?"Start a new expedition?":confirmedAction_== -112?"Restore all controls?":"Load this saved Cove?";
        content_.subtitle=confirmedAction_== -112?"Your custom bindings will be replaced":"Only confirmed saves will be kept";
        content_.status=status_;
        choices_.push_back({{"Cancel",true},-1,confirmReturn_,0,0});
        choices_.push_back({{"Confirm",confirmedAction_== -112?bool(preferences_):facts.gameAvailable&&(confirmedAction_==354?facts.canQuit:facts.canChangeWorld)},-52,Page::Closed,0,0});break;
    }
    content_.rows.clear(); content_.rows.reserve(choices_.size());
    for (const auto& choice : choices_) content_.rows.push_back(choice.row);
    content_.selected = choices_.empty() ? 0 : std::min(content_.selected, choices_.size() - 1);
}
std::vector<std::byte> NativeWorkshopMenu::capture() {
    std::vector<std::byte> bytes;
    if (!blueprint_ || !parseDesignBlueprintHex(blueprint_(1, {}), bytes)) status_ = "Keep or cancel changes before saving.";
    return bytes;
}
void NativeWorkshopMenu::name(Naming operation, std::string initial) {
    naming_ = operation; namingReturn_ = page_; page(Page::Naming);
    content_.name = std::move(initial); content_.key = 0; content_.keyboardFocus = true; replaceName_ = !content_.name.empty();
}
void NativeWorkshopMenu::eraseCharacter() {
    if (replaceName_) { content_.name.clear(); replaceName_ = false; return; }
    if (content_.name.empty()) return;
    size_t index = content_.name.size() - 1;
    while (index > 0 && (static_cast<unsigned char>(content_.name[index]) & 0xc0u) == 0x80u) --index;
    content_.name.resize(index);
}
void NativeWorkshopMenu::appendText(std::string_view text) {
    if (replaceName_) { content_.name.clear(); replaceName_ = false; }
    if (content_.name.size() + text.size() > kMaximumDesignNameBytes) { status_ = "Name is full (96 UTF-8 bytes)."; return; }
    content_.name.append(text); status_.clear();
}
void NativeWorkshopMenu::finishName() {
    if (!validDesignName(content_.name)) { status_ = "Use a short name, with no leading or trailing spaces."; return; }
    bool accepted = false;
    if (naming_ == Naming::Duplicate) accepted = library_.duplicate(selectedId_, selectedRevision_, content_.name);
    else if (naming_ == Naming::Rename) accepted = library_.rename(selectedId_, selectedRevision_, content_.name);
    else {
        auto bytes = capture(); if (bytes.empty()) return;
        if (naming_ == Naming::SaveNew) accepted = library_.saveNew(content_.name, std::move(bytes));
        else if (naming_ == Naming::Update) accepted = library_.update(selectedId_, selectedRevision_, content_.name, std::move(bytes));
        else accepted = library_.exportFile(content_.name, std::move(bytes));
    }
    if (accepted) page(namingReturn_); else status_ = library_.message();
}
void NativeWorkshopMenu::back() {
    switch (page_) {
    case Page::Closed: break;
    case Page::Main:dismiss();break;
    case Page::PlayerCamera:page(cameraReturn_);break;
    case Page::Naming: page(namingReturn_); break;
    case Page::Design: case Page::Imports: page(Page::Library); break;
    case Page::Remove: page(Page::Design); break;
    case Page::Game:dismiss();break;
    case Page::Job:case Page::Map:case Page::Inventory:case Page::Title:page(Page::Game);break;
    case Page::Accessibility:page(settingsReturn_);break;
    case Page::Bindings:page(Page::Accessibility);break;
    case Page::Binding:page(Page::Bindings);break;
    case Page::CaptureKey:case Page::ChooseKey:case Page::ChoosePad:page(Page::Binding);break;
    case Page::Help:page(helpReturn_);break;
    case Page::Worlds:page(Page::Title);break;
    case Page::Confirm:page(confirmReturn_);break;
    default: page(Page::Main); break;
    }
}
void NativeWorkshopMenu::closeGame(const Facts& facts) {
    if(facts.pausePending){status_="Waiting for movement to stop.";return;}
    if(!facts.paused){dismiss();return;}
    if(facts.canResume&&action_&&action_(91))dismiss();
}
void NativeWorkshopMenu::applyPreferences(const CoveInputPreferences& preferences) {
    std::string text,error;
    if(!encodeCoveInputPreferences(preferences,text,error)){status_=error;return;}
    if(!preferences_){status_="Settings are unavailable.";return;}
    const auto result=preferences_(2,text);status_=result=="ok"?"":result;
}
void NativeWorkshopMenu::updatePreference(int command,const Facts& facts) {
    auto candidate=facts.preferences?*facts.preferences:CoveInputPreferences{};
    const auto speed=[](double value){return value>=3? .25:value+.25;};
    const auto zone=[](double value){const auto step=std::lround(value*20);return step>=9?.05:double(step+1)/20;};
    switch(command) {
    case -100:candidate.textScale=candidate.textScale==1?1.25:candidate.textScale==1.25?1.5:1;break;
    case -101:candidate.highContrast=!candidate.highContrast;break;
    case -102:candidate.tutorialsEnabled=!candidate.tutorialsEnabled;break;
    case -103:candidate.captionsEnabled=!candidate.captionsEnabled;break;
    case -104:candidate.mouseSensitivity=speed(candidate.mouseSensitivity);break;
    case -105:candidate.padSensitivity=speed(candidate.padSensitivity);break;
    case -106:candidate.moveDeadzone=zone(candidate.moveDeadzone);break;
    case -107:candidate.lookDeadzone=zone(candidate.lookDeadzone);break;
    case -108:candidate.invertX=!candidate.invertX;break;
    case -109:candidate.invertY=!candidate.invertY;break;
    case -110:candidate.reelToggle=!candidate.reelToggle;break;
    case -111:candidate.orbitToggle=!candidate.orbitToggle;break;
    default:return;
    }
    applyPreferences(candidate);
}
void NativeWorkshopMenu::activate(size_t index, const Facts& facts) {
    if (index >= choices_.size() || !choices_[index].row.enabled) return;
    const auto choice = choices_[index];
    if(choice.action<=-5000) {
        const auto world=static_cast<size_t>(-5000-choice.action);
        if(world>=facts.worldNames.size()||world>=64||!facts.canChangeWorld)return;
        confirmReturn_=Page::Worlds;confirmedAction_=4000+static_cast<int>(world);page(Page::Confirm);return;
    }
    if(choice.action<=-4000) {
        const auto id=static_cast<size_t>(-4000-choice.action);if(id>=kCoveActionCount)return;
        bindingAction_=static_cast<CoveAction>(id);
        bindingDraft_=facts.preferences?facts.preferences->bindings[id]:CoveInputPreferences{}.bindings[id];
        page(Page::Binding);return;
    }
    if(choice.action<=-3500) {
        bindingDraft_.pad=-3500-choice.action-1;page(Page::Binding);return;
    }
    if(choice.action<=-3000) {
        const int key=-3000-choice.action;
        if(alternateKey_)bindingDraft_.alternate=key;else bindingDraft_.key=key;
        page(Page::Binding);return;
    }
    if (choice.action == -1) {
        if(choice.page==Page::Accessibility)settingsReturn_=page_;
        if(choice.page==Page::Help)helpReturn_=page_;
        if(choice.page==Page::PlayerCamera)cameraReturn_=page_;
        if (choice.page == Page::Library && !libraryOpened_) { libraryOpened_ = true; (void)library_.open(); }
        if (choice.page == Page::Imports) (void)library_.refreshImports();
        if (choice.page == Page::Design && choice.id) { selectedId_ = choice.id; selectedRevision_ = choice.revision; }
        page(choice.page); return;
    }
    if (choice.action <= -2000) {
        const auto item = static_cast<size_t>(-2000 - choice.action);
        if (item < library_.imports().size()) (void)library_.importFile(library_.imports()[item]);
        status_ = library_.message(); return;
    }
    if (choice.action <= -1000) {
        const auto target = static_cast<size_t>(-1000 - choice.action);
        // Public actions 82/83 select without adding; cap the loop to the
        // supplied bounded catalogue and stop on the first authoritative refusal.
        if (target >= facts.catalogNames.size() || facts.catalogIndex >= facts.catalogNames.size()) return;
        const size_t count = facts.catalogNames.size();
        const size_t forward = (target + count - facts.catalogIndex) % count;
        const size_t backward = (facts.catalogIndex + count - target) % count;
        for (size_t step = 0; step < std::min(forward, backward); ++step)
            if (!action_ || !action_(forward <= backward ? 83 : 82)) { status_ = "That part cannot be selected now."; return; }
        status_.clear(); return;
    }
    switch (choice.action) {
    case -50:confirmedAction_=354;confirmReturn_=Page::Game;page(Page::Confirm);return;
    case -51:confirmedAction_=360;confirmReturn_=Page::Title;page(Page::Confirm);return;
    case -52:
        if(confirmedAction_== -112){page(Page::Accessibility);applyPreferences(CoveInputPreferences{});return;}
        if(action_&&action_(confirmedAction_))dismiss();
        return;
    case -53:
        if(facts.paused&&(!facts.canResume||!action_||!action_(91)))return;
        if(facts.workshopOpen){dismiss();return;}
        if(action_&&action_(60))dismiss();
        return;
    case -112:confirmedAction_= -112;confirmReturn_=Page::Accessibility;page(Page::Confirm);return;
    case -120:alternateKey_=false;page(Page::CaptureKey);return;
    case -121:alternateKey_=true;page(Page::CaptureKey);return;
    case -122:bindingDraft_.key=0;bindingDraft_.modifiers=0;return;
    case -123:bindingDraft_.alternate=0;bindingDraft_.alternateModifiers=0;return;
    case -124: {
        auto prefs=facts.preferences?*facts.preferences:CoveInputPreferences{};std::string error;
        if(!rebindCoveAction(prefs,bindingAction_,bindingDraft_,error)){status_=error;return;}
        applyPreferences(prefs);return;
    }
    case -125:bindingDraft_=coveActionList()[static_cast<size_t>(bindingAction_)].defaults;return;
    case -126:case -127:case -128: {
        const bool alternate=choice.action== -128||(choice.action== -126&&alternateKey_);
        auto& modifiers=alternate?bindingDraft_.alternateModifiers:bindingDraft_.modifiers;
        modifiers=static_cast<uint8_t>((modifiers+1)%8);return;
    }
    case -10: name(Naming::SaveNew); return;
    case -11: name(Naming::Update, chosen() ? chosen()->name : ""); return;
    case -12: name(Naming::Duplicate); return;
    case -13: name(Naming::Rename, chosen() ? chosen()->name : ""); return;
    case -14: name(Naming::Export); return;
    case -20: {
        std::vector<std::byte> bytes;
        if (!library_.read(selectedId_, selectedRevision_, bytes)) { status_ = library_.message(); return; }
        if (!blueprint_ || blueprint_(2, designBlueprintHex(bytes)) != "ok") { status_ = "Design could not load. Keep or cancel the current change."; return; }
        page(Page::Main); status_ = "Design loaded. Check the cost, then Launch."; return;
    }
    case -21: (void)library_.restore(selectedId_, selectedRevision_); status_ = library_.message(); return;
    case -22: (void)library_.refreshImports(); status_ = library_.message(); return;
    case -23: if (library_.remove(selectedId_, selectedRevision_)) page(Page::Library); else status_ = library_.message(); return;
    case -30: finishName(); return;
    case -31: eraseCharacter(); return;
    case -32: back(); return;
    default: break;
    }
    if(choice.action>=-111&&choice.action<=-100){updatePreference(choice.action,facts);return;}
    const bool resumeJob=page_==Page::Job&&choice.action==91;
    if (action_ && action_(choice.action)) {
        status_.clear();
        if (choice.action == 79 || choice.action == 80 || choice.action == 81||(choice.action==91&&!resumeJob)||choice.action==340||choice.action==341||choice.action==60) dismiss();
    } else {
        // The authoritative action updates facts.message on the next host
        // snapshot. Do not cover that concrete refusal with generic UI text.
        if(resumeJob)status_="The expedition could not resume yet.";
        else status_.clear();
    }
}
bool NativeWorkshopMenu::tick(Input& input, const Facts& facts, uint32_t width, uint32_t height, glm::vec2 pointerScale) {
    if (library_.poll()) {
        status_ = library_.message();
        if (library_.lastSucceeded() && library_.selectedId()) {
            selectedId_ = library_.selectedId();
            if (const auto* row = chosen()) selectedRevision_ = row->revision;
        }
    }
    const bool wasActive = active();
    const bool playerCamera=page_==Page::PlayerCamera;
    // A mode/ownership handoff closes the old modal and consumes this entire
    // frame. Its old selected action cannot execute in the new context.
    if (active() && ((playerCamera&&(!facts.cameraAvailable||facts.workshopOpen))
        ||(!playerCamera&&!generalPage()&&!facts.workshopOpen)
        ||(generalPage()&&!facts.gameAvailable&&!facts.workshopOpen))) {
        dismiss();rebuild(facts);return true;
    }
    if (!facts.workshopOpen&&!facts.cameraAvailable&&!facts.gameAvailable)return wasActive;
    if (!input.focused()) return active();
    const auto& pad = input.gamepad();
    static const CoveInputPreferences defaults;
    const auto& prefs=facts.preferences?*facts.preferences:defaults;
    const auto bindingPressed=[&](CoveAction action) {
        const auto& binding=prefs.bindings[static_cast<size_t>(action)];
        const auto key=[&](int code,uint8_t modifiers) {return code>0&&code<512&&input.wasKeyPressed(static_cast<Key>(code))
            &&input.keyPressModifiers(static_cast<Key>(code))==modifiers;};
        return key(binding.key,binding.modifiers)||key(binding.alternate,binding.alternateModifiers)
            ||(binding.pad>=0&&binding.pad<17&&pad.pressed(static_cast<PadButton>(binding.pad)));
    };
    const bool capturing=page_==Page::CaptureKey||page_==Page::ChooseKey||page_==Page::ChoosePad||page_==Page::Binding||page_==Page::Naming;
    if((!facts.workshopOpen||facts.paused)&&facts.gameAvailable&&!capturing
        &&(bindingPressed(CoveAction::Pause)||pad.pressed(PadButton::Menu)||(!active()&&input.wasKeyPressed(Key::Escape)))) {
        if(page_==Page::Game)closeGame(facts);
        else {
            if(!facts.paused&&!facts.pausePending&&action_)(void)action_(90);
            openGame();
        }
        rebuild(facts);return true;
    }
    if (!capturing&&(input.wasKeyPressed(Key::F2)
        ||bindingPressed(facts.workshopOpen?CoveAction::ToolsMenu:CoveAction::CameraMenu))) {
        if (active()) dismiss(); else if(facts.workshopOpen)open();else openCamera();
        rebuild(facts); return true;
    }
    if (!active()) return false;
    rebuild(facts);
    if (input.wasKeyPressed(Key::Escape) || pad.pressed(PadButton::Back)) {
        if(page_==Page::Game)closeGame(facts);else back();
        rebuild(facts);return true;
    }
    if(!facts.workshopOpen&&!capturing&&bindingPressed(CoveAction::Save)) {
        if(facts.canSave&&!facts.testActive&&action_)(void)action_(350);
        rebuild(facts);return true;
    }
    if(page_==Page::CaptureKey) {
        // Enter/standard Confirm navigates to the controller-friendly picker.
        // Other physical key edges capture their event-time modifiers exactly.
        for(int key=32;key<=301;++key)if(key!=256&&key!=257&&input.wasKeyPressed(static_cast<Key>(key))) {
            if(alternateKey_){bindingDraft_.alternate=key;bindingDraft_.alternateModifiers=input.keyPressModifiers(static_cast<Key>(key));}
            else {bindingDraft_.key=key;bindingDraft_.modifiers=input.keyPressModifiers(static_cast<Key>(key));}
            page(Page::Binding);rebuild(facts);return true;
        }
    }
    if (page_ == Page::Naming && !facts.pending && !library_.busy()) {
        const bool control = input.isKeyDown(Key::LeftControl) || input.isKeyDown(Key::RightControl);
        if (control && input.wasKeyPressed(Key::A)) replaceName_ = true;
        if (!control && !input.textInput().empty()) {
            appendText(input.textInput()); content_.keyboardFocus = false; content_.selected = 0;
        }
        if (input.wasKeyPressed(Key::Backspace) || pad.pressed(PadButton::Tool)) eraseCharacter();
        if (pad.pressed(PadButton::Alternate)) { content_.name.clear(); replaceName_ = false; }
        if (input.wasKeyPressed(Key::Tab) || pad.pressed(PadButton::LeftShoulder) || pad.pressed(PadButton::RightShoulder)) content_.keyboardFocus = !content_.keyboardFocus;
    }
    const bool up = input.wasKeyPressed(Key::Up) || pad.navigation(0);
    const bool down = input.wasKeyPressed(Key::Down) || pad.navigation(1);
    const bool left = input.wasKeyPressed(Key::Left) || pad.navigation(2);
    const bool right = input.wasKeyPressed(Key::Right) || pad.navigation(3);
    const bool grid = page_ == Page::Naming && content_.keyboardFocus;
    if (grid) {
        if (left) content_.key = (content_.key / 10) * 10 + (content_.key + 9) % 10;
        if (right) content_.key = (content_.key / 10) * 10 + (content_.key + 1) % 10;
        if (up) content_.key = (content_.key + 30) % render::kCoveNameKeys.size();
        if (down) content_.key = (content_.key + 10) % render::kCoveNameKeys.size();
    } else if (!choices_.empty()) {
        if (up || input.scrollDelta() > .1f) content_.selected = (content_.selected + choices_.size() - 1) % choices_.size();
        if (down || input.scrollDelta() < -.1f) content_.selected = (content_.selected + 1) % choices_.size();
    }
    bool activateChoice = input.wasKeyPressed(Key::Enter) || pad.pressed(PadButton::Confirm);
    bool clickedKey = false;
    if (input.wasMouseButtonPressed(MouseButton::Left)) {
        render::CoveHudContent content; content.menu = content_; content.title = content_.title;
        content.textScale=static_cast<float>(prefs.textScale);content.highContrast=prefs.highContrast;
        const auto layout = render::layoutCoveHud(content, width, height);
        const auto pointer = input.mousePosition() * pointerScale;
        for (const auto& hit : layout.menuHits) {
            const auto& b = hit.bounds;
            if (pointer.x < b.x || pointer.y < b.y || pointer.x >= b.x + b.z || pointer.y >= b.y + b.w) continue;
            if (hit.key >= 0) { content_.key = static_cast<size_t>(hit.key); content_.keyboardFocus = true; clickedKey = true; activateChoice = true; }
            else if (hit.row >= 0) { content_.selected = static_cast<size_t>(hit.row); content_.keyboardFocus = false; activateChoice = true; }
            break;
        }
    }
    if (activateChoice && (page_==Page::PlayerCamera?facts.cameraAvailable:generalPage()||!facts.pending)) {
        if (page_ == Page::Naming && (clickedKey || content_.keyboardFocus)) {
            if (!library_.busy() && content_.key < render::kCoveNameKeys.size()) appendText(render::kCoveNameKeys.substr(content_.key, 1));
        } else activate(content_.selected, facts);
    }
    rebuild(facts); return true;
}
} // namespace voxy::platform
