#pragma once

#include "game/assets/rigid_prefab.hpp"

#include <array>
#include <optional>
#include <string_view>

namespace voxy::game::assets {

inline constexpr size_t kRigidAnimationMaximumNodes = 32;
inline constexpr size_t kRigidAnimationMaximumDraws = 24;
inline constexpr std::array<std::string_view, 8> kRobotClips{
    "idle", "walk", "jump", "fall", "swim", "helm", "tool", "land"};
inline constexpr std::array<std::string_view, 6> kRobotAnchors{
    "robot_head_anchor", "robot_hand_l_anchor", "robot_hand_r_anchor",
    "robot_tool_anchor", "robot_helm_l_anchor", "robot_helm_r_anchor"};

// Immutable CPU owner, retained by the same generation as its MeshPath upload.
// Animation is presentation only: no inventory, body, save or collision identity.
struct RigidAnimationAsset {
    moto::VmeshData mesh;
    RigidPrefab prefab;
    std::array<uint32_t, kRigidAnimationMaximumNodes> evaluationOrder{};
    std::array<uint32_t, kRobotClips.size()> clips{};
    std::array<uint32_t, kRobotAnchors.size()> anchors{};
};

// Validates the strict robot contract before GPU admission. The input is owned
// by value, permitting a move from readVmesh. Failure preserves output. Includes
// animation bytes in prefab.counts.decodedBytes. Hard limits: 32 nodes, 24 mesh
// instances, 48 expanded draws, 1 MiB GPU payload and 2 MiB decoded payload.
[[nodiscard]] bool prepareRigidAnimation(moto::VmeshData data,
    RigidAnimationAsset& output, std::string& error);

struct RigidAnimationBlend {
    uint32_t clipIndex = 0; // Actual mesh.anims index, as recorded in asset.clips.
    double timeSeconds = 0;
    double weight = 0; // Blend towards this clip; [0,1].
    // An outgoing one-shot must not start looping just because its destination
    // loops. Omission preserves the original caller's shared loop policy.
    std::optional<bool> loop = std::nullopt;
};

struct RigidAnimationPose {
    // Full hierarchy in exported asset space, before the explicit basis bridge.
    std::array<glm::dmat4, kRigidAnimationMaximumNodes> nodeToAsset{};
    std::array<RigidPrefabDraw, kRigidAnimationMaximumDraws> draws{};
    uint32_t drawCount = 0;
    // Camera-relative canonical space, using the exact same transforms as draws.
    std::array<glm::dmat4, kRobotAnchors.size()> anchors{};
    PrefabBounds bounds{};
};

// Allocation-free successful sampling. Looping is explicit; nonlooping clips
// clamp at both ends. Blend local TRS, then evaluate parents before children.
// Quaternions use normalized shortest-arc SLERP; no skinning or root motion.
// cameraRelativeRoot is a finite proper rigid transform. Output is transactional.
[[nodiscard]] bool sampleRigidAnimation(const RigidAnimationAsset& asset,
    uint32_t clipIndex, double timeSeconds, bool loop,
    std::optional<RigidAnimationBlend> blend, const glm::dmat4& cameraRelativeRoot,
    RigidAnimationPose& output, std::string& error);

} // namespace voxy::game::assets
