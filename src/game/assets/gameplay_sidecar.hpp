#pragma once

#include "game/construction/part_catalog.hpp"

namespace voxy::game::assets {

inline constexpr uint32_t kGameplaySidecarSchema = 1;
inline constexpr size_t kMaximumSidecarBytes = 1024 * 1024;
inline constexpr size_t kMaximumSidecarAnchors = 128;
inline constexpr uint64_t kMaximumSourceGlbBytes = 64 * 1024 * 1024;
inline constexpr size_t kMaximumCookedVmeshBytes = 256 * 1024 * 1024;

struct LodBinding {
    uint64_t id = 0; // Stable local ID scoped by the part content key, not LOD index.
    game::construction::ContentKey asset{};
    std::string sourceFile{};
    std::string sourceSha256{};
    uint64_t sourceBytes = 0;
    game::construction::CubeRotation renderToCanonical{};
    double minimumScreenHeightPixels = 0.0;
};

struct ToolAnchor {
    uint64_t id = 0;
    std::string nameKey{};
    game::construction::GridTransform frame{};
};

struct GameplaySidecar {
    game::construction::PartDefinition part{};
    std::vector<LodBinding> lods{};
    std::vector<ToolAnchor> anchors{};
    std::string normalizedJson{};
};

// Parses bounded schema-1 data, then uses the existing PartCatalog validator
// for all physical, socket, module, cost and visual invariants. The resolver
// must verify actual cooked assets. This function does not verify source SHA256
// bytes; the file orchestrator must bind self-contained GLBs before cooking.
// No output is published on error. Unknown fields/duplicates/oversized arrays
// reject rather than silently losing gameplay metadata.
[[nodiscard]] std::optional<GameplaySidecar> parseGameplaySidecar(
    std::string_view input, const game::construction::CookedAssetResolver& resolver,
    std::string& error);

[[nodiscard]] std::string cookedLodFilename(uint64_t id);
// Explicit source-basis adapter. VMESH vertices/nodes remain in their exported
// frame; runtime prefab evaluation composes node transforms separately.
[[nodiscard]] std::optional<game::construction::MetresPosition> renderPointToCanonical(
    const LodBinding& binding, game::construction::MetresPosition point) noexcept;

} // namespace voxy::game::assets
