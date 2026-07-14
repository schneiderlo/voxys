#include <gtest/gtest.h>

#include "physics/deterministic/fixed.hpp"
#include "physics/deterministic/replay.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <utility>
#include <vector>

namespace voxy::physics::deterministic {
namespace {

int32_t position(float value) {
    return Position::fromDouble(value).raw();
}

int32_t velocity(float value) {
    return LinearVelocity::fromDouble(value).raw();
}

LockstepBody makeBody(uint32_t id, float x, float radius) {
    LockstepBody result;
    result.identity = {
        id, 1u, LockstepBodyAlive | LockstepBodyAwake, 0u};
    result.sectorRadius = {0, 0, 0, position(radius)};
    result.positionInvMass = {
        position(x), position(1.0f), 0, velocity(1.0f)};
    return result;
}

CanonicalReplayCommand command(
    uint64_t tick, uint64_t sequence, uint32_t producer,
    ReplayCommandType type, uint32_t body, uint32_t generation) {
    CanonicalReplayCommand result;
    result.tick = tick;
    result.sequence = sequence;
    result.producer = producer;
    result.type = type;
    result.body = body;
    result.generation = generation;
    return result;
}

void appendHashes(ReplayRecorder& recorder, uint64_t tick,
                  const LockstepTelemetry& telemetry) {
    for (uint32_t body = 0; body < telemetry.hashes.bodies.size(); ++body) {
        recorder.recordHash({tick, ReplayHashStage::Body, body,
                             telemetry.hashes.bodies[body]});
    }
    for (uint32_t contact = 0; contact < telemetry.contacts; ++contact) {
        recorder.recordHash({tick, ReplayHashStage::Contact, contact,
                             telemetry.hashes.contacts[contact]});
    }
    for (uint32_t root = 0; root < telemetry.hashes.islands.size(); ++root) {
        if (telemetry.hashes.islands[root] == 0u) continue;
        recorder.recordHash({tick, ReplayHashStage::Island, root,
                             telemetry.hashes.islands[root]});
    }
    recorder.recordHash({tick, ReplayHashStage::World, 0u,
                         telemetry.hashes.world});
}

ReplayRecording makeRecording() {
    constexpr uint32_t bodyCapacity = 8;
    constexpr uint32_t contactCapacity = 16;
    ReplayRecording seed;
    seed.header.backendVersionHash = 0x1020304050607080ull;
    seed.header.shaderVersionHash = 0x8877665544332211ull;
    seed.header.terrainHash = 0xABCDEF01u;
    seed.header.generatorHash = 0x10293847u;
    seed.header.contentHash = 0x55667788u;
    seed.header.buildFingerprint = "lockstep-cpu-wgsl-test-v1";
    seed.header.capacity.residentBodies = bodyCapacity;
    seed.header.capacity.activeBodies = bodyCapacity;
    seed.header.capacity.commandsPerTick = 16;
    seed.header.capacity.contacts = contactCapacity;
    seed.header.capacity.manifolds = contactCapacity;
    seed.header.capacity.events = 32;
    seed.checkpoint.tick = 0;
    seed.checkpoint.randomState = 0x0123456789ABCDEFull;
    seed.checkpoint.deterministicWaterState = {1, -2, 3, -4};
    seed.checkpoint.bodies.resize(bodyCapacity);
    seed.checkpoint.bodies[1] = makeBody(1u, -0.35f, 0.5f);
    seed.checkpoint.bodies[2] = makeBody(2u, 0.35f, 0.5f);
    seed.checkpoint.contacts.push_back(LockstepContact{
        .ids = {1u, 2u, 0u, 1u},
        .normalPenetration = {
            kLockstepUnitOne, 0, 0, position(0.30f)},
    });
    seed.checkpoint.islandRoots.assign(
        bodyCapacity, std::numeric_limits<uint32_t>::max());
    seed.checkpoint.islandRoots[1] = 1u;
    seed.checkpoint.islandRoots[2] = 1u;
    seed.checkpoint.freeBodyIds = {0u, 3u, 4u, 5u, 6u, 7u};
    seed.checkpoint.manifoldWords = {1u, 2u, 3u, 4u};
    seed.checkpoint.graphColors = {0u};
    seed.checkpoint.sleepCounters.assign(bodyCapacity, 0u);

    std::vector<CanonicalReplayCommand> commands;
    auto setVelocity = command(
        1u, 4u, 2u, ReplayCommandType::SetVelocity, 1u, 1u);
    setVelocity.payload[0] = velocity(0.75f);
    commands.push_back(setVelocity);

    auto impulse = command(
        2u, 9u, 1u, ReplayCommandType::ApplyImpulse, 2u, 1u);
    impulse.payload[1] = velocity(0.25f);
    commands.push_back(impulse);

    auto sleep = command(
        2u, 3u, 7u, ReplayCommandType::SetAwake, 1u, 1u);
    sleep.payload[0] = 0;
    commands.push_back(sleep);

    auto correction = command(
        3u, 8u, 3u, ReplayCommandType::Correction, 2u, 1u);
    correction.payload[0] = 0;
    correction.payload[1] = 0;
    correction.payload[2] = 0;
    correction.payload[4] = position(1.25f);
    correction.payload[5] = position(1.5f);
    correction.payload[6] = 0;
    correction.payload[8] = velocity(-0.5f);
    commands.push_back(correction);

    commands.push_back(command(
        4u, 1u, 0u, ReplayCommandType::DestroyBody, 2u, 1u));

    auto spawn = command(
        5u, 2u, 0u, ReplayCommandType::SpawnBody, 2u, 2u);
    spawn.payload = {
        0, 0, 0, position(0.4f),
        position(0.2f), position(2.0f), 0, velocity(1.0f),
        velocity(0.1f), 0, 0,
        static_cast<int32_t>(LockstepBodyAlive | LockstepBodyAwake)};
    commands.push_back(spawn);

    const auto initialBodies = seed.checkpoint.bodies;
    const auto checkpointContacts = seed.checkpoint.contacts;
    const auto checkpointRoots = seed.checkpoint.islandRoots;
    ReplayRecorder recorder(std::move(seed));
    for (auto iterator = commands.rbegin(); iterator != commands.rend(); ++iterator)
        recorder.record(*iterator);

    LockstepTelemetry checkpointTelemetry;
    checkpointTelemetry.tick = 0u;
    checkpointTelemetry.liveBodies = 2u;
    checkpointTelemetry.contacts = 1u;
    checkpointTelemetry.hashes = LockstepWorld::computeHashes(
        initialBodies, checkpointContacts, checkpointRoots,
        0u, contactCapacity);
    appendHashes(recorder, 0u, checkpointTelemetry);

    LockstepWorld world;
    LockstepWorld::Config config;
    config.bodyCapacity = bodyCapacity;
    config.contactCapacity = contactCapacity;
    EXPECT_TRUE(world.initialize(config));
    EXPECT_TRUE(world.setBodies(initialBodies));
    std::stable_sort(commands.begin(), commands.end(),
                     canonicalReplayCommandLess);
    size_t commandIndex = 0;
    for (uint32_t tick = 1; tick <= 5u; ++tick) {
        while (commandIndex < commands.size()
               && commands[commandIndex].tick == tick) {
            EXPECT_TRUE(applyCanonicalReplayCommand(
                world, commands[commandIndex]));
            ++commandIndex;
        }
        appendHashes(recorder, tick, world.step(tick));
    }
    return std::move(recorder).finish();
}

TEST(Replay, CanonicalCodecIsStableAcrossProducerInsertionOrder) {
    ReplayRecording recording = makeRecording();
    ReplayRecording shuffled = recording;
    std::reverse(shuffled.commands.begin(), shuffled.commands.end());
    std::reverse(shuffled.hashes.begin(), shuffled.hashes.end());

    const auto canonicalBytes = ReplayCodec::encode(recording);
    const auto shuffledBytes = ReplayCodec::encode(shuffled);
    EXPECT_EQ(canonicalBytes, shuffledBytes);

    const ReplayReadResult decoded = ReplayCodec::decode(canonicalBytes);
    ASSERT_TRUE(decoded.recording.has_value()) << decoded.error;
    EXPECT_TRUE(decoded.error.empty());
    EXPECT_EQ(decoded.recording->header.buildFingerprint,
              recording.header.buildFingerprint);
    EXPECT_EQ(decoded.recording->checkpoint.randomState,
              recording.checkpoint.randomState);
    EXPECT_EQ(decoded.recording->commands.size(), recording.commands.size());
    EXPECT_EQ(ReplayCodec::encode(*decoded.recording), canonicalBytes);
}

TEST(Replay, SelectedCorpusMatchesSchemaOneGoldenHashes) {
    const ReplayRecording recording = makeRecording();
    constexpr std::array<uint32_t, 6> expectedWorldHashes{
        606'370'524u,
        629'378'040u,
        3'369'838'235u,
        3'424'328'930u,
        1'820'688'115u,
        629'271'576u,
    };
    std::array<uint32_t, 6> actualWorldHashes{};
    for (const auto& hash : recording.hashes) {
        if (hash.stage == ReplayHashStage::World
            && hash.tick < actualWorldHashes.size()) {
            actualWorldHashes[hash.tick] = hash.hash;
        }
    }
    for (uint32_t tick = 0; tick < expectedWorldHashes.size(); ++tick) {
        EXPECT_EQ(actualWorldHashes[tick], expectedWorldHashes[tick])
            << "tick " << tick;
    }
    const auto bytes = ReplayCodec::encode(recording);
    EXPECT_EQ(bytes.size(), 2'921u);
    uint32_t storedChecksum = 0;
    for (uint32_t byte = 0; byte < 4u; ++byte) {
        storedChecksum |= std::to_integer<uint32_t>(
            bytes[bytes.size() - 4u + byte]) << (byte * 8u);
    }
    EXPECT_EQ(storedChecksum, 2'084'415'715u);
}

TEST(Replay, PlayerMatchesEveryHashAndReportsFirstDivergence) {
    const ReplayRecording recording = makeRecording();
    const ReplayPlayer::Result exact = ReplayPlayer{}.play(recording);
    EXPECT_TRUE(exact.completed);
    EXPECT_FALSE(exact.divergence.has_value());
    EXPECT_EQ(exact.finalTelemetry.tick, 5u);

    ReplayRecording changed = recording;
    auto mismatch = std::find_if(
        changed.hashes.rbegin(), changed.hashes.rend(),
        [](const ReplayHashRecord& hash) {
            return hash.stage == ReplayHashStage::World;
        });
    ASSERT_NE(mismatch, changed.hashes.rend());
    mismatch->hash ^= 1u;
    const ReplayPlayer::Result divergent = ReplayPlayer{}.play(changed);
    ASSERT_TRUE(divergent.divergence.has_value());
    EXPECT_FALSE(divergent.completed);
    EXPECT_EQ(divergent.divergence->tick, mismatch->tick);
    EXPECT_EQ(divergent.divergence->stage, ReplayHashStage::World);
    EXPECT_EQ(divergent.divergence->expected, mismatch->hash);
    EXPECT_NE(divergent.divergence->actual, mismatch->hash);
    EXPECT_NE(divergent.divergence->message.find("first replay divergence"),
              std::string::npos);
}

TEST(Replay, RejectsCorruptionAndStaleHandles) {
    auto bytes = ReplayCodec::encode(makeRecording());
    ASSERT_GT(bytes.size(), 32u);
    bytes[24] ^= std::byte{0x40};
    const ReplayReadResult corrupt = ReplayCodec::decode(bytes);
    EXPECT_FALSE(corrupt.recording.has_value());
    EXPECT_EQ(corrupt.error, "replay checksum mismatch");

    LockstepWorld world;
    LockstepWorld::Config config;
    config.bodyCapacity = 4;
    config.contactCapacity = 4;
    ASSERT_TRUE(world.initialize(config));
    std::array<LockstepBody, 4> bodies{};
    bodies[1] = makeBody(1u, 0.0f, 0.5f);
    ASSERT_TRUE(world.setBodies(bodies));
    auto stale = command(
        1u, 0u, 0u, ReplayCommandType::SetVelocity, 1u, 99u);
    stale.payload[0] = velocity(10.0f);
    EXPECT_FALSE(applyCanonicalReplayCommand(world, stale));
    EXPECT_EQ(world.bodies()[1].linearVelocity[0], 0);
}

} // namespace
} // namespace voxy::physics::deterministic
