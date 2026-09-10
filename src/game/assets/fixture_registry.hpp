#pragma once

#include "game/assets/cooked_part_directory.hpp"
#include "game/assets/fixture_limits.hpp"
#include "game/construction/build_model.hpp"

namespace voxy::game::assets {

struct FixtureBundleSpec {
    std::string directory{}; // Relative to the trusted registry directory.
    CookedPartSelection selection{};
};
struct FixturePartPlacement {
    uint32_t bundleIndex = 0;
    construction::GridTransform placement{};
    // In schema 2 the same bounded index instead selects registry.prototypes.
    bool prototype = false;
    // Runtime workshop overlay only; installed registry schemas do not read it.
    // Absence means authored defaults. Canonical Launch fills this from the
    // owned part so local history and subsequent edits preserve configuration.
    std::optional<construction::ModuleSettings> settings{};
    std::optional<std::array<uint8_t,4>> paint{};
};
struct FixtureSocketConnection {
    uint32_t aPlacement = 0, bPlacement = 0; // Zero-based registry ordinals, not durable IDs.
    construction::SocketId aSocket{}, bSocket{};
};
// Schema 3: metre coordinates in the same local frame as the authored parts.
// These are grounded interaction positions, not unconstrained teleport targets.
struct CoveDeliveryZone {
    glm::dvec3 center{};
    double radius=5,minimumHeight=-1,maximumSpeed=.8,maximumAngularSpeed=1;
};
struct CoveNavigation {
    glm::dvec3 spawn{}, dockBoarding{}, boatBoarding{}, helmStanding{}, lookTarget{};
    std::vector<uint32_t> boatPlacements;
    std::vector<uint32_t> cargoPlacements;
    std::optional<CoveDeliveryZone> delivery;
};
struct AssetFixtureRegistry {
    uint32_t schema = 1;
    std::vector<FixtureBundleSpec> bundles{};
    std::vector<FixturePartPlacement> placements{};
    std::vector<construction::ContentKey> prototypes{};
    std::vector<FixtureSocketConnection> connections{};
    glm::dvec3 cameraEye{6,4,-8};
    glm::dvec3 cameraTarget{0,0,0};
    std::optional<CoveNavigation> navigation{};
};
struct LoadedAssetFixture {
    // Digest of the exact installed world layout, before runtime edits or
    // additive catalogue admission. Existing parts may never be overridden.
    std::array<std::byte,32> installedRegistryDigest{};
    AssetFixtureRegistry registry{};
    std::vector<std::shared_ptr<const CookedPartBundle>> bundles{};
    std::vector<construction::PartDefinition> prototypes{};
    // Private static inspection validation, never exported or added to a
    // GameSession/inventory. All six rendered parts use these exact placements.
    std::optional<construction::BuildModel> assembly{};
    std::vector<std::vector<construction::SocketId>> connectedSockets{};
};

// Trusted installed-content selection, kept as data so changing an authored
// candidate needs no C++/shader edit. Closed schemas 1–6, 64 KiB; explicit
// catalogue/placement/connection ceilings are in fixture_limits.hpp.
[[nodiscard]] std::optional<AssetFixtureRegistry> parseAssetFixtureRegistry(
    std::string_view json, std::string& error);
// No-follow directory adapter for the registry and every referenced bundle.
// Schema 3 adds cove navigation and explicit static boat placement membership.
// Schema 4 also carries the actual boat welds for CoveBoatAssembly compilation.
// Schema 5 separates one independent salvage load from boat/fixed scenery.
// Failure returns no partially loaded fixture. No GPU or catalog publication.
[[nodiscard]] std::unique_ptr<const LoadedAssetFixture> loadAssetFixture(
    const std::filesystem::path& registryPath, std::string& error);

// Trusted additive catalogue, schema 1 {schema,bundles}. It admits exact
// immutable content through the same no-follow loader. It cannot replace an
// existing content ID, alter world placements or change the base layout
// identity. Older saves still resolve the same exact part keys; new saves
// require their new definitions to be installed. Failure leaves base intact.
[[nodiscard]] std::unique_ptr<const LoadedAssetFixture> appendAssetFixtureCatalog(
    const LoadedAssetFixture& base, const std::filesystem::path& catalogPath,
    std::string& error);

// Projects the union of all canonical LOD bounds. Returns a stable ID by
// threshold order, independent of the admitted bundle's ID-sorted array order.
// A bound crossing the eye plane conservatively chooses the finest LOD.
[[nodiscard]] std::optional<uint64_t> selectFixtureLod(const CookedPartBundle& bundle,
    const glm::dmat4& cameraRelativeRoot, construction::GridTransform placement,
    const glm::dmat4& viewProjection, uint32_t physicalViewportHeight);

} // namespace voxy::game::assets
