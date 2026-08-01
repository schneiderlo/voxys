#include "generated/wreckwater_build_content.hpp"
#include "gpu/context.hpp"
#include "network/native_tcp_transport.hpp"
#include "physics/physics_world.hpp"
#include "server/wreckwater_authority_runtime.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <charconv>
#include <chrono>
#include <csignal>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <limits>
#include <memory>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace {

volatile std::sig_atomic_t gStopRequested = 0;

extern "C" void requestStop(int) {
    gStopRequested = 1;
}

struct Options {
    std::string bindAddress = "127.0.0.1";
    uint16_t port = 7777u;
    std::array<
        voxy::network::NativeTcpAuthenticationKey,
        voxy::server::kWreckwaterAuthorityPeerCount> keys{};
    std::array<bool, voxy::server::kWreckwaterAuthorityPeerCount>
        keyPresent{};
    std::string replayOutput;
    uint64_t maximumTicks = 0u;
    bool help = false;
};

void printUsage(const char* executable) {
    std::cerr
        << "Usage: " << executable
        << " [--bind ADDRESS] [--port PORT]"
        << " --key1 HEX64 --key2 HEX64 --key3 HEX64 --key4 HEX64"
        << " [--max-ticks COUNT] [--replay-output PATH]\n"
        << "  --max-ticks 0 runs until SIGINT or SIGTERM.\n";
}

[[nodiscard]] int hexDigit(char value) noexcept {
    if (value >= '0' && value <= '9') return value - '0';
    if (value >= 'a' && value <= 'f') return value - 'a' + 10;
    if (value >= 'A' && value <= 'F') return value - 'A' + 10;
    return -1;
}

[[nodiscard]] bool parseKey(
    std::string_view text,
    voxy::network::NativeTcpAuthenticationKey& key) noexcept {
    if (text.size()
        != voxy::network::kNativeTcpAuthenticationKeyBytes * 2u) {
        return false;
    }
    for (size_t index = 0u; index < key.size(); ++index) {
        const int high = hexDigit(text[index * 2u]);
        const int low = hexDigit(text[index * 2u + 1u]);
        if (high < 0 || low < 0) return false;
        key[index] = std::byte{
            static_cast<uint8_t>((high << 4) | low)};
    }
    return true;
}

template <typename Integer>
[[nodiscard]] bool parseInteger(
    std::string_view text, Integer& value) noexcept {
    Integer parsed = 0;
    const auto result = std::from_chars(
        text.data(), text.data() + text.size(), parsed);
    if (result.ec != std::errc{}
        || result.ptr != text.data() + text.size()) {
        return false;
    }
    value = parsed;
    return true;
}

[[nodiscard]] bool parseOptions(
    int argc, char** argv, Options& options) {
    for (int index = 1; index < argc; ++index) {
        const std::string_view argument(argv[index]);
        if (argument == "--help" || argument == "-h") {
            options.help = true;
            return true;
        }
        if (index + 1 >= argc) return false;
        const std::string_view value(argv[++index]);
        if (argument == "--bind") {
            if (value.empty()) return false;
            options.bindAddress = value;
            continue;
        }
        if (argument == "--port") {
            uint32_t port = 0u;
            if (!parseInteger(value, port)
                || port > std::numeric_limits<uint16_t>::max()) {
                return false;
            }
            options.port = static_cast<uint16_t>(port);
            continue;
        }
        if (argument == "--max-ticks") {
            if (!parseInteger(value, options.maximumTicks)) {
                return false;
            }
            continue;
        }
        if (argument == "--replay-output") {
            if (value.empty()) return false;
            options.replayOutput = value;
            continue;
        }
        if (argument.size() == 6u
            && argument.substr(0u, 5u) == "--key"
            && argument[5] >= '1' && argument[5] <= '4') {
            const size_t keyIndex =
                static_cast<size_t>(argument[5] - '1');
            if (options.keyPresent[keyIndex]
                || !parseKey(value, options.keys[keyIndex])) {
                return false;
            }
            options.keyPresent[keyIndex] = true;
            continue;
        }
        return false;
    }
    return std::all_of(
        options.keyPresent.begin(), options.keyPresent.end(),
        [](bool present) { return present; });
}

class TickTimings {
public:
    void record(double milliseconds) noexcept {
        samples_[write_] = milliseconds;
        write_ = (write_ + 1u) % samples_.size();
        count_ = std::min(count_ + 1u, samples_.size());
        maximum_ = std::max(maximum_, milliseconds);
    }

    void print() const {
        std::vector<double> ordered;
        ordered.reserve(count_);
        for (size_t index = 0u; index < count_; ++index) {
            ordered.push_back(samples_[index]);
        }
        std::sort(ordered.begin(), ordered.end());
        const auto percentile = [&ordered](double fraction) {
            if (ordered.empty()) return 0.0;
            const size_t index = static_cast<size_t>(
                fraction
                * static_cast<double>(ordered.size() - 1u));
            return ordered[index];
        };
        std::cerr
            << "server_tick_ms p50=" << percentile(0.50)
            << " p95=" << percentile(0.95)
            << " max=" << maximum_
            << " samples=" << count_ << '\n';
    }

private:
    std::array<double, 8'192u> samples_{};
    size_t write_ = 0u;
    size_t count_ = 0u;
    double maximum_ = 0.0;
};

void printRuntimeDiagnostic(
    const voxy::server::WreckwaterAuthorityRuntime& runtime) {
    const auto& telemetry = runtime.telemetry();
    const auto& matchTelemetry = runtime.match().telemetry();
    const auto& apply =
        runtime.liveWorld().lastApplyDiagnostic();
    std::cerr
        << "WRECKWATER_SERVER_RUNTIME"
        << " tick=" << runtime.match().currentTick()
        << " fail_reason="
        << voxy::server::wreckwaterAuthorityFailStopReasonName(
            runtime.failStopReason())
        << " service_calls=" << telemetry.serviceCalls
        << " frames_polled=" << telemetry.framesPolled
        << " lifecycle_frames=" << telemetry.lifecycleFrames
        << " rejected_frames=" << telemetry.rejectedFrames
        << " character_packets="
        << telemetry.characterInputPacketsAccepted
        << " character_samples="
        << telemetry.acceptedCharacterInputs
        << " character_replayed="
        << telemetry.replayedCharacterInputs
        << " character_superseded="
        << telemetry.supersededCharacterInputs
        << " character_expired="
        << telemetry.expiredCharacterInputs
        << " character_rejected="
        << telemetry.rejectedCharacterInputs
        << " match_fault="
        << static_cast<uint32_t>(runtime.match().fault())
        << " transaction_aborts="
        << matchTelemetry.transactionAborts
        << " tow_count=" << runtime.liveWorld().towCount()
        << " live_apply_failure="
        << voxy::game::wreckwaterLiveApplyFailureName(
            apply.failure)
        << " intent_index=" << apply.intentIndex
        << " intent_count=" << apply.intentCount
        << " application_tick=" << apply.applicationTick
        << " evidence_tick=" << apply.physicsEvidenceTick
        << " prepare_status="
        << static_cast<uint32_t>(apply.preparedStatus)
        << " prepare_target=" << apply.preparedTargetTick
        << " prepare_created=" << apply.preparedCreatedCount
        << " requested_destroy="
        << apply.requestedDestroyCount
        << " requested_create=" << apply.requestedCreateCount
        << " commit_status="
        << static_cast<uint32_t>(apply.committedStatus)
        << " commit_target=" << apply.committedTargetTick
        << " commit_body=" << apply.committedBodyCommandCount
        << " commit_destroy=" << apply.committedDestroyCount
        << " commit_create=" << apply.committedCreateCount
        << " replay_records="
        << runtime.replay().summary().recordCount
        << " replay_payload_bytes="
        << runtime.replay().summary().payloadBytes
        << " replay_finalized="
        << (runtime.replay().finalized() ? 1u : 0u)
        << " replay_hash="
        << runtime.replay().summary().replayHash
        << " replay_error="
        << voxy::game::wreckwaterReplayErrorName(
            runtime.lastReplayError());
    for (uint32_t peerId = 1u;
         peerId <= voxy::server::kWreckwaterAuthorityPeerCount;
         ++peerId) {
        const auto peer = runtime.peer(peerId);
        if (!peer.has_value()) continue;
        std::cerr
            << " peer" << peerId << "_active="
            << (peer->active ? 1u : 0u)
            << " peer" << peerId << "_serial="
            << peer->connectionSerial
            << " peer" << peerId << "_generation="
            << peer->connectionGeneration
            << " peer" << peerId << "_sequence="
            << peer->latestInboundSequence;
    }
    std::cerr << '\n';
}

} // namespace

int main(int argc, char** argv) {
    Options options;
    if (!parseOptions(argc, argv, options)) {
        printUsage(argv[0]);
        return 1;
    }
    if (options.help) {
        printUsage(argv[0]);
        return 0;
    }

    std::cerr
        << "WARNING: wreckwater_server uses authenticated PLAINTEXT TCP. "
        << "Bind only to loopback or a trusted private network. "
        << "Internet deployment requires TLS/mTLS or QUIC.\n";

    voxy::gpu::Context context;
    voxy::gpu::ContextConfig contextConfig;
    contextConfig.enableValidation = true;
    if (!context.initHeadless(contextConfig)) {
        std::cerr << "Failed to initialize headless WebGPU.\n";
        return 2;
    }

    std::atomic<bool> gpuError = false;
    context.setErrorCallback(
        [&gpuError](WGPUErrorType type, const char* message) {
            std::cerr << "WebGPU error type="
                      << voxy::gpu::errorTypeToString(type)
                      << " message="
                      << (message != nullptr ? message : "") << '\n';
            gpuError.store(true, std::memory_order_relaxed);
        });
    context.setDeviceLostCallback(
        [&gpuError](WGPUDeviceLostReason, const char* message) {
            std::cerr << "WebGPU device lost: "
                      << (message != nullptr ? message : "") << '\n';
            gpuError.store(true, std::memory_order_relaxed);
        });

    voxy::physics::PhysicsWorld world;
    voxy::physics::PhysicsInitContext physicsConfig;
    physicsConfig.requestedBackend =
        voxy::physics::BackendType::WebGpuSoft;
    physicsConfig.allowCpuFallback = false;
    physicsConfig.device = context.getDevice();
    physicsConfig.queue = context.getQueue();
    // Three primary bodies plus at most eight significant fragments.
    // Eleven live bodies have at most 55 unordered body pairs.
    physicsConfig.maxBodies =
        voxy::server::kWreckwaterAuthorityMaximumResidentBodyCount;
    physicsConfig.maxActiveBodies =
        voxy::server::kWreckwaterAuthorityMaximumResidentBodyCount;
    physicsConfig.maxPairs =
        voxy::server::kWreckwaterAuthorityGpuPairCapacity;
    physicsConfig.maxCandidatePairs = 256u;
    physicsConfig.maxContacts = 256u;
    // WebGpuSoft stores one body-body manifold per unique broad-phase pair.
    // Terrain is not a resident body and uses the separate static-contact
    // stage. This server does not attach terrain yet.
    physicsConfig.maxManifolds =
        voxy::server::kWreckwaterAuthorityGpuPairCapacity;
    physicsConfig.enableValidation = true;
    physicsConfig.gpu.maximumCatchUpTicks = 1u;
    physicsConfig.gpu.commandCapacity = 256u;
    physicsConfig.gpu.attachmentCapacity = 16u;
    physicsConfig.gpu.attachmentCommandCapacity = 64u;
    // Readback capacity must exceed the runtime's accepted lag windows.
    // Otherwise a transient callback stall exhausts the ring before the
    // authority can apply its explicit gap/timeout policy.
    physicsConfig.gpu.debugReadbackSlots =
        voxy::server::kWreckwaterAuthorityReadbackRingSlots;
    physicsConfig.gpu.debugReadbackBodyCapacity =
        voxy::server::kWreckwaterAuthorityPhysicalBodyCount;
    physicsConfig.gpu.eventReadbackSlots =
        voxy::server::kWreckwaterAuthorityReadbackRingSlots;
    physicsConfig.gpu.asyncQueryCapacity = 1u;
    physicsConfig.gpu.asyncQueryReadbackSlots = 1u;
    // Fixed authority ticks carry body commands every frame. GPU telemetry
    // treats mutations as forced samples, so leaving this enabled would
    // consume the readback ring even though this headless slice does not poll
    // that optional diagnostic stream.
    physicsConfig.gpu.enableTelemetryReadback = false;
    // Authority WGSL is generated into the same immutable artifact as the
    // advertised 256-bit content identity. A nonempty bundle is strict, so
    // WebGpuSoft cannot reopen mutable deployment files or silently fall back.
    physicsConfig.gpu.shaderSources =
        voxy::build_content::kWreckwaterAuthorityShaderSources;
    if (!world.initialize(physicsConfig)
        || world.backendType()
            != voxy::physics::BackendType::WebGpuSoft) {
        std::cerr
            << "Failed to initialize WebGpuSoft physics; "
            << "CPU fallback is disabled.\n";
        context.shutdown();
        return 2;
    }

    voxy::network::NativeTcpServerConfig transportConfig;
    transportConfig.bindAddress = options.bindAddress;
    transportConfig.port = options.port;
    transportConfig.expectedContentDigest =
        voxy::build_content::
            kWreckwaterAuthorityContentDigest;
    transportConfig.limits.maximumPeers =
        voxy::server::kWreckwaterAuthorityPeerCount;
    transportConfig.limits.maximumPendingConnections = 8u;
    transportConfig.limits.maximumFrameBytes =
        voxy::network::kConservativeRealtimeMtu;
    transportConfig.limits.maximumQueuedFramesPerPeer = 64u;
    transportConfig.limits.maximumQueuedInboundFramesPerPeer = 64u;
    transportConfig.limits.maximumInboundFrames = 256u;
    transportConfig.limits.maximumLifecycleEvents = 32u;
    for (uint32_t peer = 1u;
         peer <= voxy::server::kWreckwaterAuthorityPeerCount; ++peer) {
        transportConfig.peers.push_back({
            .peerId = peer,
            .authenticationKey = options.keys[peer - 1u],
        });
    }
    std::string transportError;
    auto transport =
        voxy::network::NativeTcpServerTransport::create(
            std::move(transportConfig), &transportError);
    if (transport == nullptr) {
        std::cerr << "Failed to create TCP server: "
                  << transportError << '\n';
        world.shutdown();
        context.shutdown();
        return 2;
    }
    const uint16_t listeningPort = transport->listeningPort();

    voxy::server::RealWreckwaterAuthorityPhysics authorityPhysics(
        context, world);
    voxy::server::WreckwaterAuthorityRuntime::Config runtimeConfig;
    runtimeConfig.sessionId = 1u;
    runtimeConfig.matchId = 1u;
    runtimeConfig.worldId = 1u;
    runtimeConfig.worldEpoch = 1u;
    runtimeConfig.authorityEpoch = 1u;
    runtimeConfig.worldEventStreamId = 1u;
    // Proof movement targets certified evidence + 24 ticks. With 5..10
    // service quanta in each direction, arrival is 4..14 ticks ahead of the
    // live character frontier. Sixteen admits that bounded path while the
    // character bridge retains its existing 16-tick exact-input window.
    runtimeConfig.maximumRequestedTickLead = 16u;
    // Replay schema 2 retains the generated SHA-256 prefix. Native TCP uses
    // all 32 bytes for compatibility admission.
    runtimeConfig.replay.contentHash =
        voxy::build_content::
            kWreckwaterAuthorityReplayContentHash;
    // The exact-tick rings have 32 slots. A 16-tick callback-latency budget
    // keeps a full ring of safety without accepting a missing or reordered
    // evidence tick.
    runtimeConfig.maximumEventReadbackLagTicks =
        voxy::server::
            kWreckwaterAuthorityMaximumEventReadbackLagTicks;
    runtimeConfig.liveWorld.maximumInteractionDistance = 24.0f;
    runtimeConfig.liveWorld.extractionRadius = 24.0f;
    runtimeConfig.liveWorld.extractionCenters[0] = {
        .sector = {0, 0, 0}, .local = {-12.0f, 0.0f, 0.0f}};
    runtimeConfig.liveWorld.extractionCenters[1] = {
        .sector = {0, 0, 0}, .local = {12.0f, 0.0f, 0.0f}};
    // The spawn anchors are about 18 m apart. Matching that rest length
    // avoids an artificial first-frame shock while retaining a finite,
    // exceptionally high break threshold for genuine runaway constraints.
    runtimeConfig.liveWorld.towTargetLength = 18.0f;
    runtimeConfig.liveWorld.towBreakForce = 10'000'000.0f;
    voxy::server::WreckwaterAuthorityRuntime runtime(
        runtimeConfig, std::move(transport), authorityPhysics);
    if (!runtime.initialize()) {
        std::cerr
            << "Authority initialization failed reason="
            << voxy::server::wreckwaterAuthorityFailStopReasonName(
                runtime.failStopReason())
            << " tick=" << runtime.failStopTick() << '\n';
        world.shutdown();
        context.shutdown();
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
        << "wreckwater_server listening=" << options.bindAddress
        << ':' << listeningPort
        << " peers=1..4 tick_hz=60"
        << " max_ticks=" << options.maximumTicks << '\n';

    TickTimings timings;
    constexpr auto tickPeriod =
        std::chrono::nanoseconds(1'000'000'000ll / 60ll);
    auto deadline = std::chrono::steady_clock::now();
    int exitCode = 0;
    while (gStopRequested == 0) {
        const auto begin = std::chrono::steady_clock::now();
        if (gpuError.load(std::memory_order_relaxed)) {
            runtime.requestFailStop(
                voxy::server::WreckwaterAuthorityFailStopReason::
                    GpuDeviceError);
        }
        const voxy::server::WreckwaterAuthorityTickResult tick =
            runtime.tick();
        const auto end = std::chrono::steady_clock::now();
        timings.record(
            std::chrono::duration<double, std::milli>(
                end - begin).count());

        if (tick.failStopped) {
            std::cerr
                << "AUTHORITY FAIL-STOP reason="
                << voxy::server::
                    wreckwaterAuthorityFailStopReasonName(
                        runtime.failStopReason())
                << " tick=" << runtime.failStopTick()
                << " physics_status="
                << voxy::server::
                    wreckwaterAuthorityPhysicsStepStatusName(
                        runtime.lastPhysicsStepStatus())
                << '\n';
            printRuntimeDiagnostic(runtime);
            exitCode = 3;
            break;
        }
        if (options.maximumTicks != 0u
            && runtime.match().currentTick()
                >= options.maximumTicks) {
            break;
        }
        if (runtime.replay().finalized()) break;

        deadline += tickPeriod;
        const auto now = std::chrono::steady_clock::now();
        if (deadline + tickPeriod * 4 < now) deadline = now;
        std::this_thread::sleep_until(deadline);
    }

    runtime.close();
    context.tick();
    timings.print();
    if (!runtime.failStopped()) {
        printRuntimeDiagnostic(runtime);
    }
    const voxy::network::WreckwaterCertifiedSnapshot finalSnapshot =
        runtime.lastPublishedSnapshot().value_or(
            voxy::network::WreckwaterCertifiedSnapshot{});
    std::cerr
        << "WRECKWATER_SERVER_FINAL"
        << " tick=" << runtime.match().currentTick()
        << " sequence=" << finalSnapshot.snapshotSequence
        << " application=" << finalSnapshot.applicationTick
        << " evidence=" << finalSnapshot.physicsEvidenceTick
        << " phase=" << static_cast<uint32_t>(finalSnapshot.phase)
        << " outcome=" << static_cast<uint32_t>(finalSnapshot.outcome)
        << " winner=" << static_cast<uint32_t>(finalSnapshot.winner)
        << " score1=" << finalSnapshot.crewOneScore
        << " score2=" << finalSnapshot.crewTwoScore
        << " state_hash=" << finalSnapshot.matchStateHash
        << " event_hash=" << finalSnapshot.eventStreamHash
        << " serialized_hash=" << finalSnapshot.serializedByteHash
        << '\n';
    if (!options.replayOutput.empty()) {
        if (!runtime.replay().finalized()) {
            std::cerr
                << "Replay output requested, but no terminal "
                << "certified state was recorded.\n";
            if (exitCode == 0) exitCode = 4;
        } else {
            const voxy::game::WreckwaterReplayWriteResult replay =
                voxy::game::WreckwaterReplayCodec::encode(
                    runtime.replay());
            const bool sizeFits =
                replay.bytes.size()
                <= static_cast<size_t>(
                    std::numeric_limits<std::streamsize>::max());
            std::ofstream output(
                options.replayOutput,
                std::ios::binary | std::ios::trunc);
            if (!replay || !sizeFits || !output
                || !(output.write(
                        reinterpret_cast<const char*>(
                            replay.bytes.data()),
                        static_cast<std::streamsize>(
                            replay.bytes.size()))
                    .flush())) {
                std::cerr
                    << "Failed to write replay output path="
                    << options.replayOutput
                    << " error="
                    << voxy::game::wreckwaterReplayErrorName(
                        replay.error)
                    << '\n';
                if (exitCode == 0) exitCode = 4;
            } else {
                std::cerr
                    << "WRECKWATER_REPLAY_WRITTEN path="
                    << options.replayOutput
                    << " bytes=" << replay.bytes.size()
                    << " archive_hash=" << replay.archiveHash
                    << " replay_hash="
                    << runtime.replay().summary().replayHash
                    << '\n';
            }
        }
    }
    world.shutdown();
    context.shutdown();
    return exitCode;
}
