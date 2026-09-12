#include <gtest/gtest.h>
#include "render/cove_hud.hpp"
#include "gpu/context.hpp"
#include "gpu/resources.hpp"
#include <glm/vec2.hpp>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <memory>
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
