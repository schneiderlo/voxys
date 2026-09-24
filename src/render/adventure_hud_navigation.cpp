#include "render/adventure_hud.hpp"
#include <algorithm>
#include <cmath>
#include <numbers>
#include <glm/common.hpp>

namespace voxy::render {
namespace {
constexpr glm::vec4 ivory{.97f,.98f,.96f,1},gold{1,.84f,.20f,1};
struct NavigationPainter {
    CoveHudLayout& out;
    float width,height;
    void quad(glm::vec4 bounds,glm::vec4 uv,glm::vec4 color) {
        if(bounds.z<=0||bounds.w<=0||bounds.x<0||bounds.y<0||bounds.x+bounds.z>width+.01f||bounds.y+bounds.w>height+.01f)return;
        if(out.count==out.quads.size()){out.truncated=true;return;}
        out.quads[out.count++]={bounds,uv,color};
    }
    void round(glm::vec4 bounds,glm::vec4 color,float radius) {quad(bounds,{-1,radius,bounds.z,bounds.w},color);}
    void stroke(glm::vec4 bounds,glm::vec4 color,float radius,float thickness) {quad(bounds,{-3-thickness,radius,bounds.z,bounds.w},color);}
    void text(std::string_view value,glm::vec4 bounds,float size,glm::vec4 color=ivory) {
        // A narrow shadow supports text directly over the world, without
        // putting every label inside an opaque card.
        (void)appendAdventureHudText(out,value,{bounds.x+1,bounds.y+1,bounds.z,bounds.w},size,{.02f,.04f,.045f,.65f});
        (void)appendAdventureHudText(out,value,bounds,size,color);
    }
    void arrow(glm::vec4 bounds,float radians,glm::vec4 color) {quad(bounds,{-2,radians,bounds.z,bounds.w},color);}
};
}

CoveHudLayout layoutAdventureNavigation(const AdventureHudNavigation& navigation,const AdventureHudContent& content,uint32_t width,uint32_t height) {
    CoveHudLayout out;
    if(!content.creative||!navigation.visible)return out;
    if(content.mode!=AdventureHudMode::Explore&&content.mode!=AdventureHudMode::Build&&content.mode!=AdventureHudMode::Catalog)return out;
    const float density=std::isfinite(content.pixelScale)&&content.pixelScale>0?std::clamp(content.pixelScale,.125f,16.f):1;
    const float w=float(width)/density,h=float(height)/density;
    if(w<760||h<540)return out;
    const float preference=std::isfinite(content.textScale)?std::clamp(content.textScale,1.f,1.5f):1;
    NavigationPainter p{out,w,h};
    const float margin=w>=1200?32.f:22.f,portrait=88;
    const glm::vec4 photo{margin,28,portrait,portrait};
    out.menuHits.push_back({{margin-4,24,portrait+292,110*preference},-1,-1});
    p.round({photo.x-3,photo.y-3,portrait+6,portrait+6},{.03f,.08f,.11f,.45f},portrait);
    p.round(photo,{.12f,.23f,.28f,.60f},portrait);
    p.stroke(photo,{.97f,.98f,.96f,.95f},portrait,2.5f);
    p.quad({photo.x+4,photo.y+4,portrait-8,portrait-8},{6.f,288.f/512,192.f/1024,192.f/512},ivory);
    const float tx=margin+portrait+18,title=25*preference,sub=17*preference;
    p.text("Builder",{tx,26,270,title*1.3f},title);
    p.text("Free build",{tx,30+title*1.3f,260,sub*1.3f},sub,{.84f,.92f,.93f,1});
    const std::string count=std::to_string(navigation.placedPieces)+(navigation.placedPieces==1?" piece placed":" pieces placed");
    p.round({tx,37+title*1.3f+sub*1.3f,5,5},gold,2.5f);
    p.text(count,{tx+14,28+title*1.3f+sub*1.3f,250,sub*1.3f},sub,gold);

    // North-up terrain comes from the world's real heightfield. Only the
    // heading marker moves each frame; the image changes at tile boundaries.
    const float diameter=w>=1100?192.f:160.f,mx=w-margin-diameter,my=34,cx=mx+diameter*.5f,cy=my+diameter*.5f;
    if(navigation.map) {
        out.menuHits.push_back({{mx-4,my-16,diameter+8,diameter+20},-1,-1});
        p.round({mx-4,my-3,diameter+8,diameter+8},{.025f,.06f,.08f,.35f},diameter);
        p.round({mx-2,my-2,diameter+4,diameter+4},{.91f,.95f,.96f,.86f},diameter);
        // Pan a smaller window within the cached tile. This keeps the world
        // moving smoothly under the centered marker across tile changes.
        constexpr float window=.75f;
        const glm::vec2 mapCenter=glm::clamp(navigation.playerUv,glm::vec2(window*.5f),glm::vec2(1-window*.5f));
        const glm::vec2 mapStart=mapCenter-glm::vec2(window*.5f);
        p.quad({mx,my,diameter,diameter},{6.f+(256+mapStart.x*256)/1024,(256+mapStart.y*256)/512,window*256/1024,window*256/512},ivory);
        for(int axis=0;axis<4;++axis) {
            const float angle=float(axis)*std::numbers::pi_v<float>*.5f;
            const float x=cx+std::cos(angle)*(diameter*.5f),y=cy+std::sin(angle)*(diameter*.5f);
            p.round({x-1,y-4,2,8},{.96f,.98f,.97f,.75f},1);
        }
        const glm::vec2 uv=glm::clamp((navigation.playerUv-mapStart)/window,glm::vec2(.08f),glm::vec2(.92f));
        const float px=mx+uv.x*diameter,py=my+uv.y*diameter;
        const float radians=navigation.cameraBearingDegrees*std::numbers::pi_v<float>/180;
        p.arrow({px-15,py-15,30,30},radians,{.03f,.09f,.13f,.95f});
        p.arrow({px-11,py-12,22,24},radians,ivory);
        p.round({cx-16,my-15,32,32},{.08f,.17f,.22f,.90f},16);
        p.text("N",{cx-7,my-14,22,29},21);
    }

    if(w>=1180) {
        const float span=std::min(470.f,w-740),middle=w*.5f,lineY=62+(preference-1)*32;
        p.round({middle-span*.5f,lineY,span,1},{.91f,.96f,.97f,.36f},.5f);
        const float bearing=std::isfinite(navigation.cameraBearingDegrees)?navigation.cameraBearingDegrees:0;
        const int base=static_cast<int>(std::floor(bearing/15))*15;
        for(int step=-6;step<=6;++step) {
            const int heading=base+step*15;
            const float delta=std::remainder(float(heading)-bearing,360.f);
            if(std::abs(delta)>90)continue;
            const float x=middle+delta*(span/180),fade=1-std::abs(delta)/120;
            const bool cardinal=heading%90==0;
            p.round({x-.6f,lineY-(cardinal?13.f:7.f),1.2f,cardinal?13.f:7.f},{.94f,.98f,.97f,fade},.6f);
            if(cardinal) {
                const int index=((heading/90)%4+4)%4;
                constexpr std::array<std::string_view,4> labels{"N","E","S","W"};
                const float labelWidth=measureAdventureHudText(labels[size_t(index)],20*preference);
                p.text(labels[size_t(index)],{x-labelWidth*.5f,27,labelWidth+2,26*preference},20*preference,{.94f,.98f,.97f,fade});
            }
        }
        p.arrow({middle-7,13,14,14},0,gold);
    }
    // Dynamic navigation has a separate fixed quad buffer. It never causes
    // the hotbar artwork to be rebuilt or uploaded while walking/orbiting.
    for(size_t i=0;i<out.count;++i) {
        auto& q=out.quads[i];q.bounds*=density;
        if(q.uv.x<0) {
            if(q.uv.x<=-3)q.uv.x=-3-(-q.uv.x-3)*density;
            if(q.uv.x!=-2)q.uv.y*=density;
            q.uv.z*=density;q.uv.w*=density;
        }
    }
    for(auto& hit:out.menuHits)hit.bounds*=density;
    out.bodyPixels=18*preference*density;
    return out;
}
}
