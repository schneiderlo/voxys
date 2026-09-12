#include "engine/platform/gamepad.hpp"
#include "engine/platform/input.hpp"
#include <gtest/gtest.h>
#include <limits>
using namespace voxy;
TEST(GamepadControls, ConnectionHeldConfirmNeedsReleaseAndNextPress) {
    GamepadInput pad;GamepadSample s;s.device=1;s.connected=true;s.buttons[0]=true;
    pad.update(s,true,1);EXPECT_FALSE(pad.armed());EXPECT_FALSE(pad.pressed(PadButton::Confirm));
    s.buttons[0]=false;pad.update(s,true,2);ASSERT_TRUE(pad.armed());
    s.buttons[0]=true;pad.update(s,true,3);EXPECT_TRUE(pad.pressed(PadButton::Confirm));
    pad.update(s,true,9);EXPECT_FALSE(pad.pressed(PadButton::Confirm));
    pad.disarm();pad.update(s,true,10);EXPECT_FALSE(pad.down(PadButton::Confirm));
    s.buttons[0]=false;pad.update(s,true,11);s.buttons[0]=true;pad.update(s,true,12);
    EXPECT_TRUE(pad.pressed(PadButton::Confirm));
}
TEST(GamepadControls, FocusAndReconnectNeutralizeAxesAndButtons) {
    GamepadInput pad;GamepadSample s;s.device=0;s.connected=true;pad.update(s,true,1);
    s.axes[1]=1;s.buttons[7]=true;pad.update(s,true,2);ASSERT_EQ(pad.axis(1),1);
    pad.update(s,false,3);EXPECT_EQ(pad.axis(1),0);EXPECT_FALSE(pad.down(PadButton::RightTrigger));
    pad.update(s,true,4);EXPECT_FALSE(pad.armed());
    s.axes.fill(0);s.buttons.fill(false);pad.update(s,true,5);ASSERT_TRUE(pad.armed());
    s.device=2;s.buttons[0]=true;pad.update(s,true,6);EXPECT_FALSE(pad.armed());
    s.connected=false;pad.update(s,true,7);EXPECT_FALSE(pad.connected());
}
TEST(GamepadControls, DeadzoneDiagonalClampAndBoundedDirectionRepeat) {
    GamepadInput pad;GamepadSample s;s.connected=true;s.device=0;pad.update(s,true,1);
    s.axes={.1f,.1f,0,0};pad.update(s,true,2);EXPECT_EQ(pad.axis(0),0);
    s.axes={1,1,0,0};pad.update(s,true,3);EXPECT_NEAR(std::hypot(pad.axis(0),pad.axis(1)),1,1e-6);
    EXPECT_TRUE(pad.navigation(1));EXPECT_TRUE(pad.navigation(3));
    pad.update(s,true,3.1);EXPECT_FALSE(pad.navigation(1));
    pad.update(s,true,3.4);EXPECT_TRUE(pad.navigation(1));
    pad.update(s,true,100);EXPECT_TRUE(pad.navigation(1));
    pad.update(s,true,100.01);EXPECT_FALSE(pad.navigation(1));
    s.axes[0]=std::numeric_limits<float>::quiet_NaN();pad.update(s,true,101);
    EXPECT_FALSE(pad.armed());EXPECT_EQ(pad.axis(0),0);
}
TEST(GamepadControls, TextEventsAreBoundedUnicodeAndFocusIsolatesGameplay) {
    Input input;input.onCharacter('A');input.onCharacter(0xe9);input.onCharacter(0x1f6a4);
    input.onCharacter(0xd800);input.onCharacter(10);input.beginFrame();input.computeDeltas();
    EXPECT_EQ(input.textInput(),"A\xc3\xa9\xf0\x9f\x9a\xa4");
    input.onKeyDown(static_cast<int>(Key::Enter));input.onFocusChanged(false);
    input.onKeyDown(static_cast<int>(Key::B));input.onCharacter('B');
    input.beginFrame();input.computeDeltas();EXPECT_TRUE(input.textInput().empty());
    EXPECT_FALSE(input.wasKeyPressed(Key::Enter));EXPECT_FALSE(input.wasKeyPressed(Key::B));
    input.onFocusChanged(true);for(int i=0;i<1000;++i)input.onCharacter(0x1f6a4);
    input.beginFrame();input.computeDeltas();EXPECT_LE(input.textInput().size(),256u);
}

TEST(GamepadControls, QuickKeyboardChordKeepsItsEventModifiersAfterRelease) {
    Input input;
    input.onKeyDown(static_cast<int>(Key::LeftControl));input.onKeyDown(static_cast<int>(Key::Z));
    input.onKeyUp(static_cast<int>(Key::Z));input.onKeyUp(static_cast<int>(Key::LeftControl));
    input.beginFrame();input.computeDeltas();
    EXPECT_TRUE(input.wasKeyPressed(Key::Z));EXPECT_FALSE(input.isKeyDown(Key::LeftControl));
    EXPECT_EQ(input.keyPressModifiers(Key::Z),Input::controlModifier);
    input.beginFrame();input.onKeyDown(static_cast<int>(Key::R));
    input.onKeyDown(static_cast<int>(Key::LeftShift));input.computeDeltas();
    EXPECT_TRUE(input.wasKeyPressed(Key::R));EXPECT_EQ(input.keyPressModifiers(Key::R),0);
    input.resetState();EXPECT_EQ(input.keyPressModifiers(Key::Z),0);
}
