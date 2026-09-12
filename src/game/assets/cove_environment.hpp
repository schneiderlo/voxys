#pragma once

#include "game/assets/cooked_part_directory.hpp"
#include "game/assets/rigid_prefab.hpp"

#include <array>
#include <memory>
#include <string>
#include <vector>

namespace voxy::game::assets {

inline constexpr size_t kCoveEnvironmentLods=3;
inline constexpr size_t kCoveEnvironmentMaximumCollisionBoxes=48;
inline constexpr uint64_t kCoveEnvironmentMaximumGpuBytes=2ull*1024ull*1024ull;
inline constexpr uint32_t kCoveEnvironmentMaximumInstances=60;
inline constexpr uint32_t kCoveEnvironmentMaximumDraws=90;

// Scene-local canonical lattice boxes. The application adds the unchanged
// Cove origin for physical admission and supplies these same solids to player
// and camera collision. They never receive a construction/inventory identity.
struct CoveEnvironmentBox {
    construction::GridPosition minimum{},maximum{};
};

struct CoveEnvironmentLod {
    moto::VmeshData mesh;
    RigidPrefab prefab;
};

struct CoveEnvironmentAsset {
    // Scenery vertices already use scene-local canonical positions through
    // explicit basis12. Gantry vertices are relative to the existing lift root.
    std::array<CoveEnvironmentLod,kCoveEnvironmentLods> scenery,gantry;
    std::vector<CoveEnvironmentBox> collision;
    uint64_t gpuBytes=0; // All six admitted LOD payloads, not shared frame buffers.
    uint64_t decodedBytes=0;
};

// Re-admission at the renderer ownership boundary. Ignores public cached
// prefabs/counts and rebuilds them from all six actual mesh payloads. Invalid
// input preserves output; success owns a fresh immutable-ready CPU snapshot.
[[nodiscard]] bool prepareCoveEnvironment(const CoveEnvironmentAsset& input,
    CoveEnvironmentAsset& output,std::string& error);

// Fixed installed package only: bounded snapshots, exact hashes, strict rigid
// admission, shared collision/envelope validation. No GPU or game mutation.
[[nodiscard]] std::shared_ptr<const CoveEnvironmentAsset> loadCoveEnvironment(
    const CookedPartByteProvider&,std::string& error);
[[nodiscard]] std::shared_ptr<const CoveEnvironmentAsset> loadCoveEnvironment(
    const std::filesystem::path& directory,std::string& error);

} // namespace voxy::game::assets
