#pragma once

#include "game/assets/cooked_part_bundle.hpp"

#include <filesystem>

namespace voxy::game::assets {

// Opens a trusted package root component by component without following links.
// The returned provider snapshots regular leaf files through a retained native
// directory descriptor. Emscripten's single-threaded packaged MEMFS additionally
// rejects a replaced root path because its openat emulation resolves paths.
// No arbitrary URL, directory traversal, FIFO or unbounded allocation is allowed.
// Linux/MEMFS implementation; other platform adapters remain explicit work.
[[nodiscard]] std::optional<CookedPartByteProvider> openCookedPartDirectory(
    const std::filesystem::path& root, std::string& error);

} // namespace voxy::game::assets
