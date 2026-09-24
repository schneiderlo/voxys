#include "gpu/pipeline.hpp"
#include "render/adventure_hud.hpp"
#include "render/generated/adventure_hud_art.hpp"
#include "render/generated/adventure_piece_thumbnails.hpp"
#include "gpu/resources.hpp"
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstddef>
#include <limits>
#include <glm/vec2.hpp>
#include <glm/common.hpp>

namespace voxy::render {
namespace {
static_assert(std::endian::native==std::endian::little);
static_assert(AdventureHudLayout::maximumTriangles==AdventureHudLayout::maximumMeshThumbnails*adventure_thumbnails::maximumPieceTriangles);
constexpr glm::vec4 solidUv{1.5f/512.f,1.5f/256.f,0,0};
constexpr glm::vec4 brown{.19f,.14f,.10f,1},muted{.39f,.34f,.27f,1};
constexpr glm::vec4 moss{.22f,.34f,.20f,1},cream{.97f,.94f,.85f,.98f};
constexpr glm::vec4 edge{.65f,.56f,.40f,1};
constexpr glm::vec4 chalk{.97f,.97f,.92f,1},fog{.72f,.78f,.77f,1};
constexpr glm::vec4 gold{1.f,.82f,.24f,1},night{.075f,.13f,.15f,.86f};
float number(size_t n){return static_cast<float>(n);}
float actionWidth(const AdventureHudRow& row,float pixels) {
    return std::clamp(measureCoveHudText(row.label,pixels)+24,96.f,230.f);
}
struct ActionLine {size_t first=0,count=0;float occupied=0;};
struct GuideLines {std::vector<std::string_view> lines;bool complete=true;};
GuideLines guideLines(std::string_view source,float width,float pixels,bool creative=false) {
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
            const float advance=creative?measureAdventureHudText(source.substr(end,next-end),pixels):measureCoveHudText(source.substr(end,next-end),pixels);
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
    bool creative=false;
    uint32_t paint=0;
    void quad(glm::vec4 b,glm::vec4 color) {
        const float x=std::max(0.f,b.x),y=std::max(0.f,b.y);
        const float r=std::min(width,b.x+b.z),bottom=std::min(height,b.y+b.w);
        if(r<=x||bottom<=y)return;
        if(out.canvas.count==out.canvas.quads.size()){out.canvas.truncated=true;return;}
        out.canvas.quads[out.canvas.count++]={{x,y,r-x,bottom-y},solidUv,color};
    }
    void panel(glm::vec4 b) {
        out.panels.push_back(b);
        if(creative) {
            rounded({b.x,b.y+3,b.z,b.w},{.015f,.03f,.035f,.20f},18);
            rounded(b,{.7f,.8f,.8f,.32f},18);
            rounded({b.x+1,b.y+1,b.z-2,b.w-2},contrast?glm::vec4(.025f,.055f,.065f,1):night,17);
            return;
        }
        quad({b.x+2,b.y+4,b.z,b.w},{.09f,.07f,.04f,.24f});
        quad(b,edge);quad({b.x+1,b.y+1,b.z-2,b.w-2},contrast?glm::vec4(1,1,.95f,1):cream);
    }
    void text(std::string_view s,glm::vec4 b,glm::vec4 color=brown,size_t lines=1,float size=0) {
        if(creative) {
            if(color==brown)color=chalk;
            else if(color==muted)color=fog;
            else if(color==moss)color=gold;
        }
        if(creative)(void)appendAdventureHudText(out.canvas,s,b,size>0?size:pixels,color,lines);
        else (void)appendCoveHudText(out.canvas,s,b,size>0?size:pixels,color,lines);
    }
    void rounded(glm::vec4 b,glm::vec4 color,float radius=12) {
        if(b.z<=0||b.w<=0||b.x<0||b.y<0||b.x+b.z>width+.01f||b.y+b.w>height+.01f)return;
        if(out.canvas.count==out.canvas.quads.size()){out.canvas.truncated=true;return;}
        out.canvas.quads[out.canvas.count++]={b,{-1,radius,b.z,b.w},color};
    }
    void stroke(glm::vec4 b,glm::vec4 color,float radius,float thickness) {
        if(thickness<=0||b.z<=0||b.w<=0||b.x<0||b.y<0||b.x+b.z>width+.01f||b.y+b.w>height+.01f)return;
        if(out.canvas.count==out.canvas.quads.size()){out.canvas.truncated=true;return;}
        out.canvas.quads[out.canvas.count++]={b,{-3-thickness,radius,b.z,b.w},color};
    }
    void shadowText(std::string_view value,glm::vec4 b,glm::vec4 color=chalk,float size=0) {
        text(value,{b.x+1,b.y+1,b.z,b.w},{.015f,.03f,.02f,.90f},1,size);
        text(value,b,color,1,size);
    }
    void centerText(std::string_view value,glm::vec4 b,glm::vec4 color=chalk,float size=0,bool shadow=false) {
        const float px=size>0?size:pixels;
        const float tw=std::min(b.z,creative?measureAdventureHudText(value,px):measureCoveHudText(value,px)),th=px*1.3f;
        const glm::vec4 bounds{b.x+(b.z-tw)*.5f,b.y+(b.w-th)*.5f,tw,th};
        if(shadow)shadowText(value,bounds,color,px);
        else text(value,bounds,color,1,px);
    }
    void surface(glm::vec4 b,bool selected=false) {
        rounded(b,contrast?glm::vec4(.025f,.045f,.035f,1):glm::vec4(.055f,.09f,.07f,.64f),11);
        stroke(b,selected?gold:glm::vec4(.94f,.94f,.85f,.64f),11,selected?2.5f:1.5f);
    }
    void button(const AdventureHudRow& row,glm::vec4 b,bool selected=false,size_t index=SIZE_MAX) {
        if(b.z<44||b.w<44)return;
        if(creative) {
            surface(b,selected);
            text(row.label,{b.x+12,b.y+(b.w-pixels*1.3f)*.5f,b.z-24,pixels*1.3f},row.enabled?chalk:fog);
            out.hits.push_back({b,row.action,row.value,row.intent,index,row.enabled,row.label});
            if(selected)out.selectedVisible=true;
            return;
        }
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
        if(creative) {
            if(out.thumbnailCount==AdventureHudLayout::maximumVisibleThumbnails) {
                out.trianglesTruncated=true;return;
            }
            if(out.canvas.count==out.canvas.quads.size()){out.canvas.truncated=true;return;}
            const float side=std::min(box.z,box.w);
            const glm::vec4 bounds{box.x+(box.z-side)*.5f,box.y+(box.w-side)*.5f,side,side};
            const glm::vec4 ink=paint?glm::vec4(float((paint>>16)&255u)/255.f,float((paint>>8)&255u)/255.f,float(paint&255u)/255.f,1):glm::vec4(1);
            const float column=float((kind-1u)%8u),row=float((kind-1u)/8u);
            out.canvas.quads[out.canvas.count++]={bounds,{(paint?4.f:2.f)+column*.125f,row*.25f,.125f,.25f},ink};
            ++out.thumbnailCount;return;
        }
        const auto& piece=thumbnails::pieces[kind-1u];
        // Never allocate from row count or emit a partial picture. The image
        // bounds stay inside its card, so the later triangle pass cannot cover
        // labels, costs, focus indicators or neighboring input targets.
        if(out.thumbnailCount==AdventureHudLayout::maximumMeshThumbnails
            ||out.triangles.size()+piece.count>AdventureHudLayout::maximumTriangles) {
            out.trianglesTruncated=true;return;
        }
        const float scale=std::min(box.z/static_cast<float>(thumbnails::width),box.w/static_cast<float>(thumbnails::height));
        const float x=box.x+(box.z-static_cast<float>(thumbnails::width)*scale)*.5f;
        const float y=box.y+(box.w-static_cast<float>(thumbnails::height)*scale)*.5f;
        const float quantizedScale=scale/static_cast<float>(thumbnails::units);
        uint32_t brightest=1;
        if(paint)for(size_t i=piece.first;i<size_t{piece.first}+piece.count;++i) {
            const uint32_t rgb=thumbnails::triangles[i].rgba;
            brightest=std::max({brightest,rgb&255u,(rgb>>8)&255u,(rgb>>16)&255u});
        }
        for(size_t i=piece.first;i<size_t{piece.first}+piece.count;++i) {
            const auto& source=thumbnails::triangles[i];AdventureHudTriangle triangle;
            for(size_t vertex=0;vertex<3;++vertex) {
                triangle.xy[vertex*2]=std::clamp(x+static_cast<float>(source.xy[vertex*2])*quantizedScale,box.x,box.x+box.z);
                triangle.xy[vertex*2+1]=std::clamp(y+static_cast<float>(source.xy[vertex*2+1])*quantizedScale,box.y,box.y+box.w);
            }
            triangle.rgba=source.rgba;
            if(paint) {
                // Retain the installed model's face shading while showing the
                // chosen paint. Original materials remain byte-identical.
                const float shade=float(std::max({source.rgba&255u,(source.rgba>>8)&255u,(source.rgba>>16)&255u}))/float(brightest);
                const auto channel=[&](uint32_t byte){return static_cast<uint32_t>(std::lround(float(byte)*shade));};
                triangle.rgba=channel((paint>>16)&255u)|(channel((paint>>8)&255u)<<8)|(channel(paint&255u)<<16)|0xff000000u;
            }
            out.triangles.push_back(triangle);
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

// Action artwork and small vector symbols share the existing ordered quad
// pass. Neither consumes the building-piece thumbnail budget.
void creativeIcon(Painter& p,int action,glm::vec4 b,glm::vec4 ink=chalk) {
    const float x=b.x+b.z*.5f,y=b.y+b.w*.5f;
    if(action==32||action==33||action==8) {
        const auto& region=adventure_hud_art::controlRegions[action==32?size_t{0}:action==33?size_t{1}:size_t{2}];
        const float side=std::min({38.f,b.z,b.w});
        const glm::vec4 bounds{x-side*.5f,y-side*.5f,side,side};
        if(side<=0||bounds.x<0||bounds.y<0||bounds.x+side>p.width+.01f||bounds.y+side>p.height+.01f)return;
        if(p.out.canvas.count==p.out.canvas.quads.size()){p.out.canvas.truncated=true;return;}
        const float atlasWidth=static_cast<float>(adventure_hud_art::width),atlasHeight=static_cast<float>(adventure_hud_art::height);
        const glm::vec4 uv{2+static_cast<float>(region.x)/atlasWidth,static_cast<float>(region.y)/atlasHeight,
            static_cast<float>(region.width)/atlasWidth,static_cast<float>(region.height)/atlasHeight};
        p.out.canvas.quads[p.out.canvas.count++]={bounds,uv,{1,1,1,ink==fog?.45f:ink.a}};
        return;
    }
    const auto bar=[&](float dx,float dy,float w,float h){p.rounded({x+dx,y+dy,w,h},ink,std::min(w,h)*.5f);};
    switch(action) {
    case 23:case 21: // Four pieces / enter build.
        for(float dx:{-10.f,2.f})for(float dy:{-10.f,2.f})bar(dx,dy,8,8);
        break;
    case 38: // Paint colours.
        p.rounded({x-11,y-10,12,12},{.9f,.49f,.4f,1},6);
        p.rounded({x+1,y-6,12,12},gold,6);
        p.rounded({x-7,y+2,12,12},{.53f,.72f,.68f,1},6);break;
    case 9:case 31:
        for(float dy:{-8.f,-1.f,6.f})bar(-10,dy,20,2);
        break;
    case 12:p.centerText("+",b,ink,24);break;
    case 5:case 20:case 22:p.centerText(action==22?"B":"x",b,ink,22);break;
    case 3:p.centerText("R",b,ink,20);break;
    case 6:p.centerText("Z",b,ink,20);break;
    case 29:p.centerText("?",b,ink,22);break;
    case 7:p.centerText("E",b,ink,20);break;
    default:p.centerText("...",b,ink,20);break;
    }
}
void creativeControl(Painter& p,const AdventureHudRow& row,glm::vec4 b,bool label=false,size_t index=SIZE_MAX) {
    p.surface(b);
    const glm::vec4 icon{b.x,b.y,44,b.w};
    if(row.action==25)p.centerText(row.value<0?"<":">",label?icon:b,row.enabled?chalk:fog,24);
    else if(row.action==12&&row.value<0)p.centerText("-",icon,row.enabled?chalk:fog,24);
    else creativeIcon(p,row.action,icon,row.enabled?chalk:fog);
    const std::string_view caption=row.action==32&&row.label=="Motorbike"?"Ride":
        row.action==33&&row.label=="Leave cannon"?"Leave":std::string_view(row.label);
    if(label)p.text(caption,
        {b.x+42,b.y+(b.w-p.pixels*1.3f)*.5f,b.z-50,p.pixels*1.3f},row.enabled?chalk:fog);
    p.out.hits.push_back({b,row.action,row.value,row.intent,index,row.enabled,row.label});
}
void creativeMenuButton(Painter& p,const AdventureHudRow& row,glm::vec4 box) {
    if((row.action==25||row.action==20)&&measureAdventureHudText(row.label,p.pixels)+24>box.z) {
        if(row.action==20) {
            p.surface(box);p.centerText("x",box,chalk,22);
            p.out.hits.push_back({box,row.action,row.value,row.intent,SIZE_MAX,row.enabled,row.label});
        } else creativeControl(p,row,box);
    } else p.button(row,box);
}
void creativeBareControl(Painter& p,const AdventureHudRow& row,glm::vec4 b) {
    if(row.action==25)p.centerText(row.value<0?"<":">",b,row.enabled?chalk:fog,22);
    else creativeIcon(p,row.action,b,row.enabled?chalk:fog);
    p.out.hits.push_back({b,row.action,row.value,row.intent,SIZE_MAX,row.enabled,row.label});
}
void creativeTop(Painter& p,const AdventureHudContent& c) {
    if(c.topActions.empty())return;
    const size_t count=std::min(c.topActions.size(),size_t{4});
    const float cell=44,gap=8,all=number(count)*cell+number(count-1)*gap;
    const float y=p.width>=760&&p.height>=540?(p.width>=1100?250.f:218.f):12.f;
    float x=p.width-26-all;
    for(size_t i=0;i<count;++i) {
        const glm::vec4 box{x,y,cell,cell};
        p.rounded(box,p.contrast?night:glm::vec4(.055f,.085f,.065f,.40f),22);
        creativeBareControl(p,c.topActions[i],box);x+=cell+gap;
    }
    const auto saving=std::find_if(c.topActions.begin(),c.topActions.end(),[](const auto& row){return row.action==8&&!row.detail.empty();});
    if(saving!=c.topActions.end()) {
        const float sw=std::min(p.width-32,measureAdventureHudText(saving->detail,p.pixels)+16);
        p.text(saving->detail,{p.width-26-sw,y+52,sw,p.pixels*1.3f},chalk);
    }
}
void creativeHotbar(Painter& p,const AdventureHudContent& c,float areaWidth=0) {
    const bool narrow=p.width<620;
    const float cell=narrow?64.f:84.f,cardHeight=narrow?64.f:80.f,gap=narrow?8.f:10.f;
    const float usable=areaWidth>0?areaWidth:p.width-24;
    const size_t capacity=std::clamp(static_cast<size_t>((usable+gap)/(cell+gap)),size_t{1},size_t{6});
    const size_t count=std::min(capacity,c.hotbar.size());
    size_t selected=0;
    for(size_t i=0;i<c.hotbar.size();++i)if((c.quickSlot&&c.hotbar[i].action==39&&c.hotbar[i].value==c.quickSlot)||
        (!c.quickSlot&&c.hotbar[i].pieceKind==c.pieceKind&&c.hotbar[i].paint==c.paint)){selected=i;break;}
    const size_t first=count?std::min(selected>count/2?selected-count/2:0,c.hotbar.size()-count):0;
    const float all=number(count)*cell+number(count?count-1:0)*gap;
    const float bx=(areaWidth>0?areaWidth:p.width)*.5f-all*.5f,by=p.height-60-cardHeight;
    for(size_t i=0;i<count;++i) {
        const auto& row=c.hotbar[first+i];const bool chosen=first+i==selected;
        const auto slot=row.action==39&&row.value>=1&&row.value<=6?static_cast<uint32_t>(row.value):static_cast<uint32_t>(i+1);
        const glm::vec4 box{bx+number(i)*(cell+gap),by,cell,cardHeight};
        p.surface(box,chosen);
        const auto activePaint=p.paint;p.paint=row.paint;
        p.miniature(row.pieceKind,{box.x+3,box.y+1,cell-6,cardHeight-3});p.paint=activePaint;
        const float keyWidth=narrow?24.f:28.f,keyHeight=26;
        const glm::vec4 key{box.x+(cell-keyWidth)*.5f,box.y+cardHeight-12,keyWidth,keyHeight};
        p.rounded(key,chosen?gold:glm::vec4(.88f,.89f,.81f,.75f),5);
        if(!chosen)p.rounded({key.x+1,key.y+1,key.z-2,key.w-2},{.08f,.12f,.085f,.90f},4);
        p.centerText(std::to_string(slot),key,chosen?glm::vec4(.16f,.18f,.08f,1):chalk,std::min(17.f,p.pixels*.875f));
        p.out.hits.push_back({{box.x,box.y,box.z,box.w+14},row.action,row.value,row.intent,SIZE_MAX,row.enabled,row.label});
        p.out.hits.back().shortcutKey=static_cast<uint32_t>('0')+slot;
        p.out.selectedVisible|=chosen;
    }
    if(!count&&c.pieceKind){p.miniature(c.pieceKind,{p.width*.5f-40,by,80,80});p.out.selectedVisible=true;}
}
void creativeHints(Painter& p,const AdventureHudContent& c) {
    std::vector<const AdventureHudRow*> rows;
    for(int action:{23,3,38,22}) {
        if(p.width<500&&action==22)continue;
        const auto item=std::find_if(c.buildControls.begin(),c.buildControls.end(),[&](const auto& row){return row.action==action;});
        if(item!=c.buildControls.end())rows.push_back(&*item);
    }
    if(rows.empty())return;
    const float cell=std::min(112.f,(p.width-24)/number(rows.size())),all=cell*number(rows.size());
    for(size_t i=0;i<rows.size();++i) {
        const auto& row=*rows[i];const glm::vec4 box{(p.width-all)*.5f+number(i)*cell,p.height-45,cell,44};
        const std::string_view key=!row.detail.empty()?std::string_view(row.detail):row.action==23?"Tab":row.action==3?"R":row.action==38?"P":"B";
        const float font=p.pixels*.80f,kw=std::max(20.f,measureAdventureHudText(key,font)+8);
        const float labelWidth=measureAdventureHudText(row.label,font),content=std::min(cell-6,kw+6+labelWidth),x=box.x+(cell-content)*.5f;
        p.rounded({x,box.y+11,kw,22},{.93f,.94f,.86f,.40f},4);
        p.rounded({x+1,box.y+12,kw-2,20},{.055f,.085f,.065f,.72f},3);
        p.centerText(key,{x,box.y+11,kw,22},row.enabled?chalk:fog,font);
        p.shadowText(row.label,{x+kw+6,box.y+(44-font*1.3f)*.5f,cell-(x-box.x)-kw-10,font*1.3f},row.enabled?chalk:fog,font);
        p.out.hits.push_back({box,row.action,row.value,row.intent,SIZE_MAX,row.enabled,row.label});
    }
}
void creativeCategories(Painter& p,const AdventureHudContent& c,glm::vec4 b,bool keyboardHints=true) {
    const float cell=b.z/3;
    static constexpr std::array<std::string_view,3> names{"Parts","Bricks","Home"};
    for(size_t i=0;i<3;++i) {
        const auto& row=c.categories[i];const glm::vec4 box{b.x+number(i)*cell,b.y,cell,44};
        const bool selected=i==static_cast<size_t>(c.paletteCategory);
        p.centerText(names[i],box,selected?gold:chalk,std::min(p.pixels,18.f),true);
        if(selected)p.rounded({box.x+cell*.5f-10,box.y+36,20,2},gold,1);
        p.out.hits.push_back({box,row.action,row.value,row.intent,SIZE_MAX,row.enabled,row.label});
    }
    if(keyboardHints) {
        p.centerText("Q",{b.x-20,b.y,16,44},fog,12,true);
        p.centerText("E",{b.x+b.z+4,b.y,16,44},fog,12,true);
    }
}
void creativeSwatch(Painter& p,const AdventureHudContent& c,size_t index,glm::vec4 b) {
    const auto& row=c.colours[index];const uint32_t rgb=static_cast<uint32_t>(row.value);
    const glm::vec4 fill=rgb?glm::vec4(float((rgb>>16)&255u)/255.f,float((rgb>>8)&255u)/255.f,float(rgb&255u)/255.f,1):glm::vec4(.69f,.59f,.43f,1);
    const bool chosen=rgb==c.paint,focused=index==c.selectedRow;
    p.rounded(b,p.contrast?night:glm::vec4(.055f,.09f,.07f,.72f),b.z*.5f);
    const float inset=chosen||focused?3.f:1.5f;
    p.stroke(b,focused?chalk:chosen?gold:glm::vec4(.92f,.93f,.86f,.64f),b.z*.5f,inset);
    p.rounded({b.x+7,b.y+7,b.z-14,b.w-14},fill,(b.z-14)*.5f);
    if(!rgb)p.centerText("O",{b.x+7,b.y+7,b.z-14,b.w-14},{.15f,.18f,.15f,1},16);
    if(chosen)p.rounded({b.x+b.z-11,b.y+3,7,7},gold,3.5f);
    const glm::vec4 hit{b.x+(b.z-44)*.5f,b.y+(b.w-44)*.5f,44,44};
    p.out.hits.push_back({hit,row.action,row.value,row.intent,index,row.enabled,row.label});
}
void creativePieceDisk(Painter& p,const AdventureHudRow& row,glm::vec2 center,float diameter,bool selected,size_t index) {
    const float r=diameter*.5f,inset=selected?3.f:1.5f;
    const glm::vec4 disk{center.x-r,center.y-r,diameter,diameter};
    p.rounded(disk,p.contrast?night:glm::vec4(.055f,.09f,.07f,.72f),r);
    p.stroke(disk,selected?gold:glm::vec4(.93f,.94f,.88f,.67f),r,inset);
    p.miniature(row.pieceKind,{center.x-r+3,center.y-r+3,diameter-6,diameter-6});
    // Disks can overlap visually, while their inset hit targets are disjoint.
    // This keeps touch, mouse and semantic peers on the same exact authority.
    const float target=selected?56.f:44.f;
    p.out.hits.push_back({{center.x-target*.5f,center.y-target*.5f,target,target},row.action,row.value,row.intent,index,row.enabled,row.label});
}
AdventureHudLayout layoutCreativeHud(const AdventureHudContent& c,uint32_t width,uint32_t height) {
    AdventureHudLayout result;
    const float w=static_cast<float>(width),h=static_cast<float>(height),margin=12;
    const float scale=std::isfinite(c.textScale)?std::clamp(c.textScale,1.f,1.5f):1.f;
    const float pixels=18*scale,pitch=pixels*1.3f,available=w-2*margin;
    result.canvas.bodyPixels=pixels;
    Painter p{result,w,h,pixels,c.highContrast,true,c.paint};
    const auto finish=[&](){
        if(!c.hoverLabel.empty()&&c.hoverBounds.z>=44&&c.hoverBounds.w>=44) {
            const float tw=std::min(available,measureAdventureHudText(c.hoverLabel,pixels)+24),th=pitch+12;
            const float tx=std::clamp(c.hoverBounds.x+(c.hoverBounds.z-tw)*.5f,margin,w-margin-tw);
            float ty=c.hoverBounds.y-th-8;
            if(ty<margin)ty=c.hoverBounds.y+c.hoverBounds.w+8;
            ty=std::clamp(ty,margin,h-margin-th);
            p.rounded({tx,ty,tw,th},night,8);p.centerText(c.hoverLabel,{tx+8,ty,tw-16,th});
        }
        if(!result.panels.empty())result.canvas.panel=result.panels.front();
        return result;
    };
    if(c.mode==AdventureHudMode::Explore) {
        creativeTop(p,c);
        const size_t count=std::min(c.quickActions.size(),size_t{4});
        const float cell=std::min(112.f,(available-8*number(count?count-1:0))/number(std::max(count,size_t{1})));
        const float all=number(count)*cell+8*number(count?count-1:0),y=h-margin-44;
        for(size_t i=0;i<count;++i) {
            const auto& row=c.quickActions[i];const glm::vec4 box{(w-all)*.5f+number(i)*(cell+8),y,cell,44};
            p.rounded(box,{.065f,.10f,.07f,.54f},12);
            creativeIcon(p,row.action,{box.x,box.y,38,44},row.enabled?chalk:fog);
            p.text(row.label,{box.x+38,box.y+(44-pitch)*.5f,box.z-44,pitch},row.enabled?chalk:fog);
            result.hits.push_back({box,row.action,row.value,row.intent,SIZE_MAX,row.enabled,row.label});
        }
        const auto& feedback=c.status.empty()?c.context:c.status;
        if(!feedback.empty()&&h>=340) {
            const float fw=std::min(available,measureAdventureHudText(feedback,pixels)+24);
            p.centerText(feedback,{(w-fw)*.5f,y-pitch-12,fw,pitch},chalk);
        }
        return finish();
    }
    const bool catalog=c.mode==AdventureHudMode::Catalog||c.pickerOpen;
    if(c.colourPickerOpen||catalog) {
        const bool ring=h>=480||(w>=620&&h>=360);
        if(ring) {
            const bool stacked=w<1120&&!(w>=620&&h<600);
            const float wheelWidth=230,wheelHeight=240;
            const float px=w<480?(w-wheelWidth)*.5f:w-26-wheelWidth;
            const float py=h-(stacked?156.f:50.f)-wheelHeight;
            const float cx=px+115,cy=py+112;
            creativeHotbar(p,c,w<1120&&!stacked?w-280-24:0);
            if(w>=1120&&h>=720)creativeTop(p,c);
            if(c.colourPickerOpen) {
                constexpr float radius=82,diameter=48;
                for(size_t i=0;i<c.colours.size();++i) {
                    const float angle=-1.5707963f+number(i)*6.2831853f/7;
                    creativeSwatch(p,c,i,{cx+std::cos(angle)*radius-diameter*.5f,cy+std::sin(angle)*radius-diameter*.5f,diameter,diameter});
                }
                p.rounded({cx-46,cy-46,92,92},{.055f,.09f,.07f,.72f},46);
                p.stroke({cx-46,cy-46,92,92},gold,46,3);
                p.miniature(c.pieceKind,{cx-42,cy-42,84,84});
                const auto close=std::find_if(c.buildControls.begin(),c.buildControls.end(),[](const auto& row){return row.action==20||row.action==38;});
                if(close!=c.buildControls.end())creativeBareControl(p,*close,{px+186,py-22,44,44});
                const auto& focused=c.colours[std::min(c.selectedRow,c.colours.size()-1)];
                p.centerText(focused.label,{px,py+221,wheelWidth,pitch},chalk,0,true);
                result.selectedVisible=true;
            } else {
                creativeCategories(p,c,{px,py-58,wheelWidth,44});
                if(!c.rows.empty()) {
                    const size_t selected=std::min(c.selectedRow,c.rows.size()-1),count=std::min(c.rows.size(),size_t{5});
                    const size_t first=std::min(selected>count/2?selected-count/2:0,c.rows.size()-count);
                    static constexpr std::array<glm::vec2,4> offsets{{{0,-84},{-72,-42},{72,-42},{72,42}}};
                    size_t petal=0;
                    for(size_t i=first;i<first+count;++i)if(i!=selected) {
                        const auto offset=count==2?offsets[0]:count==3?offsets[petal+1]:offsets[petal];
                        creativePieceDisk(p,c.rows[i],{cx+offset.x,cy+offset.y},76,false,i);++petal;
                    }
                    for(const auto& row:c.buildControls) {
                        if(row.action==25&&row.value>0) {
                            const glm::vec4 more{cx-110,cy+4,76,76};
                            p.rounded(more,{.055f,.09f,.07f,.72f},38);
                            p.stroke(more,{.93f,.94f,.88f,.67f},38,1.5f);
                            p.centerText("...",more,selected+1<c.rows.size()?chalk:fog,24);
                            result.hits.push_back({{cx-94,cy+20,44,44},row.action,row.value,row.intent,SIZE_MAX,row.enabled,row.label});
                        } else if(row.action==25&&row.value<0) {
                            creativeBareControl(p,row,{cx-94,cy+82,44,44});
                        } else if(row.action==20)creativeBareControl(p,row,{cx+50,cy+82,44,44});
                    }
                    // The center sits in front of every petal, including the
                    // overlapping ellipsis control, so its gold ring is whole.
                    creativePieceDisk(p,c.rows[selected],{cx,cy},96,true,selected);
                    result.selectedVisible=true;
                } else {
                    const auto close=std::find_if(c.buildControls.begin(),c.buildControls.end(),[](const auto& row){return row.action==20;});
                    if(close!=c.buildControls.end())creativeBareControl(p,*close,{cx+50,cy+82,44,44});
                }
            }
        } else if(c.colourPickerOpen) {
            const float diameter=44,gap=10,gx=(w-4*diameter-3*gap)*.5f,gy=margin+52;
            const auto close=std::find_if(c.buildControls.begin(),c.buildControls.end(),[](const auto& row){return row.action==20||row.action==38;});
            if(close!=c.buildControls.end())creativeBareControl(p,*close,{w-margin-44,margin,44,44});
            for(size_t i=0;i<c.colours.size();++i)creativeSwatch(p,c,i,{gx+number(i%4)*(diameter+gap),gy+number(i/4)*(diameter+gap),diameter,diameter});
            p.miniature(c.pieceKind,{gx+3*(diameter+gap),gy+diameter+gap,44,44});
            const auto& focused=c.colours[std::min(c.selectedRow,c.colours.size()-1)];
            p.centerText(focused.label,{margin,h-margin-pitch,available,pitch},chalk);
            result.selectedVisible=true;
        } else {
            creativeCategories(p,c,{margin,margin,available,44},false);
            const float cell=64,gap=8;
            const size_t capacity=std::clamp(static_cast<size_t>((available+gap)/(cell+gap)),size_t{1},size_t{6});
            const size_t count=std::min(capacity,c.rows.size()),selected=c.rows.empty()?0:std::min(c.selectedRow,c.rows.size()-1);
            const size_t first=count?std::min(selected>count/2?selected-count/2:0,c.rows.size()-count):0;
            const float all=number(count)*cell+number(count?count-1:0)*gap,bx=(w-all)*.5f,by=margin+56;
            for(size_t i=0;i<count;++i) {
                const size_t index=first+i;const auto& row=c.rows[index];const glm::vec4 box{bx+number(i)*(cell+gap),by,cell,cell};
                p.surface(box,index==selected);p.miniature(row.pieceKind,{box.x+2,box.y+2,60,60});
                result.hits.push_back({box,row.action,row.value,row.intent,index,row.enabled,row.label});
                result.selectedVisible|=index==selected;
            }
            if(count)p.centerText(c.rows[selected].label,{margin,by+cell+6,available,pitch},chalk);
            const size_t controls=std::min(c.buildControls.size(),size_t{3});
            const float allControls=number(controls)*44+number(controls?controls-1:0)*20;
            for(size_t i=0;i<controls;++i)creativeBareControl(p,c.buildControls[i],{(w-allControls)*.5f+number(i)*64,h-margin-44,44,44});
        }
        return finish();
    }
    if(c.mode!=AdventureHudMode::Build) {
        const float pw=std::min(available,660.f),px=(w-pw)*.5f,pad=14,inner=pw-2*pad;
        const float maximum=h-2*margin,rowHeight=std::max(44.f,pitch+12),titleHeight=pitch+12;
        const size_t navigationCount=std::min(c.buildControls.size(),size_t{3});
        const float navigationHeight=navigationCount?52.f:0;
        const float spare=maximum-2*pad-titleHeight-navigationHeight;
        const float statusHeight=!c.menuStatus.empty()&&spare>=rowHeight+pitch+14?pitch+8:0;
        const size_t textLines=c.menuText.empty()?0:std::min(size_t{3},static_cast<size_t>(std::max(0.f,std::floor((spare-statusHeight-rowHeight-14)/pitch))));
        const float textHeight=textLines?number(textLines)*pitch+12:0;
        const size_t capacity=std::max(size_t{1},static_cast<size_t>(std::max(0.f,std::floor((spare-textHeight-statusHeight+6)/(rowHeight+6)))));
        const size_t count=std::min({capacity,c.rows.size(),size_t{6}});
        const size_t selected=c.rows.empty()?0:std::min(c.selectedRow,c.rows.size()-1);
        const size_t first=count?std::min(selected>count/2?selected-count/2:0,c.rows.size()-count):0;
        const float ph=2*pad+titleHeight+textHeight+statusHeight+number(count)*(rowHeight+6)+navigationHeight;
        const float py=(h-ph)*.5f;
        p.panel({px,py,pw,ph});
        p.text(c.title,{px+pad,py+pad,inner,pitch},chalk);
        float y=py+pad+titleHeight;
        if(textLines){p.text(c.menuText,{px+pad,y,inner,number(textLines)*pitch},fog,textLines);y+=textHeight;}
        if(statusHeight){p.text(c.menuStatus,{px+pad,y,inner,pitch},gold);y+=statusHeight;}
        for(size_t i=0;i<count;++i) {
            const size_t index=first+i;auto row=c.rows[index];
            if(!row.detail.empty())row.label+="  /  "+row.detail;
            p.button(row,{px+pad,y,inner,rowHeight},index==selected,index);y+=rowHeight+6;
        }
        if(navigationCount) {
            const float cell=(inner-8*number(navigationCount-1))/number(navigationCount);
            for(size_t i=0;i<navigationCount;++i)creativeMenuButton(p,c.buildControls[i],{px+pad+number(i)*(cell+8),py+ph-pad-44,cell,44});
        }
        return finish();
    }
    creativeTop(p,c);
    creativeHotbar(p,c);
    creativeHints(p,c);
    if(!c.status.empty()&&h>=500&&w>=860) {
        const float statusWidth=std::min(340.f,w*.29f);
        p.text(c.status,{margin+4,h-190,statusWidth,pitch},c.tone==CoveHudTone::Blocked?glm::vec4(1,.74f,.58f,1):chalk);
    } else if(!c.status.empty()&&h>=340) {
        p.centerText(c.status,{margin,70,available,pitch},c.tone==CoveHudTone::Blocked?glm::vec4(1,.74f,.58f,1):chalk);
    }
    return finish();
}
}

AdventureHudLayout layoutAdventureHud(const AdventureHudContent& c,uint32_t width,uint32_t height) {
    const float pixelScale=std::isfinite(c.pixelScale)&&c.pixelScale>0?std::clamp(c.pixelScale,.125f,16.f):1.f;
    if(c.creative&&pixelScale!=1.f) {
        auto logical=c;logical.pixelScale=1;logical.hoverBounds/=pixelScale;
        // Layout in logical pixels so font size and 44px targets stay stable
        // across Retina displays and reduced-resolution gameplay. Keep the
        // public layout in framebuffer coordinates for native input and peers.
        // Canvas rounding can make the two physical/logical ratios differ
        // slightly. Rounding up retains a 320px logical viewport instead of
        // truncating it to 319 and hiding its controls. Layout margins absorb
        // the resulting fraction of a logical pixel at the outer edge.
        const auto dimension=[pixelScale](uint32_t extent) {
            return static_cast<uint32_t>(std::clamp(std::ceil(double(extent)/double(pixelScale)),
                0.,double(std::numeric_limits<uint32_t>::max())));
        };
        auto scaled=layoutAdventureHud(logical,dimension(width),dimension(height));
        scaled.canvas.bodyPixels*=pixelScale;scaled.canvas.panel*=pixelScale;
        for(size_t i=0;i<scaled.canvas.count;++i) {
            auto& quad=scaled.canvas.quads[i];quad.bounds*=pixelScale;
            if(quad.uv.x<0) {
                if(quad.uv.x<=-3)quad.uv.x=-3-(-quad.uv.x-3)*pixelScale;
                if(quad.uv.x!=-2)quad.uv.y*=pixelScale;
                quad.uv.z*=pixelScale;quad.uv.w*=pixelScale;
            }
        }
        for(auto& hit:scaled.canvas.menuHits)hit.bounds*=pixelScale;
        for(auto& hit:scaled.hits)hit.bounds*=pixelScale;
        for(auto& panel:scaled.panels)panel*=pixelScale;
        for(auto& triangle:scaled.triangles)for(auto& coordinate:triangle.xy)coordinate*=pixelScale;
        scaled.guideBodyBounds*=pixelScale;
        return scaled;
    }
    AdventureHudLayout result;
    if(width<320||height<240)return result;
    if(c.creative&&c.mode!=AdventureHudMode::Guide)
        return layoutCreativeHud(c,width,height);
    const float w=static_cast<float>(width),h=static_cast<float>(height);
    const float s=std::isfinite(c.textScale)?std::clamp(c.textScale,1.f,1.5f):1.f;
    const float pixels=20*s,pitch=pixels*1.3f,margin=12,pad=14,buttonHeight=std::max(44.f,pitch+16);
    result.canvas.bodyPixels=pixels;
    Painter p{result,w,h,pixels,c.highContrast,c.creative};
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
            const size_t capacity=std::clamp(static_cast<size_t>((pw-2*pad+8)/(138*s+8)),size_t{1},size_t{AdventureHudLayout::maximumMeshThumbnails});
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
        const auto title=guideLines(c.title,inner,pixels,c.creative),body=guideLines(c.menuText,inner,pixels,c.creative);
        const auto measure=c.creative?measureAdventureHudText:measureCoveHudText;
        const auto append=c.creative?appendAdventureHudText:appendCoveHudText;
        const size_t titleCount=std::min(title.lines.size(),size_t{2});
        const float titleHeight=number(titleCount)*pitch+8;
        const size_t rowCount=std::min(c.rows.size(),size_t{8}); // Six topics plus return; individual cards use at most four.
        const size_t selected=rowCount?std::min(c.selectedRow,rowCount-1):0;
        std::vector<ActionLine> navigation;
        for(size_t i=0;i<rowCount;++i) {
            const float wanted=std::min(inner,std::max(96.f,measure(c.rows[i].label,pixels)+24));
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
            const auto rendered=append(result.canvas,body.lines[i],{px+pad,y,inner,pitch},pixels,c.creative?fog:muted,1);
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
                const float cell=std::min(inner,std::max(96.f,measure(row.label,pixels)+24))+extra;
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
    navigationBackend_.setLayoutFactory([this](uint32_t w,uint32_t h){return layoutAdventureNavigation(navigation_,content_,w,h);});
}

void AdventureHudPath::setNavigation(AdventureHudNavigation navigation) {
    const auto heading=[](float degrees){return std::isfinite(degrees)?std::round(std::remainder(degrees,360.f)*2.f)*.5f:0.f;};
    navigation.cameraBearingDegrees=heading(navigation.cameraBearingDegrees);
    navigation.playerBearingDegrees=heading(navigation.playerBearingDegrees);
    if(!std::isfinite(navigation.playerUv.x)||!std::isfinite(navigation.playerUv.y))navigation.playerUv={.5f,.5f};
    navigation.playerUv=glm::round(glm::clamp(navigation.playerUv,glm::vec2(0),glm::vec2(1))*1024.f)/1024.f;
    if(navigation_==navigation)return;
    navigation_=std::move(navigation);
    navigationBackend_.setLayoutFactory([this](uint32_t w,uint32_t h){return layoutAdventureNavigation(navigation_,content_,w,h);});
}
bool AdventureHudPath::navigationContains(glm::vec2 point) const noexcept {
    for(const auto& hit:navigationBackend_.layout().menuHits) {
        const auto& b=hit.bounds;
        if(point.x>=b.x&&point.y>=b.y&&point.x<b.x+b.z&&point.y<b.y+b.w)return true;
    }
    return false;
}

bool AdventureHudPath::init(WGPUDevice device,WGPUQueue queue,WGPUTextureFormat format,const std::filesystem::path& fontShader) {
    shutdown();
    if(!backend_.init(device,queue,format,fontShader))return false;
    const auto art=decodeAdventureHudArt();
    if(art.empty()||!backend_.setSpriteAtlas(1024,512,art)
        ||!navigationBackend_.init(device,queue,format,fontShader)
        ||!navigationBackend_.useSpriteAtlas(backend_.spriteAtlasView())) {shutdown();return false;}
    navigationBackend_.setLayoutFactory([this](uint32_t w,uint32_t h){return layoutAdventureNavigation(navigation_,content_,w,h);});
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
    pipeline_=::voxy::gpu::createRenderPipeline(device,&descriptor);
    if(!pipeline_){shutdown();return false;}
    return true;
}
void AdventureHudPath::shutdown() noexcept {
    // Submitted work retains these references until its actual completion.
    // Release handles; do not destroy a buffer still used by an encoded frame.
    navigationBackend_.shutdown();backend_.shutdown();
    if(pipeline_)wgpuRenderPipelineRelease(pipeline_);
    if(pipelineLayout_)wgpuPipelineLayoutRelease(pipelineLayout_);
    if(shader_)wgpuShaderModuleRelease(shader_);
    if(triangles_)wgpuBufferRelease(triangles_);
    pipeline_=nullptr;pipelineLayout_=nullptr;shader_=nullptr;triangles_=nullptr;queue_=nullptr;
    lastEncodedTriangles_=0;triangleUploadCount_=0;trianglesDirty_=true;
    layout_={};content_={};contentSet_=false;updated_={};
    navigation_={};uploadedMap_.reset();mapUploadCount_=0;
}
bool AdventureHudPath::render(WGPUCommandEncoder encoder,WGPUTextureView target,uint32_t width,uint32_t height) {
    clearEncodedObservation();
    if(!initialized()||!encoder||!target||!width||!height)return false;
    if(!backend_.render(encoder,target,width,height)||layout_.trianglesTruncated
        ||layout_.triangles.size()>maximumTriangleCount)return false;
    if(content_.creative&&navigation_.visible&&navigation_.map&&uploadedMap_!=navigation_.map) {
        const auto& map=*navigation_.map;
        if(map.width!=256||map.height!=256||!backend_.updateSpriteRegion(256,256,256,256,map.rgba))return false;
        uploadedMap_=navigation_.map;++mapUploadCount_;
    }
    if(!navigationBackend_.render(encoder,target,width,height))return false;
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
