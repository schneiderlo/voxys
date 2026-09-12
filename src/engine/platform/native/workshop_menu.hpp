#pragma once

#include "engine/platform/input.hpp"
#include "engine/platform/native/design_library.hpp"
#include "render/cove_hud.hpp"

namespace voxy::platform {

class NativeWorkshopMenu {
public:
    struct Facts {
        bool workshopOpen = false, pending = false;
        bool canLoadBlueprint = false, canKeep = false, canUndo = false, canRedo = false;
        bool canLaunch = false, canPaint = false, canConfigure = false;
        bool canAdd = false, brushActive = false, canUndoLaunch = false, canRedoLaunch = false;
        bool hasOutputLimit = false, canReverse = false;
        size_t selectedCount = 0;
        std::string selectedName, message;
        std::vector<std::string> catalogNames;
        size_t catalogIndex = 0;
        std::optional<uint32_t> paintIndex;
    };
    using Action = std::function<bool(int)>;
    using Blueprint = std::function<std::string(int, std::string_view)>;
    NativeWorkshopMenu(std::filesystem::path designsRoot, Action, Blueprint);
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
    void dismiss();
    [[nodiscard]] const render::CoveHudMenu& menuContent() const noexcept { return content_; }
    [[nodiscard]] const NativeDesignLibrary& library() const noexcept { return library_; }
    [[nodiscard]] std::string_view pageName() const noexcept;
private:
    enum class Page { Closed, Main, Selection, Move, Parts, Paint, Settings, Camera,
                      Library, Design, Imports, Naming, Remove };
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
    [[nodiscard]] std::vector<std::byte> capture();
    [[nodiscard]] const NativeDesignLibrary::Row* chosen() const;
    Action action_;
    Blueprint blueprint_;
    NativeDesignLibrary library_;
    bool libraryOpened_ = false;
    Page page_ = Page::Closed, namingReturn_ = Page::Library;
    Naming naming_ = Naming::SaveNew;
    render::CoveHudMenu content_;
    std::vector<Choice> choices_;
    uint64_t selectedId_ = 0, selectedRevision_ = 0;
    std::string status_;
    bool replaceName_ = false;
};
} // namespace voxy::platform
