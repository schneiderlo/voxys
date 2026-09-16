#include "game/adventure/adventure_session.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <set>

namespace voxy::game::adventure {
namespace {
bool refuse(std::string& error,const char* message) {error=message;return false;}
bool increment(uint64_t& value) noexcept {
    if(value==std::numeric_limits<uint64_t>::max()) return false;
    ++value;return true;
}
bool digestPresent(const core::Sha256Digest& digest) {
    return std::any_of(digest.bytes.begin(),digest.bytes.end(),[](std::byte b){return b!=std::byte{};});
}
bool gridInWorld(GridPosition position) noexcept {
    return construction::isValid(position) && std::abs(int64_t(position.x))<=409600 &&
        std::abs(int64_t(position.z))<=409600 && std::abs(int64_t(position.y))<=204800;
}
StructureComponent* component(AdventureState& state,uint64_t id) {
    const auto found=std::find_if(state.components.begin(),state.components.end(),[&](const auto& c){return c.id==id;});
    return found==state.components.end()?nullptr:&*found;
}
struct ContainerView {std::span<ItemStack> slots;uint64_t* revision=nullptr;};
ContainerView container(AdventureState& state,uint64_t id) {
    if(!id)return {state.backpack,&state.backpackRevision};
    auto* chest=component(state,id);
    if(!chest || chest->kind!=FurnitureKind::Chest || chest->owner!=1)return {};
    return {chest->slots,&chest->revision};
}
bool validContent(const AdventureContent& content) {
    if(!digestPresent(content.identity) || !AdventureSession::validPose(content.town) || content.resourceNodes.size()>kMaximumResourceNodes)return false;
    if(content.legacyIdentity && !digestPresent(*content.legacyIdentity))return false;
    for(const auto& identity:content.compatibilityIdentities)if(identity&&!digestPresent(*identity))return false;
    if(content.legacyIdentity&&content.compatibilityIdentities[0]&&content.legacyIdentity!=content.compatibilityIdentities[0])return false;
    if(!validEncounterContent(content)||!validTrailContent(content))return false;
    uint32_t previous=0;
    for(const auto& node:content.resourceNodes) {
        if(!node.id || node.id<=previous || !AdventureSession::validPose(node.position) || !validStack(node.yield) ||
            (node.yield.kind!=ItemKind::Wood && node.yield.kind!=ItemKind::Stone && node.yield.kind!=ItemKind::Scrap) || node.yield.quantity>499) return false;
        previous=node.id;
    }
    return true;
}
}

bool AdventureSession::validPose(PlayerPose p) noexcept {
    return std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.z)&&std::isfinite(p.yaw)&&
        std::abs(p.x)<=8192 && std::abs(p.z)<=8192 && std::abs(p.y)<=4096 && std::abs(p.yaw)<=3.14159265358979323846;
}
const WorldPart* AdventureSession::findPart(const AdventureState& state,uint64_t id) noexcept {
    for(const auto& structure:state.structures)
        for(const auto& part:structure.parts)if(part.id==id)return &part;
    return nullptr;
}
const StructureComponent* AdventureSession::findComponent(const AdventureState& state,uint64_t id) noexcept {
    for(const auto& value:state.components)if(value.id==id)return &value;
    return nullptr;
}
bool AdventureSession::validate(const AdventureState& state,const AdventureContent& content,std::string& error) {
    if(!validContent(content))return refuse(error,"Installed adventure content is invalid.");
    if(!construction::isValid(state.world) || state.content!=content.identity || !state.epoch || state.lastIssuedId<2 ||
        !state.starterGranted || !validPose(state.player) || !validPose(state.recovery) || state.health>100)
        return refuse(error,"Adventure identity or player state is invalid.");
    if(state.backpackRevision>state.revision || !std::all_of(state.backpack.begin(),state.backpack.end(),validStack) ||
        !validStack(state.equippedTool) || (state.equippedTool.kind!=ItemKind::None && state.equippedTool.kind!=ItemKind::FieldHammer&&state.equippedTool.kind!=ItemKind::TrailStaff))
        return refuse(error,"Backpack or equipment is invalid.");
    if((state.metNpcMask&0xf8u)!=0 || !validFirstHomeProgress(state.firstHome,state.revision))
        return refuse(error,"Adventure quest progress is invalid.");
    if(!validStack(state.equippedUtility) ||
        (state.equippedUtility.kind!=ItemKind::None && state.equippedUtility.kind!=ItemKind::TrailCompass))
        return refuse(error,"Utility equipment is invalid.");
    bool ownsCompass=state.equippedUtility.kind==ItemKind::TrailCompass || itemCount(state.backpack,ItemKind::TrailCompass)!=0;
    if(state.structures.size()>kMaximumStructures || state.components.size()>kMaximumComponents || state.depletedNodes.size()>kMaximumResourceNodes)
        return refuse(error,"Adventure capacity exceeded.");
    std::set<uint64_t> ids{1,2};
    size_t parts=0;
    uint64_t previousStructure=0;
    for(const auto& structure:state.structures) {
        if(structure.id<=previousStructure || structure.id>state.lastIssuedId || structure.owner!=1 ||
            structure.revision>state.revision || structure.parts.empty() || !gridInWorld(structure.origin) || !ids.insert(structure.id).second)
            return refuse(error,"Structure identity or order is invalid.");
        previousStructure=structure.id;
        uint64_t previousPart=0;
        parts+=structure.parts.size();
        if(parts>kMaximumParts)return refuse(error,"Too many building pieces.");
        for(const auto& part:structure.parts) {
            if(part.id<=previousPart || part.id>state.lastIssuedId || !buildingDefinition(part.kind) || !gridInWorld(part.position) ||
                part.yawQuarterTurns>3 || part.paint>0xffffff || !ids.insert(part.id).second)
                return refuse(error,"Building piece identity or geometry is invalid.");
            previousPart=part.id;
        }
    }
    uint64_t previousComponent=0;
    std::set<uint64_t> furnitureParts;
    for(const auto& value:state.components) {
        const auto* part=findPart(state,value.part);
        const auto owner=std::find_if(state.structures.begin(),state.structures.end(),[&](const auto& s){return s.id==value.structure;});
        if(value.id<=previousComponent || value.id>state.lastIssuedId || value.owner!=1 || value.revision>state.revision ||
            !ids.insert(value.id).second || !part || owner==state.structures.end() ||
            std::none_of(owner->parts.begin(),owner->parts.end(),[&](const auto& p){return p.id==value.part;}) ||
            buildingDefinition(part->kind)->furniture==FurnitureKind::None || buildingDefinition(part->kind)->furniture!=value.kind ||
            !furnitureParts.insert(value.part).second || !std::all_of(value.slots.begin(),value.slots.end(),validStack))
            return refuse(error,"Furniture identity or contents are invalid.");
        if(value.kind!=FurnitureKind::Chest && std::any_of(value.slots.begin(),value.slots.end(),[](auto slot){return slot.kind!=ItemKind::None;}))
            return refuse(error,"Only chests can own stored items.");
        if(value.doorOpen&&value.kind!=FurnitureKind::Door)
            return refuse(error,"Only a hinged door can be open.");
        previousComponent=value.id;
        ownsCompass=ownsCompass || itemCount(value.slots,ItemKind::TrailCompass)!=0;
    }
    if(ownsCompass && !trailCompassRecipeUnlocked(state.firstHome))
        return refuse(error,"A trail compass requires its earned recipe.");
    for(const auto& structure:state.structures)for(const auto& part:structure.parts)
        if(buildingDefinition(part.kind)->furniture!=FurnitureKind::None && !furnitureParts.contains(part.id))
            return refuse(error,"Furniture is missing its functional state.");
    if(state.registeredBed) {
        const auto* bed=findComponent(state,state.registeredBed);
        if(!bed || bed->kind!=FurnitureKind::Bed)return refuse(error,"The saved recovery bed is missing.");
    } else if(state.recovery!=content.town)return refuse(error,"Town recovery point does not match installed content.");
    uint32_t previousNode=0;
    for(const auto node:state.depletedNodes) {
        if(node<=previousNode || std::none_of(content.resourceNodes.begin(),content.resourceNodes.end(),[&](const auto& n){return n.id==node;}))
            return refuse(error,"Resource depletion does not match installed content.");
        previousNode=node;
    }
    return validateAdventureProgress(state,content,error);
}
std::unique_ptr<AdventureSession> AdventureSession::create(construction::WorldNamespace world,const AdventureContent& content,std::string& error) {
    auto session=std::unique_ptr<AdventureSession>(new AdventureSession);
    session->content_=content;
    auto& state=session->state_;state.world=world;state.content=content.identity;state.player=state.recovery=content.town;
    state.starterGranted=true;
    if(!content.freeBuilding) {state.backpack[0]={ItemKind::Wood,640};state.backpack[1]={ItemKind::Stone,320};state.backpack[2]={ItemKind::Scrap,80};}
    initializeAdventureProgress(state,content);
    if(!validate(state,content,error))return nullptr;
    return session;
}
std::unique_ptr<AdventureSession> AdventureSession::restore(const AdventureState& state,const AdventureContent& content,std::string& error) {
    if(!validate(state,content,error))return nullptr;
    auto session=std::unique_ptr<AdventureSession>(new AdventureSession);
    session->state_=state;session->content_=content;return session;
}
std::optional<AdventureSession::PreparedChange> AdventureSession::begin(CommandStamp stamp,std::string& error,bool allowDefeated) const {
    if(!allowDefeated&&!state_.health){refuse(error,"Recover safely before taking another action.");return std::nullopt;}
    if(stamp.caller!=1){refuse(error,"This backpack and home belong to another player.");return std::nullopt;}
    if(stamp.expectedRevision!=state_.revision){refuse(error,"The world changed. Try again.");return std::nullopt;}
    if(!stamp.sequence || stamp.sequence<=state_.lastRequestSequence){refuse(error,"This action was already handled.");return std::nullopt;}
    PreparedChange change;change.candidate_=state_;change.baseRevision_=state_.revision;change.owner_=this;
    if(!increment(change.candidate_.revision)){refuse(error,"Adventure revision capacity reached.");return std::nullopt;}
    change.candidate_.lastRequestSequence=stamp.sequence;error.clear();return change;
}
bool AdventureSession::finish(PreparedChange& change,const CandidateValidator& validator,std::string& error) const {
    if(!validate(change.candidate_,content_,error))return false;
    if(!validator)return refuse(error,"World placement checks are unavailable.");
    if(!validator(state_,change.candidate_,error)) {
        if(error.empty())error="That action is not possible here.";
        return false;
    }
    return true;
}
bool AdventureSession::addPart(PreparedChange& change,PlacePart request,std::string& error) const {
    const auto* definition=buildingDefinition(request.kind);
    if(!definition || !gridInWorld(request.position) || request.yawQuarterTurns>3 || request.paint>0xffffff) {
        refuse(error,"Choose a valid building piece and position.");return false;
    }
    auto& state=change.candidate_;
    size_t count=0;for(const auto& s:state.structures)count+=s.parts.size();
    if(count>=kMaximumParts){refuse(error,"Building limit: 1,024 pieces.");return false;}
    if(content_.freeBuilding&&!request.structure&&!state.structures.empty())request.structure=state.structures.front().id;
    WorldStructure* structure=nullptr;
    if(request.structure) {
        for(auto& s:state.structures)if(s.id==request.structure)structure=&s;
        if(!structure){refuse(error,"The selected structure is missing.");return false;}
    } else {
        if(!definition->terrainAnchor&&!content_.freeBuilding){refuse(error,"Start with a foundation or pier.");return false;}
        if(state.structures.size()>=kMaximumStructures){refuse(error,"Building limit: four structures.");return false;}
        if(!increment(state.lastIssuedId)){refuse(error,"Building identity capacity reached.");return false;}
        state.structures.push_back({state.lastIssuedId,1,state.revision,request.position,{}});structure=&state.structures.back();
    }
    if(!content_.freeBuilding&&!consumeMaterials(state.backpack,definition->cost)){refuse(error,"Not enough supplies for this piece.");return false;}
    if(!increment(state.lastIssuedId)){refuse(error,"Building identity capacity reached.");return false;}
    const WorldPart part{state.lastIssuedId,request.kind,request.position,request.yawQuarterTurns,request.paint};
    structure->parts.push_back(part);structure->revision=state.revision;
    change.changedPart_=part.id;change.changedStructure_=structure->id;state.backpackRevision=state.revision;
    if(definition->furniture!=FurnitureKind::None) {
        if(state.components.size()>=kMaximumComponents){refuse(error,"Furniture limit: 32 useful objects.");return false;}
        if(!increment(state.lastIssuedId)){refuse(error,"Furniture identity capacity reached.");return false;}
        state.components.push_back({state.lastIssuedId,structure->id,part.id,1,state.revision,definition->furniture,{}});
    }
    return true;
}
std::optional<AdventureSession::PreparedChange> AdventureSession::preparePlace(CommandStamp stamp,PlacePart request,const CandidateValidator& validator,std::string& error) const {
    auto change=begin(stamp,error);if(!change)return std::nullopt;
    if(!addPart(*change,request,error) || !finish(*change,validator,error))return std::nullopt;
    return change;
}
std::optional<AdventureSession::PreparedChange> AdventureSession::prepareBlueprint(CommandStamp stamp,
    std::span<const PlacePart> requests,const CandidateValidator& validator,std::string& error) const {
    if(requests.empty() || requests.size()>kMaximumBlueprintParts) {
        refuse(error,"Choose a blueprint with 1 to 64 pieces.");return std::nullopt;
    }
    const auto* first=buildingDefinition(requests.front().kind);
    if(!first || !first->terrainAnchor) {
        refuse(error,"Start the blueprint with a foundation or pier.");return std::nullopt;
    }
    auto change=begin(stamp,error);if(!change)return std::nullopt;
    if(!addPart(*change,requests.front(),error))return std::nullopt;
    const auto structure=change->changedStructure_;
    for(size_t i=1;i<requests.size();++i) {
        auto request=requests[i];
        if(request.structure && request.structure!=structure) {
            refuse(error,"A blueprint must belong to one structure.");return std::nullopt;
        }
        request.structure=structure;
        if(!addPart(*change,request,error))return std::nullopt;
    }
    if(!finish(*change,validator,error))return std::nullopt;
    return change;
}
std::optional<AdventureSession::PreparedChange> AdventureSession::prepareRemove(CommandStamp stamp,uint64_t partId,const CandidateValidator& validator,std::string& error) const {
    auto change=begin(stamp,error);if(!change)return std::nullopt;
    auto& state=change->candidate_;
    const auto* part=findPart(state,partId);
    if(!part){refuse(error,"That building piece is missing.");return std::nullopt;}
    const auto cost=buildingDefinition(part->kind)->cost;
    for(const auto& value:state.components)if(value.part==partId) {
        if(std::any_of(value.slots.begin(),value.slots.end(),[](auto slot){return slot.kind!=ItemKind::None;})) {
            refuse(error,"Empty the chest before removing it.");return std::nullopt;
        }
        if(state.registeredBed==value.id){state.registeredBed=0;state.recovery=content_.town;}
    }
    if(!content_.freeBuilding&&!refundMaterials(state.backpack,cost)){refuse(error,"Make backpack space for the refunded supplies.");return std::nullopt;}
    state.backpackRevision=state.revision;
    std::erase_if(state.components,[&](const auto& c){return c.part==partId;});
    for(auto& structure:state.structures)if(std::erase_if(structure.parts,[&](const auto& p){return p.id==partId;})) {
        structure.revision=state.revision;change->changedStructure_=structure.id;
    }
    std::erase_if(state.structures,[](const auto& s){return s.parts.empty();});change->changedPart_=partId;
    if(!finish(*change,validator,error))return std::nullopt;
    return change;
}
std::optional<AdventureSession::PreparedChange> AdventureSession::prepareRemoveStructure(CommandStamp stamp,
    uint64_t structureId,const CandidateValidator& validator,std::string& error) const {
    auto change=begin(stamp,error);if(!change)return std::nullopt;
    auto& state=change->candidate_;
    const auto structure=std::find_if(state.structures.begin(),state.structures.end(),
        [&](const auto& value){return value.id==structureId;});
    if(structure==state.structures.end() || structure->owner!=stamp.caller) {
        refuse(error,"The selected structure is missing or belongs to another player.");return std::nullopt;
    }
    for(const auto& value:state.components)if(value.structure==structureId) {
        if(std::any_of(value.slots.begin(),value.slots.end(),[](auto slot){return slot.kind!=ItemKind::None;})) {
            refuse(error,"Empty every chest before removing this structure.");return std::nullopt;
        }
        if(state.registeredBed==value.id){state.registeredBed=0;state.recovery=content_.town;}
    }
    // Refund individual bounded costs into the same private backpack. This
    // supports aggregate refunds larger than one item stack without narrowing
    // a structure's total cost or publishing an intermediate partial refund.
    for(const auto& part:structure->parts) {
        const auto* definition=buildingDefinition(part.kind);
        if(!definition || (!content_.freeBuilding&&!refundMaterials(state.backpack,definition->cost))) {
            refuse(error,"Make backpack space for all refunded supplies.");return std::nullopt;
        }
    }
    state.backpackRevision=state.revision;
    std::erase_if(state.components,[&](const auto& value){return value.structure==structureId;});
    state.structures.erase(structure);change->changedStructure_=structureId;
    if(!finish(*change,validator,error))return std::nullopt;
    return change;
}
std::optional<AdventureSession::PreparedChange> AdventureSession::prepareSetDoorOpen(CommandStamp stamp,
    uint64_t componentId,bool open,uint64_t expectedComponentRevision,const CandidateValidator& validator,std::string& error) const {
    auto change=begin(stamp,error);if(!change)return std::nullopt;
    auto& state=change->candidate_;
    const auto door=std::find_if(state.components.begin(),state.components.end(),
        [&](const auto& component){return component.id==componentId;});
    if(door==state.components.end()||door->owner!=stamp.caller||door->kind!=FurnitureKind::Door) {
        refuse(error,"Choose your hinged door.");return std::nullopt;
    }
    if(door->revision!=expectedComponentRevision){refuse(error,"The door changed. Try again.");return std::nullopt;}
    if(door->doorOpen==open){refuse(error,open?"The door is already open.":"The door is already closed.");return std::nullopt;}
    const auto structure=std::find_if(state.structures.begin(),state.structures.end(),
        [&](const auto& value){return value.id==door->structure&&value.owner==stamp.caller;});
    if(structure==state.structures.end()){refuse(error,"The door's structure is unavailable.");return std::nullopt;}
    door->doorOpen=open;door->revision=state.revision;structure->revision=state.revision;
    change->changedPart_=door->part;change->changedStructure_=door->structure;
    if(!finish(*change,validator,error))return std::nullopt;
    return change;
}
std::optional<AdventureSession::PreparedChange> AdventureSession::prepareTransfer(CommandStamp stamp,TransferItems request,const CandidateValidator& validator,std::string& error) const {
    auto change=begin(stamp,error);if(!change)return std::nullopt;
    auto& state=change->candidate_;auto source=container(state,request.source),destination=container(state,request.destination);
    if(request.source==request.destination || !source.revision || !destination.revision || request.sourceSlot>=source.slots.size() || !request.quantity) {
        refuse(error,"Choose two different containers and an item.");return std::nullopt;
    }
    if(*source.revision!=request.sourceRevision || *destination.revision!=request.destinationRevision) {
        refuse(error,"Container contents changed. Try again.");return std::nullopt;
    }
    auto& slot=source.slots[request.sourceSlot];
    if(slot.kind==ItemKind::None || slot.quantity<request.quantity){refuse(error,"That many items are not available.");return std::nullopt;}
    if(!addItems(destination.slots,{slot.kind,request.quantity})){refuse(error,"The destination is full.");return std::nullopt;}
    slot.quantity-=request.quantity;if(!slot.quantity)slot={};
    *source.revision=*destination.revision=state.revision;
    if(!finish(*change,validator,error))return std::nullopt;
    return change;
}
std::optional<AdventureSession::PreparedChange> AdventureSession::prepareCraftHammer(CommandStamp stamp,uint64_t benchId,const CandidateValidator& validator,std::string& error) const {
    auto change=begin(stamp,error);if(!change)return std::nullopt;
    auto& state=change->candidate_;const auto* bench=findComponent(state,benchId);
    if(!bench || bench->kind!=FurnitureKind::Workbench){refuse(error,"Use a placed workbench to craft.");return std::nullopt;}
    // Entire operation uses the private copy: output-space failure never spends ingredients.
    if(!consumeMaterials(state.backpack,{4,0,2})){refuse(error,"Field hammer needs 4 wood and 2 scrap.");return std::nullopt;}
    if(!addItems(state.backpack,{ItemKind::FieldHammer,1})){refuse(error,"Make backpack space for the field hammer.");return std::nullopt;}
    state.backpackRevision=state.revision;
    if(!finish(*change,validator,error))return std::nullopt;
    return change;
}
std::optional<AdventureSession::PreparedChange> AdventureSession::prepareUseBed(CommandStamp stamp,uint64_t bedId,PlayerPose recovery,const CandidateValidator& validator,std::string& error) const {
    auto change=begin(stamp,error);if(!change)return std::nullopt;
    auto& state=change->candidate_;const auto* bed=findComponent(state,bedId);
    if(!bed || bed->kind!=FurnitureKind::Bed || !validPose(recovery)){refuse(error,"Use a sheltered, reachable bed.");return std::nullopt;}
    state.registeredBed=bedId;state.recovery=recovery;state.health=100;
    if(!finish(*change,validator,error))return std::nullopt;
    return change;
}
std::optional<AdventureSession::PreparedChange> AdventureSession::prepareGather(CommandStamp stamp,uint32_t nodeId,const CandidateValidator& validator,std::string& error) const {
    auto change=begin(stamp,error);if(!change)return std::nullopt;
    auto& state=change->candidate_;
    const auto node=std::find_if(content_.resourceNodes.begin(),content_.resourceNodes.end(),[&](const auto& n){return n.id==nodeId;});
    if(node==content_.resourceNodes.end()){refuse(error,"That resource is not part of this world.");return std::nullopt;}
    if(std::binary_search(state.depletedNodes.begin(),state.depletedNodes.end(),nodeId)){refuse(error,"This resource has already been gathered.");return std::nullopt;}
    auto yield=node->yield;if(state.equippedTool.kind==ItemKind::FieldHammer)yield.quantity*=2;
    if(!addItems(state.backpack,yield)){refuse(error,"Make backpack space before gathering.");return std::nullopt;}
    state.depletedNodes.insert(std::lower_bound(state.depletedNodes.begin(),state.depletedNodes.end(),nodeId),nodeId);
    state.backpackRevision=state.revision;
    if(!finish(*change,validator,error))return std::nullopt;
    return change;
}
std::optional<AdventureSession::PreparedChange> AdventureSession::prepareEquipTool(CommandStamp stamp,uint8_t slotIndex,std::string& error) const {
    auto change=begin(stamp,error);if(!change)return std::nullopt;
    auto& state=change->candidate_;
    if(slotIndex>=state.backpack.size() || (state.backpack[slotIndex].kind!=ItemKind::FieldHammer&&state.backpack[slotIndex].kind!=ItemKind::TrailStaff)){refuse(error,"Choose a tool or weapon in your backpack.");return std::nullopt;}
    auto& slot=state.backpack[slotIndex];std::swap(slot,state.equippedTool);state.backpackRevision=state.revision;
    if(!validate(state,content_,error))return std::nullopt;
    return change;
}
std::optional<AdventureSession::PreparedChange> AdventureSession::prepareGreet(CommandStamp stamp,uint8_t npcId,const CandidateValidator& validator,std::string& error) const {
    if(npcId<1 || npcId>3){refuse(error,"Choose a town resident.");return std::nullopt;}
    const auto bit=static_cast<uint8_t>(1u<<(npcId-1u));
    if((state_.metNpcMask&bit)!=0){refuse(error,"You have already met this resident.");return std::nullopt;}
    auto change=begin(stamp,error);if(!change)return std::nullopt;
    change->candidate_.metNpcMask|=bit;
    if(!finish(*change,validator,error))return std::nullopt;
    return change;
}
std::optional<AdventureSession::PreparedChange> AdventureSession::prepareAcceptHomeQuest(CommandStamp stamp,uint8_t npcId,const CandidateValidator& validator,std::string& error) const {
    if(npcId!=1){refuse(error,"Speak to Moss about your home.");return std::nullopt;}
    if(state_.firstHome.phase!=QuestPhase::NotAccepted){refuse(error,"This home quest was already accepted.");return std::nullopt;}
    auto change=begin(stamp,error);if(!change)return std::nullopt;
    change->candidate_.firstHome.phase=QuestPhase::Active;
    change->candidate_.metNpcMask|=uint8_t{1};
    if(!finish(*change,validator,error))return std::nullopt;
    return change;
}
std::optional<AdventureSession::PreparedChange> AdventureSession::prepareCompleteHomeQuest(CommandStamp stamp,uint8_t npcId,const CandidateValidator& validator,std::string& error) const {
    if(npcId!=1){refuse(error,"Return to Moss to finish your home quest.");return std::nullopt;}
    if(state_.firstHome.phase!=QuestPhase::Active){refuse(error,"This home quest is not awaiting completion.");return std::nullopt;}
    const auto* bed=findComponent(state_,state_.registeredBed);
    if(!bed || bed->kind!=FurnitureKind::Bed){refuse(error,"Use a sheltered bed to register your home.");return std::nullopt;}
    const auto hasFurniture=[&](FurnitureKind kind) {
        return std::any_of(state_.components.begin(),state_.components.end(),[&](const auto& value){
            return value.structure==bed->structure && value.kind==kind;
        });
    };
    if(!hasFurniture(FurnitureKind::Chest) || !hasFurniture(FurnitureKind::Workbench)) {
        refuse(error,"Add a chest and workbench to your registered home.");return std::nullopt;
    }
    auto change=begin(stamp,error);if(!change)return std::nullopt;
    auto& state=change->candidate_;
    state.firstHome={QuestPhase::Completed,state.revision};
    // The trusted adapter must also check the actual sheltered bed and NPC
    // range/sight. No carried reward is added, so a full backpack can accept it.
    if(!finish(*change,validator,error))return std::nullopt;
    return change;
}
std::optional<AdventureSession::PreparedChange> AdventureSession::prepareCraftCompass(CommandStamp stamp,uint64_t benchId,const CandidateValidator& validator,std::string& error) const {
    if(!trailCompassRecipeUnlocked(state_.firstHome)){refuse(error,"Moss must teach you the trail compass recipe first.");return std::nullopt;}
    const auto* bench=findComponent(state_,benchId);
    if(!bench || bench->kind!=FurnitureKind::Workbench){refuse(error,"Use a placed workbench to craft.");return std::nullopt;}
    auto change=begin(stamp,error);if(!change)return std::nullopt;
    auto& state=change->candidate_;
    if(!consumeMaterials(state.backpack,{2,0,4})){refuse(error,"Trail compass needs 2 wood and 4 scrap.");return std::nullopt;}
    if(!addItems(state.backpack,{ItemKind::TrailCompass,1})){refuse(error,"Make backpack space for the trail compass.");return std::nullopt;}
    state.backpackRevision=state.revision;
    if(!finish(*change,validator,error))return std::nullopt;
    return change;
}
std::optional<AdventureSession::PreparedChange> AdventureSession::prepareEquipUtility(CommandStamp stamp,uint8_t slotIndex,std::string& error) const {
    auto change=begin(stamp,error);if(!change)return std::nullopt;
    auto& state=change->candidate_;
    if(slotIndex==255) {
        if(state.equippedUtility.kind==ItemKind::None){refuse(error,"No utility is equipped.");return std::nullopt;}
        if(!addItems(state.backpack,state.equippedUtility)){refuse(error,"Make backpack space to put away the compass.");return std::nullopt;}
        state.equippedUtility={};
    } else {
        if(slotIndex>=state.backpack.size() || state.backpack[slotIndex].kind!=ItemKind::TrailCompass) {
            refuse(error,"Choose a trail compass in your backpack.");return std::nullopt;
        }
        std::swap(state.backpack[slotIndex],state.equippedUtility);
    }
    state.backpackRevision=state.revision;
    if(!validate(state,content_,error))return std::nullopt;
    return change;
}
std::optional<AdventureSession::PreparedChange> AdventureSession::prepareCraftStaff(CommandStamp stamp,uint64_t benchId,const CandidateValidator& validator,std::string& error) const {
    auto change=begin(stamp,error);if(!change)return std::nullopt;
    auto& state=change->candidate_;const auto* bench=findComponent(state,benchId);
    if(!bench||bench->kind!=FurnitureKind::Workbench){refuse(error,"Use a placed workbench to craft.");return std::nullopt;}
    if(!consumeMaterials(state.backpack,{4,0,4})){refuse(error,"Trail staff needs 4 wood and 4 scrap.");return std::nullopt;}
    if(!addItems(state.backpack,{ItemKind::TrailStaff,1})){refuse(error,"Make backpack space for the trail staff.");return std::nullopt;}
    state.backpackRevision=state.revision;
    if(!finish(*change,validator,error))return std::nullopt;
    return change;
}
std::optional<AdventureSession::PreparedChange> AdventureSession::prepareCombatTick(CommandStamp stamp,const CombatTick& request,const CandidateValidator& validator,std::string& error) const {
    if(state_.combat.tick>=UINT64_MAX-kMaximumCombatDeadlineTicks||request.tick!=state_.combat.tick+1||request.health>state_.health
        ||(!state_.health&&(request.health||request.player!=state_.player))) {
        refuse(error,"Combat must advance one accepted tick without healing or moving a defeated player.");return std::nullopt;
    }
    const auto& oldPlayer=state_.combat.player;const auto& player=request.playerCombat;
    if(player.attackSerial<oldPlayer.attackSerial||player.attackSerial-oldPlayer.attackSerial>1
        ||(player.attackSerial!=oldPlayer.attackSerial&&(!state_.health||state_.equippedTool.kind!=ItemKind::TrailStaff))) {
        refuse(error,"The player attack sequence is invalid.");return std::nullopt;
    }
    auto change=begin(stamp,error,true);if(!change)return std::nullopt;
    auto& state=change->candidate_;state.player=request.player;state.health=request.health;
    state.combat.tick=request.tick;state.combat.player=player;
    for(size_t i=0;i<request.enemies.size();++i) {
        const auto& before=state_.combat.encounters[i];const auto& old=before.checkpoint;const auto& next=request.enemies[i];
        if(next.encounterId!=old.encounterId||next.generation!=old.generation
            ||(old.positioned&&!next.positioned)||(before.deathRevision&&next!=old)
            ||next.attackSerial<old.attackSerial||next.attackSerial-old.attackSerial>1
            ||next.lastPlayerAttackSerial<old.lastPlayerAttackSerial
            ||(next.health>old.health&&(old.phase!=EnemyPhase::Return
                ||(next.phase!=EnemyPhase::Return&&next.phase!=EnemyPhase::Idle)
                ||next.health!=content_.encounters[i].maximumHealth))) {
            refuse(error,"The encounter generation or combat sequence changed unexpectedly.");return std::nullopt;
        }
        auto& record=state.combat.encounters[i];record.checkpoint=next;
        if(old.health&&!next.health)record.deathRevision=state.revision;
    }
    if(!finish(*change,validator,error))return std::nullopt;
    return change;
}
std::optional<AdventureSession::PreparedChange> AdventureSession::prepareClaimEncounterLoot(CommandStamp stamp,uint8_t id,uint32_t generation,const CandidateValidator& validator,std::string& error) const {
    if(!id||id>content_.encounters.size()){refuse(error,"Choose an installed encounter's loot.");return std::nullopt;}
    const auto& definition=content_.encounters[id-1u];const auto& old=state_.combat.encounters[id-1u];
    if(definition.id!=id||generation!=definition.generation||!old.deathRevision||old.lootClaimRevision) {
        refuse(error,"This encounter has no unclaimed loot for that generation.");return std::nullopt;
    }
    auto change=begin(stamp,error);if(!change)return std::nullopt;
    auto& state=change->candidate_;
    if(!addItems(state.backpack,definition.loot)){refuse(error,"Make backpack space before claiming the loot.");return std::nullopt;}
    state.backpackRevision=state.revision;state.combat.encounters[id-1u].lootClaimRevision=state.revision;
    if(!finish(*change,validator,error))return std::nullopt;
    return change;
}
std::optional<AdventureSession::PreparedChange> AdventureSession::prepareRecover(CommandStamp stamp,PlayerPose recovery,const CandidateValidator& validator,std::string& error) const {
    if(!validPose(recovery)||(recovery!=content_.town&&!state_.registeredBed)) {
        refuse(error,"Choose the current safe home or town recovery point.");return std::nullopt;
    }
    auto change=begin(stamp,error,true);if(!change)return std::nullopt;
    auto& state=change->candidate_;state.player=recovery;state.health=100;
    state.combat.player.attackImpactTick=0;state.combat.player.dodgeUntilTick=0;
    state.combat.player.invulnerableUntilTick=0;state.combat.player.dodgeDirectionX=0;state.combat.player.dodgeDirectionZ=0;
    if(!finish(*change,validator,error))return std::nullopt;
    return change;
}
std::optional<AdventureSession::PreparedChange> AdventureSession::prepareAcceptTrailQuest(CommandStamp stamp,uint8_t id,uint8_t npc,const CandidateValidator& validator,std::string& error) const {
    const auto* definition=trailQuestDefinition(id);
    if(!content_.enableTrailProgress||!definition||definition->npcId!=npc) {
        refuse(error,"Speak to the resident who offers this trail quest.");return std::nullopt;
    }
    if(state_.trail.quests[id-2u].phase!=QuestPhase::NotAccepted||!trailQuestPrerequisite(state_,id)) {
        refuse(error,"Finish the preceding quest before accepting this one.");return std::nullopt;
    }
    auto change=begin(stamp,error);if(!change)return std::nullopt;
    change->candidate_.trail.quests[id-2u].phase=QuestPhase::Active;
    change->candidate_.metNpcMask|=static_cast<uint8_t>(1u<<(npc-1u));
    if(!finish(*change,validator,error))return std::nullopt;
    return change;
}
std::optional<AdventureSession::PreparedChange> AdventureSession::prepareCompleteTrailQuest(CommandStamp stamp,uint8_t id,uint8_t npc,const CandidateValidator& validator,std::string& error) const {
    const auto* definition=trailQuestDefinition(id);
    if(!content_.enableTrailProgress||!definition||definition->npcId!=npc||id==4) {
        refuse(error,id==4?"Fit the core at the relay to finish this quest.":"Return to the resident who offered this quest.");return std::nullopt;
    }
    if(state_.trail.quests[id-2u].phase!=QuestPhase::Active||!trailQuestPrerequisite(state_,id)||!trailQuestReady(state_,id)) {
        refuse(error,"Complete this quest's objective before turning it in.");return std::nullopt;
    }
    auto change=begin(stamp,error);if(!change)return std::nullopt;
    change->candidate_.trail.quests[id-2u]={QuestPhase::Completed,change->candidate_.revision};
    if(!finish(*change,validator,error))return std::nullopt;
    return change;
}
std::optional<AdventureSession::PreparedChange> AdventureSession::prepareDiscover(CommandStamp stamp,uint8_t id,const CandidateValidator& validator,std::string& error) const {
    if(!content_.enableTrailProgress||!id||id>content_.discoveries.size()||content_.discoveries[id-1u].id!=id) {
        refuse(error,"Choose an installed discovery site.");return std::nullopt;
    }
    if(state_.trail.discoveries[id-1u].discoveredRevision){refuse(error,"This site is already discovered.");return std::nullopt;}
    auto change=begin(stamp,error);if(!change)return std::nullopt;
    change->candidate_.trail.discoveries[id-1u].discoveredRevision=change->candidate_.revision;
    if(!finish(*change,validator,error))return std::nullopt;
    return change;
}
std::optional<AdventureSession::PreparedChange> AdventureSession::prepareClaimDiscovery(CommandStamp stamp,uint8_t id,const CandidateValidator& validator,std::string& error) const {
    if(!content_.enableTrailProgress||!id||id>content_.discoveries.size()||content_.discoveries[id-1u].id!=id) {
        refuse(error,"Choose an installed discovery reward.");return std::nullopt;
    }
    const auto& record=state_.trail.discoveries[id-1u];
    if(!record.discoveredRevision||record.rewardClaimRevision){refuse(error,"This discovery has no unclaimed reward.");return std::nullopt;}
    auto change=begin(stamp,error);if(!change)return std::nullopt;
    auto& state=change->candidate_;
    if(!addItems(state.backpack,content_.discoveries[id-1u].reward)){refuse(error,"Make backpack space for the discovery reward.");return std::nullopt;}
    state.backpackRevision=state.revision;state.trail.discoveries[id-1u].rewardClaimRevision=state.revision;
    if(!finish(*change,validator,error))return std::nullopt;
    return change;
}
std::optional<AdventureSession::PreparedChange> AdventureSession::prepareActivateRelay(CommandStamp stamp,const CandidateValidator& validator,std::string& error) const {
    if(!content_.enableTrailProgress||state_.trail.quests[2].phase!=QuestPhase::Active
        ||!trailQuestPrerequisite(state_,4)||state_.trail.relayActivationRevision) {
        refuse(error,"Accept Lumen's relay restoration quest first.");return std::nullopt;
    }
    auto change=begin(stamp,error);if(!change)return std::nullopt;
    auto& state=change->candidate_;
    if(!takeItems(state.backpack,{ItemKind::RelayCore,1})){refuse(error,"Carry the recovered relay core in your backpack.");return std::nullopt;}
    state.backpackRevision=state.revision;state.trail.relayActivationRevision=state.revision;
    state.trail.quests[2]={QuestPhase::Completed,state.revision};
    // The trusted adapter must check actual relay reach/sight and a currently
    // usable nearby owned bed/chest/workbench home on this complete candidate.
    if(!finish(*change,validator,error))return std::nullopt;
    return change;
}
std::optional<AdventureSession::PreparedChange> AdventureSession::prepareBuildRecipe(CommandStamp stamp,BlueprintKind kind,GridPosition origin,uint8_t yaw,const CandidateValidator& validator,std::string& error) const {
    if(!buildingBlueprintDefinition(kind)){refuse(error,"Choose an installed building recipe.");return std::nullopt;}
    if(kind==BlueprintKind::WideStoneStep&&(!content_.enableTrailProgress||!wideStoneStepRecipeUnlocked(state_))) {
        refuse(error,"Finish The Surveyor's Notes with Moss to learn the Wide stone step.");return std::nullopt;
    }
    const auto layout=buildingBlueprintLayout(kind,origin,yaw,error);if(layout.empty())return std::nullopt;
    return prepareBlueprint(stamp,layout,validator,error);
}
bool AdventureSession::commit(PreparedChange&& change,std::string& error) {
    if(change.owner_!=this || change.baseRevision_!=state_.revision || change.candidate_.world!=state_.world ||
        change.candidate_.epoch!=state_.epoch || change.candidate_.revision!=state_.revision+1 ||
        change.candidate_.lastRequestSequence<=state_.lastRequestSequence)
        return refuse(error,"The world changed before this action was ready.");
    // Candidate can only originate from a checked preparation above. Moving its
    // vectors is noexcept with their standard allocator; no partial publication.
    state_=std::move(change.candidate_);change.owner_=nullptr;error.clear();return true;
}
bool AdventureSession::updatePlayer(PlayerPose pose,uint16_t health,std::string& error) {
    if(!validPose(pose) || health!=state_.health||(!health&&pose!=state_.player))return refuse(error,"Locomotion cannot change health or move a defeated player.");
    if(pose==state_.player && health==state_.health){error.clear();return true;}
    if(state_.revision==std::numeric_limits<uint64_t>::max())return refuse(error,"Adventure revision capacity reached.");
    ++state_.revision;state_.player=pose;state_.health=health;error.clear();return true;
}
} // namespace voxy::game::adventure
