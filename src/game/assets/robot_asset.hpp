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
// Separate installed LEGO-style person identity; legacy robot whitelist stays strict.
[[nodiscard]] std::shared_ptr<const RigidAnimationAsset> loadHumanAsset(
    const CookedPartByteProvider&,std::string& error);
[[nodiscard]] std::shared_ptr<const RigidAnimationAsset> loadHumanAsset(
    const std::filesystem::path& directory,std::string& error);
// The three installed role appearances have separate immutable identities.
// IDs 1–3 map to Moss, Rivet and Lumen. Other IDs never read a package.
[[nodiscard]] std::shared_ptr<const RigidAnimationAsset> loadResidentAsset(
    uint32_t residentId,const CookedPartByteProvider&,std::string& error);
[[nodiscard]] std::shared_ptr<const RigidAnimationAsset> loadResidentAsset(
    uint32_t residentId,const std::filesystem::path& directory,std::string& error);
// Installed woodland raider only; other character identities cannot substitute.
[[nodiscard]] std::shared_ptr<const RigidAnimationAsset> loadRaiderAsset(
    const CookedPartByteProvider&,std::string& error);
[[nodiscard]] std::shared_ptr<const RigidAnimationAsset> loadRaiderAsset(
    const std::filesystem::path& directory,std::string& error);
}
