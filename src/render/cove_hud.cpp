#include "gpu/pipeline.hpp"
#include "render/cove_hud.hpp"
#include "gpu/resources.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <span>
#include <string_view>

namespace voxy::render {
namespace {
#include "render/cove_hud_font.inc"
constexpr glm::vec4 solidUv{1.5f/512,1.5f/256,0,0};
constexpr glm::vec4 ink{.94f,.96f,.95f,1};
constexpr glm::vec4 secondary{.73f,.80f,.81f,1};
float uiScale(uint32_t height) noexcept {return std::clamp(float(height)/800.f,1.f,1.5f);}
float panelWidth(uint32_t width,uint32_t height,float textScale=1) noexcept {
    return std::min((width<960?280.f:400.f)*uiScale(height)*textScale,float(width)*.46f);
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
        else if((c&0xc0)!=0x80) {
            uint32_t cp=c;size_t extra=0;
            if((c&0xe0)==0xc0){cp=c&31;extra=1;}
            else if((c&0xf0)==0xe0){cp=c&15;extra=2;}
            else if((c&0xf8)==0xf0){cp=c&7;extra=3;}
            bool valid=extra&&i+extra<input.size();
            for(size_t j=1;j<=extra&&valid;++j) {
                const auto next=static_cast<unsigned char>(input[i+j]);
                if((next&0xc0)!=0x80)valid=false;else cp=(cp<<6)|(next&63);
            }
            if(valid){
                char escaped[16];std::snprintf(escaped,sizeof escaped,"[U+%04X]",cp);
                result+=escaped;i+=extra;
            }else result+="[invalid]";
        }
    }
    return result;
}
float advance(char c,float scale) {return float(hudGlyphs[size_t(c-32)].advance64)*(scale/64.f);}
}

glm::dvec4 coveHudWorkshopRectangle(uint32_t width,uint32_t height,float textScale) noexcept {
    if(width==0||height==0)return {-.9,-.5,.9,.9};
    const double margin=14*double(uiScale(height));
    const double left=2*(double(panelWidth(width,height,textScale))+2*margin)/double(width)-1;
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

float measureCoveHudText(std::string_view source,float pixels) {
    if(!std::isfinite(pixels)||pixels<=0)return 0;
    float result=0;
    for(const auto c:printable(source))result+=advance(c,pixels/32.f);
    return result;
}

CoveHudTextResult appendCoveHudText(CoveHudLayout& layout,std::string_view source,
    glm::vec4 bounds,float pixels,glm::vec4 color,size_t maximumLines) {
    CoveHudTextResult result;
    if(bounds.z<=0||bounds.w<=0||!std::isfinite(pixels)||pixels<=0||!maximumLines)return result;
    auto value=printable(source);
    const float scale=pixels/32.f,pitch=pixels*1.3f;
    const size_t lines=std::min(maximumLines,static_cast<size_t>(std::max(0.f,std::floor(bounds.w/pitch))));
    size_t begin=0;
    for(size_t line=0;begin<value.size()&&line<lines;++line) {
        size_t end=begin,lastSpace=begin;float measured=0;
        while(end<value.size()) {
            const float next=advance(value[end],scale);
            if(measured+next>bounds.z)break;
            measured+=next;if(value[end]==' ')lastSpace=end;++end;
        }
        const bool overflow=end<value.size();
        if(overflow&&line+1<lines&&lastSpace>begin)end=lastSpace;
        std::string current=value.substr(begin,end-begin);
        if(overflow&&line+1==lines) {
            result.clipped=true;
            while(!current.empty()&&measureCoveHudText(current+"...",pixels)>bounds.z)current.pop_back();
            if(measureCoveHudText("...",pixels)<=bounds.z)current+="...";
        }
        float x=bounds.x;const float y=bounds.y+static_cast<float>(line)*pitch;
        for(const auto c:current) {
            const auto& g=hudGlyphs[static_cast<size_t>(c-32)];
            if(g.width&&g.height) {
                const glm::vec4 original{x+static_cast<float>(g.left)*scale,y+static_cast<float>(g.top)*scale,
                    static_cast<float>(g.width)*scale,static_cast<float>(g.height)*scale};
                const float left=std::max(original.x,bounds.x),top=std::max(original.y,bounds.y);
                const float right=std::min(original.x+original.z,bounds.x+bounds.z),bottom=std::min(original.y+original.w,bounds.y+bounds.w);
                if(right>left&&bottom>top) {
                    if(layout.count==layout.quads.size()){layout.truncated=true;return result;}
                    layout.quads[layout.count++]={{left,top,right-left,bottom-top},
                        {(static_cast<float>(g.x)+(left-original.x)/scale)/512.f,
                         (static_cast<float>(g.y)+(top-original.y)/scale)/256.f,
                         (right-left)/(scale*512.f),(bottom-top)/(scale*256.f)},color};
                }
            }
            x+=advance(c,scale);
        }
        ++result.lines;begin=end;while(begin<value.size()&&value[begin]==' ')++begin;
        if(end==begin&&current.empty())break;
    }
    result.clipped|=begin<value.size();
    return result;
}

CoveHudLayout layoutCoveHud(const CoveHudContent& content,uint32_t width,uint32_t height) {
    CoveHudLayout layout;
    if(width<160||height<160||content.title.empty())return layout;
    const float requested=std::isfinite(content.textScale)?std::clamp(content.textScale,1.f,1.5f):1.f;
    const float scale=uiScale(height)*requested,margin=14*scale,pad=16*scale;
    const bool menu=content.menu.has_value();
    const float panel=menu?std::min(920.f*scale,float(width)-2*margin):panelWidth(width,height,requested);
    const float panelX=menu?(float(width)-panel)*.5f
        :(content.rightAligned?float(width)-panel-margin:margin);
    const float left=panelX+pad,right=panelX+panel-pad;
    const float bottom=(menu?float(height):float(height)*.75f)-margin;
    const float pitch=29*scale;
    layout.bodyPixels=20*scale;
    layout.panel={panelX,margin,panel,0};
    const auto add=[&](CoveHudQuad q){
        if(layout.count==layout.quads.size()){layout.truncated=true;return;}
        layout.quads[layout.count++]=q;
    };
    add({layout.panel,solidUv,content.highContrast?glm::vec4(.005f,.01f,.015f,1):glm::vec4(.035f,.065f,.075f,.97f)});
    add({{panelX,margin,4*scale,0},solidUv,toneColor(content.tone)});
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
    if(content.menu) {
        const auto& model=*content.menu;
        size_t subtitleLines=std::clamp(model.subtitleLineLimit,size_t{1},size_t{4}),statusLines=2,nameLines=2,keyRows=4;
        if(model.naming) {
            // Keep a readable name, the selected letter row and a selected
            // button visible at150% on short windows. The40-key grid keeps its
            // logical navigation; only its visible row window scrolls.
            const auto lines=static_cast<size_t>(std::max(0.f,std::floor((bottom-y-18*scale)/pitch)));
            size_t spare=lines>5?lines-5:0; // title,name,key row,button,footer
            statusLines=model.status.empty()?0:std::min(size_t{2},spare);spare-=statusLines;
            subtitleLines=model.subtitle.empty()?0:std::min(size_t{2},spare);spare-=subtitleLines;
            keyRows=1+std::min(size_t{3},spare);spare-=keyRows-1;
            nameLines=spare?2:1;
        }
        text(model.title,24*scale,ink,1);
        if(subtitleLines)text(model.subtitle,20*scale,secondary,subtitleLines);
        if(statusLines)text(model.status,20*scale,toneColor(content.tone),statusLines);
        y+=5*scale;
        if(model.naming) {
            text(model.name.empty()?"Name: _":"Name: "+model.name,20*scale,ink,nameLines);
            const float keyWidth=(right-left)/10;
            const size_t firstKeyRow=std::min(std::min(model.key,size_t{39})/10,size_t{4}-keyRows);
            for(size_t row=firstKeyRow;row<firstKeyRow+keyRows;++row) {
                if(y+pitch>bottom){layout.truncated=true;break;}
                const float rowY=y;
                for(size_t column=0;column<10;++column) {
                    const size_t k=row*10+column;
                    const glm::vec4 bounds{left+float(column)*keyWidth,rowY,keyWidth-3*scale,pitch-2*scale};
                    if(model.keyboardFocus&&model.key==k)add({bounds,solidUv,{.13f,.34f,.37f,1}});
                    layout.menuHits.push_back({bounds,-1,static_cast<int>(k)});
                    const char c=kCoveNameKeys[k];const auto& g=hudGlyphs[size_t(c-32)];
                    const float gs=20*scale/32;
                    if(g.width&&g.height)add({{bounds.x+8*scale+float(g.left)*gs,rowY+float(g.top)*gs,float(g.width)*gs,float(g.height)*gs},
                        {float(g.x)/512,float(g.y)/256,float(g.width)/512,float(g.height)/256},ink});
                }
                y+=pitch;
            }
            y+=6*scale;
        }
        // Reserve the footer lines and their glyph budget before choosing a
        // scroll window. The selected row must remain visible on short screens.
        const float footerSpace=model.naming?pitch:2*pitch;
        const size_t heightRows=static_cast<size_t>(std::max(0.f,std::floor((bottom-footerSpace-7*scale-y)/pitch)));
        const size_t glyphRows=layout.count+100<CoveHudLayout::maximumQuads
            ?(CoveHudLayout::maximumQuads-layout.count-100)/50:0;
        const size_t visible=std::min({size_t{8},model.rows.size(),heightRows,glyphRows});
        const size_t first=model.rows.size()>visible?std::min(model.selected>=visible?model.selected-visible+1:0,model.rows.size()-visible):0;
        for(size_t row=first;row<first+visible;++row) {
            if(y+pitch>bottom-footerSpace-7*scale){layout.truncated=true;break;}
            const glm::vec4 bounds{left-6*scale,y,right-left+12*scale,pitch};
            if(row==model.selected&&(!model.naming||!model.keyboardFocus))add({bounds,solidUv,{.13f,.34f,.37f,1}});
            layout.menuHits.push_back({bounds,static_cast<int>(row),-1});
            std::string label=printable(model.rows[row].label);
            float measured=0;size_t count=0;
            const float dots=3*advance('.',20*scale/32);
            while(count<label.size()&&count<45&&measured+advance(label[count],20*scale/32)+dots<=right-left) {
                measured+=advance(label[count],20*scale/32);++count;
            }
            if(count<label.size())label=label.substr(0,count)+"...";
            text(label,20*scale,model.rows[row].enabled?ink:glm::vec4(.5f,.55f,.56f,1),1);
        }
        y+=7*scale;
        text(model.naming?"Arrows: Letter   Tab / LB: Buttons"
            :"A / Enter: Choose   B / Esc: Back",20*scale,secondary,1);
        if(!model.naming)text("D-pad / arrows: Move   Scroll: More",20*scale,secondary,1);
        layout.panel.w=std::min(y+12*scale,bottom)-margin;
        layout.quads[0].bounds=layout.panel;layout.quads[1].bounds.w=layout.panel.w;
        return layout;
    }
    text(content.title,24*scale,ink,2);
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
    pipeline_=::voxy::gpu::createRenderPipeline(device,&descriptor);
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
    if(layoutFactory_){layoutFactory_={};dirty_=true;}
    if(content_!=content){content_=std::move(content);dirty_=true;}
    contentUpdated_=std::chrono::steady_clock::now();
}
void CoveHudPath::setLayoutFactory(std::function<CoveHudLayout(uint32_t,uint32_t)> factory) {
    layoutFactory_=std::move(factory);dirty_=true;
    contentUpdated_=std::chrono::steady_clock::now();
}
bool CoveHudPath::render(WGPUCommandEncoder encoder,WGPUTextureView target,uint32_t width,uint32_t height) {
    clearEncodedObservation();
    if(!pipeline_||!encoder||!target||width==0||height==0)return false;
    if(dirty_||width_!=width||height_!=height){
        layout_=layoutFactory_?layoutFactory_(width,height):layoutCoveHud(content_,width,height);
        width_=width;height_=height;dirty_=false;
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
