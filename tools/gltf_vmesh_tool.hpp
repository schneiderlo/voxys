#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "moto/vmesh.hpp"

namespace voxy::tools {

enum class GltfImportProfile : uint8_t {
    Legacy,
    SalvageRigidV1,
};

/// Convert a glTF 2.0 document held in memory into the engine's .vmesh form.
/// `inputBytes` carries the whole file (either a binary .glb or a plain-text
/// .gltf); `isGlb` selects the loader. On failure the destination is left
/// untouched and `error` holds a human readable reason.
///
/// The runtime never touches glTF; this is the offline conversion entry point.
/// The strict rigid profile accepts only self-contained GLB bytes and denies
/// external filesystem reads. Legacy loading retains its original behavior.
[[nodiscard]] bool convertGltfBytesToVmesh(const std::vector<uint8_t>& inputBytes,
                                           bool isGlb, voxy::moto::VmeshData* out,
                                           std::string* error);

[[nodiscard]] bool convertGltfBytesToVmesh(const std::vector<uint8_t>& inputBytes,
                                           bool isGlb, GltfImportProfile profile,
                                           voxy::moto::VmeshData* out,
                                           std::string* error);

}  // namespace voxy::tools
