#include "render/cove_effects_path.hpp"
#include "gpu/context.hpp"
#include "gpu/resources.hpp"
#include <gtest/gtest.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/packing.hpp>
#include <atomic>
#include <chrono>
#include <cstring>
#include <memory>
#include <thread>
#include <vector>

namespace voxy::render {
namespace {
constexpr uint32_t width=128,height=128,stride=width*4;
constexpr size_t imageBytes=size_t(stride)*height;
struct Texture {
    WGPUTexture value=nullptr;
    WGPUTextureView view=nullptr;
    ~Texture(){release();}
    void release(){if(view)wgpuTextureViewRelease(view);if(value)wgpuTextureRelease(value);view=nullptr;value=nullptr;}
};
bool waterTexture(WGPUDevice device,WGPUQueue queue,Texture& texture,float heightOffset) {
    auto desc=gpu::TextureDesc::tex2D(2,2,WGPUTextureFormat_RGBA16Float,
        WGPUTextureUsage_TextureBinding|WGPUTextureUsage_CopyDst,"effects_constant_water");
    desc.depthOrArrayLayers=4;texture.value=gpu::createTexture(device,desc);
    if(!texture.value)return false;
    WGPUTextureViewDescriptor view{};view.format=desc.format;view.dimension=WGPUTextureViewDimension_2DArray;
    view.mipLevelCount=1;view.arrayLayerCount=4;view.aspect=WGPUTextureAspect_All;
    texture.view=wgpuTextureCreateView(texture.value,&view);if(!texture.view)return false;
    std::array<uint16_t,4*2*128> bytes{}; // 256-byte row stride, four array layers.
    for(size_t layer=0;layer<4;++layer)for(size_t row=0;row<2;++row)for(size_t column=0;column<2;++column)
        bytes[layer*256+row*128+column*4+1]=glm::packHalf1x16(layer==0?heightOffset:0);
    WGPUImageCopyTexture destination{};destination.texture=texture.value;destination.aspect=WGPUTextureAspect_All;
    WGPUTextureDataLayout layout{};layout.bytesPerRow=256;layout.rowsPerImage=2;
    const WGPUExtent3D extent{2,2,4};
    wgpuQueueWriteTexture(queue,&destination,bytes.data(),sizeof(bytes),&layout,&extent);return true;
}
struct Readback {
    WGPUBuffer value=nullptr;
    ~Readback(){if(value)wgpuBufferRelease(value);}
};
struct Pixels {std::vector<uint8_t> color;std::vector<float> depth;};
size_t lit(const Pixels& pixels) {
    size_t count=0;for(size_t i=0;i<pixels.color.size();i+=4)count+=pixels.color[i]>4||pixels.color[i+1]>4||pixels.color[i+2]>4;
    return count;
}
double centroidY(const Pixels& pixels) {
    double sum=0,count=0;
    for(uint32_t y=0;y<height;++y)for(uint32_t x=0;x<width;++x){const auto index=(size_t(y)*width+x)*4;
        if(pixels.color[index]>4||pixels.color[index+1]>4||pixels.color[index+2]>4){sum+=y;++count;}}
    return count?sum/count:-1;
}
}

TEST(CoveEffectsPathGpu, BoundedSpritesFollowWaterRespectDepthAndRetainSubmittedBindings) {
    gpu::Context context;ASSERT_TRUE(context.initHeadless());
    const auto errors=std::make_shared<std::atomic<unsigned>>(0);
    context.setErrorCallback([errors](WGPUErrorType,const char*){++*errors;});
    const auto device=context.getDevice();const auto queue=context.getQueue();
    Texture color,depth,waterA,waterB;
    auto colorDesc=gpu::TextureDesc::renderTarget(width,height,WGPUTextureFormat_RGBA8Unorm,"effect_color");
    colorDesc.usage|=WGPUTextureUsage_CopySrc;color.value=gpu::createTexture(device,colorDesc);
    auto depthDesc=gpu::TextureDesc::renderTarget(width,height,WGPUTextureFormat_R32Float,"effect_visible_depth");
    depthDesc.usage|=WGPUTextureUsage_CopySrc|WGPUTextureUsage_TextureBinding;
    depth.value=gpu::createTexture(device,depthDesc);
    ASSERT_NE(color.value,nullptr);ASSERT_NE(depth.value,nullptr);
    color.view=gpu::createTextureView(color.value);depth.view=gpu::createTextureView(depth.value);
    ASSERT_NE(color.view,nullptr);ASSERT_NE(depth.view,nullptr);
    ASSERT_TRUE(waterTexture(device,queue,waterA,0));ASSERT_TRUE(waterTexture(device,queue,waterB,4));
    const auto sampler=gpu::createSampler(device,gpu::SamplerDesc::linear("effect_water_sampler"));ASSERT_NE(sampler,nullptr);
    struct SamplerOwner {WGPUSampler value;~SamplerOwner(){if(value)wgpuSamplerRelease(value);}} samplerOwner{sampler};
    Readback readback;readback.value=gpu::createBuffer(device,{"effect_numeric_readback",imageBytes*2,WGPUBufferUsage_CopyDst|WGPUBufferUsage_MapRead});
    ASSERT_NE(readback.value,nullptr);
    CoveEffectsPath effects;ASSERT_TRUE(effects.init(device,queue,WGPUTextureFormat_RGBA8Unorm,"shaders/cove_effects.wgsl"));
    EXPECT_EQ(CoveEffectsPath::residentBytes,33328u);EXPECT_LE(CoveEffectsPath::residentBytes,128u*1024u);
    CameraUniforms camera;const glm::vec3 eye{0,5,-7};
    ASSERT_TRUE(camera.setCamera(glm::lookAtLH(eye,glm::vec3(0,.5f,0),glm::vec3(0,1,0)),
        glm::perspectiveLH_ZO(glm::radians(50.f),1.f,.1f,100.f),eye));
    ASSERT_TRUE(camera.setWater(true,0,{.2f,.5f,.4f},{.02f,.1f,.15f},.2f,.25f,1,7));
    camera.ambientExposure={.8f,.8f,.8f,1};camera.lightingColor={1,1,1,0};camera.metrics.w=0;
    CoveEffectsFrame frame{&camera,{}, {.displacementTexture=waterA.view,.displacementSampler=sampler}};
    CoveEffectsPath::Instance wake{{0,0,0,0},{0,0,0,0},{1.8f,1.8f,2,0},{.7f,.85f,.82f,.8f}};
    CoveEffectsPath::Instance dust{{0,3,0,4},{0,0,0,0},{.8f,.8f,1,0},{.6f,.4f,.2f,.8f}};
    const std::array particles{wake,dust};
    const auto render=[&](float obstruction,std::span<const CoveEffectsPath::Instance> instances,Pixels& pixels,bool retire=false){
        auto encoder=wgpuDeviceCreateCommandEncoder(device,nullptr);if(!encoder)return false;
        for(const auto& [target,value]:std::array<std::pair<WGPUTextureView,float>,2>{{{color.view,0},{depth.view,obstruction}}}){
            WGPURenderPassColorAttachment attachment{};attachment.view=target;attachment.depthSlice=WGPU_DEPTH_SLICE_UNDEFINED;
            attachment.loadOp=WGPULoadOp_Clear;attachment.storeOp=WGPUStoreOp_Store;attachment.clearValue={value,0,0,1};
            WGPURenderPassDescriptor passDesc{};passDesc.colorAttachmentCount=1;passDesc.colorAttachments=&attachment;
            auto clear=wgpuCommandEncoderBeginRenderPass(encoder,&passDesc);
            if(!clear){wgpuCommandEncoderRelease(encoder);return false;}
            wgpuRenderPassEncoderEnd(clear);wgpuRenderPassEncoderRelease(clear);
        }
        if(!effects.render(encoder,color.view,depth.view,frame,instances)){wgpuCommandEncoderRelease(encoder);return false;}
        for(size_t i=0;i<2;++i){
            WGPUImageCopyTexture source{};source.texture=i?depth.value:color.value;source.aspect=WGPUTextureAspect_All;
            WGPUImageCopyBuffer destination{};destination.buffer=readback.value;destination.layout.offset=i*imageBytes;
            destination.layout.bytesPerRow=stride;destination.layout.rowsPerImage=height;
            const WGPUExtent3D extent{width,height,1};wgpuCommandEncoderCopyTextureToBuffer(encoder,&source,&destination,&extent);
        }
        auto command=wgpuCommandEncoderFinish(encoder,nullptr);wgpuCommandEncoderRelease(encoder);if(!command)return false;
        wgpuQueueSubmit(queue,1,&command);wgpuCommandBufferRelease(command);
        if(retire){effects.shutdown();waterB.release();}
        const auto completion=std::make_shared<std::atomic<int>>(0);
        auto* callback=new std::shared_ptr<std::atomic<int>>(completion);
        wgpuBufferMapAsync(readback.value,WGPUMapMode_Read,0,imageBytes*2,[](WGPUBufferMapAsyncStatus status,void* data){
            const std::unique_ptr<std::shared_ptr<std::atomic<int>>> result(static_cast<std::shared_ptr<std::atomic<int>>*>(data));
            (*result)->store(status==WGPUBufferMapAsyncStatus_Success?1:2);
        },callback);
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(10);
        while(!completion->load()&&std::chrono::steady_clock::now()<deadline){context.tick();std::this_thread::sleep_for(std::chrono::milliseconds(1));}
        if(completion->load()!=1)return false;
        const auto* bytes=static_cast<const uint8_t*>(wgpuBufferGetConstMappedRange(readback.value,0,imageBytes*2));
        if(!bytes){wgpuBufferUnmap(readback.value);return false;}
        pixels.color.assign(bytes,bytes+imageBytes);pixels.depth.resize(size_t(width)*height);
        std::memcpy(pixels.depth.data(),bytes+imageBytes,imageBytes);wgpuBufferUnmap(readback.value);return true;
    };
    // Invalid input is refused before recording any draw/upload.
    auto discard=wgpuDeviceCreateCommandEncoder(device,nullptr);ASSERT_NE(discard,nullptr);
    auto invalid=wake;invalid.sizeLife.z=0;
    EXPECT_FALSE(effects.render(discard,color.view,depth.view,frame,std::span(&invalid,1)));
    EXPECT_EQ(effects.lastEncodedInstances(),0u);EXPECT_EQ(effects.uploadCount(),0u);
    ASSERT_TRUE(effects.render(discard,color.view,depth.view,frame,std::span(&wake,1)));
    wgpuCommandEncoderRelease(discard); // An abandoned frame cannot damage the next draw.
    Pixels visible,hidden,low,high;
    ASSERT_TRUE(render(100,particles,visible));EXPECT_GT(lit(visible),200u);
    EXPECT_TRUE(std::all_of(visible.depth.begin(),visible.depth.end(),[](float v){return v==100;}));
    ASSERT_TRUE(render(1,particles,hidden));EXPECT_EQ(lit(hidden),0u);
    EXPECT_TRUE(std::all_of(hidden.depth.begin(),hidden.depth.end(),[](float v){return v==1;}));
    EXPECT_EQ(effects.bindingChanges(),1u); // Same immutable views never reallocate bindings.
    ASSERT_TRUE(render(100,std::span(&wake,1),low));EXPECT_GT(lit(low),100u);
    frame.water.displacementTexture=waterB.view;
    ASSERT_TRUE(render(100,std::span(&wake,1),high));EXPECT_EQ(effects.bindingChanges(),2u);
    EXPECT_GT(lit(high),100u);EXPECT_GT(std::abs(centroidY(high)-centroidY(low)),3);
    Pixels afterRetirement;ASSERT_TRUE(render(100,std::span(&wake,1),afterRetirement,true));
    EXPECT_EQ(afterRetirement.color,high.color);EXPECT_FALSE(effects.initialized());
    RecordProperty("visiblePixels",static_cast<int>(lit(visible)));
    RecordProperty("surfacePixels",static_cast<int>(lit(high)));
    RecordProperty("surfaceShiftPixels",std::abs(centroidY(high)-centroidY(low)));
    RecordProperty("ownerBytes",static_cast<int>(CoveEffectsPath::residentBytes));
    context.tick();EXPECT_EQ(errors->load(),0u);context.setErrorCallback({});
}
} // namespace voxy::render
