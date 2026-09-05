#include <gtest/gtest.h>

#include "network/protocol.hpp"
#include "network/ridgebreak_client_replication.hpp"
#include "network/ridgebreak_protocol.hpp"
#include "server/ridgebreak_authority.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <string_view>
#include <vector>

namespace voxy::server {
namespace {

using network::DeliveryClass;
using network::Packet;
using network::PacketCodec;
using network::PacketPayloadType;
using network::RidgebreakInputBundle;
using network::RidgebreakInputSample;

RidgebreakInputSample sample(
    uint64_t tick, uint64_t sequence, uint16_t throttle = 20'000u,
    int16_t steer = 0, uint16_t flags = 0u) {
    RidgebreakInputSample result;
    result.requestedTick = tick;
    result.inputSequence = sequence;
    result.throttleQ15 = throttle;
    result.steerQ15 = steer;
    result.flags = flags;
    return result;
}

std::vector<std::byte> inputPacket(
    const RidgebreakAuthoritySession::Config& config,
    uint64_t packetSequence, std::span<const RidgebreakInputSample> samples,
    uint64_t ackSequence = 0u, uint64_t ackBits = 0u,
    uint64_t connectionSerial = 9'001u,
    uint32_t connectionGeneration = 1u) {
    RidgebreakInputBundle bundle;
    bundle.sampleCount = static_cast<uint32_t>(samples.size());
    bundle.connectionSerial = connectionSerial;
    bundle.connectionGeneration = connectionGeneration;
    std::copy(samples.begin(), samples.end(), bundle.samples.begin());
    Packet packet;
    packet.header.payloadType = PacketPayloadType::Input;
    packet.header.sessionId = config.sessionId;
    packet.header.worldId = config.worldId;
    packet.header.worldEpoch = config.worldEpoch;
    packet.header.authorityEpoch = config.authorityEpoch;
    packet.header.sequence = packetSequence;
    packet.header.ackSequence = ackSequence;
    packet.header.ackBits = ackBits;
    packet.header.tick = samples.back().requestedTick;
    packet.payload = network::encodeRidgebreakInputBundle(bundle);
    auto encoded = PacketCodec::encode(packet, DeliveryClass::Realtime);
    EXPECT_TRUE(encoded.error.empty()) << encoded.error;
    return encoded.bytes;
}

std::vector<std::byte> snapshotPacket(
    const RidgebreakAuthoritySession::Config& config,
    const network::RidgebreakSnapshot& snapshot,
    uint64_t packetSequence) {
    Packet packet;
    packet.header.payloadType = PacketPayloadType::Snapshot;
    packet.header.sessionId = config.sessionId;
    packet.header.worldId = config.worldId;
    packet.header.worldEpoch = config.worldEpoch;
    packet.header.authorityEpoch = config.authorityEpoch;
    packet.header.sequence = packetSequence;
    packet.header.tick = snapshot.authoritativeTick;
    packet.payload = network::encodeRidgebreakSnapshot(snapshot);
    auto encoded = PacketCodec::encode(packet, DeliveryClass::Realtime);
    EXPECT_TRUE(encoded.error.empty()) << encoded.error;
    return encoded.bytes;
}

void registerFour(RidgebreakAuthoritySession& authority) {
    for (uint32_t i = 0u; i < 4u; ++i)
        ASSERT_TRUE(authority.registerClient(
            100u + i, 1u + i, 10'000u + i, 7u));
}

std::vector<std::byte> bytesFromHex(std::string_view text) {
    const auto nibble = [](char value) -> uint8_t {
        if (value >= '0' && value <= '9')
            return static_cast<uint8_t>(value - '0');
        if (value >= 'a' && value <= 'f')
            return static_cast<uint8_t>(value - 'a' + 10);
        return static_cast<uint8_t>(value - 'A' + 10);
    };
    std::vector<std::byte> result;
    uint8_t high = 0u;
    bool haveHigh = false;
    for (char value : text) {
        const bool hexadecimal =
            (value >= '0' && value <= '9')
            || (value >= 'a' && value <= 'f')
            || (value >= 'A' && value <= 'F');
        if (!hexadecimal) continue;
        if (!haveHigh) {
            high = nibble(value);
            haveHigh = true;
        } else {
            result.push_back(std::byte{
                static_cast<uint8_t>((high << 4u) | nibble(value))});
            haveHigh = false;
        }
    }
    EXPECT_FALSE(haveHigh);
    return result;
}

TEST(RidgebreakProtocol, FixedSizeRoundTripsAndRejectsNonCanonicalInput) {
    RidgebreakInputBundle bundle;
    bundle.sampleCount = 2u;
    bundle.connectionSerial = 0x0102030405060708ull;
    bundle.connectionGeneration = 3u;
    bundle.samples[0] = sample(40u, 90u, 12'345u, -4'000);
    bundle.samples[1] = sample(
        41u, 91u, 32'767u, 8'000,
        network::RidgebreakInputStanding);
    const auto encoded = network::encodeRidgebreakInputBundle(bundle);
    ASSERT_EQ(encoded.size(), network::kRidgebreakInputBundleBytes);
    const auto decoded = network::decodeRidgebreakInputBundle(encoded);
    ASSERT_TRUE(decoded.bundle.has_value());
    EXPECT_EQ(*decoded.bundle, bundle);

    bundle.samples[1].flags = network::RidgebreakInputGearUp
        | network::RidgebreakInputGearDown;
    EXPECT_TRUE(network::encodeRidgebreakInputBundle(bundle).empty());

    network::RidgebreakSnapshot snapshot;
    snapshot.playerCount = 1u;
    snapshot.authoritativeTick = 44u;
    snapshot.canonicalStateHash = 0x123456789abcdef0ull;
    snapshot.players[0].playerId = 9u;
    snapshot.players[0].connectionGeneration = 2u;
    snapshot.players[0].connectionSerial = 90u;
    snapshot.players[0].lifecycle =
        network::RidgebreakPlayerLifecycle::Riding;
    snapshot.players[0].positionXMillimeters = -123'456;
    const auto snapshotBytes = network::encodeRidgebreakSnapshot(snapshot);
    ASSERT_EQ(snapshotBytes.size(), network::kRidgebreakSnapshotBytes);
    const auto decodedSnapshot =
        network::decodeRidgebreakSnapshot(snapshotBytes);
    ASSERT_TRUE(decodedSnapshot.snapshot.has_value());
    EXPECT_EQ(*decodedSnapshot.snapshot, snapshot);
}

TEST(RidgebreakProtocol, EncoderRejectsNonCanonicalUnusedInputSamples) {
    RidgebreakInputBundle bundle;
    bundle.sampleCount = 1u;
    bundle.connectionSerial = 123u;
    bundle.connectionGeneration = 1u;
    bundle.samples[0] = sample(40u, 90u);
    ASSERT_FALSE(network::encodeRidgebreakInputBundle(bundle).empty());
    for (uint32_t i = bundle.sampleCount;
         i < network::kRidgebreakMaximumRedundantInputs; ++i) {
        bundle.samples[i] = sample(41u, 91u);
        EXPECT_TRUE(network::encodeRidgebreakInputBundle(bundle).empty());
        bundle.samples[i] = {};
    }
}

TEST(RidgebreakProtocol, ProducesExplicitPredictionReconciliationMetadata) {
    network::RidgebreakPlayerState predicted;
    predicted.playerId = 3u;
    predicted.connectionGeneration = 5u;
    predicted.connectionSerial = 123u;
    predicted.lifecycle = network::RidgebreakPlayerLifecycle::Riding;
    predicted.positionXMillimeters = 1'000;
    predicted.positionZMillimeters = 2'000;
    predicted.forwardSpeedMillimetersPerSecond = 8'000;

    auto authoritative = predicted;
    authoritative.lastProcessedInputSequence = 81u;
    authoritative.positionXMillimeters = 1'250;
    authoritative.positionZMillimeters = 1'900;
    authoritative.forwardSpeedMillimetersPerSecond = 7'500;
    auto correction = network::reconcileRidgebreakPrediction(
        900u, predicted, authoritative, 500u);
    EXPECT_EQ(correction.authoritativeTick, 900u);
    EXPECT_EQ(correction.acknowledgedInputSequence, 81u);
    EXPECT_EQ(correction.positionErrorXMillimeters, 250);
    EXPECT_EQ(correction.positionErrorZMillimeters, -100);
    EXPECT_EQ(correction.speedErrorMillimetersPerSecond, -500);
    EXPECT_FALSE(correction.hardCorrectionRequired);

    authoritative.positionXMillimeters = 4'000;
    correction = network::reconcileRidgebreakPrediction(
        900u, predicted, authoritative, 500u);
    EXPECT_TRUE(correction.hardCorrectionRequired);
}

TEST(RidgebreakProtocol, InputWireEncodingMatchesGoldenVector) {
    RidgebreakInputBundle bundle;
    bundle.sampleCount = 1u;
    bundle.connectionSerial = 0x0102030405060708ull;
    bundle.connectionGeneration = 0x0a0b0c0du;
    bundle.samples[0] = sample(
        0x1112131415161718ull, 0x2122232425262728ull,
        0x1234u, 0x3456);
    bundle.samples[0].brakeQ15 = 0x2345u;
    bundle.samples[0].leanQ15 = std::bit_cast<int16_t>(uint16_t{0xcdefu});
    bundle.samples[0].flags = network::RidgebreakInputStanding;

    const auto golden = bytesFromHex(
        "01000000 01000000 0807060504030201 0d0c0b0a 00000000 "
        "1817161514131211 2827262524232221 3412 4523 5634 efcd 0100 0000 "
        "00000000000000000000000000000000000000000000000000000000 "
        "00000000000000000000000000000000000000000000000000000000");
    ASSERT_EQ(golden.size(), network::kRidgebreakInputBundleBytes);
    EXPECT_EQ(network::encodeRidgebreakInputBundle(bundle), golden);
    const auto decoded = network::decodeRidgebreakInputBundle(golden);
    ASSERT_TRUE(decoded.bundle.has_value());
    EXPECT_EQ(*decoded.bundle, bundle);
}

TEST(RidgebreakProtocol, SnapshotWireEncodingMatchesGoldenVector) {
    network::RidgebreakSnapshot snapshot;
    snapshot.playerCount = 1u;
    snapshot.authoritativeTick = 0x0102030405060708ull;
    snapshot.canonicalStateHash = 0x1112131415161718ull;
    auto& player = snapshot.players[0];
    player.playerId = 0x11223344u;
    player.connectionGeneration = 0x55667788u;
    player.connectionSerial = 0x2122232425262728ull;
    player.lastProcessedInputSequence = 0x3132333435363738ull;
    player.positionXMillimeters = 1;
    player.positionZMillimeters = -2;
    player.velocityXMillimetersPerSecond = 3;
    player.velocityZMillimetersPerSecond = -4;
    player.headingTurnsQ16 = 5;
    player.forwardSpeedMillimetersPerSecond = 6;
    player.leanQ15 = 7;
    player.gear = 2u;
    player.lifecycle = network::RidgebreakPlayerLifecycle::Riding;
    player.stateFlags = network::RidgebreakInputStanding;

    const auto golden = bytesFromHex(
        "01000000 01000000 0807060504030201 1817161514131211 "
        "44332211 88776655 2827262524232221 3837363534333231 "
        "01000000 feffffff 03000000 fcffffff 05000000 06000000 "
        "0700 02 01 01000000 "
        "00000000 00000000 0000000000000000 0000000000000000 "
        "00000000 00000000 00000000 00000000 00000000 00000000 "
        "0000 01 00 00000000 "
        "00000000 00000000 0000000000000000 0000000000000000 "
        "00000000 00000000 00000000 00000000 00000000 00000000 "
        "0000 01 00 00000000 "
        "00000000 00000000 0000000000000000 0000000000000000 "
        "00000000 00000000 00000000 00000000 00000000 00000000 "
        "0000 01 00 00000000");
    ASSERT_EQ(golden.size(), network::kRidgebreakSnapshotBytes);
    EXPECT_EQ(network::encodeRidgebreakSnapshot(snapshot), golden);
    const auto decoded = network::decodeRidgebreakSnapshot(golden);
    ASSERT_TRUE(decoded.snapshot.has_value());
    EXPECT_EQ(*decoded.snapshot, snapshot);
}

TEST(RidgebreakProtocol, RejectsAmbiguousSnapshotSemantics) {
    network::RidgebreakSnapshot snapshot;
    snapshot.playerCount = 2u;
    for (uint32_t i = 0u; i < 2u; ++i) {
        auto& player = snapshot.players[i];
        player.playerId = i + 1u;
        player.connectionGeneration = 1u;
        player.connectionSerial = 100u + i;
        player.lifecycle = network::RidgebreakPlayerLifecycle::Riding;
    }
    const auto valid = network::encodeRidgebreakSnapshot(snapshot);
    ASSERT_EQ(valid.size(), network::kRidgebreakSnapshotBytes);

    auto duplicatePlayer = valid;
    constexpr size_t secondPlayer = 24u + network::kRidgebreakPlayerStateBytes;
    duplicatePlayer.at(secondPlayer) = std::byte{1u};
    duplicatePlayer.at(secondPlayer + 1u) = std::byte{0u};
    duplicatePlayer.at(secondPlayer + 2u) = std::byte{0u};
    duplicatePlayer.at(secondPlayer + 3u) = std::byte{0u};
    EXPECT_FALSE(network::decodeRidgebreakSnapshot(
        duplicatePlayer).snapshot.has_value());

    auto missingSerial = valid;
    for (size_t offset = 32u; offset < 40u; ++offset)
        missingSerial.at(offset) = std::byte{0u};
    EXPECT_FALSE(network::decodeRidgebreakSnapshot(
        missingSerial).snapshot.has_value());

    auto duplicateSerial = valid;
    constexpr size_t firstSerial = 24u + 8u;
    constexpr size_t secondSerial = secondPlayer + 8u;
    for (size_t byte = 0u; byte < 8u; ++byte)
        duplicateSerial.at(secondSerial + byte) =
            duplicateSerial.at(firstSerial + byte);
    EXPECT_FALSE(network::decodeRidgebreakSnapshot(
        duplicateSerial).snapshot.has_value());

    auto impossibleHeading = valid;
    impossibleHeading.at(24u + 40u) = std::byte{0u};
    impossibleHeading.at(24u + 41u) = std::byte{0u};
    impossibleHeading.at(24u + 42u) = std::byte{1u};
    impossibleHeading.at(24u + 43u) = std::byte{0u};
    EXPECT_FALSE(network::decodeRidgebreakSnapshot(
        impossibleHeading).snapshot.has_value());

    auto unknownFlags = valid;
    unknownFlags.at(24u + 52u) = std::byte{0x80u};
    EXPECT_FALSE(network::decodeRidgebreakSnapshot(
        unknownFlags).snapshot.has_value());
}

TEST(RidgebreakProtocol, ClientSnapshotWindowFencesReconnectLifetime) {
    RidgebreakAuthoritySession::Config authorityConfig;
    network::RidgebreakSnapshot oldSnapshot;
    oldSnapshot.playerCount = 1u;
    oldSnapshot.authoritativeTick = 10u;
    oldSnapshot.canonicalStateHash = 100u;
    oldSnapshot.players[0].playerId = 1u;
    oldSnapshot.players[0].connectionSerial = 101u;
    oldSnapshot.players[0].connectionGeneration = 1u;
    oldSnapshot.players[0].lifecycle =
        network::RidgebreakPlayerLifecycle::Riding;
    const auto oldFrame = snapshotPacket(authorityConfig, oldSnapshot, 200u);

    network::RidgebreakClientSnapshotWindow window;
    ASSERT_TRUE(window.reset({
        .sessionId = authorityConfig.sessionId,
        .worldId = authorityConfig.worldId,
        .worldEpoch = authorityConfig.worldEpoch,
        .authorityEpoch = authorityConfig.authorityEpoch,
        .playerId = 1u,
        .connectionSerial = 101u,
        .connectionGeneration = 1u,
    }));
    EXPECT_TRUE(window.accept(
        101u, DeliveryClass::Realtime, oldFrame));
    EXPECT_EQ(window.latestSequence(), 200u);

    ASSERT_TRUE(window.reset({
        .sessionId = authorityConfig.sessionId,
        .worldId = authorityConfig.worldId,
        .worldEpoch = authorityConfig.worldEpoch,
        .authorityEpoch = authorityConfig.authorityEpoch,
        .playerId = 1u,
        .connectionSerial = 202u,
        .connectionGeneration = 2u,
    }));
    EXPECT_EQ(window.accept(
        101u, DeliveryClass::Realtime, oldFrame).error,
        network::RidgebreakSnapshotWindowError::ConnectionSerialMismatch);
    EXPECT_EQ(window.accept(
        202u, DeliveryClass::Realtime, oldFrame).error,
        network::RidgebreakSnapshotWindowError::PlayerIdentityMismatch);

    auto newSnapshot = oldSnapshot;
    newSnapshot.authoritativeTick = 11u;
    newSnapshot.canonicalStateHash = 101u;
    newSnapshot.players[0].connectionSerial = 202u;
    newSnapshot.players[0].connectionGeneration = 2u;
    const auto poisoned = snapshotPacket(
        authorityConfig, newSnapshot, std::numeric_limits<uint64_t>::max());
    EXPECT_EQ(window.accept(
        202u, DeliveryClass::Realtime, poisoned).error,
        network::RidgebreakSnapshotWindowError::SequenceJump);
    const auto fresh = snapshotPacket(authorityConfig, newSnapshot, 1u);
    EXPECT_TRUE(window.accept(202u, DeliveryClass::Realtime, fresh));
    EXPECT_EQ(window.latestSequence(), 1u);
    EXPECT_EQ(window.accept(
        202u, DeliveryClass::Realtime, fresh).error,
        network::RidgebreakSnapshotWindowError::DuplicateOrStaleSequence);

    auto staleSnapshot = newSnapshot;
    staleSnapshot.authoritativeTick = 10u;
    const auto stale = snapshotPacket(authorityConfig, staleSnapshot, 257u);
    EXPECT_EQ(window.accept(
        202u, DeliveryClass::Realtime, stale).error,
        network::RidgebreakSnapshotWindowError::StaleSnapshotTick);
    EXPECT_EQ(window.latestSequence(), 1u);
    EXPECT_EQ(window.latestAuthoritativeTick(), 11u);

    auto nextSnapshot = newSnapshot;
    nextSnapshot.authoritativeTick = 12u;
    nextSnapshot.canonicalStateHash = 102u;
    const auto next = snapshotPacket(authorityConfig, nextSnapshot, 2u);
    EXPECT_TRUE(window.accept(202u, DeliveryClass::Realtime, next));
    EXPECT_EQ(window.latestSequence(), 2u);
    EXPECT_EQ(window.latestAuthoritativeTick(), 12u);
}

TEST(RidgebreakAuthority, AcknowledgesPacketsSnapshotsAndProcessedInputs) {
    RidgebreakAuthoritySession authority;
    RidgebreakAuthoritySession::Config config;
    config.sessionId = 11u;
    config.worldId = 22u;
    ASSERT_TRUE(authority.initialize(config));
    ASSERT_TRUE(authority.registerClient(100u, 1u, 9'001u, 1u));

    const std::array first{sample(1u, 10u)};
    const auto firstPacket = inputPacket(config, 1u, first);
    const auto accepted = authority.ingest(
        100u, DeliveryClass::Realtime, firstPacket);
    ASSERT_TRUE(accepted);
    EXPECT_EQ(accepted.acceptedSamples, 1u);
    authority.step();

    const auto framedSnapshot = authority.buildSnapshotPacket(100u);
    ASSERT_TRUE(framedSnapshot.has_value());
    ASSERT_LE(framedSnapshot->size(), network::kConservativeRealtimeMtu);
    const auto outer = PacketCodec::decode(
        *framedSnapshot, DeliveryClass::Realtime);
    ASSERT_TRUE(outer.packet.has_value());
    EXPECT_EQ(outer.packet->header.sequence, 1u);
    EXPECT_EQ(outer.packet->header.ackSequence, 1u);
    EXPECT_EQ(outer.packet->header.ackBits & 1u, 1u);
    const auto snapshot = network::decodeRidgebreakSnapshot(
        outer.packet->payload);
    ASSERT_TRUE(snapshot.snapshot.has_value());
    EXPECT_EQ(snapshot.snapshot->authoritativeTick, 1u);
    EXPECT_EQ(snapshot.snapshot->players[0].lastProcessedInputSequence, 10u);

    const std::array second{sample(2u, 11u)};
    const auto secondPacket = inputPacket(config, 2u, second, 1u, 1u);
    EXPECT_TRUE(authority.ingest(
        100u, DeliveryClass::Realtime, secondPacket));
    auto view = authority.peerView(100u);
    ASSERT_TRUE(view.has_value());
    EXPECT_EQ(view->latestAcknowledgedSnapshotSequence, 1u);

    const auto duplicate = authority.ingest(
        100u, DeliveryClass::Realtime, secondPacket);
    EXPECT_EQ(duplicate.error, RidgebreakIngressError::DuplicatePacket);
}

TEST(RidgebreakAuthority, MalformedPacketCannotPoisonSequenceWindow) {
    RidgebreakAuthoritySession authority;
    RidgebreakAuthoritySession::Config config;
    ASSERT_TRUE(authority.initialize(config));
    ASSERT_TRUE(authority.registerClient(100u, 1u, 9'001u, 1u));

    Packet malformed;
    malformed.header.payloadType = PacketPayloadType::Input;
    malformed.header.sessionId = config.sessionId;
    malformed.header.worldId = config.worldId;
    malformed.header.worldEpoch = config.worldEpoch;
    malformed.header.authorityEpoch = config.authorityEpoch;
    malformed.header.sequence = 64u;
    malformed.header.tick = 1u;
    malformed.payload.resize(network::kRidgebreakInputBundleBytes);
    const auto malformedFrame = PacketCodec::encode(
        malformed, DeliveryClass::Realtime);
    ASSERT_TRUE(malformedFrame.error.empty());
    EXPECT_EQ(authority.ingest(
        100u, DeliveryClass::Realtime, malformedFrame.bytes).error,
        RidgebreakIngressError::InputDecodeFailed);

    const std::array valid{sample(1u, 1u)};
    EXPECT_TRUE(authority.ingest(
        100u, DeliveryClass::Realtime,
        inputPacket(config, 1u, valid)));
}

TEST(RidgebreakAuthority, RedundantFutureInputRecoversAWholeLostPacket) {
    RidgebreakAuthoritySession authority;
    RidgebreakAuthoritySession::Config config;
    ASSERT_TRUE(authority.initialize(config));
    ASSERT_TRUE(authority.registerClient(100u, 1u, 9'001u, 1u));

    // The packet that originally carried input 1 for tick 2 was lost.
    authority.step();
    const std::array recovered{
        sample(2u, 1u, 32'767u),
        sample(3u, 2u, 32'767u),
    };
    const auto result = authority.ingest(
        100u, DeliveryClass::Realtime,
        inputPacket(config, 2u, recovered));
    ASSERT_TRUE(result);
    EXPECT_EQ(result.acceptedSamples, 2u);
    authority.step();
    const auto snapshot = authority.canonicalSnapshot();
    EXPECT_EQ(snapshot.players[0].lastProcessedInputSequence, 1u);
    EXPECT_GT(snapshot.players[0].forwardSpeedMillimetersPerSecond, 0);
}

TEST(RidgebreakAuthority, BoundsIngressAndFencesAReconnectedPlayer) {
    RidgebreakAuthoritySession authority;
    RidgebreakAuthoritySession::Config config;
    config.maximumPacketsPerPeerTick = 1u;
    ASSERT_TRUE(authority.initialize(config));
    ASSERT_TRUE(authority.registerClient(100u, 1u, 9'001u, 4u));

    const std::array first{sample(1u, 1u)};
    EXPECT_TRUE(authority.ingest(
        100u, DeliveryClass::Realtime,
        inputPacket(config, 1u, first, 0u, 0u, 9'001u, 4u)));
    const std::array second{sample(2u, 2u)};
    EXPECT_EQ(authority.ingest(
        100u, DeliveryClass::Realtime,
        inputPacket(config, 2u, second, 0u, 0u, 9'001u, 4u)).error,
        RidgebreakIngressError::InputRateExceeded);

    ASSERT_TRUE(authority.disconnectClient(100u));
    ASSERT_TRUE(authority.registerClient(200u, 1u, 9'002u, 5u));
    EXPECT_FALSE(authority.peerView(100u).has_value());
    const auto replacement = authority.peerView(200u);
    ASSERT_TRUE(replacement.has_value());
    EXPECT_EQ(replacement->connectionGeneration, 5u);
    EXPECT_EQ(replacement->latestReceivedPacketSequence, 0u);
    EXPECT_EQ(replacement->latestProcessedInputSequence, 0u);
}

TEST(RidgebreakAuthority, PeerRebindingReclaimsExactlyOneTombstone) {
    RidgebreakAuthoritySession authority;
    RidgebreakAuthoritySession::Config config;
    ASSERT_TRUE(authority.initialize(config));
    ASSERT_TRUE(authority.registerClient(10u, 1u, 101u, 1u));
    ASSERT_TRUE(authority.registerClient(20u, 2u, 102u, 1u));
    ASSERT_TRUE(authority.disconnectClient(10u));

    ASSERT_TRUE(authority.registerClient(10u, 3u, 103u, 1u));
    const auto rebound = authority.peerView(10u);
    ASSERT_TRUE(rebound.has_value());
    EXPECT_EQ(rebound->playerId, 3u);
    EXPECT_TRUE(rebound->active);
    const auto snapshot = authority.canonicalSnapshot();
    ASSERT_EQ(snapshot.playerCount, 2u);
    EXPECT_EQ(snapshot.players[0].playerId, 2u);
    EXPECT_EQ(snapshot.players[1].playerId, 3u);

    ASSERT_TRUE(authority.registerClient(30u, 1u, 104u, 2u));
    EXPECT_EQ(authority.peerView(30u)->playerId, 1u);
}

TEST(RidgebreakAuthority, CrossedPeerAndPlayerTombstonesCannotAlias) {
    RidgebreakAuthoritySession authority;
    RidgebreakAuthoritySession::Config config;
    ASSERT_TRUE(authority.initialize(config));
    ASSERT_TRUE(authority.registerClient(10u, 1u, 101u, 1u));
    ASSERT_TRUE(authority.registerClient(20u, 2u, 102u, 1u));
    ASSERT_TRUE(authority.disconnectClient(10u));
    ASSERT_TRUE(authority.disconnectClient(20u));

    ASSERT_TRUE(authority.registerClient(20u, 1u, 103u, 2u));
    EXPECT_FALSE(authority.peerView(10u).has_value());
    const auto rebound = authority.peerView(20u);
    ASSERT_TRUE(rebound.has_value());
    EXPECT_EQ(rebound->playerId, 1u);
    const auto snapshot = authority.canonicalSnapshot();
    ASSERT_EQ(snapshot.playerCount, 1u);
    EXPECT_EQ(snapshot.players[0].playerId, 1u);

    ASSERT_TRUE(authority.registerClient(10u, 3u, 104u, 1u));
    EXPECT_EQ(authority.canonicalSnapshot().playerCount, 2u);
}

TEST(RidgebreakAuthority, PerPlayerFencesPermitUnboundedBoundedMemoryChurn) {
    RidgebreakAuthoritySession authority;
    RidgebreakAuthoritySession::Config config;
    ASSERT_TRUE(authority.initialize(config));
    for (uint32_t i = 0u; i < 1'000u; ++i) {
        ASSERT_TRUE(authority.registerClient(
            10u, 1u, 1'000u + i, 1u + i));
        ASSERT_TRUE(authority.disconnectClient(10u));
    }
    EXPECT_FALSE(authority.registerClient(10u, 1u, 1'999u, 1'001u));
    EXPECT_EQ(authority.lastRegistrationError(),
              RidgebreakRegistrationError::ReusedConnectionSerial);
    EXPECT_FALSE(authority.registerClient(10u, 1u, 2'000u, 1'000u));
    EXPECT_EQ(authority.lastRegistrationError(),
              RidgebreakRegistrationError::NonIncreasingGeneration);
    EXPECT_TRUE(authority.registerClient(10u, 1u, 2'000u, 1'001u));

    // Rider one cannot consume any reconnect capacity belonging to rider two.
    EXPECT_TRUE(authority.registerClient(20u, 2u, 3'000u, 1u));
}

TEST(RidgebreakAuthority, SnapshotRateIsValidatedAndExactlyDerived) {
    RidgebreakAuthoritySession invalid;
    RidgebreakAuthoritySession::Config invalidConfig;
    invalidConfig.snapshotRateHz = 7u;
    EXPECT_FALSE(invalid.initialize(invalidConfig));

    RidgebreakAuthoritySession authority;
    RidgebreakAuthoritySession::Config config;
    config.snapshotRateHz = 30u;
    ASSERT_TRUE(authority.initialize(config));
    authority.step();
    EXPECT_FALSE(authority.snapshotDue());
    authority.step();
    EXPECT_TRUE(authority.snapshotDue());
}

TEST(RidgebreakAuthority, RejectsStaleConnectionReplayAfterReconnect) {
    RidgebreakAuthoritySession authority;
    RidgebreakAuthoritySession::Config config;
    ASSERT_TRUE(authority.initialize(config));
    ASSERT_TRUE(authority.registerClient(100u, 1u, 50'001u, 9u));

    const std::array oldInput{sample(1u, 1u)};
    const auto oldFrame = inputPacket(
        config, 1u, oldInput, 0u, 0u, 50'001u, 9u);
    ASSERT_TRUE(authority.disconnectClient(100u));
    EXPECT_FALSE(authority.registerClient(201u, 2u, 50'001u, 1u));
    EXPECT_FALSE(authority.registerClient(200u, 1u, 50'002u, 9u));
    EXPECT_FALSE(authority.registerClient(200u, 1u, 50'002u, 8u));
    EXPECT_FALSE(authority.registerClient(200u, 1u, 50'001u, 10u));
    ASSERT_TRUE(authority.registerClient(200u, 1u, 50'002u, 10u));

    EXPECT_EQ(authority.ingest(
        200u, DeliveryClass::Realtime, oldFrame).error,
        RidgebreakIngressError::ConnectionIdentityMismatch);
    const std::array freshInput{sample(1u, 1u)};
    EXPECT_TRUE(authority.ingest(
        200u, DeliveryClass::Realtime,
        inputPacket(config, 1u, freshInput, 0u, 0u, 50'002u, 10u)));
}

TEST(RidgebreakAuthority, SequenceJumpCannotPoisonPacketOrInputWindows) {
    RidgebreakAuthoritySession authority;
    RidgebreakAuthoritySession::Config config;
    config.maximumPacketSequenceAdvance = 8u;
    config.maximumInputSequenceAdvance = 8u;
    ASSERT_TRUE(authority.initialize(config));
    ASSERT_TRUE(authority.registerClient(100u, 1u, 70'001u, 1u));

    const std::array poisonedPacketInput{sample(1u, 1u)};
    EXPECT_EQ(authority.ingest(
        100u, DeliveryClass::Realtime,
        inputPacket(config, std::numeric_limits<uint64_t>::max(),
                    poisonedPacketInput, 0u, 0u, 70'001u, 1u)).error,
        RidgebreakIngressError::PacketSequenceJump);

    const std::array poisonedLogicalInput{
        sample(1u, std::numeric_limits<uint64_t>::max())};
    EXPECT_EQ(authority.ingest(
        100u, DeliveryClass::Realtime,
        inputPacket(config, 1u, poisonedLogicalInput,
                    0u, 0u, 70'001u, 1u)).error,
        RidgebreakIngressError::InputSequenceJump);

    const std::array valid{sample(1u, 1u)};
    EXPECT_TRUE(authority.ingest(
        100u, DeliveryClass::Realtime,
        inputPacket(config, 1u, valid, 0u, 0u, 70'001u, 1u)));
    const auto view = authority.peerView(100u);
    ASSERT_TRUE(view.has_value());
    EXPECT_EQ(view->latestReceivedPacketSequence, 1u);
}

TEST(RidgebreakAuthority,
     GameplayHashIgnoresPendingInputWhileTranscriptCommitsIt) {
    RidgebreakAuthoritySession first;
    RidgebreakAuthoritySession second;
    RidgebreakAuthoritySession::Config config;
    ASSERT_TRUE(first.initialize(config));
    ASSERT_TRUE(second.initialize(config));
    ASSERT_TRUE(first.registerClient(100u, 1u, 80'001u, 1u));
    ASSERT_TRUE(second.registerClient(100u, 1u, 80'001u, 1u));

    auto throttle = sample(2u, 1u, 30'000u);
    auto braking = sample(2u, 1u, 0u);
    braking.brakeQ15 = 30'000u;
    const std::array throttleBundle{throttle};
    const std::array brakingBundle{braking};
    ASSERT_TRUE(first.ingest(
        100u, DeliveryClass::Realtime,
        inputPacket(config, 1u, throttleBundle,
                    0u, 0u, 80'001u, 1u)));
    ASSERT_TRUE(second.ingest(
        100u, DeliveryClass::Realtime,
        inputPacket(config, 1u, brakingBundle,
                    0u, 0u, 80'001u, 1u)));

    const auto firstSnapshot = first.canonicalSnapshot();
    const auto secondSnapshot = second.canonicalSnapshot();
    EXPECT_EQ(firstSnapshot.players, secondSnapshot.players);
    EXPECT_EQ(firstSnapshot.canonicalStateHash,
              secondSnapshot.canonicalStateHash);
    EXPECT_NE(first.authorityTranscriptHash(),
              second.authorityTranscriptHash());
}

TEST(RidgebreakAuthority,
     GameplayHashIgnoresHiddenHistoryWhileTranscriptCommitsIt) {
    RidgebreakAuthoritySession withHistory;
    RidgebreakAuthoritySession withoutHistory;
    RidgebreakAuthoritySession::Config config;
    ASSERT_TRUE(withHistory.initialize(config));
    ASSERT_TRUE(withoutHistory.initialize(config));

    ASSERT_TRUE(withHistory.registerClient(10u, 1u, 101u, 1u));
    ASSERT_TRUE(withHistory.disconnectClient(10u));
    ASSERT_TRUE(withHistory.registerClient(10u, 2u, 202u, 1u));
    ASSERT_TRUE(withoutHistory.registerClient(10u, 2u, 202u, 1u));

    const auto historicalSnapshot = withHistory.canonicalSnapshot();
    const auto cleanSnapshot = withoutHistory.canonicalSnapshot();
    ASSERT_EQ(historicalSnapshot.authoritativeTick,
              cleanSnapshot.authoritativeTick);
    ASSERT_EQ(historicalSnapshot.players, cleanSnapshot.players);
    EXPECT_EQ(historicalSnapshot.canonicalStateHash,
              cleanSnapshot.canonicalStateHash);
    EXPECT_NE(withHistory.authorityTranscriptHash(),
              withoutHistory.authorityTranscriptHash());

    // The hidden ledger is genuinely future-influencing: player 1 generation
    // 1 is stale only in the authority that has already observed it.
    EXPECT_FALSE(withHistory.registerClient(20u, 1u, 303u, 1u));
    EXPECT_EQ(withHistory.lastRegistrationError(),
              RidgebreakRegistrationError::NonIncreasingGeneration);
    EXPECT_TRUE(withoutHistory.registerClient(20u, 1u, 303u, 1u));
}

TEST(RidgebreakAuthority,
     GameplayHashIgnoresDisconnectTimingWhileTranscriptCommitsIt) {
    RidgebreakAuthoritySession earlyDisconnect;
    RidgebreakAuthoritySession lateDisconnect;
    RidgebreakAuthoritySession::Config config;
    ASSERT_TRUE(earlyDisconnect.initialize(config));
    ASSERT_TRUE(lateDisconnect.initialize(config));
    ASSERT_TRUE(earlyDisconnect.registerClient(10u, 1u, 101u, 1u));
    ASSERT_TRUE(lateDisconnect.registerClient(10u, 1u, 101u, 1u));

    ASSERT_TRUE(earlyDisconnect.disconnectClient(10u));
    earlyDisconnect.step();
    lateDisconnect.step();
    ASSERT_TRUE(lateDisconnect.disconnectClient(10u));
    earlyDisconnect.step();
    lateDisconnect.step();

    const auto early = earlyDisconnect.canonicalSnapshot();
    const auto late = lateDisconnect.canonicalSnapshot();
    ASSERT_EQ(early.authoritativeTick, late.authoritativeTick);
    ASSERT_EQ(early.players, late.players);
    EXPECT_EQ(early.canonicalStateHash, late.canonicalStateHash);
    EXPECT_NE(earlyDisconnect.authorityTranscriptHash(),
              lateDisconnect.authorityTranscriptHash());
}

TEST(RidgebreakAuthority,
     GameplayHashIgnoresSnapshotSequenceWhileTranscriptCommitsIt) {
    RidgebreakAuthoritySession withSnapshot;
    RidgebreakAuthoritySession withoutSnapshot;
    RidgebreakAuthoritySession::Config config;
    ASSERT_TRUE(withSnapshot.initialize(config));
    ASSERT_TRUE(withoutSnapshot.initialize(config));
    ASSERT_TRUE(withSnapshot.registerClient(100u, 1u, 90'001u, 1u));
    ASSERT_TRUE(withoutSnapshot.registerClient(100u, 1u, 90'001u, 1u));

    ASSERT_TRUE(withSnapshot.buildSnapshotPacket(100u).has_value());
    const auto sent = withSnapshot.canonicalSnapshot();
    const auto unsent = withoutSnapshot.canonicalSnapshot();
    ASSERT_EQ(sent.authoritativeTick, unsent.authoritativeTick);
    ASSERT_EQ(sent.players, unsent.players);
    EXPECT_EQ(sent.canonicalStateHash, unsent.canonicalStateHash);
    EXPECT_NE(withSnapshot.authorityTranscriptHash(),
              withoutSnapshot.authorityTranscriptHash());

    const std::array input{sample(1u, 1u)};
    const auto acknowledging = inputPacket(
        config, 1u, input, 1u, 1u, 90'001u, 1u);
    EXPECT_TRUE(withSnapshot.ingest(
        100u, DeliveryClass::Realtime, acknowledging));
    EXPECT_EQ(withSnapshot.peerView(100u)
                  ->latestAcknowledgedSnapshotSequence,
              1u);
    EXPECT_EQ(withoutSnapshot.ingest(
                  100u, DeliveryClass::Realtime, acknowledging).error,
              RidgebreakIngressError::InvalidAcknowledgement);
}

TEST(RidgebreakAuthority,
     GameplayHashIgnoresSnapshotAckWhileTranscriptCommitsIt) {
    RidgebreakAuthoritySession acknowledged;
    RidgebreakAuthoritySession unacknowledged;
    RidgebreakAuthoritySession::Config config;
    ASSERT_TRUE(acknowledged.initialize(config));
    ASSERT_TRUE(unacknowledged.initialize(config));
    ASSERT_TRUE(acknowledged.registerClient(100u, 1u, 91'001u, 1u));
    ASSERT_TRUE(unacknowledged.registerClient(100u, 1u, 91'001u, 1u));
    ASSERT_TRUE(acknowledged.buildSnapshotPacket(100u).has_value());
    ASSERT_TRUE(unacknowledged.buildSnapshotPacket(100u).has_value());

    const std::array input{sample(1u, 1u)};
    ASSERT_TRUE(acknowledged.ingest(
        100u, DeliveryClass::Realtime,
        inputPacket(config, 1u, input, 1u, 1u, 91'001u, 1u)));
    ASSERT_TRUE(unacknowledged.ingest(
        100u, DeliveryClass::Realtime,
        inputPacket(config, 1u, input, 0u, 0u, 91'001u, 1u)));

    const auto withAck = acknowledged.canonicalSnapshot();
    const auto withoutAck = unacknowledged.canonicalSnapshot();
    ASSERT_EQ(withAck.authoritativeTick, withoutAck.authoritativeTick);
    ASSERT_EQ(withAck.players, withoutAck.players);
    EXPECT_EQ(withAck.canonicalStateHash,
              withoutAck.canonicalStateHash);
    EXPECT_NE(acknowledged.authorityTranscriptHash(),
              unacknowledged.authorityTranscriptHash());
}

TEST(RidgebreakAuthority, TranscriptIgnoresObservationalRegistrationError) {
    RidgebreakAuthoritySession rejectedDiagnostic;
    RidgebreakAuthoritySession clean;
    RidgebreakAuthoritySession::Config config;
    ASSERT_TRUE(rejectedDiagnostic.initialize(config));
    ASSERT_TRUE(clean.initialize(config));
    ASSERT_TRUE(rejectedDiagnostic.registerClient(10u, 1u, 101u, 1u));
    ASSERT_TRUE(clean.registerClient(10u, 1u, 101u, 1u));

    EXPECT_FALSE(rejectedDiagnostic.registerClient(0u, 1u, 102u, 2u));
    EXPECT_EQ(rejectedDiagnostic.lastRegistrationError(),
              RidgebreakRegistrationError::InvalidIdentity);
    EXPECT_EQ(rejectedDiagnostic.gameplayStateHash(),
              clean.gameplayStateHash());
    EXPECT_EQ(rejectedDiagnostic.authorityTranscriptHash(),
              clean.authorityTranscriptHash());
}

TEST(RidgebreakAuthority, FourClientsRemainCanonicalAcrossIngressOrder) {
    RidgebreakAuthoritySession first;
    RidgebreakAuthoritySession second;
    RidgebreakAuthoritySession::Config config;
    config.sessionId = 77u;
    config.worldId = 88u;
    ASSERT_TRUE(first.initialize(config));
    ASSERT_TRUE(second.initialize(config));
    registerFour(first);
    registerFour(second);

    for (uint64_t tick = 1u; tick <= 240u; ++tick) {
        std::array<std::vector<std::byte>, 4> packets;
        for (uint32_t player = 0u; player < 4u; ++player) {
            const uint16_t throttle = static_cast<uint16_t>(
                17'000u + player * 3'000u);
            const int16_t steer = player % 2u == 0u ? 3'500 : -3'500;
            const uint16_t flags = tick == 20u + player
                ? static_cast<uint16_t>(network::RidgebreakInputStanding)
                : uint16_t{0u};
            const std::array input{
                sample(tick, tick, throttle, steer, flags)};
            packets[player] = inputPacket(
                config, tick, input, 0u, 0u,
                10'000u + player, 7u);
        }
        for (uint32_t player = 0u; player < 4u; ++player) {
            ASSERT_TRUE(first.ingest(
                100u + player, DeliveryClass::Realtime, packets[player]));
        }
        for (uint32_t player = 4u; player-- > 0u;) {
            ASSERT_TRUE(second.ingest(
                100u + player, DeliveryClass::Realtime, packets[player]));
        }
        first.step();
        second.step();
        ASSERT_EQ(first.canonicalSnapshot(), second.canonicalSnapshot());

        if (first.snapshotDue()) {
            auto firstBatch = first.captureSnapshotBatch();
            auto secondBatch = second.captureSnapshotBatch();
            for (uint32_t player = 0u; player < 4u; ++player) {
                const auto frame = first.buildSnapshotPacket(
                    100u + player, firstBatch);
                const auto mirror = second.buildSnapshotPacket(
                    100u + player, secondBatch);
                ASSERT_TRUE(frame.has_value());
                ASSERT_TRUE(mirror.has_value());
                ASSERT_LE(frame->size(), network::kConservativeRealtimeMtu);
                const auto outer = PacketCodec::decode(
                    *frame, DeliveryClass::Realtime);
                ASSERT_TRUE(outer.packet.has_value());
                const auto snapshot = network::decodeRidgebreakSnapshot(
                    outer.packet->payload);
                ASSERT_TRUE(snapshot.snapshot.has_value());
                EXPECT_EQ(snapshot.snapshot->playerCount, 4u);
                EXPECT_EQ(snapshot.snapshot->authoritativeTick, tick);
            }
        }
    }

    const auto final = first.canonicalSnapshot();
    EXPECT_EQ(final.playerCount, 4u);
    for (const auto& player : final.players) {
        EXPECT_EQ(player.lastProcessedInputSequence, 240u);
        EXPECT_GT(player.forwardSpeedMillimetersPerSecond, 0);
        EXPECT_NE(player.positionZMillimeters, 0);
    }
    EXPECT_EQ(first.telemetry().simulationTicks, 240u);
    EXPECT_EQ(first.telemetry().acceptedInputSamples, 960u);
}

} // namespace
} // namespace voxy::server
