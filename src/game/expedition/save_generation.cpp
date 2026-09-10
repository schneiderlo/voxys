#include "game/expedition/save_generation.hpp"
#include "core/sha256.hpp"
#include <algorithm>
#include <new>
#include <string_view>

namespace voxy::game::expedition {
namespace {
void appendInteger(std::vector<std::byte>& bytes,uint64_t value,size_t width) {
    for(size_t i=0;i<width;++i)bytes.push_back(static_cast<std::byte>((value>>(8*i))&255));
}
uint64_t readInteger(std::span<const std::byte> bytes,size_t offset,size_t width) {
    uint64_t value=0;
    for(size_t i=0;i<width;++i)value|=uint64_t(std::to_integer<uint8_t>(bytes[offset+i]))<<(8*i);
    return value;
}
}
bool encodeStoredGeneration(construction::WorldNamespace world,uint64_t generation,std::span<const std::byte> payload,
    std::vector<std::byte>& output,StoreIssue& issue) {
    issue={};
    if(!construction::isValid(world)||generation==0||payload.empty()){issue.error=StoreError::InvalidData;return false;}
    if(payload.size()>kMaximumStoredPayloadBytes){issue.error=StoreError::Capacity;return false;}
    try {
        std::vector<std::byte> bytes;bytes.reserve(payload.size()+kStoredGenerationEnvelopeBytes);
        for(char c:std::string_view("SVSG"))bytes.push_back(static_cast<std::byte>(c));
        appendInteger(bytes,1,4);for(auto b:world.bytes)bytes.push_back(static_cast<std::byte>(b));
        appendInteger(bytes,generation,8);appendInteger(bytes,payload.size(),8);
        bytes.insert(bytes.end(),payload.begin(),payload.end());
        const auto hash=core::sha256(bytes);bytes.insert(bytes.end(),hash.bytes.begin(),hash.bytes.end());
        output=std::move(bytes);return true;
    }catch(const std::bad_alloc&){issue.error=StoreError::Capacity;return false;}
}
bool decodeStoredGeneration(std::span<const std::byte> bytes,construction::WorldNamespace world,StoredGeneration& output,StoreIssue& issue) {
    issue={};
    if(bytes.size()>kMaximumStoredPayloadBytes+kStoredGenerationEnvelopeBytes){issue.error=StoreError::Capacity;return false;}
    if(!construction::isValid(world)||bytes.size()<=kStoredGenerationEnvelopeBytes){issue.error=StoreError::InvalidData;return false;}
    const std::array<std::byte,4> magic{std::byte{'S'},std::byte{'V'},std::byte{'S'},std::byte{'G'}};
    if(!std::equal(magic.begin(),magic.end(),bytes.begin())){issue.error=StoreError::InvalidData;return false;}
    if(readInteger(bytes,4,4)!=1){issue.error=StoreError::UnsupportedSchema;return false;}
    const auto hash=core::sha256(bytes.first(bytes.size()-32));
    if(!std::equal(hash.bytes.begin(),hash.bytes.end(),bytes.end()-32)
        ||!std::equal(world.bytes.begin(),world.bytes.end(),bytes.begin()+8,[](auto a,auto b){return a==std::to_integer<uint8_t>(b);})
        ||readInteger(bytes,24,8)==0||readInteger(bytes,32,8)!=bytes.size()-kStoredGenerationEnvelopeBytes){issue.error=StoreError::InvalidData;return false;}
    try {
        StoredGeneration value;value.generation=readInteger(bytes,24,8);value.payload.assign(bytes.begin()+40,bytes.end()-32);
        output=std::move(value);return true;
    }catch(const std::bad_alloc&){issue.error=StoreError::Capacity;return false;}
}
} // namespace voxy::game::expedition
