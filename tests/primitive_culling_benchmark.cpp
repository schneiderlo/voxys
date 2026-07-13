#include <gtest/gtest.h>

#include "render/primitive_culling.hpp"
#include "render/primitive_instance_packing.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <span>
#include <string_view>
#include <vector>

namespace voxy::render {
namespace {

using Clock = std::chrono::steady_clock;
using Snapshot = physics::PhysicsWorld::DynamicBodySnapshot;
using Shape = physics::PhysicsWorld::ThrowableShape;

constexpr size_t kIterations = 1000;

struct Summary {
    double p50;
    double p95;
    double p99;
    double callsPerSecond;
};

Summary summarize(std::vector<double> samples) {
    std::sort(samples.begin(), samples.end());
    const auto value = [&](double fraction) {
        const size_t rank = static_cast<size_t>(std::ceil(
            fraction * static_cast<double>(samples.size())));
        return samples[std::min(std::max<size_t>(rank, 1) - 1,
                                samples.size() - 1)];
    };
    double totalMs = 0.0;
    for (double sample : samples) totalMs += sample;
    return {value(0.50), value(0.95), value(0.99),
            static_cast<double>(samples.size()) * 1000.0 / totalMs};
}

uint64_t hashInstances(std::span<const detail::GpuInstance> instances) {
    uint64_t hash = 14695981039346656037ull;
    for (std::byte value : std::as_bytes(instances)) {
        hash ^= std::to_integer<uint8_t>(value);
        hash *= 1099511628211ull;
    }
    return hash;
}

std::vector<Snapshot> makeBodies(size_t count, bool mostlyOutside) {
    std::vector<Snapshot> bodies;
    bodies.reserve(count);
    for (size_t index = 0; index < count; ++index) {
        const bool outside = mostlyOutside && index >= count / 10;
        bodies.push_back({
            static_cast<Shape>(index % static_cast<size_t>(Shape::Count)),
            glm::vec3(outside ? 10'000.0f + static_cast<float>(index % 257)
                              : -0.8f + static_cast<float>(index % 17) * 0.1f,
                      -0.8f + static_cast<float>(index % 13) * 0.1f,
                      0.2f + static_cast<float>(index % 7) * 0.1f),
            glm::quat(1.0f, 0.0f, 0.0f, 0.0f), glm::vec3(0.05f), false});
    }
    return bodies;
}

Summary run(size_t count, bool mostlyOutside, bool candidate,
            size_t& submittedCount) {
    const auto source = makeBodies(count, mostlyOutside);
    PrimitiveCullController controller;
    uint64_t consumed = 0;
    for (size_t warmup = 0; warmup < 40; ++warmup) {
        auto frame = source;
        if (candidate) {
            static_cast<void>(controller.cull(frame, glm::mat4(1.0f)));
        }
        auto batch = detail::packPrimitiveInstances(frame);
        consumed += batch.instances.size();
    }

    std::vector<double> samples;
    samples.reserve(kIterations);
    for (size_t iteration = 0; iteration < kIterations; ++iteration) {
        auto frame = source;
        const auto start = Clock::now();
        if (candidate) {
            static_cast<void>(controller.cull(frame, glm::mat4(1.0f)));
        }
        auto batch = detail::packPrimitiveInstances(frame);
        const auto end = Clock::now();
        submittedCount = batch.instances.size();
        consumed += batch.instances[(iteration * 8191u) % batch.instances.size()]
                        .color.w > 0.0f;
        samples.push_back(std::chrono::duration<double, std::milli>(
            end - start).count());
    }
    EXPECT_NE(consumed, 0u);
    return summarize(std::move(samples));
}

void benchmarkCase(size_t count, bool mostlyOutside) {
    auto oracleBodies = makeBodies(count, mostlyOutside);
    if (mostlyOutside) {
        cullPrimitiveSnapshotsInPlace(
            oracleBodies, Frustum::fromViewProj(glm::mat4(1.0f)));
    }
    const auto oracleBatch = detail::packPrimitiveInstances(oracleBodies);
    const uint64_t goldenHash = hashInstances(oracleBatch.instances);
    const uint64_t expectedHash = count == 10'000
        ? (mostlyOutside ? 0x26b6eed549094363ull : 0x5b542d148fe4de30ull)
        : (mostlyOutside ? 0x1645140332b7db3dull : 0x74a0b8962f677c6eull);
    EXPECT_EQ(goldenHash, expectedHash);

    size_t baselineSubmitted = 0;
    size_t candidateSubmitted = 0;
    const Summary baseline = run(
        count, mostlyOutside, false, baselineSubmitted);
    const Summary candidate = run(
        count, mostlyOutside, true, candidateSubmitted);

    if (mostlyOutside) {
        EXPECT_EQ(candidateSubmitted, count / 10);
        // The production bound also handles non-unit quaternions and floating
        // clip-edge error, so its CPU-only guard is intentionally conservative.
        EXPECT_LE(candidate.p50, baseline.p50 * 0.75);
    } else {
        EXPECT_EQ(candidateSubmitted, count);
        EXPECT_LE(candidate.p50, baseline.p50 * 1.10);
    }
    std::cout << std::fixed << std::setprecision(6)
              << "primitive_cull bodies=" << count
              << " scene=" << (mostlyOutside ? "outside90" : "visible")
              << " baseline_p50_ms=" << baseline.p50
              << " baseline_p95_ms=" << baseline.p95
              << " baseline_p99_ms=" << baseline.p99
              << " candidate_p50_ms=" << candidate.p50
              << " candidate_p95_ms=" << candidate.p95
              << " candidate_p99_ms=" << candidate.p99
              << " candidate_calls_per_s=" << candidate.callsPerSecond
              << " submitted=" << candidateSubmitted
              << " fnv64=0x" << std::hex << goldenHash << std::dec << '\n';
}

TEST(PrimitiveCullingBenchmark, LargeBodyCounts) {
    for (size_t count : {size_t{10'000}, size_t{16'384}}) {
        benchmarkCase(count, true);
        benchmarkCase(count, false);
    }
}

} // namespace
} // namespace voxy::render
