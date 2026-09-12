#include "game/expedition/cove_onboarding.hpp"
#include <gtest/gtest.h>
#include <limits>

using voxy::game::expedition::CoveOnboarding;
TEST(CoveOnboarding, ActualFactsAdvanceAndNoTutorialCommandAwardsProgress) {
    CoveOnboarding model;CoveOnboarding::Facts f;f.epoch=1;f.tick=1;
    model.observe(f);EXPECT_EQ(model.step(),CoveOnboarding::Step::Walk);
    f.walkedMetres=1.1;model.observe(f);EXPECT_EQ(model.step(),CoveOnboarding::Step::Workshop);
    f.workshop=true;model.observe(f);EXPECT_EQ(model.step(),CoveOnboarding::Step::Edit);
    f.keptEdit=true;model.observe(f);EXPECT_EQ(model.step(),CoveOnboarding::Step::TryBoat);
    EXPECT_EQ(model.card(false,false,true).action,340);
    f.tested=true;model.observe(f);f.onBoat=true;model.observe(f);
    f.sailedMetres=2;model.observe(f);EXPECT_EQ(model.step(),CoveOnboarding::Step::Sail);
    f.atHelm=true;model.observe(f);EXPECT_EQ(model.step(),CoveOnboarding::Step::Save);
    model.observe(f);EXPECT_FALSE(model.card(false,true,false).complete);
    EXPECT_EQ(model.card(false,true,false).action,350);
    f.saved=true;model.observe(f);EXPECT_TRUE(model.card(true,true,false).complete);
}
TEST(CoveOnboarding, RejectsInvalidStaleFactsAndRestartsForAnotherWorld) {
    CoveOnboarding model;CoveOnboarding::Facts f;f.epoch=3;f.tick=20;
    f.walkedMetres=std::numeric_limits<double>::infinity();model.observe(f);
    EXPECT_EQ(model.step(),CoveOnboarding::Step::Walk);
    f.walkedMetres=2;model.observe(f);f.tick=19;f.workshop=true;model.observe(f);
    EXPECT_EQ(model.step(),CoveOnboarding::Step::Workshop);
    f.epoch=4;f.tick=1;f.walkedMetres=0;f.workshop=false;model.observe(f);
    EXPECT_EQ(model.step(),CoveOnboarding::Step::Walk);
    model.restart();EXPECT_EQ(model.step(),CoveOnboarding::Step::Walk);
}
TEST(CoveOnboarding, GuidanceFitsShortCardsAndUsesDeviceSpecificInstructions) {
    CoveOnboarding model;
    EXPECT_NE(model.card(false,false,false).text,model.card(true,false,false).text);
    EXPECT_EQ(model.card(true,true,false).action,91);
    EXPECT_LT(model.card(true,false,false).text.size(),120u);
    EXPECT_EQ(voxy::game::expedition::coveUiText("menu.save"),"Save expedition");
    EXPECT_EQ(voxy::game::expedition::coveUiText("future.key"),"future.key");
}
