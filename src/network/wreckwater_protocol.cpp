#include "network/wreckwater_protocol.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <limits>
#include <tuple>

namespace voxy::network {
namespace {

constexpr std::array<std::byte, 8> kActionMagic{
    std::byte{'V'}, std::byte{'O'}, std::byte{'X'}, std::byte{'Y'},
    std::byte{'W'}, std::byte{'R'}, std::byte{'E'}, std::byte{'Q'}};
constexpr std::array<std::byte, 8> kCharacterInputMagic{
    std::byte{'V'}, std::byte{'O'}, std::byte{'X'}, std::byte{'Y'},
    std::byte{'W'}, std::byte{'C'}, std::byte{'I'}, std::byte{'N'}};
constexpr std::array<std::byte, 8> kSnapshotMagic{
    std::byte{'V'}, std::byte{'O'}, std::byte{'X'}, std::byte{'Y'},
    std::byte{'W'}, std::byte{'S'}, std::byte{'N'}, std::byte{'P'}};
constexpr uint64_t kFnv64Offset = 14'695'981'039'346'656'037ull;
constexpr uint64_t kFnv64Prime = 1'099'511'628'211ull;
constexpr float kMaximumQuaternionInputComponent = 16.0f;
constexpr double kMinimumQuaternionLengthSquared = 1.0e-12;
constexpr double kQuaternionUnitTolerance = 2.0e-5;
constexpr uint32_t kWreckwaterCharacterModeMask = 0x3u;
constexpr uint32_t kWreckwaterCharacterStateKnownFlags =
    kWreckwaterCharacterModeMask |
    kWreckwaterCharacterStateActiveFlag |
    kWreckwaterCharacterStateConnectedFlag;

class Writer {
public:
  explicit Writer(size_t capacity) { bytes_.reserve(capacity); }

  void raw(std::span<const std::byte> bytes) {
    bytes_.insert(bytes_.end(), bytes.begin(), bytes.end());
  }

  void u32(uint32_t value) {
    for (uint32_t shift = 0u; shift < 32u; shift += 8u) {
      bytes_.push_back(std::byte{static_cast<uint8_t>(value >> shift)});
    }
  }

  void i16(int16_t value) {
    const uint16_t bits = std::bit_cast<uint16_t>(value);
    bytes_.push_back(std::byte{static_cast<uint8_t>(bits)});
    bytes_.push_back(std::byte{static_cast<uint8_t>(bits >> 8u)});
  }

  void i32(int32_t value) { u32(std::bit_cast<uint32_t>(value)); }

  void u64(uint64_t value) {
    u32(static_cast<uint32_t>(value));
    u32(static_cast<uint32_t>(value >> 32u));
  }

  void f32(float value) { u32(std::bit_cast<uint32_t>(value)); }

  [[nodiscard]] std::vector<std::byte> &bytes() noexcept { return bytes_; }

private:
  std::vector<std::byte> bytes_;
};

class Reader {
public:
  explicit Reader(std::span<const std::byte> bytes) : bytes_(bytes) {}

  [[nodiscard]] bool skip(size_t count) noexcept {
    if (count > remaining())
      return false;
    offset_ += count;
    return true;
  }

  [[nodiscard]] bool u32(uint32_t &value) noexcept {
    if (remaining() < sizeof(uint32_t))
      return false;
    value = 0u;
    for (uint32_t byte = 0u; byte < 4u; ++byte) {
      value |= std::to_integer<uint32_t>(bytes_[offset_ + byte]) << (byte * 8u);
    }
    offset_ += sizeof(uint32_t);
    return true;
  }

  [[nodiscard]] bool i32(int32_t &value) noexcept {
    uint32_t bits = 0u;
    if (!u32(bits))
      return false;
    value = std::bit_cast<int32_t>(bits);
    return true;
  }

  [[nodiscard]] bool i16(int16_t &value) noexcept {
    if (remaining() < sizeof(uint16_t))
      return false;
    const uint32_t bits =
        std::to_integer<uint32_t>(bytes_[offset_]) |
        (std::to_integer<uint32_t>(bytes_[offset_ + 1u]) << 8u);
    offset_ += sizeof(uint16_t);
    value = std::bit_cast<int16_t>(static_cast<uint16_t>(bits));
    return true;
  }

  [[nodiscard]] bool u64(uint64_t &value) noexcept {
    uint32_t low = 0u;
    uint32_t high = 0u;
    if (!u32(low) || !u32(high))
      return false;
    value = uint64_t{low} | (uint64_t{high} << 32u);
    return true;
  }

  [[nodiscard]] bool f32(float &value) noexcept {
    uint32_t bits = 0u;
    if (!u32(bits))
      return false;
    value = std::bit_cast<float>(bits);
    return true;
  }

  [[nodiscard]] size_t remaining() const noexcept {
    return bytes_.size() - offset_;
  }

private:
  std::span<const std::byte> bytes_;
  size_t offset_ = 0u;
};

[[nodiscard]] bool allZero(std::span<const uint32_t> values) noexcept {
  return std::all_of(values.begin(), values.end(),
                     [](uint32_t value) { return value == 0u; });
}

[[nodiscard]] bool validAction(WreckwaterAction action) noexcept {
  switch (action) {
  case WreckwaterAction::Tow:
  case WreckwaterAction::Cut:
  case WreckwaterAction::Steal:
  case WreckwaterAction::Bank:
  case WreckwaterAction::Helm:
    return true;
  }
  return false;
}

[[nodiscard]] bool validCharacterMode(
    WreckwaterCharacterMode mode) noexcept {
  return mode == WreckwaterCharacterMode::Airborne ||
         mode == WreckwaterCharacterMode::OnSkiff ||
         mode == WreckwaterCharacterMode::Swimming;
}

[[nodiscard]] WreckwaterCodecError validateCharacterInputSample(
    const WreckwaterCharacterInputSample &sample) noexcept {
  if (sample.requestedApplicationTick == 0u ||
      sample.requestedApplicationTick >
          kWreckwaterMaximumApplicationTick) {
    return WreckwaterCodecError::InvalidTick;
  }
  if (sample.characterInputSequence == 0u) {
    return WreckwaterCodecError::InvalidIdentity;
  }
  if (sample.moveXQ15 < kWreckwaterHelmAxisMinimum ||
      sample.moveZQ15 < kWreckwaterHelmAxisMinimum ||
      (sample.inputFlags & ~kWreckwaterCharacterInputKnownFlags) != 0u) {
    return WreckwaterCodecError::InvalidControl;
  }
  return WreckwaterCodecError::None;
}

[[nodiscard]] bool zeroCharacterInputSample(
    const WreckwaterCharacterInputSample &sample) noexcept {
  return sample == WreckwaterCharacterInputSample{};
}

[[nodiscard]] WreckwaterCodecError validateCharacterInputRequest(
    const WreckwaterCharacterInputRequest &request) noexcept {
  if (request.schemaVersion != kWreckwaterWireSchemaVersion) {
    return WreckwaterCodecError::UnsupportedVersion;
  }
  if (request.clientRequestSequence == 0u ||
      request.characterHandle == 0u ||
      request.connectionGeneration == 0u) {
    return WreckwaterCodecError::InvalidIdentity;
  }
  if (request.redundantInputCount >
      kWreckwaterCharacterInputMaximumRedundantSamples) {
    return WreckwaterCodecError::InvalidCount;
  }

  uint64_t priorTick = 0u;
  uint64_t priorSequence = 0u;
  for (uint32_t index = 0u;
       index < kWreckwaterCharacterInputMaximumRedundantSamples;
       ++index) {
    const WreckwaterCharacterInputSample &sample =
        request.redundantInputs[index];
    if (index >= request.redundantInputCount) {
      if (!zeroCharacterInputSample(sample)) {
        return WreckwaterCodecError::ReservedNonZero;
      }
      continue;
    }
    const WreckwaterCodecError sampleError =
        validateCharacterInputSample(sample);
    if (sampleError != WreckwaterCodecError::None) {
      return sampleError;
    }
    if ((priorTick != 0u &&
         sample.requestedApplicationTick < priorTick) ||
        (priorSequence != 0u &&
         sample.characterInputSequence <= priorSequence)) {
      return sample.requestedApplicationTick < priorTick
                 ? WreckwaterCodecError::InvalidTick
                 : WreckwaterCodecError::InvalidIdentity;
    }
    priorTick = sample.requestedApplicationTick;
    priorSequence = sample.characterInputSequence;
  }

  const WreckwaterCharacterInputSample primary{
      .requestedApplicationTick = request.requestedApplicationTick,
      .characterInputSequence = request.characterInputSequence,
      .moveXQ15 = request.moveXQ15,
      .moveZQ15 = request.moveZQ15,
      .inputFlags = request.inputFlags,
  };
  const WreckwaterCodecError primaryError =
      validateCharacterInputSample(primary);
  if (primaryError != WreckwaterCodecError::None) {
    return primaryError;
  }
  if ((priorTick != 0u &&
       primary.requestedApplicationTick < priorTick) ||
      (priorSequence != 0u &&
       primary.characterInputSequence <= priorSequence)) {
    return primary.requestedApplicationTick < priorTick
               ? WreckwaterCodecError::InvalidTick
               : WreckwaterCodecError::InvalidIdentity;
  }
  return WreckwaterCodecError::None;
}

[[nodiscard]] bool validCrew(WreckwaterCrew crew) noexcept {
  switch (crew) {
  case WreckwaterCrew::None:
  case WreckwaterCrew::CrewOne:
  case WreckwaterCrew::CrewTwo:
    return true;
  }
  return false;
}

[[nodiscard]] bool playableCrew(WreckwaterCrew crew) noexcept {
  return crew == WreckwaterCrew::CrewOne || crew == WreckwaterCrew::CrewTwo;
}

[[nodiscard]] bool validPhase(WreckwaterPhase phase) noexcept {
  switch (phase) {
  case WreckwaterPhase::Warmup:
  case WreckwaterPhase::Live:
  case WreckwaterPhase::Overtime:
  case WreckwaterPhase::Finished:
    return true;
  }
  return false;
}

[[nodiscard]] bool validOutcome(WreckwaterOutcomeType outcome,
                                WreckwaterCrew winner) noexcept {
  if (!validCrew(winner))
    return false;
  switch (outcome) {
  case WreckwaterOutcomeType::Undecided:
  case WreckwaterOutcomeType::Tie:
    return winner == WreckwaterCrew::None;
  case WreckwaterOutcomeType::CrewVictory:
    return playableCrew(winner);
  }
  return false;
}

[[nodiscard]] bool validKind(WreckwaterEntityKind kind) noexcept {
  return kind == WreckwaterEntityKind::Skiff ||
         kind == WreckwaterEntityKind::Cargo;
}

[[nodiscard]] bool validShape(WreckwaterShape shape) noexcept {
  switch (shape) {
  case WreckwaterShape::Sphere:
  case WreckwaterShape::Box:
  case WreckwaterShape::Capsule:
  case WreckwaterShape::Cylinder:
  case WreckwaterShape::ConvexHull:
  case WreckwaterShape::Compound:
    return true;
  }
  return false;
}

[[nodiscard]] bool
finiteCanonicalVector(const WreckwaterVec3 &vector) noexcept {
  return isCanonicalWreckwaterFloat(vector.x) &&
         isCanonicalWreckwaterFloat(vector.y) &&
         isCanonicalWreckwaterFloat(vector.z);
}

void canonicalizeVector(WreckwaterVec3 &vector) noexcept {
  vector.x = canonicalWreckwaterFloat(vector.x);
  vector.y = canonicalWreckwaterFloat(vector.y);
  vector.z = canonicalWreckwaterFloat(vector.z);
}

[[nodiscard]] bool zeroVector(const WreckwaterVec3 &vector) noexcept {
  return vector.x == 0.0f && vector.y == 0.0f && vector.z == 0.0f;
}

[[nodiscard]] bool boundedVector(const WreckwaterVec3 &vector,
                                 float magnitude) noexcept {
  return std::abs(vector.x) <= magnitude && std::abs(vector.y) <= magnitude &&
         std::abs(vector.z) <= magnitude;
}

[[nodiscard]] bool
zeroCargo(const WreckwaterCargoLogicalState &cargo) noexcept {
  return cargo.cargoId == 0u && cargo.generation == 0u &&
         cargo.revision == 0u &&
         cargo.disposition == WreckwaterCargoDisposition::NotApplicable &&
         cargo.ownerCrew == WreckwaterCrew::None && cargo.towingSkiffId == 0u &&
         cargo.towingSkiffGeneration == 0u;
}

[[nodiscard]] bool
zeroSkiff(const WreckwaterSkiffLogicalState &skiff) noexcept {
  return skiff.skiffId == 0u && skiff.generation == 0u &&
         skiff.disposition == WreckwaterSkiffDisposition::NotApplicable;
}

[[nodiscard]] bool
zeroAttachment(const WreckwaterAttachmentLogicalState &attachment) noexcept {
  return attachment.attachmentId == 0u && attachment.generation == 0u &&
         attachment.state == WreckwaterAttachmentState::None;
}

[[nodiscard]] bool
validCargoLogicalState(const WreckwaterEntityState &entity) noexcept {
  const WreckwaterCargoLogicalState &cargo = entity.cargo;
  if (cargo.cargoId == 0u || cargo.generation == 0u || cargo.revision == 0u ||
      !validCrew(cargo.ownerCrew) || !zeroSkiff(entity.skiff)) {
    return false;
  }

  const bool noTow = cargo.towingSkiffId == 0u &&
                     cargo.towingSkiffGeneration == 0u &&
                     zeroAttachment(entity.attachment);
  switch (cargo.disposition) {
  case WreckwaterCargoDisposition::Free:
  case WreckwaterCargoDisposition::Lost:
    return entity.crew == WreckwaterCrew::None &&
           cargo.ownerCrew == WreckwaterCrew::None && noTow;
  case WreckwaterCargoDisposition::Banked:
    return playableCrew(cargo.ownerCrew) && entity.crew == cargo.ownerCrew &&
           noTow;
  case WreckwaterCargoDisposition::Towed:
    return playableCrew(cargo.ownerCrew) && entity.crew == cargo.ownerCrew &&
           cargo.towingSkiffId != 0u && cargo.towingSkiffGeneration != 0u &&
           entity.attachment.attachmentId != 0u &&
           entity.attachment.generation != 0u &&
           entity.attachment.state == WreckwaterAttachmentState::Attached;
  case WreckwaterCargoDisposition::NotApplicable:
    return false;
  }
  return false;
}

[[nodiscard]] bool
validSkiffLogicalState(const WreckwaterEntityState &entity) noexcept {
  if (!playableCrew(entity.crew) || !zeroCargo(entity.cargo) ||
      !zeroAttachment(entity.attachment) || entity.skiff.skiffId == 0u ||
      entity.skiff.generation == 0u) {
    return false;
  }
  return entity.skiff.disposition == WreckwaterSkiffDisposition::Active ||
         entity.skiff.disposition == WreckwaterSkiffDisposition::Sunk;
}

[[nodiscard]] WreckwaterCodecError
validateEntity(const WreckwaterEntityState &entity) noexcept {
  if (entity.netEntityId == 0u || entity.netGeneration == 0u ||
      !validKind(entity.kind) || !validCrew(entity.crew) ||
      !validShape(entity.shape) || !allZero(entity.reserved)) {
    return WreckwaterCodecError::InvalidEntity;
  }
  for (int32_t sector : entity.sector) {
    if (sector < -kWreckwaterMaximumSectorMagnitude ||
        sector > kWreckwaterMaximumSectorMagnitude) {
      return WreckwaterCodecError::InvalidEntity;
    }
  }
  if (!finiteCanonicalVector(entity.localPosition) ||
      !finiteCanonicalVector(entity.linearVelocity) ||
      !finiteCanonicalVector(entity.angularVelocity) ||
      !finiteCanonicalVector(entity.dimensions)) {
    return WreckwaterCodecError::InvalidFloat;
  }
  if (entity.localPosition.x < kWreckwaterSectorLocalMinimum ||
      entity.localPosition.x >= kWreckwaterSectorLocalMaximum ||
      entity.localPosition.y < kWreckwaterSectorLocalMinimum ||
      entity.localPosition.y >= kWreckwaterSectorLocalMaximum ||
      entity.localPosition.z < kWreckwaterSectorLocalMinimum ||
      entity.localPosition.z >= kWreckwaterSectorLocalMaximum ||
      !boundedVector(entity.linearVelocity, kWreckwaterMaximumLinearVelocity) ||
      !boundedVector(entity.angularVelocity,
                     kWreckwaterMaximumAngularVelocity)) {
    return WreckwaterCodecError::InvalidFloat;
  }
  if (!isCanonicalWreckwaterQuaternion(entity.orientation)) {
    return WreckwaterCodecError::InvalidQuaternion;
  }
  if (entity.dimensions.x < kWreckwaterMinimumDimension ||
      entity.dimensions.x > kWreckwaterMaximumDimension ||
      entity.dimensions.y < kWreckwaterMinimumDimension ||
      entity.dimensions.y > kWreckwaterMaximumDimension ||
      entity.dimensions.z < kWreckwaterMinimumDimension ||
      entity.dimensions.z > kWreckwaterMaximumDimension) {
    return WreckwaterCodecError::InvalidFloat;
  }
  const bool validLogical = entity.kind == WreckwaterEntityKind::Cargo
                                ? validCargoLogicalState(entity)
                                : validSkiffLogicalState(entity);
  return validLogical ? WreckwaterCodecError::None
                      : WreckwaterCodecError::InvalidEntity;
}

[[nodiscard]] WreckwaterCodecError validateCharacter(
    const WreckwaterCharacterState &character) noexcept {
  const auto mode = static_cast<WreckwaterCharacterMode>(
      character.stateFlags & kWreckwaterCharacterModeMask);
  if (character.characterHandle == 0u || character.playerId == 0u ||
      character.connectionGeneration == 0u ||
      (character.stateFlags & ~kWreckwaterCharacterStateKnownFlags) != 0u ||
      (character.stateFlags & kWreckwaterCharacterStateActiveFlag) == 0u ||
      !validCharacterMode(mode)) {
    return WreckwaterCodecError::InvalidEntity;
  }
  for (int32_t sector : character.sector) {
    if (sector < -kWreckwaterMaximumSectorMagnitude ||
        sector > kWreckwaterMaximumSectorMagnitude) {
      return WreckwaterCodecError::InvalidEntity;
    }
  }
  if (!finiteCanonicalVector(character.localFeetPosition) ||
      !finiteCanonicalVector(character.worldVelocity) ||
      !finiteCanonicalVector(character.skiffLocalFeetPosition) ||
      !finiteCanonicalVector(character.skiffLocalVelocity)) {
    return WreckwaterCodecError::InvalidFloat;
  }
  if (character.localFeetPosition.x < kWreckwaterSectorLocalMinimum ||
      character.localFeetPosition.x >= kWreckwaterSectorLocalMaximum ||
      character.localFeetPosition.y < kWreckwaterSectorLocalMinimum ||
      character.localFeetPosition.y >= kWreckwaterSectorLocalMaximum ||
      character.localFeetPosition.z < kWreckwaterSectorLocalMinimum ||
      character.localFeetPosition.z >= kWreckwaterSectorLocalMaximum ||
      !boundedVector(character.worldVelocity,
                     kWreckwaterMaximumCharacterVelocity) ||
      !boundedVector(character.skiffLocalFeetPosition,
                     kWreckwaterMaximumDimension) ||
      !boundedVector(character.skiffLocalVelocity,
                     kWreckwaterMaximumLinearVelocity)) {
    return WreckwaterCodecError::InvalidFloat;
  }
  const bool aboard = mode == WreckwaterCharacterMode::OnSkiff;
  if (aboard) {
    if (character.skiffId == 0u || character.skiffGeneration == 0u) {
      return WreckwaterCodecError::InvalidEntity;
    }
  } else if (character.skiffId != 0u ||
             character.skiffGeneration != 0u ||
             !zeroVector(character.skiffLocalFeetPosition) ||
             !zeroVector(character.skiffLocalVelocity)) {
    return WreckwaterCodecError::InvalidEntity;
  }
  return WreckwaterCodecError::None;
}

[[nodiscard]] WreckwaterCodecError
validateSnapshotHeader(const WreckwaterCertifiedSnapshot &snapshot) noexcept {
  if (snapshot.schemaVersion != kWreckwaterWireSchemaVersion) {
    return WreckwaterCodecError::UnsupportedVersion;
  }
  if (snapshot.flags != kWreckwaterCertifiedFullSnapshotFlag) {
    return WreckwaterCodecError::InvalidArgument;
  }
  if (snapshot.sessionId == 0u || snapshot.matchId == 0u ||
      snapshot.worldId == 0u || snapshot.worldEpoch == 0u ||
      snapshot.authorityEpoch == 0u || snapshot.snapshotSequence == 0u) {
    return WreckwaterCodecError::InvalidIdentity;
  }
  if (snapshot.applicationTick > kWreckwaterMaximumApplicationTick ||
      snapshot.physicsEvidenceTick > snapshot.applicationTick ||
      snapshot.applicationTick - snapshot.physicsEvidenceTick >
          kWreckwaterMaximumPhysicsEvidenceLagTicks) {
    return WreckwaterCodecError::InvalidTick;
  }
  if (!validPhase(snapshot.phase)) {
    return WreckwaterCodecError::InvalidPhase;
  }
  if (snapshot.crewOneScore > kWreckwaterMaximumScore ||
      snapshot.crewTwoScore > kWreckwaterMaximumScore) {
    return WreckwaterCodecError::InvalidArgument;
  }
  if (!validOutcome(snapshot.outcome, snapshot.winner) ||
      (snapshot.phase == WreckwaterPhase::Finished &&
       snapshot.outcome == WreckwaterOutcomeType::Undecided) ||
      (snapshot.phase != WreckwaterPhase::Finished &&
       snapshot.outcome != WreckwaterOutcomeType::Undecided) ||
      (snapshot.outcome == WreckwaterOutcomeType::Tie &&
       snapshot.crewOneScore != snapshot.crewTwoScore) ||
      (snapshot.outcome == WreckwaterOutcomeType::CrewVictory &&
       ((snapshot.winner == WreckwaterCrew::CrewOne &&
         snapshot.crewOneScore <= snapshot.crewTwoScore) ||
        (snapshot.winner == WreckwaterCrew::CrewTwo &&
         snapshot.crewTwoScore <= snapshot.crewOneScore)))) {
    return WreckwaterCodecError::InvalidOutcome;
  }
  if (!allZero(snapshot.reserved)) {
    return WreckwaterCodecError::ReservedNonZero;
  }
  if (snapshot.entities.empty() ||
      snapshot.entities.size() > kWreckwaterMaximumSnapshotEntities ||
      snapshot.characters.size() >
          kWreckwaterMaximumSnapshotCharacters) {
    return WreckwaterCodecError::InvalidCount;
  }
  return WreckwaterCodecError::None;
}

[[nodiscard]] WreckwaterCodecError validateSnapshotEntities(
    std::span<const WreckwaterEntityState> entities,
    std::span<const WreckwaterCharacterState> characters) noexcept {
  for (size_t index = 0u; index < entities.size(); ++index) {
    const WreckwaterCodecError entityError = validateEntity(entities[index]);
    if (entityError != WreckwaterCodecError::None)
      return entityError;
    if (index != 0u) {
      if (entities[index - 1u].netEntityId > entities[index].netEntityId) {
        return WreckwaterCodecError::UnsortedEntities;
      }
      if (entities[index - 1u].netEntityId == entities[index].netEntityId) {
        return WreckwaterCodecError::DuplicateEntity;
      }
    }
  }

  for (size_t first = 0u; first < entities.size(); ++first) {
    const WreckwaterEntityState &lhs = entities[first];
    for (size_t second = first + 1u; second < entities.size(); ++second) {
      const WreckwaterEntityState &rhs = entities[second];
      if (lhs.kind == WreckwaterEntityKind::Cargo &&
          rhs.kind == WreckwaterEntityKind::Cargo &&
          lhs.cargo.cargoId == rhs.cargo.cargoId) {
        return WreckwaterCodecError::DuplicateEntity;
      }
      if (lhs.kind == WreckwaterEntityKind::Skiff &&
          rhs.kind == WreckwaterEntityKind::Skiff &&
          lhs.skiff.skiffId == rhs.skiff.skiffId) {
        return WreckwaterCodecError::DuplicateEntity;
      }
      if (lhs.attachment.attachmentId != 0u &&
          lhs.attachment.attachmentId == rhs.attachment.attachmentId) {
        return WreckwaterCodecError::DuplicateEntity;
      }
    }
  }

  for (const WreckwaterEntityState &cargo : entities) {
    if (cargo.kind != WreckwaterEntityKind::Cargo ||
        cargo.cargo.disposition != WreckwaterCargoDisposition::Towed) {
      continue;
    }
    const auto skiff = std::find_if(
        entities.begin(), entities.end(),
        [&cargo](const WreckwaterEntityState &candidate) {
          return candidate.kind == WreckwaterEntityKind::Skiff &&
                 candidate.skiff.skiffId == cargo.cargo.towingSkiffId &&
                 candidate.skiff.generation ==
                     cargo.cargo.towingSkiffGeneration;
        });
    if (skiff == entities.end() || skiff->crew != cargo.crew ||
        skiff->skiff.disposition != WreckwaterSkiffDisposition::Active) {
      return WreckwaterCodecError::InvalidEntity;
    }
  }

  for (size_t index = 0u; index < characters.size(); ++index) {
    const WreckwaterCharacterState &character = characters[index];
    const WreckwaterCodecError characterError =
        validateCharacter(character);
    if (characterError != WreckwaterCodecError::None) {
      return characterError;
    }
    if (index != 0u) {
      if (characters[index - 1u].playerId > character.playerId) {
        return WreckwaterCodecError::UnsortedEntities;
      }
      if (characters[index - 1u].playerId == character.playerId ||
          characters[index - 1u].characterHandle ==
              character.characterHandle) {
        return WreckwaterCodecError::DuplicateEntity;
      }
    }
    for (size_t previous = 0u; previous < index; ++previous) {
      if (characters[previous].characterHandle ==
          character.characterHandle) {
        return WreckwaterCodecError::DuplicateEntity;
      }
    }
    const auto mode = static_cast<WreckwaterCharacterMode>(
        character.stateFlags & kWreckwaterCharacterModeMask);
    if (mode != WreckwaterCharacterMode::OnSkiff) continue;
    const auto skiff = std::find_if(
        entities.begin(), entities.end(),
        [&character](const WreckwaterEntityState &candidate) {
          return candidate.kind == WreckwaterEntityKind::Skiff &&
                 candidate.skiff.skiffId == character.skiffId &&
                 candidate.skiff.generation ==
                     character.skiffGeneration &&
                 candidate.skiff.disposition ==
                     WreckwaterSkiffDisposition::Active;
        });
    if (skiff == entities.end()) {
      return WreckwaterCodecError::InvalidEntity;
    }
  }
  return WreckwaterCodecError::None;
}

[[nodiscard]] WreckwaterCodecError
validateSnapshot(const WreckwaterCertifiedSnapshot &snapshot) noexcept {
  const WreckwaterCodecError headerError = validateSnapshotHeader(snapshot);
  return headerError == WreckwaterCodecError::None
             ? validateSnapshotEntities(
                   snapshot.entities, snapshot.characters)
             : headerError;
}

void writeVector(Writer &writer, const WreckwaterVec3 &vector) {
  writer.f32(vector.x);
  writer.f32(vector.y);
  writer.f32(vector.z);
}

[[nodiscard]] bool readVector(Reader &reader, WreckwaterVec3 &vector) noexcept {
  return reader.f32(vector.x) && reader.f32(vector.y) && reader.f32(vector.z);
}

void writeEntity(Writer &writer, const WreckwaterEntityState &entity) {
  writer.u64(entity.netEntityId);
  writer.u32(entity.netGeneration);
  writer.u32(static_cast<uint32_t>(entity.kind));
  writer.u32(static_cast<uint32_t>(entity.crew));
  for (int32_t sector : entity.sector)
    writer.i32(sector);
  writeVector(writer, entity.localPosition);
  writer.f32(entity.orientation.x);
  writer.f32(entity.orientation.y);
  writer.f32(entity.orientation.z);
  writer.f32(entity.orientation.w);
  writeVector(writer, entity.linearVelocity);
  writeVector(writer, entity.angularVelocity);
  writer.u32(static_cast<uint32_t>(entity.shape));
  writeVector(writer, entity.dimensions);
  writer.u32(entity.packedMaterialFlags);
  writer.u32(entity.cargo.cargoId);
  writer.u32(entity.cargo.generation);
  writer.u32(entity.cargo.revision);
  writer.u32(static_cast<uint32_t>(entity.cargo.disposition));
  writer.u32(static_cast<uint32_t>(entity.cargo.ownerCrew));
  writer.u32(entity.cargo.towingSkiffId);
  writer.u32(entity.cargo.towingSkiffGeneration);
  writer.u32(entity.skiff.skiffId);
  writer.u32(entity.skiff.generation);
  writer.u32(static_cast<uint32_t>(entity.skiff.disposition));
  writer.u64(entity.attachment.attachmentId);
  writer.u32(entity.attachment.generation);
  writer.u32(static_cast<uint32_t>(entity.attachment.state));
  for (uint32_t reserved : entity.reserved)
    writer.u32(reserved);
}

[[nodiscard]] bool readEntity(Reader &reader,
                              WreckwaterEntityState &entity) noexcept {
  uint32_t kind = 0u;
  uint32_t crew = 0u;
  uint32_t shape = 0u;
  uint32_t cargoDisposition = 0u;
  uint32_t cargoOwner = 0u;
  uint32_t skiffDisposition = 0u;
  uint32_t attachmentState = 0u;
  if (!reader.u64(entity.netEntityId) || !reader.u32(entity.netGeneration) ||
      !reader.u32(kind) || !reader.u32(crew)) {
    return false;
  }
  for (int32_t &sector : entity.sector) {
    if (!reader.i32(sector))
      return false;
  }
  if (!readVector(reader, entity.localPosition) ||
      !reader.f32(entity.orientation.x) || !reader.f32(entity.orientation.y) ||
      !reader.f32(entity.orientation.z) || !reader.f32(entity.orientation.w) ||
      !readVector(reader, entity.linearVelocity) ||
      !readVector(reader, entity.angularVelocity) || !reader.u32(shape) ||
      !readVector(reader, entity.dimensions) ||
      !reader.u32(entity.packedMaterialFlags) ||
      !reader.u32(entity.cargo.cargoId) ||
      !reader.u32(entity.cargo.generation) ||
      !reader.u32(entity.cargo.revision) || !reader.u32(cargoDisposition) ||
      !reader.u32(cargoOwner) || !reader.u32(entity.cargo.towingSkiffId) ||
      !reader.u32(entity.cargo.towingSkiffGeneration) ||
      !reader.u32(entity.skiff.skiffId) ||
      !reader.u32(entity.skiff.generation) || !reader.u32(skiffDisposition) ||
      !reader.u64(entity.attachment.attachmentId) ||
      !reader.u32(entity.attachment.generation) ||
      !reader.u32(attachmentState)) {
    return false;
  }
  for (uint32_t &reserved : entity.reserved) {
    if (!reader.u32(reserved))
      return false;
  }
  entity.kind = static_cast<WreckwaterEntityKind>(kind);
  entity.crew = static_cast<WreckwaterCrew>(crew);
  entity.shape = static_cast<WreckwaterShape>(shape);
  entity.cargo.disposition =
      static_cast<WreckwaterCargoDisposition>(cargoDisposition);
  entity.cargo.ownerCrew = static_cast<WreckwaterCrew>(cargoOwner);
  entity.skiff.disposition =
      static_cast<WreckwaterSkiffDisposition>(skiffDisposition);
  entity.attachment.state =
      static_cast<WreckwaterAttachmentState>(attachmentState);
  return true;
}

void writeCharacter(
    Writer &writer, const WreckwaterCharacterState &character) {
  writer.u32(character.characterHandle);
  writer.u32(character.stateFlags);
  writer.u64(character.playerId);
  writer.u32(character.connectionGeneration);
  for (int32_t sector : character.sector)
    writer.i32(sector);
  writeVector(writer, character.localFeetPosition);
  writeVector(writer, character.worldVelocity);
  writer.u32(character.skiffId);
  writer.u32(character.skiffGeneration);
  writeVector(writer, character.skiffLocalFeetPosition);
  writeVector(writer, character.skiffLocalVelocity);
  writer.u64(character.lastAppliedCharacterInputSequence);
}

[[nodiscard]] bool readCharacter(
    Reader &reader, WreckwaterCharacterState &character) noexcept {
  if (!reader.u32(character.characterHandle) ||
      !reader.u32(character.stateFlags) ||
      !reader.u64(character.playerId) ||
      !reader.u32(character.connectionGeneration)) {
    return false;
  }
  for (int32_t &sector : character.sector) {
    if (!reader.i32(sector)) return false;
  }
  return readVector(reader, character.localFeetPosition) &&
         readVector(reader, character.worldVelocity) &&
         reader.u32(character.skiffId) &&
         reader.u32(character.skiffGeneration) &&
         readVector(reader, character.skiffLocalFeetPosition) &&
         readVector(reader, character.skiffLocalVelocity) &&
         reader.u64(character.lastAppliedCharacterInputSequence);
}

void patchU64(std::vector<std::byte> &bytes, size_t offset,
              uint64_t value) noexcept {
  for (uint32_t byte = 0u; byte < 8u; ++byte) {
    bytes[offset + byte] =
        std::byte{static_cast<uint8_t>(value >> (byte * 8u))};
  }
}

[[nodiscard]] bool
magicMatches(std::span<const std::byte> bytes,
             const std::array<std::byte, 8> &magic) noexcept {
  return bytes.size() >= magic.size() &&
         std::equal(magic.begin(), magic.end(), bytes.begin());
}

} // namespace

const char *wreckwaterCodecErrorName(WreckwaterCodecError error) noexcept {
  switch (error) {
  case WreckwaterCodecError::None:
    return "none";
  case WreckwaterCodecError::InvalidArgument:
    return "invalid argument";
  case WreckwaterCodecError::InvalidMagic:
    return "invalid magic";
  case WreckwaterCodecError::UnsupportedVersion:
    return "unsupported version";
  case WreckwaterCodecError::WrongPayloadType:
    return "wrong payload type";
  case WreckwaterCodecError::Truncated:
    return "truncated";
  case WreckwaterCodecError::TrailingBytes:
    return "trailing bytes";
  case WreckwaterCodecError::LengthMismatch:
    return "length mismatch";
  case WreckwaterCodecError::ReservedNonZero:
    return "reserved field is nonzero";
  case WreckwaterCodecError::InvalidAction:
    return "invalid action";
  case WreckwaterCodecError::InvalidControl:
    return "invalid control";
  case WreckwaterCodecError::InvalidIdentity:
    return "invalid identity";
  case WreckwaterCodecError::InvalidTick:
    return "invalid tick";
  case WreckwaterCodecError::InvalidPhase:
    return "invalid phase";
  case WreckwaterCodecError::InvalidOutcome:
    return "invalid outcome";
  case WreckwaterCodecError::InvalidCount:
    return "invalid count";
  case WreckwaterCodecError::SizeOverflow:
    return "size overflow";
  case WreckwaterCodecError::InvalidEntity:
    return "invalid entity";
  case WreckwaterCodecError::InvalidFloat:
    return "invalid float";
  case WreckwaterCodecError::InvalidQuaternion:
    return "invalid quaternion";
  case WreckwaterCodecError::UnsortedEntities:
    return "unsorted entities";
  case WreckwaterCodecError::DuplicateEntity:
    return "duplicate entity";
  case WreckwaterCodecError::HashMismatch:
    return "hash mismatch";
  }
  return "unknown";
}

float canonicalWreckwaterFloat(float value) noexcept {
  return value == 0.0f || (std::isfinite(value) &&
                           std::abs(value) < std::numeric_limits<float>::min())
             ? 0.0f
             : value;
}

bool isCanonicalWreckwaterFloat(float value) noexcept {
  return std::isfinite(value) && !(value == 0.0f && std::signbit(value)) &&
         std::fpclassify(value) != FP_SUBNORMAL;
}

bool canonicalizeWreckwaterQuaternion(
    WreckwaterQuaternion &quaternion) noexcept {
  const std::array<float, 4> input{quaternion.x, quaternion.y, quaternion.z,
                                   quaternion.w};
  for (float component : input) {
    if (!std::isfinite(component) ||
        std::abs(component) > kMaximumQuaternionInputComponent) {
      return false;
    }
  }

  const double x = static_cast<double>(quaternion.x);
  const double y = static_cast<double>(quaternion.y);
  const double z = static_cast<double>(quaternion.z);
  const double w = static_cast<double>(quaternion.w);
  const double lengthSquared = x * x + y * y + z * z + w * w;
  if (!std::isfinite(lengthSquared) ||
      lengthSquared < kMinimumQuaternionLengthSquared) {
    return false;
  }
  const double inverseLength = 1.0 / std::sqrt(lengthSquared);
  quaternion.x = canonicalWreckwaterFloat(
      static_cast<float>(static_cast<double>(quaternion.x) * inverseLength));
  quaternion.y = canonicalWreckwaterFloat(
      static_cast<float>(static_cast<double>(quaternion.y) * inverseLength));
  quaternion.z = canonicalWreckwaterFloat(
      static_cast<float>(static_cast<double>(quaternion.z) * inverseLength));
  quaternion.w = canonicalWreckwaterFloat(
      static_cast<float>(static_cast<double>(quaternion.w) * inverseLength));

  const std::array<float, 4> signOrder{quaternion.w, quaternion.x, quaternion.y,
                                       quaternion.z};
  bool negate = false;
  for (float component : signOrder) {
    if (component > 0.0f)
      break;
    if (component < 0.0f) {
      negate = true;
      break;
    }
  }
  if (negate) {
    quaternion.x = canonicalWreckwaterFloat(-quaternion.x);
    quaternion.y = canonicalWreckwaterFloat(-quaternion.y);
    quaternion.z = canonicalWreckwaterFloat(-quaternion.z);
    quaternion.w = canonicalWreckwaterFloat(-quaternion.w);
  }
  return isCanonicalWreckwaterQuaternion(quaternion);
}

bool isCanonicalWreckwaterQuaternion(
    const WreckwaterQuaternion &quaternion) noexcept {
  if (!isCanonicalWreckwaterFloat(quaternion.x) ||
      !isCanonicalWreckwaterFloat(quaternion.y) ||
      !isCanonicalWreckwaterFloat(quaternion.z) ||
      !isCanonicalWreckwaterFloat(quaternion.w) ||
      std::abs(quaternion.x) > 1.0f || std::abs(quaternion.y) > 1.0f ||
      std::abs(quaternion.z) > 1.0f || std::abs(quaternion.w) > 1.0f) {
    return false;
  }
  const double x = static_cast<double>(quaternion.x);
  const double y = static_cast<double>(quaternion.y);
  const double z = static_cast<double>(quaternion.z);
  const double w = static_cast<double>(quaternion.w);
  const double lengthSquared = x * x + y * y + z * z + w * w;
  if (std::abs(lengthSquared - 1.0) > kQuaternionUnitTolerance) {
    return false;
  }
  const std::array<float, 4> signOrder{quaternion.w, quaternion.x, quaternion.y,
                                       quaternion.z};
  for (float component : signOrder) {
    if (component > 0.0f)
      return true;
    if (component < 0.0f)
      return false;
  }
  return false;
}

bool canonicalizeWreckwaterEntityState(WreckwaterEntityState &entity) noexcept {
  canonicalizeVector(entity.localPosition);
  canonicalizeVector(entity.linearVelocity);
  canonicalizeVector(entity.angularVelocity);
  canonicalizeVector(entity.dimensions);
  if (!canonicalizeWreckwaterQuaternion(entity.orientation))
    return false;
  return validateEntity(entity) == WreckwaterCodecError::None;
}

bool isCanonicalWreckwaterEntityState(
    const WreckwaterEntityState &entity) noexcept {
  return validateEntity(entity) == WreckwaterCodecError::None;
}

bool canonicalizeWreckwaterCharacterState(
    WreckwaterCharacterState &character) noexcept {
  canonicalizeVector(character.localFeetPosition);
  canonicalizeVector(character.worldVelocity);
  canonicalizeVector(character.skiffLocalFeetPosition);
  canonicalizeVector(character.skiffLocalVelocity);
  return validateCharacter(character) == WreckwaterCodecError::None;
}

bool isCanonicalWreckwaterCharacterState(
    const WreckwaterCharacterState &character) noexcept {
  return validateCharacter(character) == WreckwaterCodecError::None;
}

bool canonicalizeWreckwaterSnapshot(WreckwaterCertifiedSnapshot &snapshot) {
  if (snapshot.entities.empty() ||
      snapshot.entities.size() > kWreckwaterMaximumSnapshotEntities ||
      snapshot.characters.size() >
          kWreckwaterMaximumSnapshotCharacters) {
    return false;
  }
  snapshot.serializedByteHash = 0u;
  for (WreckwaterEntityState &entity : snapshot.entities) {
    if (!canonicalizeWreckwaterEntityState(entity))
      return false;
  }
  for (WreckwaterCharacterState &character : snapshot.characters) {
    if (!canonicalizeWreckwaterCharacterState(character))
      return false;
  }
  std::sort(
      snapshot.entities.begin(), snapshot.entities.end(),
      [](const WreckwaterEntityState &lhs, const WreckwaterEntityState &rhs) {
        return std::tie(lhs.netEntityId, lhs.netGeneration) <
               std::tie(rhs.netEntityId, rhs.netGeneration);
      });
  std::sort(
      snapshot.characters.begin(), snapshot.characters.end(),
      [](const WreckwaterCharacterState &lhs,
         const WreckwaterCharacterState &rhs) {
        return std::tie(lhs.playerId, lhs.characterHandle) <
               std::tie(rhs.playerId, rhs.characterHandle);
      });
  return validateSnapshot(snapshot) == WreckwaterCodecError::None;
}

bool isCanonicalWreckwaterSnapshot(
    const WreckwaterCertifiedSnapshot &snapshot) noexcept {
  return validateSnapshot(snapshot) == WreckwaterCodecError::None;
}

bool wreckwaterSnapshotPayloadBytes(
    size_t entityCount, size_t characterCount, size_t &bytes) noexcept {
  bytes = 0u;
  if (entityCount == 0u || entityCount > kWreckwaterMaximumSnapshotEntities ||
      characterCount > kWreckwaterMaximumSnapshotCharacters ||
      entityCount > (std::numeric_limits<size_t>::max() -
                     kWreckwaterSnapshotHeaderBytes) /
                        kWreckwaterEntityStateBytes) {
    return false;
  }
  const size_t entityBytes = entityCount * kWreckwaterEntityStateBytes;
  if (characterCount >
      (std::numeric_limits<size_t>::max() -
       kWreckwaterSnapshotHeaderBytes - entityBytes) /
          kWreckwaterCharacterStateBytes) {
    return false;
  }
  bytes = kWreckwaterSnapshotHeaderBytes + entityBytes +
          characterCount * kWreckwaterCharacterStateBytes;
  return true;
}

uint64_t
wreckwaterSerializedByteHash(std::span<const std::byte> bytes) noexcept {
  uint64_t hash = kFnv64Offset;
  for (std::byte value : bytes) {
    hash = (hash ^ std::to_integer<uint64_t>(value)) * kFnv64Prime;
  }
  return hash;
}

uint64_t wreckwaterSnapshotSerializedByteHash(
    std::span<const std::byte> bytes) noexcept {
  uint64_t hash = kFnv64Offset;
  for (size_t index = 0u; index < bytes.size(); ++index) {
    const std::byte value =
        index >= kWreckwaterSnapshotByteHashOffset &&
                index < kWreckwaterSnapshotByteHashOffset + sizeof(uint64_t)
            ? std::byte{0u}
            : bytes[index];
    hash = (hash ^ std::to_integer<uint64_t>(value)) * kFnv64Prime;
  }
  return hash;
}

bool wreckwaterSnapshotMatchesCanonicalBytes(
    const WreckwaterCertifiedSnapshot &snapshot,
    std::span<const std::byte> bytes) noexcept {
  size_t expectedBytes = 0u;
  if (!isCanonicalWreckwaterSnapshot(snapshot) ||
      !wreckwaterSnapshotPayloadBytes(
          snapshot.entities.size(), snapshot.characters.size(),
          expectedBytes) ||
      bytes.size() != expectedBytes || !magicMatches(bytes, kSnapshotMagic)) {
    return false;
  }

  Reader reader(bytes);
  uint32_t schema = 0u;
  uint32_t payloadType = 0u;
  uint32_t payloadBytes = 0u;
  uint32_t flags = 0u;
  uint64_t sessionId = 0u;
  uint64_t matchId = 0u;
  uint64_t worldId = 0u;
  uint32_t worldEpoch = 0u;
  uint32_t authorityEpoch = 0u;
  uint64_t snapshotSequence = 0u;
  uint64_t applicationTick = 0u;
  uint64_t physicsEvidenceTick = 0u;
  uint32_t phase = 0u;
  uint32_t crewOneScore = 0u;
  uint32_t crewTwoScore = 0u;
  uint32_t outcome = 0u;
  uint32_t winner = 0u;
  uint32_t matchStateHash = 0u;
  uint32_t eventStreamHash = 0u;
  uint32_t entityCount = 0u;
  uint32_t characterCount = 0u;
  std::array<uint32_t, 1> reserved{};
  uint64_t serializedByteHash = 0u;
  if (!reader.skip(kSnapshotMagic.size()) || !reader.u32(schema) ||
      !reader.u32(payloadType) || !reader.u32(payloadBytes) ||
      !reader.u32(flags) || !reader.u64(sessionId) ||
      !reader.u64(matchId) || !reader.u64(worldId) ||
      !reader.u32(worldEpoch) || !reader.u32(authorityEpoch) ||
      !reader.u64(snapshotSequence) || !reader.u64(applicationTick) ||
      !reader.u64(physicsEvidenceTick) || !reader.u32(phase) ||
      !reader.u32(crewOneScore) || !reader.u32(crewTwoScore) ||
      !reader.u32(outcome) || !reader.u32(winner) ||
      !reader.u32(matchStateHash) || !reader.u32(eventStreamHash) ||
      !reader.u32(entityCount) || !reader.u32(characterCount) ||
      !reader.u32(reserved[0]) || !reader.u64(serializedByteHash)) {
    return false;
  }
  if (schema != snapshot.schemaVersion ||
      payloadType !=
          static_cast<uint32_t>(WreckwaterPayloadType::CertifiedSnapshot) ||
      payloadBytes != bytes.size() || flags != snapshot.flags ||
      sessionId != snapshot.sessionId || matchId != snapshot.matchId ||
      worldId != snapshot.worldId || worldEpoch != snapshot.worldEpoch ||
      authorityEpoch != snapshot.authorityEpoch ||
      snapshotSequence != snapshot.snapshotSequence ||
      applicationTick != snapshot.applicationTick ||
      physicsEvidenceTick != snapshot.physicsEvidenceTick ||
      phase != static_cast<uint32_t>(snapshot.phase) ||
      crewOneScore != snapshot.crewOneScore ||
      crewTwoScore != snapshot.crewTwoScore ||
      outcome != static_cast<uint32_t>(snapshot.outcome) ||
      winner != static_cast<uint32_t>(snapshot.winner) ||
      matchStateHash != snapshot.matchStateHash ||
      eventStreamHash != snapshot.eventStreamHash ||
      entityCount != snapshot.entities.size() ||
      characterCount != snapshot.characters.size() ||
      reserved != snapshot.reserved ||
      serializedByteHash != snapshot.serializedByteHash ||
      serializedByteHash != wreckwaterSnapshotSerializedByteHash(bytes)) {
    return false;
  }

  for (const WreckwaterEntityState &expected : snapshot.entities) {
    WreckwaterEntityState decoded;
    if (!readEntity(reader, decoded) || decoded != expected) {
      return false;
    }
  }
  for (const WreckwaterCharacterState &expected :
       snapshot.characters) {
    WreckwaterCharacterState decoded;
    if (!readCharacter(reader, decoded) || decoded != expected) {
      return false;
    }
  }
  return reader.remaining() == 0u;
}

WreckwaterCodecError
validateWreckwaterSnapshotBytes(
    std::span<const std::byte> bytes) noexcept {
  if (bytes.size() < kSnapshotMagic.size()) {
    return WreckwaterCodecError::Truncated;
  }
  if (!magicMatches(bytes, kSnapshotMagic)) {
    return WreckwaterCodecError::InvalidMagic;
  }
  if (bytes.size() < kWreckwaterSnapshotHeaderBytes) {
    return WreckwaterCodecError::Truncated;
  }

  Reader reader(bytes);
  uint32_t schema = 0u;
  uint32_t payloadType = 0u;
  uint32_t payloadBytes = 0u;
  uint32_t phase = 0u;
  uint32_t outcome = 0u;
  uint32_t winner = 0u;
  uint32_t entityCount = 0u;
  uint32_t characterCount = 0u;
  WreckwaterCertifiedSnapshot snapshot;
  if (!reader.skip(kSnapshotMagic.size()) || !reader.u32(schema) ||
      !reader.u32(payloadType) || !reader.u32(payloadBytes) ||
      !reader.u32(snapshot.flags) || !reader.u64(snapshot.sessionId) ||
      !reader.u64(snapshot.matchId) || !reader.u64(snapshot.worldId) ||
      !reader.u32(snapshot.worldEpoch) ||
      !reader.u32(snapshot.authorityEpoch) ||
      !reader.u64(snapshot.snapshotSequence) ||
      !reader.u64(snapshot.applicationTick) ||
      !reader.u64(snapshot.physicsEvidenceTick) || !reader.u32(phase) ||
      !reader.u32(snapshot.crewOneScore) ||
      !reader.u32(snapshot.crewTwoScore) || !reader.u32(outcome) ||
      !reader.u32(winner) || !reader.u32(snapshot.matchStateHash) ||
      !reader.u32(snapshot.eventStreamHash) || !reader.u32(entityCount) ||
      !reader.u32(characterCount) ||
      !reader.u32(snapshot.reserved[0]) ||
      !reader.u64(snapshot.serializedByteHash)) {
    return WreckwaterCodecError::Truncated;
  }
  if (schema != kWreckwaterWireSchemaVersion) {
    return WreckwaterCodecError::UnsupportedVersion;
  }
  if (payloadType !=
      static_cast<uint32_t>(WreckwaterPayloadType::CertifiedSnapshot)) {
    return WreckwaterCodecError::WrongPayloadType;
  }
  if (payloadBytes < bytes.size()) {
    return WreckwaterCodecError::TrailingBytes;
  }
  if (payloadBytes > bytes.size()) {
    return WreckwaterCodecError::Truncated;
  }
  if (!allZero(snapshot.reserved)) {
    return WreckwaterCodecError::ReservedNonZero;
  }
  if (entityCount == 0u ||
      entityCount > kWreckwaterMaximumSnapshotEntities ||
      characterCount > kWreckwaterMaximumSnapshotCharacters) {
    return WreckwaterCodecError::InvalidCount;
  }
  size_t expectedBytes = 0u;
  if (!wreckwaterSnapshotPayloadBytes(
          entityCount, characterCount, expectedBytes)) {
    return WreckwaterCodecError::SizeOverflow;
  }
  if (payloadBytes != expectedBytes || bytes.size() != expectedBytes) {
    return WreckwaterCodecError::LengthMismatch;
  }
  if (snapshot.serializedByteHash !=
      wreckwaterSnapshotSerializedByteHash(bytes)) {
    return WreckwaterCodecError::HashMismatch;
  }

  snapshot.schemaVersion = schema;
  snapshot.phase = static_cast<WreckwaterPhase>(phase);
  snapshot.outcome = static_cast<WreckwaterOutcomeType>(outcome);
  snapshot.winner = static_cast<WreckwaterCrew>(winner);
  const WreckwaterCodecError headerError =
      validateSnapshotHeader(snapshot);
  if (headerError != WreckwaterCodecError::None &&
      headerError != WreckwaterCodecError::InvalidCount) {
    return headerError;
  }

  std::array<WreckwaterEntityState,
             kWreckwaterMaximumSnapshotEntities>
      entities{};
  for (size_t index = 0u; index < entityCount; ++index) {
    if (!readEntity(reader, entities[index])) {
      return WreckwaterCodecError::Truncated;
    }
  }
  std::array<WreckwaterCharacterState,
             kWreckwaterMaximumSnapshotCharacters>
      characters{};
  for (size_t index = 0u; index < characterCount; ++index) {
    if (!readCharacter(reader, characters[index])) {
      return WreckwaterCodecError::Truncated;
    }
  }
  if (reader.remaining() != 0u) {
    return WreckwaterCodecError::TrailingBytes;
  }
  return validateSnapshotEntities(
      std::span<const WreckwaterEntityState>(
          entities.data(), entityCount),
      std::span<const WreckwaterCharacterState>(
          characters.data(), characterCount));
}

WreckwaterWriteResult
WreckwaterActionRequestCodec::encode(const WreckwaterActionRequest &request) {
  WreckwaterWriteResult result;
  if (request.schemaVersion != kWreckwaterWireSchemaVersion) {
    result.error = WreckwaterCodecError::UnsupportedVersion;
    return result;
  }
  if (request.controlReserved != 0u) {
    result.error = WreckwaterCodecError::ReservedNonZero;
    return result;
  }
  if (!validAction(request.action)) {
    result.error = WreckwaterCodecError::InvalidAction;
    return result;
  }
  if (request.requestedApplicationTick > kWreckwaterMaximumApplicationTick) {
    result.error = WreckwaterCodecError::InvalidTick;
    return result;
  }
  if (request.clientRequestSequence == 0u) {
    result.error = WreckwaterCodecError::InvalidIdentity;
    return result;
  }
  const bool helm = request.action == WreckwaterAction::Helm;
  if (helm) {
    if (request.cargoId != 0u || request.cargoGeneration != 0u ||
        request.observedCargoRevision != 0u) {
      result.error = WreckwaterCodecError::InvalidIdentity;
      return result;
    }
    if (request.helmThrottleQ15 < kWreckwaterHelmAxisMinimum ||
        request.helmSteeringQ15 < kWreckwaterHelmAxisMinimum) {
      result.error = WreckwaterCodecError::InvalidControl;
      return result;
    }
  } else {
    if (request.cargoId == 0u || request.cargoGeneration == 0u ||
        request.observedCargoRevision == 0u) {
      result.error = WreckwaterCodecError::InvalidIdentity;
      return result;
    }
    if (request.helmThrottleQ15 != 0 || request.helmSteeringQ15 != 0) {
      result.error = WreckwaterCodecError::InvalidControl;
      return result;
    }
  }

  Writer writer(kWreckwaterActionRequestBytes);
  writer.raw(kActionMagic);
  writer.u32(kWreckwaterWireSchemaVersion);
  writer.u32(static_cast<uint32_t>(WreckwaterPayloadType::ActionRequest));
  writer.u32(static_cast<uint32_t>(kWreckwaterActionRequestBytes));
  writer.u32(0u);
  writer.u64(request.requestedApplicationTick);
  writer.u64(request.clientRequestSequence);
  writer.u32(static_cast<uint32_t>(request.action));
  writer.u32(request.cargoId);
  writer.u32(request.cargoGeneration);
  writer.u32(request.observedCargoRevision);
  writer.i16(request.helmThrottleQ15);
  writer.i16(request.helmSteeringQ15);
  writer.u32(request.controlReserved);
  result.bytes = std::move(writer.bytes());
  if (result.bytes.size() != kWreckwaterActionRequestBytes) {
    result.bytes.clear();
    result.error = WreckwaterCodecError::LengthMismatch;
    return result;
  }
  result.serializedByteHash = wreckwaterSerializedByteHash(result.bytes);
  return result;
}

WreckwaterActionReadResult WreckwaterActionRequestCodec::decode(
    std::span<const std::byte> bytes) noexcept {
  WreckwaterActionReadResult result;
  if (bytes.size() < kActionMagic.size()) {
    result.error = WreckwaterCodecError::Truncated;
    return result;
  }
  if (!magicMatches(bytes, kActionMagic)) {
    result.error = WreckwaterCodecError::InvalidMagic;
    return result;
  }
  if (bytes.size() < kWreckwaterActionRequestBytes) {
    result.error = WreckwaterCodecError::Truncated;
    return result;
  }
  if (bytes.size() > kWreckwaterActionRequestBytes) {
    result.error = WreckwaterCodecError::TrailingBytes;
    return result;
  }

  Reader reader(bytes);
  uint32_t schema = 0u;
  uint32_t payloadType = 0u;
  uint32_t payloadBytes = 0u;
  uint32_t envelopeReserved = 0u;
  uint32_t action = 0u;
  WreckwaterActionRequest request;
  if (!reader.skip(kActionMagic.size()) || !reader.u32(schema) ||
      !reader.u32(payloadType) || !reader.u32(payloadBytes) ||
      !reader.u32(envelopeReserved) ||
      !reader.u64(request.requestedApplicationTick) ||
      !reader.u64(request.clientRequestSequence) || !reader.u32(action) ||
      !reader.u32(request.cargoId) || !reader.u32(request.cargoGeneration) ||
      !reader.u32(request.observedCargoRevision) ||
      !reader.i16(request.helmThrottleQ15) ||
      !reader.i16(request.helmSteeringQ15) ||
      !reader.u32(request.controlReserved)) {
    result.error = WreckwaterCodecError::Truncated;
    return result;
  }
  if (schema != kWreckwaterWireSchemaVersion) {
    result.error = WreckwaterCodecError::UnsupportedVersion;
    return result;
  }
  if (payloadType !=
      static_cast<uint32_t>(WreckwaterPayloadType::ActionRequest)) {
    result.error = WreckwaterCodecError::WrongPayloadType;
    return result;
  }
  if (payloadBytes != kWreckwaterActionRequestBytes ||
      reader.remaining() != 0u) {
    result.error = WreckwaterCodecError::LengthMismatch;
    return result;
  }
  if (envelopeReserved != 0u || request.controlReserved != 0u) {
    result.error = WreckwaterCodecError::ReservedNonZero;
    return result;
  }
  request.schemaVersion = schema;
  request.action = static_cast<WreckwaterAction>(action);
  if (!validAction(request.action)) {
    result.error = WreckwaterCodecError::InvalidAction;
    return result;
  }
  if (request.requestedApplicationTick > kWreckwaterMaximumApplicationTick) {
    result.error = WreckwaterCodecError::InvalidTick;
    return result;
  }
  if (request.clientRequestSequence == 0u) {
    result.error = WreckwaterCodecError::InvalidIdentity;
    return result;
  }
  const bool helm = request.action == WreckwaterAction::Helm;
  if (helm) {
    if (request.cargoId != 0u || request.cargoGeneration != 0u ||
        request.observedCargoRevision != 0u) {
      result.error = WreckwaterCodecError::InvalidIdentity;
      return result;
    }
    if (request.helmThrottleQ15 < kWreckwaterHelmAxisMinimum ||
        request.helmSteeringQ15 < kWreckwaterHelmAxisMinimum) {
      result.error = WreckwaterCodecError::InvalidControl;
      return result;
    }
  } else {
    if (request.cargoId == 0u || request.cargoGeneration == 0u ||
        request.observedCargoRevision == 0u) {
      result.error = WreckwaterCodecError::InvalidIdentity;
      return result;
    }
    if (request.helmThrottleQ15 != 0 || request.helmSteeringQ15 != 0) {
      result.error = WreckwaterCodecError::InvalidControl;
      return result;
    }
  }
  result.request = request;
  return result;
}

WreckwaterWriteResult WreckwaterCharacterInputRequestCodec::encode(
    const WreckwaterCharacterInputRequest &request) {
  WreckwaterWriteResult result;
  result.error = validateCharacterInputRequest(request);
  if (result.error != WreckwaterCodecError::None) return result;

  Writer writer(kWreckwaterCharacterInputRequestBytes);
  writer.raw(kCharacterInputMagic);
  writer.u32(kWreckwaterWireSchemaVersion);
  writer.u32(static_cast<uint32_t>(
      WreckwaterPayloadType::CharacterInputRequest));
  writer.u32(static_cast<uint32_t>(
      kWreckwaterCharacterInputRequestBytes));
  writer.u32(0u);
  writer.u64(request.requestedApplicationTick);
  writer.u64(request.clientRequestSequence);
  writer.u32(request.characterHandle);
  writer.u32(request.connectionGeneration);
  writer.i16(request.moveXQ15);
  writer.i16(request.moveZQ15);
  writer.u32(request.inputFlags);
  writer.u64(request.characterInputSequence);
  writer.u32(request.redundantInputCount);
  writer.u32(0u);
  for (const WreckwaterCharacterInputSample &sample :
       request.redundantInputs) {
    writer.u64(sample.requestedApplicationTick);
    writer.u64(sample.characterInputSequence);
    writer.i16(sample.moveXQ15);
    writer.i16(sample.moveZQ15);
    writer.u32(sample.inputFlags);
  }
  result.bytes = std::move(writer.bytes());
  if (result.bytes.size() != kWreckwaterCharacterInputRequestBytes) {
    result.bytes.clear();
    result.error = WreckwaterCodecError::LengthMismatch;
    return result;
  }
  result.serializedByteHash = wreckwaterSerializedByteHash(result.bytes);
  return result;
}

WreckwaterCharacterInputReadResult
WreckwaterCharacterInputRequestCodec::decode(
    std::span<const std::byte> bytes) noexcept {
  WreckwaterCharacterInputReadResult result;
  if (bytes.size() < kCharacterInputMagic.size()) {
    result.error = WreckwaterCodecError::Truncated;
    return result;
  }
  if (!magicMatches(bytes, kCharacterInputMagic)) {
    result.error = WreckwaterCodecError::InvalidMagic;
    return result;
  }
  if (bytes.size() < kWreckwaterCharacterInputRequestBytes) {
    result.error = WreckwaterCodecError::Truncated;
    return result;
  }
  if (bytes.size() > kWreckwaterCharacterInputRequestBytes) {
    result.error = WreckwaterCodecError::TrailingBytes;
    return result;
  }

  Reader reader(bytes);
  uint32_t schema = 0u;
  uint32_t payloadType = 0u;
  uint32_t payloadBytes = 0u;
  uint32_t envelopeReserved = 0u;
  uint32_t redundancyReserved = 0u;
  WreckwaterCharacterInputRequest request;
  if (!reader.skip(kCharacterInputMagic.size()) ||
      !reader.u32(schema) || !reader.u32(payloadType) ||
      !reader.u32(payloadBytes) ||
      !reader.u32(envelopeReserved) ||
      !reader.u64(request.requestedApplicationTick) ||
      !reader.u64(request.clientRequestSequence) ||
      !reader.u32(request.characterHandle) ||
      !reader.u32(request.connectionGeneration) ||
      !reader.i16(request.moveXQ15) ||
      !reader.i16(request.moveZQ15) ||
      !reader.u32(request.inputFlags) ||
      !reader.u64(request.characterInputSequence) ||
      !reader.u32(request.redundantInputCount) ||
      !reader.u32(redundancyReserved)) {
    result.error = WreckwaterCodecError::Truncated;
    return result;
  }
  for (WreckwaterCharacterInputSample &sample :
       request.redundantInputs) {
    if (!reader.u64(sample.requestedApplicationTick) ||
        !reader.u64(sample.characterInputSequence) ||
        !reader.i16(sample.moveXQ15) ||
        !reader.i16(sample.moveZQ15) ||
        !reader.u32(sample.inputFlags)) {
      result.error = WreckwaterCodecError::Truncated;
      return result;
    }
  }
  if (schema != kWreckwaterWireSchemaVersion) {
    result.error = WreckwaterCodecError::UnsupportedVersion;
    return result;
  }
  if (payloadType != static_cast<uint32_t>(
          WreckwaterPayloadType::CharacterInputRequest)) {
    result.error = WreckwaterCodecError::WrongPayloadType;
    return result;
  }
  if (payloadBytes != kWreckwaterCharacterInputRequestBytes ||
      reader.remaining() != 0u) {
    result.error = WreckwaterCodecError::LengthMismatch;
    return result;
  }
  if (envelopeReserved != 0u || redundancyReserved != 0u) {
    result.error = WreckwaterCodecError::ReservedNonZero;
    return result;
  }
  request.schemaVersion = schema;
  result.error = validateCharacterInputRequest(request);
  if (result.error != WreckwaterCodecError::None) return result;
  result.request = request;
  return result;
}

WreckwaterWriteResult
WreckwaterSnapshotCodec::encode(const WreckwaterCertifiedSnapshot &source) {
  WreckwaterWriteResult result;
  if (source.entities.empty() ||
      source.entities.size() > kWreckwaterMaximumSnapshotEntities ||
      source.characters.size() >
          kWreckwaterMaximumSnapshotCharacters) {
    result.error = WreckwaterCodecError::InvalidCount;
    return result;
  }
  WreckwaterCertifiedSnapshot snapshot = source;
  if (!canonicalizeWreckwaterSnapshot(snapshot)) {
    result.error = validateSnapshot(snapshot);
    if (result.error == WreckwaterCodecError::None) {
      result.error = WreckwaterCodecError::InvalidArgument;
    }
    return result;
  }

  size_t payloadBytes = 0u;
  if (!wreckwaterSnapshotPayloadBytes(
          snapshot.entities.size(), snapshot.characters.size(),
          payloadBytes)) {
    result.error = WreckwaterCodecError::SizeOverflow;
    return result;
  }
  if (payloadBytes > std::numeric_limits<uint32_t>::max()) {
    result.error = WreckwaterCodecError::SizeOverflow;
    return result;
  }

  Writer writer(payloadBytes);
  writer.raw(kSnapshotMagic);
  writer.u32(kWreckwaterWireSchemaVersion);
  writer.u32(static_cast<uint32_t>(WreckwaterPayloadType::CertifiedSnapshot));
  writer.u32(static_cast<uint32_t>(payloadBytes));
  writer.u32(snapshot.flags);
  writer.u64(snapshot.sessionId);
  writer.u64(snapshot.matchId);
  writer.u64(snapshot.worldId);
  writer.u32(snapshot.worldEpoch);
  writer.u32(snapshot.authorityEpoch);
  writer.u64(snapshot.snapshotSequence);
  writer.u64(snapshot.applicationTick);
  writer.u64(snapshot.physicsEvidenceTick);
  writer.u32(static_cast<uint32_t>(snapshot.phase));
  writer.u32(snapshot.crewOneScore);
  writer.u32(snapshot.crewTwoScore);
  writer.u32(static_cast<uint32_t>(snapshot.outcome));
  writer.u32(static_cast<uint32_t>(snapshot.winner));
  writer.u32(snapshot.matchStateHash);
  writer.u32(snapshot.eventStreamHash);
  writer.u32(static_cast<uint32_t>(snapshot.entities.size()));
  writer.u32(static_cast<uint32_t>(snapshot.characters.size()));
  writer.u32(snapshot.reserved[0]);
  writer.u64(0u);
  for (const WreckwaterEntityState &entity : snapshot.entities) {
    writeEntity(writer, entity);
  }
  for (const WreckwaterCharacterState &character :
       snapshot.characters) {
    writeCharacter(writer, character);
  }

  result.bytes = std::move(writer.bytes());
  if (result.bytes.size() != payloadBytes) {
    result.bytes.clear();
    result.error = WreckwaterCodecError::LengthMismatch;
    return result;
  }
  result.serializedByteHash =
      wreckwaterSnapshotSerializedByteHash(result.bytes);
  patchU64(result.bytes, kWreckwaterSnapshotByteHashOffset,
           result.serializedByteHash);
  return result;
}

WreckwaterSnapshotReadResult
WreckwaterSnapshotCodec::decode(std::span<const std::byte> bytes) {
  WreckwaterSnapshotReadResult result;
  if (bytes.size() < kSnapshotMagic.size()) {
    result.error = WreckwaterCodecError::Truncated;
    return result;
  }
  if (!magicMatches(bytes, kSnapshotMagic)) {
    result.error = WreckwaterCodecError::InvalidMagic;
    return result;
  }
  if (bytes.size() < kWreckwaterSnapshotHeaderBytes) {
    result.error = WreckwaterCodecError::Truncated;
    return result;
  }

  Reader reader(bytes);
  uint32_t schema = 0u;
  uint32_t payloadType = 0u;
  uint32_t payloadBytes = 0u;
  uint32_t phase = 0u;
  uint32_t outcome = 0u;
  uint32_t winner = 0u;
  uint32_t entityCount = 0u;
  uint32_t characterCount = 0u;
  WreckwaterCertifiedSnapshot snapshot;
  if (!reader.skip(kSnapshotMagic.size()) || !reader.u32(schema) ||
      !reader.u32(payloadType) || !reader.u32(payloadBytes) ||
      !reader.u32(snapshot.flags) || !reader.u64(snapshot.sessionId) ||
      !reader.u64(snapshot.matchId) || !reader.u64(snapshot.worldId) ||
      !reader.u32(snapshot.worldEpoch) ||
      !reader.u32(snapshot.authorityEpoch) ||
      !reader.u64(snapshot.snapshotSequence) ||
      !reader.u64(snapshot.applicationTick) ||
      !reader.u64(snapshot.physicsEvidenceTick) || !reader.u32(phase) ||
      !reader.u32(snapshot.crewOneScore) ||
      !reader.u32(snapshot.crewTwoScore) || !reader.u32(outcome) ||
      !reader.u32(winner) || !reader.u32(snapshot.matchStateHash) ||
      !reader.u32(snapshot.eventStreamHash) || !reader.u32(entityCount) ||
      !reader.u32(characterCount) ||
      !reader.u32(snapshot.reserved[0]) ||
      !reader.u64(snapshot.serializedByteHash)) {
    result.error = WreckwaterCodecError::Truncated;
    return result;
  }
  if (schema != kWreckwaterWireSchemaVersion) {
    result.error = WreckwaterCodecError::UnsupportedVersion;
    return result;
  }
  if (payloadType !=
      static_cast<uint32_t>(WreckwaterPayloadType::CertifiedSnapshot)) {
    result.error = WreckwaterCodecError::WrongPayloadType;
    return result;
  }
  if (payloadBytes < bytes.size()) {
    result.error = WreckwaterCodecError::TrailingBytes;
    return result;
  }
  if (payloadBytes > bytes.size()) {
    result.error = WreckwaterCodecError::Truncated;
    return result;
  }
  if (!allZero(snapshot.reserved)) {
    result.error = WreckwaterCodecError::ReservedNonZero;
    return result;
  }
  if (entityCount == 0u ||
      entityCount > kWreckwaterMaximumSnapshotEntities ||
      characterCount > kWreckwaterMaximumSnapshotCharacters) {
    result.error = WreckwaterCodecError::InvalidCount;
    return result;
  }
  size_t expectedBytes = 0u;
  if (!wreckwaterSnapshotPayloadBytes(
          entityCount, characterCount, expectedBytes)) {
    result.error = WreckwaterCodecError::SizeOverflow;
    return result;
  }
  if (payloadBytes != expectedBytes || bytes.size() != expectedBytes) {
    result.error = WreckwaterCodecError::LengthMismatch;
    return result;
  }
  const uint64_t expectedHash = wreckwaterSnapshotSerializedByteHash(bytes);
  if (snapshot.serializedByteHash != expectedHash) {
    result.error = WreckwaterCodecError::HashMismatch;
    return result;
  }

  snapshot.schemaVersion = schema;
  snapshot.phase = static_cast<WreckwaterPhase>(phase);
  snapshot.outcome = static_cast<WreckwaterOutcomeType>(outcome);
  snapshot.winner = static_cast<WreckwaterCrew>(winner);
  const WreckwaterCodecError headerError = validateSnapshotHeader(snapshot);
  if (headerError != WreckwaterCodecError::None &&
      headerError != WreckwaterCodecError::InvalidCount) {
    result.error = headerError;
    return result;
  }

  // Allocation is deliberately after magic, version, exact length, count,
  // reserved-field, and byte-hash validation. The public count cap limits
  // these allocations to 64 entities and four characters.
  snapshot.entities.resize(entityCount);
  for (WreckwaterEntityState &entity : snapshot.entities) {
    if (!readEntity(reader, entity)) {
      result.error = WreckwaterCodecError::Truncated;
      return result;
    }
  }
  snapshot.characters.resize(characterCount);
  for (WreckwaterCharacterState &character : snapshot.characters) {
    if (!readCharacter(reader, character)) {
      result.error = WreckwaterCodecError::Truncated;
      return result;
    }
  }
  if (reader.remaining() != 0u) {
    result.error = WreckwaterCodecError::TrailingBytes;
    return result;
  }
  const WreckwaterCodecError entityError =
      validateSnapshotEntities(
          snapshot.entities, snapshot.characters);
  if (entityError != WreckwaterCodecError::None) {
    result.error = entityError;
    return result;
  }
  result.snapshot = std::move(snapshot);
  return result;
}

} // namespace voxy::network
