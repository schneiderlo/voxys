#pragma once
#include "game/adventure/frontier_state.hpp"
#include <glm/vec2.hpp>
#include <glm/geometric.hpp>
#include <algorithm>
#include <cmath>

namespace voxy::game::adventure {
enum class FrontierAttackPhase : uint8_t { Idle, Windup, Strike, Recovery };
struct FrontierCombatProfile {
    double windup, strike, recovery;
    double approachSpeed, strikeSpeed, retreatSpeed, triggerRange, strikeRange, strikeCone;
    uint16_t damage;
    bool committed;
};
inline constexpr FrontierCombatProfile frontierCombatProfile(FrontierEnemyArchetype kind) noexcept {
    // Speeds multiply the scaled controller's normal walking speed. Health stays
    // at the installed 70/100 values so existing durable records remain valid.
    return kind==FrontierEnemyArchetype::Brute
        ?FrontierCombatProfile{.95,.55,1.7,.5,2.,0,10.5,4.7,.72,30,true}
        :FrontierCombatProfile{.48,.32,1.15,1.,1.65,.65,7.,4.4,.55,12,false};
}
struct FrontierAttackState {
    FrontierAttackPhase phase=FrontierAttackPhase::Idle;
    double seconds=0;
    glm::dvec2 direction{0,-1};
    bool hitSpent=false;
};
inline bool frontierBeginAttack(FrontierAttackState& state,const FrontierCombatProfile& profile,glm::dvec2 direction) noexcept {
    const double length=glm::length(direction);
    if(state.phase!=FrontierAttackPhase::Idle||!std::isfinite(length)||length<1e-6)return false;
    state={FrontierAttackPhase::Windup,profile.windup,direction/length,false};return true;
}
inline void frontierAdvanceAttack(FrontierAttackState& state,const FrontierCombatProfile& profile,double seconds) noexcept {
    if(!std::isfinite(seconds)||seconds<=0)return;
    for(unsigned transition=0;transition<3&&state.phase!=FrontierAttackPhase::Idle;++transition) {
        if(seconds+1e-10<state.seconds){state.seconds-=seconds;return;}
        seconds=std::max(0.,seconds-state.seconds);
        if(state.phase==FrontierAttackPhase::Windup){state.phase=FrontierAttackPhase::Strike;state.seconds=profile.strike;}
        else if(state.phase==FrontierAttackPhase::Strike){state.phase=FrontierAttackPhase::Recovery;state.seconds=profile.recovery;}
        else {state.phase=FrontierAttackPhase::Idle;state.seconds=0;}
        if(seconds<=0)return;
    }
}
inline void frontierEndStrike(FrontierAttackState& state,const FrontierCombatProfile& profile) noexcept {
    state.phase=FrontierAttackPhase::Recovery;state.seconds=profile.recovery;
}
inline bool frontierInterruptAttack(FrontierAttackState& state,const FrontierCombatProfile& profile) noexcept {
    if(profile.committed&&(state.phase==FrontierAttackPhase::Windup||state.phase==FrontierAttackPhase::Strike))return false;
    // Hits never shorten the exposed recovery window or renew a charge.
    if(state.phase!=FrontierAttackPhase::Recovery) {
        state.phase=FrontierAttackPhase::Recovery;state.seconds=profile.recovery;
    }
    return true;
}
inline bool frontierSpendStrikeContact(FrontierAttackState& state,bool reachable,bool invulnerable) noexcept {
    if(state.phase!=FrontierAttackPhase::Strike||state.hitSpent||!reachable)return false;
    // A successfully dodged contact also spends the swing: it cannot catch the
    // player again at the end of the same long charge when immunity expires.
    state.hitSpent=true;return !invulnerable;
}
} // namespace voxy::game::adventure
