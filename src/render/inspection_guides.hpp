#pragma once

#include "game/assets/rigid_prefab.hpp"
#include "game/construction/part_catalog.hpp"

namespace voxy::render {

enum class InspectionGuides : uint8_t { Off, Dimensions, Sockets };

struct InspectionGuideBox {
    // Camera-relative canonical metadata; no glTF/exporter basis is applied.
    glm::mat4 model{1.0f};
    glm::vec4 color{1.0f};
};

// One shared, outward-wound unit cube, including an unlit material. Requested
// GPU storage: 24*72 vertex + 36*4 index + 64 material = 1936 bytes.
inline constexpr uint64_t inspectionGuideGpuBytes = 1936;
[[nodiscard]] moto::VmeshData inspectionGuideMesh();
// Validates a complete prototype-box definition, then produces its exact
// canonical footprint and authored linear material. Never edits its version.
[[nodiscard]] bool makePrototypeFixtureMesh(const game::construction::PartDefinition& part,
    moto::VmeshData& mesh, game::assets::RigidPrefab& prefab, std::string& error);

// Pure CPU, bounded and transactional. Requires a rigid camera-relative root.
// Each socket shows +X red, outward +Y green, +Z blue, and amber clearance.
// Dimensions show exact render bounds and metre/plate rulers on the first part
// selected by the caller. The asymmetric stand points blue toward forward -Z.
[[nodiscard]] bool makeInspectionGuides(
    const game::construction::PartDefinition& part,
    const game::assets::PrefabBounds& renderBounds,
    const glm::dmat4& cameraRelativeRoot,
    game::construction::GridTransform placement, InspectionGuides mode,
    size_t maximumBoxes, std::vector<InspectionGuideBox>& output, std::string& error,
    std::optional<std::span<const game::construction::SocketId>> selectedSockets = std::nullopt);

} // namespace voxy::render
