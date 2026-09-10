// Deterministic, asset-bound authoring for the Wreckwater review cove.
//
// The source heightfield remains the world-scale terrain. This module only
// replaces one feathered 100 x 100 metre patch with a deliberate coastal
// profile so foreground review shots do not depend on inflated diffusion
// noise.

#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

#include <glm/vec2.hpp>

namespace voxy::terrain {

// Starter cove crop only: a four-metre-deep berth and seaward exit at world
// X [-28,-11], Z [-114,-84], with a four-metre feather. Never lowers dry land.
// Apply before both GPU terrain uploads and collision attachment.
[[nodiscard]] bool applySalvageBerth(std::span<uint16_t> heights,uint32_t width,uint32_t height,
    float heightScale,float cellScale,float waterHeight) noexcept;

struct AuthoredCoveConfig {
    // +normal points inland on a shallow mainland coast. Production
    // integration is source-aware, so this local profile preserves the
    // connected upland while smoothing and carving its waterline.
    glm::vec2 center{-650.0f, 3450.0f};
    glm::vec2 inlandNormal{-0.2730f, 0.9620f};
    glm::vec2 alongshoreTangent{-0.9620f, -0.2730f};
    float waterHeight = -200.0f;
    float authoredHalfExtent = 50.0f;
    float feather = 28.0f;
};

struct CoveMaterialWeights {
    float drySand = 0.0f;
    float soil = 0.0f;
    float grass = 0.0f;
    float rock = 0.0f;
    float wetSand = 0.0f;
};

struct AuthoredCoveSample {
    float height = 0.0f;
    float signedShoreDistance = 0.0f;
    float blend = 0.0f;
    CoveMaterialWeights material{};
};

struct AuthoredCoveStats {
    size_t touchedSamples = 0;
    size_t fullyAuthoredSamples = 0;
    float minimumAuthoredHeight = 0.0f;
    float maximumAuthoredHeight = 0.0f;
};

/// Evaluate the analytic cove before it is blended with the source heightmap.
[[nodiscard]] AuthoredCoveSample sampleAuthoredCove(
    glm::vec2 worldPosition,
    const AuthoredCoveConfig& config = {}) noexcept;

/// CPU reference for the shader's terrain-zone rules. Wet sand is a physical
/// response layered on the sand family, not a fifth texture allocation.
[[nodiscard]] CoveMaterialWeights classifyCoveMaterial(
    float relativeElevation,
    float upFacing,
    float authoredRockCue = 0.0f) noexcept;

/// Sculpt one feathered authored patch into an existing R16 heightfield.
/// Invalid layouts are rejected before any sample is changed.
[[nodiscard]] bool applyAuthoredCove(
    std::span<uint16_t> heights,
    uint32_t width,
    uint32_t height,
    float heightScale,
    float cellScale,
    const AuthoredCoveConfig& config,
    AuthoredCoveStats* stats = nullptr) noexcept;

} // namespace voxy::terrain
