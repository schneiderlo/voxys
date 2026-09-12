#pragma once
#include "game/assets/rigid_animation.hpp"
#include "game/expedition/cove_player.hpp"
#include <algorithm>
#include <cmath>

namespace voxy::game::expedition {

// Cosmetic clip state consumes elapsed accepted character ticks. It never
// advances the controller, creates root motion, or survives as a GPU handle.
class CoveCharacter {
public:
    enum class Clip : uint8_t { Idle,Walk,Jump,Fall,Swim,Helm,Tool,Land };
    void reset() noexcept {*this=CoveCharacter{};}
    [[nodiscard]] bool restoreMode(CovePlayer::Mode mode,double verticalSpeed) noexcept {
        if(mode<CovePlayer::Mode::Walking||mode>CovePlayer::Mode::Helm||!std::isfinite(verticalSpeed)
            ||std::abs(verticalSpeed)>150)return false;
        reset();previousMode_=mode;
        if(mode==CovePlayer::Mode::Airborne)clip_=verticalSpeed>0?Clip::Jump:Clip::Fall;
        else if(mode==CovePlayer::Mode::Swimming)clip_=Clip::Swim;
        else if(mode==CovePlayer::Mode::Helm)clip_=Clip::Helm;
        previous_=clip_;return true;
    }
    void update(double seconds,CovePlayer::Mode mode,double relativeSpeed,
        double verticalSpeed,bool toolActive) noexcept {
        if(!std::isfinite(seconds)||seconds<=0||!std::isfinite(relativeSpeed)||relativeSpeed<0
            ||!std::isfinite(verticalSpeed)||mode<CovePlayer::Mode::Walking||mode>CovePlayer::Mode::Helm)return;
        const double dt=std::min(seconds,.25);
        Clip next=Clip::Idle;
        if(mode==CovePlayer::Mode::Airborne)next=verticalSpeed>0?Clip::Jump:Clip::Fall;
        else if(mode==CovePlayer::Mode::Swimming)next=Clip::Swim;
        else if(mode==CovePlayer::Mode::Helm)next=toolActive?Clip::Tool:Clip::Helm;
        else if(previousMode_==CovePlayer::Mode::Airborne) {landing_= .16;next=Clip::Land;}
        else if(landing_>0)next=Clip::Land;
        else if(relativeSpeed>.1)next=Clip::Walk;
        else if(toolActive)next=Clip::Tool;
        landing_=std::max(0.,landing_-dt);previousMode_=mode;
        if(next!=clip_){previous_=clip_;previousTime_=time_;clip_=next;time_=0;blend_=0;}
        const double rate=clip_==Clip::Walk?std::clamp(relativeSpeed/3.6,.2,1.5):1;
        time_+=dt*rate;previousTime_+=dt;blend_=std::min(1.,blend_+dt/.14);
        // Keep looping time bounded during long-lived sessions; nonlooping
        // jump/land clips clamp in the sampler and never overflow a frame.
        if(time_>3600)time_=std::fmod(time_,3600.);
        if(previousTime_>3600)previousTime_=std::fmod(previousTime_,3600.);
    }
    [[nodiscard]] bool sample(const assets::RigidAnimationAsset& asset,const glm::dmat4& root,
        assets::RigidAnimationPose& pose,std::string& error) const {
        const auto index=asset.clips[static_cast<size_t>(clip_)];
        const bool loop=loops(clip_);
        std::optional<assets::RigidAnimationBlend> blend;
        if(blend_<1)blend=assets::RigidAnimationBlend{asset.clips[static_cast<size_t>(previous_)],previousTime_,1-blend_,loops(previous_)};
        return assets::sampleRigidAnimation(asset,index,time_,loop,blend,root,pose,error);
    }
    [[nodiscard]] Clip clip() const noexcept{return clip_;}
    [[nodiscard]] std::string_view name() const noexcept{return assets::kRobotClips[static_cast<size_t>(clip_)];}
    [[nodiscard]] double time() const noexcept{return time_;}
    [[nodiscard]] double blend() const noexcept{return blend_;}
private:
    [[nodiscard]] static bool loops(Clip clip) noexcept {return clip!=Clip::Jump&&clip!=Clip::Land&&clip!=Clip::Fall;}
    Clip clip_=Clip::Idle,previous_=Clip::Idle;
    CovePlayer::Mode previousMode_=CovePlayer::Mode::Walking;
    double time_=0,previousTime_=0,blend_=1,landing_=0;
};
}
