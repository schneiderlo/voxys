// ═══════════════════════════════════════════════════════════════════════════════
// input.cpp (WASM) - Emscripten Input System Implementation
// ═══════════════════════════════════════════════════════════════════════════════

#include "engine/platform/input.hpp"
#include "engine/platform/window.hpp"
#include "core/log.hpp"

#include <emscripten/html5.h>
#include <cstring>

namespace voxy {

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
    currentButtons_.fill(false);
    previousButtons_.fill(false);
    buttonsPressedThisFrame_.fill(false);
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
    buttonsPressedThisFrame_.fill(false);
    
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
            if (event.down) {
                currentKeys_[event.key] = true;
                keysPressedThisFrame_[event.key] = true;
            } else {
                currentKeys_[event.key] = false;
            }
        }
    }
    keyQueue_.clear();
    
    // Process buffered mouse button events
    for (const auto& event : mouseButtonQueue_) {
        if (isValidButton(event.button)) {
            if (event.down) {
                currentButtons_[event.button] = true;
                buttonsPressedThisFrame_[event.button] = true;
            } else {
                currentButtons_[event.button] = false;
            }
        }
    }
    mouseButtonQueue_.clear();
}

void Input::computeDeltas() {
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

// ─────────────────────────────────────────────────────────────────────────────
// Keyboard State
// ─────────────────────────────────────────────────────────────────────────────

bool Input::isKeyDown(Key key) const {
    int code = static_cast<int>(key);
    if (!isValidKey(code)) return false;
    return currentKeys_[code];
}

bool Input::wasKeyPressed(Key key) const {
    int code = static_cast<int>(key);
    if (!isValidKey(code)) return false;
    // Check both: standard press detection AND the per-frame accumulator
    // The accumulator catches quick press+release within a single frame
    return (currentKeys_[code] && !previousKeys_[code]) || keysPressedThisFrame_[code];
}

bool Input::wasKeyReleased(Key key) const {
    int code = static_cast<int>(key);
    if (!isValidKey(code)) return false;
    return !currentKeys_[code] && previousKeys_[code];
}

// ─────────────────────────────────────────────────────────────────────────────
// Mouse State
// ─────────────────────────────────────────────────────────────────────────────

bool Input::isMouseButtonDown(MouseButton button) const {
    int idx = static_cast<int>(button);
    if (!isValidButton(idx)) return false;
    return currentButtons_[idx];
}

bool Input::wasMouseButtonPressed(MouseButton button) const {
    int idx = static_cast<int>(button);
    if (!isValidButton(idx)) return false;
    // Check both: standard press detection AND the per-frame accumulator
    // The accumulator catches quick press+release within a single frame
    bool standardResult = currentButtons_[idx] && !previousButtons_[idx];
    return standardResult || buttonsPressedThisFrame_[idx];
}

bool Input::wasMouseButtonReleased(MouseButton button) const {
    int idx = static_cast<int>(button);
    if (!isValidButton(idx)) return false;
    return !currentButtons_[idx] && previousButtons_[idx];
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
    
    emscripten_request_pointerlock("#voxy-canvas", EM_TRUE);
    
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
        keyQueue_.push_back({keyCode, true});
    }
}

void Input::onKeyUp(int keyCode) {
    if (isValidKey(keyCode)) {
        keyQueue_.push_back({keyCode, false});
    }
}

void Input::onMouseMove(float x, float y) {
    if (firstMouseMove_) {
        prevMousePos_ = glm::vec2(x, y);
        firstMouseMove_ = false;
    }
    mousePos_ = glm::vec2(x, y);
}

void Input::onMouseDown(int button) {
    if (isValidButton(button)) {
        mouseButtonQueue_.push_back({button, true});
    }
}

void Input::onMouseUp(int button) {
    if (isValidButton(button)) {
        mouseButtonQueue_.push_back({button, false});
    }
}

void Input::onScroll(float delta) {
    accumulatedScroll_ += delta;
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
        g_inputInstance->onMouseDown(e->button);
    }
    return EM_TRUE;
}

EM_BOOL emMouseUpCallback(int /*eventType*/, const EmscriptenMouseEvent* e, void* /*userData*/) {
    if (g_inputInstance) {
        g_inputInstance->onMouseUp(e->button);
    }
    return EM_TRUE;
}

EM_BOOL emWheelCallback(int /*eventType*/, const EmscriptenWheelEvent* e, void* /*userData*/) {
    if (g_inputInstance) {
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

}  // namespace

void Input::setupEmscriptenCallbacks(const char* canvasSelector) {
    g_inputInstance = this;
    
    // Keyboard events on document (to capture when canvas doesn't have focus)
    emscripten_set_keydown_callback(EMSCRIPTEN_EVENT_TARGET_DOCUMENT, nullptr, false, emKeyDownCallback);
    emscripten_set_keyup_callback(EMSCRIPTEN_EVENT_TARGET_DOCUMENT, nullptr, false, emKeyUpCallback);
    
    // Mouse events on canvas
    emscripten_set_mousemove_callback(canvasSelector, nullptr, false, emMouseMoveCallback);
    emscripten_set_mousedown_callback(canvasSelector, nullptr, false, emMouseDownCallback);
    emscripten_set_mouseup_callback(canvasSelector, nullptr, false, emMouseUpCallback);
    emscripten_set_wheel_callback(canvasSelector, nullptr, false, emWheelCallback);
    
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

} // namespace voxy
