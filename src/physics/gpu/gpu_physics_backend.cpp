#include "physics/gpu/gpu_physics_backend.hpp"

#include "core/log.hpp"
#include "gpu/resources.hpp"
#include "physics/character/cpu_capsule_mover.hpp"
#include "physics/gpu/debug_readback_ring.hpp"
#include "physics/gpu/gpu_body_metadata.hpp"
#include "physics/gpu/gpu_broad_phase.hpp"
#include "physics/gpu/gpu_buffer_arena.hpp"
#include "physics/gpu/gpu_ccd.hpp"
#include "physics/gpu/gpu_attachments.hpp"
#include "physics/gpu/gpu_dynamic_solver.hpp"
#include "physics/gpu/gpu_event_readback.hpp"
#include "physics/gpu/gpu_islands.hpp"
#include "physics/gpu/gpu_narrow_phase.hpp"
#include "physics/gpu/gpu_queries.hpp"
#include "physics/terrain_topology.hpp"
#include "terrain/mip_generator.hpp"

#include <glm/common.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <deque>
#include <filesystem>
#include <limits>
#include <set>
#include <tuple>
#include <utility>
#include <vector>

namespace voxy::physics {
namespace {

constexpr uint32_t kWorkgroupSize = 256;
constexpr uint32_t kMaximumSubsteps = 16;
constexpr uint32_t kMaximumCatchUpTicks = 1'024;
constexpr uint32_t kMaximumReadbackSlots = 1'024;
// The terrain kernel is register-heavy; smaller groups expose more parallelism.
constexpr uint32_t kStaticContactWorkgroupSize = 128;
// The command and integration layouts are the widest physics layouts. Each
// exposes eight storage buffers, matching WebGPU's guaranteed minimum.
constexpr uint32_t kRequiredStorageBuffersPerShaderStage = 8;
constexpr size_t kDebugVec4Count = 9;
constexpr size_t kGpuBodyBytes = kDebugVec4Count * sizeof(glm::uvec4);
constexpr uint32_t kStageBoundaryCount =
    static_cast<uint32_t>(kPhysicsGpuStageCount) + 1u;
#if defined(VOXY_WASM)
// emdawnwebgpu currently forwards UINT32_MAX instead of omitting an undefined
// pass timestamp index. Supply a valid, discarded end timestamp in browsers.
constexpr uint32_t kStageQueryWordsPerBoundary = 2u;
#else
constexpr uint32_t kStageQueryWordsPerBoundary = 1u;
#endif
constexpr uint32_t kStagePacketWordCount =
    kStageBoundaryCount * kStageQueryWordsPerBoundary;
constexpr uint32_t kCoreTelemetryWordCount = 16u;
constexpr uint32_t kCoreTelemetryOffset = 0u;
constexpr uint32_t kCcdTelemetryOffset =
    kCoreTelemetryOffset + kCoreTelemetryWordCount;
constexpr uint32_t kBroadTelemetryOffset =
    kCcdTelemetryOffset + GpuCcd::kTelemetryWordCount;
constexpr uint32_t kNarrowTelemetryOffset =
    kBroadTelemetryOffset + GpuBroadPhase::kTelemetryWordCount;
constexpr uint32_t kSolverTelemetryOffset =
    kNarrowTelemetryOffset + GpuNarrowPhase::kTelemetryWordCount;
constexpr uint32_t kIslandTelemetryOffset =
    kSolverTelemetryOffset + GpuDynamicSolver::kTelemetryWordCount;
constexpr uint32_t kAttachmentTelemetryOffset =
    kIslandTelemetryOffset + GpuIslandManager::kTelemetryWordCount;
constexpr uint32_t kNarrowCollisionPairClassOffset =
    kAttachmentTelemetryOffset + GpuAttachmentSolver::kTelemetryWordCount;
constexpr uint32_t kTelemetrySnapshotWordCount =
    kNarrowCollisionPairClassOffset + kGpuNarrowPhasePairClassCount;
constexpr size_t kTelemetrySnapshotBytes =
    size_t{kTelemetrySnapshotWordCount} * sizeof(uint32_t);

struct alignas(16) GpuPose {
    glm::vec4 positionInvMass{0.0f};
    glm::vec4 orientation{0.0f, 0.0f, 0.0f, 1.0f};
};

struct alignas(16) GpuMotion {
    glm::vec4 linearVelocitySleep{0.0f};
    glm::vec4 angularVelocityFlags{0.0f};
};

struct alignas(16) GpuShape {
    glm::vec4 dimensionsType{0.0f};
    glm::vec4 invInertiaMaterial{0.0f};
    glm::vec4 materialCoefficients{0.0f};
};

struct alignas(16) GpuCommand {
    glm::uvec4 header{0u};
    glm::vec4 p0{0.0f};
    glm::vec4 p1{0.0f};
    glm::vec4 p2{0.0f};
    glm::vec4 p3{0.0f};
    glm::vec4 p4{0.0f};
    glm::ivec4 p5{0};
    glm::vec4 p6{0.0f};
};

struct alignas(16) SimulationUniforms {
    glm::vec4 gravityAndDt{0.0f};
    glm::vec4 dampingAndClamps{0.0f};
    glm::uvec4 counts{0u};
    glm::uvec4 debugRange{0u};
    glm::vec4 terrainOriginCellHeight{0.0f};
    glm::uvec4 terrainSizeMipFlags{0u};
    glm::vec4 water{0.0f};
    glm::vec4 contact{0.0f};
    glm::vec4 solver{0.0f};
    glm::vec4 terrainMaterials{0.0f};
    glm::vec4 bodyMaterials{0.0f};
    glm::vec4 waterSurface{0.0f};
    glm::ivec4 worldSector{0};
};

struct alignas(16) GpuTerrainContactCache {
    glm::vec4 normalImpulses{0.0f};
    glm::uvec4 featureIds{0u};
    glm::uvec4 state{0u};
};

struct CoreGpuTelemetry {
    uint32_t activeBodies = 0;
    uint32_t tick = 0;
    uint32_t terrainContactBodies = 0;
    uint32_t terrainContactPoints = 0;
    uint32_t maximumTerrainContactsPerBody = 0;
    uint32_t submergedBodies = 0;
    uint32_t waterSamples = 0;
    uint32_t commands = 0;
    uint32_t highActiveBodies = 0;
    uint32_t highTerrainContactBodies = 0;
    uint32_t highTerrainContactPoints = 0;
    uint32_t highSubmergedBodies = 0;
    uint32_t highWaterSamples = 0;
    uint32_t highCommands = 0;
    bool activeOverflow = false;
    uint32_t kinematicBodies = 0;
};

CoreGpuTelemetry decodeCoreTelemetry(
    std::span<const uint32_t> words) noexcept {
    CoreGpuTelemetry result;
    if (words.size() < kCoreTelemetryWordCount) return result;
    result.activeBodies = words[0];
    result.tick = words[1];
    result.terrainContactBodies = words[2];
    result.terrainContactPoints = words[3];
    result.maximumTerrainContactsPerBody = words[4];
    result.submergedBodies = words[5];
    result.waterSamples = words[6];
    result.commands = words[7];
    result.highActiveBodies = words[8];
    result.highTerrainContactBodies = words[9];
    result.highTerrainContactPoints = words[10];
    result.highSubmergedBodies = words[11];
    result.highWaterSamples = words[12];
    result.highCommands = words[13];
    result.activeOverflow = words[14] != 0u;
    result.kinematicBodies = words[15];
    return result;
}

static_assert(sizeof(GpuPose) == 32);
static_assert(sizeof(GpuMotion) == 32);
static_assert(sizeof(GpuShape) == 48);
static_assert(sizeof(GpuCommand) == 128);
static_assert(sizeof(SimulationUniforms) == 208);
static_assert(sizeof(GpuTerrainContactCache) == 48);

uint32_t commandPriority(PhysicsCommandType type) noexcept {
    switch (type) {
        case PhysicsCommandType::DestroyBody: return 0;
        case PhysicsCommandType::SpawnBody: return 1;
        case PhysicsCommandType::Teleport:
        case PhysicsCommandType::SetKinematicTarget: return 2;
        case PhysicsCommandType::SetMaterial: return 3;
        case PhysicsCommandType::SetVelocity:
        case PhysicsCommandType::SetAngularVelocity: return 4;
        case PhysicsCommandType::ApplyImpulse:
        case PhysicsCommandType::ApplyForce:
        case PhysicsCommandType::ApplyForceAtLocalPoint: return 5;
        case PhysicsCommandType::Wake:
        case PhysicsCommandType::Sleep: return 6;
    }
    return 7;
}

uint32_t nextBodyGeneration(uint32_t generation) noexcept {
    generation = (generation + 1u) & kGpuBodyGenerationMask;
    return generation == 0u ? 1u : generation;
}

struct AttachmentMutation {
    GpuAttachmentCommandType type = GpuAttachmentCommandType::CreateDistance;
    AttachmentHandle attachment{};
    uint64_t targetTick = 0u;
    uint64_t sequence = 0u;
    DistanceAttachmentDesc desc{};
    float value = 0.0f;
};

bool finiteVector(const glm::vec3& value) noexcept {
    return std::isfinite(value.x) && std::isfinite(value.y)
        && std::isfinite(value.z);
}

bool finiteQuaternion(const glm::quat& value) noexcept {
    return std::isfinite(value.w) && std::isfinite(value.x)
        && std::isfinite(value.y) && std::isfinite(value.z);
}

bool finiteVector(const glm::vec4& value) noexcept {
    return std::isfinite(value.x) && std::isfinite(value.y)
        && std::isfinite(value.z) && std::isfinite(value.w);
}

bool validMaterial(const PhysicsMaterial& material) noexcept {
    return std::isfinite(material.friction)
        && std::isfinite(material.restitution)
        && std::isfinite(material.rollingResistance)
        && std::isfinite(material.density)
        && material.friction >= 0.0f
        && material.restitution >= 0.0f
        && material.rollingResistance >= 0.0f
        && material.density > 0.0f;
}

glm::vec4 materialCoefficients(const PhysicsMaterial& material) noexcept {
    return {material.friction, material.restitution,
            material.rollingResistance, material.density};
}

WGPUComputePipeline makeComputePipeline(WGPUDevice device,
                                        WGPUPipelineLayout layout,
                                        WGPUShaderModule module,
                                        const char* entryPoint,
                                        const char* label) {
    WGPUComputePipelineDescriptor desc{};
    WGPU_SET_LABEL(desc, label);
    desc.layout = layout;
    desc.compute.module = module;
    WGPU_SET_ENTRY_POINT(desc.compute, entryPoint);
    return wgpuDeviceCreateComputePipeline(device, &desc);
}

template <typename T>
void releaseHandle(T& handle, void (*release)(T)) {
    if (handle) {
        release(handle);
        handle = nullptr;
    }
}

} // namespace

class GpuPhysicsBackend::Impl {
public:
    enum class SchedulingMode : uint8_t {
        Undecided = 0u,
        Accumulator,
        FixedTicks,
    };

    struct PreparedMutationState {
        uint64_t token = 0u;
        uint64_t targetTick = 0u;
        uint32_t bodyCommandCount = 0u;
        uint32_t destroyedAttachmentCount = 0u;
        uint32_t createdAttachmentCount = 0u;
        uint64_t nextSequence = 1u;
        uint32_t nextUnusedAttachmentIndex = 1u;
        uint32_t residentAttachments = 0u;
        uint32_t highResidentAttachments = 0u;
        std::vector<PhysicsCommand> commands;
        std::vector<AttachmentMutation> attachmentCommands;
        std::vector<uint32_t> attachmentGenerations;
        std::vector<bool> hostAttachmentAlive;
        std::vector<uint32_t> freeAttachmentIndices;
        std::vector<AttachmentHandle> createdAttachments;
    };

    ~Impl() { shutdown(); }

    bool initialize(const PhysicsInitContext& context) {
        if (initialized_) return true;
        const auto& gpuConfig = context.gpu;
        const bool validScalars = finiteVector(gpuConfig.gravity)
            && std::isfinite(gpuConfig.fixedTickSeconds)
            && gpuConfig.fixedTickSeconds > 0.0f
            && std::isfinite(gpuConfig.linearDamping)
            && gpuConfig.linearDamping >= 0.0f
            && std::isfinite(gpuConfig.angularDamping)
            && gpuConfig.angularDamping >= 0.0f
            && std::isfinite(gpuConfig.maximumLinearSpeed)
            && gpuConfig.maximumLinearSpeed > 0.0f
            && std::isfinite(gpuConfig.maximumAngularSpeed)
            && gpuConfig.maximumAngularSpeed > 0.0f
            && std::isfinite(gpuConfig.bodyFriction)
            && gpuConfig.bodyFriction >= 0.0f
            && std::isfinite(gpuConfig.terrainFriction)
            && gpuConfig.terrainFriction >= 0.0f
            && std::isfinite(gpuConfig.bodySphereRestitution)
            && gpuConfig.bodySphereRestitution >= 0.0f
            && std::isfinite(gpuConfig.bodyOtherRestitution)
            && gpuConfig.bodyOtherRestitution >= 0.0f
            && std::isfinite(gpuConfig.terrainSphereRestitution)
            && gpuConfig.terrainSphereRestitution >= 0.0f
            && std::isfinite(gpuConfig.terrainOtherRestitution)
            && gpuConfig.terrainOtherRestitution >= 0.0f
            && std::isfinite(gpuConfig.linearSlop)
            && gpuConfig.linearSlop > 0.0f
            && std::isfinite(gpuConfig.speculativeDistance)
            && gpuConfig.speculativeDistance >= 0.0f
            && std::isfinite(gpuConfig.waterBuoyancy)
            && gpuConfig.waterBuoyancy >= 0.0f
            && std::isfinite(gpuConfig.waterLinearDrag)
            && gpuConfig.waterLinearDrag >= 0.0f
            && std::isfinite(gpuConfig.waterAngularDrag)
            && gpuConfig.waterAngularDrag >= 0.0f;
        if (!context.device || !context.queue || context.maxBodies == 0u
            || context.maxBodies == std::numeric_limits<uint32_t>::max()
            || !validScalars
            || (context.maxCandidatePairs != 0u
                && context.maxCandidatePairs < context.maxPairs)
            || gpuConfig.substeps == 0
            || gpuConfig.substeps > kMaximumSubsteps
            || gpuConfig.solverColorCount == 0
            || gpuConfig.solverColorCount > kGpuSolverMaximumColors
            || gpuConfig.solverParallelColorCount
                > gpuConfig.solverColorCount
            || (gpuConfig.solverWorkgroupSize != 64u
                && gpuConfig.solverWorkgroupSize != 128u
                && gpuConfig.solverWorkgroupSize != 256u)
            || gpuConfig.maximumCatchUpTicks == 0
            || gpuConfig.maximumCatchUpTicks > kMaximumCatchUpTicks
            || context.gpu.commandCapacity == 0
            || context.gpu.attachmentCapacity == 0u
            || context.gpu.attachmentCapacity
                == std::numeric_limits<uint32_t>::max()
            || context.gpu.attachmentCommandCapacity == 0u
            || context.gpu.debugReadbackSlots == 0
            || context.gpu.debugReadbackSlots > kMaximumReadbackSlots
            || context.gpu.debugReadbackBodyCapacity == 0
            || context.gpu.asyncQueryCapacity == 0
            || context.gpu.asyncQueryReadbackSlots == 0
            || context.gpu.asyncQueryReadbackSlots > kMaximumReadbackSlots
            || context.gpu.eventReadbackSlots > kMaximumReadbackSlots
            || (context.gpu.enableTelemetryReadback
                && (context.gpu.telemetryReadbackSlots == 0
                    || context.gpu.telemetryReadbackSlots
                        > kMaximumReadbackSlots))
            || (context.gpu.enableStageProfiling
                && (context.gpu.stageProfilingReadbackSlots == 0
                    || context.gpu.stageProfilingReadbackSlots
                        > kMaximumReadbackSlots))) {
            LOG_ERROR("WebGPU physics requires a device, queue, and valid capacities");
            return false;
        }

        WGPULimits limits{};
        if (!gpu::getDeviceLimits(context.device, limits)) {
            LOG_ERROR("WebGPU physics could not query device limits");
            return false;
        }
        const uint32_t requiredWorkgroupSize = std::max(
            kWorkgroupSize, gpuConfig.solverWorkgroupSize);
        if (limits.maxStorageBuffersPerShaderStage
                < kRequiredStorageBuffersPerShaderStage
            || limits.maxComputeInvocationsPerWorkgroup
                < requiredWorkgroupSize
            || limits.maxComputeWorkgroupSizeX < requiredWorkgroupSize) {
            LOG_WARN(
                "WebGPU physics device profile rejected: storage buffers {}/{}, compute invocations {}/{}, workgroup X {}/{}",
                limits.maxStorageBuffersPerShaderStage,
                kRequiredStorageBuffersPerShaderStage,
                limits.maxComputeInvocationsPerWorkgroup,
                requiredWorkgroupSize,
                limits.maxComputeWorkgroupSizeX, requiredWorkgroupSize);
            return false;
        }

        const uint64_t maximumStorageBytes = std::min(
            static_cast<uint64_t>(limits.maxStorageBufferBindingSize),
            static_cast<uint64_t>(limits.maxBufferSize));
        const auto fitsStorage = [maximumStorageBytes](
                                     uint64_t count, uint64_t stride) {
            return stride != 0u && count <= maximumStorageBytes / stride;
        };
        const uint64_t bodyCapacity = uint64_t{context.maxBodies} + 1u;
        const uint64_t candidateCapacity = context.maxCandidatePairs != 0u
            ? context.maxCandidatePairs
            : std::min(
                uint64_t{context.maxPairs} * 4u,
                uint64_t{std::numeric_limits<uint32_t>::max()});
        const uint64_t maximumEventRecords =
            uint64_t{context.maxManifolds} * 3u + context.maxBodies
            + context.gpu.attachmentCapacity;
        if (maximumEventRecords > std::numeric_limits<uint32_t>::max()
            || !fitsStorage(bodyCapacity, 128u) // solver color claims
            || !fitsStorage(candidateCapacity, sizeof(GpuKeyValue))
            || !fitsStorage(context.maxPairs, sizeof(GpuKeyValue))
            || !fitsStorage(context.maxContacts,
                            sizeof(GpuContactManifold))
            || !fitsStorage(context.maxManifolds,
                            sizeof(GpuContactManifold))
            || !fitsStorage(maximumEventRecords, sizeof(GpuPhysicsEvent))
            || !fitsStorage(context.gpu.commandCapacity,
                            sizeof(GpuCommand))
            || !fitsStorage(
                uint64_t{context.gpu.attachmentCapacity} + 1u,
                sizeof(GpuDistanceAttachment))
            || !fitsStorage(context.gpu.attachmentCommandCapacity,
                            sizeof(GpuAttachmentCommand))
            || !fitsStorage(context.gpu.debugReadbackBodyCapacity,
                            kGpuBodyBytes)
            || !fitsStorage(context.gpu.asyncQueryCapacity,
                            sizeof(GpuQueryOutput))) {
            LOG_ERROR("WebGPU physics capacities exceed device buffer limits");
            return false;
        }

        device_ = context.device;
        queue_ = context.queue;
        deviceLimits_ = limits;
        config_ = context.gpu;
        // Slot zero is the invalid-handle sentinel. Allocate one additional GPU
        // slot so the public maxBodies value remains the number of usable bodies.
        bodyLimit_ = context.maxBodies;
        bodyCapacity_ = bodyLimit_ + 1u;
        activeCapacity_ = std::min(context.maxActiveBodies, context.maxBodies);
        pairCapacity_ = context.maxPairs;
        candidatePairCapacity_ = static_cast<uint32_t>(candidateCapacity);
        contactCapacity_ = context.maxContacts;
        manifoldCapacity_ = context.maxManifolds;
        attachmentLimit_ = config_.attachmentCapacity;
        attachmentCapacity_ = attachmentLimit_ + 1u;
        if (activeCapacity_ == 0 || pairCapacity_ == 0
            || contactCapacity_ == 0 || manifoldCapacity_ == 0) {
            LOG_ERROR("WebGPU physics capacities must all be non-zero");
            shutdown();
            return false;
        }
        blockCount_ = (bodyCapacity_ + kWorkgroupSize - 1u) / kWorkgroupSize;
        generations_.assign(bodyCapacity_, 1u);
        generations_[0] = 0u;
        hostAlive_.assign(bodyCapacity_, false);
        scheduledSpawnTicks_.assign(bodyCapacity_, 0u);
        scheduledDestroyTicks_.assign(bodyCapacity_, 0u);
        attachmentGenerations_.assign(attachmentCapacity_, 1u);
        attachmentGenerations_[0] = 0u;
        hostAttachmentAlive_.assign(attachmentCapacity_, false);
        commands_.reserve(
            size_t{config_.commandCapacity} + 1u);
        commandSortScratch_.reserve(
            size_t{config_.commandCapacity} + 1u);
        commandSuperseded_.reserve(
            size_t{config_.commandCapacity} + 1u);
        commandUpload_.reserve(config_.commandCapacity);
        latestKinematicIndex_.assign(
            bodyCapacity_, std::numeric_limits<size_t>::max());
        latestKinematicTick_.assign(bodyCapacity_, 0u);
        latestKinematicGeneration_.assign(bodyCapacity_, 0u);
        attachmentCommands_.reserve(
            config_.attachmentCommandCapacity);
        attachmentCommandSortScratch_.reserve(
            config_.attachmentCommandCapacity);
        attachmentUpload_.reserve(
            config_.attachmentCommandCapacity);
        freeAttachmentIndices_.reserve(attachmentLimit_);
        preparedMutation_.emplace();
        preparedMutation_->commands.reserve(
            size_t{config_.commandCapacity} + 1u);
        preparedMutation_->attachmentCommands.reserve(
            config_.attachmentCommandCapacity);
        preparedMutation_->attachmentGenerations.assign(
            attachmentCapacity_, 1u);
        preparedMutation_->hostAttachmentAlive.assign(
            attachmentCapacity_, false);
        preparedMutation_->freeAttachmentIndices.reserve(
            attachmentLimit_);
        preparedMutation_->createdAttachments.reserve(
            attachmentLimit_);
        arena_.initialize(device_);
        if (!characterMover_.initialize()) {
            shutdown();
            return false;
        }

        const auto storage = static_cast<uint64_t>(
            WGPUBufferUsage_Storage | WGPUBufferUsage_CopyDst
            | WGPUBufferUsage_CopySrc);
        const auto scratchStorage = static_cast<uint64_t>(
            WGPUBufferUsage_Storage | WGPUBufferUsage_CopyDst
            | WGPUBufferUsage_CopySrc);
        poseBuffer_ = arena_.create("physics_body_pose",
            uint64_t{bodyCapacity_} * sizeof(GpuPose), storage);
        motionBuffer_ = arena_.create("physics_body_motion",
            uint64_t{bodyCapacity_} * sizeof(GpuMotion), storage);
        shapeBuffer_ = arena_.create("physics_body_shape",
            uint64_t{bodyCapacity_} * sizeof(GpuShape), storage);
        metadataBuffer_ = arena_.create("physics_body_metadata",
            uint64_t{bodyCapacity_} * sizeof(glm::uvec4), storage);
        if (config_.enableRenderInterpolation) {
            previousPoseBuffer_ = arena_.create("physics_previous_pose",
                uint64_t{bodyCapacity_} * sizeof(GpuPose), storage);
            previousMetadataBuffer_ = arena_.create("physics_previous_metadata",
                uint64_t{bodyCapacity_} * sizeof(glm::uvec4), storage);
            if (!previousPoseBuffer_ || !previousMetadataBuffer_) {
                shutdown();
                return false;
            }
        }
        forceBuffer_ = arena_.create("physics_body_forces",
            uint64_t{bodyCapacity_} * sizeof(glm::vec4), storage);
        terrainContactCacheBuffer_ = arena_.create(
            "physics_terrain_contact_cache",
            uint64_t{bodyCapacity_} * sizeof(GpuTerrainContactCache), storage);
        activeIdsBuffer_ = arena_.create("physics_active_body_ids",
            uint64_t{activeCapacity_} * sizeof(uint32_t), scratchStorage, true);
        activeOffsetsBuffer_ = arena_.create("physics_active_offsets",
            uint64_t{bodyCapacity_} * sizeof(uint32_t), scratchStorage, true);
        blockSumsBuffer_ = arena_.create("physics_active_block_sums",
            uint64_t{blockCount_} * sizeof(uint32_t), scratchStorage, true);
        blockPrefixBuffer_ = arena_.create("physics_active_block_prefix",
            uint64_t{blockCount_} * sizeof(uint32_t), scratchStorage, true);
        countersBuffer_ = arena_.create("physics_counters",
            kCoreTelemetryWordCount * sizeof(uint32_t), scratchStorage, true);
        commandBuffer_ = arena_.create("physics_commands",
            uint64_t{config_.commandCapacity} * sizeof(GpuCommand),
            WGPUBufferUsage_Storage | WGPUBufferUsage_CopyDst, true);
        uniformBuffer_ = arena_.create("physics_simulation_uniforms",
            gpu::alignUniformBufferSize(sizeof(SimulationUniforms)),
            WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst, true);
        debugPackedBuffer_ = arena_.create("physics_debug_packed",
            uint64_t{config_.debugReadbackBodyCapacity} * kGpuBodyBytes,
            WGPUBufferUsage_Storage | WGPUBufferUsage_CopySrc, true);
        if (config_.enableTelemetryReadback) {
            telemetrySnapshotBuffer_ = arena_.create(
                "physics_telemetry_snapshot", kTelemetrySnapshotBytes,
                WGPUBufferUsage_CopyDst | WGPUBufferUsage_CopySrc, true);
        }
        if (!poseBuffer_ || !motionBuffer_ || !shapeBuffer_
            || !metadataBuffer_ || !forceBuffer_
            || !terrainContactCacheBuffer_
            || !activeIdsBuffer_ || !activeOffsetsBuffer_ || !blockSumsBuffer_
            || !blockPrefixBuffer_ || !countersBuffer_ || !commandBuffer_
            || !uniformBuffer_ || !debugPackedBuffer_
            || (config_.enableTelemetryReadback
                && !telemetrySnapshotBuffer_)) {
            shutdown();
            return false;
        }

        const std::array<uint32_t, kCoreTelemetryWordCount> zeroCounters{};
        if (!gpu::writeBuffer(queue_, countersBuffer_, 0, zeroCounters)) {
            shutdown();
            return false;
        }

        if (!createFallbackTerrain() || !createFallbackWater()) {
            shutdown();
            return false;
        }

        shaderModule_ = gpu::loadShaderModule(
            device_, config_.shaderPath, "physics_ballistic.wgsl",
            config_.shaderSources);
        if (!shaderModule_ || !createPipelinesAndBindings()) {
            shutdown();
            return false;
        }

        const size_t readbackBytes =
            size_t{config_.debugReadbackBodyCapacity} * kGpuBodyBytes;
        if (!readbackRing_.initialize(device_, config_.debugReadbackSlots,
                                      readbackBytes)) {
            shutdown();
            return false;
        }
        if (config_.enableTelemetryReadback
            && !telemetryReadback_.initialize(
                device_, std::max(config_.telemetryReadbackSlots, 1u),
                kTelemetrySnapshotBytes)) {
            shutdown();
            return false;
        }

        GpuCcd::Config ccdConfig;
        ccdConfig.bodyCapacity = bodyCapacity_;
        ccdConfig.bulletCapacity = std::min(
            std::max(config_.ccdBulletCapacity, 1u), bodyLimit_);
        ccdBulletCapacity_ = ccdConfig.bulletCapacity;
        ccdConfig.workgroupSize = config_.ccdWorkgroupSize;
        ccdConfig.coarseSteps = config_.ccdCoarseSteps;
        ccdConfig.bisectionIterations = config_.ccdBisectionIterations;
        ccdConfig.fastDistanceRatio = config_.ccdFastDistanceRatio;
        ccdConfig.linearSlop = config_.linearSlop;
        const std::filesystem::path mainShaderPath(config_.shaderPath);
        const std::filesystem::path shaderDirectory =
            mainShaderPath.parent_path().empty()
                ? std::filesystem::path("shaders")
                : mainShaderPath.parent_path();
        const auto shaderFile = [&shaderDirectory](const char* name) {
            return shaderDirectory / name;
        };
        ccdConfig.shaderPath = shaderFile("physics_ccd.wgsl");
        ccdConfig.primitivesShaderPath = shaderFile(
            "physics_deterministic_primitives.wgsl");
        ccdConfig.shaderSources = config_.shaderSources;
        if (!ccd_.initialize(device_, queue_, ccdConfig)) {
            shutdown();
            return false;
        }

        GpuBroadPhase::Config broadConfig;
        broadConfig.bodyCapacity = bodyCapacity_;
        broadConfig.pairCapacity = pairCapacity_;
        broadConfig.contactCapacity = manifoldCapacity_;
        broadConfig.candidatePairCapacity = candidatePairCapacity_;
        broadConfig.cellSize = config_.broadPhaseCellSize;
        broadConfig.speculativeMargin = config_.speculativeDistance;
        broadConfig.shaderPath = shaderFile("physics_broad_phase.wgsl");
        broadConfig.primitivesShaderPath = shaderFile(
            "physics_deterministic_primitives.wgsl");
        broadConfig.shaderSources = config_.shaderSources;
        if (!broadPhase_.initialize(device_, queue_, broadConfig)) {
            shutdown();
            return false;
        }
        broadPhase_.setBodyView({
            .poseBuffer = poseBuffer_,
            .shapeBuffer = shapeBuffer_,
            .metadataBuffer = metadataBuffer_,
            .bodyCapacity = 1u,
        });

        GpuNarrowPhase::Config narrowConfig;
        narrowConfig.pairCapacity = pairCapacity_;
        narrowConfig.manifoldCapacity = manifoldCapacity_;
        narrowConfig.dispatchContactCapacity = contactCapacity_;
        narrowConfig.linearSlop = config_.linearSlop;
        narrowConfig.speculativeDistance = config_.speculativeDistance;
        narrowConfig.recycleDistance = std::max(
            0.05f, config_.speculativeDistance * 2.0f);
        narrowConfig.shaderPath = shaderFile("physics_narrow_phase.wgsl");
        narrowConfig.primitivesShaderPath = shaderFile(
            "physics_deterministic_primitives.wgsl");
        narrowConfig.shaderSources = config_.shaderSources;
        if (!narrowPhase_.initialize(device_, queue_, narrowConfig)) {
            shutdown();
            return false;
        }
        narrowPhase_.setInput({
            .poseBuffer = poseBuffer_,
            .shapeBuffer = shapeBuffer_,
            .uniquePairBuffer = broadPhase_.uniquePairs(),
            .broadPhaseTelemetryBuffer = broadPhase_.telemetryBuffer(),
            .bodyCapacity = 1u,
            .pairCapacity = pairCapacity_,
            .metadataBuffer = metadataBuffer_,
        });

        GpuDynamicSolver::Config solverConfig;
        solverConfig.bodyCapacity = bodyCapacity_;
        solverConfig.contactCapacity = contactCapacity_;
        solverConfig.workgroupSize = config_.solverWorkgroupSize;
        solverConfig.colorCount = config_.solverColorCount;
        solverConfig.parallelColorCount =
            config_.solverParallelColorCount;
        solverConfig.substeps = config_.substeps;
        solverConfig.tickSeconds = config_.fixedTickSeconds;
        // The ballistic preparation pass applies forces, gravity, damping,
        // and water exactly once. The dynamic solver owns pose integration.
        solverConfig.gravity = {0.0f, 0.0f, 0.0f};
        solverConfig.linearDamping = 0.0f;
        solverConfig.angularDamping = 0.0f;
        solverConfig.linearSlop = config_.linearSlop;
        solverConfig.speculativeDistance = config_.speculativeDistance;
        solverConfig.friction = config_.bodyFriction;
        solverConfig.sphereRestitution = config_.bodySphereRestitution;
        solverConfig.otherRestitution = config_.bodyOtherRestitution;
        solverConfig.shaderPath = shaderFile("physics_dynamic_solver.wgsl");
        solverConfig.primitivesShaderPath = shaderFile(
            "physics_deterministic_primitives.wgsl");
        solverConfig.shaderSources = config_.shaderSources;
        if (!dynamicSolver_.initialize(device_, queue_, solverConfig)) {
            shutdown();
            return false;
        }

        GpuAttachmentSolver::Config attachmentConfig;
        attachmentConfig.attachmentCapacity = attachmentCapacity_;
        attachmentConfig.commandCapacity =
            config_.attachmentCommandCapacity;
        attachmentConfig.tickSeconds = config_.fixedTickSeconds;
        attachmentConfig.linearSlop = config_.linearSlop;
        attachmentConfig.biasFactor = 0.2f;
        attachmentConfig.shaderPath =
            shaderFile("physics_attachments.wgsl");
        attachmentConfig.shaderSources = config_.shaderSources;
        if (!attachmentSolver_.initialize(
                device_, queue_, attachmentConfig)) {
            shutdown();
            return false;
        }
        attachmentSolver_.setInput({
            .poseBuffer = poseBuffer_,
            .motionBuffer = motionBuffer_,
            .shapeBuffer = shapeBuffer_,
            .metadataBuffer = metadataBuffer_,
            .coreCountersBuffer = countersBuffer_,
            .bodyCapacity = 1u,
        });

        GpuIslandManager::Config islandConfig;
        islandConfig.bodyCapacity = bodyCapacity_;
        islandConfig.contactCapacity = contactCapacity_;
        islandConfig.eventCapacity = bodyLimit_;
        islandConfig.sleepingCellSize = config_.broadPhaseCellSize;
        islandConfig.shaderPath = shaderFile("physics_islands.wgsl");
        islandConfig.primitivesShaderPath = shaderFile(
            "physics_deterministic_primitives.wgsl");
        islandConfig.shaderSources = config_.shaderSources;
        if (!islandManager_.initialize(device_, queue_, islandConfig)) {
            shutdown();
            return false;
        }
        refreshContactInputs();

        GpuAsyncQuerySystem::Config queryConfig;
        queryConfig.bodyCapacity = bodyCapacity_;
        queryConfig.requestCapacity = config_.asyncQueryCapacity;
        queryConfig.readbackSlots = config_.asyncQueryReadbackSlots;
        queryConfig.shaderPath = shaderFile("physics_queries.wgsl");
        queryConfig.shaderSources = config_.shaderSources;
        if (!querySystem_.initialize(device_, queue_, queryConfig)) {
            shutdown();
            return false;
        }
        querySystem_.setBodyView({
            .poseBuffer = poseBuffer_,
            .shapeBuffer = shapeBuffer_,
            .metadataBuffer = metadataBuffer_,
            .bodyCapacity = 1u,
        });

        const uint64_t maximumEvents = uint64_t{manifoldCapacity_} * 3u
                                     + bodyLimit_ + attachmentLimit_;
        if (maximumEvents > std::numeric_limits<uint32_t>::max()) {
            LOG_ERROR("WebGPU physics event capacity exceeds u32 range");
            shutdown();
            return false;
        }
        eventCapacity_ = static_cast<uint32_t>(maximumEvents);

        if (config_.enableStageProfiling) {
            if (!std::isfinite(
                    config_.stageProfilingTimestampPeriodNanoseconds)
                || config_.stageProfilingTimestampPeriodNanoseconds <= 0.0) {
                LOG_WARN("GPU physics stage profiling requested with an invalid timestamp period");
            } else if (!wgpuDeviceHasFeature(
                    device_, WGPUFeatureName_TimestampQuery)) {
                LOG_WARN("GPU physics stage profiling requested, but timestamp queries are unavailable");
            } else {
                stageQueryCapacity_ = kStagePacketWordCount
                    * std::max(config_.maximumCatchUpTicks, 1u);
                WGPUQuerySetDescriptor queryDesc{};
                WGPU_SET_LABEL(queryDesc, "physics_stage_timestamps");
                queryDesc.type = WGPUQueryType_Timestamp;
                queryDesc.count = stageQueryCapacity_;
                stageQuerySet_ = wgpuDeviceCreateQuerySet(device_, &queryDesc);
                stageResolveBuffer_ = gpu::createBuffer(
                    device_, gpu::BufferDesc{
                        .label = "physics_stage_timestamp_resolve",
                        .size = uint64_t{stageQueryCapacity_}
                              * sizeof(uint64_t),
                        .usage = WGPUBufferUsage_QueryResolve
                               | WGPUBufferUsage_CopySrc,
                    });
                const uint32_t slots = std::max(
                    config_.stageProfilingReadbackSlots, 1u);
                if (!stageQuerySet_ || !stageResolveBuffer_
                    || !stageReadback_.initialize(
                        device_, slots,
                        size_t{stageQueryCapacity_} * sizeof(uint64_t))) {
                    stageReadback_.shutdown();
                    releaseHandle(stageQuerySet_, wgpuQuerySetRelease);
                    if (stageResolveBuffer_) {
                        wgpuBufferDestroy(stageResolveBuffer_);
                        wgpuBufferRelease(stageResolveBuffer_);
                        stageResolveBuffer_ = nullptr;
                    }
                    stageQueryCapacity_ = 0u;
                    LOG_WARN("GPU physics stage profiler allocation failed; continuing without it");
                } else {
                    stageProfilingEnabled_ = true;
                }
            }
        }

        initialized_ = true;
        LOG_INFO("WebGPU physics initialized: {} usable body slots, {} MiB resident, {} MiB scratch",
                 bodyLimit_, arena_.persistentBytes() / (1024 * 1024),
                 arena_.scratchBytes() / (1024 * 1024));
        return true;
    }

    void shutdown() {
        // Native WebGPU queues texture uploads until the next submission.
        // Flush them before destroying their destinations, including when a
        // world is shut down before its first physics tick.
        if (queue_) wgpuQueueSubmit(queue_, 0u, nullptr);
        initialized_ = false;
        stageReadback_.shutdown();
        telemetryReadback_.shutdown();
        releaseHandle(stageQuerySet_, wgpuQuerySetRelease);
        if (stageResolveBuffer_) {
            wgpuBufferDestroy(stageResolveBuffer_);
            wgpuBufferRelease(stageResolveBuffer_);
            stageResolveBuffer_ = nullptr;
        }
        characterMover_.shutdown();
        eventReadback_.shutdown();
        querySystem_.shutdown();
        islandManager_.shutdown();
        attachmentSolver_.shutdown();
        dynamicSolver_.shutdown();
        narrowPhase_.shutdown();
        broadPhase_.shutdown();
        ccd_.shutdown();
        readbackRing_.shutdown();
        releaseHandle(commandBindGroup_, wgpuBindGroupRelease);
        releaseHandle(compactBindGroup_, wgpuBindGroupRelease);
        releaseHandle(integrateBindGroup_, wgpuBindGroupRelease);
        releaseHandle(tickBindGroup_, wgpuBindGroupRelease);
        releaseHandle(debugBindGroup_, wgpuBindGroupRelease);
        releaseHandle(applyCommandsPipeline_, wgpuComputePipelineRelease);
        releaseHandle(compactBlocksPipeline_, wgpuComputePipelineRelease);
        releaseHandle(scanBlocksPipeline_, wgpuComputePipelineRelease);
        releaseHandle(scatterActivePipeline_, wgpuComputePipelineRelease);
        releaseHandle(preparePipeline_, wgpuComputePipelineRelease);
        releaseHandle(staticContactPipeline_, wgpuComputePipelineRelease);
        releaseHandle(advanceTickPipeline_, wgpuComputePipelineRelease);
        releaseHandle(packDebugPipeline_, wgpuComputePipelineRelease);
        releaseHandle(commandPipelineLayout_, wgpuPipelineLayoutRelease);
        releaseHandle(compactPipelineLayout_, wgpuPipelineLayoutRelease);
        releaseHandle(integratePipelineLayout_, wgpuPipelineLayoutRelease);
        releaseHandle(tickPipelineLayout_, wgpuPipelineLayoutRelease);
        releaseHandle(debugPipelineLayout_, wgpuPipelineLayoutRelease);
        releaseHandle(commandLayout_, wgpuBindGroupLayoutRelease);
        releaseHandle(compactLayout_, wgpuBindGroupLayoutRelease);
        releaseHandle(integrateLayout_, wgpuBindGroupLayoutRelease);
        releaseHandle(tickLayout_, wgpuBindGroupLayoutRelease);
        releaseHandle(debugLayout_, wgpuBindGroupLayoutRelease);
        releaseHandle(shaderModule_, wgpuShaderModuleRelease);
        releaseTerrainTexture(ownedTerrainTexture_, ownedTerrainView_);
        releaseTerrainTexture(fallbackTerrainTexture_, fallbackTerrainView_);
        releaseHandle(fallbackWaterSampler_, wgpuSamplerRelease);
        releaseTerrainTexture(fallbackWaterTexture_, fallbackWaterView_);
        arena_.shutdown();
        poseBuffer_ = nullptr;
        previousPoseBuffer_ = nullptr;
        previousMetadataBuffer_ = nullptr;
        motionBuffer_ = nullptr;
        shapeBuffer_ = nullptr;
        metadataBuffer_ = nullptr;
        forceBuffer_ = nullptr;
        terrainContactCacheBuffer_ = nullptr;
        activeIdsBuffer_ = nullptr;
        activeOffsetsBuffer_ = nullptr;
        blockSumsBuffer_ = nullptr;
        blockPrefixBuffer_ = nullptr;
        countersBuffer_ = nullptr;
        commandBuffer_ = nullptr;
        uniformBuffer_ = nullptr;
        debugPackedBuffer_ = nullptr;
        telemetrySnapshotBuffer_ = nullptr;
        device_ = nullptr;
        queue_ = nullptr;
        deviceLimits_ = {};
        bodyCapacity_ = 0;
        bodyLimit_ = 0;
        activeCapacity_ = 0;
        pairCapacity_ = 0;
        candidatePairCapacity_ = 0;
        contactCapacity_ = 0;
        manifoldCapacity_ = 0;
        attachmentCapacity_ = 0u;
        attachmentLimit_ = 0u;
        eventCapacity_ = 0;
        ccdBulletCapacity_ = 0;
        blockCount_ = 0;
        nextUnusedIndex_ = 1;
        nextUnusedAttachmentIndex_ = 1u;
        residentBodies_ = 0;
        residentAttachments_ = 0u;
        highResidentBodies_ = 0;
        highResidentAttachments_ = 0u;
        bodyCapacityOverflow_ = false;
        commandCapacityOverflow_ = false;
        attachmentCapacityOverflow_ = false;
        attachmentCommandCapacityOverflow_ = false;
        lastGpuUploadBytes_ = 0;
        lastGpuReadbackBytes_ = 0;
        accumulator_ = 0.0;
        pendingTicks_ = 0;
        schedulingMode_ = SchedulingMode::Undecided;
        encodedTick_ = 0;
        nextSequence_ = 1;
        nextPreparedMutationToken_ = 1u;
        preparedMutation_.reset();
        queryPending_ = false;
        pendingQueryCount_ = 0;
        eventReadbackEnabled_ = false;
        stageProfilingEnabled_ = false;
        stageQueryCapacity_ = 0u;
        lastStageProfileTick_ = 0u;
        lastTelemetryReadbackTick_ = 0u;
        lastMutationTick_ = 0u;
        idleWorldConfirmed_ = false;
        gpuTickSynchronized_ = true;
        stageTimingResults_.clear();
        cachedTelemetry_ = {};
        commands_.clear();
        commandSortScratch_.clear();
        commandSuperseded_.clear();
        commandUpload_.clear();
        latestKinematicIndex_.clear();
        latestKinematicTick_.clear();
        latestKinematicGeneration_.clear();
        attachmentCommands_.clear();
        attachmentCommandSortScratch_.clear();
        attachmentUpload_.clear();
        pendingFrees_.clear();
        freeIndices_.clear();
        freeAttachmentIndices_.clear();
        generations_.clear();
        hostAlive_.clear();
        attachmentGenerations_.clear();
        hostAttachmentAlive_.clear();
        scheduledSpawnTicks_.clear();
        scheduledDestroyTicks_.clear();
        debugRequest_.reset();
        cachedDebugBodies_.clear();
        terrainAttached_ = false;
        terrainStateNeedsClear_ = false;
        externalTerrainView_ = nullptr;
        externalWaterView_ = nullptr;
        externalWaterSampler_ = nullptr;
        waterSurfaceStrength_ = 0.0f;
        waterPatchLengths_ = {1949.0f, 326.0f};
        warnedCpuWaterSampler_ = false;
        terrainWidth_ = 0;
        terrainHeight_ = 0;
        terrainMipLevelCount_ = 0;
        externalTerrainMipLevelCount_ = 0;
        ownedTerrainMipLevelCount_ = 0;
        terrainHeightScale_ = 0.0f;
        terrainCellScale_ = 0.0f;
        ownedTerrainBytes_ = 0;
        lastStepStats_ = {};
    }

    static void releaseTerrainTexture(WGPUTexture& texture,
                                      WGPUTextureView& view) {
        releaseHandle(view, wgpuTextureViewRelease);
        if (texture) {
            wgpuTextureDestroy(texture);
            wgpuTextureRelease(texture);
            texture = nullptr;
        }
    }

    bool createFallbackTerrain() {
        const uint16_t seaLevel = 32'768u;
        auto desc = gpu::TextureDesc::tex2D(
            1, 1, WGPUTextureFormat_R16Uint,
            WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst,
            "physics_fallback_heightmap");
        fallbackTerrainTexture_ = gpu::createTextureWithData(
            device_, queue_, desc,
            std::as_bytes(std::span<const uint16_t>(&seaLevel, 1)),
            sizeof(seaLevel));
        if (!fallbackTerrainTexture_) return false;
        gpu::TextureViewDesc viewDesc;
        viewDesc.label = "physics_fallback_heightmap_view";
        viewDesc.format = WGPUTextureFormat_R16Uint;
        fallbackTerrainView_ = gpu::createTextureView(
            fallbackTerrainTexture_, viewDesc);
        return fallbackTerrainView_ != nullptr;
    }

    bool createFallbackWater() {
        auto desc = gpu::TextureDesc::storage(
            1, 1, WGPUTextureFormat_RGBA16Float,
            "physics_fallback_water_displacement");
        desc.depthOrArrayLayers = 3u;
        fallbackWaterTexture_ = gpu::createTexture(device_, desc);
        if (!fallbackWaterTexture_) return false;
        gpu::TextureViewDesc viewDesc;
        viewDesc.label = "physics_fallback_water_displacement_view";
        viewDesc.format = WGPUTextureFormat_RGBA16Float;
        viewDesc.dimension = WGPUTextureViewDimension_2DArray;
        viewDesc.arrayLayerCount = 3u;
        fallbackWaterView_ = gpu::createTextureView(
            fallbackWaterTexture_, viewDesc);
        auto samplerDesc = gpu::SamplerDesc::repeat(
            WGPUFilterMode_Linear, "physics_fallback_water_sampler");
        fallbackWaterSampler_ = gpu::createSampler(device_, samplerDesc);
        return fallbackWaterView_ && fallbackWaterSampler_;
    }

    [[nodiscard]] WGPUTextureView terrainBindingView() const noexcept {
        if (externalTerrainView_) return externalTerrainView_;
        if (ownedTerrainView_) return ownedTerrainView_;
        return fallbackTerrainView_;
    }

    [[nodiscard]] WGPUTextureView waterBindingView() const noexcept {
        return externalWaterView_ ? externalWaterView_ : fallbackWaterView_;
    }

    [[nodiscard]] WGPUSampler waterBindingSampler() const noexcept {
        return externalWaterSampler_
            ? externalWaterSampler_ : fallbackWaterSampler_;
    }

    bool createOwnedTerrain(std::span<const uint16_t> samples,
                            uint32_t width, uint32_t height) {
        const uint32_t mipCount = terrain::calculateMipLevelCount(width, height);
        auto desc = gpu::TextureDesc::tex2D(
            width, height, WGPUTextureFormat_R16Uint,
            WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst,
            "physics_owned_heightmap");
        desc.mipLevelCount = mipCount;
        WGPUTexture replacementTexture = gpu::createTexture(device_, desc);
        WGPUTextureView replacementView = nullptr;
        if (!replacementTexture) return false;

        if (!gpu::writeTexture(
                queue_, replacementTexture, std::as_bytes(samples),
                width, height, width * sizeof(uint16_t), 0)) {
            releaseTerrainTexture(replacementTexture, replacementView);
            return false;
        }
        size_t allocatedBytes = samples.size_bytes();
        std::span<const uint16_t> previous = samples;
        uint32_t previousWidth = width;
        uint32_t previousHeight = height;
        terrain::MipLevel current;
        for (uint32_t level = 1; level < mipCount; ++level) {
            terrain::MipLevel next = terrain::generateNextMipLevel(
                previous, previousWidth, previousHeight);
            if (!next.isValid()) {
                releaseTerrainTexture(replacementTexture, replacementView);
                return false;
            }
            if (!gpu::writeTexture(
                    queue_, replacementTexture,
                    std::as_bytes(
                        std::span<const uint16_t>(next.data)),
                    next.width, next.height,
                    next.width * sizeof(uint16_t), level)) {
                releaseTerrainTexture(
                    replacementTexture, replacementView);
                return false;
            }
            allocatedBytes += next.sizeBytes();
            current = std::move(next);
            previous = current.data;
            previousWidth = current.width;
            previousHeight = current.height;
        }

        gpu::TextureViewDesc viewDesc;
        viewDesc.label = "physics_owned_heightmap_view";
        viewDesc.format = WGPUTextureFormat_R16Uint;
        viewDesc.mipLevelCount = mipCount;
        replacementView = gpu::createTextureView(replacementTexture, viewDesc);
        if (!replacementView
            || !replaceIntegrateBindGroup(
                replacementView, waterBindingView(), waterBindingSampler())) {
            releaseTerrainTexture(replacementTexture, replacementView);
            return false;
        }
        releaseTerrainTexture(ownedTerrainTexture_, ownedTerrainView_);
        ownedTerrainTexture_ = replacementTexture;
        ownedTerrainView_ = replacementView;
        terrainMipLevelCount_ = mipCount;
        ownedTerrainMipLevelCount_ = mipCount;
        ownedTerrainBytes_ = allocatedBytes;
        return true;
    }

    bool createPipelinesAndBindings() {
        using LE = gpu::BindGroupLayoutEntry;
        using BE = gpu::BindGroupEntry;

        std::vector<LE> commandEntries;
        for (uint32_t binding : {0u, 1u, 2u, 3u, 5u, 6u}) {
            commandEntries.emplace_back(binding);
            commandEntries.back().computeVisible().storageBuffer(false);
        }
        commandEntries.emplace_back(7u);
        commandEntries.back().computeVisible().storageBuffer(true);
        commandEntries.emplace_back(8u);
        commandEntries.back().computeVisible().uniformBuffer(
            false, sizeof(SimulationUniforms));
        commandLayout_ = gpu::createBindGroupLayout(
            device_, commandEntries, "physics_command_layout");

        std::vector<LE> compactEntries;
        for (uint32_t binding : {3u, 6u, 9u, 10u, 11u, 12u}) {
            compactEntries.emplace_back(binding);
            compactEntries.back().computeVisible().storageBuffer(false);
        }
        compactEntries.emplace_back(8u);
        compactEntries.back().computeVisible().uniformBuffer(
            false, sizeof(SimulationUniforms));
        compactLayout_ = gpu::createBindGroupLayout(
            device_, compactEntries, "physics_compact_layout");

        std::vector<LE> integrateEntries;
        for (uint32_t binding : {0u, 1u, 2u, 3u, 5u, 6u, 9u, 15u}) {
            integrateEntries.emplace_back(binding);
            integrateEntries.back().computeVisible().storageBuffer(false);
        }
        integrateEntries.emplace_back(8u);
        integrateEntries.back().computeVisible().uniformBuffer(
            false, sizeof(SimulationUniforms));
        integrateEntries.emplace_back(14u);
        integrateEntries.back().computeVisible().texture(
            WGPUTextureSampleType_Uint, WGPUTextureViewDimension_2D, false);
        integrateEntries.emplace_back(16u);
        integrateEntries.back().computeVisible().texture(
            WGPUTextureSampleType_Float,
            WGPUTextureViewDimension_2DArray, false);
        integrateEntries.emplace_back(17u);
        integrateEntries.back().computeVisible().sampler(
            WGPUSamplerBindingType_Filtering);
        integrateLayout_ = gpu::createBindGroupLayout(
            device_, integrateEntries, "physics_integrate_layout");

        std::vector<LE> tickEntries;
        tickEntries.emplace_back(6u);
        tickEntries.back().computeVisible().storageBuffer(false);
        tickLayout_ = gpu::createBindGroupLayout(
            device_, tickEntries, "physics_tick_layout");

        std::vector<LE> debugEntries;
        for (uint32_t binding : {0u, 1u, 2u, 3u, 13u}) {
            debugEntries.emplace_back(binding);
            debugEntries.back().computeVisible().storageBuffer(false);
        }
        debugEntries.emplace_back(8u);
        debugEntries.back().computeVisible().uniformBuffer(
            false, sizeof(SimulationUniforms));
        debugLayout_ = gpu::createBindGroupLayout(
            device_, debugEntries, "physics_debug_layout");
        if (!commandLayout_ || !compactLayout_ || !integrateLayout_
            || !tickLayout_ || !debugLayout_) return false;

        const std::array<WGPUBindGroupLayout, 1> commandLayouts{commandLayout_};
        const std::array<WGPUBindGroupLayout, 1> compactLayouts{compactLayout_};
        const std::array<WGPUBindGroupLayout, 1> integrateLayouts{integrateLayout_};
        const std::array<WGPUBindGroupLayout, 1> tickLayouts{tickLayout_};
        const std::array<WGPUBindGroupLayout, 1> debugLayouts{debugLayout_};
        commandPipelineLayout_ = gpu::createPipelineLayout(
            device_, commandLayouts, "physics_command_pipeline_layout");
        compactPipelineLayout_ = gpu::createPipelineLayout(
            device_, compactLayouts, "physics_compact_pipeline_layout");
        integratePipelineLayout_ = gpu::createPipelineLayout(
            device_, integrateLayouts, "physics_integrate_pipeline_layout");
        tickPipelineLayout_ = gpu::createPipelineLayout(
            device_, tickLayouts, "physics_tick_pipeline_layout");
        debugPipelineLayout_ = gpu::createPipelineLayout(
            device_, debugLayouts, "physics_debug_pipeline_layout");
        if (!commandPipelineLayout_ || !compactPipelineLayout_
            || !integratePipelineLayout_ || !tickPipelineLayout_
            || !debugPipelineLayout_) return false;

        applyCommandsPipeline_ = makeComputePipeline(
            device_, commandPipelineLayout_, shaderModule_, "apply_commands",
            "physics_apply_commands");
        compactBlocksPipeline_ = makeComputePipeline(
            device_, compactPipelineLayout_, shaderModule_, "compact_blocks",
            "physics_compact_blocks");
        scanBlocksPipeline_ = makeComputePipeline(
            device_, compactPipelineLayout_, shaderModule_, "scan_blocks",
            "physics_scan_blocks");
        scatterActivePipeline_ = makeComputePipeline(
            device_, compactPipelineLayout_, shaderModule_, "scatter_active",
            "physics_scatter_active");
        preparePipeline_ = makeComputePipeline(
            device_, integratePipelineLayout_, shaderModule_,
            "prepare_dynamic_bodies", "physics_prepare_dynamic_bodies");
        staticContactPipeline_ = makeComputePipeline(
            device_, integratePipelineLayout_, shaderModule_,
            "solve_static_contacts", "physics_solve_static_contacts");
        advanceTickPipeline_ = makeComputePipeline(
            device_, tickPipelineLayout_, shaderModule_, "advance_tick",
            "physics_advance_tick");
        packDebugPipeline_ = makeComputePipeline(
            device_, debugPipelineLayout_, shaderModule_, "pack_debug",
            "physics_pack_debug");
        if (!applyCommandsPipeline_ || !compactBlocksPipeline_
            || !scanBlocksPipeline_ || !scatterActivePipeline_
            || !preparePipeline_ || !staticContactPipeline_
            || !advanceTickPipeline_
            || !packDebugPipeline_) return false;

        const std::array<BE, 8> commandBindings = {
            BE(0).buffer(poseBuffer_), BE(1).buffer(motionBuffer_),
            BE(2).buffer(shapeBuffer_), BE(3).buffer(metadataBuffer_),
            BE(5).buffer(forceBuffer_), BE(6).buffer(countersBuffer_),
            BE(7).buffer(commandBuffer_), BE(8).buffer(uniformBuffer_)};
        commandBindGroup_ = gpu::createBindGroup(
            device_, commandLayout_, commandBindings, "physics_commands");

        const std::array<BE, 7> compactBindings = {
            BE(3).buffer(metadataBuffer_), BE(6).buffer(countersBuffer_),
            BE(9).buffer(activeIdsBuffer_), BE(10).buffer(activeOffsetsBuffer_),
            BE(11).buffer(blockSumsBuffer_), BE(12).buffer(blockPrefixBuffer_),
            BE(8).buffer(uniformBuffer_)};
        compactBindGroup_ = gpu::createBindGroup(
            device_, compactLayout_, compactBindings, "physics_compaction");

        const std::array<BE, 1> tickBindings = {
            BE(6).buffer(countersBuffer_)};
        tickBindGroup_ = gpu::createBindGroup(
            device_, tickLayout_, tickBindings, "physics_tick");

        const std::array<BE, 6> debugBindings = {
            BE(0).buffer(poseBuffer_), BE(1).buffer(motionBuffer_),
            BE(2).buffer(shapeBuffer_), BE(3).buffer(metadataBuffer_),
            BE(13).buffer(debugPackedBuffer_), BE(8).buffer(uniformBuffer_)};
        debugBindGroup_ = gpu::createBindGroup(
            device_, debugLayout_, debugBindings, "physics_debug_pack");
        return commandBindGroup_ && compactBindGroup_ && tickBindGroup_
            && debugBindGroup_ && rebuildIntegrateBindGroup();
    }

    bool replaceIntegrateBindGroup(WGPUTextureView terrainView,
                                   WGPUTextureView waterView,
                                   WGPUSampler waterSampler) {
        using BE = gpu::BindGroupEntry;
        if (!integrateLayout_ || !terrainView || !waterView || !waterSampler)
            return false;
        const std::array<BE, 12> bindings = {
            BE(0).buffer(poseBuffer_),
            BE(1).buffer(motionBuffer_),
            BE(2).buffer(shapeBuffer_),
            BE(3).buffer(metadataBuffer_),
            BE(5).buffer(forceBuffer_),
            BE(6).buffer(countersBuffer_),
            BE(9).buffer(activeIdsBuffer_),
            BE(15).buffer(terrainContactCacheBuffer_),
            BE(8).buffer(uniformBuffer_),
            BE(14).textureView(terrainView),
            BE(16).textureView(waterView),
            BE(17).sampler(waterSampler),
        };
        WGPUBindGroup replacement = gpu::createBindGroup(
            device_, integrateLayout_, bindings, "physics_integration");
        if (!replacement) return false;
        releaseHandle(integrateBindGroup_, wgpuBindGroupRelease);
        integrateBindGroup_ = replacement;
        return true;
    }

    bool rebuildIntegrateBindGroup() {
        return replaceIntegrateBindGroup(
            terrainBindingView(), waterBindingView(), waterBindingSampler());
    }

    bool setTerrain(std::span<const uint16_t> samples,
                    uint32_t width, uint32_t height,
                    float heightScale, float cellScale, bool lego = false) {
        const bool hadTerrain = terrainAttached_;
        if (!initialized_ || width < 2 || height < 2
            || !std::isfinite(heightScale) || heightScale <= 0.0f
            || !std::isfinite(cellScale) || cellScale <= 0.0f
            || uint64_t{width} * height
                > std::numeric_limits<size_t>::max()
            || (!externalTerrainView_
                && (width > deviceLimits_.maxTextureDimension2D
                    || height > deviceLimits_.maxTextureDimension2D))) {
            terrainStateNeedsClear_ = terrainStateNeedsClear_
                || terrainAttached_;
            terrainAttached_ = false;
            characterMover_.clearTerrain();
            refreshCcdInput();
            return false;
        }
        const size_t expected = size_t{width} * height;
        if (samples.size() < expected) {
            terrainStateNeedsClear_ = terrainStateNeedsClear_
                || terrainAttached_;
            terrainAttached_ = false;
            characterMover_.clearTerrain();
            refreshCcdInput();
            return false;
        }

        terrainWidth_ = width;
        terrainHeight_ = height;
        terrainHeightScale_ = heightScale;
        terrainCellScale_ = cellScale;
        legoTerrain_ = lego;
        bool terrainBindingReady = false;
        if (externalTerrainView_) {
            terrainMipLevelCount_ = std::max(externalTerrainMipLevelCount_, 1u);
            terrainBindingReady = rebuildIntegrateBindGroup();
        } else if (!createOwnedTerrain(samples.first(expected), width, height)) {
            terrainStateNeedsClear_ = terrainStateNeedsClear_
                || terrainAttached_;
            terrainAttached_ = false;
            characterMover_.clearTerrain();
            refreshCcdInput();
            return false;
        } else {
            terrainBindingReady = true;
        }
        if (!terrainBindingReady) {
            terrainStateNeedsClear_ = terrainStateNeedsClear_ || hadTerrain;
            terrainAttached_ = false;
            characterMover_.clearTerrain();
            refreshCcdInput();
            return false;
        }
        terrainAttached_ = true;
        if (!characterMover_.setTerrain(
                samples.first(expected), width, height,
                heightScale, cellScale, lego)) {
            terrainStateNeedsClear_ = terrainStateNeedsClear_ || hadTerrain;
            terrainAttached_ = false;
        }
        if (terrainAttached_) terrainStateNeedsClear_ = false;
        refreshCcdInput();
        return terrainAttached_;
    }

    void setTerrainGpuResources(const TerrainGpuResources& resources) {
        externalTerrainView_ = resources.maxHeightTexture;
        externalTerrainMipLevelCount_ = resources.maxHeightTexture
            ? std::max(resources.mipLevelCount, 1u) : 0u;
        if (terrainAttached_ && externalTerrainView_) {
            terrainMipLevelCount_ = externalTerrainMipLevelCount_;
        }
        if (initialized_ && !rebuildIntegrateBindGroup()) {
            LOG_ERROR("Failed to bind GPU terrain resources");
            terrainStateNeedsClear_ = terrainStateNeedsClear_
                || terrainAttached_;
            terrainAttached_ = false;
        }
        if (externalTerrainView_ && ownedTerrainTexture_) {
            releaseTerrainTexture(ownedTerrainTexture_, ownedTerrainView_);
            ownedTerrainBytes_ = 0;
        }
        refreshCcdInput();
    }

    void clearTerrain() {
        terrainStateNeedsClear_ = terrainStateNeedsClear_ || terrainAttached_;
        terrainAttached_ = false;
        characterMover_.clearTerrain();
        externalTerrainView_ = nullptr;
        externalTerrainMipLevelCount_ = 0;
        terrainWidth_ = 0;
        terrainHeight_ = 0;
        terrainMipLevelCount_ = 0;
        terrainHeightScale_ = 0.0f;
        terrainCellScale_ = 0.0f;
        releaseTerrainTexture(ownedTerrainTexture_, ownedTerrainView_);
        ownedTerrainBytes_ = 0;
        if (initialized_ && !rebuildIntegrateBindGroup()) {
            LOG_ERROR("Failed to restore fallback terrain binding");
        }
        refreshCcdInput();
    }

    void setWaterGpuResources(const WaterGpuResources& resources) {
        if (resources.displacementTexture || resources.displacementSampler) {
            if (!resources.valid()) {
                LOG_WARN("Ignoring invalid GPU water-surface resources");
                return;
            }
            externalWaterView_ = resources.displacementTexture;
            externalWaterSampler_ = resources.displacementSampler;
            waterSurfaceStrength_ = resources.strength;
            waterPatchLengths_ = {
                resources.broadPatchLength, resources.detailPatchLength};
        } else {
            externalWaterView_ = nullptr;
            externalWaterSampler_ = nullptr;
            waterSurfaceStrength_ = 0.0f;
            waterPatchLengths_ = {1949.0f, 326.0f};
        }
        if (initialized_ && !rebuildIntegrateBindGroup()) {
            LOG_ERROR("Failed to bind GPU water-surface resources");
            externalWaterView_ = nullptr;
            externalWaterSampler_ = nullptr;
            waterSurfaceStrength_ = 0.0f;
            waterPatchLengths_ = {1949.0f, 326.0f};
            static_cast<void>(rebuildIntegrateBindGroup());
        }
    }

    [[nodiscard]] uint32_t executionBodyCount() const noexcept {
        if (bodyCapacity_ == 0u) return 0u;
        uint32_t count = std::clamp(nextUnusedIndex_, 1u, bodyCapacity_);
        if (debugRequest_) {
            const uint64_t debugEnd = uint64_t{debugRequest_->firstBody}
                                    + debugRequest_->bodyCount;
            count = std::max(count, static_cast<uint32_t>(std::min(
                debugEnd, uint64_t{bodyCapacity_})));
        }
        return count;
    }

    void refreshCcdInput(uint32_t executionBodies = 0u) {
        if (executionBodies == 0u) executionBodies = executionBodyCount();
        if (!terrainAttached_) {
            ccd_.setInput({});
            return;
        }
        ccd_.setInput({
            .poseBuffer = poseBuffer_,
            .motionBuffer = motionBuffer_,
            .shapeBuffer = shapeBuffer_,
            .metadataBuffer = metadataBuffer_,
            .terrainTexture = terrainBindingView(),
            .bodyCapacity = executionBodies,
            .terrainWidth = terrainWidth_,
            .terrainHeight = terrainHeight_,
            .terrainHeightScale = terrainHeightScale_,
            .terrainCellScale = terrainCellScale_,
            .legoTerrain = legoTerrain_,
            .terrainSector = {0, 0, 0},
        });
    }

    void refreshContactInputs(uint32_t executionBodies = 0u) {
        if (executionBodies == 0u) executionBodies = executionBodyCount();
        const WGPUBuffer manifolds = narrowPhase_.activeManifolds();
        dynamicSolver_.setInput({
            .poseBuffer = poseBuffer_,
            .motionBuffer = motionBuffer_,
            .shapeBuffer = shapeBuffer_,
            .metadataBuffer = metadataBuffer_,
            .manifoldBuffer = manifolds,
            .narrowPhaseTelemetryBuffer = narrowPhase_.telemetryBuffer(),
            .bodyCapacity = executionBodies,
            .contactCapacity = contactCapacity_,
            .activeContactDispatchBuffer =
                narrowPhase_.activeContactDispatchBuffer(),
            .activeContactDispatchOffset =
                GpuNarrowPhase::kActiveContactDispatchOffset,
        });
        islandManager_.setInput({
            .poseBuffer = poseBuffer_,
            .motionBuffer = motionBuffer_,
            .metadataBuffer = metadataBuffer_,
            .manifoldBuffer = manifolds,
            .narrowPhaseTelemetryBuffer = narrowPhase_.telemetryBuffer(),
            .bodyCapacity = executionBodies,
            .contactCapacity = contactCapacity_,
        });
    }

    void refreshEventSources(uint32_t executionBodies) {
        eventReadback_.setSources({
            .contactEvents = broadPhase_.contactEvents(),
            .contactTelemetry = broadPhase_.telemetryBuffer(),
            .contactCapacity = broadPhase_.contactCapacity(),
            .islandEvents = islandManager_.events(),
            .islandTelemetry = islandManager_.telemetryBuffer(),
            .islandEventCapacity = executionBodies,
            .manifolds = narrowPhase_.manifolds(),
            .narrowPhaseTelemetry = narrowPhase_.telemetryBuffer(),
            .manifoldCapacity = manifoldCapacity_,
            .metadata = metadataBuffer_,
            .bodyCapacity = executionBodies,
            .attachments = attachmentSolver_.attachmentBuffer(),
            .attachmentCapacity = attachmentCapacity_,
        });
    }

    [[nodiscard]] bool initializeEventReadback() {
        if (eventReadback_.allocatedBytes() != 0u) return true;
        GpuEventReadbackRing::Config eventConfig;
        eventConfig.eventCapacity = eventCapacity_;
        eventConfig.readbackSlots = config_.eventReadbackSlots == 0
            ? std::max(config_.maximumCatchUpTicks, 3u)
            : config_.eventReadbackSlots;
        const std::filesystem::path mainShaderPath(config_.shaderPath);
        eventConfig.shaderPath = (mainShaderPath.parent_path().empty()
                ? std::filesystem::path("shaders")
                : mainShaderPath.parent_path())
            / "physics_event_readback.wgsl";
        eventConfig.shaderSources = config_.shaderSources;
        if (!eventReadback_.initialize(device_, queue_, eventConfig)) {
            LOG_ERROR("Failed to allocate GPU physics event readback");
            return false;
        }
        refreshEventSources(executionBodyCount());
        return true;
    }

    void refreshExecutionInputs(uint32_t executionBodies) {
        broadPhase_.setBodyView({
            .poseBuffer = poseBuffer_,
            .shapeBuffer = shapeBuffer_,
            .metadataBuffer = metadataBuffer_,
            .bodyCapacity = executionBodies,
        });
        narrowPhase_.setInput({
            .poseBuffer = poseBuffer_,
            .shapeBuffer = shapeBuffer_,
            .uniquePairBuffer = broadPhase_.uniquePairs(),
            .broadPhaseTelemetryBuffer = broadPhase_.telemetryBuffer(),
            .bodyCapacity = executionBodies,
            .pairCapacity = pairCapacity_,
            .metadataBuffer = metadataBuffer_,
        });
        querySystem_.setBodyView({
            .poseBuffer = poseBuffer_,
            .shapeBuffer = shapeBuffer_,
            .metadataBuffer = metadataBuffer_,
            .bodyCapacity = executionBodies,
        });
        attachmentSolver_.setInput({
            .poseBuffer = poseBuffer_,
            .motionBuffer = motionBuffer_,
            .shapeBuffer = shapeBuffer_,
            .metadataBuffer = metadataBuffer_,
            .coreCountersBuffer = countersBuffer_,
            .bodyCapacity = executionBodies,
        });
        refreshCcdInput(executionBodies);
        refreshContactInputs(executionBodies);
        refreshEventSources(executionBodies);
    }

    void shrinkUnusedTail() {
        while (nextUnusedIndex_ > 1u) {
            const uint32_t last = nextUnusedIndex_ - 1u;
            if (hostAlive_[last] || !freeIndices_.contains(last)) break;
            freeIndices_.erase(last);
            --nextUnusedIndex_;
        }
    }

    [[nodiscard]] bool hostHandleAllocated(BodyHandle handle) const noexcept {
        return initialized_ && handle.valid() && handle.index < bodyCapacity_
            && hostAlive_[handle.index]
            && generations_[handle.index] == handle.generation;
    }

    [[nodiscard]] bool preparedMutationActive() const noexcept {
        return preparedMutation_.has_value()
            && preparedMutation_->token != 0u;
    }

    [[nodiscard]] bool hostHandleAliveAt(
        BodyHandle handle, uint64_t targetTick) const noexcept {
        if (!hostHandleAllocated(handle)) return false;
        const uint64_t spawnTick = scheduledSpawnTicks_[handle.index];
        const uint64_t destroyTick = scheduledDestroyTicks_[handle.index];
        return (spawnTick == 0u || targetTick >= spawnTick)
            && (destroyTick == 0u || targetTick < destroyTick);
    }

    uint64_t assignCommandSequence(uint64_t requested = 0u) noexcept {
        if (requested != 0u) {
            if (requested >= nextSequence_) {
                nextSequence_ = requested == std::numeric_limits<uint64_t>::max()
                    ? requested : requested + 1u;
            }
            return requested;
        }
        const uint64_t assigned = nextSequence_;
        if (nextSequence_ != std::numeric_limits<uint64_t>::max()) {
            ++nextSequence_;
        }
        return assigned;
    }

    void releaseHostBody(uint32_t index) {
        hostAlive_[index] = false;
        scheduledSpawnTicks_[index] = 0u;
        scheduledDestroyTicks_[index] = 0u;
        generations_[index] = nextBodyGeneration(generations_[index]);
        if (residentBodies_ != 0u) --residentBodies_;
        freeIndices_.insert(index);
        shrinkUnusedTail();
    }

    bool cancelPendingSpawn(BodyHandle handle, uint64_t destroyTick) {
        const auto pendingSpawn = std::find_if(
            commands_.begin(), commands_.end(), [handle](const auto& command) {
                return command.type == PhysicsCommandType::SpawnBody
                    && command.body == handle;
            });
        if (pendingSpawn == commands_.end()
            || destroyTick > pendingSpawn->targetTick) return false;
        commands_.erase(std::remove_if(
            commands_.begin(), commands_.end(), [handle](const auto& command) {
                return command.body == handle;
            }), commands_.end());
        pendingFrees_.erase(std::remove_if(
            pendingFrees_.begin(), pendingFrees_.end(),
            [handle](const PendingFree& pending) {
                return pending.index == handle.index
                    && pending.generation == handle.generation;
            }), pendingFrees_.end());
        releaseHostBody(handle.index);
        return true;
    }

    bool queueDestroy(BodyHandle handle, uint64_t targetTick,
                      uint64_t sequence) {
        if (!hostHandleAllocated(handle) || targetTick <= encodedTick_) {
            return false;
        }
        // Destroying on or before a not-yet-executed spawn cancels that entire
        // lifetime. A later destroy must remain queued so the body exists for
        // every intervening tick.
        if (cancelPendingSpawn(handle, targetTick)) return true;

        const uint64_t previousDestroy = scheduledDestroyTicks_[handle.index];
        if (previousDestroy != 0u) {
            if (previousDestroy <= targetTick) return false;
            commands_.erase(std::remove_if(
                commands_.begin(), commands_.end(),
                [handle](const PhysicsCommand& command) {
                    return command.type == PhysicsCommandType::DestroyBody
                        && command.body == handle;
                }), commands_.end());
            pendingFrees_.erase(std::remove_if(
                pendingFrees_.begin(), pendingFrees_.end(),
                [handle](const PendingFree& pending) {
                    return pending.index == handle.index
                        && pending.generation == handle.generation;
                }), pendingFrees_.end());
            scheduledDestroyTicks_[handle.index] = 0u;
        }
        if (commands_.size() >= config_.commandCapacity) {
            commandCapacityOverflow_ = true;
            return false;
        }
        PhysicsCommand command;
        command.type = PhysicsCommandType::DestroyBody;
        command.body = handle;
        command.targetTick = targetTick;
        command.sequence = sequence;
        commands_.push_back(command);
        scheduledDestroyTicks_[handle.index] = targetTick;
        pendingFrees_.push_back({targetTick, handle.index, handle.generation});
        return true;
    }

    BodyHandle spawn(const BodySpawnDesc& requested) {
        if (!initialized_ || preparedMutationActive()) return {};
        if (!finiteVector(requested.position)
            || !finiteQuaternion(requested.orientation)
            || !finiteVector(requested.linearVelocity)
            || !finiteVector(requested.angularVelocity)
            || !finiteVector(requested.dimensions)
            || !std::isfinite(requested.inverseMass)
            || (requested.material
                && !validMaterial(*requested.material))) {
            LOG_WARN("Discarding non-finite GPU body spawn");
            return {};
        }
        if (commands_.size() >= config_.commandCapacity) {
            commandCapacityOverflow_ = true;
            return {};
        }
        uint32_t index = 0;
        if (!freeIndices_.empty()) {
            const auto first = freeIndices_.begin();
            index = *first;
            freeIndices_.erase(first);
        } else if (nextUnusedIndex_ < bodyCapacity_) {
            index = nextUnusedIndex_++;
        }
        if (index == 0) {
            bodyCapacityOverflow_ = true;
            return {};
        }

        BodySpawnDesc desc = requested;
        if (desc.shape >= ThrowableShape::Count) desc.shape = ThrowableShape::Sphere;
        if (glm::any(glm::lessThanEqual(desc.dimensions, glm::vec3(0.0f)))) {
            desc.dimensions = throwableShapeDimensions(desc.shape);
        }
        desc.inverseMass = std::max(desc.inverseMass, 0.0f);
        if (!desc.material) {
            desc.material = PhysicsMaterial{
                .friction = config_.bodyFriction,
                .restitution = desc.shape == ThrowableShape::Sphere
                    ? config_.bodySphereRestitution
                    : config_.bodyOtherRestitution,
                .rollingResistance = 0.01f,
                .density = 1.0f,
                .flags = 0u,
            };
        }
        const float quaternionLength = glm::length(desc.orientation);
        desc.orientation = quaternionLength > 1e-6f
            ? desc.orientation / quaternionLength
            : glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
        const WorldPosition worldPosition = canonicalWorldPosition(
            desc.sector, glm::dvec3(desc.position));
        desc.sector = worldPosition.sector;
        desc.position = worldPosition.local;

        const BodyHandle handle{index, generations_[index]};
        PhysicsCommand command;
        command.type = PhysicsCommandType::SpawnBody;
        command.body = handle;
        command.targetTick = nextMutationTick();
        command.sequence = assignCommandSequence();
        command.shape = desc.shape;
        command.a = glm::vec4(desc.position, desc.inverseMass);
        command.b = glm::vec4(desc.orientation.x, desc.orientation.y,
                              desc.orientation.z, desc.orientation.w);
        command.c = glm::vec4(desc.linearVelocity, 0.0f);
        command.d = glm::vec4(desc.angularVelocity, desc.bullet ? 1.0f : 0.0f);
        command.e = glm::vec4(desc.dimensions,
                              static_cast<float>(desc.shape));
        command.sector = desc.sector;
        command.material = desc.material;
        commands_.push_back(command);
        hostAlive_[index] = true;
        scheduledSpawnTicks_[index] = command.targetTick;
        scheduledDestroyTicks_[index] = 0u;
        ++residentBodies_;
        highResidentBodies_ = std::max(highResidentBodies_, residentBodies_);
        return handle;
    }

    bool destroy(BodyHandle handle) {
        if (preparedMutationActive()) return false;
        return queueDestroy(
            handle, nextMutationTick(), assignCommandSequence());
    }

    template <typename Value, typename Less>
    static void stableSortWithScratch(
        std::vector<Value>& values,
        std::vector<Value>& scratch,
        Less less) noexcept {
        const size_t count = values.size();
        if (count < 2u) return;
        for (size_t width = 1u; width < count;) {
            scratch.clear();
            for (size_t begin = 0u; begin < count;
                 begin += 2u * width) {
                const size_t middle =
                    std::min(begin + width, count);
                const size_t end =
                    std::min(begin + 2u * width, count);
                size_t left = begin;
                size_t right = middle;
                while (left < middle && right < end) {
                    if (less(values[right], values[left])) {
                        scratch.push_back(values[right++]);
                    } else {
                        scratch.push_back(values[left++]);
                    }
                }
                while (left < middle) {
                    scratch.push_back(values[left++]);
                }
                while (right < end) {
                    scratch.push_back(values[right++]);
                }
            }
            values.swap(scratch);
            if (width > count / 2u) break;
            width *= 2u;
        }
    }

    void sortAndCoalesceKinematicTargets() noexcept {
        stableSortWithScratch(
            commands_, commandSortScratch_,
            [](const PhysicsCommand& lhs, const PhysicsCommand& rhs) {
                if (lhs.targetTick != rhs.targetTick)
                    return lhs.targetTick < rhs.targetTick;
                const uint32_t lp = commandPriority(lhs.type);
                const uint32_t rp = commandPriority(rhs.type);
                if (lp != rp) return lp < rp;
                return lhs.sequence < rhs.sequence;
            });

        // A kinematic target describes the pose at the end of a tick, not an
        // incremental move. Applying several targets for the same body/tick
        // would otherwise derive velocity from only the last tiny segment.
        // Retain the final target in each uninterrupted pose-command run.
        commandSuperseded_.assign(commands_.size(), uint8_t{0u});
        std::fill(
            latestKinematicIndex_.begin(),
            latestKinematicIndex_.end(),
            std::numeric_limits<size_t>::max());
        for (size_t index = 0u; index < commands_.size(); ++index) {
            const PhysicsCommand& command = commands_[index];
            if (command.body.index >= bodyCapacity_) continue;
            const uint32_t body = command.body.index;
            if (command.type == PhysicsCommandType::SetKinematicTarget) {
                if (latestKinematicTick_[body] == command.targetTick
                    && latestKinematicGeneration_[body]
                        == command.body.generation
                    && latestKinematicIndex_[body]
                        != std::numeric_limits<size_t>::max()) {
                    commandSuperseded_[latestKinematicIndex_[body]] = 1u;
                }
                latestKinematicTick_[body] = command.targetTick;
                latestKinematicGeneration_[body] =
                    command.body.generation;
                latestKinematicIndex_[body] = index;
            } else if (command.type == PhysicsCommandType::Teleport
                       || command.type == PhysicsCommandType::SpawnBody
                       || command.type == PhysicsCommandType::DestroyBody) {
                if (latestKinematicTick_[body] == command.targetTick
                    && latestKinematicGeneration_[body]
                        == command.body.generation) {
                    latestKinematicIndex_[body] =
                        std::numeric_limits<size_t>::max();
                }
            }
        }
        size_t destination = 0u;
        for (size_t source = 0u; source < commands_.size(); ++source) {
            if (commandSuperseded_[source] != 0u) continue;
            if (destination != source) {
                commands_[destination] =
                    std::move(commands_[source]);
            }
            ++destination;
        }
        commands_.resize(destination);
    }

    void enqueueCommands(std::span<const PhysicsCommand> input) {
        if (preparedMutationActive()) return;
        bool queuedKinematicTarget = false;
        for (PhysicsCommand command : input) {
            if (static_cast<uint32_t>(command.type)
                    > static_cast<uint32_t>(
                        PhysicsCommandType::ApplyForceAtLocalPoint)
                || !finiteVector(command.a) || !finiteVector(command.b)
                || !finiteVector(command.c) || !finiteVector(command.d)
                || !finiteVector(command.e)
                || (command.material
                    && !validMaterial(*command.material))
                || (command.type == PhysicsCommandType::SetMaterial
                    && !command.material)) {
                LOG_WARN("Discarding invalid or non-finite GPU physics command");
                continue;
            }
            if (command.targetTick == 0u) {
                command.targetTick = nextMutationTick();
            } else if (command.targetTick <= encodedTick_) {
                LOG_WARN("Discarding GPU physics command for retired tick {} (current {})",
                         command.targetTick, encodedTick_);
                continue;
            }
            command.sequence = assignCommandSequence(command.sequence);

            if (command.type == PhysicsCommandType::DestroyBody) {
                static_cast<void>(queueDestroy(
                    command.body, command.targetTick, command.sequence));
                continue;
            }
            if (commands_.size() >= config_.commandCapacity) {
                sortAndCoalesceKinematicTargets();
                if (commands_.size() >= config_.commandCapacity
                    && command.type
                        != PhysicsCommandType::SetKinematicTarget) {
                    commandCapacityOverflow_ = true;
                    LOG_WARN("GPU physics command capacity {} exceeded",
                             config_.commandCapacity);
                    break;
                }
            }
            if (!command.body.valid() || command.body.index >= bodyCapacity_)
                continue;
            if (command.type == PhysicsCommandType::SpawnBody) {
                const uint32_t index = command.body.index;
                const bool nextSlot = index == nextUnusedIndex_
                    && nextUnusedIndex_ < bodyCapacity_;
                const bool reusableSlot = freeIndices_.contains(index);
                if (hostAlive_[index]
                    || generations_[index] != command.body.generation
                    || (!nextSlot && !reusableSlot)) {
                    LOG_WARN("Discarding invalid replay SpawnBody for slot {} generation {}",
                             index, command.body.generation);
                    continue;
                }
                command.shape = std::min(
                    command.shape, ThrowableShape::Cylinder);
                command.a.w = std::max(command.a.w, 0.0f);
                const float quaternionLength = glm::length(command.b);
                command.b = quaternionLength > 1.0e-6f
                    ? command.b / quaternionLength
                    : glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
                command.e.w = static_cast<float>(command.shape);
                if (glm::any(glm::lessThanEqual(
                        glm::vec3(command.e), glm::vec3(0.0f)))) {
                    command.e = glm::vec4(
                        throwableShapeDimensions(command.shape),
                        static_cast<float>(command.shape));
                }
                if (nextSlot) {
                    ++nextUnusedIndex_;
                } else {
                    freeIndices_.erase(index);
                }
                hostAlive_[index] = true;
                scheduledSpawnTicks_[index] = command.targetTick;
                scheduledDestroyTicks_[index] = 0u;
                ++residentBodies_;
                highResidentBodies_ = std::max(
                    highResidentBodies_, residentBodies_);
                if (!command.material) {
                    command.material = PhysicsMaterial{
                        .friction = config_.bodyFriction,
                        .restitution = command.shape == ThrowableShape::Sphere
                            ? config_.bodySphereRestitution
                            : config_.bodyOtherRestitution,
                        .rollingResistance = 0.01f,
                        .density = 1.0f,
                        .flags = 0u,
                    };
                }
            } else if (!hostHandleAliveAt(
                           command.body, command.targetTick)) {
                continue;
            }
            if (command.type == PhysicsCommandType::SpawnBody
                || command.type == PhysicsCommandType::Teleport
                || command.type == PhysicsCommandType::SetKinematicTarget) {
                const WorldPosition position = canonicalWorldPosition(
                    command.sector, glm::dvec3(command.a));
                command.sector = position.sector;
                command.a = glm::vec4(position.local, command.a.w);
            }
            if (commands_.size() >= config_.commandCapacity) {
                // The queue is already sorted and coalesced here. Admit a
                // final target without allocating a rollback vector: either a
                // later target in the same uninterrupted pose run already
                // supersedes it, or it replaces the one earlier target that
                // it supersedes. A teleport is a run boundary.
                const auto less =
                    [](const PhysicsCommand& lhs,
                       const PhysicsCommand& rhs) {
                        if (lhs.targetTick != rhs.targetTick) {
                            return lhs.targetTick < rhs.targetTick;
                        }
                        const uint32_t lhsPriority =
                            commandPriority(lhs.type);
                        const uint32_t rhsPriority =
                            commandPriority(rhs.type);
                        if (lhsPriority != rhsPriority) {
                            return lhsPriority < rhsPriority;
                        }
                        return lhs.sequence < rhs.sequence;
                    };
                const size_t insertion = static_cast<size_t>(
                    std::distance(
                        commands_.begin(),
                        std::upper_bound(
                            commands_.begin(), commands_.end(),
                            command, less)));
                constexpr size_t kNoCommand =
                    std::numeric_limits<size_t>::max();
                size_t previousTarget = kNoCommand;
                for (size_t cursor = insertion; cursor > 0u;) {
                    --cursor;
                    const PhysicsCommand& existing =
                        commands_[cursor];
                    if (existing.targetTick
                        != command.targetTick) {
                        break;
                    }
                    if (existing.body.index
                        != command.body.index) {
                        continue;
                    }
                    if (existing.type
                            == PhysicsCommandType::
                                SetKinematicTarget
                        && existing.body.generation
                            != command.body.generation) {
                        break;
                    }
                    if (existing.body.generation
                        != command.body.generation) {
                        continue;
                    }
                    if (existing.type
                        == PhysicsCommandType::
                            SetKinematicTarget) {
                        previousTarget = cursor;
                        break;
                    }
                    if (existing.type
                            == PhysicsCommandType::Teleport
                        || existing.type
                            == PhysicsCommandType::SpawnBody
                        || existing.type
                            == PhysicsCommandType::DestroyBody) {
                        break;
                    }
                }
                bool supersededByLaterTarget = false;
                for (size_t cursor = insertion;
                     cursor < commands_.size(); ++cursor) {
                    const PhysicsCommand& existing =
                        commands_[cursor];
                    if (existing.targetTick
                        != command.targetTick) {
                        break;
                    }
                    if (existing.body.index
                        != command.body.index) {
                        continue;
                    }
                    if (existing.type
                            == PhysicsCommandType::
                                SetKinematicTarget
                        && existing.body.generation
                            != command.body.generation) {
                        break;
                    }
                    if (existing.body.generation
                        != command.body.generation) {
                        continue;
                    }
                    if (existing.type
                        == PhysicsCommandType::
                            SetKinematicTarget) {
                        supersededByLaterTarget = true;
                        break;
                    }
                    if (existing.type
                            == PhysicsCommandType::Teleport
                        || existing.type
                            == PhysicsCommandType::SpawnBody
                        || existing.type
                            == PhysicsCommandType::DestroyBody) {
                        break;
                    }
                }
                if (supersededByLaterTarget) {
                    continue;
                }
                if (previousTarget != kNoCommand) {
                    commands_[previousTarget] =
                        std::move(command);
                    queuedKinematicTarget = true;
                    continue;
                }
                commandCapacityOverflow_ = true;
                LOG_WARN("GPU physics command capacity {} exceeded",
                         config_.commandCapacity);
                continue;
            }
            commands_.push_back(command);
            queuedKinematicTarget = queuedKinematicTarget
                || command.type == PhysicsCommandType::SetKinematicTarget;
        }
        if (queuedKinematicTarget) sortAndCoalesceKinematicTargets();
    }

    void shrinkUnusedAttachmentTail() {
        while (nextUnusedAttachmentIndex_ > 1u) {
            const uint32_t last = nextUnusedAttachmentIndex_ - 1u;
            const auto free = std::lower_bound(
                freeAttachmentIndices_.begin(),
                freeAttachmentIndices_.end(), last);
            if (hostAttachmentAlive_[last]
                || free == freeAttachmentIndices_.end()
                || *free != last) {
                break;
            }
            freeAttachmentIndices_.erase(free);
            --nextUnusedAttachmentIndex_;
        }
    }

    [[nodiscard]] bool hostAttachmentAllocated(
        AttachmentHandle handle) const noexcept {
        return initialized_ && handle.valid()
            && handle.index < attachmentCapacity_
            && hostAttachmentAlive_[handle.index]
            && attachmentGenerations_[handle.index] == handle.generation;
    }

    [[nodiscard]] bool validAttachmentDesc(
        const DistanceAttachmentDesc& desc, uint64_t targetTick) const noexcept {
        return desc.bodyA != desc.bodyB
            && hostHandleAliveAt(desc.bodyA, targetTick)
            && hostHandleAliveAt(desc.bodyB, targetTick)
            && finiteVector(desc.localAnchorA)
            && finiteVector(desc.localAnchorB)
            && std::isfinite(desc.targetLength)
            && std::isfinite(desc.minimumLength)
            && std::isfinite(desc.maximumLength)
            && std::isfinite(desc.motorSpeed)
            && std::isfinite(desc.maximumForce)
            && std::isfinite(desc.breakForce)
            && desc.targetLength >= 0.0f
            && desc.minimumLength >= 0.0f
            && desc.maximumLength >= desc.minimumLength
            && desc.maximumForce > 0.0f
            && desc.breakForce >= 0.0f;
    }

    AttachmentHandle createAttachment(
        const DistanceAttachmentDesc& requested) {
        if (!initialized_ || preparedMutationActive()) return {};
        const uint64_t targetTick = nextMutationTick();
        if (!validAttachmentDesc(requested, targetTick)) {
            LOG_WARN("Discarding invalid GPU distance attachment");
            return {};
        }
        if (attachmentCommands_.size()
            >= config_.attachmentCommandCapacity) {
            attachmentCommandCapacityOverflow_ = true;
            return {};
        }
        uint32_t index = 0u;
        if (!freeAttachmentIndices_.empty()) {
            const auto first = freeAttachmentIndices_.begin();
            index = *first;
            freeAttachmentIndices_.erase(first);
        } else if (nextUnusedAttachmentIndex_ < attachmentCapacity_) {
            index = nextUnusedAttachmentIndex_++;
        }
        if (index == 0u) {
            attachmentCapacityOverflow_ = true;
            return {};
        }

        DistanceAttachmentDesc desc = requested;
        desc.targetLength = std::clamp(
            desc.targetLength, desc.minimumLength, desc.maximumLength);
        const AttachmentHandle handle{
            index, attachmentGenerations_[index]};
        attachmentCommands_.push_back({
            .type = GpuAttachmentCommandType::CreateDistance,
            .attachment = handle,
            .targetTick = targetTick,
            .sequence = assignCommandSequence(),
            .desc = desc,
        });
        hostAttachmentAlive_[index] = true;
        ++residentAttachments_;
        highResidentAttachments_ = std::max(
            highResidentAttachments_, residentAttachments_);
        idleWorldConfirmed_ = false;
        return handle;
    }

    bool destroyAttachment(AttachmentHandle handle) {
        if (preparedMutationActive()) return false;
        if (!hostAttachmentAllocated(handle)) return false;
        const bool pendingCreate = std::any_of(
            attachmentCommands_.begin(), attachmentCommands_.end(),
            [handle](const AttachmentMutation& mutation) {
                return mutation.attachment == handle
                    && mutation.type
                        == GpuAttachmentCommandType::CreateDistance;
            });
        if (pendingCreate) {
            attachmentCommands_.erase(std::remove_if(
                attachmentCommands_.begin(), attachmentCommands_.end(),
                [handle](const AttachmentMutation& mutation) {
                    return mutation.attachment == handle;
                }), attachmentCommands_.end());
        } else {
            if (attachmentCommands_.size()
                >= config_.attachmentCommandCapacity) {
                attachmentCommandCapacityOverflow_ = true;
                return false;
            }
            attachmentCommands_.push_back({
                .type = GpuAttachmentCommandType::Destroy,
                .attachment = handle,
                .targetTick = nextMutationTick(),
                .sequence = assignCommandSequence(),
            });
        }
        hostAttachmentAlive_[handle.index] = false;
        attachmentGenerations_[handle.index] =
            nextBodyGeneration(attachmentGenerations_[handle.index]);
        if (residentAttachments_ != 0u) --residentAttachments_;
        freeAttachmentIndices_.insert(
            std::lower_bound(
                freeAttachmentIndices_.begin(),
                freeAttachmentIndices_.end(), handle.index),
            handle.index);
        shrinkUnusedAttachmentTail();
        return true;
    }

    bool setAttachmentTarget(
        AttachmentHandle handle, float targetLength) {
        if (preparedMutationActive() || !hostAttachmentAllocated(handle)
            || !std::isfinite(targetLength) || targetLength < 0.0f) {
            return false;
        }
        if (attachmentCommands_.size()
            >= config_.attachmentCommandCapacity) {
            attachmentCommandCapacityOverflow_ = true;
            return false;
        }
        attachmentCommands_.push_back({
            .type = GpuAttachmentCommandType::SetTargetLength,
            .attachment = handle,
            .targetTick = nextMutationTick(),
            .sequence = assignCommandSequence(),
            .value = targetLength,
        });
        idleWorldConfirmed_ = false;
        return true;
    }

    bool setAttachmentMotor(
        AttachmentHandle handle, float motorSpeed) {
        if (preparedMutationActive() || !hostAttachmentAllocated(handle)
            || !std::isfinite(motorSpeed)) {
            return false;
        }
        if (attachmentCommands_.size()
            >= config_.attachmentCommandCapacity) {
            attachmentCommandCapacityOverflow_ = true;
            return false;
        }
        attachmentCommands_.push_back({
            .type = GpuAttachmentCommandType::SetMotorSpeed,
            .attachment = handle,
            .targetTick = nextMutationTick(),
            .sequence = assignCommandSequence(),
            .value = motorSpeed,
        });
        idleWorldConfirmed_ = false;
        return true;
    }

    PreparedPhysicsMutation prepareMutationBatch(
        const PhysicsMutationBatch& batch) noexcept {
        PreparedPhysicsMutation result;
        if (!initialized_) {
            result.status = PhysicsMutationStatus::NotInitialized;
            return result;
        }
        result.targetTick = nextMutationTick();
        if (preparedMutationActive()) {
            result.status = PhysicsMutationStatus::PendingMutation;
            return result;
        }
        // The authority path prepares immediately after the preceding tick is
        // closed, then schedules exactly one tick after commit. Admitting a
        // transaction into accumulated clock debt would make its application
        // tick ambiguous.
        if (pendingTicks_ != 0u) {
            result.status = PhysicsMutationStatus::PendingPhysicsTicks;
            return result;
        }
        if (schedulingMode_ == SchedulingMode::Accumulator) {
            result.status =
                PhysicsMutationStatus::IncompatibleSchedulingMode;
            return result;
        }
        if (batch.bodyCommands.empty()
            && batch.attachmentDestroys.empty()
            && batch.attachmentCreates.empty()) {
            result.status = PhysicsMutationStatus::EmptyBatch;
            return result;
        }
        if (commands_.size() > config_.commandCapacity
            || batch.bodyCommands.size()
                > config_.commandCapacity - commands_.size()
            || batch.attachmentDestroys.size()
                > config_.attachmentCommandCapacity
            || batch.attachmentCreates.size()
                > config_.attachmentCommandCapacity) {
            result.status = PhysicsMutationStatus::CapacityExceeded;
            return result;
        }

        {
            if (!preparedMutation_) {
                result.status = PhysicsMutationStatus::NotInitialized;
                return result;
            }
            PreparedMutationState& staged = *preparedMutation_;
            // Preparation is a fixed-tick runtime path. All scratch storage is
            // allocated by initialize(); fail closed if a later refactor ever
            // violates that capacity invariant instead of allocating here.
            if (staged.commands.capacity()
                    < commands_.size() + batch.bodyCommands.size()
                || staged.attachmentCommands.capacity()
                    < attachmentCommands_.size()
                || staged.attachmentGenerations.capacity()
                    < attachmentGenerations_.size()
                || staged.hostAttachmentAlive.capacity()
                    < hostAttachmentAlive_.size()
                || staged.freeAttachmentIndices.capacity()
                    < freeAttachmentIndices_.size()
                || staged.createdAttachments.capacity()
                    < batch.attachmentCreates.size()) {
                result.status = PhysicsMutationStatus::CapacityExceeded;
                return result;
            }
            staged.token = 0u;
            staged.targetTick = result.targetTick;
            staged.bodyCommandCount = 0u;
            staged.destroyedAttachmentCount = 0u;
            staged.createdAttachmentCount = 0u;
            staged.nextSequence = nextSequence_;
            staged.nextUnusedAttachmentIndex =
                nextUnusedAttachmentIndex_;
            staged.residentAttachments = residentAttachments_;
            staged.highResidentAttachments =
                highResidentAttachments_;
            staged.commands = commands_;
            staged.attachmentCommands = attachmentCommands_;
            staged.attachmentGenerations = attachmentGenerations_;
            staged.hostAttachmentAlive = hostAttachmentAlive_;
            staged.freeAttachmentIndices = freeAttachmentIndices_;
            staged.createdAttachments.clear();

            const auto assignStagedSequence =
                [&staged](uint64_t requested = 0u) {
                    if (requested != 0u) {
                        if (requested >= staged.nextSequence) {
                            staged.nextSequence =
                                requested
                                    == std::numeric_limits<uint64_t>::max()
                                ? requested
                                : requested + 1u;
                        }
                        return requested;
                    }
                    const uint64_t assigned = staged.nextSequence;
                    if (staged.nextSequence
                        != std::numeric_limits<uint64_t>::max()) {
                        ++staged.nextSequence;
                    }
                    return assigned;
                };

            for (PhysicsCommand command : batch.bodyCommands) {
                if (static_cast<uint32_t>(command.type)
                        > static_cast<uint32_t>(
                            PhysicsCommandType::ApplyForceAtLocalPoint)
                    || command.type == PhysicsCommandType::SpawnBody
                    || command.type == PhysicsCommandType::DestroyBody
                    || !finiteVector(command.a)
                    || !finiteVector(command.b)
                    || !finiteVector(command.c)
                    || !finiteVector(command.d)
                    || !finiteVector(command.e)
                    || (command.material
                        && !validMaterial(*command.material))
                    || (command.type == PhysicsCommandType::SetMaterial
                        && !command.material)
                    || (command.targetTick != 0u
                        && command.targetTick != result.targetTick)
                    || !hostHandleAliveAt(
                        command.body, result.targetTick)) {
                    result.status = PhysicsMutationStatus::InvalidInput;
                    return result;
                }
                command.targetTick = result.targetTick;
                command.sequence =
                    assignStagedSequence(command.sequence);
                if (command.type == PhysicsCommandType::Teleport
                    || command.type
                        == PhysicsCommandType::SetKinematicTarget) {
                    const WorldPosition position =
                        canonicalWorldPosition(
                            command.sector, glm::dvec3(command.a));
                    command.sector = position.sector;
                    command.a =
                        glm::vec4(position.local, command.a.w);
                }
                staged.commands.push_back(command);
                ++staged.bodyCommandCount;
            }

            const auto stagedAttachmentAllocated =
                [&staged, this](AttachmentHandle handle) {
                    return handle.valid()
                        && handle.index < attachmentCapacity_
                        && staged.hostAttachmentAlive[handle.index]
                        && staged.attachmentGenerations[handle.index]
                            == handle.generation;
                };
            const auto shrinkStagedAttachmentTail =
                [&staged] {
                    while (staged.nextUnusedAttachmentIndex > 1u) {
                        const uint32_t last =
                            staged.nextUnusedAttachmentIndex - 1u;
                        const auto free = std::lower_bound(
                            staged.freeAttachmentIndices.begin(),
                            staged.freeAttachmentIndices.end(), last);
                        if (staged.hostAttachmentAlive[last]
                            || free
                                == staged.freeAttachmentIndices.end()
                            || *free != last) {
                            break;
                        }
                        staged.freeAttachmentIndices.erase(free);
                        --staged.nextUnusedAttachmentIndex;
                    }
                };

            // Destruction is deliberately staged first. A transfer therefore
            // succeeds at full attachment capacity and creates the replacement
            // in the retired slot with the next generation.
            for (const AttachmentHandle handle
                 : batch.attachmentDestroys) {
                if (!stagedAttachmentAllocated(handle)) {
                    result.status =
                        PhysicsMutationStatus::InvalidInput;
                    return result;
                }
                const bool pendingCreate = std::any_of(
                    staged.attachmentCommands.begin(),
                    staged.attachmentCommands.end(),
                    [handle](const AttachmentMutation& mutation) {
                        return mutation.attachment == handle
                            && mutation.type
                                == GpuAttachmentCommandType::
                                    CreateDistance;
                    });
                if (pendingCreate) {
                    staged.attachmentCommands.erase(
                        std::remove_if(
                            staged.attachmentCommands.begin(),
                            staged.attachmentCommands.end(),
                            [handle](
                                const AttachmentMutation& mutation) {
                                return mutation.attachment == handle;
                            }),
                        staged.attachmentCommands.end());
                } else {
                    if (staged.attachmentCommands.size()
                        >= config_.attachmentCommandCapacity) {
                        result.status =
                            PhysicsMutationStatus::CapacityExceeded;
                        return result;
                    }
                    staged.attachmentCommands.push_back({
                        .type = GpuAttachmentCommandType::Destroy,
                        .attachment = handle,
                        .targetTick = result.targetTick,
                        .sequence = assignStagedSequence(),
                    });
                }
                staged.hostAttachmentAlive[handle.index] = false;
                staged.attachmentGenerations[handle.index] =
                    nextBodyGeneration(
                        staged.attachmentGenerations[handle.index]);
                if (staged.residentAttachments != 0u) {
                    --staged.residentAttachments;
                }
                staged.freeAttachmentIndices.insert(
                    std::lower_bound(
                        staged.freeAttachmentIndices.begin(),
                        staged.freeAttachmentIndices.end(),
                        handle.index),
                    handle.index);
                shrinkStagedAttachmentTail();
                ++staged.destroyedAttachmentCount;
            }

            for (const DistanceAttachmentDesc& requested
                 : batch.attachmentCreates) {
                if (!validAttachmentDesc(
                        requested, result.targetTick)) {
                    result.status =
                        PhysicsMutationStatus::InvalidInput;
                    return result;
                }
                if (staged.attachmentCommands.size()
                    >= config_.attachmentCommandCapacity) {
                    result.status =
                        PhysicsMutationStatus::CapacityExceeded;
                    return result;
                }
                uint32_t index = 0u;
                if (!staged.freeAttachmentIndices.empty()) {
                    const auto first =
                        staged.freeAttachmentIndices.begin();
                    index = *first;
                    staged.freeAttachmentIndices.erase(first);
                } else if (staged.nextUnusedAttachmentIndex
                           < attachmentCapacity_) {
                    index = staged.nextUnusedAttachmentIndex++;
                }
                if (index == 0u) {
                    result.status =
                        PhysicsMutationStatus::CapacityExceeded;
                    return result;
                }

                DistanceAttachmentDesc desc = requested;
                desc.targetLength = std::clamp(
                    desc.targetLength, desc.minimumLength,
                    desc.maximumLength);
                const AttachmentHandle handle{
                    index, staged.attachmentGenerations[index]};
                staged.attachmentCommands.push_back({
                    .type =
                        GpuAttachmentCommandType::CreateDistance,
                    .attachment = handle,
                    .targetTick = result.targetTick,
                    .sequence = assignStagedSequence(),
                    .desc = desc,
                });
                staged.hostAttachmentAlive[index] = true;
                ++staged.residentAttachments;
                staged.highResidentAttachments = std::max(
                    staged.highResidentAttachments,
                    staged.residentAttachments);
                staged.createdAttachments.push_back(handle);
                ++staged.createdAttachmentCount;
            }

            const uint64_t token = nextPreparedMutationToken_;
            if (token == 0u) {
                result.status =
                    PhysicsMutationStatus::TokenExhausted;
                return result;
            }
            nextPreparedMutationToken_ =
                token == std::numeric_limits<uint64_t>::max()
                ? 0u : token + 1u;
            staged.token = token;
            result.status = PhysicsMutationStatus::Prepared;
            result.token = token;
            result.createdAttachments = staged.createdAttachments;
            return result;
        }
    }

    PhysicsMutationResult commitPrepared(
        const PreparedPhysicsMutation& prepared) noexcept {
        PhysicsMutationResult result;
        if (!initialized_) {
            result.status = PhysicsMutationStatus::NotInitialized;
            return result;
        }
        if (!prepared.ready() || !preparedMutationActive()
            || prepared.token != preparedMutation_->token
            || prepared.targetTick
                != preparedMutation_->targetTick) {
            result.status = PhysicsMutationStatus::InvalidToken;
            return result;
        }

        PreparedMutationState& staged = *preparedMutation_;
        result.status = PhysicsMutationStatus::Committed;
        result.targetTick = staged.targetTick;
        result.bodyCommandCount = staged.bodyCommandCount;
        result.destroyedAttachmentCount =
            staged.destroyedAttachmentCount;
        result.createdAttachmentCount =
            staged.createdAttachmentCount;

        // Every operation below is a noexcept scalar assignment or container
        // swap. All validation and allocation happened during preparation.
        commands_.swap(staged.commands);
        attachmentCommands_.swap(staged.attachmentCommands);
        attachmentGenerations_.swap(
            staged.attachmentGenerations);
        hostAttachmentAlive_.swap(staged.hostAttachmentAlive);
        freeAttachmentIndices_.swap(
            staged.freeAttachmentIndices);
        nextSequence_ = staged.nextSequence;
        nextUnusedAttachmentIndex_ =
            staged.nextUnusedAttachmentIndex;
        residentAttachments_ = staged.residentAttachments;
        highResidentAttachments_ =
            staged.highResidentAttachments;
        idleWorldConfirmed_ = false;
        staged.token = 0u;
        return result;
    }

    bool discardPrepared(
        const PreparedPhysicsMutation& prepared) noexcept {
        if (!prepared.ready() || !preparedMutationActive()
            || prepared.token != preparedMutation_->token
            || prepared.targetTick
                != preparedMutation_->targetTick) {
            return false;
        }
        preparedMutation_->token = 0u;
        return true;
    }

    void sortAttachmentCommands() noexcept {
        stableSortWithScratch(
            attachmentCommands_, attachmentCommandSortScratch_,
            [](const AttachmentMutation& lhs,
               const AttachmentMutation& rhs) {
                if (lhs.targetTick != rhs.targetTick) {
                    return lhs.targetTick < rhs.targetTick;
                }
                if (lhs.type != rhs.type) {
                    return static_cast<uint32_t>(lhs.type)
                        < static_cast<uint32_t>(rhs.type);
                }
                return lhs.sequence < rhs.sequence;
            });
    }

    uint64_t nextMutationTick() const noexcept {
        return encodedTick_ + pendingTicks_ + 1u;
    }

    uint64_t encodedTick() const noexcept { return encodedTick_; }

    void pollTelemetry() {
        if (!config_.enableTelemetryReadback) return;
        auto raw = telemetryReadback_.poll();
        if (!raw) return;
        if (raw->bytes.size() != kTelemetrySnapshotBytes) {
            LOG_WARN("Discarding malformed GPU physics telemetry packet");
            return;
        }
        std::array<uint32_t, kTelemetrySnapshotWordCount> words{};
        std::memcpy(words.data(), raw->bytes.data(), raw->bytes.size());
        const std::span<const uint32_t> view(words);
        cachedTelemetry_.valid = true;
        cachedTelemetry_.tick = raw->tick;
        cachedTelemetry_.core = decodeCoreTelemetry(view.subspan(
            kCoreTelemetryOffset, kCoreTelemetryWordCount));
        cachedTelemetry_.ccd = GpuCcd::decodeTelemetry(view.subspan(
            kCcdTelemetryOffset, GpuCcd::kTelemetryWordCount));
        cachedTelemetry_.broad = GpuBroadPhase::decodeTelemetry(view.subspan(
            kBroadTelemetryOffset, GpuBroadPhase::kTelemetryWordCount));
        broadPhase_.updateMediumPairPath(
            cachedTelemetry_.broad.gridEntries,
            cachedTelemetry_.broad.occupiedCells,
            cachedTelemetry_.broad.pairDrivingBodies,
            cachedTelemetry_.broad.maximumCellBodies);
        cachedTelemetry_.narrow = GpuNarrowPhase::decodeTelemetry(
            view.subspan(kNarrowTelemetryOffset,
                         GpuNarrowPhase::kTelemetryWordCount),
            view.subspan(kNarrowCollisionPairClassOffset,
                         kGpuNarrowPhasePairClassCount));
        cachedTelemetry_.solver = GpuDynamicSolver::decodeTelemetry(view.subspan(
            kSolverTelemetryOffset, GpuDynamicSolver::kTelemetryWordCount));
        dynamicSolver_.updateColorRoundLimit(
            cachedTelemetry_.solver.maximumBodyDegree,
            cachedTelemetry_.solver.overflowContacts);
        cachedTelemetry_.islands = GpuIslandManager::decodeTelemetry(view.subspan(
            kIslandTelemetryOffset, GpuIslandManager::kTelemetryWordCount));
        cachedTelemetry_.attachments =
            GpuAttachmentSolver::decodeTelemetry(view.subspan(
                kAttachmentTelemetryOffset,
                GpuAttachmentSolver::kTelemetryWordCount));
        if (raw->tick >= lastMutationTick_) {
            idleWorldConfirmed_ =
                cachedTelemetry_.core.activeBodies == 0u
                && cachedTelemetry_.core.kinematicBodies == 0u
                && cachedTelemetry_.broad.pairDrivingBodies == 0u;
        }
    }

    void schedule(float deltaTime) {
        const auto start = std::chrono::steady_clock::now();
        pollTelemetry();
        if (!initialized_ || preparedMutationActive()
            || schedulingMode_ == SchedulingMode::FixedTicks
            || !std::isfinite(deltaTime) || deltaTime <= 0.0f) {
            lastStepStats_ = {};
            return;
        }
        schedulingMode_ = SchedulingMode::Accumulator;
        // The encoded batch stays strictly capped, but retain enough clock
        // debt to bridge Chrome's coarse queue-completion callbacks. Recovery
        // happens over later submissions; no submission can exceed the
        // configured maximumCatchUpTicks.
        const uint32_t accumulatorCatchUpTicks =
            std::max(config_.maximumCatchUpTicks, 4u);
        const double maximumDelta = double{config_.fixedTickSeconds}
                                  * accumulatorCatchUpTicks;
        const double fixedTick = static_cast<double>(config_.fixedTickSeconds);
        // A minimized or occluded application may keep stepping the CPU side
        // while no command encoder is submitted. Bound pending ticks and their
        // companion accumulator as one debt; otherwise every such frame adds
        // another catch-up batch and restoring the window can spend hundreds
        // of frames behind real time.
        const double pendingDebt = fixedTick * pendingTicks_;
        const double availableDebt = std::max(maximumDelta - pendingDebt, 0.0);
        accumulator_ = std::min(
            accumulator_ + std::min(
                static_cast<double>(deltaTime), maximumDelta),
            availableDebt);
        uint32_t scheduled = 0;
        while (accumulator_ + 1e-12 >= fixedTick
               && pendingTicks_ < config_.maximumCatchUpTicks) {
            accumulator_ -= fixedTick;
            ++pendingTicks_;
            ++scheduled;
        }
        const auto elapsed = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - start).count();
        lastStepStats_ = {
            .totalMs = elapsed,
            .simulationMs = elapsed,
            .substepCount = scheduled * config_.substeps,
        };
    }

    bool scheduleFixedTicks(uint32_t tickCount) noexcept {
        if (!initialized_ || preparedMutationActive()
            || schedulingMode_ == SchedulingMode::Accumulator
            || tickCount == 0u
            || tickCount > config_.maximumCatchUpTicks
            || pendingTicks_
                > config_.maximumCatchUpTicks - tickCount
            || encodedTick_
                > std::numeric_limits<uint64_t>::max()
                    - pendingTicks_ - tickCount) {
            return false;
        }
        schedulingMode_ = SchedulingMode::FixedTicks;
        pendingTicks_ += tickCount;
        lastStepStats_ = {
            .substepCount = tickCount * config_.substeps,
        };
        return true;
    }

    bool submitQueries(std::span<const PhysicsQueryRequest> requests,
                       uint64_t resultTick) {
        if (!initialized_ || queryPending_ || requests.empty()) return false;
        std::vector<GpuQueryRequest> packed;
        packed.reserve(requests.size());
        const auto finiteVector = [](const glm::vec3& value) {
            return std::isfinite(value.x) && std::isfinite(value.y)
                && std::isfinite(value.z);
        };
        for (const PhysicsQueryRequest& request : requests) {
            const uint32_t type = static_cast<uint32_t>(request.type);
            const bool finite = finiteVector(request.origin)
                && finiteVector(request.direction)
                && finiteVector(request.capsuleAxis)
                && std::isfinite(request.radius)
                && std::isfinite(request.maximumDistance)
                && std::isfinite(request.capsuleHalfHeight);
            if (!finite || type > static_cast<uint32_t>(
                    PhysicsQueryType::CapsuleCast)
                || request.maximumHits == 0 || request.radius < 0.0f
                || request.maximumDistance < 0.0f
                || request.capsuleHalfHeight < 0.0f
                || (request.flags & ~kPhysicsQueryKnownFlags) != 0u
                || ((request.flags & PhysicsQueryExcludeStatic) != 0u
                    && (request.flags & PhysicsQueryExcludeDynamic) != 0u)
                || ((request.flags & PhysicsQueryExcludeSleeping) != 0u
                    && (request.flags & PhysicsQueryExcludeAwake) != 0u)
                || (request.type != PhysicsQueryType::OverlapSphere
                    && glm::dot(request.direction, request.direction)
                        <= 1e-12f)
                || (request.type == PhysicsQueryType::CapsuleCast
                    && glm::dot(request.capsuleAxis, request.capsuleAxis)
                        <= 1e-12f)) {
                return false;
            }
            const WorldPosition queryOrigin = canonicalWorldPosition(
                request.sector, glm::dvec3(request.origin));
            double reach = static_cast<double>(request.radius);
            if (request.type != PhysicsQueryType::OverlapSphere) {
                reach += static_cast<double>(request.maximumDistance);
            }
            if (request.type == PhysicsQueryType::CapsuleCast) {
                reach += static_cast<double>(request.capsuleHalfHeight);
            }
            const double sectorReach = std::ceil(
                reach / static_cast<double>(kWorldSectorSize)) + 2.0;
            if (sectorReach
                > static_cast<double>(kGpuQueryMaximumSectorDelta)) {
                return false;
            }
            const uint32_t maximumSectorDelta = std::max(
                1u, static_cast<uint32_t>(sectorReach));
            GpuQueryRequest gpuRequest;
            gpuRequest.ids = {
                request.requestId,
                type,
                std::min(request.maximumHits, kGpuQueryMaximumHits),
                request.flags,
            };
            gpuRequest.originRadius = {
                queryOrigin.local.x, queryOrigin.local.y,
                queryOrigin.local.z,
                request.radius,
            };
            gpuRequest.directionDistance = {
                request.direction.x, request.direction.y, request.direction.z,
                request.maximumDistance,
            };
            gpuRequest.dimensions = {
                request.capsuleAxis.x, request.capsuleAxis.y,
                request.capsuleAxis.z, request.capsuleHalfHeight,
            };
            gpuRequest.sector = {
                queryOrigin.sector.x, queryOrigin.sector.y,
                queryOrigin.sector.z,
                static_cast<int32_t>(maximumSectorDelta),
            };
            packed.push_back(gpuRequest);
        }
        if (resultTick == 0) resultTick = encodedTick_ + pendingTicks_;
        if (!querySystem_.submit(packed, resultTick)) return false;
        queryPending_ = true;
        pendingQueryCount_ = static_cast<uint32_t>(packed.size());
        return true;
    }

    std::optional<PhysicsQueryBatch> pollQueryResults() {
        auto batch = querySystem_.poll();
        if (!batch) return std::nullopt;
        PhysicsQueryBatch result;
        result.tick = batch->tick;
        result.outputs.reserve(batch->outputs.size());
        for (const GpuQueryOutput& source : batch->outputs) {
            PhysicsQueryOutput output;
            output.requestId = source.header[0];
            output.type = static_cast<PhysicsQueryType>(std::min(
                source.header[3],
                static_cast<uint32_t>(PhysicsQueryType::CapsuleCast)));
            output.overflow = source.header[2] != 0u;
            const uint32_t hitCount = std::min(
                source.header[1], kGpuQueryMaximumHits);
            output.hits.reserve(hitCount);
            for (uint32_t index = 0; index < hitCount; ++index) {
                const GpuQueryHit& sourceHit = source.hits[index];
                PhysicsQueryHit hit;
                hit.requestId = sourceHit.ids[0];
                hit.bodyIndex = sourceHit.ids[1];
                hit.bodyGeneration = static_cast<uint32_t>(
                    sourceHit.sector[3]);
                hit.featureId = sourceHit.ids[2];
                hit.type = static_cast<PhysicsQueryType>(std::min(
                    sourceHit.ids[3],
                    static_cast<uint32_t>(PhysicsQueryType::CapsuleCast)));
                hit.fraction = sourceHit.metricDistance[0];
                hit.distance = sourceHit.metricDistance[1];
                const WorldPosition point = canonicalWorldPosition(
                    glm::ivec3(sourceHit.sector[0], sourceHit.sector[1],
                               sourceHit.sector[2]),
                    glm::dvec3(sourceHit.point[0], sourceHit.point[1],
                               sourceHit.point[2]));
                hit.sector = point.sector;
                hit.point = point.local;
                hit.normal = {sourceHit.normal[0], sourceHit.normal[1],
                              sourceHit.normal[2]};
                output.hits.push_back(hit);
            }
            result.outputs.push_back(std::move(output));
        }
        return result;
    }

    bool setEventReadbackEnabled(bool enabled) noexcept {
        eventReadbackEnabled_ = false;
        if (!initialized_) return false;
        if (!enabled) return true;
        eventReadbackEnabled_ = initializeEventReadback();
        return eventReadbackEnabled_;
    }

    std::optional<PhysicsEventBatch> pollEvents() {
        auto batch = eventReadback_.poll();
        if (!batch) return std::nullopt;
        PhysicsEventBatch result;
        result.tick = batch->tick;
        result.overflow = batch->overflow;
        result.events.reserve(batch->events.size());
        for (const GpuPhysicsEvent& source : batch->events) {
            PhysicsEvent event;
            event.tick = batch->tick;
            event.type = static_cast<PhysicsEventType>(std::clamp(
                source.header[1],
                static_cast<uint32_t>(PhysicsEventType::ContactBegin),
                static_cast<uint32_t>(
                    PhysicsEventType::AttachmentBreak)));
            const bool contactEvent =
                event.type == PhysicsEventType::ContactBegin
                || event.type == PhysicsEventType::ContactEnd
                || event.type == PhysicsEventType::ContactHit;
            const bool sourceIsSorted =
                source.header[2] <= source.header[3];
            if (contactEvent) {
                event.bodyA = sourceIsSorted
                    ? source.header[2] : source.header[3];
                event.bodyB = sourceIsSorted
                    ? source.header[3] : source.header[2];
                event.bodyGenerationA = sourceIsSorted
                    ? source.identity[0] : source.identity[1];
                event.bodyGenerationB = sourceIsSorted
                    ? source.identity[1] : source.identity[0];
            } else {
                event.bodyA = source.header[2];
                event.bodyB = source.header[3];
                event.bodyGenerationA = source.identity[0];
                event.bodyGenerationB = source.identity[1];
            }
            if (event.type == PhysicsEventType::ContactHit) {
                event.featureId = sourceIsSorted
                    ? source.detail[0] : source.detail[3];
                event.otherFeatureId = sourceIsSorted
                    ? source.detail[3] : source.detail[0];
            } else {
                event.featureId = source.detail[0];
            }
            event.sourceId = source.detail[1];
            event.auxiliaryCount = source.detail[2];
            event.flags =
                event.type == PhysicsEventType::ContactHit
                    ? 0u : source.detail[3];
            if (event.type == PhysicsEventType::ContactHit) {
                const glm::vec3 sourceAnchorA{
                    source.localAnchorASeparation[0],
                    source.localAnchorASeparation[1],
                    source.localAnchorASeparation[2],
                };
                const glm::vec3 sourceAnchorB{
                    source.localAnchorBImpulse[0],
                    source.localAnchorBImpulse[1],
                    source.localAnchorBImpulse[2],
                };
                const glm::vec3 sourceNormal{
                    source.normalSpeed[0],
                    source.normalSpeed[1],
                    source.normalSpeed[2],
                };
                event.localAnchorA = sourceIsSorted
                    ? sourceAnchorA : sourceAnchorB;
                event.localAnchorB = sourceIsSorted
                    ? sourceAnchorB : sourceAnchorA;
                event.normalAtoB = sourceIsSorted
                    ? sourceNormal : -sourceNormal;
                event.impulse = source.localAnchorBImpulse[3];
                event.impactSpeed = source.normalSpeed[3];
            } else if (
                event.type == PhysicsEventType::AttachmentBreak) {
                event.impulse = std::bit_cast<float>(source.detail[2]);
                event.force = std::bit_cast<float>(source.detail[3]);
            }
            result.events.push_back(event);
        }
        std::stable_sort(
            result.events.begin(), result.events.end(),
            [](const PhysicsEvent& lhs, const PhysicsEvent& rhs) {
                return std::tuple{
                    static_cast<uint32_t>(lhs.type),
                    lhs.bodyA, lhs.bodyGenerationA,
                    lhs.bodyB, lhs.bodyGenerationB,
                    lhs.sourceId, lhs.featureId,
                    lhs.otherFeatureId}
                    < std::tuple{
                    static_cast<uint32_t>(rhs.type),
                    rhs.bodyA, rhs.bodyGenerationA,
                    rhs.bodyB, rhs.bodyGenerationB,
                    rhs.sourceId, rhs.featureId,
                    rhs.otherFeatureId};
            });
        return result;
    }

    std::optional<PhysicsGpuStageTiming> pollGpuStageTimings() {
        if (!stageTimingResults_.empty()) {
            PhysicsGpuStageTiming result = stageTimingResults_.front();
            stageTimingResults_.pop_front();
            return result;
        }
        auto raw = stageReadback_.poll();
        if (!raw) return std::nullopt;
        const uint32_t tickCount = raw->firstBody;
        const uint32_t boundaryCount = raw->bodyCount;
        const size_t expectedBytes = size_t{tickCount} * boundaryCount
                                   * sizeof(uint64_t);
        if (tickCount == 0u || boundaryCount != kStagePacketWordCount
            || raw->bytes.size() != expectedBytes) {
            LOG_WARN("Discarding malformed GPU stage timing packet");
            return std::nullopt;
        }
        std::vector<uint64_t> timestamps(size_t{tickCount} * boundaryCount);
        std::memcpy(timestamps.data(), raw->bytes.data(), expectedBytes);
        const double timestampPeriod =
            config_.stageProfilingTimestampPeriodNanoseconds;
        const double tickToMilliseconds = timestampPeriod * 1.0e-6;
        for (uint32_t tick = 0u; tick < tickCount; ++tick) {
            PhysicsGpuStageTiming timing;
            timing.tick = raw->tick + tick;
            timing.timestampPeriodNanoseconds = timestampPeriod;
            const uint64_t* boundaries = timestamps.data()
                + size_t{tick} * boundaryCount;
            for (size_t stage = 0; stage < kPhysicsGpuStageCount; ++stage) {
                const uint64_t start = boundaries[
                    stage * kStageQueryWordsPerBoundary];
                const uint64_t end = boundaries[
                    (stage + 1u) * kStageQueryWordsPerBoundary];
                const uint64_t ticks = end >= start ? end - start : 0u;
                timing.timestampTicks[stage] = ticks;
                timing.milliseconds[stage] =
                    static_cast<double>(ticks) * tickToMilliseconds;
            }
            stageTimingResults_.push_back(timing);
        }
        if (stageTimingResults_.empty()) return std::nullopt;
        PhysicsGpuStageTiming result = stageTimingResults_.front();
        stageTimingResults_.pop_front();
        return result;
    }

    PhysicsEncodeReport encode(
        WGPUCommandEncoder encoder, bool requireClosedReadbacks) {
        PhysicsEncodeReport report;
        if (!initialized_) {
            report.status = PhysicsEncodeStatus::NotInitialized;
            return report;
        }
        if (preparedMutationActive()) {
            report.status =
                PhysicsEncodeStatus::PreparedMutationPending;
            return report;
        }
        if (!encoder) {
            report.status = PhysicsEncodeStatus::InvalidEncoder;
            return report;
        }
        lastGpuUploadBytes_ = 0u;
        lastGpuReadbackBytes_ = 0u;
        const uint64_t finalTick = encodedTick_ + pendingTicks_;
        report.firstTick =
            pendingTicks_ != 0u ? encodedTick_ + 1u : encodedTick_;
        report.finalTick = finalTick;
        report.tickCount = pendingTicks_;
        const auto completeReport =
            [&](PhysicsEncodeStatus status, bool failStopped = false) {
                report.status = status;
                report.gpuUploadBytes = lastGpuUploadBytes_;
                report.gpuReadbackBytes = lastGpuReadbackBytes_;
                report.failStopped = failStopped;
                return report;
            };
        const auto failStop =
            [&](PhysicsEncodeStatus status) {
                initialized_ = false;
                return completeReport(status, true);
            };
        sortAndCoalesceKinematicTargets();
        sortAttachmentCommands();

        std::vector<GpuCommand>& upload = commandUpload_;
        upload.clear();
        for (size_t commandIndex = 0u; commandIndex < commands_.size();
             ++commandIndex) {
            const auto& command = commands_[commandIndex];
            if (command.targetTick > finalTick) continue;
            if (upload.size() >= config_.commandCapacity) break;
            GpuCommand gpuCommand;
            gpuCommand.header = glm::uvec4(
                static_cast<uint32_t>(command.type), command.body.index,
                command.body.generation & kGpuBodyGenerationMask,
                static_cast<uint32_t>(command.targetTick));
            gpuCommand.p0 = command.a;
            gpuCommand.p1 = command.b;
            gpuCommand.p2 = command.c;
            gpuCommand.p3 = command.d;
            gpuCommand.p4 = command.e;
            gpuCommand.p5 = glm::ivec4(
                command.sector,
                command.material
                    ? std::bit_cast<int32_t>(command.material->flags) : 0);
            if (command.material) {
                gpuCommand.p6 = materialCoefficients(*command.material);
            }
            upload.push_back(gpuCommand);
        }
        if (!upload.empty()) {
            if (!gpu::writeBuffer(
                    queue_, commandBuffer_, 0,
                    std::as_bytes(std::span<const GpuCommand>(upload)))) {
                LOG_ERROR("Failed to upload GPU physics commands");
                return failStop(PhysicsEncodeStatus::UploadFailed);
            }
            lastGpuUploadBytes_ +=
                uint64_t{upload.size()} * sizeof(GpuCommand);
            // Telemetry from before this mutation cannot prove that the
            // resulting world is idle. A readback tagged at or after this
            // batch will re-arm the idle path when appropriate.
            lastMutationTick_ = finalTick;
            idleWorldConfirmed_ = false;
        }

        std::vector<GpuAttachmentCommand>& attachmentUpload =
            attachmentUpload_;
        attachmentUpload.clear();
        for (const AttachmentMutation& mutation : attachmentCommands_) {
            if (mutation.targetTick > finalTick) continue;
            if (attachmentUpload.size()
                >= config_.attachmentCommandCapacity) {
                break;
            }
            GpuAttachmentCommand command;
            command.header = {
                static_cast<uint32_t>(mutation.type),
                mutation.attachment.index,
                mutation.attachment.generation & kGpuBodyGenerationMask,
                static_cast<uint32_t>(mutation.targetTick),
            };
            if (mutation.type
                == GpuAttachmentCommandType::CreateDistance) {
                command.bodies = {
                    mutation.desc.bodyA.index,
                    mutation.desc.bodyA.generation
                        & kGpuBodyGenerationMask,
                    mutation.desc.bodyB.index,
                    mutation.desc.bodyB.generation
                        & kGpuBodyGenerationMask,
                };
                command.anchorATarget = {
                    mutation.desc.localAnchorA.x,
                    mutation.desc.localAnchorA.y,
                    mutation.desc.localAnchorA.z,
                    mutation.desc.targetLength,
                };
                command.anchorBMotor = {
                    mutation.desc.localAnchorB.x,
                    mutation.desc.localAnchorB.y,
                    mutation.desc.localAnchorB.z,
                    mutation.desc.motorSpeed,
                };
                command.limits = {
                    mutation.desc.minimumLength,
                    mutation.desc.maximumLength,
                    mutation.desc.maximumForce,
                    mutation.desc.breakForce,
                };
            } else if (mutation.type
                       == GpuAttachmentCommandType::SetTargetLength) {
                command.anchorATarget[3] = mutation.value;
            } else if (mutation.type
                       == GpuAttachmentCommandType::SetMotorSpeed) {
                command.anchorBMotor[3] = mutation.value;
            }
            attachmentUpload.push_back(command);
        }
        if (!attachmentSolver_.uploadCommands(attachmentUpload)) {
            LOG_ERROR("Failed to upload GPU attachment commands");
            return failStop(PhysicsEncodeStatus::UploadFailed);
        }
        if (!attachmentUpload.empty()) {
            lastGpuUploadBytes_ += uint64_t{attachmentUpload.size()}
                * sizeof(GpuAttachmentCommand);
            lastMutationTick_ = finalTick;
            idleWorldConfirmed_ = false;
        }

        uint32_t executionBodies = executionBodyCount();
        for (const GpuCommand& command : upload) {
            executionBodies = std::max(
                executionBodies,
                std::min(command.header.y + 1u, bodyCapacity_));
        }
        const uint32_t executionBlocks =
            (executionBodies + kWorkgroupSize - 1u) / kWorkgroupSize;
        const bool executeAttachmentPipeline =
            residentAttachments_ != 0u || !attachmentUpload.empty();
        const bool executeBodyPipeline = residentBodies_ != 0u
            || !upload.empty() || !pendingFrees_.empty()
            || executeAttachmentPipeline;
        // A telemetry-confirmed sleeping world has no state to integrate or
        // contacts to discover. Keep the authoritative GPU tick moving while
        // avoiding dozens of empty passes. Any mutation or observer that
        // needs current body data takes the complete pipeline below.
        const bool idleOnlyBatch = pendingTicks_ != 0u
            && idleWorldConfirmed_
            && commands_.empty()
            && attachmentCommands_.empty()
            && residentAttachments_ == 0u
            && pendingFrees_.empty()
            && !queryPending_
            && !debugRequest_.has_value()
            && !terrainStateNeedsClear_;
        if (idleOnlyBatch) {
            // No GPU-visible state can change while the world is confirmed
            // asleep. Advance the host clock without submitting even a
            // one-workgroup pass: on browser WebGPU, any write-capable physics
            // dispatch serializes the directly rendered body buffers. The GPU
            // counter is restored before the next real physics tick.
            encodedTick_ = finalTick;
            pendingTicks_ = 0u;
            gpuTickSynchronized_ = false;
            return completeReport(PhysicsEncodeStatus::Encoded);
        }
        if (pendingTicks_ != 0u && !gpuTickSynchronized_) {
            const uint32_t gpuTick = static_cast<uint32_t>(encodedTick_);
            if (!gpu::writeBuffer(
                    queue_, countersBuffer_, sizeof(uint32_t), gpuTick)) {
                LOG_ERROR("Failed to synchronize the GPU physics tick");
                return failStop(PhysicsEncodeStatus::UploadFailed);
            }
            lastGpuUploadBytes_ += sizeof(gpuTick);
            gpuTickSynchronized_ = true;
        }
        refreshExecutionInputs(executionBodies);

        SimulationUniforms uniforms;
        uniforms.gravityAndDt = glm::vec4(config_.gravity,
                                           config_.fixedTickSeconds);
        uniforms.dampingAndClamps = glm::vec4(
            config_.linearDamping, config_.angularDamping,
            config_.maximumLinearSpeed, config_.maximumAngularSpeed);
        uniforms.counts = glm::uvec4(
            executionBodies, static_cast<uint32_t>(upload.size()),
            executionBlocks, config_.substeps);
        if (debugRequest_) {
            uniforms.debugRange = glm::uvec4(
                debugRequest_->firstBody, debugRequest_->bodyCount, 0u, 0u);
        }
        uniforms.debugRange.z = std::min(activeCapacity_, executionBodies);
        const glm::vec2 terrainOrigin = terrain_topology::centeredOrigin(
            terrainWidth_, terrainHeight_, terrainCellScale_);
        uniforms.terrainOriginCellHeight = glm::vec4(
            terrainOrigin, terrainCellScale_, terrainHeightScale_);
        uniforms.terrainSizeMipFlags = glm::uvec4(
            terrainWidth_, terrainHeight_, terrainMipLevelCount_,
            terrainAttached_ ? (legoTerrain_ ? 2u : 1u) : 0u);
        uniforms.water = glm::vec4(
            waterHeight_, waterEnabled_ ? 1.0f : 0.0f,
            config_.waterBuoyancy, config_.waterLinearDrag);
        uniforms.contact = glm::vec4(
            config_.waterAngularDrag, config_.terrainFriction,
            config_.terrainSphereRestitution, config_.linearSlop);
        uniforms.solver = glm::vec4(
            config_.speculativeDistance, 0.20f, 0.05f, 0.50f);
        uniforms.terrainMaterials = glm::vec4(
            config_.terrainFriction, config_.terrainSphereRestitution,
            config_.terrainOtherRestitution, 0.0f);
        uniforms.bodyMaterials = glm::vec4(
            config_.bodyFriction, config_.bodySphereRestitution,
            config_.bodyOtherRestitution, 0.01f);
        uniforms.waterSurface = glm::vec4(
            externalWaterView_ ? waterSurfaceStrength_ : 0.0f,
            static_cast<float>(std::fmod(
                static_cast<double>(finalTick) *
                    static_cast<double>(config_.fixedTickSeconds),
                4096.0)),
            waterPatchLengths_.x, waterPatchLengths_.y);
        uniforms.worldSector = glm::ivec4(0);
        if (!gpu::writeBuffer(queue_, uniformBuffer_, 0, uniforms)) {
            LOG_ERROR("Failed to upload GPU physics uniforms");
            return failStop(PhysicsEncodeStatus::UploadFailed);
        }
        lastGpuUploadBytes_ += sizeof(SimulationUniforms);

        WGPUComputePassDescriptor passDesc{};
        WGPU_SET_LABEL(passDesc, "physics_dynamic_world_step");
        const uint32_t profileTickCount = pendingTicks_;
        const uint64_t profileFirstTick = encodedTick_ + 1u;
        const auto intervalElapsed = [](uint64_t currentTick,
                                        uint64_t previousTick,
                                        uint32_t intervalTicks) {
            const uint64_t interval = std::max(intervalTicks, 1u);
            return previousTick == 0u || currentTick < previousTick
                || currentTick - previousTick >= interval;
        };
        // Body mutations are exactly when an interactive diagnostic sample is
        // most useful. Do not make an overloaded world wait for the cadence.
        const bool forceDiagnosticsSample =
            !upload.empty() || !attachmentUpload.empty();
        const bool profileThisBatch = stageProfilingEnabled_
            && !idleOnlyBatch
            && (forceDiagnosticsSample
                || intervalElapsed(finalTick, lastStageProfileTick_,
                                   config_.stageProfilingIntervalTicks))
            && uint64_t{profileTickCount} * kStagePacketWordCount
                <= stageQueryCapacity_;
        uint32_t profileQueryCount = 0u;
        bool timestampEncodingFailed = false;
        const auto writeStageTimestamp = [&] {
            if (!profileThisBatch || timestampEncodingFailed) return;
            gpu::CompatPassTimestampWrites writes{};
            writes.querySet = stageQuerySet_;
            writes.beginningOfPassWriteIndex = profileQueryCount;
#if defined(VOXY_WASM)
            writes.endOfPassWriteIndex = profileQueryCount + 1u;
#else
            writes.endOfPassWriteIndex = WGPU_QUERY_SET_INDEX_UNDEFINED;
#endif
            WGPUComputePassDescriptor timestampPassDesc{};
            WGPU_SET_LABEL(timestampPassDesc, "physics_stage_boundary");
            timestampPassDesc.timestampWrites = &writes;
            WGPUComputePassEncoder timestampPass =
                wgpuCommandEncoderBeginComputePass(
                    encoder, &timestampPassDesc);
            if (!timestampPass) {
                timestampEncodingFailed = true;
                return;
            }
            wgpuComputePassEncoderEnd(timestampPass);
            wgpuComputePassEncoderRelease(timestampPass);
#if defined(VOXY_WASM)
            profileQueryCount += 2u;
#else
            ++profileQueryCount;
#endif
        };
        bool batchSucceeded = true;
        PhysicsEncodeStatus batchFailure =
            PhysicsEncodeStatus::PipelineFailed;
        for (uint32_t tick = 0; tick < pendingTicks_; ++tick) {
            writeStageTimestamp();
            if (previousPoseBuffer_) {
                // Keep the last two fixed ticks on the GPU. Copy only the
                // reachable body prefix, before this tick applies mutations.
                wgpuCommandEncoderCopyBufferToBuffer(
                    encoder, poseBuffer_, 0u, previousPoseBuffer_, 0u,
                    uint64_t{executionBodies} * sizeof(GpuPose));
                wgpuCommandEncoderCopyBufferToBuffer(
                    encoder, metadataBuffer_, 0u, previousMetadataBuffer_, 0u,
                    uint64_t{executionBodies} * sizeof(glm::uvec4));
            }
            WGPUComputePassEncoder pass =
                wgpuCommandEncoderBeginComputePass(encoder, &passDesc);
            if (!pass) {
                batchSucceeded = false;
                break;
            }
            wgpuComputePassEncoderSetPipeline(pass, applyCommandsPipeline_);
            wgpuComputePassEncoderSetBindGroup(
                pass, 0, commandBindGroup_, 0, nullptr);
            wgpuComputePassEncoderDispatchWorkgroups(pass, 1, 1, 1);

            if (executeBodyPipeline) {
                wgpuComputePassEncoderSetPipeline(pass, compactBlocksPipeline_);
                wgpuComputePassEncoderSetBindGroup(
                    pass, 0, compactBindGroup_, 0, nullptr);
                wgpuComputePassEncoderDispatchWorkgroups(
                    pass, executionBlocks, 1, 1);
                wgpuComputePassEncoderSetPipeline(pass, scanBlocksPipeline_);
                wgpuComputePassEncoderDispatchWorkgroups(pass, 1, 1, 1);
                wgpuComputePassEncoderSetPipeline(pass, scatterActivePipeline_);
                wgpuComputePassEncoderDispatchWorkgroups(
                    pass, executionBlocks, 1, 1);
            }
            wgpuComputePassEncoderEnd(pass);
            wgpuComputePassEncoderRelease(pass);
            writeStageTimestamp();

            if (executeBodyPipeline && terrainAttached_ && !ccd_.encode(
                    encoder, config_.fixedTickSeconds)) {
                LOG_ERROR("Failed to encode GPU CCD pass");
                batchSucceeded = false;
                break;
            }
            writeStageTimestamp();

            if (executeBodyPipeline) {
                pass = wgpuCommandEncoderBeginComputePass(encoder, &passDesc);
                if (!pass) {
                    batchSucceeded = false;
                    break;
                }
                wgpuComputePassEncoderSetPipeline(pass, preparePipeline_);
                wgpuComputePassEncoderSetBindGroup(
                    pass, 0, integrateBindGroup_, 0, nullptr);
                wgpuComputePassEncoderDispatchWorkgroups(
                    pass, executionBlocks, 1, 1);
                wgpuComputePassEncoderEnd(pass);
                wgpuComputePassEncoderRelease(pass);
            }
            if (executeAttachmentPipeline
                && !attachmentSolver_.encode(encoder)) {
                LOG_ERROR("Failed to encode GPU distance attachments");
                batchSucceeded = false;
                break;
            }
            writeStageTimestamp();

            const bool dynamicContactsEnabled =
                config_.enableBodyBodyContacts;
            GpuBroadPhase::ProfilingBoundary broadProfilingBoundary;
            if (profileThisBatch) {
                broadProfilingBoundary.callback = [](const void* userData) {
                    (*static_cast<
                        const decltype(writeStageTimestamp)*>(userData))();
                };
                broadProfilingBoundary.userData = &writeStageTimestamp;
            }
            bool broadPhaseEncoded = true;
            if (executeBodyPipeline && dynamicContactsEnabled) {
                broadPhaseEncoded = broadPhase_.encode(
                    encoder, broadProfilingBoundary);
            } else {
                for (uint32_t boundary = 0u;
                     boundary
                        < GpuBroadPhase::kProfilingInternalBoundaryCount;
                     ++boundary) {
                    writeStageTimestamp();
                }
            }
            writeStageTimestamp();
            GpuNarrowPhase::ProfilingBoundary narrowProfilingBoundary;
            if (profileThisBatch) {
                narrowProfilingBoundary.callback = [](const void* userData) {
                    (*static_cast<
                        const decltype(writeStageTimestamp)*>(userData))();
                };
                narrowProfilingBoundary.userData = &writeStageTimestamp;
            }
            const bool narrowPhaseEnabled = executeBodyPipeline
                && dynamicContactsEnabled;
            const bool narrowPhaseEncoded = broadPhaseEncoded
                && (!narrowPhaseEnabled
                    || narrowPhase_.encode(
                        encoder, narrowProfilingBoundary));
            if (executeBodyPipeline && dynamicContactsEnabled
                && !narrowPhaseEncoded) {
                LOG_ERROR("Failed to encode a GPU broad/narrow-phase stage");
                batchSucceeded = false;
                break;
            }
            if (!narrowPhaseEnabled) {
                for (uint32_t boundary = 0u;
                     boundary
                        < GpuNarrowPhase::kProfilingInternalBoundaryCount;
                     ++boundary) {
                    writeStageTimestamp();
                }
            }
            writeStageTimestamp();
            if (executeBodyPipeline && dynamicContactsEnabled
                && narrowPhaseEncoded) {
                refreshContactInputs(executionBodies);
                // Narrow-phase manifolds ping-pong every tick. Keep event
                // packing on the just-produced buffer as well.
                refreshEventSources(executionBodies);
            }
            // Even without body-body contacts, the dynamic solver owns pose
            // integration. Its contact count remains zero when broad and
            // narrow phase are disabled.
            // The one-workgroup serial solver is a latency win only for truly
            // small worlds. At larger counts, even a few hundred independent
            // contacts serialize thousands of Soft Step constraint solves on
            // one lane. Let the global path classify and solve them in
            // parallel instead.
            constexpr uint32_t kSerialWorldBodyLimit = 256u;
            constexpr uint32_t kCompactIslandBodyLimit = 1'024u;
            GpuDynamicSolver::ProfilingBoundary solverProfilingBoundary;
            if (profileThisBatch) {
                solverProfilingBoundary.callback = [](const void* userData) {
                    (*static_cast<
                        const decltype(writeStageTimestamp)*>(userData))();
                };
                solverProfilingBoundary.userData = &writeStageTimestamp;
            }
            bool dynamicWorldEncoded = true;
            if (executeBodyPipeline && narrowPhaseEncoded) {
                dynamicWorldEncoded = dynamicSolver_.encode(
                    encoder, executionBodies <= kSerialWorldBodyLimit,
                    solverProfilingBoundary);
            } else {
                for (uint32_t boundary = 0u;
                     boundary
                        < GpuDynamicSolver::kProfilingInternalBoundaryCount;
                     ++boundary) {
                    writeStageTimestamp();
                }
                dynamicWorldEncoded = !executeBodyPipeline;
            }
            if (executeBodyPipeline && !dynamicWorldEncoded) {
                LOG_ERROR("Failed to encode a GPU dynamic-world stage");
                batchSucceeded = false;
                break;
            }
            if (executeBodyPipeline && dynamicContactsEnabled
                && narrowPhaseEncoded && dynamicWorldEncoded
                && !narrowPhase_.encodeCommitActiveManifolds(encoder)) {
                LOG_ERROR("Failed to commit dense GPU contact manifolds");
                dynamicWorldEncoded = false;
                batchSucceeded = false;
                break;
            }
            writeStageTimestamp();

            if (executeBodyPipeline
                && (terrainAttached_ || terrainStateNeedsClear_)) {
                pass = wgpuCommandEncoderBeginComputePass(encoder, &passDesc);
                if (!pass) {
                    batchSucceeded = false;
                    break;
                }
                wgpuComputePassEncoderSetPipeline(pass, staticContactPipeline_);
                wgpuComputePassEncoderSetBindGroup(
                    pass, 0, integrateBindGroup_, 0, nullptr);
                wgpuComputePassEncoderDispatchWorkgroups(
                    pass,
                    (executionBodies + kStaticContactWorkgroupSize - 1u)
                        / kStaticContactWorkgroupSize,
                    1, 1);
                wgpuComputePassEncoderEnd(pass);
                wgpuComputePassEncoderRelease(pass);
                terrainStateNeedsClear_ = false;
            }
            writeStageTimestamp();

            if (executeBodyPipeline && !islandManager_.encode(
                    encoder,
                    executionBodies <= kCompactIslandBodyLimit)) {
                LOG_ERROR("Failed to encode the GPU island stage");
                batchSucceeded = false;
                break;
            }
            writeStageTimestamp();

            pass = wgpuCommandEncoderBeginComputePass(encoder, &passDesc);
            if (!pass) {
                batchSucceeded = false;
                break;
            }
            wgpuComputePassEncoderSetBindGroup(
                pass, 0, tickBindGroup_, 0, nullptr);
            wgpuComputePassEncoderSetPipeline(pass, advanceTickPipeline_);
            wgpuComputePassEncoderDispatchWorkgroups(pass, 1, 1, 1);
            wgpuComputePassEncoderEnd(pass);
            wgpuComputePassEncoderRelease(pass);

            if (eventReadbackEnabled_) {
                if (!eventReadback_.encodeReadback(
                        encoder, encodedTick_ + tick + 1u)) {
                    LOG_WARN("GPU physics event readback ring is full");
                    if (requireClosedReadbacks) {
                        batchFailure =
                            PhysicsEncodeStatus::
                                EventReadbackUnavailable;
                        batchSucceeded = false;
                        break;
                    }
                } else {
                    lastGpuReadbackBytes_ += 16u
                        + uint64_t{eventCapacity_} * sizeof(GpuPhysicsEvent);
                }
            }
            writeStageTimestamp();
        }

        if (!batchSucceeded) {
            // Earlier passes may already be present in the caller's encoder.
            // Do not pretend that the authoritative tick completed or retire
            // its commands. Mark the backend unusable so a fresh world must be
            // initialized instead of replaying against partially mutated GPU
            // state.
            initialized_ = false;
            LOG_ERROR("WebGPU physics stopped after an incomplete encoded tick; reinitialize the world");
            return completeReport(batchFailure, true);
        }

        const bool sampleTelemetry = config_.enableTelemetryReadback
            && profileTickCount != 0u
            && !idleOnlyBatch
            && (forceDiagnosticsSample
                || intervalElapsed(finalTick, lastTelemetryReadbackTick_,
                                   config_.telemetryReadbackIntervalTicks));
        if (sampleTelemetry) {
            const auto copyTelemetry = [&](WGPUBuffer source,
                                           uint32_t destinationWord,
                                           uint32_t wordCount) {
                wgpuCommandEncoderCopyBufferToBuffer(
                    encoder, source, 0u, telemetrySnapshotBuffer_,
                    uint64_t{destinationWord} * sizeof(uint32_t),
                    uint64_t{wordCount} * sizeof(uint32_t));
            };
            copyTelemetry(countersBuffer_, kCoreTelemetryOffset,
                          kCoreTelemetryWordCount);
            copyTelemetry(ccd_.telemetryBuffer(), kCcdTelemetryOffset,
                          GpuCcd::kTelemetryWordCount);
            copyTelemetry(broadPhase_.telemetryBuffer(),
                          kBroadTelemetryOffset,
                          GpuBroadPhase::kTelemetryWordCount);
            copyTelemetry(narrowPhase_.telemetryBuffer(),
                          kNarrowTelemetryOffset,
                          GpuNarrowPhase::kTelemetryWordCount);
            copyTelemetry(dynamicSolver_.telemetryBuffer(),
                          kSolverTelemetryOffset,
                          GpuDynamicSolver::kTelemetryWordCount);
            copyTelemetry(islandManager_.telemetryBuffer(),
                          kIslandTelemetryOffset,
                          GpuIslandManager::kTelemetryWordCount);
            copyTelemetry(attachmentSolver_.telemetryBuffer(),
                          kAttachmentTelemetryOffset,
                          GpuAttachmentSolver::kTelemetryWordCount);
            copyTelemetry(narrowPhase_.pairClassTable(),
                          kNarrowCollisionPairClassOffset,
                          kGpuNarrowPhasePairClassCount);
            if (!telemetryReadback_.encodeCopy(
                    encoder, telemetrySnapshotBuffer_, 0u,
                    kTelemetrySnapshotBytes, finalTick, 0u,
                    kTelemetrySnapshotWordCount)) {
                LOG_WARN("GPU physics telemetry readback ring is full");
            } else {
                lastGpuReadbackBytes_ += kTelemetrySnapshotBytes;
                lastTelemetryReadbackTick_ = finalTick;
            }
        }

        if (queryPending_) {
            if (!querySystem_.encode(encoder)) {
                LOG_WARN("Failed to encode pending GPU physics queries");
            } else {
                lastGpuReadbackBytes_ += uint64_t{pendingQueryCount_}
                    * sizeof(GpuQueryOutput);
                queryPending_ = false;
                pendingQueryCount_ = 0u;
            }
        }
        bool debugPassEncoded = !debugRequest_.has_value();
        if (debugRequest_) {
            WGPUComputePassEncoder pass =
                wgpuCommandEncoderBeginComputePass(encoder, &passDesc);
            if (!pass) {
                LOG_WARN("Failed to encode GPU physics debug snapshot");
            } else {
                wgpuComputePassEncoderSetPipeline(pass, packDebugPipeline_);
                wgpuComputePassEncoderSetBindGroup(
                    pass, 0, debugBindGroup_, 0, nullptr);
                wgpuComputePassEncoderDispatchWorkgroups(
                    pass, (debugRequest_->bodyCount + kWorkgroupSize - 1u)
                        / kWorkgroupSize, 1, 1);
                wgpuComputePassEncoderEnd(pass);
                wgpuComputePassEncoderRelease(pass);
                debugPassEncoded = true;
            }
        }
        if (debugRequest_ && debugPassEncoded) {
            const size_t bytes =
                size_t{debugRequest_->bodyCount} * kGpuBodyBytes;
            if (!readbackRing_.encodeCopy(
                    encoder, debugPackedBuffer_, 0u, bytes, finalTick,
                    debugRequest_->firstBody,
                    debugRequest_->bodyCount)) {
                LOG_WARN("GPU physics debug readback ring is full");
                if (requireClosedReadbacks) {
                    debugRequest_.reset();
                    return failStop(
                        PhysicsEncodeStatus::
                            DebugReadbackUnavailable);
                }
            } else {
                lastGpuReadbackBytes_ += bytes;
            }
            debugRequest_.reset();
        } else if (debugRequest_) {
            if (requireClosedReadbacks) {
                debugRequest_.reset();
                return failStop(
                    PhysicsEncodeStatus::DebugReadbackUnavailable);
            }
            debugRequest_.reset();
        }

        if (profileQueryCount != 0u && !timestampEncodingFailed) {
            wgpuCommandEncoderResolveQuerySet(
                encoder, stageQuerySet_, 0u, profileQueryCount,
                stageResolveBuffer_, 0u);
            if (!stageReadback_.encodeCopy(
                    encoder, stageResolveBuffer_, 0u,
                    uint64_t{profileQueryCount} * sizeof(uint64_t),
                    profileFirstTick, profileTickCount,
                    kStagePacketWordCount)) {
                LOG_WARN("GPU physics stage timing readback ring is full");
            } else {
                lastGpuReadbackBytes_ +=
                    uint64_t{profileQueryCount} * sizeof(uint64_t);
                lastStageProfileTick_ = finalTick;
            }
        }

        encodedTick_ = finalTick;
        pendingTicks_ = 0;
        for (const PhysicsCommand& command : commands_) {
            if (command.type == PhysicsCommandType::SpawnBody
                && command.targetTick <= finalTick
                && command.body.index < scheduledSpawnTicks_.size()
                && generations_[command.body.index]
                    == command.body.generation
                && scheduledSpawnTicks_[command.body.index]
                    == command.targetTick) {
                scheduledSpawnTicks_[command.body.index] = 0u;
            }
        }
        commands_.erase(std::remove_if(commands_.begin(), commands_.end(),
            [finalTick](const PhysicsCommand& command) {
                return command.targetTick <= finalTick;
            }), commands_.end());
        attachmentCommands_.erase(std::remove_if(
            attachmentCommands_.begin(), attachmentCommands_.end(),
            [finalTick](const AttachmentMutation& mutation) {
                return mutation.targetTick <= finalTick;
            }), attachmentCommands_.end());
        for (auto it = pendingFrees_.begin(); it != pendingFrees_.end();) {
            if (it->tick <= finalTick) {
                if (it->index < bodyCapacity_
                    && hostAlive_[it->index]
                    && generations_[it->index] == it->generation
                    && scheduledDestroyTicks_[it->index] == it->tick) {
                    releaseHostBody(it->index);
                }
                it = pendingFrees_.erase(it);
            } else {
                ++it;
            }
        }
        shrinkUnusedTail();

        return completeReport(
            report.tickCount == 0u
                ? PhysicsEncodeStatus::NothingScheduled
                : PhysicsEncodeStatus::Encoded);
    }

    void requestDebug(DebugSnapshotRequest request) {
        if (!initialized_ || request.firstBody >= bodyCapacity_) return;
        if (request.bodyCount == 0) {
            request.bodyCount = std::min(
                bodyCapacity_ - request.firstBody,
                config_.debugReadbackBodyCapacity);
        }
        request.bodyCount = std::min({
            request.bodyCount,
            bodyCapacity_ - request.firstBody,
            config_.debugReadbackBodyCapacity});
        if (request.bodyCount != 0) debugRequest_ = request;
    }

    std::optional<DebugSnapshot> pollDebug() {
        auto raw = readbackRing_.poll();
        if (!raw) return std::nullopt;
        DebugSnapshot result;
        result.tick = raw->tick;
        result.bodies.reserve(raw->bodyCount);
        for (uint32_t local = 0; local < raw->bodyCount; ++local) {
            std::array<glm::uvec4, kDebugVec4Count> packedBody{};
            std::memcpy(
                packedBody.data(),
                raw->bytes.data() + size_t{local} * kGpuBodyBytes,
                kGpuBodyBytes);
            const glm::uvec4* body = packedBody.data();
            auto asFloat = [](const glm::uvec4& value) {
                glm::vec4 resultValue;
                std::memcpy(&resultValue, &value, sizeof(resultValue));
                return resultValue;
            };
            const glm::vec4 position = asFloat(body[0]);
            const glm::vec4 orientation = asFloat(body[1]);
            const glm::vec4 linear = asFloat(body[2]);
            const glm::vec4 angular = asFloat(body[3]);
            const glm::vec4 shape = asFloat(body[4]);
            const glm::vec4 inertia = asFloat(body[5]);
            const glm::vec4 material = asFloat(body[6]);
            const uint32_t packedMetadata = body[8].w;
            const uint32_t flags =
                packedMetadata & ~kGpuBodyGenerationMask;
            DebugBodyState state;
            state.handle = {
                raw->firstBody + local,
                packedMetadata & kGpuBodyGenerationMask};
            glm::ivec4 signedMetadata;
            std::memcpy(&signedMetadata, &body[8], sizeof(signedMetadata));
            state.sector = glm::ivec3(signedMetadata);
            state.position = glm::vec3(position);
            state.orientation = glm::quat(
                orientation.w, orientation.x, orientation.y, orientation.z);
            state.linearVelocity = glm::vec3(linear);
            state.angularVelocity = glm::vec3(angular);
            state.dimensions = glm::vec3(shape);
            state.inverseInertia = glm::vec3(inertia);
            state.material = {
                .friction = material.x,
                .restitution = material.y,
                .rollingResistance = material.z,
                .density = material.w,
                .flags = body[7].x,
            };
            state.shape = static_cast<ThrowableShape>(std::min(
                static_cast<uint32_t>(std::max(shape.w, 0.0f)),
                static_cast<uint32_t>(ThrowableShape::Count) - 1u));
            state.alive = (flags & kGpuBodyAliveFlag) != 0;
            state.awake = (flags & kGpuBodyAwakeFlag) != 0;
            state.staticContactCount =
                (flags & kGpuBodyTerrainContactMask)
                >> kGpuBodyTerrainContactShift;
            state.terrainRejectedByMip =
                (flags & kGpuBodyTerrainMipRejectedFlag) != 0;
            state.submerged = (flags & kGpuBodySubmergedFlag) != 0;
            state.kinematic = (flags & kGpuBodyKinematicFlag) != 0;
            state.runtimeFlags = flags;
            result.bodies.push_back(state);
        }
        cachedDebugBodies_ = result.bodies;
        return result;
    }

    PhysicsRenderView view() const noexcept {
        if (residentBodies_ == 0u) {
            return {};
        }
        return {
            .poseBuffer = poseBuffer_,
            .shapeBuffer = shapeBuffer_,
            .metadataBuffer = metadataBuffer_,
            .previousPoseBuffer = previousPoseBuffer_,
            .previousMetadataBuffer = previousMetadataBuffer_,
            .interpolationAlpha = previousPoseBuffer_
                    && schedulingMode_ == SchedulingMode::Accumulator
                    && !idleWorldConfirmed_
                ? static_cast<float>(std::clamp(
                    accumulator_ / static_cast<double>(config_.fixedTickSeconds),
                    0.0, 1.0)) : 1.0f,
            .maximumInterpolationDistance =
                2.0f * config_.maximumLinearSpeed * config_.fixedTickSeconds,
            .activeBodyIds = activeIdsBuffer_,
            .residentBodyCapacity = nextUnusedIndex_,
            .shapeCount = static_cast<uint32_t>(ThrowableShape::Count),
        };
    }

    PhysicsStats stats() const noexcept {
        PhysicsStats result;
        result.backend = BackendType::WebGpuSoft;
        result.arithmeticMode = PhysicsArithmeticMode::FastFloat;
        result.substeps = config_.substeps;
        result.fixedTickSeconds = config_.fixedTickSeconds;
        result.maximumCatchUpTicks = config_.maximumCatchUpTicks;
        result.broadPhaseCellSize = config_.broadPhaseCellSize;
        result.solverWorkgroupSize = config_.solverWorkgroupSize;
        result.solverColorCount = config_.solverColorCount;
        result.solverParallelColorCount =
            config_.solverParallelColorCount;
        result.residentBodies = residentBodies_;
        result.activeBodies = residentBodies_;
        result.bodyCapacity = bodyLimit_;
        result.pairCapacity = pairCapacity_;
        result.contactCapacity = contactCapacity_;
        result.manifoldCapacity = manifoldCapacity_;
        result.queryCapacity = config_.asyncQueryCapacity;
        result.eventCapacity = eventCapacity_;
        result.workerConcurrency = 1;
        result.estimatedPersistentBytes = arena_.persistentBytes()
                                        + attachmentSolver_.persistentBytes()
                                        + ownedTerrainBytes_;
        result.scratchBytes = arena_.scratchBytes()
                            + readbackRing_.allocatedBytes()
                            + telemetryReadback_.allocatedBytes()
                            + ccd_.allocatedBytes()
                            + broadPhase_.scratchBytes()
                            + narrowPhase_.scratchBytes()
                            + dynamicSolver_.scratchBytes()
                            + attachmentSolver_.scratchBytes()
                            + islandManager_.scratchBytes()
                            + querySystem_.allocatedBytes()
                            + eventReadback_.allocatedBytes()
                            + stageReadback_.allocatedBytes()
                            + size_t{stageQueryCapacity_} * sizeof(uint64_t);
        result.gpuUploadBytes = lastGpuUploadBytes_;
        result.gpuReadbackBytes = lastGpuReadbackBytes_;
        result.deviceMaxStorageBuffersPerShaderStage =
            deviceLimits_.maxStorageBuffersPerShaderStage;
        result.deviceMaxComputeInvocationsPerWorkgroup =
            deviceLimits_.maxComputeInvocationsPerWorkgroup;
        result.deviceMaxStorageBufferBindingSize =
            deviceLimits_.maxStorageBufferBindingSize;
        result.deviceMaxBufferSize = deviceLimits_.maxBufferSize;
        result.bodyCapacityOverflow = bodyCapacityOverflow_;
        result.commandCapacityOverflow = commandCapacityOverflow_;
        result.attachmentCapacityOverflow =
            attachmentCapacityOverflow_;
        result.attachmentCommandCapacityOverflow =
            attachmentCommandCapacityOverflow_;

        const auto saturatingU32 = [](uint64_t value) {
            return static_cast<uint32_t>(std::min(
                value, uint64_t{std::numeric_limits<uint32_t>::max()}));
        };
        const uint32_t terrainContactCapacity = saturatingU32(
            uint64_t{activeCapacity_} * 4u);
        const uint32_t waterSampleCapacity = saturatingU32(
            uint64_t{activeCapacity_} * 8u
            * std::max(config_.substeps, 1u));
        result.residentBodyUsage = {
            residentBodies_, bodyLimit_,
            std::max(highResidentBodies_, residentBodies_),
            bodyCapacityOverflow_};
        result.activeBodyUsage = {
            residentBodies_, activeCapacity_, residentBodies_, false};
        result.commandUsage = {
            saturatingU32(commands_.size()), config_.commandCapacity,
            saturatingU32(commands_.size()), commandCapacityOverflow_};
        result.attachmentUsage = {
            residentAttachments_, attachmentLimit_,
            std::max(highResidentAttachments_, residentAttachments_),
            attachmentCapacityOverflow_};
        result.attachmentCommandUsage = {
            saturatingU32(attachmentCommands_.size()),
            config_.attachmentCommandCapacity,
            saturatingU32(attachmentCommands_.size()),
            attachmentCommandCapacityOverflow_};
        result.gridEntryUsage.capacity = broadPhase_.gridEntryCapacity();
        result.candidatePairUsage.capacity =
            broadPhase_.candidatePairCapacity();
        result.uniquePairUsage.capacity = pairCapacity_;
        result.contactUsage.capacity = contactCapacity_;
        result.manifoldUsage.capacity = manifoldCapacity_;
        result.terrainContactUsage.capacity = terrainContactCapacity;
        result.overflowConstraintUsage.capacity = contactCapacity_;
        result.eventUsage.capacity = eventCapacity_;
        result.visibleBodyUsage.capacity = bodyLimit_;
        result.sleepingGridUsage.capacity = bodyLimit_;
        result.bulletUsage.capacity = ccdBulletCapacity_;
        result.waterSampleUsage.capacity = waterSampleCapacity;
        if (!cachedTelemetry_.valid) return result;

        const auto& core = cachedTelemetry_.core;
        const auto& ccd = cachedTelemetry_.ccd;
        const auto& broad = cachedTelemetry_.broad;
        const auto& narrow = cachedTelemetry_.narrow;
        const auto& solver = cachedTelemetry_.solver;
        const auto& islands = cachedTelemetry_.islands;
        const auto& attachments = cachedTelemetry_.attachments;
        // An idle-only batch changes no body or contact state. The last
        // confirmed snapshot therefore remains authoritative at the current
        // encoded tick without another mapped GPU readback.
        result.telemetryTick = idleWorldConfirmed_
            ? encodedTick_ : cachedTelemetry_.tick;
        // The compacted active list is the authoritative awake-body count.
        // Island telemetry can span several GPU-resident catch-up ticks.
        result.activeBodies = core.activeBodies;
        result.sleepingBodies = islands.sleepingBodies;
        result.activeBodyUsage = {
            core.activeBodies, activeCapacity_, core.highActiveBodies,
            core.activeOverflow};
        result.commandUsage = {
            core.commands, config_.commandCapacity, core.highCommands,
            commandCapacityOverflow_};
        result.attachmentUsage = {
            attachments.live, attachmentLimit_, attachments.highLive,
            attachmentCapacityOverflow_};
        result.attachmentCommandUsage = {
            attachments.commands, config_.attachmentCommandCapacity,
            attachments.highCommands, attachmentCommandCapacityOverflow_};
        result.gridEntryUsage = {
            broad.gridEntries, broadPhase_.gridEntryCapacity(),
            broad.highGridEntries, false};
        result.candidatePairUsage = {
            broad.candidatePairs, broadPhase_.candidatePairCapacity(),
            broad.highCandidatePairs, broad.candidateOverflow};
        result.uniquePairUsage = {
            broad.uniquePairs, pairCapacity_, broad.highUniquePairs,
            broad.pairOverflow};
        result.contactUsage = {
            solver.contactCount, contactCapacity_,
            std::max(broad.highContacts, solver.highContacts),
            broad.contactOverflow || solver.contactOverflow};
        result.manifoldUsage = {
            narrow.manifolds, manifoldCapacity_, narrow.highManifolds,
            narrow.pairOverflow};
        result.terrainContactUsage = {
            core.terrainContactPoints, terrainContactCapacity,
            core.highTerrainContactPoints, false};
        result.overflowConstraintUsage = {
            solver.overflowContacts, contactCapacity_, solver.highOverflow,
            solver.contactOverflow};
        result.eventUsage = {
            saturatingU32(uint64_t{broad.beginEvents} + broad.endEvents
                          + islands.events + attachments.breaks),
            eventCapacity_,
            saturatingU32(uint64_t{broad.highEvents} + islands.highEvents
                          + attachments.highBreaks),
            broad.eventOverflow || islands.eventOverflow};
        result.sleepingGridUsage = {
            islands.sleepingGridEntries, bodyLimit_,
            islands.highSleepingGridEntries, islands.gridOverflow};
        const bool ccdIsCurrent = ccd.tick == core.tick;
        result.bulletUsage = {
            ccdIsCurrent ? ccd.bulletRequested : 0u,
            ccdBulletCapacity_, ccd.highBulletRequested,
            ccdIsCurrent && ccd.bulletOverflow != 0u};
        result.waterSampleUsage = {
            core.waterSamples, waterSampleCapacity, core.highWaterSamples,
            false};

        result.occupiedCells = broad.occupiedCells;
        result.maximumCellBodies = broad.maximumCellBodies;
        result.activeSleepingPairs = broad.activeSleepingPairs;
        result.oversizedBodies = broad.oversizedBodies;
        result.broadPhasePairDrivingBodies = broad.pairDrivingBodies;
        result.persistentContacts = broad.persistentContacts;
        result.narrowPairClasses = narrow.pairClasses;
        result.narrowCollisionPairClasses = narrow.collisionPairClasses;
        result.manifoldPoints = narrow.manifoldPoints;
        result.speculativeManifolds = narrow.speculativeManifolds;
        result.invalidManifolds = narrow.invalidManifolds;
        result.terrainContactBodies = core.terrainContactBodies;
        result.maximumTerrainContactsPerBody =
            core.maximumTerrainContactsPerBody;
        result.submergedBodies = core.submergedBodies;
        result.kinematicBodies = core.kinematicBodies;
        result.compactIslandContacts = solver.smallIslandContacts;
        result.compactIslandBodies = solver.smallIslandBodies;
        result.serialWorldSolver = solver.serialWorld;
        result.activeGraphColors = static_cast<uint32_t>(std::count_if(
            solver.colorCounts.begin(), solver.colorCounts.end(),
            [](uint32_t count) { return count != 0u; }));
        result.maximumBodyDegree = solver.maximumBodyDegree;
        result.colorConflictErrors = solver.conflictErrors;
        result.islandCount = islands.islandCount;
        result.awakeIslands = islands.awakeIslands;
        result.sleepingIslands = islands.sleepingIslands;
        result.maximumIslandBodies = islands.maximumIslandBodies;
        result.sleepTransitions = islands.sleepTransitions;
        result.wakeTransitions = islands.wakeTransitions;
        result.sleepingGridCells = islands.sleepingGridCells;
        result.islandRootErrors = islands.rootErrors;
        result.ccdHits = ccdIsCurrent ? ccd.hits : 0u;
        result.ccdStalls = ccdIsCurrent ? ccd.stalls : 0u;
        result.ccdFailures = ccdIsCurrent ? ccd.failures : 0u;
        result.ccdMaximumIterations =
            ccdIsCurrent ? ccd.maximumIterations : 0u;
        result.contactBeginEvents = broad.beginEvents;
        result.contactEndEvents = broad.endEvents;
        result.islandEvents = islands.events;
        result.tautAttachments = attachments.taut;
        result.slackAttachments = attachments.slack;
        result.attachmentBreakEvents = attachments.breaks;
        result.staleAttachmentCommands = attachments.staleCommands;
        result.invalidAttachmentEndpoints = attachments.invalidEndpoints;
        result.highGridEntries = broad.highGridEntries;
        result.highOccupiedCells = broad.highOccupiedCells;
        result.highCandidatePairs = broad.highCandidatePairs;
        result.highPairs = std::max(broad.highUniquePairs,
                                    narrow.highInputPairs);
        result.highContacts = std::max(broad.highContacts,
                                       solver.highContacts);
        result.highManifolds = narrow.highManifolds;
        result.highManifoldPoints = narrow.highManifoldPoints;
        result.highSolverOverflowContacts = solver.highOverflow;
        result.highIslands = islands.highIslands;
        result.highSleepingBodies = islands.highSleepingBodies;
        result.highSleepingGridEntries = islands.highSleepingGridEntries;
        result.pairCapacityOverflow = broad.candidateOverflow
            || broad.pairOverflow || narrow.pairOverflow;
        result.contactCapacityOverflow = broad.contactOverflow
            || solver.contactOverflow;
        result.eventCapacityOverflow = broad.eventOverflow
            || islands.eventOverflow;
        result.sleepingGridOverflow = islands.gridOverflow;
        result.activeCapacityOverflow = core.activeOverflow;
        result.manifoldCapacityOverflow = narrow.pairOverflow;
        result.bulletCapacityOverflow = result.bulletUsage.overflow;
        return result;
    }

    struct PendingFree {
        uint64_t tick = 0;
        uint32_t index = 0;
        uint32_t generation = 0;
    };

    struct CachedTelemetry {
        bool valid = false;
        uint64_t tick = 0;
        CoreGpuTelemetry core{};
        GpuCcdTelemetry ccd{};
        GpuBroadPhaseTelemetry broad{};
        GpuNarrowPhaseTelemetry narrow{};
        GpuDynamicSolverTelemetry solver{};
        GpuIslandTelemetry islands{};
        GpuAttachmentTelemetry attachments{};
    };

    bool initialized_ = false;
    bool terrainAttached_ = false;
    bool legoTerrain_ = false;
    bool terrainStateNeedsClear_ = false;
    bool waterEnabled_ = false;
    float waterHeight_ = 0.0f;
    uint32_t terrainWidth_ = 0;
    uint32_t terrainHeight_ = 0;
    uint32_t terrainMipLevelCount_ = 0;
    uint32_t externalTerrainMipLevelCount_ = 0;
    uint32_t ownedTerrainMipLevelCount_ = 0;
    float terrainHeightScale_ = 0.0f;
    float terrainCellScale_ = 0.0f;
    size_t ownedTerrainBytes_ = 0;
    WGPUDevice device_ = nullptr;
    WGPUQueue queue_ = nullptr;
    PhysicsInitContext::GpuConfig config_{};
    WGPULimits deviceLimits_{};
    uint32_t bodyCapacity_ = 0;
    uint32_t bodyLimit_ = 0;
    uint32_t activeCapacity_ = 0;
    uint32_t pairCapacity_ = 0;
    uint32_t candidatePairCapacity_ = 0;
    uint32_t contactCapacity_ = 0;
    uint32_t manifoldCapacity_ = 0;
    uint32_t attachmentCapacity_ = 0;
    uint32_t attachmentLimit_ = 0;
    uint32_t eventCapacity_ = 0;
    uint32_t ccdBulletCapacity_ = 0;
    uint32_t blockCount_ = 0;
    uint32_t nextUnusedIndex_ = 1;
    uint32_t nextUnusedAttachmentIndex_ = 1;
    uint32_t residentBodies_ = 0;
    uint32_t residentAttachments_ = 0;
    uint32_t highResidentBodies_ = 0;
    uint32_t highResidentAttachments_ = 0;
    bool bodyCapacityOverflow_ = false;
    bool commandCapacityOverflow_ = false;
    bool attachmentCapacityOverflow_ = false;
    bool attachmentCommandCapacityOverflow_ = false;
    uint64_t lastGpuUploadBytes_ = 0;
    uint64_t lastGpuReadbackBytes_ = 0;
    double accumulator_ = 0.0;
    uint32_t pendingTicks_ = 0;
    SchedulingMode schedulingMode_ = SchedulingMode::Undecided;
    uint64_t encodedTick_ = 0;
    uint64_t nextSequence_ = 1;
    uint64_t nextPreparedMutationToken_ = 1u;
    bool queryPending_ = false;
    uint32_t pendingQueryCount_ = 0;
    bool eventReadbackEnabled_ = false;
    bool stageProfilingEnabled_ = false;
    uint32_t stageQueryCapacity_ = 0u;
    uint64_t lastStageProfileTick_ = 0u;
    uint64_t lastTelemetryReadbackTick_ = 0u;
    uint64_t lastMutationTick_ = 0u;
    bool idleWorldConfirmed_ = false;
    bool gpuTickSynchronized_ = true;
    float waterSurfaceStrength_ = 0.0f;
    glm::vec2 waterPatchLengths_{1949.0f, 326.0f};
    bool warnedCpuWaterSampler_ = false;
    PhysicsStepStats lastStepStats_{};
    CachedTelemetry cachedTelemetry_{};
    DynamicBodyReadStats lastReadStats_{};
    std::vector<uint32_t> generations_;
    std::vector<bool> hostAlive_;
    std::vector<uint64_t> scheduledSpawnTicks_;
    std::vector<uint64_t> scheduledDestroyTicks_;
    std::set<uint32_t> freeIndices_;
    std::vector<uint32_t> attachmentGenerations_;
    std::vector<bool> hostAttachmentAlive_;
    std::vector<uint32_t> freeAttachmentIndices_;
    std::vector<PendingFree> pendingFrees_;
    std::vector<PhysicsCommand> commands_;
    std::vector<PhysicsCommand> commandSortScratch_;
    std::vector<uint8_t> commandSuperseded_;
    std::vector<GpuCommand> commandUpload_;
    std::vector<size_t> latestKinematicIndex_;
    std::vector<uint64_t> latestKinematicTick_;
    std::vector<uint32_t> latestKinematicGeneration_;
    std::vector<AttachmentMutation> attachmentCommands_;
    std::vector<AttachmentMutation> attachmentCommandSortScratch_;
    std::vector<GpuAttachmentCommand> attachmentUpload_;
    std::optional<PreparedMutationState> preparedMutation_;
    std::optional<DebugSnapshotRequest> debugRequest_;
    std::vector<DebugBodyState> cachedDebugBodies_;

    GpuBufferArena arena_;
    CpuCapsuleMoverWorld characterMover_;
    DebugReadbackRing readbackRing_;
    DebugReadbackRing stageReadback_;
    DebugReadbackRing telemetryReadback_;
    GpuCcd ccd_;
    GpuBroadPhase broadPhase_;
    GpuNarrowPhase narrowPhase_;
    GpuDynamicSolver dynamicSolver_;
    GpuAttachmentSolver attachmentSolver_;
    GpuIslandManager islandManager_;
    GpuAsyncQuerySystem querySystem_;
    GpuEventReadbackRing eventReadback_;
    std::deque<PhysicsGpuStageTiming> stageTimingResults_;
    WGPUQuerySet stageQuerySet_ = nullptr;
    WGPUBuffer stageResolveBuffer_ = nullptr;
    WGPUBuffer telemetrySnapshotBuffer_ = nullptr;
    WGPUBuffer poseBuffer_ = nullptr;
    WGPUBuffer previousPoseBuffer_ = nullptr;
    WGPUBuffer previousMetadataBuffer_ = nullptr;
    WGPUBuffer motionBuffer_ = nullptr;
    WGPUBuffer shapeBuffer_ = nullptr;
    WGPUBuffer metadataBuffer_ = nullptr;
    WGPUBuffer forceBuffer_ = nullptr;
    WGPUBuffer terrainContactCacheBuffer_ = nullptr;
    WGPUBuffer activeIdsBuffer_ = nullptr;
    WGPUBuffer activeOffsetsBuffer_ = nullptr;
    WGPUBuffer blockSumsBuffer_ = nullptr;
    WGPUBuffer blockPrefixBuffer_ = nullptr;
    WGPUBuffer countersBuffer_ = nullptr;
    WGPUBuffer commandBuffer_ = nullptr;
    WGPUBuffer uniformBuffer_ = nullptr;
    WGPUBuffer debugPackedBuffer_ = nullptr;
    WGPUTexture fallbackTerrainTexture_ = nullptr;
    WGPUTextureView fallbackTerrainView_ = nullptr;
    WGPUTexture ownedTerrainTexture_ = nullptr;
    WGPUTextureView ownedTerrainView_ = nullptr;
    WGPUTextureView externalTerrainView_ = nullptr;
    WGPUTexture fallbackWaterTexture_ = nullptr;
    WGPUTextureView fallbackWaterView_ = nullptr;
    WGPUSampler fallbackWaterSampler_ = nullptr;
    WGPUTextureView externalWaterView_ = nullptr;
    WGPUSampler externalWaterSampler_ = nullptr;
    WGPUShaderModule shaderModule_ = nullptr;
    WGPUBindGroupLayout commandLayout_ = nullptr;
    WGPUBindGroupLayout compactLayout_ = nullptr;
    WGPUBindGroupLayout integrateLayout_ = nullptr;
    WGPUBindGroupLayout tickLayout_ = nullptr;
    WGPUBindGroupLayout debugLayout_ = nullptr;
    WGPUPipelineLayout commandPipelineLayout_ = nullptr;
    WGPUPipelineLayout compactPipelineLayout_ = nullptr;
    WGPUPipelineLayout integratePipelineLayout_ = nullptr;
    WGPUPipelineLayout tickPipelineLayout_ = nullptr;
    WGPUPipelineLayout debugPipelineLayout_ = nullptr;
    WGPUComputePipeline applyCommandsPipeline_ = nullptr;
    WGPUComputePipeline compactBlocksPipeline_ = nullptr;
    WGPUComputePipeline scanBlocksPipeline_ = nullptr;
    WGPUComputePipeline scatterActivePipeline_ = nullptr;
    WGPUComputePipeline preparePipeline_ = nullptr;
    WGPUComputePipeline staticContactPipeline_ = nullptr;
    WGPUComputePipeline advanceTickPipeline_ = nullptr;
    WGPUComputePipeline packDebugPipeline_ = nullptr;
    WGPUBindGroup commandBindGroup_ = nullptr;
    WGPUBindGroup compactBindGroup_ = nullptr;
    WGPUBindGroup integrateBindGroup_ = nullptr;
    WGPUBindGroup tickBindGroup_ = nullptr;
    WGPUBindGroup debugBindGroup_ = nullptr;
};

GpuPhysicsBackend::GpuPhysicsBackend() : impl_(std::make_unique<Impl>()) {}
GpuPhysicsBackend::~GpuPhysicsBackend() = default;
GpuPhysicsBackend::GpuPhysicsBackend(GpuPhysicsBackend&&) noexcept = default;
GpuPhysicsBackend& GpuPhysicsBackend::operator=(GpuPhysicsBackend&&) noexcept = default;

bool GpuPhysicsBackend::initialize(const PhysicsInitContext& context) {
    return impl_->initialize(context);
}
void GpuPhysicsBackend::shutdown() { impl_->shutdown(); }
bool GpuPhysicsBackend::isInitialized() const noexcept { return impl_->initialized_; }
BackendType GpuPhysicsBackend::type() const noexcept { return BackendType::WebGpuSoft; }
BackendCapabilities GpuPhysicsBackend::capabilities() const noexcept {
    return {
        .gpuResidentState = true,
        .directRenderView = true,
        .synchronousCharacter = true,
        .deterministicFloat = false,
        .lockstep = false,
        .bodyBodyContacts = impl_->config_.enableBodyBodyContacts,
        .continuousCollision = true,
        .asynchronousQueries = true,
        .eventReadback = true,
        .distanceAttachments = true,
        .atomicMutationBatches = true,
        .fixedTickScheduling = true,
        .checkedGpuEncoding = true,
    };
}
bool GpuPhysicsBackend::setTerrain(std::span<const uint16_t> samples,
                                   uint32_t width, uint32_t height,
                                   float heightScale, float cellScale) {
    return impl_->setTerrain(samples, width, height, heightScale, cellScale);
}
bool GpuPhysicsBackend::setLegoTerrain(std::span<const uint16_t> samples,
    uint32_t width, uint32_t height, float heightScale, float cellScale) {
    return impl_->setTerrain(samples, width, height, heightScale, cellScale, true);
}

void GpuPhysicsBackend::setTerrainGpuResources(
    const TerrainGpuResources& resources) {
    impl_->setTerrainGpuResources(resources);
}
void GpuPhysicsBackend::clearTerrain() { impl_->clearTerrain(); }
bool GpuPhysicsBackend::hasTerrain() const noexcept { return impl_->terrainAttached_; }
void GpuPhysicsBackend::setWaterPlane(float height, bool enabled) {
    if (!std::isfinite(height)) {
        LOG_WARN("Ignoring non-finite GPU water-plane height");
        impl_->waterEnabled_ = false;
        return;
    }
    impl_->waterHeight_ = height;
    impl_->waterEnabled_ = enabled;
}
void GpuPhysicsBackend::setWaterSurfaceSampler(WaterSurfaceSampler sampler) {
    if (sampler && !impl_->warnedCpuWaterSampler_) {
        LOG_WARN("WebGPU physics cannot execute a CPU water callback; bind the shared displacement texture with setWaterGpuResources instead");
        impl_->warnedCpuWaterSampler_ = true;
    }
}
void GpuPhysicsBackend::setWaterGpuResources(
    const WaterGpuResources& resources) {
    impl_->setWaterGpuResources(resources);
}
CharacterHandle GpuPhysicsBackend::createCharacter(
    const glm::vec3& feetPosition, const CharacterSettings& settings) {
    return impl_->characterMover_.createCharacter(feetPosition, settings);
}
CharacterHandle GpuPhysicsBackend::createCharacter(
    const WorldPosition& feetPosition, const CharacterSettings& settings) {
    return impl_->characterMover_.createCharacter(feetPosition, settings);
}
void GpuPhysicsBackend::destroyCharacter(CharacterHandle handle) {
    impl_->characterMover_.destroyCharacter(handle);
}
bool GpuPhysicsBackend::setCharacterPosition(
    CharacterHandle handle, const glm::vec3& feetPosition) {
    return impl_->characterMover_.setCharacterPosition(handle, feetPosition);
}
bool GpuPhysicsBackend::setCharacterPosition(
    CharacterHandle handle, const WorldPosition& feetPosition) {
    return impl_->characterMover_.setCharacterPosition(handle, feetPosition);
}
CharacterMotion GpuPhysicsBackend::moveCharacter(
    CharacterHandle handle, const glm::vec3& desiredHorizontalVelocity,
    bool jump, float jumpSpeed, float gravity, float terminalVelocity,
    float deltaTime) {
    return impl_->characterMover_.moveCharacter(
        handle, desiredHorizontalVelocity, jump, jumpSpeed, gravity,
        terminalVelocity, deltaTime);
}
bool GpuPhysicsBackend::throwBody(ThrowableShape shape, const glm::vec3& position,
                                  const glm::vec3& velocity) {
    BodySpawnDesc desc;
    desc.shape = shape;
    desc.position = position;
    desc.linearVelocity = velocity;
    desc.dimensions = throwableShapeDimensions(shape);
    return impl_->spawn(desc).valid();
}
BodyHandle GpuPhysicsBackend::spawnBody(const BodySpawnDesc& desc) {
    return impl_->spawn(desc);
}
bool GpuPhysicsBackend::destroyBody(BodyHandle handle) {
    return impl_->destroy(handle);
}
void GpuPhysicsBackend::enqueue(std::span<const PhysicsCommand> commands) {
    impl_->enqueueCommands(commands);
}
AttachmentHandle GpuPhysicsBackend::createDistanceAttachment(
    const DistanceAttachmentDesc& desc) {
    return impl_->createAttachment(desc);
}
bool GpuPhysicsBackend::destroyAttachment(AttachmentHandle handle) {
    return impl_->destroyAttachment(handle);
}
bool GpuPhysicsBackend::setAttachmentTargetLength(
    AttachmentHandle handle, float targetLength) {
    return impl_->setAttachmentTarget(handle, targetLength);
}
bool GpuPhysicsBackend::setAttachmentMotorSpeed(
    AttachmentHandle handle, float motorSpeed) {
    return impl_->setAttachmentMotor(handle, motorSpeed);
}
PreparedPhysicsMutation GpuPhysicsBackend::prepareMutationBatch(
    const PhysicsMutationBatch& batch) noexcept {
    return impl_->prepareMutationBatch(batch);
}
PhysicsMutationResult GpuPhysicsBackend::commitPrepared(
    const PreparedPhysicsMutation& prepared) noexcept {
    return impl_->commitPrepared(prepared);
}
bool GpuPhysicsBackend::discardPrepared(
    const PreparedPhysicsMutation& prepared) noexcept {
    return impl_->discardPrepared(prepared);
}
void GpuPhysicsBackend::stepCpu(float deltaTime) { impl_->schedule(deltaTime); }
bool GpuPhysicsBackend::scheduleFixedTicks(uint32_t tickCount) {
    return impl_->scheduleFixedTicks(tickCount);
}
void GpuPhysicsBackend::encodeGpuStep(WGPUCommandEncoder encoder) {
    static_cast<void>(impl_->encode(encoder, false));
}
PhysicsEncodeReport GpuPhysicsBackend::encodeGpuStepChecked(
    WGPUCommandEncoder encoder) {
    return impl_->encode(encoder, true);
}
bool GpuPhysicsBackend::submitQueries(
    std::span<const PhysicsQueryRequest> requests, uint64_t tick) {
    return impl_->submitQueries(requests, tick);
}
std::optional<PhysicsQueryBatch> GpuPhysicsBackend::pollQueryResults() {
    return impl_->pollQueryResults();
}
bool GpuPhysicsBackend::setEventReadbackEnabled(bool enabled) {
    return impl_->setEventReadbackEnabled(enabled);
}
std::optional<PhysicsEventBatch> GpuPhysicsBackend::pollEvents() {
    return impl_->pollEvents();
}
std::optional<PhysicsGpuStageTiming>
GpuPhysicsBackend::pollGpuStageTimings() {
    return impl_->pollGpuStageTimings();
}
PhysicsRenderView GpuPhysicsBackend::renderView() const { return impl_->view(); }
void GpuPhysicsBackend::requestDebugSnapshot(DebugSnapshotRequest request) {
    impl_->requestDebug(request);
}
std::optional<DebugSnapshot> GpuPhysicsBackend::pollDebugSnapshot() {
    return impl_->pollDebug();
}
std::vector<DynamicBodySnapshot> GpuPhysicsBackend::dynamicBodies(
    size_t additionalCapacity) const {
    std::vector<DynamicBodySnapshot> result;
    result.reserve(impl_->cachedDebugBodies_.size() + additionalCapacity);
    for (const auto& body : impl_->cachedDebugBodies_) {
        if (!body.alive) continue;
        result.push_back({body.shape, body.position, body.orientation,
                          body.dimensions, body.awake,
                          body.sector});
    }
    impl_->lastReadStats_ = {
        .bodyCount = result.size(),
        .cachedBodyCount = result.size(),
    };
    return result;
}
DynamicBodyReadStats GpuPhysicsBackend::lastDynamicBodyReadStats() const noexcept {
    return impl_->lastReadStats_;
}
PhysicsStats GpuPhysicsBackend::stats() const noexcept { return impl_->stats(); }
PhysicsStepStats GpuPhysicsBackend::lastStepStats() const noexcept {
    return impl_->lastStepStats_;
}
uint64_t GpuPhysicsBackend::encodedTick() const noexcept {
    return impl_->encodedTick();
}

} // namespace voxy::physics
