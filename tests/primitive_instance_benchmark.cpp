#include <gtest/gtest.h>

#include "render/primitive_instance_packing.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <span>
#include <utility>
#include <vector>

namespace voxy::render::detail {
namespace {

using Clock = std::chrono::steady_clock;
using Snapshot = physics::PhysicsWorld::DynamicBodySnapshot;
using Shape = physics::PhysicsWorld::ThrowableShape;

constexpr size_t kWarmupIterations = 100;
constexpr size_t kIterations = 1'000;
constexpr uint64_t kFnvOffset = 14695981039346656037ull;
constexpr uint64_t kFnvPrime = 1099511628211ull;

std::vector<Snapshot> makeBodies(size_t count, bool active) {
    constexpr std::array<glm::quat, 4> rotations = {
        glm::quat(1.0f, 0.0f, 0.0f, 0.0f),
        glm::quat(0.9238795f, 0.0f, 0.38268343f, 0.0f),
        glm::quat(0.70710677f, 0.70710677f, 0.0f, 0.0f),
        glm::quat(0.5f, 0.5f, 0.5f, 0.5f),
    };
    std::vector<Snapshot> bodies;
    bodies.reserve(count);
    for (size_t i = 0; i < count; ++i) {
        const auto index = static_cast<uint32_t>(i);
        bodies.push_back({
            static_cast<Shape>(index % static_cast<uint32_t>(Shape::Count)),
            glm::vec3(static_cast<float>(index % 97) * 0.25f,
                      static_cast<float>(index % 31) * -0.5f,
                      static_cast<float>(index % 53) * 0.125f),
            rotations[index % rotations.size()],
            glm::vec3(0.5f + static_cast<float>(index % 7) * 0.125f,
                      0.75f + static_cast<float>(index % 5) * 0.25f,
                      1.0f + static_cast<float>(index % 3) * 0.5f),
            active,
        });
    }
    return bodies;
}

uint64_t hashInstances(std::span<const GpuInstance> instances) {
    uint64_t hash = kFnvOffset;
    for (std::byte value : std::as_bytes(instances)) {
        hash ^= std::to_integer<uint8_t>(value);
        hash *= kFnvPrime;
    }
    return hash;
}

struct SampleSummary {
    double p50Ms = 0.0;
    double p95Ms = 0.0;
    double p99Ms = 0.0;
    double callsPerSecond = 0.0;
    double millionInstancesPerSecond = 0.0;
};

template <bool UseCache>
SampleSummary benchmark(size_t bodyCount, bool active, uint64_t& outputHash,
                        size_t& cacheBytes) {
    const auto bodies = makeBodies(bodyCount, active);
    PrimitiveInstanceCache cache;
    {
        const auto oracle = [&] {
            if constexpr (UseCache) {
                static_cast<void>(packPrimitiveInstances(bodies, cache));
                return packPrimitiveInstances(bodies, cache);
            }
            return packPrimitiveInstances(bodies);
        }();
        outputHash = hashInstances(oracle.instances);
    }

    uint64_t consumed = 0;
    for (size_t iteration = 0; iteration < kWarmupIterations; ++iteration) {
        const auto batch = [&] {
            if constexpr (UseCache) {
                return packPrimitiveInstances(bodies, cache);
            }
            return packPrimitiveInstances(bodies);
        }();
        consumed += std::bit_cast<uint32_t>(batch.instances[iteration % bodyCount]
                                                .model[iteration % 4][iteration % 4]);
    }
    cacheBytes = cache.entries.capacity() * sizeof(CachedPrimitiveInstance);

    std::vector<double> samples;
    samples.reserve(kIterations);
    const auto totalStart = Clock::now();
    for (size_t iteration = 0; iteration < kIterations; ++iteration) {
        const auto start = Clock::now();
        const auto batch = [&] {
            if constexpr (UseCache) {
                return packPrimitiveInstances(bodies, cache);
            }
            return packPrimitiveInstances(bodies);
        }();
        const auto end = Clock::now();
        samples.push_back(std::chrono::duration<double, std::milli>(end - start).count());
        consumed += std::bit_cast<uint32_t>(batch.instances[iteration % bodyCount]
                                                .model[iteration % 4][iteration % 4]);
    }
    const double elapsedSeconds =
        std::chrono::duration<double>(Clock::now() - totalStart).count();
    EXPECT_NE(consumed, 0u);

    std::sort(samples.begin(), samples.end());
    auto percentile = [&](double fraction) {
        const size_t index = static_cast<size_t>(
            fraction * static_cast<double>(samples.size()));
        return samples[std::min(index, samples.size() - 1)];
    };
    return {
        percentile(0.50), percentile(0.95), percentile(0.99),
        static_cast<double>(kIterations) / elapsedSeconds,
        static_cast<double>(kIterations * bodyCount) / elapsedSeconds / 1.0e6,
    };
}

TEST(PrimitiveInstanceBenchmark, ReportsPackingAtLargeBodyCounts) {
    constexpr std::array cases = {
        std::pair{size_t{10'000}, uint64_t{0x6575d6a6dd063659ull}},
        std::pair{size_t{16'384}, uint64_t{0x53019b8cfeabd641ull}},
    };
    for (const auto [bodyCount, expectedHash] : cases) {
        uint64_t baselineHash = 0;
        size_t baselineCacheBytes = 0;
        const auto baseline = benchmark<false>(
            bodyCount, false, baselineHash, baselineCacheBytes);
        EXPECT_EQ(baselineHash, expectedHash);
        std::cout << "primitive_pack workload=uncached"
                  << " bodies=" << bodyCount
                  << " iterations=" << kIterations
                  << " p50_ms=" << baseline.p50Ms
                  << " p95_ms=" << baseline.p95Ms
                  << " p99_ms=" << baseline.p99Ms
                  << " calls_per_s=" << baseline.callsPerSecond
                  << " million_instances_per_s="
                  << baseline.millionInstancesPerSecond
                  << " output_bytes=" << bodyCount * sizeof(GpuInstance)
                  << " cache_bytes=" << baselineCacheBytes
                  << " fnv64=0x" << std::hex << baselineHash << std::dec << '\n';

        for (bool active : {false, true}) {
            uint64_t hash = 0;
            size_t cacheBytes = 0;
            const auto result = benchmark<true>(
                bodyCount, active, hash, cacheBytes);
            EXPECT_EQ(hash, expectedHash);
            if (active) {
                EXPECT_LE(result.p50Ms, baseline.p50Ms * 1.15);
            } else {
                EXPECT_LE(result.p50Ms, baseline.p50Ms * 0.65);
            }
            std::cout << "primitive_pack workload="
                      << (active ? "active" : "sleeping")
                      << " bodies=" << bodyCount
                      << " iterations=" << kIterations
                      << " p50_ms=" << result.p50Ms
                      << " p95_ms=" << result.p95Ms
                      << " p99_ms=" << result.p99Ms
                      << " calls_per_s=" << result.callsPerSecond
                      << " million_instances_per_s="
                      << result.millionInstancesPerSecond
                      << " output_bytes=" << bodyCount * sizeof(GpuInstance)
                      << " cache_bytes=" << cacheBytes
                      << " fnv64=0x" << std::hex << hash << std::dec << '\n';
        }
    }
}

} // namespace
} // namespace voxy::render::detail
