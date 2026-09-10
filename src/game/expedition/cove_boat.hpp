#pragma once

#include "game/assets/fixture_registry.hpp"
#include "game/construction/compiled_assembly.hpp"
#include "physics/authored_body_frame.hpp"

namespace voxy::game::expedition {

// Exact fixed scene exterior, independently owned from the welded boat. This
// union is scenery, not a welded machine; source labels retain placement/proxy
// provenance and the reference mass is never activated as a dynamic body.
class CoveSceneryCollision {
public:
    struct Source { uint32_t placement; construction::ProxyId proxy; };
    [[nodiscard]] static std::unique_ptr<CoveSceneryCollision> compile(
        const assets::LoadedAssetFixture&, std::string& error);
    [[nodiscard]] const physics::AuthoredShape& shape() const noexcept { return shape_; }
    [[nodiscard]] glm::dvec3 origin() const noexcept { return origin_; }
    [[nodiscard]] std::span<const Source> sources() const noexcept { return sources_; }
private:
    CoveSceneryCollision(physics::AuthoredShape shape, glm::dvec3 origin, std::vector<Source> sources)
        : shape_(std::move(shape)), origin_(origin), sources_(std::move(sources)) {}
    physics::AuthoredShape shape_;
    glm::dvec3 origin_;
    std::vector<Source> sources_;
};

// Catalog from the exact admitted cooked bundles, for canonical session builds.
[[nodiscard]] std::optional<construction::PartCatalog> makeCovePartCatalog(
    const assets::LoadedAssetFixture&, std::string& error);

// Effective output for the current single propeller/helm/winch cove. Engine
// drive networks, control channels and automated rope payout are not consumers.
[[nodiscard]] inline float coveModuleOutput(const construction::AssemblyModuleBinding& module) noexcept {
    if(!module.settings.enabled)return 0;
    if(module.settings.kind==construction::SettingsKind::Power || module.settings.kind==construction::SettingsKind::Steering)
        return static_cast<float>(module.settings.limitPermille)/1000.0f;
    return 1;
}

// Physical preparation of the boat selected by cove navigation. Dock/scenery
// never enter its mass or flotation. compile(scene)/compileCargo use transient
// inspection identities; compileBuild preserves the supplied canonical build
// identities and explicit part-to-placement bindings. Live body admission and
// its owned submission, not CPU compilation, activate the physical assembly.
class CoveBoatAssembly {
public:
    struct Part { uint32_t placement = 0; construction::DurableId id{}; };
    // Indices match every compiled assembly plan. Water cells/points use this
    // root's authored frame, never another fragment's COM or placement anchor.
    struct Root {
        physics::AuthoredShape shape;
        std::vector<physics::AuthoredWaterCell> cells{};
        std::optional<construction::DurableId> propeller{},helm{};
        glm::vec3 propellerPoint{},propellerDirection{0,0,-1};
        float maximumThrustNewtons=0,maximumSteeringRadians=0;
        // Returned cells borrow this immutable root. No cached span survives a
        // vector move and no live body handle is stored in prepared content.
        [[nodiscard]] physics::AuthoredWaterBodyDesc water(physics::BodyHandle body) const noexcept;
    };
    [[nodiscard]] static std::unique_ptr<CoveBoatAssembly> compile(
        const assets::LoadedAssetFixture&, std::string& error);
    // Inspection copy of an accepted broken design. Requires one explicit helm;
    // live ownership always uses compileFragments with canonical identities.
    [[nodiscard]] static std::unique_ptr<CoveBoatAssembly> compileSeparatedScene(
        const assets::LoadedAssetFixture&, std::string& error);
    [[nodiscard]] static std::unique_ptr<CoveBoatAssembly> compileCargo(
        const assets::LoadedAssetFixture&, uint32_t placement, std::string& error);
    [[nodiscard]] static std::unique_ptr<CoveBoatAssembly> compileBuild(
        const construction::BuildSnapshot&, const construction::PartCatalog&,
        std::span<const Part> placements, std::string& error);
    // Cuts keep one owning build. This prepares every resulting rigid root;
    // callers still reserve and publish all live bodies under one transaction.
    // The selected control part must be an existing helm. No root-index guess.
    [[nodiscard]] static std::unique_ptr<CoveBoatAssembly> compileFragments(
        const construction::BuildSnapshot&,const construction::PartCatalog&,
        std::span<const Part> placements,construction::DurableId controlPart,std::string& error);
    [[nodiscard]] const construction::BuildSnapshot& build() const noexcept { return build_; }
    [[nodiscard]] const construction::CompiledAssembly& assembly() const noexcept { return assembly_; }
    [[nodiscard]] std::span<const Root> roots() const noexcept { return roots_; }
    [[nodiscard]] uint32_t primaryRootIndex() const noexcept { return primaryRoot_; }
    [[nodiscard]] const Root& primaryRoot() const noexcept { return roots_[primaryRoot_]; }
    [[nodiscard]] const construction::AssemblyMassRoot& primaryMassRoot() const noexcept { return assembly_.mass().roots()[primaryRoot_]; }
    [[nodiscard]] const physics::AuthoredShape& shape() const noexcept { return primaryRoot().shape; }
    [[nodiscard]] std::optional<uint32_t> rootForPart(construction::DurableId) const noexcept;
    [[nodiscard]] std::span<const Part> parts() const noexcept { return parts_; }
    [[nodiscard]] double massKg() const noexcept {
        double total=0;for(const auto& root:assembly_.mass().roots())total+=root.mass.dryMassKg;return total;
    }
    [[nodiscard]] double displacementCubicMetres() const noexcept;
    // Upright primary-root equilibrium against a flat water plane at local zero.
    // Initial placement only; the GPU owns subsequent hydrostatic motion.
    [[nodiscard]] double equilibriumRootHeight(double waterDensity = 1000) const noexcept;
private:
    [[nodiscard]] static std::unique_ptr<CoveBoatAssembly> compileMembers(
        const assets::LoadedAssetFixture&, std::span<const uint32_t>, std::string& error, bool separated = false);
    [[nodiscard]] static std::unique_ptr<CoveBoatAssembly> compileRoots(
        const construction::BuildSnapshot&,const construction::PartCatalog&,
        std::span<const Part>,std::optional<construction::DurableId>,std::string& error);
    CoveBoatAssembly(construction::CompiledAssembly assembly,std::vector<Root> roots,uint32_t primaryRoot,
                     std::vector<Part> parts,construction::BuildSnapshot build)
        : assembly_(std::move(assembly)),roots_(std::move(roots)),primaryRoot_(primaryRoot),parts_(std::move(parts)),build_(std::move(build)) {}
    construction::CompiledAssembly assembly_;
    std::vector<Root> roots_;
    uint32_t primaryRoot_=0;
    std::vector<Part> parts_;
    construction::BuildSnapshot build_;
};
} // namespace voxy::game::expedition
