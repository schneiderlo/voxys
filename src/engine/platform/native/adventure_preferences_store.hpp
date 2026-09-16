#pragma once

#include "game/adventure/adventure_preferences.hpp"
#include <filesystem>

namespace voxy::platform {
enum class AdventurePreferencesStatus { Missing,Loaded,Saved,Invalid,Unavailable,Uncertain,Unsupported };
// Optional controls/UI sidecar. A failed read leaves the caller's active
// preferences unchanged. Linux reads at most the core's 4096-byte allowance
// from one regular, non-symlink file and checks for changes during the read.
// The caller chooses the absolute path; this module never opens a world save.
[[nodiscard]] AdventurePreferencesStatus loadNativeAdventurePreferences(const std::filesystem::path&,
    game::adventure::AdventurePreferences&,std::string& message);
// Creates missing parent directories, writes an exclusive private temporary
// file, flushes it, atomically renames it, then flushes the parent directory.
// Uncertain means replacement happened but directory durability was not
// confirmed. It never revokes applied settings; the caller can retry saving.
// Concurrent applications use the last completed rename, as an optional UI
// sidecar rather than a world transaction. Other platforms return Unsupported.
[[nodiscard]] AdventurePreferencesStatus saveNativeAdventurePreferences(const std::filesystem::path&,
    const game::adventure::AdventurePreferences&,std::string& message);
} // namespace voxy::platform
