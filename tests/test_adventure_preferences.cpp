#include "game/adventure/adventure_preferences.hpp"
#include "game/adventure/adventure_input.hpp"
#include <gtest/gtest.h>
#include <json.hpp>
#include <algorithm>
#include <limits>

namespace voxy::game::adventure {
namespace {
using Sample=expedition::CoveInputSample;
using Action=CombatAction;
void keyPress(Sample& sample,int key,uint8_t modifiers=0) {
    const auto i=static_cast<size_t>(key);
    sample.keys[i]=sample.physicalKeys[i]=sample.pressed[i]=true;
    sample.modifiers=modifiers;sample.pressModifiers[i]=modifiers;
}
void keyRelease(Sample& sample,int key) {
    const auto i=static_cast<size_t>(key);
    sample.keys[i]=sample.physicalKeys[i]=sample.pressed[i]=false;
}
void padPress(Sample& sample,int button) {
    sample.padDown[static_cast<size_t>(button)]=true;sample.padPressed[static_cast<size_t>(button)]=true;
}
TEST(AdventurePreferences, IndependentCanonicalProfileKeepsAdventureMovementAndBuildBindings) {
    AdventurePreferences p;std::string encoded,error;
    EXPECT_EQ(p.combat,(std::array<CombatBinding,2>{{{0,0,5},{81,-1,1}}}));
    EXPECT_EQ(combatBindingLabel(p,Action::Attack,false),"Left mouse");
    EXPECT_EQ(combatBindingLabel(p,Action::Dodge,false),"Q");
    EXPECT_EQ(combatBindingLabel(p,Action::Attack,true),"RB / R1");
    p.textScale=1.5;p.mouseSensitivity=.25;p.padSensitivity=3;p.moveDeadzone=.05;p.lookDeadzone=.45;
    p.highContrast=p.reducedMotion=p.invertX=p.invertY=p.orbitToggle=true;
    p.combat[0]={67,2,7};p.combat[1]={57,-1,-1};
    ASSERT_TRUE(encodeAdventurePreferences(p,encoded,error))<<error;
    EXPECT_LT(encoded.size(),kMaximumAdventurePreferencesBytes);
    EXPECT_EQ(nlohmann::json::parse(encoded).at("version"),1);
    AdventurePreferences decoded;ASSERT_TRUE(parseAdventurePreferences(encoded,decoded,error))<<error;EXPECT_EQ(decoded,p);
    std::string second;ASSERT_TRUE(encodeAdventurePreferences(decoded,second,error));EXPECT_EQ(second,encoded);
    EXPECT_EQ(combatBindingLabel(decoded,Action::Attack,false),"C / Middle mouse");
    EXPECT_EQ(combatBindingLabel(decoded,Action::Dodge,true),"Unbound");
    const auto routing=adventureRoutingPreferences(decoded);
    EXPECT_EQ(routing.bindings,adventureInputDefaults().bindings);
    EXPECT_EQ(routing.textScale,p.textScale);EXPECT_EQ(routing.mouseSensitivity,p.mouseSensitivity);
    EXPECT_EQ(routing.padSensitivity,p.padSensitivity);EXPECT_EQ(routing.moveDeadzone,p.moveDeadzone);
    EXPECT_EQ(routing.lookDeadzone,p.lookDeadzone);EXPECT_EQ(routing.highContrast,p.highContrast);
    EXPECT_EQ(routing.invertX,p.invertX);EXPECT_EQ(routing.invertY,p.invertY);EXPECT_EQ(routing.orbitToggle,p.orbitToggle);
    auto reordered=nlohmann::json::parse(encoded);std::swap(reordered["combat"][0],reordered["combat"][1]);
    ASSERT_TRUE(parseAdventurePreferences(reordered.dump(),decoded,error));EXPECT_EQ(decoded,p);
}
TEST(AdventurePreferences, MalformedConflictingAndForeignProfilesKeepEveryPreviousSetting) {
    AdventurePreferences kept;kept.highContrast=true;kept.combat[0].key=67;
    const auto before=kept;std::string text,error;ASSERT_TRUE(encodeAdventurePreferences(kept,text,error));
    const auto original=nlohmann::json::parse(text);
    const auto refuse=[&](std::string_view bad) {
        EXPECT_FALSE(parseAdventurePreferences(bad,kept,error));EXPECT_EQ(kept,before);EXPECT_FALSE(error.empty());
    };
    refuse("");refuse(text+"{}");refuse(text.substr(0,text.size()-1));
    refuse("{\"version\":1,"+text.substr(1));
    auto duplicate=text;const auto key=duplicate.find("\"key\":67");ASSERT_NE(key,std::string::npos);
    duplicate.insert(key,"\"key\":67,");refuse(duplicate);
    refuse(std::string(kMaximumAdventurePreferencesBytes+1,' '));
    auto bad=original;bad["version"]=2;refuse(bad.dump());
    bad=original;bad["version"]=1.;refuse(bad.dump());
    bad=original;bad.erase("reducedMotion");refuse(bad.dump());
    bad=original;bad["world"]="unrelated";refuse(bad.dump());
    bad=original;bad["highContrast"]=1;refuse(bad.dump());
    bad=original;bad["mouseSensitivity"]="1";refuse(bad.dump());
    bad=original;bad["textScale"]=1.1;refuse(bad.dump());
    bad=original;bad["combat"][0]["key"]=67.;refuse(bad.dump());
    bad=original;bad["combat"][0]["key"]=std::numeric_limits<uint64_t>::max();refuse(bad.dump());
    bad=original;bad["combat"][0]["key"]=std::numeric_limits<int64_t>::min();refuse(bad.dump());
    bad=original;bad["combat"][0]["modifiers"]=0;refuse(bad.dump());
    bad=original;bad["combat"][1]["action"]="attack";refuse(bad.dump());
    bad=original;bad["combat"][0]["action"]="jump";refuse(bad.dump());
    bad=original;bad["combat"].erase(1);refuse(bad.dump());
    bad=original;bad["combat"][1]["key"]=67;refuse(bad.dump());
    EXPECT_NE(error.find("Attack"),std::string::npos);EXPECT_NE(error.find("Dodge"),std::string::npos);
    bad=original;bad["bindings"]=bad["combat"];bad.erase("combat");refuse(bad.dump());
    refuse("{\"version\":1,\"combat\":[[[[[[[[[[[0]]]]]]]]]]]}");
    auto nonfinite=text;const auto number=nonfinite.find("\"mouseSensitivity\":1.0");ASSERT_NE(number,std::string::npos);
    nonfinite.replace(number,22,"\"mouseSensitivity\":1e999");refuse(nonfinite);
}
TEST(AdventurePreferences, ReservedControlsAndConflictsRefuseWithoutPartialEncoding) {
    AdventurePreferences p;std::string error;
    const auto refuse=[&](AdventurePreferences invalid) {
        std::string output="retained profile";EXPECT_FALSE(validateAdventurePreferences(invalid,error));
        EXPECT_FALSE(encodeAdventurePreferences(invalid,output,error));EXPECT_EQ(output,"retained profile");
    };
    for(int key:{32,65,66,68,69,71,82,83,87,258,291,294,298,341}) {auto invalid=p;invalid.combat[0].key=key;refuse(invalid);}
    for(int mouse:{-2,1,3}) {auto invalid=p;invalid.combat[0].mouse=mouse;refuse(invalid);}
    for(int pad:{-2,0,2,8,9,11,16,17}) {auto invalid=p;invalid.combat[0].pad=pad;refuse(invalid);}
    for(int field=0;field<3;++field) {
        auto invalid=p;
        if(field==0)invalid.combat[0].key=81;
        else if(field==1)invalid.combat[1].mouse=0;
        else invalid.combat[1].pad=5;
        refuse(invalid);EXPECT_NE(error.find("Attack"),std::string::npos);EXPECT_NE(error.find("Dodge"),std::string::npos);
    }
    for(double invalid:{.24,3.01,std::numeric_limits<double>::infinity(),std::numeric_limits<double>::quiet_NaN()}) {
        auto changed=p;changed.mouseSensitivity=invalid;refuse(changed);
        changed=p;changed.padSensitivity=invalid;refuse(changed);
    }
    for(double invalid:{.049,.451,std::numeric_limits<double>::quiet_NaN()}) {
        auto changed=p;changed.moveDeadzone=invalid;refuse(changed);
        changed=p;changed.lookDeadzone=invalid;refuse(changed);
    }
    for(double scale:{1.,1.25,1.5}) {p.textScale=scale;EXPECT_TRUE(validateAdventurePreferences(p,error));}
    p.combat.fill({});ASSERT_TRUE(validateAdventurePreferences(p,error));
    EXPECT_EQ(combatBindingLabel(p,Action::Attack,false),"Unbound");
    for(int choice:combatKeyChoices()){p.combat[0].key=choice;EXPECT_TRUE(validateAdventurePreferences(p,error))<<error;}
    p.combat[0].key=0;
    for(int choice:combatMouseChoices()){p.combat[0].mouse=choice;EXPECT_TRUE(validateAdventurePreferences(p,error))<<error;}
    p.combat[0].mouse=-1;
    for(int choice:combatPadChoices()){p.combat[0].pad=choice;EXPECT_TRUE(validateAdventurePreferences(p,error))<<error;}
}
TEST(AdventureCombatInput, ShortKeyTapsUseCapturedNoModifierEdgesAndNeverHeldState) {
    AdventurePreferences p;AdventureCombatInputRouter router;Sample s;router.tick(s,true,p);
    keyPress(s,81);s.keys[81]=s.physicalKeys[81]=false;
    router.tick(s,true,p);EXPECT_TRUE(router.pressed(Action::Dodge));
    s.pressed.fill(false);router.tick(s,true,p);EXPECT_FALSE(router.pressed(Action::Dodge));
    keyPress(s,81,2);s.modifiers=0;router.tick(s,true,p);EXPECT_FALSE(router.pressed(Action::Dodge));
    s.pressed.fill(false);router.tick(s,true,p);EXPECT_FALSE(router.pressed(Action::Dodge));
    keyRelease(s,81);router.tick(s,true,p);keyPress(s,81);s.modifiers=1;
    router.tick(s,true,p);EXPECT_TRUE(router.pressed(Action::Dodge));
    s.pressed.fill(false);router.tick(s,true,p);EXPECT_FALSE(router.pressed(Action::Dodge));
    EXPECT_FALSE(router.pressed(static_cast<Action>(255)));
}
TEST(AdventureCombatInput, PhysicalKeysRequireReleaseAfterMenusFocusResetAndRebinding) {
    for(int boundary=0;boundary<4;++boundary) {
        SCOPED_TRACE(boundary);
        AdventurePreferences p;AdventureCombatInputRouter router;Sample s;router.tick(s,true,p);
        keyPress(s,81);router.tick(s,true,p);ASSERT_TRUE(router.pressed(Action::Dodge));
        s.pressed.fill(false);
        if(boundary==0){router.tick(s,false,p);router.tick(s,true,p);}
        if(boundary==1){s.focused=false;router.tick(s,true,p);s.focused=true;router.tick(s,true,p);}
        if(boundary==2){++s.resetSerial;s.keys.fill(false);router.tick(s,true,p);}
        if(boundary==3){router.reset();router.tick(s,true,p);}
        EXPECT_FALSE(router.pressed(Action::Dodge));
        s.pressed[81]=true;router.tick(s,true,p);EXPECT_FALSE(router.pressed(Action::Dodge));
        keyRelease(s,81);router.tick(s,true,p);keyPress(s,81);router.tick(s,true,p);EXPECT_TRUE(router.pressed(Action::Dodge));
    }
    AdventurePreferences p;AdventureCombatInputRouter router;Sample s;router.tick(s,true,p);
    keyPress(s,67);p.combat[0].key=67;router.tick(s,true,p);EXPECT_FALSE(router.pressed(Action::Attack));
    s.pressed.fill(false);router.tick(s,true,p);EXPECT_FALSE(router.pressed(Action::Attack));
    keyRelease(s,67);router.tick(s,true,p);keyPress(s,67);router.tick(s,true,p);EXPECT_TRUE(router.pressed(Action::Attack));
}
TEST(AdventureCombatInput, HeldMouseCannotAttackAcrossMenuFocusResetOrPreferenceChanges) {
    for(int boundary=0;boundary<5;++boundary) {
        SCOPED_TRACE(boundary);
        AdventurePreferences p;AdventureCombatInputRouter router;Sample s;router.tick(s,true,p);
        s.mouseLeft=true;router.tick(s,true,p);ASSERT_TRUE(router.pressed(Action::Attack));
        router.tick(s,true,p);EXPECT_FALSE(router.pressed(Action::Attack));
        if(boundary==0){router.tick(s,false,p);router.tick(s,true,p);}
        if(boundary==1){s.focused=false;router.tick(s,true,p);s.focused=true;router.tick(s,true,p);}
        if(boundary==2){++s.resetSerial;router.tick(s,true,p);}
        if(boundary==3){p.textScale=1.25;router.tick(s,true,p);}
        if(boundary==4){router.reset();router.tick(s,true,p);}
        EXPECT_FALSE(router.pressed(Action::Attack));router.tick(s,true,p);EXPECT_FALSE(router.pressed(Action::Attack));
        s.mouseLeft=false;router.tick(s,true,p);s.mouseLeft=true;router.tick(s,true,p);EXPECT_TRUE(router.pressed(Action::Attack));
    }
    AdventurePreferences p;AdventureCombatInputRouter router;Sample s;router.tick(s,true,p);
    s.mouseMiddle=true;p.combat[0].mouse=2;router.tick(s,true,p);EXPECT_FALSE(router.pressed(Action::Attack));
    s.mouseMiddle=false;router.tick(s,true,p);s.mouseMiddle=true;router.tick(s,true,p);EXPECT_TRUE(router.pressed(Action::Attack));
}
TEST(AdventureCombatInput, AccumulatedShortMouseClicksSurviveFramesButNeverRearmAcrossBoundaries) {
    for(size_t button:{size_t{0},size_t{2}}) {
        SCOPED_TRACE(button);
        AdventurePreferences p;p.combat[0].mouse=static_cast<int>(button);
        AdventureCombatInputRouter router;Sample s;std::array<bool,3> click{};click[button]=true;
        router.tick(s,true,p);
        // Both physical states are up: the full click occurred between frames.
        router.tick(s,true,p,click);EXPECT_TRUE(router.pressed(Action::Attack));
        router.tick(s,true,p);EXPECT_FALSE(router.pressed(Action::Attack));
        router.tick(s,false,p,click);EXPECT_FALSE(router.pressed(Action::Attack));
        router.tick(s,true,p,click);EXPECT_FALSE(router.pressed(Action::Attack));
        router.tick(s,true,p,click);EXPECT_FALSE(router.pressed(Action::Attack));
        router.tick(s,true,p);router.tick(s,true,p,click);EXPECT_TRUE(router.pressed(Action::Attack));

        // A pending press cannot count as neutral after reset/rebind, and a
        // synthetic edge cannot bypass a still physically held button.
        router.reset();router.tick(s,true,p,click);EXPECT_FALSE(router.pressed(Action::Attack));
        if(button==0)s.mouseLeft=true;else s.mouseMiddle=true;
        router.tick(s,true,p);EXPECT_FALSE(router.pressed(Action::Attack));
        router.tick(s,true,p,click);EXPECT_FALSE(router.pressed(Action::Attack));
        s.mouseLeft=s.mouseMiddle=false;router.tick(s,true,p);
        p.mouseSensitivity=1.25;router.tick(s,true,p,click);EXPECT_FALSE(router.pressed(Action::Attack));
        router.tick(s,true,p);router.tick(s,true,p,click);EXPECT_TRUE(router.pressed(Action::Attack));
        s.focused=false;router.tick(s,true,p,click);s.focused=true;
        router.tick(s,true,p,click);EXPECT_FALSE(router.pressed(Action::Attack));
        router.tick(s,true,p);router.tick(s,true,p,click);EXPECT_TRUE(router.pressed(Action::Attack));
    }
}
TEST(AdventureCombatInput, PadReconnectAndSerialChangesRequireNeutralWithoutCancellingKeyboard) {
    AdventurePreferences p;AdventureCombatInputRouter router;Sample s;
    s.padConnected=s.padArmed=true;router.tick(s,true,p);
    padPress(s,5);router.tick(s,true,p);ASSERT_TRUE(router.pressed(Action::Attack));
    s.padPressed.fill(false);router.tick(s,true,p);EXPECT_FALSE(router.pressed(Action::Attack));
    router.tick(s,false,p);router.tick(s,true,p);EXPECT_FALSE(router.pressed(Action::Attack));
    s.padPressed[5]=true;router.tick(s,true,p);EXPECT_FALSE(router.pressed(Action::Attack));
    s.padDown.fill(false);s.padPressed.fill(false);s.axes[0]=.2f;router.tick(s,true,p);
    padPress(s,5);router.tick(s,true,p);EXPECT_FALSE(router.pressed(Action::Attack));
    s.padDown.fill(false);s.padPressed.fill(false);s.axes.fill(0);router.tick(s,true,p);
    // Moving after the neutral sample is valid. The router does not apply the
    // configurable deadzone again to an already filtered analog sample.
    s.axes[0]=.01f;padPress(s,5);router.tick(s,true,p);EXPECT_TRUE(router.pressed(Action::Attack));
    ++s.padSerial;keyPress(s,81);router.tick(s,true,p);
    EXPECT_FALSE(router.pressed(Action::Attack));EXPECT_TRUE(router.pressed(Action::Dodge));
    keyRelease(s,81);s.padConnected=false;s.padArmed=false;router.tick(s,true,p);
    s.padConnected=s.padArmed=true;++s.padSerial;router.tick(s,true,p);EXPECT_FALSE(router.pressed(Action::Attack));
    s.padDown.fill(false);s.padPressed.fill(false);s.axes.fill(0);router.tick(s,true,p);
    padPress(s,5);router.tick(s,true,p);EXPECT_TRUE(router.pressed(Action::Attack));
    s.padPressed.fill(false);s.padArmed=false;router.tick(s,true,p);
    s.padArmed=true;s.padPressed[5]=true;router.tick(s,true,p);EXPECT_FALSE(router.pressed(Action::Attack));
}
TEST(AdventureCombatInput, ReboundPadAndInvalidOrUnboundProfilesNeverLeakAnAction) {
    AdventurePreferences p;AdventureCombatInputRouter router;Sample s;
    s.padConnected=s.padArmed=true;router.tick(s,true,p);
    padPress(s,7);p.combat[0].pad=7;router.tick(s,true,p);EXPECT_FALSE(router.pressed(Action::Attack));
    s.padDown.fill(false);s.padPressed.fill(false);router.tick(s,true,p);
    padPress(s,7);s.mouseLeft=true;router.tick(s,true,p);EXPECT_TRUE(router.pressed(Action::Attack));
    s.padPressed.fill(false);router.tick(s,true,p);EXPECT_FALSE(router.pressed(Action::Attack));
    p.combat[1].pad=7;s.padPressed[7]=true;router.tick(s,true,p);
    EXPECT_FALSE(router.pressed(Action::Attack));EXPECT_FALSE(router.pressed(Action::Dodge));
    p.combat[1].pad=1;router.tick(s,true,p);EXPECT_FALSE(router.pressed(Action::Attack));
    s.padDown.fill(false);s.padPressed.fill(false);s.mouseLeft=false;router.tick(s,true,p);
    padPress(s,7);router.tick(s,true,p);EXPECT_TRUE(router.pressed(Action::Attack));
    p.combat.fill({});s={};s.padConnected=s.padArmed=true;router.tick(s,true,p);
    keyPress(s,81);s.mouseLeft=true;padPress(s,7);router.tick(s,true,p);
    EXPECT_FALSE(router.pressed(Action::Attack));EXPECT_FALSE(router.pressed(Action::Dodge));
}
} // namespace
} // namespace voxy::game::adventure
