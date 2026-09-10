#pragma once
#include "game/expedition/cove_boat.hpp"
#include "game/construction/build_refit.hpp"
#include "game/expedition/session_transactions.hpp"

namespace voxy::game::expedition {

// Granted only when this local host world is created, never by Reset/Launch.
inline constexpr construction::ResourceAmounts coveStartingMaterials{48,0};

struct CoveBuildSeed {
    construction::PartCatalog catalog;
    construction::BuildSnapshot build;
    std::vector<CoveBoatAssembly::Part> placements;
    construction::DurableId starterEntitlement{};
    uint64_t issuedThrough=0;
};
// Trusted bootstrap only, before GameSession owns this world's allocator. Uses
// the admitted starter parts, creates distinct build/part/weld/loan identities,
// and returns the high-water to install atomically with the whole bootstrap.
// Never call this to import a blueprint or replace an already running session.
[[nodiscard]] std::optional<CoveBuildSeed> prepareCoveBuild(
    const assets::LoadedAssetFixture&, construction::DurableId owner,
    uint64_t issuedThrough, std::string& error);

// Build the immutable grant policy from the original, installed starter. The
// recipe keeps no player ownership IDs; bindings keep its exact ordinal order.
[[nodiscard]] std::optional<StarterKit> prepareCoveStarterKit(
    const construction::BuildSnapshot&,construction::DurableId entitlement,std::string& error);
[[nodiscard]] std::optional<std::vector<CoveBoatAssembly::Part>> bindCoveStarterKit(
    const StarterKit& installed,const StarterKit* current,
    std::span<const CoveBoatAssembly::Part> originalBindings,std::string& error);

struct CoveRefitRequest {
    std::shared_ptr<const construction::BuildRefitRequest> design;
    std::vector<CoveBoatAssembly::Part> placements;
};
// Connect design slots to retained canonical IDs or zero-source paid additions.
// It creates no IDs/inventory and cannot revive removed starter loans in their
// reserved slots. Pass the original loan slot set from the installed cove.
[[nodiscard]] std::optional<CoveRefitRequest> prepareCoveRefit(
    const assets::LoadedAssetFixture& design,const CoveBoatAssembly& owned,
    const construction::PartCatalog&,std::string& error,
    std::span<const uint32_t> reservedSlots = {},
    std::span<const construction::PartInstance> storedParts = {});

struct CoveLaunchDesign {
    assets::LoadedAssetFixture scene;
    std::vector<CoveBoatAssembly::Part> placements;
};
// Rebuild stable scene slots from the authoritative build, including undo of a
// prior removal. Bindings are trusted identity-to-content assignments, never
// guessed by matching a mesh/definition. Missing part assets refuse admission.
[[nodiscard]] std::optional<CoveLaunchDesign> prepareCoveLaunchDesign(
    const assets::LoadedAssetFixture& installed,const construction::BuildSnapshot&,
    std::span<const CoveBoatAssembly::Part> bindings,std::string& error);

// Paid additions receive IDs from GameSession. Preserve retained render slots;
// assign restored/new paid IDs to free dynamic slots using exact admitted
// definition keys. These mappings are derived, not inventory or identity.
enum class CoveSceneTopology { Welded, AcceptedRoots };
[[nodiscard]] std::optional<CoveLaunchDesign> prepareCoveExpandedLaunchDesign(
    const assets::LoadedAssetFixture& original,const assets::LoadedAssetFixture& current,
    const construction::BuildSnapshot&,std::span<const CoveBoatAssembly::Part> originalBindings,
    std::span<const CoveBoatAssembly::Part> currentBindings,std::string& error,
    CoveSceneTopology topology=CoveSceneTopology::Welded);

struct CoveDesignCost {
    construction::ResourceAmounts debit{},credit{};
    [[nodiscard]] bool affordable(construction::ResourceAmounts) const noexcept;
};
[[nodiscard]] std::optional<CoveDesignCost> quoteCoveDesign(
    const assets::LoadedAssetFixture&,const CoveBoatAssembly&,
    const construction::PartCatalog&,std::span<const construction::PartInstance> storedParts = {}) noexcept;
[[nodiscard]] const construction::PartInstance* availableCoveStoredPart(
    const assets::LoadedAssetFixture&,const CoveBoatAssembly&,std::span<const construction::PartInstance>,construction::ContentKey) noexcept;

} // namespace voxy::game::expedition
