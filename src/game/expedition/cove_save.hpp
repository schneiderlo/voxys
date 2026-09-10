#pragma once
#include "game/expedition/session_save.hpp"
#include "game/expedition/cove_harbor_state.hpp"
#include "game/assets/fixture_limits.hpp"

namespace voxy::game::expedition {
inline constexpr uint32_t kCoveSaveSchema=1;
inline constexpr uint32_t kCoveSaveHarborSchema=2;
inline constexpr uint32_t kCoveSaveRecoverySchema=3;
inline constexpr uint32_t kCoveSaveRootsSchema=4;
inline constexpr uint32_t kCoveSaveJobsSchema=5;
inline constexpr size_t kMaximumCoveSavedRoots=32;
inline constexpr size_t kMaximumCoveSavedCargo=2;
// v5 appends a count and one 169-byte cargo record after the complete root
// table. Both jobs fit the existing physical allowance; legacy ceilings stay.
static_assert(419+22+4+52+88*kMaximumCoveSavedRoots+4
    +169*(kMaximumCoveSavedCargo-1)+8+32<=4096);
inline constexpr size_t kMaximumCoveRecoveryDesigns=4,kMaximumCoveRecoveryDesignBytes=128*1024;
inline constexpr size_t kMaximumCoveSaveBytes=2*kMaximumSessionSaveBytes+4096
    +kMaximumCoveRecoveryDesigns*(kMaximumCoveRecoveryDesignBytes+4);
using CoveRecoveryDesigns=std::vector<std::vector<std::byte>>;
// Geometry/configuration only. Part order is normalized independently of live
// paid/loan IDs so a restored instance does not consume a second backup slot.
[[nodiscard]] std::vector<std::byte> makeCoveRecoveryDesign(
    const construction::BuildSnapshot&,const construction::PartCatalog&);
enum class CoveRememberDesign:uint8_t { Stored,Known,Full,Invalid };
[[nodiscard]] CoveRememberDesign rememberCoveRecoveryDesign(CoveRecoveryDesigns&,
    std::span<const std::byte>,const construction::PartCatalog&);
// Stage protection before cutting/rebuilding. For an already cut build, a
// temporary all-weld copy only identifies its existing backup; it never repairs
// the owning build or invents a missing saved design. Failure preserves output.
[[nodiscard]] bool protectCoveRecoveryDesign(const construction::BuildSnapshot&,
    const construction::PartCatalog&,std::span<const std::byte> builtIn,
    CoveRecoveryDesigns&,size_t& selected,std::string& error);

struct CoveSavedMotion {
    construction::MetresPosition position{}; // Absolute authored-root origin, not COM.
    construction::CanonicalQuaternion orientation{};
    std::array<float,3> originVelocity{},angularVelocity{}; // World axes, m/s and rad/s.
    [[nodiscard]] bool operator==(const CoveSavedMotion&) const = default;
};
struct CoveSavedRoot {
    // Least member part ID in this accepted build/revision, not a GPU handle.
    construction::DurableId key{};
    CoveSavedMotion motion{};
    [[nodiscard]] bool operator==(const CoveSavedRoot&) const = default;
};
enum class CoveSavedPlayerMode:uint8_t { Walking,Airborne,Swimming,Helm };
struct CoveSavedPlayer {
    // Authored boat coordinates aboard; cove-scene coordinates otherwise.
    construction::MetresPosition feet{};
    double verticalSpeed=0;
    uint64_t tick=0,interactions=0;
    CoveSavedPlayerMode mode=CoveSavedPlayerMode::Walking;
    bool aboard=false;
    float viewYaw=0,viewPitch=0;
    [[nodiscard]] bool operator==(const CoveSavedPlayer&) const = default;
};
struct CoveSavedWater {
    uint32_t model=1; // Current two-cascade algorithm and its fixed seed recipe.
    double seconds=0;
    float height=0,strength=1;
    float significantWaveHeight=25.9f,directionRadians=.9948377f,choppiness=2.24f;
    float peakEnhancement=.65f,windAlignment=.32f,animationSpeed=2;
    std::array<float,2> patchLengths{1949,326},cascadeAmplitudes{.33f,.07f};
    float directionalSineScale=.68f;
    [[nodiscard]] bool operator==(const CoveSavedWater&) const = default;
};
enum class CoveSavedCargoState:uint8_t { Loose,Towed,BrokenTow,Banked };
struct CoveSavedCargo {
    construction::DurableId cargo{},job{};
    construction::ContentKey definition{};
    CoveSavedMotion motion{};
    CoveSavedCargoState state=CoveSavedCargoState::Loose;
    construction::DurableId winchPart{};
    float ropeLength=0;
    [[nodiscard]] bool operator==(const CoveSavedCargo&) const = default;
};
struct CovePhysicalSave {
    construction::SimulationTick tick{};
    construction::MetresPosition origin{};
    construction::DurableId boat{},cargo{},job{};
    construction::ContentKey cargoDefinition{};
    // In v4, boatMotion must equal the root carrying controlPart. Retaining
    // this legacy field does not substitute for the complete boatRoots set.
    CoveSavedMotion boatMotion{},cargoMotion{};
    CoveSavedPlayer player{};
    CoveSavedWater water{};
    CoveSavedCargoState cargoState=CoveSavedCargoState::Loose;
    // Nonzero only for Towed/BrokenTow. Derive anchors/limits from this part's
    // accepted definition/settings; never accept saved GPU handles or forces.
    construction::DurableId winchPart{};
    float ropeLength=0;
    // v1 remains canonical for pre-hoist worlds (profile 0). v2 explicitly
    // installs the service and its four-line state, including while detached.
    CoveHarborLiftState harborLift{};
    // v3: protected design-only layouts; explicit removal is a saved action.
    CoveRecoveryDesigns recoveryDesigns{};
    // v4: exact compiled root order, including a detached root with no module.
    // Empty selects legacy v1-v3 and is allowed only for a single rigid root.
    std::vector<CoveSavedRoot> boatRoots{};
    construction::DurableId controlPart{},playerRoot{};
    // controlPart is a stable helm part. playerRoot is zero ashore and an
    // exact root key aboard; aboard feet retain authored BUILD coordinates.
    // v5: first cargo remains the generator in the legacy fields. Every other
    // installed mission load retains its own motion, identity and delivery.
    // Banked loads stay present physically even after logical payout removes
    // their CargoRecord. No active-target selection can omit a saved load.
    std::vector<CoveSavedCargo> additionalCargo{};
    [[nodiscard]] bool operator==(const CovePhysicalSave&) const = default;
};
struct CoveCargoBinding {
    construction::DurableId cargo{},job{};
    CargoDefinition definition{};
    [[nodiscard]] bool operator==(const CoveCargoBinding&) const = default;
};
// Independently selected slot/content roles. Never obtain these expected values
// by trusting the archive being checked. Context/catalog must describe one cove.
struct CoveSaveContext {
    ExpectedRecoveryIdentity identity{};
    construction::DurableId boat{},cargo{},job{};
    CargoDefinition cargoDefinition{};
    construction::MetresPosition origin{};
    // Trusted installed mission roles, never inferred from imported bytes.
    // Empty accepts only the original one-job profile. One entry requires v5
    // with both physical loads and both logical job records, including banked
    // receipts. The second job unlocks after the generator powers the harbor.
    std::vector<CoveCargoBinding> additionalCargo{};
};
enum class CoveSaveError:uint8_t {
    None,Capacity,Encoding,UnsupportedSchema,Checksum,NonCanonical,Identity,
    LogicalState,PhysicalState,Lineage,
};
struct CoveSaveIssue {
    CoveSaveError error=CoveSaveError::None;
    SaveCodecIssue session{};
    [[nodiscard]] explicit operator bool() const noexcept {return error!=CoveSaveError::None;}
};
struct CoveSaveArchive {
    std::unique_ptr<ValidatedRecoveryCheckpoint> current,retiredParent;
    CovePhysicalSave physical{};
};
// Portable bounded bytes and cross-record validation, not a GPU certificate or
// live load. The host captures at the paused joined tick, then on load compiles
// the accepted scene and validates physical/character placement before replacing
// any owner. No adapters, allocations of durable IDs, or storage acknowledgments.
class CoveSaveCodec {
public:
    [[nodiscard]] static bool encode(const ValidatedRecoveryCheckpoint& current,
        const ValidatedRecoveryCheckpoint* retiredParent,const CovePhysicalSave&,
        const CoveSaveContext&,const construction::PartCatalog&,
        std::vector<std::byte>& output,CoveSaveIssue&);
    [[nodiscard]] static std::unique_ptr<CoveSaveArchive> decode(std::span<const std::byte>,
        const CoveSaveContext&,const construction::PartCatalog&,CoveSaveIssue&);
};
} // namespace voxy::game::expedition
