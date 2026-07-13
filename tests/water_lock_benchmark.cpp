#include <gtest/gtest.h>

#include "physics/physics_world.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <span>
#include <vector>

namespace voxy::physics {
namespace {

constexpr uint32_t kIterations = 80;

double percentile(const std::vector<double>& sortedSamples, double fraction) {
    const size_t rank = static_cast<size_t>(std::ceil(
        fraction * static_cast<double>(sortedSamples.size())));
    return sortedSamples[std::min(std::max<size_t>(rank, 1) - 1,
                                  sortedSamples.size() - 1)];
}

uint64_t hashSnapshots(
    std::span<const PhysicsWorld::DynamicBodySnapshot> snapshots) {
    uint64_t hash = 14695981039346656037ull;
    const auto append = [&](uint32_t word) {
        for (uint32_t shift = 0; shift < 32; shift += 8) {
            hash ^= static_cast<uint8_t>(word >> shift);
            hash *= 1099511628211ull;
        }
    };
    for (const auto& body : snapshots) {
        append(static_cast<uint32_t>(body.shape));
        for (float value : {body.position.x, body.position.y, body.position.z,
                            body.rotation.x, body.rotation.y, body.rotation.z,
                            body.rotation.w, body.dimensions.x,
                            body.dimensions.y, body.dimensions.z}) {
            append(std::bit_cast<uint32_t>(value));
        }
        append(body.active ? 1u : 0u);
    }
    return hash;
}

void populateDryWorld(PhysicsWorld& world, uint32_t bodyCount,
                      bool waterEnabled) {
    ASSERT_TRUE(world.initialize());
    world.setWaterPlane(5.0f, waterEnabled);
    constexpr uint32_t columns = 128;
    constexpr uint32_t shapeCount =
        static_cast<uint32_t>(PhysicsWorld::ThrowableShape::Count);
    for (uint32_t index = 0; index < bodyCount; ++index) {
        ASSERT_TRUE(world.throwBody(
            static_cast<PhysicsWorld::ThrowableShape>(index % shapeCount),
            glm::vec3(static_cast<float>(index % columns) * 3.0f, 300.0f,
                      static_cast<float>(index / columns) * 3.0f),
            glm::vec3(0.0f)));
    }
}

void runWaterOverheadBenchmark(uint32_t bodyCount,
                               double maximumMedianOverheadMs) {
    PhysicsWorld withWater;
    PhysicsWorld withoutWater;
    populateDryWorld(withWater, bodyCount, true);
    populateDryWorld(withoutWater, bodyCount, false);

    for (uint32_t iteration = 0; iteration < 5; ++iteration) {
        withWater.update(1.0f / 60.0f);
        withoutWater.update(1.0f / 60.0f);
    }

    std::vector<double> enabledSamples;
    std::vector<double> disabledSamples;
    enabledSamples.reserve(kIterations);
    disabledSamples.reserve(kIterations);
    for (uint32_t iteration = 0; iteration < kIterations; ++iteration) {
        const bool enabledFirst = (iteration & 1u) == 0;
        const auto measure = [](PhysicsWorld& world) {
            const auto start = std::chrono::steady_clock::now();
            world.update(1.0f / 60.0f);
            return std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - start).count();
        };
        if (enabledFirst) {
            enabledSamples.push_back(measure(withWater));
            disabledSamples.push_back(measure(withoutWater));
        } else {
            disabledSamples.push_back(measure(withoutWater));
            enabledSamples.push_back(measure(withWater));
        }
    }
    std::sort(enabledSamples.begin(), enabledSamples.end());
    std::sort(disabledSamples.begin(), disabledSamples.end());

    const auto enabledBodies = withWater.dynamicBodies();
    const auto disabledBodies = withoutWater.dynamicBodies();
    const uint64_t enabledHash = hashSnapshots(enabledBodies);
    const uint64_t disabledHash = hashSnapshots(disabledBodies);
    ASSERT_EQ(enabledHash, disabledHash);

    const double enabledP50 = percentile(enabledSamples, 0.50);
    const double disabledP50 = percentile(disabledSamples, 0.50);
    const double overhead = enabledP50 - disabledP50;
    EXPECT_LE(overhead, maximumMedianOverheadMs);
    std::cout << std::fixed << std::setprecision(6)
              << "water_lock bodies=" << bodyCount
              << " iterations=" << kIterations
              << " enabled_p50_ms=" << enabledP50
              << " enabled_p95_ms=" << percentile(enabledSamples, 0.95)
              << " enabled_p99_ms=" << percentile(enabledSamples, 0.99)
              << " enabled_throughput_per_s=" << 1000.0 / enabledP50
              << " disabled_p50_ms=" << disabledP50
              << " water_overhead_p50_ms=" << overhead
              << " golden_hash=0x" << std::hex << enabledHash << std::dec
              << '\n';
}

TEST(WaterLockBenchmark, TenThousandDryBodies) {
    runWaterOverheadBenchmark(10'000, 0.30);
}

TEST(WaterLockBenchmark, MaximumDryBodies) {
    runWaterOverheadBenchmark(16'384, 0.50);
}

} // namespace
} // namespace voxy::physics
