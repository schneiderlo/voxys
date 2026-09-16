#include "game/adventure/quests.hpp"
#include "game/adventure/adventure_session.hpp"
#include "game/adventure/construction_policy.hpp"
#include <algorithm>
#include <utility>

namespace voxy::game::adventure {
bool validFirstHomeProgress(const FirstHomeProgress& progress,uint64_t acceptedRevision) noexcept {
    switch(progress.phase) {
    case QuestPhase::NotAccepted:
    case QuestPhase::Active:return progress.rewardRevision==0;
    case QuestPhase::Completed:return progress.rewardRevision!=0&&progress.rewardRevision<=acceptedRevision;
    }
    return false;
}
bool trailCompassRecipeUnlocked(const FirstHomeProgress& progress) noexcept {
    return progress.phase==QuestPhase::Completed&&progress.rewardRevision!=0;
}
FirstHomeReadiness firstHomeReadiness(const AdventureState& state,const AdventureSpatialQueries& queries) {
    const auto* bed=AdventureSession::findComponent(state,state.registeredBed);
    if(!bed||bed->kind!=FurnitureKind::Bed||bed->owner!=1)
        return {FirstHomeStep::RegisterBed,0,"Use a sheltered bed to register your home."};
    const auto home=std::find_if(state.structures.begin(),state.structures.end(),[&](const auto& structure){return structure.id==bed->structure&&structure.owner==bed->owner;});
    if(home==state.structures.end())return {FirstHomeStep::RegisterBed,0,"Use a sheltered bed to register your home."};
    PlayerPose recovery;std::string reason;
    if(!usableBed(state,bed->id,queries,recovery,reason))return {FirstHomeStep::MakeBedUsable,home->id,std::move(reason)};
    const auto hasFurniture=[&](FurnitureKind kind) {
        return std::any_of(state.components.begin(),state.components.end(),[&](const auto& component){return component.structure==home->id&&component.owner==home->owner&&component.kind==kind;});
    };
    if(!hasFurniture(FurnitureKind::Chest))return {FirstHomeStep::AddChest,home->id,"Add a chest to your registered home."};
    if(!hasFurniture(FurnitureKind::Workbench))return {FirstHomeStep::AddWorkbench,home->id,"Add a workbench to your registered home."};
    return {FirstHomeStep::Ready,home->id,"Your home is ready. Return to Moss."};
}
} // namespace voxy::game::adventure
