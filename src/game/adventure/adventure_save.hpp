#pragma once
#include "game/adventure/adventure_session.hpp"

namespace voxy::game::adventure {
inline constexpr uint32_t kAdventureSaveSchema=6;
inline constexpr size_t kMaximumAdventureSaveBytes=1024*1024;
inline constexpr size_t kAdventureSchema3ExtensionBytes=298;
// Frozen schema 1–5 content identities, independent of later catalogs.
[[nodiscard]] std::array<std::optional<core::Sha256Digest>,5> installedAdventureCompatibilityIdentities();
// Hash after the frozen schema5 identity. Includes the current full catalog
// digest plus the immutable first hinged-door geometry and transition recipe.
[[nodiscard]] std::string adventureDoorContentFingerprint();
struct AdventureSaveLoadMetadata {
    uint32_t sourceSchema=0;
    bool migrated=false;
    bool operator==(const AdventureSaveLoadMetadata&) const = default;
};
// Canonical little-endian whole-world image, distinct from every Cove schema.
// SHA-256 detects accidental damage; trusted content and expected world come
// from the host, never from arbitrary save payload definitions. Both functions
// preserve output on refusal. Decoding does not acknowledge durable storage.
class AdventureSaveCodec {
public:
    [[nodiscard]] static bool encode(const AdventureState&,const AdventureContent&,
        std::vector<std::byte>& output,std::string& error);
    [[nodiscard]] static bool decode(std::span<const std::byte>,construction::WorldNamespace expectedWorld,
        const AdventureContent&,AdventureState& output,std::string& error,
        AdventureSaveLoadMetadata* metadata=nullptr);
};
} // namespace voxy::game::adventure
