#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

namespace voxy { class Input; }
namespace voxy::game::expedition {

enum class CoveInputContext : uint8_t { World=1, Workshop=2, Menu=4 };
enum class CoveAction : uint8_t {
    MoveForward, MoveBack, MoveLeft, MoveRight, Interact, Jump, Workshop, CameraMenu,
    ToolsMenu, Pause, Save, Rescue, Hook, ReelIn, PayOut, AcceptJob, Deliver, PowerHarbor,
    Cut, CameraMode, Recenter, FrameLoad, ReducedMotion,
    PreviousPart, NextPart, PartLeft, PartRight, PartForward, PartBack, PartUp, PartDown,
    RotateY, RotateX, RotateZ, Snap, Keep, Undo, Redo, Cancel, Remove, Launch,
    UndoLaunch, RedoLaunch, NextType, AddPart, TogglePart, LimitPart, ReversePart,
    RebuildStarter, LoadRecovery, NextRecovery, RemoveRecovery, FocusPart, WholeBoat,
    Paint, BrickOne, BrickTwo, BrickThree, SelectAll, OnlyPrimary, Duplicate,
    MirrorX, MirrorZ, Replace, ToggleNext, OrbitLeft, OrbitRight, ZoomIn, ZoomOut,
    PanMode, ZoomMode, Count
};
inline constexpr size_t kCoveActionCount=static_cast<size_t>(CoveAction::Count);
inline constexpr size_t kMaximumCovePreferencesBytes=32*1024;
struct CoveBinding {
    int key=0, alternate=0; // GLFW-compatible code; zero means unbound.
    uint8_t modifiers=0, alternateModifiers=0; // Shift1, Control2, Alt4; exact match.
    int pad=-1; // Standard PadButton index; -1 means unbound.
    bool operator==(const CoveBinding&) const=default;
};
struct CoveActionInfo {
    CoveAction action;
    std::string_view id, label;
    uint8_t contexts;
    CoveBinding defaults;
};
[[nodiscard]] std::span<const CoveActionInfo> coveActionList() noexcept;
[[nodiscard]] std::string coveKeyLabel(int key, uint8_t modifiers=0);
[[nodiscard]] std::string covePadLabel(int button);
struct CoveInputPreferences {
    double mouseSensitivity=1, padSensitivity=1; // Multipliers .25..3.
    double moveDeadzone=.2, lookDeadzone=.2; // Radial .05...45.
    bool invertX=false, invertY=false, reelToggle=false, orbitToggle=false;
    double textScale=1; // Exactly1,1.25,1.5.
    bool highContrast=false, tutorialsEnabled=true, captionsEnabled=true;
    std::string locale="en"; // Only English is installed.
    std::array<CoveBinding,kCoveActionCount> bindings{};
    CoveInputPreferences();
    bool operator==(const CoveInputPreferences&) const=default;
};
// Complete, strict version1 JSON. Duplicate/unknown/missing fields, conflicting
// bindings or non-finite/range-invalid values refuse without changing output.
[[nodiscard]] bool validateCoveInputPreferences(const CoveInputPreferences&, std::string& error);
[[nodiscard]] bool parseCoveInputPreferences(std::string_view, CoveInputPreferences&, std::string& error);
[[nodiscard]] bool encodeCoveInputPreferences(const CoveInputPreferences&, std::string&, std::string& error);
[[nodiscard]] bool rebindCoveAction(CoveInputPreferences&, CoveAction, CoveBinding, std::string& error);

struct CoveInputSample {
    bool focused=true, padConnected=false, padArmed=false;
    uint64_t resetSerial=0, padSerial=0;
    std::array<bool,512> keys{}, pressed{}, physicalKeys{};
    std::array<uint8_t,512> pressModifiers{};
    uint8_t modifiers=0;
    std::array<bool,17> padDown{}, padPressed{};
    std::array<float,4> axes{}; // Already radial-filtered once by GamepadInput.
    bool mouseLeft=false, mouseRight=false, mouseMiddle=false, rightPressed=false;
};
[[nodiscard]] CoveInputSample sampleCoveInput(const voxy::Input&);
struct CoveActionState { bool down=false, pressed=false, released=false; };
// One tick per displayed input frame. Use Menu while any modal UI owns input.
// World and Workshop actions never fire in Menu. Fixed menu navigation remains
// outside this router, so rebinding cannot remove Escape/Enter/controller Back.
class CoveInputRouter {
public:
    void reset() noexcept;
    void tick(const CoveInputSample&, CoveInputContext, const CoveInputPreferences&);
    [[nodiscard]] CoveActionState state(CoveAction action) const noexcept;
    [[nodiscard]] bool down(CoveAction action) const noexcept {return state(action).down;}
    [[nodiscard]] bool pressed(CoveAction action) const noexcept {return state(action).pressed;}
    [[nodiscard]] bool released(CoveAction action) const noexcept {return state(action).released;}
    [[nodiscard]] std::array<double,2> movement() const noexcept {return movement_;}
    [[nodiscard]] std::array<double,2> look() const noexcept {return look_;}
    [[nodiscard]] bool orbitDrag() const noexcept {return orbitDrag_;}
private:
    bool initialized_=false,focused_=false,padConnected_=false,padArmed_=false;
    uint64_t resetSerial_=0,padSerial_=0;
    uint64_t preferenceSignature_=0;
    CoveInputContext context_=CoveInputContext::Menu;
    std::array<bool,512> blockedKeys_{};
    std::array<CoveActionState,kCoveActionCount> states_{};
    std::array<double,2> movement_{},look_{};
    int reelDirection_=0;
    bool blockedPad_=true,blockedMouse_=true,orbitDrag_=false;
};
} // namespace voxy::game::expedition
