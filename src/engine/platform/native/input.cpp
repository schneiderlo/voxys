// ═══════════════════════════════════════════════════════════════════════════════
// input.cpp (Native) - GLFW Input System Implementation
// ═══════════════════════════════════════════════════════════════════════════════

#include "engine/platform/input.hpp"
#include "engine/platform/window.hpp"
#include "core/log.hpp"

#include <cstring>

namespace voxy {

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
                currentKeys_[static_cast<size_t>(event.key)] = true;
                keysPressedThisFrame_[static_cast<size_t>(event.key)] = true;
            } else {
                currentKeys_[static_cast<size_t>(event.key)] = false;
            }
        }
    }
    keyQueue_.clear();

    // Process buffered mouse button events
    for (const auto& event : mouseButtonQueue_) {
        if (isValidButton(event.button)) {
            if (event.down) {
                currentButtons_[static_cast<size_t>(event.button)] = true;
                buttonsPressedThisFrame_[static_cast<size_t>(event.button)] = true;
            } else {
                currentButtons_[static_cast<size_t>(event.button)] = false;
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
    return currentKeys_[static_cast<size_t>(code)];
}

bool Input::wasKeyPressed(Key key) const {
    int code = static_cast<int>(key);
    if (!isValidKey(code)) return false;
    // Check both: standard press detection AND the per-frame accumulator
    // The accumulator catches quick press+release within a single frame
    return (currentKeys_[static_cast<size_t>(code)] && !previousKeys_[static_cast<size_t>(code)]) || keysPressedThisFrame_[static_cast<size_t>(code)];
}

bool Input::wasKeyReleased(Key key) const {
    int code = static_cast<int>(key);
    if (!isValidKey(code)) return false;
    return !currentKeys_[static_cast<size_t>(code)] && previousKeys_[static_cast<size_t>(code)];
}

// ─────────────────────────────────────────────────────────────────────────────
// Mouse State
// ─────────────────────────────────────────────────────────────────────────────

bool Input::isMouseButtonDown(MouseButton button) const {
    int idx = static_cast<int>(button);
    if (!isValidButton(idx)) return false;
    return currentButtons_[static_cast<size_t>(idx)];
}

bool Input::wasMouseButtonPressed(MouseButton button) const {
    int code = static_cast<int>(button);
    if (!isValidButton(code)) return false;
    // Check both: standard press detection AND the per-frame accumulator
    // The accumulator catches quick press+release within a single frame
    bool standardResult = currentButtons_[static_cast<size_t>(code)] && !previousButtons_[static_cast<size_t>(code)];
    return standardResult || buttonsPressedThisFrame_[static_cast<size_t>(code)];
}

bool Input::wasMouseButtonReleased(MouseButton button) const {
    int code = static_cast<int>(button);
    if (!isValidButton(code)) return false;
    return !currentButtons_[static_cast<size_t>(code)] && previousButtons_[static_cast<size_t>(code)];
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

    LOG_DEBUG("Mouse captured");
}

void Input::releaseMouse() {
    if (!captured_) return;

    captured_ = false;

    if (window_) {
        window_->setCursorCaptured(false);
    }

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

// ─────────────────────────────────────────────────────────────────────────────
// Native Platform Integration (GLFW)
// ─────────────────────────────────────────────────────────────────────────────

void Input::attachToWindow(Window& window) {
    window_ = &window;

    // Set up key callback
    window.setKeyCallback([this](int key, int /*scancode*/, int action, int /*mods*/) {
        if (action == GLFW_PRESS || action == GLFW_REPEAT) {
            onKeyDown(key);
        } else if (action == GLFW_RELEASE) {
            onKeyUp(key);
        }
    });

    // Set up mouse move callback
    window.setMouseMoveCallback([this](double x, double y) {
        onMouseMove(static_cast<float>(x), static_cast<float>(y));
    });

    // Set up mouse button callback
    window.setMouseButtonCallback([this](int button, int action, int /*mods*/) {
        if (action == GLFW_PRESS) {
            onMouseDown(button);
        } else if (action == GLFW_RELEASE) {
            onMouseUp(button);
        }
    });

    // Set up scroll callback
    window.setScrollCallback([this](double /*xoffset*/, double yoffset) {
        onScroll(static_cast<float>(yoffset));
    });

    LOG_DEBUG("Input attached to window");
}
void Input::setupEmscriptenCallbacks(const char* /*canvasSelector*/) {
    // No-op for native
}

} // namespace voxy
