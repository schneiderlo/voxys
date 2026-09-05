#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "moto/vmesh.hpp"

namespace voxy::tools {

/// Convert a glTF 2.0 document held in memory into the engine's .vmesh form.
/// `inputBytes` carries the whole file (either a binary .glb or a plain-text
/// .gltf); `isGlb` selects the loader. On failure the destination is left
/// untouched and `error` holds a human readable reason.
///
/// The runtime never touches glTF; this is the offline conversion entry point.
/// The function is a pure transformation over the byte input so unit tests can
/// exercise it without touching the filesystem.
[[nodiscard]] bool convertGltfBytesToVmesh(const std::vector<uint8_t>& inputBytes,
                                           bool isGlb, voxy::moto::VmeshData* out,
                                           std::string* error);

}  // namespace voxy::tools
