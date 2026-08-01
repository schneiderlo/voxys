#include "client/wreckwater_client_probe.hpp"

#include "generated/wreckwater_build_content.hpp"
#include "network/deterministic_adversity_transport.hpp"
#include "network/native_tcp_transport.hpp"
#include "network/wreckwater_client_runtime.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <csignal>
#include <cstdint>
#include <iostream>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace {

volatile std::sig_atomic_t gStopRequested = 0;

extern "C" void requestStop(int) {
    gStopRequested = 1;
}

void printUsage(const char* executable) {
    std::cerr
        << "Usage: " << executable
        << " [--server ADDRESS] [--port PORT]"
        << " --peer 1..4 --key HEX64"
        << " [--session ID] [--match ID] [--world ID]"
        << " [--world-epoch EPOCH] [--authority-epoch EPOCH]"
        << " [--max-ticks COUNT] [--reconnect-at PUMP_TICK]"
        << " [--chaos-seed SEED]\n"
        << "  max-ticks is a bounded 60 Hz pump timeout.\n"
        << "  reconnect-at 0 disables the deliberate reconnect.\n"
        << "  chaos-seed 0 disables deterministic app-frame adversity.\n";
}

struct CreatedTransport {
    std::unique_ptr<voxy::network::IMultiplayerTransport> transport;
    voxy::network::DeterministicAdversityTransport* adversity =
        nullptr;
};

[[nodiscard]] CreatedTransport
createTransport(
    const voxy::client::WreckwaterClientProbeOptions& options,
    std::string& error) {
    voxy::network::NativeTcpClientConfig config;
    config.serverAddress = options.serverAddress;
    config.serverPort = options.serverPort;
    config.credential = {
        .peerId = options.peerId,
        .authenticationKey = options.authenticationKey,
    };
    config.expectedContentDigest =
        voxy::build_content::
            kWreckwaterAuthorityContentDigest;
    config.limits.maximumFrameBytes =
        voxy::network::kConservativeRealtimeMtu;
    config.limits.maximumQueuedFramesPerPeer = 64u;
    config.limits.maximumQueuedBytesPerPeer =
        64u * voxy::network::kConservativeRealtimeMtu;
    config.limits.maximumQueuedInboundFramesPerPeer = 64u;
    config.limits.maximumQueuedInboundBytesPerPeer =
        64u * voxy::network::kConservativeRealtimeMtu;
    config.limits.maximumInboundFrames = 64u;
    config.limits.maximumInboundBytes =
        64u * voxy::network::kConservativeRealtimeMtu;
    config.limits.maximumLifecycleEvents = 8u;
    auto native =
        voxy::network::NativeTcpClientTransport::create(
            std::move(config), &error);
    if (native == nullptr) return {};
    std::unique_ptr<voxy::network::IMultiplayerTransport> transport =
        std::move(native);
    if (options.chaosSeed == 0u) {
        return {
            .transport = std::move(transport),
            .adversity = nullptr,
        };
    }

    voxy::network::DeterministicAdversityTransportConfig
        adversityConfig;
    adversityConfig.seed = options.chaosSeed;
    adversityConfig.maximumPeers = 1u;
    adversityConfig.maximumQueuedDataFramesPerPeer = 128u;
    adversityConfig.maximumQueuedLifecycleFramesPerPeer = 4u;
    adversityConfig.maximumFrameBytes =
        voxy::network::kConservativeRealtimeMtu;
    adversityConfig.maximumUnderlyingFramesPerService = 64u;
    adversityConfig.maximumOutgoingFramesPerService = 64u;
    adversityConfig.preserveLastRealtimeBeforeDisconnect = true;
    adversityConfig.incoming.minimumDelayServiceQuanta =
        voxy::client::
            kWreckwaterProbeChaosMinimumDelayServiceQuanta;
    adversityConfig.incoming.maximumDelayServiceQuanta =
        voxy::client::
            kWreckwaterProbeChaosMaximumDelayServiceQuanta;
    adversityConfig.incoming.realtimeDropPermille = 100u;
    // Snapshot delivery stays ordered. The WRECKWATER client rejects an
    // out-of-order authoritative sequence instead of masking it.
    adversityConfig.incoming.realtimeDuplicatePermille = 0u;
    adversityConfig.incoming.reliableDuplicatePermille = 0u;
    adversityConfig.incoming.reorderPermille = 0u;
    adversityConfig.outgoing.minimumDelayServiceQuanta =
        voxy::client::
            kWreckwaterProbeChaosMinimumDelayServiceQuanta;
    adversityConfig.outgoing.maximumDelayServiceQuanta =
        voxy::client::
            kWreckwaterProbeChaosMaximumDelayServiceQuanta;
    adversityConfig.outgoing.realtimeDropPermille = 100u;
    adversityConfig.outgoing.realtimeDuplicatePermille = 100u;
    adversityConfig.outgoing.reliableDuplicatePermille = 1'000u;
    adversityConfig.outgoing.reorderPermille = 1'000u;
    auto adversity =
        voxy::network::DeterministicAdversityTransport::create(
            std::move(transport), adversityConfig, &error);
    if (adversity == nullptr) return {};
    voxy::network::DeterministicAdversityTransport* raw =
        adversity.get();
    return {
        .transport = std::move(adversity),
        .adversity = raw,
    };
}

void logAdversityStats(
    uint32_t peerId, uint64_t seed,
    const voxy::network::
        DeterministicAdversityTransportTelemetry& value) {
    std::cerr
        << "WRECKWATER_CLIENT_CHAOS"
        << " peer=" << peerId
        << " seed=" << seed
        << " services=" << value.serviceCalls
        << " inbound_drop=" << value.inboundRealtimeDrops
        << " terminal_recovery="
        << value.inboundRealtimeDisconnectRecoveries
        << " outbound_drop=" << value.outboundRealtimeDrops
        << " outbound_duplicates="
        << value.outboundDuplicatesQueued
        << " reliable_duplicates="
        << value.outboundReliableDuplicatesQueued
        << " outbound_reorders=" << value.outboundReorders
        << " reliable_reorders="
        << value.outboundReliableReorders
        << " stale_purged=" << value.staleFramesPurged
        << " reliable_retries=" << value.reliableSendRetries
        << " inbound_high_water="
        << value.inboundDataHighWater
        << " outbound_high_water="
        << value.outboundDataHighWater
        << " sequence_exhaustions="
        << value.scheduleSequenceExhaustions
        << " faults=" << value.faults
        << '\n';
}

[[nodiscard]] const char* cargoDispositionName(
    voxy::network::WreckwaterCargoDisposition disposition) noexcept {
    switch (disposition) {
        case voxy::network::WreckwaterCargoDisposition::NotApplicable:
            return "not_applicable";
        case voxy::network::WreckwaterCargoDisposition::Free:
            return "free";
        case voxy::network::WreckwaterCargoDisposition::Towed:
            return "towed";
        case voxy::network::WreckwaterCargoDisposition::Banked:
            return "banked";
        case voxy::network::WreckwaterCargoDisposition::Lost:
            return "lost";
    }
    return "unknown";
}

[[nodiscard]] const voxy::network::WreckwaterCharacterState*
characterForPlayer(
    const voxy::network::WreckwaterClientSample& sample,
    uint64_t playerId) noexcept {
    for (uint32_t index = 0u;
         index < sample.characterCount; ++index) {
        const auto& character =
            sample.characters[index].authoritativeState;
        if (character.playerId == playerId) return &character;
    }
    return nullptr;
}

class CharacterProofEvidence {
public:
    explicit CharacterProofEvidence(uint32_t peerId) noexcept
        : peerId_(peerId) {}

    [[nodiscard]] bool observe(
        const voxy::network::WreckwaterClientSample& sample)
        noexcept {
        const uint64_t rosterHash =
            voxy::client::
                wreckwaterClientProbeCharacterRosterHash(sample);
        const auto* local = characterForPlayer(sample, peerId_);
        if (rosterHash == 0u || local == nullptr) return false;
        const bool aboard =
            (local->stateFlags & 0x3u)
            == static_cast<uint32_t>(
                voxy::network::WreckwaterCharacterMode::OnSkiff);
        if (handle_ != 0u
            && local->characterHandle != handle_) {
            return false;
        }
        if (generation_ != 0u
            && local->connectionGeneration != generation_
            && (generation_
                    == std::numeric_limits<uint32_t>::max()
                || local->connectionGeneration
                    != generation_ + 1u)) {
            return false;
        }
        if (handle_ == 0u) {
            if (!aboard || local->skiffId == 0u
                || local->skiffGeneration == 0u) {
                return false;
            }
            handle_ = local->characterHandle;
            skiffId_ = local->skiffId;
            skiffGeneration_ = local->skiffGeneration;
            baselineX_ = local->skiffLocalFeetPosition.x;
            baselineY_ = local->skiffLocalFeetPosition.y;
            baselineZ_ = local->skiffLocalFeetPosition.z;
        } else if (aboard
            && (local->skiffId != skiffId_
                || local->skiffGeneration != skiffGeneration_)) {
            return false;
        }
        if (local->connectionGeneration != generation_) {
            generation_ = local->connectionGeneration;
            latestAck_ = 0u;
            highestSentSequence_ = 0u;
            sentCurrentGeneration_ = 0u;
        } else if (
            local->lastAppliedCharacterInputSequence
            < latestAck_) {
            return false;
        }
        const uint64_t ack =
            local->lastAppliedCharacterInputSequence;
        if (ack != 0u
            && (highestSentSequence_ == 0u
                || ack > highestSentSequence_)) {
            return false;
        }
        latestAck_ = ack;
        if (generation_ >= 2u && ack != 0u) {
            postReconnectAck_ = ack;
        }
        // A mode transition clears skiff-local fields. Counting that clear
        // would produce a false movement certificate. Measure only while the
        // authoritative character is still linked to the pinned skiff.
        if (aboard) {
            const double dx =
                static_cast<double>(
                    local->skiffLocalFeetPosition.x)
                - static_cast<double>(baselineX_);
            const double dy =
                static_cast<double>(
                    local->skiffLocalFeetPosition.y)
                - static_cast<double>(baselineY_);
            const double dz =
                static_cast<double>(
                    local->skiffLocalFeetPosition.z)
                - static_cast<double>(baselineZ_);
            maximumFeetDisplacementSquared_ = std::max(
                maximumFeetDisplacementSquared_,
                dx * dx + dy * dy + dz * dz);
        }
        rosterHash_ = rosterHash;
        rosterCount_ = sample.characterCount;
        finalConnected_ =
            (local->stateFlags
             & voxy::network::
                 kWreckwaterCharacterStateConnectedFlag)
            != 0u;
        observed_ = true;
        return true;
    }

    [[nodiscard]] bool recordSent(
        uint32_t handle, uint32_t generation,
        uint64_t characterInputSequence) noexcept {
        if (!observed_ || handle != handle_
            || generation != generation_
            || characterInputSequence == 0u
            || characterInputSequence
                <= highestSentSequence_) {
            return false;
        }
        highestSentSequence_ = characterInputSequence;
        if (sentCount_ != std::numeric_limits<uint64_t>::max()) {
            ++sentCount_;
        }
        if (sentCurrentGeneration_
            != std::numeric_limits<uint64_t>::max()) {
            ++sentCurrentGeneration_;
        }
        if (generation >= 2u
            && postReconnectSent_
                != std::numeric_limits<uint64_t>::max()) {
            ++postReconnectSent_;
        }
        return true;
    }

    [[nodiscard]] uint64_t certifiedFeetMillimetres()
        const noexcept {
        const double millimetres =
            std::sqrt(maximumFeetDisplacementSquared_) * 1'000.0;
        if (!std::isfinite(millimetres)
            || millimetres <= 0.0) {
            return 0u;
        }
        if (millimetres
            >= static_cast<double>(
                std::numeric_limits<uint64_t>::max())) {
            return std::numeric_limits<uint64_t>::max();
        }
        return static_cast<uint64_t>(
            std::llround(millimetres));
    }

    [[nodiscard]] bool complete(
        uint32_t expectedGeneration,
        bool requireReconnect) const noexcept {
        return observed_
            && rosterCount_
                == voxy::client::kWreckwaterProbePeerCount
            && rosterHash_ != 0u
            && handle_ != 0u
            && generation_ == expectedGeneration
            && finalConnected_
            && sentCount_ != 0u
            && sentCurrentGeneration_ != 0u
            && latestAck_ != 0u
            && latestAck_ <= highestSentSequence_
            && certifiedFeetMillimetres()
                >= voxy::client::
                    kWreckwaterProbeMinimumCertifiedDeckDisplacementMillimetres
            && (!requireReconnect
                || (postReconnectSent_ != 0u
                    && postReconnectAck_ != 0u));
    }

    [[nodiscard]] uint32_t rosterCount() const noexcept {
        return rosterCount_;
    }
    [[nodiscard]] uint64_t rosterHash() const noexcept {
        return rosterHash_;
    }
    [[nodiscard]] uint32_t handle() const noexcept {
        return handle_;
    }
    [[nodiscard]] uint32_t generation() const noexcept {
        return generation_;
    }
    [[nodiscard]] uint64_t sentCount() const noexcept {
        return sentCount_;
    }
    [[nodiscard]] uint64_t sentCurrentGeneration() const noexcept {
        return sentCurrentGeneration_;
    }
    [[nodiscard]] uint64_t latestAck() const noexcept {
        return latestAck_;
    }
    [[nodiscard]] uint64_t postReconnectSent() const noexcept {
        return postReconnectSent_;
    }
    [[nodiscard]] uint64_t postReconnectAck() const noexcept {
        return postReconnectAck_;
    }

private:
    uint32_t peerId_ = 0u;
    uint32_t rosterCount_ = 0u;
    uint32_t handle_ = 0u;
    uint32_t generation_ = 0u;
    uint32_t skiffId_ = 0u;
    uint32_t skiffGeneration_ = 0u;
    uint64_t rosterHash_ = 0u;
    uint64_t sentCount_ = 0u;
    uint64_t sentCurrentGeneration_ = 0u;
    uint64_t highestSentSequence_ = 0u;
    uint64_t latestAck_ = 0u;
    uint64_t postReconnectSent_ = 0u;
    uint64_t postReconnectAck_ = 0u;
    double maximumFeetDisplacementSquared_ = 0.0;
    float baselineX_ = 0.0f;
    float baselineY_ = 0.0f;
    float baselineZ_ = 0.0f;
    bool finalConnected_ = false;
    bool observed_ = false;
};

void logSnapshot(
    uint32_t peerId,
    const voxy::network::WreckwaterClientSample& sample,
    const voxy::client::WreckwaterClientProbeFinalSnapshot& value) {
    const voxy::network::WreckwaterCargoLogicalState* cargo = nullptr;
    for (uint32_t index = 0u; index < sample.entityCount; ++index) {
        const voxy::network::WreckwaterEntityState& entity =
            sample.entities[index].authoritativeState;
        if (entity.kind
                == voxy::network::WreckwaterEntityKind::Cargo
            && entity.cargo.cargoId != 0u) {
            cargo = &entity.cargo;
            break;
        }
    }
    std::cerr
        << "WRECKWATER_CLIENT_SNAPSHOT"
        << " peer=" << peerId
        << " sequence=" << value.sequence
        << " application=" << value.applicationTick
        << " evidence=" << value.evidenceTick
        << " phase="
        << voxy::client::wreckwaterProbePhaseName(value.phase)
        << " score1=" << value.crewOneScore
        << " score2=" << value.crewTwoScore
        << " cargo_revision="
        << (cargo != nullptr ? cargo->revision : 0u)
        << " cargo_state="
        << (cargo != nullptr
                ? cargoDispositionName(cargo->disposition)
                : "missing")
        << " cargo_owner="
        << (cargo != nullptr
                ? voxy::client::wreckwaterProbeCrewName(
                    cargo->ownerCrew)
                : "none")
        << " state_hash=" << value.stateHash
        << " event_hash=" << value.eventHash
        << " serialized_hash=" << value.serializedHash
        << '\n';
}

void logFinal(
    uint32_t peerId,
    const voxy::client::WreckwaterClientProbeFinalSnapshot& value) {
    std::cerr
        << "WRECKWATER_CLIENT_FINAL"
        << " peer=" << peerId
        << " sequence=" << value.sequence
        << " application=" << value.applicationTick
        << " evidence=" << value.evidenceTick
        << " phase="
        << voxy::client::wreckwaterProbePhaseName(value.phase)
        << " outcome="
        << voxy::client::wreckwaterProbeOutcomeName(value.outcome)
        << " winner="
        << voxy::client::wreckwaterProbeCrewName(value.winner)
        << " score1=" << value.crewOneScore
        << " score2=" << value.crewTwoScore
        << " state_hash=" << value.stateHash
        << " event_hash=" << value.eventHash
        << " serialized_hash=" << value.serializedHash
        << '\n';
}

void logCharacterFinal(
    uint32_t peerId, const CharacterProofEvidence& evidence,
    bool oldGenerationBlocked,
    const voxy::network::WreckwaterClientRuntimeTelemetry&
        telemetry) {
    std::cerr
        << "WRECKWATER_CLIENT_CHARACTER_FINAL"
        << " peer=" << peerId
        << " roster=" << evidence.rosterCount()
        << " roster_hash=" << evidence.rosterHash()
        << " handle=" << evidence.handle()
        << " stable=1"
        << " generation=" << evidence.generation()
        << " sent=" << evidence.sentCount()
        << " generation_sent="
        << evidence.sentCurrentGeneration()
        << " ack=" << evidence.latestAck()
        << " certified_feet_mm="
        << evidence.certifiedFeetMillimetres()
        << " minimum_deck_displacement_mm="
        << voxy::client::
            kWreckwaterProbeMinimumCertifiedDeckDisplacementMillimetres
        << " post_reconnect_sent="
        << evidence.postReconnectSent()
        << " post_reconnect_ack="
        << evidence.postReconnectAck()
        << " old_generation_blocked="
        << (oldGenerationBlocked ? 1u : 0u)
        << " target_lead="
        << voxy::client::
            kWreckwaterProbeCharacterTargetLeadTicks
        << " redundancy=fixed_window"
        << " redundancy_window="
        << voxy::network::
            kWreckwaterCharacterInputMaximumRedundantSamples
        << " retransmitted="
        << telemetry.characterInputSamplesRetransmitted
        << " acknowledged="
        << telemetry.characterInputSamplesAcknowledged
        << " retired="
        << telemetry.characterInputSamplesRetiredUnacknowledged
        << " history_purges="
        << telemetry.characterInputHistoryPurges
        << " max_redundancy="
        << telemetry.maximumCharacterInputRedundancy
        << '\n';
}

} // namespace

int main(int argc, char** argv) {
    std::vector<std::string_view> arguments;
    arguments.reserve(argc > 1 ? static_cast<size_t>(argc - 1) : 0u);
    for (int index = 1; index < argc; ++index) {
        arguments.emplace_back(argv[index]);
    }

    voxy::client::WreckwaterClientProbeOptions options;
    const voxy::client::WreckwaterClientProbeOptionResult parsed =
        voxy::client::parseWreckwaterClientProbeOptions(
            arguments, options);
    if (!parsed) {
        std::cerr
            << "Invalid client probe arguments: "
            << voxy::client::
                wreckwaterClientProbeOptionErrorName(parsed.error)
            << '\n';
        printUsage(argv[0]);
        return 1;
    }
    if (options.help) {
        printUsage(argv[0]);
        return 0;
    }

    std::cerr
        << "WARNING: wreckwater_client_probe uses authenticated "
        << "PLAINTEXT TCP for a loopback/private-network proof. "
        << "It is not a public-Internet security boundary.\n";

    std::string transportError;
    CreatedTransport created =
        createTransport(options, transportError);
    if (created.transport == nullptr) {
        std::cerr
            << "Client transport creation failed peer="
            << options.peerId << " reason=" << transportError
            << '\n';
        return 2;
    }
    voxy::network::DeterministicAdversityTransport*
        activeAdversity = created.adversity;
    voxy::network::DeterministicAdversityTransportTelemetry
        adversityTelemetry;
    const auto captureAdversity = [&]() {
        if (activeAdversity == nullptr) return;
        voxy::network::
            mergeDeterministicAdversityTransportTelemetry(
                adversityTelemetry,
                activeAdversity->telemetry());
        activeAdversity = nullptr;
    };

    voxy::network::WreckwaterClientRuntime::Config runtimeConfig;
    runtimeConfig.serverPeerId = 0u;
    runtimeConfig.serverConnectionSerial = 0u;
    runtimeConfig.sessionId = options.sessionId;
    runtimeConfig.matchId = options.matchId;
    runtimeConfig.worldId = options.worldId;
    runtimeConfig.worldEpoch = options.worldEpoch;
    runtimeConfig.authorityEpoch = options.authorityEpoch;
    runtimeConfig.localPlayerId = options.peerId;
    runtimeConfig.maximumFramesPerPump = 64u;
    runtimeConfig.firstClientRequestSequence = 1u;
    runtimeConfig.snapshotBuffer.historySnapshots = 32u;
    runtimeConfig.snapshotBuffer.maximumEntities = 3u;
    voxy::network::WreckwaterClientRuntime runtime(
        runtimeConfig, std::move(created.transport));
    voxy::client::WreckwaterClientProbeScript script(options.peerId);
    voxy::client::WreckwaterClientProbeCharacterScript
        characterScript(options.peerId);
    CharacterProofEvidence characterEvidence(options.peerId);
    if (!runtime.initialized() || !script.initialized()
        || !characterScript.initialized()) {
        std::cerr
            << "Client probe initialization failed peer="
            << options.peerId << '\n';
        return 2;
    }

    std::signal(SIGINT, requestStop);
    std::signal(SIGTERM, requestStop);
    std::cerr
        << "WRECKWATER_BUILD_CONTENT"
        << " content_digest="
        << voxy::build_content::
            kWreckwaterAuthorityContentDigestHex
        << " replay_prefix="
        << voxy::build_content::
            kWreckwaterAuthorityReplayContentHash
        << '\n';
    std::cerr
        << "wreckwater_client_probe server="
        << options.serverAddress << ':' << options.serverPort
        << " peer=" << options.peerId
        << " role="
        << (voxy::client::wreckwaterProbeIsHelmPeer(options.peerId)
                ? "helm" : "deck")
        << " tick_hz=60"
        << " max_ticks=" << options.maximumPumpTicks
        << " reconnect_at=" << options.reconnectAtPumpTick
        << " chaos_seed=" << options.chaosSeed
        << " character_target_lead="
        << voxy::client::
            kWreckwaterProbeCharacterTargetLeadTicks
        << " character_redundancy=fixed_window_3"
        << '\n';

    constexpr auto tickPeriod =
        std::chrono::nanoseconds(1'000'000'000ll / 60ll);
    auto deadline = std::chrono::steady_clock::now();
    std::optional<
        voxy::client::WreckwaterClientProbeFinalSnapshot> finalSnapshot;
    std::optional<voxy::network::WreckwaterClientSample> finalSample;
    voxy::client::WreckwaterClientProbeSnapshotLogGate
        snapshotLogGate;
    uint64_t acceptedSnapshotCount = 0u;
    uint64_t loggedSnapshotCount = 0u;
    uint64_t reconnectOldSerial = 0u;
    uint64_t reconnectExpectedSequence = 0u;
    uint32_t reconnectExpectedHistory = 0u;
    uint32_t consecutiveActionSendFailures = 0u;
    uint32_t consecutiveCharacterSendFailures = 0u;
    bool everConnected = false;
    bool reconnectWaiting = false;
    bool reconnectCompleted = false;
    bool oldGenerationBlocked = false;
    bool sessionCompleted = false;

    for (uint64_t pumpTick = 1u;
         pumpTick <= options.maximumPumpTicks
         && gStopRequested == 0;
         ++pumpTick) {
        const bool activeBefore = runtime.connectionActive();
        const uint64_t serialBefore =
            runtime.serverConnectionSerial();
        const voxy::network::WreckwaterClientPumpResult pumped =
            runtime.pump();
        if (activeAdversity != nullptr
            && activeAdversity->faulted()) {
            std::cerr
                << "Client adversity fault peer="
                << options.peerId
                << " pump_tick=" << pumpTick
                << " error="
                << voxy::network::
                    deterministicAdversityTransportErrorName(
                        activeAdversity->lastError())
                << '\n';
            return 4;
        }
        if (!pumped) {
            std::cerr
                << "Client runtime fault peer=" << options.peerId
                << " pump_tick=" << pumpTick
                << " error="
                << voxy::network::
                    wreckwaterClientRuntimeErrorName(
                        pumped.lastError)
                << " snapshot_error="
                << voxy::network::wreckwaterCodecErrorName(
                    pumped.lastSnapshotCodecError)
                << " replication_error="
                << voxy::network::
                    wreckwaterClientReplicationErrorName(
                        pumped.lastReplicationError)
                << '\n';
            return 4;
        }

        const bool activeAfter = runtime.connectionActive();
        const uint64_t serialAfter =
            runtime.serverConnectionSerial();
        if (activeAfter
            && (!activeBefore || serialAfter != serialBefore)) {
            if (serialAfter == 0u) {
                std::cerr
                    << "Client connected without a serial peer="
                    << options.peerId << '\n';
                return 4;
            }
            if (reconnectWaiting) {
                if (serialAfter <= reconnectOldSerial
                    || runtime.nextClientRequestSequence()
                        != reconnectExpectedSequence
                    || runtime.snapshotBuffer().size()
                        < reconnectExpectedHistory) {
                    std::cerr
                        << "Reconnect state continuity failed peer="
                        << options.peerId << '\n';
                    return 5;
                }
                reconnectWaiting = false;
                reconnectCompleted = true;
                std::cerr
                    << "WRECKWATER_CLIENT_RECONNECTED"
                    << " peer=" << options.peerId
                    << " serial=" << serialAfter
                    << " next_sequence="
                    << runtime.nextClientRequestSequence()
                    << " history="
                    << runtime.snapshotBuffer().size()
                    << '\n';
            } else {
                std::cerr
                    << "WRECKWATER_CLIENT_CONNECTED"
                    << " peer=" << options.peerId
                    << " serial=" << serialAfter << '\n';
            }
            everConnected = true;
        }

        if (pumped.snapshotsAccepted != 0u) {
            const uint64_t accepted =
                pumped.snapshotsAccepted;
            acceptedSnapshotCount =
                acceptedSnapshotCount
                    > std::numeric_limits<uint64_t>::max()
                        - accepted
                ? std::numeric_limits<uint64_t>::max()
                : acceptedSnapshotCount + accepted;
            const voxy::network::WreckwaterClientSampleResult latest =
                runtime.latestSample();
            if (!latest) {
                std::cerr
                    << "Latest snapshot sampling failed peer="
                    << options.peerId << " error="
                    << voxy::network::
                        wreckwaterClientReplicationErrorName(
                            latest.error)
                    << '\n';
                return 4;
            }
            finalSnapshot =
                voxy::client::wreckwaterClientProbeFinalSnapshot(
                    latest.sample);
            finalSample = latest.sample;
            if (!characterEvidence.observe(latest.sample)) {
                std::cerr
                    << "Certified character roster evidence failed peer="
                    << options.peerId
                    << " snapshot="
                    << latest.sample.authoritative.snapshotSequence
                    << '\n';
                return 5;
            }
            if (snapshotLogGate.shouldLog(latest.sample)) {
                logSnapshot(
                    options.peerId, latest.sample, *finalSnapshot);
                ++loggedSnapshotCount;
            }
        }

        const bool remoteDisconnected =
            activeBefore && !activeAfter;
        if (remoteDisconnected) {
            if (!finalSnapshot.has_value()) {
                std::cerr
                    << "Server disconnected before any snapshot peer="
                    << options.peerId << '\n';
                return 6;
            }
            sessionCompleted = true;
            break;
        }

        if (options.reconnectAtPumpTick != 0u
            && pumpTick == options.reconnectAtPumpTick) {
            if (!runtime.connectionActive()
                || reconnectWaiting || reconnectCompleted) {
                std::cerr
                    << "Scheduled reconnect has no active connection peer="
                    << options.peerId
                    << " pump_tick=" << pumpTick << '\n';
                return 5;
            }
            reconnectOldSerial =
                runtime.serverConnectionSerial();
            reconnectExpectedSequence =
                runtime.nextClientRequestSequence();
            reconnectExpectedHistory =
                runtime.snapshotBuffer().size();
            characterScript.discardPending();
            if (!runtime.disconnectForTransportReplacement()) {
                std::cerr
                    << "Local transport disconnect failed peer="
                    << options.peerId << '\n';
                return 5;
            }
            captureAdversity();
            transportError.clear();
            CreatedTransport replacement =
                createTransport(options, transportError);
            voxy::network::DeterministicAdversityTransport*
                replacementAdversity = replacement.adversity;
            if (replacement.transport == nullptr
                || !runtime.replaceTransport(
                    std::move(replacement.transport))) {
                std::cerr
                    << "Transport replacement failed peer="
                    << options.peerId
                    << " reason=" << transportError << '\n';
                return 5;
            }
            activeAdversity = replacementAdversity;
            if (runtime.nextClientRequestSequence()
                    != reconnectExpectedSequence
                || runtime.snapshotBuffer().size()
                    != reconnectExpectedHistory) {
                std::cerr
                    << "Transport replacement mutated client state peer="
                    << options.peerId << '\n';
                return 5;
            }
            const auto blocked =
                runtime.sendLocalCharacterInput(
                    finalSnapshot->evidenceTick,
                    voxy::client::
                        kWreckwaterProbeCharacterMoveQ15,
                    0, false, false);
            if (blocked.error
                    != voxy::network::
                        WreckwaterClientRuntimeError::
                            CharacterIdentityUnavailable
                || runtime.localCharacterBindingState()
                    != voxy::network::
                        WreckwaterLocalCharacterBindingState::
                            AwaitingAuthoritativeGeneration
                || runtime.localCharacterHandle() != 0u
                || runtime.localCharacterConnectionGeneration()
                    != 0u
                || runtime.nextCharacterInputSequence() != 0u
                || runtime.nextClientRequestSequence()
                    != reconnectExpectedSequence) {
                std::cerr
                    << "Old-generation character history unlocked input peer="
                    << options.peerId << '\n';
                return 5;
            }
            oldGenerationBlocked = true;
            reconnectWaiting = true;
            std::cerr
                << "WRECKWATER_CLIENT_RECONNECTING"
                << " peer=" << options.peerId
                << " old_serial=" << reconnectOldSerial
                << " next_sequence=" << reconnectExpectedSequence
                << " history=" << reconnectExpectedHistory
                << '\n';
        }

        if (runtime.connectionActive()
            && finalSnapshot.has_value()) {
            const voxy::network::WreckwaterClientSampleResult latest =
                runtime.latestSample();
            if (!latest) return 4;
            const std::optional<
                voxy::client::WreckwaterClientProbeAction> action =
                script.plan(latest.sample);
            if (action.has_value()) {
                const voxy::network::
                    WreckwaterClientActionSendResult sent =
                    action->action
                        == voxy::network::WreckwaterAction::Helm
                    ? runtime.sendHelm(
                        action->requestedApplicationTick,
                        action->helmThrottleQ15,
                        action->helmSteeringQ15)
                    : runtime.sendCargoAction(
                        action->requestedApplicationTick,
                        action->action, action->cargoId,
                        action->cargoGeneration,
                        action->observedCargoRevision);
                if (!sent) {
                    if (sent.error
                        != voxy::network::
                            WreckwaterClientRuntimeError::
                                TransportSendFailed) {
                        std::cerr
                            << "Client action failed peer="
                            << options.peerId
                            << " action="
                            << voxy::client::
                                wreckwaterProbeActionName(
                                    action->action)
                            << " error="
                            << voxy::network::
                                wreckwaterClientRuntimeErrorName(
                                    sent.error)
                            << '\n';
                        return 5;
                    }
                    ++consecutiveActionSendFailures;
                    if (consecutiveActionSendFailures > 120u) {
                        std::cerr
                            << "Client action backpressure timeout peer="
                            << options.peerId << '\n';
                        return 5;
                    }
                } else {
                    if (!script.markSent(*action)) {
                        std::cerr
                            << "Client script commit failed peer="
                            << options.peerId << '\n';
                        return 5;
                    }
                    consecutiveActionSendFailures = 0u;
                    if (action->action
                            != voxy::network::WreckwaterAction::Helm
                        || sent.clientRequestSequence % 60u == 0u) {
                        std::cerr
                            << "WRECKWATER_CLIENT_ACTION"
                            << " peer=" << options.peerId
                            << " action="
                            << voxy::client::
                                wreckwaterProbeActionName(
                                    action->action)
                            << " request_tick="
                            << action->requestedApplicationTick
                            << " request_sequence="
                            << sent.clientRequestSequence
                            << " snapshot="
                            << action->sourceSnapshotSequence
                            << '\n';
                    }
                }
            }

            if (runtime.localCharacterBindingState()
                == voxy::network::
                    WreckwaterLocalCharacterBindingState::Ready) {
                const std::optional<
                    voxy::client::
                        WreckwaterClientProbeCharacterInput>
                    input = characterScript.plan(latest.sample);
                if (input.has_value()) {
                    const uint32_t handle =
                        runtime.localCharacterHandle();
                    const uint32_t generation =
                        runtime
                            .localCharacterConnectionGeneration();
                    if (handle == 0u || generation == 0u
                        || input->sourceConnectionGeneration
                            != generation) {
                        std::cerr
                            << "Character plan identity drifted peer="
                            << options.peerId << '\n';
                        return 5;
                    }
                    const voxy::network::
                        WreckwaterClientActionSendResult sent =
                        runtime.sendLocalCharacterInput(
                            input->requestedApplicationTick,
                            input->moveXQ15, input->moveZQ15,
                            input->jump, input->board);
                    if (!sent) {
                        if (sent.error
                            != voxy::network::
                                WreckwaterClientRuntimeError::
                                    TransportSendFailed) {
                            std::cerr
                                << "Client character input failed peer="
                                << options.peerId
                                << " error="
                                << voxy::network::
                                    wreckwaterClientRuntimeErrorName(
                                        sent.error)
                                << '\n';
                            return 5;
                        }
                        ++consecutiveCharacterSendFailures;
                        if (consecutiveCharacterSendFailures > 120u) {
                            std::cerr
                                << "Client character backpressure timeout peer="
                                << options.peerId << '\n';
                            return 5;
                        }
                    } else {
                        if (!characterScript.markSent(*input)
                            || !characterEvidence.recordSent(
                                handle, generation,
                                sent.characterInputSequence)) {
                            std::cerr
                                << "Character proof commit failed peer="
                                << options.peerId << '\n';
                            return 5;
                        }
                        consecutiveCharacterSendFailures = 0u;
                        if (sent.characterInputSequence == 1u
                            || sent.characterInputSequence % 60u
                                == 0u) {
                            std::cerr
                                << "WRECKWATER_CLIENT_CHARACTER"
                                << " peer=" << options.peerId
                                << " handle=" << handle
                                << " generation=" << generation
                                << " input_sequence="
                                << sent.characterInputSequence
                                << " request_sequence="
                                << sent.clientRequestSequence
                                << " request_tick="
                                << input
                                    ->requestedApplicationTick
                                << " evidence="
                                << input->sourceEvidenceTick
                                << " snapshot="
                                << input->sourceSnapshotSequence
                                << '\n';
                        }
                    }
                }
            } else if (
                runtime.localCharacterBindingState()
                == voxy::network::
                    WreckwaterLocalCharacterBindingState::Disabled) {
                std::cerr
                    << "Local character binding disabled peer="
                    << options.peerId << '\n';
                return 5;
            }
        }

        deadline += tickPeriod;
        const auto now = std::chrono::steady_clock::now();
        if (deadline + tickPeriod * 4 < now) deadline = now;
        std::this_thread::sleep_until(deadline);
    }

    if (gStopRequested != 0) {
        std::cerr
            << "Client probe interrupted peer=" << options.peerId
            << '\n';
        return 3;
    }
    if (!finalSnapshot.has_value() || !finalSample.has_value()) {
        std::cerr
            << "Client probe observed no snapshots peer="
            << options.peerId << '\n';
        return 6;
    }
    if (!sessionCompleted) {
        std::cerr
            << "Client probe timeout peer=" << options.peerId
            << " max_ticks=" << options.maximumPumpTicks << '\n';
        return 3;
    }
    if (options.reconnectAtPumpTick != 0u
        && !reconnectCompleted) {
        std::cerr
            << "Scheduled reconnect did not complete peer="
            << options.peerId << '\n';
        return 5;
    }
    if (!everConnected) {
        std::cerr
            << "Client probe never connected peer="
            << options.peerId << '\n';
        return 4;
    }
    const bool reconnectRequired =
        options.reconnectAtPumpTick != 0u;
    const uint32_t expectedCharacterGeneration =
        reconnectRequired ? 2u : 1u;
    if (!characterEvidence.complete(
            expectedCharacterGeneration, reconnectRequired)
        || (reconnectRequired && !oldGenerationBlocked)) {
        std::cerr
            << "Client character proof incomplete peer="
            << options.peerId
            << " roster=" << characterEvidence.rosterCount()
            << " generation=" << characterEvidence.generation()
            << " sent=" << characterEvidence.sentCount()
            << " generation_sent="
            << characterEvidence.sentCurrentGeneration()
            << " ack=" << characterEvidence.latestAck()
            << " certified_feet_mm="
            << characterEvidence.certifiedFeetMillimetres()
            << " post_reconnect_sent="
            << characterEvidence.postReconnectSent()
            << " post_reconnect_ack="
            << characterEvidence.postReconnectAck()
            << " old_generation_blocked="
            << (oldGenerationBlocked ? 1u : 0u)
            << '\n';
        return 6;
    }
    const uint64_t finalRosterHash =
        voxy::client::wreckwaterClientProbeCharacterRosterHash(
            *finalSample);
    if (finalRosterHash == 0u
        || finalRosterHash != characterEvidence.rosterHash()) {
        std::cerr
            << "Final character roster hash mismatch peer="
            << options.peerId << '\n';
        return 6;
    }

    const uint64_t skippedSnapshotLogs =
        acceptedSnapshotCount >= loggedSnapshotCount
        ? acceptedSnapshotCount - loggedSnapshotCount : 0u;
    std::cerr
        << "WRECKWATER_CLIENT_SNAPSHOT_STATS"
        << " peer=" << options.peerId
        << " accepted=" << acceptedSnapshotCount
        << " logged=" << loggedSnapshotCount
        << " skipped=" << skippedSnapshotLogs
        << '\n';
    logCharacterFinal(
        options.peerId, characterEvidence,
        oldGenerationBlocked, runtime.telemetry());
    logFinal(options.peerId, *finalSnapshot);
    runtime.close();
    captureAdversity();
    if (options.chaosSeed != 0u) {
        logAdversityStats(
            options.peerId, options.chaosSeed,
            adversityTelemetry);
    }
    return 0;
}
