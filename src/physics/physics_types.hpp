#pragma once

#include "gpu/webgpu_compat.hpp"

#include <glm/gtc/quaternion.hpp>
#include <glm/mat4x4.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include <cstddef>
#include <array>
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
        JoltJobSystemMode::SingleThreaded) noexcept;

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
    uint32_t maxContacts = 16'384;
    uint32_t maxManifolds = 65'536;
    bool enableValidation = false;

    // Baseline-only tuning. Gameplay remains single-threaded by default.
    JoltJobSystemMode joltJobSystem = JoltJobSystemMode::SingleThreaded;
    // Zero lets Jolt select the native worker count.
    uint32_t joltWorkerThreads = 0;

    // Box3D owns an internal scheduler when this is greater than one.
    // The reference baseline stays single-threaded by default.
    uint32_t box3dWorkerThreads = 1;

    struct GpuConfig {
        glm::vec3 gravity{0.0f, -9.81f, 0.0f};
        float fixedTickSeconds = 1.0f / 60.0f;
        float linearDamping = 0.05f;
        float angularDamping = 0.05f;
        float maximumLinearSpeed = 500.0f;
        float maximumAngularSpeed = 100.0f;
        float terrainFriction = 0.65f;
        float terrainRestitution = 0.20f;
        float linearSlop = 0.005f;
        float speculativeDistance = 0.02f;
        // Must evenly divide the 256 m world sector for sector-aware grid keys.
        float broadPhaseCellSize = 4.0f;
        float waterBuoyancy = 1.05f;
        float waterLinearDrag = 1.5f;
        float waterAngularDrag = 0.8f;
        uint32_t substeps = 4;
        uint32_t maximumCatchUpTicks = 8;
        uint32_t commandCapacity = 262'144;
        uint32_t debugReadbackSlots = 3;
        uint32_t debugReadbackBodyCapacity = 4'096;
        uint32_t asyncQueryCapacity = 256;
        uint32_t asyncQueryReadbackSlots = 3;
        // Zero uses maximumCatchUpTicks so one encoded catch-up batch fits.
        uint32_t eventReadbackSlots = 0;
        // Requires the optional WebGPU timestamp-query feature. Unsupported
        // devices keep running and simply return no timing batches.
        bool enableStageProfiling = false;
        uint32_t stageProfilingReadbackSlots = 3;
        // Conversion for timestamp-query device ticks. Browser WebGPU uses
        // nanoseconds. Native Vulkan callers must supply the adapter's
        // VkPhysicalDeviceLimits::timestampPeriod when it is not 1 ns.
        double stageProfilingTimestampPeriodNanoseconds = 1.0;
        uint32_t telemetryReadbackSlots = 3;
        uint32_t ccdBulletCapacity = 1'024;
        uint32_t ccdWorkgroupSize = 128;
        uint32_t ccdCoarseSteps = 16;
        uint32_t ccdBisectionIterations = 8;
        float ccdFastDistanceRatio = 0.5f;
        std::string shaderPath = "shaders/physics_ballistic.wgsl";
    } gpu;
};

enum class PhysicsGpuStage : uint32_t {
    CommandsAndActiveCompaction = 0,
    ContinuousCollision,
    ForcesAndWater,
    BroadPhase,
    NarrowPhase,
    DynamicSolver,
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

struct PhysicsStats {
    BackendType backend = BackendType::JoltLegacy;
    PhysicsArithmeticMode arithmeticMode = PhysicsArithmeticMode::FastFloat;
    uint32_t substeps = 0;
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

    uint32_t kinematicBodies = 0;
    uint32_t occupiedCells = 0;
    uint32_t activeSleepingPairs = 0;
    uint32_t oversizedBodies = 0;
    uint32_t persistentContacts = 0;
    uint32_t manifoldPoints = 0;
    uint32_t speculativeManifolds = 0;
    uint32_t invalidManifolds = 0;
    uint32_t terrainContactBodies = 0;
    uint32_t maximumTerrainContactsPerBody = 0;
    uint32_t submergedBodies = 0;
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

struct BodySpawnDesc {
    ThrowableShape shape = ThrowableShape::Sphere;
    glm::vec3 position{0.0f};
    glm::quat orientation{1.0f, 0.0f, 0.0f, 0.0f};
    glm::vec3 linearVelocity{0.0f};
    glm::vec3 angularVelocity{0.0f};
    glm::vec3 dimensions{1.0f};
    float inverseMass = 1.0f;
    bool bullet = false;
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
};

struct PhysicsRenderView {
    WGPUBuffer poseBuffer = nullptr;
    WGPUBuffer shapeBuffer = nullptr;
    WGPUBuffer activeBodyIds = nullptr;
    WGPUBuffer visibleBodyIds = nullptr;
    WGPUBuffer perShapeRanges = nullptr;
    WGPUBuffer indirectDrawArgs = nullptr;
    uint32_t residentBodyCapacity = 0;
    uint32_t shapeCount = 0;

    [[nodiscard]] bool valid() const noexcept {
        return poseBuffer != nullptr && shapeBuffer != nullptr
            && residentBodyCapacity != 0;
    }
};

struct TerrainGpuResources {
    WGPUTextureView maxHeightTexture = nullptr;
    uint32_t mipLevelCount = 0;
};

struct DebugSnapshotRequest {
    uint32_t firstBody = 1;
    uint32_t bodyCount = 0;
};

struct DebugBodyState {
    BodyHandle handle{};
    glm::vec3 position{0.0f};
    glm::quat orientation{1.0f, 0.0f, 0.0f, 0.0f};
    glm::vec3 linearVelocity{0.0f};
    glm::vec3 angularVelocity{0.0f};
    ThrowableShape shape = ThrowableShape::Sphere;
    bool alive = false;
    bool awake = false;
    uint32_t staticContactCount = 0;
    bool terrainRejectedByMip = false;
    bool submerged = false;
    glm::ivec3 sector{0};
};

struct DebugSnapshot {
    uint64_t tick = 0;
    std::vector<DebugBodyState> bodies;
};

enum class PhysicsQueryType : uint32_t {
    RayCast = 0,
    OverlapSphere,
    SphereCast,
    CapsuleCast,
};

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
    uint32_t featureId = 0;
    PhysicsQueryType type = PhysicsQueryType::RayCast;
    float fraction = 0.0f;
    float distance = 0.0f;
    glm::vec3 point{0.0f};
    glm::vec3 normal{0.0f};
    glm::ivec3 sector{0};
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
};

struct PhysicsEvent {
    uint64_t tick = 0;
    PhysicsEventType type = PhysicsEventType::ContactBegin;
    uint32_t bodyA = 0;
    uint32_t bodyB = 0;
    uint32_t featureId = 0;
    uint32_t sourceId = 0;
    uint32_t auxiliaryCount = 0;
    uint32_t flags = 0;
};

struct PhysicsEventBatch {
    uint64_t tick = 0;
    bool overflow = false;
    std::vector<PhysicsEvent> events;
};

struct ShapeHandle {
    uint32_t index = 0;
    uint32_t generation = 0;

    [[nodiscard]] constexpr bool valid() const noexcept { return index != 0; }
    [[nodiscard]] constexpr auto operator<=>(const ShapeHandle&) const noexcept = default;
};

using CharacterHandle = uint32_t;
inline constexpr CharacterHandle InvalidCharacter = 0;

struct DynamicBodySnapshot {
    ThrowableShape shape = ThrowableShape::Sphere;
    glm::vec3 position{0.0f};
    glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
    glm::vec3 dimensions{1.0f};
    // Rendering hint only. Transform fields remain authoritative state.
    bool active = false;
    glm::ivec3 sector{0};
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

struct CharacterMotion {
    glm::vec3 position{0.0f}; // Feet position in world space.
    glm::vec3 velocity{0.0f};
    glm::vec3 groundNormal{0.0f, 1.0f, 0.0f};
    bool grounded = false;
    bool onSteepGround = false;
    glm::ivec3 sector{0};
};

[[nodiscard]] const char* throwableShapeName(ThrowableShape shape) noexcept;
[[nodiscard]] glm::vec3 throwableShapeDimensions(ThrowableShape shape) noexcept;

} // namespace voxy::physics
