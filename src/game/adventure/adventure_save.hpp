#pragma once
#include "game/adventure/adventure_session.hpp"

namespace voxy::game::adventure {
inline constexpr uint32_t kAdventureSaveSchema=1;
inline constexpr size_t kMaximumAdventureSaveBytes=1024*1024;
// Canonical little-endian whole-world image, distinct from every Cove schema.
// SHA-256 detects accidental damage; trusted content and expected world come
// from the host, never from arbitrary save payload definitions. Both functions
// preserve output on refusal. Decoding does not acknowledge durable storage.
class AdventureSaveCodec {
public:
    [[nodiscard]] static bool encode(const AdventureState&,const AdventureContent&,
        std::vector<std::byte>& output,std::string& error);
    [[nodiscard]] static bool decode(std::span<const std::byte>,construction::WorldNamespace expectedWorld,
        const AdventureContent&,AdventureState& output,std::string& error);
};
} // namespace voxy::game::adventure
