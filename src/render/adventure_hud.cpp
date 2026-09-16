#include "render/adventure_hud.hpp"
#include "render/generated/adventure_piece_thumbnails.hpp"
#include "gpu/resources.hpp"
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstddef>
#include <glm/vec2.hpp>
#include <glm/common.hpp>

namespace voxy::render {
namespace {
static_assert(std::endian::native==std::endian::little);
static_assert(AdventureHudLayout::maximumTriangles==AdventureHudLayout::maximumVisibleThumbnails*adventure_thumbnails::maximumPieceTriangles);
constexpr glm::vec4 solidUv{1.5f/512.f,1.5f/256.f,0,0};
constexpr glm::vec4 brown{.19f,.14f,.10f,1},muted{.39f,.34f,.27f,1};
constexpr glm::vec4 moss{.22f,.34f,.20f,1},cream{.97f,.94f,.85f,.98f};
constexpr glm::vec4 edge{.65f,.56f,.40f,1};
float number(size_t n){return static_cast<float>(n);}
float actionWidth(const AdventureHudRow& row,float pixels) {
    return std::clamp(measureCoveHudText(row.label,pixels)+24,96.f,230.f);
}
struct ActionLine {size_t first=0,count=0;float occupied=0;};
struct GuideLines {std::vector<std::string_view> lines;bool complete=true;};
GuideLines guideLines(std::string_view source,float width,float pixels) {
    // Cards are short trusted content. Bound measurement and allocation even
    // if a future caller accidentally supplies a document instead of a tip.
    GuideLines result;
    result.complete=source.size()<=512;source=source.substr(0,512);
    size_t begin=0;
    while(begin<source.size()&&result.lines.size()<32) {
        size_t end=begin,lastSpace=begin;float measured=0;
        while(end<source.size()&&source[end]!='\n') {
            size_t next=end+1;
            while(next<source.size()&&(static_cast<unsigned char>(source[next])&0xc0)==0x80)++next;
            const float advance=measureCoveHudText(source.substr(end,next-end),pixels);
            if(measured+advance>width||next-begin>128)break;
            measured+=advance;if(source[end]==' ')lastSpace=end;end=next;
        }
        if(end<source.size()&&source[end]!='\n'&&lastSpace>begin)end=lastSpace;
        if(end==begin&&source[end]!='\n'){result.complete=false;break;}
        result.lines.push_back(source.substr(begin,end-begin));begin=end;
        if(begin<source.size()&&source[begin]=='\n')++begin;
        while(begin<source.size()&&source[begin]==' ')++begin;
    }
    result.complete=result.complete&&begin==source.size();return result;
}
std::vector<ActionLine> actionLines(const std::vector<AdventureHudRow>& rows,float width,float pixels) {
    std::vector<ActionLine> lines;
    for(size_t i=0;i<std::min(rows.size(),size_t{8});++i) {
        const float wanted=std::min(width,actionWidth(rows[i],pixels));
        if(lines.empty()||lines.back().occupied+6+wanted>width)lines.push_back({i,0,0});
        auto& line=lines.back();line.occupied+=(line.count?6:0)+wanted;++line.count;
    }
    return lines;
}
struct Painter {
    AdventureHudLayout& out;
    float width,height,pixels;
    bool contrast;
    void quad(glm::vec4 b,glm::vec4 color) {
        const float x=std::max(0.f,b.x),y=std::max(0.f,b.y);
        const float r=std::min(width,b.x+b.z),bottom=std::min(height,b.y+b.w);
        if(r<=x||bottom<=y)return;
        if(out.canvas.count==out.canvas.quads.size()){out.canvas.truncated=true;return;}
        out.canvas.quads[out.canvas.count++]={{x,y,r-x,bottom-y},solidUv,color};
    }
    void panel(glm::vec4 b) {
        out.panels.push_back(b);
        quad({b.x+2,b.y+4,b.z,b.w},{.09f,.07f,.04f,.24f});
        quad(b,edge);quad({b.x+1,b.y+1,b.z-2,b.w-2},contrast?glm::vec4(1,1,.95f,1):cream);
    }
    void text(std::string_view s,glm::vec4 b,glm::vec4 color=brown,size_t lines=1,float size=0) {
        (void)appendCoveHudText(out.canvas,s,b,size>0?size:pixels,color,lines);
    }
    void button(const AdventureHudRow& row,glm::vec4 b,bool selected=false,size_t index=SIZE_MAX) {
        if(b.z<44||b.w<44)return;
        quad(b,selected?moss:edge);
        quad({b.x+2,b.y+2,b.z-4,b.w-4},selected?glm::vec4(.88f,.91f,.78f,1):glm::vec4(.94f,.90f,.79f,1));
        if(selected)quad({b.x+4,b.y+5,4,b.w-10},moss);
        const float labelHeight=pixels*1.3f;
        text(row.label,{b.x+12,b.y+(b.w-labelHeight)*.5f,b.z-24,labelHeight},row.enabled?brown:muted);
        out.hits.push_back({b,row.action,row.value,row.intent,index,row.enabled});
        if(selected)out.selectedVisible=true;
    }
    void actions(const std::vector<AdventureHudRow>& rows,glm::vec4 b) {
        if(rows.empty())return;
        const auto lines=actionLines(rows,b.z,pixels);
        const float rowHeight=(b.w-6*number(lines.size()-1))/number(lines.size());
        for(size_t i=0;i<lines.size();++i) {
            const auto& line=lines[i];const float extra=(b.z-line.occupied)/number(line.count);float x=b.x;
            for(size_t j=0;j<line.count;++j) {
                const auto& item=rows[line.first+j];const float cell=std::min(b.z,actionWidth(item,pixels))+extra;
                button(item,{x,b.y+number(i)*(rowHeight+6),cell,rowHeight});x+=cell+6;
            }
        }
    }
    void miniature(uint8_t kind,glm::vec4 box) {
        namespace thumbnails=adventure_thumbnails;
        if(!kind||kind>thumbnails::pieces.size()||box.z<=0||box.w<=0)return;
        const auto& piece=thumbnails::pieces[kind-1u];
        // Never allocate from row count or emit a partial picture. The image
        // bounds stay inside its card, so the later triangle pass cannot cover
        // labels, costs, focus indicators or neighboring input targets.
        if(out.thumbnailCount==AdventureHudLayout::maximumVisibleThumbnails
            ||out.triangles.size()+piece.count>AdventureHudLayout::maximumTriangles) {
            out.trianglesTruncated=true;return;
        }
        const float scale=std::min(box.z/static_cast<float>(thumbnails::width),box.w/static_cast<float>(thumbnails::height));
        const float x=box.x+(box.z-static_cast<float>(thumbnails::width)*scale)*.5f;
        const float y=box.y+(box.w-static_cast<float>(thumbnails::height)*scale)*.5f;
        const float quantizedScale=scale/static_cast<float>(thumbnails::units);
        for(size_t i=piece.first;i<size_t{piece.first}+piece.count;++i) {
            const auto& source=thumbnails::triangles[i];AdventureHudTriangle triangle;
            for(size_t vertex=0;vertex<3;++vertex) {
                triangle.xy[vertex*2]=std::clamp(x+static_cast<float>(source.xy[vertex*2])*quantizedScale,box.x,box.x+box.z);
                triangle.xy[vertex*2+1]=std::clamp(y+static_cast<float>(source.xy[vertex*2+1])*quantizedScale,box.y,box.y+box.w);
            }
            triangle.rgba=source.rgba;out.triangles.push_back(triangle);
        }
        ++out.thumbnailCount;
    }
};
glm::vec4 tone(CoveHudTone t) {
    if(t==CoveHudTone::Blocked)return {.59f,.17f,.10f,1};
    if(t==CoveHudTone::Waiting)return {.48f,.31f,.06f,1};
    return moss;
}
float actionHeight(const std::vector<AdventureHudRow>& rows,float width,float pixels) {
    if(rows.empty())return 0;
    const size_t lines=actionLines(rows,width,pixels).size();
    return number(lines)*std::max(44.f,pixels*1.3f+16)+number(lines-1)*6;
}
}

AdventureHudLayout layoutAdventureHud(const AdventureHudContent& c,uint32_t width,uint32_t height) {
    AdventureHudLayout result;
    if(width<320||height<240)return result;
    const float w=static_cast<float>(width),h=static_cast<float>(height);
    const float s=std::isfinite(c.textScale)?std::clamp(c.textScale,1.f,1.5f):1.f;
    const float pixels=20*s,pitch=pixels*1.3f,margin=12,pad=14,buttonHeight=std::max(44.f,pitch+16);
    result.canvas.bodyPixels=pixels;
    Painter p{result,w,h,pixels,c.highContrast};
    const float maxWidth=w-2*margin;
    if(c.mode==AdventureHudMode::Explore) {
        if(!c.objective.empty()) {
            const float pw=std::min(maxWidth,440*s),ph=pitch*2+2*pad;
            p.panel({margin,margin,pw,ph});p.quad({margin,margin,4,ph},moss);
            p.text(c.objective,{margin+pad,margin+pad,pw-2*pad,2*pitch},brown,2);
        }
        if(!c.compass.empty()) {
            const float pw=std::min(maxWidth,320*s),y=c.objective.empty()?margin:margin+2*pitch+2*pad+8;
            p.panel({margin,y,pw,pitch+16});p.text(c.compass,{margin+pad,y+8,pw-2*pad,pitch},moss);
        }
        const float qw=std::min(maxWidth,680*s),quickHeight=actionHeight(c.quickActions,qw,pixels),qy=h-margin-quickHeight;
        p.actions(c.quickActions,{(w-qw)*.5f,qy,qw,quickHeight});
        const auto& context=c.context.empty()?c.status:c.context;
        if(!context.empty()) {
            const float cw=std::min(maxWidth,600*s),ch=pitch*2+16,cy=qy-ch-10;
            p.panel({(w-cw)*.5f,cy,cw,ch});p.text(context,{(w-cw)*.5f+pad,cy+8,cw-2*pad,2*pitch},tone(c.tone),2);
        }
    } else if(c.mode==AdventureHudMode::Build||c.mode==AdventureHudMode::Catalog) {
        const bool catalog=c.mode==AdventureHudMode::Catalog||c.pickerOpen;
        const float pw=std::min(maxWidth,1180.f),px=(w-pw)*.5f;
        const float controlHeight=actionHeight(c.buildControls,pw-2*pad,pixels);
        const float controls=c.buildControls.empty()?0:controlHeight+8;
        if(!catalog) {
            const float ph=std::min(h-margin*2,std::max(76.f,2*pitch)+2*pad+controls),py=h-margin-ph;
            p.panel({px,py,pw,ph});
            const float thumb=std::min(92.f,ph-controls-2*pad);
            p.miniature(c.pieceKind,{px+pad,py+pad,thumb,thumb});
            const float left=px+pad+thumb+16,right=pw-thumb-3*pad;
            p.text(c.selected+"  /  "+c.cost,{left,py+pad,right,pitch},brown);
            p.text(c.status.empty()?c.context:c.status,{left,py+pad+pitch,right,pitch},tone(c.tone));
            p.actions(c.buildControls,{px+pad,py+ph-pad-controlHeight,pw-2*pad,controlHeight});
            result.selectedVisible=true;
        } else {
            if(!c.cost.empty()||!c.status.empty()) {
                const float infoWidth=std::min(maxWidth,650.f),infoHeight=2*pitch+16;
                p.panel({margin,margin,infoWidth,infoHeight});
                p.text(c.selected+"  /  "+c.cost,{margin+pad,margin+8,infoWidth-2*pad,pitch},brown);
                p.text(c.status,{margin+pad,margin+8+pitch,infoWidth-2*pad,pitch},tone(c.tone));
            }
            const float cardHeight=std::max(112.f,64.f+2*pitch),header=buttonHeight+16;
            const float ph=std::min(h-margin*2,header+cardHeight+2*pad+controls);
            const float py=h-margin-ph;
            p.panel({px,py,pw,ph});
            const float tabWidth=(pw-2*pad-12)/3;
            static constexpr std::array<std::string_view,3> names{"Structure","Bricks","Furniture"};
            for(size_t i=0;i<3;++i) {
                auto category=c.categories[i];if(category.label.empty())category.label=names[i];
                p.button(category,{px+pad+number(i)*(tabWidth+6),py+pad,tabWidth,buttonHeight},i==static_cast<size_t>(c.paletteCategory));
            }
            result.selectedVisible=false; // Category focus is independent of selected piece.
            const size_t capacity=std::clamp(static_cast<size_t>((pw-2*pad+8)/(138*s+8)),size_t{1},size_t{AdventureHudLayout::maximumVisibleThumbnails});
            const size_t count=std::min(capacity,c.rows.size());
            if(count) {
                const size_t selected=std::min(c.selectedRow,c.rows.size()-1);
                const size_t first=std::min(selected>count/2?selected-count/2:0,c.rows.size()-count);
                const float cell=(pw-2*pad-8*number(count-1))/number(count),y=py+pad+header;
                for(size_t i=0;i<count;++i) {
                    const size_t index=first+i;const auto& row=c.rows[index];
                    const glm::vec4 b{px+pad+number(i)*(cell+8),y,cell,cardHeight};
                    p.quad(b,index==selected?moss:edge);p.quad({b.x+3,b.y+3,b.z-6,b.w-6},{.94f,.90f,.79f,1});
                    if(row.pieceKind)p.miniature(row.pieceKind,{b.x+12,b.y+9,b.z-24,50});
                    else p.text("HOME",{b.x+12,b.y+16,b.z-24,pitch},moss);
                    p.text(row.label,{b.x+10,b.y+64,b.z-20,pitch},row.enabled?brown:muted);
                    if(!row.detail.empty())p.text(row.detail,{b.x+10,b.y+64+pitch,b.z-20,pitch},muted);
                    result.hits.push_back({b,row.action,row.value,row.intent,index,row.enabled});
                    result.selectedVisible|=index==selected;
                }
            }
            p.actions(c.buildControls,{px+pad,py+ph-pad-controlHeight,pw-2*pad,controlHeight});
        }
    } else if(c.mode==AdventureHudMode::Guide) {
        // Tips get their measured body first. Navigation can scroll around its
        // selected row on a narrow window; it never takes space from the tip.
        const float pw=std::min(maxWidth,900.f),px=(w-pw)*.5f,inner=pw-2*pad,maxSheet=h-2*margin;
        const auto title=guideLines(c.title,inner,pixels),body=guideLines(c.menuText,inner,pixels);
        const size_t titleCount=std::min(title.lines.size(),size_t{2});
        const float titleHeight=number(titleCount)*pitch+8;
        const size_t rowCount=std::min(c.rows.size(),size_t{8}); // Six topics plus return; individual cards use at most four.
        const size_t selected=rowCount?std::min(c.selectedRow,rowCount-1):0;
        std::vector<ActionLine> navigation;
        for(size_t i=0;i<rowCount;++i) {
            const float wanted=std::min(inner,std::max(96.f,measureCoveHudText(c.rows[i].label,pixels)+24));
            if(navigation.empty()||navigation.back().occupied+6+wanted>inner)navigation.push_back({i,0,0});
            auto& line=navigation.back();line.occupied+=(line.count?6:0)+wanted;++line.count;
        }
        const float minimumNavigation=rowCount?buttonHeight+8:0;
        float statusHeight=c.menuStatus.empty()?0:pitch+6;
        const float wantedBody=number(body.lines.size())*pitch+(body.lines.empty()?0:8);
        if(2*pad+titleHeight+wantedBody+statusHeight+minimumNavigation>maxSheet)statusHeight=0;
        const float bodyAvailable=std::max(0.f,maxSheet-2*pad-titleHeight-statusHeight-minimumNavigation-8);
        const size_t bodyCount=std::min(body.lines.size(),static_cast<size_t>(std::floor(bodyAvailable/pitch)));
        const float bodyHeight=number(bodyCount)*pitch+(bodyCount?8:0);
        const float spare=maxSheet-2*pad-titleHeight-bodyHeight-statusHeight;
        const size_t capacity=static_cast<size_t>(std::max(0.f,std::floor((spare+6)/(buttonHeight+6))));
        const size_t count=std::min(capacity,navigation.size());
        size_t selectedLine=0;
        for(size_t i=0;i<navigation.size();++i)if(selected>=navigation[i].first&&selected<navigation[i].first+navigation[i].count)selectedLine=i;
        const size_t first=count?std::min(selectedLine>=count?selectedLine-count+1:0,navigation.size()-count):0;
        const float navigationHeight=count?number(count)*buttonHeight+number(count-1)*6:0;
        const float ph=std::min(maxSheet,2*pad+titleHeight+bodyHeight+statusHeight+navigationHeight),py=(h-ph)*.5f;
        p.panel({px,py,pw,ph});p.quad({px,py,pw,4},moss);
        float y=py+pad;
        for(size_t i=0;i<titleCount;++i){p.text(title.lines[i],{px+pad,y,inner,pitch},brown);y+=pitch;}
        y+=8;
        result.guideBodyBounds={px+pad,y,inner,number(bodyCount)*pitch};
        result.guideBodyFirstQuad=result.canvas.count;
        result.guideBodyComplete=body.complete&&bodyCount==body.lines.size();
        for(size_t i=0;i<bodyCount;++i) {
            const auto rendered=appendCoveHudText(result.canvas,body.lines[i],{px+pad,y,inner,pitch},pixels,muted);
            result.guideBodyComplete=result.guideBodyComplete&&!rendered.clipped;y+=pitch;
        }
        result.guideBodyLines=bodyCount;result.guideBodyQuadCount=result.canvas.count-result.guideBodyFirstQuad;
        result.guideBodyComplete=result.guideBodyComplete&&!result.canvas.truncated;
        if(bodyCount)y+=8;
        if(statusHeight>0){p.text(c.menuStatus,{px+pad,y,inner,pitch},moss);y+=statusHeight;}
        for(size_t lineIndex=first;lineIndex<first+count;++lineIndex) {
            const auto& line=navigation[lineIndex];const float extra=(inner-line.occupied)/number(line.count);float x=px+pad;
            for(size_t j=0;j<line.count;++j) {
                const size_t index=line.first+j;const auto& row=c.rows[index];
                const float cell=std::min(inner,std::max(96.f,measureCoveHudText(row.label,pixels)+24))+extra;
                p.button(row,{x,y,cell,buttonHeight},index==selected,index);x+=cell+6;
            }
            y+=buttonHeight+6;
        }
    } else {
        // Each focused sheet replaces exploration controls. The selected choice
        // remains in a bounded scrolling window even at the largest text size.
        const bool dialogue=c.mode==AdventureHudMode::Dialogue;
        const float pw=std::min(maxWidth,dialogue?820.f:760.f),px=(w-pw)*.5f;
        const size_t selected=c.rows.empty()?0:std::min(c.selectedRow,c.rows.size()-1);
        const float titleHeight=pitch+14;
        const size_t desiredLines=dialogue?4u:3u;
        const float maxSheet=std::min(h-2*margin,std::max(h*.65f,330.f));
        const float statusHeight=c.menuStatus.empty()?0:pitch+8;
        const float navigationHeight=actionHeight(c.buildControls,pw-2*pad,pixels);
        const float navigationSpace=navigationHeight>0?navigationHeight+8:0;
        const float available=maxSheet-2*pad-titleHeight-statusHeight-buttonHeight-navigationSpace;
        const size_t textLines=c.menuText.empty()?0:std::min(desiredLines,static_cast<size_t>(std::max(0.f,std::floor(available/pitch))));
        const float textHeight=number(textLines)*pitch+(textLines?12.f:0);
        const float rowHeight=buttonHeight;
        const float spare=maxSheet-2*pad-titleHeight-textHeight-statusHeight-navigationSpace;
        const size_t capacity=std::max(size_t{1},static_cast<size_t>(std::max(0.f,std::floor((spare+8)/(rowHeight+8)))));
        const size_t count=std::min({capacity,c.rows.size(),size_t{6}});
        const size_t first=count?std::min(selected>count/2?selected-count/2:0,c.rows.size()-count):0;
        const float ph=std::min(maxSheet,2*pad+titleHeight+textHeight+statusHeight+number(count)*(rowHeight+8)+navigationSpace);
        const float py=dialogue?h-margin-ph:std::max(margin,(h-ph)*.5f);
        p.panel({px,py,pw,ph});p.quad({px,py,pw,4},moss);
        float y=py+pad;p.text(c.title,{px+pad,y,pw-2*pad,pitch},brown);y+=titleHeight;
        if(textLines){p.text(c.menuText,{px+pad,y,pw-2*pad,number(textLines)*pitch},muted,textLines);y+=textHeight;}
        if(statusHeight>0){p.text(c.menuStatus,{px+pad,y,pw-2*pad,pitch},tone(c.tone));y+=statusHeight;}
        for(size_t i=0;i<count;++i) {
            const size_t index=first+i;auto row=c.rows[index];
            if(!row.detail.empty())row.label+="  /  "+row.detail;
            p.button(row,{px+pad,y,pw-2*pad,rowHeight},index==selected,index);y+=rowHeight+8;
        }
        p.actions(c.buildControls,{px+pad,py+ph-pad-navigationHeight,pw-2*pad,navigationHeight});
    }
    if(!result.panels.empty())result.canvas.panel=result.panels.front();
    return result;
}

void AdventureHudPath::setContent(AdventureHudContent content) {
    updated_=std::chrono::steady_clock::now();
    if(contentSet_&&content_==content)return;
    content_=std::move(content);contentSet_=true;
    backend_.setLayoutFactory([this](uint32_t w,uint32_t h){layout_=layoutAdventureHud(content_,w,h);trianglesDirty_=true;return layout_.canvas;});
}

bool AdventureHudPath::init(WGPUDevice device,WGPUQueue queue,WGPUTextureFormat format,const std::filesystem::path& fontShader) {
    shutdown();
    if(!backend_.init(device,queue,format,fontShader))return false;
    queue_=queue;
    triangles_=gpu::createBuffer(device,gpu::BufferDesc::vertex(maximumBufferBytes,"adventure_hud_triangles"));
    shader_=gpu::loadShaderModule(device,fontShader.parent_path()/"adventure_hud_triangles.wgsl","adventure_hud_triangles");
    pipelineLayout_=gpu::createPipelineLayout(device,{},"adventure_hud_triangles_layout");
    if(!triangles_||!shader_||!pipelineLayout_){shutdown();return false;}
    // Emscripten's WebGPU header adds nextInChain ahead of these fields.
    // Zero-initialize optional fields and assign shared members by name.
    std::array<WGPUVertexAttribute,4> attributes{};
    for(uint32_t i=0;i<3;++i) {
        attributes[i].format=WGPUVertexFormat_Float32x2;
        attributes[i].offset=uint64_t{i}*8;
        attributes[i].shaderLocation=i;
    }
    attributes[3].format=WGPUVertexFormat_Unorm8x4;
    attributes[3].offset=offsetof(AdventureHudTriangle,rgba);
    attributes[3].shaderLocation=3;
    WGPUVertexBufferLayout vertices{};
    vertices.arrayStride=sizeof(AdventureHudTriangle);vertices.stepMode=WGPUVertexStepMode_Instance;
    vertices.attributeCount=attributes.size();vertices.attributes=attributes.data();
    WGPUBlendState blend{};
    blend.color.operation=WGPUBlendOperation_Add;blend.color.srcFactor=WGPUBlendFactor_SrcAlpha;
    blend.color.dstFactor=WGPUBlendFactor_OneMinusSrcAlpha;
    blend.alpha.operation=WGPUBlendOperation_Add;blend.alpha.srcFactor=WGPUBlendFactor_One;
    blend.alpha.dstFactor=WGPUBlendFactor_OneMinusSrcAlpha;
    WGPUColorTargetState color{};color.format=format;color.writeMask=WGPUColorWriteMask_All;color.blend=&blend;
    WGPUFragmentState fragment{};fragment.module=shader_;WGPU_SET_ENTRY_POINT(fragment,"fs");
    fragment.targetCount=1;fragment.targets=&color;
    WGPURenderPipelineDescriptor descriptor{};WGPU_SET_LABEL(descriptor,"adventure_hud_triangle_pipeline");
    descriptor.layout=pipelineLayout_;descriptor.vertex.module=shader_;WGPU_SET_ENTRY_POINT(descriptor.vertex,"vs");
    descriptor.vertex.bufferCount=1;descriptor.vertex.buffers=&vertices;
    descriptor.primitive.topology=WGPUPrimitiveTopology_TriangleList;
    descriptor.primitive.frontFace=WGPUFrontFace_CCW;descriptor.primitive.cullMode=WGPUCullMode_None;
    descriptor.multisample.count=1;descriptor.multisample.mask=0xffffffff;descriptor.fragment=&fragment;
    pipeline_=wgpuDeviceCreateRenderPipeline(device,&descriptor);
    if(!pipeline_){shutdown();return false;}
    return true;
}
void AdventureHudPath::shutdown() noexcept {
    // Submitted work retains these references until its actual completion.
    // Release handles; do not destroy a buffer still used by an encoded frame.
    backend_.shutdown();
    if(pipeline_)wgpuRenderPipelineRelease(pipeline_);
    if(pipelineLayout_)wgpuPipelineLayoutRelease(pipelineLayout_);
    if(shader_)wgpuShaderModuleRelease(shader_);
    if(triangles_)wgpuBufferRelease(triangles_);
    pipeline_=nullptr;pipelineLayout_=nullptr;shader_=nullptr;triangles_=nullptr;queue_=nullptr;
    lastEncodedTriangles_=0;triangleUploadCount_=0;trianglesDirty_=true;
    layout_={};content_={};contentSet_=false;updated_={};
}
bool AdventureHudPath::render(WGPUCommandEncoder encoder,WGPUTextureView target,uint32_t width,uint32_t height) {
    clearEncodedObservation();
    if(!initialized()||!encoder||!target||!width||!height)return false;
    if(!backend_.render(encoder,target,width,height)||layout_.trianglesTruncated
        ||layout_.triangles.size()>maximumTriangleCount)return false;
    if(layout_.triangles.empty())return true;
    if(trianglesDirty_) {
        auto data=layout_.triangles;
        for(auto& triangle:data)for(size_t i=0;i<3;++i) {
            triangle.xy[i*2]=2*triangle.xy[i*2]/static_cast<float>(width)-1;
            triangle.xy[i*2+1]=1-2*triangle.xy[i*2+1]/static_cast<float>(height);
        }
        wgpuQueueWriteBuffer(queue_,triangles_,0,data.data(),data.size()*sizeof(AdventureHudTriangle));
        ++triangleUploadCount_;trianglesDirty_=false;
    }
    WGPURenderPassColorAttachment color{};color.view=target;color.loadOp=WGPULoadOp_Load;color.storeOp=WGPUStoreOp_Store;
#if defined(VOXY_WASM)
    color.depthSlice=WGPU_DEPTH_SLICE_UNDEFINED;
#endif
    WGPURenderPassDescriptor pass{};WGPU_SET_LABEL(pass,"adventure_hud_thumbnails");pass.colorAttachmentCount=1;pass.colorAttachments=&color;
    auto draw=wgpuCommandEncoderBeginRenderPass(encoder,&pass);
    if(!draw)return false;
    wgpuRenderPassEncoderSetPipeline(draw,pipeline_);
    wgpuRenderPassEncoderSetVertexBuffer(draw,0,triangles_,0,layout_.triangles.size()*sizeof(AdventureHudTriangle));
    wgpuRenderPassEncoderDraw(draw,3,static_cast<uint32_t>(layout_.triangles.size()),0,0);
    wgpuRenderPassEncoderEnd(draw);wgpuRenderPassEncoderRelease(draw);
    lastEncodedTriangles_=static_cast<uint32_t>(layout_.triangles.size());return true;
}
}
