#pragma once

#include "geometry/grid_types.hpp"

#include <array>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace voxy::game::construction {

inline constexpr int32_t kTicksPerMetre = 50;
inline constexpr int32_t kStudTicks = 50;
inline constexpr int32_t kPlateTicks = 16;
inline constexpr int32_t kBrickBodyTicks = 48;
inline constexpr int32_t kStudInsertionTicks = 9;
inline constexpr int32_t kMaximumGridCoordinate =
    std::numeric_limits<int32_t>::max();
inline constexpr double kLatticeToleranceMetres = 1.0e-8;

// INT32_MIN is excluded so negation and all proper rotations preserve the range.
using GridPosition = geometry::GridPosition;

struct MetresPosition {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
    [[nodiscard]] bool operator==(const MetresPosition&) const = default;
};

// Serialized IDs are frozen in schema 1. Columns are transformed local axes.
// Enumerate local X over +X,+Y,+Z,-X,-Y,-Z, then local Y in that same order,
// skipping parallel axes; local Z = X cross Y. ID 0 is identity.
struct CubeRotation {
    uint8_t value = 0;
    [[nodiscard]] bool operator==(const CubeRotation&) const = default;
};

struct RotationMatrix {
    std::array<int8_t, 9> elements{}; // Row-major; column vectors.
    [[nodiscard]] bool operator==(const RotationMatrix&) const = default;
};

struct GridTransform {
    GridPosition translation{};
    CubeRotation rotation{};
    [[nodiscard]] bool operator==(const GridTransform&) const = default;
};

[[nodiscard]] bool isValid(GridPosition position) noexcept;
[[nodiscard]] bool isValid(CubeRotation rotation) noexcept;
[[nodiscard]] std::optional<RotationMatrix> rotationMatrix(CubeRotation rotation) noexcept;
[[nodiscard]] std::optional<GridPosition> checkedAdd(GridPosition a, GridPosition b) noexcept;
[[nodiscard]] std::optional<GridPosition> checkedSubtract(GridPosition a, GridPosition b) noexcept;
[[nodiscard]] std::optional<GridPosition> rotate(CubeRotation rotation, GridPosition position) noexcept;
[[nodiscard]] std::optional<GridPosition> transformPosition(GridTransform transform, GridPosition position) noexcept;
// Composition applies b first, then a. Overflow is an error, never saturation.
[[nodiscard]] std::optional<CubeRotation> compose(CubeRotation a, CubeRotation b) noexcept;
[[nodiscard]] std::optional<GridTransform> compose(GridTransform a, GridTransform b) noexcept;
[[nodiscard]] std::optional<CubeRotation> inverse(CubeRotation rotation) noexcept;
[[nodiscard]] std::optional<GridTransform> inverse(GridTransform transform) noexcept;
[[nodiscard]] std::optional<MetresPosition> toMetres(GridPosition position) noexcept;
[[nodiscard]] std::optional<GridPosition> gridFromMetres(MetresPosition position) noexcept;
// Intentional UI operation: nearest lattice point, half ticks away from zero.
[[nodiscard]] std::optional<GridPosition> snapToGrid(MetresPosition position) noexcept;

enum class SourceFrame : uint8_t {
    Canonical,
    RawBlenderZUpMinusYForward,
    ExportedGltf,
};

// A glTF caller MUST provide its recorded exporter-to-canonical rotation.
// Canonical/raw Blender callers MUST NOT provide a second conversion.
[[nodiscard]] std::optional<CubeRotation> basisToCanonical(
    SourceFrame source, std::optional<CubeRotation> gltfToCanonical = std::nullopt) noexcept;
[[nodiscard]] std::optional<MetresPosition> toCanonicalFrame(
    MetresPosition position, SourceFrame source,
    std::optional<CubeRotation> gltfToCanonical = std::nullopt) noexcept;

struct CanonicalQuaternion {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float w = 1.0f;
    [[nodiscard]] bool operator==(const CanonicalQuaternion&) const = default;
};

inline constexpr double kQuaternionUnitTolerance = 2.0e-6;
// Normalize at the input boundary. Serialization validates, never renormalizes.
// Sign: first nonzero of w,x,y,z is positive. Zero/subnormal outputs become +0.
[[nodiscard]] std::optional<CanonicalQuaternion> canonicalQuaternion(
    double x, double y, double z, double w) noexcept;
[[nodiscard]] bool isCanonical(CanonicalQuaternion quaternion) noexcept;
[[nodiscard]] std::optional<std::array<std::byte, 16>> encodeQuaternion(
    CanonicalQuaternion quaternion) noexcept;
[[nodiscard]] std::optional<CanonicalQuaternion> decodeQuaternion(
    std::span<const std::byte> bytes) noexcept;

struct WorldNamespace {
    std::array<uint8_t, 16> bytes{};
    [[nodiscard]] auto operator<=>(const WorldNamespace&) const = default;
};

struct DurableId {
    WorldNamespace world{};
    uint64_t counter = 0;
    [[nodiscard]] auto operator<=>(const DurableId&) const = default;
};

[[nodiscard]] bool isValid(WorldNamespace world) noexcept;
[[nodiscard]] bool isValid(DurableId id) noexcept;

// Single-owner, not thread-safe. The session owns namespace generation and
// durable high-water checkpoints; never create concurrent owners for one world.
// Restore only after validating the checkpoint against all existing IDs.
class IdAllocator {
public:
    explicit IdAllocator(WorldNamespace world, uint64_t lastIssued = 0) noexcept;
    IdAllocator(const IdAllocator&) = delete;
    IdAllocator& operator=(const IdAllocator&) = delete;
    IdAllocator(IdAllocator&&) = delete;
    IdAllocator& operator=(IdAllocator&&) = delete;
    [[nodiscard]] std::optional<DurableId> allocate() noexcept;
    [[nodiscard]] uint64_t lastIssued() const noexcept { return lastIssued_; }

private:
    WorldNamespace world_{};
    uint64_t lastIssued_ = 0;
};

template <typename Tag, bool ZeroIsValid>
class Counter {
public:
    constexpr Counter() = default;
    explicit constexpr Counter(uint64_t value) noexcept : value_(value) {}
    [[nodiscard]] constexpr uint64_t value() const noexcept { return value_; }
    [[nodiscard]] constexpr bool valid() const noexcept { return ZeroIsValid || value_ != 0; }
    [[nodiscard]] auto operator<=>(const Counter&) const = default;

private:
    uint64_t value_ = 0;
};

struct SimulationTickTag;
struct TopologyRevisionTag;
struct AuthorityEpochTag;
struct RequestSequenceTag;
using SimulationTick = Counter<SimulationTickTag, true>;
using TopologyRevision = Counter<TopologyRevisionTag, true>;
using AuthorityEpoch = Counter<AuthorityEpochTag, false>;
using RequestSequence = Counter<RequestSequenceTag, false>;

// Revision/tick 0 describe initial state. Epoch/request 0 are invalid sentinels.
// Explicit initialization to 1 is required before advancing epoch/request.
template <typename Tag, bool ZeroIsValid>
[[nodiscard]] constexpr std::optional<Counter<Tag, ZeroIsValid>> next(
    Counter<Tag, ZeroIsValid> current) noexcept {
    if (!current.valid() || current.value() == std::numeric_limits<uint64_t>::max()) {
        return std::nullopt;
    }
    return Counter<Tag, ZeroIsValid>{current.value() + 1};
}

// Canonical decimal: "0" or a nonzero digit followed by digits; no whitespace,
// signs, exponents or leading zeroes. JSON MUST use strings, never JS Numbers.
[[nodiscard]] std::string u64ToDecimal(uint64_t value);
[[nodiscard]] std::optional<uint64_t> u64FromDecimal(std::string_view text) noexcept;

struct PlacementRecord {
    DurableId id{};
    GridTransform placement{};
    [[nodiscard]] bool operator==(const PlacementRecord&) const = default;
};

// Foundation exchange only, NOT a complete BuildModel, command, or save file.
struct PlacementEnvelope {
    SimulationTick tick{};
    TopologyRevision revision{};
    AuthorityEpoch epoch{1};
    RequestSequence sequence{1};
    std::vector<PlacementRecord> records{};
    [[nodiscard]] bool operator==(const PlacementEnvelope&) const = default;
};

inline constexpr uint32_t kPlacementSchemaVersion = 1;
inline constexpr size_t kMaximumPlacementRecords = 65536;
inline constexpr size_t kPlacementHeaderBytes = 44;
inline constexpr size_t kPlacementRecordBytes = 37;

enum class CodecError : uint8_t {
    None,
    InvalidValue,
    DuplicateId,
    RecordCapacity,
    InvalidEncoding,
    UnsupportedSchema,
    NonCanonicalOrder,
};

// Wire layout: ASCII SVCP, u32 schema, four u64 counters in declaration order,
// u32 count, then records: namespace[16], u64 ID, i32 x/y/z, u8 rotation.
// All numeric fields are little-endian; records sort by namespace then counter.
// Both operations leave output unchanged on validation failure. They allocate;
// allocation failure follows standard vector behavior, not CodecError.
[[nodiscard]] CodecError encodePlacements(
    const PlacementEnvelope& envelope, std::vector<std::byte>& output);
[[nodiscard]] CodecError decodePlacements(
    std::span<const std::byte> bytes, PlacementEnvelope& output);

} // namespace voxy::game::construction
