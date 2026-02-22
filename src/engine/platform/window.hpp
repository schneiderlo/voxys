// ═══════════════════════════════════════════════════════════════════════════════
// window.hpp - Cross-platform Window Management
// ═══════════════════════════════════════════════════════════════════════════════
// GLFW-based window wrapper for native builds. Provides window creation,
// event handling, and native handle extraction for WebGPU surface creation.
// ═══════════════════════════════════════════════════════════════════════════════

#pragma once

#include <cstdint>
#include <functional>
#include <string>

// Platform detection and GLFW configuration
#if defined(VOXY_NATIVE)
    #define GLFW_INCLUDE_NONE
    #include <GLFW/glfw3.h>
    
    #if defined(__APPLE__)
        #define GLFW_EXPOSE_NATIVE_COCOA
    #elif defined(_WIN32)
        #define GLFW_EXPOSE_NATIVE_WIN32
    #elif defined(__linux__)
        // Check for Wayland vs X11 at runtime, but expose both
        #define GLFW_EXPOSE_NATIVE_X11
        // Note: Wayland support requires additional setup
    #endif
    #include <GLFW/glfw3native.h>
#endif

namespace voxy {

// ─────────────────────────────────────────────────────────────────────────────
// Window Configuration
// ─────────────────────────────────────────────────────────────────────────────

struct WindowConfig {
    int width = 1280;
    int height = 720;
    const char* title = "voxy";
    bool resizable = true;
    bool fullscreen = false;
    bool vsync = true;
};

// ─────────────────────────────────────────────────────────────────────────────
// Window Class
// ─────────────────────────────────────────────────────────────────────────────

class Window {
public:
    // Callbacks
    using ResizeCallback = std::function<void(int width, int height)>;
    using CloseCallback = std::function<void()>;
    using KeyCallback = std::function<void(int key, int scancode, int action, int mods)>;
    using MouseButtonCallback = std::function<void(int button, int action, int mods)>;
    using MouseMoveCallback = std::function<void(double x, double y)>;
    using ScrollCallback = std::function<void(double xoffset, double yoffset)>;
    
    // Construction / Destruction
    Window() = default;
    ~Window();
    
    // Non-copyable
    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;
    
    // Movable
    Window(Window&& other) noexcept;
    Window& operator=(Window&& other) noexcept;
    
    // ─────────────────────────────────────────────────────────────────────────
    // Initialization
    // ─────────────────────────────────────────────────────────────────────────
    
    // Initialize window with configuration
    [[nodiscard]] bool init(const WindowConfig& config = {});
    
    // Shutdown and cleanup
    void shutdown();
    
    // ─────────────────────────────────────────────────────────────────────────
    // Window State
    // ─────────────────────────────────────────────────────────────────────────
    
    // Check if window should close
    [[nodiscard]] bool shouldClose() const;
    
    // Request window close
    void requestClose();
    
    // Check if window is valid/initialized
    [[nodiscard]] bool isValid() const;
    
    // ─────────────────────────────────────────────────────────────────────────
    // Dimensions
    // ─────────────────────────────────────────────────────────────────────────
    
    // Get window dimensions in screen coordinates
    [[nodiscard]] int getWidth() const { return width_; }
    [[nodiscard]] int getHeight() const { return height_; }
    
    // Get framebuffer dimensions (may differ on high-DPI displays)
    [[nodiscard]] int getFramebufferWidth() const { return fbWidth_; }
    [[nodiscard]] int getFramebufferHeight() const { return fbHeight_; }
    
    // Get content scale (DPI scaling factor)
    [[nodiscard]] float getContentScale() const;
    
    // ─────────────────────────────────────────────────────────────────────────
    // Event Processing
    // ─────────────────────────────────────────────────────────────────────────
    
    // Poll for events (non-blocking)
    void pollEvents();
    
    // Wait for events (blocking)
    void waitEvents();
    
    // ─────────────────────────────────────────────────────────────────────────
    // Native Handles (for WebGPU surface creation)
    // ─────────────────────────────────────────────────────────────────────────
    
#if defined(VOXY_NATIVE)
    // Get GLFW window handle
    [[nodiscard]] GLFWwindow* getGLFWHandle() const { return window_; }
    
    #if defined(__APPLE__)
    // Get Cocoa window handle (NSWindow*)
    [[nodiscard]] void* getCocoaWindow() const;
    #elif defined(_WIN32)
    // Get Win32 window handle (HWND)
    [[nodiscard]] void* getWin32Window() const;
    // Get Win32 instance handle (HINSTANCE)
    [[nodiscard]] void* getWin32Instance() const;
    #elif defined(__linux__)
    // Get X11 display
    [[nodiscard]] void* getX11Display() const;
    // Get X11 window
    [[nodiscard]] unsigned long getX11Window() const;
    #endif
#endif
    
    // ─────────────────────────────────────────────────────────────────────────
    // Callbacks
    // ─────────────────────────────────────────────────────────────────────────
    
    void setResizeCallback(ResizeCallback callback) { onResize_ = std::move(callback); }
    void setCloseCallback(CloseCallback callback) { onClose_ = std::move(callback); }
    void setKeyCallback(KeyCallback callback) { onKey_ = std::move(callback); }
    void setMouseButtonCallback(MouseButtonCallback callback) { onMouseButton_ = std::move(callback); }
    void setMouseMoveCallback(MouseMoveCallback callback) { onMouseMove_ = std::move(callback); }
    void setScrollCallback(ScrollCallback callback) { onScroll_ = std::move(callback); }
    
    // ─────────────────────────────────────────────────────────────────────────
    // Input State
    // ─────────────────────────────────────────────────────────────────────────
    
    // Mouse cursor modes
    void setCursorVisible(bool visible);
    void setCursorCaptured(bool captured);
    [[nodiscard]] bool isCursorCaptured() const { return cursorCaptured_; }
    
    // Get current cursor position
    void getCursorPos(double& x, double& y) const;
    
    // ─────────────────────────────────────────────────────────────────────────
    // Global GLFW Management
    // ─────────────────────────────────────────────────────────────────────────
    
    // Initialize GLFW (called automatically, but can be called explicitly)
    static bool initGLFW();
    
    // Terminate GLFW (call at application shutdown)
    static void terminateGLFW();
    
private:
#if defined(VOXY_NATIVE)
    GLFWwindow* window_ = nullptr;
#endif
    
    int width_ = 0;
    int height_ = 0;
    int fbWidth_ = 0;
    int fbHeight_ = 0;
    bool cursorCaptured_ = false;
    
    // Callbacks
    ResizeCallback onResize_;
    CloseCallback onClose_;
    KeyCallback onKey_;
    MouseButtonCallback onMouseButton_;
    MouseMoveCallback onMouseMove_;
    ScrollCallback onScroll_;
    
#if defined(VOXY_NATIVE)
    // Internal callback dispatching (GLFW)
    static void glfwFramebufferSizeCallback(GLFWwindow* window, int width, int height);
    static void glfwWindowCloseCallback(GLFWwindow* window);
    static void glfwKeyCallback(GLFWwindow* window, int key, int scancode, int action, int mods);
    static void glfwMouseButtonCallback(GLFWwindow* window, int button, int action, int mods);
    static void glfwCursorPosCallback(GLFWwindow* window, double x, double y);
    static void glfwScrollCallback(GLFWwindow* window, double xoffset, double yoffset);
    static void glfwErrorCallback(int error, const char* description);
#endif
    
    void updateFramebufferSize();
};

} // namespace voxy

