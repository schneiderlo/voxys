#include <gtest/gtest.h>
#include "render/cove_hud.hpp"
#include "render/cove_recovery_guidance.hpp"
#include "gpu/context.hpp"
#include "gpu/resources.hpp"
#include <glm/vec2.hpp>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <memory>
#include <limits>
#include <thread>

namespace voxy::render {
namespace {
CoveHudContent workshop() {
    return {"Brick workshop","Brick 2 x 4 - orange","Material 48  Launch 3",
        "Ready to place",{"Click: Place   R: Rotate","Esc: Select   U: Undo","Enter: Launch   B: Close"},CoveHudTone::Ready};
}
}

TEST(CoveHud, ReadableBoundsKeepTheBuildAndPaletteClear) {
    for(const auto size:std::array<glm::uvec2,5>{{{640,480},{960,540},{1280,800},{1920,1080},{3440,1440}}}){
        SCOPED_TRACE(::testing::Message()<<size.x<<" x "<<size.y);
        const auto hud=layoutCoveHud(workshop(),size.x,size.y);
        ASSERT_GT(hud.count,100u);
        EXPECT_FALSE(hud.truncated);
        EXPECT_GE(hud.bodyPixels,20.f);
        EXPECT_LT(hud.count,CoveHudLayout::maximumQuads);
        const auto rectangle=coveHudWorkshopRectangle(size.x,size.y);
        EXPECT_GE((rectangle.x+1)*.5*double(size.x),double(hud.panel.x+hud.panel.z));
        EXPECT_LT(hud.panel.y+hud.panel.w,float(size.y)*.78f);
        for(size_t i=0;i<hud.count;++i){
            const auto& q=hud.quads[i];
            EXPECT_GE(q.bounds.x,hud.panel.x);EXPECT_GE(q.bounds.y,hud.panel.y);
            EXPECT_LE(q.bounds.x+q.bounds.z,hud.panel.x+hud.panel.z);
            EXPECT_LE(q.bounds.y+q.bounds.w,hud.panel.y+hud.panel.w);
            for(int j=0;j<4;++j)EXPECT_TRUE(std::isfinite(q.bounds[j]));
        }
    }
}

TEST(CoveHud, AdventureDialogueRetainsInstructionsAndChoicesAtLargeText) {
    CoveHudContent content;content.title="Voxys";content.textScale=1.5f;content.menu.emplace();
    auto& menu=*content.menu;menu.title="Moss / Builder";menu.subtitleLineLimit=4;
    menu.subtitle="The relay is dark. First, make somewhere safe to return to: a sheltered bed, a chest and a workbench. A home you already built counts.";
    menu.rows={{"Accept: A Place to Return",true},{"Leave conversation",true}};
    for(const auto size:{glm::uvec2(960,600),glm::uvec2(1280,720)}) {
        const auto layout=layoutCoveHud(content,size.x,size.y);
        EXPECT_FALSE(layout.truncated);EXPECT_GE(layout.bodyPixels,30.f);
        EXPECT_TRUE(std::any_of(layout.menuHits.begin(),layout.menuHits.end(),[](const auto& hit){return hit.row==1;}));
    }
}
TEST(CoveHud, MenuKeepsSelectedRowsVisibleWithinViewportAndGlyphBudget) {
    for(const auto size:std::array<glm::uvec2,3>{{{640,480},{960,540},{1920,1080}}})
    for(bool naming:{false,true}) {
        CoveHudContent content;content.title="Workshop tools";content.menu.emplace();
        auto& menu=*content.menu;menu.title="Saved designs";menu.subtitle="Two bricks";
        menu.status="Choose a design";menu.naming=naming;menu.name="BOAT";
        for(size_t i=0;i<(naming?4u:32u);++i)menu.rows.push_back({std::to_string(i)+" "+std::string(96,'W'),true});
        for(size_t selected=0;selected<menu.rows.size();++selected) {
            menu.selected=selected;const auto layout=layoutCoveHud(content,size.x,size.y);
            SCOPED_TRACE(::testing::Message()<<size.x<<"x"<<size.y<<" name="<<naming<<" selection="<<selected);
            EXPECT_FALSE(layout.truncated);EXPECT_LT(layout.count,CoveHudLayout::maximumQuads);
            EXPECT_TRUE(std::any_of(layout.menuHits.begin(),layout.menuHits.end(),[&](const auto& hit){return hit.row==int(selected);}));
            for(const auto& hit:layout.menuHits) {
                EXPECT_GE(hit.bounds.x,0);EXPECT_GE(hit.bounds.y,0);
                EXPECT_LE(hit.bounds.x+hit.bounds.z,float(size.x));EXPECT_LE(hit.bounds.y+hit.bounds.w,float(size.y));
            }
        }
    }
}

TEST(CoveHud, ImportFolderAndRefusalRemainReadableTogether) {
    CoveHudContent content;content.title="Workshop tools";content.menu.emplace();
    auto& menu=*content.menu;menu.title="Import a design file";
    menu.subtitle="/home/player/workspace/schneiderlo/voxys/build-workshop-tools-r01/native-storage-r04/Designs/Imports";
    menu.status="This design file is invalid or incompatible.";
    menu.rows={{"corrupt.voxy-design.json",true},{"transfer.voxy-design.json",true},{"Refresh list",true},{"Back",true}};
    for(const auto size:std::array<glm::uvec2,2>{{{960,540},{960,800}}}) {
        const auto layout=layoutCoveHud(content,size.x,size.y);
        EXPECT_FALSE(layout.truncated);EXPECT_LT(layout.count,CoveHudLayout::maximumQuads);
        EXPECT_GE(layout.bodyPixels,20.f);
        EXPECT_TRUE(std::any_of(layout.menuHits.begin(),layout.menuHits.end(),[](const auto& hit){return hit.row==0;}));
        for(const auto& hit:layout.menuHits) {
            EXPECT_GE(hit.bounds.x,0);EXPECT_GE(hit.bounds.y,0);
            EXPECT_LE(hit.bounds.x+hit.bounds.z,float(size.x));EXPECT_LE(hit.bounds.y+hit.bounds.w,float(size.y));
        }
    }
}
TEST(CoveHud, LargeTextNamingScrollsEveryLetterAndSelectedButtonWithoutShrinking) {
    for(const auto size:std::array<glm::uvec2,2>{{{640,480},{960,540}}}) {
        CoveHudContent content;content.title="Name the boat";content.textScale=1.5f;content.menu.emplace();
        auto& menu=*content.menu;menu.title="Name this boat";menu.subtitle="Use the letter grid";
        menu.status="Choose a letter";menu.naming=true;menu.name="BOAT";
        menu.rows={{"Done",true},{"Backspace",true},{"Cancel",true}};
        for(size_t key=0;key<40;++key)for(size_t selected=0;selected<3;++selected) {
            menu.key=key;menu.selected=selected;
            const auto layout=layoutCoveHud(content,size.x,size.y);
            SCOPED_TRACE(::testing::Message()<<size.x<<"x"<<size.y<<" key="<<key<<" row="<<selected);
            EXPECT_FALSE(layout.truncated);EXPECT_GE(layout.bodyPixels,30.f);EXPECT_LT(layout.count,CoveHudLayout::maximumQuads);
            EXPECT_TRUE(std::any_of(layout.menuHits.begin(),layout.menuHits.end(),[&](const auto& hit){return hit.key==static_cast<int>(key);}));
            EXPECT_TRUE(std::any_of(layout.menuHits.begin(),layout.menuHits.end(),[&](const auto& hit){return hit.row==static_cast<int>(selected);}));
            for(size_t i=0;i<layout.count;++i) {
                const auto& bounds=layout.quads[i].bounds;
                EXPECT_GE(bounds.x,0);EXPECT_GE(bounds.y,0);EXPECT_LE(bounds.x+bounds.z,float(size.x));EXPECT_LE(bounds.y+bounds.w,float(size.y));
            }
        }
    }
}

TEST(CoveHud, ActualControllerGuidanceTitleAndHintsWrapAtSupportedTextScales) {
    CoveHudContent content{"Guide 1/7: Walk along the dock","Walk beside the boat","Material 48",
        "First job: sunken generator",{"Left stick: Walk  B / Circle: Jump","D-pad Up: Accept job",
        "View: Build  Y / Triangle: Camera  Menu: Menu"},CoveHudTone::Neutral};
    content.rightAligned=true;
    for(const float scale:std::array{1.f,1.25f,1.5f}) {
        content.textScale=scale;const auto layout=layoutCoveHud(content,960,800);
        EXPECT_FALSE(layout.truncated)<<scale;EXPECT_GE(layout.bodyPixels,20*scale);
        EXPECT_GT(layout.count,154u);EXPECT_LT(layout.count,CoveHudLayout::maximumQuads);
        EXPECT_LE(layout.panel.x+layout.panel.z,960.f);EXPECT_LE(layout.panel.y+layout.panel.w,800.f);
    }
}

TEST(CoveHud, ActualGameAndAccessibilityMenusKeepLargeTextAndNavigationVisible) {
    for(const std::string title:{"Expedition menu","Controls and accessibility"}) {
        CoveHudContent content;content.title=title;content.textScale=1.5f;content.menu.emplace();
        auto& menu=*content.menu;menu.title=title;menu.subtitle="Changes apply immediately";
        menu.status="Preferences saved.";
        menu.rows={{"Resume",true},{"Save expedition",true},{"Job board",true},{"Nearby map",true},
            {"Inventory",true},{"Controls and accessibility",true},{"Text size: 150%",true},
            {"High contrast: On",true},{"Winch: Press to start / stop",true},{"Remap controls",true},{"Back",true}};
        for(size_t selected=0;selected<menu.rows.size();++selected) {
            menu.selected=selected;const auto layout=layoutCoveHud(content,960,800);
            EXPECT_FALSE(layout.truncated)<<title<<selected;EXPECT_EQ(layout.bodyPixels,30.f);
            EXPECT_LT(layout.count,CoveHudLayout::maximumQuads);
            EXPECT_TRUE(std::any_of(layout.menuHits.begin(),layout.menuHits.end(),[&](const auto& hit){return hit.row==static_cast<int>(selected);}));
        }
    }
}

TEST(CoveHud, OversizedTextAndTinyWindowsStayBoundedWithoutShrinking) {
    auto content=workshop();
    content.selected=std::string(100000,'W');
    content.economy=std::string(100000,'M');
    content.status="Unsupported UTF-8: \xe2\x80\xa6 / Brick 2\xc3\x97" "4";
    auto hud=layoutCoveHud(content,320,240);
    EXPECT_TRUE(hud.truncated);EXPECT_GE(hud.bodyPixels,20.f);
    EXPECT_LE(hud.count,CoveHudLayout::maximumQuads);
    for(size_t i=0;i<hud.count;++i){
        const auto& q=hud.quads[i];
        EXPECT_GE(q.bounds.x,0.f);EXPECT_GE(q.bounds.y,0.f);
        EXPECT_LE(q.bounds.x+q.bounds.z,320.f);EXPECT_LE(q.bounds.y+q.bounds.w,240.f);
    }
    EXPECT_EQ(layoutCoveHud(content,0,0).count,0u);
    EXPECT_EQ(layoutCoveHud(content,1280,0).count,0u);
    EXPECT_EQ(layoutCoveHud({},1280,800).count,0u);
}

namespace {
CoveRecoveryFacts acceptedRecovery() {
    CoveRecoveryFacts f;
    f.job=CoveRecoveryFacts::Job::Accepted;
    f.commandsReady=f.storageReady=f.cargoObserved=f.onBoat=true;
    f.hasWinch=f.onWinchRoot=f.towReady=true;
    f.hookDistance=6;f.harborDistance=6;f.harborLimit=3.9;
    f.height=-3;f.minimumHeight=-1;f.maximumSpeed=.8;f.maximumSpin=1;
    return f;
}
CoveHudContent recoveryContent(const CoveRecoveryFacts& f) {
    const auto g=coveRecoveryGuidance(f);
    CoveHudContent content;
    content.title=g.title;content.selected="E: Return to dock";content.economy="Material 96";
    content.status=g.status;content.tone=g.tone;content.objective=g.step;
    content.hints={"WASD: Walk  Space: Jump",std::string(g.controls),"B: Build  P: Pause  R: Rescue"};
    return content;
}
}

TEST(CoveRecoveryGuidance, AcceptOnFootButNeverInventDeliveryPermissionFromGeometry) {
    auto f=acceptedRecovery();
    f.job=CoveRecoveryFacts::Job::Available;f.onBoat=false;f.canAccept=true;
    EXPECT_EQ(coveRecoveryGuidance(f).step,"accept");
    EXPECT_EQ(coveRecoveryGuidance(f).controls,"J: Accept job");
    f.commandsReady=false;
    EXPECT_EQ(coveRecoveryGuidance(f).step,"waiting");
    f=acceptedRecovery();f.harborDistance=3.9;f.height=-1;f.speed=.8;f.spin=1;
    f.hasWinch=false; // Delivery never requires a fitted winch, rope or helm.
    EXPECT_EQ(coveRecoveryGuidance(f).step,"waiting");
    f.canDeliver=true;
    EXPECT_EQ(coveRecoveryGuidance(f).step,"deliver");
    EXPECT_EQ(coveRecoveryGuidance(f).controls,"H: Deliver generator");
    f.commandsReady=false;
    EXPECT_EQ(coveRecoveryGuidance(f).step,"waiting");
    f.commandsReady=true;f.canDeliver=false;f.storageReady=false;
    EXPECT_EQ(coveRecoveryGuidance(f).step,"storage");
}

TEST(CoveRecoveryGuidance, UsesCargoRangeHeightAndFullSpeedIncludingExactBoundaries) {
    auto f=acceptedRecovery();f.hookDistance=8;
    EXPECT_EQ(coveRecoveryGuidance(f).step,"hook");
    f.hookDistance=std::nextafter(8.0,9.0);
    EXPECT_EQ(coveRecoveryGuidance(f).step,"approach");
    f=acceptedRecovery();f.towAttached=f.towConfirmed=true;
    EXPECT_EQ(coveRecoveryGuidance(f).step,"return");
    EXPECT_EQ(coveRecoveryGuidance(f).status,"Harbor 6.0 / 3.9 m");
    f.harborDistance=f.harborLimit;
    EXPECT_EQ(coveRecoveryGuidance(f).step,"lift");
    EXPECT_EQ(coveRecoveryGuidance(f).status,"Raise load 2.0 m");
    f.height=std::nextafter(f.minimumHeight,-2.0);
    EXPECT_EQ(coveRecoveryGuidance(f).status,"Raise load 0.1 m");
    f.height=f.minimumHeight;f.speed=std::nextafter(f.maximumSpeed,1.0);
    EXPECT_EQ(coveRecoveryGuidance(f).step,"slow");
    f.speed=f.maximumSpeed;f.spin=std::nextafter(f.maximumSpin,2.0);
    EXPECT_EQ(coveRecoveryGuidance(f).step,"steady");
    f.spin=f.maximumSpin;
    EXPECT_EQ(coveRecoveryGuidance(f).step,"waiting"); // Only eligible() grants H.
    f.canDeliver=true;
    EXPECT_EQ(coveRecoveryGuidance(f).step,"deliver");
    f.canDeliver=false;f.harborDistance=std::numeric_limits<double>::quiet_NaN();
    EXPECT_EQ(coveRecoveryGuidance(f).step,"waiting");
}

TEST(CoveRecoveryGuidance, MissingBodiesWinchOrConfirmedTowCannotAdvertiseAReadyHook) {
    auto f=acceptedRecovery();f.cargoObserved=false;
    EXPECT_EQ(coveRecoveryGuidance(f).step,"waiting");
    f=acceptedRecovery();f.onBoat=false;
    EXPECT_EQ(coveRecoveryGuidance(f).step,"board");
    f=acceptedRecovery();f.hasWinch=false;
    EXPECT_EQ(coveRecoveryGuidance(f).step,"winch");
    f=acceptedRecovery();f.onWinchRoot=false;
    EXPECT_EQ(coveRecoveryGuidance(f).step,"winch");
    f=acceptedRecovery();f.towReady=false;
    EXPECT_EQ(coveRecoveryGuidance(f).step,"waiting");
    f=acceptedRecovery();f.towAttached=true;f.towConfirmed=false;
    EXPECT_EQ(coveRecoveryGuidance(f).step,"waiting");
}

TEST(CoveRecoveryGuidance, PhysicalBankingAndInstalledHarborAreNotDurableSuccess) {
    auto f=acceptedRecovery();f.deliveryPending=true;
    EXPECT_EQ(coveRecoveryGuidance(f).step,"securing");
    EXPECT_EQ(coveRecoveryGuidance(f).status,"Securing the observed load");
    f.cargoBanked=true;
    EXPECT_EQ(coveRecoveryGuidance(f).step,"saving");
    f.deliveryPending=false;f.job=CoveRecoveryFacts::Job::Completed;
    f.harborPresent=true;f.harborInstalled=true;f.harborDurable=true;
    EXPECT_EQ(coveRecoveryGuidance(f).step,"saving"); // Generator receipt is not durable yet.
    f.deliveryDurable=true;f.harborDurable=false;f.harborPending=true;
    EXPECT_EQ(coveRecoveryGuidance(f).step,"powering");
    f.harborPending=false;f.onBoat=false;f.atDock=true;
    EXPECT_EQ(coveRecoveryGuidance(f).step,"waiting");
    f.harborInstalled=false;f.canInstall=true;
    EXPECT_EQ(coveRecoveryGuidance(f).step,"power");
    EXPECT_EQ(coveRecoveryGuidance(f).controls,"K: Power harbor");
    f.canInstall=false;f.atDock=false;
    EXPECT_EQ(coveRecoveryGuidance(f).step,"dock");
    f.atDock=true;f.storageReady=false;
    EXPECT_EQ(coveRecoveryGuidance(f).step,"storage");
    f.storageReady=true;f.harborInstalled=f.harborDurable=true;
    EXPECT_EQ(coveRecoveryGuidance(f).step,"powered");
}

TEST(CoveRecoveryGuidance, PausedPowerAndManualSaveOverrideHistoricalDeliverySuccess) {
    CovePauseFacts f;
    f.paused=f.cargoBanked=f.deliveryDurable=true;
    f.saveStatus="Delivery saved. +60 material | P: Resume";
    CoveHudContent hud;hud.economy="Material 96";
    const auto apply=[&]{
        applyCovePauseGuidance(f,hud);
        for(const auto size:std::array<glm::uvec2,2>{{{640,480},{960,540}}}){
            SCOPED_TRACE(::testing::Message()<<hud.objective<<" "<<size.x<<"x"<<size.y);
            const auto layout=layoutCoveHud(hud,size.x,size.y);
            EXPECT_FALSE(layout.truncated);EXPECT_GE(layout.bodyPixels,20.f);
            EXPECT_LT(layout.count,CoveHudLayout::maximumQuads);
        }
    };
    f.harborPending=true; // Requested/Uploading/Admitting, before checkpoint.
    apply();
    EXPECT_EQ(hud.objective,"powering");EXPECT_EQ(hud.status,"Preparing harbor lift");
    for(const auto& hint:hud.hints)EXPECT_EQ(hint.find("P: Resume"),std::string::npos);
    f.checkpointPending=true;apply();
    EXPECT_EQ(hud.objective,"powering");EXPECT_EQ(hud.status,"Waiting for durable save");
    f.saveStatus="Save failed. Disk full. Pause, then F10 to retry.";
    apply();
    EXPECT_EQ(hud.objective,"powering");EXPECT_EQ(hud.status,"Save failed. F10: Retry");
    f.harborPending=f.checkpointPending=false; // Separate later manual failure.
    apply();
    EXPECT_EQ(hud.objective,"save-failed");EXPECT_EQ(hud.status,"Save failed. F10: Retry");
    f.saveStatus="Saving expedition...";apply();
    EXPECT_EQ(hud.objective,"manual-saving");EXPECT_EQ(hud.status,"Saving expedition...");
    f.saveStatus="Expedition saved. F10: Save again | P: Resume";f.harborPowered=true;
    apply();
    EXPECT_EQ(hud.objective,"powered");
    f.rescuePending=true;apply();
    EXPECT_EQ(hud.objective,"rescuing");
    for(const auto& hint:hud.hints)EXPECT_EQ(hint.find("P: Resume"),std::string::npos);
    f.rescuePending=false;f.harborPowered=false;
    f.harborRefusal="Clear space on the pier before powering the lift.";
    apply();
    EXPECT_EQ(hud.objective,"harbor-blocked");EXPECT_EQ(hud.status,f.harborRefusal);
    EXPECT_EQ(hud.hints[1],"P: Resume");
    f.storageRevoked=true;apply();
    EXPECT_EQ(hud.objective,"storage-revoked");EXPECT_EQ(hud.status,"Restart game to recover");
    for(const auto& hint:hud.hints)EXPECT_EQ(hint.find("F10"),std::string::npos);
    EXPECT_EQ(hud.economy,"Material 96");
}

TEST(CoveRecoveryGuidance, EveryMissionStepKeepsReadableLayoutAndExistingGpuBounds) {
    std::vector<CoveRecoveryFacts> samples;
    auto f=acceptedRecovery();samples.push_back(f);
    f.job=CoveRecoveryFacts::Job::Available;f.canAccept=true;samples.push_back(f);
    f=acceptedRecovery();f.onBoat=false;samples.push_back(f);
    f=acceptedRecovery();f.hookDistance=123.4;samples.push_back(f);
    f.towAttached=f.towConfirmed=true;samples.push_back(f);
    f.harborDistance=3;f.height=-6;samples.push_back(f);
    f.height=-1;f.speed=2.3;samples.push_back(f);
    f.speed=.8;f.spin=2.3;samples.push_back(f);
    f.canDeliver=true;samples.push_back(f);
    f.canDeliver=false;f.deliveryPending=true;samples.push_back(f);
    f.cargoBanked=true;samples.push_back(f);
    f.deliveryPending=false;f.job=CoveRecoveryFacts::Job::Completed;f.deliveryDurable=true;
    f.harborPresent=true;f.onBoat=false;f.atDock=true;f.canInstall=true;samples.push_back(f);
    f.harborPending=true;samples.push_back(f);
    f.harborPending=false;f.harborInstalled=f.harborDurable=true;f.canUseLift=true;samples.push_back(f);
    for(const auto& sample:samples)for(const auto size:std::array<glm::uvec2,2>{{{640,480},{960,540}}}){
        const auto content=recoveryContent(sample);
        SCOPED_TRACE(::testing::Message()<<content.objective<<" "<<size.x<<"x"<<size.y);
        const auto hud=layoutCoveHud(content,size.x,size.y);
        EXPECT_FALSE(hud.truncated);EXPECT_GE(hud.bodyPixels,20.f);
        EXPECT_GT(hud.count,50u);EXPECT_LT(hud.count,CoveHudLayout::maximumQuads);
        EXPECT_LT(hud.panel.y+hud.panel.w,float(size.y)*.78f);
    }
    EXPECT_EQ(CoveHudLayout::maximumQuads,768u);
    EXPECT_EQ(CoveHudPath::residentBytes,167936u);
}

TEST(CoveHud, AtlasIsBoundedAntialiasedAndIndependentOfInstalledFonts) {
    const auto atlas=decodeCoveHudAtlas();
    ASSERT_EQ(atlas.size(),512u*256u);
    EXPECT_GT(std::count(atlas.begin(),atlas.end(),uint8_t{255}),1000);
    EXPECT_GT(std::count_if(atlas.begin(),atlas.end(),[](uint8_t p){return p>0&&p<255;}),1000);
    EXPECT_EQ(atlas[512+1],255);EXPECT_EQ(atlas[0],0);
    EXPECT_EQ(CoveHudPath::residentBytes,167936u);
    EXPECT_EQ(sizeof(CoveHudQuad),48u);
}

TEST(CoveHudGpu, TextBlendsAndSurvivesSubmittedShutdownWhileDiscardDoesNotLeak) {
    gpu::Context context;
    ASSERT_TRUE(context.initHeadless());
    auto errors=std::make_shared<std::atomic<unsigned>>(0);
    context.setErrorCallback([errors](WGPUErrorType,const char*){++*errors;});
    const auto device=context.getDevice();const auto queue=context.getQueue();
    struct Resources {
        WGPUTexture texture=nullptr;
        WGPUTextureView view=nullptr;
        WGPUBuffer readback=nullptr;
        ~Resources(){if(readback)wgpuBufferRelease(readback);if(view)wgpuTextureViewRelease(view);if(texture)wgpuTextureRelease(texture);}
    } owned;
    constexpr uint32_t width=640,height=480,stride=width*4;
    auto texture=gpu::TextureDesc::renderTarget(width,height,WGPUTextureFormat_RGBA8Unorm,"hud_test_target");
    texture.usage|=WGPUTextureUsage_CopySrc;
    owned.texture=gpu::createTexture(device,texture);ASSERT_NE(owned.texture,nullptr);
    owned.view=gpu::createTextureView(owned.texture);ASSERT_NE(owned.view,nullptr);
    owned.readback=gpu::createBuffer(device,{"hud_numeric_readback",stride*height,WGPUBufferUsage_CopyDst|WGPUBufferUsage_MapRead});
    ASSERT_NE(owned.readback,nullptr);
    CoveHudPath hud;
    ASSERT_TRUE(hud.init(device,queue,WGPUTextureFormat_RGBA8Unorm,"shaders/cove_hud.wgsl"));
    hud.setContent(workshop());
    auto discard=wgpuDeviceCreateCommandEncoder(device,nullptr);ASSERT_NE(discard,nullptr);
    ASSERT_TRUE(hud.render(discard,owned.view,width,height));
    EXPECT_GT(hud.lastEncodedQuads(),100u);
    wgpuCommandEncoderRelease(discard); // No submit: the following ordinary frame remains valid.
    hud.clearEncodedObservation();EXPECT_EQ(hud.lastEncodedQuads(),0u);
    const auto uploads=hud.uploadCount();
    auto encoder=wgpuDeviceCreateCommandEncoder(device,nullptr);ASSERT_NE(encoder,nullptr);
    WGPURenderPassColorAttachment color{};color.view=owned.view;color.loadOp=WGPULoadOp_Clear;color.storeOp=WGPUStoreOp_Store;
    color.clearValue={.2,.4,.6,1};
    WGPURenderPassDescriptor pass{};pass.colorAttachmentCount=1;pass.colorAttachments=&color;
    auto clear=wgpuCommandEncoderBeginRenderPass(encoder,&pass);ASSERT_NE(clear,nullptr);
    wgpuRenderPassEncoderEnd(clear);wgpuRenderPassEncoderRelease(clear);
    ASSERT_TRUE(hud.render(encoder,owned.view,width,height));
    EXPECT_EQ(hud.uploadCount(),uploads); // Same content and viewport never re-upload.
    WGPUImageCopyTexture source{};source.texture=owned.texture;source.aspect=WGPUTextureAspect_All;
    WGPUImageCopyBuffer destination{};destination.buffer=owned.readback;destination.layout.bytesPerRow=stride;destination.layout.rowsPerImage=height;
    WGPUExtent3D extent{width,height,1};
    wgpuCommandEncoderCopyTextureToBuffer(encoder,&source,&destination,&extent);
    auto command=wgpuCommandEncoderFinish(encoder,nullptr);ASSERT_NE(command,nullptr);
    wgpuQueueSubmit(queue,1,&command);wgpuCommandBufferRelease(command);wgpuCommandEncoderRelease(encoder);
    // Releasing HUD owners before completion must preserve the already submitted draw.
    hud.shutdown();EXPECT_FALSE(hud.initialized());EXPECT_EQ(hud.lastEncodedQuads(),0u);
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
    const auto* pixels=static_cast<const uint8_t*>(wgpuBufferGetConstMappedRange(owned.readback,0,stride*height));
    ASSERT_NE(pixels,nullptr);
    size_t textPixels=0,backgroundPixels=0,accentPixels=0;
    for(uint32_t y=15;y<340;++y)for(uint32_t x=15;x<290;++x){
        const auto* p=pixels+(y*width+x)*4;
        textPixels+=p[0]>160&&p[1]>170&&p[2]>160;
        backgroundPixels+=p[0]<30&&p[1]<40&&p[2]<45;
        accentPixels+=p[1]>180&&p[0]<150;
    }
    RecordProperty("textPixels",static_cast<int>(textPixels));
    RecordProperty("backgroundPixels",static_cast<int>(backgroundPixels));
    RecordProperty("accentPixels",static_cast<int>(accentPixels));
    EXPECT_GT(textPixels,1000u);EXPECT_GT(backgroundPixels,20000u);EXPECT_GT(accentPixels,300u);
    const auto* untouched=pixels+(200*width+550)*4;
    EXPECT_NEAR(untouched[0],51,1);EXPECT_NEAR(untouched[1],102,1);EXPECT_NEAR(untouched[2],153,1);
    wgpuBufferUnmap(owned.readback);context.tick();EXPECT_EQ(errors->load(),0u);context.setErrorCallback({});
}
}
