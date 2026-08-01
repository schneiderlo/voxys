#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace voxy::network {

// This schema versions the WRECKWATER payload itself. It is independent from
// the outer PacketCodec version and from any native/browser transport framing.
inline constexpr uint32_t kWreckwaterWireSchemaVersion = 5u;
inline constexpr uint32_t kWreckwaterCertifiedFullSnapshotFlag = 1u;

inline constexpr size_t kWreckwaterActionRequestBytes = 64u;
inline constexpr uint32_t
    kWreckwaterCharacterInputMaximumRedundantSamples = 3u;
inline constexpr uint32_t
    kWreckwaterCharacterInputMaximumSamplesPerRequest =
        kWreckwaterCharacterInputMaximumRedundantSamples + 1u;
inline constexpr size_t kWreckwaterCharacterInputSampleBytes = 24u;
inline constexpr size_t kWreckwaterCharacterInputRequestBytes = 144u;
static_assert(kWreckwaterCharacterInputMaximumSamplesPerRequest == 4u);
static_assert(
    kWreckwaterCharacterInputRequestBytes ==
    64u + 8u +
        kWreckwaterCharacterInputMaximumRedundantSamples *
            kWreckwaterCharacterInputSampleBytes);
inline constexpr size_t kWreckwaterSnapshotHeaderBytes = 128u;
inline constexpr size_t kWreckwaterEntityStateBytes = 176u;
inline constexpr size_t kWreckwaterCharacterStateBytes = 96u;
inline constexpr size_t kWreckwaterSnapshotByteHashOffset = 120u;
inline constexpr uint32_t kWreckwaterMaximumSnapshotEntities = 64u;
inline constexpr uint32_t kWreckwaterMaximumSnapshotCharacters = 4u;
inline constexpr uint32_t kWreckwaterFirstSliceEntityCount = 3u;
inline constexpr uint32_t kWreckwaterFirstSliceCharacterCount = 4u;
inline constexpr size_t kWreckwaterFirstSliceSnapshotBytes =
    kWreckwaterSnapshotHeaderBytes +
    kWreckwaterFirstSliceEntityCount * kWreckwaterEntityStateBytes +
    kWreckwaterFirstSliceCharacterCount * kWreckwaterCharacterStateBytes;

// PacketCodec currently contributes 84 bytes to a 1,200-byte realtime frame.
// The focused test also compares these proof constants with PacketCodec so a
// future outer-frame change cannot silently invalidate this result.
inline constexpr size_t kWreckwaterRealtimeMtuProofBytes = 1'200u;
inline constexpr size_t kWreckwaterOuterPacketProofBytes = 84u;
static_assert(kWreckwaterFirstSliceSnapshotBytes == 1'040u);
static_assert(kWreckwaterFirstSliceSnapshotBytes +
                  kWreckwaterOuterPacketProofBytes <=
              kWreckwaterRealtimeMtuProofBytes);

inline constexpr uint64_t kWreckwaterMaximumApplicationTick = 5'184'000u;
inline constexpr uint64_t kWreckwaterMaximumPhysicsEvidenceLagTicks = 64u;
inline constexpr uint32_t kWreckwaterMaximumScore = 1'000'000u;
inline constexpr int32_t kWreckwaterMaximumSectorMagnitude = 1'000'000;
inline constexpr float kWreckwaterSectorLocalMinimum = -128.0f;
inline constexpr float kWreckwaterSectorLocalMaximum = 128.0f;
inline constexpr float kWreckwaterMaximumLinearVelocity = 500.0f;
inline constexpr float kWreckwaterMaximumAngularVelocity = 100.0f;
inline constexpr float kWreckwaterMaximumCharacterVelocity = 8'192.0f;
inline constexpr float kWreckwaterMinimumDimension = 0.0001f;
inline constexpr float kWreckwaterMaximumDimension = 128.0f;
inline constexpr int16_t kWreckwaterHelmAxisMinimum = -32'767;
inline constexpr int16_t kWreckwaterHelmAxisMaximum = 32'767;

using NetEntityId = uint64_t;
using LogicalAttachmentId = uint64_t;

enum class WreckwaterPayloadType : uint32_t {
  ActionRequest = 1u,
  CertifiedSnapshot = 2u,
  CharacterInputRequest = 3u,
};

enum class WreckwaterAction : uint32_t {
  Tow = 1u,
  Cut = 2u,
  Steal = 3u,
  Bank = 4u,
  Helm = 5u,
};

enum class WreckwaterCrew : uint32_t {
  None = 0u,
  CrewOne = 1u,
  CrewTwo = 2u,
};

enum class WreckwaterPhase : uint32_t {
  Warmup = 0u,
  Live = 1u,
  Overtime = 2u,
  Finished = 3u,
};

enum class WreckwaterOutcomeType : uint32_t {
  Undecided = 0u,
  CrewVictory = 1u,
  Tie = 2u,
};

enum class WreckwaterEntityKind : uint32_t {
  Skiff = 1u,
  Cargo = 2u,
};

// These values describe network geometry. They are not GPU shape handles.
enum class WreckwaterShape : uint32_t {
  Sphere = 1u,
  Box = 2u,
  Capsule = 3u,
  Cylinder = 4u,
  ConvexHull = 5u,
  Compound = 6u,
};

enum class WreckwaterCargoDisposition : uint32_t {
  NotApplicable = 0u,
  Free = 1u,
  Towed = 2u,
  Banked = 3u,
  Lost = 4u,
};

enum class WreckwaterSkiffDisposition : uint32_t {
  NotApplicable = 0u,
  Active = 1u,
  Sunk = 2u,
};

enum class WreckwaterAttachmentState : uint32_t {
  None = 0u,
  Attached = 1u,
};

enum class WreckwaterCharacterMode : uint32_t {
  Airborne = 0u,
  OnSkiff = 1u,
  Swimming = 2u,
};

inline constexpr uint32_t kWreckwaterCharacterInputJumpFlag = 1u << 0u;
inline constexpr uint32_t kWreckwaterCharacterInputBoardFlag = 1u << 1u;
inline constexpr uint32_t kWreckwaterCharacterInputKnownFlags =
    kWreckwaterCharacterInputJumpFlag |
    kWreckwaterCharacterInputBoardFlag;

inline constexpr uint32_t kWreckwaterCharacterStateActiveFlag = 1u << 8u;
inline constexpr uint32_t kWreckwaterCharacterStateConnectedFlag = 1u << 9u;

struct WreckwaterVec3 {
  float x = 0.0f;
  float y = 0.0f;
  float z = 0.0f;

  [[nodiscard]] bool operator==(const WreckwaterVec3 &) const = default;
};

// Serialized component order is x, y, z, w. Canonical sign selection examines
// w first, then x, y, z, and makes the first nonzero component positive.
struct WreckwaterQuaternion {
  float x = 0.0f;
  float y = 0.0f;
  float z = 0.0f;
  float w = 1.0f;

  [[nodiscard]] bool operator==(const WreckwaterQuaternion &) const = default;
};

struct WreckwaterActionRequest {
  uint32_t schemaVersion = kWreckwaterWireSchemaVersion;
  uint64_t requestedApplicationTick = 0u;
  uint64_t clientRequestSequence = 0u;
  WreckwaterAction action = WreckwaterAction::Tow;
  uint32_t cargoId = 0u;
  uint32_t cargoGeneration = 0u;
  uint32_t observedCargoRevision = 0u;
  int16_t helmThrottleQ15 = 0;
  int16_t helmSteeringQ15 = 0;
  uint32_t controlReserved = 0u;

  [[nodiscard]] bool
  operator==(const WreckwaterActionRequest &) const = default;
};

struct WreckwaterCharacterInputSample {
  uint64_t requestedApplicationTick = 0u;
  uint64_t characterInputSequence = 0u;
  int16_t moveXQ15 = 0;
  int16_t moveZQ15 = 0;
  uint32_t inputFlags = 0u;

  [[nodiscard]] bool
  operator==(const WreckwaterCharacterInputSample &) const = default;
};

// A fixed-size, peer-authenticated movement bundle. Player identity comes
// from the transport roster. The opaque character handle and connection
// generation fence stale packets across lifecycle transitions. The primary
// fields carry the newest logical sample. The fixed tail carries up to three
// exact older samples, in increasing tick and sequence order.
struct WreckwaterCharacterInputRequest {
  uint32_t schemaVersion = kWreckwaterWireSchemaVersion;
  uint64_t requestedApplicationTick = 0u;
  // Orders the outer request stream across actions and movement. This must
  // match PacketHeader::sequence but is not the movement acknowledgement.
  uint64_t clientRequestSequence = 0u;
  uint32_t characterHandle = 0u;
  uint32_t connectionGeneration = 0u;
  int16_t moveXQ15 = 0;
  int16_t moveZQ15 = 0;
  uint32_t inputFlags = 0u;
  // Restarts at one for each authoritative connection generation. The
  // authority uses only this sequence to order and acknowledge movement.
  uint64_t characterInputSequence = 0u;
  uint32_t redundantInputCount = 0u;
  std::array<
      WreckwaterCharacterInputSample,
      kWreckwaterCharacterInputMaximumRedundantSamples>
      redundantInputs{};

  [[nodiscard]] bool
  operator==(const WreckwaterCharacterInputRequest &) const = default;
};

struct WreckwaterCargoLogicalState {
  uint32_t cargoId = 0u;
  uint32_t generation = 0u;
  uint32_t revision = 0u;
  WreckwaterCargoDisposition disposition =
      WreckwaterCargoDisposition::NotApplicable;
  WreckwaterCrew ownerCrew = WreckwaterCrew::None;
  uint32_t towingSkiffId = 0u;
  uint32_t towingSkiffGeneration = 0u;

  [[nodiscard]] bool
  operator==(const WreckwaterCargoLogicalState &) const = default;
};

struct WreckwaterSkiffLogicalState {
  uint32_t skiffId = 0u;
  uint32_t generation = 0u;
  WreckwaterSkiffDisposition disposition =
      WreckwaterSkiffDisposition::NotApplicable;

  [[nodiscard]] bool
  operator==(const WreckwaterSkiffLogicalState &) const = default;
};

// This is a stable network identity assigned by an application registry. It
// must never contain a physics::AttachmentHandle index or generation.
struct WreckwaterAttachmentLogicalState {
  LogicalAttachmentId attachmentId = 0u;
  uint32_t generation = 0u;
  WreckwaterAttachmentState state = WreckwaterAttachmentState::None;

  [[nodiscard]] bool
  operator==(const WreckwaterAttachmentLogicalState &) const = default;
};

struct WreckwaterEntityState {
  NetEntityId netEntityId = 0u;
  uint32_t netGeneration = 0u;
  WreckwaterEntityKind kind = WreckwaterEntityKind::Skiff;
  WreckwaterCrew crew = WreckwaterCrew::None;
  std::array<int32_t, 3> sector{};
  WreckwaterVec3 localPosition{};
  WreckwaterQuaternion orientation{};
  WreckwaterVec3 linearVelocity{};
  WreckwaterVec3 angularVelocity{};
  WreckwaterShape shape = WreckwaterShape::Box;
  WreckwaterVec3 dimensions{1.0f, 1.0f, 1.0f};
  uint32_t packedMaterialFlags = 0u;
  WreckwaterCargoLogicalState cargo{};
  WreckwaterSkiffLogicalState skiff{};
  WreckwaterAttachmentLogicalState attachment{};
  std::array<uint32_t, 4> reserved{};

  [[nodiscard]] bool operator==(const WreckwaterEntityState &) const = default;
};

// Compact certified character state. stateFlags stores the mode in bits
// [0, 1], plus Active and Connected. The 96-byte record keeps the complete
// 2v2 snapshot below the conservative 1,200-byte realtime MTU.
struct WreckwaterCharacterState {
  uint32_t characterHandle = 0u;
  uint32_t stateFlags = 0u;
  uint64_t playerId = 0u;
  uint32_t connectionGeneration = 0u;
  std::array<int32_t, 3> sector{};
  WreckwaterVec3 localFeetPosition{};
  WreckwaterVec3 worldVelocity{};
  uint32_t skiffId = 0u;
  uint32_t skiffGeneration = 0u;
  WreckwaterVec3 skiffLocalFeetPosition{};
  WreckwaterVec3 skiffLocalVelocity{};
  // Per authoritative connection generation. This acknowledges movement
  // input only; it is intentionally unrelated to outer packet ordering.
  uint64_t lastAppliedCharacterInputSequence = 0u;

  [[nodiscard]] bool
  operator==(const WreckwaterCharacterState &) const = default;
};

struct WreckwaterCertifiedSnapshot {
  uint32_t schemaVersion = kWreckwaterWireSchemaVersion;
  uint32_t flags = kWreckwaterCertifiedFullSnapshotFlag;
  uint64_t sessionId = 0u;
  uint64_t matchId = 0u;
  uint64_t worldId = 0u;
  uint32_t worldEpoch = 0u;
  uint32_t authorityEpoch = 0u;
  uint64_t snapshotSequence = 0u;
  uint64_t applicationTick = 0u;
  uint64_t physicsEvidenceTick = 0u;
  WreckwaterPhase phase = WreckwaterPhase::Warmup;
  uint32_t crewOneScore = 0u;
  uint32_t crewTwoScore = 0u;
  WreckwaterOutcomeType outcome = WreckwaterOutcomeType::Undecided;
  WreckwaterCrew winner = WreckwaterCrew::None;
  uint32_t matchStateHash = 0u;
  uint32_t eventStreamHash = 0u;
  std::array<uint32_t, 1> reserved{};
  uint64_t serializedByteHash = 0u;
  std::vector<WreckwaterEntityState> entities;
  std::vector<WreckwaterCharacterState> characters;

  [[nodiscard]] bool
  operator==(const WreckwaterCertifiedSnapshot &) const = default;
};

enum class WreckwaterCodecError : uint32_t {
  None = 0u,
  InvalidArgument,
  InvalidMagic,
  UnsupportedVersion,
  WrongPayloadType,
  Truncated,
  TrailingBytes,
  LengthMismatch,
  ReservedNonZero,
  InvalidAction,
  InvalidControl,
  InvalidIdentity,
  InvalidTick,
  InvalidPhase,
  InvalidOutcome,
  InvalidCount,
  SizeOverflow,
  InvalidEntity,
  InvalidFloat,
  InvalidQuaternion,
  UnsortedEntities,
  DuplicateEntity,
  HashMismatch,
};

struct WreckwaterWriteResult {
  std::vector<std::byte> bytes;
  WreckwaterCodecError error = WreckwaterCodecError::None;
  uint64_t serializedByteHash = 0u;

  [[nodiscard]] explicit operator bool() const noexcept {
    return error == WreckwaterCodecError::None && !bytes.empty();
  }
};

struct WreckwaterActionReadResult {
  std::optional<WreckwaterActionRequest> request;
  WreckwaterCodecError error = WreckwaterCodecError::None;

  [[nodiscard]] explicit operator bool() const noexcept {
    return request.has_value();
  }
};

struct WreckwaterCharacterInputReadResult {
  std::optional<WreckwaterCharacterInputRequest> request;
  WreckwaterCodecError error = WreckwaterCodecError::None;

  [[nodiscard]] explicit operator bool() const noexcept {
    return request.has_value();
  }
};

struct WreckwaterSnapshotReadResult {
  std::optional<WreckwaterCertifiedSnapshot> snapshot;
  WreckwaterCodecError error = WreckwaterCodecError::None;

  [[nodiscard]] explicit operator bool() const noexcept {
    return snapshot.has_value();
  }
};

[[nodiscard]] const char *
wreckwaterCodecErrorName(WreckwaterCodecError error) noexcept;

[[nodiscard]] float canonicalWreckwaterFloat(float value) noexcept;
[[nodiscard]] bool isCanonicalWreckwaterFloat(float value) noexcept;
[[nodiscard]] bool
canonicalizeWreckwaterQuaternion(WreckwaterQuaternion &quaternion) noexcept;
[[nodiscard]] bool isCanonicalWreckwaterQuaternion(
    const WreckwaterQuaternion &quaternion) noexcept;
[[nodiscard]] bool
canonicalizeWreckwaterEntityState(WreckwaterEntityState &entity) noexcept;
[[nodiscard]] bool
isCanonicalWreckwaterEntityState(const WreckwaterEntityState &entity) noexcept;
[[nodiscard]] bool canonicalizeWreckwaterCharacterState(
    WreckwaterCharacterState &character) noexcept;
[[nodiscard]] bool isCanonicalWreckwaterCharacterState(
    const WreckwaterCharacterState &character) noexcept;
[[nodiscard]] bool
canonicalizeWreckwaterSnapshot(WreckwaterCertifiedSnapshot &snapshot);
[[nodiscard]] bool isCanonicalWreckwaterSnapshot(
    const WreckwaterCertifiedSnapshot &snapshot) noexcept;

[[nodiscard]] bool wreckwaterSnapshotPayloadBytes(
    size_t entityCount, size_t characterCount, size_t &bytes) noexcept;

// FNV-1a-64 is a deterministic corruption/convergence fingerprint, not a MAC.
[[nodiscard]] uint64_t
wreckwaterSerializedByteHash(std::span<const std::byte> bytes) noexcept;

// Hashes the entire snapshot payload while treating the eight-byte hash field
// at [120, 128) as zero. This avoids a self-referential wire value.
[[nodiscard]] uint64_t
wreckwaterSnapshotSerializedByteHash(std::span<const std::byte> bytes) noexcept;

// Verifies the complete canonical header and every entity without allocating.
// This is intended for fixed-tick consumers that already own encoded bytes.
[[nodiscard]] bool wreckwaterSnapshotMatchesCanonicalBytes(
    const WreckwaterCertifiedSnapshot &snapshot,
    std::span<const std::byte> bytes) noexcept;

// Performs complete hostile-input validation with fixed stack storage.
[[nodiscard]] WreckwaterCodecError
validateWreckwaterSnapshotBytes(
    std::span<const std::byte> bytes) noexcept;

class WreckwaterActionRequestCodec {
public:
  [[nodiscard]] static WreckwaterWriteResult
  encode(const WreckwaterActionRequest &request);
  [[nodiscard]] static WreckwaterActionReadResult
  decode(std::span<const std::byte> bytes) noexcept;
};

class WreckwaterCharacterInputRequestCodec {
public:
  [[nodiscard]] static WreckwaterWriteResult
  encode(const WreckwaterCharacterInputRequest &request);
  [[nodiscard]] static WreckwaterCharacterInputReadResult
  decode(std::span<const std::byte> bytes) noexcept;
};

class WreckwaterSnapshotCodec {
public:
  [[nodiscard]] static WreckwaterWriteResult
  encode(const WreckwaterCertifiedSnapshot &snapshot);
  [[nodiscard]] static WreckwaterSnapshotReadResult
  decode(std::span<const std::byte> bytes);
};

} // namespace voxy::network
