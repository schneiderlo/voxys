#include "render/adventure_hud.hpp"
#include "render/generated/adventure_hud_art.hpp"
#include <stb_image.h>
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace voxy::render {
namespace {
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
float advance(char c,float scale) {return float(adventure_hud_art::glyphs[size_t(c-32)].advance64)*(scale/64.f);}
}
std::vector<uint8_t> decodeAdventureHudArt() {
    int width=0,height=0,channels=0;
    auto* bytes=stbi_load_from_memory(adventure_hud_art::png.data(),static_cast<int>(adventure_hud_art::png.size()),&width,&height,&channels,4);
    if(!bytes)return {};
    std::vector<uint8_t> result;
    if(width==1024&&height==512)result.assign(bytes,bytes+size_t{1024}*512*4);
    stbi_image_free(bytes);return result;
}
float measureAdventureHudText(std::string_view source,float pixels) {
    if(!std::isfinite(pixels)||pixels<=0)return 0;
    float result=0;
    for(const auto c:printable(source))result+=advance(c,pixels/32.f);
    return result;
}

CoveHudTextResult appendAdventureHudText(CoveHudLayout& layout,std::string_view source,
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
            while(!current.empty()&&measureAdventureHudText(current+"...",pixels)>bounds.z)current.pop_back();
            if(measureAdventureHudText("...",pixels)<=bounds.z)current+="...";
        }
        float x=bounds.x;const float y=bounds.y+static_cast<float>(line)*pitch;
        for(const auto c:current) {
            const auto& g=adventure_hud_art::glyphs[static_cast<size_t>(c-32)];
            if(g.width&&g.height) {
                const glm::vec4 original{x+static_cast<float>(g.left)*scale,y+static_cast<float>(g.top)*scale,
                    static_cast<float>(g.width)*scale,static_cast<float>(g.height)*scale};
                const float left=std::max(original.x,bounds.x),top=std::max(original.y,bounds.y);
                const float right=std::min(original.x+original.z,bounds.x+bounds.z),bottom=std::min(original.y+original.w,bounds.y+bounds.w);
                if(right>left&&bottom>top) {
                    if(layout.count==layout.quads.size()){layout.truncated=true;return result;}
                    layout.quads[layout.count++]={{left,top,right-left,bottom-top},
                        {2.f+(static_cast<float>(g.x)+(left-original.x)/scale)/1024.f,
                         (static_cast<float>(g.y)+(top-original.y)/scale)/512.f,
                         (right-left)/(scale*1024.f),(bottom-top)/(scale*512.f)},color};
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

}
