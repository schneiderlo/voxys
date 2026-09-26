#include "game/adventure/adventure_runtime.hpp"
#include "game/adventure/construction_policy.hpp"

namespace voxy::game::adventure {
namespace {
bool sameAppearance(const WorldPart& a,const WorldPart& b) {
    return a.kind==b.kind&&a.position==b.position&&a.yawQuarterTurns==b.yawQuarterTurns&&a.paint==b.paint;
}
}
void AdventureRuntime::frontierUndo() {
    if(frontierUndo_.empty()){status_="Nothing to undo.";return;}
    const auto edit=frontierUndo_.back();
    const auto* current=AdventureSession::findPart(state(),edit.after?edit.after->part.id:edit.before->part.id);
    if(edit.after&&(!current||!sameAppearance(*current,edit.after->part))) {status_="That piece has changed. This edit cannot be undone.";return;}
    std::optional<AdventureSession::PreparedChange> change;
    if(!edit.before)change=session_->prepareRemove(stamp(),edit.after->part.id,validator(),status_);
    else if(!edit.after)change=session_->prepareRestorePart(stamp(),*edit.before,validator(),status_);
    else if(edit.before->part.paint!=edit.after->part.paint)change=session_->prepareRepaint(stamp(),edit.before->part.id,edit.before->part.paint,validator(),status_);
    else change=session_->prepareMovePart(stamp(),edit.before->part.id,edit.before->part.position,edit.before->part.yawQuarterTurns,validator(),status_);
    frontierHistoryApplying_=true;const bool accepted=commit(std::move(change));frontierHistoryApplying_=false;
    if(accepted){frontierUndo_.pop_back();frontierRedo_.push_back(edit);frontierSound("place");status_="Edit undone. Your other progress is kept.";}
}
void AdventureRuntime::frontierRedo() {
    if(frontierRedo_.empty()){status_="Nothing to redo.";return;}
    const auto edit=frontierRedo_.back();
    const auto* current=AdventureSession::findPart(state(),edit.before?edit.before->part.id:edit.after->part.id);
    if(edit.before&&(!current||!sameAppearance(*current,edit.before->part))) {status_="That piece has changed. This edit cannot be redone.";return;}
    std::optional<AdventureSession::PreparedChange> change;
    if(!edit.after)change=session_->prepareRemove(stamp(),edit.before->part.id,validator(),status_);
    else if(!edit.before)change=session_->prepareRestorePart(stamp(),*edit.after,validator(),status_);
    else if(edit.before->part.paint!=edit.after->part.paint)change=session_->prepareRepaint(stamp(),edit.after->part.id,edit.after->part.paint,validator(),status_);
    else change=session_->prepareMovePart(stamp(),edit.after->part.id,edit.after->part.position,edit.after->part.yawQuarterTurns,validator(),status_);
    frontierHistoryApplying_=true;const bool accepted=commit(std::move(change));frontierHistoryApplying_=false;
    if(accepted){frontierRedo_.pop_back();frontierUndo_.push_back(edit);frontierSound("place");status_="Edit redone.";}
}
bool AdventureRuntime::frontierEditTarget(bool repaint) {
    if(!building_||menu_!=Menu::None)return false;
    if(!repaint&&frontierMoving_){frontierMoving_.reset();status_="Move cancelled.";return true;}
    const auto* part=AdventureSession::findPart(state(),targetPart_);
    if(!part){status_="Aim at one of your pieces.";return false;}
    if(repaint) {
        if(commit(session_->prepareRepaint(stamp(),part->id,selectedPaint_,validator(),status_))) {
            status_="Piece repainted.";frontierSound("place");return true;
        }
        return false;
    }
    RemovedPartSnapshot snapshot;snapshot.part=*part;
    for(const auto& structure:state().structures)for(const auto& entry:structure.parts)if(entry.id==part->id){snapshot.structure=structure.id;snapshot.origin=structure.origin;}
    for(const auto& component:state().components)if(component.part==part->id)snapshot.component=component;
    frontierMoving_=snapshot;selected_=part->kind;selectedPaint_=part->paint;yaw_=part->yawQuarterTurns;
    heightSteps_=0;previewResult_.reset();status_="Aim and place to move this piece. G or Remove cancels.";return true;
}
} // namespace voxy::game::adventure
