#pragma once
#include "game/construction/construction_types.hpp"
#include <span>
#include <vector>

namespace voxy::game::expedition {
inline constexpr size_t kMaximumStoredPayloadBytes=16*1024*1024;
inline constexpr size_t kStoredGenerationEnvelopeBytes=72;
struct StoredGeneration {
    uint64_t generation=0; // Zero is an empty slot, never an encoded generation.
    std::vector<std::byte> payload;
    bool needsRepair=false; // Native replicas differed; not part of the envelope.
};
enum class StoreError:uint8_t { None,InvalidPath,Busy,InvalidData,UnsupportedSchema,Capacity,Conflict,Io,NoSpace,Permission,RecoveryRequired,UnsupportedPlatform };
struct StoreIssue {
    StoreError error=StoreError::None;
    int systemError=0;
    bool publicationMayHaveHappened=false;
    [[nodiscard]] explicit operator bool()const noexcept{return error!=StoreError::None;}
};
// SVSG v1: magic(4), version u32, world(16), generation u64, size u64,
// payload, SHA-256(32). This outer integrity envelope grants no game authority.
[[nodiscard]] bool encodeStoredGeneration(construction::WorldNamespace,uint64_t,std::span<const std::byte>,std::vector<std::byte>&,StoreIssue&);
[[nodiscard]] bool decodeStoredGeneration(std::span<const std::byte>,construction::WorldNamespace,StoredGeneration&,StoreIssue&);
} // namespace voxy::game::expedition
