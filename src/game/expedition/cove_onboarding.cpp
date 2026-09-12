#include "game/expedition/cove_onboarding.hpp"
#include <array>
#include <algorithm>
#include <cmath>
#include <utility>

namespace voxy::game::expedition {
namespace {
constexpr std::array copy{
    std::pair{"menu.pause", "Game paused"},
    std::pair{"menu.resume", "Resume game"},
    std::pair{"menu.job", "Recovery job"},
    std::pair{"menu.map", "Nearby places"},
    std::pair{"menu.inventory", "Parts and materials"},
    std::pair{"menu.settings", "Controls and accessibility"},
    std::pair{"menu.help", "Learn to play"},
    std::pair{"menu.save", "Save expedition"},
    std::pair{"menu.quit", "Save or leave"},
    std::pair{"learn.walk.title", "Walk along the dock"},
    std::pair{"learn.walk.keys", "Use your movement keys to walk. The mouse turns your view."},
    std::pair{"learn.walk.pad", "Move with the left stick. Turn your view with the right stick."},
    std::pair{"learn.workshop.title", "Open the workshop"},
    std::pair{"learn.workshop.text", "Stand on the dock. Open Workshop to change your boat."},
    std::pair{"learn.edit.title", "Make a small change"},
    std::pair{"learn.edit.text", "Choose a part or brick. Move it to a clear connection, then Keep."},
    std::pair{"learn.try.title", "Try your design"},
    std::pair{"learn.try.text", "Test sail uses a temporary boat. Return discards the test. Launch keeps your design."},
    std::pair{"learn.board.title", "Step aboard"},
    std::pair{"learn.board.text", "Walk to the orange boarding mark. Use Interact to board your boat."},
    std::pair{"learn.sail.title", "Take the helm"},
    std::pair{"learn.sail.keys", "Interact at the helm. Use your movement keys to drive and steer."},
    std::pair{"learn.sail.pad", "Interact at the helm. Use the left stick to drive and steer."},
    std::pair{"learn.save.title", "Keep your progress"},
    std::pair{"learn.save.text", "Pause, then Save expedition. Wait for Saved before leaving."},
    std::pair{"learn.done.title", "Ready to explore"},
    std::pair{"learn.done.text", "Your next goal is the sunken generator. Recovery job shows what to do next."},
};
}
std::string_view coveUiText(std::string_view key) noexcept {
    for(const auto& [name,text]:copy)if(key==name)return text;
    return key;
}
void CoveOnboarding::restart() noexcept { *this={}; }
void CoveOnboarding::observe(const Facts& f) noexcept {
    if(!f.epoch||!std::isfinite(f.walkedMetres)||!std::isfinite(f.sailedMetres)
        ||f.walkedMetres<0||f.sailedMetres<0)return;
    if(seen_&&f.epoch!=epoch_)restart();
    if(seen_&&f.tick<tick_)return;
    epoch_=f.epoch;tick_=f.tick;seen_=true;
    walked_=std::max(walked_,f.walkedMetres);
    workshopSeen_|=f.workshop;editSeen_|=f.keptEdit;
    trySeen_|=f.tested||f.launched;boardSeen_|=f.onBoat;
    sailSeen_|=f.atHelm&&f.sailedMetres>=1.;savedSeen_|=f.saved;
    // One step at a time retains the action/instruction association even when
    // an experienced player's current save already satisfies later facts.
    switch(step_){
    case Step::Walk:if(walked_>=1.)step_=Step::Workshop;break;
    case Step::Workshop:if(workshopSeen_)step_=Step::Edit;break;
    case Step::Edit:if(editSeen_)step_=Step::TryBoat;break;
    case Step::TryBoat:if(trySeen_)step_=Step::Board;break;
    case Step::Board:if(boardSeen_)step_=Step::Sail;break;
    case Step::Sail:if(sailSeen_)step_=Step::Save;break;
    case Step::Save:if(savedSeen_)step_=Step::Finished;break;
    case Step::Finished:break;
    }
}
CoveOnboarding::Card CoveOnboarding::card(bool controller,bool paused,bool workshop) const noexcept {
    Card result;result.step=step_;result.number=static_cast<unsigned>(step_)+1;
    const auto set=[&](std::string_view key,std::string_view title,std::string_view text,std::string_view action,int command){
        result.key=key;result.title=coveUiText(title);result.text=coveUiText(text);
        result.actionLabel=action;result.action=command;
    };
    switch(step_){
    case Step::Walk:set("learn.walk","learn.walk.title",controller?"learn.walk.pad":"learn.walk.keys","Resume",paused?91:0);break;
    case Step::Workshop:set("learn.workshop","learn.workshop.title","learn.workshop.text","Workshop",paused?91:60);break;
    case Step::Edit:set("learn.edit","learn.edit.title","learn.edit.text",workshop?"Back to building":"Workshop",workshop?0:paused?91:60);break;
    case Step::TryBoat:set("learn.try","learn.try.title","learn.try.text",workshop?"Test sail":"Workshop",workshop?340:paused?91:60);break;
    case Step::Board:set("learn.board","learn.board.title","learn.board.text","Interact",paused?91:30);break;
    case Step::Sail:set("learn.sail","learn.sail.title",controller?"learn.sail.pad":"learn.sail.keys","Interact",paused?91:30);break;
    case Step::Save:set("learn.save","learn.save.title","learn.save.text",paused?"Save expedition":"Pause",paused?350:90);break;
    case Step::Finished:set("learn.done","learn.done.title","learn.done.text","Recovery job",351);result.number=7;result.complete=true;break;
    }
    if(paused&&result.action==91)result.actionLabel="Resume";
    return result;
}
}
