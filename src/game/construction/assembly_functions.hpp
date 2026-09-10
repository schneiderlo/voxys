#pragma once

#include "game/construction/assembly_buoyancy.hpp"

namespace voxy::game::construction {

inline constexpr size_t kMaximumAssemblyFunctionFrames = 3 * kMaximumBuildParts;
inline constexpr uint32_t kNoAssemblySocket = std::numeric_limits<uint32_t>::max();
struct AssemblyFunctionLimits {
    size_t modules = kMaximumBuildParts;
    size_t sockets = kMaximumBuildSocketRecords;
    size_t connections = kMaximumBuildConnections;
    size_t frames = kMaximumAssemblyFunctionFrames;
};
enum class AssemblyFunctionError : uint8_t { None, InvalidProfile, Capacity, InvalidFrame };
struct AssemblyFunctionIssue {
    AssemblyBuoyancyIssue buoyancy{};
    AssemblyFunctionError error = AssemblyFunctionError::None;
    DurableId object{};
};

// PartOrigin is a spatial frame, unrelated to paid/loan PartProvenance.
enum class AssemblyFrameKind : uint8_t { PartOrigin, DriveShaft, Thrust, Operator, TowLine, TowEye, CargoLatch };
struct AssemblyFunctionFrame {
    uint32_t module = 0; // Same index as the mass plan's part array.
    AssemblyFrameKind kind = AssemblyFrameKind::PartOrigin;
    GridTransform rootFromFrame{};
    uint32_t socket = kNoAssemblySocket; // Index in this plan, only for socket-derived frames.
    [[nodiscard]] bool operator==(const AssemblyFunctionFrame&) const = default;
};
struct AssemblySocketBinding {
    SocketEndpoint endpoint{};
    uint32_t root = 0;
    SocketDefinition definition{}; // Owns authored part-local frame/clearance/limits.
    GridTransform rootFromSocket{}; // +Y outward, +X key, all exact lattice data.
    uint8_t usedSlots = 0; // Includes disabled links, as canonical construction does.
};
struct AssemblyModuleBinding {
    DurableId part{};
    uint32_t root = 0;
    PartModule parameters{}; // Authored numeric limits, not mutable live controls.
    ModuleSettings settings{};
    uint16_t health = kFullHealth;
    StrengthLimits strength{};
    uint32_t firstSocket = 0, socketCount = 0;
    uint32_t firstFrame = 0, frameCount = 0;
};
struct AssemblyConnectionBinding {
    Connection definition{}; // Canonical endpoints; preserves ID, damage, strength and rope lengths.
    uint32_t socketA = 0, socketB = 0;
};

// Immutable, owned functional preparation accompanying the same mass/geometry
// compile. This is not live joint creation, engine dispatch or cargo capture.
// Enabled welds are already one root; don't emit a redundant solver constraint.
// Non-weld links may join different OR identical roots. Later mechanics must
// explicitly decide whether/how to activate them; no inferred rest/capture pose.
class AssemblyFunctionPlan {
public:
    [[nodiscard]] static std::optional<AssemblyFunctionPlan> compile(
        const BuildSnapshot&, const PartCatalog&, AssemblyFunctionIssue&,
        AssemblyMassLimits = {}, BoxUnionLimits = {}, BoxCoverageLimits = {}, AssemblyFunctionLimits = {});
    [[nodiscard]] const AssemblyBuoyancyPlan& buoyancyPlan() const noexcept { return buoyancy_; }
    [[nodiscard]] const AssemblyMassPlan& massPlan() const noexcept { return buoyancy_.collisionPlan().massPlan(); }
    [[nodiscard]] std::span<const AssemblyModuleBinding> modules() const noexcept { return modules_; }
    [[nodiscard]] std::span<const AssemblySocketBinding> sockets() const noexcept { return sockets_; }
    [[nodiscard]] std::span<const AssemblyFunctionFrame> frames() const noexcept { return frames_; }
    [[nodiscard]] std::span<const AssemblyConnectionBinding> connections() const noexcept { return connections_; }
    [[nodiscard]] std::span<const AssemblyFunctionFrame> moduleFrames(size_t module) const noexcept;
    [[nodiscard]] const AssemblyModuleBinding* module(DurableId part) const noexcept;
    [[nodiscard]] const AssemblySocketBinding* socket(SocketEndpoint endpoint) const noexcept;
private:
    explicit AssemblyFunctionPlan(AssemblyBuoyancyPlan buoyancy) : buoyancy_(std::move(buoyancy)) {}
    [[nodiscard]] uint32_t socketIndex(SocketEndpoint endpoint) const noexcept;
    AssemblyBuoyancyPlan buoyancy_;
    std::vector<AssemblyModuleBinding> modules_{};
    std::vector<AssemblySocketBinding> sockets_{};
    std::vector<AssemblyFunctionFrame> frames_{};
    std::vector<AssemblyConnectionBinding> connections_{};
};

} // namespace voxy::game::construction
