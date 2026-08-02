// ═══════════════════════════════════════════════════════════════════════════════
// window.cpp (Native) - GLFW Window Management Implementation
// ═══════════════════════════════════════════════════════════════════════════════

#include "engine/platform/window.hpp"
#include "core/log.hpp"

#include <atomic>
#include <cstdlib>
#include <string_view>
#include <utility>

#if defined(__linux__) && !defined(GLFW_EXPOSE_NATIVE_WAYLAND)
struct wl_display;
struct wl_surface;
extern "C" {
GLFWAPI wl_display* glfwGetWaylandDisplay(void);
GLFWAPI wl_surface* glfwGetWaylandWindow(GLFWwindow* window);
}
#endif

namespace voxy {

// ─────────────────────────────────────────────────────────────────────────────
// Static State
// ─────────────────────────────────────────────────────────────────────────────

static std::atomic<int> s_glfwRefCount{0};
static bool s_glfwInitialized = false;

const char* nativeWindowPlatformName(
    NativeWindowPlatform platform) noexcept {
    switch (platform) {
        case NativeWindowPlatform::Cocoa: return "Cocoa";
        case NativeWindowPlatform::Win32: return "Win32";
        case NativeWindowPlatform::X11: return "X11";
        case NativeWindowPlatform::Wayland: return "Wayland";
        case NativeWindowPlatform::Unknown: return "unknown";
    }
    return "unknown";
}

// ─────────────────────────────────────────────────────────────────────────────
// Global GLFW Management
// ─────────────────────────────────────────────────────────────────────────────

bool Window::initGLFW() {
    if (s_glfwInitialized) {
        return true;
    }
    
    glfwSetErrorCallback(glfwErrorCallback);
    bool requestedWayland = false;
    bool requestedX11 = false;
    #if defined(__linux__)
        // Allow capture farms and packaged builds to select a surface backend
        // supported by their wgpu-native build. Without an override, prefer
        // the session-native backend for correct fractional scaling.
        const char* backendOverride = std::getenv("VOXY_WINDOW_BACKEND");
        const char* waylandDisplay = std::getenv("WAYLAND_DISPLAY");
        const std::string_view requested = backendOverride != nullptr
            ? std::string_view{backendOverride} : std::string_view{};
        requestedX11 = requested == "x11" || requested == "X11";
        requestedWayland = requested == "wayland"
            || requested == "Wayland"
            || (!requestedX11 && requested.empty()
                && waylandDisplay != nullptr
                && std::string_view{waylandDisplay}.size() != 0u);
        if (requestedWayland) {
            glfwInitHint(GLFW_PLATFORM, GLFW_PLATFORM_WAYLAND);
        } else if (requestedX11) {
            glfwInitHint(GLFW_PLATFORM, GLFW_PLATFORM_X11);
        }
    #endif
    
    if (!glfwInit()) {
        #if defined(__linux__)
        if (requestedWayland) {
            LOG_WARN("Native Wayland initialization failed; falling back to X11");
            glfwInitHint(GLFW_PLATFORM, GLFW_PLATFORM_X11);
            if (!glfwInit()) {
                LOG_ERROR("Failed to initialize GLFW on Wayland or X11");
                return false;
            }
        } else if (requestedX11) {
            LOG_WARN("Native X11 initialization failed; falling back to Wayland");
            glfwInitHint(GLFW_PLATFORM, GLFW_PLATFORM_WAYLAND);
            if (!glfwInit()) {
                LOG_ERROR("Failed to initialize GLFW on X11 or Wayland");
                return false;
            }
        } else
        #endif
        {
            LOG_ERROR("Failed to initialize GLFW");
            return false;
        }
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
    , nativePlatform_(other.nativePlatform_)
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
    other.cursorCaptured_ = false;
    other.nativePlatform_ = NativeWindowPlatform::Unknown;
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
        nativePlatform_ = other.nativePlatform_;
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
        other.cursorCaptured_ = false;
        other.nativePlatform_ = NativeWindowPlatform::Unknown;
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
    if (config.width <= 0 || config.height <= 0 || !config.title) {
        LOG_ERROR("Invalid window configuration: {}x{}, title={}",
                  config.width, config.height,
                  config.title ? config.title : "<null>");
        return false;
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
        if (!monitor) {
            LOG_ERROR("Cannot create fullscreen window: no primary monitor");
            return false;
        }
        const GLFWvidmode* mode = glfwGetVideoMode(monitor);
        if (!mode || mode->width <= 0 || mode->height <= 0) {
            LOG_ERROR("Cannot create fullscreen window: no valid video mode");
            return false;
        }
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
    cursorCaptured_ = false;

    #if defined(__APPLE__)
        nativePlatform_ = NativeWindowPlatform::Cocoa;
    #elif defined(_WIN32)
        nativePlatform_ = NativeWindowPlatform::Win32;
    #elif defined(__linux__)
        switch (glfwGetPlatform()) {
            case GLFW_PLATFORM_WAYLAND:
                nativePlatform_ = NativeWindowPlatform::Wayland;
                break;
            case GLFW_PLATFORM_X11:
                nativePlatform_ = NativeWindowPlatform::X11;
                break;
            default:
                nativePlatform_ = NativeWindowPlatform::Unknown;
                break;
        }
    #endif
    
    ++s_glfwRefCount;
    
    float xscale = 1.0f;
    float yscale = 1.0f;
    glfwGetWindowContentScale(window_, &xscale, &yscale);
    LOG_INFO("Window created via {}: {}x{} logical, {}x{} framebuffer "
             "({:.2f}x{:.2f} scale)",
             nativeWindowPlatformName(nativePlatform_), width_, height_,
             fbWidth_, fbHeight_, xscale, yscale);
    
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
    cursorCaptured_ = false;
    nativePlatform_ = NativeWindowPlatform::Unknown;
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
    if (s_glfwInitialized) {
        glfwPollEvents();
    }
}

void Window::waitEvents() {
    if (s_glfwInitialized) {
        glfwWaitEvents();
    }
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
void* Window::getWaylandDisplay() const {
    return window_ && nativePlatform_ == NativeWindowPlatform::Wayland
        ? glfwGetWaylandDisplay() : nullptr;
}
void* Window::getWaylandSurface() const {
    return window_ && nativePlatform_ == NativeWindowPlatform::Wayland
        ? glfwGetWaylandWindow(window_) : nullptr;
}
void* Window::getX11Display() const {
    return window_ && nativePlatform_ == NativeWindowPlatform::X11
        ? glfwGetX11Display() : nullptr;
}
unsigned long Window::getX11Window() const {
    return window_ && nativePlatform_ == NativeWindowPlatform::X11
        ? glfwGetX11Window(window_) : 0;
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
