#pragma once

#include "game/construction/build_model.hpp"

namespace voxy::game::construction {

inline constexpr size_t kMaximumAssemblyRoots = 64;
inline constexpr int32_t kMaximumAssemblyRadiusTicks = 12800; // ±256 m per root axis.

struct AssemblyMassLimits {
    size_t parts = kMaximumBuildParts;
    size_t roots = kMaximumAssemblyRoots;
    int32_t radiusTicks = kMaximumAssemblyRadiusTicks;
};
enum class AssemblyError : uint8_t { None, InvalidProfile, InvalidBuild, EmptyBuild, Capacity, Extent, InvalidMass };
struct AssemblyIssue {
    AssemblyError error = AssemblyError::None;
    DurableId object{};
    BuildIssue build{};
};

struct AssemblyMassRoot {
    DurableId key{}; // Least member part ID, scoped by the plan build/revision; not a BodyHandle.
    GridTransform buildFromRoot{}; // Translation anchor, axes parallel to build axes.
    uint32_t partCount = 0;
    MassProperties mass{}; // Dry COM in root coordinates; full inertia about that COM.
    [[nodiscard]] bool operator==(const AssemblyMassRoot& other) const noexcept {
        return key == other.key && buildFromRoot == other.buildFromRoot && partCount == other.partCount
            && mass.dryMassKg == other.mass.dryMassKg && mass.localCenterOfMass == other.mass.localCenterOfMass
            && mass.inertia == other.mass.inertia;
    }
};
struct AssemblyMassPart {
    DurableId part{};
    ContentKey definition{};
    uint32_t root = 0; // Index valid only for this immutable plan.
    GridTransform rootFromPart{};
    [[nodiscard]] bool operator==(const AssemblyMassPart&) const = default;
};

// Intermediate mass/connectivity result only. Exterior collision, buoyancy,
// module/joint frames and physical-content cache identity remain SIM-01 work.
// No solver may activate this as a complete CompiledAssembly.
class AssemblyMassPlan {
public:
    [[nodiscard]] static std::optional<AssemblyMassPlan> compile(
        const BuildSnapshot&, const PartCatalog&, AssemblyIssue&, AssemblyMassLimits = {});
    [[nodiscard]] DurableId build() const noexcept { return build_; }
    [[nodiscard]] TopologyRevision revision() const noexcept { return revision_; }
    [[nodiscard]] std::span<const AssemblyMassRoot> roots() const noexcept { return roots_; }
    [[nodiscard]] std::span<const AssemblyMassPart> parts() const noexcept { return parts_; }

private:
    AssemblyMassPlan() = default;
    DurableId build_{};
    TopologyRevision revision_{};
    std::vector<AssemblyMassRoot> roots_{};
    std::vector<AssemblyMassPart> parts_{};
};

} // namespace voxy::game::construction
