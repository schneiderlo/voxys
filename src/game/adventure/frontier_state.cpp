#include "game/adventure/frontier_state.hpp"
#include "game/adventure/adventure_session.hpp"
#include <algorithm>

namespace voxy::game::adventure {
namespace {
bool fail(std::string& error,const char* text){error=text;return false;}
bool material(ItemStack stack) noexcept {
    return validStack(stack)&&(stack.kind==ItemKind::Wood||stack.kind==ItemKind::Stone||stack.kind==ItemKind::Scrap);
}
}
bool validFrontierContent(const AdventureContent& content) noexcept {
    if(!content.frontier)return content.frontierEnemies.empty()&&content.frontierSites.empty();
    if(content.freeBuilding||content.enableTrailProgress||content.frontierEnemies.size()>kMaximumFrontierEnemies
        ||content.frontierSites.empty()||content.frontierSites.size()>kMaximumFrontierSites)return false;
    // Legacy encounters remain uninstalled in this distinct progression profile.
    if(std::any_of(content.encounters.begin(),content.encounters.end(),[](const auto& e){return e.id!=0;}))return false;
    uint32_t previous=0;
    for(const auto& e:content.frontierEnemies) {
        if(!e.id||e.id<=previous||!e.generation||!AdventureSession::validPose(e.spawn)||!e.maximumHealth
            ||e.maximumHealth>1000||!material(e.loot)||e.archetype>FrontierEnemyArchetype::Brute)return false;
        previous=e.id;
    }
    previous=0;bool beacon=false;
    for(const auto& site:content.frontierSites) {
        if(!site.id||site.id<=previous||!AdventureSession::validPose(site.position)||site.kind>FrontierSiteKind::Quarry
            ||!validStack(site.reward)||(site.kind==FrontierSiteKind::Cache?!material(site.reward):site.reward!=ItemStack{}))return false;
        if((!site.defenders.empty()||site.requiresQuarryCharge)&&site.kind!=FrontierSiteKind::Beacon)return false;
        if(site.defenders.size()>kMaximumFrontierEnemies)return false;
        uint32_t previousDefender=0;
        for(const auto id:site.defenders) {
            if(id<=previousDefender||std::none_of(content.frontierEnemies.begin(),content.frontierEnemies.end(),
                [id](const auto& enemy){return enemy.id==id;}))return false;
            previousDefender=id;
        }
        if(site.requiresQuarryCharge&&std::none_of(content.resourceNodes.begin(),content.resourceNodes.end(),
            [](const auto& node){return node.yield.kind==ItemKind::CutStone;}))return false;
        previous=site.id;beacon|=site.kind==FrontierSiteKind::Beacon;
    }
    return beacon;
}
void initializeFrontierProgress(AdventureState& state,const AdventureContent& content) {
    state.frontier={};
    if(!content.frontier)return;
    for(const auto& e:content.frontierEnemies)state.frontier.enemies.push_back({e.id,e.generation,e.spawn,e.maximumHealth,0,0});
    for(const auto& site:content.frontierSites)state.frontier.sites.push_back({site.id,0,0,0});
}
const FrontierEnemyProgress* frontierEnemy(const AdventureState& state,uint32_t id) noexcept {
    for(const auto& value:state.frontier.enemies)if(value.id==id)return &value;
    return nullptr;
}
const FrontierSiteProgress* frontierSite(const AdventureState& state,uint32_t id) noexcept {
    for(const auto& value:state.frontier.sites)if(value.id==id)return &value;
    return nullptr;
}
size_t frontierRemainingDefenders(const AdventureState& state,const FrontierSiteContent& site) noexcept {
    return static_cast<size_t>(std::count_if(site.defenders.begin(),site.defenders.end(),[&](uint32_t id) {
        const auto* enemy=frontierEnemy(state,id);return !enemy||enemy->health||!enemy->deathRevision;
    }));
}
bool frontierHasQuarryHarvest(const AdventureState& state,const AdventureContent& content) noexcept {
    return std::any_of(content.resourceNodes.begin(),content.resourceNodes.end(),[&](const auto& node) {
        return node.yield.kind==ItemKind::CutStone&&std::binary_search(state.depletedNodes.begin(),state.depletedNodes.end(),node.id);
    });
}
bool frontierBeaconRequirements(const AdventureState& state,const AdventureContent& content,const FrontierSiteContent& site,std::string& error) {
    const auto remaining=frontierRemainingDefenders(state,site);
    if(remaining) {
        error="Clear this beacon's "+std::to_string(remaining)+" remaining warden"+(remaining==1?".":"s.");return false;
    }
    if(site.requiresQuarryCharge&&(!state.frontier.quarryUnlockRevision||!frontierHasQuarryHarvest(state,content)))
        return fail(error,"Mine dense stone at the Old Quarry, then carry its charge to this sanctuary.");
    return true;
}
bool validateFrontierProgress(const AdventureState& state,const AdventureContent& content,std::string& error) {
    if(!content.frontier) {
        if(!state.frontier.enemies.empty()||!state.frontier.sites.empty()||state.frontier.quarryUnlockRevision)
            return fail(error,"Frontier progress cannot enter a legacy world.");
        const auto legacy=[](ItemStack stack){return stack.kind<ItemKind::QuarryHammer;};
        if(!legacy(state.equippedTool)||!std::all_of(state.backpack.begin(),state.backpack.end(),legacy))
            return fail(error,"Frontier items cannot enter a legacy world.");
        for(const auto& component:state.components)if(!std::all_of(component.slots.begin(),component.slots.end(),legacy))
            return fail(error,"Frontier items cannot enter a legacy world.");
        return true;
    }
    if(state.firstHome!=FirstHomeProgress{}||state.trail!=AdventureTrailProgress{}||state.combat!=AdventureCombatProgress{}
        ||state.equippedUtility!=ItemStack{})return fail(error,"Legacy quest or combat state cannot enter a frontier world.");
    if(state.frontier.enemies.size()!=content.frontierEnemies.size()||state.frontier.sites.size()!=content.frontierSites.size()
        ||state.frontier.quarryUnlockRevision>state.revision)return fail(error,"Frontier records do not match installed content.");
    for(size_t i=0;i<content.frontierEnemies.size();++i) {
        const auto& definition=content.frontierEnemies[i];const auto& record=state.frontier.enemies[i];
        if(record.id!=definition.id||record.generation!=definition.generation||!AdventureSession::validPose(record.pose)
            ||record.health>definition.maximumHealth||record.deathRevision>state.revision||record.lootClaimRevision>state.revision
            ||(record.health==0)!=(record.deathRevision!=0)
            ||(record.lootClaimRevision&&(!record.deathRevision||record.lootClaimRevision<=record.deathRevision)))
            return fail(error,"Frontier enemy health, identity or loot receipt is invalid.");
    }
    uint64_t firstRestored=0;
    for(size_t i=0;i<content.frontierSites.size();++i) {
        const auto& definition=content.frontierSites[i];const auto& record=state.frontier.sites[i];
        if(record.id!=definition.id||record.discoveredRevision>state.revision||record.restoredRevision>state.revision
            ||record.rewardClaimRevision>state.revision
            ||(record.restoredRevision&&(definition.kind!=FrontierSiteKind::Beacon||!record.discoveredRevision
                ||record.restoredRevision<record.discoveredRevision))
            ||(record.rewardClaimRevision&&(definition.kind!=FrontierSiteKind::Cache||!record.discoveredRevision
                ||record.rewardClaimRevision<record.discoveredRevision)))
            return fail(error,"Frontier site state or reward receipt is invalid.");
        if(record.restoredRevision&&(!firstRestored||record.restoredRevision<firstRestored))firstRestored=record.restoredRevision;
    }
    if(state.frontier.quarryUnlockRevision!=firstRestored)return fail(error,"Quarry recipe must come from the first restored beacon.");
    const auto earned=[&](ItemStack stack){return state.frontier.quarryUnlockRevision
        ||(stack.kind!=ItemKind::QuarryHammer&&stack.kind!=ItemKind::CutStone);};
    if(!earned(state.equippedTool)||!std::all_of(state.backpack.begin(),state.backpack.end(),earned))
        return fail(error,"Restore a beacon before owning quarry equipment or cut stone.");
    for(const auto& component:state.components)if(!std::all_of(component.slots.begin(),component.slots.end(),earned))
        return fail(error,"Stored quarry items require the earned recipe.");
    return true;
}
std::string frontierContentFingerprint() {
    return "frontier-r01:schema7:stable16enemies-8sites:health-death-loot-site-receipts:"
        "starter48wood32stone12scrap-staff:restore12wood12stone8scrap-outer4cutstone:quarry-recipe4wood4stone4scrap:"
        "item8quarryhammer1-item9cutstone999:one-shot-harvest-hammer-double:camp-recovery";
}
} // namespace voxy::game::adventure
