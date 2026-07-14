#include "network/authoritative_sandbox.hpp"

#include <algorithm>
#include <array>
#include <limits>
#include <tuple>
#include <utility>

namespace voxy::network {
namespace {

using physics::deterministic::CanonicalReplayCommand;
using physics::deterministic::LockstepBody;
using physics::deterministic::LockstepBodyAlive;
using physics::deterministic::LockstepBodyAwake;
using physics::deterministic::LockstepBodyStatic;
using physics::deterministic::ReplayCommandType;

constexpr int32_t kMaximumInputQ16 = 65'536;
constexpr int32_t kMaximumImpulsePerTickQ16 = 16'384;

bool bodyAlive(const LockstepBody& body) noexcept {
    return (body.identity[2] & LockstepBodyAlive) != 0u;
}

int32_t clampInput(int32_t value) noexcept {
    return std::clamp(value, -kMaximumImpulsePerTickQ16,
                      kMaximumImpulsePerTickQ16);
}

bool inputInRange(int32_t value) noexcept {
    return value >= -kMaximumInputQ16 && value <= kMaximumInputQ16;
}

LockstepBody dynamicBody(uint32_t id, int32_t xQ12, int32_t yQ12,
                         int32_t radiusQ12) {
    LockstepBody body;
    body.identity = {id, 1u, LockstepBodyAlive | LockstepBodyAwake, 0u};
    body.sectorRadius = {0, 0, 0, radiusQ12};
    body.positionInvMass = {xQ12, yQ12, 0,
        physics::deterministic::kLockstepVelocityOne};
    return body;
}

LockstepBody staticBody(uint32_t id, int32_t xQ12, int32_t yQ12,
                        int32_t radiusQ12) {
    LockstepBody body = dynamicBody(id, xQ12, yQ12, radiusQ12);
    body.identity[2] |= LockstepBodyStatic;
    body.positionInvMass[3] = 0;
    return body;
}

} // namespace

TwoClientMeteorBoxSandbox::TwoClientMeteorBoxSandbox()
    : TwoClientMeteorBoxSandbox(Config{}) {}

TwoClientMeteorBoxSandbox::TwoClientMeteorBoxSandbox(Config config)
    : config_(config), snapshots_(std::max(config.historyTicks, 1u)) {}

bool TwoClientMeteorBoxSandbox::initialize() {
    initialized_ = false;
    if (config_.bodyCapacity < 8u || config_.contactCapacity == 0u
        || config_.historyTicks == 0u || config_.inputFutureWindow == 0u
        || config_.islandId == 0u || config_.authorityEpoch == 0u) return false;
    physics::deterministic::LockstepWorld::Config worldConfig;
    worldConfig.bodyCapacity = config_.bodyCapacity;
    worldConfig.contactCapacity = config_.contactCapacity;
    if (!world_.initialize(worldConfig)) return false;
    std::vector<LockstepBody> bodies(config_.bodyCapacity);
    bodies.at(1) = dynamicBody(
        1u, -2 * physics::deterministic::kLockstepPositionOne,
        2 * physics::deterministic::kLockstepPositionOne,
        physics::deterministic::kLockstepPositionOne / 2);
    bodies.at(2) = dynamicBody(
        2u, 2 * physics::deterministic::kLockstepPositionOne,
        2 * physics::deterministic::kLockstepPositionOne,
        physics::deterministic::kLockstepPositionOne / 2);
    // The current Lockstep corpus uses bounding spheres. Body 3 is the fixed
    // collision proxy for the visible sandbox box.
    bodies.at(3) = staticBody(
        3u, 0, 0, 3 * physics::deterministic::kLockstepPositionOne / 4);
    if (!world_.setBodies(bodies)) return false;
    authority_ = {};
    if (!authority_.assign(IslandAuthority{
            .islandId = config_.islandId,
            .epoch = config_.authorityEpoch,
            .workerId = 0u,
            .startTick = 0u,
            .checkpointHash = {},
        })) return false;
    snapshots_ = SnapshotHistory(config_.historyTicks);
    acknowledgements_ = {};
    clients_[0] = ClientState{1u, 1u, InputRedundancyBuffer(64)};
    clients_[1] = ClientState{2u, 2u, InputRedundancyBuffer(64)};
    pending_.clear();
    networkRecording_.clear();
    replayRecording_.clear();
    currentTick_ = 0;
    nextServerSequence_ = 1;
    telemetry_ = {};
    initialized_ = true;
    static_cast<void>(snapshots_.store(currentSnapshot()));
    return true;
}

TwoClientMeteorBoxSandbox::ClientState*
TwoClientMeteorBoxSandbox::client(uint32_t clientId) noexcept {
    auto iterator = std::find_if(clients_.begin(), clients_.end(),
        [clientId](const ClientState& value) {
            return value.clientId == clientId;
        });
    return iterator == clients_.end() ? nullptr : &*iterator;
}

const TwoClientMeteorBoxSandbox::ClientState*
TwoClientMeteorBoxSandbox::client(uint32_t clientId) const noexcept {
    auto iterator = std::find_if(clients_.begin(), clients_.end(),
        [clientId](const ClientState& value) {
            return value.clientId == clientId;
        });
    return iterator == clients_.end() ? nullptr : &*iterator;
}

uint32_t TwoClientMeteorBoxSandbox::controlledBody(
    uint32_t clientId) const noexcept {
    const ClientState* value = client(clientId);
    return value != nullptr ? value->controlledBody : 0u;
}

bool TwoClientMeteorBoxSandbox::submitInputs(
    uint32_t clientId, uint64_t islandId, uint32_t authorityEpoch,
    std::span<const InputFrame> redundantFrames) {
    if (!initialized_) return false;
    ClientState* state = client(clientId);
    if (state == nullptr) {
        telemetry_.rejectedCommands += redundantFrames.size();
        return false;
    }
    if (islandId != config_.islandId
        || !authority_.accepts(islandId, authorityEpoch, currentTick_ + 1u)) {
        telemetry_.staleEpochCommands += redundantFrames.size();
        telemetry_.rejectedCommands += redundantFrames.size();
        return false;
    }
    std::vector<InputFrame> valid;
    bool allValid = true;
    for (const auto& frame : redundantFrames) {
        if (frame.tick <= currentTick_) {
            ++telemetry_.duplicateInputs;
            continue;
        }
        if (frame.tick > currentTick_ + config_.inputFutureWindow
            || !inputInRange(frame.moveXQ16)
            || !inputInRange(frame.moveZQ16)
            || !inputInRange(frame.lookXQ16)
            || !inputInRange(frame.lookYQ16)) {
            ++telemetry_.rejectedCommands;
            allValid = false;
            continue;
        }
        valid.push_back(frame);
    }
    const auto accepted = state->receivedInputs.ingest(valid);
    telemetry_.duplicateInputs += valid.size() - accepted.size();
    for (const auto& frame : accepted) {
        CanonicalNetworkCommand command;
        command.tick = frame.tick;
        command.sequence = frame.sequence;
        command.islandId = islandId;
        command.authorityEpoch = authorityEpoch;
        command.clientId = clientId;
        command.type = (frame.buttons & 1u) != 0u
            ? NetworkCommandType::FireMeteorRequest
            : NetworkCommandType::MoveInput;
        command.body = state->controlledBody;
        command.generation = world_.bodies()[command.body].identity[1];
        command.payload[0] = frame.moveXQ16;
        command.payload[2] = frame.moveZQ16;
        pending_.push_back(command);
    }
    return allValid;
}

bool TwoClientMeteorBoxSandbox::validateClientCommand(
    const CanonicalNetworkCommand& command) const noexcept {
    const ClientState* state = client(command.clientId);
    if (state == nullptr || !isClientCommandAllowed(command.type)
        || (command.flags & NetworkCommandServerIssued) != 0u
        || command.islandId != config_.islandId
        || !authority_.accepts(command.islandId, command.authorityEpoch,
                               command.tick)
        || command.tick <= currentTick_
        || command.tick > currentTick_ + config_.inputFutureWindow
        || command.body != state->controlledBody
        || command.body >= world_.bodies().size()
        || command.generation != world_.bodies()[command.body].identity[1]) {
        return false;
    }
    if (command.type == NetworkCommandType::MoveInput
        || command.type == NetworkCommandType::ApplyImpulseRequest) {
        return inputInRange(command.payload[0])
            && inputInRange(command.payload[1])
            && inputInRange(command.payload[2]);
    }
    return true;
}

bool TwoClientMeteorBoxSandbox::submitCommand(
    const CanonicalNetworkCommand& command) {
    if (!initialized_ || !validateClientCommand(command)) {
        ++telemetry_.rejectedCommands;
        if (command.islandId == config_.islandId
            && command.authorityEpoch != config_.authorityEpoch)
            ++telemetry_.staleEpochCommands;
        return false;
    }
    pending_.push_back(command);
    return true;
}

bool TwoClientMeteorBoxSandbox::queueCorrection(
    uint32_t bodyId, const LockstepBody& authoritativeBody) {
    if (!initialized_ || bodyId == 0u || bodyId >= world_.bodies().size()
        || authoritativeBody.identity[0] != bodyId
        || !bodyAlive(authoritativeBody)) return false;
    CanonicalNetworkCommand correction;
    correction.tick = currentTick_ + 1u;
    correction.sequence = nextServerSequence_++;
    correction.islandId = config_.islandId;
    correction.authorityEpoch = config_.authorityEpoch;
    correction.type = NetworkCommandType::Correction;
    correction.body = bodyId;
    correction.generation = authoritativeBody.identity[1];
    correction.flags = NetworkCommandServerIssued
        | NetworkCommandCorrectionEvent;
    correction.payload[0] = authoritativeBody.sectorRadius[0];
    correction.payload[1] = authoritativeBody.sectorRadius[1];
    correction.payload[2] = authoritativeBody.sectorRadius[2];
    correction.payload[4] = authoritativeBody.positionInvMass[0];
    correction.payload[5] = authoritativeBody.positionInvMass[1];
    correction.payload[6] = authoritativeBody.positionInvMass[2];
    correction.payload[8] = authoritativeBody.linearVelocity[0];
    correction.payload[9] = authoritativeBody.linearVelocity[1];
    correction.payload[10] = authoritativeBody.linearVelocity[2];
    pending_.push_back(correction);
    return true;
}

uint32_t TwoClientMeteorBoxSandbox::allocateBodyId() const noexcept {
    for (uint32_t body = 4u; body < world_.bodies().size(); ++body) {
        if (!bodyAlive(world_.bodies()[body])) return body;
    }
    return 0u;
}

void TwoClientMeteorBoxSandbox::recordReplay(
    const CanonicalReplayCommand& command) {
    auto iterator = std::upper_bound(
        replayRecording_.begin(), replayRecording_.end(), command,
        [](const auto& value, const auto& existing) {
            return physics::deterministic::canonicalReplayCommandLess(
                value, existing);
        });
    replayRecording_.insert(iterator, command);
}

void TwoClientMeteorBoxSandbox::recordNetwork(
    const CanonicalNetworkCommand& command) {
    auto iterator = std::upper_bound(
        networkRecording_.begin(), networkRecording_.end(), command,
        [](const auto& value, const auto& existing) {
            return canonicalNetworkCommandLess(value, existing);
        });
    networkRecording_.insert(iterator, command);
}

bool TwoClientMeteorBoxSandbox::execute(CanonicalNetworkCommand command) {
    command.flags |= NetworkCommandServerIssued;
    command.sequence = nextServerSequence_++;
    recordNetwork(command);
    ++telemetry_.acceptedCommands;

    if (command.type == NetworkCommandType::FireMeteorRequest) {
        const uint32_t bodyId = allocateBodyId();
        if (bodyId == 0u) {
            ++telemetry_.rejectedCommands;
            return false;
        }
        const auto& owner = world_.bodies()[command.body];
        const uint32_t generation = world_.bodies()[bodyId].identity[1] == 0u
            ? 1u : world_.bodies()[bodyId].identity[1];
        CanonicalNetworkCommand spawn;
        spawn.tick = command.tick;
        spawn.sequence = nextServerSequence_++;
        spawn.islandId = config_.islandId;
        spawn.authorityEpoch = config_.authorityEpoch;
        spawn.clientId = command.clientId;
        spawn.type = NetworkCommandType::SpawnBody;
        spawn.body = bodyId;
        spawn.generation = generation;
        spawn.flags = NetworkCommandServerIssued;
        spawn.payload = {
            owner.sectorRadius[0], owner.sectorRadius[1], owner.sectorRadius[2],
            physics::deterministic::kLockstepPositionOne / 4,
            owner.positionInvMass[0],
            owner.positionInvMass[1] + physics::deterministic::kLockstepPositionOne,
            owner.positionInvMass[2],
            physics::deterministic::kLockstepVelocityOne,
            command.clientId == 1u ? 32'768 : -32'768,
            32'768,
            0,
            static_cast<int32_t>(LockstepBodyAlive | LockstepBodyAwake),
        };
        recordNetwork(spawn);
        const auto replay = toReplayCommand(spawn);
        if (!replay.has_value()
            || !physics::deterministic::applyCanonicalReplayCommand(
                world_, *replay)) return false;
        recordReplay(*replay);
        ++telemetry_.spawnedMeteors;
        return true;
    }

    auto replay = toReplayCommand(command);
    if (!replay.has_value()) return false;
    if (command.type == NetworkCommandType::MoveInput
        || command.type == NetworkCommandType::ApplyImpulseRequest) {
        replay->type = ReplayCommandType::ApplyImpulse;
        replay->payload[0] = clampInput(command.payload[0]);
        replay->payload[1] = clampInput(command.payload[1]);
        replay->payload[2] = clampInput(command.payload[2]);
    }
    const bool applied = physics::deterministic::applyCanonicalReplayCommand(
        world_, *replay);
    recordReplay(*replay);
    if (command.type == NetworkCommandType::Correction) {
        ++telemetry_.correctionEvents;
    }
    return applied;
}

bool TwoClientMeteorBoxSandbox::step() {
    if (!initialized_ || currentTick_ == std::numeric_limits<uint32_t>::max())
        return false;
    const uint64_t tick = currentTick_ + 1u;
    std::stable_sort(pending_.begin(), pending_.end(),
                     canonicalNetworkCommandLess);
    std::vector<CanonicalNetworkCommand> future;
    bool success = true;
    for (auto command : pending_) {
        if (command.tick < tick) {
            ++telemetry_.rejectedCommands;
            continue;
        }
        if (command.tick > tick) {
            future.push_back(command);
            continue;
        }
        if (!authority_.accepts(command.islandId, command.authorityEpoch,
                                command.tick)) {
            ++telemetry_.staleEpochCommands;
            ++telemetry_.rejectedCommands;
            continue;
        }
        success = execute(command) && success;
    }
    pending_ = std::move(future);
    const auto stepTelemetry = world_.step(static_cast<uint32_t>(tick));
    currentTick_ = tick;
    static_cast<void>(snapshots_.store(currentSnapshot()));
    telemetry_.tick = currentTick_;
    telemetry_.liveBodies = stepTelemetry.liveBodies;
    telemetry_.worldHash = stepTelemetry.hashes.world;
    ++telemetry_.snapshots;
    return success;
}

AuthoritativeSnapshot TwoClientMeteorBoxSandbox::currentSnapshot() const {
    AuthoritativeSnapshot snapshot;
    snapshot.tick = currentTick_;
    snapshot.islandId = config_.islandId;
    snapshot.authorityEpoch = config_.authorityEpoch;
    snapshot.full = true;
    for (const auto& body : world_.bodies()) {
        if (bodyAlive(body)) snapshot.bodies.push_back(body);
    }
    snapshot.stateHash = snapshotStateHash(snapshot);
    return snapshot;
}

std::optional<AuthoritativeSnapshot>
TwoClientMeteorBoxSandbox::snapshotFor(uint32_t clientId) {
    if (client(clientId) == nullptr) return std::nullopt;
    AuthoritativeSnapshot current = currentSnapshot();
    const auto baseline = acknowledgements_.acknowledgedTick(clientId);
    if (baseline.has_value() && *baseline != current.tick) {
        auto delta = snapshots_.deltaFrom(current, *baseline);
        if (delta.has_value()) {
            ++telemetry_.deltas;
            return delta;
        }
    }
    return current;
}

bool TwoClientMeteorBoxSandbox::acknowledgeSnapshot(
    uint32_t clientId, uint64_t tick) {
    if (client(clientId) == nullptr
        || !snapshots_.find(config_.islandId, config_.authorityEpoch, tick)
                .has_value()) return false;
    acknowledgements_.acknowledge(clientId, tick);
    return true;
}

std::optional<IslandAuthority>
TwoClientMeteorBoxSandbox::advanceAuthority(
    uint32_t destinationWorker, uint64_t startTick) {
    const auto snapshot = currentSnapshot();
    const std::array<uint64_t, 2> hash{
        snapshot.stateHash,
        (uint64_t{snapshot.stateHash} << 32u) | snapshot.stateHash,
    };
    auto next = authority_.advanceEpoch(config_.islandId, destinationWorker,
                                        startTick, hash);
    if (next.has_value()) config_.authorityEpoch = next->epoch;
    return next;
}

} // namespace voxy::network
