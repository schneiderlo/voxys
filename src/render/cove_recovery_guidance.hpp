#pragma once

#include "render/cove_hud.hpp"
#include <algorithm>
#include <cmath>
#include <string_view>
#include <utility>

namespace voxy::render {

// Read-only facts from the current native command guards and observed cargo.
// This formatter cannot grant permission or mutate a mission. In particular,
// canDeliver includes the authoritative physical eligibility callback; numeric
// values below explain refusal but are never used to declare success.
struct CoveRecoveryFacts {
    enum class Job { Missing, Available, Accepted, Completed };
    Job job=Job::Missing;
    bool commandsReady=false,canAccept=false,canDeliver=false,storageReady=false;
    bool deliveryPending=false,deliveryDurable=false,cargoBanked=false,cargoObserved=false,onBoat=false;
    bool hasWinch=false,onWinchRoot=false,towReady=false,towAttached=false,towConfirmed=false;
    double hookDistance=0,harborDistance=0,harborLimit=0;
    double height=0,minimumHeight=0,speed=0,maximumSpeed=0,spin=0,maximumSpin=0;
    bool harborPresent=false,harborInstalled=false,harborDurable=false,harborPending=false;
    bool atDock=false,canInstall=false,canUseLift=false;
};
struct CoveRecoveryGuidance {
    std::string_view step="none",title="Salvage Cove",controls="B: Build at dock";
    std::string status="P: Pause before saving";
    CoveHudTone tone=CoveHudTone::Neutral;
};
struct CovePauseFacts {
    bool paused=false,storageRevoked=false,checkpointPending=false,harborPending=false,rescuePending=false;
    bool cargoBanked=false,deliveryDurable=false,harborPowered=false;
    std::string_view saveStatus{},harborRefusal{};
};
// Pause is presented before workshop/running guidance. Historical delivery or
// power must never hide a current installation, save attempt or save failure.
inline void applyCovePauseGuidance(const CovePauseFacts& f,CoveHudContent& hud) {
    using Tone=CoveHudTone;
    hud.title=f.paused?"Cove paused":"Stopping safely";
    hud.selected="Expedition on hold";hud.objective="paused";hud.tone=Tone::Neutral;
    hud.status=f.saveStatus.empty()?(f.paused?"Ready to save":"Waiting for motion to stop"):std::string(f.saveStatus);
    hud.hints={f.paused?"F10: Save expedition":"Wait for motion to stop",f.paused?"P: Resume":"", ""};
    if(f.storageRevoked){
        hud.objective="storage-revoked";hud.selected="Save needs recovery";
        hud.status="Restart game to recover";hud.hints={"Progress stays paused","",""};hud.tone=Tone::Blocked;
        return;
    }
    if(f.harborPending||f.checkpointPending||f.rescuePending){
        hud.objective=f.harborPending?"powering":f.rescuePending?"rescuing"
            :f.cargoBanked&&!f.deliveryDurable?"saving":"checkpoint";
        hud.selected=f.harborPending?"Powering the harbor":f.rescuePending?"Recovering your boat"
            :f.cargoBanked&&!f.deliveryDurable?"Saving delivery":"Progress awaiting save";
        hud.status=f.checkpointPending?(f.paused?"Waiting for durable save":"Waiting for motion to stop")
            :f.harborPending?"Preparing harbor lift":"Returning boat safely";
        hud.hints={f.paused&&f.checkpointPending?"F10: Retry save":"Wait for completion","Controls wait for completion",""};
        hud.tone=Tone::Waiting;
        if(f.paused&&f.checkpointPending&&f.saveStatus.starts_with("Save failed.")){
            hud.status="Save failed. F10: Retry";hud.tone=Tone::Blocked;
        }
        return;
    }
    if(f.saveStatus.starts_with("Save failed.")){
        hud.objective="save-failed";hud.selected="Save not complete";
        hud.status=f.paused?"Save failed. F10: Retry":"Save failed. Stopping safely";hud.tone=Tone::Blocked;
        return;
    }
    if(f.saveStatus.starts_with("Saving ")){
        hud.objective="manual-saving";hud.selected="Saving expedition";
        hud.hints={"Wait for save to finish","",""};hud.tone=Tone::Waiting;
        return;
    }
    if(f.deliveryDurable){
        hud.objective=f.harborPowered?"powered":"delivered";
        hud.selected=f.harborPowered?"Harbor power saved":"Generator delivery saved";
        if(f.saveStatus.empty())hud.status="P: Resume when ready";
        if(!f.harborRefusal.empty()){
            hud.objective="harbor-blocked";hud.selected="Harbor not installed";
            hud.status=f.harborRefusal;hud.tone=Tone::Blocked;
        }
    }
}
namespace cove_guidance_detail {
inline std::string decimal(double value) {
    // Bounded, locale-independent output. Upward rounding of a positive
    // deficit never tells the player to move/lift zero while still outside.
    if(value>=999)return "999+";
    const auto tenths=static_cast<unsigned>(std::ceil(std::clamp(value,0.0,999.0)*10.0));
    return std::to_string(tenths/10)+'.'+std::to_string(tenths%10);
}
}
[[nodiscard]] inline CoveRecoveryGuidance coveRecoveryGuidance(const CoveRecoveryFacts& f) {
    using Job=CoveRecoveryFacts::Job;
    using Tone=CoveHudTone;
    if(f.job==Job::Missing)return {};
    const auto result=[](std::string_view step,std::string_view title,std::string status,
        std::string_view controls,Tone tone=Tone::Neutral){return CoveRecoveryGuidance{step,title,controls,std::move(status),tone};};
    if(f.harborPending)
        return result("powering","Powering harbor","Preparing lift and save","Wait for harbor power",Tone::Waiting);
    if(f.deliveryPending)
        return f.cargoBanked?result("saving","Saving delivery","Wait for delivery save","Wait for save to finish",Tone::Waiting)
            :result("securing","Deliver generator","Securing the observed load","Wait for hand-off",Tone::Waiting);
    if(!f.commandsReady)
        return result("waiting","Recovery job","Waiting for game to be ready","B: Build at dock",Tone::Waiting);
    if(f.job==Job::Available)
        return f.canAccept?result("accept","Recover generator","First job: sunken generator","J: Accept job",Tone::Ready)
            :result("waiting","Recovery job","Waiting to accept the job","B: Build at dock",Tone::Waiting);
    if(f.job==Job::Completed) {
        if(!f.deliveryDurable)return result("saving","Saving delivery","Wait for delivery save","F10: Retry save",Tone::Waiting);
        if(!f.harborPresent)return result("delivered","Delivery saved","Generator recovered","B: Build at dock",Tone::Ready);
        if(f.harborInstalled&&f.harborDurable)
            return result("powered","Harbor powered","Harbor power saved",f.canUseLift?"F: Attach/release lift":"Walk to dock controls",Tone::Ready);
        if(!f.storageReady)return result("storage","Power harbor","Save storage unavailable","P: Pause",Tone::Blocked);
        if(f.canInstall)return result("power","Power harbor","Generator delivery saved","K: Power harbor",Tone::Ready);
        if(f.onBoat)return result("dock","Power harbor","Step off the boat at dock","E: Nearby interaction");
        if(!f.atDock)return result("dock","Power harbor","Walk to dock controls","B: Build at dock");
        return result("waiting","Power harbor","Waiting for harbor controls","P: Pause",Tone::Waiting);
    }
    // H does not require an attached tow, a winch or the helm. Check actual
    // eligibility before suggesting any of those optional ways to recover it.
    if(f.canDeliver)return result("deliver","Deliver generator","Load ready for delivery","H: Deliver generator",Tone::Ready);
    if(!f.storageReady)return result("storage","Recover generator","Save storage unavailable","P: Pause",Tone::Blocked);
    if(!f.cargoObserved)return result("waiting","Recover generator","Waiting for cargo position","B: Build at dock",Tone::Waiting);
    if(!f.onBoat)return result("board","Recover generator","Board your boat","E: Nearby interaction");
    const bool numeric=std::isfinite(f.hookDistance)&&std::isfinite(f.harborDistance)&&std::isfinite(f.harborLimit)
        &&std::isfinite(f.height)&&std::isfinite(f.minimumHeight)&&std::isfinite(f.speed)
        &&std::isfinite(f.maximumSpeed)&&std::isfinite(f.spin)&&std::isfinite(f.maximumSpin)
        &&f.harborLimit>=0&&f.maximumSpeed>=0&&f.maximumSpin>=0;
    if(!numeric)return result("waiting","Recover generator","Waiting for cargo position","P: Pause",Tone::Waiting);
    if(!f.towAttached) {
        // Cargo already in the delivery zone only needs to settle. Do not
        // demand a cable or winch that the delivery mechanic does not require.
        if(f.harborDistance<=f.harborLimit&&f.height>=f.minimumHeight) {
            if(f.speed>f.maximumSpeed)return result("slow","Settle the load",
                "Speed "+cove_guidance_detail::decimal(f.speed)+" / "+cove_guidance_detail::decimal(f.maximumSpeed)+" m/s","Stop and let it settle");
            if(f.spin>f.maximumSpin)return result("steady","Settle the load",
                "Spin "+cove_guidance_detail::decimal(f.spin)+" / "+cove_guidance_detail::decimal(f.maximumSpin)+" rad/s","Stop and let it settle");
            return result("waiting","Deliver generator","Waiting for delivery check","P: Pause",Tone::Waiting);
        }
        if(!f.hasWinch)return result("winch","Prepare your boat","Fit or enable a winch","B: Build at dock",Tone::Blocked);
        if(!f.onWinchRoot)return result("winch","Recover generator","Board the winch section","E: Nearby interaction");
        if(!f.towReady)return result("waiting","Recover generator","Waiting for winch controls","P: Pause",Tone::Waiting);
        if(f.hookDistance>8)return result("approach","Reach generator",
            "Hook range "+cove_guidance_detail::decimal(f.hookDistance)+" / 8.0 m","Steer closer to the load");
        return result("hook","Hook the generator","Generator within reach","F: Hook generator",Tone::Ready);
    }
    if(!f.towConfirmed||!f.towReady)return result("waiting","Recover generator","Waiting for tow controls","P: Pause",Tone::Waiting);
    if(f.harborDistance>f.harborLimit)return result("return","Return to harbor",
        "Harbor "+cove_guidance_detail::decimal(f.harborDistance)+" / "+cove_guidance_detail::decimal(f.harborLimit)+" m","Q/Z: Reel  F: Release");
    if(f.height<f.minimumHeight)return result("lift","Lift the generator",
        "Raise load "+cove_guidance_detail::decimal(f.minimumHeight-f.height)+" m","Q: Lift  Z: Lower");
    if(f.speed>f.maximumSpeed)return result("slow","Settle the load",
        "Speed "+cove_guidance_detail::decimal(f.speed)+" / "+cove_guidance_detail::decimal(f.maximumSpeed)+" m/s","Stop and let it settle");
    if(f.spin>f.maximumSpin)return result("steady","Settle the load",
        "Spin "+cove_guidance_detail::decimal(f.spin)+" / "+cove_guidance_detail::decimal(f.maximumSpin)+" rad/s","Stop and let it settle");
    return result("waiting","Deliver generator","Waiting for delivery check","P: Pause",Tone::Waiting);
}

} // namespace voxy::render
