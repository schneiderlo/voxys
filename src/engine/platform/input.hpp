// ═══════════════════════════════════════════════════════════════════════════════
// input.hpp - Cross-platform Input System
// ═══════════════════════════════════════════════════════════════════════════════
// Provides unified keyboard and mouse input handling across native (GLFW) and
// web (Emscripten) platforms. Tracks key/button states, mouse position/delta,
// and mouse capture mode.
// ═══════════════════════════════════════════════════════════════════════════════

#pragma once

#include <array>
#include <cstdint>
#include <glm/vec2.hpp>
#include <vector>

#if defined(VOXY_NATIVE)
    #define GLFW_INCLUDE_NONE
    #include <GLFW/glfw3.h>
#elif defined(__EMSCRIPTEN__)
    #include <emscripten/html5.h>
#endif

namespace voxy {

// Forward declaration
class Window;

// ─────────────────────────────────────────────────────────────────────────────
// Key Enumeration
// ─────────────────────────────────────────────────────────────────────────────
// Maps common keys to platform-independent codes.
// Values match GLFW key codes on native platforms.

enum class Key : int {
    // Letters
    A = 65, B = 66, C = 67, D = 68, E = 69, F = 70, G = 71, H = 72,
    I = 73, J = 74, K = 75, L = 76, M = 77, N = 78, O = 79, P = 80,
    Q = 81, R = 82, S = 83, T = 84, U = 85, V = 86, W = 87, X = 88,
    Y = 89, Z = 90,
    
    // Numbers
    Num0 = 48, Num1 = 49, Num2 = 50, Num3 = 51, Num4 = 52,
    Num5 = 53, Num6 = 54, Num7 = 55, Num8 = 56, Num9 = 57,
    
    // Function keys
    F1 = 290, F2 = 291, F3 = 292, F4 = 293, F5 = 294, F6 = 295,
    F7 = 296, F8 = 297, F9 = 298, F10 = 299, F11 = 300, F12 = 301,
    
    // Special keys
    Space = 32,
    Escape = 256,
    Enter = 257,
    Tab = 258,
    Backspace = 259,
    Insert = 260,
    Delete = 261,
    
    // Arrow keys
    Right = 262,
    Left = 263,
    Down = 264,
    Up = 265,
    
    // Modifiers
    LeftShift = 340,
    LeftControl = 341,
    LeftAlt = 342,
    RightShift = 344,
    RightControl = 345,
    RightAlt = 346,
    
    // Common aliases
    Shift = LeftShift,
    Control = LeftControl,
    Ctrl = LeftControl,
    Alt = LeftAlt,
    
    // Key count for array sizing
    MaxKey = 512
};

// ─────────────────────────────────────────────────────────────────────────────
// Mouse Button Enumeration
// ─────────────────────────────────────────────────────────────────────────────

enum class MouseButton : int {
    Left = 0,
    Right = 1,
    Middle = 2,
    
    MaxButton = 8
};

// ─────────────────────────────────────────────────────────────────────────────
// Input Class
// ─────────────────────────────────────────────────────────────────────────────
// Manages input state including keyboard, mouse position, and mouse buttons.
// Call update() once per frame before processing input.

class Input {
public:
    Input();
    ~Input() = default;
    
    // Non-copyable
    Input(const Input&) = delete;
    Input& operator=(const Input&) = delete;
    
    // Movable
    Input(Input&&) noexcept = default;
    Input& operator=(Input&&) noexcept = default;
    
    // ─────────────────────────────────────────────────────────────────────────
    // Frame Update
    // ─────────────────────────────────────────────────────────────────────────
    
    // Call once per frame to save previous state.
    // Must be called BEFORE polling input events.
    void beginFrame();
    
    // Call once per frame AFTER polling events to compute deltas.
    void computeDeltas();
    
    // Call once per frame after processing events to finalize state.
    void endFrame();
    
    // ─────────────────────────────────────────────────────────────────────────
    // Keyboard State
    // ─────────────────────────────────────────────────────────────────────────
    
    // Check if a key is currently held down
    [[nodiscard]] bool isKeyDown(Key key) const;
    
    // Check if a key was just pressed this frame (not held)
    [[nodiscard]] bool wasKeyPressed(Key key) const;
    
    // Check if a key was just released this frame
    [[nodiscard]] bool wasKeyReleased(Key key) const;
    
    // ─────────────────────────────────────────────────────────────────────────
    // Mouse State
    // ─────────────────────────────────────────────────────────────────────────
    
    // Get current mouse position in window coordinates
    [[nodiscard]] glm::vec2 mousePosition() const { return mousePos_; }
    
    // Get mouse movement since last frame
    [[nodiscard]] glm::vec2 mouseDelta() const { return mouseDelta_; }
    
    // Check if a mouse button is currently held down
    [[nodiscard]] bool isMouseButtonDown(MouseButton button) const;
    
    // Check if a mouse button was just pressed this frame
    [[nodiscard]] bool wasMouseButtonPressed(MouseButton button) const;
    
    // Check if a mouse button was just released this frame
    [[nodiscard]] bool wasMouseButtonReleased(MouseButton button) const;
    
    // Get scroll wheel delta since last frame
    [[nodiscard]] float scrollDelta() const { return scrollDelta_; }
    
    // ─────────────────────────────────────────────────────────────────────────
    // Mouse Capture
    // ─────────────────────────────────────────────────────────────────────────
    
    // Check if mouse is captured (hidden and locked to window)
    [[nodiscard]] bool isMouseCaptured() const { return captured_; }
    
    // Capture/release mouse (hides cursor and provides raw delta movement)
    void captureMouse();
    void releaseMouse();
    
    // Toggle capture state
    void toggleMouseCapture();
    
    // ─────────────────────────────────────────────────────────────────────────
    // Platform Integration
    // ─────────────────────────────────────────────────────────────────────────
    
    // Connect input to a GLFW window (native only) or no-op (WASM)
    void attachToWindow(Window& window);

    // Set up Emscripten event handlers (WASM only) or no-op (native)
    void setupEmscriptenCallbacks(const char* canvasSelector = "#canvas");
    
    // ─────────────────────────────────────────────────────────────────────────
    // Event Handlers (called by platform-specific code)
    // ─────────────────────────────────────────────────────────────────────────
    
    void onKeyDown(int keyCode);
    void onKeyUp(int keyCode);
    void onMouseMove(float x, float y);
    void onMouseDown(int button);
    void onMouseUp(int button);
    void onScroll(float delta);
    
    // ─────────────────────────────────────────────────────────────────────────
    // Accessors for Window (for cursor mode changes)
    // ─────────────────────────────────────────────────────────────────────────
    
    void setWindow(Window* window) { window_ = window; }
    [[nodiscard]] Window* getWindow() const { return window_; }
    
private:
    static constexpr size_t kMaxKeys = static_cast<size_t>(Key::MaxKey);
    static constexpr size_t kMaxButtons = static_cast<size_t>(MouseButton::MaxButton);
    
    // Keyboard state
    std::array<bool, kMaxKeys> currentKeys_{};
    std::array<bool, kMaxKeys> previousKeys_{};
    // Track keys pressed this frame (even if released before frame end)
    // This prevents losing quick press+release events within a single frame
    std::array<bool, kMaxKeys> keysPressedThisFrame_{};
    
    // Mouse position and delta
    glm::vec2 mousePos_{0.0f, 0.0f};
    glm::vec2 prevMousePos_{0.0f, 0.0f};
    glm::vec2 mouseDelta_{0.0f, 0.0f};
    bool firstMouseMove_ = true;
    
    // Mouse buttons
    std::array<bool, kMaxButtons> currentButtons_{};
    std::array<bool, kMaxButtons> previousButtons_{};
    // Track buttons pressed this frame (even if released before frame end)
    std::array<bool, kMaxButtons> buttonsPressedThisFrame_{};
    
    // Scroll
    float scrollDelta_ = 0.0f;
    float accumulatedScroll_ = 0.0f;
    
    // Capture state
    bool captured_ = false;
    
    // Associated window (for cursor mode changes)
    Window* window_ = nullptr;
    
    // Helper to validate key index
    [[nodiscard]] bool isValidKey(int keyCode) const {
        return keyCode >= 0 && keyCode < static_cast<int>(kMaxKeys);
    }
    
    // Helper to validate button index
    [[nodiscard]] bool isValidButton(int button) const {
        return button >= 0 && button < static_cast<int>(kMaxButtons);
    }

    // ─────────────────────────────────────────────────────────────────────────
    // Event Buffering
    // ─────────────────────────────────────────────────────────────────────────
    
    struct KeyEvent {
        int key;
        bool down;
    };
    
    struct MouseButtonEvent {
        int button;
        bool down;
    };
    
    std::vector<KeyEvent> keyQueue_;
    std::vector<MouseButtonEvent> mouseButtonQueue_;
    
    void processEvents();
};

// ─────────────────────────────────────────────────────────────────────────────
// Emscripten Key Code Conversion
// ─────────────────────────────────────────────────────────────────────────────

#if defined(__EMSCRIPTEN__)
// Convert Emscripten key code string to our Key enum value
int emscriptenKeyToCode(const char* code);
#endif

} // namespace voxy


