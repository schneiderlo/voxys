#include "engine/platform/native/workshop_menu.hpp"
#include "core/sha256.hpp"
#include <gtest/gtest.h>
#include <chrono>
#include <cstdlib>
#include <thread>

namespace {
using namespace voxy;
using namespace voxy::platform;
using namespace voxy::game::expedition;
std::vector<std::byte> blueprint() {
    std::vector<std::byte> bytes(75, std::byte{0});
    bytes[0] = std::byte{'S'}; bytes[1] = std::byte{'V'}; bytes[2] = std::byte{'B'}; bytes[3] = std::byte{'P'};
    bytes[4] = std::byte{1}; bytes[8] = std::byte{1}; bytes[16] = std::byte{1};
    const auto digest = core::sha256(bytes); bytes.insert(bytes.end(), digest.bytes.begin(), digest.bytes.end()); return bytes;
}
struct MenuFixture : testing::Test {
    std::filesystem::path directory;
    Input input;
    NativeWorkshopMenu::Facts facts;
    std::vector<int> actions;
    std::unique_ptr<NativeWorkshopMenu> menu;
    std::string loaded;
    CoveInputPreferences preferences;
    int preferenceApplies=0;
    int refusedAction=0;
    void SetUp() override {
        auto name = (std::filesystem::temp_directory_path() / "voxys-menu-XXXXXX").string();
        const auto* path = ::mkdtemp(name.data()); ASSERT_NE(path, nullptr); directory = path;
        facts.workshopOpen = true; facts.canKeep = true; facts.canLoadBlueprint = true; facts.canAdd = true;
        facts.canPaint = true; facts.canConfigure = true; facts.canLaunch = true; facts.canUndo = true; facts.canRedo = true;
        facts.selectedCount = 2; facts.selectedName = "Two bricks";
        facts.preferences=&preferences;
        facts.catalogNames = {"Brick 1 x 2", "Brick 2 x 2", "Brick 2 x 4"};
        menu = std::make_unique<NativeWorkshopMenu>(directory / "Designs", [&](int action) {
            actions.push_back(action);
            if(action==refusedAction)return false;
            if (action == 83) facts.catalogIndex = (facts.catalogIndex + 1) % facts.catalogNames.size();
            if (action == 82) facts.catalogIndex = (facts.catalogIndex + facts.catalogNames.size() - 1) % facts.catalogNames.size();
            return true;
        }, [&](int action, std::string_view text) {
            if (action == 1) return designBlueprintHex(blueprint());
            if (action == 2) { loaded = text; return std::string("ok"); }
            std::vector<std::byte> parsed; return parseDesignBlueprintHex(text, parsed) ? std::string("ok") : std::string{};
        },[&](int action,std::string_view text) {
            if(action!=2)return std::string("Unsupported preference action.");
            CoveInputPreferences candidate;std::string error;
            if(!parseCoveInputPreferences(text,candidate,error))return error;
            preferences=std::move(candidate);++preferenceApplies;return std::string("ok");
        });
    }
    void TearDown() override { menu.reset(); std::error_code ignored; std::filesystem::remove_all(directory, ignored); }
    bool frame() { input.beginFrame(); return menu->tick(input, facts, 960, 800); }
    void key(Key value) { input.onKeyDown(static_cast<int>(value)); input.onKeyUp(static_cast<int>(value)); ASSERT_TRUE(frame()); }
    void choose(std::string_view label) {
        for (size_t step = 0; step < menu->menuContent().rows.size(); ++step) {
            if (menu->menuContent().rows[menu->menuContent().selected].label == label) { key(Key::Enter); return; }
            key(Key::Down);
        }
        FAIL() << "Missing menu row: " << label;
    }
    void waitLibrary() {
        const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (menu->library().busy() && std::chrono::steady_clock::now() < until) { (void)frame(); std::this_thread::sleep_for(std::chrono::milliseconds(1)); }
        ASSERT_FALSE(menu->library().busy()); ASSERT_TRUE(menu->library().ready()) << menu->library().message();
    }
};
TEST_F(MenuFixture, OpeningClosingAndFocusLossConsumeWithoutConstruction) {
    EXPECT_FALSE(frame()); key(Key::F2); EXPECT_TRUE(menu->active()); EXPECT_EQ(menu->pageName(), "main");
    input.onFocusChanged(false); input.onKeyDown(static_cast<int>(Key::Enter)); EXPECT_TRUE(frame()); EXPECT_TRUE(actions.empty());
    input.onFocusChanged(true); EXPECT_TRUE(frame()); EXPECT_TRUE(actions.empty());
    key(Key::Escape); EXPECT_FALSE(menu->active()); EXPECT_FALSE(frame());
    menu->open(); facts.workshopOpen = false; EXPECT_TRUE(frame()); EXPECT_FALSE(menu->active()); EXPECT_TRUE(actions.empty());
}
TEST_F(MenuFixture, GroupAndHistoryChoicesUseOnlyApprovedActionsAndExposeRefusal) {
    key(Key::F2); choose("Select and group"); choose("Select whole boat"); choose("Duplicate group");
    choose("Mirror across X = 0"); choose("Mirror across Z = 0"); choose("Replace with chosen part"); choose("Redo draft");
    EXPECT_EQ(actions, (std::vector<int>{300, 302, 303, 304, 305, 306}));
    key(Key::Escape); choose("Move and rotate"); choose("Rotate around X"); choose("Rotate around Z");
    EXPECT_EQ(actions[actions.size() - 2], 307); EXPECT_EQ(actions.back(), 308);
    facts.pending = true; choose("Left"); EXPECT_EQ(actions.back(), 308);
    facts.pending = false; facts.message = "Selected group overlaps another part."; (void)frame();
    EXPECT_EQ(menu->menuContent().status, facts.message);
}
TEST_F(MenuFixture, CatalogChoiceDoesNotAddAndLaunchRequiresAnEnabledAction) {
    key(Key::F2); choose("Choose parts"); choose("Brick 2 x 4");
    EXPECT_EQ(facts.catalogIndex, 2u); EXPECT_EQ(actions, (std::vector<int>{82}));
    choose("Add chosen part"); EXPECT_EQ(actions.back(), 84); key(Key::Escape);
    facts.canLaunch = false; (void)frame(); choose("Launch boat"); EXPECT_TRUE(menu->active()); EXPECT_EQ(actions.back(), 84);
    facts.canLaunch = true; (void)frame(); choose("Launch boat"); EXPECT_EQ(actions.back(), 79); EXPECT_FALSE(menu->active());
}
TEST_F(MenuFixture, UnicodeNamingBackspaceAndSaveDoNotLeakGameplayKeys) {
    key(Key::F2); choose("Saved designs"); waitLibrary(); choose("Save boat as new");
    EXPECT_EQ(menu->pageName(), "naming");
    input.onCharacter('B'); input.onCharacter('E'); input.onCharacter(' '); input.onCharacter(0xe9); EXPECT_TRUE(frame());
    EXPECT_EQ(menu->menuContent().name, "BE \xc3\xa9"); EXPECT_FALSE(menu->menuContent().keyboardFocus);
    key(Key::Backspace); EXPECT_EQ(menu->menuContent().name, "BE "); input.onCharacter('T'); EXPECT_TRUE(frame());
    key(Key::Enter); waitLibrary(); ASSERT_EQ(menu->library().rows().size(), 1u); EXPECT_EQ(menu->library().rows()[0].name, "BE T");
    EXPECT_TRUE(actions.empty()); EXPECT_EQ(menu->library().generation(), 1u);
    choose("BE T"); choose("Load into workshop"); EXPECT_EQ(loaded, designBlueprintHex(blueprint())); EXPECT_TRUE(actions.empty());
}
TEST_F(MenuFixture, OnscreenLetterGridNamesWithoutPhysicalTextAndCancelPreservesLibrary) {
    key(Key::F2); choose("Saved designs"); waitLibrary(); choose("Save boat as new");
    key(Key::Enter); key(Key::Right); key(Key::Enter); EXPECT_EQ(menu->menuContent().name, "AB");
    key(Key::Tab); key(Key::Enter); waitLibrary(); ASSERT_EQ(menu->library().rows().size(), 1u); EXPECT_EQ(menu->library().rows()[0].name, "AB");
    choose("Save boat as new"); key(Key::Enter); key(Key::Escape); EXPECT_EQ(menu->library().rows().size(), 1u); EXPECT_TRUE(actions.empty());
}
TEST_F(MenuFixture, PointerUsesRendererHitBoundsAndClosingFrameRemainsConsumed) {
    key(Key::F2);
    render::CoveHudContent content; content.title = "Menu"; content.menu = menu->menuContent();
    const auto layout = render::layoutCoveHud(content, 960, 800);
    const auto target = std::find_if(layout.menuHits.begin(), layout.menuHits.end(), [](const auto& hit) { return hit.row == 0; });
    ASSERT_NE(target, layout.menuHits.end()); input.onMouseMove(target->bounds.x + 2, target->bounds.y + 2);
    input.onMouseDown(0); input.onMouseUp(0); EXPECT_TRUE(frame()); EXPECT_EQ(menu->pageName(), "selection");
    key(Key::F2); EXPECT_FALSE(menu->active()); EXPECT_TRUE(actions.empty());
}
TEST_F(MenuFixture, HighDpiPointerHitsTheExactFramebufferRow) {
    menu->open(); input.beginFrame(); ASSERT_TRUE(menu->tick(input, facts, 1920, 1600, {2, 2}));
    render::CoveHudContent content; content.title = "Menu"; content.menu = menu->menuContent();
    const auto layout = render::layoutCoveHud(content, 1920, 1600);
    const auto target = std::find_if(layout.menuHits.begin(), layout.menuHits.end(), [](const auto& hit) { return hit.row == 1; });
    ASSERT_NE(target, layout.menuHits.end());
    input.onMouseMove((target->bounds.x + 3) / 2, (target->bounds.y + 3) / 2);
    input.onMouseDown(0); input.onMouseUp(0); input.beginFrame();
    ASSERT_TRUE(menu->tick(input, facts, 1920, 1600, {2, 2}));
    EXPECT_EQ(menu->pageName(), "move"); EXPECT_TRUE(actions.empty());
}
TEST_F(MenuFixture, GamePauseWaitsForCompletionAndBackNeverQuits) {
    facts.workshopOpen=false;facts.gameAvailable=true;facts.cameraAvailable=true;
    key(Key::Escape);EXPECT_EQ(menu->pageName(),"game");EXPECT_EQ(actions,(std::vector<int>{90}));
    facts.pausePending=true;key(Key::Escape);EXPECT_TRUE(menu->active());EXPECT_EQ(actions.size(),1u);
    input.onFocusChanged(false);input.onKeyDown(257);EXPECT_TRUE(frame());EXPECT_EQ(actions.size(),1u);
    input.onFocusChanged(true);facts.pausePending=false;facts.paused=true;facts.canResume=true;
    facts.inventoryLines={"Material: 48","Owned parts: 11"};choose("Inventory");EXPECT_EQ(menu->pageName(),"inventory");
    choose("Material: 48");EXPECT_EQ(actions.size(),1u);key(Key::Escape);choose("Resume");
    EXPECT_EQ(actions,(std::vector<int>{90,91}));EXPECT_FALSE(menu->active());
    EXPECT_FALSE(std::filesystem::exists(directory/"Designs"));
}
TEST_F(MenuFixture, WorkshopFromPauseResumesBeforeOpeningAndPendingCannotDispatch) {
    facts.workshopOpen=false;facts.gameAvailable=true;facts.paused=true;facts.canResume=true;
    facts.pausePending=true;menu->openGame();EXPECT_TRUE(frame());choose("Resume and open workshop");EXPECT_TRUE(actions.empty());
    facts.pausePending=false;EXPECT_TRUE(frame());choose("Resume and open workshop");
    EXPECT_EQ(actions,(std::vector<int>{91,60}));EXPECT_FALSE(menu->active());
}
TEST_F(MenuFixture, PausedWorkshopReturnCanOpenGameAndResumeWithoutOpeningTools) {
    facts.workshopOpen=true;facts.gameAvailable=true;facts.paused=true;facts.canResume=true;
    key(Key::P);EXPECT_EQ(menu->pageName(),"game");EXPECT_TRUE(actions.empty());
    choose("Return to building");EXPECT_EQ(actions,(std::vector<int>{91}));EXPECT_FALSE(menu->active());
    facts.paused=false;key(Key::F2);EXPECT_EQ(menu->pageName(),"main");
    key(Key::Escape);facts.paused=true;key(Key::Escape);EXPECT_EQ(menu->pageName(),"game");
    facts.canResume=false;choose("Resume");EXPECT_EQ(actions.size(),1u);EXPECT_TRUE(menu->active());
}
TEST_F(MenuFixture, JobResumeKeepsPageAndRequiresFreshPermissionBeforeAcceptOrDeliver) {
    facts.workshopOpen=false;facts.gameAvailable=true;facts.paused=true;
    facts.jobAction=50;facts.jobActionLabel="Accept job";facts.jobActionEnabled=false;
    menu->openJob();EXPECT_TRUE(frame());
    choose("Resume expedition");EXPECT_TRUE(actions.empty());
    EXPECT_EQ(menu->pageName(),"job");

    facts.canResume=true;refusedAction=91;EXPECT_TRUE(frame());choose("Resume expedition");
    EXPECT_EQ(actions,(std::vector<int>{91}));EXPECT_EQ(menu->pageName(),"job");
    EXPECT_NE(menu->menuContent().status.find("could not resume"),std::string::npos);
    choose("Accept job");EXPECT_EQ(actions.size(),1u);

    refusedAction=0;choose("Resume expedition");
    EXPECT_EQ(actions,(std::vector<int>{91,91}));EXPECT_EQ(menu->pageName(),"job");
    // Accepted Resume does not infer job eligibility or dispatch a second
    // action from the same Confirm. Only the following host facts enable it.
    choose("Accept job");EXPECT_EQ(actions.size(),2u);
    facts.paused=false;facts.canResume=false;facts.jobActionEnabled=true;EXPECT_TRUE(frame());
    choose("Accept job");EXPECT_EQ(actions,(std::vector<int>{91,91,50}));
    EXPECT_EQ(menu->pageName(),"job");

    facts.paused=true;facts.canResume=true;facts.jobAction=51;facts.jobActionLabel="Deliver cargo";
    facts.jobActionEnabled=false;EXPECT_TRUE(frame());choose("Resume expedition");
    EXPECT_EQ(actions.back(),91);EXPECT_EQ(menu->pageName(),"job");
    facts.paused=false;facts.canResume=false;EXPECT_TRUE(frame());choose("Deliver cargo");
    EXPECT_EQ(actions.size(),4u); // Unmet delivery geometry still refuses.
    facts.jobActionEnabled=true;EXPECT_TRUE(frame());choose("Deliver cargo");
    EXPECT_EQ(actions,(std::vector<int>{91,91,50,91,51}));EXPECT_EQ(menu->pageName(),"job");
}
TEST_F(MenuFixture, DynamicSaveBindingWorksInGameMenuButNotDuringRebinding) {
    facts.workshopOpen=false;facts.gameAvailable=true;facts.paused=true;facts.canSave=true;
    preferences.bindings[static_cast<size_t>(CoveAction::Save)].key=290;
    menu->openGame();EXPECT_TRUE(frame());key(Key::F10);EXPECT_TRUE(actions.empty());key(Key::F1);EXPECT_EQ(actions,(std::vector<int>{350}));
    choose("Controls and accessibility");choose("Remap controls");choose("Jump: Space");choose("Primary: Space");
    EXPECT_EQ(menu->pageName(),"capture-key");key(Key::F1);EXPECT_EQ(menu->pageName(),"binding");EXPECT_EQ(actions.size(),1u);
    choose("Apply binding");EXPECT_EQ(preferenceApplies,0);EXPECT_NE(menu->menuContent().status.find("Already used"),std::string::npos);
}
TEST_F(MenuFixture, KeyPickerIsControllerReachableAndConflictsPreservePreferences) {
    facts.workshopOpen=false;facts.gameAvailable=true;facts.paused=true;menu->openGame();EXPECT_TRUE(frame());
    choose("Controls and accessibility");choose("Text size: 100%");EXPECT_EQ(preferences.textScale,1.25);
    choose("High contrast: Off");EXPECT_TRUE(preferences.highContrast);
    choose("Remap controls");choose("Jump: Space");choose("Primary: Space");choose("Choose from key list");choose("T");
    choose("Apply binding");EXPECT_EQ(preferences.bindings[static_cast<size_t>(CoveAction::Jump)].key,84);
    const auto prior=preferences;choose("Primary: T");choose("Choose from key list");choose("E");choose("Apply binding");
    EXPECT_EQ(preferences,prior);EXPECT_NE(menu->menuContent().status.find("Already used"),std::string::npos);
    key(Key::Escape);EXPECT_EQ(menu->pageName(),"bindings");key(Key::Escape);EXPECT_EQ(menu->pageName(),"accessibility");
    choose("Restore default controls...");EXPECT_EQ(menu->pageName(),"confirm");EXPECT_EQ(preferences,prior);
    choose("Cancel");EXPECT_EQ(preferences,prior);choose("Restore default controls...");choose("Confirm");EXPECT_EQ(preferences,CoveInputPreferences{});
}
TEST_F(MenuFixture, WorldChangesAndQuitRequireExplicitConfirmationAndCurrentPermission) {
    facts.workshopOpen=false;facts.gameAvailable=true;facts.paused=true;facts.canChangeWorld=true;facts.canQuit=true;
    facts.worldNames={"Saved Cove 1"};menu->openGame();EXPECT_TRUE(frame());choose("Expeditions");choose("New expedition...");
    EXPECT_EQ(menu->pageName(),"confirm");EXPECT_TRUE(actions.empty());EXPECT_EQ(menu->menuContent().selected,0u);
    choose("Cancel");choose("Load saved Cove");choose("Saved Cove 1");EXPECT_TRUE(actions.empty());
    facts.canChangeWorld=false;EXPECT_TRUE(frame());choose("Confirm");EXPECT_TRUE(actions.empty());
    facts.canChangeWorld=true;EXPECT_TRUE(frame());choose("Confirm");EXPECT_EQ(actions,(std::vector<int>{4000}));EXPECT_FALSE(menu->active());
    menu->openGame();EXPECT_TRUE(frame());choose("Quit game...");key(Key::Escape);EXPECT_EQ(actions.size(),1u);
    choose("Quit game...");choose("Confirm");EXPECT_EQ(actions.back(),354);EXPECT_FALSE(menu->active());
}
TEST_F(MenuFixture, PracticeControlsDoNotOfferExpeditionSaveAndUseOnlyOwnedReturn) {
    facts.workshopOpen=false;facts.gameAvailable=true;facts.paused=true;facts.canSave=true;
    facts.testActive=true;facts.canReturnTest=true;facts.practiceMessage="Temporary test: no rewards are kept.";
    menu->openGame();EXPECT_TRUE(frame());EXPECT_EQ(menu->menuContent().status,facts.practiceMessage);
    choose("Save expedition");EXPECT_TRUE(actions.empty());
    facts.testBusy=true;EXPECT_TRUE(frame());choose("Return from test sail");EXPECT_TRUE(actions.empty());
    facts.testBusy=false;EXPECT_TRUE(frame());choose("Return from test sail");EXPECT_EQ(actions,(std::vector<int>{341}));EXPECT_FALSE(menu->active());
}
TEST_F(MenuFixture, EnlargedHighContrastMenuUsesExactScaledPointerBounds) {
    preferences.textScale=1.5;preferences.highContrast=true;menu->open();ASSERT_TRUE(frame());
    render::CoveHudContent content;content.title="Menu";content.menu=menu->menuContent();content.textScale=1.5f;content.highContrast=true;
    const auto layout=render::layoutCoveHud(content,960,800);
    const auto hit=std::find_if(layout.menuHits.begin(),layout.menuHits.end(),[](const auto& item){return item.row==1;});
    ASSERT_NE(hit,layout.menuHits.end());input.onMouseMove(hit->bounds.x+2,hit->bounds.y+2);input.onMouseDown(0);input.onMouseUp(0);
    ASSERT_TRUE(frame());EXPECT_EQ(menu->pageName(),"move");
}
TEST_F(MenuFixture, OutsideCameraOptionsUseOnlyCameraActionsEvenWhileWorkshopPending) {
    facts.workshopOpen=false;facts.pending=true;facts.cameraAvailable=true;
    key(Key::F2);EXPECT_EQ(menu->pageName(),"player-camera");
    choose("View: Chase");choose("Recenter behind robot");choose("Frame load: Off");
    choose("Reduced motion: Off");choose("Move camera closer");choose("Move camera farther");
    EXPECT_EQ(actions,(std::vector<int>{320,321,322,323,324,325}));
    EXPECT_FALSE(menu->library().ready());EXPECT_FALSE(std::filesystem::exists(directory/"Designs"));
    facts.cameraDistance=1.5;(void)frame();choose("Move camera closer");EXPECT_EQ(actions.size(),6u);
    facts.cameraDistance=12.;(void)frame();choose("Move camera farther");EXPECT_EQ(actions.size(),6u);
    facts.chaseCamera=false;facts.reducedMotion=true;facts.frameLoad=true;(void)frame();
    choose("View: Orbit");choose("Reduced motion: On");choose("Frame load: On");
    EXPECT_EQ(actions.size(),9u);choose("Back to game");EXPECT_FALSE(menu->active());
    EXPECT_FALSE(frame());
}
TEST_F(MenuFixture, CameraOwnershipRevocationAndWorkshopTransitionConsumeStaleConfirm) {
    facts.workshopOpen=false;facts.pending=true;facts.cameraAvailable=true;key(Key::F2);
    facts.cameraAvailable=false;key(Key::Enter);
    EXPECT_FALSE(menu->active());EXPECT_TRUE(actions.empty());EXPECT_FALSE(frame());
    facts.cameraAvailable=true;key(Key::F2);facts.workshopOpen=true;key(Key::Enter);
    EXPECT_FALSE(menu->active());EXPECT_TRUE(actions.empty());
    facts.pending=false;key(Key::F2);EXPECT_EQ(menu->pageName(),"main");choose("Camera");
    EXPECT_EQ(menu->pageName(),"camera");facts.pending=false;(void)frame();choose("Focus selection");
    EXPECT_EQ(actions,(std::vector<int>{97})); // Existing builder camera kept.
}
TEST_F(MenuFixture, ControllerCameraMenuNeedsFreshEdgesAndConsumesOpeningAndClosingFrames) {
    facts.workshopOpen=false;facts.cameraAvailable=true;facts.pending=true;
    GamepadSample sample;sample.device=0;sample.connected=true;
    // The production Input owns this nonconst object; inject samples directly
    // after its platform poll to exercise the real edge/navigation state.
    auto& pad=const_cast<GamepadInput&>(input.gamepad());
    double clock=0;
    const auto controllerFrame=[&] {
        input.beginFrame();pad.update(sample,true,clock+=.02);
        return menu->tick(input,facts,960,800);
    };
    sample.buttons[static_cast<size_t>(PadButton::Alternate)]=true;
    EXPECT_FALSE(controllerFrame());EXPECT_FALSE(menu->active()); // Held on attach.
    sample.buttons.fill(false);EXPECT_FALSE(controllerFrame());
    sample.buttons[static_cast<size_t>(PadButton::Alternate)]=true;
    EXPECT_TRUE(controllerFrame());EXPECT_EQ(menu->pageName(),"player-camera");
    EXPECT_TRUE(controllerFrame());EXPECT_TRUE(menu->active());EXPECT_TRUE(actions.empty());
    sample.buttons.fill(false);EXPECT_TRUE(controllerFrame());
    sample.buttons[static_cast<size_t>(PadButton::Down)]=true;EXPECT_TRUE(controllerFrame());
    sample.buttons.fill(false);EXPECT_TRUE(controllerFrame());
    sample.buttons[static_cast<size_t>(PadButton::Confirm)]=true;EXPECT_TRUE(controllerFrame());
    EXPECT_EQ(actions,(std::vector<int>{321}));EXPECT_TRUE(controllerFrame());EXPECT_EQ(actions.size(),1u);
    sample.buttons.fill(false);EXPECT_TRUE(controllerFrame());
    sample.buttons[static_cast<size_t>(PadButton::Back)]=true;EXPECT_TRUE(controllerFrame());EXPECT_FALSE(menu->active());
    sample.buttons.fill(false);EXPECT_FALSE(controllerFrame());
    sample.buttons[static_cast<size_t>(PadButton::Menu)]=true;EXPECT_FALSE(controllerFrame());
    EXPECT_FALSE(menu->active()); // Parent retains the outside Pause binding.
}
} // namespace
