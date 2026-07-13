#include <gtest/gtest.h>

#include "physics/physics_world.hpp"

#include <algorithm>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <span>
#include <vector>

namespace voxy::physics {
namespace {

constexpr uint32_t kSnapshotIterations = 200;

double observedPercentile(const std::vector<double>& sortedSamples,
                          double percentile) {
    const auto rank = static_cast<size_t>(std::ceil(
        percentile * static_cast<double>(sortedSamples.size())));
    return sortedSamples[std::min(std::max<size_t>(rank, 1u) - 1u,
                                  sortedSamples.size() - 1u)];
}

uint64_t hashSnapshots(std::span<const PhysicsWorld::DynamicBodySnapshot> snapshots) {
    uint64_t hash = 14695981039346656037ull;
    const auto append = [&](uint32_t word) {
        for (uint32_t shift = 0; shift < 32; shift += 8) {
            hash ^= static_cast<uint8_t>(word >> shift);
            hash *= 1099511628211ull;
        }
    };

    for (const auto& snapshot : snapshots) {
        append(static_cast<uint32_t>(snapshot.shape));
        append(std::bit_cast<uint32_t>(snapshot.position.x));
        append(std::bit_cast<uint32_t>(snapshot.position.y));
        append(std::bit_cast<uint32_t>(snapshot.position.z));
        append(std::bit_cast<uint32_t>(snapshot.rotation.x));
        append(std::bit_cast<uint32_t>(snapshot.rotation.y));
        append(std::bit_cast<uint32_t>(snapshot.rotation.z));
        append(std::bit_cast<uint32_t>(snapshot.rotation.w));
        append(std::bit_cast<uint32_t>(snapshot.dimensions.x));
        append(std::bit_cast<uint32_t>(snapshot.dimensions.y));
        append(std::bit_cast<uint32_t>(snapshot.dimensions.z));
    }
    return hash;
}

void runSnapshotBenchmark(uint32_t bodyCount, uint64_t expectedHash) {
    PhysicsWorld world;
    ASSERT_TRUE(world.initialize());

    constexpr uint32_t kColumns = 128;
    constexpr float kSpacing = 3.0f;
    const uint32_t shapeCount =
        static_cast<uint32_t>(PhysicsWorld::ThrowableShape::Count);
    for (uint32_t index = 0; index < bodyCount; ++index) {
        const glm::vec3 position{
            static_cast<float>(index % kColumns) * kSpacing,
            300.0f,
            static_cast<float>(index / kColumns) * kSpacing};
        const auto shape = static_cast<PhysicsWorld::ThrowableShape>(index % shapeCount);
        ASSERT_TRUE(world.throwBody(shape, position, glm::vec3(0.0f)));
    }

    for (uint32_t iteration = 0; iteration < 5; ++iteration) {
        ASSERT_EQ(world.dynamicBodies().size(), bodyCount);
    }

    std::vector<double> samples;
    samples.reserve(kSnapshotIterations);
    uint64_t consumed = 0;
    uint64_t goldenHash = 0;
    for (uint32_t iteration = 0; iteration < kSnapshotIterations; ++iteration) {
        const auto start = std::chrono::steady_clock::now();
        const auto snapshots = world.dynamicBodies();
        const auto end = std::chrono::steady_clock::now();
        ASSERT_EQ(snapshots.size(), bodyCount);

        samples.push_back(std::chrono::duration<double, std::milli>(end - start).count());
        consumed ^= std::bit_cast<uint32_t>(snapshots[iteration % bodyCount].position.x);
        if (iteration == 0) {
            goldenHash = hashSnapshots(snapshots);
        }
    }

    std::sort(samples.begin(), samples.end());
    EXPECT_EQ(goldenHash, expectedHash);
    const double p50 = observedPercentile(samples, 0.50);
    const double p95 = observedPercentile(samples, 0.95);
    const double p99 = observedPercentile(samples, 0.99);
    if (bodyCount == 10'000) {
        EXPECT_GE(1000.0 / p50, 10'000.0);
    }
    std::cout << "[snapshot-benchmark] bodies=" << bodyCount
              << " p50_ms=" << p50
              << " p95_ms=" << p95
              << " p99_ms=" << p99
              << " throughput_per_s=" << 1000.0 / p50
              << " golden_hash=0x" << std::hex << goldenHash
              << " consumed=0x" << consumed << std::dec << '\n';
}

TEST(PhysicsSnapshotBenchmark, TenThousandBodies) {
    runSnapshotBenchmark(10'000, 0x456fe021babb145bull);
}

TEST(PhysicsSnapshotBenchmark, MaximumSixteenThousandBodies) {
    runSnapshotBenchmark(16'384, 0x84741d2358655601ull);
}

} // namespace
} // namespace voxy::physics
