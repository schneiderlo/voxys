#include "network/ridgebreak_client_replication.hpp"

namespace voxy::network {

bool RidgebreakClientSnapshotWindow::reset(const Config& config) noexcept {
    if (config.sessionId == 0u || config.worldId == 0u
        || config.worldEpoch == 0u || config.authorityEpoch == 0u
        || config.playerId == 0u || config.connectionSerial == 0u
        || config.connectionGeneration == 0u
        || config.maximumSequenceAdvance == 0u) {
        return false;
    }
    config_ = config;
    received_ = {};
    latestAuthoritativeTick_ = 0u;
    initialized_ = true;
    return true;
}

RidgebreakSnapshotWindowResult RidgebreakClientSnapshotWindow::accept(
    uint64_t transportConnectionSerial,
    DeliveryClass delivery,
    std::span<const std::byte> framedPacket) noexcept {
    RidgebreakSnapshotWindowResult result;
    if (!initialized_) {
        result.error = RidgebreakSnapshotWindowError::NotInitialized;
        return result;
    }
    if (transportConnectionSerial != config_.connectionSerial) {
        result.error =
            RidgebreakSnapshotWindowError::ConnectionSerialMismatch;
        return result;
    }
    if (delivery != DeliveryClass::Realtime) {
        result.error = RidgebreakSnapshotWindowError::WrongDeliveryClass;
        return result;
    }
    const auto outer = PacketCodec::decode(framedPacket, delivery);
    if (!outer.packet.has_value()) {
        result.error = RidgebreakSnapshotWindowError::OuterDecodeFailed;
        return result;
    }
    const Packet& packet = *outer.packet;
    if (packet.header.payloadType != PacketPayloadType::Snapshot
        || packet.header.flags != 0u || packet.header.sequence == 0u
        || packet.header.sessionId != config_.sessionId
        || packet.header.worldId != config_.worldId
        || packet.header.worldEpoch != config_.worldEpoch
        || packet.header.authorityEpoch != config_.authorityEpoch) {
        result.error = RidgebreakSnapshotWindowError::OuterIdentityMismatch;
        return result;
    }
    const uint64_t latest = received_.latest();
    if (packet.header.sequence > latest
        && packet.header.sequence - latest > config_.maximumSequenceAdvance) {
        result.error = RidgebreakSnapshotWindowError::SequenceJump;
        return result;
    }
    if (latest != 0u && packet.header.sequence <= latest) {
        result.error =
            RidgebreakSnapshotWindowError::DuplicateOrStaleSequence;
        return result;
    }
    const auto decoded = decodeRidgebreakSnapshot(packet.payload);
    if (!decoded.snapshot.has_value()) {
        result.error = RidgebreakSnapshotWindowError::SnapshotDecodeFailed;
        return result;
    }
    if (packet.header.tick != decoded.snapshot->authoritativeTick) {
        result.error = RidgebreakSnapshotWindowError::SnapshotTickMismatch;
        return result;
    }
    bool foundPlayer = false;
    for (uint32_t i = 0u; i < decoded.snapshot->playerCount; ++i) {
        const auto& player = decoded.snapshot->players[i];
        if (player.playerId != config_.playerId) continue;
        foundPlayer = player.connectionSerial == config_.connectionSerial
            && player.connectionGeneration == config_.connectionGeneration
            && player.lifecycle != RidgebreakPlayerLifecycle::Disconnected;
        break;
    }
    if (!foundPlayer) {
        result.error = RidgebreakSnapshotWindowError::PlayerIdentityMismatch;
        return result;
    }
    if (decoded.snapshot->authoritativeTick <= latestAuthoritativeTick_) {
        result.error = RidgebreakSnapshotWindowError::StaleSnapshotTick;
        return result;
    }
    // Commit both receive cursors only after every structural and semantic
    // fence has passed. A rejected high sequence must not poison the window.
    if (!received_.observe(packet.header.sequence)) {
        result.error =
            RidgebreakSnapshotWindowError::DuplicateOrStaleSequence;
        return result;
    }
    latestAuthoritativeTick_ = decoded.snapshot->authoritativeTick;
    result.packetSequence = packet.header.sequence;
    result.snapshot = *decoded.snapshot;
    return result;
}

} // namespace voxy::network
