#pragma once
#include "game/expedition/cove_input_preferences.hpp"
#include <filesystem>

namespace voxy::platform {
enum class CovePreferencesStatus { Missing, Loaded, Saved, Invalid, Unavailable, Uncertain, Unsupported };
// Optional UI sidecar only. These statuses never revoke an expedition store.
// A bounded synchronous read at startup/write at explicit Apply is intentional:
// <=32KiB, no simulation callbacks, no authoritative generation/lock identity.
// Concurrent applications use last completed rename; this is not a world save.
[[nodiscard]] CovePreferencesStatus loadNativeCovePreferences(const std::filesystem::path&,
    game::expedition::CoveInputPreferences&,std::string& message);
// Creates the parent directory if absent. Exact serialization is flushed to an
// exclusive temporary file, renamed atomically, then the directory is flushed.
// Uncertain means rename happened but directory flush failed; active UI settings
// may stay applied and the caller can retry. Never claim durable Saved then.
[[nodiscard]] CovePreferencesStatus saveNativeCovePreferences(const std::filesystem::path&,
    const game::expedition::CoveInputPreferences&,std::string& message);
} // namespace voxy::platform
