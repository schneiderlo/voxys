#include "render/adventure_hud.hpp"
#include "game/adventure/building_catalog.hpp"
#include "game/adventure/adventure_presentation.hpp"
#include "game/adventure/adventure_navigation.hpp"
#include "render/generated/adventure_piece_thumbnails.hpp"
#include "core/sha256.hpp"
#include "gpu/context.hpp"
#include "gpu/resources.hpp"
#include <gtest/gtest.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <climits>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <set>
#include <span>
#include <memory>
#include <thread>
#include <glm/vec2.hpp>
#include <stb_image.h>
#include <stb_image_write.h>

namespace voxy::render {
namespace {
AdventureHudRow row(std::string label,uint32_t intent=65) {
    AdventureHudRow r;r.label=std::move(label);r.intent=intent;return r;
}
AdventureHudRow creativeAction(std::string label,int action,int value,uint32_t intent) {
    auto result=row(std::move(label),intent);result.action=action;result.value=value;return result;
}
AdventureHudContent creativeContent() {
    AdventureHudContent c;c.creative=true;c.mode=AdventureHudMode::Build;
    c.title="Free build";c.selected="Brick 2 x 2";c.pieceKind=9;c.paint=0xe53b33;c.quickSlot=1;
    c.status="Ready to place";c.context="Aim to place";c.tone=CoveHudTone::Ready;
    c.topActions={creativeAction("Motorbike",32,0,501),creativeAction("Cannon",33,0,503),
        creativeAction("Save",8,0,507),creativeAction("Menu",31,0,509)};
    c.quickActions={creativeAction("Use",7,0,601),creativeAction("Build",21,0,603),creativeAction("Pause",9,0,605)};
    c.buildControls={creativeAction("Pieces",23,0,701),creativeAction("Rotate",3,0,703),
        creativeAction("Undo",6,0,705),creativeAction("Colour",38,0,707),
        creativeAction("Raise",12,1,709),creativeAction("Lower",12,-1,711),
        creativeAction("Remove",5,0,713),creativeAction("Help",29,1,715),creativeAction("Done",22,0,717)};
    for(const auto& definition:game::adventure::buildingCatalog()) {
        const auto kind=static_cast<uint8_t>(definition.kind);
        auto item=creativeAction(std::string(definition.name),10,0,1001u+uint32_t{kind}*7);
        item.pieceKind=kind;c.rows.push_back(item);
    }
    constexpr std::array<uint8_t,6> kinds{9,10,2,1,4,15};
    constexpr std::array<uint32_t,6> paints{0xe53b33,0xf3f2eb,0,0x3ba85c,0xf3f2eb,0};
    for(size_t slot=0;slot<kinds.size();++slot) {
        auto shortcut=c.rows[kinds[slot]-1u];shortcut.action=39;shortcut.value=static_cast<int>(slot+1);shortcut.intent+=500;
        shortcut.paint=paints[slot];c.hotbar.push_back(shortcut);
    }
    c.selectedRow=8;
    constexpr std::array<std::string_view,7> names{"Original","Red","Yellow","Green","Blue","White","Graphite"};
    constexpr std::array<uint32_t,7> colours{0,0xe53b33,0xffd83d,0x3ba85c,0x2d91cc,0xf3f2eb,0x354450};
    for(size_t i=0;i<c.colours.size();++i)c.colours[i]=creativeAction(std::string(names[i]),10,static_cast<int>(colours[i]),1801u+static_cast<uint32_t>(i)*11);
    c.categories={creativeAction("Structure",13,1,1901),creativeAction("Bricks",13,2,1903),creativeAction("Furniture",13,3,1907)};
    return c;
}
std::vector<CoveHudQuad> creativeSprites(const AdventureHudLayout& layout) {
    std::vector<CoveHudQuad> result;
    for(size_t i=0;i<layout.canvas.count;++i) {
        const auto& quad=layout.canvas.quads[i];
        if(((quad.uv.x>=2&&quad.uv.x<3)||(quad.uv.x>=4&&quad.uv.x<5))&&quad.uv.z==.125f&&quad.uv.w==.25f)
            result.push_back(quad);
    }
    return result;
}
bool sameIdentity(const AdventureHudHit& hit,const AdventureHudRow& source) {
    return hit.action==source.action&&hit.value==source.value&&hit.intent==source.intent&&hit.enabled==source.enabled;
}
void identitiesComeFromContent(const AdventureHudLayout& layout,const AdventureHudContent& content) {
    for(const auto& hit:layout.hits) {
        const auto matches=[&](const auto& rows){return std::any_of(rows.begin(),rows.end(),[&](const auto& item){return sameIdentity(hit,item)&&hit.label==item.label;});};
        EXPECT_TRUE(matches(content.rows)||matches(content.hotbar)||matches(content.topActions)
            ||matches(content.quickActions)||matches(content.buildControls)||matches(content.colours)||matches(content.categories));
    }
}
void bounded(const AdventureHudLayout& layout,uint32_t width,uint32_t height) {
    EXPECT_FALSE(layout.canvas.truncated);
    EXPECT_FALSE(layout.trianglesTruncated);
    EXPECT_LE(layout.canvas.count,CoveHudLayout::maximumQuads);
    EXPECT_LE(layout.triangles.size(),AdventureHudLayout::maximumTriangles);
    EXPECT_LE(layout.thumbnailCount,AdventureHudLayout::maximumVisibleThumbnails);
    for(const auto& triangle:layout.triangles)for(size_t i=0;i<3;++i) {
        EXPECT_TRUE(std::isfinite(triangle.xy[i*2]));EXPECT_TRUE(std::isfinite(triangle.xy[i*2+1]));
        EXPECT_GE(triangle.xy[i*2],0);EXPECT_LE(triangle.xy[i*2],static_cast<float>(width));
        EXPECT_GE(triangle.xy[i*2+1],0);EXPECT_LE(triangle.xy[i*2+1],static_cast<float>(height));
    }
    for(size_t i=0;i<layout.canvas.count;++i) {
        const auto b=layout.canvas.quads[i].bounds;
        for(int k=0;k<4;++k)EXPECT_TRUE(std::isfinite(b[k]));
        EXPECT_GE(b.x,0);EXPECT_GE(b.y,0);EXPECT_GT(b.z,0);EXPECT_GT(b.w,0);
        EXPECT_LE(b.x+b.z,static_cast<float>(width)+.001f);
        EXPECT_LE(b.y+b.w,static_cast<float>(height)+.001f);
    }
    for(const auto& hit:layout.hits) {
        EXPECT_GE(hit.bounds.z,44);EXPECT_GE(hit.bounds.w,44);
        EXPECT_GE(hit.bounds.x,0);EXPECT_GE(hit.bounds.y,0);
        EXPECT_LE(hit.bounds.x+hit.bounds.z,static_cast<float>(width)+.001f);
        EXPECT_LE(hit.bounds.y+hit.bounds.w,static_cast<float>(height)+.001f);
    }
    for(size_t i=0;i<layout.hits.size();++i)for(size_t j=i+1;j<layout.hits.size();++j) {
        const auto a=layout.hits[i].bounds,b=layout.hits[j].bounds;
        EXPECT_FALSE(a.x<b.x+b.z-.01f&&a.x+a.z>b.x+.01f&&a.y<b.y+b.w-.01f&&a.y+a.w>b.y+.01f);
    }
}
void completeGuideBody(const AdventureHudLayout& layout,std::string_view text,float pixels,bool creative=false) {
    ASSERT_TRUE(layout.guideBodyComplete);
    ASSERT_GT(layout.guideBodyLines,0u);
    ASSERT_LE(layout.guideBodyFirstQuad+layout.guideBodyQuadCount,layout.canvas.count);
    // Build an independent expected glyph sequence one source character at a
    // time. This catches a lost tail after character 160, an omitted paragraph,
    // or an ellipsis even if the layout's own completion flag were mistaken.
    std::vector<glm::vec4> expected;
    for(char c:text) {
        if(c==' '||c=='\n')continue;
        ASSERT_GE(c,32);ASSERT_LE(c,126);
        CoveHudLayout single;
        const auto append=creative?appendAdventureHudText:appendCoveHudText;
        const auto rendered=append(single,std::string(" ")+c,{0,0,100,100},pixels,{1,1,1,1},1);
        ASSERT_FALSE(rendered.clipped);ASSERT_EQ(single.count,1u);expected.push_back(single.quads[0].uv);
    }
    ASSERT_EQ(layout.guideBodyQuadCount,expected.size());
    const auto body=layout.guideBodyBounds;
    for(size_t i=0;i<expected.size();++i) {
        const auto& quad=layout.canvas.quads[layout.guideBodyFirstQuad+i];const auto uv=quad.uv,want=expected[i];
        // A glyph's side bearing can touch the text rectangle. Its atlas
        // rectangle must still identify this exact character, in order.
        EXPECT_GE(uv.x,want.x-.00001f);EXPECT_LE(uv.x+uv.z,want.x+want.z+.00001f);
        EXPECT_NEAR(uv.y,want.y,.00001f);EXPECT_NEAR(uv.w,want.w,.00001f);
        EXPECT_GE(quad.bounds.x,body.x);EXPECT_GE(quad.bounds.y,body.y);
        EXPECT_LE(quad.bounds.x+quad.bounds.z,body.x+body.z+.001f);
        EXPECT_LE(quad.bounds.y+quad.bounds.w,body.y+body.w+.001f);
    }
    for(const auto& hit:layout.hits)EXPECT_GE(hit.bounds.y,body.y+body.w+7.99f);
}
}
TEST(AdventureHud, ExploreLeavesWorldClearAndCarriesExactActionIdentity) {
    AdventureHudContent c;c.objective="A place to return: add a chest to your home.";
    c.context="E  Talk to Moss";c.quickActions={row("B Build",91),row("J Journal",93),row("Esc Menu",95)};
    for(const auto size:{glm::uvec2(640,480),glm::uvec2(960,600),glm::uvec2(1280,800)})
    for(float scale:{1.f,1.25f,1.5f}) {
        c.textScale=scale;const auto layout=layoutAdventureHud(c,size.x,size.y);bounded(layout,size.x,size.y);
        ASSERT_EQ(layout.hits.size(),3u);EXPECT_EQ(layout.hits[1].intent,93u);EXPECT_EQ(layout.hits[1].action,10);
        const glm::vec2 center(static_cast<float>(size.x)*.5f,static_cast<float>(size.y)*.5f);
        for(const auto& panel:layout.panels)
            EXPECT_FALSE(center.x>panel.x&&center.x<panel.x+panel.z&&center.y>panel.y&&center.y<panel.y+panel.w);
        EXPECT_EQ(layout.canvas.bodyPixels,20*scale);
    }
}
TEST(AdventureHud, EveryCatalogSelectionHasAUsableVisibleCardAtEveryTextSize) {
    AdventureHudContent c;c.mode=AdventureHudMode::Catalog;c.selected="Doorway";c.cost="6 wood";c.status="Ready to place";
    for(const auto& definition:game::adventure::buildingCatalog()) {
        auto item=row(std::string(definition.name),100+static_cast<uint32_t>(definition.kind));
        item.pieceKind=static_cast<uint8_t>(definition.kind);c.rows.push_back(item);
    }
    c.rows.push_back(row("Starter home",199));
    for(const auto size:{glm::uvec2(640,480),glm::uvec2(960,600),glm::uvec2(1920,1080)})
    for(float scale:{1.f,1.25f,1.5f})for(size_t selected=0;selected<c.rows.size();++selected) {
        SCOPED_TRACE(::testing::Message()<<size.x<<"x"<<size.y<<" scale "<<scale<<" selected "<<selected);
        c.textScale=scale;c.selectedRow=selected;
        const auto layout=layoutAdventureHud(c,size.x,size.y);bounded(layout,size.x,size.y);EXPECT_TRUE(layout.selectedVisible);
        EXPECT_TRUE(std::any_of(layout.hits.begin(),layout.hits.end(),[&](const auto& hit){return hit.row==selected&&hit.intent==c.rows[selected].intent;}));
    }
}
TEST(AdventureHud, FocusedModesReplaceQuickbarAndKeepSelectedDisabledChoicesVisible) {
    AdventureHudContent c;c.title="Moss / Builder";
    c.menuText="A sheltered bed, a chest and a workbench make a useful home. A home you already built counts.";
    c.menuStatus="Add a roof above your bed.";c.quickActions={row("Must not appear",2000)};
    c.buildControls={row("Previous",1900),row("Next",1901),row("Close",1902)};
    for(size_t i=0;i<32;++i){auto choice=row("Meaningful choice "+std::to_string(i),300+static_cast<uint32_t>(i));choice.enabled=i%2==0;c.rows.push_back(choice);}
    for(auto mode:{AdventureHudMode::Dialogue,AdventureHudMode::Workbench,AdventureHudMode::Chest,AdventureHudMode::Journal,AdventureHudMode::Pause,AdventureHudMode::Bag})
    for(const auto size:{glm::uvec2(480,360),glm::uvec2(640,480),glm::uvec2(1280,800)})
    for(float scale:{1.f,1.25f,1.5f})for(size_t selected:{size_t{0},size_t{17},size_t{31}}) {
        c.mode=mode;c.textScale=scale;c.selectedRow=selected;
        const auto layout=layoutAdventureHud(c,size.x,size.y);bounded(layout,size.x,size.y);EXPECT_TRUE(layout.selectedVisible);
        const auto hit=std::find_if(layout.hits.begin(),layout.hits.end(),[&](const auto& h){return h.row==selected;});
        ASSERT_NE(hit,layout.hits.end());EXPECT_EQ(hit->enabled,c.rows[selected].enabled);EXPECT_EQ(hit->intent,c.rows[selected].intent);
        EXPECT_FALSE(std::any_of(layout.hits.begin(),layout.hits.end(),[](const auto& h){return h.intent==2000;}));
    }
}
TEST(AdventureHud, CompleteActualGuideCardsRetainTheirTailAndNavigationAtEveryTextSize) {
    using namespace game::adventure;
    bool sawParagraph=false;
    for(int profile=0;profile<3;++profile)for(bool gamepad:{false,true}) {
        AdventurePreferences preferences;
        preferences.orbitToggle=profile!=0;
        if(profile==2){preferences.combat[0]={67,2,7};preferences.combat[1]={72,0,15};}
        for(uint8_t topic=0;topic<static_cast<uint8_t>(AdventureGuideTopic::Count);++topic) {
            const auto card=adventureGuideCard(static_cast<AdventureGuideTopic>(topic),preferences,gamepad);
            sawParagraph=sawParagraph||card.text.find('\n')!=std::string::npos;
            AdventureHudContent c;c.mode=AdventureHudMode::Guide;c.title=card.title;c.menuText=card.text;
            c.rows={row("Next tip",801),row("Previous tip",802),row("All topics",803),row("Return to adventure",804)};
            c.quickActions={row("Unrelated quickbar",900)};c.buildControls={row("Generic previous",901),row("Generic close",902)};
            for(const auto size:{glm::uvec2(480,480),glm::uvec2(640,480),glm::uvec2(1280,800)})
            for(float scale:{1.f,1.25f,1.5f})for(size_t selected=0;selected<c.rows.size();++selected) {
                SCOPED_TRACE(::testing::Message()<<card.title<<" profile "<<profile<<" pad "<<gamepad<<" "<<size.x<<"x"<<size.y<<" scale "<<scale<<" selected "<<selected);
                c.textScale=scale;c.selectedRow=selected;c.rows[1].enabled=false;
                const auto layout=layoutAdventureHud(c,size.x,size.y);bounded(layout,size.x,size.y);
                EXPECT_EQ(layout.canvas.bodyPixels,20*scale);completeGuideBody(layout,card.text,20*scale);
                ASSERT_EQ(layout.panels.size(),1u);EXPECT_TRUE(layout.selectedVisible);
                const auto chosen=std::find_if(layout.hits.begin(),layout.hits.end(),[&](const auto& hit){return hit.row==selected;});
                ASSERT_NE(chosen,layout.hits.end());EXPECT_EQ(chosen->intent,c.rows[selected].intent);EXPECT_EQ(chosen->enabled,c.rows[selected].enabled);
                for(const auto& hit:layout.hits){EXPECT_LT(hit.row,c.rows.size());EXPECT_LT(hit.intent,900u);}
            }
        }
    }
    EXPECT_TRUE(sawParagraph);
}
TEST(AdventureHud, GuideTopicPickerKeepsEverySelectionAndDoesNotBorrowGenericControls) {
    using namespace game::adventure;
    AdventureHudContent c;c.mode=AdventureHudMode::Guide;c.title="How to play";
    c.menuText="Choose a short tip. Read at your own pace.";c.menuStatus="Six topics";
    for(uint8_t topic=0;topic<static_cast<uint8_t>(AdventureGuideTopic::Count);++topic)
        c.rows.push_back(row(adventureGuideCard(static_cast<AdventureGuideTopic>(topic),{},false).title,1000u+static_cast<uint32_t>(topic)));
    c.rows.push_back(row("Return to building",1006));
    c.buildControls={row("Unrelated bottom control",2000)};
    for(const auto size:{glm::uvec2(480,360),glm::uvec2(480,480),glm::uvec2(640,480),glm::uvec2(1280,800)})
    for(float scale:{1.f,1.25f,1.5f})for(size_t selected=0;selected<c.rows.size();++selected) {
        SCOPED_TRACE(::testing::Message()<<size.x<<"x"<<size.y<<" scale "<<scale<<" selected "<<selected);
        c.textScale=scale;c.selectedRow=selected;
        const auto layout=layoutAdventureHud(c,size.x,size.y);bounded(layout,size.x,size.y);
        completeGuideBody(layout,c.menuText,20*scale);EXPECT_TRUE(layout.selectedVisible);
        const auto chosen=std::find_if(layout.hits.begin(),layout.hits.end(),[&](const auto& hit){return hit.row==selected;});
        ASSERT_NE(chosen,layout.hits.end());EXPECT_EQ(chosen->intent,c.rows[selected].intent);
        for(const auto& hit:layout.hits)EXPECT_LT(hit.intent,2000u);
    }
    c.menuText=std::string(1024,'W');const auto oversized=layoutAdventureHud(c,640,480);
    bounded(oversized,640,480);EXPECT_FALSE(oversized.guideBodyComplete);
}
TEST(AdventureHud, CompactBuildKeepsItsTrayBelowTheAimAndAllInputsReachable) {
    AdventureHudContent c;c.mode=AdventureHudMode::Build;c.selected="Wall";c.cost="4 wood";c.status="Ready to place";c.pieceKind=3;
    c.buildControls={row("Pieces"),row("Rotate"),row("Raise"),row("Lower"),row("Remove"),row("Undo"),row("Help"),row("Done")};
    for(const auto size:{glm::uvec2(640,480),glm::uvec2(1280,800)})for(float scale:{1.f,1.25f,1.5f}) {
        c.textScale=scale;const auto layout=layoutAdventureHud(c,size.x,size.y);bounded(layout,size.x,size.y);
        ASSERT_EQ(layout.panels.size(),1u);EXPECT_GT(layout.panels[0].y,static_cast<float>(size.y)*.45f);
        EXPECT_EQ(layout.hits.size(),c.buildControls.size());EXPECT_TRUE(layout.selectedVisible);
    }
}
TEST(AdventureHud, CreativeGuideUsesItsMeasuredFontForEveryBodyGlyph) {
    auto content=creativeContent();content.mode=AdventureHudMode::Guide;
    content.title="Building";content.menuText="Choose a piece. R rotates it.\nUse the colour wheel to paint your next brick.";
    content.rows={row("Next tip",2101),row("Previous tip",2103),row("Return to building",2107)};
    content.selectedRow=2;content.buildControls.clear();
    for(float scale:{1.f,1.25f,1.5f}) {
        content.textScale=scale;const auto layout=layoutAdventureHud(content,640,480);
        bounded(layout,640,480);completeGuideBody(layout,content.menuText,20*scale,true);
        EXPECT_TRUE(layout.selectedVisible);
    }
}
TEST(AdventureHud, CreativeLayoutsRemainBoundedAndKeepRuntimeActionIdentityAtSmallSizes) {
    for(const auto size:{glm::uvec2(320,240),glm::uvec2(320,740),glm::uvec2(480,800),glm::uvec2(844,390),glm::uvec2(1600,900)})
    for(float scale:{1.f,1.25f,1.5f})for(bool contrast:{false,true})
    for(const auto mode:{AdventureHudMode::Explore,AdventureHudMode::Build,AdventureHudMode::Catalog,AdventureHudMode::Pause}) {
        SCOPED_TRACE(::testing::Message()<<size.x<<"x"<<size.y<<" scale "<<scale<<" contrast "<<contrast<<" mode "<<int(mode));
        auto content=creativeContent();
        content.mode=mode;content.textScale=scale;content.highContrast=contrast;
        if(mode==AdventureHudMode::Catalog||mode==AdventureHudMode::Pause)
            content.buildControls={creativeAction("Previous",25,-1,2001),creativeAction("Next",25,1,2003),creativeAction("Close",20,0,2007)};
        const auto layout=layoutAdventureHud(content,size.x,size.y);
        bounded(layout,size.x,size.y);identitiesComeFromContent(layout,content);
        ASSERT_FALSE(layout.hits.empty());
        if(mode!=AdventureHudMode::Explore){EXPECT_TRUE(layout.selectedVisible);}
        if(mode==AdventureHudMode::Build) {
            EXPECT_TRUE(std::any_of(layout.hits.begin(),layout.hits.end(),[&](const auto& hit){return sameIdentity(hit,content.topActions.back());}));
        }
    }
}
TEST(AdventureHud, CreativeSelectionStaysReachableWhenItsShelfMustPage) {
    auto content=creativeContent();
    for(const auto size:{glm::uvec2(320,240),glm::uvec2(320,740),glm::uvec2(1600,900)})
    for(float scale:{1.f,1.5f})for(size_t selected=0;selected<content.rows.size();++selected)
    for(const auto mode:{AdventureHudMode::Build,AdventureHudMode::Catalog}) {
        SCOPED_TRACE(::testing::Message()<<size.x<<"x"<<size.y<<" scale "<<scale<<" selected "<<selected<<" mode "<<int(mode));
        content.mode=mode;content.textScale=scale;content.selectedRow=selected;
        content.pieceKind=content.rows[selected].pieceKind;content.selected=content.rows[selected].label;
        // Selecting any catalog piece replaces the active session preset.
        content.hotbar[0].pieceKind=content.pieceKind;content.hotbar[0].label=content.selected;
        content.rows[selected].enabled=false;
        const auto layout=layoutAdventureHud(content,size.x,size.y);bounded(layout,size.x,size.y);
        ASSERT_TRUE(layout.selectedVisible);
        const auto hotbar=std::find_if(content.hotbar.begin(),content.hotbar.end(),[&](const auto& item){return item.pieceKind==content.pieceKind;});
        ASSERT_NE(hotbar,content.hotbar.end());
        const auto& source=mode==AdventureHudMode::Build?*hotbar:content.rows[selected];
        EXPECT_TRUE(std::any_of(layout.hits.begin(),layout.hits.end(),[&](const auto& hit){return sameIdentity(hit,source);}));
        content.rows[selected].enabled=true;
    }
}
TEST(AdventureHud, CreativeColourPickerKeepsEverySwatchIdentityAndClearsWhenWalking) {
    auto content=creativeContent();content.colourPickerOpen=true;
    for(const auto size:{glm::uvec2(320,240),glm::uvec2(320,740),glm::uvec2(844,390),glm::uvec2(1600,900)})
    for(float scale:{1.f,1.25f,1.5f})for(size_t selected=0;selected<content.colours.size();++selected) {
        SCOPED_TRACE(::testing::Message()<<size.x<<"x"<<size.y<<" scale "<<scale<<" selected "<<selected);
        content.textScale=scale;content.selectedRow=selected;content.paint=static_cast<uint32_t>(content.colours[selected].value);
        const auto layout=layoutAdventureHud(content,size.x,size.y);bounded(layout,size.x,size.y);
        identitiesComeFromContent(layout,content);EXPECT_TRUE(layout.selectedVisible);
        for(const auto& colour:content.colours)
            EXPECT_EQ(std::count_if(layout.hits.begin(),layout.hits.end(),[&](const auto& hit){return sameIdentity(hit,colour);}),1);
        EXPECT_LE(layout.thumbnailCount,7u);
        EXPECT_TRUE(layout.triangles.empty());EXPECT_EQ(creativeSprites(layout).size(),layout.thumbnailCount);
    }
    content.mode=AdventureHudMode::Explore;
    const auto explore=layoutAdventureHud(content,1600,900);bounded(explore,1600,900);
    EXPECT_EQ(explore.thumbnailCount,0u);EXPECT_TRUE(explore.triangles.empty());
    for(const auto& hit:explore.hits) {
        EXPECT_FALSE(std::any_of(content.hotbar.begin(),content.hotbar.end(),[&](const auto& item){return sameIdentity(hit,item);}));
        EXPECT_FALSE(std::any_of(content.colours.begin(),content.colours.end(),[&](const auto& item){return sameIdentity(hit,item);}));
    }
}
TEST(AdventureHud, CreativeRadialCatalogHandlesEveryCategorySizeWithoutOverlappingTargets) {
    for(const auto size:{glm::uvec2(320,740),glm::uvec2(1600,900)})
    for(float scale:{1.f,1.25f,1.5f})for(size_t count=1;count<=9;++count)for(size_t selected=0;selected<count;++selected) {
        SCOPED_TRACE(::testing::Message()<<size.x<<"x"<<size.y<<" scale "<<scale<<" count "<<count<<" selected "<<selected);
        auto content=creativeContent();content.mode=AdventureHudMode::Catalog;content.textScale=scale;
        content.rows.resize(count);content.selectedRow=selected;
        content.buildControls={creativeAction("Previous",25,-1,2001),creativeAction("Next",25,1,2003),creativeAction("Close",20,0,2007)};
        const auto layout=layoutAdventureHud(content,size.x,size.y);bounded(layout,size.x,size.y);
        const auto shelf=static_cast<size_t>(std::count_if(layout.hits.begin(),layout.hits.end(),[](const auto& hit){return hit.shortcutKey!=0;}));
        EXPECT_GT(shelf,0u);EXPECT_LE(shelf,6u);
        EXPECT_EQ(layout.thumbnailCount,shelf+std::min(count,size_t{5}));EXPECT_TRUE(layout.selectedVisible);
        EXPECT_TRUE(layout.triangles.empty());EXPECT_EQ(creativeSprites(layout).size(),layout.thumbnailCount);
        EXPECT_TRUE(std::any_of(layout.hits.begin(),layout.hits.end(),[&](const auto& hit){return hit.row==selected&&sameIdentity(hit,content.rows[selected]);}));
    }
}
TEST(AdventureHud, CreativeBuildAndExploreLeaveTheAimingRegionUncovered) {
    auto content=creativeContent();
    for(const auto size:{glm::uvec2(640,480),glm::uvec2(1600,900)})
    for(const auto mode:{AdventureHudMode::Explore,AdventureHudMode::Build}) {
        content.mode=mode;const auto layout=layoutAdventureHud(content,size.x,size.y);
        const glm::vec4 aim{static_cast<float>(size.x)*.4f,static_cast<float>(size.y)*.3f,
            static_cast<float>(size.x)*.2f,static_cast<float>(size.y)*.3f};
        const auto overlaps=[&](glm::vec4 box){return box.x<aim.x+aim.z&&box.x+box.z>aim.x&&box.y<aim.y+aim.w&&box.y+box.w>aim.y;};
        for(const auto& hit:layout.hits)EXPECT_FALSE(overlaps(hit.bounds));
        for(const auto& panel:layout.panels)EXPECT_FALSE(overlaps(panel));
    }
}
TEST(AdventureHud, CreativePaintSelectsTintedSpritesWithoutChangingTheirArtOrGeometry) {
    auto content=creativeContent();content.paint=0;content.hotbar[0].paint=0;
    const auto original=creativeSprites(layoutAdventureHud(content,1600,900));ASSERT_EQ(original.size(),6u);
    content.paint=0xff0000;content.hotbar[0].paint=content.paint;
    const auto tintedLayout=layoutAdventureHud(content,1600,900);const auto painted=creativeSprites(tintedLayout);
    EXPECT_TRUE(tintedLayout.triangles.empty());ASSERT_EQ(original.size(),painted.size());
    for(size_t i=0;i<original.size();++i) {
        EXPECT_EQ(original[i].bounds,painted[i].bounds);
        EXPECT_EQ(painted[i].uv.x,original[i].uv.x+(i==0?2.f:0.f));
        for(int component=1;component<4;++component)EXPECT_EQ(painted[i].uv[component],original[i].uv[component]);
        EXPECT_EQ(painted[i].color,i==0?glm::vec4(1,0,0,1):original[i].color);
    }
    content.paint=0;content.hotbar[0].paint=0;const auto restored=creativeSprites(layoutAdventureHud(content,1600,900));
    ASSERT_EQ(original.size(),restored.size());
    for(size_t i=0;i<original.size();++i) {
        EXPECT_EQ(original[i].bounds,restored[i].bounds);EXPECT_EQ(original[i].uv,restored[i].uv);EXPECT_EQ(original[i].color,restored[i].color);
    }
}
TEST(AdventureHud, NumberKeysReferToTheExactVisibleSlotsAndPickerRetainsTheirDisabledPeers) {
    for(const auto size:{glm::uvec2(320,740),glm::uvec2(844,390),glm::uvec2(1600,900)})
    for(size_t selected=0;selected<6;++selected) {
        auto content=creativeContent();content.pieceKind=content.hotbar[selected].pieceKind;content.paint=content.hotbar[selected].paint;
        content.quickSlot=static_cast<uint8_t>(selected+1);
        for(const auto mode:{AdventureHudMode::Build,AdventureHudMode::Catalog}) {
            content.mode=mode;
            if(mode==AdventureHudMode::Catalog) {
                for(auto& item:content.hotbar)item.enabled=false;
                content.buildControls={creativeAction("Previous",25,-1,2001),creativeAction("Next",25,1,2003),creativeAction("Close",20,0,2007)};
            }
            const auto layout=layoutAdventureHud(content,size.x,size.y);bounded(layout,size.x,size.y);
            EXPECT_TRUE(layout.panels.empty());EXPECT_TRUE(layout.triangles.empty());
            size_t slots=0;float previousX=-1;bool selectedVisible=false;
            for(const auto& hit:layout.hits)if(hit.shortcutKey) {
                EXPECT_GT(hit.bounds.x,previousX);previousX=hit.bounds.x;++slots;
                const auto source=std::find_if(content.hotbar.begin(),content.hotbar.end(),[&](const auto& item){return sameIdentity(hit,item);});
                ASSERT_NE(source,content.hotbar.end());EXPECT_EQ(hit.row,SIZE_MAX);
                EXPECT_EQ(hit.shortcutKey,static_cast<uint32_t>('0'+source->value));
                EXPECT_EQ(hit.enabled,mode==AdventureHudMode::Build);
                selectedVisible=selectedVisible||source->pieceKind==content.pieceKind;
            }
            EXPECT_GE(slots,3u);EXPECT_LE(slots,6u);EXPECT_TRUE(selectedVisible);
            EXPECT_EQ(creativeSprites(layout).size(),layout.thumbnailCount);
        }
    }
}
TEST(AdventureHud, DuplicatePresetsHighlightTheChosenSlotRatherThanTheFirstMatchingPiece) {
    auto content=creativeContent();content.hotbar[5].pieceKind=content.hotbar[0].pieceKind;content.hotbar[5].paint=content.hotbar[0].paint;
    content.quickSlot=6;
    const auto layout=layoutAdventureHud(content,1600,900);
    const auto selected=std::find_if(layout.hits.begin(),layout.hits.end(),[](const auto& hit){return hit.action==39&&hit.value==6;});
    ASSERT_NE(selected,layout.hits.end());size_t selectedBorders=0;
    for(size_t i=0;i<layout.canvas.count;++i) {
        const auto& quad=layout.canvas.quads[i];
        if(quad.uv.x<=-3&&quad.color.r>.95f&&quad.color.g>.75f&&quad.color.b<.3f) {
            EXPECT_EQ(quad.bounds.x,selected->bounds.x);EXPECT_EQ(quad.bounds.z,selected->bounds.z);++selectedBorders;
        }
    }
    EXPECT_EQ(selectedBorders,1u);
}
TEST(AdventureHud, NavigationBlocksOnlyItsVisiblePortraitAndActualMapBounds) {
    auto content=creativeContent();AdventureHudNavigation navigation;navigation.visible=true;
    const auto portraitOnly=layoutAdventureNavigation(navigation,content,1600,900);ASSERT_EQ(portraitOnly.menuHits.size(),1u);
    navigation.map=std::make_shared<AdventureHudMap>();
    const auto withMap=layoutAdventureNavigation(navigation,content,1600,900);ASSERT_EQ(withMap.menuHits.size(),2u);
    EXPECT_FALSE(withMap.truncated);EXPECT_LE(withMap.count,CoveHudLayout::maximumQuads);
    for(const auto& hit:withMap.menuHits) {
        EXPECT_GE(hit.bounds.z,44);EXPECT_GE(hit.bounds.w,44);EXPECT_EQ(hit.row,-1);EXPECT_EQ(hit.key,-1);
        EXPECT_GE(hit.bounds.x,0);EXPECT_GE(hit.bounds.y,0);EXPECT_LE(hit.bounds.x+hit.bounds.z,1600);EXPECT_LE(hit.bounds.y+hit.bounds.w,900);
    }
    for(const auto size:{glm::uvec2(768,600),glm::uvec2(960,720),glm::uvec2(1280,600)}) {
        const auto navigationLayout=layoutAdventureNavigation(navigation,content,size.x,size.y);
        const auto controls=layoutAdventureHud(content,size.x,size.y);
        ASSERT_EQ(navigationLayout.menuHits.size(),2u);
        for(const auto& reserved:navigationLayout.menuHits)for(const auto& hit:controls.hits) {
            const auto a=reserved.bounds,b=hit.bounds;
            EXPECT_FALSE(a.x<b.x+b.z&&a.x+a.z>b.x&&a.y<b.y+b.w&&a.y+a.w>b.y);
        }
    }
    content.pixelScale=2;const auto dense=layoutAdventureNavigation(navigation,content,3200,1800);
    ASSERT_EQ(dense.menuHits.size(),2u);
    for(size_t i=0;i<dense.menuHits.size();++i)for(int component=0;component<4;++component)
        EXPECT_NEAR(dense.menuHits[i].bounds[component],withMap.menuHits[i].bounds[component]*2,.001f);
    content.mode=AdventureHudMode::Pause;EXPECT_TRUE(layoutAdventureNavigation(navigation,content,3200,1800).menuHits.empty());
    content.mode=AdventureHudMode::Build;content.pixelScale=1;EXPECT_TRUE(layoutAdventureNavigation(navigation,content,320,740).menuHits.empty());
}
TEST(AdventureHud, CreativePixelDensityPreservesLogicalGeometryTextAndHitIdentity) {
    for(const auto size:{glm::uvec2(320,240),glm::uvec2(320,568),glm::uvec2(1600,900)})
    for(const auto mode:{AdventureHudMode::Explore,AdventureHudMode::Build,AdventureHudMode::Catalog,AdventureHudMode::Pause,AdventureHudMode::Guide})
    for(bool contrast:{false,true}) {
        auto content=creativeContent();content.mode=mode;content.highContrast=contrast;content.textScale=1.25f;
        if(mode==AdventureHudMode::Catalog||mode==AdventureHudMode::Pause)
            content.buildControls={creativeAction("Previous",25,-1,2001),creativeAction("Next",25,1,2003),creativeAction("Close",20,0,2007)};
        if(mode==AdventureHudMode::Guide) {
            content.title="Building";content.menuText="Choose a piece. R rotates it.";content.selectedRow=0;
            content.rows={row("Next tip",2101),row("Previous tip",2103),row("Return",2107)};content.buildControls.clear();
        }
        const auto initial=layoutAdventureHud(content,size.x,size.y);
        if(!initial.hits.empty()){content.hoverLabel=initial.hits.front().label;content.hoverBounds=initial.hits.front().bounds;}
        const auto logical=layoutAdventureHud(content,size.x,size.y);bounded(logical,size.x,size.y);
        for(float density:{1.5f,2.f}) {
            SCOPED_TRACE(::testing::Message()<<size.x<<"x"<<size.y<<" mode "<<int(mode)<<" contrast "<<contrast<<" density "<<density);
            auto scaledContent=content;scaledContent.pixelScale=density;scaledContent.hoverBounds*=density;
            const auto physicalWidth=static_cast<uint32_t>(static_cast<float>(size.x)*density);
            const auto physicalHeight=static_cast<uint32_t>(static_cast<float>(size.y)*density);
            const auto scaled=layoutAdventureHud(scaledContent,physicalWidth,physicalHeight);bounded(scaled,physicalWidth,physicalHeight);
            const auto sameRect=[&](glm::vec4 actual,glm::vec4 expected){for(int i=0;i<4;++i)EXPECT_NEAR(actual[i],expected[i]*density,.003f);};
            EXPECT_NEAR(scaled.canvas.bodyPixels,logical.canvas.bodyPixels*density,.001f);
            EXPECT_EQ(scaled.selectedVisible,logical.selectedVisible);EXPECT_EQ(scaled.thumbnailCount,logical.thumbnailCount);
            ASSERT_EQ(scaled.canvas.count,logical.canvas.count);ASSERT_EQ(scaled.hits.size(),logical.hits.size());
            ASSERT_EQ(scaled.triangles.size(),logical.triangles.size());ASSERT_EQ(scaled.panels.size(),logical.panels.size());
            for(size_t i=0;i<logical.canvas.count;++i) {
                const auto& original=logical.canvas.quads[i];const auto& highDensity=scaled.canvas.quads[i];
                sameRect(highDensity.bounds,original.bounds);
                for(int j=0;j<4;++j)EXPECT_EQ(highDensity.color[j],original.color[j]);
                if(original.uv.x<0) {
                    // The analytic round-rectangle radius and extent are pixel
                    // distances. Scaling positions alone would square corners.
                    if(original.uv.x<=-3)EXPECT_NEAR(highDensity.uv.x,-3-(-3-original.uv.x)*density,.003f);
                    else EXPECT_EQ(highDensity.uv.x,original.uv.x);
                    for(int j=1;j<4;++j)EXPECT_NEAR(highDensity.uv[j],original.uv[j]*density,.003f);
                } else for(int j=0;j<4;++j)EXPECT_EQ(highDensity.uv[j],original.uv[j]);
            }
            for(size_t i=0;i<logical.hits.size();++i) {
                const auto& a=scaled.hits[i];const auto& b=logical.hits[i];sameRect(a.bounds,b.bounds);
                EXPECT_EQ(a.action,b.action);EXPECT_EQ(a.value,b.value);EXPECT_EQ(a.intent,b.intent);
                EXPECT_EQ(a.row,b.row);EXPECT_EQ(a.enabled,b.enabled);EXPECT_EQ(a.label,b.label);EXPECT_EQ(a.shortcutKey,b.shortcutKey);
                EXPECT_GE(a.bounds.z/density,44-.001f);EXPECT_GE(a.bounds.w/density,44-.001f);
            }
            for(size_t i=0;i<logical.triangles.size();++i) {
                for(size_t j=0;j<6;++j)EXPECT_NEAR(scaled.triangles[i].xy[j],logical.triangles[i].xy[j]*density,.003f);
                EXPECT_EQ(scaled.triangles[i].rgba,logical.triangles[i].rgba);
            }
            for(size_t i=0;i<logical.panels.size();++i)sameRect(scaled.panels[i],logical.panels[i]);
            sameRect(scaled.canvas.panel,logical.canvas.panel);sameRect(scaled.guideBodyBounds,logical.guideBodyBounds);
            EXPECT_EQ(scaled.guideBodyComplete,logical.guideBodyComplete);
            EXPECT_EQ(scaled.guideBodyFirstQuad,logical.guideBodyFirstQuad);EXPECT_EQ(scaled.guideBodyQuadCount,logical.guideBodyQuadCount);
        }
    }
}
TEST(AdventureHud, FractionalFramebufferRoundingCannotHideA320CssPixelHud) {
    struct Viewport {uint32_t width,height,cssWidth,cssHeight;};
    // Independent rounding of width/height at 125% produces unequal axis
    // ratios. Both truncation and nearest rounding can lose the minimum width.
    for(const auto size:{Viewport{400,709,320,567},Viewport{400,303,320,242}})
    for(float textScale:{1.f,1.5f})
    for(const auto mode:{AdventureHudMode::Explore,AdventureHudMode::Build,AdventureHudMode::Catalog})
    for(bool palette:{false,true}) {
        if(palette&&mode!=AdventureHudMode::Build)continue;
        SCOPED_TRACE(::testing::Message()<<size.width<<"x"<<size.height<<" / "<<size.cssWidth<<"x"<<size.cssHeight
            <<" mode "<<int(mode)<<" text "<<textScale<<" palette "<<palette);
        auto content=creativeContent();content.mode=mode;content.textScale=textScale;content.colourPickerOpen=palette;
        const float xScale=static_cast<float>(size.width)/static_cast<float>(size.cssWidth);
        const float yScale=static_cast<float>(size.height)/static_cast<float>(size.cssHeight);
        content.pixelScale=std::max(xScale,yScale);
        if(mode==AdventureHudMode::Catalog||palette)
            content.buildControls={creativeAction("Previous",25,-1,2001),creativeAction("Next",25,1,2003),creativeAction("Close",20,0,2007)};
        const auto layout=layoutAdventureHud(content,size.width,size.height);
        bounded(layout,size.width,size.height);ASSERT_FALSE(layout.hits.empty());ASSERT_GT(layout.canvas.count,0u);
        if(mode!=AdventureHudMode::Explore){EXPECT_TRUE(layout.selectedVisible);}
        for(const auto& hit:layout.hits) {
            EXPECT_GE(hit.bounds.z/xScale,44-.001f);EXPECT_GE(hit.bounds.w/yScale,44-.001f);
            EXPECT_LE((hit.bounds.x+hit.bounds.z)/xScale,static_cast<float>(size.cssWidth)+.001f);
            EXPECT_LE((hit.bounds.y+hit.bounds.w)/yScale,static_cast<float>(size.cssHeight)+.001f);
        }
    }
}
TEST(AdventureHud, SharedAtlasTextClipsGeometryAndUsesMeasuredEllipsis) {
    CoveHudLayout canvas;const glm::vec4 bounds{10,10,95,39};
    const auto text=appendCoveHudText(canvas,"An exceptionally long action",bounds,30,{1,1,1,1});
    EXPECT_TRUE(text.clipped);EXPECT_EQ(text.lines,1u);EXPECT_GT(canvas.count,3u);
    for(size_t i=0;i<canvas.count;++i) {
        const auto b=canvas.quads[i].bounds;
        EXPECT_GE(b.x,bounds.x);EXPECT_GE(b.y,bounds.y);
        EXPECT_LE(b.x+b.z,bounds.x+bounds.z+.001f);EXPECT_LE(b.y+b.w,bounds.y+bounds.w+.001f);
    }
    EXPECT_GT(measureCoveHudText("WWW",30),measureCoveHudText("iii",30));
}
TEST(AdventureHud, AdventureTypographyIsMeasuredClippedAndBoundedIndependentlyOfLegacyAtlas) {
    for(float pixels:{18.f,22.5f,27.f}) {
        CoveHudLayout canvas;const glm::vec4 bounds{12,8,120,pixels*1.3f};
        const auto result=appendAdventureHudText(canvas,"Choose another building piece",bounds,pixels,{1,1,1,1});
        EXPECT_TRUE(result.clipped);EXPECT_EQ(result.lines,1u);EXPECT_GT(canvas.count,3u);EXPECT_FALSE(canvas.truncated);
        for(size_t i=0;i<canvas.count;++i) {
            const auto& q=canvas.quads[i];
            EXPECT_GE(q.bounds.x,bounds.x);EXPECT_GE(q.bounds.y,bounds.y);
            EXPECT_LE(q.bounds.x+q.bounds.z,bounds.x+bounds.z+.001f);EXPECT_LE(q.bounds.y+q.bounds.w,bounds.y+bounds.w+.001f);
            for(int component=0;component<4;++component)EXPECT_TRUE(std::isfinite(q.uv[component]));
        }
        EXPECT_GT(measureAdventureHudText("WWW",pixels),measureAdventureHudText("iii",pixels));
        EXPECT_NEAR(measureAdventureHudText("Build",pixels*2),measureAdventureHudText("Build",pixels)*2,.001f);
    }
}
TEST(AdventureHud, MeshThumbnailsHaveCompleteVersionedGeometryAndBrowserSourceIdentity) {
    namespace thumbnails=adventure_thumbnails;
    EXPECT_EQ(thumbnails::recipe,"adventure-installed-mesh-thumbnails-r03");
    EXPECT_EQ(thumbnails::sourceSha256,"08ce2ff933bdc9d86d68d62e29b15f62d7f22984753f20b51fdb89348fd3236b");
    EXPECT_EQ(core::sha256Hex(core::sha256(std::as_bytes(std::span(thumbnails::triangles)))),thumbnails::packedSha256);
    EXPECT_EQ(thumbnails::doorSourceSha256,"be59bb6888aacc94c345fa3aebf0ebf1df1cacc1c3082e55c01eff16753b1e5f");
    ASSERT_EQ(thumbnails::pieces.size(),15u);ASSERT_EQ(thumbnails::triangles.size(),3686u);
    // Upright projection retains triangle ordering; the composed door
    // appends after the original 3350 triangles.
    EXPECT_EQ(thumbnails::pieces[14].first,3350u);
    EXPECT_EQ(thumbnails::pieces[14].mesh,"15_hinged_door");
    size_t next=0;std::set<uint32_t> colors;
    for(const auto& piece:thumbnails::pieces) {
        EXPECT_EQ(piece.first,next);EXPECT_GT(piece.count,0u);EXPECT_LE(piece.count,thumbnails::maximumPieceTriangles);
        ASSERT_LE(size_t{piece.first}+piece.count,thumbnails::triangles.size());
        EXPECT_FALSE(piece.mesh.empty());next+=piece.count;
    }
    EXPECT_EQ(next,thumbnails::triangles.size());
    for(const auto& triangle:thumbnails::triangles) {
        for(size_t i=0;i<3;++i){EXPECT_LE(triangle.xy[i*2],thumbnails::width*thumbnails::units);EXPECT_LE(triangle.xy[i*2+1],thumbnails::height*thumbnails::units);}
        EXPECT_EQ(triangle.rgba>>24,255u);colors.insert(triangle.rgba);
    }
    EXPECT_GT(colors.size(),20u); // Authored material and face lighting, not one proxy silhouette.
}
TEST(AdventureHud, WorstVisibleMeshPageFitsDedicatedBufferAndNeverCoversLabelsOrHitBorders) {
    AdventureHudContent content;content.mode=AdventureHudMode::Catalog;
    content.buildControls={row("Previous",2000),row("Next",2001),row("Close",2002)};
    for(uint32_t i=0;i<64;++i){auto item=row("Brick 2 x 4",100+i);item.pieceKind=10;item.detail="8 stone";item.enabled=i%2==0;content.rows.push_back(item);}
    for(const auto size:{glm::uvec2(480,360),glm::uvec2(640,480),glm::uvec2(1920,1080)})
    for(float scale:{1.f,1.25f,1.5f}) {
        content.textScale=scale;content.selectedRow=31;
        const auto layout=layoutAdventureHud(content,size.x,size.y);bounded(layout,size.x,size.y);
        std::vector<AdventureHudHit> cards;
        for(const auto& hit:layout.hits)if(hit.row!=SIZE_MAX)cards.push_back(hit);
        EXPECT_EQ(layout.thumbnailCount,cards.size());EXPECT_EQ(layout.triangles.size(),cards.size()*812);
        ASSERT_LE(layout.triangles.size()*sizeof(AdventureHudTriangle),AdventureHudPath::maximumBufferBytes);
        for(size_t card=0;card<cards.size();++card)for(size_t tri=0;tri<812;++tri)for(size_t vertex=0;vertex<3;++vertex) {
            const auto& triangle=layout.triangles[card*812+tri];const auto& bounds=cards[card].bounds;
            EXPECT_GE(triangle.xy[vertex*2],bounds.x+12);EXPECT_LE(triangle.xy[vertex*2],bounds.x+bounds.z-12);
            EXPECT_GE(triangle.xy[vertex*2+1],bounds.y+9);EXPECT_LE(triangle.xy[vertex*2+1],bounds.y+59);
        }
        const auto chosen=std::find_if(cards.begin(),cards.end(),[](const auto& hit){return hit.row==31;});
        ASSERT_NE(chosen,cards.end());EXPECT_FALSE(chosen->enabled);EXPECT_EQ(chosen->intent,131u);
    }
    EXPECT_EQ(AdventureHudPath::maximumTriangleCount,5684u);EXPECT_EQ(AdventureHudPath::maximumVertexCount,17052u);
    EXPECT_EQ(AdventureHudPath::maximumBufferBytes,159152u);EXPECT_LT(AdventureHudPath::residentBytes,3u*1024u*1024u);
    content.textScale=1;const auto maximum=layoutAdventureHud(content,1920,1080);
    EXPECT_EQ(maximum.triangles.size(),AdventureHudPath::maximumTriangleCount);
}
TEST(AdventureHud, ThumbnailPassRefusesUninitializedUseAndShutdownIsIdempotent) {
    AdventureHudPath path;EXPECT_FALSE(path.initialized());EXPECT_FALSE(path.init(nullptr,nullptr,WGPUTextureFormat_RGBA8Unorm,"missing"));
    EXPECT_FALSE(path.render(nullptr,nullptr,640,480));EXPECT_EQ(path.lastEncodedTriangles(),0u);
    path.shutdown();path.shutdown();EXPECT_FALSE(path.initialized());EXPECT_EQ(path.triangleUploadCount(),0u);
    for(uint8_t kind:{uint8_t{0},uint8_t(game::adventure::kBuildingPieceCount+1),uint8_t{255}}) {
        AdventureHudContent content;content.mode=AdventureHudMode::Build;content.pieceKind=kind;
        const auto layout=layoutAdventureHud(content,640,480);EXPECT_TRUE(layout.triangles.empty());EXPECT_EQ(layout.thumbnailCount,0u);
    }
}
TEST(AdventureHudGpu, ActualMeshColorsSurviveDiscardAndSubmittedShutdownAtMaximumCapacity) {
    gpu::Context context;ASSERT_TRUE(context.initHeadless());
    auto errors=std::make_shared<std::atomic<unsigned>>(0);
    context.setErrorCallback([errors](WGPUErrorType,const char*){++*errors;});
    const auto device=context.getDevice();const auto queue=context.getQueue();
    struct Resources {
        WGPUTexture texture=nullptr;WGPUTextureView view=nullptr;WGPUBuffer readback=nullptr;
        ~Resources(){if(readback)wgpuBufferRelease(readback);if(view)wgpuTextureViewRelease(view);if(texture)wgpuTextureRelease(texture);}
    } owned;
    constexpr uint32_t width=1920,height=480,stride=width*4;
    auto texture=gpu::TextureDesc::renderTarget(width,height,WGPUTextureFormat_RGBA8Unorm,"adventure_thumbnail_test");
    texture.usage|=WGPUTextureUsage_CopySrc;
    owned.texture=gpu::createTexture(device,texture);ASSERT_NE(owned.texture,nullptr);
    owned.view=gpu::createTextureView(owned.texture);ASSERT_NE(owned.view,nullptr);
    owned.readback=gpu::createBuffer(device,{"adventure_thumbnail_readback",stride*height,WGPUBufferUsage_CopyDst|WGPUBufferUsage_MapRead});
    ASSERT_NE(owned.readback,nullptr);
    AdventureHudContent content;content.mode=AdventureHudMode::Catalog;
    for(uint32_t i=0;i<7;++i){auto item=row("Brick 2 x 4",100+i);item.pieceKind=10;item.detail="8 stone";content.rows.push_back(item);}
    AdventureHudPath hud;ASSERT_TRUE(hud.init(device,queue,WGPUTextureFormat_RGBA8Unorm,"shaders/cove_hud.wgsl"));
    hud.setContent(content);
    auto discard=wgpuDeviceCreateCommandEncoder(device,nullptr);ASSERT_NE(discard,nullptr);
    ASSERT_TRUE(hud.render(discard,owned.view,width,height));
    ASSERT_EQ(hud.lastEncodedTriangles(),AdventureHudPath::maximumTriangleCount);
    wgpuCommandEncoderRelease(discard);
    const auto uploads=hud.triangleUploadCount();hud.clearEncodedObservation();EXPECT_EQ(hud.lastEncodedTriangles(),0u);
    auto encoder=wgpuDeviceCreateCommandEncoder(device,nullptr);ASSERT_NE(encoder,nullptr);
    WGPURenderPassColorAttachment attachment{};attachment.view=owned.view;
    attachment.loadOp=WGPULoadOp_Clear;attachment.storeOp=WGPUStoreOp_Store;attachment.clearValue={.02,.03,.04,1};
    WGPURenderPassDescriptor pass{};pass.colorAttachmentCount=1;pass.colorAttachments=&attachment;
    auto clear=wgpuCommandEncoderBeginRenderPass(encoder,&pass);ASSERT_NE(clear,nullptr);
    wgpuRenderPassEncoderEnd(clear);wgpuRenderPassEncoderRelease(clear);
    hud.setContent(content);ASSERT_TRUE(hud.render(encoder,owned.view,width,height));
    EXPECT_EQ(hud.triangleUploadCount(),uploads);EXPECT_EQ(hud.lastEncodedTriangles(),5684u);
    const auto layout=hud.layout();
    WGPUImageCopyTexture source{};source.texture=owned.texture;source.aspect=WGPUTextureAspect_All;
    WGPUImageCopyBuffer destination{};destination.buffer=owned.readback;destination.layout.bytesPerRow=stride;destination.layout.rowsPerImage=height;
    WGPUExtent3D extent{width,height,1};wgpuCommandEncoderCopyTextureToBuffer(encoder,&source,&destination,&extent);
    auto command=wgpuCommandEncoderFinish(encoder,nullptr);ASSERT_NE(command,nullptr);
    wgpuQueueSubmit(queue,1,&command);wgpuCommandBufferRelease(command);wgpuCommandEncoderRelease(encoder);
    hud.shutdown();EXPECT_FALSE(hud.initialized());EXPECT_EQ(hud.lastEncodedTriangles(),0u);
    auto completion=std::make_shared<std::atomic<int>>(0);
    auto* callback=new std::shared_ptr<std::atomic<int>>(completion);
    wgpuBufferMapAsync(owned.readback,WGPUMapMode_Read,0,stride*height,
        [](WGPUBufferMapAsyncStatus status,void* data){
            std::unique_ptr<std::shared_ptr<std::atomic<int>>> result(static_cast<std::shared_ptr<std::atomic<int>>*>(data));
            (*result)->store(status==WGPUBufferMapAsyncStatus_Success?1:2);
        },callback);
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(10);
    while(completion->load()==0&&std::chrono::steady_clock::now()<deadline){context.tick();std::this_thread::sleep_for(std::chrono::milliseconds(1));}
    ASSERT_EQ(completion->load(),1);
    const auto* pixels=static_cast<const uint8_t*>(wgpuBufferGetConstMappedRange(owned.readback,0,stride*height));ASSERT_NE(pixels,nullptr);
    // Independent point-in-triangle reference selects the last visible face.
    // Ignore subpixel edges so the test measures triangle colors and ordering,
    // not a vendor's rasterization tie-break at a shared edge.
    size_t samples=0,card=0;
    for(const auto& hit:layout.hits)if(hit.row!=SIZE_MAX) {
        for(float y=hit.bounds.y+10;y<hit.bounds.y+58;y+=3)for(float x=hit.bounds.x+13;x<hit.bounds.x+hit.bounds.z-13;x+=3) {
            const auto px=static_cast<uint32_t>(x),py=static_cast<uint32_t>(y);
            const float sx=static_cast<float>(px)+.5f,sy=static_cast<float>(py)+.5f;
            bool covered=false,safe=false;uint32_t color=0;
            for(size_t i=card*812;i<(card+1)*812;++i) {
                const auto& triangle=layout.triangles[i];bool positive=false,negative=false;float margin=1000;
                const auto& points=triangle.xy;
                const float area=(points[2]-points[0])*(points[5]-points[1])-(points[3]-points[1])*(points[4]-points[0]);
                if(std::abs(area)<.000001f)continue;
                for(size_t edge=0;edge<3;++edge) {
                    const auto next=(edge+1)%3;
                    const float ax=triangle.xy[edge*2],ay=triangle.xy[edge*2+1];
                    const float dx=triangle.xy[next*2]-ax,dy=triangle.xy[next*2+1]-ay;
                    const float cross=dx*(sy-ay)-dy*(sx-ax);
                    positive=positive||cross>0;negative=negative||cross<0;
                    margin=std::min(margin,std::abs(cross)/std::max(.0001f,std::hypot(dx,dy)));
                }
                if(positive&&negative)continue;
                covered=true;safe=margin>.75f;color=triangle.rgba;
            }
            if(!covered||!safe)continue;
            const auto* actual=pixels+(py*width+px)*4;
            EXPECT_NEAR(actual[0],color&255u,1);EXPECT_NEAR(actual[1],(color>>8)&255u,1);EXPECT_NEAR(actual[2],(color>>16)&255u,1);
            ++samples;
        }
        ++card;
    }
    RecordProperty("matchedMeshColorSamples",static_cast<int>(samples));RecordProperty("triangleInstances",5684);
    RecordProperty("maximumBufferBytes",159152);EXPECT_GT(samples,50u);EXPECT_EQ(card,7u);
    const auto* untouched=pixels+(50*width+width/2)*4;
    EXPECT_NEAR(untouched[0],5,1);EXPECT_NEAR(untouched[1],8,1);EXPECT_NEAR(untouched[2],10,1);
    wgpuBufferUnmap(owned.readback);context.tick();EXPECT_EQ(errors->load(),0u);context.setErrorCallback({});
}
TEST(AdventureHudGpu, CreativeContentCachesUploadsAndCannotKeepStaleBuildGeometry) {
    gpu::Context context;ASSERT_TRUE(context.initHeadless());
    auto errors=std::make_shared<std::atomic<unsigned>>(0);
    context.setErrorCallback([errors](WGPUErrorType,const char*){++*errors;});
    const auto device=context.getDevice();const auto queue=context.getQueue();
    struct Target {
        WGPUTexture texture=nullptr;WGPUTextureView view=nullptr;
        ~Target(){if(view)wgpuTextureViewRelease(view);if(texture)wgpuTextureRelease(texture);}
    } target;
    target.texture=gpu::createTexture(device,gpu::TextureDesc::renderTarget(1600,900,WGPUTextureFormat_RGBA8Unorm,"creative_cache_test"));
    ASSERT_NE(target.texture,nullptr);target.view=gpu::createTextureView(target.texture);ASSERT_NE(target.view,nullptr);
    AdventureHudPath hud;ASSERT_TRUE(hud.init(device,queue,WGPUTextureFormat_RGBA8Unorm,"shaders/cove_hud.wgsl"));
    const auto render=[&](const AdventureHudContent& content,uint32_t width=1600,uint32_t height=900){
        hud.setContent(content);auto encoder=wgpuDeviceCreateCommandEncoder(device,nullptr);
        EXPECT_NE(encoder,nullptr);if(!encoder)return;
        EXPECT_TRUE(hud.render(encoder,target.view,width,height));
        EXPECT_EQ(hud.lastEncodedTriangles(),hud.layout().triangles.size());
        // A discarded encoder must neither invalidate a queued upload nor
        // leave a previous frame's observations after mode/viewport changes.
        wgpuCommandEncoderRelease(encoder);
    };
    auto content=creativeContent();render(content);ASSERT_EQ(hud.lastEncodedTriangles(),0u);ASSERT_EQ(creativeSprites(hud.layout()).size(),6u);
    const auto first=hud.uploadCount();const auto firstTriangles=hud.triangleUploadCount();
    for(int i=0;i<8;++i)render(content);
    EXPECT_EQ(hud.uploadCount(),first);EXPECT_EQ(hud.triangleUploadCount(),firstTriangles);
    render(content,800,600);EXPECT_GT(hud.uploadCount(),first);EXPECT_EQ(hud.triangleUploadCount(),firstTriangles);
    const auto resized=hud.uploadCount();render(content,800,600);EXPECT_EQ(hud.uploadCount(),resized);
    content.mode=AdventureHudMode::Explore;render(content,800,600);
    EXPECT_TRUE(hud.layout().triangles.empty());EXPECT_EQ(hud.lastEncodedTriangles(),0u);
    const auto walking=hud.uploadCount();render(content,800,600);EXPECT_EQ(hud.uploadCount(),walking);
    content.mode=AdventureHudMode::Build;content.colourPickerOpen=true;render(content,800,600);
    EXPECT_EQ(hud.lastEncodedTriangles(),0u);EXPECT_EQ(hud.layout().thumbnailCount,7u);
    const auto palette=hud.uploadCount();render(content,800,600);EXPECT_EQ(hud.uploadCount(),palette);
    content.paint=0x2d91cc;content.selectedRow=4;render(content,800,600);
    EXPECT_GT(hud.uploadCount(),palette);const auto recoloured=hud.uploadCount();
    render(content,800,600);EXPECT_EQ(hud.uploadCount(),recoloured);
    content.pixelScale=1.5f;render(content,800,600);
    EXPECT_GT(hud.uploadCount(),recoloured);EXPECT_NEAR(hud.layout().canvas.bodyPixels,27.f,.001f);
    const auto dense=hud.uploadCount();render(content,800,600);EXPECT_EQ(hud.uploadCount(),dense);
    content.pixelScale=2;render(content,800,600);EXPECT_GT(hud.uploadCount(),dense);
    EXPECT_NEAR(hud.layout().canvas.bodyPixels,36.f,.001f);
    const auto denser=hud.uploadCount();render(content,800,600);EXPECT_EQ(hud.uploadCount(),denser);
    hud.clearEncodedObservation();EXPECT_EQ(hud.lastEncodedQuads(),0u);EXPECT_EQ(hud.lastEncodedTriangles(),0u);
    context.tick();EXPECT_EQ(errors->load(),0u);context.setErrorCallback({});
}
TEST(AdventureHudGpu, CaptureSharedRendererOverCleanGameScreenshotWhenRequested) {
    const char* backgroundPath=std::getenv("VOXY_HUD_CAPTURE_BACKGROUND");
    const char* directory=std::getenv("VOXY_HUD_CAPTURE_DIRECTORY");
    if(!backgroundPath||!directory)GTEST_SKIP()<<"Opt in with VOXY_HUD_CAPTURE_BACKGROUND and VOXY_HUD_CAPTURE_DIRECTORY";
    std::ifstream sourceFile(backgroundPath,std::ios::binary);
    ASSERT_TRUE(sourceFile.good());
    const std::vector<uint8_t> sourceBytes((std::istreambuf_iterator<char>(sourceFile)),std::istreambuf_iterator<char>());
    ASSERT_LE(sourceBytes.size(),static_cast<size_t>(INT_MAX));
    int imageWidth=0,imageHeight=0,channels=0;
    std::unique_ptr<stbi_uc,decltype(&stbi_image_free)> background(
        stbi_load_from_memory(sourceBytes.data(),static_cast<int>(sourceBytes.size()),&imageWidth,&imageHeight,&channels,4),stbi_image_free);
    ASSERT_NE(background,nullptr);ASSERT_GT(imageWidth,0);ASSERT_GT(imageHeight,0);
    const auto width=static_cast<uint32_t>(imageWidth),height=static_cast<uint32_t>(imageHeight);
    ASSERT_GE(width,320u);ASSERT_GE(height,240u);ASSERT_LE(width,4096u);ASSERT_LE(height,4096u);
    const uint32_t stride=(width*4+255u)&~255u;
    gpu::Context context;ASSERT_TRUE(context.initHeadless());
    auto errors=std::make_shared<std::atomic<unsigned>>(0);
    context.setErrorCallback([errors](WGPUErrorType,const char*){++*errors;});
    const auto device=context.getDevice();const auto queue=context.getQueue();
    struct Target {
        WGPUTexture texture=nullptr;WGPUTextureView view=nullptr;WGPUBuffer readback=nullptr;
        ~Target(){if(readback)wgpuBufferRelease(readback);if(view)wgpuTextureViewRelease(view);if(texture)wgpuTextureRelease(texture);}
    } target;
    auto texture=gpu::TextureDesc::renderTarget(width,height,WGPUTextureFormat_RGBA8Unorm,"shared_hud_capture");
    texture.usage|=WGPUTextureUsage_CopySrc|WGPUTextureUsage_CopyDst;
    target.texture=gpu::createTexture(device,texture);ASSERT_NE(target.texture,nullptr);
    target.view=gpu::createTextureView(target.texture);ASSERT_NE(target.view,nullptr);
    target.readback=gpu::createBuffer(device,{"shared_hud_capture_readback",uint64_t{stride}*height,WGPUBufferUsage_CopyDst|WGPUBufferUsage_MapRead});
    ASSERT_NE(target.readback,nullptr);
    AdventureHudPath hud;ASSERT_TRUE(hud.init(device,queue,WGPUTextureFormat_RGBA8Unorm,"shaders/cove_hud.wgsl"));
    if(const char* terrainPath=std::getenv("VOXY_HUD_CAPTURE_TERRAIN")) {
        // The optional navigation fixture uses the same decoded heightfield
        // as the actual scene, sampled by the runtime's map implementation.
        // It does not substitute a drawn island for the accepted terrain.
        constexpr uint32_t terrainWidth=8192,terrainHeight=8192;
        std::ifstream terrainFile(terrainPath,std::ios::binary);ASSERT_TRUE(terrainFile.good());
        std::vector<uint16_t> samples(size_t{terrainWidth}*terrainHeight);
        const auto bytes=static_cast<std::streamsize>(samples.size()*sizeof(uint16_t));
        terrainFile.read(reinterpret_cast<char*>(samples.data()),bytes);ASSERT_EQ(terrainFile.gcount(),bytes);
        const terrain::lego::Surface surface{samples,terrainWidth,terrainHeight,600.f,1.f};
        game::adventure::AdventureNavigationCache cache;
        auto map=std::make_shared<AdventureHudMap>();map->revision=1;
        map->rgba=cache.rasterize(surface,-200.f,{1200,-1120},1,{});
        ASSERT_EQ(map->rgba.size(),size_t{map->width}*map->height*4);
        AdventureHudNavigation navigation;navigation.visible=true;navigation.map=std::move(map);
        navigation.playerUv=cache.playerUv({1200,-1120});hud.setNavigation(std::move(navigation));
    }
    std::filesystem::create_directories(directory);
    std::ofstream report(std::filesystem::path(directory)/"renderer-capture.tsv");
    report<<"view\twidth\theight\tquads\ttriangles\tuploads\n";
    for(const std::string_view name:{"explore","build","catalog","catalog-bricks","palette"}) {
        auto content=creativeContent();
        if(name=="explore"){content.mode=AdventureHudMode::Explore;content.status="";content.context="";}
        if(name=="catalog"||name=="catalog-bricks"){
            content.mode=AdventureHudMode::Catalog;content.title="Building pieces";content.pickerOpen=true;
            if(name=="catalog-bricks") {
                content.paletteCategory=AdventurePaletteCategory::Bricks;
                content.rows={content.rows[7],content.rows[8],content.rows[9]};content.selectedRow=1;
            } else {
                content.rows={content.rows[0],content.rows[1],content.rows[2],content.rows[3],content.rows[4],content.rows[5],content.rows[6],content.rows[13],content.rows[14]};
                content.selectedRow=2;
            }
            content.buildControls={creativeAction("Previous",25,-1,2001),creativeAction("Next",25,1,2003),creativeAction("Close",20,0,2007)};
        }
        if(name=="palette"){
            content.colourPickerOpen=true;content.selectedRow=1;content.paint=0xe53b33;
            content.buildControls={creativeAction("Previous",25,-1,2001),creativeAction("Next",25,1,2003),creativeAction("Close",20,0,2007)};
        }
        if(name=="catalog"||name=="catalog-bricks"||name=="palette")for(auto& item:content.hotbar)item.enabled=false;
        hud.setContent(content);
        WGPUImageCopyTexture backgroundTarget{};backgroundTarget.texture=target.texture;backgroundTarget.aspect=WGPUTextureAspect_All;
        WGPUTextureDataLayout backgroundLayout{};backgroundLayout.bytesPerRow=width*4;backgroundLayout.rowsPerImage=height;
        const WGPUExtent3D extent{width,height,1};
        wgpuQueueWriteTexture(queue,&backgroundTarget,background.get(),size_t{width}*height*4,&backgroundLayout,&extent);
        auto encoder=wgpuDeviceCreateCommandEncoder(device,nullptr);ASSERT_NE(encoder,nullptr);
        ASSERT_TRUE(hud.render(encoder,target.view,width,height));bounded(hud.layout(),width,height);
        WGPUImageCopyBuffer destination{};destination.buffer=target.readback;destination.layout.bytesPerRow=stride;destination.layout.rowsPerImage=height;
        wgpuCommandEncoderCopyTextureToBuffer(encoder,&backgroundTarget,&destination,&extent);
        auto command=wgpuCommandEncoderFinish(encoder,nullptr);ASSERT_NE(command,nullptr);
        wgpuQueueSubmit(queue,1,&command);wgpuCommandBufferRelease(command);wgpuCommandEncoderRelease(encoder);
        auto completion=std::make_shared<std::atomic<int>>(0);
        auto* callback=new std::shared_ptr<std::atomic<int>>(completion);
        wgpuBufferMapAsync(target.readback,WGPUMapMode_Read,0,uint64_t{stride}*height,
            [](WGPUBufferMapAsyncStatus status,void* data){
                std::unique_ptr<std::shared_ptr<std::atomic<int>>> result(static_cast<std::shared_ptr<std::atomic<int>>*>(data));
                (*result)->store(status==WGPUBufferMapAsyncStatus_Success?1:2);
            },callback);
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(10);
        while(completion->load()==0&&std::chrono::steady_clock::now()<deadline){context.tick();std::this_thread::sleep_for(std::chrono::milliseconds(1));}
        ASSERT_EQ(completion->load(),1);
        const auto* pixels=static_cast<const uint8_t*>(wgpuBufferGetConstMappedRange(target.readback,0,uint64_t{stride}*height));ASSERT_NE(pixels,nullptr);
        const auto path=std::filesystem::path(directory)/(std::string(name)+".png");
        ASSERT_TRUE(stbi_write_png(path.string().c_str(),imageWidth,imageHeight,4,pixels,static_cast<int>(stride)));
        // Ordinary play keeps the aiming region untouched. An open piece
        // picker may intentionally occupy more of the world while choosing.
        if(name=="explore"||name=="build") {
            const auto pixel=size_t{height/2}*width+width/2;
            const auto captured=size_t{height/2}*stride+(width/2)*4;
            for(size_t channel=0;channel<4;++channel)EXPECT_EQ(pixels[captured+channel],background.get()[pixel*4+channel]);
        }
        report<<name<<'\t'<<width<<'\t'<<height<<'\t'<<hud.lastEncodedQuads()<<'\t'<<hud.lastEncodedTriangles()<<'\t'<<hud.uploadCount()<<'\n';
        wgpuBufferUnmap(target.readback);
    }
    context.tick();EXPECT_EQ(errors->load(),0u);context.setErrorCallback({});
}
}
