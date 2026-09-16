#include "render/adventure_hud.hpp"
#include "game/adventure/building_catalog.hpp"
#include "game/adventure/adventure_presentation.hpp"
#include "render/generated/adventure_piece_thumbnails.hpp"
#include "core/sha256.hpp"
#include "gpu/context.hpp"
#include "gpu/resources.hpp"
#include <gtest/gtest.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <set>
#include <span>
#include <memory>
#include <thread>
#include <glm/vec2.hpp>

namespace voxy::render {
namespace {
AdventureHudRow row(std::string label,uint32_t intent=65) {
    AdventureHudRow r;r.label=std::move(label);r.intent=intent;return r;
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
void completeGuideBody(const AdventureHudLayout& layout,std::string_view text,float pixels) {
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
        const auto rendered=appendCoveHudText(single,std::string(" ")+c,{0,0,100,100},pixels,{1,1,1,1});
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
TEST(AdventureHud, MeshThumbnailsHaveCompleteVersionedGeometryAndBrowserSourceIdentity) {
    namespace thumbnails=adventure_thumbnails;
    EXPECT_EQ(thumbnails::recipe,"adventure-installed-mesh-thumbnails-r02");
    EXPECT_EQ(thumbnails::sourceSha256,"08ce2ff933bdc9d86d68d62e29b15f62d7f22984753f20b51fdb89348fd3236b");
    EXPECT_EQ(core::sha256Hex(core::sha256(std::as_bytes(std::span(thumbnails::triangles)))),thumbnails::packedSha256);
    EXPECT_EQ(thumbnails::doorSourceSha256,"be59bb6888aacc94c345fa3aebf0ebf1df1cacc1c3082e55c01eff16753b1e5f");
    ASSERT_EQ(thumbnails::pieces.size(),15u);ASSERT_EQ(thumbnails::triangles.size(),3686u);
    // The old kit retains its exact projected geometry; the new composed door
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
    EXPECT_EQ(AdventureHudPath::maximumBufferBytes,159152u);EXPECT_EQ(AdventureHudPath::residentBytes,327088u);
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
}
