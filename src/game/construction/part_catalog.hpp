#pragma once

#include "game/construction/construction_types.hpp"

#include <functional>
#include <variant>

namespace voxy::game::construction {

inline constexpr uint32_t kPartCatalogSchemaVersion = 1;
inline constexpr size_t kMaximumCatalogDefinitions = 1024;
inline constexpr size_t kMaximumPartBoxes = 32;
inline constexpr size_t kMaximumPartSockets = 128;
inline constexpr size_t kMaximumVisualLods = 8;
inline constexpr int32_t kMaximumPartExtentTicks = 5000; // Authoring bound: 100 m per axis.

struct ContentKey {
    DurableId id{};
    uint32_t version = 1;
    [[nodiscard]] auto operator<=>(const ContentKey&) const = default;
};

struct SocketDefinitionTag;
struct ProxyDefinitionTag;
using SocketId = Counter<SocketDefinitionTag, false>;
using ProxyId = Counter<ProxyDefinitionTag, false>;

// Socket IDs are scoped by the PartDefinition key. A runtime endpoint is
// (part instance ID, socket ID), never a socket's vector index. Proxy IDs are
// scoped by definition AND collection (collision, solidOccupancy, buoyancy).
struct GridBox {
    GridPosition minimum{};
    GridPosition maximum{};
    [[nodiscard]] bool operator==(const GridBox&) const = default;
};

// Half-open analytical box. Touching faces do not overlap solid occupancy.
struct PartBox {
    ProxyId id{};
    GridTransform frame{};
    GridPosition halfExtents{};
    [[nodiscard]] bool operator==(const PartBox&) const = default;
};

struct InertiaTensor {
    std::array<double, 9> elements{}; // Symmetric, row-major, kg m^2 about dry COM.
    [[nodiscard]] bool operator==(const InertiaTensor&) const = default;
};

struct MassProperties {
    double dryMassKg = 0.0;
    MetresPosition localCenterOfMass{};
    InertiaTensor inertia{};
};

struct StrengthLimits {
    double tensionNewtons = 0.0;
    double shearNewtons = 0.0;
    double bendingNewtonMetres = 0.0;
    double torsionNewtonMetres = 0.0;
};

enum class BuoyancyKind : uint8_t { SolidMaterial, SealedCompartment };

struct BuoyancyRegion {
    PartBox box{};
    BuoyancyKind kind = BuoyancyKind::SolidMaterial;
};

enum class SocketFamily : uint8_t { Structural, DriveShaft, TowLine, CargoLatch };
enum class SocketRole : uint8_t { Neutral, Plug, Receptacle };
enum class ConnectionKind : uint8_t { Weld, Rope, Latch };

struct SocketDefinition {
    SocketId id{};
    SocketFamily family = SocketFamily::Structural;
    SocketRole role = SocketRole::Neutral;
    uint32_t profile = 1; // Same family/profile required; profile is stable content data.
    GridTransform frame{}; // +Y points outward; +X is the key/tangent direction.
    uint8_t connectionCapacity = 1;
    // Socket-local clearance volume, not additional solid or buoyancy volume.
    // Occupancy reserves the endpoint slot; DATA-03 handles paired clearances.
    GridBox clearance{};
    StrengthLimits strength{};
};

enum class SocketMatchError : uint8_t {
    None, InvalidSocket, FamilyMismatch, ProfileMismatch, RoleMismatch, ConnectionKindMismatch,
};

// Checks connector types, not world alignment, motion, inventory or occupancy.
[[nodiscard]] SocketMatchError matchSockets(
    const SocketDefinition& a, const SocketDefinition& b, ConnectionKind connection) noexcept;

struct StructureModule {};
struct FlotationModule { std::array<double, 3> dragCoefficients{1.0, 1.0, 1.0}; };
struct EngineModule { SocketId shaft{}; double maximumPowerWatts = 0.0; double maximumTorqueNewtonMetres = 0.0; };
// Thrust acts at forceFrame.translation along its local -Z direction.
struct PropellerModule { SocketId shaft{}; GridTransform forceFrame{}; double maximumThrustNewtons = 0.0; double requiredPowerWatts = 0.0; };
struct HelmModule { GridTransform operatorFrame{}; double maximumSteeringRadians = 0.0; };
struct WinchModule {
    SocketId line{};
    double minimumLengthMetres = 0.0;
    double maximumLengthMetres = 0.0;
    double reelSpeedMetresPerSecond = 0.0;
    double maximumForceNewtons = 0.0;
};
struct TowEyeModule { SocketId eye{}; };
struct CargoCradleModule {
    SocketId latch{};
    double maximumCargoMassKg = 0.0;
    double captureDistanceMetres = 0.10;
    double captureAngleRadians = 0.08726646259971647; // 5 degrees.
    double captureSpeedMetresPerSecond = 0.5;
    double captureAngularSpeedRadiansPerSecond = 0.5235987755982988; // 30 degrees/s.
};
struct BraceModule { double loadTransferFactor = 1.0; };
struct BallastModule {};
struct RepairModule {
    double reachMetres = 0.0;
    double healthFractionPerSecond = 0.0;
    uint32_t materialUnitsPerFullHealth = 0;
};

// No cutting/crane placeholders: add parameter variants with their mechanics.
using PartModule = std::variant<
    StructureModule, FlotationModule, EngineModule, PropellerModule, HelmModule,
    WinchModule, TowEyeModule, CargoCradleModule, BraceModule, BallastModule, RepairModule>;

struct ResourceAmounts {
    uint64_t salvageMaterial = 0;
    uint64_t specialMachinery = 0;
    [[nodiscard]] bool operator==(const ResourceAmounts&) const = default;
};

struct MaterialDefinition {
    std::array<double, 3> linearBaseColor{0.8, 0.7, 0.5};
    double roughness = 0.65;
    double metallic = 0.0;
};

struct PrototypeBoxVisual {
    ContentKey asset{};
    GridBox bounds{}; // Must match the analytical part footprint exactly.
};

struct CookedMeshVisual {
    ContentKey asset{};
    std::string path{}; // Canonical repository/bundle-relative .vmesh path.
};

using VisualAsset = std::variant<PrototypeBoxVisual, CookedMeshVisual>;

struct VisualLod {
    VisualAsset asset{};
    double minimumScreenHeightPixels = 0.0; // Strictly descending; last is zero.
};

struct PartDefinition {
    ContentKey key{};
    std::string nameKey{}; // Stable localization key, not a file path or display name.
    uint32_t permittedRotationMask = 0x00ffffffu;
    GridBox footprint{};
    std::vector<PartBox> solidOccupancy{};
    std::vector<PartBox> collision{}; // Box-only profile 1; no decorative stud contacts.
    MassProperties mass{};
    std::vector<BuoyancyRegion> buoyancy{};
    std::vector<SocketDefinition> sockets{};
    StrengthLimits strength{};
    PartModule module{};
    ResourceAmounts cost{};
    ResourceAmounts salvageYield{}; // Paid-part upper bound; loan provenance overrides to zero.
    MaterialDefinition material{};
    std::vector<VisualLod> visuals{};
};

struct PartCatalogDraft {
    uint32_t schemaVersion = kPartCatalogSchemaVersion;
    std::vector<PartDefinition> definitions{};
};

enum class CatalogError : uint8_t {
    None, UnsupportedSchema, EmptyCatalog, Capacity, InvalidDefinitionId,
    InvalidVersion, DuplicateDefinition, InvalidName, InvalidRotationMask,
    InvalidFootprint, MissingCollision, InvalidBox, DuplicateProxyId,
    InvalidMass, InvalidCenterOfMass, InvalidInertia, InvalidBuoyancy,
    OverlappingBuoyancy, InvalidSocket, DuplicateSocketId, InvalidStrength,
    InvalidModule, MissingModuleSocket, IncompatibleModuleSocket,
    InvalidCost, InvalidSalvageYield, InvalidMaterial, InvalidVisualLod,
    InvalidAssetReference, MissingAsset, PrototypeNotAllowed,
    UnknownDefinition, UnknownVersion,
};

struct CatalogIssue {
    CatalogError error = CatalogError::None;
    size_t definitionIndex = 0; // Index in the input draft, before canonical sorting.
    std::string_view field{}; // Static property name.
    uint64_t elementId = 0;
};

struct CatalogPolicy { bool allowPrototypeVisuals = true; };

// Resolver must confirm the exact content ID/version/path from a known asset
// inventory. Empty resolver rejects every cooked asset; there is no fallback.
// Format/digest validation belongs to the cooker/manifest supplying this lookup.
using CookedAssetResolver = std::function<bool(const CookedMeshVisual&)>;

struct PartLookup {
    const PartDefinition* definition = nullptr;
    CatalogError error = CatalogError::UnknownDefinition;
    [[nodiscard]] explicit operator bool() const noexcept { return definition != nullptr; }
};

class PartCatalog {
public:
    [[nodiscard]] static std::optional<PartCatalog> create(
        const PartCatalogDraft& draft, CatalogIssue& issue,
        const CookedAssetResolver& resolver = {}, CatalogPolicy policy = {});
    [[nodiscard]] std::span<const PartDefinition> definitions() const noexcept { return definitions_; }
    [[nodiscard]] PartLookup lookup(ContentKey key) const noexcept;

private:
    PartCatalog() = default;
    std::vector<PartDefinition> definitions_{};
};

[[nodiscard]] const SocketDefinition* findSocket(const PartDefinition& part, SocketId id) noexcept;
[[nodiscard]] std::optional<GridBox> boxBounds(const PartBox& box) noexcept;
[[nodiscard]] std::optional<double> boxVolumeCubicMetres(const PartBox& box) noexcept;
[[nodiscard]] bool physicallyValidInertia(const InertiaTensor& inertia) noexcept;
[[nodiscard]] ContentKey prototypeBoxAsset() noexcept;
[[nodiscard]] bool prototypeAssetExists(const PrototypeBoxVisual& asset) noexcept;

enum class StarterPart : uint8_t {
    Beam = 1, Plate, Pontoon, Engine, Propeller, Helm, Winch,
    TowEye, CargoCradle, Brace, Ballast, RepairModule,
};

[[nodiscard]] ContentKey starterPartKey(StarterPart part) noexcept;
// Twelve initial definitions, all explicit procedural-box prototypes. These are
// functional authoring defaults, not measured handling or final render assets.
[[nodiscard]] PartCatalogDraft makeStarterCatalogDraft();

} // namespace voxy::game::construction
