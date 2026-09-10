#pragma once

#include "core/sha256.hpp"
#include "game/construction/assembly_functions.hpp"

namespace voxy::game::construction {

// Advance when compiler algorithms, canonical digest grammar or physical
// interpretation change. A data ContentKey cannot substitute for this version.
inline constexpr uint32_t kAssemblyCompilerVersion = 1;
struct AssemblyCompileProfile {
    AssemblyMassLimits mass{};
    BoxUnionLimits collision{};
    BoxCoverageLimits buoyancy{};
    AssemblyFunctionLimits functions{};
};

// Complete CPU preparation, not an activated backend shape or canonical
// inventory. Owned mass/exterior geometry/displacement/functions originate in
// one fully validated build/catalog invocation. SIM-02 must honor exterior
// patches and explicitly adapt root/COM frames; later mechanics activate links.
class CompiledAssembly {
public:
    [[nodiscard]] static std::optional<CompiledAssembly> compile(
        const BuildSnapshot&, const PartCatalog&, AssemblyFunctionIssue&, AssemblyCompileProfile = {});
    [[nodiscard]] const AssemblyFunctionPlan& functions() const noexcept { return functions_; }
    [[nodiscard]] const AssemblyMassPlan& mass() const noexcept { return functions_.massPlan(); }
    [[nodiscard]] const AssemblyBuoyancyPlan& buoyancy() const noexcept { return functions_.buoyancyPlan(); }
    [[nodiscard]] const AssemblyCollisionPlan& collision() const noexcept { return buoyancy().collisionPlan(); }
    [[nodiscard]] const AssemblyCompileProfile& profile() const noexcept { return profile_; }
    // Versioned exact physical-input identity, including build/revision and
    // actual used content. Excludes economics, permission and visual state;
    // never use a matching digest as authorization, a save or a live cache hit.
    [[nodiscard]] core::Sha256Digest inputIdentity() const noexcept { return identity_; }
private:
    CompiledAssembly(AssemblyFunctionPlan functions, AssemblyCompileProfile profile, core::Sha256Digest identity)
        : functions_(std::move(functions)), profile_(profile), identity_(identity) {}
    AssemblyFunctionPlan functions_;
    AssemblyCompileProfile profile_{};
    core::Sha256Digest identity_{};
};
} // namespace voxy::game::construction
