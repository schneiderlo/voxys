#pragma once

#include "engine/platform/input.hpp"
#include "engine/platform/native/design_library.hpp"
#include "render/cove_hud.hpp"
#include "game/expedition/cove_input_preferences.hpp"

namespace voxy::platform {

class NativeWorkshopMenu {
public:
    struct Facts {
        bool workshopOpen = false, pending = false;
        bool canLoadBlueprint = false, canKeep = false, canUndo = false, canRedo = false;
        bool canLaunch = false, canPaint = false, canConfigure = false;
        bool canAdd = false, brushActive = false, canUndoLaunch = false, canRedoLaunch = false;
        bool hasOutputLimit = false, canReverse = false;
        // Independent from workshop pending: viewing options remain usable
        // while paused, but not during a revoked/retiring/replacing session.
        bool cameraAvailable = false, chaseCamera = true, reducedMotion = false, frameLoad = false;
        double cameraDistance = 4.8;
        size_t selectedCount = 0;
        std::string selectedName, message;
        std::vector<std::string> catalogNames;
        size_t catalogIndex = 0;
        std::optional<uint32_t> paintIndex;
        bool gameAvailable=false,paused=false,pausePending=false,canResume=false,canSave=false;
        bool canRescue=false,canQuit=false,canChangeWorld=false;
        std::string saveMessage,jobTitle,jobText,jobActionLabel;
        int jobAction=0;
        bool jobActionEnabled=false;
        std::vector<std::string> mapLines,inventoryLines,helpLines,worldNames;
        std::string tutorialTitle,tutorialText,tutorialActionLabel;
        int tutorialAction=0;
        bool tutorialComplete=false;
        bool testAvailable=false,testActive=false,testBusy=false,canReturnTest=false;
        std::string practiceMessage,preferencesMessage;
        const game::expedition::CoveInputPreferences* preferences=nullptr;
    };
    using Action = std::function<bool(int)>;
    using Blueprint = std::function<std::string(int, std::string_view)>;
    using Preferences = std::function<std::string(int, std::string_view)>;
    NativeWorkshopMenu(std::filesystem::path designsRoot, Action, Blueprint, Preferences = {});
    // Call every frame, including when hidden, to consume storage completions.
    // Returns true for every modal frame AND its opening/closing frame. The
    // caller must gate all gameplay/global/F10 input when true and reset held
    // input on active transitions. Filesystem work continues when the menu hides.
    // Dimensions match the rendered framebuffer. pointerScale converts the
    // platform's logical cursor coordinates into that same pixel space.
    [[nodiscard]] bool tick(Input&, const Facts&, uint32_t framebufferWidth, uint32_t framebufferHeight,
                            glm::vec2 pointerScale = {1, 1});
    [[nodiscard]] bool active() const noexcept;
    void open();
    void openCamera();
    void openGame();
    void openJob();
    void openHelp();
    void dismiss();
    [[nodiscard]] const render::CoveHudMenu& menuContent() const noexcept { return content_; }
    [[nodiscard]] const NativeDesignLibrary& library() const noexcept { return library_; }
    [[nodiscard]] std::string_view pageName() const noexcept;
private:
    enum class Page { Closed, Main, Selection, Move, Parts, Paint, Settings, Camera, PlayerCamera,
                      Library, Design, Imports, Naming, Remove, Game, Job, Map, Inventory,
                      Accessibility, Bindings, Binding, CaptureKey, ChooseKey, ChoosePad, Help,
                      Title, Worlds, Confirm };
    enum class Naming { SaveNew, Update, Duplicate, Rename, Export };
    struct Choice {
        render::CoveHudMenuRow row;
        int action = -1;
        Page page = Page::Closed;
        uint64_t id = 0, revision = 0;
    };
    void rebuild(const Facts&);
    void activate(size_t, const Facts&);
    void back();
    void page(Page);
    void name(Naming, std::string initial = {});
    void finishName();
    void eraseCharacter();
    void appendText(std::string_view);
    void closeGame(const Facts&);
    void applyPreferences(const game::expedition::CoveInputPreferences&);
    void updatePreference(int,const Facts&);
    [[nodiscard]] bool generalPage() const noexcept;
    [[nodiscard]] std::vector<std::byte> capture();
    [[nodiscard]] const NativeDesignLibrary::Row* chosen() const;
    Action action_;
    Blueprint blueprint_;
    Preferences preferences_;
    NativeDesignLibrary library_;
    bool libraryOpened_ = false;
    Page page_ = Page::Closed, namingReturn_ = Page::Library;
    Naming naming_ = Naming::SaveNew;
    render::CoveHudMenu content_;
    std::vector<Choice> choices_;
    uint64_t selectedId_ = 0, selectedRevision_ = 0;
    std::string status_;
    bool replaceName_ = false;
    Page settingsReturn_=Page::Game,helpReturn_=Page::Game,confirmReturn_=Page::Game,cameraReturn_=Page::Closed;
    int confirmedAction_=0;
    game::expedition::CoveAction bindingAction_=game::expedition::CoveAction::MoveForward;
    game::expedition::CoveBinding bindingDraft_{};
    bool alternateKey_=false;
};
} // namespace voxy::platform
