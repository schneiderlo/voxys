#include <gtest/gtest.h>

#include "network/multiplayer_session.hpp"

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <memory>
#include <numeric>
#include <span>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <vector>

namespace voxy::network {
namespace {

uint32_t option(const char* name, uint32_t fallback,
                uint32_t minimum, uint32_t maximum) {
    const char* text = std::getenv(name);
    if (text == nullptr) return fallback;
    const std::string_view value(text);
    uint32_t parsed = 0u;
    const auto result = std::from_chars(
        value.data(), value.data() + value.size(), parsed);
    if (result.ec != std::errc{} || result.ptr != value.data() + value.size()
        || parsed < minimum || parsed > maximum) {
        throw std::invalid_argument(name);
    }
    return parsed;
}

// Isolate the complete session serialization/fan-out path from socket pacing.
// This transport neither allocates nor retains a growing packet queue.
class CountingTransport final : public IMultiplayerTransport {
public:
    bool send(uint32_t, DeliveryClass, std::span<const std::byte> data) override {
        bytes += data.size();
        ++packets;
        return true;
    }
    std::optional<MultiplayerTransportFrame> poll() override {
        return std::nullopt;
    }
    void close() override {}
    uint64_t bytes = 0u;
    uint64_t packets = 0u;
};

TEST(ReplicationBenchmark, TwelvePeerFanoutPreservesGoldenSnapshots) {
    using namespace physics::deterministic;
    constexpr uint32_t bodyCount = 65'536u;
    constexpr uint32_t clientCount = 12u;
    constexpr uint32_t warmupFrames = 20u;
    const uint32_t frames = option("VOXY_REPLICATION_FRAMES", 2000u, 20u, 100'000u);
    const bool dense = option("VOXY_REPLICATION_DENSE", 0u, 0u, 1u) != 0u;
    const uint32_t maximumP95Micros = option(
        "VOXY_REPLICATION_MAX_P95_US", UINT32_MAX, 1u, UINT32_MAX);
    std::vector<LockstepBody> bodies(bodyCount);
    for (uint32_t index = 1u; index < bodyCount; ++index) {
        auto& body = bodies[index];
        body.identity = {index, 1u, LockstepBodyAlive | LockstepBodyAwake, 0u};
        body.sectorRadius = {
            dense ? 0 : static_cast<int32_t>(index % 256u) - 128,
            0,
            dense ? 0 : static_cast<int32_t>(index / 256u) - 128,
            kLockstepPositionOne / 2,
        };
        body.positionInvMass = {
            0, 4 * kLockstepPositionOne, 0, kLockstepVelocityOne};
    }
    MultiplayerSession::ServerConfig config;
    config.identity = {0x1002u, 0x2003u, 4u};
    config.world.bodyCapacity = bodyCount;
    config.world.contactCapacity = 64u;
    config.world.gravityPerSubstepQ16 = 0;
    config.maximumClients = clientCount + 2u;
    config.interest.maximumEntries = bodyCount;
    config.snapshotHistoryTicks = 16u;
    auto transport = std::make_unique<CountingTransport>();
    const auto* counters = transport.get();
    MultiplayerSession session;
    ASSERT_TRUE(session.initializeServer(config, bodies, std::move(transport)));
    for (uint32_t client = 1u; client <= clientCount; ++client) {
        ASSERT_TRUE(session.addClient({
            client, client * (bodyCount / (clientCount + 1u)), 2u, 256u}));
    }
    const auto goldenHash = [&]() {
        uint64_t hash = 0u;
        for (uint32_t client = 1u; client <= clientCount; ++client) {
            const auto snapshot = session.snapshotForClient(client);
            if (!snapshot) throw std::runtime_error("missing snapshot");
            const auto bytes = SnapshotCodec::encode(*snapshot);
            for (const std::byte byte : bytes)
                hash = (hash ^ std::to_integer<uint8_t>(byte)) * 1'099'511'628'211ull;
        }
        return hash;
    };
    // Captured from 6701a85, before the indexed query. The complete encoded
    // snapshots include canonical body order, all body fields, and state hash.
    const uint64_t expectedHash = dense
        ? 0x2bea'129f'589a'aa69ull : 0x5978'e3be'ece3'72f6ull;
    ASSERT_EQ(goldenHash(), expectedHash);
    std::vector<double> samples;
    samples.reserve(frames);
    for (uint32_t frame = 0u; frame < warmupFrames + frames; ++frame) {
        const auto start = std::chrono::steady_clock::now();
        ASSERT_TRUE(session.sendSnapshots());
        if (frame >= warmupFrames) {
            samples.push_back(std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - start).count());
        }
    }
    EXPECT_EQ(goldenHash(), expectedHash);
    EXPECT_EQ(counters->packets, uint64_t{warmupFrames + frames} * clientCount);
    EXPECT_EQ(counters->bytes, counters->packets * (dense ? 16'520u : 200u));
    EXPECT_EQ(session.currentTick(), 0u);
    const double totalMs = std::accumulate(samples.begin(), samples.end(), 0.0);
    std::sort(samples.begin(), samples.end());
    const auto percentile = [&](size_t percent) {
        return samples[(samples.size() * percent + 99u) / 100u - 1u];
    };
    std::cout << std::fixed << std::setprecision(6)
              << "replication bodies=" << bodyCount << " clients=" << clientCount
              << " dense=" << dense << " frames=" << frames
              << " p50_ms=" << percentile(50u)
              << " p95_ms=" << percentile(95u)
              << " p99_ms=" << percentile(99u)
              << " rounds_per_s=" << static_cast<double>(frames) * 1000.0 / totalMs
              << " bytes=" << counters->bytes << " packets=" << counters->packets
              << " oracle=" << std::hex << expectedHash << std::dec << '\n';
    EXPECT_LE(percentile(95u), static_cast<double>(maximumP95Micros) / 1000.0);
}

} // namespace
} // namespace voxy::network
