#include "game/adventure/adventure_navigation.hpp"
#include <array>
#include <limits>
#include <numbers>

namespace voxy::game::adventure {
namespace {
// sRGB forms of the actual LEGO terrain palette's middle shades. The same
// colorFamily classifier supplies sand, grass, upland and exposed rock.
constexpr std::array<std::array<uint8_t,3>,4> terrainColours{{
    {223,193,133},{134,173,101},{88,133,109},{164,164,151}}};
constexpr std::array<uint8_t,3> deepWater{58,119,143},shallowWater{94,156,168};
bool finite(glm::dvec2 v) noexcept {return std::isfinite(v.x)&&std::isfinite(v.y);}
}
float adventureNavigationBearing(glm::dvec2 forward) noexcept {
    if(!finite(forward)||(forward.x==0&&forward.y==0))return 0;
    double degrees=std::atan2(forward.x,-forward.y)*180/std::numbers::pi;
    if(degrees<0)degrees+=360;
    const float bearing=static_cast<float>(degrees);
    return bearing<360?bearing:0;
}
glm::dvec2 AdventureNavigationCache::quantizedCenter(glm::dvec2 p) noexcept {
    return finite(p)?glm::floor(p/centerStep+glm::dvec2(.5))*centerStep:glm::dvec2(0);
}
bool AdventureNavigationCache::sameTerrain(const terrain::lego::Surface& s,float water) const noexcept {
    return !terrainRgba_.empty()&&source_.samples.data()==s.samples.data()&&source_.samples.size()==s.samples.size()
        &&source_.width==s.width&&source_.height==s.height&&source_.cellScale==s.cellScale
        &&source_.heightScale==s.heightScale&&waterHeight_==water;
}
bool AdventureNavigationCache::needsUpdate(const terrain::lego::Surface& s,float water,
    glm::dvec2 player,uint64_t epoch) const noexcept {
    return !sameTerrain(s,water)||center_!=quantizedCenter(player)||geometryEpoch_!=epoch;
}
glm::vec2 AdventureNavigationCache::playerUv(glm::dvec2 p) const noexcept {
    return finite(p)?glm::vec2((p-center_)/span+glm::dvec2(.5)):glm::vec2(.5f);
}
std::vector<uint8_t> AdventureNavigationCache::rasterize(const terrain::lego::Surface& s,float water,
    glm::dvec2 player,uint64_t epoch,std::span<const AdventureNavigationFootprint> footprints) {
    const auto nextCenter=quantizedCenter(player);
    if(!sameTerrain(s,water)||center_!=nextCenter) {
        center_=nextCenter;source_=s;waterHeight_=water;++terrainRasterizations_;
        terrainRgba_.resize(size_t{size}*size*4);
        const bool valid=s.valid()&&s.width<=uint32_t(std::numeric_limits<int>::max())
            &&s.height<=uint32_t(std::numeric_limits<int>::max())&&std::isfinite(water);
        const glm::dvec2 origin=valid?glm::dvec2(s.origin()):glm::dvec2(0);
        const int radius=valid?static_cast<int>(std::clamp(std::ceil(span/double(size)/double(s.cellScale)),
            1.,double(std::max(3u,std::max(s.width,s.height))-2))):1;
        for(uint32_t z=0;z<size;++z)for(uint32_t x=0;x<size;++x) {
            const glm::dvec2 world=center_+(glm::dvec2(double(x)+.5,double(z)+.5)/double(size)-glm::dvec2(.5))*span;
            const glm::dvec2 cell=valid?(world+origin)/double(s.cellScale):glm::dvec2(-1);
            auto rgb=deepWater;
            if(valid&&cell.x>=0&&cell.y>=0&&cell.x<double(s.width-1)&&cell.y<double(s.height-1)) {
                const int ix=static_cast<int>(cell.x),iz=static_cast<int>(cell.y);
                const float top=s.cellTop(ix,iz);
                if(top<=water) {
                    const float nearShore=std::clamp(1-(water-top)/(24*s.cellScale),0.f,1.f);
                    for(size_t channel=0;channel<3;++channel)
                        rgb[channel]=static_cast<uint8_t>(std::lround(float(deepWater[channel])
                            +(float(shallowWater[channel])-float(deepWater[channel]))*nearShore));
                } else {
                    rgb=terrainColours[terrain::lego::colorFamily(s,uint32_t(ix),uint32_t(iz),water)];
                    // A restrained northwest light reveals real slopes without
                    // decorative noise or creating imaginary roads/landmarks.
                    const int left=std::max(0,ix-radius),right=ix+std::min(radius,int(s.width)-2-ix);
                    const int north=std::max(0,iz-radius),south=iz+std::min(radius,int(s.height)-2-iz);
                    const float slope=(s.cellTop(left,iz)-s.cellTop(right,iz)+s.cellTop(ix,north)-s.cellTop(ix,south))
                        /std::max(1.f,float(radius)*s.cellScale);
                    const float shade=std::clamp(1.f+slope*.065f,.82f,1.12f);
                    for(auto& channel:rgb)channel=static_cast<uint8_t>(std::clamp(std::lround(float(channel)*shade),0l,255l));
                }
            }
            const size_t offset=(size_t(z)*size+x)*4;
            for(size_t channel=0;channel<3;++channel)terrainRgba_[offset+channel]=rgb[channel];
            terrainRgba_[offset+3]=255;
        }
    }
    auto result=terrainRgba_;
    for(const auto& footprint:footprints) {
        if(!finite(footprint.minimum)||!finite(footprint.maximum)
            ||footprint.minimum.x>=footprint.maximum.x||footprint.minimum.y>=footprint.maximum.y)continue;
        const auto low=((footprint.minimum-center_)/span+glm::dvec2(.5))*double(size);
        const auto high=((footprint.maximum-center_)/span+glm::dvec2(.5))*double(size);
        if(high.x<=0||high.y<=0||low.x>=double(size)||low.y>=double(size))continue;
        const int x0=static_cast<int>(std::clamp(std::floor(low.x),0.,double(size)));
        const int z0=static_cast<int>(std::clamp(std::floor(low.y),0.,double(size)));
        const int x1=static_cast<int>(std::clamp(std::ceil(high.x),0.,double(size)));
        const int z1=static_cast<int>(std::clamp(std::ceil(high.y),0.,double(size)));
        for(int z=z0;z<z1;++z)for(int x=x0;x<x1;++x) {
            const size_t offset=(size_t(z)*size+size_t(x))*4;
            result[offset]=uint8_t(footprint.rgb>>16);result[offset+1]=uint8_t(footprint.rgb>>8);result[offset+2]=uint8_t(footprint.rgb);
        }
    }
    geometryEpoch_=epoch;
    return result;
}
}
