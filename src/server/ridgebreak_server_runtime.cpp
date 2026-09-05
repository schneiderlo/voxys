#include "server/ridgebreak_server_runtime.hpp"

#include <limits>
#include <utility>

namespace voxy::server {

bool RidgebreakServerRuntime::initialize(
    const Config& config,
    std::unique_ptr<network::IMultiplayerTransport> transport) noexcept {
    if (initialized_ || transport == nullptr
        || config.maximumFramesPerTick == 0u
        || config.snapshotRetryIntervalTicks == 0u
        || config.maximumSnapshotSendAttempts == 0u
        || !authority_.initialize(config.authority)) {
        fault_ = RidgebreakServerFault::InvalidConfiguration;
        return false;
    }
    config_ = config;
    transport_ = std::move(transport);
    peers_ = {};
    lastSnapshotFrames_ = {};
    lastSnapshotSerials_ = {};
    lastBroadcastSnapshot_.reset();
    telemetry_ = {};
    fault_ = RidgebreakServerFault::None;
    lastRegistrationError_ = RidgebreakRegistrationError::None;
    initialized_ = true;
    return true;
}

RidgebreakServerTickResult RidgebreakServerRuntime::tickOnce() noexcept {
    RidgebreakServerTickResult result;
    if (!initialized_ || faulted() || transport_ == nullptr) return result;
    transport_->service();
    ++telemetry_.serviceCalls;

    for (uint32_t drained = 0u; drained < config_.maximumFramesPerTick;
         ++drained) {
        auto frame = transport_->poll();
        if (!frame.has_value()) break;
        ++result.framesPolled;
        ++telemetry_.framesPolled;
        if (!frame->isData()) {
            if (!processLifecycle(*frame) && faulted()) return result;
            continue;
        }
        if (frame->peerId == 0u
            || frame->peerId > kRidgebreakAuthorityPlayerCount) {
            ++result.inputsRejected;
            ++telemetry_.inputFramesRejected;
            continue;
        }
        const Peer& peer = peers_[frame->peerId - 1u];
        if (!peer.active || peer.connectionSerial != frame->connectionSerial) {
            ++result.inputsRejected;
            ++telemetry_.inputFramesRejected;
            continue;
        }
        const auto ingress = authority_.ingest(
            frame->peerId, frame->delivery, frame->bytes);
        if (ingress) {
            ++result.inputsAccepted;
            ++telemetry_.inputFramesAccepted;
        } else {
            ++result.inputsRejected;
            ++telemetry_.inputFramesRejected;
            result.lastIngressError = ingress.error;
        }
    }

    authority_.step();
    ++telemetry_.simulationTicks;
    retireAcknowledgedSnapshots();

    bool pending = pendingSnapshotPeers() != 0u;
    if (pending) {
        bool exhausted = false;
        for (const Peer& peer : peers_) {
            exhausted = exhausted
                || (peer.pendingSnapshotSequence != 0u
                    && peer.snapshotSendAttempts
                        >= config_.maximumSnapshotSendAttempts);
        }
        if (exhausted && authority_.snapshotDue()) {
            for (uint32_t index = 0u; index < peers_.size(); ++index)
                clearPendingSnapshot(index);
            ++telemetry_.snapshotSupersessions;
            pending = false;
        } else {
            for (uint32_t index = 0u; index < peers_.size(); ++index) {
                Peer& peer = peers_[index];
                if (peer.pendingSnapshotSequence == 0u
                    || peer.snapshotSendAttempts
                        >= config_.maximumSnapshotSendAttempts
                    || authority_.tick() - peer.lastSnapshotAttemptTick
                        < config_.snapshotRetryIntervalTicks) {
                    continue;
                }
                (void)sendPendingSnapshot(index, true, &result);
            }
            return result;
        }
    }
    if (pending || !authority_.snapshotDue()) return result;

    auto batch = authority_.captureSnapshotBatch();
    lastBroadcastSnapshot_ = batch.snapshot();
    for (uint32_t index = 0u; index < peers_.size(); ++index) {
        const Peer& peer = peers_[index];
        if (!peer.active) {
            clearPendingSnapshot(index);
            continue;
        }
        const uint32_t peerId = index + 1u;
        auto snapshot = authority_.buildSnapshotPacket(peerId, batch);
        if (!snapshot.has_value()) {
            fail(RidgebreakServerFault::SnapshotBuildFailed);
            return result;
        }
        lastSnapshotFrames_[index] = *snapshot;
        lastSnapshotSerials_[index] = peer.connectionSerial;
        Peer& mutablePeer = peers_[index];
        const auto view = authority_.peerView(peerId);
        if (!view.has_value()) {
            fail(RidgebreakServerFault::SnapshotBuildFailed);
            return result;
        }
        mutablePeer.pendingSnapshotSequence =
            view->latestSentSnapshotSequence;
        mutablePeer.lastSnapshotAttemptTick = authority_.tick();
        mutablePeer.snapshotSendAttempts = 0u;
        (void)sendPendingSnapshot(index, false, &result);
    }
    return result;
}

void RidgebreakServerRuntime::serviceTransportOnly() noexcept {
    if (!initialized_ || faulted() || transport_ == nullptr) return;
    transport_->service();
    ++telemetry_.serviceCalls;
}

uint32_t RidgebreakServerRuntime::retransmitLastSnapshot() noexcept {
    if (!initialized_ || faulted() || transport_ == nullptr
        || !lastBroadcastSnapshot_.has_value()) return 0u;
    uint32_t sent = 0u;
    for (uint32_t index = 0u; index < peers_.size(); ++index) {
        const Peer& peer = peers_[index];
        if (!peer.active || peer.pendingSnapshotSequence == 0u
            || peer.snapshotSendAttempts
                >= config_.maximumSnapshotSendAttempts
            || lastSnapshotFrames_[index].empty()
            || lastSnapshotSerials_[index] != peer.connectionSerial) continue;
        if (sendPendingSnapshot(index, true)) ++sent;
    }
    return sent;
}

void RidgebreakServerRuntime::close() noexcept {
    if (transport_ != nullptr) transport_->close();
    transport_.reset();
    initialized_ = false;
}

void RidgebreakServerRuntime::fail(RidgebreakServerFault fault) noexcept {
    if (fault_ != RidgebreakServerFault::None) return;
    fault_ = fault;
    if (transport_ != nullptr) transport_->close();
}

uint32_t RidgebreakServerRuntime::pendingSnapshotPeers() const noexcept {
    uint32_t count = 0u;
    for (const Peer& peer : peers_)
        count += peer.pendingSnapshotSequence != 0u ? 1u : 0u;
    return count;
}

void RidgebreakServerRuntime::clearPendingSnapshot(uint32_t index) noexcept {
    peers_[index].pendingSnapshotSequence = 0u;
    peers_[index].lastSnapshotAttemptTick = 0u;
    peers_[index].snapshotSendAttempts = 0u;
    lastSnapshotFrames_[index].clear();
    lastSnapshotSerials_[index] = 0u;
}

void RidgebreakServerRuntime::retireAcknowledgedSnapshots() noexcept {
    for (uint32_t index = 0u; index < peers_.size(); ++index) {
        const Peer& peer = peers_[index];
        if (!peer.active || peer.pendingSnapshotSequence == 0u) continue;
        const auto view = authority_.peerView(index + 1u);
        if (view.has_value()
            && view->latestAcknowledgedSnapshotSequence
                >= peer.pendingSnapshotSequence) {
            clearPendingSnapshot(index);
        }
    }
}

bool RidgebreakServerRuntime::sendPendingSnapshot(
    uint32_t index, bool retransmission,
    RidgebreakServerTickResult* result) noexcept {
    Peer& peer = peers_[index];
    if (!peer.active || peer.pendingSnapshotSequence == 0u
        || lastSnapshotFrames_[index].empty()
        || lastSnapshotSerials_[index] != peer.connectionSerial) {
        return false;
    }
    peer.lastSnapshotAttemptTick = authority_.tick();
    ++peer.snapshotSendAttempts;
    if (!transport_->sendLatestRealtime(
            index + 1u, peer.connectionSerial,
            lastSnapshotFrames_[index])) {
        ++telemetry_.snapshotSendFailures;
        return false;
    }
    if (result != nullptr) ++result->snapshotsSent;
    ++telemetry_.snapshotsSent;
    telemetry_.snapshotBytesSent += lastSnapshotFrames_[index].size();
    if (retransmission) ++telemetry_.snapshotRetransmissionsSent;
    return true;
}

bool RidgebreakServerRuntime::processLifecycle(
    const network::MultiplayerTransportFrame& frame) noexcept {
    if (!frame.bytes.empty() || frame.connectionSerial == 0u
        || frame.peerId == 0u
        || frame.peerId > kRidgebreakAuthorityPlayerCount) {
        fail(RidgebreakServerFault::InvalidLifecycle);
        return false;
    }
    Peer& peer = peers_[frame.peerId - 1u];
    if (frame.type
            == network::MultiplayerTransportFrameType::Connected
        || frame.type
            == network::MultiplayerTransportFrameType::ConnectionRequested) {
        const bool admissionRequested = frame.type
            == network::MultiplayerTransportFrameType::ConnectionRequested;
        if (peer.active
            && peer.connectionSerial == frame.connectionSerial) {
            ++telemetry_.staleLifecycleFrames;
            if (admissionRequested) {
                transport_->rejectConnection(
                    frame.peerId, frame.connectionSerial);
            }
            return false;
        }
        if (peer.lastConnectionGeneration
                == std::numeric_limits<uint32_t>::max()) {
            ++telemetry_.registrationRejections;
            if (admissionRequested) {
                transport_->rejectConnection(
                    frame.peerId, frame.connectionSerial);
            }
            return false;
        }
        const uint32_t generation = peer.lastConnectionGeneration + 1u;
        if (!authority_.registerClient(
                frame.peerId, frame.peerId,
                frame.connectionSerial, generation)) {
            lastRegistrationError_ = authority_.lastRegistrationError();
            ++telemetry_.registrationRejections;
            if (admissionRequested) {
                transport_->rejectConnection(
                    frame.peerId, frame.connectionSerial);
            }
            return false;
        }
        if (admissionRequested
            && !transport_->acceptConnection(
                frame.peerId, frame.connectionSerial)) {
            fail(RidgebreakServerFault::AdmissionCommitFailed);
            return false;
        }
        peer.connectionSerial = frame.connectionSerial;
        peer.lastConnectionGeneration = generation;
        peer.active = true;
        clearPendingSnapshot(frame.peerId - 1u);
        ++telemetry_.lifecycleFrames;
        return true;
    }
    if (frame.type
        == network::MultiplayerTransportFrameType::Disconnected) {
        if (!peer.active || peer.connectionSerial != frame.connectionSerial) {
            ++telemetry_.staleLifecycleFrames;
            return false;
        }
        if (!authority_.disconnectClient(frame.peerId)) {
            fail(RidgebreakServerFault::InvalidLifecycle);
            return false;
        }
        peer.connectionSerial = 0u;
        peer.active = false;
        clearPendingSnapshot(frame.peerId - 1u);
        ++telemetry_.lifecycleFrames;
        return true;
    }
    fail(RidgebreakServerFault::InvalidLifecycle);
    return false;
}

} // namespace voxy::server
