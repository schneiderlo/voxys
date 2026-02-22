// ═══════════════════════════════════════════════════════════════════════════════
// window.cpp (Native) - GLFW Window Management Implementation
// ═══════════════════════════════════════════════════════════════════════════════

#include "engine/platform/window.hpp"
#include "core/log.hpp"

#include <atomic>
#include <utility>

namespace voxy {

// ─────────────────────────────────────────────────────────────────────────────
// Static State
// ─────────────────────────────────────────────────────────────────────────────

static std::atomic<int> s_glfwRefCount{0};
static bool s_glfwInitialized = false;

// ─────────────────────────────────────────────────────────────────────────────
// Global GLFW Management
// ─────────────────────────────────────────────────────────────────────────────

bool Window::initGLFW() {
    if (s_glfwInitialized) {
        return true;
    }
    
    glfwSetErrorCallback(glfwErrorCallback);
    // [FIX] Force X11 platform on Linux for wgpu-native compatibility
    // wgpu-native (via Context::createSurface) currently expects an X11 Display pointer
    #if defined(__linux__)
        #ifndef GLFW_PLATFORM
        #define GLFW_PLATFORM 0x00050003
        #endif
        #ifndef GLFW_PLATFORM_X11
        #define GLFW_PLATFORM_X11 0x00060002
        #endif
        glfwInitHint(GLFW_PLATFORM, GLFW_PLATFORM_X11);
    #endif
    
    if (!glfwInit()) {
        LOG_ERROR("Failed to initialize GLFW");
        return false;
    }
    
    s_glfwInitialized = true;
    LOG_DEBUG("GLFW initialized successfully");
    
    // Log GLFW version
    int major, minor, revision;
    glfwGetVersion(&major, &minor, &revision);
    LOG_DEBUG("GLFW version: {}.{}.{}", major, minor, revision);
    
    return true;
}

void Window::terminateGLFW() {
    if (s_glfwInitialized && s_glfwRefCount == 0) {
        glfwTerminate();
        s_glfwInitialized = false;
        LOG_DEBUG("GLFW terminated");
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Constructor / Destructor
// ─────────────────────────────────────────────────────────────────────────────

Window::~Window() {
    shutdown();
}

Window::Window(Window&& other) noexcept
    : window_(other.window_)
    , width_(other.width_)
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
    other.window_ = nullptr;
    if (window_) {
        glfwSetWindowUserPointer(window_, this);
    }
    other.width_ = 0;
    other.height_ = 0;
    other.fbWidth_ = 0;
    other.fbHeight_ = 0;
}

Window& Window::operator=(Window&& other) noexcept {
    if (this != &other) {
        shutdown();
        
        window_ = other.window_;
        other.window_ = nullptr;
        if (window_) {
            glfwSetWindowUserPointer(window_, this);
        }
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
    if (window_) {
        LOG_WARN("Window already initialized");
        return true;
    }
    
    if (!initGLFW()) {
        return false;
    }
    
    // Configure window hints for WebGPU (no OpenGL context needed)
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, config.resizable ? GLFW_TRUE : GLFW_FALSE);
    
    // Create window
    GLFWmonitor* monitor = nullptr;
    int windowWidth = config.width;
    int windowHeight = config.height;
    
    if (config.fullscreen) {
        monitor = glfwGetPrimaryMonitor();
        const GLFWvidmode* mode = glfwGetVideoMode(monitor);
        windowWidth = mode->width;
        windowHeight = mode->height;
    }
    
    window_ = glfwCreateWindow(windowWidth, windowHeight, config.title, monitor, nullptr);
    if (!window_) {
        LOG_ERROR("Failed to create GLFW window");
        return false;
    }
    
    // Store this pointer for callbacks
    glfwSetWindowUserPointer(window_, this);
    
    // Set up callbacks
    glfwSetFramebufferSizeCallback(window_, glfwFramebufferSizeCallback);
    glfwSetWindowCloseCallback(window_, glfwWindowCloseCallback);
    glfwSetKeyCallback(window_, glfwKeyCallback);
    glfwSetMouseButtonCallback(window_, glfwMouseButtonCallback);
    glfwSetCursorPosCallback(window_, glfwCursorPosCallback);
    glfwSetScrollCallback(window_, glfwScrollCallback);
    
    // Get initial dimensions
    glfwGetWindowSize(window_, &width_, &height_);
    updateFramebufferSize();
    
    ++s_glfwRefCount;
    
    LOG_INFO("Window created: {}x{} (framebuffer: {}x{})", 
             width_, height_, fbWidth_, fbHeight_);
    
    return true;
}

void Window::shutdown() {
    if (window_) {
        glfwDestroyWindow(window_);
        window_ = nullptr;
        --s_glfwRefCount;
        LOG_DEBUG("Window destroyed");
    }
    width_ = 0;
    height_ = 0;
    fbWidth_ = 0;
    fbHeight_ = 0;
}

// ─────────────────────────────────────────────────────────────────────────────
// Window State
// ─────────────────────────────────────────────────────────────────────────────

bool Window::shouldClose() const {
    return window_ ? glfwWindowShouldClose(window_) : true;
}

void Window::requestClose() {
    if (window_) {
        glfwSetWindowShouldClose(window_, GLFW_TRUE);
    }
}

bool Window::isValid() const {
    return window_ != nullptr;
}

// ─────────────────────────────────────────────────────────────────────────────
// Dimensions
// ─────────────────────────────────────────────────────────────────────────────

float Window::getContentScale() const {
    if (!window_) return 1.0f;
    float xscale, yscale;
    glfwGetWindowContentScale(window_, &xscale, &yscale);
    return (xscale + yscale) * 0.5f;  // Average of X and Y scale
}

void Window::updateFramebufferSize() {
    if (window_) {
        glfwGetFramebufferSize(window_, &fbWidth_, &fbHeight_);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Event Processing
// ─────────────────────────────────────────────────────────────────────────────

void Window::pollEvents() {
    glfwPollEvents();
}

void Window::waitEvents() {
    glfwWaitEvents();
}

// ─────────────────────────────────────────────────────────────────────────────
// Native Handles
// ─────────────────────────────────────────────────────────────────────────────

#if defined(__APPLE__)
void* Window::getCocoaWindow() const {
    return window_ ? glfwGetCocoaWindow(window_) : nullptr;
}
#elif defined(_WIN32)
void* Window::getWin32Window() const {
    return window_ ? glfwGetWin32Window(window_) : nullptr;
}
void* Window::getWin32Instance() const {
    return GetModuleHandle(nullptr);
}
#elif defined(__linux__)
void* Window::getX11Display() const {
    return window_ ? glfwGetX11Display() : nullptr;
}
unsigned long Window::getX11Window() const {
    return window_ ? glfwGetX11Window(window_) : 0;
}
#endif

// ─────────────────────────────────────────────────────────────────────────────
// Input State
// ─────────────────────────────────────────────────────────────────────────────

void Window::setCursorVisible(bool visible) {
    if (window_) {
        glfwSetInputMode(window_, GLFW_CURSOR, 
                         visible ? GLFW_CURSOR_NORMAL : GLFW_CURSOR_HIDDEN);
    }
}

void Window::setCursorCaptured(bool captured) {
    if (window_) {
        glfwSetInputMode(window_, GLFW_CURSOR,
                         captured ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
        cursorCaptured_ = captured;
    }
}

void Window::getCursorPos(double& x, double& y) const {
    if (window_) {
        glfwGetCursorPos(window_, &x, &y);
    } else {
        x = y = 0.0;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// GLFW Callbacks
// ─────────────────────────────────────────────────────────────────────────────

void Window::glfwFramebufferSizeCallback(GLFWwindow* window, int width, int height) {
    auto* self = static_cast<Window*>(glfwGetWindowUserPointer(window));
    if (self) {
        self->fbWidth_ = width;
        self->fbHeight_ = height;
        glfwGetWindowSize(window, &self->width_, &self->height_);
        
        if (self->onResize_) {
            self->onResize_(width, height);
        }
    }
}

void Window::glfwWindowCloseCallback(GLFWwindow* window) {
    auto* self = static_cast<Window*>(glfwGetWindowUserPointer(window));
    if (self && self->onClose_) {
        self->onClose_();
    }
}

void Window::glfwKeyCallback(GLFWwindow* window, int key, int scancode, int action, int mods) {
    auto* self = static_cast<Window*>(glfwGetWindowUserPointer(window));
    if (self && self->onKey_) {
        self->onKey_(key, scancode, action, mods);
    }
}

void Window::glfwMouseButtonCallback(GLFWwindow* window, int button, int action, int mods) {
    auto* self = static_cast<Window*>(glfwGetWindowUserPointer(window));
    if (self && self->onMouseButton_) {
        self->onMouseButton_(button, action, mods);
    }
}

void Window::glfwCursorPosCallback(GLFWwindow* window, double x, double y) {
    auto* self = static_cast<Window*>(glfwGetWindowUserPointer(window));
    if (self && self->onMouseMove_) {
        self->onMouseMove_(x, y);
    }
}

void Window::glfwScrollCallback(GLFWwindow* window, double xoffset, double yoffset) {
    auto* self = static_cast<Window*>(glfwGetWindowUserPointer(window));
    if (self && self->onScroll_) {
        self->onScroll_(xoffset, yoffset);
    }
}

void Window::glfwErrorCallback(int error, const char* description) {
    LOG_ERROR("GLFW error {}: {}", error, description);
}

} // namespace voxy
