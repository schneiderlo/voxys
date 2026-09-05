#include "server/ridgebreak_authority.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <limits>
#include <type_traits>
#include <utility>

namespace voxy::server {
namespace {

constexpr int32_t kMaximumForwardSpeed = 55'000;
constexpr int32_t kEngineAcceleration = 14'000;
constexpr int32_t kBrakeDeceleration = 24'000;
constexpr int32_t kWorldLimitMillimeters = 2'000'000'000;

template <typename T>
void hashValue(uint64_t& hash, T value) noexcept {
    using U = std::make_unsigned_t<T>;
    U bits = std::bit_cast<U>(value);
    for (size_t byte = 0u; byte < sizeof(U); ++byte) {
        hash ^= static_cast<uint8_t>(bits >> (byte * 8u));
        hash *= 1'099'511'628'211ull;
    }
}

// Integer CORDIC. Angles use one full turn = 65536 and outputs use Q15.
std::array<int32_t, 2> directionQ15(int32_t heading) noexcept {
    static constexpr std::array<int32_t, 15> kAtanTurnsQ16{
        8192, 4836, 2555, 1297, 651, 326, 163, 81,
        41, 20, 10, 5, 3, 1, 1};
    int32_t angle = static_cast<int16_t>(heading & 0xffff);
    int32_t sign = 1;
    if (angle > 16'384) {
        angle -= 32'768;
        sign = -1;
    } else if (angle < -16'384) {
        angle += 32'768;
        sign = -1;
    }
    int32_t cosine = 19'898;
    int32_t sine = 0;
    for (uint32_t i = 0u; i < kAtanTurnsQ16.size(); ++i) {
        const int32_t direction = angle >= 0 ? 1 : -1;
        const int32_t nextCosine = cosine - direction * (sine >> i);
        sine += direction * (cosine >> i);
        cosine = nextCosine;
        angle -= direction * kAtanTurnsQ16[i];
    }
    return {sign * sine, sign * cosine};
}

uint8_t automaticGear(int32_t speed) noexcept {
    if (speed < 8'000) return 1u;
    if (speed < 16'000) return 2u;
    if (speed < 27'000) return 3u;
    if (speed < 40'000) return 4u;
    return 5u;
}

} // namespace

bool RidgebreakAuthoritySession::initialize(const Config& config) noexcept {
    if (config.sessionId == 0u || config.worldId == 0u
        || config.worldEpoch == 0u || config.authorityEpoch == 0u
        || config.inputFutureWindowTicks == 0u
        || config.inputFutureWindowTicks >= kPendingInputSlots
        || config.inputHistoryWindowTicks == 0u
        || config.maximumPacketsPerPeerTick == 0u
        || config.maximumHeldInputTicks == 0u
        || config.snapshotRateHz == 0u
        || config.snapshotRateHz > kRidgebreakAuthorityTickRate
        || kRidgebreakAuthorityTickRate % config.snapshotRateHz != 0u
        || config.maximumPacketSequenceAdvance == 0u
        || config.maximumInputSequenceAdvance == 0u) {
        return false;
    }
    config_ = config;
    peers_ = {};
    telemetry_ = {};
    highestConnectionSerials_ = {};
    highestConnectionGenerations_ = {};
    acceptedConnectionSerials_ = {};
    lastRegistrationError_ = RidgebreakRegistrationError::None;
    tick_ = 0u;
    initialized_ = true;
    return true;
}

bool RidgebreakAuthoritySession::registerClient(
    uint32_t peerId, uint32_t playerId,
    uint64_t connectionSerial,
    uint32_t connectionGeneration) noexcept {
    lastRegistrationError_ = RidgebreakRegistrationError::None;
    if (!initialized_) {
        lastRegistrationError_ = RidgebreakRegistrationError::NotInitialized;
        return false;
    }
    if (peerId == 0u || playerId == 0u
        || playerId > kRidgebreakAuthorityPlayerCount
        || connectionSerial == 0u
        || connectionGeneration == 0u) {
        lastRegistrationError_ = RidgebreakRegistrationError::InvalidIdentity;
        return false;
    }
    // Replacing the same active peer/player is transactional. This is
    // important at the lifecycle boundary: a rejected reconnect must not
    // destroy the still-valid authority slot belonging to the old lifetime.
    Peer* activeReplacement = nullptr;
    for (auto& peer : peers_) {
        if (!peer.active) continue;
        if (peer.peerId == peerId && peer.playerId == playerId) {
            activeReplacement = &peer;
            continue;
        }
        if (peer.peerId == peerId) {
            lastRegistrationError_ =
                RidgebreakRegistrationError::ActivePeerCollision;
            return false;
        }
        if (peer.playerId == playerId) {
            lastRegistrationError_ =
                RidgebreakRegistrationError::ActivePlayerCollision;
            return false;
        }
        if (peer.connectionSerial == connectionSerial) {
            lastRegistrationError_ =
                RidgebreakRegistrationError::ReusedConnectionSerial;
            return false;
        }
    }
    const size_t fenceIndex = static_cast<size_t>(playerId - 1u);
    network::AckWindow candidateSerials = acceptedConnectionSerials_;
    if (!candidateSerials.observe(connectionSerial)) {
        lastRegistrationError_ =
            RidgebreakRegistrationError::ReusedConnectionSerial;
        return false;
    }
    if (connectionSerial <= highestConnectionSerials_[fenceIndex]) {
        lastRegistrationError_ =
            RidgebreakRegistrationError::ReusedConnectionSerial;
        return false;
    }
    if (connectionGeneration
        <= highestConnectionGenerations_[fenceIndex]) {
        lastRegistrationError_ =
            RidgebreakRegistrationError::NonIncreasingGeneration;
        return false;
    }

    Peer* samePlayerTombstone = nullptr;
    Peer* samePeerTombstone = nullptr;
    for (auto& peer : peers_) {
        if (peer.occupied && !peer.active && peer.playerId == playerId) {
            samePlayerTombstone = &peer;
        }
        if (peer.occupied && !peer.active && peer.peerId == peerId) {
            samePeerTombstone = &peer;
        }
    }
    Peer* reusable = activeReplacement != nullptr
        ? activeReplacement
        : (samePlayerTombstone != nullptr
            ? samePlayerTombstone : samePeerTombstone);
    if (samePlayerTombstone != nullptr && samePeerTombstone != nullptr
        && samePlayerTombstone != samePeerTombstone) {
        // One reconnect may cross both an old player identity and a reused
        // transport endpoint. The per-player high-water fences preserve both
        // lifetimes, so the second tombstone must be removed to keep
        // peer/player lookup one-to-one.
        *samePeerTombstone = {};
    }
    if (reusable == nullptr) {
        auto slot = std::find_if(peers_.begin(), peers_.end(),
            [](const Peer& peer) { return !peer.occupied; });
        if (slot != peers_.end()) reusable = &*slot;
    }
    if (reusable == nullptr) {
        auto slot = std::min_element(
            peers_.begin(), peers_.end(),
            [](const Peer& lhs, const Peer& rhs) {
                if (lhs.active != rhs.active) return !lhs.active;
                return lhs.disconnectedTick < rhs.disconnectedTick;
            });
        if (slot != peers_.end() && !slot->active) reusable = &*slot;
    }
    if (reusable == nullptr) {
        lastRegistrationError_ = RidgebreakRegistrationError::NoInactiveSlot;
        return false;
    }

    highestConnectionSerials_[fenceIndex] = connectionSerial;
    highestConnectionGenerations_[fenceIndex] = connectionGeneration;
    acceptedConnectionSerials_ = candidateSerials;
    const auto index = static_cast<uint32_t>(reusable - peers_.data());
    *reusable = {};
    Peer* slot = reusable;
    slot->peerId = peerId;
    slot->playerId = playerId;
    slot->connectionGeneration = connectionGeneration;
    slot->connectionSerial = connectionSerial;
    slot->occupied = true;
    slot->active = true;
    const int32_t signedIndex = static_cast<int32_t>(index);
    slot->spawnXMillimeters = (signedIndex % 2) * 4'000 - 2'000;
    slot->spawnZMillimeters = (signedIndex / 2) * -5'000;
    slot->state.playerId = playerId;
    slot->state.connectionGeneration = connectionGeneration;
    slot->state.connectionSerial = connectionSerial;
    slot->state.positionXMillimeters = slot->spawnXMillimeters;
    slot->state.positionZMillimeters = slot->spawnZMillimeters;
    slot->state.gear = 1u;
    slot->state.lifecycle = network::RidgebreakPlayerLifecycle::Riding;
    return true;
}

bool RidgebreakAuthoritySession::disconnectClient(uint32_t peerId) noexcept {
    Peer* peer = findPeer(peerId);
    if (peer == nullptr || !peer->active) return false;
    peer->active = false;
    peer->disconnectedTick = tick_;
    peer->state.lifecycle = network::RidgebreakPlayerLifecycle::Disconnected;
    peer->state.velocityXMillimetersPerSecond = 0;
    peer->state.velocityZMillimetersPerSecond = 0;
    peer->state.forwardSpeedMillimetersPerSecond = 0;
    peer->state.leanQ15 = 0;
    peer->state.stateFlags = 0u;
    peer->pending = {};
    peer->heldInput = {};
    peer->ticksSinceFreshInput = 0u;
    peer->positionRemainderX = 0;
    peer->positionRemainderZ = 0;
    return true;
}

RidgebreakIngressResult RidgebreakAuthoritySession::ingest(
    uint32_t peerId, network::DeliveryClass delivery,
    std::span<const std::byte> framedPacket) noexcept {
    RidgebreakIngressResult result;
    Peer* peer = findPeer(peerId);
    if (peer == nullptr) result.error = RidgebreakIngressError::UnknownPeer;
    else if (!peer->active) result.error = RidgebreakIngressError::InactivePeer;
    else if (delivery != network::DeliveryClass::Realtime)
        result.error = RidgebreakIngressError::WrongDeliveryClass;
    if (result.error != RidgebreakIngressError::None) {
        ++telemetry_.rejectedPackets;
        return result;
    }
    if (peer->packetsThisTick >= config_.maximumPacketsPerPeerTick) {
        result.error = RidgebreakIngressError::InputRateExceeded;
        ++telemetry_.rejectedPackets;
        return result;
    }
    ++peer->packetsThisTick;

    const auto decoded = network::PacketCodec::decode(framedPacket, delivery);
    if (!decoded.packet.has_value()) {
        result.error = RidgebreakIngressError::PacketDecodeFailed;
    } else if (decoded.packet->header.payloadType
               != network::PacketPayloadType::Input) {
        result.error = RidgebreakIngressError::WrongPayloadType;
    } else if (decoded.packet->header.sessionId != config_.sessionId
               || decoded.packet->header.worldId != config_.worldId
               || decoded.packet->header.worldEpoch != config_.worldEpoch
               || decoded.packet->header.authorityEpoch
                    != config_.authorityEpoch
               || decoded.packet->header.flags != 0u
               || decoded.packet->header.sequence == 0u) {
        result.error = RidgebreakIngressError::IdentityMismatch;
    } else if (decoded.packet->header.ackSequence > peer->outboundSequence
               || (decoded.packet->header.ackSequence == 0u
                   && decoded.packet->header.ackBits != 0u)
               || (decoded.packet->header.ackSequence != 0u
                   && (decoded.packet->header.ackBits & 1u) == 0u)) {
        result.error = RidgebreakIngressError::InvalidAcknowledgement;
    }
    if (result.error != RidgebreakIngressError::None) {
        ++telemetry_.rejectedPackets;
        return result;
    }

    const network::Packet& packet = *decoded.packet;
    const uint64_t latestPacketSequence = peer->receivedPackets.latest();
    if (packet.header.sequence > latestPacketSequence
        && packet.header.sequence - latestPacketSequence
            > config_.maximumPacketSequenceAdvance) {
        result.error = RidgebreakIngressError::PacketSequenceJump;
        ++telemetry_.rejectedPackets;
        return result;
    }
    const auto input = network::decodeRidgebreakInputBundle(packet.payload);
    if (!input.bundle.has_value()) {
        result.error = RidgebreakIngressError::InputDecodeFailed;
        ++telemetry_.rejectedPackets;
        return result;
    }
    if (input.bundle->connectionSerial != peer->connectionSerial
        || input.bundle->connectionGeneration
            != peer->connectionGeneration) {
        result.error = RidgebreakIngressError::ConnectionIdentityMismatch;
        ++telemetry_.rejectedPackets;
        return result;
    }
    const uint64_t newestInputSequence = input.bundle->samples[
        input.bundle->sampleCount - 1u].inputSequence;
    if (newestInputSequence > peer->latestReceivedInputSequence
        && newestInputSequence - peer->latestReceivedInputSequence
            > config_.maximumInputSequenceAdvance) {
        result.error = RidgebreakIngressError::InputSequenceJump;
        ++telemetry_.rejectedPackets;
        return result;
    }
    if (packet.header.tick
        != input.bundle->samples[input.bundle->sampleCount - 1u].requestedTick) {
        result.error = RidgebreakIngressError::IdentityMismatch;
        ++telemetry_.rejectedPackets;
        return result;
    }
    for (uint32_t i = 0u; i < input.bundle->sampleCount; ++i) {
        const uint64_t requested = input.bundle->samples[i].requestedTick;
        if ((requested > tick_
             && requested - tick_ > config_.inputFutureWindowTicks)
            || (requested <= tick_
                && tick_ - requested > config_.inputHistoryWindowTicks)) {
            result.error = RidgebreakIngressError::InputOutsideTickWindow;
            ++telemetry_.rejectedPackets;
            return result;
        }
    }

    if (!peer->receivedPackets.observe(packet.header.sequence)) {
        result.error = RidgebreakIngressError::DuplicatePacket;
        ++telemetry_.duplicatePackets;
        ++telemetry_.rejectedPackets;
        return result;
    }
    const uint64_t previouslyReceivedInput =
        peer->latestReceivedInputSequence;
    for (uint32_t i = 0u; i < input.bundle->sampleCount; ++i) {
        const auto& sample = input.bundle->samples[i];
        if (sample.inputSequence <= previouslyReceivedInput) {
            ++result.redundantSamples;
            continue;
        }
        peer->latestReceivedInputSequence = std::max(
            peer->latestReceivedInputSequence, sample.inputSequence);
        if (sample.requestedTick <= tick_) {
            ++result.expiredSamples;
            continue;
        }
        PendingInput& pending = peer->pending[
            sample.requestedTick % kPendingInputSlots];
        if (pending.valid && pending.tick == sample.requestedTick
            && pending.sample.inputSequence >= sample.inputSequence) {
            ++result.redundantSamples;
            continue;
        }
        pending.tick = sample.requestedTick;
        pending.sample = sample;
        pending.valid = true;
        ++result.acceptedSamples;
    }

    if (packet.header.ackSequence != 0u) {
        for (uint64_t sequence = peer->outboundSequence;
             sequence > peer->latestAcknowledgedSnapshotSequence
             && peer->outboundSequence - sequence < 64u; --sequence) {
            if (acknowledgedBy(packet.header.ackSequence,
                               packet.header.ackBits, sequence)) {
                peer->latestAcknowledgedSnapshotSequence = sequence;
                break;
            }
        }
    }
    telemetry_.acceptedInputSamples += result.acceptedSamples;
    telemetry_.redundantInputSamples += result.redundantSamples;
    telemetry_.expiredInputSamples += result.expiredSamples;
    ++telemetry_.acceptedPackets;
    return result;
}

void RidgebreakAuthoritySession::step() noexcept {
    if (!initialized_ || tick_ == std::numeric_limits<uint64_t>::max()) return;
    ++tick_;
    for (Peer& peer : peers_) {
        peer.packetsThisTick = 0u;
        if (!peer.occupied || !peer.active) continue;
        PendingInput& pending = peer.pending[tick_ % kPendingInputSlots];
        bool fresh = pending.valid && pending.tick == tick_;
        network::RidgebreakInputSample input{};
        if (fresh) {
            input = pending.sample;
            pending = {};
            peer.heldInput = input;
            peer.ticksSinceFreshInput = 0u;
            peer.latestProcessedInputSequence = input.inputSequence;
            peer.state.lastProcessedInputSequence = input.inputSequence;
        } else {
            ++peer.ticksSinceFreshInput;
            if (peer.ticksSinceFreshInput <= config_.maximumHeldInputTicks) {
                input = peer.heldInput;
                input.flags &= network::RidgebreakInputStanding
                    | network::RidgebreakInputDucking;
                ++telemetry_.heldInputTicks;
            } else {
                ++telemetry_.neutralInputTicks;
            }
        }
        simulate(peer, input, fresh);
    }
    ++telemetry_.simulationTicks;
}

bool RidgebreakAuthoritySession::snapshotDue() const noexcept {
    return initialized_ && tick_ != 0u
        && tick_ % (kRidgebreakAuthorityTickRate
                    / config_.snapshotRateHz) == 0u;
}

network::RidgebreakSnapshot
RidgebreakAuthoritySession::canonicalSnapshot() const noexcept {
    network::RidgebreakSnapshot snapshot;
    snapshot.authoritativeTick = tick_;
    populateCanonicalPlayers(snapshot);
    snapshot.canonicalStateHash = gameplayStateHash();
    return snapshot;
}

uint64_t RidgebreakAuthoritySession::gameplayStateHash() const noexcept {
    uint64_t hash = 14'695'981'039'346'656'037ull;
    hashValue(hash, tick_);
    network::RidgebreakSnapshot snapshot;
    populateCanonicalPlayers(snapshot);
    hashValue(hash, snapshot.playerCount);
    for (uint32_t i = 0u; i < snapshot.playerCount; ++i) {
        const auto& state = snapshot.players[i];
        hashValue(hash, state.playerId);
        hashValue(hash, state.connectionGeneration);
        hashValue(hash, state.connectionSerial);
        hashValue(hash, state.lastProcessedInputSequence);
        hashValue(hash, state.positionXMillimeters);
        hashValue(hash, state.positionZMillimeters);
        hashValue(hash, state.velocityXMillimetersPerSecond);
        hashValue(hash, state.velocityZMillimetersPerSecond);
        hashValue(hash, state.headingTurnsQ16);
        hashValue(hash, state.forwardSpeedMillimetersPerSecond);
        hashValue(hash, state.leanQ15);
        hashValue(hash, state.gear);
        hashValue(hash, static_cast<uint8_t>(state.lifecycle));
        hashValue(hash, state.stateFlags);
    }
    return hash;
}

uint64_t RidgebreakAuthoritySession::authorityTranscriptHash() const noexcept {
    return transcriptHash();
}

void RidgebreakAuthoritySession::populateCanonicalPlayers(
    network::RidgebreakSnapshot& snapshot) const noexcept {
    for (const Peer& peer : peers_) {
        if (!peer.occupied) continue;
        snapshot.players[snapshot.playerCount++] = peer.state;
    }
    // The roster is bounded to four entries. An explicit insertion sort keeps
    // every access visibly inside the fixed array under optimized GCC builds.
    for (uint32_t i = 1u; i < snapshot.playerCount; ++i) {
        const network::RidgebreakPlayerState value = snapshot.players[i];
        uint32_t destination = i;
        while (destination > 0u
               && snapshot.players[destination - 1u].playerId
                    > value.playerId) {
            snapshot.players[destination] =
                snapshot.players[destination - 1u];
            --destination;
        }
        snapshot.players[destination] = value;
    }
}

std::optional<std::vector<std::byte>>
RidgebreakAuthoritySession::buildSnapshotPacket(uint32_t peerId) noexcept {
    auto batch = captureSnapshotBatch();
    return buildSnapshotPacket(peerId, batch);
}

RidgebreakAuthoritySession::SnapshotBatch
RidgebreakAuthoritySession::captureSnapshotBatch() const noexcept {
    return SnapshotBatch(canonicalSnapshot());
}

std::optional<std::vector<std::byte>>
RidgebreakAuthoritySession::buildSnapshotPacket(
    uint32_t peerId, SnapshotBatch& batch) noexcept {
    Peer* peer = findPeer(peerId);
    if (!initialized_ || peer == nullptr || !peer->active
        || peer->outboundSequence == std::numeric_limits<uint64_t>::max()) {
        return std::nullopt;
    }
    const uint32_t peerIndex = static_cast<uint32_t>(peer - peers_.data());
    const uint32_t peerBit = 1u << peerIndex;
    if ((batch.usedPeerMask_ & peerBit) != 0u) return std::nullopt;
    const auto& snapshot = batch.snapshot();
    if (snapshot.authoritativeTick != tick_) return std::nullopt;
    network::RidgebreakSnapshot current;
    current.authoritativeTick = tick_;
    populateCanonicalPlayers(current);
    if (snapshot.playerCount != current.playerCount
        || snapshot.players != current.players) return std::nullopt;
    auto payload = network::encodeRidgebreakSnapshot(snapshot);
    if (payload.empty()) return std::nullopt;
    network::Packet packet;
    packet.header.payloadType = network::PacketPayloadType::Snapshot;
    packet.header.sessionId = config_.sessionId;
    packet.header.worldId = config_.worldId;
    packet.header.worldEpoch = config_.worldEpoch;
    packet.header.authorityEpoch = config_.authorityEpoch;
    packet.header.sequence = ++peer->outboundSequence;
    packet.header.ackSequence = peer->receivedPackets.latest();
    packet.header.ackBits = peer->receivedPackets.bits();
    packet.header.tick = tick_;
    packet.payload = std::move(payload);
    auto encoded = network::PacketCodec::encode(
        packet, network::DeliveryClass::Realtime);
    if (!encoded.error.empty()) {
        --peer->outboundSequence;
        return std::nullopt;
    }
    batch.usedPeerMask_ |= peerBit;
    ++telemetry_.snapshotsBuilt;
    telemetry_.snapshotBytesBuilt += encoded.bytes.size();
    return std::move(encoded.bytes);
}

std::optional<RidgebreakPeerView> RidgebreakAuthoritySession::peerView(
    uint32_t peerId) const noexcept {
    const Peer* peer = findPeer(peerId);
    if (peer == nullptr) return std::nullopt;
    return RidgebreakPeerView{
        .peerId = peer->peerId,
        .playerId = peer->playerId,
        .connectionGeneration = peer->connectionGeneration,
        .connectionSerial = peer->connectionSerial,
        .latestReceivedPacketSequence = peer->receivedPackets.latest(),
        .latestProcessedInputSequence = peer->latestProcessedInputSequence,
        .latestSentSnapshotSequence = peer->outboundSequence,
        .latestAcknowledgedSnapshotSequence =
            peer->latestAcknowledgedSnapshotSequence,
        .active = peer->active,
    };
}

RidgebreakAuthoritySession::Peer* RidgebreakAuthoritySession::findPeer(
    uint32_t peerId) noexcept {
    auto found = std::find_if(peers_.begin(), peers_.end(),
        [peerId](const Peer& peer) {
            return peer.occupied && peer.peerId == peerId;
        });
    return found == peers_.end() ? nullptr : &*found;
}

const RidgebreakAuthoritySession::Peer* RidgebreakAuthoritySession::findPeer(
    uint32_t peerId) const noexcept {
    auto found = std::find_if(peers_.begin(), peers_.end(),
        [peerId](const Peer& peer) {
            return peer.occupied && peer.peerId == peerId;
        });
    return found == peers_.end() ? nullptr : &*found;
}

bool RidgebreakAuthoritySession::acknowledgedBy(
    uint64_t ackSequence, uint64_t ackBits, uint64_t sequence) noexcept {
    if (sequence > ackSequence) return false;
    const uint64_t distance = ackSequence - sequence;
    return distance < 64u
        && (ackBits & (uint64_t{1} << distance)) != 0u;
}

void RidgebreakAuthoritySession::simulate(
    Peer& peer, const network::RidgebreakInputSample& input,
    bool freshInput) noexcept {
    auto& state = peer.state;
    if (freshInput && (input.flags & network::RidgebreakInputReset) != 0u) {
        state.positionXMillimeters = peer.spawnXMillimeters;
        state.positionZMillimeters = peer.spawnZMillimeters;
        state.velocityXMillimetersPerSecond = 0;
        state.velocityZMillimetersPerSecond = 0;
        state.forwardSpeedMillimetersPerSecond = 0;
        state.headingTurnsQ16 = 0;
        state.leanQ15 = 0;
        state.gear = 1u;
        peer.positionRemainderX = 0;
        peer.positionRemainderZ = 0;
    }

    int32_t acceleration =
        static_cast<int32_t>(input.throttleQ15) * kEngineAcceleration / 32'767;
    acceleration -=
        static_cast<int32_t>(input.brakeQ15) * kBrakeDeceleration / 32'767;
    acceleration -= state.forwardSpeedMillimetersPerSecond / 10;
    state.forwardSpeedMillimetersPerSecond = std::clamp(
        state.forwardSpeedMillimetersPerSecond
            + acceleration / static_cast<int32_t>(kRidgebreakAuthorityTickRate),
        0, kMaximumForwardSpeed);

    if (freshInput && (input.flags & network::RidgebreakInputGearUp) != 0u)
        state.gear = std::min<uint8_t>(5u, state.gear + 1u);
    else if (freshInput
             && (input.flags & network::RidgebreakInputGearDown) != 0u)
        state.gear = static_cast<uint8_t>(std::max<int32_t>(
            1, static_cast<int32_t>(state.gear) - 1));
    else
        state.gear = automaticGear(state.forwardSpeedMillimetersPerSecond);

    const int32_t steeringScale = 32
        + state.forwardSpeedMillimetersPerSecond / 220;
    const int32_t headingDelta =
        static_cast<int32_t>(input.steerQ15) * steeringScale / 32'767;
    const uint16_t wrappedHeading = static_cast<uint16_t>(
        static_cast<uint32_t>(state.headingTurnsQ16)
        + static_cast<uint32_t>(headingDelta));
    state.headingTurnsQ16 = std::bit_cast<int16_t>(wrappedHeading);
    state.leanQ15 = static_cast<int16_t>(
        state.leanQ15
        + (static_cast<int32_t>(input.leanQ15) - state.leanQ15) / 4);

    const auto direction = directionQ15(state.headingTurnsQ16);
    state.velocityXMillimetersPerSecond = static_cast<int32_t>(
        int64_t{direction[0]} * state.forwardSpeedMillimetersPerSecond / 32'768);
    state.velocityZMillimetersPerSecond = static_cast<int32_t>(
        int64_t{direction[1]} * state.forwardSpeedMillimetersPerSecond / 32'768);
    const int64_t nextX = int64_t{peer.positionRemainderX}
        + state.velocityXMillimetersPerSecond;
    const int64_t nextZ = int64_t{peer.positionRemainderZ}
        + state.velocityZMillimetersPerSecond;
    state.positionXMillimeters = static_cast<int32_t>(std::clamp<int64_t>(
        int64_t{state.positionXMillimeters}
            + nextX / kRidgebreakAuthorityTickRate,
        -kWorldLimitMillimeters, kWorldLimitMillimeters));
    state.positionZMillimeters = static_cast<int32_t>(std::clamp<int64_t>(
        int64_t{state.positionZMillimeters}
            + nextZ / kRidgebreakAuthorityTickRate,
        -kWorldLimitMillimeters, kWorldLimitMillimeters));
    peer.positionRemainderX = static_cast<int32_t>(
        nextX % kRidgebreakAuthorityTickRate);
    peer.positionRemainderZ = static_cast<int32_t>(
        nextZ % kRidgebreakAuthorityTickRate);
    state.stateFlags = input.flags
        & (network::RidgebreakInputStanding | network::RidgebreakInputDucking);
}

uint64_t RidgebreakAuthoritySession::transcriptHash() const noexcept {
    uint64_t hash = 14'695'981'039'346'656'037ull;
    hashValue(hash, tick_);
    hashValue(hash, config_.sessionId);
    hashValue(hash, config_.worldId);
    hashValue(hash, config_.worldEpoch);
    hashValue(hash, config_.authorityEpoch);
    hashValue(hash, config_.inputFutureWindowTicks);
    hashValue(hash, config_.inputHistoryWindowTicks);
    hashValue(hash, config_.maximumPacketsPerPeerTick);
    hashValue(hash, config_.maximumHeldInputTicks);
    hashValue(hash, config_.snapshotRateHz);
    hashValue(hash, config_.maximumPacketSequenceAdvance);
    hashValue(hash, config_.maximumInputSequenceAdvance);
    for (size_t player = 0u; player < highestConnectionSerials_.size();
         ++player) {
        hashValue(hash, highestConnectionSerials_[player]);
        hashValue(hash, highestConnectionGenerations_[player]);
    }
    hashValue(hash, acceptedConnectionSerials_.latest());
    hashValue(hash, acceptedConnectionSerials_.bits());
    for (const Peer& peer : peers_) {
        hashValue(hash, static_cast<uint8_t>(peer.occupied));
        if (!peer.occupied) continue;
        hashValue(hash, static_cast<uint8_t>(peer.active));
        hashValue(hash, peer.disconnectedTick);
        hashValue(hash, peer.peerId);
        hashValue(hash, peer.playerId);
        hashValue(hash, peer.connectionSerial);
        hashValue(hash, peer.connectionGeneration);
        hashValue(hash, peer.receivedPackets.latest());
        hashValue(hash, peer.receivedPackets.bits());
        hashValue(hash, peer.latestReceivedInputSequence);
        hashValue(hash, peer.latestProcessedInputSequence);
        hashValue(hash, peer.outboundSequence);
        hashValue(hash, peer.latestAcknowledgedSnapshotSequence);
        hashValue(hash, peer.packetsThisTick);
        hashValue(hash, peer.ticksSinceFreshInput);
        hashValue(hash, peer.positionRemainderX);
        hashValue(hash, peer.positionRemainderZ);
        hashValue(hash, peer.spawnXMillimeters);
        hashValue(hash, peer.spawnZMillimeters);
        const auto hashInput = [&hash](
            const network::RidgebreakInputSample& input) {
            hashValue(hash, input.requestedTick);
            hashValue(hash, input.inputSequence);
            hashValue(hash, input.throttleQ15);
            hashValue(hash, input.brakeQ15);
            hashValue(hash, input.steerQ15);
            hashValue(hash, input.leanQ15);
            hashValue(hash, input.flags);
            hashValue(hash, input.reserved);
        };
        hashInput(peer.heldInput);
        for (const PendingInput& pending : peer.pending) {
            hashValue(hash, static_cast<uint8_t>(pending.valid));
            hashValue(hash, pending.tick);
            hashInput(pending.sample);
        }
        const auto& state = peer.state;
        hashValue(hash, state.playerId);
        hashValue(hash, state.connectionGeneration);
        hashValue(hash, state.connectionSerial);
        hashValue(hash, state.lastProcessedInputSequence);
        hashValue(hash, state.positionXMillimeters);
        hashValue(hash, state.positionZMillimeters);
        hashValue(hash, state.velocityXMillimetersPerSecond);
        hashValue(hash, state.velocityZMillimetersPerSecond);
        hashValue(hash, state.headingTurnsQ16);
        hashValue(hash, state.forwardSpeedMillimetersPerSecond);
        hashValue(hash, state.leanQ15);
        hashValue(hash, state.gear);
        hashValue(hash, static_cast<uint8_t>(state.lifecycle));
        hashValue(hash, state.stateFlags);
    }
    return hash;
}

} // namespace voxy::server
