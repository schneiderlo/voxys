#include "game/expedition/cove_input_preferences.hpp"
#include "engine/platform/input.hpp"
#include <gtest/gtest.h>
#include <json.hpp>
#include <limits>

namespace {
using namespace voxy;
using namespace voxy::game::expedition;
using A=CoveAction;
constexpr size_t idx(A a){return static_cast<size_t>(a);}
void press(CoveInputSample& s,int key,uint8_t mods=0) {
    s.keys[static_cast<size_t>(key)]=true;s.physicalKeys[static_cast<size_t>(key)]=true;
    s.pressed[static_cast<size_t>(key)]=true;s.pressModifiers[static_cast<size_t>(key)]=mods;s.modifiers=mods;
}
void release(CoveInputSample& s,int key) {
    s.keys[static_cast<size_t>(key)]=false;s.physicalKeys[static_cast<size_t>(key)]=false;s.pressed.fill(false);
}
TEST(CoveInputPreferences, CompleteCanonicalRoundTripAndDistinctContextDefaults) {
    CoveInputPreferences p;std::string encoded,error;
    ASSERT_TRUE(encodeCoveInputPreferences(p,encoded,error))<<error;
    EXPECT_LT(encoded.size(),kMaximumCovePreferencesBytes);
    auto changed=p;changed.mouseSensitivity=2.5;changed.textScale=1.5;changed.locale="bad";
    ASSERT_TRUE(parseCoveInputPreferences(encoded,changed,error))<<error;EXPECT_EQ(changed,p);
    std::string second;ASSERT_TRUE(encodeCoveInputPreferences(changed,second,error));EXPECT_EQ(encoded,second);
    ASSERT_EQ(coveActionList().size(),kCoveActionCount);
    for(const auto& info:coveActionList())EXPECT_EQ(info.defaults,p.bindings[idx(info.action)]);
    EXPECT_EQ(p.bindings[idx(A::Interact)].key,p.bindings[idx(A::Keep)].key);
    EXPECT_NE(coveActionList()[idx(A::Interact)].contexts,coveActionList()[idx(A::Keep)].contexts);
}
TEST(CoveInputPreferences, ConflictsRefuseAtomicallyButSeparateContextsCanShare) {
    CoveInputPreferences p;const auto original=p;std::string error;
    auto binding=p.bindings[idx(A::Jump)];binding.key=69;
    EXPECT_FALSE(rebindCoveAction(p,A::Jump,binding,error));EXPECT_EQ(p,original);EXPECT_FALSE(error.empty());
    binding.key=84;ASSERT_TRUE(rebindCoveAction(p,A::Jump,binding,error)); // Workshop Snap remains T.
    EXPECT_EQ(p.bindings[idx(A::Jump)].key,84);
    binding.key=291;EXPECT_FALSE(rebindCoveAction(p,A::Jump,binding,error));
    binding.key=256;EXPECT_FALSE(rebindCoveAction(p,A::Jump,binding,error)); // Fixed Back cannot disappear.
}
TEST(CoveInputPreferences, FixedMenuButtonCannotBecomeAnUnreachableGameplayBinding) {
    CoveInputPreferences p;std::string error;
    p.bindings[static_cast<size_t>(CoveAction::Pause)].pad=-1;
    const auto original=p;auto jump=p.bindings[static_cast<size_t>(CoveAction::Jump)];jump.pad=9;
    EXPECT_FALSE(rebindCoveAction(p,CoveAction::Jump,jump,error));EXPECT_EQ(p,original);
    EXPECT_EQ(error,"Menu is reserved for the pause or tools menu.");
    auto pause=p.bindings[static_cast<size_t>(CoveAction::Pause)];pause.pad=9;
    EXPECT_TRUE(rebindCoveAction(p,CoveAction::Pause,pause,error));
}
TEST(CoveInputPreferences, MalformedUnknownDuplicateAndExcessiveJsonPreserveOutput) {
    CoveInputPreferences p;p.highContrast=true;const auto original=p;std::string encoded,error;
    ASSERT_TRUE(encodeCoveInputPreferences(p,encoded,error));
    auto object=nlohmann::json::parse(encoded);
    const auto reject=[&](const std::string& text){EXPECT_FALSE(parseCoveInputPreferences(text,p,error));EXPECT_EQ(p,original);};
    auto changed=object;changed["mouseSensitivity"]=false;reject(changed.dump());
    changed=object;changed["textScale"]=1.1;reject(changed.dump());
    changed=object;changed["lookDeadzone"]=.01;reject(changed.dump());
    changed=object;changed["locale"]="de";reject(changed.dump());
    changed=object;changed["unexpected"]=1;reject(changed.dump());
    changed=object;changed["bindings"][1]=changed["bindings"][0];reject(changed.dump());
    changed=object;changed["bindings"][0]["pad"]=1e100;reject(changed.dump());
    reject("{\"version\":1,"+encoded.substr(1));reject(std::string(kMaximumCovePreferencesBytes+1,' '));
    reject(std::string(1000,'[')+std::string(1000,']'));
    std::string unchanged="retained";p.padSensitivity=std::numeric_limits<double>::infinity();
    EXPECT_FALSE(encodeCoveInputPreferences(p,unchanged,error));EXPECT_EQ(unchanged,"retained");
}
TEST(CoveInputPreferences, EventModifiersAndContextChangesDoNotBleedIntoPlay) {
    CoveInputPreferences p;CoveInputRouter router;CoveInputSample s;
    router.tick(s,CoveInputContext::Workshop,p);
    press(s,90,2);release(s,90);s.pressed[90]=true;s.pressModifiers[90]=2;s.modifiers=0;
    router.tick(s,CoveInputContext::Workshop,p);EXPECT_TRUE(router.pressed(A::Undo));EXPECT_FALSE(router.pressed(A::PartDown));
    s={};press(s,69);router.tick(s,CoveInputContext::Menu,p);
    s.pressed.fill(false);router.tick(s,CoveInputContext::World,p);EXPECT_FALSE(router.pressed(A::Interact));EXPECT_FALSE(router.down(A::Interact));
    release(s,69);router.tick(s,CoveInputContext::World,p);press(s,69);router.tick(s,CoveInputContext::World,p);
    EXPECT_TRUE(router.pressed(A::Interact));
}
TEST(CoveInputPreferences, MotorToggleStopsOnSecondPressOppositeDirectionFocusAndMenu) {
    CoveInputPreferences p;p.reelToggle=true;CoveInputRouter router;CoveInputSample s;
    router.tick(s,CoveInputContext::World,p);press(s,81);router.tick(s,CoveInputContext::World,p);
    EXPECT_TRUE(router.pressed(A::ReelIn));release(s,81);router.tick(s,CoveInputContext::World,p);EXPECT_TRUE(router.down(A::ReelIn));
    press(s,81);router.tick(s,CoveInputContext::World,p);EXPECT_FALSE(router.pressed(A::ReelIn));EXPECT_TRUE(router.released(A::ReelIn));
    release(s,81);router.tick(s,CoveInputContext::World,p);press(s,81);router.tick(s,CoveInputContext::World,p);
    release(s,81);press(s,90);router.tick(s,CoveInputContext::World,p);
    EXPECT_TRUE(router.released(A::ReelIn));EXPECT_TRUE(router.pressed(A::PayOut));
    release(s,90);s.focused=false;router.tick(s,CoveInputContext::World,p);EXPECT_TRUE(router.released(A::PayOut));
    s.focused=true;router.tick(s,CoveInputContext::World,p);EXPECT_FALSE(router.down(A::PayOut));
    press(s,81);router.tick(s,CoveInputContext::World,p);router.tick(s,CoveInputContext::Menu,p);
    EXPECT_FALSE(router.down(A::ReelIn));EXPECT_TRUE(router.released(A::ReelIn));
}
TEST(CoveInputPreferences, OppositeHeldMotorsStopAndRemainingHoldResumesOnce) {
    CoveInputPreferences p;CoveInputRouter router;CoveInputSample s;router.tick(s,CoveInputContext::World,p);
    press(s,81);router.tick(s,CoveInputContext::World,p);ASSERT_TRUE(router.pressed(A::ReelIn));
    s.pressed.fill(false);press(s,90);router.tick(s,CoveInputContext::World,p);
    EXPECT_FALSE(router.down(A::ReelIn));EXPECT_FALSE(router.down(A::PayOut));EXPECT_TRUE(router.released(A::ReelIn));
    release(s,90);router.tick(s,CoveInputContext::World,p);EXPECT_TRUE(router.pressed(A::ReelIn));
    router.tick(s,CoveInputContext::World,p);EXPECT_FALSE(router.pressed(A::ReelIn));
}
TEST(CoveInputPreferences, RebindingAndControllerReconnectRequireNeutralAndClearToggles) {
    CoveInputPreferences p;p.reelToggle=true;CoveInputRouter router;CoveInputSample s;
    router.tick(s,CoveInputContext::World,p);press(s,81);router.tick(s,CoveInputContext::World,p);release(s,81);
    router.tick(s,CoveInputContext::World,p);ASSERT_TRUE(router.down(A::ReelIn));
    s.padConnected=true;s.padArmed=true;s.padSerial=1;s.padDown[0]=true;s.padPressed[0]=true;
    router.tick(s,CoveInputContext::World,p);EXPECT_TRUE(router.released(A::ReelIn));EXPECT_FALSE(router.pressed(A::Interact));
    s.padDown.fill(false);s.padPressed.fill(false);router.tick(s,CoveInputContext::World,p);
    s.padDown[0]=true;s.padPressed[0]=true;router.tick(s,CoveInputContext::World,p);EXPECT_TRUE(router.pressed(A::Interact));
    s={};press(s,84);p.bindings[idx(A::Jump)].key=84;router.tick(s,CoveInputContext::World,p);EXPECT_FALSE(router.pressed(A::Jump));
    release(s,84);router.tick(s,CoveInputContext::World,p);press(s,84);router.tick(s,CoveInputContext::World,p);EXPECT_TRUE(router.pressed(A::Jump));
}
TEST(CoveInputPreferences, SensitivityInversionAndMouseToggleRespectOwnership) {
    CoveInputPreferences p;p.padSensitivity=2;p.invertX=true;p.orbitToggle=true;
    CoveInputSample s;s.padConnected=true;s.padArmed=true;CoveInputRouter router;router.tick(s,CoveInputContext::World,p);
    s.axes={.5f,-.25f,.25f,.5f};s.mouseRight=true;s.rightPressed=true;router.tick(s,CoveInputContext::World,p);
    EXPECT_EQ(router.movement(),(std::array<double,2>{.5,.25}));EXPECT_EQ(router.look(),(std::array<double,2>{-.5,1}));
    EXPECT_TRUE(router.orbitDrag());s.mouseRight=false;s.rightPressed=false;router.tick(s,CoveInputContext::World,p);EXPECT_TRUE(router.orbitDrag());
    router.tick(s,CoveInputContext::Menu,p);EXPECT_FALSE(router.orbitDrag());EXPECT_EQ(router.look(),(std::array<double,2>{}));
}
TEST(CoveInputPreferences, PhysicalKeysSurviveResetAndUnmatchedRepeatsCannotResumeAction) {
    Input input;input.onKeyDown(81);input.beginFrame();input.computeDeltas();
    ASSERT_TRUE(input.physicalKeyDown(Key::Q));const auto serial=input.resetSerial();input.resetState();
    EXPECT_GT(input.resetSerial(),serial);EXPECT_TRUE(input.physicalKeyDown(Key::Q));
    input.onKeyDown(81);input.beginFrame();input.computeDeltas();EXPECT_FALSE(input.wasKeyPressed(Key::Q));
    input.onKeyUp(81);input.beginFrame();input.computeDeltas();EXPECT_FALSE(input.physicalKeyDown(Key::Q));
    input.onFocusChanged(false);input.onFocusChanged(true);input.onKeyDown(81,true);
    input.beginFrame();input.computeDeltas();EXPECT_FALSE(input.wasKeyPressed(Key::Q));EXPECT_FALSE(input.physicalKeyDown(Key::Q));
    input.onKeyDown(81);input.beginFrame();input.computeDeltas();EXPECT_TRUE(input.wasKeyPressed(Key::Q));
}
TEST(CoveInputPreferences, ConfigurableRadialDeadzonesRearmAtNewThreshold) {
    GamepadInput pad;GamepadSample s;s.connected=true;s.device=3;pad.update(s,true,0);
    ASSERT_TRUE(pad.setDeadzones(.05f,.4f));s.axes={.1f,0,.3f,0};pad.update(s,true,1);EXPECT_FALSE(pad.armed());
    s.axes={.03f,.03f,0,0};pad.update(s,true,2);ASSERT_TRUE(pad.armed());
    s.axes={.1f,0,.3f,0};pad.update(s,true,3);EXPECT_NEAR(pad.axis(0),(.1-.05)/.95,1e-6);EXPECT_EQ(pad.axis(2),0);
    const auto serial=pad.serial();EXPECT_FALSE(pad.setDeadzones(-1,.2f));EXPECT_EQ(pad.serial(),serial);
    EXPECT_TRUE(pad.setDeadzones(.05f,.4f));EXPECT_EQ(pad.serial(),serial);
    EXPECT_TRUE(pad.setDeadzones(.2f,.2f));EXPECT_FALSE(pad.armed());
    s.axes={.19f,.19f,0,0};pad.update(s,true,4);EXPECT_FALSE(pad.armed()); // Radial, not square neutral gate.
}
TEST(CoveInputPreferences, DisconnectedGamepadPollingDoesNotCancelKeyboardToggle) {
    CoveInputPreferences prefs;prefs.reelToggle=true;CoveInputRouter router;Input input;
    const auto frame=[&]{input.beginFrame();input.computeDeltas();router.tick(sampleCoveInput(input),CoveInputContext::World,prefs);};
    frame();input.onKeyDown(81);frame();ASSERT_TRUE(router.down(CoveAction::ReelIn));
    input.onKeyUp(81);frame();ASSERT_TRUE(router.down(CoveAction::ReelIn));
    for(int i=0;i<5;++i){frame();EXPECT_TRUE(router.down(CoveAction::ReelIn));}
    input.onFocusChanged(false);frame();EXPECT_FALSE(router.down(CoveAction::ReelIn));
}
} // namespace
