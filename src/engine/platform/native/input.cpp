// ═══════════════════════════════════════════════════════════════════════════════
// input.cpp (Native) - GLFW Input System Implementation
// ═══════════════════════════════════════════════════════════════════════════════

#include "engine/platform/input.hpp"
#include "engine/platform/window.hpp"
#include "core/log.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace voxy {

namespace {

constexpr size_t kMaximumQueuedInputEvents = 4'096u;
constexpr float kMaximumMouseCoordinate = 1.0e9f;
constexpr double kMaximumAccumulatedScroll = 10'000.0;

} // namespace

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

Input::~Input() = default;

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
            if (event.down) {
                // Only flag a fresh press if the key was not already held. OS key
                // auto-repeat (GLFW_REPEAT) fires "down" every frame while held; if
                // we set the accumulator each time, wasKeyPressed() stays true for
                // the whole hold and toggles (F1/F3/...) flicker.
                if (!currentKeys_[static_cast<size_t>(event.key)]) {
                    keysPressedThisFrame_[static_cast<size_t>(event.key)] = true;
                }
                currentKeys_[static_cast<size_t>(event.key)] = true;
            } else {
                if (currentKeys_[static_cast<size_t>(event.key)]) {
                    keysReleasedThisFrame_[static_cast<size_t>(event.key)] = true;
                }
                currentKeys_[static_cast<size_t>(event.key)] = false;
            }
        }
    }
    keyQueue_.clear();

    // Process buffered mouse button events
    for (const auto& event : mouseButtonQueue_) {
        if (isValidButton(event.button)) {
            if (event.down) {
                const size_t button = static_cast<size_t>(event.button);
                if (!currentButtons_[button]) {
                    buttonsPressedThisFrame_[button] = true;
                }
                currentButtons_[button] = true;
            } else {
                const size_t button = static_cast<size_t>(event.button);
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
    // GLFW is polled after beginFrame(). Drain events produced by that poll now
    // so buttons and keys are visible in the frame in which they occurred.
    processEvents();

    // Scroll callbacks are also delivered by the GLFW poll.
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
    const size_t index = static_cast<size_t>(code);
    return (!currentKeys_[index] && previousKeys_[index])
        || keysReleasedThisFrame_[index];
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
    const size_t index = static_cast<size_t>(code);
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
    mousePos_ = glm::vec2(x, y);
}

void Input::onMouseDown(int button) {
    if (isValidButton(button)) {
        if (mouseButtonQueue_.size() >= kMaximumQueuedInputEvents) {
            mouseButtonQueue_.clear();
            for (size_t index = 0; index < currentButtons_.size(); ++index) {
                if (currentButtons_[index]) {
                    mouseButtonQueue_.push_back(
                        {static_cast<int>(index), false});
                }
            }
        }
        mouseButtonQueue_.push_back({button, true});
    }
}

void Input::onMouseUp(int button) {
    if (isValidButton(button)) {
        if (mouseButtonQueue_.size() >= kMaximumQueuedInputEvents) {
            mouseButtonQueue_.clear();
            for (size_t index = 0; index < currentButtons_.size(); ++index) {
                if (currentButtons_[index]) {
                    mouseButtonQueue_.push_back(
                        {static_cast<int>(index), false});
                }
            }
        }
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
