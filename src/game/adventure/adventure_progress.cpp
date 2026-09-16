#include "game/adventure/adventure_progress.hpp"
#include "game/adventure/adventure_session.hpp"
#include <cmath>

namespace voxy::game::adventure {
namespace {
bool fail(std::string& error,const char* message){error=message;return false;}
bool deadline(uint64_t value,uint64_t tick) {
    return value<=tick||value-tick<=kMaximumCombatDeadlineTicks;
}
}
bool validEncounterContent(const AdventureContent& content) noexcept {
    bool empty=false;unsigned cores=0;
    for(size_t i=0;i<content.encounters.size();++i) {
        const auto& value=content.encounters[i];
        if(!value.id){if(value!=EncounterContent{})return false;empty=true;continue;}
        if(empty||value.id!=i+1||value.generation!=1||!AdventureSession::validPose(value.spawn)
            ||!value.maximumHealth||value.maximumHealth>1000||!validStack(value.loot))return false;
        if(value.grantsRelayCore) {
            if(value.id!=2||value.loot!=ItemStack{ItemKind::RelayCore,1}||++cores>1)return false;
        } else if(value.loot.kind!=ItemKind::Wood&&value.loot.kind!=ItemKind::Stone&&value.loot.kind!=ItemKind::Scrap)return false;
    }
    return true;
}
void initializeAdventureProgress(AdventureState& state,const AdventureContent& content) noexcept {
    state.combat={};state.trail={};
    for(size_t i=0;i<content.encounters.size();++i) {
        const auto& definition=content.encounters[i];if(!definition.id)continue;
        auto& checkpoint=state.combat.encounters[i].checkpoint;
        checkpoint.encounterId=definition.id;checkpoint.generation=definition.generation;
        checkpoint.pose=definition.spawn;checkpoint.health=definition.maximumHealth;
    }
}
bool validateAdventureProgress(const AdventureState& state,const AdventureContent& content,std::string& error) {
    if(!validEncounterContent(content)||state.combat.tick>state.revision)
        return fail(error,"Combat content or clock is invalid.");
    const auto tick=state.combat.tick;const auto& player=state.combat.player;
    if(player.attackSerial>tick||!deadline(player.attackReadyTick,tick)||!deadline(player.attackImpactTick,tick)
        ||!deadline(player.dodgeReadyTick,tick)||!deadline(player.dodgeUntilTick,tick)||!deadline(player.invulnerableUntilTick,tick)
        ||(player.attackImpactTick&&(!player.attackSerial||player.attackImpactTick<=tick))
        ||!std::isfinite(player.dodgeDirectionX)||!std::isfinite(player.dodgeDirectionZ)
        ||std::hypot(player.dodgeDirectionX,player.dodgeDirectionZ)>1.000001)
        return fail(error,"Player combat checkpoint is invalid.");
    if(!state.health&&(player.attackImpactTick||player.dodgeUntilTick>tick||player.invulnerableUntilTick>tick))
        return fail(error,"A defeated player cannot retain an active attack or dodge.");
    uint32_t ownedCore=itemCount(state.backpack,ItemKind::RelayCore);
    for(const auto& component:state.components)ownedCore+=itemCount(component.slots,ItemKind::RelayCore);
    unsigned claimedCore=0;
    for(size_t i=0;i<content.encounters.size();++i) {
        const auto& definition=content.encounters[i];const auto& record=state.combat.encounters[i];const auto& enemy=record.checkpoint;
        if(!definition.id){if(record!=EncounterProgress{})return fail(error,"Unknown encounter progress.");continue;}
        if(enemy.encounterId!=definition.id||enemy.generation!=definition.generation||!AdventureSession::validPose(enemy.pose)
            ||enemy.health>definition.maximumHealth||enemy.phase>EnemyPhase::Dead||enemy.phaseTicks>kMaximumCombatDeadlineTicks
            ||enemy.attackSerial>tick||enemy.lastPlayerAttackSerial>player.attackSerial
            ||record.deathRevision>state.revision||record.lootClaimRevision>state.revision)
            return fail(error,"Encounter checkpoint is invalid.");
        if(!enemy.positioned&&(enemy.phase!=EnemyPhase::Dormant||enemy.pose!=definition.spawn||enemy.health!=definition.maximumHealth
            ||enemy.phaseTicks||enemy.attackSerial||enemy.lastPlayerAttackSerial))
            return fail(error,"An unpositioned encounter must retain its untouched installed state.");
        if((enemy.health==0)!=(enemy.phase==EnemyPhase::Dead)||(enemy.health==0)!=(record.deathRevision!=0)
            ||(record.deathRevision&&!enemy.positioned)
            ||(record.lootClaimRevision&&(!record.deathRevision||record.lootClaimRevision<=record.deathRevision)))
            return fail(error,"Encounter death or loot receipt is invalid.");
        if(definition.grantsRelayCore&&record.lootClaimRevision)++claimedCore;
    }
    if(!validateTrailProgress(state,content,error))return false;
    const unsigned consumedCore=state.trail.relayActivationRevision?1u:0u;
    if(ownedCore+consumedCore!=claimedCore||ownedCore>1)return fail(error,"Relay core ownership does not match its unique loot receipt.");
    error.clear();return true;
}
} // namespace voxy::game::adventure
