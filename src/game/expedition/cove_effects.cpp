#include "game/expedition/cove_effects.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>

namespace voxy::game::expedition {
namespace {
using Effects=CoveEffects;
void increment(uint64_t& value,uint64_t amount=1) noexcept {
    value+=std::min(amount,std::numeric_limits<uint64_t>::max()-value);
}
bool finite(glm::dvec3 v,double bound=1e6) noexcept {
    return std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z)
        &&std::abs(v.x)<=bound&&std::abs(v.y)<=bound&&std::abs(v.z)<=bound;
}
bool valid(const Effects::SourceId& id) noexcept {
    return construction::isValid(id.owner)&&id.kind<=Effects::SourceKind::Player
        &&(id.kind==Effects::SourceKind::Player?!id.body.valid():id.body.valid()&&id.body.generation!=0);
}
bool valid(const Effects::MotionSource& source) noexcept {
    return valid(source.id)&&finite(source.point)&&finite(source.pointVelocity,150)
        &&std::isfinite(source.waterHeight)&&std::abs(source.waterHeight)<=1e6
        &&std::isfinite(source.footprintRadius)&&source.footprintRadius>0&&source.footprintRadius<=16
        &&std::isfinite(source.effectiveDrive)&&std::abs(source.effectiveDrive)<=1
        &&(!source.hasPropeller||(source.id.kind==Effects::SourceKind::Boat
            &&finite(source.propellerPoint)&&finite(source.propellerVelocity,150)
            &&std::isfinite(source.propellerWaterHeight)&&std::abs(source.propellerWaterHeight)<=1e6));
}
bool sameImpact(const Effects::Impact& a,const Effects::Impact& b) noexcept {
    return a.source==b.source&&a.otherBody==b.otherBody&&a.feature==b.feature&&a.otherFeature==b.otherFeature;
}
}

bool CoveEffects::reset(uint64_t epoch,uint64_t tick) noexcept {
    if(!epoch)return false;
    *this=CoveEffects{};stats_.epoch=epoch;stats_.tick=tick;
    return true;
}

float CoveEffects::random() noexcept {
    random_^=random_<<13u;random_^=random_>>17u;random_^=random_<<5u;
    return float(random_>>8u)*(1.f/16777216.f);
}

void CoveEffects::emit(Kind kind,glm::dvec3 point,glm::dvec3 velocity,double waterHeight,
    float width,float height,float lifetime,float angle,glm::vec4 color) noexcept {
    if(count_==maximumInstances){increment(stats_.dropped);return;}
    instances_[count_]={glm::vec4(glm::vec3(point),float(kind)),
        glm::vec4(glm::vec3(velocity),0),{width,height,lifetime,angle},color};
    waterAtBirth_[count_]=float(waterHeight);++count_;
    increment(stats_.emitted);increment(stats_.emittedByKind[size_t(kind)]);
    stats_.active=static_cast<uint32_t>(count_);stats_.highWater=std::max(stats_.highWater,stats_.active);
}

void CoveEffects::burst(Kind kind,glm::dvec3 point,glm::dvec3 velocity,glm::dvec3 normal,
    double waterHeight,uint32_t count,double strength) noexcept {
    const bool dust=kind==Kind::Dust,drain=kind==Kind::Drain;
    for(uint32_t i=0;i<count;++i) {
        const double angle=double(random())*2*std::numbers::pi;
        const double radial=(.2+.6*double(random()))*strength;
        auto speed=velocity*.12+glm::dvec3(std::cos(angle)*radial,
            drain?-.4-double(random()):(.7+double(random()))*strength,std::sin(angle)*radial);
        if(dust)speed=velocity*.08+normal*(.5+double(random()))*strength
            +glm::dvec3(std::cos(angle),.4,std::sin(angle))*.4;
        const float size=dust?.10f+random()*.13f:.045f+random()*.065f;
        emit(kind,point,speed,waterHeight,size,dust?size:size*1.8f,
            dust?.55f+random()*.45f:drain?.45f+random()*.3f:.5f+random()*.65f,
            float(angle),dust?glm::vec4(.42f,.34f,.23f,.45f):glm::vec4(.74f,.88f,.89f,.70f));
    }
}

void CoveEffects::age(double seconds) noexcept {
    const float dt=float(seconds);
    size_t output=0;
    for(size_t i=0;i<count_;++i) {
        auto value=instances_[i];value.velocityAge.w+=dt;
        if(value.velocityAge.w>=value.sizeLife.z)continue;
        const auto kind=static_cast<Kind>(uint32_t(value.positionKind.w));
        auto velocity=glm::vec3(value.velocityAge);auto point=glm::vec3(value.positionKind);
        if(kind==Kind::Splash||kind==Kind::Drain) {
            point+=velocity*dt+glm::vec3(0,-4.9f*dt*dt,0);velocity.y-=9.8f*dt;
            if(point.y<waterAtBirth_[i]-.08f&&velocity.y<0)continue;
        } else if(kind==Kind::Dust) {
            const float damping=std::exp(-2.f*dt);
            point+=velocity*((1-damping)*.5f);velocity*=damping;
        } else point+=velocity*dt;
        value.positionKind=glm::vec4(point,value.positionKind.w);
        value.velocityAge=glm::vec4(velocity,value.velocityAge.w);
        instances_[output]=value;waterAtBirth_[output]=waterAtBirth_[i];++output;
    }
    count_=output;stats_.active=static_cast<uint32_t>(count_);
}

bool CoveEffects::observe(const Inputs& input) noexcept {
    const auto reject=[&]{increment(stats_.rejected);return false;};
    if(!stats_.epoch||input.epoch!=stats_.epoch||input.tick<stats_.tick
        ||input.sources.size()>maximumSources||input.impacts.size()>maximumImpacts)return reject();
    for(size_t i=0;i<input.sources.size();++i) {
        if(!valid(input.sources[i]))return reject();
        if((worldBound_&&input.sources[i].id.owner.world!=world_)
            ||(i&&input.sources[i].id.owner.world!=input.sources.front().id.owner.world))return reject();
        for(size_t j=0;j<i;++j)if(input.sources[i].id==input.sources[j].id)return reject();
    }
    for(const auto& impact:input.impacts) {
        if(!valid(impact.source)||impact.tick!=input.tick||!finite(impact.point)
            ||!finite(impact.normal,1.001)||std::abs(glm::length(impact.normal)-1)>.001
            ||!std::isfinite(impact.speed)||impact.speed<0||impact.speed>300
            ||!std::isfinite(impact.impulse)||impact.impulse<0||impact.impulse>1e12
            ||!std::isfinite(impact.waterHeight)||std::abs(impact.waterHeight)>1e6
            ||std::none_of(input.sources.begin(),input.sources.end(),[&](const auto& s){return s.id==impact.source;}))return reject();
    }
    if(input.tick==stats_.tick)return true; // Readbacks/display frames cannot replay emission.
    if(!worldBound_&&!input.sources.empty()){world_=input.sources.front().id.owner.world;worldBound_=true;}
    const auto ticks=input.tick-stats_.tick;
    const bool gap=ticks>maximumTickGap;
    if(gap){count_=0;stats_.active=0;baselineCount_=0;increment(stats_.discontinuities);}
    const double seconds=double(std::min(ticks,maximumTickGap))/60.;
    if(input.running&&!gap)age(seconds);
    std::array<Baseline,maximumSources> next{};
    for(size_t i=0;i<input.sources.size();++i) {
        const auto& source=input.sources[i];auto& target=next[i];target.source=source;
        const auto baselineEnd=baselines_.begin()+static_cast<std::ptrdiff_t>(baselineCount_);
        const auto previous=std::find_if(baselines_.begin(),baselineEnd,
            [&](const auto& p){return p.source.id==source.id;});
        if(!input.running||gap||previous==baselineEnd)continue;
        const auto distance=glm::length(glm::dvec2(source.point.x-previous->source.point.x,source.point.z-previous->source.point.z));
        if(glm::length(source.point-previous->source.point)>std::max(3.,glm::length(source.pointVelocity)*seconds*2+.5)) {
            increment(stats_.discontinuities);continue; // Unannounced relocation does not draw a wake across the map.
        }
        const auto speed=glm::length(glm::dvec2(source.pointVelocity.x,source.pointVelocity.z));
        if(source.waterContact&&source.id.kind==SourceKind::Boat&&speed>.35) {
            const double requested=previous->wakeCredit+distance/.35;
            if(requested>=9)increment(stats_.dropped,static_cast<uint64_t>(requested)-8);
            target.wakeCredit=std::min(8.,requested);
            const uint32_t count=static_cast<uint32_t>(target.wakeCredit);target.wakeCredit-=count;
            for(uint32_t n=0;n<count;++n) {
                const double fraction=double(n+1)/double(count+1);
                auto point=glm::mix(previous->source.point,source.point,fraction);point.y=source.waterHeight+.025;
                const float width=float(std::clamp(source.footprintRadius*1.5,.4,2.4));
                emit(Kind::Wake,point,glm::dvec3(source.pointVelocity.x,0,source.pointVelocity.z)*.05,
                    source.waterHeight,width,.5f+float(std::min(speed,6.))*.1f,2.6f,
                    float(std::atan2(source.pointVelocity.x,source.pointVelocity.z)),{.74f,.84f,.81f,.46f});
            }
        }
        if(source.hasPropeller&&std::abs(source.effectiveDrive)>.01) {
            const double wet=std::clamp((source.propellerWaterHeight-source.propellerPoint.y+.15)/.3,0.,1.);
            target.foamCredit=std::min(8.,previous->foamCredit+seconds*24*std::abs(source.effectiveDrive)*wet);
            const uint32_t count=static_cast<uint32_t>(target.foamCredit);target.foamCredit-=count;
            for(uint32_t n=0;n<count;++n) {
                auto point=source.propellerPoint;point.y=source.propellerWaterHeight+.035;
                point.x+=(double(random())-.5)*.25;point.z+=(double(random())-.5)*.25;
                emit(Kind::Foam,point,glm::dvec3(source.propellerVelocity.x,0,source.propellerVelocity.z)*.07,
                    source.propellerWaterHeight,.32f,.6f,1.8f,random()*float(2*std::numbers::pi),{.86f,.92f,.89f,.64f});
            }
        }
        // A kinematic player can clamp its endpoint velocity on entering
        // swimming. Its accepted displacement still proves downward travel.
        const double entrySpeed=std::min(source.pointVelocity.y,
            (source.point.y-previous->source.point.y)/seconds);
        if(source.waterContact&&!previous->source.waterContact&&entrySpeed<-.35) {
            auto point=source.point;point.y=source.waterHeight+.06;
            auto velocity=source.pointVelocity;velocity.y=std::max(entrySpeed,-150.);
            const auto before=count_;
            burst(Kind::Splash,point,velocity,{0,1,0},source.waterHeight,12,
                std::clamp(-entrySpeed*.35,.5,2.));
            if(source.id.kind==SourceKind::Player)increment(stats_.playerEntrySplashes,count_-before);
        } else if(!source.waterContact&&previous->source.waterContact&&source.pointVelocity.y>.15) {
            // Water shed by a physically rising surface; this is not a pump,
            // compartment volume or a claim of authoritative drainage.
            burst(Kind::Drain,source.point,source.pointVelocity,{0,-1,0},source.waterHeight,8,.5);
        }
    }
    if(input.running&&!gap)for(size_t i=0;i<input.impacts.size();++i) {
        const auto& impact=input.impacts[i];
        bool duplicate=false;
        for(size_t j=0;j<i;++j)if(sameImpact(impact,input.impacts[j]))duplicate=true;
        if(duplicate||impact.speed<1||impact.impulse<.1)continue;
        const bool wet=impact.point.y<=impact.waterHeight+.15;
        // Deep impacts do not inexplicably spray through metres of water.
        if(impact.point.y<impact.waterHeight-.4)continue;
        auto point=impact.point;if(wet)point.y=impact.waterHeight+.05;
        burst(wet?Kind::Splash:Kind::Dust,point,{},impact.normal,impact.waterHeight,
            std::min(16u,4u+uint32_t(std::min(impact.speed*2.,12.))),std::clamp(impact.speed*.3,.5,2.));
    }
    baselines_=next;baselineCount_=input.sources.size();stats_.tick=input.tick;
    return true;
}

} // namespace voxy::game::expedition
