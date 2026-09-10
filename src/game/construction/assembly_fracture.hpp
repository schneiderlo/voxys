#pragma once

#include "game/construction/compiled_assembly.hpp"
#include "physics/rigid_mass_frame.hpp"

namespace voxy::game::construction {

enum class AssemblyFractureError : uint8_t {
    None, InvalidRequest, StaleRevision, RevisionExhausted, UnknownConnection,
    InactiveConnection, UnsupportedConnection, InvalidBuild, Compilation,
    Capacity, InvalidMapping, MotionIdentity, MotionRoots, InvalidMotion,
};
struct AssemblyFractureIssue {
    AssemblyFractureError error = AssemblyFractureError::None;
    DurableId object{};
    BuildIssue build{};
    AssemblyFunctionIssue assembly{};
};
struct AssemblyFragmentBinding {
    uint32_t parentRoot = 0; // Index in the before compilation.
    physics::MassVector parentFromRoot{}; // Translation in parent-root axes, metres.
};
struct AssemblyRootMotion {
    DurableId root{}; // Least member part ID, never a solver handle.
    physics::MassPose worldFromRoot{}; // Common caller-selected origin/sector frame.
    physics::MassVector originVelocity{}, angularVelocity{}; // World axes.
};
struct AssemblyMotionSource {
    DurableId build{};
    TopologyRevision revision{};
    SimulationTick completedTick{};
    std::span<const AssemblyRootMotion> roots{};
};

// Immutable preparation for tool cuts/structural failure. It neither authorizes
// a player action nor commits ownership, physics or storage. One owning build
// retains every part and disabled bond, including loan provenance and damage.
// The existing compiler determines connected roots and prepares all geometry,
// mass, buoyancy, functions and remapped socket bindings before publication.
class AssemblyFracturePlan {
public:
    [[nodiscard]] static std::optional<AssemblyFracturePlan> prepare(
        const BuildSnapshot&, TopologyRevision expected, std::span<const DurableId> cuts,
        const PartCatalog&, AssemblyFractureIssue&, AssemblyCompileProfile = {});
    [[nodiscard]] const BuildSnapshot& afterBuild() const noexcept { return afterBuild_; }
    [[nodiscard]] const CompiledAssembly& before() const noexcept { return before_; }
    [[nodiscard]] const CompiledAssembly& after() const noexcept { return after_; }
    [[nodiscard]] std::span<const DurableId> cuts() const noexcept { return cuts_; }
    // Same order as after().mass().roots(). Untouched roots are included.
    [[nodiscard]] std::span<const AssemblyFragmentBinding> fragments() const noexcept { return fragments_; }

    // Caller must supply a certified, completed POST-SOLVE observation. The
    // identity/tick checks reject stale or mixed snapshots, but this value API
    // cannot certify a GPU fence or accept client-asserted motion as authority.
    // v_new_origin = v_parent_origin + omega_parent x rotated_root_offset.
    // No impact impulse is accepted/reapplied. Output uses the same common
    // reference frame; sector/GPU representability remains backend admission.
    [[nodiscard]] std::optional<std::vector<AssemblyRootMotion>> inheritMotion(
        const AssemblyMotionSource&, SimulationTick expectedTick, AssemblyFractureIssue&) const;
private:
    AssemblyFracturePlan(BuildSnapshot build, CompiledAssembly before, CompiledAssembly after,
        std::vector<DurableId> cuts, std::vector<AssemblyFragmentBinding> fragments)
        : afterBuild_(std::move(build)), before_(std::move(before)), after_(std::move(after)),
          cuts_(std::move(cuts)), fragments_(std::move(fragments)) {}
    BuildSnapshot afterBuild_;
    CompiledAssembly before_, after_;
    std::vector<DurableId> cuts_;
    std::vector<AssemblyFragmentBinding> fragments_;
};
} // namespace voxy::game::construction
