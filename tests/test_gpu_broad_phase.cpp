#include <gtest/gtest.h>

#include "gpu/context.hpp"
#include "gpu/resources.hpp"
#include "physics/gpu/gpu_body_metadata.hpp"
#include "physics/gpu/gpu_broad_phase.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <map>
#include <random>
#include <set>
#include <span>
#include <vector>

#include <glm/vec4.hpp>
#include <glm/vec3.hpp>
#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <glm/gtx/component_wise.hpp>
#include <glm/vector_relational.hpp>

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

constexpr uint32_t kAlive = kGpuBodyAliveFlag;
constexpr uint32_t kAwake = kGpuBodyAwakeFlag;

struct alignas(16) TestPose {
    glm::vec4 positionInvMass{0.0f};
    glm::vec4 orientation{0.0f, 0.0f, 0.0f, 1.0f};
};
struct alignas(16) TestShape {
    glm::vec4 dimensionsType{0.0f};
    glm::vec4 properties{0.0f};
};
using TestMetadata = std::array<uint32_t, 4>;

TestMetadata makeMetadata(uint32_t flags) {
    return {0u, 0u, 0u, packGpuBodyMetadata(1u, flags)};
}

struct PairKey {
    uint32_t minimum = 0;
    uint32_t maximum = 0;
    bool sleeping = false;
    auto operator<=>(const PairKey&) const = default;
};

struct BroadPhaseSnapshot {
    GpuBroadPhaseTelemetry telemetry;
    std::vector<GpuKeyValue> pairs;
    std::vector<GpuPersistentContact> contacts;
    std::vector<GpuContactEvent> begins;
    std::vector<GpuContactEvent> ends;
};

template <typename T>
WGPUBuffer makeInput(gpu::Context& context, std::span<const T> values,
                     const char* label) {
    return gpu::createBufferWithData(
        context.getDevice(), context.getQueue(),
        gpu::BufferDesc::storage(values.size_bytes(), false, label), values);
}

void releaseBuffer(WGPUBuffer& buffer) {
    if (buffer) {
        wgpuBufferDestroy(buffer);
        wgpuBufferRelease(buffer);
        buffer = nullptr;
    }
}

std::vector<PairKey> bruteForcePairs(
    const std::vector<TestPose>& poses,
    const std::vector<TestShape>& shapes,
    const std::vector<TestMetadata>& metadata, float margin) {
    std::vector<PairKey> result;
    for (uint32_t first = 0; first < poses.size(); ++first) {
        if ((metadata[first][3] & kAlive) == 0u
            || glm::compMax(glm::abs(glm::vec3(shapes[first].dimensionsType)))
                <= 0.0f) continue;
        const float firstRadius = 0.5f
            * glm::length(glm::vec3(shapes[first].dimensionsType)) + margin;
        for (uint32_t second = first + 1u; second < poses.size(); ++second) {
            if ((metadata[second][3] & kAlive) == 0u
                || glm::compMax(glm::abs(glm::vec3(shapes[second].dimensionsType)))
                    <= 0.0f) continue;
            if ((metadata[first][3] & kAwake) == 0u
                && (metadata[second][3] & kAwake) == 0u) continue;
            const float secondRadius = 0.5f
                * glm::length(glm::vec3(shapes[second].dimensionsType)) + margin;
            const glm::vec3 delta = glm::abs(
                glm::vec3(poses[first].positionInvMass)
                - glm::vec3(poses[second].positionInvMass));
            if (glm::all(glm::lessThanEqual(
                    delta, glm::vec3(firstRadius + secondRadius)))) {
                result.push_back({
                    first, second,
                    (metadata[first][3] & kAwake) == 0u
                        || (metadata[second][3] & kAwake) == 0u});
            }
        }
    }
    return result;
}

BroadPhaseSnapshot runAndRead(gpu::Context& context, GpuBroadPhase& broadPhase) {
    WGPUCommandEncoderDescriptor encoderDesc{};
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(
        context.getDevice(), &encoderDesc);
    EXPECT_TRUE(broadPhase.encode(encoder));

    const size_t telemetryBytes = 32u * sizeof(uint32_t);
    const size_t pairBytes = size_t{broadPhase.pairCapacity()} * sizeof(GpuKeyValue);
    const size_t contactBytes = size_t{broadPhase.contactCapacity()}
                              * sizeof(GpuPersistentContact);
    const size_t eventBytes = size_t{broadPhase.contactCapacity()} * 2u
                            * sizeof(GpuContactEvent);
    const size_t totalBytes = telemetryBytes + pairBytes + contactBytes + eventBytes;
    WGPUBuffer readback = gpu::createBuffer(
        context.getDevice(), gpu::BufferDesc{
            .label = "broad_phase_readback",
            .size = totalBytes,
            .usage = WGPUBufferUsage_CopyDst | WGPUBufferUsage_MapRead,
        });
    EXPECT_NE(readback, nullptr);
    if (!readback) return {};
    size_t offset = 0;
    wgpuCommandEncoderCopyBufferToBuffer(
        encoder, broadPhase.telemetryBuffer(), 0, readback, offset, telemetryBytes);
    offset += telemetryBytes;
    wgpuCommandEncoderCopyBufferToBuffer(
        encoder, broadPhase.uniquePairs(), 0, readback, offset, pairBytes);
    offset += pairBytes;
    wgpuCommandEncoderCopyBufferToBuffer(
        encoder, broadPhase.persistentContacts(), 0, readback, offset, contactBytes);
    offset += contactBytes;
    wgpuCommandEncoderCopyBufferToBuffer(
        encoder, broadPhase.contactEvents(), 0, readback, offset, eventBytes);

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
    wgpuBufferMapAsync(readback, WGPUMapMode_Read, 0, totalBytes,
                       callback, &mapState);
    while (!mapState.done) {
        static_cast<void>(wgpuDevicePoll(
            context.getDevice(), true, &submission));
    }
    EXPECT_TRUE(mapState.success);
    BroadPhaseSnapshot snapshot;
    if (mapState.success) {
        const auto* bytes = static_cast<const std::byte*>(
            wgpuBufferGetConstMappedRange(readback, 0, totalBytes));
        std::array<uint32_t, 32> telemetry{};
        std::memcpy(telemetry.data(), bytes, telemetryBytes);
        snapshot.telemetry = GpuBroadPhase::decodeTelemetry(telemetry);
        offset = telemetryBytes;
        snapshot.pairs.resize(std::min(
            snapshot.telemetry.uniquePairs, broadPhase.pairCapacity()));
        std::memcpy(snapshot.pairs.data(), bytes + offset,
                    snapshot.pairs.size() * sizeof(GpuKeyValue));
        offset += pairBytes;
        snapshot.contacts.resize(snapshot.telemetry.persistentContacts);
        std::memcpy(snapshot.contacts.data(), bytes + offset,
                    snapshot.contacts.size() * sizeof(GpuPersistentContact));
        offset += contactBytes;
        const auto* events = reinterpret_cast<const GpuContactEvent*>(bytes + offset);
        snapshot.begins.assign(events,
            events + std::min(snapshot.telemetry.beginEvents,
                              broadPhase.contactCapacity()));
        snapshot.ends.assign(
            events + broadPhase.contactCapacity(),
            events + broadPhase.contactCapacity()
                + std::min(snapshot.telemetry.endEvents,
                           broadPhase.contactCapacity()));
    }
    wgpuBufferUnmap(readback);
    wgpuBufferDestroy(readback);
    wgpuBufferRelease(readback);
    wgpuCommandBufferRelease(command);
    wgpuCommandEncoderRelease(encoder);
    return snapshot;
}

std::vector<PairKey> snapshotPairs(const BroadPhaseSnapshot& snapshot) {
    std::vector<PairKey> result;
    result.reserve(snapshot.pairs.size());
    for (const auto& pair : snapshot.pairs) {
        result.push_back({pair.keyHigh, pair.keyLow, pair.value != 0u});
    }
    return result;
}

std::map<std::pair<uint32_t, uint32_t>, uint32_t> contactIds(
    const BroadPhaseSnapshot& snapshot) {
    std::map<std::pair<uint32_t, uint32_t>, uint32_t> result;
    for (const auto& contact : snapshot.contacts) {
        result[{contact.pair.keyHigh, contact.pair.keyLow}] = contact.state[0];
    }
    return result;
}

class GpuBroadPhaseTest : public ::testing::TestWithParam<uint32_t> {};

TEST_P(GpuBroadPhaseTest, MatchesBruteForceAndPersistsLifecycle) {
    // Exercise the cooperative pair enumerator's inclusive upper boundary.
    constexpr uint32_t bodyCapacity = 256;
    constexpr float margin = 0.02f;
    gpu::Context context;
    gpu::ContextConfig contextConfig;
    contextConfig.enableValidation = false;
    if (!context.initHeadless(contextConfig)) {
        GTEST_SKIP() << "Headless WebGPU is unavailable";
    }

    std::vector<TestPose> poses(bodyCapacity);
    std::vector<TestShape> shapes(bodyCapacity);
    std::vector<TestMetadata> metadata(bodyCapacity);
    std::mt19937 random(0xb04d'faceu);
    std::uniform_real_distribution<float> position(-12.0f, 12.0f);
    std::uniform_real_distribution<float> dimension(0.4f, 1.8f);
    for (uint32_t body = 1; body < bodyCapacity - 1u; ++body) {
        poses[body].positionInvMass = {
            position(random), position(random), position(random), 1.0f};
        shapes[body].dimensionsType = {
            dimension(random), dimension(random), dimension(random), 0.0f};
        metadata[body] = makeMetadata(
            kAlive | (body % 7u == 0u ? 0u : kAwake));
    }
    poses[1].positionInvMass = {7.0f, 7.0f, 7.0f, 1.0f};
    shapes[1].dimensionsType = {12.0f, 8.0f, 10.0f, 2.0f};
    poses[2].positionInvMass = {0.0f, 0.0f, 0.0f, 1.0f};
    poses[3].positionInvMass = {0.5f, 0.0f, 0.0f, 1.0f};
    poses[4].positionInvMass = {0.7f, 0.0f, 0.0f, 1.0f};
    for (uint32_t body : {2u, 3u, 4u}) {
        shapes[body].dimensionsType = {1.0f, 1.0f, 1.0f, 1.0f};
        metadata[body] = makeMetadata(
            kAlive | (body == 2u ? kAwake : 0u));
    }

    WGPUBuffer poseBuffer = makeInput<TestPose>(context, poses, "broad_poses");
    WGPUBuffer shapeBuffer = makeInput<TestShape>(context, shapes, "broad_shapes");
    WGPUBuffer metadataBuffer = makeInput<TestMetadata>(
        context, metadata, "broad_metadata");
    ASSERT_NE(poseBuffer, nullptr);
    ASSERT_NE(shapeBuffer, nullptr);
    ASSERT_NE(metadataBuffer, nullptr);

    GpuBroadPhase broadPhase;
    GpuBroadPhase::Config config;
    config.bodyCapacity = bodyCapacity;
    config.candidatePairCapacity = 65'536;
    config.pairCapacity = 8'192;
    config.contactCapacity = 8'192;
    config.cellSize = 4.0f;
    config.speculativeMargin = margin;
    config.workgroupSize = GetParam();
    ASSERT_TRUE(broadPhase.initialize(
        context.getDevice(), context.getQueue(), config));
    broadPhase.setBodyView({poseBuffer, shapeBuffer, metadataBuffer, bodyCapacity});

    const auto expectedFirst = bruteForcePairs(poses, shapes, metadata, margin);
    const BroadPhaseSnapshot first = runAndRead(context, broadPhase);
    EXPECT_EQ(snapshotPairs(first), expectedFirst);
    EXPECT_EQ(first.telemetry.uniquePairs, expectedFirst.size());
    EXPECT_EQ(first.telemetry.persistentContacts, expectedFirst.size());
    EXPECT_EQ(first.telemetry.beginEvents, expectedFirst.size());
    EXPECT_EQ(first.telemetry.endEvents, 0u);
    EXPECT_EQ(first.telemetry.oversizedBodies, 1u);
    EXPECT_GT(first.telemetry.occupiedCells, 0u);
    EXPECT_FALSE(first.telemetry.candidateOverflow);
    EXPECT_FALSE(first.telemetry.pairOverflow);
    EXPECT_FALSE(first.telemetry.contactOverflow);
    const auto firstIds = contactIds(first);
    std::set<uint32_t> uniqueIds;
    for (const auto& [pair, id] : firstIds) {
        static_cast<void>(pair);
        EXPECT_LT(id, config.contactCapacity);
        uniqueIds.insert(id);
    }
    EXPECT_EQ(uniqueIds.size(), firstIds.size());

    const BroadPhaseSnapshot second = runAndRead(context, broadPhase);
    EXPECT_EQ(snapshotPairs(second), expectedFirst);
    EXPECT_EQ(second.telemetry.beginEvents, 0u);
    EXPECT_EQ(second.telemetry.endEvents, 0u);
    EXPECT_EQ(contactIds(second), firstIds);
    for (const auto& contact : second.contacts) EXPECT_EQ(contact.state[1], 1u);

    poses[2].positionInvMass = {40.0f, 40.0f, 40.0f, 1.0f};
    metadata[3][3] |= kAwake;
    metadata[10][3] = 0u;
    poses.back().positionInvMass = {0.2f, 0.2f, 0.2f, 1.0f};
    shapes.back().dimensionsType = {1.2f, 1.2f, 1.2f, 0.0f};
    metadata.back() = makeMetadata(kAlive | kAwake);
    gpu::writeBuffer(context.getQueue(), poseBuffer, 0,
                     std::as_bytes(std::span<const TestPose>(poses)));
    gpu::writeBuffer(context.getQueue(), metadataBuffer, 0,
                     std::as_bytes(std::span<const TestMetadata>(metadata)));
    gpu::writeBuffer(context.getQueue(), shapeBuffer, 0,
                     std::as_bytes(std::span<const TestShape>(shapes)));
    const auto expectedThird = bruteForcePairs(poses, shapes, metadata, margin);
    const BroadPhaseSnapshot third = runAndRead(context, broadPhase);
    EXPECT_EQ(snapshotPairs(third), expectedThird);
    EXPECT_EQ(third.telemetry.tick, 3u);

    std::set<std::pair<uint32_t, uint32_t>> firstSet;
    std::set<std::pair<uint32_t, uint32_t>> thirdSet;
    for (const auto& pair : expectedFirst) firstSet.emplace(pair.minimum, pair.maximum);
    for (const auto& pair : expectedThird) thirdSet.emplace(pair.minimum, pair.maximum);
    std::vector<std::pair<uint32_t, uint32_t>> expectedBegins;
    std::vector<std::pair<uint32_t, uint32_t>> expectedEnds;
    std::set_difference(thirdSet.begin(), thirdSet.end(),
                        firstSet.begin(), firstSet.end(),
                        std::back_inserter(expectedBegins));
    std::set_difference(firstSet.begin(), firstSet.end(),
                        thirdSet.begin(), thirdSet.end(),
                        std::back_inserter(expectedEnds));
    std::vector<std::pair<uint32_t, uint32_t>> actualBegins;
    std::vector<std::pair<uint32_t, uint32_t>> actualEnds;
    for (const auto& event : third.begins) {
        EXPECT_EQ(event.type, static_cast<uint32_t>(ContactEventType::Begin));
        actualBegins.emplace_back(event.pairHigh, event.pairLow);
    }
    for (const auto& event : third.ends) {
        EXPECT_EQ(event.type, static_cast<uint32_t>(ContactEventType::End));
        actualEnds.emplace_back(event.pairHigh, event.pairLow);
    }
    EXPECT_EQ(actualBegins, expectedBegins);
    EXPECT_EQ(actualEnds, expectedEnds);
    const auto thirdIds = contactIds(third);
    for (const auto& [pair, id] : firstIds) {
        if (thirdSet.contains(pair)) {
            EXPECT_EQ(thirdIds.at(pair), id);
        }
    }

    releaseBuffer(metadataBuffer);
    releaseBuffer(shapeBuffer);
    releaseBuffer(poseBuffer);
}

TEST_P(GpuBroadPhaseTest, DenseMediumWorldMatchesBruteForce) {
    constexpr uint32_t bodyCapacity = 65;
    constexpr uint32_t pairCapacity = 4'096;
    constexpr float margin = 0.02f;
    gpu::Context context;
    gpu::ContextConfig contextConfig;
    contextConfig.enableValidation = false;
    if (!context.initHeadless(contextConfig)) {
        GTEST_SKIP() << "Headless WebGPU is unavailable";
    }

    std::vector<TestPose> poses(bodyCapacity);
    std::vector<TestShape> shapes(bodyCapacity);
    std::vector<TestMetadata> metadata(bodyCapacity);
    for (uint32_t body = 0; body < bodyCapacity; ++body) {
        poses[body].positionInvMass = {0.0f, 0.0f, 0.0f, 1.0f};
        shapes[body].dimensionsType = {1.0f, 1.0f, 1.0f, 1.0f};
        metadata[body] = makeMetadata(kAlive | kAwake);
    }

    WGPUBuffer poseBuffer = makeInput<TestPose>(
        context, poses, "dense_medium_poses");
    WGPUBuffer shapeBuffer = makeInput<TestShape>(
        context, shapes, "dense_medium_shapes");
    WGPUBuffer metadataBuffer = makeInput<TestMetadata>(
        context, metadata, "dense_medium_metadata");
    ASSERT_NE(poseBuffer, nullptr);
    ASSERT_NE(shapeBuffer, nullptr);
    ASSERT_NE(metadataBuffer, nullptr);

    GpuBroadPhase broadPhase;
    GpuBroadPhase::Config config;
    config.bodyCapacity = bodyCapacity;
    config.candidatePairCapacity = pairCapacity;
    config.pairCapacity = pairCapacity;
    config.contactCapacity = pairCapacity;
    config.cellSize = 4.0f;
    config.speculativeMargin = margin;
    config.workgroupSize = GetParam();
    ASSERT_TRUE(broadPhase.initialize(
        context.getDevice(), context.getQueue(), config));
    broadPhase.setBodyView({
        poseBuffer, shapeBuffer, metadataBuffer, bodyCapacity});

    const auto expected = bruteForcePairs(poses, shapes, metadata, margin);
    ASSERT_GT(expected.size(), 1'024u);
    const BroadPhaseSnapshot first = runAndRead(context, broadPhase);
    EXPECT_EQ(snapshotPairs(first), expected);
    EXPECT_EQ(first.telemetry.uniquePairs, expected.size());
    EXPECT_EQ(first.telemetry.persistentContacts, expected.size());
    EXPECT_EQ(first.telemetry.beginEvents, expected.size());
    EXPECT_FALSE(first.telemetry.candidateOverflow);
    EXPECT_FALSE(first.telemetry.pairOverflow);
    EXPECT_FALSE(first.telemetry.contactOverflow);

    const auto firstIds = contactIds(first);
    const BroadPhaseSnapshot second = runAndRead(context, broadPhase);
    EXPECT_EQ(snapshotPairs(second), expected);
    EXPECT_EQ(contactIds(second), firstIds);
    EXPECT_EQ(second.telemetry.beginEvents, 0u);
    EXPECT_EQ(second.telemetry.endEvents, 0u);

    releaseBuffer(metadataBuffer);
    releaseBuffer(shapeBuffer);
    releaseBuffer(poseBuffer);
}

TEST_P(GpuBroadPhaseTest, CooperativeUpperBoundaryKeepsCanonicalPairs) {
    constexpr uint32_t bodyCapacity = 1'024;
    constexpr uint32_t pairCapacity = 4'096;
    constexpr float margin = 0.02f;
    gpu::Context context;
    gpu::ContextConfig contextConfig;
    contextConfig.enableValidation = false;
    if (!context.initHeadless(contextConfig)) {
        GTEST_SKIP() << "Headless WebGPU is unavailable";
    }

    std::vector<TestPose> poses(bodyCapacity);
    std::vector<TestShape> shapes(bodyCapacity);
    std::vector<TestMetadata> metadata(bodyCapacity);
    for (uint32_t body = 0; body < bodyCapacity; ++body) {
        poses[body].positionInvMass = {
            static_cast<float>(body) * 10.0f, 0.0f, 0.0f, 1.0f};
        shapes[body].dimensionsType = {1.0f, 1.0f, 1.0f, 1.0f};
        metadata[body] = makeMetadata(kAlive | kAwake);
    }
    poses[700].positionInvMass = poses[10].positionInvMass;
    poses[256].positionInvMass = poses[255].positionInvMass;
    poses[1'023].positionInvMass = poses[512].positionInvMass;

    WGPUBuffer poseBuffer = makeInput<TestPose>(
        context, poses, "cooperative_boundary_poses");
    WGPUBuffer shapeBuffer = makeInput<TestShape>(
        context, shapes, "cooperative_boundary_shapes");
    WGPUBuffer metadataBuffer = makeInput<TestMetadata>(
        context, metadata, "cooperative_boundary_metadata");
    ASSERT_NE(poseBuffer, nullptr);
    ASSERT_NE(shapeBuffer, nullptr);
    ASSERT_NE(metadataBuffer, nullptr);

    GpuBroadPhase broadPhase;
    GpuBroadPhase::Config config;
    config.bodyCapacity = bodyCapacity;
    config.candidatePairCapacity = pairCapacity;
    config.pairCapacity = pairCapacity;
    config.contactCapacity = pairCapacity;
    config.cellSize = 4.0f;
    config.speculativeMargin = margin;
    config.workgroupSize = GetParam();
    ASSERT_TRUE(broadPhase.initialize(
        context.getDevice(), context.getQueue(), config));
    broadPhase.setBodyView({
        poseBuffer, shapeBuffer, metadataBuffer, bodyCapacity});

    const std::vector<PairKey> expected = {
        {10u, 700u, false},
        {255u, 256u, false},
        {512u, 1'023u, false},
    };
    const BroadPhaseSnapshot first = runAndRead(context, broadPhase);
    EXPECT_EQ(snapshotPairs(first), expected);
    EXPECT_EQ(first.telemetry.gridEntries, bodyCapacity);
    EXPECT_EQ(first.telemetry.occupiedCells, bodyCapacity - expected.size());
    EXPECT_EQ(first.telemetry.oversizedBodies, 0u);
    EXPECT_EQ(first.telemetry.persistentContacts, expected.size());
    EXPECT_EQ(first.telemetry.beginEvents, expected.size());
    EXPECT_FALSE(first.telemetry.candidateOverflow);
    EXPECT_FALSE(first.telemetry.pairOverflow);
    EXPECT_FALSE(first.telemetry.contactOverflow);

    const auto firstIds = contactIds(first);
    const BroadPhaseSnapshot second = runAndRead(context, broadPhase);
    EXPECT_EQ(snapshotPairs(second), expected);
    EXPECT_EQ(contactIds(second), firstIds);
    EXPECT_EQ(second.telemetry.beginEvents, 0u);
    EXPECT_EQ(second.telemetry.endEvents, 0u);

    releaseBuffer(metadataBuffer);
    releaseBuffer(shapeBuffer);
    releaseBuffer(poseBuffer);
}

TEST(GpuBroadPhaseParallelMediumTest,
     KeepsCanonicalPairsAboveCooperativeLimit) {
    constexpr uint32_t bodyCapacity = 1'300;
    constexpr uint32_t pairCapacity = 4'096;
    constexpr float margin = 0.02f;
    gpu::Context context;
    gpu::ContextConfig contextConfig;
    contextConfig.enableValidation = false;
    if (!context.initHeadless(contextConfig)) {
        GTEST_SKIP() << "Headless WebGPU is unavailable";
    }

    std::vector<TestPose> poses(bodyCapacity);
    std::vector<TestShape> shapes(bodyCapacity);
    std::vector<TestMetadata> metadata(bodyCapacity);
    for (uint32_t body = 0; body < bodyCapacity; ++body) {
        poses[body].positionInvMass = {
            static_cast<float>(body) * 10.0f, 0.0f, 0.0f, 1.0f};
        shapes[body].dimensionsType = {1.0f, 1.0f, 1.0f, 1.0f};
        metadata[body] = makeMetadata(kAlive | kAwake);
    }
    poses[700].positionInvMass = poses[10].positionInvMass;
    poses[256].positionInvMass = poses[255].positionInvMass;
    poses[1'023].positionInvMass = poses[512].positionInvMass;
    poses[1'299].positionInvMass = poses[1'100].positionInvMass;

    WGPUBuffer poseBuffer = makeInput<TestPose>(
        context, poses, "parallel_medium_poses");
    WGPUBuffer shapeBuffer = makeInput<TestShape>(
        context, shapes, "parallel_medium_shapes");
    WGPUBuffer metadataBuffer = makeInput<TestMetadata>(
        context, metadata, "parallel_medium_metadata");
    ASSERT_NE(poseBuffer, nullptr);
    ASSERT_NE(shapeBuffer, nullptr);
    ASSERT_NE(metadataBuffer, nullptr);

    GpuBroadPhase broadPhase;
    GpuBroadPhase::Config config;
    config.bodyCapacity = bodyCapacity;
    config.candidatePairCapacity = pairCapacity;
    config.pairCapacity = pairCapacity;
    config.contactCapacity = pairCapacity;
    config.cellSize = 4.0f;
    config.speculativeMargin = margin;
    config.workgroupSize = 128u;
    ASSERT_TRUE(broadPhase.initialize(
        context.getDevice(), context.getQueue(), config));
    broadPhase.setBodyView({
        poseBuffer, shapeBuffer, metadataBuffer, bodyCapacity});

    const std::vector<PairKey> expected = {
        {10u, 700u, false},
        {255u, 256u, false},
        {512u, 1'023u, false},
        {1'100u, 1'299u, false},
    };
    const BroadPhaseSnapshot first = runAndRead(context, broadPhase);
    EXPECT_EQ(snapshotPairs(first), expected);
    EXPECT_EQ(first.telemetry.gridEntries, bodyCapacity);
    EXPECT_EQ(first.telemetry.occupiedCells, bodyCapacity - expected.size());
    EXPECT_EQ(first.telemetry.oversizedBodies, 0u);
    EXPECT_EQ(first.telemetry.persistentContacts, expected.size());
    EXPECT_EQ(first.telemetry.beginEvents, expected.size());
    EXPECT_FALSE(first.telemetry.candidateOverflow);
    EXPECT_FALSE(first.telemetry.pairOverflow);
    EXPECT_FALSE(first.telemetry.contactOverflow);

    const auto firstIds = contactIds(first);
    const BroadPhaseSnapshot second = runAndRead(context, broadPhase);
    EXPECT_EQ(snapshotPairs(second), expected);
    EXPECT_EQ(contactIds(second), firstIds);
    EXPECT_EQ(second.telemetry.beginEvents, 0u);
    EXPECT_EQ(second.telemetry.endEvents, 0u);

    releaseBuffer(metadataBuffer);
    releaseBuffer(shapeBuffer);
    releaseBuffer(poseBuffer);
}

INSTANTIATE_TEST_SUITE_P(
    TuningProfiles, GpuBroadPhaseTest,
    ::testing::Values(64u, 128u, 256u));

TEST(GpuBroadPhaseSectorTest, RejectsCellSizeThatDoesNotDivideSector) {
    gpu::Context context;
    gpu::ContextConfig contextConfig;
    contextConfig.enableValidation = false;
    if (!context.initHeadless(contextConfig)) {
        GTEST_SKIP() << "Headless WebGPU is unavailable";
    }
    GpuBroadPhase broadPhase;
    GpuBroadPhase::Config config;
    config.bodyCapacity = 8;
    config.candidatePairCapacity = 8;
    config.pairCapacity = 8;
    config.contactCapacity = 8;
    config.cellSize = 3.0f;
    EXPECT_FALSE(broadPhase.initialize(
        context.getDevice(), context.getQueue(), config));
}

TEST(GpuBroadPhaseSectorTest,
     WrapsToroidalNeighborsAndRejectsDistantKeyAliases) {
    constexpr uint32_t bodyCapacity = 3;
    gpu::Context context;
    gpu::ContextConfig contextConfig;
    contextConfig.enableValidation = false;
    if (!context.initHeadless(contextConfig)) {
        GTEST_SKIP() << "Headless WebGPU is unavailable";
    }

    std::vector<TestPose> poses(bodyCapacity);
    std::vector<TestShape> shapes(bodyCapacity);
    std::vector<TestMetadata> metadata(bodyCapacity);
    poses[0].positionInvMass = {-0.4f, 0.0f, 0.0f, 1.0f};
    poses[1].positionInvMass = {0.4f, 0.0f, 0.0f, 1.0f};
    poses[2].positionInvMass = poses[1].positionInvMass;
    for (uint32_t body = 0; body < bodyCapacity; ++body) {
        shapes[body].dimensionsType = {0.5f, 0.5f, 0.5f, 0.0f};
        metadata[body] = makeMetadata(kAlive | kAwake);
    }
    // With 4 m cells, sector 16,384 starts exactly at the signed 21-bit key
    // wrap. Bodies 0 and 1 therefore occupy max-key and min-key neighbors.
    metadata[0][0] = 16'384u;
    metadata[1][0] = 16'384u;
    // 32,768 sectors is one complete key period. This body aliases body 1's
    // bucket but is not physically close and must never become a pair.
    metadata[2][0] = 16'384u + 32'768u;

    WGPUBuffer poseBuffer = makeInput<TestPose>(
        context, poses, "sector_wrap_poses");
    WGPUBuffer shapeBuffer = makeInput<TestShape>(
        context, shapes, "sector_wrap_shapes");
    WGPUBuffer metadataBuffer = makeInput<TestMetadata>(
        context, metadata, "sector_wrap_metadata");
    ASSERT_NE(poseBuffer, nullptr);
    ASSERT_NE(shapeBuffer, nullptr);
    ASSERT_NE(metadataBuffer, nullptr);

    GpuBroadPhase broadPhase;
    GpuBroadPhase::Config config;
    config.bodyCapacity = bodyCapacity;
    config.candidatePairCapacity = 16;
    config.pairCapacity = 8;
    config.contactCapacity = 8;
    config.cellSize = 4.0f;
    config.workgroupSize = 64;
    ASSERT_TRUE(broadPhase.initialize(
        context.getDevice(), context.getQueue(), config));
    broadPhase.setBodyView({
        poseBuffer, shapeBuffer, metadataBuffer, bodyCapacity});

    const BroadPhaseSnapshot snapshot = runAndRead(context, broadPhase);
    ASSERT_EQ(snapshot.pairs.size(), 1u);
    EXPECT_EQ(snapshot.pairs[0].keyHigh, 0u);
    EXPECT_EQ(snapshot.pairs[0].keyLow, 1u);
    EXPECT_EQ(snapshot.telemetry.gridEntries, bodyCapacity);
    EXPECT_EQ(snapshot.telemetry.oversizedBodies, 0u);
    EXPECT_FALSE(snapshot.telemetry.candidateOverflow);
    EXPECT_FALSE(snapshot.telemetry.pairOverflow);

    releaseBuffer(metadataBuffer);
    releaseBuffer(shapeBuffer);
    releaseBuffer(poseBuffer);
}

TEST(GpuBroadPhaseOverflowTest, ReportsAndRetainsCanonicalPrefix) {
    constexpr uint32_t bodyCapacity = 32;
    gpu::Context context;
    gpu::ContextConfig contextConfig;
    contextConfig.enableValidation = false;
    if (!context.initHeadless(contextConfig)) {
        GTEST_SKIP() << "Headless WebGPU is unavailable";
    }

    std::vector<TestPose> poses(bodyCapacity);
    std::vector<TestShape> shapes(bodyCapacity);
    std::vector<TestMetadata> metadata(bodyCapacity);
    for (uint32_t body = 0; body < bodyCapacity; ++body) {
        poses[body].positionInvMass = {1.0f, 1.0f, 1.0f, 1.0f};
        shapes[body].dimensionsType = {0.25f, 0.25f, 0.25f, 0.0f};
        metadata[body] = makeMetadata(kAlive | kAwake);
    }
    WGPUBuffer poseBuffer = makeInput<TestPose>(context, poses, "overflow_poses");
    WGPUBuffer shapeBuffer = makeInput<TestShape>(
        context, shapes, "overflow_shapes");
    WGPUBuffer metadataBuffer = makeInput<TestMetadata>(
        context, metadata, "overflow_metadata");
    ASSERT_NE(poseBuffer, nullptr);
    ASSERT_NE(shapeBuffer, nullptr);
    ASSERT_NE(metadataBuffer, nullptr);

    GpuBroadPhase broadPhase;
    GpuBroadPhase::Config config;
    config.bodyCapacity = bodyCapacity;
    config.candidatePairCapacity = 16;
    config.pairCapacity = 8;
    config.contactCapacity = 4;
    config.cellSize = 8.0f;
    config.workgroupSize = 64;
    ASSERT_TRUE(broadPhase.initialize(
        context.getDevice(), context.getQueue(), config));
    broadPhase.setBodyView({poseBuffer, shapeBuffer, metadataBuffer, bodyCapacity});

    const BroadPhaseSnapshot first = runAndRead(context, broadPhase);
    EXPECT_TRUE(first.telemetry.candidateOverflow);
    EXPECT_TRUE(first.telemetry.pairOverflow);
    EXPECT_TRUE(first.telemetry.contactOverflow);
    EXPECT_EQ(first.telemetry.candidatePairs,
              bodyCapacity * (bodyCapacity - 1u) / 2u);
    ASSERT_EQ(first.pairs.size(), config.pairCapacity);
    for (uint32_t index = 0; index < first.pairs.size(); ++index) {
        EXPECT_EQ(first.pairs[index].keyHigh, 0u);
        EXPECT_EQ(first.pairs[index].keyLow, index + 1u);
    }

    const auto firstIds = contactIds(first);
    const BroadPhaseSnapshot second = runAndRead(context, broadPhase);
    EXPECT_EQ(second.pairs, first.pairs);
    EXPECT_EQ(contactIds(second), firstIds);
    EXPECT_EQ(second.telemetry.beginEvents, 0u);
    EXPECT_EQ(second.telemetry.endEvents, 0u);
    EXPECT_GE(second.telemetry.highCandidatePairs,
              first.telemetry.candidatePairs);

    releaseBuffer(metadataBuffer);
    releaseBuffer(shapeBuffer);
    releaseBuffer(poseBuffer);
}

} // namespace
} // namespace voxy::physics
