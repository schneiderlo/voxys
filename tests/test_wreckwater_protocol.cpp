#include "network/protocol.hpp"
#include "network/wreckwater_protocol.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <type_traits>
#include <utility>
#include <vector>

namespace voxy::network {
namespace {

#define VOXY_DEFINE_MEMBER_DETECTOR(name)                                      \
  template <typename T, typename = void>                                       \
  struct HasMember_##name : std::false_type {};                                \
  template <typename T>                                                        \
  struct HasMember_##name<T, std::void_t<decltype(std::declval<T &>().name)>>  \
      : std::true_type {}

VOXY_DEFINE_MEMBER_DETECTOR(playerId);
VOXY_DEFINE_MEMBER_DETECTOR(connectionId);
VOXY_DEFINE_MEMBER_DETECTOR(connectionGeneration);
VOXY_DEFINE_MEMBER_DETECTOR(crew);
VOXY_DEFINE_MEMBER_DETECTOR(seat);
VOXY_DEFINE_MEMBER_DETECTOR(skiffId);
VOXY_DEFINE_MEMBER_DETECTOR(matchId);
VOXY_DEFINE_MEMBER_DETECTOR(worldId);
VOXY_DEFINE_MEMBER_DETECTOR(worldEpoch);
VOXY_DEFINE_MEMBER_DETECTOR(authorityEpoch);
VOXY_DEFINE_MEMBER_DETECTOR(physicsEvidenceTick);
VOXY_DEFINE_MEMBER_DETECTOR(body);
VOXY_DEFINE_MEMBER_DETECTOR(bodyHandle);
VOXY_DEFINE_MEMBER_DETECTOR(attachmentId);
VOXY_DEFINE_MEMBER_DETECTOR(attachmentHandle);

#undef VOXY_DEFINE_MEMBER_DETECTOR

static_assert(!HasMember_playerId<WreckwaterActionRequest>::value);
static_assert(!HasMember_connectionId<WreckwaterActionRequest>::value);
static_assert(!HasMember_connectionGeneration<WreckwaterActionRequest>::value);
static_assert(!HasMember_crew<WreckwaterActionRequest>::value);
static_assert(!HasMember_seat<WreckwaterActionRequest>::value);
static_assert(!HasMember_skiffId<WreckwaterActionRequest>::value);
static_assert(!HasMember_matchId<WreckwaterActionRequest>::value);
static_assert(!HasMember_worldId<WreckwaterActionRequest>::value);
static_assert(!HasMember_worldEpoch<WreckwaterActionRequest>::value);
static_assert(!HasMember_authorityEpoch<WreckwaterActionRequest>::value);
static_assert(!HasMember_physicsEvidenceTick<WreckwaterActionRequest>::value);
static_assert(!HasMember_body<WreckwaterActionRequest>::value);
static_assert(!HasMember_bodyHandle<WreckwaterActionRequest>::value);
static_assert(!HasMember_attachmentId<WreckwaterActionRequest>::value);
static_assert(!HasMember_attachmentHandle<WreckwaterActionRequest>::value);

constexpr size_t kSnapshotEntityOffset = kWreckwaterSnapshotHeaderBytes;
constexpr size_t kEntityNetIdOffset = 0u;
constexpr size_t kEntityNetGenerationOffset = 8u;
constexpr size_t kEntityLocalPositionOffset = 32u;
constexpr size_t kEntityQuaternionOffset = 44u;

void writeU16(std::vector<std::byte> &bytes, size_t offset, uint16_t value) {
  ASSERT_LE(offset + sizeof(uint16_t), bytes.size());
  for (uint32_t byte = 0u; byte < 2u; ++byte) {
    bytes[offset + byte] =
        std::byte{static_cast<uint8_t>(value >> (byte * 8u))};
  }
}

void writeU32(std::vector<std::byte> &bytes, size_t offset, uint32_t value) {
  ASSERT_LE(offset + sizeof(uint32_t), bytes.size());
  for (uint32_t byte = 0u; byte < 4u; ++byte) {
    bytes[offset + byte] =
        std::byte{static_cast<uint8_t>(value >> (byte * 8u))};
  }
}

void writeU64(std::vector<std::byte> &bytes, size_t offset, uint64_t value) {
  ASSERT_LE(offset + sizeof(uint64_t), bytes.size());
  for (uint32_t byte = 0u; byte < 8u; ++byte) {
    bytes[offset + byte] =
        std::byte{static_cast<uint8_t>(value >> (byte * 8u))};
  }
}

void writeF32(std::vector<std::byte> &bytes, size_t offset, float value) {
  writeU32(bytes, offset, std::bit_cast<uint32_t>(value));
}

uint64_t readU64(std::span<const std::byte> bytes, size_t offset) {
  EXPECT_LE(offset + sizeof(uint64_t), bytes.size());
  uint64_t value = 0u;
  for (uint32_t byte = 0u; byte < 8u; ++byte) {
    value |= std::to_integer<uint64_t>(bytes[offset + byte]) << (byte * 8u);
  }
  return value;
}

void refreshSnapshotHash(std::vector<std::byte> &bytes) {
  writeU64(bytes, kWreckwaterSnapshotByteHashOffset, 0u);
  writeU64(bytes, kWreckwaterSnapshotByteHashOffset,
           wreckwaterSnapshotSerializedByteHash(bytes));
}

WreckwaterEntityState makeSkiff(NetEntityId netId, uint32_t netGeneration,
                                uint32_t skiffId, uint32_t skiffGeneration,
                                WreckwaterCrew crew, float x) {
  WreckwaterEntityState entity;
  entity.netEntityId = netId;
  entity.netGeneration = netGeneration;
  entity.kind = WreckwaterEntityKind::Skiff;
  entity.crew = crew;
  entity.sector = {2, -1, 4};
  entity.localPosition = {x, 1.5f, -7.25f};
  entity.orientation = {0.0f, 0.38268343f, 0.0f, 0.9238795f};
  entity.linearVelocity = {4.0f, 0.0f, -1.0f};
  entity.angularVelocity = {0.0f, 0.25f, 0.0f};
  entity.shape = WreckwaterShape::Compound;
  entity.dimensions = {3.5f, 1.25f, 6.0f};
  entity.packedMaterialFlags =
      crew == WreckwaterCrew::CrewOne ? 0xa123'4567u : 0xa765'4321u;
  entity.skiff = {
      .skiffId = skiffId,
      .generation = skiffGeneration,
      .disposition = WreckwaterSkiffDisposition::Active,
  };
  return entity;
}

WreckwaterEntityState makeCargo() {
  WreckwaterEntityState entity;
  entity.netEntityId = 20u;
  entity.netGeneration = 9u;
  entity.kind = WreckwaterEntityKind::Cargo;
  entity.crew = WreckwaterCrew::CrewOne;
  entity.sector = {2, -1, 4};
  entity.localPosition = {-2.0f, 0.75f, 3.0f};
  entity.orientation = {0.0f, 0.0f, 0.0f, 1.0f};
  entity.linearVelocity = {3.0f, -0.25f, -1.0f};
  entity.angularVelocity = {0.1f, 0.2f, 0.3f};
  entity.shape = WreckwaterShape::Cylinder;
  entity.dimensions = {1.5f, 2.0f, 1.5f};
  entity.packedMaterialFlags = 0xa246'8aceu;
  entity.cargo = {
      .cargoId = 1u,
      .generation = 3u,
      .revision = 7u,
      .disposition = WreckwaterCargoDisposition::Towed,
      .ownerCrew = WreckwaterCrew::CrewOne,
      .towingSkiffId = 11u,
      .towingSkiffGeneration = 4u,
  };
  entity.attachment = {
      .attachmentId = 9'001u,
      .generation = 6u,
      .state = WreckwaterAttachmentState::Attached,
  };
  return entity;
}

WreckwaterCharacterState makeCharacter(
    uint32_t playerId, uint32_t skiffId,
    uint32_t skiffGeneration, float x) {
  WreckwaterCharacterState character;
  character.characterHandle = (1u << 16u) | playerId;
  character.stateFlags =
      static_cast<uint32_t>(WreckwaterCharacterMode::OnSkiff) |
      kWreckwaterCharacterStateActiveFlag |
      kWreckwaterCharacterStateConnectedFlag;
  character.playerId = playerId;
  character.connectionGeneration = 3u;
  character.sector = {2, -1, 4};
  character.localFeetPosition = {x, 2.75f, -6.0f};
  character.worldVelocity = {2.0f, 0.0f, -1.0f};
  character.skiffId = skiffId;
  character.skiffGeneration = skiffGeneration;
  character.skiffLocalFeetPosition = {x * 0.1f, 0.75f, 1.0f};
  character.skiffLocalVelocity = {1.0f, 0.0f, -0.5f};
  character.lastAppliedCharacterInputSequence = 100u + playerId;
  return character;
}

WreckwaterCertifiedSnapshot makeSnapshot() {
  WreckwaterCertifiedSnapshot snapshot;
  snapshot.sessionId = 0x0102'0304'0506'0708ull;
  snapshot.matchId = 0x1112'1314'1516'1718ull;
  snapshot.worldId = 0x2122'2324'2526'2728ull;
  snapshot.worldEpoch = 31u;
  snapshot.authorityEpoch = 41u;
  snapshot.snapshotSequence = 51u;
  snapshot.applicationTick = 720u;
  snapshot.physicsEvidenceTick = 718u;
  snapshot.phase = WreckwaterPhase::Live;
  snapshot.crewOneScore = 1'000u;
  snapshot.crewTwoScore = 0u;
  snapshot.outcome = WreckwaterOutcomeType::Undecided;
  snapshot.winner = WreckwaterCrew::None;
  snapshot.matchStateHash = 0x89ab'cdefu;
  snapshot.eventStreamHash = 0x1020'3040u;
  // Deliberately noncanonical input order. The encoder must sort by stable
  // network identity without changing the caller's vector.
  snapshot.entities = {
      makeSkiff(30u, 8u, 22u, 5u, WreckwaterCrew::CrewTwo, 12.0f),
      makeCargo(),
      makeSkiff(10u, 7u, 11u, 4u, WreckwaterCrew::CrewOne, -12.0f),
  };
  snapshot.characters = {
      makeCharacter(4u, 22u, 5u, 1.5f),
      makeCharacter(2u, 11u, 4u, -1.5f),
      makeCharacter(3u, 22u, 5u, 0.5f),
      makeCharacter(1u, 11u, 4u, -0.5f),
  };
  return snapshot;
}

TEST(WreckwaterActionRequestCodecTest, GoldenLittleEndianBytesAndRoundTrip) {
  WreckwaterActionRequest request;
  request.requestedApplicationTick = 0x0000'0000'0005'0607ull;
  request.clientRequestSequence = 0x1112'1314'1516'1718ull;
  request.action = WreckwaterAction::Steal;
  request.cargoId = 0x2122'2324u;
  request.cargoGeneration = 0x3132'3334u;
  request.observedCargoRevision = 0x4142'4344u;

  const WreckwaterWriteResult encoded =
      WreckwaterActionRequestCodec::encode(request);
  ASSERT_TRUE(encoded) << wreckwaterCodecErrorName(encoded.error);
  constexpr std::array<uint8_t, kWreckwaterActionRequestBytes> golden{
      0x56, 0x4f, 0x58, 0x59, 0x57, 0x52, 0x45, 0x51, 0x05, 0x00, 0x00,
      0x00, 0x01, 0x00, 0x00, 0x00, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x07, 0x06, 0x05, 0x00, 0x00, 0x00, 0x00, 0x00, 0x18,
      0x17, 0x16, 0x15, 0x14, 0x13, 0x12, 0x11, 0x03, 0x00, 0x00, 0x00,
      0x24, 0x23, 0x22, 0x21, 0x34, 0x33, 0x32, 0x31, 0x44, 0x43, 0x42,
      0x41, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  };
  ASSERT_EQ(encoded.bytes.size(), golden.size());
  for (size_t index = 0u; index < golden.size(); ++index) {
    EXPECT_EQ(std::to_integer<uint8_t>(encoded.bytes[index]), golden[index])
        << "byte " << index;
  }
  // Golden guards accidental schema drift. Update only with an explicit
  // schema-version change and a reviewed migration.
  EXPECT_EQ(encoded.serializedByteHash, 8'983'209'211'921'191'173ull);

  const WreckwaterActionReadResult decoded =
      WreckwaterActionRequestCodec::decode(encoded.bytes);
  ASSERT_TRUE(decoded) << wreckwaterCodecErrorName(decoded.error);
  EXPECT_EQ(*decoded.request, request);
}

TEST(WreckwaterActionRequestCodecTest,
     RejectsTruncationTrailingReservedAndInvalidRanges) {
  WreckwaterActionRequest request;
  request.requestedApplicationTick = 7u;
  request.clientRequestSequence = 9u;
  request.action = WreckwaterAction::Tow;
  request.cargoId = 1u;
  request.cargoGeneration = 2u;
  request.observedCargoRevision = 3u;
  const auto encoded = WreckwaterActionRequestCodec::encode(request);
  ASSERT_TRUE(encoded);

  for (size_t length = 0u; length < encoded.bytes.size(); ++length) {
    EXPECT_FALSE(WreckwaterActionRequestCodec::decode(
        std::span<const std::byte>(encoded.bytes).first(length)));
  }
  std::vector<std::byte> trailing = encoded.bytes;
  trailing.push_back(std::byte{0u});
  EXPECT_EQ(WreckwaterActionRequestCodec::decode(trailing).error,
            WreckwaterCodecError::TrailingBytes);

  std::vector<std::byte> malformed = encoded.bytes;
  writeU32(malformed, 20u, 1u);
  EXPECT_EQ(WreckwaterActionRequestCodec::decode(malformed).error,
            WreckwaterCodecError::ReservedNonZero);
  malformed = encoded.bytes;
  writeU32(malformed, 40u, 99u);
  EXPECT_EQ(WreckwaterActionRequestCodec::decode(malformed).error,
            WreckwaterCodecError::InvalidAction);
  malformed = encoded.bytes;
  writeU64(malformed, 24u, kWreckwaterMaximumApplicationTick + 1u);
  EXPECT_EQ(WreckwaterActionRequestCodec::decode(malformed).error,
            WreckwaterCodecError::InvalidTick);

  request.controlReserved = 1u;
  EXPECT_EQ(WreckwaterActionRequestCodec::encode(request).error,
            WreckwaterCodecError::ReservedNonZero);
  request.controlReserved = 0u;
  request.clientRequestSequence = 0u;
  EXPECT_EQ(WreckwaterActionRequestCodec::encode(request).error,
            WreckwaterCodecError::InvalidIdentity);
}

TEST(WreckwaterActionRequestCodecTest,
     HelmRoundTripsSignedQ15AndRejectsAmbiguousFields) {
  WreckwaterActionRequest request;
  request.requestedApplicationTick = 23u;
  request.clientRequestSequence = 41u;
  request.action = WreckwaterAction::Helm;
  request.helmThrottleQ15 = kWreckwaterHelmAxisMaximum;
  request.helmSteeringQ15 = -12'345;

  const auto encoded = WreckwaterActionRequestCodec::encode(request);
  ASSERT_TRUE(encoded) << wreckwaterCodecErrorName(encoded.error);
  EXPECT_EQ(std::to_integer<uint8_t>(encoded.bytes[56]), 0xffu);
  EXPECT_EQ(std::to_integer<uint8_t>(encoded.bytes[57]), 0x7fu);
  EXPECT_EQ(std::to_integer<uint8_t>(encoded.bytes[58]), 0xc7u);
  EXPECT_EQ(std::to_integer<uint8_t>(encoded.bytes[59]), 0xcfu);
  const auto decoded = WreckwaterActionRequestCodec::decode(encoded.bytes);
  ASSERT_TRUE(decoded) << wreckwaterCodecErrorName(decoded.error);
  EXPECT_EQ(*decoded.request, request);

  request.helmThrottleQ15 = std::numeric_limits<int16_t>::min();
  EXPECT_EQ(WreckwaterActionRequestCodec::encode(request).error,
            WreckwaterCodecError::InvalidControl);
  std::vector<std::byte> malformed = encoded.bytes;
  writeU16(malformed, 56u, 0x8000u);
  EXPECT_EQ(WreckwaterActionRequestCodec::decode(malformed).error,
            WreckwaterCodecError::InvalidControl);
  request.helmThrottleQ15 = 0;
  request.cargoId = 1u;
  request.cargoGeneration = 1u;
  request.observedCargoRevision = 1u;
  EXPECT_EQ(WreckwaterActionRequestCodec::encode(request).error,
            WreckwaterCodecError::InvalidIdentity);

  request.action = WreckwaterAction::Tow;
  request.helmSteeringQ15 = 1;
  EXPECT_EQ(WreckwaterActionRequestCodec::encode(request).error,
            WreckwaterCodecError::InvalidControl);

  malformed = encoded.bytes;
  writeU32(malformed, 60u, 1u);
  EXPECT_EQ(WreckwaterActionRequestCodec::decode(malformed).error,
            WreckwaterCodecError::ReservedNonZero);
}

TEST(WreckwaterActionRequestCodecTest,
     RejectsUnknownEnvelopeVersionTypeLengthAndMagic) {
  WreckwaterActionRequest request;
  request.requestedApplicationTick = 7u;
  request.clientRequestSequence = 9u;
  request.action = WreckwaterAction::Cut;
  request.cargoId = 1u;
  request.cargoGeneration = 2u;
  request.observedCargoRevision = 3u;
  const auto encoded = WreckwaterActionRequestCodec::encode(request);
  ASSERT_TRUE(encoded);

  std::vector<std::byte> malformed = encoded.bytes;
  malformed[0] ^= std::byte{0x01u};
  EXPECT_EQ(WreckwaterActionRequestCodec::decode(malformed).error,
            WreckwaterCodecError::InvalidMagic);
  malformed = encoded.bytes;
  writeU32(malformed, 8u, kWreckwaterWireSchemaVersion + 1u);
  EXPECT_EQ(WreckwaterActionRequestCodec::decode(malformed).error,
            WreckwaterCodecError::UnsupportedVersion);
  malformed = encoded.bytes;
  writeU32(malformed, 12u,
           static_cast<uint32_t>(WreckwaterPayloadType::CertifiedSnapshot));
  EXPECT_EQ(WreckwaterActionRequestCodec::decode(malformed).error,
            WreckwaterCodecError::WrongPayloadType);
  malformed = encoded.bytes;
  writeU32(malformed, 16u,
           static_cast<uint32_t>(kWreckwaterActionRequestBytes - 1u));
  EXPECT_EQ(WreckwaterActionRequestCodec::decode(malformed).error,
            WreckwaterCodecError::LengthMismatch);
}

TEST(WreckwaterCharacterInputRequestCodecTest,
     GoldenLittleEndianRoundTripAndHostileValidation) {
  WreckwaterCharacterInputRequest request;
  request.requestedApplicationTick = 0x0000'0000'0005'0607ull;
  request.clientRequestSequence = 0x1112'1314'1516'1718ull;
  request.characterHandle = 0x2122'2324u;
  request.connectionGeneration = 0x3132'3334u;
  request.moveXQ15 = 0x1234;
  request.moveZQ15 = -12'345;
  request.inputFlags =
      kWreckwaterCharacterInputJumpFlag |
      kWreckwaterCharacterInputBoardFlag;
  request.characterInputSequence =
      0x4142'4344'5152'5354ull;
  request.redundantInputCount = 3u;
  request.redundantInputs = {{
      {
          .requestedApplicationTick = 1u,
          .characterInputSequence = 1u,
          .moveXQ15 = 101,
          .moveZQ15 = -101,
          .inputFlags = kWreckwaterCharacterInputJumpFlag,
      },
      {
          .requestedApplicationTick = 2u,
          .characterInputSequence = 2u,
          .moveXQ15 = 202,
          .moveZQ15 = -202,
          .inputFlags = kWreckwaterCharacterInputBoardFlag,
      },
      {
          .requestedApplicationTick = 3u,
          .characterInputSequence = 3u,
          .moveXQ15 = 303,
          .moveZQ15 = -303,
      },
  }};

  const WreckwaterWriteResult encoded =
      WreckwaterCharacterInputRequestCodec::encode(request);
  ASSERT_TRUE(encoded) << wreckwaterCodecErrorName(encoded.error);
  ASSERT_EQ(encoded.bytes.size(), kWreckwaterCharacterInputRequestBytes);
  EXPECT_EQ(std::to_integer<uint8_t>(encoded.bytes[8]), 0x05u);
  EXPECT_EQ(std::to_integer<uint8_t>(encoded.bytes[12]), 0x03u);
  EXPECT_EQ(std::to_integer<uint8_t>(encoded.bytes[48]), 0x34u);
  EXPECT_EQ(std::to_integer<uint8_t>(encoded.bytes[49]), 0x12u);
  EXPECT_EQ(std::to_integer<uint8_t>(encoded.bytes[50]), 0xc7u);
  EXPECT_EQ(std::to_integer<uint8_t>(encoded.bytes[51]), 0xcfu);
  EXPECT_EQ(std::to_integer<uint8_t>(encoded.bytes[56]), 0x54u);
  EXPECT_EQ(std::to_integer<uint8_t>(encoded.bytes[63]), 0x41u);
  EXPECT_EQ(std::to_integer<uint8_t>(encoded.bytes[64]), 0x03u);
  EXPECT_EQ(std::to_integer<uint8_t>(encoded.bytes[72]), 0x01u);
  EXPECT_EQ(std::to_integer<uint8_t>(encoded.bytes[80]), 0x01u);
  EXPECT_EQ(encoded.serializedByteHash, 8'163'336'535'766'063'300ull);

  const WreckwaterCharacterInputReadResult decoded =
      WreckwaterCharacterInputRequestCodec::decode(encoded.bytes);
  ASSERT_TRUE(decoded) << wreckwaterCodecErrorName(decoded.error);
  EXPECT_EQ(*decoded.request, request);

  for (size_t length = 0u; length < encoded.bytes.size(); ++length) {
    EXPECT_FALSE(WreckwaterCharacterInputRequestCodec::decode(
        std::span<const std::byte>(encoded.bytes).first(length)));
  }
  std::vector<std::byte> malformed = encoded.bytes;
  malformed.push_back(std::byte{0u});
  EXPECT_EQ(WreckwaterCharacterInputRequestCodec::decode(malformed).error,
            WreckwaterCodecError::TrailingBytes);
  malformed = encoded.bytes;
  writeU32(malformed, 8u, 4u);
  EXPECT_EQ(WreckwaterCharacterInputRequestCodec::decode(malformed).error,
            WreckwaterCodecError::UnsupportedVersion);
  malformed = encoded.bytes;
  writeU16(malformed, 48u, 0x8000u);
  EXPECT_EQ(WreckwaterCharacterInputRequestCodec::decode(malformed).error,
            WreckwaterCodecError::InvalidControl);
  malformed = encoded.bytes;
  writeU32(malformed, 52u, 1u << 31u);
  EXPECT_EQ(WreckwaterCharacterInputRequestCodec::decode(malformed).error,
            WreckwaterCodecError::InvalidControl);
  malformed = encoded.bytes;
  writeU32(malformed, 56u, 0u);
  writeU32(malformed, 60u, 0u);
  EXPECT_EQ(WreckwaterCharacterInputRequestCodec::decode(malformed).error,
            WreckwaterCodecError::InvalidIdentity);
  malformed = encoded.bytes;
  writeU32(
      malformed, 64u,
      kWreckwaterCharacterInputMaximumRedundantSamples + 1u);
  EXPECT_EQ(WreckwaterCharacterInputRequestCodec::decode(malformed).error,
            WreckwaterCodecError::InvalidCount);
  malformed = encoded.bytes;
  writeU32(malformed, 68u, 1u);
  EXPECT_EQ(WreckwaterCharacterInputRequestCodec::decode(malformed).error,
            WreckwaterCodecError::ReservedNonZero);
  malformed = encoded.bytes;
  writeU32(malformed, 104u, 1u);
  writeU32(malformed, 108u, 0u);
  EXPECT_EQ(WreckwaterCharacterInputRequestCodec::decode(malformed).error,
            WreckwaterCodecError::InvalidIdentity);
  WreckwaterCharacterInputRequest nonzeroUnused = request;
  nonzeroUnused.redundantInputCount = 1u;
  EXPECT_EQ(
      WreckwaterCharacterInputRequestCodec::encode(nonzeroUnused).error,
      WreckwaterCodecError::ReservedNonZero);
  WreckwaterCharacterInputRequest regressingTick = request;
  regressingTick.requestedApplicationTick = 2u;
  EXPECT_EQ(
      WreckwaterCharacterInputRequestCodec::encode(regressingTick).error,
      WreckwaterCodecError::InvalidTick);
  request.characterHandle = 0u;
  EXPECT_EQ(WreckwaterCharacterInputRequestCodec::encode(request).error,
            WreckwaterCodecError::InvalidIdentity);
}

TEST(WreckwaterSnapshotCodecTest,
     CanonicalRoundTripHasGoldenHashAndFitsRealtimeOuterPacket) {
  const WreckwaterCertifiedSnapshot source = makeSnapshot();
  const WreckwaterWriteResult encoded = WreckwaterSnapshotCodec::encode(source);
  ASSERT_TRUE(encoded) << wreckwaterCodecErrorName(encoded.error);
  EXPECT_EQ(encoded.bytes.size(), kWreckwaterFirstSliceSnapshotBytes);
  EXPECT_EQ(encoded.bytes.size() + kNetworkPacketOverheadBytes, 1'124u);
  EXPECT_LE(encoded.bytes.size() + kNetworkPacketOverheadBytes,
            kConservativeRealtimeMtu);
  EXPECT_EQ(kNetworkPacketOverheadBytes, kWreckwaterOuterPacketProofBytes);
  EXPECT_EQ(kConservativeRealtimeMtu, kWreckwaterRealtimeMtuProofBytes);
  EXPECT_EQ(readU64(encoded.bytes, kWreckwaterSnapshotByteHashOffset),
            encoded.serializedByteHash);
  EXPECT_EQ(encoded.serializedByteHash, 2'832'980'362'156'531'989ull);

  const WreckwaterSnapshotReadResult decoded =
      WreckwaterSnapshotCodec::decode(encoded.bytes);
  ASSERT_TRUE(decoded) << wreckwaterCodecErrorName(decoded.error);
  WreckwaterCertifiedSnapshot expected = source;
  ASSERT_TRUE(canonicalizeWreckwaterSnapshot(expected));
  expected.serializedByteHash = encoded.serializedByteHash;
  EXPECT_EQ(*decoded.snapshot, expected);
  ASSERT_EQ(decoded.snapshot->entities.size(), 3u);
  EXPECT_EQ(decoded.snapshot->entities[0].netEntityId, 10u);
  EXPECT_EQ(decoded.snapshot->entities[1].netEntityId, 20u);
  EXPECT_EQ(decoded.snapshot->entities[2].netEntityId, 30u);
  ASSERT_EQ(decoded.snapshot->characters.size(), 4u);
  EXPECT_EQ(decoded.snapshot->characters[0].playerId, 1u);
  EXPECT_EQ(decoded.snapshot->characters[3].playerId, 4u);

  const WreckwaterWriteResult reencoded =
      WreckwaterSnapshotCodec::encode(*decoded.snapshot);
  ASSERT_TRUE(reencoded);
  EXPECT_EQ(reencoded.bytes, encoded.bytes);
  EXPECT_EQ(reencoded.serializedByteHash, encoded.serializedByteHash);

  Packet packet;
  packet.header.payloadType = PacketPayloadType::Snapshot;
  packet.header.sessionId = source.sessionId;
  packet.header.worldId = source.worldId;
  packet.header.worldEpoch = source.worldEpoch;
  packet.header.authorityEpoch = source.authorityEpoch;
  packet.header.sequence = source.snapshotSequence;
  packet.header.tick = source.applicationTick;
  packet.payload = encoded.bytes;
  const PacketWriteResult outer =
      PacketCodec::encode(packet, DeliveryClass::Realtime);
  ASSERT_TRUE(outer.error.empty()) << outer.error;
  EXPECT_EQ(outer.bytes.size(), 1'124u);
  const PacketReadResult outerDecoded =
      PacketCodec::decode(outer.bytes, DeliveryClass::Realtime);
  ASSERT_TRUE(outerDecoded.packet.has_value()) << outerDecoded.error;
  EXPECT_TRUE(WreckwaterSnapshotCodec::decode(outerDecoded.packet->payload));
}

TEST(WreckwaterSnapshotCodecTest,
     QuaternionSignAndNegativeZeroProduceIdenticalBytes) {
  WreckwaterCertifiedSnapshot first = makeSnapshot();
  const WreckwaterWriteResult firstEncoded =
      WreckwaterSnapshotCodec::encode(first);
  ASSERT_TRUE(firstEncoded);

  WreckwaterCertifiedSnapshot equivalent = makeSnapshot();
  std::reverse(equivalent.entities.begin(), equivalent.entities.end());
  for (WreckwaterEntityState &entity : equivalent.entities) {
    entity.orientation.x = -entity.orientation.x;
    entity.orientation.y = -entity.orientation.y;
    entity.orientation.z = -entity.orientation.z;
    entity.orientation.w = -entity.orientation.w;
    if (entity.localPosition.y == 0.0f) {
      entity.localPosition.y = -0.0f;
    }
    if (entity.linearVelocity.y == 0.0f) {
      entity.linearVelocity.y = -0.0f;
    }
    if (entity.angularVelocity.x == 0.0f) {
      entity.angularVelocity.x = -0.0f;
    }
  }
  const WreckwaterWriteResult equivalentEncoded =
      WreckwaterSnapshotCodec::encode(equivalent);
  ASSERT_TRUE(equivalentEncoded)
      << wreckwaterCodecErrorName(equivalentEncoded.error);
  EXPECT_EQ(equivalentEncoded.bytes, firstEncoded.bytes);
  EXPECT_EQ(equivalentEncoded.serializedByteHash,
            firstEncoded.serializedByteHash);

  WreckwaterQuaternion halfTurn{-2.0f, -0.0f, -0.0f, -0.0f};
  ASSERT_TRUE(canonicalizeWreckwaterQuaternion(halfTurn));
  EXPECT_FLOAT_EQ(halfTurn.x, 1.0f);
  EXPECT_FALSE(std::signbit(halfTurn.y));
  EXPECT_FALSE(std::signbit(halfTurn.z));
  EXPECT_FALSE(std::signbit(halfTurn.w));
}

TEST(WreckwaterSnapshotCodecTest,
     RejectsNonfiniteHugeOutOfBoundsAndBadQuaternionsBeforeEncode) {
  const std::array<float, 3> invalidValues{
      std::numeric_limits<float>::quiet_NaN(),
      std::numeric_limits<float>::infinity(),
      1.0e30f,
  };
  for (float invalid : invalidValues) {
    WreckwaterCertifiedSnapshot snapshot = makeSnapshot();
    snapshot.entities[0].linearVelocity.x = invalid;
    EXPECT_FALSE(WreckwaterSnapshotCodec::encode(snapshot));

    snapshot = makeSnapshot();
    snapshot.entities[0].dimensions.z = invalid;
    EXPECT_FALSE(WreckwaterSnapshotCodec::encode(snapshot));

    snapshot = makeSnapshot();
    snapshot.entities[0].orientation.w = invalid;
    EXPECT_FALSE(WreckwaterSnapshotCodec::encode(snapshot));
  }

  WreckwaterCertifiedSnapshot snapshot = makeSnapshot();
  snapshot.entities[0].localPosition.x = 128.0f;
  EXPECT_EQ(WreckwaterSnapshotCodec::encode(snapshot).error,
            WreckwaterCodecError::InvalidFloat);
  snapshot = makeSnapshot();
  snapshot.entities[0].sector[0] = kWreckwaterMaximumSectorMagnitude + 1;
  EXPECT_EQ(WreckwaterSnapshotCodec::encode(snapshot).error,
            WreckwaterCodecError::InvalidEntity);
  snapshot = makeSnapshot();
  snapshot.entities[0].orientation = {0.0f, 0.0f, 0.0f, 0.0f};
  EXPECT_EQ(WreckwaterSnapshotCodec::encode(snapshot).error,
            WreckwaterCodecError::InvalidQuaternion);
}

TEST(WreckwaterSnapshotCodecTest,
     RejectsInvalidHeaderIdentityTicksPhaseOutcomeScoreAndReserved) {
  WreckwaterCertifiedSnapshot snapshot = makeSnapshot();
  snapshot.sessionId = 0u;
  EXPECT_EQ(WreckwaterSnapshotCodec::encode(snapshot).error,
            WreckwaterCodecError::InvalidIdentity);
  snapshot = makeSnapshot();
  snapshot.physicsEvidenceTick = snapshot.applicationTick + 1u;
  EXPECT_EQ(WreckwaterSnapshotCodec::encode(snapshot).error,
            WreckwaterCodecError::InvalidTick);
  snapshot = makeSnapshot();
  snapshot.physicsEvidenceTick =
      snapshot.applicationTick -
      (kWreckwaterMaximumPhysicsEvidenceLagTicks + 1u);
  EXPECT_EQ(WreckwaterSnapshotCodec::encode(snapshot).error,
            WreckwaterCodecError::InvalidTick);
  snapshot = makeSnapshot();
  snapshot.phase = static_cast<WreckwaterPhase>(99u);
  EXPECT_EQ(WreckwaterSnapshotCodec::encode(snapshot).error,
            WreckwaterCodecError::InvalidPhase);
  snapshot = makeSnapshot();
  snapshot.outcome = WreckwaterOutcomeType::CrewVictory;
  snapshot.winner = WreckwaterCrew::None;
  EXPECT_EQ(WreckwaterSnapshotCodec::encode(snapshot).error,
            WreckwaterCodecError::InvalidOutcome);
  snapshot = makeSnapshot();
  snapshot.phase = WreckwaterPhase::Finished;
  EXPECT_EQ(WreckwaterSnapshotCodec::encode(snapshot).error,
            WreckwaterCodecError::InvalidOutcome);
  snapshot = makeSnapshot();
  snapshot.phase = WreckwaterPhase::Finished;
  snapshot.outcome = WreckwaterOutcomeType::Tie;
  EXPECT_EQ(WreckwaterSnapshotCodec::encode(snapshot).error,
            WreckwaterCodecError::InvalidOutcome);
  snapshot = makeSnapshot();
  snapshot.phase = WreckwaterPhase::Finished;
  snapshot.outcome = WreckwaterOutcomeType::CrewVictory;
  snapshot.winner = WreckwaterCrew::CrewTwo;
  EXPECT_EQ(WreckwaterSnapshotCodec::encode(snapshot).error,
            WreckwaterCodecError::InvalidOutcome);
  snapshot = makeSnapshot();
  snapshot.crewOneScore = kWreckwaterMaximumScore + 1u;
  EXPECT_EQ(WreckwaterSnapshotCodec::encode(snapshot).error,
            WreckwaterCodecError::InvalidArgument);
  snapshot = makeSnapshot();
  snapshot.reserved[0] = 1u;
  EXPECT_EQ(WreckwaterSnapshotCodec::encode(snapshot).error,
            WreckwaterCodecError::ReservedNonZero);
}

TEST(WreckwaterSnapshotCodecTest,
     RejectsWireNegativeZeroNonunitQuaternionAndNoncanonicalSign) {
  const WreckwaterWriteResult encoded =
      WreckwaterSnapshotCodec::encode(makeSnapshot());
  ASSERT_TRUE(encoded);

  std::vector<std::byte> malformed = encoded.bytes;
  writeU32(malformed, kSnapshotEntityOffset + kEntityLocalPositionOffset,
           0x8000'0000u);
  refreshSnapshotHash(malformed);
  EXPECT_EQ(WreckwaterSnapshotCodec::decode(malformed).error,
            WreckwaterCodecError::InvalidFloat);

  malformed = encoded.bytes;
  writeF32(malformed, kSnapshotEntityOffset + kEntityQuaternionOffset + 12u,
           0.5f);
  refreshSnapshotHash(malformed);
  EXPECT_EQ(WreckwaterSnapshotCodec::decode(malformed).error,
            WreckwaterCodecError::InvalidQuaternion);

  malformed = encoded.bytes;
  writeF32(malformed, kSnapshotEntityOffset + kEntityQuaternionOffset, 0.0f);
  writeF32(malformed, kSnapshotEntityOffset + kEntityQuaternionOffset + 4u,
           0.0f);
  writeF32(malformed, kSnapshotEntityOffset + kEntityQuaternionOffset + 8u,
           0.0f);
  writeF32(malformed, kSnapshotEntityOffset + kEntityQuaternionOffset + 12u,
           -1.0f);
  refreshSnapshotHash(malformed);
  EXPECT_EQ(WreckwaterSnapshotCodec::decode(malformed).error,
            WreckwaterCodecError::InvalidQuaternion);
}

TEST(WreckwaterSnapshotCodecTest,
     RejectsWireNaNInfinityHugeFloatUnknownShapeAndReservedBits) {
  const WreckwaterWriteResult encoded =
      WreckwaterSnapshotCodec::encode(makeSnapshot());
  ASSERT_TRUE(encoded);

  const std::array<float, 3> invalidValues{
      std::numeric_limits<float>::quiet_NaN(),
      std::numeric_limits<float>::infinity(),
      1.0e30f,
  };
  for (float invalid : invalidValues) {
    std::vector<std::byte> malformed = encoded.bytes;
    writeF32(malformed, kSnapshotEntityOffset + kEntityLocalPositionOffset,
             invalid);
    refreshSnapshotHash(malformed);
    EXPECT_EQ(WreckwaterSnapshotCodec::decode(malformed).error,
              WreckwaterCodecError::InvalidFloat);
  }

  std::vector<std::byte> malformed = encoded.bytes;
  writeU32(malformed, kSnapshotEntityOffset + 84u, 99u);
  refreshSnapshotHash(malformed);
  EXPECT_EQ(WreckwaterSnapshotCodec::decode(malformed).error,
            WreckwaterCodecError::InvalidEntity);

  malformed = encoded.bytes;
  writeU32(malformed, kSnapshotEntityOffset + 160u, 1u);
  refreshSnapshotHash(malformed);
  EXPECT_EQ(WreckwaterSnapshotCodec::decode(malformed).error,
            WreckwaterCodecError::InvalidEntity);
}

TEST(WreckwaterSnapshotCodecTest,
     RejectsDuplicateUnsortedAndStaleGenerationEntityIdentity) {
  const WreckwaterWriteResult encoded =
      WreckwaterSnapshotCodec::encode(makeSnapshot());
  ASSERT_TRUE(encoded);

  std::vector<std::byte> malformed = encoded.bytes;
  std::swap_ranges(
      malformed.begin() + static_cast<std::ptrdiff_t>(kSnapshotEntityOffset),
      malformed.begin() +
          static_cast<std::ptrdiff_t>(kSnapshotEntityOffset +
                                      kWreckwaterEntityStateBytes),
      malformed.begin() +
          static_cast<std::ptrdiff_t>(kSnapshotEntityOffset +
                                      kWreckwaterEntityStateBytes));
  refreshSnapshotHash(malformed);
  EXPECT_EQ(WreckwaterSnapshotCodec::decode(malformed).error,
            WreckwaterCodecError::UnsortedEntities);

  malformed = encoded.bytes;
  writeU64(malformed,
           kSnapshotEntityOffset + kWreckwaterEntityStateBytes +
               kEntityNetIdOffset,
           10u);
  refreshSnapshotHash(malformed);
  EXPECT_EQ(WreckwaterSnapshotCodec::decode(malformed).error,
            WreckwaterCodecError::DuplicateEntity);

  malformed = encoded.bytes;
  writeU32(malformed, kSnapshotEntityOffset + kEntityNetGenerationOffset, 0u);
  refreshSnapshotHash(malformed);
  EXPECT_EQ(WreckwaterSnapshotCodec::decode(malformed).error,
            WreckwaterCodecError::InvalidEntity);

  WreckwaterCertifiedSnapshot duplicateGeneration = makeSnapshot();
  duplicateGeneration.entities.push_back(
      makeSkiff(10u, 99u, 33u, 1u, WreckwaterCrew::CrewTwo, 1.0f));
  EXPECT_EQ(WreckwaterSnapshotCodec::encode(duplicateGeneration).error,
            WreckwaterCodecError::DuplicateEntity);
}

TEST(WreckwaterSnapshotCodecTest,
     RejectsBrokenLogicalReferencesAndDuplicateLogicalIds) {
  WreckwaterCertifiedSnapshot snapshot = makeSnapshot();
  snapshot.entities[1].cargo.towingSkiffGeneration = 999u;
  EXPECT_EQ(WreckwaterSnapshotCodec::encode(snapshot).error,
            WreckwaterCodecError::InvalidEntity);

  snapshot = makeSnapshot();
  snapshot.entities[0].skiff.skiffId = snapshot.entities[2].skiff.skiffId;
  EXPECT_EQ(WreckwaterSnapshotCodec::encode(snapshot).error,
            WreckwaterCodecError::DuplicateEntity);

  snapshot = makeSnapshot();
  snapshot.entities[0].attachment = {
      .attachmentId = 7u,
      .generation = 1u,
      .state = WreckwaterAttachmentState::Attached,
  };
  EXPECT_EQ(WreckwaterSnapshotCodec::encode(snapshot).error,
            WreckwaterCodecError::InvalidEntity);
}

TEST(WreckwaterSnapshotCodecTest,
     RejectsEveryTruncationEverySingleByteMutationAndTrailingBytes) {
  const WreckwaterWriteResult encoded =
      WreckwaterSnapshotCodec::encode(makeSnapshot());
  ASSERT_TRUE(encoded);

  for (size_t length = 0u; length < encoded.bytes.size(); ++length) {
    const auto decoded = WreckwaterSnapshotCodec::decode(
        std::span<const std::byte>(encoded.bytes).first(length));
    EXPECT_FALSE(decoded) << "accepted prefix " << length;
  }

  for (size_t index = 0u; index < encoded.bytes.size(); ++index) {
    std::vector<std::byte> mutated = encoded.bytes;
    mutated[index] ^= std::byte{0x01u};
    const auto decoded = WreckwaterSnapshotCodec::decode(mutated);
    EXPECT_FALSE(decoded) << "accepted mutation at byte " << index;
  }

  std::vector<std::byte> trailing = encoded.bytes;
  trailing.push_back(std::byte{0u});
  EXPECT_EQ(WreckwaterSnapshotCodec::decode(trailing).error,
            WreckwaterCodecError::TrailingBytes);
}

TEST(WreckwaterSnapshotCodecTest,
     HostileCountsCannotBypassFixedAllocationAndSizeLimits) {
  size_t bytes = 123u;
  EXPECT_TRUE(wreckwaterSnapshotPayloadBytes(
      kWreckwaterMaximumSnapshotEntities,
      kWreckwaterMaximumSnapshotCharacters, bytes));
  EXPECT_EQ(bytes, kWreckwaterSnapshotHeaderBytes +
                       kWreckwaterMaximumSnapshotEntities *
                           kWreckwaterEntityStateBytes +
                       kWreckwaterMaximumSnapshotCharacters *
                           kWreckwaterCharacterStateBytes);
  EXPECT_FALSE(wreckwaterSnapshotPayloadBytes(
      kWreckwaterMaximumSnapshotEntities + 1u, 0u, bytes));
  EXPECT_EQ(bytes, 0u);
  EXPECT_FALSE(wreckwaterSnapshotPayloadBytes(
      std::numeric_limits<size_t>::max(), 0u, bytes));
  EXPECT_EQ(bytes, 0u);

  const WreckwaterWriteResult encoded =
      WreckwaterSnapshotCodec::encode(makeSnapshot());
  ASSERT_TRUE(encoded);
  std::vector<std::byte> hostile = encoded.bytes;
  writeU32(hostile, 108u, std::numeric_limits<uint32_t>::max());
  EXPECT_EQ(WreckwaterSnapshotCodec::decode(hostile).error,
            WreckwaterCodecError::InvalidCount);
}

TEST(WreckwaterSnapshotCodecTest,
     DeterministicStressRoundTripsWithoutSchemaOrHashDrift) {
  WreckwaterCertifiedSnapshot snapshot = makeSnapshot();
  for (uint64_t iteration = 0u; iteration < 5'000u; ++iteration) {
    snapshot.snapshotSequence = iteration + 1u;
    snapshot.applicationTick = iteration % 10'000u;
    snapshot.physicsEvidenceTick =
        snapshot.applicationTick == 0u ? 0u : snapshot.applicationTick - 1u;
    snapshot.entities[0].localPosition.x =
        static_cast<float>(static_cast<int32_t>(iteration % 200u) - 100) * 0.5f;
    const WreckwaterWriteResult first =
        WreckwaterSnapshotCodec::encode(snapshot);
    ASSERT_TRUE(first) << iteration;
    const WreckwaterSnapshotReadResult decoded =
        WreckwaterSnapshotCodec::decode(first.bytes);
    ASSERT_TRUE(decoded) << iteration << ": "
                         << wreckwaterCodecErrorName(decoded.error);
    const WreckwaterWriteResult second =
        WreckwaterSnapshotCodec::encode(*decoded.snapshot);
    ASSERT_TRUE(second) << iteration;
    ASSERT_EQ(second.bytes, first.bytes) << iteration;
    ASSERT_EQ(second.serializedByteHash, first.serializedByteHash) << iteration;
  }
}

} // namespace
} // namespace voxy::network
