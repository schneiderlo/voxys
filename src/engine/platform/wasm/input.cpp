// ═══════════════════════════════════════════════════════════════════════════════
// input.cpp (WASM) - Emscripten Input System Implementation
// ═══════════════════════════════════════════════════════════════════════════════

#include "engine/platform/input.hpp"
#include <glm/common.hpp>
#include "engine/platform/window.hpp"
#include "core/log.hpp"

#include <emscripten/html5.h>
#include <emscripten.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>

namespace voxy {

namespace {

constexpr size_t kMaximumQueuedInputEvents = 4'096u;
constexpr float kMaximumMouseCoordinate = 1.0e9f;
constexpr double kMaximumAccumulatedScroll = 10'000.0;

} // namespace

// ─────────────────────────────────────────────────────────────────────────────
// Forward Declarations
// ─────────────────────────────────────────────────────────────────────────────

int emscriptenKeyToCode(const char* code);

// ─────────────────────────────────────────────────────────────────────────────
// Constructor
// ─────────────────────────────────────────────────────────────────────────────

Input::Input() {
    currentKeys_.fill(false);
    previousKeys_.fill(false);
    keysPressedThisFrame_.fill(false);
    keysReleasedThisFrame_.fill(false);
    currentButtons_.fill(false);
    previousButtons_.fill(false);
    buttonsPressedThisFrame_.fill(false);
    buttonsReleasedThisFrame_.fill(false);
}

// ─────────────────────────────────────────────────────────────────────────────
// Frame Update
// ─────────────────────────────────────────────────────────────────────────────

void Input::beginFrame() {
    // Copy current state to previous state BEFORE events are processed
    previousKeys_ = currentKeys_;
    previousButtons_ = currentButtons_;
    
    // Clear per-frame press accumulators
    keysPressedThisFrame_.fill(false);
    keysReleasedThisFrame_.fill(false);
    buttonsPressedThisFrame_.fill(false);
    buttonsReleasedThisFrame_.fill(false);
    
    // Process buffered events
    processEvents();
    
    // Reset per-frame deltas
    scrollDelta_ = accumulatedScroll_;
    accumulatedScroll_ = 0.0f;
}

void Input::processEvents() {
    // Process buffered key events
    for (const auto& event : keyQueue_) {
        if (isValidKey(event.key)) {
            const size_t key = static_cast<size_t>(event.key);
            if (event.down) {
                // Only flag a fresh press if the key was not already held. Browser
                // key auto-repeat fires "keydown" every frame while held; if we set
                // the accumulator each time, wasKeyPressed() stays true for the whole
                // hold and toggles (F1/F3/...) flicker.
                if (!currentKeys_[key]) {
                    keysPressedThisFrame_[key] = true;
                }
                currentKeys_[key] = true;
            } else {
                if (currentKeys_[key]) {
                    keysReleasedThisFrame_[key] = true;
                }
                currentKeys_[key] = false;
            }
        }
    }
    keyQueue_.clear();
    
    // Process buffered mouse button events
    for (const auto& event : mouseButtonQueue_) {
        if (isValidButton(event.button)) {
            const size_t button = static_cast<size_t>(event.button);
            if (event.down) {
                if (!currentButtons_[button]) {
                    buttonsPressedThisFrame_[button] = true;
                }
                currentButtons_[button] = true;
            } else {
                if (currentButtons_[button]) {
                    buttonsReleasedThisFrame_[button] = true;
                }
                currentButtons_[button] = false;
            }
        }
    }
    mouseButtonQueue_.clear();
}

void Input::computeDeltas() {
    dragDeltas_=accumulatedDrags_;accumulatedDrags_.fill(glm::vec2(0));
    // Events can arrive between beginFrame() and update(). Process them before
    // input is queried so native and web have the same frame semantics.
    processEvents();

    scrollDelta_ += accumulatedScroll_;
    accumulatedScroll_ = 0.0f;

    // Compute mouse delta AFTER events have been polled
    if (firstMouseMove_) {
        mouseDelta_ = glm::vec2(0.0f);
    } else {
        mouseDelta_ = mousePos_ - prevMousePos_;
    }
    prevMousePos_ = mousePos_;
}

void Input::endFrame() {
    // Nothing to do here
}

void Input::resetState() {
    rawButtons_.fill(false);accumulatedDrags_.fill(glm::vec2(0));dragDeltas_.fill(glm::vec2(0));
    releaseMouse();
    currentKeys_.fill(false);
    previousKeys_.fill(false);
    keysPressedThisFrame_.fill(false);
    keysReleasedThisFrame_.fill(false);
    currentButtons_.fill(false);
    previousButtons_.fill(false);
    buttonsPressedThisFrame_.fill(false);
    buttonsReleasedThisFrame_.fill(false);
    keyQueue_.clear();
    mouseButtonQueue_.clear();
    mouseDelta_ = glm::vec2(0.0f);
    prevMousePos_ = mousePos_;
    firstMouseMove_ = true;
    scrollDelta_ = 0.0f;
    accumulatedScroll_ = 0.0f;
}

// ─────────────────────────────────────────────────────────────────────────────
// Keyboard State
// ─────────────────────────────────────────────────────────────────────────────

bool Input::isKeyDown(Key key) const {
    const int code = static_cast<int>(key);
    if (!isValidKey(code)) return false;
    return currentKeys_[static_cast<size_t>(code)];
}

bool Input::wasKeyPressed(Key key) const {
    const int code = static_cast<int>(key);
    if (!isValidKey(code)) return false;
    // Check both: standard press detection AND the per-frame accumulator
    // The accumulator catches quick press+release within a single frame
    const size_t index = static_cast<size_t>(code);
    return (currentKeys_[index] && !previousKeys_[index])
        || keysPressedThisFrame_[index];
}

bool Input::wasKeyReleased(Key key) const {
    const int code = static_cast<int>(key);
    if (!isValidKey(code)) return false;
    const size_t index = static_cast<size_t>(code);
    return (!currentKeys_[index] && previousKeys_[index])
        || keysReleasedThisFrame_[index];
}

// ─────────────────────────────────────────────────────────────────────────────
// Mouse State
// ─────────────────────────────────────────────────────────────────────────────

bool Input::isMouseButtonDown(MouseButton button) const {
    const int idx = static_cast<int>(button);
    if (!isValidButton(idx)) return false;
    return currentButtons_[static_cast<size_t>(idx)];
}

bool Input::wasMouseButtonPressed(MouseButton button) const {
    const int idx = static_cast<int>(button);
    if (!isValidButton(idx)) return false;
    // Check both: standard press detection AND the per-frame accumulator
    // The accumulator catches quick press+release within a single frame
    const size_t index = static_cast<size_t>(idx);
    const bool standardResult = currentButtons_[index]
        && !previousButtons_[index];
    return standardResult || buttonsPressedThisFrame_[index];
}

bool Input::wasMouseButtonReleased(MouseButton button) const {
    const int idx = static_cast<int>(button);
    if (!isValidButton(idx)) return false;
    const size_t index = static_cast<size_t>(idx);
    return (!currentButtons_[index] && previousButtons_[index])
        || buttonsReleasedThisFrame_[index];
}

// ─────────────────────────────────────────────────────────────────────────────
// Mouse Capture
// ─────────────────────────────────────────────────────────────────────────────

void Input::captureMouse() {
    if (captured_) return;
    
    captured_ = true;
    firstMouseMove_ = true;  // Reset to avoid large delta on capture
    
    if (window_) {
        window_->setCursorCaptured(true);
    }
    
    const EMSCRIPTEN_RESULT result =
        emscripten_request_pointerlock("#voxy-canvas", EM_TRUE);
    if (result < EMSCRIPTEN_RESULT_SUCCESS) {
        onMouseCaptureChanged(false);
        LOG_WARN("Pointer lock request failed: {}", result);
        return;
    }
    
    LOG_DEBUG("Mouse captured");
}

void Input::releaseMouse() {
    if (!captured_) return;
    
    captured_ = false;
    
    if (window_) {
        window_->setCursorCaptured(false);
    }
    
    emscripten_exit_pointerlock();
    
    LOG_DEBUG("Mouse released");
}

void Input::toggleMouseCapture() {
    if (captured_) {
        releaseMouse();
    } else {
        captureMouse();
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Event Handlers
// ─────────────────────────────────────────────────────────────────────────────

void Input::onKeyDown(int keyCode) {
    if (isValidKey(keyCode)) {
        if (keyQueue_.size() >= kMaximumQueuedInputEvents) {
            keyQueue_.clear();
            for (size_t key = 0; key < currentKeys_.size(); ++key) {
                if (currentKeys_[key]) {
                    keyQueue_.push_back(
                        {static_cast<int>(key), false});
                }
            }
        }
        keyQueue_.push_back({keyCode, true});
    }
}

void Input::onKeyUp(int keyCode) {
    if (isValidKey(keyCode)) {
        if (keyQueue_.size() >= kMaximumQueuedInputEvents) {
            keyQueue_.clear();
            for (size_t key = 0; key < currentKeys_.size(); ++key) {
                if (currentKeys_[key]) {
                    keyQueue_.push_back(
                        {static_cast<int>(key), false});
                }
            }
        }
        keyQueue_.push_back({keyCode, false});
    }
}

void Input::onMouseMove(float x, float y) {
    if (!std::isfinite(x) || !std::isfinite(y)
        || std::abs(x) > kMaximumMouseCoordinate
        || std::abs(y) > kMaximumMouseCoordinate) {
        return;
    }
    if (firstMouseMove_) {
        prevMousePos_ = glm::vec2(x, y);
        firstMouseMove_ = false;
    }
    for(size_t i=0;i<rawButtons_.size();++i)if(rawButtons_[i]) {
        accumulatedDrags_[i]=glm::clamp(accumulatedDrags_[i]+glm::vec2(x,y)-mousePos_,
            glm::vec2(-kMaximumMouseCoordinate),glm::vec2(kMaximumMouseCoordinate));
    }
    mousePos_ = glm::vec2(x, y);
}

void Input::onMouseDown(int button) {
    if (isValidButton(button)) {
        if (mouseButtonQueue_.size() >= kMaximumQueuedInputEvents) {
            rawButtons_.fill(false);accumulatedDrags_.fill(glm::vec2(0));
            mouseButtonQueue_.clear();
            for (size_t index = 0; index < currentButtons_.size(); ++index) {
                if (currentButtons_[index]) {
                    mouseButtonQueue_.push_back(
                        {static_cast<int>(index), false});
                }
            }
        }
        rawButtons_[static_cast<size_t>(button)]=true;
        mouseButtonQueue_.push_back({button, true});
    }
}

void Input::onMouseUp(int button) {
    if (isValidButton(button)) {
        if (mouseButtonQueue_.size() >= kMaximumQueuedInputEvents) {
            rawButtons_.fill(false);accumulatedDrags_.fill(glm::vec2(0));
            mouseButtonQueue_.clear();
            for (size_t index = 0; index < currentButtons_.size(); ++index) {
                if (currentButtons_[index]) {
                    mouseButtonQueue_.push_back(
                        {static_cast<int>(index), false});
                }
            }
        }
        rawButtons_[static_cast<size_t>(button)]=false;
        mouseButtonQueue_.push_back({button, false});
    }
}

void Input::onScroll(float delta) {
    if (!std::isfinite(delta)) return;
    accumulatedScroll_ = static_cast<float>(std::clamp(
        static_cast<double>(accumulatedScroll_)
            + static_cast<double>(delta),
        -kMaximumAccumulatedScroll, kMaximumAccumulatedScroll));
}

void Input::attachToWindow(Window& /*window*/) {
    // No-op for WASM
}

// ─────────────────────────────────────────────────────────────────────────────
// Web Platform Integration (Emscripten)
// ─────────────────────────────────────────────────────────────────────────────

// Emscripten callback helpers
namespace {

Input* g_inputInstance = nullptr;
std::string g_inputCanvas;

int browserButtonToVoxyButton(int button) {
    // DOM: left=0, middle=1, right=2. The engine follows GLFW:
    // left=0, right=1, middle=2.
    if (button == 1) return static_cast<int>(MouseButton::Middle);
    if (button == 2) return static_cast<int>(MouseButton::Right);
    return button;
}

EM_BOOL emKeyDownCallback(int /*eventType*/, const EmscriptenKeyboardEvent* e, void* /*userData*/) {
    if (g_inputInstance) {
        int code = emscriptenKeyToCode(e->code);
        g_inputInstance->onKeyDown(code);
    }
    return EM_TRUE;
}

EM_BOOL emKeyUpCallback(int /*eventType*/, const EmscriptenKeyboardEvent* e, void* /*userData*/) {
    if (g_inputInstance) {
        int code = emscriptenKeyToCode(e->code);
        g_inputInstance->onKeyUp(code);
    }
    return EM_TRUE;
}

EM_BOOL emMouseMoveCallback(int /*eventType*/, const EmscriptenMouseEvent* e, void* /*userData*/) {
    if (g_inputInstance) {
        if (g_inputInstance->isMouseCaptured()) {
            // When captured, use movement deltas
            glm::vec2 currentPos = g_inputInstance->mousePosition();
            g_inputInstance->onMouseMove(
                currentPos.x + static_cast<float>(e->movementX),
                currentPos.y + static_cast<float>(e->movementY)
            );
        } else {
            // When not captured, use absolute position
            g_inputInstance->onMouseMove(
                static_cast<float>(e->targetX),
                static_cast<float>(e->targetY)
            );
        }
    }
    return EM_TRUE;
}

EM_BOOL emMouseDownCallback(int /*eventType*/, const EmscriptenMouseEvent* e, void* /*userData*/) {
    if (g_inputInstance) {
        g_inputInstance->onMouseDown(browserButtonToVoxyButton(e->button));
    }
    return EM_TRUE;
}

EM_BOOL emMouseUpCallback(int /*eventType*/, const EmscriptenMouseEvent* e, void* /*userData*/) {
    if (g_inputInstance) {
        g_inputInstance->onMouseUp(browserButtonToVoxyButton(e->button));
    }
    return EM_FALSE; // Document-level release must not swallow UI activation.
}

EM_BOOL emWheelCallback(int /*eventType*/, const EmscriptenWheelEvent* e, void* /*userData*/) {
    if (g_inputInstance) {
        // Unlocked workshop scroll over UI belongs to the panel. Pointer-lock
        // wheel events can still be retargeted to the document by the browser.
        if(!g_inputInstance->isMouseCaptured()&&!EM_ASM_INT({
            return document.querySelector(UTF8ToString($0))===document.elementFromPoint($1,$2);
        },g_inputCanvas.c_str(),e->mouse.clientX,e->mouse.clientY))return EM_FALSE;
        // Normalize scroll delta (different browsers report different values)
        float delta = static_cast<float>(-e->deltaY);
        if (e->deltaMode == DOM_DELTA_LINE) {
            delta *= 40.0f;  // Approximate pixels per line
        } else if (e->deltaMode == DOM_DELTA_PAGE) {
            delta *= 800.0f;  // Approximate pixels per page
        }
        delta /= 100.0f;  // Normalize to reasonable range
        g_inputInstance->onScroll(delta);
    }
    return EM_TRUE;
}

EM_BOOL emBlurCallback(int /*eventType*/, const EmscriptenFocusEvent* /*event*/,
                       void* /*userData*/) {
    if (g_inputInstance) {
        g_inputInstance->resetState();
    }
    return EM_FALSE;
}

EM_BOOL emPointerLockChangeCallback(
    int /*eventType*/, const EmscriptenPointerlockChangeEvent* event,
    void* /*userData*/) {
    if (g_inputInstance) {
        g_inputInstance->onMouseCaptureChanged(event->isActive);
    }
    return EM_FALSE;
}

}  // namespace

void Input::setupEmscriptenCallbacks(const char* canvasSelector) {
    g_inputInstance = this;
    g_inputCanvas=canvasSelector;
    
    // Keyboard events on document (to capture when canvas doesn't have focus)
    emscripten_set_keydown_callback(EMSCRIPTEN_EVENT_TARGET_DOCUMENT, nullptr, false, emKeyDownCallback);
    emscripten_set_keyup_callback(EMSCRIPTEN_EVENT_TARGET_DOCUMENT, nullptr, false, emKeyUpCallback);
    
    // Mouse events on canvas
    emscripten_set_mousemove_callback(canvasSelector, nullptr, false, emMouseMoveCallback);
    emscripten_set_mousedown_callback(canvasSelector, nullptr, false, emMouseDownCallback);
    emscripten_set_mouseup_callback(EMSCRIPTEN_EVENT_TARGET_DOCUMENT, nullptr, false, emMouseUpCallback);
    // Pointer lock can retarget wheel events away from the canvas in some
    // browsers. The game owns the full document, so listen there reliably.
    emscripten_set_wheel_callback(EMSCRIPTEN_EVENT_TARGET_DOCUMENT, nullptr,
                                  false, emWheelCallback);
    // Browsers do not guarantee keyup or mouseup delivery after a tab/window
    // loses focus. Clear all held state before it can become a stuck control.
    emscripten_set_blur_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, nullptr,
                                 false, emBlurCallback);
    emscripten_set_pointerlockchange_callback(
        EMSCRIPTEN_EVENT_TARGET_DOCUMENT, nullptr, false,
        emPointerLockChangeCallback);
    
    LOG_DEBUG("Emscripten input callbacks set up for {}", canvasSelector);
}

// Convert Emscripten key code string to our Key enum value
int emscriptenKeyToCode(const char* code) {
    // Letters (KeyA, KeyB, etc.)
    if (code[0] == 'K' && code[1] == 'e' && code[2] == 'y' && code[3] != '\0' && code[4] == '\0') {
        char letter = code[3];
        if (letter >= 'A' && letter <= 'Z') {
            return static_cast<int>(Key::A) + (letter - 'A');
        }
    }
    
    // Digits (Digit0, Digit1, etc.)
    if (std::strncmp(code, "Digit", 5) == 0 && code[5] >= '0' && code[5] <= '9' && code[6] == '\0') {
        return static_cast<int>(Key::Num0) + (code[5] - '0');
    }
    
    // Function keys (F1, F2, etc.)
    if (code[0] == 'F' && code[1] >= '1' && code[1] <= '9') {
        if (code[2] == '\0') {
            return static_cast<int>(Key::F1) + (code[1] - '1');
        }
        if (code[1] == '1' && code[2] >= '0' && code[2] <= '2' && code[3] == '\0') {
            return static_cast<int>(Key::F10) + (code[2] - '0');
        }
    }
    
    // Special keys
    if (std::strcmp(code, "Space") == 0) return static_cast<int>(Key::Space);
    if (std::strcmp(code, "Escape") == 0) return static_cast<int>(Key::Escape);
    if (std::strcmp(code, "Enter") == 0) return static_cast<int>(Key::Enter);
    if (std::strcmp(code, "Tab") == 0) return static_cast<int>(Key::Tab);
    if (std::strcmp(code, "Backspace") == 0) return static_cast<int>(Key::Backspace);
    if (std::strcmp(code, "Insert") == 0) return static_cast<int>(Key::Insert);
    if (std::strcmp(code, "Delete") == 0) return static_cast<int>(Key::Delete);
    
    // Arrow keys
    if (std::strcmp(code, "ArrowRight") == 0) return static_cast<int>(Key::Right);
    if (std::strcmp(code, "ArrowLeft") == 0) return static_cast<int>(Key::Left);
    if (std::strcmp(code, "ArrowDown") == 0) return static_cast<int>(Key::Down);
    if (std::strcmp(code, "ArrowUp") == 0) return static_cast<int>(Key::Up);
    
    // Modifiers
    if (std::strcmp(code, "ShiftLeft") == 0) return static_cast<int>(Key::LeftShift);
    if (std::strcmp(code, "ShiftRight") == 0) return static_cast<int>(Key::RightShift);
    if (std::strcmp(code, "ControlLeft") == 0) return static_cast<int>(Key::LeftControl);
    if (std::strcmp(code, "ControlRight") == 0) return static_cast<int>(Key::RightControl);
    if (std::strcmp(code, "AltLeft") == 0) return static_cast<int>(Key::LeftAlt);
    if (std::strcmp(code, "AltRight") == 0) return static_cast<int>(Key::RightAlt);
    
    // Unknown key
    return -1;
}

Input::~Input() {
    if (g_inputInstance == this) {
        g_inputInstance = nullptr;
    }
    if (captured_) {
        releaseMouse();
    }
}

} // namespace voxy
