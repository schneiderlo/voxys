#pragma once

#include "game/wreckwater_character_authority_bridge.hpp"
#include "game/wreckwater_live_world.hpp"
#include "game/wreckwater_match.hpp"
#include "game/wreckwater_replay.hpp"
#include "game/wreckwater_vessel_damage.hpp"
#include "gpu/context.hpp"
#include "network/session_transport.hpp"
#include "network/wreckwater_protocol.hpp"
#include "physics/physics_world.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>

namespace voxy::server {

inline constexpr uint32_t kWreckwaterAuthorityPeerCount = 4u;
inline constexpr uint32_t kWreckwaterAuthorityPhysicalBodyCount = 3u;
inline constexpr uint32_t kWreckwaterAuthorityLogicalEntityCount = 7u;
inline constexpr uint32_t kWreckwaterAuthoritySnapshotIntervalTicks = 3u;
inline constexpr uint32_t kWreckwaterAuthorityMaximumResidentBodyCount =
    kWreckwaterAuthorityPhysicalBodyCount
    + game::kWreckwaterMaximumSignificantFragments;
inline constexpr uint32_t kWreckwaterAuthorityMaximumBodyPairCount =
    kWreckwaterAuthorityMaximumResidentBodyCount
    * (kWreckwaterAuthorityMaximumResidentBodyCount - 1u) / 2u;
inline constexpr uint32_t kWreckwaterAuthorityGpuPairCapacity = 64u;
inline constexpr uint32_t kWreckwaterAuthorityReadbackRingSlots =
    game::kWreckwaterCertifiedEvidenceFrameCount;
inline constexpr uint32_t
    kWreckwaterAuthorityMaximumEventReadbackLagTicks = 16u;
inline constexpr size_t kWreckwaterAuthorityReplayMaximumRecords =
    262'144u;
inline constexpr size_t kWreckwaterAuthorityReplayMaximumPayloadBytes =
    64u * 1024u * 1024u;
static_assert(kWreckwaterAuthorityMaximumResidentBodyCount == 11u);
static_assert(kWreckwaterAuthorityMaximumBodyPairCount == 55u);
static_assert(
    kWreckwaterAuthorityGpuPairCapacity
    >= kWreckwaterAuthorityMaximumBodyPairCount);
static_assert(
    kWreckwaterAuthorityMaximumEventReadbackLagTicks
    < kWreckwaterAuthorityReadbackRingSlots);
inline constexpr size_t kWreckwaterAuthorityFramedActionBytes =
    network::kNetworkPacketOverheadBytes
    + network::kWreckwaterActionRequestBytes;
inline constexpr size_t kWreckwaterAuthorityFramedCharacterInputBytes =
    network::kNetworkPacketOverheadBytes
    + network::kWreckwaterCharacterInputRequestBytes;
static_assert(kWreckwaterAuthorityFramedActionBytes == 148u);
static_assert(
    kWreckwaterAuthorityFramedCharacterInputBytes == 228u);
static_assert(
    kWreckwaterAuthorityFramedCharacterInputBytes
    <= network::kConservativeRealtimeMtu);

enum class WreckwaterAuthorityIngressError : uint32_t {
    None = 0u,
    UnknownPeer,
    InvalidLifecycleFrame,
    StaleConnectionSerial,
    InactivePeer,
    AuthorityFrozen,
    WrongDeliveryClass,
    WrongPayloadType,
    WrongFrameSize,
    OuterPacketDecodeFailed,
    OuterIdentityMismatch,
    ActionDecodeFailed,
    CharacterInputDecodeFailed,
    ActionIdentityMismatch,
    CharacterIdentityMismatch,
    ReplayedSequence,
    RequestedTickOutOfWindow,
    SeatNotAuthorized,
    PhysicsEvidenceUnavailable,
    CharacterInputRejected,
    MatchCommandRejected,
};

[[nodiscard]] const char* wreckwaterAuthorityIngressErrorName(
    WreckwaterAuthorityIngressError error) noexcept;

enum class WreckwaterAuthorityPhysicsStepStatus : uint32_t {
    Submitted = 0u,
    NotReady,
    DebugReadbackRejected,
    ScheduleRejected,
    EncoderCreationFailed,
    EncodeRejected,
    EventReadbackUnavailable,
    DebugReadbackUnavailable,
    EncodedTickMismatch,
    CommandBufferCreationFailed,
};

[[nodiscard]] const char* wreckwaterAuthorityPhysicsStepStatusName(
    WreckwaterAuthorityPhysicsStepStatus status) noexcept;

struct WreckwaterAuthorityPhysicsStepResult {
    WreckwaterAuthorityPhysicsStepStatus status =
        WreckwaterAuthorityPhysicsStepStatus::NotReady;
    physics::PhysicsEncodeReport encode{};
    uint64_t encodedTick = 0u;

    [[nodiscard]] explicit operator bool() const noexcept {
        return status
            == WreckwaterAuthorityPhysicsStepStatus::Submitted;
    }
};

enum class WreckwaterAuthorityFailStopReason : uint32_t {
    None = 0u,
    InvalidConfiguration,
    PhysicsNotReady,
    MatchInitializationFailed,
    PlayerRegistrationFailed,
    CharacterInitializationFailed,
    CharacterLifecycleFailed,
    CharacterCertificationFailed,
    BodySpawnFailed,
    BodyDestroyFailed,
    BodyRespawnFailed,
    BodyRegistryFailed,
    DamageInitializationFailed,
    DamageEvidenceRejected,
    DamageLifecycleFailed,
    ConnectionIdentityExhausted,
    MatchConnectionFailed,
    MatchDisconnectionFailed,
    MatchStartFailed,
    EventReadbackOverflow,
    EventReadbackGap,
    EventReadbackMalformed,
    EventReadbackTimeout,
    UnknownBrokenAttachment,
    BrokenAttachmentStateMismatch,
    WorldEventRejected,
    PoseReadbackGap,
    PoseReadbackMalformed,
    PoseReadbackTimeout,
    PoseQueueOverflow,
    EvidenceCertificationFailed,
    BodyCommandSequenceExhausted,
    MatchStepFailed,
    PhysicsStepFailed,
    SnapshotSequenceExhausted,
    SnapshotBuildFailed,
    ReplayInitializationFailed,
    ReplayEventRecordingFailed,
    ReplaySnapshotRecordingFailed,
    ReplayFinalizeFailed,
    GpuDeviceError,
    TransportAdmissionFailed,
};

[[nodiscard]] const char* wreckwaterAuthorityFailStopReasonName(
    WreckwaterAuthorityFailStopReason reason) noexcept;

struct WreckwaterAuthorityPeerView {
    uint32_t peerId = 0u;
    game::PlayerId playerId = 0u;
    game::CrewId crew = game::CrewId::None;
    uint32_t seat = 0u;
    game::SkiffId skiffId = 0u;
    uint64_t connectionSerial = 0u;
    game::ConnectionId connectionId = 0u;
    uint32_t connectionGeneration = 0u;
    game::WreckwaterCharacterHandle character =
        game::kInvalidWreckwaterCharacter;
    uint64_t latestInboundSequence = 0u;
    bool active = false;
};

struct WreckwaterAuthorityTelemetry {
    uint64_t serviceCalls = 0u;
    uint64_t framesPolled = 0u;
    uint64_t lifecycleFrames = 0u;
    uint64_t acceptedHelmInputs = 0u;
    uint64_t acceptedCargoCommands = 0u;
    uint64_t acceptedCharacterInputs = 0u;
    uint64_t supersededCharacterInputs = 0u;
    uint64_t rejectedCharacterInputs = 0u;
    uint64_t characterInputPacketsAccepted = 0u;
    uint64_t replayedCharacterInputs = 0u;
    uint64_t expiredCharacterInputs = 0u;
    uint64_t certifiedCharacterTicks = 0u;
    uint64_t characterMovementTransitions = 0u;
    uint64_t rejectedFrames = 0u;
    uint64_t staleSerialFrames = 0u;
    uint64_t replayedFrames = 0u;
    uint64_t matchCommandRejections = 0u;
    uint64_t physicsTicksSubmitted = 0u;
    uint64_t eventBatchesClosed = 0u;
    uint64_t attachmentBreakEvents = 0u;
    uint64_t contactHitEvents = 0u;
    uint64_t structuralEdgesBroken = 0u;
    uint64_t breachesOpened = 0u;
    uint64_t floodForceCommands = 0u;
    uint64_t significantFragmentsSpawned = 0u;
    uint64_t significantFragmentsRetired = 0u;
    uint64_t skiffsSunk = 0u;
    uint64_t skiffsRespawned = 0u;
    uint64_t evidenceFramesCertified = 0u;
    uint64_t snapshotsBuilt = 0u;
    uint64_t snapshotSendAttempts = 0u;
    uint64_t snapshotsSent = 0u;
    uint64_t snapshotSendFailures = 0u;
    uint64_t snapshotPacketBytes = 0u;
    uint64_t replayEventsRecorded = 0u;
    uint64_t replaySnapshotsRecorded = 0u;
    uint64_t replayFinalizations = 0u;
    uint32_t maximumFramesDrainedInTick = 0u;
    uint32_t maximumEventBatchesDrainedInTick = 0u;
    uint32_t maximumPoseBatchesDrainedInTick = 0u;

    [[nodiscard]] bool operator==(
        const WreckwaterAuthorityTelemetry&) const = default;
};

struct WreckwaterAuthorityTickResult {
    uint32_t framesPolled = 0u;
    uint32_t framesAccepted = 0u;
    uint32_t framesRejected = 0u;
    uint32_t eventBatchesClosed = 0u;
    uint32_t poseBatchesAccepted = 0u;
    uint32_t snapshotSendAttempts = 0u;
    uint32_t snapshotsSent = 0u;
    WreckwaterAuthorityIngressError lastIngressError =
        WreckwaterAuthorityIngressError::None;
    game::CommandRejectReason lastCommandRejectReason =
        game::CommandRejectReason::None;
    network::WreckwaterCodecError lastCodecError =
        network::WreckwaterCodecError::None;
    game::WreckwaterCharacterAuthorityBridgeStatus
        lastCharacterStatus =
            game::WreckwaterCharacterAuthorityBridgeStatus::Accepted;
    WreckwaterAuthorityPhysicsStepStatus physicsStatus =
        WreckwaterAuthorityPhysicsStepStatus::Submitted;
    bool authorityStarted = false;
    bool simulationAdvanced = false;
    bool failStopped = false;
};

// The runtime owns rule/network ordering. This seam owns only the physical
// world, exact checked submission, and asynchronous readback. Production uses
// RealWreckwaterAuthorityPhysics; deterministic tests provide a fixed fake.
class IWreckwaterAuthorityPhysics
    : public game::IWreckwaterPhysicsTransactions {
public:
    ~IWreckwaterAuthorityPhysics() override = default;

    [[nodiscard]] virtual bool authorityReady() const noexcept = 0;
    [[nodiscard]] virtual physics::PhysicsWorld* nativeWorld()
        noexcept = 0;
    [[nodiscard]] virtual bool enableAuthorityReadbackAndWater(
        float waterHeight) noexcept = 0;
    [[nodiscard]] virtual physics::BodyHandle spawnBody(
        const physics::BodySpawnDesc& desc) = 0;
    [[nodiscard]] virtual bool destroyBody(
        physics::BodyHandle body) = 0;
    virtual void enqueue(
        std::span<const physics::PhysicsCommand> commands) = 0;
    virtual void serviceAsync() = 0;
    [[nodiscard]] virtual WreckwaterAuthorityPhysicsStepResult
    submitExactTick(
        uint64_t expectedTick, bool requestDebugPose,
        physics::DebugSnapshotRequest debugRequest) = 0;
    [[nodiscard]] virtual std::optional<physics::PhysicsEventBatch>
    pollEvents() = 0;
    [[nodiscard]] virtual std::optional<physics::DebugSnapshot>
    pollDebugSnapshot() = 0;
};

class RealWreckwaterAuthorityPhysics final
    : public IWreckwaterAuthorityPhysics {
public:
    RealWreckwaterAuthorityPhysics(
        gpu::Context& context, physics::PhysicsWorld& world) noexcept;

    [[nodiscard]] bool authorityReady() const noexcept override;
    [[nodiscard]] physics::PhysicsWorld* nativeWorld()
        noexcept override;
    [[nodiscard]] bool enableAuthorityReadbackAndWater(
        float waterHeight) noexcept override;
    [[nodiscard]] physics::BodyHandle spawnBody(
        const physics::BodySpawnDesc& desc) override;
    [[nodiscard]] bool destroyBody(
        physics::BodyHandle body) override;
    void enqueue(
        std::span<const physics::PhysicsCommand> commands) override;
    void serviceAsync() override;
    [[nodiscard]] WreckwaterAuthorityPhysicsStepResult submitExactTick(
        uint64_t expectedTick, bool requestDebugPose,
        physics::DebugSnapshotRequest debugRequest) override;
    [[nodiscard]] std::optional<physics::PhysicsEventBatch>
    pollEvents() override;
    [[nodiscard]] std::optional<physics::DebugSnapshot>
    pollDebugSnapshot() override;

    [[nodiscard]] physics::PreparedPhysicsMutation prepare(
        const physics::PhysicsMutationBatch& batch) noexcept override;
    [[nodiscard]] physics::PhysicsMutationResult commit(
        const physics::PreparedPhysicsMutation& prepared)
        noexcept override;
    [[nodiscard]] bool discard(
        const physics::PreparedPhysicsMutation& prepared)
        noexcept override;

private:
    gpu::Context* context_ = nullptr;
    physics::PhysicsWorld* world_ = nullptr;
};

class WreckwaterAuthorityRuntime {
public:
    struct Config {
        uint64_t sessionId = 1u;
        uint64_t matchId = 1u;
        uint64_t worldId = 1u;
        uint32_t worldEpoch = 1u;
        uint32_t authorityEpoch = 1u;
        uint32_t worldEventStreamId = 1u;
        uint32_t maximumFramesPerTick = 64u;
        uint32_t maximumReadbackBatchesPerTick = 16u;
        uint32_t maximumRequestedTickLead = 8u;
        uint32_t maximumRequestedTickLag = 64u;
        uint32_t helmTimeoutTicks = 6u;
        uint32_t maximumEventReadbackLagTicks =
            kWreckwaterAuthorityMaximumEventReadbackLagTicks;
        uint32_t maximumPoseReadbackLagTicks = 12u;
        float helmMaximumForce = 24'000.0f;
        float helmMaximumYawSpeed = 0.9f;
        float waterHeight = 0.0f;
        game::WreckwaterMatch::Config match{};
        game::WreckwaterLiveWorld::Config liveWorld{};
        game::WreckwaterVesselDamageAuthority::Config damage{};
        game::WreckwaterCharacterAuthorityBridge::Config characters{};
        game::WreckwaterReplayConfig replay{
            .maximumRecords =
                kWreckwaterAuthorityReplayMaximumRecords,
            .maximumPayloadBytes =
                kWreckwaterAuthorityReplayMaximumPayloadBytes,
        };
    };

    WreckwaterAuthorityRuntime(
        Config config,
        std::unique_ptr<network::IMultiplayerTransport> transport,
        IWreckwaterAuthorityPhysics& physics);
    ~WreckwaterAuthorityRuntime();

    WreckwaterAuthorityRuntime(
        const WreckwaterAuthorityRuntime&) = delete;
    WreckwaterAuthorityRuntime& operator=(
        const WreckwaterAuthorityRuntime&) = delete;
    WreckwaterAuthorityRuntime(
        WreckwaterAuthorityRuntime&&) = delete;
    WreckwaterAuthorityRuntime& operator=(
        WreckwaterAuthorityRuntime&&) = delete;

    [[nodiscard]] bool initialize();

    // One nonblocking 60 Hz authority quantum: one transport service call,
    // bounded drains, and at most one exact GPU tick.
    [[nodiscard]] WreckwaterAuthorityTickResult tick();
    void close();
    void requestFailStop(
        WreckwaterAuthorityFailStopReason reason) noexcept;

    [[nodiscard]] bool initialized() const noexcept {
        return initialized_;
    }
    [[nodiscard]] bool started() const noexcept {
        return match_.started();
    }
    [[nodiscard]] bool frozen() const noexcept {
        return initialized_ && !match_.started() && !failStopped();
    }
    [[nodiscard]] bool failStopped() const noexcept {
        return failStopReason_
            != WreckwaterAuthorityFailStopReason::None;
    }
    [[nodiscard]] bool closed() const noexcept { return closed_; }
    [[nodiscard]] WreckwaterAuthorityFailStopReason failStopReason()
        const noexcept {
        return failStopReason_;
    }
    [[nodiscard]] uint64_t failStopTick() const noexcept {
        return failStopTick_;
    }
    [[nodiscard]] uint64_t latestCertifiedEvidenceTick()
        const noexcept {
        return latestCertifiedEvidenceTick_;
    }
    [[nodiscard]] uint64_t eventClosedThroughTick() const noexcept {
        return eventClosedThroughTick_;
    }
    [[nodiscard]] uint64_t lastPublishedSnapshotSequence()
        const noexcept {
        return lastPublishedSnapshotSequence_;
    }
    [[nodiscard]] const std::optional<
        network::WreckwaterCertifiedSnapshot>&
    lastPublishedSnapshot() const noexcept {
        return lastPublishedSnapshot_;
    }
    [[nodiscard]] WreckwaterAuthorityPhysicsStepStatus
    lastPhysicsStepStatus() const noexcept {
        return lastPhysicsStepStatus_;
    }
    [[nodiscard]] const WreckwaterAuthorityTelemetry& telemetry()
        const noexcept {
        return telemetry_;
    }
    [[nodiscard]] const game::WreckwaterMatch& match()
        const noexcept {
        return match_;
    }
    [[nodiscard]] const game::WreckwaterLiveWorld& liveWorld()
        const noexcept {
        return liveWorld_;
    }
    [[nodiscard]] const game::WreckwaterVesselDamageAuthority&
    vesselDamage() const noexcept {
        return damage_;
    }
    [[nodiscard]] const game::WreckwaterCharacterAuthorityBridge&
    characterAuthority() const noexcept {
        return characters_;
    }
    [[nodiscard]] const game::WreckwaterReplayRecorder& replay()
        const noexcept {
        return replay_;
    }
    [[nodiscard]] game::WreckwaterReplayError lastReplayError()
        const noexcept {
        return lastReplayError_;
    }
    [[nodiscard]] std::optional<WreckwaterAuthorityPeerView> peer(
        uint32_t peerId) const noexcept;

private:
    struct PeerState {
        uint32_t peerId = 0u;
        game::PlayerId playerId = 0u;
        game::CrewId crew = game::CrewId::None;
        uint32_t seat = 0u;
        game::SkiffId skiffId = 0u;
        uint64_t connectionSerial = 0u;
        game::ConnectionId connectionId = 0u;
        uint32_t connectionGeneration = 0u;
        uint32_t lastCharacterConnectionGeneration = 0u;
        game::WreckwaterCharacterHandle character =
            game::kInvalidWreckwaterCharacter;
        network::AckWindow inboundWindow{};
        uint64_t lastHelmSequence = 0u;
        bool active = false;
    };

    struct HelmState {
        int16_t throttleQ15 = 0;
        int16_t steeringQ15 = 0;
        uint64_t lastAcceptedServerTick = 0u;
        uint64_t sourceSequence = 0u;
        bool active = false;
    };

    struct PendingPose {
        uint64_t tick = 0u;
        std::array<
            physics::DebugBodyState,
            kWreckwaterAuthorityPhysicalBodyCount> bodies{};
        bool occupied = false;
    };

    struct PendingAttachmentBreak {
        uint64_t sourcePhysicsTick = 0u;
        game::WreckwaterTowBinding logical{};
        bool occupied = false;
    };

    struct PendingDamageFrame {
        uint64_t tick = 0u;
        std::array<
            game::WreckwaterContactHitEvidence,
            game::kWreckwaterMaximumContactHitsPerTick> contacts{};
        size_t contactCount = 0u;
        bool occupied = false;
    };

    struct FragmentBinding {
        game::WreckwaterFragmentHandle fragment{};
        physics::BodyHandle body{};
        uint32_t skiffId = 0u;
        uint32_t skiffGeneration = 0u;
        bool occupied = false;
    };

    struct FrameResult {
        WreckwaterAuthorityIngressError error =
            WreckwaterAuthorityIngressError::None;
        game::CommandRejectReason commandRejectReason =
            game::CommandRejectReason::None;
        network::WreckwaterCodecError codecError =
            network::WreckwaterCodecError::None;
        game::WreckwaterCharacterAuthorityBridgeStatus
            characterStatus =
                game::WreckwaterCharacterAuthorityBridgeStatus::Accepted;
        bool accepted = false;
    };

    [[nodiscard]] bool validConfig() const noexcept;
    [[nodiscard]] bool initializeMatchAndRoster();
    [[nodiscard]] bool spawnAndBindBodies();
    [[nodiscard]] bool initializeCharacterAuthority();
    [[nodiscard]] physics::BodySpawnDesc skiffSpawnDesc(
        size_t index, uint32_t skiffGeneration) const noexcept;
    [[nodiscard]] FrameResult processFrame(
        const network::MultiplayerTransportFrame& frame);
    [[nodiscard]] FrameResult processLifecycle(
        const network::MultiplayerTransportFrame& frame,
        PeerState& peer);
    [[nodiscard]] FrameResult processData(
        const network::MultiplayerTransportFrame& frame,
        PeerState& peer);
    [[nodiscard]] bool connectPeer(
        PeerState& peer, uint64_t connectionSerial);
    [[nodiscard]] bool disconnectPeer(PeerState& peer);
    [[nodiscard]] bool startIfReady();

    void drainReadbacks(WreckwaterAuthorityTickResult& result);
    [[nodiscard]] bool processEventBatch(
        const physics::PhysicsEventBatch& batch);
    [[nodiscard]] bool processDebugSnapshot(
        const physics::DebugSnapshot& snapshot);
    [[nodiscard]] bool submitCertifiedAttachmentBreaks();
    [[nodiscard]] bool closeDamageFrame(
        const PendingPose& pose,
        const PendingDamageFrame& damageFrame);
    [[nodiscard]] bool applyDamageOutputs(
        const PendingPose& pose,
        const game::WreckwaterVesselDamageTickResult& damage);
    [[nodiscard]] bool submitDamageWorldEvent(
        const game::WreckwaterDamageLifecycleIntent& intent);
    [[nodiscard]] bool reconcileDamageLifecycle();
    [[nodiscard]] bool spawnSignificantFragment(
        const PendingPose& pose,
        const game::WreckwaterDamageLifecycleIntent& intent);
    [[nodiscard]] bool retireSignificantFragment(
        const game::WreckwaterDamageLifecycleIntent& intent);
    [[nodiscard]] game::WreckwaterImpactMaterial impactMaterial(
        physics::BodyHandle body) const noexcept;
    void flushClosedPoses(WreckwaterAuthorityTickResult& result);
    [[nodiscard]] bool certifyPose(
        const PendingPose& pose,
        WreckwaterAuthorityTickResult& result);
    [[nodiscard]] bool certifyCharacters(
        const PendingPose& pose);
    [[nodiscard]] bool publishSnapshot(
        const PendingPose& pose,
        WreckwaterAuthorityTickResult& result);
    [[nodiscard]] bool recordPendingMatchEvents() noexcept;
    [[nodiscard]] network::WreckwaterCertifiedSnapshot buildSnapshot(
        const PendingPose& pose, uint64_t snapshotSequence) const;

    [[nodiscard]] bool enqueueHelmCommands(uint64_t targetTick);
    [[nodiscard]] bool simulateOneTick(
        WreckwaterAuthorityTickResult& result);
    [[nodiscard]] bool requestedTickInWindow(
        uint64_t requestedTick) const noexcept;
    [[nodiscard]] bool allPeersActive() const noexcept;
    [[nodiscard]] PeerState* peerState(uint32_t peerId) noexcept;
    [[nodiscard]] const PeerState* peerState(
        uint32_t peerId) const noexcept;
    void clearHelmForSkiff(game::SkiffId skiffId) noexcept;
    void failStop(
        WreckwaterAuthorityFailStopReason reason) noexcept;

    Config config_{};
    std::unique_ptr<network::IMultiplayerTransport> transport_;
    IWreckwaterAuthorityPhysics* physics_ = nullptr;
    game::WreckwaterMatch match_{};
    game::WreckwaterLiveWorld liveWorld_{};
    game::WreckwaterVesselDamageAuthority damage_{};
    game::WreckwaterCharacterAuthorityBridge characters_{};
    game::WreckwaterReplayRecorder replay_{};
    std::array<PeerState, kWreckwaterAuthorityPeerCount> peers_{};
    std::array<HelmState, game::kWreckwaterSkiffCount> helm_{};
    std::array<
        physics::BodyHandle,
        kWreckwaterAuthorityPhysicalBodyCount> bodies_{};
    std::array<bool, game::kWreckwaterSkiffCount>
        skiffBodyAlive_{{true, true}};
    std::array<
        physics::DebugBodyState,
        kWreckwaterAuthorityPhysicalBodyCount> lastBodyStates_{};
    std::array<
        FragmentBinding,
        game::kWreckwaterMaximumSignificantFragments>
        fragmentBindings_{};
    std::array<
        PendingPose,
        game::kWreckwaterCertifiedEvidenceFrameCount> pendingPoses_{};
    size_t pendingPoseBegin_ = 0u;
    size_t pendingPoseCount_ = 0u;
    std::array<
        PendingDamageFrame,
        game::kWreckwaterCertifiedEvidenceFrameCount>
        pendingDamageFrames_{};
    size_t pendingDamageBegin_ = 0u;
    size_t pendingDamageCount_ = 0u;
    std::array<
        PendingAttachmentBreak,
        game::kWreckwaterLiveMaximumTows> pendingAttachmentBreaks_{};
    size_t pendingAttachmentBreakCount_ = 0u;
    uint32_t pendingAttachmentBreakWaitTicks_ = 0u;
    uint64_t nextConnectionId_ = 1u;
    uint64_t nextBodyCommandSequence_ = 1u;
    uint64_t nextExpectedEventTick_ = 1u;
    uint64_t nextExpectedPoseTick_ = 1u;
    uint64_t eventClosedThroughTick_ = 0u;
    uint64_t latestCertifiedEvidenceTick_ = 0u;
    uint64_t terminalSettlementTick_ = 0u;
    uint64_t nextSnapshotSequence_ = 1u;
    uint64_t lastPublishedSnapshotSequence_ = 0u;
    size_t replayEventCursor_ = 0u;
    game::WreckwaterReplayError lastReplayError_ =
        game::WreckwaterReplayError::None;
    std::optional<network::WreckwaterCertifiedSnapshot>
        lastPublishedSnapshot_{};
    WreckwaterAuthorityPhysicsStepStatus lastPhysicsStepStatus_ =
        WreckwaterAuthorityPhysicsStepStatus::Submitted;
    WreckwaterAuthorityFailStopReason failStopReason_ =
        WreckwaterAuthorityFailStopReason::None;
    uint64_t failStopTick_ = 0u;
    WreckwaterAuthorityTelemetry telemetry_{};
    bool initialized_ = false;
    bool closed_ = false;
};

} // namespace voxy::server
