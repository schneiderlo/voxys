#pragma once
#include "game/assets/rigid_animation.hpp"
#include "game/assets/cooked_part_directory.hpp"
namespace voxy::game::assets {
// Installed presentation package only. Capped immutable snapshots, SHA binding,
// strict profile, then independent VMESH/hierarchy/animation validation.
[[nodiscard]] std::shared_ptr<const RigidAnimationAsset> loadRobotAsset(
    const CookedPartByteProvider&,std::string& error);
[[nodiscard]] std::shared_ptr<const RigidAnimationAsset> loadRobotAsset(
    const std::filesystem::path& directory,std::string& error);
}
