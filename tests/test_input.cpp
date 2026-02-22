// ═══════════════════════════════════════════════════════════════════════════════
// test_input.cpp - Unit tests for Input class
// ═══════════════════════════════════════════════════════════════════════════════

#include <gtest/gtest.h>
#include "engine/platform/input.hpp"

namespace voxy {

// ─────────────────────────────────────────────────────────────────────────────
// Key Enum Tests
// ─────────────────────────────────────────────────────────────────────────────

TEST(KeyEnumTest, LetterKeysHaveCorrectValues) {
    // GLFW key codes for letters are ASCII values
    EXPECT_EQ(static_cast<int>(Key::A), 65);
    EXPECT_EQ(static_cast<int>(Key::Z), 90);
    EXPECT_EQ(static_cast<int>(Key::W), 87);
    EXPECT_EQ(static_cast<int>(Key::S), 83);
    EXPECT_EQ(static_cast<int>(Key::D), 68);
}

TEST(KeyEnumTest, NumberKeysHaveCorrectValues) {
    EXPECT_EQ(static_cast<int>(Key::Num0), 48);
    EXPECT_EQ(static_cast<int>(Key::Num9), 57);
}

TEST(KeyEnumTest, SpecialKeysHaveCorrectValues) {
    EXPECT_EQ(static_cast<int>(Key::Space), 32);
    EXPECT_EQ(static_cast<int>(Key::Escape), 256);
    EXPECT_EQ(static_cast<int>(Key::Enter), 257);
}

TEST(KeyEnumTest, ModifierAliasesMatchPrimary) {
    EXPECT_EQ(static_cast<int>(Key::Shift), static_cast<int>(Key::LeftShift));
    EXPECT_EQ(static_cast<int>(Key::Control), static_cast<int>(Key::LeftControl));
    EXPECT_EQ(static_cast<int>(Key::Ctrl), static_cast<int>(Key::LeftControl));
    EXPECT_EQ(static_cast<int>(Key::Alt), static_cast<int>(Key::LeftAlt));
}

// ─────────────────────────────────────────────────────────────────────────────
// MouseButton Enum Tests
// ─────────────────────────────────────────────────────────────────────────────

TEST(MouseButtonEnumTest, ButtonsHaveCorrectValues) {
    EXPECT_EQ(static_cast<int>(MouseButton::Left), 0);
    EXPECT_EQ(static_cast<int>(MouseButton::Right), 1);
    EXPECT_EQ(static_cast<int>(MouseButton::Middle), 2);
}

// ─────────────────────────────────────────────────────────────────────────────
// Input Construction Tests
// ─────────────────────────────────────────────────────────────────────────────

TEST(InputTest, DefaultConstruction) {
    Input input;
    
    // All keys should be up by default
    EXPECT_FALSE(input.isKeyDown(Key::W));
    EXPECT_FALSE(input.isKeyDown(Key::Space));
    EXPECT_FALSE(input.isKeyDown(Key::Escape));
    
    // All buttons should be up by default
    EXPECT_FALSE(input.isMouseButtonDown(MouseButton::Left));
    EXPECT_FALSE(input.isMouseButtonDown(MouseButton::Right));
    EXPECT_FALSE(input.isMouseButtonDown(MouseButton::Middle));
    
    // Mouse should not be captured by default
    EXPECT_FALSE(input.isMouseCaptured());
    
    // Mouse position should be zero
    EXPECT_FLOAT_EQ(input.mousePosition().x, 0.0f);
    EXPECT_FLOAT_EQ(input.mousePosition().y, 0.0f);
    
    // Mouse delta should be zero
    EXPECT_FLOAT_EQ(input.mouseDelta().x, 0.0f);
    EXPECT_FLOAT_EQ(input.mouseDelta().y, 0.0f);
    
    // Scroll delta should be zero
    EXPECT_FLOAT_EQ(input.scrollDelta(), 0.0f);
}

TEST(InputTest, MoveConstruction) {
    Input input1;
    input1.onKeyDown(static_cast<int>(Key::W));
    input1.beginFrame();
    
    Input input2(std::move(input1));
    
    // Moved-to object should have the state
    EXPECT_TRUE(input2.isKeyDown(Key::W));
    
    // Moved-from object is in valid but unspecified state
    // (std::array moves are actually copies, so state may still be present)
    // We just verify it doesn't crash when accessed
    (void)input1.isKeyDown(Key::W);
    SUCCEED();
}

// ─────────────────────────────────────────────────────────────────────────────
// Keyboard State Tests
// ─────────────────────────────────────────────────────────────────────────────

TEST(InputTest, KeyDownTracking) {
    Input input;
    
    EXPECT_FALSE(input.isKeyDown(Key::W));
    
    input.onKeyDown(static_cast<int>(Key::W));
    input.beginFrame();
    EXPECT_TRUE(input.isKeyDown(Key::W));
    
    input.onKeyUp(static_cast<int>(Key::W));
    input.beginFrame();
    EXPECT_FALSE(input.isKeyDown(Key::W));
}

TEST(InputTest, KeyPressedDetection) {
    Input input;
    
    // Frame 1: Key not pressed
    input.beginFrame();
    EXPECT_FALSE(input.wasKeyPressed(Key::W));
    input.endFrame();
    
    // Frame 2: Key pressed
    input.onKeyDown(static_cast<int>(Key::W));
    input.beginFrame();
    EXPECT_TRUE(input.wasKeyPressed(Key::W));
    input.endFrame();
    
    // Frame 3: Key still held, not "just pressed"
    input.beginFrame();
    EXPECT_FALSE(input.wasKeyPressed(Key::W));
    EXPECT_TRUE(input.isKeyDown(Key::W));
    input.endFrame();
}

TEST(InputTest, KeyReleasedDetection) {
    Input input;
    
    // Frame 1: Press key
    input.onKeyDown(static_cast<int>(Key::W));
    input.beginFrame();
    EXPECT_FALSE(input.wasKeyReleased(Key::W));
    input.endFrame();
    
    // Frame 2: Release key
    input.onKeyUp(static_cast<int>(Key::W));
    input.beginFrame();
    EXPECT_TRUE(input.wasKeyReleased(Key::W));
    input.endFrame();
    
    // Frame 3: Key still released, not "just released"
    input.beginFrame();
    EXPECT_FALSE(input.wasKeyReleased(Key::W));
    input.endFrame();
}

TEST(InputTest, MultipleKeysTracking) {
    Input input;
    
    input.onKeyDown(static_cast<int>(Key::W));
    input.onKeyDown(static_cast<int>(Key::A));
    input.onKeyDown(static_cast<int>(Key::W));
    input.onKeyDown(static_cast<int>(Key::A));
    input.onKeyDown(static_cast<int>(Key::Shift));
    
    input.beginFrame();
    
    EXPECT_TRUE(input.isKeyDown(Key::W));
    EXPECT_TRUE(input.isKeyDown(Key::A));
    EXPECT_TRUE(input.isKeyDown(Key::Shift));
    EXPECT_FALSE(input.isKeyDown(Key::S));
    EXPECT_FALSE(input.isKeyDown(Key::D));
}

TEST(InputTest, InvalidKeyCodeHandling) {
    Input input;
    
    // Invalid key codes should not crash
    input.onKeyDown(-1);
    input.onKeyDown(1000);
    
    // Should return false for invalid keys
    // Note: Can't test directly as Key enum doesn't have invalid values
    SUCCEED();
}

// ─────────────────────────────────────────────────────────────────────────────
// Mouse Position Tests
// ─────────────────────────────────────────────────────────────────────────────

TEST(InputTest, MousePositionTracking) {
    Input input;
    
    input.onMouseMove(100.0f, 200.0f);
    
    EXPECT_FLOAT_EQ(input.mousePosition().x, 100.0f);
    EXPECT_FLOAT_EQ(input.mousePosition().y, 200.0f);
}

TEST(InputTest, MouseDeltaComputation) {
    Input input;
    
    // Frame 1: Initial position
    // Order: beginFrame (save prev) -> events -> computeDeltas
    input.beginFrame();
    input.onMouseMove(100.0f, 100.0f);
    input.computeDeltas();
    input.endFrame();
    
    // Frame 2: Moved position
    input.beginFrame();
    input.onMouseMove(150.0f, 120.0f);
    input.computeDeltas();
    input.endFrame();
    
    // Delta should be the difference
    EXPECT_FLOAT_EQ(input.mouseDelta().x, 50.0f);
    EXPECT_FLOAT_EQ(input.mouseDelta().y, 20.0f);
}

TEST(InputTest, MouseDeltaFirstMoveZero) {
    Input input;
    
    // First move should not produce large delta
    input.beginFrame();
    input.onMouseMove(500.0f, 500.0f);
    input.computeDeltas();
    input.endFrame();
    
    // Delta should be zero on first move
    EXPECT_FLOAT_EQ(input.mouseDelta().x, 0.0f);
    EXPECT_FLOAT_EQ(input.mouseDelta().y, 0.0f);
}

// ─────────────────────────────────────────────────────────────────────────────
// Mouse Button Tests
// ─────────────────────────────────────────────────────────────────────────────

TEST(InputTest, MouseButtonDownTracking) {
    Input input;
    
    EXPECT_FALSE(input.isMouseButtonDown(MouseButton::Left));
    
    input.onMouseDown(static_cast<int>(MouseButton::Left));
    input.beginFrame();
    EXPECT_TRUE(input.isMouseButtonDown(MouseButton::Left));
    
    input.onMouseUp(static_cast<int>(MouseButton::Left));
    input.beginFrame();
    EXPECT_FALSE(input.isMouseButtonDown(MouseButton::Left));
}

TEST(InputTest, MouseButtonPressedDetection) {
    Input input;
    
    // Frame 1: Button not pressed
    input.beginFrame();
    EXPECT_FALSE(input.wasMouseButtonPressed(MouseButton::Left));
    input.endFrame();
    
    // Frame 2: Button pressed
    input.onMouseDown(static_cast<int>(MouseButton::Left));
    input.beginFrame();
    EXPECT_TRUE(input.wasMouseButtonPressed(MouseButton::Left));
    input.endFrame();
    
    // Frame 3: Button still held, not "just pressed"
    input.beginFrame();
    EXPECT_FALSE(input.wasMouseButtonPressed(MouseButton::Left));
    input.endFrame();
}

TEST(InputTest, MouseButtonReleasedDetection) {
    Input input;
    
    // Frame 1: Press button
    input.onMouseDown(static_cast<int>(MouseButton::Left));
    input.beginFrame();
    input.endFrame();
    
    // Frame 2: Release button
    input.onMouseUp(static_cast<int>(MouseButton::Left));
    input.beginFrame();
    EXPECT_TRUE(input.wasMouseButtonReleased(MouseButton::Left));
    input.endFrame();
    
    // Frame 3: Button still released
    input.beginFrame();
    EXPECT_FALSE(input.wasMouseButtonReleased(MouseButton::Left));
    input.endFrame();
}

// ─────────────────────────────────────────────────────────────────────────────
// Scroll Tests
// ─────────────────────────────────────────────────────────────────────────────

TEST(InputTest, ScrollDeltaTracking) {
    Input input;
    
    // Frame 1: Scroll events
    input.beginFrame();
    input.onScroll(1.0f);
    input.onScroll(0.5f);
    input.endFrame();
    
    // Frame 2: Get accumulated scroll
    input.beginFrame();
    EXPECT_FLOAT_EQ(input.scrollDelta(), 1.5f);
    input.endFrame();
    
    // Frame 3: Scroll reset
    input.beginFrame();
    EXPECT_FLOAT_EQ(input.scrollDelta(), 0.0f);
    input.endFrame();
}

// ─────────────────────────────────────────────────────────────────────────────
// Mouse Capture Tests
// ─────────────────────────────────────────────────────────────────────────────

TEST(InputTest, MouseCaptureState) {
    Input input;
    
    EXPECT_FALSE(input.isMouseCaptured());
    
    input.captureMouse();
    EXPECT_TRUE(input.isMouseCaptured());
    
    input.releaseMouse();
    EXPECT_FALSE(input.isMouseCaptured());
}

TEST(InputTest, MouseCaptureToggle) {
    Input input;
    
    EXPECT_FALSE(input.isMouseCaptured());
    
    input.toggleMouseCapture();
    EXPECT_TRUE(input.isMouseCaptured());
    
    input.toggleMouseCapture();
    EXPECT_FALSE(input.isMouseCaptured());
}

TEST(InputTest, MouseCaptureDuplicateCalls) {
    Input input;
    
    // Multiple captures should be idempotent
    input.captureMouse();
    input.captureMouse();
    EXPECT_TRUE(input.isMouseCaptured());
    
    // Multiple releases should be idempotent
    input.releaseMouse();
    input.releaseMouse();
    EXPECT_FALSE(input.isMouseCaptured());
}

// ─────────────────────────────────────────────────────────────────────────────
// Window Association Tests
// ─────────────────────────────────────────────────────────────────────────────

TEST(InputTest, WindowAssociation) {
    Input input;
    
    EXPECT_EQ(input.getWindow(), nullptr);
    
    // Setting null window should work
    input.setWindow(nullptr);
    EXPECT_EQ(input.getWindow(), nullptr);
}

// ─────────────────────────────────────────────────────────────────────────────
// Edge Case Tests
// ─────────────────────────────────────────────────────────────────────────────

TEST(InputTest, RapidKeyPressRelease) {
    Input input;
    
    // Rapid press/release in same frame
    input.beginFrame();
    input.onKeyDown(static_cast<int>(Key::Space));
    input.onKeyUp(static_cast<int>(Key::Space));
    
    // Key should be up (last state wins)
    EXPECT_FALSE(input.isKeyDown(Key::Space));
    input.endFrame();
}

TEST(InputTest, FrameOrderingMatters) {
    Input input;
    
    // Press in frame 1
    input.onKeyDown(static_cast<int>(Key::W));
    input.beginFrame();
    input.endFrame();
    
    // Check state in frame 2 before any events
    input.beginFrame();
    EXPECT_TRUE(input.isKeyDown(Key::W));
    EXPECT_FALSE(input.wasKeyPressed(Key::W));  // Was pressed last frame
    input.endFrame();
}

} // namespace voxy

