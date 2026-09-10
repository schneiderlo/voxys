#pragma once
#include "game/expedition/cove_save.hpp"
#include "game/expedition/cove_build.hpp"
#include "game/expedition/cove_player.hpp"
#include "game/expedition/cove_rigid_roots.hpp"
#include "physics/authored_body_frame.hpp"

namespace voxy::game::expedition {
// Fully owned CPU load preparation. The catalog passed to prepare must outlive
// archive and the eventual recovered session. No GPU handles or authority are
// created here; exclusive storage ownership and live activation remain host work.
struct CoveRestoreCandidate {
    std::unique_ptr<CoveSaveArchive> archive;
    std::unique_ptr<const assets::LoadedAssetFixture> scene;
    std::unique_ptr<CoveBoatAssembly> boat,cargo;
    std::unique_ptr<CoveRigidRoots> roots;
    std::unique_ptr<CovePlayer> player;
    physics::AuthoredRootMotion boatMotion{},cargoMotion{};
    physics::AuthoredBodyMotionType cargoMotionType=physics::AuthoredBodyMotionType::Dynamic;
    // Body handles remain invalid until the host admits both actual bodies.
    physics::DistanceAttachmentDesc tow{};
    glm::vec3 towBoatPoint{},towCargoPoint{};
    construction::DurableId towRoot{};
    float reelSpeed=0;
    bool hasWinch=false;

    [[nodiscard]] static std::unique_ptr<CoveRestoreCandidate> prepare(
        std::span<const std::byte> archiveBytes,const CoveSaveContext&,
        const assets::LoadedAssetFixture& installed,const construction::PartCatalog&,
        std::span<const CoveBoatAssembly::Part> originalBindings,CovePlayer::Ground,
        std::string& error,const StarterKit* installedStarter = nullptr);
};
} // namespace voxy::game::expedition
