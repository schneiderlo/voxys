// ═══════════════════════════════════════════════════════════════════════════════
// window.cpp (WASM) - Dummy Window Management Implementation
// ═══════════════════════════════════════════════════════════════════════════════

#include "engine/platform/window.hpp"
#include "core/log.hpp"

#include <utility>

namespace voxy {

// ─────────────────────────────────────────────────────────────────────────────
// Global GLFW Management (Stubs)
// ─────────────────────────────────────────────────────────────────────────────

bool Window::initGLFW() {
    return true;  // WASM doesn't use GLFW
}

void Window::terminateGLFW() {
    // No-op
}

// ─────────────────────────────────────────────────────────────────────────────
// Constructor / Destructor
// ─────────────────────────────────────────────────────────────────────────────

Window::~Window() {
    shutdown();
}

Window::Window(Window&& other) noexcept
    : width_(other.width_)
    , height_(other.height_)
    , fbWidth_(other.fbWidth_)
    , fbHeight_(other.fbHeight_)
    , cursorCaptured_(other.cursorCaptured_)
    , onResize_(std::move(other.onResize_))
    , onClose_(std::move(other.onClose_))
    , onKey_(std::move(other.onKey_))
    , onMouseButton_(std::move(other.onMouseButton_))
    , onMouseMove_(std::move(other.onMouseMove_))
    , onScroll_(std::move(other.onScroll_))
{
    other.width_ = 0;
    other.height_ = 0;
    other.fbWidth_ = 0;
    other.fbHeight_ = 0;
}

Window& Window::operator=(Window&& other) noexcept {
    if (this != &other) {
        shutdown();

        width_ = other.width_;
        height_ = other.height_;
        fbWidth_ = other.fbWidth_;
        fbHeight_ = other.fbHeight_;
        cursorCaptured_ = other.cursorCaptured_;
        onResize_ = std::move(other.onResize_);
        onClose_ = std::move(other.onClose_);
        onKey_ = std::move(other.onKey_);
        onMouseButton_ = std::move(other.onMouseButton_);
        onMouseMove_ = std::move(other.onMouseMove_);
        onScroll_ = std::move(other.onScroll_);

        other.width_ = 0;
        other.height_ = 0;
        other.fbWidth_ = 0;
        other.fbHeight_ = 0;
    }
    return *this;
}

// ─────────────────────────────────────────────────────────────────────────────
// Initialization
// ─────────────────────────────────────────────────────────────────────────────

bool Window::init(const WindowConfig& config) {
    // WASM uses canvas, not GLFW
    width_ = config.width;
    height_ = config.height;
    fbWidth_ = config.width;
    fbHeight_ = config.height;
    return true;
}

void Window::shutdown() {
    width_ = 0;
    height_ = 0;
    fbWidth_ = 0;
    fbHeight_ = 0;
}

// ─────────────────────────────────────────────────────────────────────────────
// Window State
// ─────────────────────────────────────────────────────────────────────────────

bool Window::shouldClose() const {
    return false;  // WASM handles this differently (or not at all)
}

void Window::requestClose() {
    // No-op on WASM usually
}

bool Window::isValid() const {
    return width_ > 0 && height_ > 0;
}

// ─────────────────────────────────────────────────────────────────────────────
// Dimensions
// ─────────────────────────────────────────────────────────────────────────────

float Window::getContentScale() const {
    return 1.0f;
}

void Window::updateFramebufferSize() {
    // No-op
}

// ─────────────────────────────────────────────────────────────────────────────
// Event Processing
// ─────────────────────────────────────────────────────────────────────────────

void Window::pollEvents() {
    // No-op
}

void Window::waitEvents() {
    // No-op
}

// ─────────────────────────────────────────────────────────────────────────────
// Input State
// ─────────────────────────────────────────────────────────────────────────────

void Window::setCursorVisible(bool visible) {
    // Handled by browser/canvas, no-op here usually unless we bind JS
}

void Window::setCursorCaptured(bool captured) {
    cursorCaptured_ = captured;
}

void Window::getCursorPos(double& x, double& y) const {
    x = y = 0.0;
}

} // namespace voxy
