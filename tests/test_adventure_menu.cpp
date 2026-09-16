#include "game/adventure/menu_intent.hpp"
#include <gtest/gtest.h>
#include <limits>

namespace voxy::game::adventure {
TEST(AdventureMenuIntents, StablePublicationKeepsIntentButNewReplyCannotReuseOldClick) {
    MenuIntents menu;
    ASSERT_TRUE(menu.publish("dialogue:1",{{"accept-home",true},{"close",true}}));
    const auto accept=menu.intent(0);const auto token=menu.token();
    ASSERT_TRUE(menu.publish("dialogue:1",{{"accept-home",true},{"close",true}}));
    EXPECT_EQ(menu.token(),token);EXPECT_EQ(menu.resolve(accept),0u);
    ASSERT_TRUE(menu.publish("dialogue:1",{{"complete-home",true},{"close",true}}));
    EXPECT_FALSE(menu.resolve(accept));EXPECT_EQ(menu.resolve(menu.intent(0)),0u);
    const auto complete=menu.intent(0);
    ASSERT_TRUE(menu.publish("explore",{}));ASSERT_TRUE(menu.publish("dialogue:1",{{"complete-home",true},{"close",true}}));
    EXPECT_FALSE(menu.resolve(complete));
}
TEST(AdventureMenuIntents, ContainerTargetOwnershipAndEligibilityAreBoundToThePublishedChoice) {
    MenuIntents menu;ASSERT_TRUE(menu.publish("chest:10",{{"store:slot0:wood:10:packrev8:chestrev3",true}}));
    const auto beforeTransfer=menu.intent(0);
    ASSERT_TRUE(menu.publish("chest:10",{{"store:slot0:wood:10:packrev9:chestrev9",true}}));
    EXPECT_FALSE(menu.resolve(beforeTransfer));const auto firstChest=menu.intent(0);
    ASSERT_TRUE(menu.publish("chest:20",{{"store:slot0:wood:10:packrev9:chestrev9",true}}));EXPECT_FALSE(menu.resolve(firstChest));
    const auto enabled=menu.intent(0);ASSERT_TRUE(menu.publish("chest:20",{{"store:slot0:wood:10:packrev9:chestrev9",false}}));
    EXPECT_FALSE(menu.resolve(enabled));EXPECT_EQ(menu.intent(0),0);
}
TEST(AdventureMenuIntents, SameFrameDoubleActivationCannotAcceptThenCompleteOrEquipThenUnequip) {
    MenuIntents menu;ASSERT_TRUE(menu.publish("dialogue:1",{{"accept",true}}));menu.beginFrame();
    ASSERT_TRUE(menu.consume(menu.intent(0)));ASSERT_TRUE(menu.publish("dialogue:1",{{"complete",true}}));
    EXPECT_FALSE(menu.consume(menu.intent(0)));menu.beginFrame();EXPECT_TRUE(menu.consume(menu.intent(0)));
    ASSERT_TRUE(menu.publish("bag",{{"equip:slot3",true}}));menu.beginFrame();ASSERT_TRUE(menu.consume(menu.intent(0)));
    ASSERT_TRUE(menu.publish("bag",{{"unequip",true}}));EXPECT_FALSE(menu.consume(menu.intent(0)));
    menu.beginFrame();EXPECT_TRUE(menu.consume(menu.intent(0)));
}
TEST(AdventureMenuIntents, UnscopedRowsMalformedValuesAndOverCapacityFailClosed) {
    MenuIntents menu;ASSERT_TRUE(menu.publish("bag",{{"equip:slot3",true}}));
    for(const int value:{-1,0,1,63,64,std::numeric_limits<int>::max()})EXPECT_FALSE(menu.resolve(value));
    EXPECT_EQ(menu.intent(1),0);EXPECT_FALSE(menu.resolve(menu.intent(0)+1));
    EXPECT_LT(static_cast<uint64_t>(MenuIntents::maximumToken)*64u+64u,static_cast<uint64_t>(std::numeric_limits<int>::max()));
    std::vector<MenuIntentChoice> tooMany(65,{"overflow",true});const auto old=menu.intent(0);
    EXPECT_FALSE(menu.publish("overflow",tooMany));EXPECT_EQ(menu.token(),0u);EXPECT_FALSE(menu.resolve(old));
    EXPECT_FALSE(menu.publish("bag",{{"equip:slot3",true}}));
}
} // namespace voxy::game::adventure
