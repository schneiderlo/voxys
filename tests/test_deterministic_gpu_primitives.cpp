#include <gtest/gtest.h>

#include "gpu/context.hpp"
#include "gpu/resources.hpp"
#include "physics/gpu/deterministic_primitives.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <numeric>
#include <random>
#include <span>
#include <vector>

#ifndef WGPUWrappedSubmissionIndex
using WGPUSubmissionIndex = uint64_t;
struct WGPUWrappedSubmissionIndex {
    WGPUQueue queue;
    WGPUSubmissionIndex submissionIndex;
};
#endif

extern "C" WGPUSubmissionIndex wgpuQueueSubmitForIndex(
    WGPUQueue queue, size_t commandCount,
    const WGPUCommandBuffer* commands);
extern "C" WGPUBool wgpuDevicePoll(
    WGPUDevice device, WGPUBool wait,
    const WGPUWrappedSubmissionIndex* wrappedSubmissionIndex);

namespace voxy::physics {
namespace {

class BufferOwner {
public:
    BufferOwner() = default;
    explicit BufferOwner(WGPUBuffer buffer) : buffer_(buffer) {}
    ~BufferOwner() { reset(); }
    BufferOwner(const BufferOwner&) = delete;
    BufferOwner& operator=(const BufferOwner&) = delete;
    BufferOwner(BufferOwner&& other) noexcept : buffer_(other.buffer_) {
        other.buffer_ = nullptr;
    }
    BufferOwner& operator=(BufferOwner&& other) noexcept {
        if (this != &other) {
            reset();
            buffer_ = other.buffer_;
            other.buffer_ = nullptr;
        }
        return *this;
    }
    operator WGPUBuffer() const noexcept { return buffer_; }
    void reset() {
        if (buffer_) {
            wgpuBufferDestroy(buffer_);
            wgpuBufferRelease(buffer_);
            buffer_ = nullptr;
        }
    }
private:
    WGPUBuffer buffer_ = nullptr;
};

template <typename T>
BufferOwner inputBuffer(gpu::Context& context, std::span<const T> values,
                        const char* label) {
    return BufferOwner(gpu::createBufferWithData(
        context.getDevice(), context.getQueue(),
        gpu::BufferDesc::storage(values.size_bytes(), true, label), values));
}

template <typename T>
BufferOwner outputBuffer(gpu::Context& context, size_t count,
                         const char* label) {
    return BufferOwner(gpu::createBuffer(
        context.getDevice(),
        gpu::BufferDesc::storage(count * sizeof(T), false, label)));
}

struct CopyLayout {
    size_t cursor = 0;
    size_t scan = 0;
    size_t compact = 0;
    size_t compactResult = 0;
    size_t radix32 = 0;
    size_t radixByte = 0;
    size_t radixWord = 0;
    size_t radix64 = 0;
    size_t unique = 0;
    size_t uniqueResult = 0;
    size_t merge = 0;
    size_t mergeTags = 0;
    size_t mergeResult = 0;
    size_t assigned = 0;
    size_t assignResult = 0;

    size_t allocate(size_t bytes) {
        const size_t result = cursor;
        cursor += bytes;
        return result;
    }
};

class DeterministicGpuPrimitivesTest
    : public ::testing::TestWithParam<uint32_t> {};

TEST_P(DeterministicGpuPrimitivesTest, MatchesLargeRandomizedCpuReferences) {
    constexpr uint32_t count = 8'193;
    constexpr uint32_t capacity = 32'768;
    gpu::Context context;
    gpu::ContextConfig contextConfig;
    contextConfig.enableValidation = false;
    if (!context.initHeadless(contextConfig)) {
        GTEST_SKIP() << "Headless WebGPU is unavailable";
    }

    DeterministicGpuPrimitives primitives;
    DeterministicGpuPrimitives::Config config;
    config.capacity = capacity;
    config.workgroupSize = GetParam();
    ASSERT_TRUE(primitives.initialize(
        context.getDevice(), context.getQueue(), config));
    EXPECT_FALSE(primitives.usesSubgroups());

    std::mt19937 random(0x51a7'c0deu);
    std::uniform_int_distribution<uint32_t> smallValue(0u, 31u);
    std::uniform_int_distribution<uint32_t> keyValue(0u, 0xffffu);
    std::uniform_int_distribution<uint32_t> boundedKeyValue(
        0u, 0x00fffffeu);
    std::vector<uint32_t> scanInput(count);
    std::vector<uint32_t> values(count);
    std::vector<uint32_t> predicates(count);
    std::vector<GpuKeyValue> records32(count);
    std::vector<GpuKeyValue> recordsByte(count);
    std::vector<GpuKeyValue> records64(count);
    for (uint32_t index = 0; index < count; ++index) {
        scanInput[index] = smallValue(random);
        values[index] = index * 17u + 3u;
        predicates[index] = (smallValue(random) % 5u) == 0u ? 1u : 0u;
        records32[index] = {
            .keyLow = keyValue(random) & 0x3ffu,
            .keyHigh = keyValue(random) & 0x7fffu,
            .value = index ^ 0xa5a5u,
            .ordinal = index,
        };
        recordsByte[index] = records32[index];
        recordsByte[index].keyHigh &= 31u;
        if (index % 997u == 0u) {
            recordsByte[index].keyHigh =
                std::numeric_limits<uint32_t>::max();
        }
        records64[index] = {
            .keyLow = boundedKeyValue(random),
            .keyHigh = boundedKeyValue(random),
            .value = index * 5u,
            .ordinal = index,
        };
        if (index % 997u == 0u) {
            records64[index].keyLow = std::numeric_limits<uint32_t>::max();
            records64[index].keyHigh = std::numeric_limits<uint32_t>::max();
        }
    }

    std::vector<uint32_t> expectedScan(count);
    uint32_t prefix = 0;
    for (uint32_t index = 0; index < count; ++index) {
        expectedScan[index] = prefix;
        prefix += scanInput[index];
    }
    std::vector<uint32_t> expectedCompact;
    for (uint32_t index = 0; index < count; ++index) {
        if (predicates[index] != 0u) expectedCompact.push_back(values[index]);
    }
    auto expected32 = records32;
    std::stable_sort(expected32.begin(), expected32.end(),
        [](const GpuKeyValue& lhs, const GpuKeyValue& rhs) {
            return lhs.keyLow < rhs.keyLow;
        });
    auto expectedWord = records32;
    std::stable_sort(expectedWord.begin(), expectedWord.end(),
        [](const GpuKeyValue& lhs, const GpuKeyValue& rhs) {
            return lhs.keyHigh < rhs.keyHigh;
        });
    auto expectedByte = recordsByte;
    std::stable_sort(expectedByte.begin(), expectedByte.end(),
        [](const GpuKeyValue& lhs, const GpuKeyValue& rhs) {
            return lhs.keyHigh < rhs.keyHigh;
        });
    auto expected64 = records64;
    std::stable_sort(expected64.begin(), expected64.end(),
        [](const GpuKeyValue& lhs, const GpuKeyValue& rhs) {
            return lhs.keyHigh < rhs.keyHigh
                || (lhs.keyHigh == rhs.keyHigh && lhs.keyLow < rhs.keyLow);
        });
    std::vector<GpuKeyValue> expectedUnique;
    for (const GpuKeyValue& record : expected64) {
        if (expectedUnique.empty()
            || expectedUnique.back().keyLow != record.keyLow
            || expectedUnique.back().keyHigh != record.keyHigh) {
            expectedUnique.push_back(record);
        }
    }

    std::vector<GpuKeyValue> previous;
    std::vector<GpuKeyValue> current;
    for (uint32_t key = 0; key < 7'000u; key += 2u) {
        previous.push_back({key, key / 97u, key + 10u, key});
    }
    for (uint32_t key = 0; key < 7'000u; key += 3u) {
        current.push_back({key, key / 97u, key + 20u, key});
    }
    auto keyLess = [](const GpuKeyValue& lhs, const GpuKeyValue& rhs) {
        return lhs.keyHigh < rhs.keyHigh
            || (lhs.keyHigh == rhs.keyHigh && lhs.keyLow < rhs.keyLow);
    };
    std::sort(previous.begin(), previous.end(), keyLess);
    std::sort(current.begin(), current.end(), keyLess);
    std::vector<GpuKeyValue> expectedMerge;
    std::vector<uint32_t> expectedMergeTags;
    size_t previousIndex = 0;
    size_t currentIndex = 0;
    while (previousIndex < previous.size() || currentIndex < current.size()) {
        if (previousIndex == previous.size()) {
            expectedMerge.push_back(current[currentIndex++]);
            expectedMergeTags.push_back(
                static_cast<uint32_t>(GpuMergeTag::CurrentOnly));
        } else if (currentIndex == current.size()) {
            expectedMerge.push_back(previous[previousIndex++]);
            expectedMergeTags.push_back(
                static_cast<uint32_t>(GpuMergeTag::PreviousOnly));
        } else if (previous[previousIndex].keyLow == current[currentIndex].keyLow
                   && previous[previousIndex].keyHigh
                      == current[currentIndex].keyHigh) {
            expectedMerge.push_back(current[currentIndex++]);
            ++previousIndex;
            expectedMergeTags.push_back(
                static_cast<uint32_t>(GpuMergeTag::Matched));
        } else if (keyLess(previous[previousIndex], current[currentIndex])) {
            expectedMerge.push_back(previous[previousIndex++]);
            expectedMergeTags.push_back(
                static_cast<uint32_t>(GpuMergeTag::PreviousOnly));
        } else {
            expectedMerge.push_back(current[currentIndex++]);
            expectedMergeTags.push_back(
                static_cast<uint32_t>(GpuMergeTag::CurrentOnly));
        }
    }

    std::vector<uint32_t> freeRequests(count);
    for (uint32_t index = 0; index < count; ++index) {
        freeRequests[index] = index % 3u == 1u ? 1u : 0u;
    }
    constexpr uint32_t freeIdCount = 1'337;
    std::vector<uint32_t> freeIds(freeIdCount);
    for (uint32_t index = 0; index < freeIdCount; ++index) {
        freeIds[index] = 10'000u + index * 2u;
    }
    std::vector<uint32_t> expectedAssignments(
        count, std::numeric_limits<uint32_t>::max());
    uint32_t requestRank = 0;
    for (uint32_t index = 0; index < count; ++index) {
        if (freeRequests[index] != 0u) {
            if (requestRank < freeIds.size()) {
                expectedAssignments[index] = freeIds[requestRank];
            }
            ++requestRank;
        }
    }

    auto scanInputBuffer = inputBuffer<uint32_t>(context, scanInput, "scan_input");
    auto valuesBuffer = inputBuffer<uint32_t>(context, values, "compact_values");
    auto predicatesBuffer = inputBuffer<uint32_t>(
        context, predicates, "compact_predicates");
    auto records32Buffer = inputBuffer<GpuKeyValue>(
        context, records32, "radix32_input");
    auto recordsByteBuffer = inputBuffer<GpuKeyValue>(
        context, recordsByte, "radix_byte_input");
    auto records64Buffer = inputBuffer<GpuKeyValue>(
        context, records64, "radix64_input");
    auto previousBuffer = inputBuffer<GpuKeyValue>(
        context, previous, "merge_previous");
    auto currentBuffer = inputBuffer<GpuKeyValue>(
        context, current, "merge_current");
    auto requestsBuffer = inputBuffer<uint32_t>(
        context, freeRequests, "free_requests");
    auto freeIdsBuffer = inputBuffer<uint32_t>(context, freeIds, "free_ids");
    auto scanOutput = outputBuffer<uint32_t>(context, count, "scan_output");
    auto compactOutput = outputBuffer<uint32_t>(context, count, "compact_output");
    auto radix32Output = outputBuffer<GpuKeyValue>(context, count, "radix32_output");
    auto radixByteOutput = outputBuffer<GpuKeyValue>(
        context, count, "radix_byte_output");
    auto radixWordOutput = outputBuffer<GpuKeyValue>(
        context, count, "radix_word_output");
    auto radix64Output = outputBuffer<GpuKeyValue>(context, count, "radix64_output");
    auto uniqueOutput = outputBuffer<GpuKeyValue>(context, count, "unique_output");
    auto mergeOutput = outputBuffer<GpuKeyValue>(
        context, previous.size() + current.size(), "merge_output");
    auto mergeTags = outputBuffer<uint32_t>(
        context, previous.size() + current.size(), "merge_tags");
    auto assignments = outputBuffer<uint32_t>(context, count, "assignments");

    WGPUCommandEncoderDescriptor encoderDesc{};
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(
        context.getDevice(), &encoderDesc);
    ASSERT_TRUE(primitives.encodeScanU32(
        encoder, scanInputBuffer, scanOutput, count));
    ASSERT_FALSE(primitives.encodeScanU32(
        encoder, scanInputBuffer, scanOutput, capacity + 1u));
    ASSERT_TRUE(primitives.encodeStableCompactU32(
        encoder, valuesBuffer, predicatesBuffer, compactOutput, count));
    ASSERT_TRUE(primitives.encodeRadixSort(
        encoder, records32Buffer, radix32Output, count, 1u));
    ASSERT_TRUE(primitives.encodeRadixSortBoundedU16Word(
        encoder, records32Buffer, radixWordOutput, count,
        1u, 0x8000u, 12u));
    ASSERT_TRUE(primitives.encodeRadixSortBoundedU16Word(
        encoder, recordsByteBuffer, radixByteOutput, count,
        1u, 33u, 14u));
    ASSERT_FALSE(primitives.encodeRadixSortBoundedU16Word(
        encoder, records32Buffer, radixWordOutput, count,
        2u, 0x8000u, 12u));
    ASSERT_TRUE(primitives.encodeRadixSortBoundedU32x2(
        encoder, records64Buffer, radix64Output, count, 0x00ffffffu, 20u));
    ASSERT_TRUE(primitives.encodeAdjacentUnique(
        encoder, radix64Output, uniqueOutput, count, 2u));
    ASSERT_TRUE(primitives.encodeSortedMerge(
        encoder, previousBuffer, static_cast<uint32_t>(previous.size()),
        currentBuffer, static_cast<uint32_t>(current.size()),
        mergeOutput, mergeTags));
    ASSERT_TRUE(primitives.encodeAssignFreeIds(
        encoder, requestsBuffer, count, freeIdsBuffer, freeIdCount, assignments));
    const size_t bindGroupsAfterFirstEncode =
        primitives.cachedBindGroupCount();
    EXPECT_GT(bindGroupsAfterFirstEncode, 0u);

    CopyLayout layout;
    const size_t u32Bytes = size_t{count} * sizeof(uint32_t);
    const size_t recordBytes = size_t{count} * sizeof(GpuKeyValue);
    const size_t mergeRecordBytes = expectedMerge.size() * sizeof(GpuKeyValue);
    const size_t mergeTagBytes = expectedMergeTags.size() * sizeof(uint32_t);
    layout.scan = layout.allocate(u32Bytes);
    layout.compact = layout.allocate(expectedCompact.size() * sizeof(uint32_t));
    layout.compactResult = layout.allocate(4u * sizeof(uint32_t));
    layout.radix32 = layout.allocate(recordBytes);
    layout.radixByte = layout.allocate(recordBytes);
    layout.radixWord = layout.allocate(recordBytes);
    layout.radix64 = layout.allocate(recordBytes);
    layout.unique = layout.allocate(expectedUnique.size() * sizeof(GpuKeyValue));
    layout.uniqueResult = layout.allocate(4u * sizeof(uint32_t));
    layout.merge = layout.allocate(mergeRecordBytes);
    layout.mergeTags = layout.allocate(mergeTagBytes);
    layout.mergeResult = layout.allocate(4u * sizeof(uint32_t));
    layout.assigned = layout.allocate(u32Bytes);
    layout.assignResult = layout.allocate(4u * sizeof(uint32_t));
    BufferOwner readback(gpu::createBuffer(
        context.getDevice(), gpu::BufferDesc{
            .label = "deterministic_primitives_readback",
            .size = layout.cursor,
            .usage = WGPUBufferUsage_CopyDst | WGPUBufferUsage_MapRead,
        }));
    ASSERT_NE(static_cast<WGPUBuffer>(readback), nullptr);

    // Copy after each operation's dispatches. The result buffer is reused, so
    // its copies preserve each operation's canonical count and overflow data.
    wgpuCommandEncoderCopyBufferToBuffer(
        encoder, scanOutput, 0, readback, layout.scan, u32Bytes);
    // Re-encode result-producing operations immediately before their result copy.
    ASSERT_TRUE(primitives.encodeStableCompactU32(
        encoder, valuesBuffer, predicatesBuffer, compactOutput, count));
    wgpuCommandEncoderCopyBufferToBuffer(
        encoder, compactOutput, 0, readback, layout.compact,
        expectedCompact.size() * sizeof(uint32_t));
    wgpuCommandEncoderCopyBufferToBuffer(
        encoder, primitives.resultBuffer(), 0, readback,
        layout.compactResult, 4u * sizeof(uint32_t));
    wgpuCommandEncoderCopyBufferToBuffer(
        encoder, radix32Output, 0, readback, layout.radix32, recordBytes);
    wgpuCommandEncoderCopyBufferToBuffer(
        encoder, radixByteOutput, 0,
        readback, layout.radixByte, recordBytes);
    wgpuCommandEncoderCopyBufferToBuffer(
        encoder, radixWordOutput, 0,
        readback, layout.radixWord, recordBytes);
    wgpuCommandEncoderCopyBufferToBuffer(
        encoder, radix64Output, 0, readback, layout.radix64, recordBytes);
    ASSERT_TRUE(primitives.encodeAdjacentUnique(
        encoder, radix64Output, uniqueOutput, count, 2u));
    wgpuCommandEncoderCopyBufferToBuffer(
        encoder, uniqueOutput, 0, readback, layout.unique,
        expectedUnique.size() * sizeof(GpuKeyValue));
    wgpuCommandEncoderCopyBufferToBuffer(
        encoder, primitives.resultBuffer(), 0, readback,
        layout.uniqueResult, 4u * sizeof(uint32_t));
    ASSERT_TRUE(primitives.encodeSortedMerge(
        encoder, previousBuffer, static_cast<uint32_t>(previous.size()),
        currentBuffer, static_cast<uint32_t>(current.size()),
        mergeOutput, mergeTags));
    wgpuCommandEncoderCopyBufferToBuffer(
        encoder, mergeOutput, 0, readback, layout.merge, mergeRecordBytes);
    wgpuCommandEncoderCopyBufferToBuffer(
        encoder, mergeTags, 0, readback, layout.mergeTags, mergeTagBytes);
    wgpuCommandEncoderCopyBufferToBuffer(
        encoder, primitives.resultBuffer(), 0, readback,
        layout.mergeResult, 4u * sizeof(uint32_t));
    ASSERT_TRUE(primitives.encodeAssignFreeIds(
        encoder, requestsBuffer, count, freeIdsBuffer, freeIdCount, assignments));
    wgpuCommandEncoderCopyBufferToBuffer(
        encoder, assignments, 0, readback, layout.assigned, u32Bytes);
    wgpuCommandEncoderCopyBufferToBuffer(
        encoder, primitives.resultBuffer(), 0, readback,
        layout.assignResult, 4u * sizeof(uint32_t));
    EXPECT_EQ(primitives.cachedBindGroupCount(),
              bindGroupsAfterFirstEncode);

    WGPUCommandBufferDescriptor commandDesc{};
    WGPUCommandBuffer command = wgpuCommandEncoderFinish(encoder, &commandDesc);
    const WGPUSubmissionIndex submissionIndex = wgpuQueueSubmitForIndex(
        context.getQueue(), 1, &command);
    const WGPUWrappedSubmissionIndex submission{
        context.getQueue(), submissionIndex};
    struct MapState { bool done = false; bool success = false; } mapState;
    const auto callback = [](WGPUBufferMapAsyncStatus status, void* userdata) {
        auto& state = *static_cast<MapState*>(userdata);
        state.success = status == WGPUBufferMapAsyncStatus_Success;
        state.done = true;
    };
    wgpuBufferMapAsync(readback, WGPUMapMode_Read, 0, layout.cursor,
                       callback, &mapState);
    while (!mapState.done) {
        static_cast<void>(wgpuDevicePoll(
            context.getDevice(), true, &submission));
    }
    ASSERT_TRUE(mapState.success);
    const auto* bytes = static_cast<const std::byte*>(
        wgpuBufferGetConstMappedRange(readback, 0, layout.cursor));
    ASSERT_NE(bytes, nullptr);
    auto expectBytes = [&](size_t offset, const auto& expected) {
        EXPECT_EQ(std::memcmp(bytes + offset, expected.data(),
                              expected.size() * sizeof(expected.front())), 0);
    };
    expectBytes(layout.scan, expectedScan);
    expectBytes(layout.compact, expectedCompact);
    expectBytes(layout.radix32, expected32);
    expectBytes(layout.radixByte, expectedByte);
    expectBytes(layout.radixWord, expectedWord);
    expectBytes(layout.radix64, expected64);
    expectBytes(layout.unique, expectedUnique);
    expectBytes(layout.merge, expectedMerge);
    expectBytes(layout.mergeTags, expectedMergeTags);
    expectBytes(layout.assigned, expectedAssignments);

    auto resultAt = [&](size_t offset) {
        std::array<uint32_t, 4> result{};
        std::memcpy(result.data(), bytes + offset, sizeof(result));
        return result;
    };
    EXPECT_EQ(resultAt(layout.compactResult)[0], expectedCompact.size());
    EXPECT_EQ(resultAt(layout.uniqueResult)[0], expectedUnique.size());
    EXPECT_EQ(resultAt(layout.mergeResult)[0], expectedMerge.size());
    const auto assignResult = resultAt(layout.assignResult);
    EXPECT_EQ(assignResult[0], requestRank);
    EXPECT_EQ(assignResult[1], freeIdCount);
    EXPECT_EQ(assignResult[2], requestRank - freeIdCount);

    wgpuBufferUnmap(readback);
    wgpuCommandBufferRelease(command);
    wgpuCommandEncoderRelease(encoder);
}

TEST_P(DeterministicGpuPrimitivesTest, DynamicCountAndDispatchMatchCpuReferences) {
    constexpr uint32_t capacity = 512u;
    constexpr uint32_t actualCount = 122u;
    constexpr uint32_t countWord = 2u;
    gpu::Context context;
    gpu::ContextConfig contextConfig;
    contextConfig.enableValidation = false;
    if (!context.initHeadless(contextConfig)) {
        GTEST_SKIP() << "Headless WebGPU is unavailable";
    }

    DeterministicGpuPrimitives primitives;
    DeterministicGpuPrimitives::Config config;
    config.capacity = capacity;
    config.workgroupSize = GetParam();
    ASSERT_TRUE(primitives.initialize(
        context.getDevice(), context.getQueue(), config));

    std::mt19937 random(0x7a11'c0deu);
    std::uniform_int_distribution<uint32_t> keyValue(0u, 31u);
    std::vector<uint32_t> scanInput(capacity);
    std::vector<GpuKeyValue> records(capacity);
    for (uint32_t index = 0; index < capacity; ++index) {
        scanInput[index] = index % 7u;
        records[index] = {
            .keyLow = keyValue(random),
            .keyHigh = keyValue(random),
            .value = index ^ 0x5a5au,
            .ordinal = index,
        };
    }
    std::vector<uint32_t> expectedScan(actualCount);
    uint32_t prefix = 0u;
    for (uint32_t index = 0; index < actualCount; ++index) {
        expectedScan[index] = prefix;
        prefix += scanInput[index];
    }
    std::vector<GpuKeyValue> expectedSort(
        records.begin(), records.begin() + actualCount);
    std::stable_sort(expectedSort.begin(), expectedSort.end(),
        [](const GpuKeyValue& lhs, const GpuKeyValue& rhs) {
            return lhs.keyHigh < rhs.keyHigh
                || (lhs.keyHigh == rhs.keyHigh
                    && lhs.keyLow < rhs.keyLow);
        });

    const std::array<uint32_t, 4> dynamicCounts = {0u, 0u, actualCount, 0u};
    const uint32_t groups =
        (actualCount + GetParam() - 1u) / GetParam();
    const std::array<uint32_t, 6> dispatch = {
        groups, 1u, 1u, 1u, 1u, 1u};
    auto scanInputBuffer = inputBuffer<uint32_t>(
        context, scanInput, "dynamic_scan_input");
    auto recordsBuffer = inputBuffer<GpuKeyValue>(
        context, records, "dynamic_radix_input");
    auto dynamicCountBuffer = inputBuffer<uint32_t>(
        context, dynamicCounts, "dynamic_counts");
    BufferOwner dispatchBuffer(gpu::createBufferWithData(
        context.getDevice(), context.getQueue(), gpu::BufferDesc{
            .label = "dynamic_dispatch",
            .size = sizeof(dispatch),
            .usage = WGPUBufferUsage_Storage | WGPUBufferUsage_CopyDst
                   | WGPUBufferUsage_Indirect,
        }, std::span<const uint32_t>(dispatch)));
    auto scanOutput = outputBuffer<uint32_t>(
        context, capacity, "dynamic_scan_output");
    auto sortOutput = outputBuffer<GpuKeyValue>(
        context, capacity, "dynamic_radix_output");

    WGPUCommandEncoderDescriptor encoderDesc{};
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(
        context.getDevice(), &encoderDesc);
    ASSERT_TRUE(primitives.encodeScanU32(
        encoder, scanInputBuffer, scanOutput, capacity, 0u,
        dispatchBuffer, 0u, 3u * sizeof(uint32_t),
        dynamicCountBuffer, countWord));
    ASSERT_TRUE(primitives.encodeRadixSortBoundedU32x2(
        encoder, recordsBuffer, sortOutput, capacity, 0xffffu, 8u,
        dispatchBuffer, 0u, 3u * sizeof(uint32_t),
        dynamicCountBuffer, countWord));

    const size_t scanBytes = expectedScan.size() * sizeof(uint32_t);
    const size_t sortBytes = expectedSort.size() * sizeof(GpuKeyValue);
    BufferOwner readback(gpu::createBuffer(
        context.getDevice(), gpu::BufferDesc{
            .label = "dynamic_primitives_readback",
            .size = scanBytes + sortBytes,
            .usage = WGPUBufferUsage_CopyDst | WGPUBufferUsage_MapRead,
        }));
    ASSERT_NE(static_cast<WGPUBuffer>(readback), nullptr);
    wgpuCommandEncoderCopyBufferToBuffer(
        encoder, scanOutput, 0u, readback, 0u, scanBytes);
    wgpuCommandEncoderCopyBufferToBuffer(
        encoder, sortOutput, 0u, readback, scanBytes, sortBytes);

    WGPUCommandBufferDescriptor commandDesc{};
    WGPUCommandBuffer command = wgpuCommandEncoderFinish(encoder, &commandDesc);
    const WGPUSubmissionIndex submissionIndex = wgpuQueueSubmitForIndex(
        context.getQueue(), 1, &command);
    const WGPUWrappedSubmissionIndex submission{
        context.getQueue(), submissionIndex};
    struct MapState { bool done = false; bool success = false; } mapState;
    const auto callback = [](WGPUBufferMapAsyncStatus status, void* userdata) {
        auto& state = *static_cast<MapState*>(userdata);
        state.success = status == WGPUBufferMapAsyncStatus_Success;
        state.done = true;
    };
    wgpuBufferMapAsync(readback, WGPUMapMode_Read, 0, scanBytes + sortBytes,
                       callback, &mapState);
    while (!mapState.done) {
        static_cast<void>(wgpuDevicePoll(
            context.getDevice(), true, &submission));
    }
    ASSERT_TRUE(mapState.success);
    const auto* bytes = static_cast<const std::byte*>(
        wgpuBufferGetConstMappedRange(readback, 0, scanBytes + sortBytes));
    ASSERT_NE(bytes, nullptr);
    EXPECT_EQ(std::memcmp(bytes, expectedScan.data(), scanBytes), 0);
    EXPECT_EQ(std::memcmp(
        bytes + scanBytes, expectedSort.data(), sortBytes), 0);

    wgpuBufferUnmap(readback);
    wgpuCommandBufferRelease(command);
    wgpuCommandEncoderRelease(encoder);
}

// Exercise both sides of the parallel-prefix cutoff with the same allocated
// storage. Tail sentinels catch writes beyond the GPU-selected active prefix.
TEST_P(DeterministicGpuPrimitivesTest,
       RadixPrefixPreservesStableBytesAcrossDynamicCountTransitions) {
    constexpr uint32_t capacity = 131'073u;
    constexpr uint32_t countWord = 2u;
    gpu::Context context;
    gpu::ContextConfig contextConfig;
    contextConfig.enableValidation = true;
    if (!context.initHeadless(contextConfig)) {
        GTEST_SKIP() << "Headless WebGPU is unavailable";
    }
    DeterministicGpuPrimitives primitives;
    DeterministicGpuPrimitives::Config config;
    config.capacity = capacity;
    config.workgroupSize = GetParam();
    ASSERT_TRUE(primitives.initialize(
        context.getDevice(), context.getQueue(), config));

    std::mt19937 random(0x51a7'1234u);
    std::vector<GpuKeyValue> records(capacity);
    for (uint32_t index = 0u; index < capacity; ++index) {
        // Repeated keys, empty histogram bins and stable all-ones sentinels.
        records[index] = {
            static_cast<uint32_t>(random()) & 127u,
            static_cast<uint32_t>(random()) & 63u,
            index ^ 0x5a5a'a5a5u, index};
        if (index % 19u == 0u) {
            records[index].keyLow = UINT32_MAX;
            records[index].keyHigh = UINT32_MAX;
        }
    }
    const GpuKeyValue untouched{0xabcd'1234u, 0x9876'5432u,
                                0xfedc'ba98u, 0x1234'5678u};
    const std::vector<GpuKeyValue> initialOutput(capacity, untouched);
    auto input = inputBuffer<GpuKeyValue>(context, records, "prefix_input");
    auto output = outputBuffer<GpuKeyValue>(context, capacity, "prefix_output");
    const std::array<uint32_t, 4> initialCounts{};
    auto counts = inputBuffer<uint32_t>(context, initialCounts, "prefix_counts");
    const std::array<uint32_t, 6> initialDispatch{};
    BufferOwner dispatch(gpu::createBufferWithData(
        context.getDevice(), context.getQueue(), gpu::BufferDesc{
            .label = "prefix_dispatch", .size = sizeof(initialDispatch),
            .usage = WGPUBufferUsage_Storage | WGPUBufferUsage_CopyDst
                   | WGPUBufferUsage_Indirect,
        }, std::span<const uint32_t>(initialDispatch)));
    const size_t bytes = size_t{capacity} * sizeof(GpuKeyValue);
    BufferOwner readback(gpu::createBuffer(context.getDevice(), gpu::BufferDesc{
        .label = "prefix_readback", .size = bytes,
        .usage = WGPUBufferUsage_CopyDst | WGPUBufferUsage_MapRead,
    }));
    ASSERT_NE(static_cast<WGPUBuffer>(input), nullptr);
    ASSERT_NE(static_cast<WGPUBuffer>(output), nullptr);
    ASSERT_NE(static_cast<WGPUBuffer>(counts), nullptr);
    ASSERT_NE(static_cast<WGPUBuffer>(dispatch), nullptr);
    ASSERT_NE(static_cast<WGPUBuffer>(readback), nullptr);

    const uint32_t boundary = GetParam() * 32u;
    const std::array<uint32_t, 8> requestedCounts{
        capacity, 0u, 1u, boundary - 1u, boundary,
        boundary + 1u, capacity - 1u, 0u};
    for (uint32_t mode = 0u; mode < 6u; ++mode) {
        for (const uint32_t requested : requestedCounts) {
            SCOPED_TRACE(::testing::Message()
                << "mode=" << mode << " requested=" << requested);
            const uint32_t scale = mode == 5u ? 2u : 1u;
            const uint32_t actual =
                std::min(requested, capacity / scale) * scale;
            const std::array<uint32_t, 4> countData{0u, 0u, requested, 0u};
            const std::array<uint32_t, 6> dispatchData{
                (actual + GetParam() - 1u) / GetParam(), 1u, 1u,
                actual == 0u ? 0u : 1u, 1u, 1u};
            ASSERT_TRUE(gpu::writeBuffer(context.getQueue(), counts, 0u,
                std::as_bytes(std::span<const uint32_t>(countData))));
            ASSERT_TRUE(gpu::writeBuffer(context.getQueue(), dispatch, 0u,
                std::as_bytes(std::span<const uint32_t>(dispatchData))));
            ASSERT_TRUE(gpu::writeBuffer(context.getQueue(), output, 0u,
                std::as_bytes(std::span<const GpuKeyValue>(initialOutput))));
            std::vector<GpuKeyValue> expected(records.begin(),
                                               records.begin() + actual);
            std::stable_sort(expected.begin(), expected.end(),
                [mode](const GpuKeyValue& lhs, const GpuKeyValue& rhs) {
                    if (mode == 0u || mode == 3u)
                        return lhs.keyLow < rhs.keyLow;
                    if (mode == 4u) return lhs.keyHigh < rhs.keyHigh;
                    return lhs.keyHigh < rhs.keyHigh
                        || (lhs.keyHigh == rhs.keyHigh
                            && lhs.keyLow < rhs.keyLow);
                });
            expected.resize(capacity, untouched);
            WGPUCommandEncoderDescriptor encoderDesc{};
            WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(
                context.getDevice(), &encoderDesc);
            ASSERT_NE(encoder, nullptr);
            bool encoded = false;
            if (mode < 2u || mode == 5u) {
                encoded = primitives.encodeRadixSort(
                    encoder, input, output, capacity,
                    mode == 0u ? 1u : 2u, 8u, dispatch, 0u,
                    3u * sizeof(uint32_t), counts, countWord, scale);
            } else if (mode == 2u) {
                encoded = primitives.encodeRadixSortBoundedU32x2(
                    encoder, input, output, capacity, 0xffffu, 8u,
                    dispatch, 0u, 3u * sizeof(uint32_t), counts, countWord);
            } else {
                encoded = primitives.encodeRadixSortBoundedU16Word(
                    encoder, input, output, capacity, mode - 3u,
                    mode == 3u ? 0xffu : 0xffffu, 8u,
                    dispatch, 0u, 3u * sizeof(uint32_t), counts, countWord);
            }
            ASSERT_TRUE(encoded);
            wgpuCommandEncoderCopyBufferToBuffer(
                encoder, output, 0u, readback, 0u, bytes);
            WGPUCommandBufferDescriptor commandDesc{};
            WGPUCommandBuffer command =
                wgpuCommandEncoderFinish(encoder, &commandDesc);
            ASSERT_NE(command, nullptr);
            const WGPUWrappedSubmissionIndex submission{
                context.getQueue(), wgpuQueueSubmitForIndex(
                    context.getQueue(), 1u, &command)};
            struct MapState { bool done = false; bool success = false; } state;
            wgpuBufferMapAsync(readback, WGPUMapMode_Read, 0u, bytes,
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
            const void* mapped =
                wgpuBufferGetConstMappedRange(readback, 0u, bytes);
            ASSERT_NE(mapped, nullptr);
            EXPECT_EQ(std::memcmp(mapped, expected.data(), bytes), 0);
            wgpuBufferUnmap(readback);
            wgpuCommandBufferRelease(command);
            wgpuCommandEncoderRelease(encoder);
        }
    }
}

INSTANTIATE_TEST_SUITE_P(
    WorkgroupFallbacks, DeterministicGpuPrimitivesTest,
    ::testing::Values(64u, 128u, 256u));

} // namespace
} // namespace voxy::physics
