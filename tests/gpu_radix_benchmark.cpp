#include <gtest/gtest.h>

#include "gpu/context.hpp"
#include "gpu/resources.hpp"
#include "physics/gpu/deterministic_primitives.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <span>
#include <stdexcept>
#include <string_view>
#include <tuple>
#include <vector>

#ifndef WGPUWrappedSubmissionIndex
using WGPUSubmissionIndex = uint64_t;
struct WGPUWrappedSubmissionIndex {
    WGPUQueue queue;
    WGPUSubmissionIndex submissionIndex;
};
#endif
extern "C" WGPUSubmissionIndex wgpuQueueSubmitForIndex(
    WGPUQueue queue, size_t count, const WGPUCommandBuffer* commands);
extern "C" WGPUBool wgpuDevicePoll(
    WGPUDevice device, WGPUBool wait,
    const WGPUWrappedSubmissionIndex* submission);

namespace voxy::physics {
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

class Buffer {
public:
    explicit Buffer(WGPUBuffer value) : value_(value) {}
    ~Buffer() {
        if (value_) {
            wgpuBufferDestroy(value_);
            wgpuBufferRelease(value_);
        }
    }
    Buffer(const Buffer&) = delete;
    Buffer& operator=(const Buffer&) = delete;
    operator WGPUBuffer() const noexcept { return value_; }
private:
    WGPUBuffer value_;
};

TEST(GpuRadixBenchmark, PreservesStableRecordsWithinLatencyLimit) {
    const uint32_t count = option("VOXY_RADIX_RECORDS", 262'144u, 1u, 1'048'576u);
    const uint32_t capacity = option(
        "VOXY_RADIX_CAPACITY", count, count, 1'048'576u);
    const uint32_t frames = option("VOXY_RADIX_FRAMES", 300u, 20u, 10'000u);
    const uint32_t workgroup = option("VOXY_RADIX_WORKGROUP", 256u, 64u, 256u);
    ASSERT_TRUE(workgroup == 64u || workgroup == 128u || workgroup == 256u);
    const uint32_t maximumP95Micros = option(
        "VOXY_RADIX_MAX_P95_US", UINT32_MAX, 1u, UINT32_MAX);
    const uint32_t maximumScratchBytes = option(
        "VOXY_RADIX_MAX_SCRATCH_BYTES", UINT32_MAX, 1u, UINT32_MAX);
    constexpr uint32_t warmupFrames = 20u;

    gpu::Context context;
    gpu::ContextConfig contextConfig;
    contextConfig.enableValidation = false;
    // A missing adapter is a failed benchmark, never a passing skipped gate.
    ASSERT_TRUE(context.initHeadless(contextConfig));
    std::vector<GpuKeyValue> input(capacity);
    uint32_t random = 123'456'789u;
    for (uint32_t index = 0u; index < count; ++index) {
        random ^= random << 13u;
        random ^= random >> 17u;
        random ^= random << 5u;
        input[index] = {random % 65'536u, (random >> 16u) % 32'768u,
                        index, index};
        if (index % 31u == 0u) {
            input[index].keyLow = UINT32_MAX;
            input[index].keyHigh = UINT32_MAX;
        }
    }
    std::vector<GpuKeyValue> golden(input.begin(), input.begin() + count);
    std::stable_sort(golden.begin(), golden.end(),
        [](const GpuKeyValue& lhs, const GpuKeyValue& rhs) {
            return std::tie(lhs.keyHigh, lhs.keyLow)
                 < std::tie(rhs.keyHigh, rhs.keyLow);
        });
    const size_t capacityBytes = size_t{capacity} * sizeof(GpuKeyValue);
    const size_t liveBytes = size_t{count} * sizeof(GpuKeyValue);
    Buffer source(gpu::createBufferWithData(
        context.getDevice(), context.getQueue(),
        gpu::BufferDesc::storage(capacityBytes, true, "radix_benchmark_source"),
        std::span<const GpuKeyValue>(input)));
    Buffer output(gpu::createBuffer(context.getDevice(),
        gpu::BufferDesc::storage(capacityBytes, false, "radix_benchmark_output")));
    const std::array<uint32_t, 4> counts{count, 0u, 0u, 0u};
    Buffer dynamicCounts(gpu::createBufferWithData(
        context.getDevice(), context.getQueue(),
        gpu::BufferDesc::storage(sizeof(counts), true, "radix_benchmark_count"),
        std::span<const uint32_t>(counts)));
    const std::array<uint32_t, 6> dispatch{
        (count + workgroup - 1u) / workgroup, 1u, 1u, 1u, 1u, 1u};
    Buffer indirect(gpu::createBufferWithData(
        context.getDevice(), context.getQueue(), gpu::BufferDesc{
            .label = "radix_benchmark_indirect", .size = sizeof(dispatch),
            .usage = WGPUBufferUsage_Indirect | WGPUBufferUsage_CopyDst,
        }, std::span<const uint32_t>(dispatch)));
    Buffer readback(gpu::createBuffer(context.getDevice(), gpu::BufferDesc{
        .label = "radix_benchmark_oracle", .size = liveBytes,
        .usage = WGPUBufferUsage_MapRead | WGPUBufferUsage_CopyDst,
    }));
    ASSERT_NE(static_cast<WGPUBuffer>(source), nullptr);
    ASSERT_NE(static_cast<WGPUBuffer>(output), nullptr);
    ASSERT_NE(static_cast<WGPUBuffer>(dynamicCounts), nullptr);
    ASSERT_NE(static_cast<WGPUBuffer>(indirect), nullptr);
    ASSERT_NE(static_cast<WGPUBuffer>(readback), nullptr);
    DeterministicGpuPrimitives primitives;
    DeterministicGpuPrimitives::Config config;
    config.capacity = capacity;
    config.workgroupSize = workgroup;
    ASSERT_TRUE(primitives.initialize(
        context.getDevice(), context.getQueue(), config));
    EXPECT_LE(primitives.scratchBytes(), maximumScratchBytes);

    std::vector<double> samples;
    samples.reserve(frames);
    size_t warmCacheSize = 0u;
    uint64_t hash = 14'695'981'039'346'656'037ull;
    for (uint32_t frame = 0u; frame < warmupFrames + frames; ++frame) {
        const auto start = std::chrono::steady_clock::now();
        WGPUCommandEncoderDescriptor encoderDesc{};
        WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(
            context.getDevice(), &encoderDesc);
        ASSERT_NE(encoder, nullptr);
        ASSERT_TRUE(primitives.encodeRadixSort(
            encoder, source, output, capacity, 2u, 8u,
            indirect, 0u, 3u * sizeof(uint32_t), dynamicCounts, 0u));
        WGPUCommandBufferDescriptor commandDesc{};
        WGPUCommandBuffer command = wgpuCommandEncoderFinish(encoder, &commandDesc);
        ASSERT_NE(command, nullptr);
        WGPUWrappedSubmissionIndex submission{
            context.getQueue(), wgpuQueueSubmitForIndex(
                context.getQueue(), 1u, &command)};
        static_cast<void>(wgpuDevicePoll(context.getDevice(), true, &submission));
        wgpuCommandBufferRelease(command);
        wgpuCommandEncoderRelease(encoder);
        const double milliseconds = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - start).count();
        if (frame >= warmupFrames) samples.push_back(milliseconds);

        if (frame != warmupFrames - 1u && frame != warmupFrames + frames - 1u)
            continue;
        if (frame == warmupFrames - 1u) {
            warmCacheSize = primitives.cachedBindGroupCount();
        } else {
            EXPECT_EQ(primitives.cachedBindGroupCount(), warmCacheSize);
        }
        // Full record comparison and GPU readback are outside the timed sort.
        encoder = wgpuDeviceCreateCommandEncoder(context.getDevice(), &encoderDesc);
        wgpuCommandEncoderCopyBufferToBuffer(
            encoder, output, 0u, readback, 0u, liveBytes);
        command = wgpuCommandEncoderFinish(encoder, &commandDesc);
        ASSERT_NE(command, nullptr);
        submission.submissionIndex = wgpuQueueSubmitForIndex(
            context.getQueue(), 1u, &command);
        struct MapState { bool done = false; bool success = false; } state;
        wgpuBufferMapAsync(readback, WGPUMapMode_Read, 0u, liveBytes,
            [](WGPUBufferMapAsyncStatus status, void* data) {
                auto& mapped = *static_cast<MapState*>(data);
                mapped.success = status == WGPUBufferMapAsyncStatus_Success;
                mapped.done = true;
            }, &state);
        while (!state.done) {
            static_cast<void>(wgpuDevicePoll(
                context.getDevice(), true, &submission));
        }
        ASSERT_TRUE(state.success);
        const auto* bytes = static_cast<const std::byte*>(
            wgpuBufferGetConstMappedRange(readback, 0u, liveBytes));
        ASSERT_NE(bytes, nullptr);
        EXPECT_EQ(std::memcmp(bytes, golden.data(), liveBytes), 0);
        if (frame == warmupFrames - 1u) {
            for (size_t index = 0u; index < liveBytes; ++index) {
                hash = (hash ^ std::to_integer<uint8_t>(bytes[index]))
                    * 1'099'511'628'211ull;
            }
        }
        wgpuBufferUnmap(readback);
        wgpuCommandBufferRelease(command);
        wgpuCommandEncoderRelease(encoder);
    }
    const double totalMs = std::accumulate(samples.begin(), samples.end(), 0.0);
    std::sort(samples.begin(), samples.end());
    const auto percentile = [&](size_t percent) {
        return samples[(samples.size() * percent + 99u) / 100u - 1u];
    };
    std::cout << std::fixed << std::setprecision(6)
              << "gpu_radix records=" << count << " capacity=" << capacity
              << " workgroup=" << workgroup << " frames=" << frames
              << " p50_ms=" << percentile(50u)
              << " p95_ms=" << percentile(95u)
              << " p99_ms=" << percentile(99u)
              << " sorts_per_s=" << static_cast<double>(frames) * 1000.0 / totalMs
              << " scratch_bytes=" << primitives.scratchBytes()
              << " cached_bind_groups=" << warmCacheSize
              << " golden_fnv1a=" << std::hex << hash << std::dec << '\n';
    EXPECT_LE(percentile(95u), static_cast<double>(maximumP95Micros) / 1000.0);
}

} // namespace
} // namespace voxy::physics
