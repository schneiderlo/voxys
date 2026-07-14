#include "physics/gpu/gpu_physics_backend.hpp"

#include "core/log.hpp"
#include "gpu/resources.hpp"
#include "physics/character/cpu_capsule_mover.hpp"
#include "physics/gpu/debug_readback_ring.hpp"
#include "physics/gpu/gpu_body_metadata.hpp"
#include "physics/gpu/gpu_broad_phase.hpp"
#include "physics/gpu/gpu_buffer_arena.hpp"
#include "physics/gpu/gpu_ccd.hpp"
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
#include <utility>
#include <vector>

namespace voxy::physics {
namespace {

constexpr uint32_t kWorkgroupSize = 256;
// The command and integration layouts are the widest physics layouts. Each
// exposes eight storage buffers, matching WebGPU's guaranteed minimum.
constexpr uint32_t kRequiredStorageBuffersPerShaderStage = 8;
constexpr size_t kGpuBodyBytes = 112;
constexpr size_t kDebugVec4Count = 7;
constexpr uint32_t kStageBoundaryCount =
    static_cast<uint32_t>(kPhysicsGpuStageCount) + 1u;
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
constexpr uint32_t kTelemetrySnapshotWordCount =
    kIslandTelemetryOffset + GpuIslandManager::kTelemetryWordCount;
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
};

struct alignas(16) GpuCommand {
    glm::uvec4 header{0u};
    glm::vec4 p0{0.0f};
    glm::vec4 p1{0.0f};
    glm::vec4 p2{0.0f};
    glm::vec4 p3{0.0f};
    glm::vec4 p4{0.0f};
    glm::ivec4 p5{0};
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
    return result;
}

static_assert(sizeof(GpuPose) == 32);
static_assert(sizeof(GpuMotion) == 32);
static_assert(sizeof(GpuShape) == 32);
static_assert(sizeof(GpuCommand) == 112);
static_assert(sizeof(SimulationUniforms) == 160);
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
        case PhysicsCommandType::ApplyForce: return 5;
        case PhysicsCommandType::Wake:
        case PhysicsCommandType::Sleep: return 6;
    }
    return 7;
}

uint32_t nextBodyGeneration(uint32_t generation) noexcept {
    generation = (generation + 1u) & kGpuBodyGenerationMask;
    return generation == 0u ? 1u : generation;
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
    ~Impl() { shutdown(); }

    bool initialize(const PhysicsInitContext& context) {
        if (initialized_) return true;
        if (!context.device || !context.queue || context.maxBodies < 2
            || context.gpu.fixedTickSeconds <= 0.0f
            || context.gpu.substeps == 0
            || context.gpu.commandCapacity == 0
            || context.gpu.asyncQueryCapacity == 0
            || context.gpu.asyncQueryReadbackSlots == 0) {
            LOG_ERROR("WebGPU physics requires a device, queue, and valid capacities");
            return false;
        }

        WGPULimits limits{};
        if (!gpu::getDeviceLimits(context.device, limits)) {
            LOG_ERROR("WebGPU physics could not query device limits");
            return false;
        }
        if (limits.maxStorageBuffersPerShaderStage
                < kRequiredStorageBuffersPerShaderStage
            || limits.maxComputeInvocationsPerWorkgroup < kWorkgroupSize
            || limits.maxComputeWorkgroupSizeX < kWorkgroupSize) {
            LOG_WARN(
                "WebGPU physics device profile rejected: storage buffers {}/{}, compute invocations {}/{}, workgroup X {}/{}",
                limits.maxStorageBuffersPerShaderStage,
                kRequiredStorageBuffersPerShaderStage,
                limits.maxComputeInvocationsPerWorkgroup, kWorkgroupSize,
                limits.maxComputeWorkgroupSizeX, kWorkgroupSize);
            return false;
        }

        device_ = context.device;
        queue_ = context.queue;
        deviceLimits_ = limits;
        config_ = context.gpu;
        bodyCapacity_ = context.maxBodies;
        activeCapacity_ = std::min(context.maxActiveBodies, context.maxBodies);
        pairCapacity_ = context.maxPairs;
        contactCapacity_ = context.maxContacts;
        manifoldCapacity_ = context.maxManifolds;
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
        telemetrySnapshotBuffer_ = arena_.create(
            "physics_telemetry_snapshot", kTelemetrySnapshotBytes,
            WGPUBufferUsage_CopyDst | WGPUBufferUsage_CopySrc, true);
        if (!poseBuffer_ || !motionBuffer_ || !shapeBuffer_
            || !metadataBuffer_ || !forceBuffer_
            || !terrainContactCacheBuffer_
            || !activeIdsBuffer_ || !activeOffsetsBuffer_ || !blockSumsBuffer_
            || !blockPrefixBuffer_ || !countersBuffer_ || !commandBuffer_
            || !uniformBuffer_ || !debugPackedBuffer_
            || !telemetrySnapshotBuffer_) {
            shutdown();
            return false;
        }

        const std::array<uint32_t, kCoreTelemetryWordCount> zeroCounters{};
        gpu::writeBuffer(queue_, countersBuffer_, 0, zeroCounters);

        if (!createFallbackTerrain()) {
            shutdown();
            return false;
        }

        shaderModule_ = gpu::loadShaderModule(
            device_, config_.shaderPath, "physics_ballistic.wgsl");
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
        if (!telemetryReadback_.initialize(
                device_, std::max(config_.telemetryReadbackSlots, 1u),
                kTelemetrySnapshotBytes)) {
            shutdown();
            return false;
        }

        GpuCcd::Config ccdConfig;
        ccdConfig.bodyCapacity = bodyCapacity_;
        ccdConfig.bulletCapacity = std::min(
            std::max(config_.ccdBulletCapacity, 1u), bodyCapacity_);
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
        if (!ccd_.initialize(device_, queue_, ccdConfig)) {
            shutdown();
            return false;
        }

        GpuBroadPhase::Config broadConfig;
        broadConfig.bodyCapacity = bodyCapacity_;
        broadConfig.pairCapacity = pairCapacity_;
        broadConfig.contactCapacity = manifoldCapacity_;
        broadConfig.candidatePairCapacity = static_cast<uint32_t>(std::min(
            uint64_t{pairCapacity_} * 4u,
            uint64_t{std::numeric_limits<uint32_t>::max()}));
        broadConfig.cellSize = config_.broadPhaseCellSize;
        broadConfig.speculativeMargin = config_.speculativeDistance;
        broadConfig.shaderPath = shaderFile("physics_broad_phase.wgsl");
        broadConfig.primitivesShaderPath = shaderFile(
            "physics_deterministic_primitives.wgsl");
        if (!broadPhase_.initialize(device_, queue_, broadConfig)) {
            shutdown();
            return false;
        }
        broadPhase_.setBodyView({
            .poseBuffer = poseBuffer_,
            .shapeBuffer = shapeBuffer_,
            .metadataBuffer = metadataBuffer_,
            .bodyCapacity = bodyCapacity_,
        });

        GpuNarrowPhase::Config narrowConfig;
        narrowConfig.pairCapacity = pairCapacity_;
        narrowConfig.manifoldCapacity = manifoldCapacity_;
        narrowConfig.linearSlop = config_.linearSlop;
        narrowConfig.speculativeDistance = config_.speculativeDistance;
        narrowConfig.recycleDistance = std::max(
            0.05f, config_.speculativeDistance * 2.0f);
        narrowConfig.shaderPath = shaderFile("physics_narrow_phase.wgsl");
        if (!narrowPhase_.initialize(device_, queue_, narrowConfig)) {
            shutdown();
            return false;
        }
        narrowPhase_.setInput({
            .poseBuffer = poseBuffer_,
            .shapeBuffer = shapeBuffer_,
            .uniquePairBuffer = broadPhase_.uniquePairs(),
            .broadPhaseTelemetryBuffer = broadPhase_.telemetryBuffer(),
            .bodyCapacity = bodyCapacity_,
            .pairCapacity = pairCapacity_,
            .metadataBuffer = metadataBuffer_,
        });

        GpuDynamicSolver::Config solverConfig;
        solverConfig.bodyCapacity = bodyCapacity_;
        solverConfig.contactCapacity = contactCapacity_;
        solverConfig.substeps = config_.substeps;
        solverConfig.tickSeconds = config_.fixedTickSeconds;
        // The ballistic preparation pass applies forces, gravity, damping,
        // and water exactly once. The dynamic solver owns pose integration.
        solverConfig.gravity = {0.0f, 0.0f, 0.0f};
        solverConfig.linearDamping = 0.0f;
        solverConfig.angularDamping = 0.0f;
        solverConfig.linearSlop = config_.linearSlop;
        solverConfig.speculativeDistance = config_.speculativeDistance;
        solverConfig.friction = config_.terrainFriction;
        solverConfig.restitution = config_.terrainRestitution;
        solverConfig.shaderPath = shaderFile("physics_dynamic_solver.wgsl");
        solverConfig.primitivesShaderPath = shaderFile(
            "physics_deterministic_primitives.wgsl");
        if (!dynamicSolver_.initialize(device_, queue_, solverConfig)) {
            shutdown();
            return false;
        }

        GpuIslandManager::Config islandConfig;
        islandConfig.bodyCapacity = bodyCapacity_;
        islandConfig.contactCapacity = contactCapacity_;
        islandConfig.eventCapacity = bodyCapacity_;
        islandConfig.sleepingCellSize = config_.broadPhaseCellSize;
        islandConfig.shaderPath = shaderFile("physics_islands.wgsl");
        islandConfig.primitivesShaderPath = shaderFile(
            "physics_deterministic_primitives.wgsl");
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
        if (!querySystem_.initialize(device_, queue_, queryConfig)) {
            shutdown();
            return false;
        }
        querySystem_.setBodyView({
            .poseBuffer = poseBuffer_,
            .shapeBuffer = shapeBuffer_,
            .metadataBuffer = metadataBuffer_,
            .bodyCapacity = bodyCapacity_,
        });

        const uint64_t maximumEvents = uint64_t{manifoldCapacity_} * 3u
                                     + bodyCapacity_;
        if (maximumEvents > std::numeric_limits<uint32_t>::max()) {
            LOG_ERROR("WebGPU physics event capacity exceeds u32 range");
            shutdown();
            return false;
        }
        eventCapacity_ = static_cast<uint32_t>(maximumEvents);
        GpuEventReadbackRing::Config eventConfig;
        eventConfig.eventCapacity = eventCapacity_;
        eventConfig.readbackSlots = config_.eventReadbackSlots == 0
            ? std::max(config_.maximumCatchUpTicks, 3u)
            : config_.eventReadbackSlots;
        eventConfig.shaderPath = shaderFile("physics_event_readback.wgsl");
        if (!eventReadback_.initialize(device_, queue_, eventConfig)) {
            shutdown();
            return false;
        }
        eventReadback_.setSources({
            .contactEvents = broadPhase_.contactEvents(),
            .contactTelemetry = broadPhase_.telemetryBuffer(),
            .contactCapacity = broadPhase_.contactCapacity(),
            .islandEvents = islandManager_.events(),
            .islandTelemetry = islandManager_.telemetryBuffer(),
            .islandEventCapacity = bodyCapacity_,
            .manifolds = narrowPhase_.manifolds(),
            .narrowPhaseTelemetry = narrowPhase_.telemetryBuffer(),
            .manifoldCapacity = manifoldCapacity_,
        });

        if (config_.enableStageProfiling) {
            if (!std::isfinite(
                    config_.stageProfilingTimestampPeriodNanoseconds)
                || config_.stageProfilingTimestampPeriodNanoseconds <= 0.0) {
                LOG_WARN("GPU physics stage profiling requested with an invalid timestamp period");
            } else if (!wgpuDeviceHasFeature(
                    device_, WGPUFeatureName_TimestampQuery)) {
                LOG_WARN("GPU physics stage profiling requested, but timestamp queries are unavailable");
            } else {
                stageQueryCapacity_ = kStageBoundaryCount
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
        LOG_INFO("WebGPU physics initialized: {} body slots, {} MiB resident, {} MiB scratch",
                 bodyCapacity_, arena_.persistentBytes() / (1024 * 1024),
                 arena_.scratchBytes() / (1024 * 1024));
        return true;
    }

    void shutdown() {
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
        dynamicSolver_.shutdown();
        narrowPhase_.shutdown();
        broadPhase_.shutdown();
        ccd_.shutdown();
        readbackRing_.shutdown();
        releaseHandle(commandBindGroup_, wgpuBindGroupRelease);
        releaseHandle(compactBindGroup_, wgpuBindGroupRelease);
        releaseHandle(integrateBindGroup_, wgpuBindGroupRelease);
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
        releaseHandle(debugPipelineLayout_, wgpuPipelineLayoutRelease);
        releaseHandle(commandLayout_, wgpuBindGroupLayoutRelease);
        releaseHandle(compactLayout_, wgpuBindGroupLayoutRelease);
        releaseHandle(integrateLayout_, wgpuBindGroupLayoutRelease);
        releaseHandle(debugLayout_, wgpuBindGroupLayoutRelease);
        releaseHandle(shaderModule_, wgpuShaderModuleRelease);
        releaseTerrainTexture(ownedTerrainTexture_, ownedTerrainView_);
        releaseTerrainTexture(fallbackTerrainTexture_, fallbackTerrainView_);
        arena_.shutdown();
        poseBuffer_ = nullptr;
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
        activeCapacity_ = 0;
        pairCapacity_ = 0;
        contactCapacity_ = 0;
        manifoldCapacity_ = 0;
        eventCapacity_ = 0;
        ccdBulletCapacity_ = 0;
        blockCount_ = 0;
        nextUnusedIndex_ = 1;
        residentBodies_ = 0;
        highResidentBodies_ = 0;
        bodyCapacityOverflow_ = false;
        commandCapacityOverflow_ = false;
        lastGpuUploadBytes_ = 0;
        lastGpuReadbackBytes_ = 0;
        accumulator_ = 0.0;
        pendingTicks_ = 0;
        encodedTick_ = 0;
        nextSequence_ = 1;
        queryPending_ = false;
        pendingQueryCount_ = 0;
        eventReadbackEnabled_ = false;
        stageProfilingEnabled_ = false;
        stageQueryCapacity_ = 0u;
        stageTimingResults_.clear();
        cachedTelemetry_ = {};
        commands_.clear();
        pendingFrees_.clear();
        freeIndices_.clear();
        generations_.clear();
        hostAlive_.clear();
        debugRequest_.reset();
        cachedDebugBodies_.clear();
        terrainAttached_ = false;
        terrainStateNeedsClear_ = false;
        externalTerrainView_ = nullptr;
        terrainWidth_ = 0;
        terrainHeight_ = 0;
        terrainMipLevelCount_ = 0;
        externalTerrainMipLevelCount_ = 0;
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

    [[nodiscard]] WGPUTextureView terrainBindingView() const noexcept {
        if (externalTerrainView_) return externalTerrainView_;
        if (ownedTerrainView_) return ownedTerrainView_;
        return fallbackTerrainView_;
    }

    bool createOwnedTerrain(std::span<const uint16_t> samples,
                            uint32_t width, uint32_t height) {
        releaseTerrainTexture(ownedTerrainTexture_, ownedTerrainView_);
        ownedTerrainBytes_ = 0;
        const uint32_t mipCount = terrain::calculateMipLevelCount(width, height);
        auto desc = gpu::TextureDesc::tex2D(
            width, height, WGPUTextureFormat_R16Uint,
            WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst,
            "physics_owned_heightmap");
        desc.mipLevelCount = mipCount;
        ownedTerrainTexture_ = gpu::createTexture(device_, desc);
        if (!ownedTerrainTexture_) return false;

        gpu::writeTexture(queue_, ownedTerrainTexture_,
                          std::as_bytes(samples), width, height,
                          width * sizeof(uint16_t), 0);
        size_t allocatedBytes = samples.size_bytes();
        std::span<const uint16_t> previous = samples;
        uint32_t previousWidth = width;
        uint32_t previousHeight = height;
        terrain::MipLevel current;
        for (uint32_t level = 1; level < mipCount; ++level) {
            terrain::MipLevel next = terrain::generateNextMipLevel(
                previous, previousWidth, previousHeight);
            if (!next.isValid()) {
                releaseTerrainTexture(ownedTerrainTexture_, ownedTerrainView_);
                return false;
            }
            gpu::writeTexture(queue_, ownedTerrainTexture_,
                              std::as_bytes(std::span<const uint16_t>(next.data)),
                              next.width, next.height,
                              next.width * sizeof(uint16_t), level);
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
        ownedTerrainView_ = gpu::createTextureView(
            ownedTerrainTexture_, viewDesc);
        if (!ownedTerrainView_) {
            releaseTerrainTexture(ownedTerrainTexture_, ownedTerrainView_);
            return false;
        }
        terrainMipLevelCount_ = mipCount;
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
        integrateLayout_ = gpu::createBindGroupLayout(
            device_, integrateEntries, "physics_integrate_layout");

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
            || !debugLayout_) return false;

        const std::array<WGPUBindGroupLayout, 1> commandLayouts{commandLayout_};
        const std::array<WGPUBindGroupLayout, 1> compactLayouts{compactLayout_};
        const std::array<WGPUBindGroupLayout, 1> integrateLayouts{integrateLayout_};
        const std::array<WGPUBindGroupLayout, 1> debugLayouts{debugLayout_};
        commandPipelineLayout_ = gpu::createPipelineLayout(
            device_, commandLayouts, "physics_command_pipeline_layout");
        compactPipelineLayout_ = gpu::createPipelineLayout(
            device_, compactLayouts, "physics_compact_pipeline_layout");
        integratePipelineLayout_ = gpu::createPipelineLayout(
            device_, integrateLayouts, "physics_integrate_pipeline_layout");
        debugPipelineLayout_ = gpu::createPipelineLayout(
            device_, debugLayouts, "physics_debug_pipeline_layout");
        if (!commandPipelineLayout_ || !compactPipelineLayout_
            || !integratePipelineLayout_ || !debugPipelineLayout_) return false;

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
            device_, integratePipelineLayout_, shaderModule_, "advance_tick",
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
            BE(7).buffer(commandBuffer_),
            BE(8).buffer(uniformBuffer_)};
        commandBindGroup_ = gpu::createBindGroup(
            device_, commandLayout_, commandBindings, "physics_commands");

        const std::array<BE, 7> compactBindings = {
            BE(3).buffer(metadataBuffer_), BE(6).buffer(countersBuffer_),
            BE(9).buffer(activeIdsBuffer_), BE(10).buffer(activeOffsetsBuffer_),
            BE(11).buffer(blockSumsBuffer_), BE(12).buffer(blockPrefixBuffer_),
            BE(8).buffer(uniformBuffer_)};
        compactBindGroup_ = gpu::createBindGroup(
            device_, compactLayout_, compactBindings, "physics_compaction");

        const std::array<BE, 6> debugBindings = {
            BE(0).buffer(poseBuffer_), BE(1).buffer(motionBuffer_),
            BE(2).buffer(shapeBuffer_), BE(3).buffer(metadataBuffer_),
            BE(13).buffer(debugPackedBuffer_), BE(8).buffer(uniformBuffer_)};
        debugBindGroup_ = gpu::createBindGroup(
            device_, debugLayout_, debugBindings, "physics_debug_pack");
        return commandBindGroup_ && compactBindGroup_ && debugBindGroup_
            && rebuildIntegrateBindGroup();
    }

    bool rebuildIntegrateBindGroup() {
        using BE = gpu::BindGroupEntry;
        if (!integrateLayout_ || !terrainBindingView()) return false;
        releaseHandle(integrateBindGroup_, wgpuBindGroupRelease);
        const std::array<BE, 10> bindings = {
            BE(0).buffer(poseBuffer_),
            BE(1).buffer(motionBuffer_),
            BE(2).buffer(shapeBuffer_),
            BE(3).buffer(metadataBuffer_),
            BE(5).buffer(forceBuffer_),
            BE(6).buffer(countersBuffer_),
            BE(9).buffer(activeIdsBuffer_),
            BE(15).buffer(terrainContactCacheBuffer_),
            BE(8).buffer(uniformBuffer_),
            BE(14).textureView(terrainBindingView()),
        };
        integrateBindGroup_ = gpu::createBindGroup(
            device_, integrateLayout_, bindings, "physics_integration");
        return integrateBindGroup_ != nullptr;
    }

    bool setTerrain(std::span<const uint16_t> samples,
                    uint32_t width, uint32_t height,
                    float heightScale, float cellScale) {
        const bool hadTerrain = terrainAttached_;
        const size_t expected = size_t{width} * height;
        if (!initialized_ || width < 2 || height < 2
            || samples.size() < expected || heightScale <= 0.0f
            || cellScale <= 0.0f) {
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
        if (externalTerrainView_) {
            terrainMipLevelCount_ = std::max(externalTerrainMipLevelCount_, 1u);
        } else if (!createOwnedTerrain(samples.first(expected), width, height)) {
            terrainStateNeedsClear_ = terrainStateNeedsClear_
                || terrainAttached_;
            terrainAttached_ = false;
            characterMover_.clearTerrain();
            refreshCcdInput();
            return false;
        }
        terrainAttached_ = rebuildIntegrateBindGroup();
        if (terrainAttached_ && !characterMover_.setTerrain(
                samples.first(expected), width, height,
                heightScale, cellScale)) {
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

    void refreshCcdInput() {
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
            .bodyCapacity = bodyCapacity_,
            .terrainWidth = terrainWidth_,
            .terrainHeight = terrainHeight_,
            .terrainHeightScale = terrainHeightScale_,
            .terrainCellScale = terrainCellScale_,
            .terrainSector = {0, 0, 0},
        });
    }

    void refreshContactInputs() {
        const WGPUBuffer manifolds = narrowPhase_.manifolds();
        dynamicSolver_.setInput({
            .poseBuffer = poseBuffer_,
            .motionBuffer = motionBuffer_,
            .shapeBuffer = shapeBuffer_,
            .metadataBuffer = metadataBuffer_,
            .manifoldBuffer = manifolds,
            .narrowPhaseTelemetryBuffer = narrowPhase_.telemetryBuffer(),
            .bodyCapacity = bodyCapacity_,
            .contactCapacity = contactCapacity_,
        });
        islandManager_.setInput({
            .poseBuffer = poseBuffer_,
            .motionBuffer = motionBuffer_,
            .metadataBuffer = metadataBuffer_,
            .manifoldBuffer = manifolds,
            .narrowPhaseTelemetryBuffer = narrowPhase_.telemetryBuffer(),
            .bodyCapacity = bodyCapacity_,
            .contactCapacity = contactCapacity_,
        });
    }

    BodyHandle spawn(const BodySpawnDesc& requested) {
        if (!initialized_) return {};
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
        command.sequence = nextSequence_++;
        command.shape = desc.shape;
        command.a = glm::vec4(desc.position, desc.inverseMass);
        command.b = glm::vec4(desc.orientation.x, desc.orientation.y,
                              desc.orientation.z, desc.orientation.w);
        command.c = glm::vec4(desc.linearVelocity, 0.0f);
        command.d = glm::vec4(desc.angularVelocity, desc.bullet ? 1.0f : 0.0f);
        command.e = glm::vec4(desc.dimensions,
                              static_cast<float>(desc.shape));
        command.sector = desc.sector;
        commands_.push_back(command);
        hostAlive_[index] = true;
        ++residentBodies_;
        highResidentBodies_ = std::max(highResidentBodies_, residentBodies_);
        return handle;
    }

    bool destroy(BodyHandle handle) {
        if (!initialized_ || !handle.valid() || handle.index >= bodyCapacity_
            || !hostAlive_[handle.index]
            || generations_[handle.index] != handle.generation) return false;
        if (commands_.size() >= config_.commandCapacity) {
            commandCapacityOverflow_ = true;
            return false;
        }
        PhysicsCommand command;
        command.type = PhysicsCommandType::DestroyBody;
        command.body = handle;
        command.targetTick = nextMutationTick();
        command.sequence = nextSequence_++;
        commands_.push_back(command);
        hostAlive_[handle.index] = false;
        generations_[handle.index] = nextBodyGeneration(
            generations_[handle.index]);
        pendingFrees_.push_back({command.targetTick, handle.index});
        if (residentBodies_ != 0) --residentBodies_;
        return true;
    }

    void enqueueCommands(std::span<const PhysicsCommand> input) {
        for (PhysicsCommand command : input) {
            if (commands_.size() >= config_.commandCapacity) {
                commandCapacityOverflow_ = true;
                LOG_WARN("GPU physics command capacity {} exceeded",
                         config_.commandCapacity);
                break;
            }
            if (!command.body.valid() || command.body.index >= bodyCapacity_)
                continue;
            if (command.type == PhysicsCommandType::SpawnBody
                || command.type == PhysicsCommandType::Teleport
                || command.type == PhysicsCommandType::SetKinematicTarget) {
                const WorldPosition position = canonicalWorldPosition(
                    command.sector, glm::dvec3(command.a));
                command.sector = position.sector;
                command.a = glm::vec4(position.local, command.a.w);
            }
            if (command.targetTick == 0) command.targetTick = nextMutationTick();
            if (command.sequence == 0) command.sequence = nextSequence_++;
            commands_.push_back(command);
        }
    }

    uint64_t nextMutationTick() const noexcept {
        return encodedTick_ + pendingTicks_ + 1u;
    }

    void pollTelemetry() {
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
        cachedTelemetry_.narrow = GpuNarrowPhase::decodeTelemetry(view.subspan(
            kNarrowTelemetryOffset, GpuNarrowPhase::kTelemetryWordCount));
        cachedTelemetry_.solver = GpuDynamicSolver::decodeTelemetry(view.subspan(
            kSolverTelemetryOffset, GpuDynamicSolver::kTelemetryWordCount));
        cachedTelemetry_.islands = GpuIslandManager::decodeTelemetry(view.subspan(
            kIslandTelemetryOffset, GpuIslandManager::kTelemetryWordCount));
    }

    void schedule(float deltaTime) {
        const auto start = std::chrono::steady_clock::now();
        pollTelemetry();
        if (!initialized_ || !std::isfinite(deltaTime) || deltaTime <= 0.0f) {
            lastStepStats_ = {};
            return;
        }
        const double maximumDelta = double{config_.fixedTickSeconds}
                                  * config_.maximumCatchUpTicks;
        accumulator_ += std::min(
            static_cast<double>(deltaTime), maximumDelta);
        uint32_t scheduled = 0;
        const double fixedTick = static_cast<double>(config_.fixedTickSeconds);
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
                || request.capsuleHalfHeight < 0.0f) {
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

    void setEventReadbackEnabled(bool enabled) noexcept {
        eventReadbackEnabled_ = initialized_ && enabled;
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
                static_cast<uint32_t>(PhysicsEventType::IslandWake)));
            if (event.type == PhysicsEventType::ContactBegin
                || event.type == PhysicsEventType::ContactEnd
                || event.type == PhysicsEventType::ContactHit) {
                event.bodyA = std::min(source.header[2], source.header[3]);
                event.bodyB = std::max(source.header[2], source.header[3]);
            } else {
                event.bodyA = source.header[2];
                event.bodyB = source.header[3];
            }
            event.featureId = source.detail[0];
            event.sourceId = source.detail[1];
            event.auxiliaryCount = source.detail[2];
            event.flags = source.detail[3];
            result.events.push_back(event);
        }
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
        if (tickCount == 0u || boundaryCount != kStageBoundaryCount
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
                const uint64_t start = boundaries[stage];
                const uint64_t end = boundaries[stage + 1u];
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

    void encode(WGPUCommandEncoder encoder) {
        if (!initialized_ || !encoder) return;
        lastGpuUploadBytes_ = 0u;
        lastGpuReadbackBytes_ = 0u;
        const uint64_t finalTick = encodedTick_ + pendingTicks_;
        std::stable_sort(commands_.begin(), commands_.end(),
            [](const PhysicsCommand& lhs, const PhysicsCommand& rhs) {
                if (lhs.targetTick != rhs.targetTick)
                    return lhs.targetTick < rhs.targetTick;
                const uint32_t lp = commandPriority(lhs.type);
                const uint32_t rp = commandPriority(rhs.type);
                if (lp != rp) return lp < rp;
                return lhs.sequence < rhs.sequence;
            });

        std::vector<GpuCommand> upload;
        upload.reserve(std::min<size_t>(commands_.size(),
                                        config_.commandCapacity));
        for (const auto& command : commands_) {
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
            gpuCommand.p5 = glm::ivec4(command.sector, 0);
            upload.push_back(gpuCommand);
        }
        if (!upload.empty()) {
            gpu::writeBuffer(queue_, commandBuffer_, 0,
                std::as_bytes(std::span<const GpuCommand>(upload)));
            lastGpuUploadBytes_ +=
                uint64_t{upload.size()} * sizeof(GpuCommand);
        }

        SimulationUniforms uniforms;
        uniforms.gravityAndDt = glm::vec4(config_.gravity,
                                           config_.fixedTickSeconds);
        uniforms.dampingAndClamps = glm::vec4(
            config_.linearDamping, config_.angularDamping,
            config_.maximumLinearSpeed, config_.maximumAngularSpeed);
        uniforms.counts = glm::uvec4(
            bodyCapacity_, static_cast<uint32_t>(upload.size()),
            blockCount_, config_.substeps);
        if (debugRequest_) {
            uniforms.debugRange = glm::uvec4(
                debugRequest_->firstBody, debugRequest_->bodyCount, 0u, 0u);
        }
        uniforms.debugRange.z = activeCapacity_;
        const glm::vec2 terrainOrigin = terrain_topology::centeredOrigin(
            terrainWidth_, terrainHeight_, terrainCellScale_);
        uniforms.terrainOriginCellHeight = glm::vec4(
            terrainOrigin, terrainCellScale_, terrainHeightScale_);
        uniforms.terrainSizeMipFlags = glm::uvec4(
            terrainWidth_, terrainHeight_, terrainMipLevelCount_,
            terrainAttached_ ? 1u : 0u);
        uniforms.water = glm::vec4(
            waterHeight_, waterEnabled_ ? 1.0f : 0.0f,
            config_.waterBuoyancy, config_.waterLinearDrag);
        uniforms.contact = glm::vec4(
            config_.waterAngularDrag, config_.terrainFriction,
            config_.terrainRestitution, config_.linearSlop);
        uniforms.solver = glm::vec4(
            config_.speculativeDistance, 0.20f, 0.05f, 0.50f);
        uniforms.worldSector = glm::ivec4(0);
        gpu::writeBuffer(queue_, uniformBuffer_, 0, uniforms);
        lastGpuUploadBytes_ += sizeof(SimulationUniforms);

        WGPUComputePassDescriptor passDesc{};
        WGPU_SET_LABEL(passDesc, "physics_dynamic_world_step");
        const uint32_t profileTickCount = pendingTicks_;
        const uint64_t profileFirstTick = encodedTick_ + 1u;
        const bool profileThisBatch = stageProfilingEnabled_
            && uint64_t{profileTickCount} * kStageBoundaryCount
                <= stageQueryCapacity_;
        uint32_t profileQueryCount = 0u;
        const auto writeStageTimestamp = [&] {
            if (!profileThisBatch) return;
            gpu::CompatPassTimestampWrites writes{};
            writes.querySet = stageQuerySet_;
            writes.beginningOfPassWriteIndex = profileQueryCount++;
            writes.endOfPassWriteIndex = WGPU_QUERY_SET_INDEX_UNDEFINED;
            WGPUComputePassDescriptor timestampPassDesc{};
            WGPU_SET_LABEL(timestampPassDesc, "physics_stage_boundary");
            timestampPassDesc.timestampWrites = &writes;
            WGPUComputePassEncoder timestampPass =
                wgpuCommandEncoderBeginComputePass(
                    encoder, &timestampPassDesc);
            wgpuComputePassEncoderEnd(timestampPass);
            wgpuComputePassEncoderRelease(timestampPass);
        };
        for (uint32_t tick = 0; tick < pendingTicks_; ++tick) {
            writeStageTimestamp();
            WGPUComputePassEncoder pass =
                wgpuCommandEncoderBeginComputePass(encoder, &passDesc);
            wgpuComputePassEncoderSetPipeline(pass, applyCommandsPipeline_);
            wgpuComputePassEncoderSetBindGroup(
                pass, 0, commandBindGroup_, 0, nullptr);
            wgpuComputePassEncoderDispatchWorkgroups(pass, 1, 1, 1);

            wgpuComputePassEncoderSetPipeline(pass, compactBlocksPipeline_);
            wgpuComputePassEncoderSetBindGroup(
                pass, 0, compactBindGroup_, 0, nullptr);
            wgpuComputePassEncoderDispatchWorkgroups(pass, blockCount_, 1, 1);
            wgpuComputePassEncoderSetPipeline(pass, scanBlocksPipeline_);
            wgpuComputePassEncoderDispatchWorkgroups(pass, 1, 1, 1);
            wgpuComputePassEncoderSetPipeline(pass, scatterActivePipeline_);
            wgpuComputePassEncoderDispatchWorkgroups(pass, blockCount_, 1, 1);
            wgpuComputePassEncoderEnd(pass);
            wgpuComputePassEncoderRelease(pass);
            writeStageTimestamp();

            if (terrainAttached_ && !ccd_.encode(
                    encoder, config_.fixedTickSeconds)) {
                LOG_WARN("Failed to encode GPU CCD pass");
            }
            writeStageTimestamp();

            pass = wgpuCommandEncoderBeginComputePass(encoder, &passDesc);
            wgpuComputePassEncoderSetPipeline(pass, preparePipeline_);
            wgpuComputePassEncoderSetBindGroup(
                pass, 0, integrateBindGroup_, 0, nullptr);
            wgpuComputePassEncoderDispatchWorkgroups(
                pass, (activeCapacity_ + kWorkgroupSize - 1u)
                    / kWorkgroupSize, 1, 1);
            wgpuComputePassEncoderEnd(pass);
            wgpuComputePassEncoderRelease(pass);
            writeStageTimestamp();

            const bool dynamicContactsEnabled =
                config_.enableBodyBodyContacts;
            const bool broadPhaseEncoded = !dynamicContactsEnabled
                || broadPhase_.encode(encoder);
            writeStageTimestamp();
            const bool narrowPhaseEncoded = broadPhaseEncoded
                && (!dynamicContactsEnabled || narrowPhase_.encode(encoder));
            writeStageTimestamp();
            if (dynamicContactsEnabled && narrowPhaseEncoded) {
                refreshContactInputs();
                // Narrow-phase manifolds ping-pong every tick. Keep event
                // packing on the just-produced buffer as well.
                eventReadback_.setSources({
                    .contactEvents = broadPhase_.contactEvents(),
                    .contactTelemetry = broadPhase_.telemetryBuffer(),
                    .contactCapacity = broadPhase_.contactCapacity(),
                    .islandEvents = islandManager_.events(),
                    .islandTelemetry = islandManager_.telemetryBuffer(),
                    .islandEventCapacity = bodyCapacity_,
                    .manifolds = narrowPhase_.manifolds(),
                    .narrowPhaseTelemetry = narrowPhase_.telemetryBuffer(),
                    .manifoldCapacity = manifoldCapacity_,
                });
            }
            // Even without body-body contacts, the dynamic solver owns pose
            // integration. Its contact count remains zero when broad and
            // narrow phase are disabled.
            const bool dynamicWorldEncoded = narrowPhaseEncoded
                && dynamicSolver_.encode(encoder);
            if (!dynamicWorldEncoded) {
                LOG_ERROR("Failed to encode a GPU dynamic-world stage");
            }
            writeStageTimestamp();

            if (terrainAttached_ || terrainStateNeedsClear_) {
                pass = wgpuCommandEncoderBeginComputePass(encoder, &passDesc);
                wgpuComputePassEncoderSetPipeline(pass, staticContactPipeline_);
                wgpuComputePassEncoderSetBindGroup(
                    pass, 0, integrateBindGroup_, 0, nullptr);
                wgpuComputePassEncoderDispatchWorkgroups(
                    pass, (activeCapacity_ + kWorkgroupSize - 1u)
                        / kWorkgroupSize, 1, 1);
                wgpuComputePassEncoderEnd(pass);
                wgpuComputePassEncoderRelease(pass);
                terrainStateNeedsClear_ = false;
            }
            writeStageTimestamp();

            if (!islandManager_.encode(encoder)) {
                LOG_ERROR("Failed to encode the GPU island stage");
            }
            writeStageTimestamp();

            pass = wgpuCommandEncoderBeginComputePass(encoder, &passDesc);
            wgpuComputePassEncoderSetBindGroup(
                pass, 0, integrateBindGroup_, 0, nullptr);
            wgpuComputePassEncoderSetPipeline(pass, advanceTickPipeline_);
            wgpuComputePassEncoderDispatchWorkgroups(pass, 1, 1, 1);
            wgpuComputePassEncoderEnd(pass);
            wgpuComputePassEncoderRelease(pass);

            if (eventReadbackEnabled_) {
                if (!eventReadback_.encodeReadback(
                        encoder, encodedTick_ + tick + 1u)) {
                    LOG_WARN("GPU physics event readback ring is full");
                } else {
                    lastGpuReadbackBytes_ += 16u
                        + uint64_t{eventCapacity_} * sizeof(GpuPhysicsEvent);
                }
            }
            writeStageTimestamp();
        }

        if (profileTickCount != 0u) {
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
            if (!telemetryReadback_.encodeCopy(
                    encoder, telemetrySnapshotBuffer_, 0u,
                    kTelemetrySnapshotBytes, finalTick, 0u,
                    kTelemetrySnapshotWordCount)) {
                LOG_WARN("GPU physics telemetry readback ring is full");
            } else {
                lastGpuReadbackBytes_ += kTelemetrySnapshotBytes;
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
        if (debugRequest_) {
            WGPUComputePassEncoder pass =
                wgpuCommandEncoderBeginComputePass(encoder, &passDesc);
            wgpuComputePassEncoderSetPipeline(pass, packDebugPipeline_);
            wgpuComputePassEncoderSetBindGroup(
                pass, 0, debugBindGroup_, 0, nullptr);
            wgpuComputePassEncoderDispatchWorkgroups(
                pass, (debugRequest_->bodyCount + kWorkgroupSize - 1u)
                    / kWorkgroupSize, 1, 1);
            wgpuComputePassEncoderEnd(pass);
            wgpuComputePassEncoderRelease(pass);
        }

        if (profileQueryCount != 0u) {
            wgpuCommandEncoderResolveQuerySet(
                encoder, stageQuerySet_, 0u, profileQueryCount,
                stageResolveBuffer_, 0u);
            if (!stageReadback_.encodeCopy(
                    encoder, stageResolveBuffer_, 0u,
                    uint64_t{profileQueryCount} * sizeof(uint64_t),
                    profileFirstTick, profileTickCount,
                    kStageBoundaryCount)) {
                LOG_WARN("GPU physics stage timing readback ring is full");
            } else {
                lastGpuReadbackBytes_ +=
                    uint64_t{profileQueryCount} * sizeof(uint64_t);
            }
        }

        encodedTick_ = finalTick;
        pendingTicks_ = 0;
        commands_.erase(std::remove_if(commands_.begin(), commands_.end(),
            [finalTick](const PhysicsCommand& command) {
                return command.targetTick <= finalTick;
            }), commands_.end());
        for (auto it = pendingFrees_.begin(); it != pendingFrees_.end();) {
            if (it->tick <= finalTick) {
                freeIndices_.insert(it->index);
                it = pendingFrees_.erase(it);
            } else {
                ++it;
            }
        }

        if (debugRequest_) {
            const size_t bytes = size_t{debugRequest_->bodyCount} * kGpuBodyBytes;
            if (!readbackRing_.encodeCopy(
                    encoder, debugPackedBuffer_, 0, bytes, encodedTick_,
                    debugRequest_->firstBody, debugRequest_->bodyCount)) {
                LOG_WARN("GPU physics debug readback ring is full");
            } else {
                lastGpuReadbackBytes_ += bytes;
            }
            debugRequest_.reset();
        }
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
            const uint32_t packedMetadata = body[6].w;
            const uint32_t flags =
                packedMetadata & ~kGpuBodyGenerationMask;
            DebugBodyState state;
            state.handle = {
                raw->firstBody + local,
                packedMetadata & kGpuBodyGenerationMask};
            glm::ivec4 signedMetadata;
            std::memcpy(&signedMetadata, &body[6], sizeof(signedMetadata));
            state.sector = glm::ivec3(signedMetadata);
            state.position = glm::vec3(position);
            state.orientation = glm::quat(
                orientation.w, orientation.x, orientation.y, orientation.z);
            state.linearVelocity = glm::vec3(linear);
            state.angularVelocity = glm::vec3(angular);
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
            result.bodies.push_back(state);
        }
        cachedDebugBodies_ = result.bodies;
        return result;
    }

    PhysicsRenderView view() const noexcept {
        return {
            .poseBuffer = poseBuffer_,
            .shapeBuffer = shapeBuffer_,
            .metadataBuffer = metadataBuffer_,
            .activeBodyIds = activeIdsBuffer_,
            .residentBodyCapacity = bodyCapacity_,
            .shapeCount = static_cast<uint32_t>(ThrowableShape::Count),
        };
    }

    PhysicsStats stats() const noexcept {
        PhysicsStats result;
        result.backend = BackendType::WebGpuSoft;
        result.arithmeticMode = PhysicsArithmeticMode::FastFloat;
        result.substeps = config_.substeps;
        result.residentBodies = residentBodies_;
        result.activeBodies = residentBodies_;
        result.bodyCapacity = bodyCapacity_;
        result.pairCapacity = pairCapacity_;
        result.contactCapacity = contactCapacity_;
        result.manifoldCapacity = manifoldCapacity_;
        result.queryCapacity = config_.asyncQueryCapacity;
        result.eventCapacity = eventCapacity_;
        result.workerConcurrency = 1;
        result.estimatedPersistentBytes = arena_.persistentBytes()
                                        + ownedTerrainBytes_;
        result.scratchBytes = arena_.scratchBytes()
                            + readbackRing_.allocatedBytes()
                            + telemetryReadback_.allocatedBytes()
                            + ccd_.allocatedBytes()
                            + broadPhase_.scratchBytes()
                            + narrowPhase_.scratchBytes()
                            + dynamicSolver_.scratchBytes()
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
            residentBodies_, bodyCapacity_,
            std::max(highResidentBodies_, residentBodies_),
            bodyCapacityOverflow_};
        result.activeBodyUsage = {
            residentBodies_, activeCapacity_, residentBodies_, false};
        result.commandUsage = {
            saturatingU32(commands_.size()), config_.commandCapacity,
            saturatingU32(commands_.size()), commandCapacityOverflow_};
        result.gridEntryUsage.capacity = broadPhase_.gridEntryCapacity();
        result.candidatePairUsage.capacity =
            broadPhase_.candidatePairCapacity();
        result.uniquePairUsage.capacity = pairCapacity_;
        result.contactUsage.capacity = contactCapacity_;
        result.manifoldUsage.capacity = manifoldCapacity_;
        result.terrainContactUsage.capacity = terrainContactCapacity;
        result.overflowConstraintUsage.capacity = contactCapacity_;
        result.eventUsage.capacity = eventCapacity_;
        result.visibleBodyUsage.capacity = bodyCapacity_;
        result.sleepingGridUsage.capacity = bodyCapacity_;
        result.bulletUsage.capacity = ccdBulletCapacity_;
        result.waterSampleUsage.capacity = waterSampleCapacity;
        if (!cachedTelemetry_.valid) return result;

        const auto& core = cachedTelemetry_.core;
        const auto& ccd = cachedTelemetry_.ccd;
        const auto& broad = cachedTelemetry_.broad;
        const auto& narrow = cachedTelemetry_.narrow;
        const auto& solver = cachedTelemetry_.solver;
        const auto& islands = cachedTelemetry_.islands;
        result.telemetryTick = cachedTelemetry_.tick;
        result.activeBodies = islands.awakeBodies;
        result.sleepingBodies = islands.sleepingBodies;
        result.activeBodyUsage = {
            core.activeBodies, activeCapacity_, core.highActiveBodies,
            core.activeOverflow};
        result.commandUsage = {
            core.commands, config_.commandCapacity, core.highCommands,
            commandCapacityOverflow_};
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
                          + islands.events),
            eventCapacity_,
            saturatingU32(uint64_t{broad.highEvents} + islands.highEvents),
            broad.eventOverflow || islands.eventOverflow};
        result.sleepingGridUsage = {
            islands.sleepingGridEntries, bodyCapacity_,
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
        result.activeSleepingPairs = broad.activeSleepingPairs;
        result.oversizedBodies = broad.oversizedBodies;
        result.persistentContacts = broad.persistentContacts;
        result.manifoldPoints = narrow.manifoldPoints;
        result.speculativeManifolds = narrow.speculativeManifolds;
        result.invalidManifolds = narrow.invalidManifolds;
        result.terrainContactBodies = core.terrainContactBodies;
        result.maximumTerrainContactsPerBody =
            core.maximumTerrainContactsPerBody;
        result.submergedBodies = core.submergedBodies;
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
    };

    bool initialized_ = false;
    bool terrainAttached_ = false;
    bool terrainStateNeedsClear_ = false;
    bool waterEnabled_ = false;
    float waterHeight_ = 0.0f;
    uint32_t terrainWidth_ = 0;
    uint32_t terrainHeight_ = 0;
    uint32_t terrainMipLevelCount_ = 0;
    uint32_t externalTerrainMipLevelCount_ = 0;
    float terrainHeightScale_ = 0.0f;
    float terrainCellScale_ = 0.0f;
    size_t ownedTerrainBytes_ = 0;
    WGPUDevice device_ = nullptr;
    WGPUQueue queue_ = nullptr;
    PhysicsInitContext::GpuConfig config_{};
    WGPULimits deviceLimits_{};
    uint32_t bodyCapacity_ = 0;
    uint32_t activeCapacity_ = 0;
    uint32_t pairCapacity_ = 0;
    uint32_t contactCapacity_ = 0;
    uint32_t manifoldCapacity_ = 0;
    uint32_t eventCapacity_ = 0;
    uint32_t ccdBulletCapacity_ = 0;
    uint32_t blockCount_ = 0;
    uint32_t nextUnusedIndex_ = 1;
    uint32_t residentBodies_ = 0;
    uint32_t highResidentBodies_ = 0;
    bool bodyCapacityOverflow_ = false;
    bool commandCapacityOverflow_ = false;
    uint64_t lastGpuUploadBytes_ = 0;
    uint64_t lastGpuReadbackBytes_ = 0;
    double accumulator_ = 0.0;
    uint32_t pendingTicks_ = 0;
    uint64_t encodedTick_ = 0;
    uint64_t nextSequence_ = 1;
    bool queryPending_ = false;
    uint32_t pendingQueryCount_ = 0;
    bool eventReadbackEnabled_ = false;
    bool stageProfilingEnabled_ = false;
    uint32_t stageQueryCapacity_ = 0u;
    WaterSurfaceSampler waterSampler_;
    PhysicsStepStats lastStepStats_{};
    CachedTelemetry cachedTelemetry_{};
    DynamicBodyReadStats lastReadStats_{};
    std::vector<uint32_t> generations_;
    std::vector<bool> hostAlive_;
    std::set<uint32_t> freeIndices_;
    std::vector<PendingFree> pendingFrees_;
    std::vector<PhysicsCommand> commands_;
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
    GpuIslandManager islandManager_;
    GpuAsyncQuerySystem querySystem_;
    GpuEventReadbackRing eventReadback_;
    std::deque<PhysicsGpuStageTiming> stageTimingResults_;
    WGPUQuerySet stageQuerySet_ = nullptr;
    WGPUBuffer stageResolveBuffer_ = nullptr;
    WGPUBuffer telemetrySnapshotBuffer_ = nullptr;
    WGPUBuffer poseBuffer_ = nullptr;
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
    WGPUShaderModule shaderModule_ = nullptr;
    WGPUBindGroupLayout commandLayout_ = nullptr;
    WGPUBindGroupLayout compactLayout_ = nullptr;
    WGPUBindGroupLayout integrateLayout_ = nullptr;
    WGPUBindGroupLayout debugLayout_ = nullptr;
    WGPUPipelineLayout commandPipelineLayout_ = nullptr;
    WGPUPipelineLayout compactPipelineLayout_ = nullptr;
    WGPUPipelineLayout integratePipelineLayout_ = nullptr;
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
    };
}
bool GpuPhysicsBackend::setTerrain(std::span<const uint16_t> samples,
                                   uint32_t width, uint32_t height,
                                   float heightScale, float cellScale) {
    return impl_->setTerrain(samples, width, height, heightScale, cellScale);
}
void GpuPhysicsBackend::setTerrainGpuResources(
    const TerrainGpuResources& resources) {
    impl_->setTerrainGpuResources(resources);
}
void GpuPhysicsBackend::clearTerrain() { impl_->clearTerrain(); }
bool GpuPhysicsBackend::hasTerrain() const noexcept { return impl_->terrainAttached_; }
void GpuPhysicsBackend::setWaterPlane(float height, bool enabled) {
    impl_->waterHeight_ = height;
    impl_->waterEnabled_ = enabled;
}
void GpuPhysicsBackend::setWaterSurfaceSampler(WaterSurfaceSampler sampler) {
    impl_->waterSampler_ = std::move(sampler);
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
void GpuPhysicsBackend::stepCpu(float deltaTime) { impl_->schedule(deltaTime); }
void GpuPhysicsBackend::encodeGpuStep(WGPUCommandEncoder encoder) {
    impl_->encode(encoder);
}
bool GpuPhysicsBackend::submitQueries(
    std::span<const PhysicsQueryRequest> requests, uint64_t tick) {
    return impl_->submitQueries(requests, tick);
}
std::optional<PhysicsQueryBatch> GpuPhysicsBackend::pollQueryResults() {
    return impl_->pollQueryResults();
}
void GpuPhysicsBackend::setEventReadbackEnabled(bool enabled) {
    impl_->setEventReadbackEnabled(enabled);
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
                          throwableShapeDimensions(body.shape), body.awake,
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

} // namespace voxy::physics
