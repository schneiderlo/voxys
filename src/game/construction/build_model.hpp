#pragma once

#include "game/construction/part_catalog.hpp"

namespace voxy::game::construction {

inline constexpr uint32_t kBuildSchemaVersion = 1;
inline constexpr size_t kMaximumBuildParts = 256;
inline constexpr size_t kMaximumBuildConnections = 1024;
inline constexpr size_t kMaximumBuildBytes = 256 * 1024;
inline constexpr size_t kMaximumBuildSolidProxies = 2048;
inline constexpr size_t kMaximumBuildSocketRecords = 8192;
inline constexpr size_t kMaximumBuildCandidatePairs = 262144;
inline constexpr uint16_t kFullHealth = 10000;

enum class PartOrigin : uint8_t { Paid, StarterLoan };
struct PartProvenance {
    PartOrigin origin = PartOrigin::Paid;
    DurableId starterEntitlement{}; // Zero for paid parts; durable entitlement for loans.
    [[nodiscard]] bool operator==(const PartProvenance&) const = default;
};

// Configuration, not live throttle/steering/rope state. Unused fields must be
// zero; changing the definition's physical limits is not a configuration edit.
enum class SettingsKind : uint8_t { Passive, Power, Steering, Winch };
struct ModuleSettings {
    SettingsKind kind = SettingsKind::Passive;
    bool enabled = true;
    uint8_t controlChannel = 0; // 0..15, interpreted by later input/session work.
    uint16_t limitPermille = 0;
    bool reversed = false;
    // Default payout before attachment. An existing rope connection's rest
    // length is authoritative; this default never resets that connected cable.
    uint32_t defaultLineLengthMillimetres = 0;
    [[nodiscard]] bool operator==(const ModuleSettings&) const = default;
};
[[nodiscard]] ModuleSettings defaultModuleSettings(const PartDefinition& definition) noexcept;

struct PartInstance {
    DurableId id{};
    ContentKey definition{};
    GridTransform placement{};
    DurableId owningBuild{};
    uint16_t health = kFullHealth;
    std::array<uint8_t, 4> paint{255, 255, 255, 255}; // sRGB RGBA8 paint override.
    ModuleSettings settings{};
    PartProvenance provenance{};
    [[nodiscard]] bool operator==(const PartInstance&) const = default;
};

struct SocketEndpoint {
    DurableId part{};
    SocketId socket{};
    [[nodiscard]] auto operator<=>(const SocketEndpoint&) const = default;
};

struct Connection {
    DurableId id{};
    SocketEndpoint a{};
    SocketEndpoint b{};
    ConnectionKind kind = ConnectionKind::Weld;
    bool enabled = true;
    uint16_t damage = 0; // 0..10000. Does not run a fracture solver.
    StrengthLimits strength{}; // Positive, bounded by both authored endpoints.
    // Rope-only settings. All zero for Weld/Latch. No rest transform, live
    // tension, merged-body status or floating-point articulated pose is stored.
    uint32_t minimumLengthMillimetres = 0;
    uint32_t maximumLengthMillimetres = 0;
    uint32_t restLengthMillimetres = 0;
};

struct EditLease {
    DurableId holder{};
    AuthorityEpoch epoch{1};
    SimulationTick expiresAfter{};
    [[nodiscard]] bool operator==(const EditLease&) const = default;
};

struct BuildSnapshot {
    DurableId id{};
    TopologyRevision revision{};
    DurableId owner{};
    std::optional<EditLease> editLease{};
    std::vector<PartInstance> parts{};
    std::vector<Connection> connections{};
};

enum class BuildError : uint8_t {
    None, Capacity, InvalidId, DuplicateId, WrongWorld, WrongBuild,
    UnknownDefinition, UnknownDefinitionVersion, InvalidPlacement,
    InvalidHealth, InvalidSettings, InvalidProvenance, InvalidLease,
    UnknownPart, UnknownSocket, SamePartConnection, IncompatibleSocket,
    InvalidConnection, DuplicateConnection, SocketCapacity, MisalignedWeld,
    SolidOverlap, ClearanceBlocked, StaleRevision, RevisionExhausted,
    ImmutableIdentity, InvalidEncoding, UnsupportedSchema, NonCanonicalOrder,
};

struct BuildIssue {
    BuildError error = BuildError::None;
    DurableId object{};
    std::string_view field{}; // Static diagnostic name, never an input lifetime.
    [[nodiscard]] explicit operator bool() const noexcept { return error != BuildError::None; }
};

struct OccupiedSolid {
    DurableId part{};
    ProxyId proxy{};
    GridBox bounds{};
};
struct OccupiedSocket {
    SocketEndpoint endpoint{};
    GridTransform frame{};
    GridBox clearance{};
    uint8_t usedSlots = 0; // Disabled authored links still reserve their slot.
    uint8_t capacity = 0;
};

// Exact signed-permutation bounds; touching faces do not overlap. Unlike the
// catalog authoring helper, this accepts world-sized translations with checked
// integer arithmetic. There is no dense .02 m allocation.
[[nodiscard]] std::optional<GridBox> transformBounds(GridTransform frame, GridBox box) noexcept;
[[nodiscard]] bool solidBoxesOverlap(GridBox a, GridBox b) noexcept;
// Coarse UI offset: local X/Z stud counts and local Y plate counts, then rotate.
// It does not change canonical lattice precision or snap existing socket offsets.
[[nodiscard]] std::optional<GridPosition> coarsePlacementOffset(
    GridPosition localSteps, CubeRotation localAxes = {}) noexcept;

class BuildModel {
public:
    [[nodiscard]] static std::optional<BuildModel> create(
        const BuildSnapshot& draft, const PartCatalog& catalog, BuildIssue& issue);
    [[nodiscard]] const BuildSnapshot& snapshot() const noexcept { return snapshot_; }
    [[nodiscard]] std::span<const OccupiedSolid> solids() const noexcept { return solids_; }
    [[nodiscard]] std::span<const OccupiedSocket> sockets() const noexcept { return sockets_; }
    // Trusted model operation for the later GameSession, not a player command.
    // Caller must authorize ownership/lease/inventory. Existing physical part
    // IDs cannot change definition, owning build or paid/loan provenance here.
    // Removed IDs cannot switch between part/connection in the same edit.
    // Historical reuse after earlier edits requires the session's durable ID
    // allocator/high-water; this bounded model does not retain tombstones.
    // The supplied revision must match expected; success advances it once.
    // Failure and allocation exceptions leave this model unchanged.
    [[nodiscard]] BuildIssue replace(
        TopologyRevision expected, const BuildSnapshot& draft, const PartCatalog& catalog);
    // Trusted cut mutation shared by session authority and fracture preparation.
    // Disable exact enabled welds, preserving all parts and bond identities.
    // One batch advances topology once. No inventory, tool reach or authority.
    [[nodiscard]] BuildIssue cutWelds(TopologyRevision expected,
        std::span<const DurableId> connections, const PartCatalog& catalog);

private:
    BuildModel() = default;
    BuildSnapshot snapshot_{};
    std::vector<OccupiedSolid> solids_{};
    std::vector<OccupiedSocket> sockets_{};
};

// Physical save/exchange codec, not authentication or a save journal. Exact
// content versions resolve against the caller's validated catalog. Bounds are
// checked before count-driven allocation. Encoding sorts records/endpoints;
// decoding rejects noncanonical ordering, trailing bytes and unknown values.
// Both leave output unchanged on validation failure or allocation exceptions.
inline constexpr size_t kBuildHeaderBytes = 113;
inline constexpr size_t kBuildPartBytes = 130;
inline constexpr size_t kBuildConnectionBytes = 136;
[[nodiscard]] BuildIssue encodeBuild(
    const BuildSnapshot& draft, const PartCatalog& catalog, std::vector<std::byte>& output);
[[nodiscard]] BuildIssue decodeBuild(
    std::span<const std::byte> bytes, const PartCatalog& catalog, std::optional<BuildModel>& output);

// Blueprint ordinals are local references (1..part count), never DurableIds.
// This type cannot carry physical authority, condition, origin or entitlement.
struct DesignPart {
    uint32_t ordinal = 0;
    ContentKey definition{};
    GridTransform placement{};
    std::array<uint8_t, 4> paint{255, 255, 255, 255};
    ModuleSettings settings{};
    [[nodiscard]] bool operator==(const DesignPart&) const = default;
};
struct DesignEndpoint {
    uint32_t partOrdinal = 0;
    SocketId socket{};
    [[nodiscard]] auto operator<=>(const DesignEndpoint&) const = default;
};
struct DesignConnection {
    DesignEndpoint a{};
    DesignEndpoint b{};
    ConnectionKind kind = ConnectionKind::Weld;
    bool enabled = true;
    StrengthLimits strength{};
    uint32_t minimumLengthMillimetres = 0;
    uint32_t maximumLengthMillimetres = 0;
    uint32_t restLengthMillimetres = 0;
};
struct BuildBlueprint {
    uint32_t schemaVersion = kBuildSchemaVersion;
    std::vector<DesignPart> parts{};
    std::vector<DesignConnection> connections{};
};

inline constexpr uint32_t kBlueprintSchemaVersion = 1;
inline constexpr size_t kMaximumBlueprintBytes = 128 * 1024;
// Canonical LE SVBP records plus SHA-256. Definition IDs only: no owned IDs,
// provenance, condition, inventory or authority. Failure preserves the output.
[[nodiscard]] BuildIssue encodeBlueprint(const BuildBlueprint&, const PartCatalog&, std::vector<std::byte>&);
[[nodiscard]] BuildIssue decodeBlueprint(std::span<const std::byte>, const PartCatalog&, std::optional<BuildBlueprint>&);

// Copies only design intent from an already validated model. There is no ID
// allocator, inventory mutation, spawn operation or physical import overload.
[[nodiscard]] BuildBlueprint duplicateDesign(const BuildModel& model);

} // namespace voxy::game::construction
