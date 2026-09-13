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
    template<class T,class F>void vector(std::vector<T>& values,size_t maximum,F item) {
        uint16_t count=static_cast<uint16_t>(values.size());
        if constexpr(!Reading)if(values.size()>maximum){valid=false;return;}
        value(count);if(!valid || count>maximum){valid=false;return;}
        if constexpr(Reading)values.resize(count);
        for(auto& element:values){if(!valid)return;item(element);}
    }
    void state(AdventureState& s) {
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
        });
        vector(s.depletedNodes,kMaximumResourceNodes,[&](auto& node){value(node);});
    }
};
}
bool AdventureSaveCodec::encode(const AdventureState& state,const AdventureContent& content,std::vector<std::byte>& output,std::string& error) {
    if(!AdventureSession::validate(state,content,error))return false;
    Archive<false> archive;archive.output.reserve(65536);
    for(auto byte:magic){uint8_t v=std::to_integer<uint8_t>(byte);archive.value(v);}
    uint32_t version=kAdventureSaveSchema;archive.value(version);
    auto copy=state;archive.state(copy);
    if(!archive.valid){error="The adventure save exceeds its size limit.";return false;}
    const auto digest=core::sha256(archive.output);
    archive.output.insert(archive.output.end(),digest.bytes.begin(),digest.bytes.end());
    output=std::move(archive.output);error.clear();return true;
}
bool AdventureSaveCodec::decode(std::span<const std::byte> bytes,construction::WorldNamespace expectedWorld,const AdventureContent& content,AdventureState& output,std::string& error) {
    if(bytes.size()<magic.size()+4+32 || bytes.size()>kMaximumAdventureSaveBytes){error="The adventure save size is invalid.";return false;}
    if(!std::equal(magic.begin(),magic.end(),bytes.begin())){error="This is not an adventure home save.";return false;}
    const auto payload=bytes.first(bytes.size()-32);const auto digest=core::sha256(payload);
    if(!std::equal(digest.bytes.begin(),digest.bytes.end(),bytes.end()-32)){error="The adventure save is damaged.";return false;}
    Archive<true> archive;archive.input=payload;archive.position=magic.size();
    uint32_t version=0;archive.value(version);
    if(version!=kAdventureSaveSchema){error="This adventure save version is not supported.";return false;}
    AdventureState candidate;archive.state(candidate);
    if(!archive.valid || archive.position!=payload.size()){error="The adventure save encoding is invalid.";return false;}
    if(candidate.world!=expectedWorld){error="This adventure save belongs to a different world.";return false;}
    if(!AdventureSession::validate(candidate,content,error))return false;
    output=std::move(candidate);error.clear();return true;
}
} // namespace voxy::game::adventure
