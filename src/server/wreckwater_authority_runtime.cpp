#include "server/wreckwater_authority_runtime.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <tuple>
#include <utility>

namespace voxy::server {
namespace {

constexpr uint32_t kMaximumRuntimeFramesPerTick = 256u;
constexpr uint32_t kMaximumRuntimeReadbacksPerTick = 64u;
constexpr float kQ15Scale = 1.0f / 32'767.0f;

void incrementSaturated(uint64_t& value) noexcept {
    if (value != std::numeric_limits<uint64_t>::max()) ++value;
}

void addSaturated(uint64_t& value, uint64_t amount) noexcept {
    if (amount > std::numeric_limits<uint64_t>::max() - value) {
        value = std::numeric_limits<uint64_t>::max();
    } else {
        value += amount;
    }
}

[[nodiscard]] bool finiteVector(const glm::vec3& value) noexcept {
    return std::isfinite(value.x) && std::isfinite(value.y)
        && std::isfinite(value.z);
}

[[nodiscard]] game::MatchCommandType commandType(
    network::WreckwaterAction action) noexcept {
    switch (action) {
        case network::WreckwaterAction::Tow:
            return game::MatchCommandType::TowCargo;
        case network::WreckwaterAction::Cut:
            return game::MatchCommandType::CutTow;
        case network::WreckwaterAction::Steal:
            return game::MatchCommandType::StealCargo;
        case network::WreckwaterAction::Bank:
            return game::MatchCommandType::BankCargo;
        case network::WreckwaterAction::Helm:
            return game::MatchCommandType::TowCargo;
    }
    return game::MatchCommandType::TowCargo;
}

[[nodiscard]] bool cargoAction(
    network::WreckwaterAction action) noexcept {
    return action == network::WreckwaterAction::Tow
        || action == network::WreckwaterAction::Cut
        || action == network::WreckwaterAction::Steal
        || action == network::WreckwaterAction::Bank;
}

[[nodiscard]] network::WreckwaterCrew networkCrew(
    game::CrewId crew) noexcept {
    switch (crew) {
        case game::CrewId::CrewOne:
            return network::WreckwaterCrew::CrewOne;
        case game::CrewId::CrewTwo:
            return network::WreckwaterCrew::CrewTwo;
        case game::CrewId::None:
            return network::WreckwaterCrew::None;
    }
    return network::WreckwaterCrew::None;
}

[[nodiscard]] network::WreckwaterPhase networkPhase(
    game::MatchPhase phase) noexcept {
    switch (phase) {
        case game::MatchPhase::Warmup:
            return network::WreckwaterPhase::Warmup;
        case game::MatchPhase::Live:
            return network::WreckwaterPhase::Live;
        case game::MatchPhase::Overtime:
            return network::WreckwaterPhase::Overtime;
        case game::MatchPhase::Finished:
            return network::WreckwaterPhase::Finished;
    }
    return network::WreckwaterPhase::Warmup;
}

[[nodiscard]] network::WreckwaterOutcomeType networkOutcome(
    game::MatchOutcomeType outcome) noexcept {
    switch (outcome) {
        case game::MatchOutcomeType::Undecided:
            return network::WreckwaterOutcomeType::Undecided;
        case game::MatchOutcomeType::CrewVictory:
            return network::WreckwaterOutcomeType::CrewVictory;
        case game::MatchOutcomeType::Tie:
            return network::WreckwaterOutcomeType::Tie;
    }
    return network::WreckwaterOutcomeType::Undecided;
}

[[nodiscard]] network::WreckwaterCargoDisposition
networkCargoDisposition(game::CargoDisposition disposition) noexcept {
    switch (disposition) {
        case game::CargoDisposition::Free:
            return network::WreckwaterCargoDisposition::Free;
        case game::CargoDisposition::Towed:
            return network::WreckwaterCargoDisposition::Towed;
        case game::CargoDisposition::Banked:
            return network::WreckwaterCargoDisposition::Banked;
        case game::CargoDisposition::Lost:
            return network::WreckwaterCargoDisposition::Lost;
    }
    return network::WreckwaterCargoDisposition::NotApplicable;
}

[[nodiscard]] network::WreckwaterSkiffDisposition
networkSkiffDisposition(game::SkiffDisposition disposition) noexcept {
    return disposition == game::SkiffDisposition::Active
        ? network::WreckwaterSkiffDisposition::Active
        : network::WreckwaterSkiffDisposition::Sunk;
}

[[nodiscard]] network::WreckwaterShape networkShape(
    physics::ThrowableShape shape) noexcept {
    switch (shape) {
        case physics::ThrowableShape::Sphere:
            return network::WreckwaterShape::Sphere;
        case physics::ThrowableShape::Cube:
        case physics::ThrowableShape::Box:
            return network::WreckwaterShape::Box;
        case physics::ThrowableShape::Capsule:
            return network::WreckwaterShape::Capsule;
        case physics::ThrowableShape::Cylinder:
            return network::WreckwaterShape::Cylinder;
        case physics::ThrowableShape::Count:
            return network::WreckwaterShape::Box;
    }
    return network::WreckwaterShape::Box;
}

[[nodiscard]] network::WreckwaterVec3 networkVector(
    const glm::vec3& value) noexcept {
    return {
        network::canonicalWreckwaterFloat(value.x),
        network::canonicalWreckwaterFloat(value.y),
        network::canonicalWreckwaterFloat(value.z),
    };
}

[[nodiscard]] network::WreckwaterCharacterMode networkCharacterMode(
    game::WreckwaterCharacterMode mode) noexcept {
    switch (mode) {
        case game::WreckwaterCharacterMode::Airborne:
            return network::WreckwaterCharacterMode::Airborne;
        case game::WreckwaterCharacterMode::OnSkiff:
            return network::WreckwaterCharacterMode::OnSkiff;
        case game::WreckwaterCharacterMode::Swimming:
            return network::WreckwaterCharacterMode::Swimming;
    }
    return network::WreckwaterCharacterMode::Airborne;
}

[[nodiscard]] bool validPhysicsEventType(
    physics::PhysicsEventType type) noexcept {
    const uint32_t value = static_cast<uint32_t>(type);
    return value
            >= static_cast<uint32_t>(
                physics::PhysicsEventType::ContactBegin)
        && value
            <= static_cast<uint32_t>(
                physics::PhysicsEventType::AttachmentBreak);
}

} // namespace

const char* wreckwaterAuthorityIngressErrorName(
    WreckwaterAuthorityIngressError error) noexcept {
    switch (error) {
        case WreckwaterAuthorityIngressError::None: return "none";
        case WreckwaterAuthorityIngressError::UnknownPeer:
            return "unknown peer";
        case WreckwaterAuthorityIngressError::InvalidLifecycleFrame:
            return "invalid lifecycle frame";
        case WreckwaterAuthorityIngressError::StaleConnectionSerial:
            return "stale connection serial";
        case WreckwaterAuthorityIngressError::InactivePeer:
            return "inactive peer";
        case WreckwaterAuthorityIngressError::AuthorityFrozen:
            return "authority frozen";
        case WreckwaterAuthorityIngressError::WrongDeliveryClass:
            return "wrong delivery class";
        case WreckwaterAuthorityIngressError::WrongPayloadType:
            return "wrong payload type";
        case WreckwaterAuthorityIngressError::WrongFrameSize:
            return "wrong frame size";
        case WreckwaterAuthorityIngressError::OuterPacketDecodeFailed:
            return "outer packet decode failed";
        case WreckwaterAuthorityIngressError::OuterIdentityMismatch:
            return "outer identity mismatch";
        case WreckwaterAuthorityIngressError::ActionDecodeFailed:
            return "action decode failed";
        case WreckwaterAuthorityIngressError::CharacterInputDecodeFailed:
            return "character input decode failed";
        case WreckwaterAuthorityIngressError::ActionIdentityMismatch:
            return "action identity mismatch";
        case WreckwaterAuthorityIngressError::CharacterIdentityMismatch:
            return "character identity mismatch";
        case WreckwaterAuthorityIngressError::ReplayedSequence:
            return "replayed sequence";
        case WreckwaterAuthorityIngressError::RequestedTickOutOfWindow:
            return "requested tick out of window";
        case WreckwaterAuthorityIngressError::SeatNotAuthorized:
            return "seat not authorized";
        case WreckwaterAuthorityIngressError::PhysicsEvidenceUnavailable:
            return "physics evidence unavailable";
        case WreckwaterAuthorityIngressError::CharacterInputRejected:
            return "character input rejected";
        case WreckwaterAuthorityIngressError::MatchCommandRejected:
            return "match command rejected";
    }
    return "unknown";
}

const char* wreckwaterAuthorityPhysicsStepStatusName(
    WreckwaterAuthorityPhysicsStepStatus status) noexcept {
    switch (status) {
        case WreckwaterAuthorityPhysicsStepStatus::Submitted:
            return "submitted";
        case WreckwaterAuthorityPhysicsStepStatus::NotReady:
            return "not ready";
        case WreckwaterAuthorityPhysicsStepStatus::DebugReadbackRejected:
            return "debug readback rejected";
        case WreckwaterAuthorityPhysicsStepStatus::ScheduleRejected:
            return "schedule rejected";
        case WreckwaterAuthorityPhysicsStepStatus::EncoderCreationFailed:
            return "encoder creation failed";
        case WreckwaterAuthorityPhysicsStepStatus::EncodeRejected:
            return "checked encode rejected";
        case WreckwaterAuthorityPhysicsStepStatus::
                EventReadbackUnavailable:
            return "event readback unavailable";
        case WreckwaterAuthorityPhysicsStepStatus::
                DebugReadbackUnavailable:
            return "debug readback unavailable";
        case WreckwaterAuthorityPhysicsStepStatus::EncodedTickMismatch:
            return "encoded tick mismatch";
        case WreckwaterAuthorityPhysicsStepStatus::
                CommandBufferCreationFailed:
            return "command buffer creation failed";
    }
    return "unknown";
}

const char* wreckwaterAuthorityFailStopReasonName(
    WreckwaterAuthorityFailStopReason reason) noexcept {
    switch (reason) {
        case WreckwaterAuthorityFailStopReason::None: return "none";
        case WreckwaterAuthorityFailStopReason::InvalidConfiguration:
            return "invalid configuration";
        case WreckwaterAuthorityFailStopReason::PhysicsNotReady:
            return "physics not ready";
        case WreckwaterAuthorityFailStopReason::MatchInitializationFailed:
            return "match initialization failed";
        case WreckwaterAuthorityFailStopReason::PlayerRegistrationFailed:
            return "player registration failed";
        case WreckwaterAuthorityFailStopReason::
                CharacterInitializationFailed:
            return "character initialization failed";
        case WreckwaterAuthorityFailStopReason::CharacterLifecycleFailed:
            return "character lifecycle failed";
        case WreckwaterAuthorityFailStopReason::
                CharacterCertificationFailed:
            return "character certification failed";
        case WreckwaterAuthorityFailStopReason::BodySpawnFailed:
            return "body spawn failed";
        case WreckwaterAuthorityFailStopReason::BodyDestroyFailed:
            return "body destroy failed";
        case WreckwaterAuthorityFailStopReason::BodyRespawnFailed:
            return "body respawn failed";
        case WreckwaterAuthorityFailStopReason::BodyRegistryFailed:
            return "body registry failed";
        case WreckwaterAuthorityFailStopReason::
                DamageInitializationFailed:
            return "damage initialization failed";
        case WreckwaterAuthorityFailStopReason::
                DamageEvidenceRejected:
            return "damage evidence rejected";
        case WreckwaterAuthorityFailStopReason::
                DamageLifecycleFailed:
            return "damage lifecycle failed";
        case WreckwaterAuthorityFailStopReason::
                ConnectionIdentityExhausted:
            return "connection identity exhausted";
        case WreckwaterAuthorityFailStopReason::MatchConnectionFailed:
            return "match connection failed";
        case WreckwaterAuthorityFailStopReason::MatchDisconnectionFailed:
            return "match disconnection failed";
        case WreckwaterAuthorityFailStopReason::MatchStartFailed:
            return "match start failed";
        case WreckwaterAuthorityFailStopReason::EventReadbackOverflow:
            return "event readback overflow";
        case WreckwaterAuthorityFailStopReason::EventReadbackGap:
            return "event readback gap";
        case WreckwaterAuthorityFailStopReason::EventReadbackMalformed:
            return "event readback malformed";
        case WreckwaterAuthorityFailStopReason::EventReadbackTimeout:
            return "event readback timeout";
        case WreckwaterAuthorityFailStopReason::UnknownBrokenAttachment:
            return "unknown broken attachment";
        case WreckwaterAuthorityFailStopReason::
                BrokenAttachmentStateMismatch:
            return "broken attachment state mismatch";
        case WreckwaterAuthorityFailStopReason::WorldEventRejected:
            return "world event rejected";
        case WreckwaterAuthorityFailStopReason::PoseReadbackGap:
            return "pose readback gap";
        case WreckwaterAuthorityFailStopReason::PoseReadbackMalformed:
            return "pose readback malformed";
        case WreckwaterAuthorityFailStopReason::PoseReadbackTimeout:
            return "pose readback timeout";
        case WreckwaterAuthorityFailStopReason::PoseQueueOverflow:
            return "pose queue overflow";
        case WreckwaterAuthorityFailStopReason::
                EvidenceCertificationFailed:
            return "evidence certification failed";
        case WreckwaterAuthorityFailStopReason::
                BodyCommandSequenceExhausted:
            return "body command sequence exhausted";
        case WreckwaterAuthorityFailStopReason::MatchStepFailed:
            return "match step failed";
        case WreckwaterAuthorityFailStopReason::PhysicsStepFailed:
            return "physics step failed";
        case WreckwaterAuthorityFailStopReason::
                SnapshotSequenceExhausted:
            return "snapshot sequence exhausted";
        case WreckwaterAuthorityFailStopReason::SnapshotBuildFailed:
            return "snapshot build failed";
        case WreckwaterAuthorityFailStopReason::
                ReplayInitializationFailed:
            return "replay initialization failed";
        case WreckwaterAuthorityFailStopReason::
                ReplayEventRecordingFailed:
            return "replay event recording failed";
        case WreckwaterAuthorityFailStopReason::
                ReplaySnapshotRecordingFailed:
            return "replay snapshot recording failed";
        case WreckwaterAuthorityFailStopReason::ReplayFinalizeFailed:
            return "replay finalize failed";
        case WreckwaterAuthorityFailStopReason::GpuDeviceError:
            return "GPU device error";
        case WreckwaterAuthorityFailStopReason::TransportAdmissionFailed:
            return "transport admission failed";
    }
    return "unknown";
}

RealWreckwaterAuthorityPhysics::RealWreckwaterAuthorityPhysics(
    gpu::Context& context, physics::PhysicsWorld& world) noexcept
    : context_(&context), world_(&world) {}

bool RealWreckwaterAuthorityPhysics::authorityReady() const noexcept {
    if (context_ == nullptr || world_ == nullptr
        || !context_->isInitialized()
        || context_->getDevice() == nullptr
        || context_->getQueue() == nullptr
        || !world_->isInitialized()
        || world_->backendType() != physics::BackendType::WebGpuSoft) {
        return false;
    }
    const physics::BackendCapabilities capabilities =
        world_->capabilities();
    return capabilities.gpuResidentState
        && capabilities.bodyBodyContacts
        && capabilities.eventReadback
        && capabilities.distanceAttachments
        && capabilities.atomicMutationBatches
        && capabilities.fixedTickScheduling
        && capabilities.checkedGpuEncoding;
}

physics::PhysicsWorld*
RealWreckwaterAuthorityPhysics::nativeWorld() noexcept {
    return world_;
}

bool RealWreckwaterAuthorityPhysics::enableAuthorityReadbackAndWater(
    float waterHeight) noexcept {
    if (!authorityReady() || world_->encodedTick() != 0u
        || !std::isfinite(waterHeight)) {
        return false;
    }
    world_->setWaterPlane(waterHeight, true);
    return world_->setEventReadbackEnabled(true);
}

physics::BodyHandle RealWreckwaterAuthorityPhysics::spawnBody(
    const physics::BodySpawnDesc& desc) {
    return world_ != nullptr ? world_->spawnBody(desc)
                             : physics::BodyHandle{};
}

bool RealWreckwaterAuthorityPhysics::destroyBody(
    physics::BodyHandle body) {
    return world_ != nullptr && world_->destroyBody(body);
}

void RealWreckwaterAuthorityPhysics::enqueue(
    std::span<const physics::PhysicsCommand> commands) {
    if (world_ != nullptr) world_->enqueue(commands);
}

void RealWreckwaterAuthorityPhysics::serviceAsync() {
    if (context_ != nullptr) context_->tick();
}

WreckwaterAuthorityPhysicsStepResult
RealWreckwaterAuthorityPhysics::submitExactTick(
    uint64_t expectedTick, bool requestDebugPose,
    physics::DebugSnapshotRequest debugRequest) {
    WreckwaterAuthorityPhysicsStepResult result;
    if (!authorityReady()
        || expectedTick == 0u
        || world_->encodedTick()
            == std::numeric_limits<uint64_t>::max()
        || world_->encodedTick() + 1u != expectedTick) {
        result.status =
            WreckwaterAuthorityPhysicsStepStatus::NotReady;
        return result;
    }
    if (requestDebugPose) {
        if (debugRequest.firstBody == 0u
            || debugRequest.bodyCount
                != kWreckwaterAuthorityPhysicalBodyCount) {
            result.status =
                WreckwaterAuthorityPhysicsStepStatus::
                    DebugReadbackRejected;
            return result;
        }
        world_->requestDebugSnapshot(debugRequest);
    }
    if (!world_->scheduleFixedTicks(1u)) {
        result.status =
            WreckwaterAuthorityPhysicsStepStatus::ScheduleRejected;
        return result;
    }

    WGPUCommandEncoderDescriptor encoderDesc{};
    WGPU_SET_LABEL(encoderDesc, "wreckwater_authority_tick");
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(
        context_->getDevice(), &encoderDesc);
    if (encoder == nullptr) {
        result.status =
            WreckwaterAuthorityPhysicsStepStatus::EncoderCreationFailed;
        return result;
    }
    result.encode = world_->encodeGpuStepChecked(encoder);
    result.encodedTick = world_->encodedTick();
    if (!result.encode
        || result.encode.failStopped
        || result.encode.tickCount != 1u) {
        if (result.encode.status
            == physics::PhysicsEncodeStatus::
                EventReadbackUnavailable) {
            result.status =
                WreckwaterAuthorityPhysicsStepStatus::
                    EventReadbackUnavailable;
        } else if (result.encode.status
                   == physics::PhysicsEncodeStatus::
                       DebugReadbackUnavailable) {
            result.status =
                WreckwaterAuthorityPhysicsStepStatus::
                    DebugReadbackUnavailable;
        } else {
            result.status =
                WreckwaterAuthorityPhysicsStepStatus::
                    EncodeRejected;
        }
        wgpuCommandEncoderRelease(encoder);
        return result;
    }
    if (result.encode.firstTick != expectedTick
        || result.encode.finalTick != expectedTick
        || result.encodedTick != expectedTick) {
        result.status =
            WreckwaterAuthorityPhysicsStepStatus::EncodedTickMismatch;
        wgpuCommandEncoderRelease(encoder);
        return result;
    }

    WGPUCommandBufferDescriptor commandDesc{};
    WGPU_SET_LABEL(commandDesc, "wreckwater_authority_tick");
    WGPUCommandBuffer command =
        wgpuCommandEncoderFinish(encoder, &commandDesc);
    if (command == nullptr) {
        result.status =
            WreckwaterAuthorityPhysicsStepStatus::
                CommandBufferCreationFailed;
        wgpuCommandEncoderRelease(encoder);
        return result;
    }
    wgpuQueueSubmit(context_->getQueue(), 1u, &command);
    wgpuCommandBufferRelease(command);
    wgpuCommandEncoderRelease(encoder);
    result.status = WreckwaterAuthorityPhysicsStepStatus::Submitted;
    return result;
}

std::optional<physics::PhysicsEventBatch>
RealWreckwaterAuthorityPhysics::pollEvents() {
    return world_ != nullptr ? world_->pollEvents() : std::nullopt;
}

std::optional<physics::DebugSnapshot>
RealWreckwaterAuthorityPhysics::pollDebugSnapshot() {
    return world_ != nullptr
        ? world_->pollDebugSnapshot() : std::nullopt;
}

physics::PreparedPhysicsMutation
RealWreckwaterAuthorityPhysics::prepare(
    const physics::PhysicsMutationBatch& batch) noexcept {
    return world_ != nullptr
        ? world_->prepareMutationBatch(batch)
        : physics::PreparedPhysicsMutation{};
}

physics::PhysicsMutationResult
RealWreckwaterAuthorityPhysics::commit(
    const physics::PreparedPhysicsMutation& prepared) noexcept {
    return world_ != nullptr
        ? world_->commitPrepared(prepared)
        : physics::PhysicsMutationResult{};
}

bool RealWreckwaterAuthorityPhysics::discard(
    const physics::PreparedPhysicsMutation& prepared) noexcept {
    return world_ != nullptr && world_->discardPrepared(prepared);
}

WreckwaterAuthorityRuntime::WreckwaterAuthorityRuntime(
    Config config,
    std::unique_ptr<network::IMultiplayerTransport> transport,
    IWreckwaterAuthorityPhysics& physics)
    : config_(std::move(config)),
      transport_(std::move(transport)),
      physics_(&physics) {}

WreckwaterAuthorityRuntime::~WreckwaterAuthorityRuntime() {
    close();
}

bool WreckwaterAuthorityRuntime::validConfig() const noexcept {
    const uint64_t totalMatchTicks =
        uint64_t{config_.match.warmupTicks}
        + config_.match.liveTicks
        + config_.match.overtimeTicks;
    const uint64_t periodicSnapshots =
        totalMatchTicks == 0u
        ? 0u
        : (totalMatchTicks
            + kWreckwaterAuthoritySnapshotIntervalTicks - 1u)
            / kWreckwaterAuthoritySnapshotIntervalTicks;
    const uint64_t terminalSnapshot =
        totalMatchTicks != 0u
            && (totalMatchTicks - 1u)
                % kWreckwaterAuthoritySnapshotIntervalTicks != 0u
        ? 1u : 0u;
    const uint64_t maximumSnapshots =
        periodicSnapshots + terminalSnapshot;
    const uint64_t requiredReplayRecords =
        uint64_t{config_.match.eventCapacity} + maximumSnapshots;
    const uint64_t requiredReplayPayload =
        uint64_t{config_.match.eventCapacity}
            * game::kWreckwaterReplayMatchEventBytes
        + maximumSnapshots
            * network::kWreckwaterFirstSliceSnapshotBytes;
    return transport_ != nullptr && physics_ != nullptr
        && config_.sessionId != 0u
        && config_.matchId != 0u
        && config_.worldId != 0u
        && config_.worldEpoch != 0u
        && config_.authorityEpoch != 0u
        && config_.worldEventStreamId != 0u
        && config_.maximumFramesPerTick != 0u
        && config_.maximumFramesPerTick <= kMaximumRuntimeFramesPerTick
        && config_.maximumReadbackBatchesPerTick != 0u
        && config_.maximumReadbackBatchesPerTick
            <= kMaximumRuntimeReadbacksPerTick
        && config_.maximumRequestedTickLead != 0u
        && config_.maximumRequestedTickLag != 0u
        && config_.helmTimeoutTicks != 0u
        && config_.maximumEventReadbackLagTicks != 0u
        && config_.maximumPoseReadbackLagTicks
            >= kWreckwaterAuthoritySnapshotIntervalTicks
        && std::isfinite(config_.helmMaximumForce)
        && config_.helmMaximumForce > 0.0f
        && std::isfinite(config_.helmMaximumYawSpeed)
        && config_.helmMaximumYawSpeed > 0.0f
        && std::isfinite(config_.waterHeight)
        && config_.match.bankScore
            <= network::kWreckwaterMaximumScore
        && config_.replay.contentHash != 0u
        && config_.replay.maximumRecords
            <= game::kWreckwaterReplayMaximumRecords
        && config_.replay.maximumPayloadBytes
            <= game::kWreckwaterReplayMaximumPayloadBytes
        && requiredReplayRecords
            <= config_.replay.maximumRecords
        && requiredReplayPayload
            <= config_.replay.maximumPayloadBytes;
}

bool WreckwaterAuthorityRuntime::initializeMatchAndRoster() {
    game::WreckwaterMatch::Config matchConfig = config_.match;
    matchConfig.matchId = config_.matchId;
    matchConfig.worldId = config_.worldId;
    matchConfig.worldEpoch = config_.worldEpoch;
    matchConfig.authorityEpoch = config_.authorityEpoch;
    matchConfig.worldEventStreamId = config_.worldEventStreamId;
    matchConfig.reactorCargoId = 1u;
    matchConfig.reactorCargoGeneration = 1u;
    matchConfig.crewOneSkiffId = 1u;
    matchConfig.crewTwoSkiffId = 2u;
    if (!match_.initialize(matchConfig)) {
        failStop(
            WreckwaterAuthorityFailStopReason::
                MatchInitializationFailed);
        return false;
    }

    peers_ = {{
        {1u, 1u, game::CrewId::CrewOne, 0u, 1u},
        {2u, 2u, game::CrewId::CrewOne, 1u, 1u},
        {3u, 3u, game::CrewId::CrewTwo, 0u, 2u},
        {4u, 4u, game::CrewId::CrewTwo, 1u, 2u},
    }};
    for (const PeerState& peer : peers_) {
        if (match_.registerPlayer({
                .playerId = peer.playerId,
                .crew = peer.crew,
                .seat = peer.seat,
            }) != game::RegistrationResult::Registered) {
            failStop(
                WreckwaterAuthorityFailStopReason::
                    PlayerRegistrationFailed);
            return false;
        }
    }
    return true;
}

physics::BodySpawnDesc
WreckwaterAuthorityRuntime::skiffSpawnDesc(
    size_t index, uint32_t skiffGeneration) const noexcept {
    physics::BodySpawnDesc result;
    result.shape = physics::ThrowableShape::Box;
    result.position = {
        index == 0u ? -12.0f : 12.0f, 2.0f, 0.0f};
    result.dimensions = {4.0f, 1.5f, 8.0f};
    result.inverseMass = 1.0f / 850.0f;
    result.material = physics::PhysicsMaterial{
        .friction = 0.55f,
        .restitution = 0.05f,
        .rollingResistance = 0.02f,
        .density = 0.55f,
        .flags = 0x5757'0000u
            | (static_cast<uint32_t>(index + 1u) << 8u)
            | (skiffGeneration & 0xffu),
    };
    return result;
}

bool WreckwaterAuthorityRuntime::spawnAndBindBodies() {
    if (!physics_->enableAuthorityReadbackAndWater(
            config_.waterHeight)) {
        failStop(WreckwaterAuthorityFailStopReason::PhysicsNotReady);
        return false;
    }

    const physics::BodySpawnDesc crewOne =
        skiffSpawnDesc(0u, 1u);
    const physics::BodySpawnDesc crewTwo =
        skiffSpawnDesc(1u, 1u);
    physics::BodySpawnDesc reactor;
    reactor.shape = physics::ThrowableShape::Cylinder;
    reactor.position = {0.0f, 2.0f, 12.0f};
    reactor.dimensions = {2.5f, 3.0f, 2.5f};
    reactor.inverseMass = 1.0f / 420.0f;
    reactor.material = physics::PhysicsMaterial{
        .friction = 0.62f,
        .restitution = 0.08f,
        .rollingResistance = 0.04f,
        .density = 0.85f,
        .flags = 0x5757'c001u,
    };

    bodies_[0] = physics_->spawnBody(crewOne);
    bodies_[1] = physics_->spawnBody(crewTwo);
    bodies_[2] = physics_->spawnBody(reactor);
    for (size_t index = 0u; index < bodies_.size(); ++index) {
        if (!bodies_[index].valid()
            || bodies_[index].generation == 0u
            || (index != 0u
                && bodies_[index].index
                    != bodies_[0].index
                        + static_cast<uint32_t>(index))) {
            failStop(WreckwaterAuthorityFailStopReason::BodySpawnFailed);
            return false;
        }
    }

    game::WreckwaterLiveWorld::Config liveConfig = config_.liveWorld;
    liveConfig.matchId = config_.matchId;
    liveConfig.worldId = config_.worldId;
    liveConfig.worldEpoch = config_.worldEpoch;
    liveConfig.authorityEpoch = config_.authorityEpoch;
    bool liveInitialized = false;
    if (physics::PhysicsWorld* native = physics_->nativeWorld();
        native != nullptr) {
        liveInitialized = liveWorld_.initialize(liveConfig, *native);
    } else {
        liveInitialized = liveWorld_.initialize(liveConfig, *physics_);
    }
    if (!liveInitialized) {
        failStop(WreckwaterAuthorityFailStopReason::BodyRegistryFailed);
        return false;
    }

    const auto bind = [this](
        game::WreckwaterEntityKey key,
        physics::BodyHandle body) {
        return liveWorld_.bindEntity(key, body)
            == game::WreckwaterBindingResult::Bound;
    };
    const bool bound =
        bind({game::WreckwaterEntityKind::Skiff, 1u, 1u}, bodies_[0])
        && bind({game::WreckwaterEntityKind::Skiff, 2u, 1u}, bodies_[1])
        && bind({game::WreckwaterEntityKind::Cargo, 1u, 1u}, bodies_[2])
        && bind({game::WreckwaterEntityKind::Player, 1u, 1u}, bodies_[0])
        && bind({game::WreckwaterEntityKind::Player, 2u, 1u}, bodies_[0])
        && bind({game::WreckwaterEntityKind::Player, 3u, 1u}, bodies_[1])
        && bind({game::WreckwaterEntityKind::Player, 4u, 1u}, bodies_[1]);
    if (!bound
        || liveWorld_.entityCount()
            != kWreckwaterAuthorityLogicalEntityCount) {
        failStop(WreckwaterAuthorityFailStopReason::BodyRegistryFailed);
        return false;
    }

    const std::array<game::WreckwaterVesselPose,
        game::kWreckwaterDamageSkiffCount> damageVessels{{
        {
            .skiffId = 1u,
            .skiffGeneration = 1u,
            .body = bodies_[0],
            .position = {
                .sector = crewOne.sector,
                .local = crewOne.position,
            },
            .orientation = crewOne.orientation,
        },
        {
            .skiffId = 2u,
            .skiffGeneration = 1u,
            .body = bodies_[1],
            .position = {
                .sector = crewTwo.sector,
                .local = crewTwo.position,
            },
            .orientation = crewTwo.orientation,
        },
    }};
    game::WreckwaterVesselDamageAuthority::Config damageConfig =
        config_.damage;
    damageConfig.waterHeight = config_.waterHeight;
    if (!damage_.initialize(damageConfig, damageVessels)) {
        failStop(
            WreckwaterAuthorityFailStopReason::
                DamageInitializationFailed);
        return false;
    }
    skiffBodyAlive_ = {true, true};
    fragmentBindings_ = {};
    return true;
}

bool WreckwaterAuthorityRuntime::initializeCharacterAuthority() {
    if (characters_.initialized()) return true;
    std::array<
        game::WreckwaterCharacterSpawn,
        game::kWreckwaterMaximumCharacters> roster{};
    for (size_t index = 0u; index < peers_.size(); ++index) {
        PeerState& peer = peers_[index];
        if (!peer.active || peer.connectionGeneration == 0u
            || peer.skiffId == 0u
            || peer.skiffId > game::kWreckwaterSkiffCount) {
            failStop(
                WreckwaterAuthorityFailStopReason::
                    CharacterInitializationFailed);
            return false;
        }
        const size_t skiffIndex =
            static_cast<size_t>(peer.skiffId - 1u);
        const physics::BodySpawnDesc skiff =
            skiffSpawnDesc(skiffIndex, 1u);
        const float localX = peer.seat == 0u ? -1.0f : 1.0f;
        roster[index] = {
            .playerId = peer.playerId,
            .connectionGeneration = peer.connectionGeneration,
            .mode = game::WreckwaterCharacterMode::OnSkiff,
            .feetPosition = {
                .sector = skiff.sector,
                .local = skiff.position
                    + glm::vec3(
                        localX, skiff.dimensions.y * 0.5f, 0.0f),
            },
            .worldVelocity = glm::vec3(0.0f),
            .skiffId = peer.skiffId,
            .skiffGeneration = 1u,
            .skiffBody = bodies_[skiffIndex],
            .skiffLocalFeetPosition = {
                localX, skiff.dimensions.y * 0.5f, 0.0f},
            .skiffLocalVelocity = glm::vec3(0.0f),
        };
    }
    game::WreckwaterCharacterAuthorityBridge::Config characterConfig =
        config_.characters;
    characterConfig.movement.waterHeight = config_.waterHeight;
    if (!characters_.initialize(characterConfig, roster)) {
        failStop(
            WreckwaterAuthorityFailStopReason::
                CharacterInitializationFailed);
        return false;
    }
    for (PeerState& peer : peers_) {
        const auto* state = characters_.player(peer.playerId);
        if (state == nullptr
            || state->character == game::kInvalidWreckwaterCharacter
            || state->connectionGeneration
                != peer.connectionGeneration
            || !state->connected) {
            failStop(
                WreckwaterAuthorityFailStopReason::
                    CharacterInitializationFailed);
            return false;
        }
        peer.character = state->character;
        peer.lastCharacterConnectionGeneration =
            state->connectionGeneration;
    }
    return true;
}

bool WreckwaterAuthorityRuntime::initialize() {
    if (initialized_ || closed_ || !validConfig()) {
        failStop(
            WreckwaterAuthorityFailStopReason::InvalidConfiguration);
        return false;
    }
    if (!physics_->authorityReady()) {
        failStop(WreckwaterAuthorityFailStopReason::PhysicsNotReady);
        return false;
    }
    if (!initializeMatchAndRoster()) return false;

    helm_ = {};
    pendingPoses_ = {};
    pendingPoseBegin_ = 0u;
    pendingPoseCount_ = 0u;
    pendingDamageFrames_ = {};
    pendingDamageBegin_ = 0u;
    pendingDamageCount_ = 0u;
    pendingAttachmentBreaks_ = {};
    pendingAttachmentBreakCount_ = 0u;
    pendingAttachmentBreakWaitTicks_ = 0u;
    nextConnectionId_ = 1u;
    nextBodyCommandSequence_ = 1u;
    nextExpectedEventTick_ = 1u;
    nextExpectedPoseTick_ = 1u;
    eventClosedThroughTick_ = 0u;
    latestCertifiedEvidenceTick_ = 0u;
    terminalSettlementTick_ = 0u;
    nextSnapshotSequence_ = 1u;
    lastPublishedSnapshotSequence_ = 0u;
    lastPublishedSnapshot_.reset();
    replayEventCursor_ = 0u;
    lastReplayError_ = game::WreckwaterReplayError::None;
    lastPhysicsStepStatus_ =
        WreckwaterAuthorityPhysicsStepStatus::Submitted;
    telemetry_ = {};

    game::WreckwaterReplayConfig replayConfig = config_.replay;
    replayConfig.sessionId = config_.sessionId;
    replayConfig.matchId = config_.matchId;
    replayConfig.worldId = config_.worldId;
    replayConfig.worldEpoch = config_.worldEpoch;
    replayConfig.authorityEpoch = config_.authorityEpoch;
    replayConfig.tickRateHz = match_.config().tickRateHz;
    replayConfig.matchConfigurationHash =
        match_.configurationHash();
    lastReplayError_ = replay_.initialize(replayConfig);
    if (lastReplayError_ != game::WreckwaterReplayError::None) {
        failStop(
            WreckwaterAuthorityFailStopReason::
                ReplayInitializationFailed);
        return false;
    }
    if (!recordPendingMatchEvents()) return false;

    if (!spawnAndBindBodies()) return false;
    initialized_ = true;
    return true;
}

WreckwaterAuthorityRuntime::PeerState*
WreckwaterAuthorityRuntime::peerState(uint32_t peerId) noexcept {
    if (peerId == 0u || peerId > peers_.size()) return nullptr;
    PeerState& peer = peers_[peerId - 1u];
    return peer.peerId == peerId ? &peer : nullptr;
}

const WreckwaterAuthorityRuntime::PeerState*
WreckwaterAuthorityRuntime::peerState(uint32_t peerId) const noexcept {
    if (peerId == 0u || peerId > peers_.size()) return nullptr;
    const PeerState& peer = peers_[peerId - 1u];
    return peer.peerId == peerId ? &peer : nullptr;
}

std::optional<WreckwaterAuthorityPeerView>
WreckwaterAuthorityRuntime::peer(uint32_t peerId) const noexcept {
    const PeerState* value = peerState(peerId);
    if (value == nullptr) return std::nullopt;
    return WreckwaterAuthorityPeerView{
        .peerId = value->peerId,
        .playerId = value->playerId,
        .crew = value->crew,
        .seat = value->seat,
        .skiffId = value->skiffId,
        .connectionSerial = value->connectionSerial,
        .connectionId = value->connectionId,
        .connectionGeneration = value->connectionGeneration,
        .character = value->character,
        .latestInboundSequence = value->inboundWindow.latest(),
        .active = value->active,
    };
}

void WreckwaterAuthorityRuntime::clearHelmForSkiff(
    game::SkiffId skiffId) noexcept {
    if (skiffId == 0u || skiffId > helm_.size()) return;
    helm_[skiffId - 1u] = {};
}

bool WreckwaterAuthorityRuntime::disconnectPeer(PeerState& peer) {
    if (!peer.active) return true;
    const uint32_t priorGeneration = peer.connectionGeneration;
    if (!match_.disconnectPlayer(peer.playerId, peer.connectionId)) {
        failStop(
            WreckwaterAuthorityFailStopReason::
                MatchDisconnectionFailed);
        return false;
    }
    if (characters_.initialized()) {
        const auto status = characters_.disconnectCharacter(
            peer.character, peer.playerId, priorGeneration);
        if (status
            != game::WreckwaterCharacterAuthorityBridgeStatus::
                Accepted) {
            failStop(
                WreckwaterAuthorityFailStopReason::
                    CharacterLifecycleFailed);
            return false;
        }
    }
    peer.lastCharacterConnectionGeneration = priorGeneration;
    clearHelmForSkiff(peer.skiffId);
    peer.connectionSerial = 0u;
    peer.connectionId = 0u;
    peer.connectionGeneration = 0u;
    peer.active = false;
    return true;
}

bool WreckwaterAuthorityRuntime::connectPeer(
    PeerState& peer, uint64_t connectionSerial) {
    if (connectionSerial == 0u) return false;
    if (peer.active && !disconnectPeer(peer)) return false;
    if (nextConnectionId_ == 0u) {
        failStop(
            WreckwaterAuthorityFailStopReason::
                ConnectionIdentityExhausted);
        return false;
    }
    const game::ConnectionId connectionId = nextConnectionId_;
    nextConnectionId_ =
        nextConnectionId_ == std::numeric_limits<uint64_t>::max()
        ? 0u : nextConnectionId_ + 1u;
    const game::ConnectionResult connected =
        match_.connectPlayer(peer.playerId, connectionId);
    if (connected != game::ConnectionResult::Connected
        && connected != game::ConnectionResult::Reconnected) {
        failStop(
            WreckwaterAuthorityFailStopReason::MatchConnectionFailed);
        return false;
    }
    const game::PlayerState* player = match_.player(peer.playerId);
    if (player == nullptr || !player->connected
        || player->connectionId != connectionId
        || player->connectionGeneration == 0u) {
        failStop(
            WreckwaterAuthorityFailStopReason::MatchConnectionFailed);
        return false;
    }
    if (characters_.initialized()) {
        const uint32_t priorGeneration =
            peer.lastCharacterConnectionGeneration;
        const auto status = characters_.reconnectCharacter(
            peer.character, peer.playerId, priorGeneration,
            player->connectionGeneration);
        if (priorGeneration == 0u
            || status
                != game::WreckwaterCharacterAuthorityBridgeStatus::
                    Accepted) {
            failStop(
                WreckwaterAuthorityFailStopReason::
                    CharacterLifecycleFailed);
            return false;
        }
    }
    peer.connectionSerial = connectionSerial;
    peer.connectionId = connectionId;
    peer.connectionGeneration = player->connectionGeneration;
    peer.lastCharacterConnectionGeneration =
        player->connectionGeneration;
    peer.active = true;
    clearHelmForSkiff(peer.skiffId);
    return true;
}

bool WreckwaterAuthorityRuntime::allPeersActive() const noexcept {
    return std::all_of(
        peers_.begin(), peers_.end(),
        [](const PeerState& peer) { return peer.active; });
}

bool WreckwaterAuthorityRuntime::startIfReady() {
    if (match_.started() || !allPeersActive()) return true;
    if (!initializeCharacterAuthority() || !match_.start()) {
        if (failStopped()) return false;
        failStop(WreckwaterAuthorityFailStopReason::MatchStartFailed);
        return false;
    }
    return true;
}

WreckwaterAuthorityRuntime::FrameResult
WreckwaterAuthorityRuntime::processLifecycle(
    const network::MultiplayerTransportFrame& frame,
    PeerState& peer) {
    FrameResult result;
    const bool admissionRequested = frame.type
        == network::MultiplayerTransportFrameType::ConnectionRequested;
    const auto rejectAdmission = [&] {
        if (admissionRequested) {
            transport_->rejectConnection(frame.peerId, frame.connectionSerial);
        }
    };
    if (!frame.bytes.empty() || frame.connectionSerial == 0u) {
        rejectAdmission();
        result.error =
            WreckwaterAuthorityIngressError::InvalidLifecycleFrame;
        return result;
    }
    if (frame.type
        == network::MultiplayerTransportFrameType::Connected
        || admissionRequested) {
        if (peer.active
            && peer.connectionSerial == frame.connectionSerial) {
            rejectAdmission();
            result.error =
                WreckwaterAuthorityIngressError::
                    InvalidLifecycleFrame;
            return result;
        }
        if (!connectPeer(peer, frame.connectionSerial)) {
            rejectAdmission();
            result.error =
                WreckwaterAuthorityIngressError::InvalidLifecycleFrame;
            return result;
        }
        // Commit the authority generation before the transport replaces the
        // old socket. A failed transport commit cannot resume the match.
        if (admissionRequested && !transport_->acceptConnection(
                frame.peerId, frame.connectionSerial)) {
            rejectAdmission();
            failStop(WreckwaterAuthorityFailStopReason::
                TransportAdmissionFailed);
            result.error =
                WreckwaterAuthorityIngressError::InvalidLifecycleFrame;
            return result;
        }
        if (!startIfReady()) {
            result.error =
                WreckwaterAuthorityIngressError::
                    InvalidLifecycleFrame;
            return result;
        }
        incrementSaturated(telemetry_.lifecycleFrames);
        result.accepted = true;
        return result;
    }
    if (frame.type
        == network::MultiplayerTransportFrameType::Disconnected) {
        if (!peer.active
            || peer.connectionSerial != frame.connectionSerial) {
            result.error =
                WreckwaterAuthorityIngressError::
                    StaleConnectionSerial;
            incrementSaturated(telemetry_.staleSerialFrames);
            return result;
        }
        if (!disconnectPeer(peer)) {
            result.error =
                WreckwaterAuthorityIngressError::
                    InvalidLifecycleFrame;
            return result;
        }
        incrementSaturated(telemetry_.lifecycleFrames);
        result.accepted = true;
        return result;
    }
    result.error =
        WreckwaterAuthorityIngressError::InvalidLifecycleFrame;
    return result;
}

bool WreckwaterAuthorityRuntime::requestedTickInWindow(
    uint64_t requestedTick) const noexcept {
    if (requestedTick == 0u) return true;
    if (match_.currentTick()
        == std::numeric_limits<uint64_t>::max()) {
        return false;
    }
    const uint64_t targetTick = match_.currentTick() + 1u;
    return requestedTick >= targetTick
        ? requestedTick - targetTick
            <= config_.maximumRequestedTickLead
        : targetTick - requestedTick
            <= config_.maximumRequestedTickLag;
}

WreckwaterAuthorityRuntime::FrameResult
WreckwaterAuthorityRuntime::processData(
    const network::MultiplayerTransportFrame& frame,
    PeerState& peer) {
    FrameResult result;
    if (!peer.active) {
        result.error = WreckwaterAuthorityIngressError::InactivePeer;
        return result;
    }
    if (frame.connectionSerial != peer.connectionSerial) {
        result.error =
            WreckwaterAuthorityIngressError::StaleConnectionSerial;
        incrementSaturated(telemetry_.staleSerialFrames);
        return result;
    }
    if (frame.delivery != network::DeliveryClass::Realtime
        && frame.delivery != network::DeliveryClass::ReliableEvent) {
        result.error =
            WreckwaterAuthorityIngressError::WrongDeliveryClass;
        return result;
    }
    const bool actionFrameSize =
        frame.bytes.size() == kWreckwaterAuthorityFramedActionBytes;
    const bool characterFrameSize =
        frame.bytes.size()
        == kWreckwaterAuthorityFramedCharacterInputBytes;
    if ((frame.delivery == network::DeliveryClass::Realtime
            && !actionFrameSize && !characterFrameSize)
        || (frame.delivery == network::DeliveryClass::ReliableEvent
            && !actionFrameSize)) {
        result.error = WreckwaterAuthorityIngressError::WrongFrameSize;
        return result;
    }

    const network::PacketReadResult decodedOuter =
        network::PacketCodec::decode(frame.bytes, frame.delivery);
    if (!decodedOuter.packet.has_value()) {
        result.error =
            WreckwaterAuthorityIngressError::
                OuterPacketDecodeFailed;
        return result;
    }
    const network::Packet& packet = *decodedOuter.packet;
    const bool inputPacket =
        packet.header.payloadType == network::PacketPayloadType::Input;
    const bool commandPacket =
        packet.header.payloadType
        == network::PacketPayloadType::Command;
    if ((frame.delivery == network::DeliveryClass::Realtime
            && !inputPacket)
        || (frame.delivery
                == network::DeliveryClass::ReliableEvent
            && !commandPacket)) {
        result.error =
            WreckwaterAuthorityIngressError::WrongPayloadType;
        return result;
    }
    if (packet.header.flags != 0u
        || packet.header.sessionId != config_.sessionId
        || packet.header.worldId != config_.worldId
        || packet.header.worldEpoch != config_.worldEpoch
        || packet.header.authorityEpoch != config_.authorityEpoch
        || packet.header.sequence == 0u
        || packet.header.ackSequence
            > lastPublishedSnapshotSequence_) {
        result.error =
            WreckwaterAuthorityIngressError::
                OuterIdentityMismatch;
        return result;
    }

    const network::WreckwaterActionReadResult decodedAction =
        network::WreckwaterActionRequestCodec::decode(packet.payload);
    network::WreckwaterCharacterInputReadResult decodedCharacter;
    bool characterInput = false;
    if (!decodedAction.request.has_value()) {
        if (!inputPacket
            || decodedAction.error
                != network::WreckwaterCodecError::InvalidMagic) {
            result.error =
                WreckwaterAuthorityIngressError::ActionDecodeFailed;
            result.codecError = decodedAction.error;
            return result;
        }
        decodedCharacter =
            network::WreckwaterCharacterInputRequestCodec::decode(
                packet.payload);
        if (!decodedCharacter.request.has_value()) {
            result.error =
                WreckwaterAuthorityIngressError::
                    CharacterInputDecodeFailed;
            result.codecError = decodedCharacter.error;
            incrementSaturated(
                telemetry_.rejectedCharacterInputs);
            return result;
        }
        characterInput = true;
    }
    const uint64_t outerRequestSequence = characterInput
        ? decodedCharacter.request->clientRequestSequence
        : decodedAction.request->clientRequestSequence;
    const uint64_t requestedTick = characterInput
        ? decodedCharacter.request->requestedApplicationTick
        : decodedAction.request->requestedApplicationTick;
    if (outerRequestSequence != packet.header.sequence
        || requestedTick != packet.header.tick
        || (!characterInput && inputPacket
            && decodedAction.request->action
                != network::WreckwaterAction::Helm)
        || (!characterInput && commandPacket
            && !cargoAction(decodedAction.request->action))) {
        result.error = characterInput
            ? WreckwaterAuthorityIngressError::
                CharacterIdentityMismatch
            : WreckwaterAuthorityIngressError::
                ActionIdentityMismatch;
        if (characterInput) {
            incrementSaturated(
                telemetry_.rejectedCharacterInputs);
        }
        return result;
    }
    if (!characterInput && !requestedTickInWindow(requestedTick)) {
        result.error =
            WreckwaterAuthorityIngressError::
                RequestedTickOutOfWindow;
        return result;
    }
    if (!match_.started()) {
        result.error =
            WreckwaterAuthorityIngressError::AuthorityFrozen;
        return result;
    }

    network::AckWindow candidate = peer.inboundWindow;
    if (!candidate.observe(packet.header.sequence)) {
        result.error =
            WreckwaterAuthorityIngressError::ReplayedSequence;
        incrementSaturated(telemetry_.replayedFrames);
        return result;
    }
    peer.inboundWindow = candidate;

    if (characterInput) {
        const auto& request = *decodedCharacter.request;
        if (!characters_.initialized()
            || request.characterHandle != peer.character
            || request.connectionGeneration
                != peer.connectionGeneration) {
            result.error =
                WreckwaterAuthorityIngressError::
                    CharacterIdentityMismatch;
            incrementSaturated(
                telemetry_.rejectedCharacterInputs);
            return result;
        }

        game::WreckwaterCharacterAuthorityBridge stagedCharacters =
            characters_;
        uint64_t acceptedSamples = 0u;
        uint64_t supersededSamples = 0u;
        uint64_t replayedSamples = 0u;
        uint64_t expiredSamples = 0u;
        bool hardRejected = false;

        const auto submitSample =
            [&](const network::WreckwaterCharacterInputSample& sample) {
                if (!requestedTickInWindow(
                        sample.requestedApplicationTick)) {
                    if (sample.requestedApplicationTick
                        <= characters_.state().lastCertifiedTick) {
                        ++expiredSamples;
                        result.characterStatus =
                            game::
                                WreckwaterCharacterAuthorityBridgeStatus::
                                    StaleInputTick;
                        return;
                    }
                    hardRejected = true;
                    result.error =
                        WreckwaterAuthorityIngressError::
                            RequestedTickOutOfWindow;
                    return;
                }

                glm::vec2 move(
                    static_cast<float>(sample.moveXQ15)
                        * kQ15Scale,
                    static_cast<float>(sample.moveZQ15)
                        * kQ15Scale);
                const float lengthSquared = glm::dot(move, move);
                if (lengthSquared > 1.0f) {
                    move *= 1.0f / std::sqrt(lengthSquared);
                }
                const auto status = stagedCharacters.submitInput({
                    .targetTick =
                        sample.requestedApplicationTick,
                    .characterInputSequence =
                        sample.characterInputSequence,
                    .character = peer.character,
                    .playerId = peer.playerId,
                    .connectionGeneration =
                        peer.connectionGeneration,
                    .move = move,
                    .jump =
                        (sample.inputFlags
                         & network::
                             kWreckwaterCharacterInputJumpFlag)
                        != 0u,
                    .board =
                        (sample.inputFlags
                         & network::
                             kWreckwaterCharacterInputBoardFlag)
                        != 0u,
                });
                result.characterStatus = status;
                switch (status) {
                    case game::
                        WreckwaterCharacterAuthorityBridgeStatus::
                            Accepted:
                        ++acceptedSamples;
                        return;
                    case game::
                        WreckwaterCharacterAuthorityBridgeStatus::
                            IgnoredLowerSequence:
                        ++supersededSamples;
                        return;
                    case game::
                        WreckwaterCharacterAuthorityBridgeStatus::
                            ReplayedInputSequence:
                        ++replayedSamples;
                        return;
                    case game::
                        WreckwaterCharacterAuthorityBridgeStatus::
                            StaleInputTick:
                        ++expiredSamples;
                        return;
                    default:
                        hardRejected = true;
                        result.error =
                            WreckwaterAuthorityIngressError::
                                CharacterInputRejected;
                        return;
                }
            };

        for (uint32_t index = 0u;
             index < request.redundantInputCount
                 && !hardRejected;
             ++index) {
            submitSample(request.redundantInputs[index]);
        }
        if (!hardRejected) {
            submitSample({
                .requestedApplicationTick =
                    request.requestedApplicationTick,
                .characterInputSequence =
                    request.characterInputSequence,
                .moveXQ15 = request.moveXQ15,
                .moveZQ15 = request.moveZQ15,
                .inputFlags = request.inputFlags,
            });
        }
        if (hardRejected) {
            if (result.error
                == WreckwaterAuthorityIngressError::None) {
                result.error =
                    WreckwaterAuthorityIngressError::
                        CharacterInputRejected;
            }
            incrementSaturated(
                telemetry_.rejectedCharacterInputs);
            return result;
        }

        characters_ = stagedCharacters;
        addSaturated(
            telemetry_.acceptedCharacterInputs,
            acceptedSamples);
        addSaturated(
            telemetry_.supersededCharacterInputs,
            supersededSamples);
        addSaturated(
            telemetry_.replayedCharacterInputs,
            replayedSamples);
        addSaturated(
            telemetry_.expiredCharacterInputs,
            expiredSamples);
        incrementSaturated(
            telemetry_.characterInputPacketsAccepted);
        result.accepted = true;
        return result;
    }

    const network::WreckwaterActionRequest& request =
        *decodedAction.request;
    if (request.action == network::WreckwaterAction::Helm) {
        if (peer.seat != 0u
            || request.clientRequestSequence
                <= peer.lastHelmSequence
            || peer.skiffId == 0u
            || peer.skiffId > helm_.size()) {
            result.error =
                WreckwaterAuthorityIngressError::SeatNotAuthorized;
            return result;
        }
        HelmState& helm = helm_[peer.skiffId - 1u];
        helm.throttleQ15 = request.helmThrottleQ15;
        helm.steeringQ15 = request.helmSteeringQ15;
        helm.lastAcceptedServerTick = match_.currentTick();
        helm.sourceSequence = request.clientRequestSequence;
        helm.active = true;
        peer.lastHelmSequence = request.clientRequestSequence;
        incrementSaturated(telemetry_.acceptedHelmInputs);
        result.accepted = true;
        return result;
    }

    if (latestCertifiedEvidenceTick_ == 0u) {
        result.error =
            WreckwaterAuthorityIngressError::
                PhysicsEvidenceUnavailable;
        return result;
    }
    const game::PlayerState* player = match_.player(peer.playerId);
    const game::SkiffState* skiff = match_.skiff(peer.skiffId);
    if (player == nullptr || skiff == nullptr || !player->connected
        || player->connectionId != peer.connectionId
        || player->connectionGeneration != peer.connectionGeneration) {
        result.error =
            WreckwaterAuthorityIngressError::MatchCommandRejected;
        result.commandRejectReason =
            game::CommandRejectReason::StaleConnection;
        return result;
    }
    const game::MatchCommand command{
        .type = commandType(request.action),
        .matchId = config_.matchId,
        .worldId = config_.worldId,
        .worldEpoch = config_.worldEpoch,
        .authorityEpoch = config_.authorityEpoch,
        .tick = match_.currentTick() + 1u,
        .physicsEvidenceTick = latestCertifiedEvidenceTick_,
        .sequence = request.clientRequestSequence,
        .playerId = peer.playerId,
        .connectionId = peer.connectionId,
        .connectionGeneration = peer.connectionGeneration,
        .cargoId = request.cargoId,
        .cargoGeneration = request.cargoGeneration,
        .observedCargoRevision = request.observedCargoRevision,
        .skiffId = peer.skiffId,
        .skiffGeneration = skiff->generation,
    };
    const game::CommandSubmitResult submitted =
        match_.submitCommand(command);
    if (!submitted) {
        result.error =
            WreckwaterAuthorityIngressError::MatchCommandRejected;
        result.commandRejectReason = submitted.reason;
        incrementSaturated(telemetry_.matchCommandRejections);
        return result;
    }
    incrementSaturated(telemetry_.acceptedCargoCommands);
    result.accepted = true;
    return result;
}

WreckwaterAuthorityRuntime::FrameResult
WreckwaterAuthorityRuntime::processFrame(
    const network::MultiplayerTransportFrame& frame) {
    PeerState* peer = peerState(frame.peerId);
    if (peer == nullptr) {
        if (frame.type
            == network::MultiplayerTransportFrameType::ConnectionRequested) {
            transport_->rejectConnection(frame.peerId, frame.connectionSerial);
        }
        return {
            .error = WreckwaterAuthorityIngressError::UnknownPeer,
        };
    }
    if (frame.type
        != network::MultiplayerTransportFrameType::Data) {
        return processLifecycle(frame, *peer);
    }
    return processData(frame, *peer);
}

bool WreckwaterAuthorityRuntime::processEventBatch(
    const physics::PhysicsEventBatch& batch) {
    if (batch.overflow) {
        failStop(
            WreckwaterAuthorityFailStopReason::EventReadbackOverflow);
        return false;
    }
    if (batch.tick != nextExpectedEventTick_) {
        failStop(WreckwaterAuthorityFailStopReason::EventReadbackGap);
        return false;
    }
    if (batch.tick == 0u || batch.tick > match_.currentTick()) {
        failStop(
            WreckwaterAuthorityFailStopReason::EventReadbackMalformed);
        return false;
    }
    if (pendingDamageCount_ >= pendingDamageFrames_.size()) {
        failStop(
            WreckwaterAuthorityFailStopReason::EventReadbackOverflow);
        return false;
    }

    PendingDamageFrame damageFrame;
    damageFrame.tick = batch.tick;
    damageFrame.occupied = true;
    const bool terminalSettlement =
        terminalSettlementTick_ != 0u
        && batch.tick == terminalSettlementTick_
        && match_.phase() == game::MatchPhase::Finished;

    for (size_t index = 0u; index < batch.events.size(); ++index) {
        const physics::PhysicsEvent& event = batch.events[index];
        if (event.tick != batch.tick
            || !validPhysicsEventType(event.type)) {
            failStop(
                WreckwaterAuthorityFailStopReason::
                    EventReadbackMalformed);
            return false;
        }
        if (event.type == physics::PhysicsEventType::ContactHit) {
            game::WreckwaterContactHitEvidence hit{
                .physicsTick = batch.tick,
                .sourceId = event.sourceId,
                .bodyA = event.bodyHandleA(),
                .bodyB = event.bodyHandleB(),
                .featureA = event.featureId,
                .featureB = event.otherFeatureId,
                .localAnchorA = event.localAnchorA,
                .localAnchorB = event.localAnchorB,
                .normalAtoB = event.normalAtoB,
                .normalImpulse = event.impulse,
                .impactSpeed = event.impactSpeed,
                .materialA = impactMaterial(event.bodyHandleA()),
                .materialB = impactMaterial(event.bodyHandleB()),
            };
            if (!game::canonicalizeWreckwaterContactHit(hit)) {
                failStop(
                    WreckwaterAuthorityFailStopReason::
                        EventReadbackMalformed);
                return false;
            }
            if (terminalSettlement) continue;
            const auto logicalA =
                liveWorld_.logicalEntity(hit.bodyA);
            const auto logicalB =
                liveWorld_.logicalEntity(hit.bodyB);
            const bool hitsSkiff =
                (logicalA.has_value()
                    && logicalA->kind
                        == game::WreckwaterEntityKind::Skiff)
                || (logicalB.has_value()
                    && logicalB->kind
                        == game::WreckwaterEntityKind::Skiff);
            const bool staleSkiffIdentity =
                std::any_of(
                    bodies_.begin(), bodies_.end(),
                    [&hit](physics::BodyHandle body) {
                        return (body.index == hit.bodyA.index
                                && body != hit.bodyA)
                            || (body.index == hit.bodyB.index
                                && body != hit.bodyB);
                    });
            if (staleSkiffIdentity) {
                failStop(
                    WreckwaterAuthorityFailStopReason::
                        EventReadbackMalformed);
                return false;
            }
            if (hitsSkiff) {
                if (damageFrame.contactCount
                    >= damageFrame.contacts.size()) {
                    failStop(
                        WreckwaterAuthorityFailStopReason::
                            EventReadbackOverflow);
                    return false;
                }
                damageFrame.contacts[
                    damageFrame.contactCount++] = hit;
                incrementSaturated(telemetry_.contactHitEvents);
            }
            continue;
        }
        if (event.type
            != physics::PhysicsEventType::AttachmentBreak) {
            continue;
        }
        if (!std::isfinite(event.impulse)
            || !std::isfinite(event.force)) {
            failStop(
                WreckwaterAuthorityFailStopReason::
                    EventReadbackMalformed);
            return false;
        }
        const physics::AttachmentHandle rawAttachment =
            event.attachmentHandle();
        if (!rawAttachment.valid()
            || rawAttachment.generation == 0u) {
            failStop(
                WreckwaterAuthorityFailStopReason::
                    EventReadbackMalformed);
            return false;
        }
        if (terminalSettlement) continue;
        for (size_t prior = 0u; prior < index; ++prior) {
            if (batch.events[prior].type
                    == physics::PhysicsEventType::AttachmentBreak
                && batch.events[prior].attachmentHandle()
                    == rawAttachment) {
                failStop(
                    WreckwaterAuthorityFailStopReason::
                        EventReadbackMalformed);
                return false;
            }
        }

        const auto logical = liveWorld_.logicalTow(rawAttachment);
        if (!logical.has_value()) {
            failStop(
                WreckwaterAuthorityFailStopReason::
                    UnknownBrokenAttachment);
            return false;
        }
        const game::CargoState* cargo =
            match_.cargo(logical->cargoId);
        const game::SkiffState* skiff =
            match_.skiff(logical->skiffId);
        if (cargo == nullptr || skiff == nullptr
            || cargo->generation != logical->cargoGeneration
            || cargo->disposition != game::CargoDisposition::Towed
            || cargo->towingSkiff != logical->skiffId
            || cargo->towingSkiffGeneration
                != logical->skiffGeneration
            || cargo->towAttachmentId != logical->attachmentId
            || cargo->towAttachmentGeneration
                != logical->attachmentGeneration
            || skiff->generation != logical->skiffGeneration) {
            failStop(
                WreckwaterAuthorityFailStopReason::
                    BrokenAttachmentStateMismatch);
            return false;
        }
        const bool alreadyPending = std::any_of(
            pendingAttachmentBreaks_.begin(),
            pendingAttachmentBreaks_.begin()
                + static_cast<std::ptrdiff_t>(
                    pendingAttachmentBreakCount_),
            [&logical](
                const PendingAttachmentBreak& pending) {
                return pending.occupied
                    && pending.logical.attachmentId
                        == logical->attachmentId
                    && pending.logical.attachmentGeneration
                        == logical->attachmentGeneration;
            });
        if (alreadyPending) {
            failStop(
                WreckwaterAuthorityFailStopReason::
                    EventReadbackMalformed);
            return false;
        }
        if (pendingAttachmentBreakCount_
            >= pendingAttachmentBreaks_.size()) {
            failStop(
                WreckwaterAuthorityFailStopReason::
                    EventReadbackOverflow);
            return false;
        }
        pendingAttachmentBreaks_[
            pendingAttachmentBreakCount_++] = {
            .sourcePhysicsTick = batch.tick,
            .logical = *logical,
            .occupied = true,
        };
    }

    const size_t damageDestination =
        (pendingDamageBegin_ + pendingDamageCount_)
        % pendingDamageFrames_.size();
    pendingDamageFrames_[damageDestination] = damageFrame;
    ++pendingDamageCount_;
    eventClosedThroughTick_ = batch.tick;
    ++nextExpectedEventTick_;
    incrementSaturated(telemetry_.eventBatchesClosed);
    return true;
}

bool WreckwaterAuthorityRuntime::processDebugSnapshot(
    const physics::DebugSnapshot& snapshot) {
    if (snapshot.tick != nextExpectedPoseTick_) {
        failStop(WreckwaterAuthorityFailStopReason::PoseReadbackGap);
        return false;
    }
    if (snapshot.tick == 0u || snapshot.tick > match_.currentTick()
        || snapshot.bodies.size() != bodies_.size()) {
        failStop(
            WreckwaterAuthorityFailStopReason::PoseReadbackMalformed);
        return false;
    }
    if (pendingPoseCount_ >= pendingPoses_.size()) {
        failStop(WreckwaterAuthorityFailStopReason::PoseQueueOverflow);
        return false;
    }

    PendingPose pending;
    pending.tick = snapshot.tick;
    pending.occupied = true;
    for (size_t index = 0u; index < bodies_.size(); ++index) {
        const physics::DebugBodyState& body = snapshot.bodies[index];
        const physics::WorldPosition position{
            .sector = body.sector,
            .local = body.position,
        };
        const bool expectedDeadSkiff =
            index < skiffBodyAlive_.size()
            && !skiffBodyAlive_[index];
        const bool identityMatches = expectedDeadSkiff
            ? body.handle.index == bodies_[index].index
                && body.handle.generation != 0u
                && !body.alive
            : body.handle == bodies_[index] && body.alive;
        if (!identityMatches
            || !physics::isValidWorldPosition(position)
            || !finiteVector(body.linearVelocity)
            || !finiteVector(body.angularVelocity)
            || !finiteVector(body.dimensions)
            || (!expectedDeadSkiff
                && (body.dimensions.x <= 0.0f
                    || body.dimensions.y <= 0.0f
                    || body.dimensions.z <= 0.0f))
            || !std::isfinite(body.orientation.w)
            || !std::isfinite(body.orientation.x)
            || !std::isfinite(body.orientation.y)
            || !std::isfinite(body.orientation.z)) {
            failStop(
                WreckwaterAuthorityFailStopReason::
                    PoseReadbackMalformed);
            return false;
        }
        pending.bodies[index] = body;
        if (expectedDeadSkiff) {
            if (lastBodyStates_[index].dimensions.x <= 0.0f
                || lastBodyStates_[index].dimensions.y <= 0.0f
                || lastBodyStates_[index].dimensions.z <= 0.0f) {
                failStop(
                    WreckwaterAuthorityFailStopReason::
                        PoseReadbackMalformed);
                return false;
            }
            pending.bodies[index].dimensions =
                lastBodyStates_[index].dimensions;
            pending.bodies[index].shape =
                lastBodyStates_[index].shape;
            pending.bodies[index].material =
                lastBodyStates_[index].material;
        } else {
            lastBodyStates_[index] = body;
        }
    }
    const size_t destination =
        (pendingPoseBegin_ + pendingPoseCount_)
        % pendingPoses_.size();
    pendingPoses_[destination] = pending;
    ++pendingPoseCount_;
    if (nextExpectedPoseTick_
        == std::numeric_limits<uint64_t>::max()) {
        failStop(WreckwaterAuthorityFailStopReason::PoseReadbackGap);
        return false;
    }
    ++nextExpectedPoseTick_;
    return true;
}

bool WreckwaterAuthorityRuntime::
submitCertifiedAttachmentBreaks() {
    while (pendingAttachmentBreakCount_ != 0u) {
        const PendingAttachmentBreak pending =
            pendingAttachmentBreaks_.front();
        if (!pending.occupied
            || pending.sourcePhysicsTick == 0u) {
            failStop(
                WreckwaterAuthorityFailStopReason::
                    EventReadbackMalformed);
            return false;
        }
        if (latestCertifiedEvidenceTick_
            < pending.sourcePhysicsTick) {
            return true;
        }

        const game::CargoState* cargo =
            match_.cargo(pending.logical.cargoId);
        const game::SkiffState* skiff =
            match_.skiff(pending.logical.skiffId);
        if (cargo == nullptr || skiff == nullptr
            || cargo->generation
                != pending.logical.cargoGeneration
            || cargo->disposition
                != game::CargoDisposition::Towed
            || cargo->towingSkiff
                != pending.logical.skiffId
            || cargo->towingSkiffGeneration
                != pending.logical.skiffGeneration
            || cargo->towAttachmentId
                != pending.logical.attachmentId
            || cargo->towAttachmentGeneration
                != pending.logical.attachmentGeneration
            || skiff->generation
                != pending.logical.skiffGeneration) {
            failStop(
                WreckwaterAuthorityFailStopReason::
                    BrokenAttachmentStateMismatch);
            return false;
        }
        if (match_.currentTick()
            == std::numeric_limits<uint64_t>::max()) {
            failStop(
                WreckwaterAuthorityFailStopReason::
                    WorldEventRejected);
            return false;
        }
        const std::optional<uint64_t> sequence =
            match_.nextWorldEventSequence();
        if (!sequence.has_value()) {
            failStop(
                WreckwaterAuthorityFailStopReason::
                    WorldEventRejected);
            return false;
        }
        const game::AuthoritativeWorldEvent worldEvent{
            .type =
                game::AuthoritativeWorldEventType::
                    AttachmentBroken,
            .matchId = config_.matchId,
            .worldId = config_.worldId,
            .worldEpoch = config_.worldEpoch,
            .authorityEpoch = config_.authorityEpoch,
            .streamId = config_.worldEventStreamId,
            .sourcePhysicsTick =
                pending.sourcePhysicsTick,
            .applicationTick = match_.currentTick() + 1u,
            .sequence = *sequence,
            .cargoId = pending.logical.cargoId,
            .cargoGeneration =
                pending.logical.cargoGeneration,
            .cargoRevision = cargo->revision,
            .skiffId = pending.logical.skiffId,
            .skiffGeneration =
                pending.logical.skiffGeneration,
            .attachmentId =
                pending.logical.attachmentId,
            .attachmentGeneration =
                pending.logical.attachmentGeneration,
        };
        if (!match_.submitWorldEvent(worldEvent)) {
            failStop(
                WreckwaterAuthorityFailStopReason::
                    WorldEventRejected);
            return false;
        }
        for (size_t index = 1u;
             index < pendingAttachmentBreakCount_; ++index) {
            pendingAttachmentBreaks_[index - 1u] =
                pendingAttachmentBreaks_[index];
        }
        --pendingAttachmentBreakCount_;
        pendingAttachmentBreaks_[
            pendingAttachmentBreakCount_] = {};
        incrementSaturated(telemetry_.attachmentBreakEvents);
    }
    return true;
}

game::WreckwaterImpactMaterial
WreckwaterAuthorityRuntime::impactMaterial(
    physics::BodyHandle body) const noexcept {
    if (body == bodies_[0] || body == bodies_[1]) {
        return game::WreckwaterImpactMaterial::Timber;
    }
    if (body == bodies_[2]) {
        return game::WreckwaterImpactMaterial::Steel;
    }
    for (const FragmentBinding& binding : fragmentBindings_) {
        if (binding.occupied && binding.body == body) {
            return game::WreckwaterImpactMaterial::Timber;
        }
    }
    return game::WreckwaterImpactMaterial::Rock;
}

bool WreckwaterAuthorityRuntime::spawnSignificantFragment(
    const PendingPose& pose,
    const game::WreckwaterDamageLifecycleIntent& intent) {
    if (!intent.fragment.valid()
        || intent.skiffId == 0u
        || intent.skiffId > game::kWreckwaterSkiffCount
        || intent.skiffGeneration == 0u
        || !finiteVector(intent.localCenter)
        || !finiteVector(intent.dimensions)
        || intent.dimensions.x <= 0.0f
        || intent.dimensions.y <= 0.0f
        || intent.dimensions.z <= 0.0f) {
        return false;
    }
    for (const FragmentBinding& binding : fragmentBindings_) {
        if (binding.occupied
            && binding.fragment == intent.fragment) {
            return false;
        }
    }
    auto free = std::find_if(
        fragmentBindings_.begin(), fragmentBindings_.end(),
        [](const FragmentBinding& binding) {
            return !binding.occupied;
        });
    if (free == fragmentBindings_.end()) return false;

    const size_t skiffIndex = intent.skiffId - 1u;
    const physics::DebugBodyState& parent = pose.bodies[skiffIndex];
    const glm::vec3 worldOffset =
        parent.orientation * intent.localCenter;
    const physics::WorldPosition fragmentPosition =
        physics::canonicalWorldPosition(
            parent.sector,
            glm::dvec3(parent.position + worldOffset));
    physics::BodySpawnDesc desc;
    desc.shape = physics::ThrowableShape::Box;
    desc.position = fragmentPosition.local;
    desc.sector = fragmentPosition.sector;
    desc.orientation = parent.orientation;
    desc.linearVelocity = parent.linearVelocity
        + glm::cross(parent.angularVelocity, worldOffset);
    desc.angularVelocity = parent.angularVelocity;
    desc.dimensions = intent.dimensions;
    desc.inverseMass = 1.0f / 18.0f;
    desc.material = physics::PhysicsMaterial{
        .friction = 0.58f,
        .restitution = 0.10f,
        .rollingResistance = 0.03f,
        .density = 0.62f,
        .flags = 0x5757'f000u
            | (intent.fragment.index & 0x0fffu),
    };
    const physics::BodyHandle body = physics_->spawnBody(desc);
    if (!body.valid() || body.generation == 0u) return false;
    *free = {
        .fragment = intent.fragment,
        .body = body,
        .skiffId = intent.skiffId,
        .skiffGeneration = intent.skiffGeneration,
        .occupied = true,
    };
    incrementSaturated(telemetry_.significantFragmentsSpawned);
    return true;
}

bool WreckwaterAuthorityRuntime::retireSignificantFragment(
    const game::WreckwaterDamageLifecycleIntent& intent) {
    auto binding = std::find_if(
        fragmentBindings_.begin(), fragmentBindings_.end(),
        [&intent](const FragmentBinding& candidate) {
            return candidate.occupied
                && candidate.fragment == intent.fragment
                && candidate.skiffId == intent.skiffId
                && candidate.skiffGeneration
                    == intent.skiffGeneration;
        });
    if (binding == fragmentBindings_.end()
        || !physics_->destroyBody(binding->body)) {
        return false;
    }
    *binding = {};
    incrementSaturated(telemetry_.significantFragmentsRetired);
    return true;
}

bool WreckwaterAuthorityRuntime::submitDamageWorldEvent(
    const game::WreckwaterDamageLifecycleIntent& intent) {
    if (match_.currentTick()
        == std::numeric_limits<uint64_t>::max()) {
        return false;
    }
    const game::SkiffState* skiff =
        match_.skiff(intent.skiffId);
    if (skiff == nullptr
        || skiff->generation != intent.skiffGeneration) {
        return false;
    }
    const std::optional<uint64_t> sequence =
        match_.nextWorldEventSequence();
    if (!sequence.has_value()) return false;

    game::AuthoritativeWorldEvent event{
        .type = intent.type
                == game::WreckwaterDamageLifecycleIntentType::
                    SkiffSunk
            ? game::AuthoritativeWorldEventType::SkiffSunk
            : game::AuthoritativeWorldEventType::SkiffRespawned,
        .matchId = config_.matchId,
        .worldId = config_.worldId,
        .worldEpoch = config_.worldEpoch,
        .authorityEpoch = config_.authorityEpoch,
        .streamId = config_.worldEventStreamId,
        .sourcePhysicsTick = intent.sourcePhysicsTick,
        .applicationTick = match_.currentTick() + 1u,
        .sequence = *sequence,
        .skiffId = intent.skiffId,
        .skiffGeneration = intent.skiffGeneration,
        .nextSkiffGeneration = intent.nextSkiffGeneration,
    };
    if (event.type
        == game::AuthoritativeWorldEventType::SkiffSunk) {
        const game::CargoState* cargo = match_.cargo(1u);
        if (cargo != nullptr
            && cargo->disposition == game::CargoDisposition::Towed
            && cargo->towingSkiff == intent.skiffId
            && cargo->towingSkiffGeneration
                == intent.skiffGeneration) {
            event.cargoId = cargo->cargoId;
            event.cargoGeneration = cargo->generation;
            event.cargoRevision = cargo->revision;
            event.attachmentId = cargo->towAttachmentId;
            event.attachmentGeneration =
                cargo->towAttachmentGeneration;
        }
    }
    if (!match_.submitWorldEvent(event)) return false;
    if (event.type
        == game::AuthoritativeWorldEventType::SkiffSunk) {
        incrementSaturated(telemetry_.skiffsSunk);
    } else {
        incrementSaturated(telemetry_.skiffsRespawned);
    }
    return true;
}

bool WreckwaterAuthorityRuntime::applyDamageOutputs(
    const PendingPose& pose,
    const game::WreckwaterVesselDamageTickResult& damage) {
    addSaturated(
        telemetry_.structuralEdgesBroken,
        damage.brokenEdgeCount);
    addSaturated(telemetry_.breachesOpened, damage.openedBreachCount);

    if (match_.currentTick()
        == std::numeric_limits<uint64_t>::max()) {
        return false;
    }
    const uint64_t targetTick = match_.currentTick() + 1u;
    if (damage.floodForces.size()
            > game::kWreckwaterMaximumFloodForcesPerTick
        || nextBodyCommandSequence_ == 0u
        || (!damage.floodForces.empty()
            && nextBodyCommandSequence_
                > std::numeric_limits<uint64_t>::max()
                    - (damage.floodForces.size() - 1u))) {
        failStop(
            WreckwaterAuthorityFailStopReason::
                BodyCommandSequenceExhausted);
        return false;
    }
    std::array<
        physics::PhysicsCommand,
        game::kWreckwaterMaximumFloodForcesPerTick> commands{};
    size_t commandCount = 0u;
    for (const game::WreckwaterFloodForceIntent& force
         : damage.floodForces) {
        if (force.skiffId == 0u
            || force.skiffId > game::kWreckwaterSkiffCount
            || force.body != bodies_[force.skiffId - 1u]
            || !finiteVector(force.localPoint)
            || !finiteVector(force.worldForce)) {
            return false;
        }
        physics::PhysicsCommand& command =
            commands[commandCount++];
        command.type =
            physics::PhysicsCommandType::ApplyForceAtLocalPoint;
        command.body = force.body;
        command.targetTick = targetTick;
        command.sequence = nextBodyCommandSequence_++;
        command.a = glm::vec4(force.worldForce, 0.0f);
        command.b = glm::vec4(force.localPoint, 0.0f);
    }
    if (commandCount != 0u) {
        physics_->enqueue(std::span(commands.data(), commandCount));
        addSaturated(telemetry_.floodForceCommands, commandCount);
    }

    for (const game::WreckwaterDamageLifecycleIntent& intent
         : damage.lifecycleIntents) {
        switch (intent.type) {
            case game::WreckwaterDamageLifecycleIntentType::
                    SpawnSignificantFragment:
                if (!spawnSignificantFragment(pose, intent)) {
                    return false;
                }
                break;
            case game::WreckwaterDamageLifecycleIntentType::
                    RetireSignificantFragment:
                if (!retireSignificantFragment(intent)) {
                    return false;
                }
                break;
            case game::WreckwaterDamageLifecycleIntentType::SkiffSunk:
            case game::WreckwaterDamageLifecycleIntentType::SkiffRespawn:
                if (!submitDamageWorldEvent(intent)) return false;
                break;
        }
    }
    return true;
}

bool WreckwaterAuthorityRuntime::closeDamageFrame(
    const PendingPose& pose,
    const PendingDamageFrame& damageFrame) {
    if (!damageFrame.occupied || damageFrame.tick != pose.tick
        || damageFrame.contactCount
            > damageFrame.contacts.size()) {
        return false;
    }
    const std::span<const game::WreckwaterVesselDamageState>
        damageVessels = damage_.vessels();
    if (damageVessels.size()
        != game::kWreckwaterDamageSkiffCount) {
        return false;
    }
    std::array<game::WreckwaterVesselPose,
        game::kWreckwaterDamageSkiffCount> vesselPoses{};
    for (size_t index = 0u; index < vesselPoses.size(); ++index) {
        vesselPoses[index] = {
            .skiffId = static_cast<uint32_t>(index + 1u),
            .skiffGeneration =
                damageVessels[index].skiffGeneration,
            .body = damageVessels[index].body,
            .position = {
                .sector = pose.bodies[index].sector,
                .local = pose.bodies[index].position,
            },
            .orientation = pose.bodies[index].orientation,
            .available = pose.bodies[index].alive,
        };
    }
    const game::WreckwaterVesselDamageTickResult closed =
        damage_.closeExactTick({
            .physicsTick = pose.tick,
            .vessels = vesselPoses,
            .contacts = std::span(
                damageFrame.contacts.data(),
                damageFrame.contactCount),
        });
    if (!closed) {
        failStop(
            WreckwaterAuthorityFailStopReason::
                DamageEvidenceRejected);
        return false;
    }
    if (!applyDamageOutputs(pose, closed)) {
        failStop(
            WreckwaterAuthorityFailStopReason::
                DamageLifecycleFailed);
        return false;
    }
    return true;
}

bool WreckwaterAuthorityRuntime::reconcileDamageLifecycle() {
    const std::span<const game::WreckwaterVesselDamageState>
        damageVessels = damage_.vessels();
    if (damageVessels.size()
        != game::kWreckwaterDamageSkiffCount) {
        return false;
    }
    for (size_t index = 0u; index < damageVessels.size(); ++index) {
        const game::WreckwaterVesselDamageState& damageVessel =
            damageVessels[index];
        const game::SkiffState* matchSkiff =
            match_.skiff(damageVessel.skiffId);
        if (matchSkiff == nullptr) return false;

        if (matchSkiff->disposition
                == game::SkiffDisposition::Sunk
            && damageVessel.lifecycle
                != game::WreckwaterVesselLifecycle::Active
            && skiffBodyAlive_[index]) {
            if (!physics_->destroyBody(bodies_[index])) {
                failStop(
                    WreckwaterAuthorityFailStopReason::
                        BodyDestroyFailed);
                return false;
            }
            skiffBodyAlive_[index] = false;
            clearHelmForSkiff(damageVessel.skiffId);
        }

        if (damageVessel.lifecycle
                != game::WreckwaterVesselLifecycle::
                    RespawnEventQueued
            || matchSkiff->disposition
                != game::SkiffDisposition::Active
            || matchSkiff->generation
                != damageVessel.skiffGeneration + 1u) {
            continue;
        }
        if (skiffBodyAlive_[index]) {
            failStop(
                WreckwaterAuthorityFailStopReason::
                    BodyRespawnFailed);
            return false;
        }
        const physics::BodyHandle oldBody = bodies_[index];
        const physics::BodyHandle newBody = physics_->spawnBody(
            skiffSpawnDesc(index, matchSkiff->generation));
        if (!newBody.valid() || newBody.generation == 0u
            || newBody.index != oldBody.index
            || newBody.generation == oldBody.generation) {
            failStop(
                WreckwaterAuthorityFailStopReason::
                    BodyRespawnFailed);
            return false;
        }
        const uint32_t firstPlayer =
            index == 0u ? 1u : 3u;
        const game::WreckwaterEntityKey oldSkiff{
            game::WreckwaterEntityKind::Skiff,
            damageVessel.skiffId,
            damageVessel.skiffGeneration,
        };
        if (!liveWorld_.unbindEntity({
                game::WreckwaterEntityKind::Player,
                firstPlayer, 1u})
            || !liveWorld_.unbindEntity({
                game::WreckwaterEntityKind::Player,
                firstPlayer + 1u, 1u})
            || !liveWorld_.unbindEntity(oldSkiff)
            || liveWorld_.bindEntity({
                    game::WreckwaterEntityKind::Skiff,
                    damageVessel.skiffId,
                    matchSkiff->generation},
                    newBody)
                != game::WreckwaterBindingResult::Bound
            || liveWorld_.bindEntity({
                    game::WreckwaterEntityKind::Player,
                    firstPlayer, 1u}, newBody)
                != game::WreckwaterBindingResult::Bound
            || liveWorld_.bindEntity({
                    game::WreckwaterEntityKind::Player,
                    firstPlayer + 1u, 1u}, newBody)
                != game::WreckwaterBindingResult::Bound
            || damage_.confirmRespawn(
                    damageVessel.skiffId,
                    damageVessel.skiffGeneration,
                    matchSkiff->generation, newBody)
                != game::WreckwaterVesselDamageStatus::Accepted) {
            failStop(
                WreckwaterAuthorityFailStopReason::
                    BodyRespawnFailed);
            return false;
        }
        bodies_[index] = newBody;
        skiffBodyAlive_[index] = true;
        clearHelmForSkiff(damageVessel.skiffId);
    }
    return true;
}

network::WreckwaterCertifiedSnapshot
WreckwaterAuthorityRuntime::buildSnapshot(
    const PendingPose& pose, uint64_t snapshotSequence) const {
    network::WreckwaterCertifiedSnapshot snapshot;
    snapshot.sessionId = config_.sessionId;
    snapshot.matchId = config_.matchId;
    snapshot.worldId = config_.worldId;
    snapshot.worldEpoch = config_.worldEpoch;
    snapshot.authorityEpoch = config_.authorityEpoch;
    snapshot.snapshotSequence = snapshotSequence;
    snapshot.applicationTick = match_.currentTick();
    snapshot.physicsEvidenceTick = pose.tick;
    snapshot.phase = networkPhase(match_.phase());
    snapshot.crewOneScore = match_.score(game::CrewId::CrewOne);
    snapshot.crewTwoScore = match_.score(game::CrewId::CrewTwo);
    snapshot.outcome = networkOutcome(match_.outcome().type);
    snapshot.winner = networkCrew(match_.outcome().winner);
    snapshot.matchStateHash = match_.stateHash();
    snapshot.eventStreamHash = match_.eventStreamHash();
    snapshot.entities.reserve(kWreckwaterAuthorityPhysicalBodyCount);
    snapshot.characters.reserve(game::kWreckwaterMaximumCharacters);

    for (size_t index = 0u; index < 2u; ++index) {
        const game::SkiffId skiffId =
            static_cast<game::SkiffId>(index + 1u);
        const game::SkiffState* skiff = match_.skiff(skiffId);
        const physics::DebugBodyState& body = pose.bodies[index];
        if (skiff == nullptr) continue;
        network::WreckwaterEntityState entity;
        entity.netEntityId = index + 1u;
        entity.netGeneration = skiff->generation;
        entity.kind = network::WreckwaterEntityKind::Skiff;
        entity.crew = networkCrew(skiff->owner);
        entity.sector = {
            body.sector.x, body.sector.y, body.sector.z};
        entity.localPosition = networkVector(body.position);
        entity.orientation = {
            network::canonicalWreckwaterFloat(body.orientation.x),
            network::canonicalWreckwaterFloat(body.orientation.y),
            network::canonicalWreckwaterFloat(body.orientation.z),
            network::canonicalWreckwaterFloat(body.orientation.w),
        };
        entity.linearVelocity =
            networkVector(body.linearVelocity);
        entity.angularVelocity =
            networkVector(body.angularVelocity);
        entity.shape = networkShape(body.shape);
        entity.dimensions = networkVector(body.dimensions);
        entity.packedMaterialFlags = body.material.flags;
        entity.skiff = {
            .skiffId = skiff->skiffId,
            .generation = skiff->generation,
            .disposition =
                networkSkiffDisposition(skiff->disposition),
        };
        snapshot.entities.push_back(entity);
    }

    const game::CargoState* cargo = match_.cargo(1u);
    if (cargo != nullptr) {
        const physics::DebugBodyState& body = pose.bodies[2];
        network::WreckwaterEntityState entity;
        entity.netEntityId = 3u;
        entity.netGeneration = cargo->generation;
        entity.kind = network::WreckwaterEntityKind::Cargo;
        entity.crew = networkCrew(cargo->owner);
        entity.sector = {
            body.sector.x, body.sector.y, body.sector.z};
        entity.localPosition = networkVector(body.position);
        entity.orientation = {
            network::canonicalWreckwaterFloat(body.orientation.x),
            network::canonicalWreckwaterFloat(body.orientation.y),
            network::canonicalWreckwaterFloat(body.orientation.z),
            network::canonicalWreckwaterFloat(body.orientation.w),
        };
        entity.linearVelocity =
            networkVector(body.linearVelocity);
        entity.angularVelocity =
            networkVector(body.angularVelocity);
        entity.shape = networkShape(body.shape);
        entity.dimensions = networkVector(body.dimensions);
        entity.packedMaterialFlags = body.material.flags;
        entity.cargo = {
            .cargoId = cargo->cargoId,
            .generation = cargo->generation,
            .revision = cargo->revision,
            .disposition =
                networkCargoDisposition(cargo->disposition),
            .ownerCrew = networkCrew(cargo->owner),
            .towingSkiffId = cargo->towingSkiff,
            .towingSkiffGeneration =
                cargo->towingSkiffGeneration,
        };
        if (cargo->disposition == game::CargoDisposition::Towed) {
            entity.attachment = {
                .attachmentId = cargo->towAttachmentId,
                .generation = cargo->towAttachmentGeneration,
                .state =
                    network::WreckwaterAttachmentState::Attached,
            };
        }
        snapshot.entities.push_back(entity);
    }
    for (const game::WreckwaterCharacterState& source :
         characters_.authority().characters()) {
        if (!source.active) continue;
        network::WreckwaterCharacterState character;
        character.characterHandle = source.handle;
        character.stateFlags =
            static_cast<uint32_t>(networkCharacterMode(source.mode))
            | network::kWreckwaterCharacterStateActiveFlag;
        if (source.connected) {
            character.stateFlags |=
                network::kWreckwaterCharacterStateConnectedFlag;
        }
        character.playerId = source.playerId;
        character.connectionGeneration =
            source.connectionGeneration;
        character.sector = {
            source.feetPosition.sector.x,
            source.feetPosition.sector.y,
            source.feetPosition.sector.z,
        };
        character.localFeetPosition =
            networkVector(source.feetPosition.local);
        character.worldVelocity =
            networkVector(source.worldVelocity);
        character.skiffId = source.skiffId;
        character.skiffGeneration = source.skiffGeneration;
        character.skiffLocalFeetPosition =
            networkVector(source.skiffLocalFeetPosition);
        character.skiffLocalVelocity =
            networkVector(source.skiffLocalVelocity);
        character.lastAppliedCharacterInputSequence =
            source.lastAppliedCharacterInputSequence;
        snapshot.characters.push_back(character);
    }
    return snapshot;
}

bool WreckwaterAuthorityRuntime::recordPendingMatchEvents() noexcept {
    const std::span<const game::MatchEvent> events = match_.events();
    if (replayEventCursor_ > events.size()) {
        lastReplayError_ = game::WreckwaterReplayError::InvalidRecord;
        failStop(
            WreckwaterAuthorityFailStopReason::
                ReplayEventRecordingFailed);
        return false;
    }
    while (replayEventCursor_ < events.size()) {
        lastReplayError_ =
            replay_.appendMatchEvent(events[replayEventCursor_]);
        if (lastReplayError_ != game::WreckwaterReplayError::None) {
            failStop(
                WreckwaterAuthorityFailStopReason::
                    ReplayEventRecordingFailed);
            return false;
        }
        ++replayEventCursor_;
        incrementSaturated(telemetry_.replayEventsRecorded);
    }
    return true;
}

bool WreckwaterAuthorityRuntime::publishSnapshot(
    const PendingPose& pose,
    WreckwaterAuthorityTickResult& result) {
    if (nextSnapshotSequence_ == 0u) {
        failStop(
            WreckwaterAuthorityFailStopReason::
                SnapshotSequenceExhausted);
        return false;
    }
    const uint64_t snapshotSequence = nextSnapshotSequence_;
    network::WreckwaterCertifiedSnapshot snapshot =
        buildSnapshot(pose, snapshotSequence);
    if (snapshot.entities.size()
            != kWreckwaterAuthorityPhysicalBodyCount
        || snapshot.characters.size()
            != game::kWreckwaterMaximumCharacters
        || !network::canonicalizeWreckwaterSnapshot(snapshot)
        || !network::isCanonicalWreckwaterSnapshot(snapshot)) {
        failStop(
            WreckwaterAuthorityFailStopReason::SnapshotBuildFailed);
        return false;
    }
    const network::WreckwaterWriteResult payload =
        network::WreckwaterSnapshotCodec::encode(snapshot);
    if (!payload
        || payload.bytes.size()
            != network::kWreckwaterFirstSliceSnapshotBytes) {
        failStop(
            WreckwaterAuthorityFailStopReason::SnapshotBuildFailed);
        return false;
    }

    std::array<network::PacketWriteResult,
        kWreckwaterAuthorityPeerCount> encoded{};
    std::array<bool, kWreckwaterAuthorityPeerCount> destinations{};
    for (size_t index = 0u; index < peers_.size(); ++index) {
        const PeerState& peer = peers_[index];
        if (!peer.active) continue;
        network::Packet packet;
        packet.header.payloadType =
            network::PacketPayloadType::Snapshot;
        packet.header.sessionId = config_.sessionId;
        packet.header.worldId = config_.worldId;
        packet.header.worldEpoch = config_.worldEpoch;
        packet.header.authorityEpoch = config_.authorityEpoch;
        packet.header.sequence = snapshotSequence;
        packet.header.ackSequence = peer.inboundWindow.latest();
        packet.header.ackBits = peer.inboundWindow.bits();
        packet.header.tick = snapshot.applicationTick;
        packet.payload = payload.bytes;
        encoded[index] = network::PacketCodec::encode(
            packet, network::DeliveryClass::Realtime);
        if (!encoded[index].error.empty()
            || encoded[index].bytes.size()
                > network::kConservativeRealtimeMtu) {
            failStop(
                WreckwaterAuthorityFailStopReason::
                    SnapshotBuildFailed);
            return false;
        }
        destinations[index] = true;
    }

    snapshot.serializedByteHash = payload.serializedByteHash;
    if (!recordPendingMatchEvents()) return false;
    lastReplayError_ =
        replay_.appendCertifiedSnapshot(snapshot, payload.bytes);
    if (lastReplayError_ != game::WreckwaterReplayError::None) {
        failStop(
            WreckwaterAuthorityFailStopReason::
                ReplaySnapshotRecordingFailed);
        return false;
    }
    incrementSaturated(telemetry_.replaySnapshotsRecorded);
    const bool terminalCertifiedState =
        match_.phase() == game::MatchPhase::Finished
        && pose.tick == match_.currentTick();
    if (terminalCertifiedState) {
        lastReplayError_ = replay_.finalize(
            snapshot.matchStateHash, snapshot.eventStreamHash);
        if (lastReplayError_ != game::WreckwaterReplayError::None) {
            failStop(
                WreckwaterAuthorityFailStopReason::
                    ReplayFinalizeFailed);
            return false;
        }
        incrementSaturated(telemetry_.replayFinalizations);
    }

    lastPublishedSnapshotSequence_ = snapshotSequence;
    lastPublishedSnapshot_ = snapshot;
    nextSnapshotSequence_ =
        snapshotSequence == std::numeric_limits<uint64_t>::max()
        ? 0u : snapshotSequence + 1u;
    incrementSaturated(telemetry_.snapshotsBuilt);
    for (size_t index = 0u; index < peers_.size(); ++index) {
        if (!destinations[index]) continue;
        ++result.snapshotSendAttempts;
        incrementSaturated(telemetry_.snapshotSendAttempts);
        addSaturated(
            telemetry_.snapshotPacketBytes,
            encoded[index].bytes.size());
        if (transport_->send(
                peers_[index].peerId,
                peers_[index].connectionSerial,
                network::DeliveryClass::Realtime,
                encoded[index].bytes)) {
            ++result.snapshotsSent;
            incrementSaturated(telemetry_.snapshotsSent);
        } else {
            incrementSaturated(telemetry_.snapshotSendFailures);
        }
    }
    return true;
}

bool WreckwaterAuthorityRuntime::certifyCharacters(
    const PendingPose& pose) {
    if (!characters_.initialized()) {
        failStop(
            WreckwaterAuthorityFailStopReason::
                CharacterCertificationFailed);
        return false;
    }
    game::WreckwaterCharacterCertifiedPlatformTick frame;
    frame.tick = pose.tick;
    frame.platformCount =
        static_cast<uint32_t>(
            game::kWreckwaterMaximumCharacterPlatforms);
    for (size_t index = 0u; index < frame.platforms.size(); ++index) {
        const game::SkiffId skiffId =
            static_cast<game::SkiffId>(index + 1u);
        const game::SkiffState* skiff = match_.skiff(skiffId);
        const physics::DebugBodyState& body = pose.bodies[index];
        if (skiff == nullptr || !body.handle.valid()
            || body.handle.generation == 0u) {
            failStop(
                WreckwaterAuthorityFailStopReason::
                    CharacterCertificationFailed);
            return false;
        }
        frame.platforms[index] = {
            .skiffId = skiffId,
            .skiffGeneration = skiff->generation,
            // A destroyed skiff readback may already expose the allocator's
            // next body generation. Keep the last bound logical handle until
            // reconcileDamageLifecycle installs the respawned generation;
            // otherwise one physical slot would alias within a logical skiff
            // generation and the character core correctly rejects it.
            .body = bodies_[index],
            .position = {
                .sector = body.sector,
                .local = body.position,
            },
            .orientation = body.orientation,
            .linearVelocity = body.linearVelocity,
            .angularVelocity = body.angularVelocity,
            .deckHalfExtents = {
                body.dimensions.x * 0.5f,
                body.dimensions.z * 0.5f,
            },
            .deckLocalHeight = body.dimensions.y * 0.5f,
        };
    }
    const auto closed =
        characters_.closeCertifiedPlatformTick(frame);
    if (!closed) {
        failStop(
            WreckwaterAuthorityFailStopReason::
                CharacterCertificationFailed);
        return false;
    }
    incrementSaturated(telemetry_.certifiedCharacterTicks);
    addSaturated(
        telemetry_.characterMovementTransitions,
        closed.transitions.size());
    return true;
}

bool WreckwaterAuthorityRuntime::certifyPose(
    const PendingPose& pose,
    WreckwaterAuthorityTickResult& result) {
    const game::SkiffState* skiffOne = match_.skiff(1u);
    const game::SkiffState* skiffTwo = match_.skiff(2u);
    if (skiffOne == nullptr || skiffTwo == nullptr) {
        failStop(
            WreckwaterAuthorityFailStopReason::
                EvidenceCertificationFailed);
        return false;
    }
    const physics::WorldPosition skiffOnePosition{
        .sector = pose.bodies[0].sector,
        .local = pose.bodies[0].position,
    };
    const physics::WorldPosition skiffTwoPosition{
        .sector = pose.bodies[1].sector,
        .local = pose.bodies[1].position,
    };
    const physics::WorldPosition cargoPosition{
        .sector = pose.bodies[2].sector,
        .local = pose.bodies[2].position,
    };
    const std::array<game::WreckwaterEvidenceBody,
        kWreckwaterAuthorityLogicalEntityCount> evidence{{
        {{game::WreckwaterEntityKind::Player, 1u, 1u},
         skiffOnePosition,
         pose.bodies[0].alive && skiffBodyAlive_[0]},
        {{game::WreckwaterEntityKind::Player, 2u, 1u},
         skiffOnePosition,
         pose.bodies[0].alive && skiffBodyAlive_[0]},
        {{game::WreckwaterEntityKind::Player, 3u, 1u},
         skiffTwoPosition,
         pose.bodies[1].alive && skiffBodyAlive_[1]},
        {{game::WreckwaterEntityKind::Player, 4u, 1u},
         skiffTwoPosition,
         pose.bodies[1].alive && skiffBodyAlive_[1]},
        {{game::WreckwaterEntityKind::Skiff, 1u,
          skiffOne->generation},
         skiffOnePosition,
         pose.bodies[0].alive && skiffBodyAlive_[0]},
        {{game::WreckwaterEntityKind::Skiff, 2u,
          skiffTwo->generation},
         skiffTwoPosition,
         pose.bodies[1].alive && skiffBodyAlive_[1]},
        {{game::WreckwaterEntityKind::Cargo, 1u, 1u},
         cargoPosition, pose.bodies[2].alive},
    }};
    if (!liveWorld_.certifyEvidenceFrame({
            .worldId = config_.worldId,
            .worldEpoch = config_.worldEpoch,
            .physicsTick = pose.tick,
            .bodies = evidence,
        })) {
        failStop(
            WreckwaterAuthorityFailStopReason::
                EvidenceCertificationFailed);
        return false;
    }
    if (!certifyCharacters(pose)) return false;
    latestCertifiedEvidenceTick_ = pose.tick;
    incrementSaturated(telemetry_.evidenceFramesCertified);
    ++result.poseBatchesAccepted;
    const bool isSnapshotTick =
        (pose.tick - 1u)
            % kWreckwaterAuthoritySnapshotIntervalTicks == 0u
        || (match_.phase() == game::MatchPhase::Finished
            && pose.tick == match_.currentTick());
    return !isSnapshotTick || publishSnapshot(pose, result);
}

void WreckwaterAuthorityRuntime::flushClosedPoses(
    WreckwaterAuthorityTickResult& result) {
    while (!failStopped() && pendingPoseCount_ != 0u) {
        PendingPose& pose = pendingPoses_[pendingPoseBegin_];
        if (!pose.occupied || pose.tick > eventClosedThroughTick_) {
            break;
        }
        if (pendingDamageCount_ == 0u) {
            failStop(
                WreckwaterAuthorityFailStopReason::
                    EventReadbackGap);
            return;
        }
        PendingDamageFrame& damageFrame =
            pendingDamageFrames_[pendingDamageBegin_];
        const bool terminalSettlement =
            terminalSettlementTick_ != 0u
            && pose.tick == terminalSettlementTick_
            && match_.phase() == game::MatchPhase::Finished;
        if (!damageFrame.occupied
            || damageFrame.tick != pose.tick
            || (!terminalSettlement
                && !closeDamageFrame(pose, damageFrame))) {
            if (!failStopped()) {
                failStop(
                    WreckwaterAuthorityFailStopReason::
                        DamageEvidenceRejected);
            }
            return;
        }
        if (!certifyPose(pose, result)) return;
        pose = {};
        pendingPoseBegin_ =
            (pendingPoseBegin_ + 1u) % pendingPoses_.size();
        --pendingPoseCount_;
        damageFrame = {};
        pendingDamageBegin_ =
            (pendingDamageBegin_ + 1u)
            % pendingDamageFrames_.size();
        --pendingDamageCount_;
    }
}

void WreckwaterAuthorityRuntime::drainReadbacks(
    WreckwaterAuthorityTickResult& result) {
    uint32_t eventDrained = 0u;
    for (; eventDrained < config_.maximumReadbackBatchesPerTick;
         ++eventDrained) {
        std::optional<physics::PhysicsEventBatch> batch =
            physics_->pollEvents();
        if (!batch.has_value()) break;
        if (!processEventBatch(*batch)) return;
        ++result.eventBatchesClosed;
    }
    telemetry_.maximumEventBatchesDrainedInTick = std::max(
        telemetry_.maximumEventBatchesDrainedInTick, eventDrained);

    uint32_t poseDrained = 0u;
    for (; poseDrained < config_.maximumReadbackBatchesPerTick;
         ++poseDrained) {
        std::optional<physics::DebugSnapshot> snapshot =
            physics_->pollDebugSnapshot();
        if (!snapshot.has_value()) break;
        if (!processDebugSnapshot(*snapshot)) return;
    }
    telemetry_.maximumPoseBatchesDrainedInTick = std::max(
        telemetry_.maximumPoseBatchesDrainedInTick, poseDrained);
    flushClosedPoses(result);
    if (failStopped()
        || !submitCertifiedAttachmentBreaks()) {
        return;
    }
    if (pendingAttachmentBreakCount_ != 0u) {
        if (pendingAttachmentBreakWaitTicks_
            >= config_.maximumPoseReadbackLagTicks) {
            failStop(
                WreckwaterAuthorityFailStopReason::
                    PoseReadbackTimeout);
            return;
        }
        ++pendingAttachmentBreakWaitTicks_;
    } else {
        pendingAttachmentBreakWaitTicks_ = 0u;
    }
    if (failStopped() || !match_.started()) return;

    const uint64_t currentTick = match_.currentTick();
    if (currentTick > eventClosedThroughTick_
        && currentTick - eventClosedThroughTick_
            > config_.maximumEventReadbackLagTicks) {
        failStop(
            WreckwaterAuthorityFailStopReason::
                EventReadbackTimeout);
        return;
    }
    if (currentTick > nextExpectedPoseTick_
        && currentTick - nextExpectedPoseTick_
            > config_.maximumPoseReadbackLagTicks) {
        failStop(
            WreckwaterAuthorityFailStopReason::
                PoseReadbackTimeout);
    }
}

bool WreckwaterAuthorityRuntime::enqueueHelmCommands(
    uint64_t targetTick) {
    constexpr size_t commandCount =
        game::kWreckwaterSkiffCount * 2u;
    if (nextBodyCommandSequence_ == 0u
        || nextBodyCommandSequence_
            > std::numeric_limits<uint64_t>::max()
                - (commandCount - 1u)) {
        failStop(
            WreckwaterAuthorityFailStopReason::
                BodyCommandSequenceExhausted);
        return false;
    }
    std::array<physics::PhysicsCommand, commandCount> commands{};
    size_t destination = 0u;
    for (size_t index = 0u; index < helm_.size(); ++index) {
        HelmState& helm = helm_[index];
        const game::SkiffState* skiff =
            match_.skiff(static_cast<game::SkiffId>(index + 1u));
        if (!skiffBodyAlive_[index]
            || skiff == nullptr
            || skiff->disposition != game::SkiffDisposition::Active) {
            helm = {};
            continue;
        }
        const bool fresh = helm.active
            && targetTick > helm.lastAcceptedServerTick
            && targetTick - helm.lastAcceptedServerTick
                <= config_.helmTimeoutTicks;
        if (!fresh) {
            helm.active = false;
            helm.throttleQ15 = 0;
            helm.steeringQ15 = 0;
        }
        const float throttle =
            static_cast<float>(helm.throttleQ15) * kQ15Scale;
        const float steering =
            static_cast<float>(helm.steeringQ15) * kQ15Scale;

        physics::PhysicsCommand& force = commands[destination++];
        force.type = physics::PhysicsCommandType::ApplyForce;
        force.body = bodies_[index];
        force.targetTick = targetTick;
        force.sequence = nextBodyCommandSequence_++;
        force.a = {
            steering * config_.helmMaximumForce * 0.25f,
            0.0f,
            throttle * config_.helmMaximumForce,
            0.0f,
        };

        physics::PhysicsCommand& yaw = commands[destination++];
        yaw.type =
            physics::PhysicsCommandType::SetAngularVelocity;
        yaw.body = bodies_[index];
        yaw.targetTick = targetTick;
        yaw.sequence = nextBodyCommandSequence_++;
        yaw.a = {
            0.0f,
            steering * config_.helmMaximumYawSpeed,
            0.0f,
            0.0f,
        };
    }
    physics_->enqueue(
        std::span(commands.data(), destination));
    return true;
}

bool WreckwaterAuthorityRuntime::simulateOneTick(
    WreckwaterAuthorityTickResult& result) {
    if (!match_.started()
        || match_.phase() == game::MatchPhase::Finished) {
        return true;
    }
    if (match_.currentTick()
        == std::numeric_limits<uint64_t>::max()) {
        failStop(WreckwaterAuthorityFailStopReason::MatchStepFailed);
        return false;
    }
    const uint64_t targetTick = match_.currentTick() + 1u;
    const bool phaseBoundary =
        match_.remainingPhaseTicks() == 1u
        && (match_.phase() == game::MatchPhase::Live
            || match_.phase() == game::MatchPhase::Overtime);
    // Physics-derived sink/break events target N+1. Before a phase boundary
    // can finish the match, close exact evidence through N so those events
    // participate in the final logical transaction. The submitted boundary
    // tick is then a certification-only settlement tick: its contacts are
    // validated but cannot create a never-applicable N+1 event tail.
    if (phaseBoundary
        && latestCertifiedEvidenceTick_ < match_.currentTick()) {
        return true;
    }
    if (!enqueueHelmCommands(targetTick)) return false;

    // Match mutations prepare and commit against PhysicsWorld target tick N
    // before that exact single GPU tick is scheduled and submitted.
    if (!match_.step(liveWorld_)
        || match_.currentTick() != targetTick) {
        failStop(WreckwaterAuthorityFailStopReason::MatchStepFailed);
        return false;
    }
    if (!reconcileDamageLifecycle()) {
        if (!failStopped()) {
            failStop(
                WreckwaterAuthorityFailStopReason::
                    DamageLifecycleFailed);
        }
        return false;
    }
    const WreckwaterAuthorityPhysicsStepResult submitted =
        physics_->submitExactTick(
            targetTick, true,
            {
                // Primary slots are born as 1/2/3. Fragments allocate above
                // them. A sunk skiff's slot is the lowest free slot and
                // reconcileDamageLifecycle additionally requires exact index
                // reuse, so this contiguous read remains generation-safe.
                .firstBody = bodies_[0].index,
                .bodyCount =
                    kWreckwaterAuthorityPhysicalBodyCount,
            });
    lastPhysicsStepStatus_ = submitted.status;
    result.physicsStatus = submitted.status;
    if (!submitted || submitted.encodedTick != targetTick) {
        failStop(WreckwaterAuthorityFailStopReason::PhysicsStepFailed);
        return false;
    }
    if (match_.phase() == game::MatchPhase::Finished) {
        terminalSettlementTick_ = targetTick;
    }
    incrementSaturated(telemetry_.physicsTicksSubmitted);
    result.simulationAdvanced = true;
    return true;
}

WreckwaterAuthorityTickResult WreckwaterAuthorityRuntime::tick() {
    WreckwaterAuthorityTickResult result;
    result.physicsStatus = lastPhysicsStepStatus_;
    if (!initialized_ || closed_ || failStopped()) {
        result.authorityStarted = match_.started();
        result.failStopped = failStopped();
        return result;
    }

    physics_->serviceAsync();
    drainReadbacks(result);
    if (failStopped()) {
        result.failStopped = true;
        return result;
    }
    if (replay_.finalized()) {
        result.authorityStarted = match_.started();
        return result;
    }

    transport_->service();
    incrementSaturated(telemetry_.serviceCalls);
    for (uint32_t drained = 0u;
         drained < config_.maximumFramesPerTick; ++drained) {
        std::optional<network::MultiplayerTransportFrame> frame =
            transport_->poll();
        if (!frame.has_value()) break;
        ++result.framesPolled;
        incrementSaturated(telemetry_.framesPolled);
        const FrameResult processed = processFrame(*frame);
        if (processed.accepted) {
            ++result.framesAccepted;
        } else {
            ++result.framesRejected;
            incrementSaturated(telemetry_.rejectedFrames);
            result.lastIngressError = processed.error;
            result.lastCommandRejectReason =
                processed.commandRejectReason;
            result.lastCodecError = processed.codecError;
            result.lastCharacterStatus =
                processed.characterStatus;
        }
        if (!recordPendingMatchEvents()) break;
        if (failStopped()) break;
    }
    telemetry_.maximumFramesDrainedInTick = std::max(
        telemetry_.maximumFramesDrainedInTick,
        result.framesPolled);
    if (!failStopped()) {
        const bool ready = startIfReady();
        const bool recorded = recordPendingMatchEvents();
        if (!ready || !recorded) {
            result.failStopped = failStopped();
            return result;
        }
    }
    result.authorityStarted = match_.started();
    if (!failStopped() && match_.started()
        && pendingAttachmentBreakCount_ == 0u) {
        static_cast<void>(simulateOneTick(result));
        static_cast<void>(recordPendingMatchEvents());
    }
    result.failStopped = failStopped();
    return result;
}

void WreckwaterAuthorityRuntime::failStop(
    WreckwaterAuthorityFailStopReason reason) noexcept {
    if (reason == WreckwaterAuthorityFailStopReason::None
        || failStopped()) {
        return;
    }
    failStopReason_ = reason;
    failStopTick_ = match_.currentTick();
    clearHelmForSkiff(1u);
    clearHelmForSkiff(2u);
}

void WreckwaterAuthorityRuntime::requestFailStop(
    WreckwaterAuthorityFailStopReason reason) noexcept {
    failStop(reason);
}

void WreckwaterAuthorityRuntime::close() {
    if (closed_) return;
    closed_ = true;
    for (HelmState& helm : helm_) helm = {};
    if (transport_ != nullptr) transport_->close();
}

} // namespace voxy::server
