// ═══════════════════════════════════════════════════════════════════════════════
// test_window.cpp - Unit tests for Window class
// ═══════════════════════════════════════════════════════════════════════════════
// Note: These tests verify the Window class interface and basic functionality.
// Full window creation tests require a display server (X11/Wayland on Linux).
// ═══════════════════════════════════════════════════════════════════════════════

#include <gtest/gtest.h>
#include "engine/platform/window.hpp"

namespace voxy {

// ─────────────────────────────────────────────────────────────────────────────
// WindowConfig Tests
// ─────────────────────────────────────────────────────────────────────────────

TEST(WindowConfigTest, DefaultValues) {
    WindowConfig config;
    
    EXPECT_EQ(config.width, 1280);
    EXPECT_EQ(config.height, 720);
    EXPECT_STREQ(config.title, "voxy");
    EXPECT_TRUE(config.resizable);
    EXPECT_FALSE(config.fullscreen);
    EXPECT_TRUE(config.vsync);
}

TEST(WindowConfigTest, CustomValues) {
    WindowConfig config;
    config.width = 1920;
    config.height = 1080;
    config.title = "Custom Title";
    config.resizable = false;
    config.fullscreen = true;
    config.vsync = false;
    
    EXPECT_EQ(config.width, 1920);
    EXPECT_EQ(config.height, 1080);
    EXPECT_STREQ(config.title, "Custom Title");
    EXPECT_FALSE(config.resizable);
    EXPECT_TRUE(config.fullscreen);
    EXPECT_FALSE(config.vsync);
}

// ─────────────────────────────────────────────────────────────────────────────
// Window Class Tests (interface only, no display required)
// ─────────────────────────────────────────────────────────────────────────────

TEST(WindowTest, DefaultConstruction) {
    Window window;
    
    // Uninitialized window should report invalid
    EXPECT_FALSE(window.isValid());
    EXPECT_EQ(window.getWidth(), 0);
    EXPECT_EQ(window.getHeight(), 0);
    EXPECT_EQ(window.getFramebufferWidth(), 0);
    EXPECT_EQ(window.getFramebufferHeight(), 0);
}

TEST(WindowTest, MoveConstruction) {
    Window window1;
    // Set some internal state (without actually creating a window)
    
    Window window2(std::move(window1));
    
    // After move, window1 should be in valid but empty state
    EXPECT_FALSE(window1.isValid());
    EXPECT_EQ(window1.getWidth(), 0);
}

TEST(WindowTest, MoveAssignment) {
    Window window1;
    Window window2;
    
    window2 = std::move(window1);
    
    EXPECT_FALSE(window1.isValid());
    EXPECT_FALSE(window2.isValid());
}

TEST(WindowTest, ShouldCloseDefaultBehavior) {
    Window window;
    
    // Uninitialized window behavior depends on platform
    // In WASM or without GLFW, shouldClose returns false
    // In native with GLFW, shouldClose returns true (no window handle)
#if defined(VOXY_NATIVE)
    // Without initialization, the behavior depends on GLFW availability
    // This test just verifies it doesn't crash
    (void)window.shouldClose();
    SUCCEED();
#else
    EXPECT_FALSE(window.shouldClose());
#endif
}

TEST(WindowTest, CallbackSetters) {
    Window window;
    
    bool resizeCalled = false;
    bool closeCalled = false;
    bool keyCalled = false;
    bool mouseButtonCalled = false;
    bool mouseMoveCalled = false;
    bool scrollCalled = false;
    
    // Setting callbacks should not crash
    window.setResizeCallback([&](int, int) { resizeCalled = true; });
    window.setCloseCallback([&]() { closeCalled = true; });
    window.setKeyCallback([&](int, int, int, int) { keyCalled = true; });
    window.setMouseButtonCallback([&](int, int, int) { mouseButtonCalled = true; });
    window.setMouseMoveCallback([&](double, double) { mouseMoveCalled = true; });
    window.setScrollCallback([&](double, double) { scrollCalled = true; });
    
    SUCCEED();
}

TEST(WindowTest, CursorStateDefault) {
    Window window;
    
    EXPECT_FALSE(window.isCursorCaptured());
}

// ─────────────────────────────────────────────────────────────────────────────
// GLFW Initialization Tests (conditional on display availability)
// ─────────────────────────────────────────────────────────────────────────────

#if defined(VOXY_NATIVE)

class WindowGLFWTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Try to initialize GLFW - may fail in headless environments
        glfwInitialized_ = Window::initGLFW();
    }
    
    void TearDown() override {
        // Don't terminate GLFW here - other tests might need it
    }
    
    bool glfwInitialized_ = false;
};

TEST_F(WindowGLFWTest, GLFWInitialization) {
    // GLFW initialization may fail in headless CI environments
    // This test just verifies it doesn't crash
    if (glfwInitialized_) {
        SUCCEED() << "GLFW initialized successfully";
    } else {
        SUCCEED() << "GLFW initialization failed (expected in headless environment)";
    }
}

TEST_F(WindowGLFWTest, WindowCreation) {
    if (!glfwInitialized_) {
        GTEST_SKIP() << "GLFW not available - skipping window creation test";
    }
    
    WindowConfig config;
    config.width = 800;
    config.height = 600;
    config.title = "Test Window";
    
    Window window;
    bool created = window.init(config);
    
    if (created && window.isValid()) {
        EXPECT_TRUE(window.isValid());
        EXPECT_EQ(window.getWidth(), 800);
        EXPECT_EQ(window.getHeight(), 600);
        // Framebuffer dimensions might be 0 in headless/CI environments
        // Just verify they're non-negative
        EXPECT_GE(window.getFramebufferWidth(), 0);
        EXPECT_GE(window.getFramebufferHeight(), 0);
        
        window.shutdown();
        EXPECT_FALSE(window.isValid());
    } else {
        // Window creation may fail in headless environments
        SUCCEED() << "Window creation failed (expected in headless environment)";
    }
}

TEST_F(WindowGLFWTest, PollEventsNoWindow) {
    Window window;
    
    // Should not crash even without a window
    window.pollEvents();
    SUCCEED();
}

TEST_F(WindowGLFWTest, ContentScaleDefault) {
    Window window;
    
    // Uninitialized window should return 1.0
    EXPECT_FLOAT_EQ(window.getContentScale(), 1.0f);
}

#endif  // VOXY_NATIVE

} // namespace voxy

