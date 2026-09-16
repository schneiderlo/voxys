#include "game/adventure/adventure_trail_content.hpp"
#include "game/adventure/adventure_session.hpp"
#include <array>

namespace voxy::game::adventure {
namespace {
constexpr std::array<TrailQuestDefinition,4> quests{{
    {2,2,1,"Prepare for the trail","Equip a Trail compass and Trail staff, then return to Rivet."},
    {3,3,2,"Recover the relay component","Collect the relay guardian's core, then return to Lumen."},
    {4,3,3,"Restore the relay","Build a useful field home near the relay and fit the recovered core."},
    {5,1,1,"The Surveyor's Notes","Explore both marked discoveries, then return to Moss for the Wide stone step recipe."},
}};
bool completed(const FirstHomeProgress& value) noexcept {
    return value.phase==QuestPhase::Completed&&value.rewardRevision!=0;
}
bool fail(std::string& error,const char* message){error=message;return false;}
}
std::span<const TrailQuestDefinition> trailQuestDefinitions() noexcept{return quests;}
std::string_view trailContentFingerprint() noexcept {
    // Frozen installed schema4 recipe. New survey semantics are hashed after
    // this accepted identity, not substituted into the old content recipe.
    return "adventure-trail-main-r01:schema4-same-layout:quest2-rivet-after1-current-equipped-compass5-staff6:"
        "quest3-lumen-after2-historic-encounter2-claim:quest4-lumen-after3-explicit-accept-atomic-relay-core7:"
        "discovery-separate-arrival-and-once-material-claim:quest5-unearned:"
        "trail-sites-r01:discovery1-signal-terrace-x30-z-923-reward-scrap4:"
        "discovery2-survey-overlook-x22-z-1031-reward-stone12:node19-x20-z-924-stone12:"
        "node20-x21-z-936-wood16:node21-x22-z-1031-scrap4:terrain-support-radius0.3-skin0.005:"
        "relay-x-63-z-975:field-home-bed-distance-relay-le25-town-gt40-owned-usable-bed-chest-bench-same-structure-no-registration-required:"
        "field-furniture-access-r01:same-bed-recovery-actual-walking-16m-region-shared8192tick-budget:"
        "watch-arch-x-40-z-1095:node-ground-no-skin:site-ground-skin:"
        "scenery-signal30,-920.5-posts3pier-z+-.72-beam:cairn22,-1033.5-3pier:"
        "watch-40,-1095-posts6-3pier-x+-.72-beam:fork-59,-917-3pier:pieces23";
}
std::string_view sideQuestContentFingerprint() noexcept {
    return "adventure-surveyor-r01:schema5-same-layout:quest5-moss-after1-explicit-accept:"
        "both-discovery-arrival-receipts-claims-not-required:permanent-recipe2-wide-stone-step:"
        "three-existing-piers-x0-z-24,0,24-grid-ticks-bottom-centre-quarterturn:6stone-no-free-stock";
}
const TrailQuestDefinition* trailQuestDefinition(uint8_t id) noexcept {
    return id>=2&&id<=5?&quests[id-2u]:nullptr;
}
bool trailQuestPrerequisite(const AdventureState& state,uint8_t id) noexcept {
    const auto* definition=trailQuestDefinition(id);if(!definition)return false;
    return definition->prerequisite==1?completed(state.firstHome):completed(state.trail.quests[definition->prerequisite-2u]);
}
bool trailQuestReady(const AdventureState& state,uint8_t id) noexcept {
    switch(id) {
    case 2:return state.equippedUtility==ItemStack{ItemKind::TrailCompass,1}&&state.equippedTool==ItemStack{ItemKind::TrailStaff,1};
    case 3:return state.combat.encounters[1].lootClaimRevision!=0;
    case 4:return state.trail.relayActivationRevision!=0;
    case 5:return state.trail.discoveries[0].discoveredRevision!=0&&state.trail.discoveries[1].discoveredRevision!=0;
    default:return false;
    }
}
bool wideStoneStepRecipeUnlocked(const AdventureState& state) noexcept {
    return completed(state.trail.quests[3])&&state.trail.quests[3].rewardRevision<=state.revision;
}
bool validTrailContent(const AdventureContent& content) noexcept {
    if(!content.enableTrailProgress)return content.discoveries==std::array<DiscoveryContent,2>{};
    if(content.encounters[1].id!=2||!content.encounters[1].grantsRelayCore)return false;
    for(size_t i=0;i<content.discoveries.size();++i) {
        const auto& site=content.discoveries[i];
        if(site.id!=i+1||!AdventureSession::validPose(site.position)||!validStack(site.reward)
            ||(site.reward.kind!=ItemKind::Wood&&site.reward.kind!=ItemKind::Stone&&site.reward.kind!=ItemKind::Scrap))return false;
    }
    return true;
}
bool validateTrailProgress(const AdventureState& state,const AdventureContent& content,std::string& error) {
    if(!validTrailContent(content))return fail(error,"Installed trail content is invalid.");
    if(!content.enableTrailProgress) {
        if(state.trail!=AdventureTrailProgress{})return fail(error,"This trail progression is not enabled by the installed content.");
        return true;
    }
    for(const auto& site:state.trail.discoveries) {
        if(site.discoveredRevision>state.revision||site.rewardClaimRevision>state.revision
            ||(site.rewardClaimRevision&&(!site.discoveredRevision||site.rewardClaimRevision<=site.discoveredRevision)))
            return fail(error,"Discovery or reward receipt is invalid.");
    }
    for(const auto& definition:quests) {
        const auto& progress=state.trail.quests[definition.id-2u];
        if(!validFirstHomeProgress(progress,state.revision))return fail(error,"Trail quest progress is invalid.");
        if(progress.phase==QuestPhase::NotAccepted)continue;
        if(!trailQuestPrerequisite(state,definition.id))return fail(error,"The preceding trail quest is incomplete.");
        const auto prior=definition.prerequisite==1?state.firstHome.rewardRevision:state.trail.quests[definition.prerequisite-2u].rewardRevision;
        if(progress.phase==QuestPhase::Completed&&progress.rewardRevision<=prior)
            return fail(error,"Trail quest completion receipts are out of order.");
    }
    const auto& recovered=state.trail.quests[1];const auto claim=state.combat.encounters[1].lootClaimRevision;
    if(recovered.phase==QuestPhase::Completed&&(!claim||recovered.rewardRevision<=claim))
        return fail(error,"Recovering the relay component requires its prior loot claim.");
    const auto& restored=state.trail.quests[2];const auto activation=state.trail.relayActivationRevision;
    if((activation!=0)!=(restored.phase==QuestPhase::Completed)
        ||(activation&&(activation!=restored.rewardRevision||activation>state.revision||!claim||activation<=claim)))
        return fail(error,"Relay activation and quest completion must share one valid receipt.");
    const auto& survey=state.trail.quests[3];
    if(survey.phase==QuestPhase::Completed&&(!trailQuestReady(state,5)
        ||survey.rewardRevision<=state.trail.discoveries[0].discoveredRevision
        ||survey.rewardRevision<=state.trail.discoveries[1].discoveredRevision))
        return fail(error,"The survey recipe requires both prior discovery arrivals.");
    error.clear();return true;
}
} // namespace voxy::game::adventure
