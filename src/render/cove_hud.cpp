#include "render/cove_hud.hpp"
#include "gpu/resources.hpp"
#include <algorithm>
#include <cmath>
#include <span>
#include <string_view>

namespace voxy::render {
namespace {
#include "render/cove_hud_font.inc"
constexpr glm::vec4 solidUv{1.5f/512,1.5f/256,0,0};
constexpr glm::vec4 ink{.94f,.96f,.95f,1};
constexpr glm::vec4 secondary{.73f,.80f,.81f,1};
float uiScale(uint32_t height) noexcept {return std::clamp(float(height)/800.f,1.f,1.5f);}
float panelWidth(uint32_t width,uint32_t height) noexcept {
    return std::min((width<960?280.f:400.f)*uiScale(height),float(width)*.46f);
}
glm::vec4 toneColor(CoveHudTone tone) noexcept {
    switch(tone){
        case CoveHudTone::Ready:return {.46f,.91f,.70f,1};
        case CoveHudTone::Blocked:return {1,.64f,.58f,1};
        case CoveHudTone::Waiting:return {1,.82f,.42f,1};
        case CoveHudTone::Neutral:return {.61f,.83f,.89f,1};
    }
    return ink;
}
std::string printable(std::string_view input) {
    std::string result;
    result.reserve(std::min(input.size(),size_t{160}));
    for(size_t i=0;i<input.size()&&result.size()<160;++i){
        const auto c=static_cast<unsigned char>(input[i]);
        if(c>=32&&c<=126)result.push_back(input[i]);
        else if(c<32)result.push_back(' ');
        else if(c==0xc3&&i+1<input.size()&&static_cast<unsigned char>(input[i+1])==0x97){result.push_back('x');++i;}
        else if((c&0xc0)!=0x80)result.push_back('?');
    }
    return result;
}
float advance(char c,float scale) {return float(hudGlyphs[size_t(c-32)].advance64)*(scale/64.f);}
}

glm::dvec4 coveHudWorkshopRectangle(uint32_t width,uint32_t height) noexcept {
    if(width==0||height==0)return {-.9,-.5,.9,.9};
    const double margin=14*double(uiScale(height));
    const double left=2*(double(panelWidth(width,height))+2*margin)/double(width)-1;
    return {std::clamp(left,-.9,.65),-.5,.9,.9};
}

std::vector<uint8_t> decodeCoveHudAtlas() {
    std::vector<uint8_t> bytes;
    bytes.reserve(512u*256u);
    static_assert(std::size(hudAtlasRle)%2==0);
    for(size_t i=0;i<std::size(hudAtlasRle);i+=2){
        if(bytes.size()+hudAtlasRle[i]>512u*256u)return {};
        bytes.insert(bytes.end(),hudAtlasRle[i],hudAtlasRle[i+1]);
    }
    if(bytes.size()!=512u*256u)return {};
    return bytes;
}

CoveHudLayout layoutCoveHud(const CoveHudContent& content,uint32_t width,uint32_t height) {
    CoveHudLayout layout;
    if(width<160||height<160||content.title.empty())return layout;
    const float scale=uiScale(height),margin=14*scale,pad=16*scale;
    const float panel=panelWidth(width,height),left=margin+pad,right=margin+panel-pad;
    const float bottom=float(height)*.75f-margin;
    const float pitch=29*scale;
    layout.bodyPixels=20*scale;
    layout.panel={margin,margin,panel,0};
    const auto add=[&](CoveHudQuad q){
        if(layout.count==layout.quads.size()){layout.truncated=true;return;}
        layout.quads[layout.count++]=q;
    };
    add({layout.panel,solidUv,{.035f,.065f,.075f,.97f}});
    add({{margin,margin,4*scale,0},solidUv,toneColor(content.tone)});
    float y=margin+10*scale;
    const auto text=[&](std::string_view source,float pixels,glm::vec4 color,size_t maxLines){
        const auto value=printable(source);
        size_t begin=0,lines=0;
        const float glyphScale=pixels/32;
        while(begin<value.size()){
            if(lines++==maxLines||y+pitch>bottom){layout.truncated=true;break;}
            size_t end=begin,lastSpace=begin;
            float measured=0;
            while(end<value.size()){
                const float next=advance(value[end],glyphScale);
                if(measured+next>right-left)break;
                measured+=next;
                if(value[end]==' ')lastSpace=end;
                ++end;
            }
            if(end==begin){layout.truncated=true;break;}
            if(end<value.size()&&lastSpace>begin)end=lastSpace;
            float x=left;
            for(size_t i=begin;i<end;++i){
                const auto& g=hudGlyphs[size_t(value[i]-32)];
                if(g.width&&g.height)add({{x+float(g.left)*glyphScale,y+float(g.top)*glyphScale,
                    float(g.width)*glyphScale,float(g.height)*glyphScale},
                    {float(g.x)/512,float(g.y)/256,float(g.width)/512,float(g.height)/256},color});
                x+=advance(value[i],glyphScale);
            }
            y+=pitch;
            begin=end;
            while(begin<value.size()&&value[begin]==' ')++begin;
        }
    };
    text(content.title,24*scale,ink,1);
    y+=6*scale;
    text(content.selected,20*scale,ink,2);
    text(content.economy,20*scale,secondary,2);
    y+=5*scale;
    text(content.status,20*scale,toneColor(content.tone),2);
    y+=9*scale;
    for(const auto& hint:content.hints)text(hint,20*scale,secondary,2);
    layout.panel.w=std::min(y+12*scale,bottom)-margin;
    layout.quads[0].bounds=layout.panel;
    layout.quads[1].bounds.w=layout.panel.w;
    return layout;
}

bool CoveHudPath::init(WGPUDevice device,WGPUQueue queue,WGPUTextureFormat format,const std::filesystem::path& shader) {
    shutdown();
    if(!device||!queue)return false;
    device_=device;queue_=queue;
    const auto bytes=decodeCoveHudAtlas();
    if(bytes.empty()){shutdown();return false;}
    atlas_=gpu::createTextureWithData(device,queue,
        gpu::TextureDesc::tex2D(512,256,WGPUTextureFormat_R8Unorm,
            WGPUTextureUsage_TextureBinding|WGPUTextureUsage_CopyDst,"cove_hud_font"),std::as_bytes(std::span(bytes)),512);
    if(atlas_)atlasView_=gpu::createTextureView(atlas_);
    sampler_=gpu::createSampler(device,gpu::SamplerDesc::linear("cove_hud_font_sampler"));
    quads_=gpu::createBuffer(device,gpu::BufferDesc::vertex(CoveHudLayout::maximumQuads*sizeof(CoveHudQuad),"cove_hud_quads"));
    shader_=gpu::loadShaderModule(device,shader,"cove_hud_shader");
    const std::array entries{
        gpu::BindGroupLayoutEntry(0).fragmentVisible().texture(WGPUTextureSampleType_Float),
        gpu::BindGroupLayoutEntry(1).fragmentVisible().sampler(WGPUSamplerBindingType_Filtering)};
    bindingsLayout_=gpu::createBindGroupLayout(device,entries,"cove_hud_bindings");
    if(!atlas_||!atlasView_||!sampler_||!quads_||!shader_||!bindingsLayout_){shutdown();return false;}
    const std::array groups{bindingsLayout_};
    pipelineLayout_=gpu::createPipelineLayout(device,groups,"cove_hud_pipeline_layout");
    const std::array bindings{gpu::BindGroupEntry(0).textureView(atlasView_),gpu::BindGroupEntry(1).sampler(sampler_)};
    bindings_=gpu::createBindGroup(device,bindingsLayout_,bindings,"cove_hud_bind_group");
    std::array<WGPUVertexAttribute,3> attributes{};
    for(uint32_t i=0;i<attributes.size();++i){
        attributes[i].format=WGPUVertexFormat_Float32x4;
        attributes[i].offset=uint64_t{i}*16;
        attributes[i].shaderLocation=i;
    }
    WGPUVertexBufferLayout vertices{};
    vertices.arrayStride=sizeof(CoveHudQuad);vertices.stepMode=WGPUVertexStepMode_Instance;
    vertices.attributeCount=attributes.size();vertices.attributes=attributes.data();
    WGPUBlendState blend{};
    blend.color={WGPUBlendOperation_Add,WGPUBlendFactor_SrcAlpha,WGPUBlendFactor_OneMinusSrcAlpha};
    blend.alpha={WGPUBlendOperation_Add,WGPUBlendFactor_One,WGPUBlendFactor_OneMinusSrcAlpha};
    WGPUColorTargetState color{};color.format=format;color.writeMask=WGPUColorWriteMask_All;color.blend=&blend;
    WGPUFragmentState fragment{};fragment.module=shader_;WGPU_SET_ENTRY_POINT(fragment,"fs");
    fragment.targetCount=1;fragment.targets=&color;
    WGPURenderPipelineDescriptor descriptor{};WGPU_SET_LABEL(descriptor,"cove_hud_pipeline");
    descriptor.layout=pipelineLayout_;descriptor.vertex.module=shader_;WGPU_SET_ENTRY_POINT(descriptor.vertex,"vs");
    descriptor.vertex.bufferCount=1;descriptor.vertex.buffers=&vertices;
    descriptor.primitive.topology=WGPUPrimitiveTopology_TriangleList;
    descriptor.primitive.frontFace=WGPUFrontFace_CCW;descriptor.primitive.cullMode=WGPUCullMode_None;
    descriptor.multisample.count=1;descriptor.multisample.mask=0xffffffff;
    descriptor.fragment=&fragment;
    pipeline_=wgpuDeviceCreateRenderPipeline(device,&descriptor);
    if(!bindings_||!pipelineLayout_||!pipeline_){shutdown();return false;}
    return true;
}

void CoveHudPath::shutdown() noexcept {
    // Release rather than destroy: already-submitted command buffers retain all
    // referenced resources until their actual queue completion.
    if(pipeline_)wgpuRenderPipelineRelease(pipeline_);
    if(bindings_)wgpuBindGroupRelease(bindings_);
    if(pipelineLayout_)wgpuPipelineLayoutRelease(pipelineLayout_);
    if(bindingsLayout_)wgpuBindGroupLayoutRelease(bindingsLayout_);
    if(shader_)wgpuShaderModuleRelease(shader_);
    if(quads_)wgpuBufferRelease(quads_);
    if(sampler_)wgpuSamplerRelease(sampler_);
    if(atlasView_)wgpuTextureViewRelease(atlasView_);
    if(atlas_)wgpuTextureRelease(atlas_);
    pipeline_=nullptr;bindings_=nullptr;pipelineLayout_=nullptr;bindingsLayout_=nullptr;
    shader_=nullptr;quads_=nullptr;sampler_=nullptr;atlasView_=nullptr;atlas_=nullptr;device_=nullptr;queue_=nullptr;
    width_=height_=lastEncodedQuads_=0;uploadCount_=0;dirty_=true;layout_={};contentUpdated_={};
}
bool CoveHudPath::needsContentUpdate() const noexcept {
    return std::chrono::steady_clock::now()-contentUpdated_>=std::chrono::milliseconds(100);
}
void CoveHudPath::setContent(CoveHudContent content) {
    if(content_!=content){content_=std::move(content);dirty_=true;}
    contentUpdated_=std::chrono::steady_clock::now();
}
bool CoveHudPath::render(WGPUCommandEncoder encoder,WGPUTextureView target,uint32_t width,uint32_t height) {
    clearEncodedObservation();
    if(!pipeline_||!encoder||!target||width==0||height==0)return false;
    if(dirty_||width_!=width||height_!=height){
        layout_=layoutCoveHud(content_,width,height);width_=width;height_=height;dirty_=false;
        auto vertices=layout_.quads;
        for(size_t i=0;i<layout_.count;++i){
            auto& b=vertices[i].bounds;
            b={2*b.x/float(width)-1,1-2*b.y/float(height),2*b.z/float(width),-2*b.w/float(height)};
        }
        if(layout_.count){wgpuQueueWriteBuffer(queue_,quads_,0,vertices.data(),layout_.count*sizeof(CoveHudQuad));++uploadCount_;}
    }
    if(!layout_.count)return true;
    WGPURenderPassColorAttachment color{};color.view=target;color.loadOp=WGPULoadOp_Load;color.storeOp=WGPUStoreOp_Store;
#if defined(VOXY_WASM)
    color.depthSlice=WGPU_DEPTH_SLICE_UNDEFINED;
#endif
    WGPURenderPassDescriptor pass{};WGPU_SET_LABEL(pass,"cove_hud_overlay");pass.colorAttachmentCount=1;pass.colorAttachments=&color;
    auto draw=wgpuCommandEncoderBeginRenderPass(encoder,&pass);
    if(!draw)return false;
    wgpuRenderPassEncoderSetPipeline(draw,pipeline_);
    wgpuRenderPassEncoderSetBindGroup(draw,0,bindings_,0,nullptr);
    wgpuRenderPassEncoderSetVertexBuffer(draw,0,quads_,0,layout_.count*sizeof(CoveHudQuad));
    wgpuRenderPassEncoderDraw(draw,6,static_cast<uint32_t>(layout_.count),0,0);
    wgpuRenderPassEncoderEnd(draw);wgpuRenderPassEncoderRelease(draw);
    lastEncodedQuads_=static_cast<uint32_t>(layout_.count);
    return true;
}
}
