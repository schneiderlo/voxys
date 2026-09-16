#include "game/adventure/adventure_save.hpp"

#include <bit>
#include <cmath>
#include <cstring>
#include <type_traits>

namespace voxy::game::adventure {
namespace {
constexpr std::array<std::byte,8> magic{std::byte{'V'},std::byte{'X'},std::byte{'A'},std::byte{'D'},std::byte{'H'},std::byte{'O'},std::byte{'M'},std::byte{'E'}};
template<bool Reading> struct Archive {
    std::span<const std::byte> input;
    std::vector<std::byte> output;
    size_t position=0;
    bool valid=true;
    void bits(uint64_t& value,size_t width) {
        if(!valid)return;
        if constexpr(Reading) {
            if(position>input.size() || width>input.size()-position){valid=false;return;}
            value=0;
            for(size_t i=0;i<width;++i)value|=uint64_t(std::to_integer<uint8_t>(input[position++]))<<(8*i);
        } else {
            if(position>kMaximumAdventureSaveBytes-32 || width>kMaximumAdventureSaveBytes-32-position){valid=false;return;}
            for(size_t i=0;i<width;++i)output.push_back(std::byte((value>>(8*i))&255));
            position+=width;
        }
    }
    template<class T> void value(T& item) {
        if constexpr(std::is_same_v<T,double>) {
            uint64_t data=std::bit_cast<uint64_t>(item==0?0.0:item);bits(data,8);
            if constexpr(Reading)item=std::bit_cast<double>(data);
            if(!std::isfinite(item) || data==0x8000000000000000ull)valid=false;
        } else if constexpr(std::is_enum_v<T>) {
            uint8_t byte=static_cast<uint8_t>(item);value(byte);
            if constexpr(Reading)item=static_cast<T>(byte);
        } else {
            using Unsigned=std::make_unsigned_t<T>;
            uint64_t data=std::bit_cast<Unsigned>(item);bits(data,sizeof(T));
            if constexpr(Reading)item=std::bit_cast<T>(static_cast<Unsigned>(data));
        }
    }
    template<class... T> void fields(T&... values){(value(values),...);}
    void pose(PlayerPose& p){fields(p.x,p.y,p.z,p.yaw);}
    void grid(GridPosition& p){fields(p.x,p.y,p.z);}
    void stack(ItemStack& s){fields(s.kind,s.quantity);}
    void boolean(bool& item) {
        uint8_t data=item?1:0;value(data);if(data>1)valid=false;
        if constexpr(Reading)item=data==1;
    }
    template<class T,class F>void vector(std::vector<T>& values,size_t maximum,F item) {
        uint16_t count=static_cast<uint16_t>(values.size());
        if constexpr(!Reading)if(values.size()>maximum){valid=false;return;}
        value(count);if(!valid || count>maximum){valid=false;return;}
        if constexpr(Reading)values.resize(count);
        for(auto& element:values){if(!valid)return;item(element);}
    }
    void state(AdventureState& s,uint32_t version) {
        for(auto& byte:s.world.bytes)value(byte);
        for(auto& byte:s.content.bytes){uint8_t v=std::to_integer<uint8_t>(byte);value(v);if constexpr(Reading)byte=std::byte(v);}
        fields(s.epoch,s.revision,s.lastRequestSequence,s.lastIssuedId);
        uint8_t starter=s.starterGranted?1:0;value(starter);if(starter>1)valid=false;
        if constexpr(Reading)s.starterGranted=starter==1;
        pose(s.player);value(s.health);value(s.backpackRevision);
        for(auto& item:s.backpack)stack(item);
        stack(s.equippedTool);value(s.registeredBed);pose(s.recovery);
        size_t partCount=0;
        vector(s.structures,kMaximumStructures,[&](auto& structure){
            fields(structure.id,structure.owner,structure.revision);grid(structure.origin);
            vector(structure.parts,kMaximumParts,[&](auto& part){
                if(++partCount>kMaximumParts){valid=false;return;}
                fields(part.id,part.kind);grid(part.position);fields(part.yawQuarterTurns,part.paint);
            });
        });
        vector(s.components,kMaximumComponents,[&](auto& component){
            fields(component.id,component.structure,component.part,component.owner,component.revision,component.kind);
            for(auto& item:component.slots)stack(item);
            if(version>=6)boolean(component.doorOpen);
        });
        vector(s.depletedNodes,kMaximumResourceNodes,[&](auto& node){value(node);});
        if(version>=2) {
            fields(s.metNpcMask,s.firstHome.phase,s.firstHome.rewardRevision);
            stack(s.equippedUtility);
        }
        if(version>=3) {
            value(s.combat.tick);auto& player=s.combat.player;
            fields(player.attackSerial,player.attackReadyTick,player.attackImpactTick,player.dodgeReadyTick,
                player.dodgeUntilTick,player.invulnerableUntilTick,player.dodgeDirectionX,player.dodgeDirectionZ);
            for(auto& record:s.combat.encounters) {
                auto& enemy=record.checkpoint;fields(enemy.encounterId,enemy.generation);boolean(enemy.positioned);pose(enemy.pose);
                fields(enemy.health,enemy.phase,enemy.phaseTicks,enemy.attackSerial,enemy.lastPlayerAttackSerial,
                    record.deathRevision,record.lootClaimRevision);
            }
            for(auto& discovery:s.trail.discoveries)fields(discovery.discoveredRevision,discovery.rewardClaimRevision);
            for(auto& quest:s.trail.quests)fields(quest.phase,quest.rewardRevision);
            value(s.trail.relayActivationRevision);
        }
    }
};
// Each old schema retains its own enum and stack bounds after new IDs exist.
bool validLegacyIds(const AdventureState& state,uint32_t version) {
    const auto oldStack=[version](ItemStack value){
        const auto id=static_cast<uint8_t>(value.kind);
        return id==0?value.quantity==0:id<=(version==1?4:version==2?5:7)&&value.quantity>0&&value.quantity<=(id<=3?999:1);
    };
    if(!oldStack(state.equippedTool) || !std::all_of(state.backpack.begin(),state.backpack.end(),oldStack))return false;
    if(state.equippedTool.kind!=ItemKind::None&&state.equippedTool.kind!=ItemKind::FieldHammer
        &&(version<3||state.equippedTool.kind!=ItemKind::TrailStaff))return false;
    if(!oldStack(state.equippedUtility)||(state.equippedUtility.kind!=ItemKind::None&&state.equippedUtility.kind!=ItemKind::TrailCompass))return false;
    for(const auto& component:state.components)
        if(static_cast<uint8_t>(component.kind)<1||static_cast<uint8_t>(component.kind)>3||component.doorOpen
            ||!std::all_of(component.slots.begin(),component.slots.end(),oldStack))return false;
    for(const auto& structure:state.structures)for(const auto& part:structure.parts)
        if(static_cast<uint8_t>(part.kind)<1 || static_cast<uint8_t>(part.kind)>14)return false;
    if(std::any_of(state.depletedNodes.begin(),state.depletedNodes.end(),[version](uint32_t id){return id>(version<4?18u:21u);}))return false;
    return true;
}
}
std::array<std::optional<core::Sha256Digest>,5> installedAdventureCompatibilityIdentities() {
    constexpr std::array<std::string_view,5> hashes{
        "1471b647cb7e8a02c06ae5bba53c7f9f835d432d6da598e8338825b845e448b8",
        "11383554b461917c507d21b20adad88c24989fc3e1272baec5e6d6bba840c431",
        "4eeb1c84d8b678093f1012b1ff4bc9edcb4ec21ec9f8d7377383641f2304aa9a",
        "0ca6be8bbc4285f4541aadc74d8cb7aeefb4b2ed078f45f86e7b0a2173ab1e93",
        "b989f53fd82527a42f3e60582f9bb0cdcafced9cfea94b53074cccf37eca290c"};
    constexpr std::string_view digits="0123456789abcdef";
    std::array<std::optional<core::Sha256Digest>,5> result;
    for(size_t i=0;i<result.size();++i) {
        core::Sha256Digest digest;
        for(size_t b=0;b<digest.bytes.size();++b)digest.bytes[b]=std::byte(digits.find(hashes[i][b*2])*16+digits.find(hashes[i][b*2+1]));
        result[i]=digest;
    }
    return result;
}
std::string adventureDoorContentFingerprint() {
    return "adventure-hinged-door-r01:schema6-component-door-open-bool:piece15-furniture4-cost6wood2scrap:"
        "defaultclosed-explicitdesiredstate-observedcomponentrevision:32sharedcomponents:"
        "leaf-grid-closed-x-32,32-y2,110-z-2,2:hinge-32,2,0:open-negative90Y-x-34,-30-y2,110-z0,64:"
        "discrete-endpoints-full-swing-clearance-no-leaf-construction-support:catalog-"+std::string(buildingCatalogFingerprint());
}
bool AdventureSaveCodec::encode(const AdventureState& state,const AdventureContent& content,std::vector<std::byte>& output,std::string& error) {
    if(!AdventureSession::validate(state,content,error))return false;
    Archive<false> archive;archive.output.reserve(65536);
    for(auto byte:magic){uint8_t v=std::to_integer<uint8_t>(byte);archive.value(v);}
    uint32_t version=kAdventureSaveSchema;archive.value(version);
    auto copy=state;archive.state(copy,version);
    if(!archive.valid){error="The adventure save exceeds its size limit.";return false;}
    const auto digest=core::sha256(archive.output);
    archive.output.insert(archive.output.end(),digest.bytes.begin(),digest.bytes.end());
    output=std::move(archive.output);error.clear();return true;
}
bool AdventureSaveCodec::decode(std::span<const std::byte> bytes,construction::WorldNamespace expectedWorld,const AdventureContent& content,AdventureState& output,std::string& error,AdventureSaveLoadMetadata* metadata) {
    if(bytes.size()<magic.size()+4+32 || bytes.size()>kMaximumAdventureSaveBytes){error="The adventure save size is invalid.";return false;}
    if(!std::equal(magic.begin(),magic.end(),bytes.begin())){error="This is not an adventure home save.";return false;}
    const auto payload=bytes.first(bytes.size()-32);const auto digest=core::sha256(payload);
    if(!std::equal(digest.bytes.begin(),digest.bytes.end(),bytes.end()-32)){error="The adventure save is damaged.";return false;}
    Archive<true> archive;archive.input=payload;archive.position=magic.size();
    uint32_t version=0;archive.value(version);
    if(version!=1 && version!=2 && version!=3 && version!=4 && version!=5 && version!=kAdventureSaveSchema){error="This adventure save version is not supported.";return false;}
    AdventureState candidate;archive.state(candidate,version);
    if(!archive.valid || archive.position!=payload.size()){error="The adventure save encoding is invalid.";return false;}
    if(candidate.world!=expectedWorld){error="This adventure save belongs to a different world.";return false;}
    if(version<kAdventureSaveSchema) {
        const auto installed=content.compatibilityIdentities[version-1].has_value()?content.compatibilityIdentities[version-1]
            :version==1?content.legacyIdentity:std::nullopt;
        if(!installed || candidate.content!=*installed) {
            error="This older adventure does not match the installed migration.";return false;
        }
        if(!validLegacyIds(candidate,version)){error="The older adventure contains unsupported item or building IDs.";return false;}
        auto legacy=content;legacy.identity=*installed;legacy.legacyIdentity.reset();legacy.compatibilityIdentities={};
        if(version<4) {
            legacy.enableTrailProgress=false;legacy.discoveries={};
            std::erase_if(legacy.resourceNodes,[](const auto& node){return node.id>18;});
        } else if(version==4&&candidate.trail.quests[3]!=FirstHomeProgress{}) {
            error="Schema4 cannot contain survey quest progress.";return false;
        }
        // The real schema3 combat/loot checkpoints already exist. Only schemas
        //1/2 receive new dormant defaults; migration never resets live combat.
        if(version<3)initializeAdventureProgress(candidate,legacy);
        if(!AdventureSession::validate(candidate,legacy,error))return false;
        candidate.content=content.identity;
    }
    if(!AdventureSession::validate(candidate,content,error))return false;
    output=std::move(candidate);
    if(metadata)*metadata={version,version<kAdventureSaveSchema};
    error.clear();return true;
}
} // namespace voxy::game::adventure
