#pragma once

#include "physics/shape_handle.hpp"

#include "gpu/shader_source.hpp"
#include "gpu/webgpu_compat.hpp"

#include <glm/gtc/quaternion.hpp>
#include <glm/mat4x4.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include <cstddef>
#include <array>
#include <cmath>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace voxy::physics {

inline constexpr float kWorldSectorSize = 256.0f;
inline constexpr float kWorldSectorHalf = 0.5f * kWorldSectorSize;

// Authoritative float-mode positions never store a far absolute coordinate in
// f32. Local coordinates are canonical in [-128, 128) on every axis.
struct WorldPosition {
    glm::ivec3 sector{0};
    glm::vec3 local{0.0f};

    [[nodiscard]] constexpr auto operator<=>(
        const WorldPosition&) const noexcept = default;
};

[[nodiscard]] bool isValidWorldPosition(
    const WorldPosition& position) noexcept;
[[nodiscard]] WorldPosition canonicalWorldPosition(
    const glm::ivec3& sector, const glm::dvec3& local) noexcept;
[[nodiscard]] WorldPosition worldPositionFromAbsolute(
    const glm::dvec3& absolute) noexcept;
[[nodiscard]] glm::dvec3 worldPositionToAbsolute(
    const WorldPosition& position) noexcept;
[[nodiscard]] bool worldPositionRelativeToSector(
    const WorldPosition& position, const glm::ivec3& referenceSector,
    glm::vec3& relative, int32_t maximumSectorDelta = 1) noexcept;
[[nodiscard]] bool isRepresentableAbsolutePosition(
    const glm::vec3& position) noexcept;

enum class BackendType : uint8_t {
    JoltLegacy,
    Box3DReference,
    WebGpuSoft,
};

[[nodiscard]] const char* backendTypeName(BackendType type) noexcept;
[[nodiscard]] BackendType backendTypeFromName(
    std::string_view name,
    BackendType fallback = BackendType::JoltLegacy) noexcept;

enum class PhysicsArithmeticMode : uint8_t {
    FastFloat,
    DeterministicFloat,
    StrictLockstep,
};

[[nodiscard]] const char* physicsArithmeticModeName(
    PhysicsArithmeticMode mode) noexcept;

enum class JoltJobSystemMode : uint8_t {
    SingleThreaded,
    ThreadPool,
};

[[nodiscard]] const char* joltJobSystemModeName(
    JoltJobSystemMode mode) noexcept;
[[nodiscard]] JoltJobSystemMode joltJobSystemModeFromName(
    std::string_view name,
    JoltJobSystemMode fallback =
        JoltJobSystemMode::ThreadPool) noexcept;

struct BackendCapabilities {
    bool gpuResidentState = false;
    bool directRenderView = false;
    bool synchronousCharacter = false;
    bool deterministicFloat = false;
    bool lockstep = false;
    bool bodyBodyContacts = false;
    bool continuousCollision = false;
    bool asynchronousQueries = false;
    bool eventReadback = false;
    // GPU-resident, tension-only distance attachments with winch and
    // deterministic break events. CPU reference backends intentionally leave
    // this false until they implement the same stable-handle contract.
    bool distanceAttachments = false;
    // A complete gameplay mutation can be validated and staged without
    // changing live host state, then committed without further allocation or
    // validation. This is the authority boundary used by the live server.
    bool atomicMutationBatches = false;
    // The server may advance an exact number of fixed ticks without routing
    // through a wall-clock accumulator.
    bool fixedTickScheduling = false;
    // GPU encoding returns a synchronous success/fail-stop report instead of
    // relying on logs from the legacy render-loop entry point.
    bool checkedGpuEncoding = false;
};

struct PhysicsInitContext {
    BackendType requestedBackend = BackendType::JoltLegacy;
    // Opt-in recovery for a missing or rejected WebGPU device. The fallback is
    // always the pinned Box3D reference backend; it is never a silent default.
    bool allowCpuFallback = false;
    WGPUDevice device = nullptr;
    WGPUQueue queue = nullptr;
    uint32_t maxBodies = 16'384;
    uint32_t maxActiveBodies = 16'384;
    uint32_t maxPairs = 65'536;
    // Zero derives a four-to-one broad-phase candidate budget from maxPairs.
    uint32_t maxCandidatePairs = 0;
    uint32_t maxContacts = 16'384;
    uint32_t maxManifolds = 65'536;
    bool enableValidation = false;

    // Native Jolt uses its worker pool by default. WASM overrides this because
    // the browser build is currently compiled without pthreads.
    JoltJobSystemMode joltJobSystem = JoltJobSystemMode::ThreadPool;
    // Zero lets Jolt select the native worker count.
    uint32_t joltWorkerThreads = 0;

    // Box3D owns an internal scheduler when this is greater than one.
    // The reference baseline stays single-threaded by default.
    uint32_t box3dWorkerThreads = 1;

    struct GpuConfig {
        // Fresh-world semantic restore base, before any commands. Not a live
        // clock setter. The host must validate and own the saved world first.
        uint64_t initialTick = 0;
        glm::vec3 gravity{0.0f, -9.81f, 0.0f};
        float fixedTickSeconds = 1.0f / 60.0f;
        // Presentation-only history; authoritative/headless worlds need none.
        bool enableRenderInterpolation = false;
        float linearDamping = 0.05f;
        float angularDamping = 0.05f;
        float maximumLinearSpeed = 500.0f;
        float maximumAngularSpeed = 100.0f;
        float bodyFriction = 0.65f;
        // Jolt combines the 0.65 body friction with its default 0.20 terrain
        // friction using the geometric mean.
        float terrainFriction = 0.36055514f;
        float bodySphereRestitution = 0.55f;
        float bodyOtherRestitution = 0.25f;
        // The four-substep terrain solver's effective bounce matches Jolt's
        // 0.55 sphere material at this calibrated impulse coefficient.
        float terrainSphereRestitution = 0.38f;
        float terrainOtherRestitution = 0.20f;
        float linearSlop = 0.005f;
        float speculativeDistance = 0.02f;
        // Production keeps the complete dynamic world enabled. Earlier
        // ballistic/static phase gates disable this to isolate their stated
        // terrain, water, and direct-render workloads.
        bool enableBodyBodyContacts = true;
        // Must evenly divide the 256 m world sector for sector-aware grid keys.
        float broadPhaseCellSize = 4.0f;
        float waterBuoyancy = 1.08f;
        float waterLinearDrag = 0.55f;
        float waterAngularDrag = 0.08f;
        uint32_t substeps = 4;
        uint32_t authoredContactPatches = 1; // Explicit bounded compound-patch opt-in (1..8).
        uint32_t solverWorkgroupSize = 128;
        uint32_t solverColorCount = 32;
        // The first colors use separate indirect dispatches. The remainder
        // use one compact dispatch to reduce browser command-stream overhead.
        uint32_t solverParallelColorCount = 4;
        uint32_t maximumCatchUpTicks = 8;
        uint32_t commandCapacity = 262'144;
        // Slot zero is reserved as the invalid attachment handle.
        uint32_t attachmentCapacity = 1'024;
        uint32_t attachmentCommandCapacity = 4'096;
        uint32_t debugReadbackSlots = 3;
        uint32_t debugReadbackBodyCapacity = 4'096;
        uint32_t debugReadbackAttachmentCapacity = 16;
        uint32_t asyncQueryCapacity = 256;
        uint32_t asyncQueryReadbackSlots = 3;
        // Zero uses maximumCatchUpTicks so one encoded catch-up batch fits.
        uint32_t eventReadbackSlots = 0;
        // Zero derives the worst-case event count from the world capacities.
        // A smaller explicit stream budget reports overflow, never truncation
        // accepted as complete evidence.
        uint32_t eventReadbackCapacity = 0;
        // Requires the optional WebGPU timestamp-query feature. Unsupported
        // devices keep running and simply return no timing batches.
        bool enableStageProfiling = false;
        uint32_t stageProfilingReadbackSlots = 3;
        // One profiles every encoded batch. Larger values keep the most recent
        // result cached and wait this many simulation ticks before sampling
        // again.
        uint32_t stageProfilingIntervalTicks = 1;
        // Conversion for timestamp-query device ticks. Browser WebGPU uses
        // nanoseconds. Native Vulkan callers must supply the adapter's
        // VkPhysicalDeviceLimits::timestampPeriod when it is not 1 ns.
        double stageProfilingTimestampPeriodNanoseconds = 1.0;
        // Telemetry is useful for tests and diagnostics. Normal gameplay uses
        // a larger interval because mapping every tick is unnecessarily noisy.
        bool enableTelemetryReadback = true;
        uint32_t telemetryReadbackSlots = 3;
        uint32_t telemetryReadbackIntervalTicks = 1;
        uint32_t ccdBulletCapacity = 1'024;
        uint32_t ccdWorkgroupSize = 128;
        uint32_t ccdCoarseSteps = 16;
        uint32_t ccdBisectionIterations = 8;
        float ccdFastDistanceRatio = 0.5f;
        std::string shaderPath = "shaders/physics_ballistic.wgsl";
        std::string narrowPhaseShaderPath; // Empty uses the standard narrow-phase shader.
        // A nonempty bundle is a strict trusted-source policy. Every WGSL
        // requested by WebGpuSoft must exist in it; filesystem fallback is
        // disabled. The caller owns the source storage for this world.
        std::span<const gpu::ShaderSource> shaderSources{};
    } gpu;
};

enum class PhysicsGpuStage : uint32_t {
    CommandsAndActiveCompaction = 0,
    ContinuousCollision,
    ForcesAndWater,
    BroadPhaseIndexBuild,
    BroadPhaseIndexSortRanges,
    BroadPhasePairCount,
    BroadPhasePairScatter,
    BroadPhasePairSortUnique,
    BroadPhaseLifecycle,
    NarrowPhaseBucketing,
    NarrowPhaseCollision,
    DynamicSolverColoring,
    DynamicSolverGraph,
    DynamicSolverSolve,
    StaticContacts,
    IslandsAndSleeping,
    TickFinalize,
    Count,
};

inline constexpr size_t kPhysicsGpuStageCount =
    static_cast<size_t>(PhysicsGpuStage::Count);

[[nodiscard]] const char* physicsGpuStageName(PhysicsGpuStage stage) noexcept;

struct PhysicsGpuStageTiming {
    uint64_t tick = 0;
    double timestampPeriodNanoseconds = 1.0;
    std::array<uint64_t, kPhysicsGpuStageCount> timestampTicks{};
    std::array<double, kPhysicsGpuStageCount> milliseconds{};

    [[nodiscard]] double totalMilliseconds() const noexcept;
};

// A single shape is used for every bounded physics resource so diagnostics do
// not accidentally omit one of the four values required by the capacity
// contract. A zero capacity means that the category is measured but unbounded
// by a dedicated allocation (for example, a derived diagnostic count).
struct PhysicsCapacityUsage {
    uint32_t current = 0;
    uint32_t capacity = 0;
    uint32_t highWater = 0;
    bool overflow = false;
};

// Owned GPU-submission interval only. Before baseTick, no evidence is asserted.
// completed requires both validated queue completion and a matching GPU counter
// readback; encoding alone never advances it. Pose/event packets retain their
// own ticks and must also be available before gameplay consumes their evidence.
struct PhysicsTickFrontier {
    bool supported = false, failed = false, backpressured = false;
    uint64_t incarnation = 0, baseTick = 0;
    uint64_t scheduled = 0, encoded = 0, submitted = 0, completed = 0;
    uint32_t maximumPendingTicks = 0, maximumInFlightTicks = 0, pendingBatches = 0;
};

struct PhysicsStats {
    BackendType backend = BackendType::JoltLegacy;
    PhysicsArithmeticMode arithmeticMode = PhysicsArithmeticMode::FastFloat;
    uint32_t substeps = 0;
    float fixedTickSeconds = 0.0f;
    uint32_t maximumCatchUpTicks = 0;
    float broadPhaseCellSize = 0.0f;
    uint32_t solverWorkgroupSize = 0;
    uint32_t solverColorCount = 0;
    uint32_t solverParallelColorCount = 0;
    uint32_t residentBodies = 0;
    uint32_t activeBodies = 0;
    uint32_t sleepingBodies = 0;
    uint32_t bodyCapacity = 0;
    uint32_t pairCapacity = 0;
    uint32_t contactCapacity = 0;
    uint32_t manifoldCapacity = 0;
    uint32_t queryCapacity = 0;
    uint32_t eventCapacity = 0;
    uint32_t workerConcurrency = 0;
    uint64_t telemetryTick = 0;
    uint32_t highGridEntries = 0;
    uint32_t highOccupiedCells = 0;
    uint32_t highCandidatePairs = 0;
    uint32_t highPairs = 0;
    uint32_t highContacts = 0;
    uint32_t highManifolds = 0;
    uint32_t highManifoldPoints = 0;
    uint32_t highSolverOverflowContacts = 0;
    uint32_t highIslands = 0;
    uint32_t highSleepingBodies = 0;
    uint32_t highSleepingGridEntries = 0;
    size_t estimatedPersistentBytes = 0;
    size_t scratchBytes = 0;
    bool bodyCapacityOverflow = false;
    bool pairCapacityOverflow = false;
    bool contactCapacityOverflow = false;
    bool eventCapacityOverflow = false;
    bool sleepingGridOverflow = false;

    // Canonical capacity diagnostics. The older scalar fields above remain for
    // API compatibility; these records are what overlays and benchmark output
    // should consume.
    PhysicsCapacityUsage residentBodyUsage{};
    PhysicsCapacityUsage activeBodyUsage{};
    PhysicsCapacityUsage commandUsage{};
    PhysicsCapacityUsage gridEntryUsage{};
    PhysicsCapacityUsage candidatePairUsage{};
    PhysicsCapacityUsage uniquePairUsage{};
    PhysicsCapacityUsage contactUsage{};
    PhysicsCapacityUsage manifoldUsage{};
    PhysicsCapacityUsage terrainContactUsage{};
    PhysicsCapacityUsage overflowConstraintUsage{};
    PhysicsCapacityUsage eventUsage{};
    PhysicsCapacityUsage visibleBodyUsage{};
    PhysicsCapacityUsage sleepingGridUsage{};
    PhysicsCapacityUsage bulletUsage{};
    PhysicsCapacityUsage waterSampleUsage{};
    PhysicsCapacityUsage attachmentUsage{};
    PhysicsCapacityUsage attachmentCommandUsage{};

    uint32_t kinematicBodies = 0;
    uint32_t occupiedCells = 0;
    uint32_t maximumCellBodies = 0;
    uint32_t activeSleepingPairs = 0;
    uint32_t oversizedBodies = 0;
    uint32_t broadPhasePairDrivingBodies = 0;
    uint32_t persistentContacts = 0;
    std::array<uint32_t, 10> narrowPairClasses{};
    std::array<uint32_t, 10> narrowCollisionPairClasses{};
    uint32_t manifoldPoints = 0;
    uint32_t speculativeManifolds = 0;
    uint32_t invalidManifolds = 0;
    uint32_t terrainContactBodies = 0;
    uint32_t maximumTerrainContactsPerBody = 0;
    uint32_t authoredTerrainFailures = 0;
    uint32_t authoredTerrainCells = 0;
    uint32_t authoredAdmissionFailures = 0;
    uint32_t submergedBodies = 0;
    uint32_t compactIslandContacts = 0;
    uint32_t compactIslandBodies = 0;
    bool serialWorldSolver = false;
    uint32_t activeGraphColors = 0;
    uint32_t maximumBodyDegree = 0;
    uint32_t colorConflictErrors = 0;
    uint32_t islandCount = 0;
    uint32_t awakeIslands = 0;
    uint32_t sleepingIslands = 0;
    uint32_t maximumIslandBodies = 0;
    uint32_t sleepTransitions = 0;
    uint32_t wakeTransitions = 0;
    uint32_t sleepingGridCells = 0;
    uint32_t islandRootErrors = 0;
    uint32_t ccdHits = 0;
    uint32_t ccdStalls = 0;
    uint32_t ccdFailures = 0;
    uint32_t ccdMaximumIterations = 0;
    uint32_t contactBeginEvents = 0;
    uint32_t contactEndEvents = 0;
    uint32_t islandEvents = 0;
    uint32_t tautAttachments = 0;
    uint32_t slackAttachments = 0;
    uint32_t attachmentBreakEvents = 0;
    uint32_t staleAttachmentCommands = 0;
    uint32_t invalidAttachmentEndpoints = 0;
    uint64_t gpuUploadBytes = 0;
    uint64_t gpuReadbackBytes = 0;
    uint64_t worldHash = 0;
    uint64_t islandHash = 0;
    uint32_t deviceMaxStorageBuffersPerShaderStage = 0;
    uint32_t deviceMaxComputeInvocationsPerWorkgroup = 0;
    uint64_t deviceMaxStorageBufferBindingSize = 0;
    uint64_t deviceMaxBufferSize = 0;
    bool activeCapacityOverflow = false;
    bool commandCapacityOverflow = false;
    bool manifoldCapacityOverflow = false;
    bool bulletCapacityOverflow = false;
    bool attachmentCapacityOverflow = false;
    bool attachmentCommandCapacityOverflow = false;
};

struct PhysicsStepStats {
    double totalMs = 0.0;
    double waterMs = 0.0;
    double simulationMs = 0.0;
    uint32_t substepCount = 0;
};

enum class ThrowableShape : uint32_t {
    Sphere = 0,
    Cube,
    Box,
    Capsule,
    Cylinder,
    Count,
};

struct BodyHandle {
    uint32_t index = 0;
    uint32_t generation = 0;

    [[nodiscard]] constexpr bool valid() const noexcept { return index != 0; }
    [[nodiscard]] constexpr auto operator<=>(const BodyHandle&) const noexcept = default;
};

struct AttachmentHandle {
    uint32_t index = 0;
    uint32_t generation = 0;

    [[nodiscard]] constexpr bool valid() const noexcept { return index != 0; }
    [[nodiscard]] constexpr auto operator<=>(
        const AttachmentHandle&) const noexcept = default;
};

// A tension-only rope between two resident bodies. Positive motor speed reels
// in; negative speed pays out. The target is clamped to [minimumLength,
// maximumLength] once per fixed tick. breakForce == 0 makes the rope
// unbreakable. maximumForce caps the impulse actually applied, while break
// evidence reports the uncapped force required by the constraint.
// A broken rope stays allocated but inert until destroyAttachment() is called;
// optional event readback never changes host-side handle lifetime.
struct DistanceAttachmentDesc {
    BodyHandle bodyA{};
    BodyHandle bodyB{};
    glm::vec3 localAnchorA{0.0f};
    glm::vec3 localAnchorB{0.0f};
    float targetLength = 1.0f;
    float minimumLength = 0.0f;
    float maximumLength = 1'000'000.0f;
    float motorSpeed = 0.0f;
    float maximumForce = 1'000'000.0f;
    float breakForce = 0.0f;
    // Axial spring compliance in metres/newton, with critical damping based
    // on the current endpoint effective mass. Zero preserves the hard rope.
    float springCompliance = 0.0f;
};

// Reserved material tag for a bounded box + up to eight studs, WebGpuSoft only.
// The low 28 bits retain the primitive plastic material encoding.
inline constexpr uint32_t kLegoBrickMaterial = 0xb0000000u;
// Collision-only GPU bodies, such as the player capsule. No visible primitive.
inline constexpr uint32_t kInvisiblePhysicsMaterial = 0xc0000000u;

struct PhysicsMaterial {
    float friction = 0.65f;
    float restitution = 0.25f;
    float rollingResistance = 0.01f;
    // Resident gameplay metadata. BodySpawnDesc::inverseMass remains the
    // authoritative mass input; changing density does not silently resize it.
    float density = 1.0f;
    // Application bits, except the reserved LEGO/invisible material tags
    // used by the GPU collision and rendering paths.
    uint32_t flags = 0u;
};

struct BodySpawnDesc {
    ThrowableShape shape = ThrowableShape::Sphere;
    glm::vec3 position{0.0f};
    glm::quat orientation{1.0f, 0.0f, 0.0f, 0.0f};
    glm::vec3 linearVelocity{0.0f};
    glm::vec3 angularVelocity{0.0f};
    glm::vec3 dimensions{1.0f};
    float inverseMass = 1.0f;
    bool bullet = false;
    // Null selects the backend configuration (including the sphere-specific
    // restitution). Friction, restitution, and rolling resistance are consumed
    // by contact solving; density and flags remain resident metadata.
    std::optional<PhysicsMaterial> material;
    // position is sector-local. Legacy callers leave sector at zero; spawn
    // canonicalization migrates out-of-range local values automatically.
    glm::ivec3 sector{0};
};

enum class PhysicsCommandType : uint32_t {
    SpawnBody,
    DestroyBody,
    ApplyImpulse,
    ApplyForce,
    SetVelocity,
    SetAngularVelocity,
    Teleport,
    SetMaterial,
    Wake,
    Sleep,
    SetKinematicTarget,
    // a.xyz is a world-space force. b.xyz is a body-local application point.
    // The checked GPU path applies both F and r x F for exactly one tick.
    ApplyForceAtLocalPoint,
    // Internal typed admission only; generic command replay/enqueue rejects it.
    SpawnAuthoredBody,
};

// Plain command payloads keep mutation replayable and make GPU upload packing
// explicit. A target tick of zero means the backend's next scheduled tick.
struct PhysicsCommand {
    PhysicsCommandType type = PhysicsCommandType::Wake;
    BodyHandle body{};
    uint64_t targetTick = 0;
    uint64_t sequence = 0;
    ThrowableShape shape = ThrowableShape::Sphere;
    glm::vec4 a{0.0f};
    glm::vec4 b{0.0f};
    glm::vec4 c{0.0f};
    glm::vec4 d{0.0f};
    glm::vec4 e{0.0f};
    glm::ivec3 sector{0};
    // SpawnBody may leave this null to select backend defaults.
    // SetMaterial requires a value.
    std::optional<PhysicsMaterial> material;
};

// A caller-supplied expected boundary, checked against actual owned GPU
// completion. This admits the accumulator-driven cove only between fully
// joined ticks. It does not certify the caller's gameplay pose/event snapshot.
struct PhysicsMutationJoin {
    uint64_t incarnation = 0;
    uint64_t completedTick = 0;
};

// The live authority prepares one complete gameplay mutation before it lets
// match state commit. Attachment destroys are staged before creates, so a
// transfer can reuse a full-capacity slot with the next generation.
struct PhysicsMutationBatch {
    std::span<const PhysicsCommand> bodyCommands{};
    std::span<const AttachmentHandle> attachmentDestroys{};
    std::span<const DistanceAttachmentDesc> attachmentCreates{};
    // Retire already encoded parent lifetimes together. The caller reserves
    // every replacement first; this never cancels or reuses an unexecuted spawn.
    // Commands/created attachments cannot target a body retired by this batch.
    std::span<const BodyHandle> bodyDestroys{};
    std::optional<PhysicsMutationJoin> joinedBoundary{};
};

enum class PhysicsMutationStatus : uint32_t {
    Prepared = 0u,
    Committed,
    Unsupported,
    NotInitialized,
    EmptyBatch,
    InvalidInput,
    CapacityExceeded,
    PendingMutation,
    PendingPhysicsTicks,
    IncompatibleSchedulingMode,
    InvalidToken,
    TokenExhausted,
    JoinedBoundaryUnavailable,
};

// This is an opaque authorization token plus a borrowed view of the stable
// handles reserved for create requests, in request order. The view remains
// valid only until this token is committed/discarded or another preparation is
// attempted. Copying a token does not duplicate the transaction.
struct PreparedPhysicsMutation {
    PhysicsMutationStatus status = PhysicsMutationStatus::Unsupported;
    uint64_t token = 0u;
    uint64_t targetTick = 0u;
    std::span<const AttachmentHandle> createdAttachments{};

    [[nodiscard]] bool ready() const noexcept {
        return status == PhysicsMutationStatus::Prepared && token != 0u;
    }
    [[nodiscard]] explicit operator bool() const noexcept { return ready(); }
};

struct PhysicsMutationResult {
    PhysicsMutationStatus status = PhysicsMutationStatus::Unsupported;
    uint64_t targetTick = 0u;
    uint32_t bodyCommandCount = 0u;
    uint32_t destroyedAttachmentCount = 0u;
    uint32_t createdAttachmentCount = 0u;
    uint32_t destroyedBodyCount = 0u;

    [[nodiscard]] bool committed() const noexcept {
        return status == PhysicsMutationStatus::Committed;
    }
    [[nodiscard]] explicit operator bool() const noexcept {
        return committed();
    }
};

enum class PhysicsEncodeStatus : uint32_t {
    Encoded = 0u,
    NothingScheduled,
    Unsupported,
    NotInitialized,
    InvalidEncoder,
    PreparedMutationPending,
    UploadFailed,
    PipelineFailed,
    EventReadbackUnavailable,
    DebugReadbackUnavailable,
    AuthoredSubmissionRequired,
    WaterFrameUnavailable,
};

struct PhysicsEncodeReport {
    PhysicsEncodeStatus status = PhysicsEncodeStatus::Unsupported;
    uint64_t firstTick = 0u;
    uint64_t finalTick = 0u;
    uint32_t tickCount = 0u;
    uint64_t gpuUploadBytes = 0u;
    uint64_t gpuReadbackBytes = 0u;
    // Once true, the backend cannot be used again without initialize().
    bool failStopped = false;

    [[nodiscard]] bool succeeded() const noexcept {
        return status == PhysicsEncodeStatus::Encoded
            || status == PhysicsEncodeStatus::NothingScheduled;
    }
    [[nodiscard]] explicit operator bool() const noexcept {
        return succeeded();
    }
};

struct PhysicsRenderView {
    WGPUBuffer poseBuffer = nullptr;
    WGPUBuffer shapeBuffer = nullptr;
    // Optional packed sector/generation/flags data. GPU physics provides this
    // so rendering can rebase canonical local poses into the camera sector.
    // CPU compatibility uploads already contain render-frame poses and leave
    // this null.
    WGPUBuffer metadataBuffer = nullptr;
    // Optional fixed-tick history. Alpha blends previous to current poses;
    // large discontinuities snap to current instead of showing a sweep.
    WGPUBuffer previousPoseBuffer = nullptr;
    WGPUBuffer previousMetadataBuffer = nullptr;
    float interpolationAlpha = 1.0f;
    float maximumInterpolationDistance = 0.0f;
    WGPUBuffer activeBodyIds = nullptr;
    WGPUBuffer visibleBodyIds = nullptr;
    WGPUBuffer perShapeRanges = nullptr;
    WGPUBuffer indirectDrawArgs = nullptr;
    uint32_t residentBodyCapacity = 0;
    uint32_t shapeCount = 0;
    // Present only during a declared authored submission; geometry/mass remain
    // owned by the world's shape pool until the actual queue use completes.
    WGPUBuffer authoredShapeBuffer = nullptr;

    [[nodiscard]] bool valid() const noexcept {
        return poseBuffer != nullptr && shapeBuffer != nullptr
            && residentBodyCapacity != 0;
    }
};

struct TerrainGpuResources {
    WGPUTextureView maxHeightTexture = nullptr;
    uint32_t mipLevelCount = 0;
};

// GPU-resident water displacement shared with rendering. The view must be a
// filterable 2D array. Layers 0..1 contain broad/detail displacement and layers
// 2..3 contain their displaced-surface normals. Handles are borrowed and must
// outlive the binding (or be cleared before their owner is destroyed).
struct WaterGpuResources {
    WGPUTextureView displacementTexture = nullptr;
    WGPUSampler displacementSampler = nullptr;
    float strength = 1.0f;
    float broadPatchLength = 1949.0f;
    float detailPatchLength = 326.0f;

    [[nodiscard]] bool valid() const noexcept {
        return displacementTexture != nullptr
            && displacementSampler != nullptr
            && std::isfinite(strength) && strength >= 0.0f
            && std::isfinite(broadPatchLength) && broadPatchLength > 0.0f
            && std::isfinite(detailPatchLength) && detailPatchLength > 0.0f;
    }
};

// Exact phase of the shared texture and analytical swells for a bounded cove
// frame. A frame contains zero or one physics tick; general catch-up needs a
// separate field per tick and must not reuse this binding for a whole batch.
struct WaterGpuFrame {
    uint64_t incarnation = 0;
    uint64_t tick = 0;
    float phaseSeconds = 0;
};

struct DebugSnapshotRequest {
    uint32_t firstBody = 1;
    uint32_t bodyCount = 0;
    // Optional exact range. An invalid/oversized range refuses the entire
    // request; zero count retains the existing bodies-only observation path.
    uint32_t firstAttachment = 0;
    uint32_t attachmentCount = 0;
};

struct DebugBodyState {
    BodyHandle handle{};
    ShapeHandle authoredShape{};
    glm::vec3 position{0.0f};
    glm::quat orientation{1.0f, 0.0f, 0.0f, 0.0f};
    glm::vec3 linearVelocity{0.0f};
    glm::vec3 angularVelocity{0.0f};
    glm::vec3 dimensions{0.0f};
    glm::vec3 inverseInertia{0.0f};
    PhysicsMaterial material{};
    ThrowableShape shape = ThrowableShape::Sphere;
    bool alive = false;
    bool awake = false;
    uint32_t staticContactCount = 0;
    bool terrainRejectedByMip = false;
    bool submerged = false;
    bool kinematic = false;
    uint32_t runtimeFlags = 0u;
    glm::ivec3 sector{0};
};

struct DebugAttachmentState {
    AttachmentHandle handle{};
    DistanceAttachmentDesc distance{};
    bool alive = false, broken = false;
    uint64_t breakTick = 0;
    uint32_t breakReason = 0;
    float requiredImpulse = 0, requiredForce = 0, measuredDistance = 0;
};

struct DebugSnapshot {
    uint64_t tick = 0;
    std::vector<DebugBodyState> bodies;
    uint64_t confirmedIncarnation = 0; // Zero means no owned completion proof.
    // Same post-solve tick, submission and readback packet as bodies. These
    // transient handles must be resolved to durable IDs before serialization.
    std::vector<DebugAttachmentState> attachments;
};

enum class PhysicsQueryType : uint32_t {
    RayCast = 0,
    OverlapSphere,
    SphereCast,
    CapsuleCast,
};

// Query filters are exclusions so the default value continues to include
// every live body.  Static bodies have zero inverse mass; awake/sleeping and
// bullet state are read from the resident GPU metadata at query execution.
enum PhysicsQueryFlag : uint32_t {
    PhysicsQueryExcludeStatic = 1u << 0u,
    PhysicsQueryExcludeDynamic = 1u << 1u,
    PhysicsQueryExcludeSleeping = 1u << 2u,
    PhysicsQueryExcludeAwake = 1u << 3u,
    PhysicsQueryExcludeBullets = 1u << 4u,
};

inline constexpr uint32_t kPhysicsQueryKnownFlags =
    PhysicsQueryExcludeStatic | PhysicsQueryExcludeDynamic
    | PhysicsQueryExcludeSleeping | PhysicsQueryExcludeAwake
    | PhysicsQueryExcludeBullets;

struct PhysicsQueryRequest {
    uint32_t requestId = 0;
    PhysicsQueryType type = PhysicsQueryType::RayCast;
    uint32_t maximumHits = 1;
    uint32_t flags = 0;
    glm::vec3 origin{0.0f};
    float radius = 0.0f;
    glm::vec3 direction{0.0f, 0.0f, 1.0f};
    float maximumDistance = 0.0f;
    glm::vec3 capsuleAxis{0.0f, 1.0f, 0.0f};
    float capsuleHalfHeight = 0.0f;
    glm::ivec3 sector{0};
};

struct PhysicsQueryHit {
    uint32_t requestId = 0;
    uint32_t bodyIndex = 0;
    uint32_t bodyGeneration = 0;
    uint32_t featureId = 0;
    PhysicsQueryType type = PhysicsQueryType::RayCast;
    float fraction = 0.0f;
    float distance = 0.0f;
    glm::vec3 point{0.0f};
    glm::vec3 normal{0.0f};
    glm::ivec3 sector{0};

    [[nodiscard]] constexpr BodyHandle bodyHandle() const noexcept {
        return BodyHandle{bodyIndex, bodyGeneration};
    }
};

struct PhysicsQueryOutput {
    uint32_t requestId = 0;
    PhysicsQueryType type = PhysicsQueryType::RayCast;
    bool overflow = false;
    std::vector<PhysicsQueryHit> hits;
};

struct PhysicsQueryBatch {
    uint64_t tick = 0;
    std::vector<PhysicsQueryOutput> outputs;
};

enum class PhysicsEventType : uint32_t {
    ContactBegin = 1,
    ContactEnd = 2,
    ContactHit = 3,
    IslandSleep = 4,
    IslandWake = 5,
    AttachmentBreak = 6,
};

struct PhysicsEvent {
    uint64_t tick = 0;
    PhysicsEventType type = PhysicsEventType::ContactBegin;
    uint32_t bodyA = 0;
    uint32_t bodyB = 0;
    uint32_t bodyGenerationA = 0;
    uint32_t bodyGenerationB = 0;
    uint32_t featureId = 0;
    uint32_t otherFeatureId = 0;
    uint32_t sourceId = 0;
    uint32_t auxiliaryCount = 0;
    uint32_t flags = 0;
    // ContactHit evidence follows bodyA/bodyB canonical order. normal points
    // from A toward B. impulse is the summed normal impulse over all manifold
    // points and impactSpeed is the greatest pre-solve closing speed.
    glm::vec3 localAnchorA{0.0f};
    glm::vec3 localAnchorB{0.0f};
    glm::vec3 normalAtoB{0.0f};
    float impactSpeed = 0.0f;
    // ContactHit: summed normal impulse. AttachmentBreak: uncapped impulse.
    float impulse = 0.0f;
    // AttachmentBreak only: uncapped force that made the rope fail.
    float force = 0.0f;

    [[nodiscard]] constexpr BodyHandle bodyHandleA() const noexcept {
        return BodyHandle{bodyA, bodyGenerationA};
    }
    [[nodiscard]] constexpr BodyHandle bodyHandleB() const noexcept {
        return BodyHandle{bodyB, bodyGenerationB};
    }
    [[nodiscard]] constexpr AttachmentHandle attachmentHandle() const noexcept {
        return type == PhysicsEventType::AttachmentBreak
            ? AttachmentHandle{sourceId, featureId}
            : AttachmentHandle{};
    }
};

struct PhysicsEventBatch {
    uint64_t tick = 0;
    bool overflow = false;
    std::vector<PhysicsEvent> events;
    uint64_t confirmedIncarnation = 0;
};

using CharacterHandle = uint32_t;
inline constexpr CharacterHandle InvalidCharacter = 0;
inline constexpr uint32_t kMaximumCharacterSlots = 0xffffu;

[[nodiscard]] inline constexpr CharacterHandle makeCharacterHandle(
    uint32_t zeroBasedSlot, uint16_t generation) noexcept {
    return zeroBasedSlot >= kMaximumCharacterSlots
        ? InvalidCharacter
        : (static_cast<uint32_t>(generation) << 16u)
            | (zeroBasedSlot + 1u);
}

[[nodiscard]] inline constexpr uint32_t characterHandleSlot(
    CharacterHandle handle) noexcept {
    const uint32_t encoded = handle & kMaximumCharacterSlots;
    return encoded == 0u ? kMaximumCharacterSlots : encoded - 1u;
}

[[nodiscard]] inline constexpr uint16_t characterHandleGeneration(
    CharacterHandle handle) noexcept {
    return static_cast<uint16_t>(handle >> 16u);
}

struct DynamicBodySnapshot {
    ThrowableShape shape = ThrowableShape::Sphere;
    glm::vec3 position{0.0f};
    glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
    glm::vec3 dimensions{1.0f};
    // Rendering hint only. Transform fields remain authoritative state.
    bool active = false;
    glm::ivec3 sector{0};
    // Optional presentation tint. Zero alpha retains the existing shape palette.
    glm::vec4 color{0.0f};
};

struct DynamicBodyReadStats {
    size_t bodyCount = 0;
    size_t lockedBodyCount = 0;
    size_t cachedBodyCount = 0;
};

struct WaterSurfaceSample {
    float heightOffset = 0.0f;
    glm::vec2 slope{0.0f};
    glm::vec3 velocity{0.0f};
};

using WaterSurfaceSampler =
    std::function<WaterSurfaceSample(glm::vec2 position, float timeSeconds)>;

struct CharacterSettings {
    float radius = 0.4f;
    float height = 1.8f;
    float maxSlopeAngleDegrees = 45.0f;
    float stepUp = 0.5f;
    float stepDown = 0.5f;
};

// Character shapes must fit inside one canonical sector. Besides keeping the
// three backends consistent, this prevents malformed finite values from
// overflowing shape construction and collision arithmetic.
[[nodiscard]] CharacterSettings sanitizeCharacterSettings(
    const CharacterSettings& settings) noexcept;

struct CharacterMotion {
    glm::vec3 position{0.0f}; // Feet position local to sector.
    glm::vec3 velocity{0.0f};
    glm::vec3 groundNormal{0.0f, 1.0f, 0.0f};
    bool grounded = false;
    bool onSteepGround = false;
    glm::ivec3 sector{0};
};

[[nodiscard]] const char* throwableShapeName(ThrowableShape shape) noexcept;
[[nodiscard]] glm::vec3 throwableShapeDimensions(ThrowableShape shape) noexcept;

} // namespace voxy::physics
