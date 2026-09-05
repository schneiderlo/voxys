// ═══════════════════════════════════════════════════════════════════════════════
// application.cpp - Main Application Shell Implementation
// ═══════════════════════════════════════════════════════════════════════════════

// NOTE: Include terrain/heightmap.hpp BEFORE any headers that might include X11
// because X11 defines "None" as a macro which conflicts with HeightmapError::None
#include "terrain/heightmap.hpp"

#include "app/application.hpp"
#include "engine/platform/window.hpp"
#include "engine/platform/input.hpp"

// Xlib exports `None` as a macro. Do not let that platform detail rewrite
// scoped Wreckwater enum values in headers included below.
#if defined(None)
    #undef None
#endif

#include "app/debug_overlay.hpp"
#include "camera/camera.hpp"
#include "camera/controller.hpp"
#include "camera/character_controller.hpp"
#include "client/wreckwater_application_client.hpp"
#include "core/timer.hpp"
#include "perf/benchmark.hpp"
#include "core/log.hpp"
#include "core/timer.hpp"
#include "gpu/context.hpp"
#include "gpu/resources.hpp"
#include "moto/race.hpp"
#include "moto/session.hpp"
#include "moto/world.hpp"
#include "physics/terrain_topology.hpp"
#include "terrain/authored_cove.hpp"
#include "terrain/textures.hpp"
#include "terrain/shadow_bake.hpp"
#include "render/triangle_path.hpp"
#include "render/raycast_path.hpp"
#include "render/blit_path.hpp"
#include "render/water_simulation.hpp"
#include "render/primitive_path.hpp"
#include "render/primitive_culling.hpp"
#include "render/mesh_path.hpp"
#include "physics/physics_world.hpp"

#if !defined(VOXY_WASM)
    #include "network/native_tcp_transport.hpp"
#endif

#include <chrono>
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <limits>
#include <numbers>
#include <span>
#include <system_error>
#include <vector>

#if defined(VOXY_NATIVE)
    #include <stb_image_write.h>
    // Platform-specific sleep includes for async polling
    #if defined(_WIN32)
        #include <windows.h>
    #else
        #include <unistd.h>
    #endif
    // wgpu-native extras (not in the core WebGPU header)
    #ifndef WGPUWrappedSubmissionIndex
    using WGPUSubmissionIndex = uint64_t;
    typedef struct WGPUWrappedSubmissionIndex {
        WGPUQueue queue;
        WGPUSubmissionIndex submissionIndex;
    } WGPUWrappedSubmissionIndex;
    #endif
    extern "C" WGPUSubmissionIndex wgpuQueueSubmitForIndex(WGPUQueue queue, size_t commandCount, const WGPUCommandBuffer* commands);
    extern "C" WGPUBool wgpuDevicePoll(WGPUDevice device, WGPUBool wait, const WGPUWrappedSubmissionIndex* wrappedSubmissionIndex);
#endif

#if defined(VOXY_WASM)
    #include <emscripten.h>
    #include <emscripten/html5.h>
#endif

namespace voxy {

struct WreckwaterApplicationClientState {
    client::WreckwaterApplicationClient client;
    client::WreckwaterCameraObstructionProbeRequest
        cameraProbe{};
    client::WreckwaterCameraTerrainCastResult
        terrainProbe{};
    bool cameraProbeSubmitted = false;
#if !defined(VOXY_WASM)
    network::NativeTcpClientTransport* transport = nullptr;
    network::NativeTcpClientState observedTransportState =
        network::NativeTcpClientState::Disconnected;
#endif
    client::WreckwaterGraphicalClientFrameStatus observedFrameStatus =
        client::WreckwaterGraphicalClientFrameStatus::NotInitialized;
    network::WreckwaterClientRuntimeError observedRuntimeError =
        network::WreckwaterClientRuntimeError::None;
};

namespace {

enum RendererSettingsDirty : uint32_t {
    RendererUniformsDirty = 1u << 0u,
    RendererCameraDirty = 1u << 1u,
    RendererWaterPhysicsDirty = 1u << 2u,
    RendererWaterCoastDirty = 1u << 3u,
    RendererWaterSpectrumDirty = 1u << 4u,
    RendererSunShadowDirty = 1u << 5u,
};

constexpr float kDegreesToRadians = std::numbers::pi_v<float> / 180.0f;
constexpr float kRadiansToDegrees = 180.0f / std::numbers::pi_v<float>;
constexpr uint32_t kPortableMaximumTextureDimension2D = 8'192u;
constexpr float kBrowserJourneyTargetX = 250.0f;
constexpr float kBrowserJourneyTargetZ = 0.0f;
constexpr float kCubeTriangleCubeSize = 1.1f;
constexpr float kCubeTriangleHorizontalPitch =
    kCubeTriangleCubeSize + 0.16f;
constexpr uint32_t kCubeTriangleImpactWakeRadiusColumns = 2u;
constexpr uint16_t kLocalMotoRacePlayer = 0u;

bool flattenCubeTriangleArena(
    terrain::Heightmap& heightmap, float cellScale) {
    if (!heightmap.isLoaded() || !std::isfinite(cellScale)
        || cellScale <= 0.0f) {
        return false;
    }

    // The 141-row wall is roughly 316 m wide. Flatten enough land to contain
    // its base and the starting camera, while sampling the target height only
    // near the wall so distant terrain cannot bias the platform elevation.
    constexpr float innerHalfWidth = 180.0f;
    constexpr float innerHalfDepth = 520.0f;
    constexpr float heightSampleHalfWidth = 160.0f;
    constexpr float heightSampleHalfDepth = 10.0f;
    constexpr float feather = 40.0f;
    const uint32_t width = heightmap.getWidth();
    const uint32_t height = heightmap.getHeight();
    const auto sampleCoordinate = [cellScale](
        float world, uint32_t sampleCount) {
        return static_cast<int32_t>(std::lround(
            world / cellScale
            + 0.5f * static_cast<float>(sampleCount - 1u)));
    };
    const int32_t centerX = sampleCoordinate(
        kBrowserJourneyTargetX, width);
    const int32_t centerZ = sampleCoordinate(
        kBrowserJourneyTargetZ, height);
    const int32_t outerSamplesX =
        static_cast<int32_t>(std::ceil(
            (innerHalfWidth + feather) / cellScale));
    const int32_t outerSamplesZ =
        static_cast<int32_t>(std::ceil(
            (innerHalfDepth + feather) / cellScale));

    const auto clampX = [width](int32_t value) {
        return std::clamp(value, 0, static_cast<int32_t>(width) - 1);
    };
    const auto clampZ = [height](int32_t value) {
        return std::clamp(value, 0, static_cast<int32_t>(height) - 1);
    };
    const int32_t heightSamplesX =
        static_cast<int32_t>(std::ceil(
            heightSampleHalfWidth / cellScale));
    const int32_t heightSamplesZ =
        static_cast<int32_t>(std::ceil(
            heightSampleHalfDepth / cellScale));
    const int32_t sampleMinX = clampX(centerX - heightSamplesX);
    const int32_t sampleMaxX = clampX(centerX + heightSamplesX);
    const int32_t sampleMinZ = clampZ(centerZ - heightSamplesZ);
    const int32_t sampleMaxZ = clampZ(centerZ + heightSamplesZ);

    uint64_t sum = 0u;
    uint64_t samples = 0u;
    const std::span<const uint16_t> source = heightmap.getData();
    for (int32_t z = sampleMinZ; z <= sampleMaxZ; ++z) {
        for (int32_t x = sampleMinX; x <= sampleMaxX; ++x) {
            sum += source[static_cast<size_t>(z) * width
                          + static_cast<uint32_t>(x)];
            ++samples;
        }
    }
    if (samples == 0u) return false;
    const uint16_t arenaHeight = static_cast<uint16_t>(
        std::min<uint64_t>((sum + samples / 2u) / samples, 65'535u));

    std::span<uint16_t> data = heightmap.getMutableData();
    const int32_t outerMinX = clampX(centerX - outerSamplesX);
    const int32_t outerMaxX = clampX(centerX + outerSamplesX);
    const int32_t outerMinZ = clampZ(centerZ - outerSamplesZ);
    const int32_t outerMaxZ = clampZ(centerZ + outerSamplesZ);
    for (int32_t z = outerMinZ; z <= outerMaxZ; ++z) {
        const float distanceZ =
            std::abs(static_cast<float>(z - centerZ)) * cellScale;
        const float edgeZ =
            (distanceZ - innerHalfDepth) / feather;
        for (int32_t x = outerMinX; x <= outerMaxX; ++x) {
            const float distanceX =
                std::abs(static_cast<float>(x - centerX)) * cellScale;
            const float edgeX =
                (distanceX - innerHalfWidth) / feather;
            const float edge = std::clamp(
                std::max(edgeX, edgeZ), 0.0f, 1.0f);
            const float smoothEdge = edge * edge * (3.0f - 2.0f * edge);
            const size_t index = static_cast<size_t>(z) * width
                               + static_cast<uint32_t>(x);
            data[index] = static_cast<uint16_t>(std::lround(
                static_cast<float>(arenaHeight) * (1.0f - smoothEdge)
                + static_cast<float>(data[index]) * smoothEdge));
        }
    }

    LOG_INFO(
        "Flattened cube-triangle arena: {:.0f}x{:.0f} m with {:.0f} m feather",
        innerHalfWidth * 2.0f, innerHalfDepth * 2.0f, feather);
    return true;
}

void setCameraWorldPose(
    Camera& camera, const glm::dvec3& absolutePosition,
    const glm::dvec3& absoluteTarget) {
    const physics::WorldPosition position =
        physics::worldPositionFromAbsolute(absolutePosition);
    const glm::dvec3 sectorOrigin =
        glm::dvec3(position.sector) *
        static_cast<double>(physics::kWorldSectorSize);
    camera.setWorldPosition(position.sector, position.local);
    camera.lookAt(glm::vec3(absoluteTarget - sectorOrigin));
}

[[nodiscard]] float finiteClamp(double value, float minimum, float maximum) {
    if (!std::isfinite(value)) return minimum;
    return std::clamp(static_cast<float>(value), minimum, maximum);
}

[[nodiscard]] float sunAzimuthDegrees(const glm::vec3& direction) {
    return std::atan2(direction.z, direction.x) * kRadiansToDegrees;
}

[[nodiscard]] float sunElevationDegrees(const glm::vec3& direction) {
    const glm::vec3 normalized = glm::dot(direction, direction) > 1.0e-10f
        ? glm::normalize(direction) : glm::vec3(0.0f, 1.0f, 0.0f);
    return std::asin(std::clamp(normalized.y, -1.0f, 1.0f))
         * kRadiansToDegrees;
}

[[nodiscard]] glm::vec3 sunDirectionFromDegrees(float azimuth,
                                                float elevation) {
    const float azimuthRadians = azimuth * kDegreesToRadians;
    const float elevationRadians = elevation * kDegreesToRadians;
    const float horizontal = std::cos(elevationRadians);
    return glm::normalize(glm::vec3(
        horizontal * std::cos(azimuthRadians),
        std::sin(elevationRadians),
        horizontal * std::sin(azimuthRadians)));
}

[[nodiscard]] render::WaterSpectrumConfig makeWaterSpectrumConfig(
    const WaterSpectrumSettings& settings) {
    return render::WaterSpectrumConfig{
        .significantWaveHeight = settings.significantWaveHeight,
        .directionRadians = settings.directionDegrees * kDegreesToRadians,
        .choppiness = settings.choppiness,
        .peakEnhancement = settings.peakEnhancement,
        .windAlignment = settings.windAlignment,
        .animationSpeed = settings.animationSpeed,
        .patchLengths = settings.patchLengths,
        .cascadeAmplitudes = settings.cascadeAmplitudes,
        .directionalSineScale = settings.directionalSineScale,
    };
}

[[nodiscard]] WaterSpectrumSettings makeWaterSpectrumSettings(
    const render::WaterSpectrumConfig& config) {
    return WaterSpectrumSettings{
        .significantWaveHeight = config.significantWaveHeight,
        .directionDegrees = config.directionRadians * kRadiansToDegrees,
        .choppiness = config.choppiness,
        .peakEnhancement = config.peakEnhancement,
        .windAlignment = config.windAlignment,
        .animationSpeed = config.animationSpeed,
        .patchLengths = config.patchLengths,
        .cascadeAmplitudes = config.cascadeAmplitudes,
        .directionalSineScale = config.directionalSineScale,
    };
}

[[nodiscard]] bool finiteVector(const glm::vec2& value) noexcept {
    return std::isfinite(value.x) && std::isfinite(value.y);
}

[[nodiscard]] bool finiteVector(const glm::vec3& value) noexcept {
    return std::isfinite(value.x) && std::isfinite(value.y)
        && std::isfinite(value.z);
}

[[nodiscard]] bool validWreckwaterApplicationClientConfig(
    const ApplicationConfig& application) noexcept {
    if (!application.wreckwaterClient) return true;
#if defined(VOXY_WASM)
    return false;
#else
    const auto& config = *application.wreckwaterClient;
    if (config.serverAddress.empty()
        || config.serverAddress.size() > 255u
        || config.serverPort == 0u
        || config.peerId == 0u || config.peerId > 4u
        || config.sessionId == 0u || config.matchId == 0u
        || config.worldId == 0u || config.worldEpoch == 0u
        || config.authorityEpoch == 0u
        || config.inputLeadTicks == 0u
        || config.inputLeadTicks > 16u
        || config.interpolationDelayTicks
            > client::
                kWreckwaterClientVisualClockMaximumInterpolationDelayTicks) {
        return false;
    }
    for (const char character : config.serverAddress) {
        const auto value =
            static_cast<unsigned char>(character);
        if (value <= 0x20u || value == 0x7fu) return false;
    }
    bool anyKeyByte = false;
    bool anyDigestByte = false;
    for (const std::byte value : config.authenticationKey) {
        anyKeyByte = anyKeyByte || value != std::byte{};
    }
    for (const std::byte value : config.expectedContentDigest) {
        anyDigestByte = anyDigestByte || value != std::byte{};
    }
    const bool automationDisabled =
        !application.benchmarkOnStartup
        && !application.exitAfterBenchmark
        && application.benchmarkBodyCount == 0u
        && application.cubePyramidBodyCount == 0u
        && !application.initialTeleportIndex
        && !application.screenshotPath
        && application.screenshotTourIndices.empty();
    return anyKeyByte && anyDigestByte && automationDisabled;
#endif
}

#if !defined(VOXY_WASM)
[[nodiscard]] WreckwaterApplicationConnectionState
wreckwaterApplicationConnectionState(
    network::NativeTcpClientState state) noexcept {
    switch (state) {
        case network::NativeTcpClientState::Disconnected:
        case network::NativeTcpClientState::Connecting:
            return WreckwaterApplicationConnectionState::Connecting;
        case network::NativeTcpClientState::Authenticating:
            return WreckwaterApplicationConnectionState::Authenticating;
        case network::NativeTcpClientState::Connected:
            return WreckwaterApplicationConnectionState::Connected;
        case network::NativeTcpClientState::Failed:
            return WreckwaterApplicationConnectionState::Failed;
    }
    return WreckwaterApplicationConnectionState::Failed;
}

[[nodiscard]] const char* nativeTcpClientStateName(
    network::NativeTcpClientState state) noexcept {
    switch (state) {
        case network::NativeTcpClientState::Disconnected:
            return "disconnected";
        case network::NativeTcpClientState::Connecting:
            return "connecting";
        case network::NativeTcpClientState::Authenticating:
            return "authenticating";
        case network::NativeTcpClientState::Connected:
            return "connected";
        case network::NativeTcpClientState::Failed:
            return "failed";
    }
    return "unknown";
}
#endif

[[nodiscard]] bool validApplicationConfig(
    const ApplicationConfig& config) noexcept {
    const bool validRenderPath = config.renderPath == RenderPath::Triangle
                              || config.renderPath == RenderPath::Raycast;
    const bool validPhysicsBackend =
        config.physicsBackend == physics::BackendType::JoltLegacy
        || config.physicsBackend == physics::BackendType::Box3DReference
        || config.physicsBackend == physics::BackendType::WebGpuSoft;
    const bool validScheduler =
        config.joltJobSystem == physics::JoltJobSystemMode::SingleThreaded
        || config.joltJobSystem == physics::JoltJobSystemMode::ThreadPool;
    const auto& spectrum = config.waterSpectrum;
    const uint64_t heightSamples =
        static_cast<uint64_t>(config.heightmapWidth)
        * config.heightmapHeight;
    const float cellsPerSector =
        256.0f / config.gpuPhysicsBroadPhaseCellSize;
    const float roundedCellsPerSector = std::round(cellsPerSector);
    const double scaledWindowWidth =
        static_cast<double>(config.windowWidth)
        * static_cast<double>(config.resolutionScale);
    const double scaledWindowHeight =
        static_cast<double>(config.windowHeight)
        * static_cast<double>(config.resolutionScale);
    const bool validGpuCellSize =
        std::isfinite(config.gpuPhysicsBroadPhaseCellSize)
        && config.gpuPhysicsBroadPhaseCellSize > 0.0f
        && std::isfinite(cellsPerSector)
        && roundedCellsPerSector >= 1.0f
        && roundedCellsPerSector <= 2'097'152.0f
        && std::abs(cellsPerSector - roundedCellsPerSector) <= 1.0e-5f;
    constexpr uint32_t kCpuBenchmarkBodyCapacity = 16'384u;
    const uint32_t benchmarkBodyCapacity =
        config.physicsBackend == physics::BackendType::WebGpuSoft
        ? config.gpuPhysicsMaxBodies
        : kCpuBenchmarkBodyCapacity;
    const uint64_t startupBodyCount =
        uint64_t{config.benchmarkBodyCount}
        + config.cubePyramidBodyCount;
    return validRenderPath && validPhysicsBackend && validScheduler
        && validWreckwaterApplicationClientConfig(config)
        && config.windowWidth > 0 && config.windowHeight > 0
        && config.heightmapWidth > 0u && config.heightmapHeight > 0u
        && config.heightmapWidth <= kPortableMaximumTextureDimension2D
        && config.heightmapHeight <= kPortableMaximumTextureDimension2D
        && heightSamples
            <= std::numeric_limits<size_t>::max() / sizeof(uint16_t)
        && std::isfinite(config.resolutionScale)
        && config.resolutionScale >= 0.25f
        && config.resolutionScale <= 2.0f
        && scaledWindowWidth
            <= static_cast<double>(kPortableMaximumTextureDimension2D)
        && scaledWindowHeight
            <= static_cast<double>(kPortableMaximumTextureDimension2D)
        && std::isfinite(config.heightScale) && config.heightScale > 0.0f
        && config.heightScale <= 1.0e6f
        && std::isfinite(config.cellScale) && config.cellScale > 0.0f
        && config.cellScale <= 1.0e6f
        && finiteVector(config.sunDirection)
        && finiteVector(config.sunColor)
        && finiteVector(config.ambientColor)
        && std::isfinite(config.ambientIntensity)
        && std::isfinite(config.fogDensity)
        && finiteVector(config.fogColor)
        && std::isfinite(config.waterHeight)
        && finiteVector(config.waterShallowColor)
        && finiteVector(config.waterDeepColor)
        && std::isfinite(config.waterRoughness)
        && std::isfinite(config.waterWaveStrength)
        && std::isfinite(config.waterReflectionStrength)
        && std::isfinite(config.waterShoreFade)
        && config.waterShoreFade > 0.0f
        && std::isfinite(spectrum.significantWaveHeight)
        && std::isfinite(spectrum.directionDegrees)
        && std::isfinite(spectrum.choppiness)
        && std::isfinite(spectrum.peakEnhancement)
        && std::isfinite(spectrum.windAlignment)
        && std::isfinite(spectrum.animationSpeed)
        && finiteVector(spectrum.patchLengths)
        && spectrum.patchLengths.x > 0.0f
        && spectrum.patchLengths.y > 0.0f
        && finiteVector(spectrum.cascadeAmplitudes)
        && std::isfinite(spectrum.directionalSineScale)
        && config.gpuPhysicsMaxBodies > 0u
        && config.gpuPhysicsMaxBodies
            != std::numeric_limits<uint32_t>::max()
        && config.gpuPhysicsMaxPairs > 0u
        && config.gpuPhysicsMaxCandidatePairs >= config.gpuPhysicsMaxPairs
        && (config.gpuPhysicsSolverWorkgroupSize == 128u
            || config.gpuPhysicsSolverWorkgroupSize == 256u)
        && config.benchmarkBodyCount <= kMaximumBenchmarkBodyCount
        && config.cubePyramidBodyCount <= kMaximumBenchmarkBodyCount
        && (config.benchmarkBodyCount == 0u
            || config.cubePyramidBodyCount == 0u)
        && startupBodyCount <= benchmarkBodyCapacity
        && validGpuCellSize
        && config.gpuPhysicsMaximumCatchUpTicks > 0u
        && config.gpuPhysicsMaximumCatchUpTicks <= 1'024u
        && std::isfinite(config.gpuPhysicsTimestampPeriodNanoseconds)
        && config.gpuPhysicsTimestampPeriodNanoseconds > 0.0
        && config.box3dWorkerThreads > 0u
        && finiteVector(config.cameraStartPos)
        && std::isfinite(config.cameraFovDegrees)
        && config.cameraFovDegrees > 0.0f
        && config.cameraFovDegrees < 180.0f
        && std::isfinite(config.cameraNear) && config.cameraNear > 0.0f
        && std::isfinite(config.cameraFar)
        && config.cameraFar > config.cameraNear
        && std::isfinite(config.cameraMoveSpeed)
        && config.cameraMoveSpeed >= 0.0f
        && config.cameraMoveSpeed <= 1'000.0f
        && std::isfinite(config.cameraMouseSensitivity)
        && config.cameraMouseSensitivity >= 0.0f
        && config.cameraMouseSensitivity <= 0.02f
        && std::isfinite(config.cameraEyeHeight)
        && config.cameraEyeHeight > 0.0f
        && config.cameraEyeHeight <= 10.0f
        && std::isfinite(config.fpsLogIntervalSeconds)
        && config.fpsLogIntervalSeconds > 0.0f
        && std::isfinite(config.benchmarkMinimumFps)
        && config.benchmarkMinimumFps >= 0.0
        && std::isfinite(config.benchmarkFixedDeltaSeconds)
        && config.benchmarkFixedDeltaSeconds >= 0.0f
        && config.screenshotFrameDelay >= 0
        && config.screenshotTourIndices.size() <= 256u;
}

[[nodiscard]] uint32_t scaledRenderExtent(uint32_t extent,
                                          float scale) noexcept {
    const double scaled = std::round(
        static_cast<double>(extent) * static_cast<double>(scale));
    return static_cast<uint32_t>(std::clamp(
        scaled, 1.0,
        static_cast<double>(kPortableMaximumTextureDimension2D)));
}

#if defined(VOXY_NATIVE)
struct ScreenshotMapState {
    std::atomic<uint32_t> references{2u};
    std::atomic<bool> done{false};
    std::atomic<bool> succeeded{false};
};

void releaseScreenshotMapState(ScreenshotMapState* state) noexcept {
    if (state->references.fetch_sub(1u, std::memory_order_acq_rel) == 1u) {
        delete state;
    }
}

void onScreenshotBufferMapped(WGPUBufferMapAsyncStatus status,
                              void* userdata) {
    auto* state = static_cast<ScreenshotMapState*>(userdata);
    state->succeeded.store(
        status == WGPUBufferMapAsyncStatus_Success,
        std::memory_order_relaxed);
    state->done.store(true, std::memory_order_release);
    if (status != WGPUBufferMapAsyncStatus_Success) {
        LOG_ERROR("Failed to map screenshot buffer: status={}",
                  static_cast<int>(status));
    }
    releaseScreenshotMapState(state);
}
#endif

} // namespace

constexpr size_t kPrimitiveOverlayHeadroom = 1u + 5u * 7u;
constexpr uint32_t kRenderGpuQueriesPerStage = 2u;
constexpr uint32_t kRenderGpuTimestampCount =
    static_cast<uint32_t>(kRenderGpuStageCount)
        * kRenderGpuQueriesPerStage;
constexpr uint32_t kRenderGpuProfilingIntervalFrames = 30u;

void appendObjectCount(
    std::vector<physics::PhysicsWorld::DynamicBodySnapshot>& instances,
    size_t objectCount, const glm::vec3& planeCenter,
    const glm::vec3& cameraRight, const glm::vec3& cameraUp,
    const glm::quat& cameraRotation, float halfWidth, float halfHeight) {
    // Segment bits: top, upper-right, lower-right, bottom,
    // lower-left, upper-left, middle.
    constexpr std::array<uint8_t, 10> digitMasks = {
        0x3f, 0x06, 0x5b, 0x4f, 0x66,
        0x6d, 0x7d, 0x07, 0x7f, 0x6f,
    };

    const std::string digits = std::to_string(objectCount);
    const float screenScale = std::min(halfWidth, halfHeight);
    const float digitHeight = screenScale * 0.18f;
    const float digitWidth = digitHeight * 0.52f;
    const float thickness = digitHeight * 0.11f;
    const float verticalLength = digitHeight * 0.39f;
    const float gap = thickness * 1.4f;
    const float advance = digitWidth + gap;
    const float totalWidth = digitWidth * static_cast<float>(digits.size())
                           + gap * static_cast<float>(digits.size() - 1);
    const float leftEdge = halfWidth * 0.90f - totalWidth;
    const float centerY = -halfHeight * 0.38f;

    const auto addSegment = [&](float centerX, float segmentY,
                                float width, float height) {
        instances.push_back({
            physics::PhysicsWorld::ThrowableShape::Cube,
            planeCenter + cameraRight * centerX + cameraUp * segmentY,
            cameraRotation,
            glm::vec3(width, height, thickness * 0.35f)});
    };

    for (size_t digitIndex = 0; digitIndex < digits.size(); ++digitIndex) {
        const uint8_t mask = digitMasks[static_cast<size_t>(digits[digitIndex] - '0')];
        const float centerX = leftEdge + digitWidth * 0.5f
                            + static_cast<float>(digitIndex) * advance;
        const float xSide = digitWidth * 0.5f;
        const float ySide = digitHeight * 0.25f;
        const float yEdge = digitHeight * 0.5f;

        if (mask & (1u << 0u)) addSegment(centerX, centerY + yEdge, digitWidth, thickness);
        if (mask & (1u << 1u)) addSegment(centerX + xSide, centerY + ySide, thickness, verticalLength);
        if (mask & (1u << 2u)) addSegment(centerX + xSide, centerY - ySide, thickness, verticalLength);
        if (mask & (1u << 3u)) addSegment(centerX, centerY - yEdge, digitWidth, thickness);
        if (mask & (1u << 4u)) addSegment(centerX - xSide, centerY - ySide, thickness, verticalLength);
        if (mask & (1u << 5u)) addSegment(centerX - xSide, centerY + ySide, thickness, verticalLength);
        if (mask & (1u << 6u)) addSegment(centerX, centerY, digitWidth, thickness);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Utility Functions
// ─────────────────────────────────────────────────────────────────────────────

const char* renderPathToString(RenderPath path) noexcept {
    switch (path) {
        case RenderPath::Triangle: return "triangle";
        case RenderPath::Raycast:  return "raycast";
    }
    return "unknown";
}

const char* debugVisModeToString(DebugVisMode mode) noexcept {
    switch (mode) {
        case DebugVisMode::Off:       return "off";
        case DebugVisMode::Depth:     return "depth";
        case DebugVisMode::Normals:   return "normals";
        case DebugVisMode::MipLevels: return "mip_levels";
    }
    return "unknown";
}

const char* controllerModeToString(ControllerMode mode) noexcept {
    switch (mode) {
        case ControllerMode::FreeFly:   return "free-fly";
        case ControllerMode::Character: return "character";
    }
    return "unknown";
}

// ─────────────────────────────────────────────────────────────────────────────
// WASM Global State (for Emscripten main loop)
// ─────────────────────────────────────────────────────────────────────────────

// ─────────────────────────────────────────────────────────────────────────────
// Constructor / Destructor
// ─────────────────────────────────────────────────────────────────────────────

Application::Application() = default;

Application::~Application() {
    shutdown();
}

// ─────────────────────────────────────────────────────────────────────────────
// Lifecycle
// ─────────────────────────────────────────────────────────────────────────────

bool Application::initWreckwaterClient() {
    wreckwaterClientState_.reset();
    wreckwaterClientTelemetry_ = {};
    if (!config_.wreckwaterClient) return true;
#if defined(VOXY_WASM)
    LOG_ERROR(
        "WRECKWATER native TCP bootstrap is unavailable on WebAssembly");
    return false;
#else
    const auto& bootstrap = *config_.wreckwaterClient;
    const client::WreckwaterThirdPersonCameraRig::Config
        cameraConfig;
    const client::WreckwaterCameraTerrainView terrainView{
        .samples = heightmap_ != nullptr
            ? heightmap_->getData()
            : std::span<const uint16_t>{},
        .width = heightmap_ != nullptr
            ? heightmap_->getWidth() : 0u,
        .height = heightmap_ != nullptr
            ? heightmap_->getHeight() : 0u,
        .heightScale = config_.heightScale,
        .cellScale = config_.cellScale,
    };
    constexpr float maximumCameraProbePath = 16.0f;
    const float cameraProbeRadius =
        cameraConfig.obstructionProbeRadius
        + cameraConfig.maximumProbeEndpointDrift;
    if (!physicsWorld_
        || !physicsWorld_->capabilities().asynchronousQueries
        || !client::wreckwaterCameraTerrainViewSupports(
            terrainView, maximumCameraProbePath,
            cameraProbeRadius)) {
        LOG_ERROR(
            "WRECKWATER camera requires async rigid-body queries "
            "and bounded CPU heightfield probe coverage");
        return false;
    }

    network::NativeTcpClientConfig transportConfig;
    transportConfig.serverAddress = bootstrap.serverAddress;
    transportConfig.serverPort = bootstrap.serverPort;
    transportConfig.credential = {
        .peerId = bootstrap.peerId,
        .authenticationKey = bootstrap.authenticationKey,
    };
    transportConfig.expectedContentDigest =
        bootstrap.expectedContentDigest;
    transportConfig.requirePrivateServerAddress = true;

    std::string transportError;
    network::NativeTcpClientCreateStatus transportStatus =
        network::NativeTcpClientCreateStatus::
            InvalidConfiguration;
    auto transport = network::NativeTcpClientTransport::create(
        std::move(transportConfig), &transportError,
        &transportStatus);
    if (transport == nullptr) {
        LOG_ERROR(
            "Failed to create WRECKWATER native TCP client ({}): {}",
            network::nativeTcpClientCreateStatusName(
                transportStatus),
            transportError);
        return false;
    }

    auto state =
        std::make_unique<WreckwaterApplicationClientState>();
    state->transport = transport.get();

    client::WreckwaterApplicationClient::Config clientConfig;
    clientConfig.runtime = {
        .serverPeerId = 0u,
        .serverConnectionSerial = 0u,
        .sessionId = bootstrap.sessionId,
        .matchId = bootstrap.matchId,
        .worldId = bootstrap.worldId,
        .worldEpoch = bootstrap.worldEpoch,
        .authorityEpoch = bootstrap.authorityEpoch,
        .localPlayerId = bootstrap.peerId,
        .maximumFramesPerPump = 64u,
        .firstClientRequestSequence = 1u,
    };
    clientConfig.graphical.maximumCatchUpTicks = 4u;
    clientConfig.graphical.inputLeadTicks =
        bootstrap.inputLeadTicks;
    clientConfig.graphical.useCertifiedVisualClock = true;
    clientConfig.graphical.visualClock.interpolationDelayTicks =
        bootstrap.interpolationDelayTicks;
    clientConfig.graphical.visualClock.maximumExtrapolationTicks =
        network::kWreckwaterClientMaximumExtrapolationTicks;
    clientConfig.graphical.controller.localPlayerId =
        bootstrap.peerId;
    clientConfig.graphical.presentation.localPlayerId =
        bootstrap.peerId;
    clientConfig.graphical.presentation.roster = {{
        {
            .playerId = 1u,
            .crew = game::CrewId::CrewOne,
            .crewSlot = 0u,
        },
        {
            .playerId = 2u,
            .crew = game::CrewId::CrewOne,
            .crewSlot = 1u,
        },
        {
            .playerId = 3u,
            .crew = game::CrewId::CrewTwo,
            .crewSlot = 0u,
        },
        {
            .playerId = 4u,
            .crew = game::CrewId::CrewTwo,
            .crewSlot = 1u,
        },
    }};
    clientConfig.camera = cameraConfig;
    if (!state->client.initialize(
            clientConfig, std::move(transport))) {
        LOG_ERROR(
            "Failed to initialize WRECKWATER graphical client");
        return false;
    }

    state->observedTransportState = state->transport->state();
    wreckwaterClientTelemetry_.enabled = true;
    wreckwaterClientTelemetry_.connectionState =
        wreckwaterApplicationConnectionState(
            state->observedTransportState);
    wreckwaterClientState_ = std::move(state);

    LOG_WARN(
        "WRECKWATER native TCP sends its bootstrap credential in "
        "plaintext; use only loopback or a trusted private development "
        "network");
    LOG_INFO(
        "WRECKWATER client starting: {}:{} peer {}",
        bootstrap.serverAddress, bootstrap.serverPort,
        bootstrap.peerId);
    return true;
#endif
}

bool Application::init(const ApplicationConfig& config) {
    if (initialized_) {
        LOG_WARN("Application already initialized");
        return true;
    }
    if (!validApplicationConfig(config)) {
        LOG_ERROR("Application configuration is invalid "
                  "(window {}x{}, heightmap {}x{}, scale {})",
                  config.windowWidth, config.windowHeight,
                  config.heightmapWidth, config.heightmapHeight,
                  config.resolutionScale);
        return false;
    }

    stats_ = {};
    physicsGpuTimingSamples_ = {};
    physicsGpuTimingSampleHead_ = 0u;
    physicsGpuTimingSampleCount_ = 0u;
    renderGpuTimingSamples_ = {};
    renderGpuTimingSampleHead_ = 0u;
    renderGpuTimingSampleCount_ = 0u;
    shouldExit_ = false;
    debugVisMode_ = DebugVisMode::Off;
    wireframeEnabled_ = false;
    legoMode_ = false;
    controllerMode_ = ControllerMode::FreeFly;
    selectedThrowable_ = 0u;
    throwableBodyLimit_ = 0u;
    throwableWheelAccumulator_ = 0.0f;
    throwableCooldown_ = 0.0f;
    benchmarkRunner_.reset();
    browserJourneyBenchmark_.reset();
    benchmarkSubmissionIndices_.clear();
    lastFrameTime_ = 0.0;
    fpsAccumulator_ = 0.0;
    fpsFrameCount_ = 0;
    recordedPositions_.clear();
    tourActive_ = false;
    tourStep_ = 0u;
    tourFrameCounter_ = 0u;
    tourCurrentPath_.clear();

    config_ = config;
    rendererSettings_.sunDirection = glm::dot(config_.sunDirection,
                                              config_.sunDirection) > 1.0e-10f
        ? glm::normalize(config_.sunDirection)
        : RendererRuntimeSettings{}.sunDirection;
    rendererSettings_.sunColor = config_.sunColor;
    rendererSettings_.ambientColor = config_.ambientColor;
    rendererSettings_.ambientIntensity = config_.ambientIntensity;
    rendererSettings_.fogDensity = config_.fogDensity;
    rendererSettings_.fogColor = config_.fogColor;
    rendererSettings_.waterEnabled = config_.waterEnabled;
    rendererSettings_.waterHeight = config_.waterHeight;
    rendererSettings_.waterShallowColor = config_.waterShallowColor;
    rendererSettings_.waterDeepColor = config_.waterDeepColor;
    rendererSettings_.waterRoughness = config_.waterRoughness;
    rendererSettings_.waterWaveStrength = config_.waterWaveStrength;
    rendererSettings_.waterReflectionStrength = config_.waterReflectionStrength;
    rendererSettings_.waterShoreFade = config_.waterShoreFade;
    rendererSettings_.waterSpectrum = config_.waterSpectrum;
    rendererSettings_.cameraFovDegrees = config_.cameraFovDegrees;
    rendererSettings_.cameraNear = config_.cameraNear;
    rendererSettings_.cameraFar = config_.cameraFar;
    rendererSettings_.cameraMoveSpeed = config_.cameraMoveSpeed;
    rendererSettings_.cameraMouseSensitivity = config_.cameraMouseSensitivity;
    rendererSettings_.cameraEyeHeight = config_.cameraEyeHeight;
    appliedWaterCoastHeight_ = rendererSettings_.waterHeight;
    appliedShadowSunDirection_ = rendererSettings_.sunDirection;
    rendererSettingsRevision_ = 1u;
    appliedRendererSettingsRevision_ = 1u;
    rendererSettingsDirty_ = 0u;
    uncappedFPS_ = !config_.vsync;
    primitiveCullController_.reset();

    // Initialize teleport targets
    // Paste recorded positions here!
    teleportTargets_ = {
        { { 202.53f, 120.92f, -27.16f }, 1.4578f, -0.0944f },
        { { 202.53f, 120.92f, -27.16f }, -0.2070f, -0.3556f },
        { { 179.01f, 121.64f, -28.72f }, -1.3958f, -0.1900f },
        { { 179.01f, 121.64f, -28.72f }, -2.0606f, -0.0380f },
    };

    LOG_INFO("═══════════════════════════════════════════════════════════════");
    LOG_INFO("  {}", config_.motoEnabled ? "RIDGEBREAK - Motocross Freeride" : "Voxy - Terrain Renderer");
#if defined(VOXY_NATIVE)
    LOG_INFO("  Build target: native");
#elif defined(VOXY_WASM)
    LOG_INFO("  Build target: wasm");
#endif
    LOG_INFO("  Render path: {}", renderPathToString(config_.renderPath));
    LOG_INFO("═══════════════════════════════════════════════════════════════");

    const auto failInitialization = [this]() {
        shutdown();
        return false;
    };

    // Initialize subsystems in order
    {
        LOG_SCOPE("Application::init");

        if (!initWindow()) {
            LOG_ERROR("Failed to initialize window");
            return failInitialization();
        }

        if (!initGPU()) {
            LOG_ERROR("Failed to initialize GPU context");
            return failInitialization();
        }

        if (!initRenderGpuProfiling()) {
            LOG_ERROR("Failed to initialize render GPU profiling");
            return failInitialization();
        }

        if (!initInput()) {
            LOG_ERROR("Failed to initialize input system");
            return failInitialization();
        }

        if (!initCamera()) {
            LOG_ERROR("Failed to initialize camera");
            return failInitialization();
        }

        if (!initTerrain()) {
            LOG_ERROR("Failed to initialize terrain");
            return failInitialization();
        }

        if (!initRenderers()) {
            LOG_ERROR("Failed to initialize renderers");
            return failInitialization();
        }

        if (!initMoto()) {
            LOG_ERROR("Failed to initialize RIDGEBREAK freeride session");
            return failInitialization();
        }

        if (!initWreckwaterClient()) {
            return failInitialization();
        }

        setupCallbacks();
    }

    initialized_ = true;
    shouldExit_ = false;

    LOG_INFO("Application initialized successfully");
    LOG_INFO("  Window: {}x{}", config_.windowWidth, config_.windowHeight);
    LOG_INFO("  Terrain: {}x{} (mips: {})", 
             stats_.terrainWidth, stats_.terrainHeight, stats_.terrainMipLevels);
    LOG_INFO("  Render path: {}", renderPathToString(config_.renderPath));
    LOG_INFO("");
    LOG_INFO("Controls:");
    if (motoSession_ && motoSession_->isInitialized()) {
        LOG_INFO("  W/Up      - Throttle");
        LOG_INFO("  S/Down    - Brake");
        LOG_INFO("  A/D       - Steer");
        LOG_INFO("  Q/E       - Rider lean");
        LOG_INFO("  Shift     - Stand");
        LOG_INFO("  Ctrl      - Duck");
        LOG_INFO("  Z/X       - Shift down/up");
        LOG_INFO("  R         - Remount after a crash");
        LOG_INFO("  C         - Start/restart circuit from the grid");
        LOG_INFO("  Backspace - Reset bike / restart active circuit");
    } else if (wreckwaterClientState_) {
        LOG_INFO("  WASD      - Camera-relative movement");
        LOG_INFO("  Space     - Jump / swim stroke");
        LOG_INFO("  E         - Board");
        LOG_INFO("  Mouse     - Orbit (click to capture)");
        LOG_INFO("  Wheel     - Camera distance");
        LOG_INFO("  Q         - Swap camera shoulder");
    } else {
        LOG_INFO("  WASD      - Move camera");
        LOG_INFO("  Mouse     - Look around (click to capture)");
        LOG_INFO("  Shift     - Speed boost / Run");
        LOG_INFO("  E/Space   - Move up / Jump");
        LOG_INFO("  Q/Ctrl    - Move down");
    }
    LOG_INFO("  F1        - Toggle debug overlay");
    LOG_INFO("  F2        - Toggle wireframe mode");
    LOG_INFO("  F3        - Toggle render path");
    LOG_INFO("  F4        - Toggle depth visualization");
    LOG_INFO("  F5        - Toggle normal visualization");
    LOG_INFO("  F6        - Toggle mip level heat map");
    if (!motoSession_) {
        LOG_INFO("  F7        - Toggle benchmark mode");
        LOG_INFO("  F8        - Toggle controller (free-fly/character)");
    }
    LOG_INFO("  F9        - Toggle uncapped/VSync presentation");
    LOG_INFO("  Escape    - Release mouse / Exit");
    if (!wreckwaterClientState_ && !motoSession_) {
        LOG_INFO("  Wheel     - Select throwable object");
        LOG_INFO(
            "  Left click- Capture mouse / throw selected object");
        LOG_INFO("  Right click- Throw 128 selected objects");
    }
    LOG_INFO("");

#if defined(VOXY_WASM)
    // RIDGEBREAK opens as a game, not as an engine diagnostics screen.
    // The F1 overlay remains available to developers and benchmarks.
    getDebugOverlay().setVisible(!motoSession_);
    if (!motoSession_) {
        // Retain the legacy terrain-sandbox default outside the moto product.
        setControllerMode(ControllerMode::Character);
    }
#endif

    // Handle initial teleportation
    if (config_.initialTeleportIndex.has_value() && camera_) {
        int index = config_.initialTeleportIndex.value();
        if (index >= 0 && static_cast<size_t>(index) < teleportTargets_.size()) {
            const auto& target = teleportTargets_[static_cast<size_t>(index)];
            camera_->setWorldPosition(target.sector, target.position);
            camera_->setYaw(target.yaw);
            camera_->setPitch(target.pitch);
            LOG_INFO("Applied initial teleport to index {} (Pos: {:.2f}, {:.2f}, {:.2f})",
                     index, target.position.x, target.position.y, target.position.z);
        } else {
            LOG_WARN("Invalid initial teleport index: {}", index);
        }
    }

    // If a screenshot tour is requested, prepare it now (after initial teleport).
    startScreenshotTour();

    if (!spawnBenchmarkBodies()) {
        LOG_ERROR("Failed to create the deterministic benchmark body set");
        return failInitialization();
    }
#if !defined(VOXY_WASM)
    if (config_.cubePyramidBodyCount != 0u
        && !startCubePyramidExperiment()) {
        LOG_ERROR("Failed to create the cube triangle experiment");
        return failInitialization();
    }
#endif

    if (config_.benchmarkOnStartup) {
        startBenchmark();
    }

    return true;
}

// Run is now handled by the platform entry point (entry.cpp)
// This method is deprecated and should be removed or made empty if the interface requires it.
// For now, we will leave it empty as the logic is moved to entry.cpp
void Application::run() {
    LOG_WARN("Application::run() is deprecated. Use platform entry point instead.");
}

void Application::requestExit() {
    LOG_INFO("Exit requested");
    shouldExit_ = true;

#if defined(VOXY_NATIVE)
    if (window_) {
        window_->requestClose();
    }
#endif
}

void Application::shutdown() {
    const bool hasResources = initialized_
        || wreckwaterClientState_
        || window_ || gpuContext_ || input_ || camera_ || freeFlyController_
        || physicsWorld_ || characterController_ || heightmap_
        || terrainTextures_ || waterSimulation_ || primitivePath_
        || meshPath_ || motoSession_
        || trianglePath_ || raycastPath_ || blitPath_
        || renderGpuQuerySet_ || renderGpuResolveBuffer_
        || depthTexture_ || depthView_
        || benchmarkTargetTexture_ || benchmarkTargetView_
        || shadowMapTexture_ || shadowMapView_;
    if (!hasResources) {
        return;
    }

    LOG_INFO("Shutting down application...");

    if (wreckwaterClientState_) {
        wreckwaterClientState_->client.close();
        wreckwaterClientState_.reset();
    }
    wreckwaterClientTelemetry_ = {};
    if (config_.wreckwaterClient) {
        config_.wreckwaterClient->authenticationKey.fill(
            std::byte{});
    }

    retireBenchmarkSubmissions(true);

    renderGpuReadback_.shutdown();
    if (renderGpuResolveBuffer_) {
        wgpuBufferDestroy(renderGpuResolveBuffer_);
        wgpuBufferRelease(renderGpuResolveBuffer_);
        renderGpuResolveBuffer_ = nullptr;
    }
    if (renderGpuQuerySet_) {
        wgpuQuerySetRelease(renderGpuQuerySet_);
        renderGpuQuerySet_ = nullptr;
    }

// No WASM global state needed in Application anymore

    // Release GPU resources
    if (depthView_) {
        wgpuTextureViewRelease(depthView_);
        depthView_ = nullptr;
    }
    if (depthTexture_) {
        wgpuTextureDestroy(depthTexture_);
        wgpuTextureRelease(depthTexture_);
        depthTexture_ = nullptr;
    }
    if (benchmarkTargetView_) {
        wgpuTextureViewRelease(benchmarkTargetView_);
        benchmarkTargetView_ = nullptr;
    }
    if (benchmarkTargetTexture_) {
        wgpuTextureDestroy(benchmarkTargetTexture_);
        wgpuTextureRelease(benchmarkTargetTexture_);
        benchmarkTargetTexture_ = nullptr;
    }

    if (shadowMapView_) {
        wgpuTextureViewRelease(shadowMapView_);
        shadowMapView_ = nullptr;
    }
    if (shadowMapTexture_) {
        wgpuTextureDestroy(shadowMapTexture_);
        wgpuTextureRelease(shadowMapTexture_);
        shadowMapTexture_ = nullptr;
    }

    // Shutdown renderers
    motoRaceSession_.reset();
    motoRaceTrickSequence_ = 0u;
    motoRaceLandingIdentity_ = 0u;
    motoRaceObservedTricksLanded_ = 0u;
    if (motoSession_) {
        motoSession_->shutdown();
        motoSession_.reset();
    }
    if (meshPath_) {
        meshPath_->shutdown();
        meshPath_.reset();
    }
    if (primitivePath_) {
        primitivePath_->shutdown();
        primitivePath_.reset();
    }
    if (blitPath_) {
        blitPath_->shutdown();
        blitPath_.reset();
    }
    if (raycastPath_) {
        raycastPath_->shutdown();
        raycastPath_.reset();
    }
    if (trianglePath_) {
        trianglePath_->shutdown();
        trianglePath_.reset();
    }
    if (waterSimulation_) {
        if (physicsWorld_) {
            physicsWorld_->setWaterSurfaceSampler({});
            physicsWorld_->setWaterGpuResources({});
        }
        waterSimulation_->shutdown();
        waterSimulation_.reset();
    }

    if (terrainTextures_) {
        terrainTextures_->shutdown();
        terrainTextures_.reset();
    }

    // Shutdown other subsystems
    characterController_.reset();
    if (physicsWorld_) {
        physicsWorld_->shutdown();
        physicsWorld_.reset();
    }

    // Jolt streams directly from the heightmap, so release it after physics.
    if (heightmap_) {
        heightmap_->release();
        heightmap_.reset();
    }

    freeFlyController_.reset();
    camera_.reset();
    input_.reset();

    if (gpuContext_) {
        gpuContext_->shutdown();
        gpuContext_.reset();
    }

    if (window_) {
        window_->shutdown();
        window_.reset();
    }

#if defined(VOXY_NATIVE)
    Window::terminateGLFW();
#endif

    initialized_ = false;
    cubePyramidSpawnAttempted_ = false;
    cubePyramidSpawned_ = false;
    cubeTriangleColumns_.clear();
    cubeTriangleColumnsWokenThisTick_.clear();
    cubeTriangleWakeDedupTick_ = std::numeric_limits<uint64_t>::max();
    LOG_INFO("Application shutdown complete");
}

// ─────────────────────────────────────────────────────────────────────────────
// Frame Processing
// ─────────────────────────────────────────────────────────────────────────────

void Application::beginFrame() {
    // Save previous input state BEFORE polling new events
    if (input_) {
        input_->beginFrame();
    }

#if defined(VOXY_NATIVE)
    // Poll events to get fresh input state
    if (window_) {
        window_->pollEvents();
    }
#endif

    // Compute input deltas AFTER events are polled
    if (input_) {
        input_->computeDeltas();
    }
}

void Application::update(float deltaTime) {
    update(deltaTime, deltaTime);
}

void Application::updateWreckwaterClient(float deltaTime) {
    if (!wreckwaterClientState_) return;

    pollWreckwaterCameraProbe();

    client::WreckwaterApplicationClientFrameInput frame;
    const double elapsedNanoseconds =
        static_cast<double>(deltaTime) * 1'000'000'000.0;
    frame.elapsedNanoseconds =
        elapsedNanoseconds
                >= static_cast<double>(
                    std::numeric_limits<uint64_t>::max())
        ? std::numeric_limits<uint64_t>::max()
        : static_cast<uint64_t>(
            std::llround(elapsedNanoseconds));

    if (camera_) {
        frame.cameraSector = camera_->worldSector();
    }
    if (input_ && camera_) {
        client::WreckwaterApplicationDigitalControls digital;
        digital.forward =
            input_->isKeyDown(Key::W)
            || input_->isKeyDown(Key::Up);
        digital.backward =
            input_->isKeyDown(Key::S)
            || input_->isKeyDown(Key::Down);
        digital.left =
            input_->isKeyDown(Key::A)
            || input_->isKeyDown(Key::Left);
        digital.right =
            input_->isKeyDown(Key::D)
            || input_->isKeyDown(Key::Right);
        digital.jumpDown = input_->isKeyDown(Key::Space);
        digital.boardDown = input_->isKeyDown(Key::E);
        if (!client::mapWreckwaterCameraRelativeControls(
                digital, camera_->forward(), camera_->right(),
                frame.controls)) {
            frame.controls = {
                .jumpDown = digital.jumpDown,
                .boardDown = digital.boardDown,
            };
        }

        if (input_->wasMouseButtonPressed(MouseButton::Left)
            && !input_->isMouseCaptured()
            && !input_->wasKeyPressed(Key::Escape)) {
            input_->captureMouse();
        }
        const float cameraDelta =
            std::clamp(deltaTime, 0.0f, 0.05f);
        if (input_->isMouseCaptured()
            && cameraDelta > 0.0f) {
            const glm::vec2 mouse = input_->mouseDelta();
            constexpr float yawSpeed = 3.5f;
            constexpr float pitchSpeed = 2.5f;
            frame.camera.orbit.x = std::clamp(
                mouse.x * config_.cameraMouseSensitivity
                    / (yawSpeed * cameraDelta),
                -1.0f, 1.0f);
            frame.camera.orbit.y = std::clamp(
                -mouse.y * config_.cameraMouseSensitivity
                    / (pitchSpeed * cameraDelta),
                -1.0f, 1.0f);
        }
        if (cameraDelta > 0.0f) {
            constexpr float zoomSpeed = 5.0f;
            constexpr float metersPerWheelStep = 0.35f;
            frame.camera.zoom = std::clamp(
                -input_->scrollDelta() * metersPerWheelStep
                    / (zoomSpeed * cameraDelta),
                -1.0f, 1.0f);
        }
        frame.camera.shoulderSwapDown =
            input_->isKeyDown(Key::Q);
    }

    const client::WreckwaterApplicationClientFrameResult result =
        wreckwaterClientState_->client.frame(frame);
    if (result.cameraUpdated && camera_) {
        const auto& pose =
            wreckwaterClientState_->client.cameraRig().pose();
        camera_->setWorldPosition(
            pose.cameraPosition.sector,
            pose.cameraPosition.local);
        camera_->lookAt(pose.cameraSectorLookTarget);
        camera_->setFovY(pose.fovYRadians);
    }

    auto addSaturated = [](uint64_t& value, uint64_t amount) {
        if (amount
            > std::numeric_limits<uint64_t>::max() - value) {
            value = std::numeric_limits<uint64_t>::max();
        } else {
            value += amount;
        }
    };
    addSaturated(wreckwaterClientTelemetry_.frames, 1u);
    addSaturated(
        wreckwaterClientTelemetry_.snapshotsAccepted,
        result.graphical.runtime.snapshotsAccepted);
    addSaturated(
        wreckwaterClientTelemetry_.rejectedFrames,
        result.graphical.runtime.framesRejected);
    wreckwaterClientTelemetry_.visibleProxies =
        static_cast<uint32_t>(
            wreckwaterClientState_->client
                .visibleInstances().size());
    wreckwaterClientTelemetry_.lastFrameStatus =
        static_cast<uint32_t>(result.graphical.status);
    wreckwaterClientTelemetry_.lastRuntimeError =
        static_cast<uint32_t>(
            result.graphical.runtime.lastError);
    wreckwaterClientTelemetry_.lastReplicationError =
        static_cast<uint32_t>(
            result.graphical.runtime.lastReplicationError);
    wreckwaterClientTelemetry_.cameraValid =
        result.cameraUpdated;
    wreckwaterClientTelemetry_.cameraProbeOutstanding =
        wreckwaterClientState_->cameraProbeSubmitted;

#if !defined(VOXY_WASM)
    const network::NativeTcpClientState transportState =
        wreckwaterClientState_->transport->state();
    wreckwaterClientTelemetry_.connectionState =
        wreckwaterApplicationConnectionState(transportState);
    const auto& transportTelemetry =
        wreckwaterClientState_->transport->telemetry();
    wreckwaterClientTelemetry_.socketErrors =
        transportTelemetry.socketErrors;
    if (transportState
        != wreckwaterClientState_->observedTransportState) {
        if (transportState
            == network::NativeTcpClientState::Failed) {
            LOG_ERROR(
                "WRECKWATER connection state: failed "
                "(auth failures {}, content mismatches {}, "
                "socket errors {})",
                transportTelemetry.authenticationFailures,
                transportTelemetry.contentDigestMismatches,
                transportTelemetry.socketErrors);
        } else {
            LOG_INFO(
                "WRECKWATER connection state: {}",
                nativeTcpClientStateName(transportState));
        }
        wreckwaterClientState_->observedTransportState =
            transportState;
    }
#endif

    if (result.graphical.runtime.lastError
            != network::WreckwaterClientRuntimeError::None
        && result.graphical.runtime.lastError
            != wreckwaterClientState_->observedRuntimeError) {
        LOG_WARN(
            "WRECKWATER runtime frame: {}",
            network::wreckwaterClientRuntimeErrorName(
                result.graphical.runtime.lastError));
    }
    wreckwaterClientState_->observedRuntimeError =
        result.graphical.runtime.lastError;
    wreckwaterClientState_->observedFrameStatus =
        result.graphical.status;

    submitWreckwaterCameraProbe();
    wreckwaterClientTelemetry_.cameraProbeOutstanding =
        wreckwaterClientState_->cameraProbeSubmitted;
}

void Application::pollWreckwaterCameraProbe() {
    if (!wreckwaterClientState_
        || !wreckwaterClientState_->cameraProbeSubmitted
        || !physicsWorld_) {
        return;
    }
    const std::optional<physics::PhysicsQueryBatch> batch =
        physicsWorld_->pollQueryResults();
    if (!batch) return;

    const client::WreckwaterCameraObstructionProbeResult merged =
        client::mergeWreckwaterCameraObstructionProbeResult(
            wreckwaterClientState_->cameraProbe,
            wreckwaterClientState_->terrainProbe, *batch);
    const client::WreckwaterThirdPersonCameraStatus status =
        wreckwaterClientState_->client
            .resolveCameraObstructionProbe(merged);
    wreckwaterClientState_->cameraProbeSubmitted = false;
    wreckwaterClientState_->cameraProbe = {};
    wreckwaterClientState_->terrainProbe = {};
    if (status
        != client::WreckwaterThirdPersonCameraStatus::Accepted) {
        if (wreckwaterClientTelemetry_.cameraProbeFailures
            != std::numeric_limits<uint64_t>::max()) {
            ++wreckwaterClientTelemetry_.cameraProbeFailures;
        }
    }
}

void Application::submitWreckwaterCameraProbe() {
    if (!wreckwaterClientState_
        || wreckwaterClientState_->cameraProbeSubmitted
        || !physicsWorld_ || !heightmap_) {
        return;
    }

    client::WreckwaterCameraObstructionProbeRequest request;
    const client::WreckwaterThirdPersonCameraStatus takeStatus =
        wreckwaterClientState_->client
            .takeCameraObstructionProbe(request);
    if (takeStatus
        == client::WreckwaterThirdPersonCameraStatus::
            ProbeUnavailable
        || takeStatus
        == client::WreckwaterThirdPersonCameraStatus::
            ProbeOutstanding) {
        return;
    }
    if (takeStatus
        != client::WreckwaterThirdPersonCameraStatus::Accepted) {
        if (wreckwaterClientTelemetry_.cameraProbeFailures
            != std::numeric_limits<uint64_t>::max()) {
            ++wreckwaterClientTelemetry_.cameraProbeFailures;
        }
        return;
    }

    const client::WreckwaterCameraTerrainView terrainView{
        .samples = heightmap_->getData(),
        .width = heightmap_->getWidth(),
        .height = heightmap_->getHeight(),
        .heightScale = config_.heightScale,
        .cellScale = config_.cellScale,
    };
    const client::WreckwaterCameraTerrainCastResult terrain =
        client::wreckwaterCameraTerrainSphereCast(
            request, terrainView);
    if (!terrain) {
        client::WreckwaterCameraObstructionProbeResult failed;
        failed.identity = request.identity;
        failed.overflow = true;
        static_cast<void>(
            wreckwaterClientState_->client
                .resolveCameraObstructionProbe(failed));
        if (wreckwaterClientTelemetry_.cameraProbeFailures
            != std::numeric_limits<uint64_t>::max()) {
            ++wreckwaterClientTelemetry_.cameraProbeFailures;
        }
        return;
    }

    if (!physicsWorld_->submitQueries(
            std::span(&request.physicsQuery, 1u))) {
        static_cast<void>(
            wreckwaterClientState_->client
                .cancelCameraObstructionProbe(
                    request.identity));
        if (wreckwaterClientTelemetry_.cameraProbeFailures
            != std::numeric_limits<uint64_t>::max()) {
            ++wreckwaterClientTelemetry_.cameraProbeFailures;
        }
        return;
    }
    wreckwaterClientState_->cameraProbe = request;
    wreckwaterClientState_->terrainProbe = terrain;
    wreckwaterClientState_->cameraProbeSubmitted = true;
}

void Application::updateMoto(float deltaTime) {
    if (!motoSession_ || !motoSession_->isInitialized() || !input_) return;

    const bool circuitRequested = input_->wasKeyPressed(Key::C);
    const bool resetRequested = input_->wasKeyPressed(Key::Backspace);
    const moto::RaceMode raceMode = motoRaceSession_
        ? motoRaceSession_->config().mode : moto::RaceMode::Freeride;
    const moto::RacePhase racePhase = motoRaceSession_
        ? motoRaceSession_->phase() : moto::RacePhase::Lobby;
    const uint32_t countdown = motoRaceSession_
        ? motoRaceSession_->countdownTicksRemaining() : 0u;
    const moto::MotoRaceApplicationPolicy applicationPolicy =
        moto::evaluateMotoRaceApplicationPolicy(
            circuitRequested, resetRequested, raceMode, racePhase, countdown);
    if (applicationPolicy.action
            == moto::MotoRaceControlAction::StartCircuit
        && !startMotoCircuit()) {
        LOG_ERROR("Could not start or restart RIDGEBREAK circuit");
    } else if (applicationPolicy.action
               == moto::MotoRaceControlAction::ResetPractice) {
        motoSession_->reset();
        motoRaceObservedTricksLanded_ = 0u;
    }

    moto::BikeInput bikeInput;
    bikeInput.throttle =
        (input_->isKeyDown(Key::W) || input_->isKeyDown(Key::Up)) ? 1.0f : 0.0f;
    bikeInput.brake =
        (input_->isKeyDown(Key::S) || input_->isKeyDown(Key::Down)) ? 1.0f : 0.0f;
    bikeInput.steer =
        (input_->isKeyDown(Key::D) || input_->isKeyDown(Key::Right) ? 1.0f : 0.0f)
        - (input_->isKeyDown(Key::A) || input_->isKeyDown(Key::Left) ? 1.0f : 0.0f);
    bikeInput.lean = (input_->isKeyDown(Key::E) ? 1.0f : 0.0f)
        - (input_->isKeyDown(Key::Q) ? 1.0f : 0.0f);
    bikeInput.seated = !(input_->isKeyDown(Key::LeftShift)
                         || input_->isKeyDown(Key::RightShift));
    bikeInput.duck = input_->isKeyDown(Key::LeftControl)
        || input_->isKeyDown(Key::RightControl);
    bikeInput.shiftDown = input_->wasKeyPressed(Key::Z);
    bikeInput.shiftUp = input_->wasKeyPressed(Key::X);
    bikeInput.remount = input_->wasKeyPressed(Key::R);

    if (!resetRequested || circuitRequested) {
        motoSession_->update(
            bikeInput,
            [this](float x, float z) { return sampleTerrainHeight(x, z); },
            deltaTime,
            [this]() {
                if (!motoRaceSession_) {
                    return moto::MotoFixedStepMode::Simulate;
                }
                const moto::MotoRaceApplicationPolicy policy =
                    moto::evaluateMotoRaceApplicationPolicy(
                        false, false, motoRaceSession_->config().mode,
                        motoRaceSession_->phase(),
                        motoRaceSession_->countdownTicksRemaining());
                return policy.gridOwned
                    ? moto::MotoFixedStepMode::HoldGrid
                    : moto::MotoFixedStepMode::Simulate;
            },
            [this](const moto::BikeState& state) {
                advanceMotoRaceFixedStep(state);
            });
    }

    if (camera_) {
        const moto::MotoCameraPose& pose = motoSession_->cameraPose();
        camera_->setWorldPosition({0, 0, 0}, pose.position);
        camera_->lookAt(pose.target);
    }
}

void Application::advanceMotoRaceFixedStep(const moto::BikeState& state) {
    if (!motoRaceSession_) return;

    std::array<moto::RaceTrickEvent, 4> trickEvents{};
    size_t trickEventCount = 0u;
    if (state.tricksLanded < motoRaceObservedTricksLanded_) {
        motoRaceObservedTricksLanded_ = state.tricksLanded;
    }
    const bool newlyCanonicalLanding =
        state.tricksLanded > motoRaceObservedTricksLanded_
        && state.lastLandedTricks != 0u
        && state.lastLanding != moto::LandingQuality::Crashed
        && motoRaceSession_->phase() == moto::RacePhase::Running;
    if (newlyCanonicalLanding) {
        const moto::RaceRiderState* rider =
            motoRaceSession_->rider(kLocalMotoRacePlayer);
        if (rider != nullptr
            && rider->lastTrickSequence
                   != std::numeric_limits<uint64_t>::max()
            && rider->lastLandingIdentity
                   != std::numeric_limits<uint64_t>::max()) {
            uint64_t sequence = rider->lastTrickSequence;
            const uint64_t landingIdentity =
                rider->lastLandingIdentity + 1u;
            const uint64_t authorityTick = motoRaceSession_->tick() + 1u;
            const auto append = [&](moto::TrickFlags flag,
                                    moto::RaceTrick trick) {
                if ((state.lastLandedTricks
                     & static_cast<uint32_t>(flag)) == 0u) return;
                trickEvents[trickEventCount++] = {
                    .sequence = ++sequence,
                    .landingIdentity = landingIdentity,
                    .authorityTick = authorityTick,
                    .trick = trick,
                };
            };
            append(moto::TrickFlags::Whip, moto::RaceTrick::Whip);
            append(moto::TrickFlags::Backflip, moto::RaceTrick::Backflip);
            append(moto::TrickFlags::Frontflip, moto::RaceTrick::Frontflip);
            append(moto::TrickFlags::BarrelRoll,
                   moto::RaceTrick::BarrelRoll);
        }
    }
    motoRaceObservedTricksLanded_ = state.tricksLanded;

    const moto::RaceRiderFrame frame{
        .player = kLocalMotoRacePlayer,
        .position = state.chassisPosition,
        .trickEvents = std::span(trickEvents.data(), trickEventCount),
    };
    motoRaceSession_->step(std::span(&frame, 1u));
    if (const moto::RaceRiderState* rider =
            motoRaceSession_->rider(kLocalMotoRacePlayer)) {
        motoRaceTrickSequence_ = rider->lastTrickSequence;
        motoRaceLandingIdentity_ = rider->lastLandingIdentity;
    }
}

MotoHudState Application::getMotoHudState() const noexcept {
    MotoHudState hud;
    if (!motoSession_ || !motoSession_->isInitialized()) return hud;

    const moto::BikeState& bike = motoSession_->bikeState();
    hud.active = true;
    hud.speedKilometersPerHour = std::abs(bike.speed) * 3.6f;
    hud.engineRpm = bike.engineRPM;
    hud.gear = bike.gear;
    hud.crashState = static_cast<uint32_t>(bike.crash);
    hud.combo = bike.comboCount;
    hud.score = bike.trickScore;
    if (motoRaceSession_) {
        const moto::MotoRaceApplicationPolicy policy =
            moto::evaluateMotoRaceApplicationPolicy(
                false, false, motoRaceSession_->config().mode,
                motoRaceSession_->phase(),
                motoRaceSession_->countdownTicksRemaining());
        hud.raceMode = static_cast<uint32_t>(
            motoRaceSession_->config().mode);
        hud.raceActive = policy.hudRaceActive;
        if (const moto::RaceRiderState* rider =
                motoRaceSession_->rider(kLocalMotoRacePlayer)) {
            hud.score = rider->score;
            if (hud.raceActive) {
                hud.racePhase = static_cast<uint32_t>(policy.hudPhase);
                hud.checkpointCount = static_cast<uint32_t>(
                    motoRaceSession_->checkpoints().size());
                hud.lapCount = motoRaceSession_->config().lapCount;
                hud.countdownTicksRemaining = policy.hudCountdownTicks;
                hud.nextCheckpoint = rider->nextCheckpoint;
                hud.completedLaps = rider->completedLaps;
                hud.finishPlace = rider->finishPlace;
                hud.didNotFinish = rider->didNotFinish;
            }
        }
    }
    return hud;
}

void Application::update(float simulationDeltaTime, float frameDeltaTime) {
    if (!std::isfinite(simulationDeltaTime)
        || simulationDeltaTime < 0.0f) {
        simulationDeltaTime = 0.0f;
    }
    if (!std::isfinite(frameDeltaTime) || frameDeltaTime < 0.0f) {
        frameDeltaTime = 0.0f;
    }
    applyRendererSettings();

    const bool browserJourneyWasRunning = browserJourneyBenchmark_
        && browserJourneyBenchmark_->isRunning();
    if (browserJourneyWasRunning) {
        updateBrowserJourneyBenchmark();
    }

    // Command-line benchmarks are scripted workloads. Ignoring gameplay input
    // keeps their camera and body count stable even if the window has focus.
    const bool scriptedBenchmark =
        (config_.benchmarkOnStartup || config_.exitAfterBenchmark)
        && isBenchmarkRunning();
    if (wreckwaterClientState_) {
        handleKeyboardShortcuts();
        updateWreckwaterClient(simulationDeltaTime);
    } else if (motoSession_ && motoSession_->isInitialized()
               && !scriptedBenchmark && !browserJourneyWasRunning) {
        updateMoto(simulationDeltaTime);
        handleKeyboardShortcuts();
    } else if (!scriptedBenchmark && !browserJourneyWasRunning) {
        processInput(simulationDeltaTime);
        handleKeyboardShortcuts();

        // Update camera controller based on active mode
        if (input_) {
            switch (controllerMode_) {
                case ControllerMode::FreeFly:
                    if (freeFlyController_) {
                        freeFlyController_->update(simulationDeltaTime, *input_);
                    }
                    break;
                case ControllerMode::Character:
                    if (characterController_) {
                        characterController_->update(simulationDeltaTime, *input_);
                    }
                    break;
            }
        }
    }

    if (physicsWorld_) {
        physicsWorld_->update(simulationDeltaTime);
        const physics::PhysicsStepStats stepStats =
            physicsWorld_->lastStepStats();
        stats_.physicsSimulationMs = stepStats.simulationMs;
        stats_.physicsWaterMs = stepStats.waterMs;
    } else {
        stats_.physicsSimulationMs = 0.0;
        stats_.physicsWaterMs = 0.0;
    }

    // Custom update callback
    if (updateCallback_) {
        updateCallback_(simulationDeltaTime);
    }

    // Update statistics
    updateStats(frameDeltaTime);
}

void Application::render() {
    stats_.physicsSnapshotMs = 0.0;
    stats_.primitiveCullMs = 0.0;
    stats_.primitivePackingMs = 0.0;
    stats_.primitiveUploadMs = 0.0;
    stats_.primitiveRenderMs = 0.0;
    if (!gpuContext_ || !gpuContext_->isInitialized()) {
        return;
    }
    
    // Skip rendering if window is minimized (zero-size swapchain)
    // Requesting a texture from a zero-sized or unconfigured surface is undefined behavior
#if defined(VOXY_NATIVE)
    if (window_ && (window_->getWidth() == 0 || window_->getHeight() == 0)) {
        return;  // Window is minimized, skip rendering
    }
#endif
    
    // Also check swapchain dimensions
    if (gpuContext_->getSwapchainWidth() == 0 || gpuContext_->getSwapchainHeight() == 0) {
        return;  // Invalid swapchain dimensions
    }

    // Scripted performance runs render the same frame offscreen so window
    // server presentation and occlusion cannot contaminate GPU throughput.
    WGPUTextureView targetView = config_.benchmarkOnStartup
        ? benchmarkTargetView_ : gpuContext_->getCurrentTextureView();
    if (!targetView) {
        return;
    }

    // Create command encoder
    WGPUCommandEncoderDescriptor encoderDesc = {};
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(
        gpuContext_->getDevice(), &encoderDesc);
    if (!encoder) {
        LOG_ERROR("Failed to create the frame command encoder");
        return;
    }

    // Update camera uniforms for all renderers
    updateCameraUniforms();

    renderGpuProfilingFrame_ = renderGpuQuerySet_
        && config_.renderPath == RenderPath::Raycast
        && raycastPath_ && raycastPath_->isInitialized()
        && blitPath_ && blitPath_->isInitialized()
        && stats_.frameCount % kRenderGpuProfilingIntervalFrames == 0u;

    // Evolve the authoritative surface before physics samples it. Keeping
    // this outside the raycast path also gives the triangle renderer and GPU
    // physics the same animated ocean instead of a never-updated texture.
    if (rendererSettings_.waterEnabled && waterSimulation_
        && waterSimulation_->isInitialized()) {
        constexpr uint32_t stage =
            static_cast<uint32_t>(RenderGpuStage::WaterSimulation);
        waterSimulation_->update(
            encoder,
            static_cast<float>(std::fmod(stats_.totalTimeSeconds, 4096.0)),
            renderGpuProfilingFrame_ ? renderGpuQuerySet_ : nullptr,
            stage * kRenderGpuQueriesPerStage,
            stage * kRenderGpuQueriesPerStage + 1u);
    }

    // GPU physics writes persistent poses into this frame's command stream.
    // CPU backends intentionally no-op here.
    if (physicsWorld_) {
        physicsWorld_->encodeGpuStep(encoder);
    }

    // Render based on active path
    switch (config_.renderPath) {
        case RenderPath::Triangle:
            renderTrianglePath(encoder, targetView);
            break;
        case RenderPath::Raycast:
            renderRaycastPath(encoder, targetView);
            break;
    }

    if (config_.renderPath == RenderPath::Raycast
        && ((primitivePath_ && primitivePath_->isInitialized())
            || (meshPath_ && meshPath_->isInitialized()))) {
        clearRayObjectDepth(encoder);
    }

    if (primitivePath_ && primitivePath_->isInitialized() && physicsWorld_) {
        perf::Timer primitiveStageTimer;
        const auto capabilities = physicsWorld_->capabilities();
        size_t objectCount = physicsWorld_->stats().residentBodies;
        if (capabilities.gpuResidentState && capabilities.directRenderView) {
            // Persistent GPU buffers flow straight into culling/rendering.
            // No body snapshot or transform upload occurs on this path.
            primitivePath_->setPhysicsRenderView(physicsWorld_->renderView());
            stats_.physicsSnapshotMs = 0.0;
            stats_.primitiveBodyLockedReadCount = 0;
            stats_.primitiveBodyCachedReadCount = 0;
        } else {
            primitiveStageTimer.start();
            auto bodies = physicsWorld_->dynamicBodies();
            primitiveStageTimer.stop();
            stats_.physicsSnapshotMs = primitiveStageTimer.elapsedMs();
            const auto bodyReadStats = physicsWorld_->lastDynamicBodyReadStats();
            stats_.primitiveBodyLockedReadCount =
                static_cast<uint32_t>(bodyReadStats.lockedBodyCount);
            stats_.primitiveBodyCachedReadCount =
                static_cast<uint32_t>(bodyReadStats.cachedBodyCount);
            objectCount = bodies.size();
            primitivePath_->setCompactPhysicsInstances(bodies);
        }

        std::span<const physics::DynamicBodySnapshot> avatarProxies;
        if (wreckwaterClientState_) {
            avatarProxies =
                wreckwaterClientState_->client.visibleInstances();
        }
        std::vector<physics::PhysicsWorld::DynamicBodySnapshot> overlays;
        overlays.reserve(
            kPrimitiveOverlayHeadroom + avatarProxies.size());
        // The camera-locked throwable preview and body counter are developer
        // controls, not scene content. Keep them out of deterministic review
        // captures and benchmark workloads so visual evidence is clean and
        // performance measurements represent the game frame.
        const bool showPrimitiveHud =
            !wreckwaterClientState_
            && !motoSession_
            && !config_.benchmarkOnStartup
            && !config_.screenshotPath.has_value()
            && !tourActive_;
        if (showPrimitiveHud) {
            const auto selectedShape =
                static_cast<physics::PhysicsWorld::ThrowableShape>(
                    selectedThrowable_);
            const float previewAngle = static_cast<float>(
                std::fmod(
                    stats_.totalTimeSeconds * 1.8,
                    2.0 * std::numbers::pi));
            const float previewDistance = 1.35f;
            const float viewportWidth = static_cast<float>(
                std::max(gpuContext_->getSwapchainWidth(), 1u));
            const float viewportHeight = static_cast<float>(
                std::max(gpuContext_->getSwapchainHeight(), 1u));
            const float halfHeight =
                std::tan(camera_->fovY() * 0.5f) * previewDistance;
            const float halfWidth =
                halfHeight * viewportWidth / viewportHeight;
            const float previewScale =
                std::min(halfWidth, halfHeight) * 0.22f;
            const glm::vec3 previewPlane =
                camera_->position()
                + camera_->forward() * previewDistance;
            const glm::quat cameraRotation = glm::quat_cast(glm::mat3(
                camera_->right(), camera_->up(), camera_->forward()));
            overlays.push_back({
                selectedShape,
                previewPlane
                    + camera_->right() * (halfWidth * 0.76f)
                    - camera_->up() * (halfHeight * 0.72f),
                glm::angleAxis(
                    previewAngle,
                    glm::normalize(glm::vec3(0.3f, 1.0f, 0.2f))),
                physics::PhysicsWorld::throwableShapeDimensions(
                    selectedShape) * previewScale});
            appendObjectCount(
                overlays, objectCount, previewPlane, camera_->right(),
                camera_->up(), cameraRotation, halfWidth, halfHeight);
        }
        overlays.insert(
            overlays.end(),
            avatarProxies.begin(), avatarProxies.end());
        const size_t submittedObjectCount =
            objectCount + avatarProxies.size();
        stats_.primitiveCullMs = 0.0;
        stats_.primitiveInputCount =
            static_cast<uint32_t>(submittedObjectCount);
        // The exact visible count remains GPU-resident by design.
        stats_.primitiveSubmittedCount =
            static_cast<uint32_t>(submittedObjectCount);
        stats_.primitiveCullRejectionRatio = 0.0f;
        stats_.primitiveCullEvaluated =
            submittedObjectCount != 0u;
        stats_.primitiveCullingEnabled = true;
        primitivePath_->setInstances(overlays);
        const auto& cpuTimings = primitivePath_->lastCpuTimings();
        stats_.primitivePackingMs = cpuTimings.packingMs;
        stats_.primitiveUploadMs = cpuTimings.uploadMs;
        const auto& uploadStats = primitivePath_->lastUploadStats();
        const auto& compactUploadStats =
            primitivePath_->lastCompactUploadStats();
        stats_.primitiveInstanceUploadBytes = uploadStats.bytesUploaded
                                            + compactUploadStats.bytesUploaded;
        stats_.primitiveInstanceUploadCalls = uploadStats.writeCalls
                                            + compactUploadStats.writeCalls;
        stats_.primitiveInstanceFullUpload = uploadStats.fullUpload
                                           || compactUploadStats.fullUpload;
        WGPUTextureView objectDepth = getOrCreateDepthView();
        if (objectDepth) {
            const render::PrimitiveLighting primitiveLighting{
                .direction = rendererSettings_.sunDirection,
                .sunColor = rendererSettings_.sunColor,
                .sunIntensity = rendererSettings_.sunIntensity,
                .ambientColor = rendererSettings_.ambientColor,
                .ambientIntensity = rendererSettings_.ambientIntensity,
                .fogColor = rendererSettings_.fogColor,
                .fogDensity = rendererSettings_.fogDensity,
                .exposure = rendererSettings_.exposure,
            };
            primitiveStageTimer.restart();
            primitivePath_->render(
                encoder, targetView, objectDepth, camera_->viewMatrix(),
                camera_->projectionMatrix(), camera_->position(),
                primitiveLighting,
                gpuContext_->getSwapchainWidth(), gpuContext_->getSwapchainHeight(),
                config_.renderPath == RenderPath::Raycast,
                camera_->worldSector(),
                renderGpuProfilingFrame_ ? renderGpuQuerySet_ : nullptr,
                static_cast<uint32_t>(RenderGpuStage::Primitives)
                    * kRenderGpuQueriesPerStage,
                static_cast<uint32_t>(RenderGpuStage::Primitives)
                    * kRenderGpuQueriesPerStage + 1u);
            primitiveStageTimer.stop();
            stats_.primitiveRenderMs = primitiveStageTimer.elapsedMs();
        }
    }

    renderMoto(encoder, targetView);

    if (renderGpuProfilingFrame_) {
        wgpuCommandEncoderResolveQuerySet(
            encoder, renderGpuQuerySet_, 0u, kRenderGpuTimestampCount,
            renderGpuResolveBuffer_, 0u);
        static_cast<void>(renderGpuReadback_.encodeCopy(
            encoder, renderGpuResolveBuffer_, 0u,
            kRenderGpuTimestampCount * sizeof(uint64_t), stats_.frameCount,
            0u, 0u));
    }

    // Submit commands
    WGPUCommandBufferDescriptor cmdBufferDesc = {};
    WGPUCommandBuffer cmdBuffer = wgpuCommandEncoderFinish(encoder, &cmdBufferDesc);
    if (!cmdBuffer) {
        LOG_ERROR("Failed to finish the frame command buffer");
        wgpuCommandEncoderRelease(encoder);
        return;
    }
#if defined(VOXY_NATIVE)
    if (config_.benchmarkOnStartup) {
        benchmarkSubmissionIndices_.push_back(wgpuQueueSubmitForIndex(
            gpuContext_->getQueue(), 1, &cmdBuffer));
    } else {
        wgpuQueueSubmit(gpuContext_->getQueue(), 1, &cmdBuffer);
    }
#else
    wgpuQueueSubmit(gpuContext_->getQueue(), 1, &cmdBuffer);
#endif

    wgpuCommandBufferRelease(cmdBuffer);
    wgpuCommandEncoderRelease(encoder);

    // Automated screenshots
    if (tourActive_) {
        if (tourFrameCounter_ >= static_cast<uint64_t>(config_.screenshotFrameDelay)) {
            captureScreenshot(tourCurrentPath_);
            tourStep_++;
            scheduleNextTourStep();
        } else {
            tourFrameCounter_++;
        }
    } else if (config_.screenshotPath.has_value() &&
               stats_.frameCount >= static_cast<uint64_t>(config_.screenshotFrameDelay)) {

        captureScreenshot(config_.screenshotPath.value());
        config_.screenshotPath.reset(); // Capture once
        requestExit();
    }
}

void Application::endFrame() {
    if (input_) {
        input_->endFrame();
    }

    if (gpuContext_) {
        if (!config_.benchmarkOnStartup) {
            gpuContext_->present();
        }
#if defined(VOXY_NATIVE)
        if (config_.benchmarkOnStartup) {
            const bool scenarioBoundary = benchmarkRunner_
                && benchmarkRunner_->willCompleteScenarioAfterCurrentFrame();
            retireBenchmarkSubmissions(scenarioBoundary);
        }
#endif
        gpuContext_->tick();
    }

    stats_.frameCount++;
}

void Application::retireBenchmarkSubmissions(bool drain) {
#if defined(VOXY_NATIVE)
    if (!gpuContext_ || benchmarkSubmissionIndices_.empty()) return;
    constexpr size_t kMaximumFramesInFlight = 3u;
    if (drain) {
        const WGPUWrappedSubmissionIndex submission{
            gpuContext_->getQueue(), benchmarkSubmissionIndices_.back()};
        static_cast<void>(wgpuDevicePoll(
            gpuContext_->getDevice(), true, &submission));
        benchmarkSubmissionIndices_.clear();
        return;
    }
    if (benchmarkSubmissionIndices_.size() < kMaximumFramesInFlight) return;
    const WGPUWrappedSubmissionIndex submission{
        gpuContext_->getQueue(), benchmarkSubmissionIndices_.front()};
    static_cast<void>(wgpuDevicePoll(
        gpuContext_->getDevice(), true, &submission));
    benchmarkSubmissionIndices_.pop_front();
#else
    static_cast<void>(drain);
#endif
}

void Application::startScreenshotTour() {
    if (config_.screenshotTourIndices.empty()) {
        return;
    }

    // Ensure output directory exists
    std::error_code ec;
    std::filesystem::create_directories(config_.screenshotTourDir, ec);
    if (ec) {
        LOG_WARN("Failed to create screenshot tour directory '{}': {}", config_.screenshotTourDir.string(), ec.message());
    }

    tourActive_ = true;
    tourStep_ = 0;
    scheduleNextTourStep();
}

void Application::scheduleNextTourStep() {
    if (!tourActive_) {
        return;
    }

    while (tourStep_ < config_.screenshotTourIndices.size()) {
        const int index = config_.screenshotTourIndices[tourStep_];
        if (camera_ && index >= 0
            && static_cast<size_t>(index) < teleportTargets_.size()) {
            const auto& target = teleportTargets_[static_cast<size_t>(index)];
            camera_->setWorldPosition(target.sector, target.position);
            camera_->setYaw(target.yaw);
            camera_->setPitch(target.pitch);
            LOG_INFO(
                "Screenshot tour: teleported to index {} (step {})",
                index, tourStep_);
            tourCurrentPath_ = (
                config_.screenshotTourDir
                / ("view_" + std::to_string(tourStep_) + ".png")).string();
            tourFrameCounter_ = 0;
            return;
        }
        LOG_WARN(
            "Screenshot tour: invalid teleport index {} (step {}), skipping",
            index, tourStep_);
        tourStep_++;
    }

    tourActive_ = false;
    requestExit();
}

void Application::processFrame(float deltaTime) {
    processFrame(deltaTime, deltaTime);
}

void Application::processFrame(float simulationDeltaTime,
                               float frameDeltaTime) {
    // Static frame timer for performance instrumentation
    static perf::FrameTimer frameTimer;
    
    // Begin frame timing
    frameTimer.beginFrame();
    
    beginFrame();
    
    // Update phase
    update(simulationDeltaTime, frameDeltaTime);
    frameTimer.markUpdate();
    
    // Render phase
    render();
    frameTimer.markRender();
    
    // Present phase
    endFrame();
    frameTimer.markPresent();
    
    // End frame timing
    frameTimer.endFrame();

    if (browserJourneyBenchmark_) {
        const perf::FrameStats frameStats = frameTimer.getLastFrameStats();
        browserJourneyBenchmark_->recordFrame({
            .frame = stats_.frameCount,
            .physicsTick = physicsWorld_ ? physicsWorld_->encodedTick() : 0u,
            .phase = perf::BrowserJourneyStatus::Idle,
            .wallMilliseconds = static_cast<float>(stats_.frameTimeMs),
            .cpuMilliseconds = static_cast<float>(frameStats.totalMs),
            .residentBodies = stats_.physicsResidentBodies,
            .activeBodies = stats_.physicsActiveBodies,
            .candidatePairs = stats_.physics.candidatePairUsage.current,
            .contacts = stats_.physics.contactUsage.current,
            .terrainContactBodies = stats_.physics.terrainContactBodies,
            .submittedPrimitives = stats_.primitiveSubmittedCount,
            .capacityOverflowMask =
                (stats_.physics.candidatePairUsage.overflow ? 1u : 0u)
                | (stats_.physics.uniquePairUsage.overflow ? 2u : 0u)
                | (stats_.physics.contactUsage.overflow ? 4u : 0u)
                | (stats_.physics.overflowConstraintUsage.overflow
                       ? 8u : 0u),
            .physicsErrorMask =
                (stats_.physics.invalidManifolds != 0u ? 1u : 0u)
                | (stats_.physics.colorConflictErrors != 0u ? 2u : 0u)
                | (stats_.physics.islandRootErrors != 0u ? 4u : 0u)
                | (stats_.physics.ccdFailures != 0u ? 8u : 0u),
        });
    }
    
    // Update benchmark if running (use frame timer stats)
    if (benchmarkRunner_ && benchmarkRunner_->isRunning()) {
        perf::FrameStats frameStats = frameTimer.getLastFrameStats();
        frameStats.physicsSimulationMs = stats_.physicsSimulationMs;
        frameStats.physicsWaterMs = stats_.physicsWaterMs;
        frameStats.physicsSnapshotMs = stats_.physicsSnapshotMs;
        frameStats.primitiveCullMs = stats_.primitiveCullMs;
        frameStats.primitivePackingMs = stats_.primitivePackingMs;
        frameStats.primitiveUploadMs = stats_.primitiveUploadMs;
        frameStats.primitiveRenderMs = stats_.primitiveRenderMs;
        frameStats.physicsResidentBodies = stats_.physicsResidentBodies;
        frameStats.physicsActiveBodies = stats_.physicsActiveBodies;
        frameStats.physicsActiveBodiesObserved =
            stats_.physicsBackend != physics::BackendType::WebGpuSoft
            || stats_.physics.telemetryTick != 0u;
        const bool stillRunning = benchmarkRunner_->onFrame(frameStats);
        if (!stillRunning && config_.exitAfterBenchmark) {
            requestExit();
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Render Path Control
// ─────────────────────────────────────────────────────────────────────────────

void Application::setRenderPath(RenderPath path) {
    if (path != RenderPath::Triangle && path != RenderPath::Raycast) {
        LOG_ERROR("Ignoring invalid render path {}",
                  static_cast<int>(path));
        return;
    }
    if (config_.renderPath != path) {
        config_.renderPath = path;
        stats_.activeRenderPath = path;
        LOG_INFO("Render path changed to: {}", renderPathToString(path));
    }
}

void Application::toggleRenderPath() {
    switch (config_.renderPath) {
        case RenderPath::Triangle:
            setRenderPath(RenderPath::Raycast);
            break;
        case RenderPath::Raycast:
            setRenderPath(RenderPath::Triangle);
            break;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Uncapped FPS Mode
// ─────────────────────────────────────────────────────────────────────────────

void Application::toggleUncappedFPS() {
    const bool nextUncapped = !uncappedFPS_;
    if (gpuContext_
        && !gpuContext_->setPresentMode(
            nextUncapped ? WGPUPresentMode_Immediate
                         : WGPUPresentMode_Fifo)) {
        LOG_WARN("Requested presentation behavior is unavailable");
        return;
    }
    uncappedFPS_ = nextUncapped;
    config_.vsync = !nextUncapped;
    LOG_INFO("Uncapped FPS mode: {}", uncappedFPS_
        ? "ENABLED (immediate presentation)"
        : "DISABLED (FIFO VSync)");

#if defined(VOXY_WASM)
    // Loop strategy update is handled by the platform entry point (entry.cpp) via isUncappedFPS()
#endif
}

// ─────────────────────────────────────────────────────────────────────────────
// Resize Handling
// ─────────────────────────────────────────────────────────────────────────────

void Application::onResize(uint32_t width, uint32_t height) {
    LOG_DEBUG("Application resize: {}x{}", width, height);
    
    // Ignore zero-size (minimized window)
    if (width == 0 || height == 0) {
        return;
    }
    const uint32_t renderWidth =
        scaledRenderExtent(width, config_.resolutionScale);
    const uint32_t renderHeight =
        scaledRenderExtent(height, config_.resolutionScale);

    // The benchmark color attachment must remain the same size as the depth
    // attachment. Its replacement is transactional, so stop before committing
    // any other size when allocation fails.
    if (config_.benchmarkOnStartup
        && !createBenchmarkTarget(renderWidth, renderHeight)) {
        LOG_ERROR("Failed to resize the benchmark render target to {}x{}",
                  renderWidth, renderHeight);
        return;
    }

    if (gpuContext_
        && !gpuContext_->resizeSwapchain(renderWidth, renderHeight)) {
        LOG_ERROR("Failed to resize the swapchain to {}x{}",
                  renderWidth, renderHeight);
        return;
    }

    bool raycastResizeSucceeded = true;
    if (raycastPath_) {
        raycastResizeSucceeded =
            raycastPath_->resize(renderWidth, renderHeight);
    }
    if (raycastResizeSucceeded && blitPath_) {
        raycastResizeSucceeded =
            blitPath_->resize(renderWidth, renderHeight);
    }
    if (raycastPath_) {
        // Rebind every borrowed view after a successful ray-output replacement.
        // This is also required when the later blit-cache allocation failed.
        if (blitPath_) {
            blitPath_->setDepthTexture(raycastPath_->getDepthOutputView());
            blitPath_->setShadowTexture(raycastPath_->getShadowOutputView());
            blitPath_->setMaterialTexture(raycastPath_->getMaterialOutputView());
            blitPath_->setStaticTerrainTextures(
                raycastPath_->getTerrainDepthCacheView(),
                raycastPath_->getTerrainShadowCacheView());
        }
        if (primitivePath_) {
            primitivePath_->setRayDepthTexture(raycastPath_->getDepthOutputView());
        }
        if (meshPath_) {
            meshPath_->setRayDepthTexture(raycastPath_->getDepthOutputView());
        }
    }

    if (!raycastResizeSucceeded) {
        LOG_ERROR(
            "Raycast renderer resize failed at {}x{}; using the independent triangle path",
            renderWidth, renderHeight);
        if (config_.renderPath == RenderPath::Raycast) {
            setRenderPath(RenderPath::Triangle);
        }
    }

    // Projection must match the actual render target after independent extent
    // rounding, especially at fractional resolution scales.
    if (camera_) {
        camera_->setAspectRatio(renderWidth, renderHeight);
    }
    
    // Invalidate depth buffer for triangle path
    depthWidth_ = 0;
    depthHeight_ = 0;
}

bool Application::setRendererSetting(std::string_view name, double value,
                                     bool commit) {
    if (!std::isfinite(value)) return false;
    uint32_t dirty = RendererUniformsDirty;
    auto setScalar = [&](float& destination, float minimum, float maximum) {
        destination = finiteClamp(value, minimum, maximum);
    };
    auto setColor = [&](float& destination) {
        setScalar(destination, 0.0f, 4.0f);
    };

    if (name == "render.path") {
        if (value != 0.0 && value != 1.0) return false;
        setRenderPath(value == 0.0 ? RenderPath::Triangle
                                   : RenderPath::Raycast);
    } else if (name == "lighting.sunAzimuth") {
        const float azimuth = finiteClamp(value, -180.0f, 180.0f);
        rendererSettings_.sunDirection = sunDirectionFromDegrees(
            azimuth, sunElevationDegrees(rendererSettings_.sunDirection));
        if (commit) dirty |= RendererSunShadowDirty;
    } else if (name == "lighting.sunElevation") {
        const float elevation = finiteClamp(value, 1.0f, 89.0f);
        rendererSettings_.sunDirection = sunDirectionFromDegrees(
            sunAzimuthDegrees(rendererSettings_.sunDirection), elevation);
        if (commit) dirty |= RendererSunShadowDirty;
    } else if (name == "lighting.sunColor.r") {
        setColor(rendererSettings_.sunColor.r);
    } else if (name == "lighting.sunColor.g") {
        setColor(rendererSettings_.sunColor.g);
    } else if (name == "lighting.sunColor.b") {
        setColor(rendererSettings_.sunColor.b);
    } else if (name == "lighting.sunIntensity") {
        setScalar(rendererSettings_.sunIntensity, 0.0f, 10.0f);
    } else if (name == "lighting.ambientColor.r") {
        setColor(rendererSettings_.ambientColor.r);
    } else if (name == "lighting.ambientColor.g") {
        setColor(rendererSettings_.ambientColor.g);
    } else if (name == "lighting.ambientColor.b") {
        setColor(rendererSettings_.ambientColor.b);
    } else if (name == "lighting.ambientIntensity") {
        setScalar(rendererSettings_.ambientIntensity, 0.0f, 4.0f);
    } else if (name == "lighting.fogColor.r") {
        setColor(rendererSettings_.fogColor.r);
    } else if (name == "lighting.fogColor.g") {
        setColor(rendererSettings_.fogColor.g);
    } else if (name == "lighting.fogColor.b") {
        setColor(rendererSettings_.fogColor.b);
    } else if (name == "lighting.fogDensity") {
        setScalar(rendererSettings_.fogDensity, 0.0f, 0.01f);
    } else if (name == "lighting.exposure") {
        setScalar(rendererSettings_.exposure, 0.05f, 8.0f);
    } else if (name == "water.enabled") {
        rendererSettings_.waterEnabled = value >= 0.5;
        dirty |= RendererWaterPhysicsDirty;
    } else if (name == "water.height") {
        setScalar(rendererSettings_.waterHeight, -4000.0f, 4000.0f);
        dirty |= RendererWaterPhysicsDirty;
        if (commit) dirty |= RendererWaterCoastDirty;
    } else if (name == "water.shallowColor.r") {
        setColor(rendererSettings_.waterShallowColor.r);
    } else if (name == "water.shallowColor.g") {
        setColor(rendererSettings_.waterShallowColor.g);
    } else if (name == "water.shallowColor.b") {
        setColor(rendererSettings_.waterShallowColor.b);
    } else if (name == "water.deepColor.r") {
        setColor(rendererSettings_.waterDeepColor.r);
    } else if (name == "water.deepColor.g") {
        setColor(rendererSettings_.waterDeepColor.g);
    } else if (name == "water.deepColor.b") {
        setColor(rendererSettings_.waterDeepColor.b);
    } else if (name == "water.roughness") {
        setScalar(rendererSettings_.waterRoughness, 0.02f, 1.0f);
    } else if (name == "water.waveStrength") {
        setScalar(rendererSettings_.waterWaveStrength, 0.0f, 2.0f);
        dirty |= RendererWaterPhysicsDirty;
    } else if (name == "water.reflectionStrength") {
        setScalar(rendererSettings_.waterReflectionStrength, 0.0f, 1.0f);
    } else if (name == "water.shoreFade") {
        setScalar(rendererSettings_.waterShoreFade, 0.01f, 500.0f);
    } else if (name == "water.ior") {
        setScalar(rendererSettings_.waterIor, 1.0f, 2.0f);
    } else if (name == "water.distortion") {
        setScalar(rendererSettings_.waterDistortion, 0.0f, 1.0f);
    } else if (name == "water.absorptionScale") {
        setScalar(rendererSettings_.waterAbsorptionScale, 0.0f, 5.0f);
    } else if (name == "water.scatterStrength") {
        setScalar(rendererSettings_.waterScatterStrength, 0.0f, 5.0f);
    } else if (name == "water.foamSize") {
        setScalar(rendererSettings_.waterFoamSize, 8.0f, 2000.0f);
    } else if (name == "water.foamOpacity") {
        setScalar(rendererSettings_.waterFoamOpacity, 0.0f, 1.0f);
    } else if (name == "water.foamCoverage") {
        setScalar(rendererSettings_.waterFoamCoverage, 0.0f, 1.0f);
    } else if (name == "water.reflectionDistance") {
        setScalar(rendererSettings_.waterReflectionDistance, 10.0f, 20000.0f);
    } else if (name == "water.spectrum.significantHeight") {
        setScalar(rendererSettings_.waterSpectrum.significantWaveHeight, 0.1f, 100.0f);
        if (commit) dirty |= RendererWaterSpectrumDirty;
    } else if (name == "water.spectrum.direction") {
        setScalar(rendererSettings_.waterSpectrum.directionDegrees, -180.0f, 180.0f);
        if (commit) dirty |= RendererWaterSpectrumDirty;
    } else if (name == "water.spectrum.choppiness") {
        setScalar(rendererSettings_.waterSpectrum.choppiness, 0.0f, 5.0f);
        dirty |= RendererWaterSpectrumDirty;
    } else if (name == "water.spectrum.peakEnhancement") {
        setScalar(rendererSettings_.waterSpectrum.peakEnhancement, 0.05f, 10.0f);
        if (commit) dirty |= RendererWaterSpectrumDirty;
    } else if (name == "water.spectrum.windAlignment") {
        setScalar(rendererSettings_.waterSpectrum.windAlignment, 0.0f, 1.0f);
        if (commit) dirty |= RendererWaterSpectrumDirty;
    } else if (name == "water.spectrum.speed") {
        setScalar(rendererSettings_.waterSpectrum.animationSpeed, 0.0f, 5.0f);
        dirty |= RendererWaterSpectrumDirty;
    } else if (name == "water.spectrum.largePatch") {
        setScalar(rendererSettings_.waterSpectrum.patchLengths.x, 64.0f, 8192.0f);
        if (commit) dirty |= RendererWaterSpectrumDirty;
    } else if (name == "water.spectrum.detailPatch") {
        setScalar(rendererSettings_.waterSpectrum.patchLengths.y, 16.0f, 2048.0f);
        if (commit) dirty |= RendererWaterSpectrumDirty;
    } else if (name == "water.spectrum.largeAmplitude") {
        setScalar(rendererSettings_.waterSpectrum.cascadeAmplitudes.x, 0.0f, 2.0f);
        dirty |= RendererWaterSpectrumDirty;
    } else if (name == "water.spectrum.detailAmplitude") {
        setScalar(rendererSettings_.waterSpectrum.cascadeAmplitudes.y, 0.0f, 2.0f);
        dirty |= RendererWaterSpectrumDirty;
    } else if (name == "water.spectrum.directionalSine") {
        setScalar(rendererSettings_.waterSpectrum.directionalSineScale, 0.0f, 1.5f);
        dirty |= RendererWaterSpectrumDirty;
    } else if (name == "camera.fov") {
        setScalar(rendererSettings_.cameraFovDegrees, 20.0f, 120.0f);
        dirty |= RendererCameraDirty;
    } else if (name == "camera.near") {
        setScalar(rendererSettings_.cameraNear, 0.01f,
                  std::max(rendererSettings_.cameraFar - 0.01f, 0.01f));
        dirty |= RendererCameraDirty;
    } else if (name == "camera.far") {
        setScalar(rendererSettings_.cameraFar,
                  rendererSettings_.cameraNear + 0.01f, 100000.0f);
        dirty |= RendererCameraDirty;
    } else if (name == "camera.moveSpeed") {
        setScalar(rendererSettings_.cameraMoveSpeed, 0.1f, 1000.0f);
        dirty |= RendererCameraDirty;
    } else if (name == "camera.mouseSensitivity") {
        setScalar(rendererSettings_.cameraMouseSensitivity, 0.00001f, 0.02f);
        dirty |= RendererCameraDirty;
    } else if (name == "camera.eyeHeight") {
        setScalar(rendererSettings_.cameraEyeHeight, 0.5f, 10.0f);
        dirty |= RendererCameraDirty;
    } else {
        return false;
    }

    rendererSettingsDirty_ |= dirty;
    ++rendererSettingsRevision_;
    return true;
}

std::optional<double> Application::getRendererSetting(
    std::string_view name) const noexcept {
    const auto& s = rendererSettings_;
    if (name == "render.path") {
        return static_cast<double>(config_.renderPath);
    }
    if (name == "lighting.sunAzimuth") return sunAzimuthDegrees(s.sunDirection);
    if (name == "lighting.sunElevation") return sunElevationDegrees(s.sunDirection);
    if (name == "lighting.sunColor.r") return s.sunColor.r;
    if (name == "lighting.sunColor.g") return s.sunColor.g;
    if (name == "lighting.sunColor.b") return s.sunColor.b;
    if (name == "lighting.sunIntensity") return s.sunIntensity;
    if (name == "lighting.ambientColor.r") return s.ambientColor.r;
    if (name == "lighting.ambientColor.g") return s.ambientColor.g;
    if (name == "lighting.ambientColor.b") return s.ambientColor.b;
    if (name == "lighting.ambientIntensity") return s.ambientIntensity;
    if (name == "lighting.fogColor.r") return s.fogColor.r;
    if (name == "lighting.fogColor.g") return s.fogColor.g;
    if (name == "lighting.fogColor.b") return s.fogColor.b;
    if (name == "lighting.fogDensity") return s.fogDensity;
    if (name == "lighting.exposure") return s.exposure;
    if (name == "water.enabled") return s.waterEnabled ? 1.0 : 0.0;
    if (name == "water.height") return s.waterHeight;
    if (name == "water.shallowColor.r") return s.waterShallowColor.r;
    if (name == "water.shallowColor.g") return s.waterShallowColor.g;
    if (name == "water.shallowColor.b") return s.waterShallowColor.b;
    if (name == "water.deepColor.r") return s.waterDeepColor.r;
    if (name == "water.deepColor.g") return s.waterDeepColor.g;
    if (name == "water.deepColor.b") return s.waterDeepColor.b;
    if (name == "water.roughness") return s.waterRoughness;
    if (name == "water.waveStrength") return s.waterWaveStrength;
    if (name == "water.reflectionStrength") return s.waterReflectionStrength;
    if (name == "water.shoreFade") return s.waterShoreFade;
    if (name == "water.ior") return s.waterIor;
    if (name == "water.distortion") return s.waterDistortion;
    if (name == "water.absorptionScale") return s.waterAbsorptionScale;
    if (name == "water.scatterStrength") return s.waterScatterStrength;
    if (name == "water.foamSize") return s.waterFoamSize;
    if (name == "water.foamOpacity") return s.waterFoamOpacity;
    if (name == "water.foamCoverage") return s.waterFoamCoverage;
    if (name == "water.reflectionDistance") return s.waterReflectionDistance;
    if (name == "water.spectrum.significantHeight") return s.waterSpectrum.significantWaveHeight;
    if (name == "water.spectrum.direction") return s.waterSpectrum.directionDegrees;
    if (name == "water.spectrum.choppiness") return s.waterSpectrum.choppiness;
    if (name == "water.spectrum.peakEnhancement") return s.waterSpectrum.peakEnhancement;
    if (name == "water.spectrum.windAlignment") return s.waterSpectrum.windAlignment;
    if (name == "water.spectrum.speed") return s.waterSpectrum.animationSpeed;
    if (name == "water.spectrum.largePatch") return s.waterSpectrum.patchLengths.x;
    if (name == "water.spectrum.detailPatch") return s.waterSpectrum.patchLengths.y;
    if (name == "water.spectrum.largeAmplitude") return s.waterSpectrum.cascadeAmplitudes.x;
    if (name == "water.spectrum.detailAmplitude") return s.waterSpectrum.cascadeAmplitudes.y;
    if (name == "water.spectrum.directionalSine") return s.waterSpectrum.directionalSineScale;
    if (name == "camera.fov") return s.cameraFovDegrees;
    if (name == "camera.near") return s.cameraNear;
    if (name == "camera.far") return s.cameraFar;
    if (name == "camera.moveSpeed") return s.cameraMoveSpeed;
    if (name == "camera.mouseSensitivity") return s.cameraMouseSensitivity;
    if (name == "camera.eyeHeight") return s.cameraEyeHeight;
    return std::nullopt;
}

void Application::applyRendererSettings() {
    if (rendererSettingsDirty_ == 0u) return;
    const uint32_t dirty = rendererSettingsDirty_;
    rendererSettingsDirty_ = 0u;

    if ((dirty & RendererCameraDirty) != 0u) {
        if (camera_) {
            camera_->setFovYDegrees(rendererSettings_.cameraFovDegrees);
            camera_->setClipPlanes(rendererSettings_.cameraNear,
                                   rendererSettings_.cameraFar);
        }
        if (freeFlyController_) {
            freeFlyController_->setBaseSpeed(rendererSettings_.cameraMoveSpeed);
            freeFlyController_->setMouseSensitivity(
                rendererSettings_.cameraMouseSensitivity);
        }
        if (characterController_) {
            CharacterConfig character = characterController_->config();
            character.walkSpeed = rendererSettings_.cameraMoveSpeed;
            character.runSpeed = rendererSettings_.cameraMoveSpeed * 2.0f;
            character.mouseSensitivity = rendererSettings_.cameraMouseSensitivity;
            character.groundOffset = rendererSettings_.cameraEyeHeight;
            character.collisionHeight = std::max(
                rendererSettings_.cameraEyeHeight, 0.82f);
            characterController_->setConfig(character);
        }
    }

    if ((dirty & RendererWaterSpectrumDirty) != 0u && waterSimulation_) {
        if (!waterSimulation_->reconfigure(makeWaterSpectrumConfig(
                rendererSettings_.waterSpectrum))) {
            LOG_ERROR("Runtime water-spectrum update failed");
            // Keep uniforms and CPU/GPU physics matched to the spectrum which
            // is still live after the transactional rebuild failed.
            rendererSettings_.waterSpectrum = makeWaterSpectrumSettings(
                waterSimulation_->spectrumConfig());
        }
        updateWaterPhysicsBindings();
    } else if ((dirty & RendererWaterPhysicsDirty) != 0u) {
        updateWaterPhysicsBindings();
    }

    if ((dirty & RendererWaterCoastDirty) != 0u &&
        !rebuildWaterCoastField()) {
        LOG_ERROR("Runtime coastal-field update failed");
        rendererSettings_.waterHeight = appliedWaterCoastHeight_;
        updateWaterPhysicsBindings();
    }
    if ((dirty & RendererSunShadowDirty) != 0u &&
        !rebuildSunShadowMap()) {
        LOG_ERROR("Runtime sun-shadow update failed");
        rendererSettings_.sunDirection = appliedShadowSunDirection_;
    }

    appliedRendererSettingsRevision_ = rendererSettingsRevision_;
}

void Application::updateWaterPhysicsBindings() {
    if (!physicsWorld_) return;
    physicsWorld_->setWaterPlane(rendererSettings_.waterHeight,
                                 rendererSettings_.waterEnabled);
    if (!waterSimulation_) return;

    const float strength = rendererSettings_.waterWaveStrength;
    if (physicsWorld_->backendType() == physics::BackendType::WebGpuSoft) {
        physicsWorld_->setWaterGpuResources({
            waterSimulation_->getOutputView(), waterSimulation_->getSampler(),
            strength,
            rendererSettings_.waterSpectrum.patchLengths.x,
            rendererSettings_.waterSpectrum.patchLengths.y});
    } else {
        physicsWorld_->setWaterSurfaceSampler(
            [simulation = waterSimulation_.get(), strength](
                glm::vec2 position, float timeSeconds) {
                const auto sample = simulation->sampleSurface(
                    position, timeSeconds, strength);
                return physics::PhysicsWorld::WaterSurfaceSample{
                    sample.heightOffset, sample.slope, sample.velocity};
            });
    }
}

bool Application::rebuildWaterCoastField() {
    if (!waterSimulation_ || !heightmap_) return false;
    if (!waterSimulation_->rebuildCoastField(
            heightmap_->getData(), heightmap_->getWidth(),
            heightmap_->getHeight(), config_.heightScale,
            config_.cellScale, rendererSettings_.waterHeight)) {
        return false;
    }
    if (raycastPath_) {
        raycastPath_->setWaterSimulation(
            waterSimulation_->getOutputView(),
            waterSimulation_->getCoastView(),
            waterSimulation_->getSampler());
    }
    appliedWaterCoastHeight_ = rendererSettings_.waterHeight;
    return true;
}

bool Application::rebuildSunShadowMap() {
    if (!gpuContext_ || !heightmap_ || !raycastPath_) return false;

    perf::Timer bakeTimer;
    bakeTimer.start();
    terrain::ShadowBakeConfig bakeConfig;
    bakeConfig.lightDir = rendererSettings_.sunDirection;
    bakeConfig.heightScale = config_.heightScale;
    bakeConfig.cellScale = config_.cellScale;
    const auto baked = terrain::bakeShadowHeightField(
        heightmap_->getData(), heightmap_->getWidth(),
        heightmap_->getHeight(), bakeConfig);
    if (baked.data.empty()) return false;

    const WGPUDevice device = gpuContext_->getDevice();
    const WGPUQueue queue = gpuContext_->getQueue();
    gpu::TextureDesc desc = gpu::TextureDesc::tex2D(
        baked.width, baked.height, WGPUTextureFormat_R16Uint,
        WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst,
        "baked_shadow_height_runtime");
    WGPUTexture nextTexture = gpu::createTextureWithData(
        device, queue, desc,
        std::as_bytes(std::span<const uint16_t>(baked.data)),
        baked.width * sizeof(uint16_t));
    if (!nextTexture) return false;
    WGPUTextureView nextView = gpu::createTextureView(nextTexture);
    if (!nextView) {
        wgpuTextureRelease(nextTexture);
        return false;
    }

    raycastPath_->setShadowMap(nextView);
    if (blitPath_ && waterSimulation_) {
        blitPath_->setWaterCompositeResources(
            heightmap_->getTextureView(), nextView,
            waterSimulation_->getOutputView(),
            waterSimulation_->getSampler());
    }

    if (shadowMapView_) wgpuTextureViewRelease(shadowMapView_);
    if (shadowMapTexture_) wgpuTextureRelease(shadowMapTexture_);
    shadowMapTexture_ = nextTexture;
    shadowMapView_ = nextView;
    appliedShadowSunDirection_ = rendererSettings_.sunDirection;
    LOG_INFO("Rebuilt sun shadow field: {}x{} ({:.1f} ms)",
             baked.width, baked.height, bakeTimer.elapsedMs());
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Debug Visualization Control
// ─────────────────────────────────────────────────────────────────────────────

void Application::setDebugVisMode(DebugVisMode mode) {
    if (debugVisMode_ != mode) {
        debugVisMode_ = mode;
        LOG_INFO("Debug visualization mode: {}", debugVisModeToString(mode));
        
        // Update blit path with new debug mode
        if (blitPath_ && blitPath_->isInitialized()) {
            blitPath_->setDebugMode(static_cast<uint32_t>(mode));
        }
    }
}

void Application::cycleDebugVisMode() {
    uint32_t nextMode = (static_cast<uint32_t>(debugVisMode_) + 1) % 4;
    setDebugVisMode(static_cast<DebugVisMode>(nextMode));
}

void Application::toggleWireframe() {
    wireframeEnabled_ = !wireframeEnabled_;
    LOG_INFO("Wireframe mode: {}", wireframeEnabled_ ? "enabled" : "disabled");
    
    // Update triangle path wireframe state
    if (trianglePath_ && trianglePath_->isInitialized()) {
        trianglePath_->setWireframe(wireframeEnabled_);
    }
}

void Application::toggleLegoMode() {
    legoMode_ = !legoMode_;
    LOG_INFO("Lego mode: {}", legoMode_ ? "enabled" : "disabled");
    // Updates will be propagated in updateCameraUniforms()
}

// ─────────────────────────────────────────────────────────────────────────────
// Benchmark Mode
// ─────────────────────────────────────────────────────────────────────────────

bool Application::spawnBenchmarkBodies() {
    const uint32_t bodyCount = config_.benchmarkBodyCount;
    if (bodyCount == 0u) return true;
    if (!physicsWorld_ || !characterController_) return false;

    uint32_t columns = 1u;
    while (uint64_t{columns} * columns < bodyCount) ++columns;
    const uint32_t rows = (bodyCount + columns - 1u) / columns;
    constexpr float spacing = 2.2f;
    const float halfColumns = 0.5f * static_cast<float>(columns - 1u);
    const float halfRows = 0.5f * static_cast<float>(rows - 1u);
    constexpr uint32_t shapeCount = static_cast<uint32_t>(
        physics::ThrowableShape::Count);

    for (uint32_t index = 0u; index < bodyCount; ++index) {
        const uint32_t column = index % columns;
        const uint32_t row = index / columns;
        const float x = (static_cast<float>(column) - halfColumns) * spacing;
        const float z = (static_cast<float>(row) - halfRows) * spacing;
        const float terrainHeight =
            characterController_->sampleTerrainHeight(x, z);

        physics::BodySpawnDesc body;
        body.shape = static_cast<physics::ThrowableShape>(index % shapeCount);
        // Keep every body active for the complete five-scenario benchmark.
        // At 400 Hz the 1,500 frames advance 3.75 simulated seconds; the
        // initial downward velocity plus gravity covers less than 73 metres.
        body.position = {x, terrainHeight + 100.0f, z};
        body.linearVelocity = {
            static_cast<float>(static_cast<int32_t>(index % 7u) - 3) * 0.2f,
            -1.0f,
            static_cast<float>(static_cast<int32_t>(index % 5u) - 2) * 0.2f};
        body.angularVelocity = {0.1f, 0.2f, 0.05f};
        body.dimensions = physics::throwableShapeDimensions(body.shape);
        if (!physicsWorld_->spawnBody(body).valid()) return false;
    }

    LOG_INFO("Spawned {} benchmark bodies 100 m above terrain with linear and angular velocity",
             bodyCount);
    return true;
}

bool Application::spawnCubePyramidExperiment() {
    const uint32_t bodyCount = config_.cubePyramidBodyCount;
    if (bodyCount == 0u) return true;
    if (!physicsWorld_ || !characterController_ || !camera_) return false;

    // One cube of thickness. Row widths are 2n, 2(n-1), ... 2, so a complete
    // n-row wall contains n(n+1) cubes and has 45-degree sides.
    constexpr float cubeSize = kCubeTriangleCubeSize;
    // Leave 160 mm between columns, safely outside the 20 mm
    // speculative-contact distance. A single impact then wakes the struck
    // stacks instead of instantly turning all 20k staged cubes into one
    // solver island. The explicit 128-projectile benchmark still exercises
    // a wide collapse.
    constexpr float horizontalPitch = kCubeTriangleHorizontalPitch;
    // Exact face contact keeps the staged towers physically supported. Before
    // an impact, selected complete columns are woken together so removing a
    // lower cube cannot strand a still-sleeping cube above it.
    constexpr float verticalPitch = cubeSize;
    constexpr float terrainClearance = 0.05f;

    struct TriangleRow {
        uint32_t width = 0u;
        uint32_t bodies = 0u;
    };
    std::vector<TriangleRow> rows;
    if (bodyCount == kDefaultCubeTriangleBodyCount) {
        // 141 * 142 = 20,022: the closest n(n+1) triangle to 20,000.
        static_assert(
            kDefaultCubeTriangleRowCount
                * (kDefaultCubeTriangleRowCount + 1u)
            == kDefaultCubeTriangleBodyCount);
        rows.reserve(kDefaultCubeTriangleRowCount);
        for (uint32_t halfWidth = kDefaultCubeTriangleRowCount;
             halfWidth != 0u; --halfWidth) {
            const uint32_t width = 2u * halfWidth;
            rows.push_back({width, width});
        }
    } else {
        const auto completeTriangleBodies = [](uint32_t rowCount) {
            return uint64_t{rowCount} * (rowCount + 1u);
        };
        uint32_t rowCount = 1u;
        while (completeTriangleBodies(rowCount) < bodyCount) ++rowCount;
        rows.reserve(rowCount);
        uint32_t remaining = bodyCount;
        for (uint32_t halfWidth = rowCount;
             remaining != 0u; --halfWidth) {
            const uint32_t width = 2u * halfWidth;
            const uint32_t capacity = width;
            const uint32_t rowBodies = std::min(remaining, capacity);
            rows.push_back({width, rowBodies});
            remaining -= rowBodies;
        }
    }
    if (rows.empty()) return false;
    const uint32_t baseWidth = rows.front().width;
    cubeTriangleColumns_.assign(baseWidth, {});
    cubeTriangleColumnsWokenThisTick_.assign(baseWidth, 0u);
    cubeTriangleWakeDedupTick_ = std::numeric_limits<uint64_t>::max();

    const float halfWidth =
        0.5f * static_cast<float>(baseWidth - 1u) * horizontalPitch
        + cubeSize * 0.5f;
    const float halfDepth = cubeSize * 0.5f;

    // Find a horizontal plane above the complete footprint. The experiment
    // already flattened this runtime heightmap for render and collision. The
    // static bottom row remains a cube-built foundation, with no hidden shape.
    float maximumTerrainHeight = -std::numeric_limits<float>::infinity();
    const uint32_t xSampleIntervals = std::max(baseWidth * 2u, 1u);
    constexpr uint32_t zSampleIntervals = 2u;
    for (uint32_t z = 0u; z <= zSampleIntervals; ++z) {
        const float zOffset = -halfDepth + 2.0f * halfDepth
                * static_cast<float>(z)
                / static_cast<float>(zSampleIntervals);
        for (uint32_t x = 0u; x <= xSampleIntervals; ++x) {
            const float xOffset = -halfWidth + 2.0f * halfWidth
                    * static_cast<float>(x)
                    / static_cast<float>(xSampleIntervals);
            const float height = characterController_->sampleTerrainHeight(
                kBrowserJourneyTargetX + xOffset,
                kBrowserJourneyTargetZ + zOffset);
            if (std::isfinite(height)) {
                maximumTerrainHeight =
                    std::max(maximumTerrainHeight, height);
            }
        }
    }
    if (!std::isfinite(maximumTerrainHeight)) return false;

    const float baseCenterY =
        maximumTerrainHeight + terrainClearance + cubeSize * 0.5f;
    const physics::PhysicsMaterial triangleMaterial{
        .friction = 0.85f,
        .restitution = 0.0f,
        .rollingResistance = 0.02f,
        .density = 1.0f,
        .flags = 0u,
    };

    uint32_t spawned = 0u;
    std::vector<physics::PhysicsCommand> initialSleepCommands;
    initialSleepCommands.reserve(
        bodyCount - std::min(bodyCount, rows.front().bodies));
    for (size_t row = 0u; row < rows.size(); ++row) {
        const uint32_t width = rows[row].width;
        const uint32_t capacity = width;
        const uint32_t rowBodies = rows[row].bodies;
        std::vector<uint32_t> slots(capacity);
        for (uint32_t slot = 0u; slot < capacity; ++slot) {
            slots[slot] = slot;
        }

        // Exploratory non-20k counts can end in a partial row. Fill it from
        // the center out so the visible triangle stays balanced.
        if (rowBodies != capacity) {
            std::stable_sort(
                slots.begin(), slots.end(),
                [width](uint32_t lhs, uint32_t rhs) {
                    const auto distanceFromCenter = [width](uint32_t slot) {
                        return std::abs(
                            static_cast<int32_t>(2u * slot)
                            - static_cast<int32_t>(width - 1u));
                    };
                    const int32_t lhsDistance = distanceFromCenter(lhs);
                    const int32_t rhsDistance = distanceFromCenter(rhs);
                    return lhsDistance != rhsDistance
                        ? lhsDistance < rhsDistance : lhs < rhs;
                });
        }

        const float rowHalf =
            0.5f * static_cast<float>(width - 1u) * horizontalPitch;
        for (uint32_t index = 0u; index < rowBodies; ++index) {
            const uint32_t slot = slots[index];
            const uint32_t column = slot;
            const uint32_t foundationColumn =
                static_cast<uint32_t>(row) + column;

            physics::BodySpawnDesc body;
            body.shape = physics::ThrowableShape::Cube;
            body.position = {
                kBrowserJourneyTargetX
                    + static_cast<float>(column) * horizontalPitch
                    - rowHalf,
                baseCenterY + static_cast<float>(row) * verticalPitch,
                kBrowserJourneyTargetZ,
            };
            body.dimensions = glm::vec3(cubeSize);
            body.inverseMass = row == 0u ? 0.0f : 1.0f;
            body.material = triangleMaterial;
            const physics::BodyHandle handle = physicsWorld_->spawnBody(body);
            if (!handle.valid()) {
                LOG_ERROR(
                    "Cube triangle stopped at body {} of {}",
                    spawned, bodyCount);
                return false;
            }
            if (row != 0u) {
                cubeTriangleColumns_[foundationColumn].push_back(handle);
                physics::PhysicsCommand sleep;
                sleep.type = physics::PhysicsCommandType::Sleep;
                sleep.body = handle;
                initialSleepCommands.push_back(sleep);
            }
            ++spawned;
        }

    }
    physicsWorld_->enqueue(initialSleepCommands);

    const float triangleHeight =
        cubeSize + static_cast<float>(rows.size() - 1u) * verticalPitch;
    const float viewDistance = std::max(80.0f, halfWidth * 1.45f);
    const glm::dvec3 target{
        kBrowserJourneyTargetX,
        baseCenterY + triangleHeight * 0.46f,
        kBrowserJourneyTargetZ};
    const glm::dvec3 position{
        kBrowserJourneyTargetX - halfWidth * 0.12f,
        baseCenterY + triangleHeight * 0.52f,
        kBrowserJourneyTargetZ - viewDistance};
    setCameraWorldPose(*camera_, position, target);
    controllerMode_ = ControllerMode::FreeFly;
    stats_.activeController = controllerMode_;
    selectedThrowable_ =
        static_cast<uint32_t>(physics::ThrowableShape::Cube);

    LOG_INFO(
        "Cube triangle experiment: {} cubes, {} base width, {} rows, one cube thick, {} static foundation cubes",
        spawned, baseWidth, rows.size(), rows.front().bodies);
    return spawned == bodyCount;
}

void Application::wakeCubeTriangleImpactColumns(double impactWorldX) {
    if (!physicsWorld_ || cubeTriangleColumns_.empty()
        || !std::isfinite(impactWorldX)) {
        return;
    }

    const uint64_t tick = physicsWorld_->encodedTick();
    if (cubeTriangleWakeDedupTick_ != tick) {
        std::fill(
            cubeTriangleColumnsWokenThisTick_.begin(),
            cubeTriangleColumnsWokenThisTick_.end(), 0u);
        cubeTriangleWakeDedupTick_ = tick;
    }

    const double rowHalf = 0.5
        * static_cast<double>(cubeTriangleColumns_.size() - 1u)
        * static_cast<double>(kCubeTriangleHorizontalPitch);
    const double columnPosition =
        (impactWorldX - static_cast<double>(kBrowserJourneyTargetX)
            + rowHalf)
        / static_cast<double>(kCubeTriangleHorizontalPitch);
    const int64_t centerColumn =
        static_cast<int64_t>(std::llround(columnPosition));
    const int64_t firstColumn = std::max<int64_t>(
        0, centerColumn
            - static_cast<int64_t>(kCubeTriangleImpactWakeRadiusColumns));
    const int64_t lastColumn = std::min<int64_t>(
        static_cast<int64_t>(cubeTriangleColumns_.size()) - 1,
        centerColumn
            + static_cast<int64_t>(kCubeTriangleImpactWakeRadiusColumns));
    if (firstColumn > lastColumn) return;

    std::vector<physics::PhysicsCommand> commands;
    for (int64_t column = firstColumn; column <= lastColumn; ++column) {
        const size_t index = static_cast<size_t>(column);
        if (cubeTriangleColumnsWokenThisTick_[index] != 0u) continue;
        cubeTriangleColumnsWokenThisTick_[index] = 1u;
        for (const physics::BodyHandle body : cubeTriangleColumns_[index]) {
            physics::PhysicsCommand wake;
            wake.type = physics::PhysicsCommandType::Wake;
            wake.body = body;
            commands.push_back(wake);
        }
    }
    if (!commands.empty()) {
        physicsWorld_->enqueue(commands);
    }
}

bool Application::startCubePyramidExperiment() {
    if (!initialized_) {
        LOG_ERROR("Cannot create the cube triangle before application initialization");
        return false;
    }
    if (config_.cubePyramidBodyCount == 0u) {
        LOG_ERROR("No cube triangle was configured at application initialization");
        return false;
    }
    if (cubePyramidSpawnAttempted_) return cubePyramidSpawned_;

    cubePyramidSpawnAttempted_ = true;
    cubePyramidSpawned_ = spawnCubePyramidExperiment();
    return cubePyramidSpawned_;
}

void Application::startBenchmark() {
    if (!benchmarkRunner_) {
        benchmarkRunner_ = std::make_unique<perf::BenchmarkRunner>();
        
        // Set up camera callback for benchmark
        benchmarkRunner_->setCameraCallback([this](const glm::vec3& pos, const glm::vec3& target) {
            if (camera_) {
                camera_->setWorldPosition(glm::ivec3(0), pos);
                camera_->lookAt(target);
            }
        });
    }

    benchmarkRunner_->setPhysicsBackend(
        physicsWorld_
            ? physics::backendTypeName(physicsWorld_->backendType())
            : "none");
    benchmarkRunner_->setExpectedBodyCount(config_.benchmarkBodyCount);
    benchmarkRunner_->setMinimumThroughputFps(config_.benchmarkMinimumFps);
    if (config_.benchmarkFixedDeltaSeconds > 0.0f) {
        LOG_INFO("Benchmark scripted time step: {:.6f} s ({:.1f} Hz)",
                 config_.benchmarkFixedDeltaSeconds,
                 1.0f / config_.benchmarkFixedDeltaSeconds);
    }
    benchmarkRunner_->start();
    LOG_INFO("Benchmark started");
}

void Application::stopBenchmark() {
    if (benchmarkRunner_ && benchmarkRunner_->isRunning()) {
        benchmarkRunner_->stop();
        LOG_INFO("Benchmark stopped");
    }
}

bool Application::isBenchmarkRunning() const noexcept {
    return benchmarkRunner_ && benchmarkRunner_->isRunning();
}

bool Application::benchmarkPassed() const noexcept {
    return benchmarkRunner_ && benchmarkRunner_->passed();
}

void Application::toggleBenchmark() {
    if (isBenchmarkRunning()) {
        stopBenchmark();
    } else {
        startBenchmark();
    }
}

bool Application::startBrowserJourneyBenchmark(
    uint32_t targetBodies, uint32_t warmupTicks, uint32_t impactTicks,
    uint32_t settleTicks, uint32_t bodiesPerVolley,
    uint32_t ticksPerVolley, perf::BrowserJourneyLayout layout,
    perf::BrowserJourneyShape shape) {
    if (!initialized_ || !physicsWorld_ || !camera_ || !characterController_
        || isBenchmarkRunning()
        || (browserJourneyBenchmark_
            && browserJourneyBenchmark_->isRunning())) {
        return false;
    }

    const physics::PhysicsStats physicsStats = physicsWorld_->stats();
    const uint32_t availableBodies =
        physicsStats.bodyCapacity >= physicsStats.residentBodies
        ? physicsStats.bodyCapacity - physicsStats.residentBodies : 0u;
    if (uint64_t{physicsStats.residentBodies} + targetBodies
        > physicsStats.bodyCapacity) {
        LOG_ERROR(
            "Browser journey requires {} additional bodies, but only {} slots remain",
            targetBodies, availableBodies);
        return false;
    }

    if (!browserJourneyBenchmark_) {
        browserJourneyBenchmark_ =
            std::make_unique<perf::BrowserJourneyBenchmark>();
    }
    const perf::BrowserJourneyConfig journeyConfig{
        .targetBodies = targetBodies,
        .warmupTicks = warmupTicks,
        .impactTicks = impactTicks,
        .settleTicks = settleTicks,
        .bodiesPerVolley = bodiesPerVolley,
        .ticksPerVolley = ticksPerVolley,
        .layout = layout,
        .shape = shape,
    };
    const bool started = browserJourneyBenchmark_->start(
        journeyConfig, physicsStats.residentBodies, stats_.frameCount,
        physicsWorld_->encodedTick());
    if (started) {
        if (layout != perf::BrowserJourneyLayout::CubePyramid) {
            prepareBrowserJourneyCamera();
        }
        LOG_INFO(
            "Browser journey armed: {} bodies, {} layout, {} shapes, {} per real volley every {} ticks, phases {} warmup / {} impact / {} settle",
            targetBodies, perf::browserJourneyLayoutName(layout),
            perf::browserJourneyShapeName(shape),
            bodiesPerVolley, ticksPerVolley, warmupTicks, impactTicks,
            settleTicks);
    } else {
        LOG_ERROR("Browser journey rejected: {}",
                  browserJourneyBenchmark_->failureReason());
    }
    return started;
}

int Application::browserJourneyBenchmarkStatus() const noexcept {
    return browserJourneyBenchmark_
        ? static_cast<int>(browserJourneyBenchmark_->status())
        : static_cast<int>(perf::BrowserJourneyStatus::Idle);
}

std::string Application::browserJourneyBenchmarkJson() const {
    return browserJourneyBenchmark_
        ? browserJourneyBenchmark_->resultJson() : std::string{};
}

void Application::prepareBrowserJourneyCamera() {
    if (!camera_ || !characterController_) return;

    // Aim a repeatable player-like view at the actual production terrain.
    // The downward ray makes each rapid volley enter a dense terrain/contact
    // phase within a few seconds instead of remaining airborne for the run.
    const float terrainY =
        characterController_->sampleTerrainHeight(
            kBrowserJourneyTargetX, kBrowserJourneyTargetZ);
    const glm::dvec3 target{
        kBrowserJourneyTargetX, terrainY + 2.0f,
        kBrowserJourneyTargetZ};
    const glm::dvec3 position{
        kBrowserJourneyTargetX - 36.0f, terrainY + 24.0f,
        kBrowserJourneyTargetZ - 20.0f};
    setCameraWorldPose(*camera_, position, target);
    controllerMode_ = ControllerMode::FreeFly;
    stats_.activeController = controllerMode_;
}

void Application::aimBrowserJourneyVolley(uint32_t volleyIndex) {
    if (!camera_ || !characterController_) return;

    // A deterministic sunflower sweep models a player traversing the terrain
    // and changing aim. It keeps thousands of bodies from becoming an
    // artificial single-column pile.
    // The production body limit needs at most 1,024 full volleys, so each
    // volley gets its own terrain zone.
    constexpr uint32_t impactZoneCount = 1'024u;
    constexpr float impactZoneSpacing = 24.0f;
    constexpr float goldenAngle = 2.39996323f;
    const uint32_t zone = volleyIndex % impactZoneCount;
    const float radius = impactZoneSpacing
        * std::sqrt(static_cast<float>(zone));
    const float angle = static_cast<float>(zone) * goldenAngle;
    const float targetX =
        kBrowserJourneyTargetX + std::cos(angle) * radius;
    const float targetZ =
        kBrowserJourneyTargetZ + std::sin(angle) * radius;
    const float terrainY =
        characterController_->sampleTerrainHeight(targetX, targetZ);
    const glm::dvec3 target{targetX, terrainY + 2.0f, targetZ};
    const glm::dvec3 position{
        targetX - 36.0f, terrainY + 24.0f, targetZ - 20.0f};
    setCameraWorldPose(*camera_, position, target);
}

void Application::prepareBrowserJourneyOverview(uint32_t volleyCount) {
    if (!camera_ || !characterController_) return;

    constexpr float impactZoneSpacing = 24.0f;
    const float radius = volleyCount > 1u
        ? impactZoneSpacing
            * std::sqrt(static_cast<float>(volleyCount - 1u))
        : 0.0f;
    const float terrainY =
        characterController_->sampleTerrainHeight(
            kBrowserJourneyTargetX, kBrowserJourneyTargetZ);
    const float distance = 60.0f + radius * 2.3f;
    const float height = 60.0f + radius * 1.3f;
    const glm::dvec3 position{
        kBrowserJourneyTargetX, terrainY + height,
        kBrowserJourneyTargetZ - distance};
    const glm::dvec3 target{
        kBrowserJourneyTargetX, terrainY + 2.0f,
        kBrowserJourneyTargetZ};
    setCameraWorldPose(*camera_, position, target);
}

void Application::updateBrowserJourneyBenchmark() {
    if (!browserJourneyBenchmark_ || !physicsWorld_
        || !browserJourneyBenchmark_->isRunning()) {
        return;
    }

    const uint64_t physicsTick = physicsWorld_->encodedTick();
    const uint32_t requested = browserJourneyBenchmark_->advance(
        physicsTick, physicsWorld_->stats().residentBodies);
    if (requested != 0u) {
        if (browserJourneyBenchmark_->layout()
            == perf::BrowserJourneyLayout::TerrainSweep) {
            aimBrowserJourneyVolley(browserJourneyBenchmark_->volleyCount());
        }
        constexpr uint32_t shapeCount =
            static_cast<uint32_t>(physics::ThrowableShape::Count);
        const uint32_t benchmarkShape = static_cast<uint32_t>(
            browserJourneyBenchmark_->shape());
        selectedThrowable_ = benchmarkShape < shapeCount
            ? benchmarkShape
            : browserJourneyBenchmark_->volleyCount() % shapeCount;
        const auto shape =
            static_cast<physics::ThrowableShape>(selectedThrowable_);
        const uint32_t spawned = throwThrowableBatch(shape, requested);
        static_cast<void>(browserJourneyBenchmark_->reportVolley(
            physicsTick, requested, spawned));
        if (browserJourneyBenchmark_->status()
                == perf::BrowserJourneyStatus::Impact
            && browserJourneyBenchmark_->layout()
                == perf::BrowserJourneyLayout::TerrainSweep) {
            prepareBrowserJourneyOverview(
                browserJourneyBenchmark_->volleyCount());
        }
    }

    if (!browserJourneyBenchmark_->isRunning()) {
        if (browserJourneyBenchmark_->passed()) {
            LOG_INFO("Browser journey complete: {} bodies across {} volleys",
                     browserJourneyBenchmark_->spawnedBodies(),
                     browserJourneyBenchmark_->volleyCount());
        } else {
            LOG_ERROR("Browser journey failed: {}",
                      browserJourneyBenchmark_->failureReason());
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Controller Mode
// ─────────────────────────────────────────────────────────────────────────────

void Application::setControllerMode(ControllerMode mode) {
    if (controllerMode_ != mode) {
        controllerMode_ = mode;
        stats_.activeController = mode;
        
        // Log current camera position for debugging
        if (camera_) {
            const auto& pos = camera_->position();
            LOG_INFO("Controller mode changed to: {} (camera at {:.1f}, {:.1f}, {:.1f})", 
                     controllerModeToString(mode), pos.x, pos.y, pos.z);
        } else {
            LOG_INFO("Controller mode changed to: {}", controllerModeToString(mode));
        }
        
        // When switching to character mode, log terrain info for debugging
        if (mode == ControllerMode::Character && characterController_ && camera_) {
            characterController_->syncPhysicsPosition();
            const auto& cfg = characterController_->config();
            LOG_INFO("Character config: terrainWidth={:.1f}, terrainHeight={:.1f}, heightScale={:.1f}", 
                     cfg.terrainWidth, cfg.terrainHeight, cfg.heightScale);
            
            const auto& pos = camera_->position();
            float terrainH = characterController_->sampleTerrainHeight(pos.x, pos.z);
            LOG_INFO("Terrain height at camera XZ ({:.1f}, {:.1f}) = {:.1f}", 
                     pos.x, pos.z, terrainH);
        }
    }
}

void Application::toggleControllerMode() {
    switch (controllerMode_) {
        case ControllerMode::FreeFly:
            setControllerMode(ControllerMode::Character);
            break;
        case ControllerMode::Character:
            setControllerMode(ControllerMode::FreeFly);
            break;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Initialization Helpers
// ─────────────────────────────────────────────────────────────────────────────

bool Application::initWindow() {
#if defined(VOXY_NATIVE)
    LOG_DEBUG("Initializing window...");

    window_ = std::make_unique<Window>();

    WindowConfig windowConfig;
    windowConfig.width = config_.windowWidth;
    windowConfig.height = config_.windowHeight;
    windowConfig.title = config_.windowTitle.c_str();
    windowConfig.fullscreen = config_.fullscreen;
    windowConfig.vsync = config_.vsync;
    windowConfig.resizable = true;

    if (!window_->init(windowConfig)) {
        return false;
    }

    LOG_DEBUG("Window created: {}x{}", window_->getWidth(), window_->getHeight());
    return true;

#elif defined(VOXY_WASM)
    // WASM uses canvas, no window needed
    LOG_DEBUG("WASM build - no window initialization needed");
    return true;
#else
    return false;
#endif
}

bool Application::initGPU() {
    LOG_DEBUG("Initializing GPU context...");

    gpuContext_ = std::make_unique<gpu::Context>();

    gpu::ContextConfig gpuConfig;
    gpuConfig.powerPreference = WGPUPowerPreference_HighPerformance;
    gpuConfig.enableValidation = config_.enableValidation;
    gpuConfig.enableTimestamps = config_.gpuPhysicsStageProfiling
        || config_.gpuRenderStageProfiling;
    gpuConfig.preferredFormat = config_.colorFormat;
    gpuConfig.presentMode = config_.vsync ? WGPUPresentMode_Fifo : WGPUPresentMode_Immediate;

#if defined(VOXY_NATIVE)
    if (!window_) {
        LOG_ERROR("Window must be initialized before GPU context");
        return false;
    }

    gpuConfig.swapchainWidth = scaledRenderExtent(
        static_cast<uint32_t>(window_->getFramebufferWidth()),
        config_.resolutionScale);
    gpuConfig.swapchainHeight = scaledRenderExtent(
        static_cast<uint32_t>(window_->getFramebufferHeight()),
        config_.resolutionScale);

    if (!gpuContext_->init(*window_, gpuConfig)) {
        return false;
    }

#elif defined(VOXY_WASM)
    gpuConfig.swapchainWidth = scaledRenderExtent(
        static_cast<uint32_t>(config_.windowWidth), config_.resolutionScale);
    gpuConfig.swapchainHeight = scaledRenderExtent(
        static_cast<uint32_t>(config_.windowHeight), config_.resolutionScale);

    if (!gpuContext_->initFromCanvas("#voxy-canvas", gpuConfig)) {
        return false;
    }
#endif

    // Surface capability negotiation may select a different format than the
    // preference. Every attachment pipeline must use the negotiated format.
    config_.colorFormat = gpuContext_->getSwapchainFormat();

    if (config_.benchmarkOnStartup) {
        if (!createBenchmarkTarget(gpuContext_->getSwapchainWidth(),
                                   gpuContext_->getSwapchainHeight())) {
            LOG_ERROR("Failed to create the benchmark offscreen target");
            return false;
        }
    }

    LOG_DEBUG("GPU context initialized");
    return true;
}

bool Application::createBenchmarkTarget(uint32_t width, uint32_t height) {
    if (!gpuContext_ || width == 0u || height == 0u) return false;

    gpu::TextureDesc targetDesc = gpu::TextureDesc::renderTarget(
        width, height, gpuContext_->getSwapchainFormat(),
        "benchmark_offscreen_target");
    targetDesc.usage = WGPUTextureUsage_RenderAttachment
                     | WGPUTextureUsage_CopySrc;
    WGPUTexture nextTexture = gpu::createTexture(
        gpuContext_->getDevice(), targetDesc);
    if (!nextTexture) return false;
    WGPUTextureView nextView = gpu::createTextureView(nextTexture);
    if (!nextView) {
        wgpuTextureRelease(nextTexture);
        return false;
    }

    // Releasing the application handles is safe with in-flight submissions:
    // WebGPU command buffers retain the resources they reference.
    if (benchmarkTargetView_) wgpuTextureViewRelease(benchmarkTargetView_);
    if (benchmarkTargetTexture_) wgpuTextureRelease(benchmarkTargetTexture_);
    benchmarkTargetTexture_ = nextTexture;
    benchmarkTargetView_ = nextView;
    return true;
}

bool Application::initRenderGpuProfiling() {
    if (!config_.gpuRenderStageProfiling || !gpuContext_
        || !wgpuDeviceHasFeature(
            gpuContext_->getDevice(), WGPUFeatureName_TimestampQuery)) {
        return true;
    }

    WGPUQuerySetDescriptor queryDesc{};
    WGPU_SET_LABEL(queryDesc, "render_stage_timestamps");
    queryDesc.type = WGPUQueryType_Timestamp;
    queryDesc.count = kRenderGpuTimestampCount;
    renderGpuQuerySet_ = wgpuDeviceCreateQuerySet(
        gpuContext_->getDevice(), &queryDesc);
    renderGpuResolveBuffer_ = gpu::createBuffer(
        gpuContext_->getDevice(), gpu::BufferDesc{
            .label = "render_stage_timestamp_resolve",
            .size = kRenderGpuTimestampCount * sizeof(uint64_t),
            .usage = WGPUBufferUsage_QueryResolve | WGPUBufferUsage_CopySrc,
        });
    if (!renderGpuQuerySet_ || !renderGpuResolveBuffer_
        || !renderGpuReadback_.initialize(
            gpuContext_->getDevice(), 4u,
            kRenderGpuTimestampCount * sizeof(uint64_t))) {
        renderGpuReadback_.shutdown();
        if (renderGpuResolveBuffer_) {
            wgpuBufferDestroy(renderGpuResolveBuffer_);
            wgpuBufferRelease(renderGpuResolveBuffer_);
            renderGpuResolveBuffer_ = nullptr;
        }
        if (renderGpuQuerySet_) {
            wgpuQuerySetRelease(renderGpuQuerySet_);
            renderGpuQuerySet_ = nullptr;
        }
        return false;
    }
    return true;
}

bool Application::initInput() {
    LOG_DEBUG("Initializing input system...");

    input_ = std::make_unique<Input>();

#if defined(VOXY_NATIVE)
    if (window_) {
        input_->attachToWindow(*window_);
    }
#elif defined(VOXY_WASM)
    input_->setupEmscriptenCallbacks("#voxy-canvas");
#endif

    LOG_DEBUG("Input system initialized");
    return true;
}

bool Application::initCamera() {
    LOG_DEBUG("Initializing camera...");

    // Create camera
    CameraConfig camConfig;
    camConfig.fovY = glm::radians(config_.cameraFovDegrees);
    camConfig.nearPlane = config_.cameraNear;
    camConfig.farPlane = config_.cameraFar;

    camConfig.aspectRatio =
        static_cast<float>(gpuContext_->getSwapchainWidth())
        / static_cast<float>(gpuContext_->getSwapchainHeight());

    const glm::vec3 defaultStart{0.0f, 80.0f, 0.0f};
    glm::vec3 startPos = config_.cameraStartPos;
    if (glm::length(startPos - defaultStart) < 0.001f) {
        const float viewDistance = std::max(200.0f, config_.cellScale * 256.0f);
        const float viewHeight = std::max(80.0f, config_.heightScale * 1.25f);
        startPos = glm::vec3(0.0f, viewHeight, -viewDistance);
    }

    camera_ = std::make_unique<Camera>(startPos, camConfig);

    // The renderer centers the terrain at (0,0,0)
    // So we should look at the origin, not the calculated positive center
    camera_->lookAt(glm::vec3(0.0f, 0.0f, 0.0f));

    // Create free-fly camera controller
    FreeFlyConfig flyConfig;
    flyConfig.baseSpeed = config_.cameraMoveSpeed;
    flyConfig.mouseSensitivity = config_.cameraMouseSensitivity;
    flyConfig.boostMultiplier = 5.0f;

    freeFlyController_ = std::make_unique<FreeFlyController>(*camera_, flyConfig);

    physicsWorld_ = std::make_unique<physics::PhysicsWorld>();
    physics::PhysicsInitContext physicsContext;
    physicsContext.requestedBackend = config_.physicsBackend;
    physicsContext.allowCpuFallback = config_.physicsCpuFallback;
    physicsContext.device = gpuContext_->getDevice();
    physicsContext.queue = gpuContext_->getQueue();
    if (config_.physicsBackend == physics::BackendType::WebGpuSoft) {
        physicsContext.maxBodies = config_.gpuPhysicsMaxBodies;
        physicsContext.maxActiveBodies = config_.gpuPhysicsMaxBodies;
        physicsContext.maxPairs = config_.gpuPhysicsMaxPairs;
        physicsContext.maxContacts = config_.gpuPhysicsMaxPairs;
        physicsContext.maxManifolds = config_.gpuPhysicsMaxPairs;
        physicsContext.maxCandidatePairs =
            config_.gpuPhysicsMaxCandidatePairs;
        if (config_.cubePyramidBodyCount != 0u) {
            // Run broad/narrow at 30 Hz and solve at 60 Hz. Two substeps avoid
            // the extra overlap seen at one while CCD protects the projectile.
            physicsContext.gpu.fixedTickSeconds = 1.0f / 30.0f;
            physicsContext.gpu.substeps = 2u;
            physicsContext.gpu.solverColorCount = 8u;
            physicsContext.gpu.solverParallelColorCount = 0u;
            physicsContext.gpu.maximumCatchUpTicks = 3u;
        } else {
            physicsContext.gpu.maximumCatchUpTicks =
                config_.gpuPhysicsMaximumCatchUpTicks;
        }
        physicsContext.gpu.broadPhaseCellSize =
            config_.gpuPhysicsBroadPhaseCellSize;
        physicsContext.gpu.solverWorkgroupSize =
            config_.gpuPhysicsSolverWorkgroupSize;
    }
    physicsContext.gpu.shaderPath =
        (config_.shaderDir / "physics_ballistic.wgsl").string();
    physicsContext.gpu.enableStageProfiling =
        config_.gpuPhysicsStageProfiling;
    // Interactive stage timings sample asynchronously at a low rate.
    // Timestamp queries remain enabled, but resolving and mapping them every
    // physics tick serializes browser GPU work and makes the profiler change
    // the performance it is measuring.
    constexpr uint32_t kInteractiveStageProfilingIntervalTicks = 30u;
    constexpr uint32_t kInteractiveTelemetryIntervalTicks = 30u;
    const uint32_t profilingInterval =
        config_.benchmarkBodyCount != 0u
            ? 1u : kInteractiveStageProfilingIntervalTicks;
    const uint32_t telemetryInterval =
        config_.gpuPhysicsStageProfiling
            || config_.benchmarkBodyCount != 0u
        ? 1u : kInteractiveTelemetryIntervalTicks;
    physicsContext.gpu.stageProfilingIntervalTicks = profilingInterval;
    physicsContext.gpu.enableTelemetryReadback = true;
    physicsContext.gpu.telemetryReadbackIntervalTicks = telemetryInterval;
    physicsContext.gpu.stageProfilingTimestampPeriodNanoseconds =
        config_.gpuPhysicsTimestampPeriodNanoseconds;
    physicsContext.enableValidation = config_.enableValidation;
    physicsContext.joltJobSystem = config_.joltJobSystem;
    physicsContext.joltWorkerThreads = config_.joltWorkerThreads;
    physicsContext.box3dWorkerThreads = config_.box3dWorkerThreads;
    if (!physicsWorld_->initialize(physicsContext)) {
        LOG_ERROR("Failed to initialize '{}' physics backend",
                  physics::backendTypeName(config_.physicsBackend));
        return false;
    }
    const char* scheduler = config_.physicsBackend
            == physics::BackendType::JoltLegacy
        ? physics::joltJobSystemModeName(config_.joltJobSystem)
        : "internal";
    LOG_INFO("Physics backend: {} (scheduler {}, concurrency {})",
             physics::backendTypeName(physicsWorld_->backendType()),
             scheduler, physicsWorld_->stats().workerConcurrency);
    physicsWorld_->setWaterPlane(rendererSettings_.waterHeight,
                                 rendererSettings_.waterEnabled);

    // Create character controller (will be fully initialized after terrain loads)
    CharacterConfig charConfig;
    charConfig.walkSpeed = config_.cameraMoveSpeed;
    charConfig.runSpeed = config_.cameraMoveSpeed * 2.0f;
    charConfig.mouseSensitivity = config_.cameraMouseSensitivity;
    charConfig.heightScale = config_.heightScale;
    charConfig.cellScale = config_.cellScale;
    charConfig.groundOffset = config_.cameraEyeHeight;
    charConfig.collisionHeight = std::max(config_.cameraEyeHeight, 0.82f);
    charConfig.terrainWidth = terrainSampleExtent(config_.heightmapWidth, config_.cellScale);
    charConfig.terrainHeight = terrainSampleExtent(config_.heightmapHeight, config_.cellScale);
    
    // Character controller is created without heightmap first, will be set after terrain init
    characterController_ = std::make_unique<CharacterController>();
    characterController_->attachCamera(*camera_);
    characterController_->setConfig(charConfig);
    characterController_->attachPhysicsWorld(*physicsWorld_);

    LOG_DEBUG("Camera initialized at ({}, {}, {})",
              config_.cameraStartPos.x, config_.cameraStartPos.y, config_.cameraStartPos.z);
    return true;
}

bool Application::initTerrain() {
    LOG_DEBUG("Initializing terrain...");

    motoWorldSpawnValid_ = false;
    motoWorldSpawn_ = {};
    motoWorldSpawnYaw_ = 0.0f;
    motoRaceRoute_.clear();
    motoTrackPoses_.clear();
    motoSurfaceMapSize_ = 0u;
    motoSurfaceMap_.clear();
    heightmap_ = std::make_unique<terrain::Heightmap>();

    if (!config_.heightmapPath.empty()) {
        // Load from file
        auto result = heightmap_->loadFromFile(config_.heightmapPath);
        if (!result) {
            LOG_ERROR("Failed to load heightmap: {}", 
                      terrain::errorToString(result.error()));
            return false;
        }
        LOG_INFO("Loaded heightmap: {}", config_.heightmapPath.string());
        
        // Check if upscaling is needed (e.g. loaded 4k, requested 8k)
        if (heightmap_->getWidth() < config_.heightmapWidth || 
            heightmap_->getHeight() < config_.heightmapHeight) {
            
            LOG_INFO("Upscaling heightmap from {}x{} to {}x{}", 
                     heightmap_->getWidth(), heightmap_->getHeight(),
                     config_.heightmapWidth, config_.heightmapHeight);
                     
            auto resizeResult = heightmap_->resize(config_.heightmapWidth, config_.heightmapHeight);
            if (!resizeResult) {
                LOG_ERROR("Failed to upscale heightmap: {}", 
                          terrain::errorToString(resizeResult.error()));
                return false;
            }
        }
        
        // Resize to power of 2 if needed (required for mip chain generation)
        if (!terrain::isPowerOfTwo(heightmap_->getWidth()) || 
            !terrain::isPowerOfTwo(heightmap_->getHeight())) {
            LOG_INFO("Heightmap dimensions {}x{} are not power of 2, resizing...",
                     heightmap_->getWidth(), heightmap_->getHeight());
            auto resizeResult = heightmap_->resizeToPowerOfTwo();
            if (!resizeResult) {
                LOG_ERROR("Failed to resize heightmap: {}", 
                          terrain::errorToString(resizeResult.error()));
                return false;
            }
        }
    } else if (config_.motoEnabled) {
        // Empty terrain paths select the canonical RIDGEBREAK world. Keep
        // generation deterministic so native, browser, authority, replay and
        // capture runs all refer to the same terrain without shipping a large
        // baked heightmap.
        moto::WorldGenConfig worldConfig;
        worldConfig.size = config_.heightmapWidth;
        worldConfig.heightScale = config_.heightScale;
        worldConfig.cellScale = config_.cellScale;

        moto::WorldGenResult world;
        std::string error;
        if (!moto::generateWorld(worldConfig, &world, &error)) {
            LOG_ERROR("Failed to generate RIDGEBREAK world: {}", error);
            return false;
        }
        motoWorldSpawn_ = world.spawnPosition;
        motoWorldSpawnYaw_ = world.spawnHeading;
        motoWorldSpawnValid_ = true;
        motoRaceRoute_ = std::move(world.raceRoute);
        motoSurfaceMapSize_ = world.surfaceMapSize;
        motoSurfaceMap_ = std::move(world.surfaceMap);
        const size_t featureCount = world.features.size();
        *heightmap_ = terrain::Heightmap::createFromData(
            std::move(world.samples), worldConfig.size, worldConfig.size);
        if (!heightmap_->isLoaded()) {
            LOG_ERROR("RIDGEBREAK world produced an invalid heightmap");
            return false;
        }
        motoTrackPoses_.reserve(world.trackProps.size());
        for (const moto::WorldTrackProp& prop : world.trackProps) {
            const glm::vec3 flatForward(
                std::sin(prop.heading), 0.0f, std::cos(prop.heading));
            const glm::vec3 flatRight(
                flatForward.z, 0.0f, -flatForward.x);
            constexpr float probe = 0.8f;
            const float frontY = sampleTerrainHeight(
                prop.position.x + flatForward.x * probe,
                prop.position.y + flatForward.z * probe);
            const float backY = sampleTerrainHeight(
                prop.position.x - flatForward.x * probe,
                prop.position.y - flatForward.z * probe);
            const float rightY = sampleTerrainHeight(
                prop.position.x + flatRight.x * probe,
                prop.position.y + flatRight.z * probe);
            const float leftY = sampleTerrainHeight(
                prop.position.x - flatRight.x * probe,
                prop.position.y - flatRight.z * probe);
            glm::vec3 forward = glm::normalize(glm::vec3(
                flatForward.x, (frontY - backY) / (2.0f * probe),
                flatForward.z));
            glm::vec3 right = glm::normalize(glm::vec3(
                flatRight.x, (rightY - leftY) / (2.0f * probe),
                flatRight.z));
            const glm::vec3 up = glm::normalize(glm::cross(forward, right));
            // Re-orthogonalize both tangents. The previous longitudinal-only
            // fit visibly clipped narrow rut decals on cross-slopes.
            right = glm::normalize(glm::cross(up, forward));
            forward = glm::normalize(glm::cross(right, up));
            glm::mat4 basis(1.0f);
            basis[0] = glm::vec4(right, 0.0f);
            basis[1] = glm::vec4(up, 0.0f);
            basis[2] = glm::vec4(forward, 0.0f);
            const bool isDecal = prop.kind == moto::TrackPropKind::RutStrip ||
                                 prop.kind == moto::TrackPropKind::LandingPatch;
            basis[3] = glm::vec4(
                prop.position.x,
                sampleTerrainHeight(prop.position.x, prop.position.y) +
                    (isDecal ? 0.025f : 0.0f),
                prop.position.y, 1.0f);
            basis = basis * glm::scale(
                glm::mat4(1.0f), glm::vec3(std::max(prop.scale, 0.1f)));
            motoTrackPoses_.push_back({
                .meshIndex = static_cast<uint32_t>(prop.kind),
                .modelMatrix = basis,
                .tintColor = glm::vec4(1.0f),
            });
        }
        LOG_INFO(
            "Generated RIDGEBREAK world: {}x{}, {} route points, {} features, "
            "{} static track props, {}x{} surface splat",
            worldConfig.size, worldConfig.size, motoRaceRoute_.size(),
            featureCount, motoTrackPoses_.size(), motoSurfaceMapSize_,
            motoSurfaceMapSize_);
    } else {
        // Create procedural wavy heightmap
        uint32_t w = config_.heightmapWidth;
        uint32_t h = config_.heightmapHeight;
        std::vector<uint16_t> data(
            static_cast<size_t>(w) * static_cast<size_t>(h));
        
        for (uint32_t y = 0; y < h; ++y) {
            for (uint32_t x = 0; x < w; ++x) {
                // Create some sine waves
                float u = static_cast<float>(x) / static_cast<float>(w) * 10.0f;
                float v = static_cast<float>(y) / static_cast<float>(h) * 10.0f;
                float height = 0.5f + 0.2f * std::sin(u) + 0.2f * std::cos(v);
                data[static_cast<size_t>(y) * w + x] =
                    static_cast<uint16_t>(height * 65535.0f);
            }
        }
        *heightmap_ = terrain::Heightmap::createFromData(std::move(data), w, h);
        LOG_INFO("Created procedural heightmap: {}x{}", 
                 config_.heightmapWidth, config_.heightmapHeight);
    }

    // The authored cove is content for the canonical Wreckwater terrain, not
    // a filter applied to arbitrary user heightmaps.
    if (!config_.heightmapPath.empty() &&
        config_.heightmapPath.filename() ==
            "td_seed_1234_8192.ldh") {
        terrain::AuthoredCoveConfig coveConfig;
        coveConfig.waterHeight = rendererSettings_.waterHeight;
        terrain::AuthoredCoveStats coveStats;
        if (!terrain::applyAuthoredCove(
                heightmap_->getMutableData(),
                heightmap_->getWidth(),
                heightmap_->getHeight(),
                config_.heightScale,
                config_.cellScale,
                coveConfig,
                &coveStats)) {
            LOG_ERROR("Failed to author the Wreckwater review cove");
            return false;
        }
        LOG_INFO(
            "Authored Wreckwater cove: {} core samples, {} feather samples, "
            "height {:.1f}..{:.1f} m",
            coveStats.fullyAuthoredSamples,
            coveStats.touchedSamples - coveStats.fullyAuthoredSamples,
            coveStats.minimumAuthoredHeight,
            coveStats.maximumAuthoredHeight);
    }

    if (config_.cubePyramidBodyCount != 0u
        && !flattenCubeTriangleArena(*heightmap_, config_.cellScale)) {
        LOG_ERROR("Failed to flatten the cube-triangle terrain arena");
        return false;
    }

    // Upload to GPU with mip chain
    // Note: Use CPU mip generation for better compatibility across drivers
    // (GPU mip generation requires StorageBinding on R16Uint which isn't universally supported)
    auto uploadResult = heightmap_->uploadToGPUWithMips(
        gpuContext_->getDevice(),
        gpuContext_->getQueue(),
        false,  // Use CPU mip generation for compatibility
        config_.shaderDir / "mip_generate.wgsl",
        "terrain_heightmap"
    );

    if (!uploadResult) {
        LOG_ERROR("Failed to upload heightmap to GPU: {}", 
                  terrain::errorToString(uploadResult.error()));
        return false;
    }

    // Update stats
    stats_.terrainWidth = heightmap_->getWidth();
    stats_.terrainHeight = heightmap_->getHeight();
    stats_.terrainMipLevels = heightmap_->getMipLevelCount();
    
    // Bind heightmap to character controller
    if (characterController_) {
        characterController_->setHeightmap(heightmap_.get());
        
        // Update terrain dimensions in character controller config
        CharacterConfig charConfig = characterController_->config();
        charConfig.terrainWidth = terrainSampleExtent(stats_.terrainWidth, config_.cellScale);
        charConfig.terrainHeight = terrainSampleExtent(stats_.terrainHeight, config_.cellScale);
        charConfig.heightScale = config_.heightScale;
        charConfig.cellScale = config_.cellScale;
        charConfig.groundOffset = config_.cameraEyeHeight;
        characterController_->setConfig(charConfig);
    }

    if (physicsWorld_) {
        physicsWorld_->setTerrainGpuResources({
            heightmap_->getTextureView(), heightmap_->getMipLevelCount()});
    }
    if (!physicsWorld_ || !physicsWorld_->setTerrain(
            heightmap_->getData(), heightmap_->getWidth(), heightmap_->getHeight(),
            config_.heightScale, config_.cellScale)) {
        LOG_ERROR("Failed to attach terrain to Jolt Physics");
        return false;
    }

    LOG_DEBUG("Terrain initialized: {}x{} with {} mip levels",
              stats_.terrainWidth, stats_.terrainHeight, stats_.terrainMipLevels);
    return true;
}

bool Application::initRenderers() {
    LOG_DEBUG("Initializing renderers...");

    WGPUDevice device = gpuContext_->getDevice();
    WGPUQueue queue = gpuContext_->getQueue();
    uint32_t width = gpuContext_->getSwapchainWidth();
    uint32_t height = gpuContext_->getSwapchainHeight();

    // The spectral ocean is shared by ray intersection and final shading.
    // Initialize it before either consumer creates its bind group.
    waterSimulation_ = std::make_unique<render::WaterSimulation>();
    if (!waterSimulation_->init(device, queue, config_.shaderDir,
                                heightmap_->getData(), heightmap_->getWidth(),
                                heightmap_->getHeight(), config_.heightScale,
                                config_.cellScale, rendererSettings_.waterHeight,
                                makeWaterSpectrumConfig(
                                    rendererSettings_.waterSpectrum))) {
        LOG_ERROR("Failed to initialize FFT water simulation");
        return false;
    }
    // WaterSimulation owns the canonical clamping/wrapping rules. Use exactly
    // the values represented by its newly-created CPU and GPU spectrum.
    rendererSettings_.waterSpectrum = makeWaterSpectrumSettings(
        waterSimulation_->spectrumConfig());
    if (physicsWorld_) {
        const float waveStrength = rendererSettings_.waterWaveStrength;
        if (physicsWorld_->backendType() == physics::BackendType::WebGpuSoft) {
            physicsWorld_->setWaterGpuResources({
                waterSimulation_->getOutputView(),
                waterSimulation_->getSampler(), waveStrength,
                rendererSettings_.waterSpectrum.patchLengths.x,
                rendererSettings_.waterSpectrum.patchLengths.y});
        } else {
            physicsWorld_->setWaterSurfaceSampler(
                [simulation = waterSimulation_.get(), waveStrength](
                    glm::vec2 position, float timeSeconds) {
                    const auto sample = simulation->sampleSurface(
                        position, timeSeconds, waveStrength);
                    return physics::PhysicsWorld::WaterSurfaceSample{
                        sample.heightOffset, sample.slope, sample.velocity};
                });
        }
    }

    // Initialize triangle path
    {
        render::TrianglePathConfig triConfig = render::TrianglePathConfig::defaults();
        triConfig.shaderPath = config_.shaderDir / "terrain.wgsl";
        triConfig.colorFormat = config_.colorFormat;
        triConfig.heightScale = config_.heightScale;
        triConfig.cellScale = config_.cellScale;

        trianglePath_ = std::make_unique<render::TrianglePath>();
        if (!trianglePath_->init(device, queue, triConfig)) {
            LOG_ERROR("Failed to initialize triangle path");
            return false;
        }

        // Bind heightmap
        if (!trianglePath_->setHeightmap(
                heightmap_->getTextureView(),
                heightmap_->getWidth(),
                heightmap_->getHeight(),
                heightmap_->getData())) {
            LOG_ERROR("Failed to bind terrain to triangle path");
            return false;
        }
    }

    // Initialize raycast path
    {
        render::RaycastPathConfig rayConfig = render::RaycastPathConfig::defaults();
        rayConfig.shaderPath = config_.shaderDir / "terrain_raycast.wgsl";
        rayConfig.heightScale = config_.heightScale;
        rayConfig.cellScale = config_.cellScale;

        raycastPath_ = std::make_unique<render::RaycastPath>();
        if (!raycastPath_->init(device, queue, width, height, rayConfig)) {
            LOG_ERROR("Failed to initialize raycast path");
            return false;
        }

        // Bind heightmap
        if (!raycastPath_->setHeightmap(
                heightmap_->getTextureView(),
                heightmap_->getWidth(),
                heightmap_->getHeight())) {
            LOG_ERROR("Failed to bind terrain to raycast path");
            return false;
        }
        raycastPath_->setWaterSimulation(waterSimulation_->getOutputView(),
                                         waterSimulation_->getCoastView(),
                                         waterSimulation_->getSampler());

        // Bake the static sun shadow height field. The raycast shader then
        // replaces its per-pixel shadow DDA with a single texture lookup.
        {
            perf::Timer bakeTimer;
            bakeTimer.start();
            terrain::ShadowBakeConfig bakeConfig;
            bakeConfig.lightDir = rendererSettings_.sunDirection;
            bakeConfig.heightScale = config_.heightScale;
            bakeConfig.cellScale = config_.cellScale;

            const auto baked = terrain::bakeShadowHeightField(
                heightmap_->getData(), heightmap_->getWidth(),
                heightmap_->getHeight(), bakeConfig);

            if (!baked.data.empty()) {
                gpu::TextureDesc desc = gpu::TextureDesc::tex2D(
                    baked.width, baked.height, WGPUTextureFormat_R16Uint,
                    WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst,
                    "baked_shadow_height");
                shadowMapTexture_ = gpu::createTextureWithData(
                    device, queue, desc,
                    std::as_bytes(std::span<const uint16_t>(baked.data)),
                    baked.width * sizeof(uint16_t));
                if (shadowMapTexture_) {
                    shadowMapView_ = gpu::createTextureView(shadowMapTexture_);
                }
                if (shadowMapView_) {
                    raycastPath_->setShadowMap(shadowMapView_);
                    LOG_INFO("Baked shadow height field: {}x{} ({:.1f} ms)",
                             baked.width, baked.height, bakeTimer.elapsedMs());
                } else {
                    LOG_WARN("Failed to upload baked shadow map; shadows disabled");
                }
            } else {
                LOG_WARN("Shadow bake produced no data; shadows disabled");
            }
        }
    }

    // Initialize terrain textures
    terrainTextures_ = std::make_unique<terrain::TerrainTextures>();
    
    terrain::TerrainTextureConfig textureConfig;
    textureConfig.albedoPath = config_.albedoPath;
    textureConfig.lightmapPath = config_.lightmapPath;
    textureConfig.placeholderWidth = config_.heightmapWidth;
    textureConfig.placeholderHeight = config_.heightmapHeight;

    if (!terrainTextures_->init(device, queue, textureConfig)) {
        LOG_ERROR("Failed to initialize terrain textures and fallbacks");
        return false;
    }
    if (motoSurfaceMapSize_ != 0u
        && !terrainTextures_->createWorldSurfaceMap(
            motoSurfaceMap_, motoSurfaceMapSize_, motoSurfaceMapSize_)) {
        LOG_ERROR("Failed to upload RIDGEBREAK world surface splat");
        return false;
    }
    // CPU staging is no longer needed after the complete mip chain is resident.
    motoSurfaceMap_.clear();
    motoSurfaceMap_.shrink_to_fit();

    // Pass textures to triangle path
    trianglePath_->setAlbedo(terrainTextures_->getAlbedoView());
    trianglePath_->setLightmap(terrainTextures_->getLightmapView());
    trianglePath_->setSampler(terrainTextures_->getSampler());

    // Initialize blit path
    {
        render::BlitPathConfig blitConfig = render::BlitPathConfig::defaults();
        blitConfig.shaderPath = config_.shaderDir / "ray_blit.wgsl";
        blitConfig.colorFormat = config_.colorFormat;
        blitConfig.heightScale = config_.heightScale;
        blitConfig.cellScale = config_.cellScale;

        blitPath_ = std::make_unique<render::BlitPath>();
        if (!blitPath_->init(device, queue, blitConfig)) {
            LOG_ERROR("Failed to initialize blit path");
            return false;
        }
        if (!blitPath_->resize(raycastPath_->getOutputWidth(),
                               raycastPath_->getOutputHeight())) {
            LOG_ERROR("Failed to create blit background cache");
            return false;
        }

        // Bind textures to blit path
        blitPath_->setDepthTexture(raycastPath_->getDepthOutputView());
        blitPath_->setShadowTexture(raycastPath_->getShadowOutputView());
        blitPath_->setMaterialTexture(raycastPath_->getMaterialOutputView());
        blitPath_->setStaticTerrainTextures(
            raycastPath_->getTerrainDepthCacheView(),
            raycastPath_->getTerrainShadowCacheView());
        blitPath_->setWaterCompositeResources(
            heightmap_->getTextureView(), raycastPath_->getShadowMapView(),
            waterSimulation_->getOutputView(), waterSimulation_->getSampler());
        
        // TerrainTextures guarantees valid views after init (either loaded or placeholder)
        blitPath_->setTerrainTexture(terrainTextures_->getAlbedoView());
        blitPath_->setTerrainMaterialTextures(
            terrainTextures_->getMaterialAlbedoView(),
            terrainTextures_->getMaterialNormalRoughnessView());
        blitPath_->setLightmapTexture(terrainTextures_->getLightmapView()); 
        blitPath_->setTerrainSize(heightmap_->getWidth(), heightmap_->getHeight());
    }

    {
        render::PrimitivePathConfig primitiveConfig;
        primitiveConfig.shaderPath = config_.shaderDir / "physics_primitives.wgsl";
        primitiveConfig.colorFormat = config_.colorFormat;
        primitivePath_ = std::make_unique<render::PrimitivePath>();
        if (!primitivePath_->init(device, queue, primitiveConfig)) {
            LOG_ERROR("Failed to initialize physics primitive renderer");
            return false;
        }
        primitivePath_->setRayDepthTexture(raycastPath_->getDepthOutputView());
    }

    LOG_DEBUG("Renderers initialized");
    return true;
}

float Application::sampleTerrainHeight(float worldX, float worldZ) const {
    if (!heightmap_ || !heightmap_->isLoaded()
        || !std::isfinite(worldX) || !std::isfinite(worldZ)
        || !std::isfinite(config_.cellScale) || config_.cellScale <= 0.0f) {
        return 0.0f;
    }

    const glm::vec2 origin = physics::terrain_topology::centeredOrigin(
        heightmap_->getWidth(), heightmap_->getHeight(), config_.cellScale);
    const float sampleX = (worldX + origin.x) / config_.cellScale;
    const float sampleZ = (worldZ + origin.y) / config_.cellScale;
    return physics::terrain_topology::worldHeight(
        heightmap_->sampleBilinear(sampleX, sampleZ), config_.heightScale);
}

glm::vec3 Application::findMotoSpawn() const {
    if (!heightmap_ || !heightmap_->isLoaded()) {
        return {0.0f, 1.0f, 0.0f};
    }

    const glm::vec2 origin = physics::terrain_topology::centeredOrigin(
        heightmap_->getWidth(), heightmap_->getHeight(), config_.cellScale);
    const float searchRadius = std::max(
        0.0f, std::min({600.0f, origin.x * 0.9f, origin.y * 0.9f}));
    constexpr float spacing = 20.0f;
    constexpr float slopeProbe = 1.5f;
    float bestScore = std::numeric_limits<float>::infinity();
    glm::vec3 best(0.0f, sampleTerrainHeight(0.0f, 0.0f), 0.0f);

    for (float z = -searchRadius; z <= searchRadius; z += spacing) {
        for (float x = -searchRadius; x <= searchRadius; x += spacing) {
            const float center = sampleTerrainHeight(x, z);
            const float slope =
                std::abs(sampleTerrainHeight(x + slopeProbe, z)
                         - sampleTerrainHeight(x - slopeProbe, z))
                + std::abs(sampleTerrainHeight(x, z + slopeProbe)
                           - sampleTerrainHeight(x, z - slopeProbe));
            const float waterPenalty = std::max(
                rendererSettings_.waterHeight + 2.0f - center, 0.0f)
                * 1'000.0f;
            const float distancePenalty =
                0.0005f * (x * x + z * z);
            const float score = slope + waterPenalty + distancePenalty;
            if (score < bestScore) {
                bestScore = score;
                best = {x, center, z};
            }
        }
    }

    best.y += 1.0f;
    return best;
}

bool Application::initMoto() {
    if (!config_.motoEnabled || config_.wreckwaterClient || config_.benchmarkOnStartup
        || config_.exitAfterBenchmark || config_.cubePyramidBodyCount != 0u) {
        return true;
    }

    render::MeshPathConfig meshConfig;
    meshConfig.shaderPath = config_.shaderDir / "mesh_path.wgsl";
    meshConfig.colorFormat = config_.colorFormat;
    meshPath_ = std::make_unique<render::MeshPath>();
    if (!meshPath_->init(gpuContext_->getDevice(), gpuContext_->getQueue(),
                         meshConfig)) {
        LOG_ERROR("Failed to initialize RIDGEBREAK mesh renderer");
        return false;
    }
    meshPath_->setEnvironmentTexture(blitPath_->getEnvironmentTextureView());
    meshPath_->setRayDepthTexture(raycastPath_->getDepthOutputView());
    if (!meshPath_->loadMesh("data/moto/bike.vmesh")
        || !meshPath_->loadMesh("data/moto/rider.vmesh")
        || !meshPath_->loadMesh("data/moto/track.vmesh")) {
        LOG_ERROR("Failed to load RIDGEBREAK bike, rider and track meshes");
        return false;
    }

    moto::MotoSessionConfig sessionConfig;
    sessionConfig.spawnPosition = motoWorldSpawnValid_
        ? motoWorldSpawn_ : findMotoSpawn();
    sessionConfig.spawnYaw = motoWorldSpawnValid_ ? motoWorldSpawnYaw_ : 0.0f;
    sessionConfig.waterHeight = rendererSettings_.waterHeight;
    motoSession_ = std::make_unique<moto::MotoSession>();
    std::string error;
    if (!motoSession_->initialize(sessionConfig, &error)) {
        LOG_ERROR("Failed to initialize RIDGEBREAK session: {}", error);
        return false;
    }

    std::vector<moto::RaceCheckpoint> raceCheckpoints;
    if (!motoRaceRoute_.empty()
        && !moto::buildDirectedRaceCheckpoints(
            motoRaceRoute_, 18.0f, 12.0f, &raceCheckpoints, &error)) {
        LOG_ERROR("Failed to build RIDGEBREAK race gates: {}", error);
        return false;
    }
    for (moto::RaceCheckpoint& checkpoint : raceCheckpoints) {
        checkpoint.center.y = sampleTerrainHeight(
            checkpoint.center.x, checkpoint.center.z) + 2.0f;
    }

    moto::RaceConfig raceConfig = moto::makeUntimedPracticeRaceConfig();
    // Launch into honest, untimed practice. The authored circuit is validated
    // above but does not start until C explicitly moves the rider to its grid
    // and resets race authority.
    motoRaceSession_ = std::make_unique<moto::RaceSession>();
    if (!motoRaceSession_->configure(raceConfig, raceCheckpoints, &error)
        || !motoRaceSession_->join(kLocalMotoRacePlayer)
        || !motoRaceSession_->start()) {
        LOG_ERROR("Failed to initialize RIDGEBREAK race rules: {}", error);
        return false;
    }
    motoRaceTrickSequence_ = 0u;
    motoRaceLandingIdentity_ = 0u;
    motoRaceObservedTricksLanded_ = motoSession_->bikeState().tricksLanded;

    const moto::MotoCameraPose& pose = motoSession_->cameraPose();
    camera_->setWorldPosition({0, 0, 0}, pose.position);
    camera_->lookAt(pose.target);
    camera_->setFovYDegrees(55.0f);
    LOG_INFO("RIDGEBREAK untimed practice spawn: ({:.1f}, {:.1f}, {:.1f}), "
             "{} validated circuit gates",
             sessionConfig.spawnPosition.x, sessionConfig.spawnPosition.y,
             sessionConfig.spawnPosition.z, raceCheckpoints.size());
    return true;
}

bool Application::startMotoCircuit() {
    if (!motoSession_ || !motoSession_->isInitialized()
        || motoRaceRoute_.empty()) {
        return false;
    }

    std::string error;
    std::vector<moto::RaceCheckpoint> checkpoints;
    if (!moto::buildDirectedRaceCheckpoints(
            motoRaceRoute_, 18.0f, 12.0f, &checkpoints, &error)) {
        LOG_ERROR("Failed to build circuit gates: {}", error);
        return false;
    }
    for (moto::RaceCheckpoint& checkpoint : checkpoints) {
        checkpoint.center.y = sampleTerrainHeight(
            checkpoint.center.x, checkpoint.center.z) + 2.0f;
    }

    moto::CircuitGridPose grid;
    if (!moto::buildCircuitGridPose(
            checkpoints, 10.0f, &grid, &error)) {
        LOG_ERROR("Failed to build circuit grid: {}", error);
        return false;
    }
    grid.position.y = sampleTerrainHeight(
        grid.position.x, grid.position.z) + 1.0f;

    auto stagedRace = std::make_unique<moto::RaceSession>();
    const moto::RaceConfig circuitConfig;
    if (!stagedRace->configure(circuitConfig, checkpoints, &error)
        || !stagedRace->join(kLocalMotoRacePlayer)
        || !stagedRace->start()) {
        LOG_ERROR("Failed to stage circuit authority: {}", error);
        return false;
    }

    motoSession_->resetAt(grid.position, grid.yaw);
    motoRaceSession_ = std::move(stagedRace);
    motoRaceTrickSequence_ = 0u;
    motoRaceLandingIdentity_ = 0u;
    motoRaceObservedTricksLanded_ = 0u;
    LOG_INFO("RIDGEBREAK circuit staged: {} gates, {:.1f}s countdown, "
             "{:.1f}s race budget",
             checkpoints.size(),
             static_cast<float>(circuitConfig.countdownTicks)
                 / static_cast<float>(circuitConfig.tickRate),
             static_cast<float>(circuitConfig.durationTicks)
                 / static_cast<float>(circuitConfig.tickRate));
    return true;
}

void Application::setupCallbacks() {
#if defined(VOXY_NATIVE)
    if (!window_) return;

    // Resize callback - use shared onResize() method
    window_->setResizeCallback([this](int width, int height) {
        if (width <= 0 || height <= 0) return;
        onResize(static_cast<uint32_t>(width), static_cast<uint32_t>(height));
    });

    // Close callback
    window_->setCloseCallback([this]() {
        LOG_DEBUG("Window close requested");
        requestExit();
    });
#endif
}

// ─────────────────────────────────────────────────────────────────────────────
// Rendering Helpers
// ─────────────────────────────────────────────────────────────────────────────

void Application::pollRenderGpuTimings() {
    auto raw = renderGpuReadback_.poll();
    if (!raw || raw->bytes.size()
        != kRenderGpuTimestampCount * sizeof(uint64_t)) return;

    std::array<uint64_t, kRenderGpuTimestampCount> timestamps{};
    std::memcpy(timestamps.data(), raw->bytes.data(), raw->bytes.size());
    RenderGpuStageTiming timing;
    timing.frame = raw->tick;
    const double tickToMilliseconds =
        config_.gpuPhysicsTimestampPeriodNanoseconds * 1.0e-6;
    for (size_t stage = 0; stage < kRenderGpuStageCount; ++stage) {
        const uint64_t start =
            timestamps[stage * kRenderGpuQueriesPerStage];
        const uint64_t end =
            timestamps[stage * kRenderGpuQueriesPerStage + 1u];
        if (end >= start) {
            timing.milliseconds[stage] = static_cast<double>(
                end - start) * tickToMilliseconds;
        }
    }
    stats_.renderGpuTiming = timing;
    if (renderGpuTimingSampleCount_ == kRenderGpuTimingSampleCapacity) {
        renderGpuTimingSampleHead_ =
            (renderGpuTimingSampleHead_ + 1u)
            % kRenderGpuTimingSampleCapacity;
        --renderGpuTimingSampleCount_;
    }
    const size_t destination =
        (renderGpuTimingSampleHead_ + renderGpuTimingSampleCount_)
        % kRenderGpuTimingSampleCapacity;
    renderGpuTimingSamples_[destination] = timing;
    ++renderGpuTimingSampleCount_;
}

void Application::clearRayObjectDepth(WGPUCommandEncoder encoder) {
    WGPUTextureView depthView = getOrCreateDepthView();
    if (!encoder || !depthView) return;

    WGPURenderPassDepthStencilAttachment depthAttachment{};
    depthAttachment.view = depthView;
    depthAttachment.depthLoadOp = WGPULoadOp_Clear;
    depthAttachment.depthStoreOp = WGPUStoreOp_Store;
    depthAttachment.depthClearValue = 1.0f;
    depthAttachment.stencilLoadOp = WGPULoadOp_Undefined;
    depthAttachment.stencilStoreOp = WGPUStoreOp_Undefined;
    depthAttachment.depthReadOnly = false;
    depthAttachment.stencilReadOnly = true;

    WGPURenderPassDescriptor descriptor{};
    WGPU_SET_LABEL(descriptor, "ray_object_depth_clear");
    descriptor.depthStencilAttachment = &depthAttachment;
    WGPURenderPassEncoder pass =
        wgpuCommandEncoderBeginRenderPass(encoder, &descriptor);
    if (!pass) return;
    wgpuRenderPassEncoderEnd(pass);
    wgpuRenderPassEncoderRelease(pass);
}

void Application::renderMoto(WGPUCommandEncoder encoder,
                             WGPUTextureView colorView) {
    if (!meshPath_ || !meshPath_->isInitialized()
        || !motoSession_ || !motoSession_->isInitialized() || !camera_
        || isBenchmarkRunning()
        || (browserJourneyBenchmark_ && browserJourneyBenchmark_->isRunning())) {
        return;
    }

    meshPath_->clearInstances();
    for (const moto::MotoPartPose& pose : motoSession_->partPoses()) {
        meshPath_->addInstance({
            .assetIndex = pose.assetIndex,
            .meshIndex = pose.meshIndex,
            .modelMatrix = pose.modelMatrix,
            .tintColor = pose.tintColor,
            .emissiveBoost = pose.emissiveBoost,
        });
    }
    for (const MotoTrackRenderPose& pose : motoTrackPoses_) {
        meshPath_->addInstance({
            .assetIndex = 2u,
            .meshIndex = pose.meshIndex,
            .modelMatrix = pose.modelMatrix,
            .tintColor = pose.tintColor,
        });
    }

    WGPUTextureView depthView = getOrCreateDepthView();
    if (!depthView) return;
    const render::PrimitiveLighting lighting{
        .direction = rendererSettings_.sunDirection,
        .sunColor = rendererSettings_.sunColor,
        .sunIntensity = rendererSettings_.sunIntensity,
        .ambientColor = rendererSettings_.ambientColor,
        .ambientIntensity = rendererSettings_.ambientIntensity,
        .fogColor = rendererSettings_.fogColor,
        .fogDensity = rendererSettings_.fogDensity,
        .exposure = rendererSettings_.exposure,
    };
    meshPath_->render(
        encoder, colorView, depthView, camera_->viewMatrix(),
        camera_->projectionMatrix(), camera_->position(), lighting,
        gpuContext_->getSwapchainWidth(), gpuContext_->getSwapchainHeight(),
        config_.renderPath == RenderPath::Raycast);
}

void Application::renderTrianglePath(WGPUCommandEncoder encoder, WGPUTextureView colorView) {
    if (!trianglePath_ || !trianglePath_->isInitialized()) {
        return;
    }

    WGPUTextureView depthView = getOrCreateDepthView();
    if (!depthView) {
        return;
    }

    trianglePath_->render(encoder, colorView, depthView);
}

void Application::renderRaycastPath(WGPUCommandEncoder encoder, WGPUTextureView colorView) {
    if (!raycastPath_ || !raycastPath_->isInitialized()) {
        return;
    }
    if (!blitPath_ || !blitPath_->isInitialized()) {
        return;
    }

    // Dispatch ray-cast compute shader
    constexpr uint32_t raycastStage =
        static_cast<uint32_t>(RenderGpuStage::TerrainRaycast);
    raycastPath_->dispatch(
        encoder, renderGpuProfilingFrame_ ? renderGpuQuerySet_ : nullptr,
        raycastStage * kRenderGpuQueriesPerStage,
        raycastStage * kRenderGpuQueriesPerStage + 1u,
        true);

    if (raycastPath_->isUsingStaticCache()) {
        ++stats_.raycastStaticCacheFrames;
    }
    if (raycastPath_->didRefreshStaticCache()) {
        ++stats_.raycastTerrainCacheRefreshes;
    }

    blitPath_->setStaticCacheState(
        raycastPath_->isUsingStaticCache(),
        raycastPath_->didRefreshStaticCache());
    blitPath_->setLinearDepthRequired(
        (primitivePath_ && primitivePath_->isInitialized()
         && stats_.physicsResidentBodies != 0u)
        || (meshPath_ && meshPath_->isInitialized()
            && motoSession_ && motoSession_->isInitialized()));
    // Render blit pass
    constexpr uint32_t blitStage =
        static_cast<uint32_t>(RenderGpuStage::LightingBlit);
    blitPath_->render(
        encoder, colorView,
        renderGpuProfilingFrame_ ? renderGpuQuerySet_ : nullptr,
        blitStage * kRenderGpuQueriesPerStage,
        blitStage * kRenderGpuQueriesPerStage + 1u);
    if (blitPath_->didUseGeometryWaterPath()) {
        ++stats_.geometryWaterFrames;
    }
}

void Application::updateCameraUniforms() {
    if (!camera_) return;

    const auto& cameraView = camera_->viewMatrix();
    const auto& proj = camera_->projectionMatrix();
    const auto& pos = camera_->position();
    const physics::WorldPosition cameraWorld =
        physics::canonicalWorldPosition(
            camera_->worldSector(), glm::dvec3(pos));
    const glm::dvec3 terrainPosition64 =
        physics::worldPositionToAbsolute(cameraWorld);
    const glm::vec3 terrainPosition{
        static_cast<float>(terrainPosition64.x),
        static_cast<float>(terrainPosition64.y),
        static_cast<float>(terrainPosition64.z)};
    // Terrain remains authored in sector-zero coordinates. Keep the camera's
    // stable rotation, but reconstruct its terrain-space translation after the
    // camera local position crosses a sector boundary. Physics primitives use
    // cameraView separately and remain camera-relative.
    const glm::mat4 terrainViewRotation{glm::mat3(cameraView)};
    const glm::mat4 terrainView = terrainViewRotation
        * glm::translate(glm::mat4(1.0f), -terrainPosition);
    const float ambient = rendererSettings_.ambientIntensity;
    const uint32_t terrainWidth = heightmap_ ? heightmap_->getWidth() : stats_.terrainWidth;
    const uint32_t terrainHeight = heightmap_ ? heightmap_->getHeight() : stats_.terrainHeight;
    const uint32_t lodStep =
        (config_.renderPath == RenderPath::Triangle && trianglePath_) ? trianglePath_->getLODStep() : 1u;
    const glm::vec3 worldLightDir = rendererSettings_.sunDirection;

    render::CameraUniforms uniforms;
    if (!uniforms.setTerrain(
            std::max(terrainWidth, 1u), std::max(terrainHeight, 1u),
            config_.heightScale, config_.cellScale,
            static_cast<float>(lodStep), rendererSettings_.fogDensity)
        || !uniforms.setCamera(terrainView, proj, terrainPosition)
        || !uniforms.setLightDirection(
            worldLightDir, terrainView, ambient)
        || !uniforms.setWater(
            rendererSettings_.waterEnabled,
            rendererSettings_.waterHeight,
            rendererSettings_.waterShallowColor,
            rendererSettings_.waterDeepColor,
            rendererSettings_.waterRoughness,
            rendererSettings_.waterWaveStrength,
            rendererSettings_.waterReflectionStrength,
            rendererSettings_.waterShoreFade)
        || !uniforms.setRendererMaterial(
            rendererSettings_.sunColor,
            rendererSettings_.sunIntensity,
            rendererSettings_.ambientColor,
            rendererSettings_.fogColor,
            rendererSettings_.exposure,
            rendererSettings_.waterIor,
            rendererSettings_.waterDistortion,
            rendererSettings_.waterAbsorptionScale,
            rendererSettings_.waterScatterStrength,
            rendererSettings_.waterFoamSize,
            rendererSettings_.waterFoamOpacity,
            rendererSettings_.waterFoamCoverage,
            rendererSettings_.waterReflectionDistance,
            rendererSettings_.waterSpectrum.patchLengths)) {
        LOG_ERROR("Application: refused invalid camera uniform state");
        return;
    }
    uniforms.setLegoMode(legoMode_);
    uniforms.invProjParams.w = config_.motoEnabled ? 1.0f : 0.0f;
    // Wrap before fp32 loses the sub-frame precision used by short waves.
    const float waterTime = static_cast<float>(
        std::fmod(stats_.totalTimeSeconds, 4096.0));
    if (!uniforms.setWaterTime(waterTime)) return;
    float cameraSurfaceOffset = 0.0f;
    if (rendererSettings_.waterEnabled && waterSimulation_ &&
        waterSimulation_->isInitialized()) {
        cameraSurfaceOffset = waterSimulation_->sampleSurface(
            glm::vec2{terrainPosition.x, terrainPosition.z}, waterTime,
            rendererSettings_.waterWaveStrength).heightOffset;
    }
    if (!uniforms.setCameraWaterSurfaceOffset(cameraSurfaceOffset)) return;

    if (config_.renderPath == RenderPath::Triangle &&
        trianglePath_ && trianglePath_->isInitialized()) {
        trianglePath_->setCameraUniforms(uniforms);
    }

    if (config_.renderPath == RenderPath::Raycast) {
        if (raycastPath_ && raycastPath_->isInitialized()) {
            raycastPath_->setCameraUniforms(uniforms);
        }

        if (blitPath_ && blitPath_->isInitialized()) {
            blitPath_->setCameraUniforms(uniforms);
        }
    }
}

void Application::updateStats(float deltaTime) {
    pollRenderGpuTimings();
    stats_.frameTimeMs = static_cast<double>(deltaTime) * 1000.0;
    stats_.totalTimeSeconds += static_cast<double>(deltaTime);
    stats_.activeRenderPath = config_.renderPath;
    if (physicsWorld_) {
        const physics::PhysicsStats physicsStats = physicsWorld_->stats();
        const uint32_t previousVisibleHigh =
            stats_.physics.visibleBodyUsage.highWater;
        stats_.physics = physicsStats;
        stats_.physics.visibleBodyUsage.current =
            stats_.primitiveSubmittedCount;
        stats_.physics.visibleBodyUsage.capacity =
            physicsStats.bodyCapacity;
        stats_.physics.visibleBodyUsage.highWater = std::max(
            previousVisibleHigh, stats_.primitiveSubmittedCount);
        while (auto timing = physicsWorld_->pollGpuStageTimings()) {
            stats_.physicsGpuTiming = *timing;
            if (physicsGpuTimingSampleCount_
                == kPhysicsGpuTimingSampleCapacity) {
                physicsGpuTimingSampleHead_ =
                    (physicsGpuTimingSampleHead_ + 1u)
                    % kPhysicsGpuTimingSampleCapacity;
                --physicsGpuTimingSampleCount_;
            }
            const size_t destination =
                (physicsGpuTimingSampleHead_ + physicsGpuTimingSampleCount_)
                % kPhysicsGpuTimingSampleCapacity;
            physicsGpuTimingSamples_[destination] = std::move(*timing);
            ++physicsGpuTimingSampleCount_;
        }
        stats_.physicsBackend = physicsStats.backend;
        stats_.physicsResidentBodies = physicsStats.residentBodies;
        stats_.physicsActiveBodies = physicsStats.activeBodies;
        stats_.physicsBodyCapacity = physicsStats.bodyCapacity;
        stats_.physicsEstimatedPersistentBytes =
            physicsStats.estimatedPersistentBytes;
        stats_.physicsScratchBytes = physicsStats.scratchBytes;
    }

    // FPS calculation
    fpsAccumulator_ += static_cast<double>(deltaTime);
    fpsFrameCount_++;

    if (fpsAccumulator_ >= static_cast<double>(config_.fpsLogIntervalSeconds)) {
        stats_.avgFrameTimeMs = (fpsAccumulator_ * 1000.0) / fpsFrameCount_;
        stats_.fps = 1000.0 / stats_.avgFrameTimeMs;

        if (config_.showFPS && !getDebugOverlay().isVisible()) {
            // Only log FPS if debug overlay is not visible
            LOG_DEBUG("FPS: {:.1f} ({:.2f} ms) | Path: {}", 
                      stats_.fps, stats_.avgFrameTimeMs, 
                      renderPathToString(config_.renderPath));
        }

        fpsAccumulator_ = 0.0;
        fpsFrameCount_ = 0;
    }
    
    // Update debug overlay
    DebugOverlayStats overlayStats;
    overlayStats.fps = stats_.fps;
    overlayStats.frameTimeMs = stats_.frameTimeMs;
    overlayStats.avgFrameTimeMs = stats_.avgFrameTimeMs;
    overlayStats.frameCount = stats_.frameCount;
    overlayStats.renderPath = config_.renderPath;
    overlayStats.terrainWidth = stats_.terrainWidth;
    overlayStats.terrainHeight = stats_.terrainHeight;
    overlayStats.terrainMipLevels = stats_.terrainMipLevels;
    overlayStats.physics = stats_.physics;
    overlayStats.physicsGpuTiming = stats_.physicsGpuTiming;
    if (stats_.renderGpuTiming) {
        overlayStats.renderGpuMilliseconds =
            stats_.renderGpuTiming->milliseconds;
    }
    
    if (camera_) {
        overlayStats.cameraPosition = camera_->position();
        overlayStats.cameraYaw = camera_->yaw();
        overlayStats.cameraPitch = camera_->pitch();
    }
    
    // Estimate memory usage (heightmap texture only for now)
    if (heightmap_) {
        // Heightmap is R16Uint (2 bytes per pixel)
        size_t baseSize = static_cast<size_t>(stats_.terrainWidth) * 
                          static_cast<size_t>(stats_.terrainHeight) * 2;
        // Account for mip chain (roughly 1.33x base size)
        overlayStats.estimatedMemoryBytes = static_cast<size_t>(static_cast<double>(baseSize) * 1.33);
    }
    overlayStats.estimatedMemoryBytes +=
        stats_.physics.estimatedPersistentBytes + stats_.physics.scratchBytes;
    
    getDebugOverlay().update(overlayStats);
    getDebugOverlay().render();
}

std::optional<physics::PhysicsGpuStageTiming>
Application::pollPhysicsGpuTimingSample() noexcept {
    if (physicsGpuTimingSampleCount_ == 0u) return std::nullopt;
    physics::PhysicsGpuStageTiming result =
        std::move(physicsGpuTimingSamples_[physicsGpuTimingSampleHead_]);
    physicsGpuTimingSampleHead_ =
        (physicsGpuTimingSampleHead_ + 1u)
        % kPhysicsGpuTimingSampleCapacity;
    --physicsGpuTimingSampleCount_;
    return result;
}

std::optional<RenderGpuStageTiming>
Application::pollRenderGpuTimingSample() noexcept {
    if (renderGpuTimingSampleCount_ == 0u) return std::nullopt;
    RenderGpuStageTiming result =
        renderGpuTimingSamples_[renderGpuTimingSampleHead_];
    renderGpuTimingSampleHead_ =
        (renderGpuTimingSampleHead_ + 1u)
        % kRenderGpuTimingSampleCapacity;
    --renderGpuTimingSampleCount_;
    return result;
}

WGPUTextureView Application::getOrCreateDepthView() {
    if (!gpuContext_) return nullptr;

    uint32_t width = gpuContext_->getSwapchainWidth();
    uint32_t height = gpuContext_->getSwapchainHeight();

    // Check if we need to recreate
    if (depthView_ && depthWidth_ == width && depthHeight_ == height) {
        return depthView_;
    }

    // Create depth texture
    WGPUTextureDescriptor desc = {};
    WGPU_SET_LABEL(desc, "depth_texture");
    desc.usage = WGPUTextureUsage_RenderAttachment;
    desc.dimension = WGPUTextureDimension_2D;
    desc.size = {width, height, 1};
    desc.format = WGPUTextureFormat_Depth32Float;
    desc.mipLevelCount = 1;
    desc.sampleCount = 1;

    WGPUTexture nextTexture =
        wgpuDeviceCreateTexture(gpuContext_->getDevice(), &desc);
    if (!nextTexture) {
        LOG_ERROR("Failed to create depth texture");
        return nullptr;
    }

    WGPUTextureViewDescriptor viewDesc = {};
    WGPU_SET_LABEL(viewDesc, "depth_view");
    viewDesc.format = WGPUTextureFormat_Depth32Float;
    viewDesc.dimension = WGPUTextureViewDimension_2D;
    viewDesc.baseMipLevel = 0;
    viewDesc.mipLevelCount = 1;
    viewDesc.baseArrayLayer = 0;
    viewDesc.arrayLayerCount = 1;
    viewDesc.aspect = WGPUTextureAspect_DepthOnly;

    WGPUTextureView nextView = wgpuTextureCreateView(nextTexture, &viewDesc);
    if (!nextView) {
        LOG_ERROR("Failed to create depth texture view");
        wgpuTextureDestroy(nextTexture);
        wgpuTextureRelease(nextTexture);
        return nullptr;
    }

    if (depthView_) wgpuTextureViewRelease(depthView_);
    if (depthTexture_) {
        wgpuTextureDestroy(depthTexture_);
        wgpuTextureRelease(depthTexture_);
    }
    depthTexture_ = nextTexture;
    depthView_ = nextView;
    depthWidth_ = width;
    depthHeight_ = height;

    return depthView_;
}

// ─────────────────────────────────────────────────────────────────────────────
// Input Processing
// ─────────────────────────────────────────────────────────────────────────────

void Application::processInput(float deltaTime) {
    processThrowableInput(deltaTime);
}

bool Application::spawnThrowable(
    physics::ThrowableShape shape, const glm::vec3& origin,
    const glm::vec3& direction, const glm::ivec3& sector) {
    if (!physicsWorld_
        || static_cast<uint32_t>(shape)
            >= static_cast<uint32_t>(physics::ThrowableShape::Count)
        || !std::isfinite(origin.x) || !std::isfinite(origin.y)
        || !std::isfinite(origin.z) || !std::isfinite(direction.x)
        || !std::isfinite(direction.y) || !std::isfinite(direction.z)
        || glm::dot(direction, direction) < 1.0e-8f) {
        return false;
    }

    const glm::vec3 normalizedDirection = glm::normalize(direction);
    glm::vec3 launchOrigin = origin;
    glm::ivec3 launchSector = sector;
    std::optional<double> cubeTriangleImpactX;
    if (config_.cubePyramidBodyCount != 0u) {
        // The complete wall needs a distant overview camera. Preserve the
        // ordinary 28 m/s projectile and its real collision response, but
        // move its starting point along the same sight line to a launch plane
        // 20 m before the wall. This avoids turning a normal click into a
        // 160 m/s cannon shot merely to cover the camera distance.
        constexpr double launchPlaneZ =
            static_cast<double>(kBrowserJourneyTargetZ) - 20.0;
        const physics::WorldPosition source =
            physics::canonicalWorldPosition(sector, glm::dvec3(origin));
        glm::dvec3 absolute =
            physics::worldPositionToAbsolute(source);
        const double directionZ =
            static_cast<double>(normalizedDirection.z);
        if (std::abs(directionZ) > 1.0e-6) {
            const double distance =
                (launchPlaneZ - absolute.z) / directionZ;
            if (distance > 0.0 && distance <= 512.0) {
                const glm::dvec3 candidate = absolute
                    + glm::dvec3(normalizedDirection) * distance;
                constexpr double wallHalfWidth = 180.0;
                constexpr double wallVerticalMargin = 200.0;
                if (std::abs(
                        candidate.x
                        - static_cast<double>(kBrowserJourneyTargetX))
                        <= wallHalfWidth
                    && std::abs(candidate.y - absolute.y)
                        <= wallVerticalMargin) {
                    const physics::WorldPosition launch =
                        physics::worldPositionFromAbsolute(candidate);
                    launchOrigin = launch.local;
                    launchSector = launch.sector;
                    cubeTriangleImpactX = candidate.x;
                }
            }
        }
    }
    if (cubeTriangleImpactX) {
        wakeCubeTriangleImpactColumns(*cubeTriangleImpactX);
    }

    constexpr float throwSpeed = 28.0f;
    if (physicsWorld_->backendType()
        == physics::BackendType::Box3DReference) {
        return physicsWorld_->throwBody(
            shape, launchOrigin, normalizedDirection * throwSpeed);
    }

    physics::BodySpawnDesc desc;
    desc.shape = shape;
    desc.position = launchOrigin;
    desc.sector = launchSector;
    desc.linearVelocity = normalizedDirection * throwSpeed;
    desc.angularVelocity = {3.5f, 5.0f, 2.5f};
    desc.dimensions = physics::throwableShapeDimensions(shape);
    return physicsWorld_->spawnBody(desc).valid();
}

uint32_t Application::throwThrowableBatch(
    physics::ThrowableShape shape, uint32_t maximumBodies) {
    if (!camera_ || !physicsWorld_ || maximumBodies == 0u) return 0u;

    constexpr uint32_t maximumColumns = 16u;
    constexpr uint32_t maximumRows = 8u;
    constexpr uint32_t fullBatchSize = maximumColumns * maximumRows;
    const uint32_t batchSize = std::min(maximumBodies, fullBatchSize);
    const uint32_t columns = std::min(batchSize, maximumColumns);
    const uint32_t rows = (batchSize + columns - 1u) / columns;
    const glm::vec3 direction = glm::normalize(camera_->forward());
    const glm::vec3 dimensions =
        physics::throwableShapeDimensions(shape);
    const float maximumDimension = std::max(
        dimensions.x, std::max(dimensions.y, dimensions.z));
    const float spacing = maximumDimension * 1.08f + 0.02f;
    const float halfWidth = 0.5f * static_cast<float>(columns - 1u)
                          * spacing + 0.5f * maximumDimension;
    const float halfHeight = 0.5f * static_cast<float>(rows - 1u)
                           * spacing + 0.5f * maximumDimension;
    const float tanHalfFov = std::max(
        std::tan(camera_->fovY() * 0.5f), 1.0e-3f);
    const float verticalDistance = halfHeight / tanHalfFov;
    const float horizontalDistance = halfWidth
        / (tanHalfFov * std::max(camera_->aspectRatio(), 1.0e-3f));
    const uint32_t batchLane =
        (physicsWorld_->stats().residentBodies / fullBatchSize) % 4u;
    const float batchDistance = std::max(
        2.2f, std::max(verticalDistance, horizontalDistance) + 0.5f)
        + static_cast<float>(batchLane) * spacing * 1.5f;
    const glm::vec3 batchCenter =
        camera_->position() + direction * batchDistance;
    const glm::vec3 cameraRight = glm::normalize(camera_->right());
    const glm::vec3 cameraUp = glm::normalize(camera_->up());
    const glm::ivec3 cameraSector = camera_->worldSector();

    uint32_t thrown = 0u;
    for (uint32_t index = 0u; index < batchSize; ++index) {
        const uint32_t column = index % columns;
        const uint32_t row = index / columns;
        const float x = (static_cast<float>(column)
            - 0.5f * static_cast<float>(columns - 1u)) * spacing;
        const float y = (static_cast<float>(row)
            - 0.5f * static_cast<float>(rows - 1u)) * spacing;
        const float coneX = x / std::max(halfWidth, 1.0e-3f);
        const float coneY = y / std::max(halfHeight, 1.0e-3f);
        glm::vec3 launchDirection = glm::normalize(
            direction + cameraRight * (coneX * 0.08f)
                      + cameraUp * (coneY * 0.08f));
        glm::vec3 spawnOrigin = batchCenter
                              + cameraRight * x + cameraUp * y;
        if (characterController_) {
            const float worldX = spawnOrigin.x
                + static_cast<float>(cameraSector.x)
                * physics::kWorldSectorSize;
            const float worldZ = spawnOrigin.z
                + static_cast<float>(cameraSector.z)
                * physics::kWorldSectorSize;
            const float terrainHeight =
                characterController_->sampleTerrainHeight(worldX, worldZ);
            const float clearance = dimensions.y * 0.5f + 0.05f;
            const float minimumY = terrainHeight
                - static_cast<float>(cameraSector.y)
                * physics::kWorldSectorSize
                + clearance;
            if (spawnOrigin.y < minimumY) {
                spawnOrigin.y = minimumY;
                const glm::vec3 terrainNormal = glm::normalize(
                    characterController_->sampleTerrainNormal(
                        worldX, worldZ));
                const float intoTerrain =
                    glm::dot(launchDirection, terrainNormal);
                if (intoTerrain < 0.0f) {
                    launchDirection = glm::normalize(
                        launchDirection - terrainNormal * intoTerrain
                        + terrainNormal * 0.1f);
                }
            }
        }
        thrown += spawnThrowable(
            shape, spawnOrigin, launchDirection, cameraSector) ? 1u : 0u;
    }
    return thrown;
}

void Application::processThrowableInput(float deltaTime) {
    if (!input_ || !camera_ || !physicsWorld_) {
        return;
    }

    const float wheel = input_->scrollDelta();
    if (wheel != 0.0f) {
        if (throwableWheelAccumulator_ * wheel < 0.0f) {
            throwableWheelAccumulator_ = 0.0f;
        }
        throwableWheelAccumulator_ += wheel;
    }

    constexpr float wheelStep = 1.0f;
    if (std::abs(throwableWheelAccumulator_) >= wheelStep) {
        constexpr uint32_t count = static_cast<uint32_t>(
            physics::PhysicsWorld::ThrowableShape::Count);
        if (throwableWheelAccumulator_ > 0.0f) {
            selectedThrowable_ = (selectedThrowable_ + 1u) % count;
        } else {
            selectedThrowable_ = (selectedThrowable_ + count - 1u) % count;
        }
        throwableWheelAccumulator_ = 0.0f;
        const auto shape = static_cast<physics::PhysicsWorld::ThrowableShape>(
            selectedThrowable_);
        LOG_INFO("Selected throwable: {}",
                 physics::PhysicsWorld::throwableShapeName(shape));
    }

    const bool mouseCaptured = input_->isMouseCaptured();
    const bool batchRequested = mouseCaptured
                             && input_->wasMouseButtonPressed(MouseButton::Right);
    const bool firing = mouseCaptured
                     && input_->isMouseButtonDown(MouseButton::Left);
    if (!batchRequested && !firing) {
        throwableCooldown_ = 0.0f;
        return;
    }

    const auto shape = static_cast<physics::PhysicsWorld::ThrowableShape>(
        selectedThrowable_);
    const glm::vec3 direction = glm::normalize(camera_->forward());
    const glm::vec3 origin = camera_->position() + direction * 2.2f;

    if (batchRequested) {
        constexpr uint32_t batchSize = 16u * 8u;
        const uint32_t thrown = throwThrowableBatch(shape, batchSize);
        LOG_INFO("Threw {} x {}", thrown,
                 physics::PhysicsWorld::throwableShapeName(shape));
    }

    if (!firing) {
        throwableCooldown_ = 0.0f;
        return;
    }

    const float frameTime = std::clamp(deltaTime, 0.0f, 0.1f);
    throwableCooldown_ -= frameTime;
    constexpr float throwInterval = 1.0f / 100.0f;
    while (throwableCooldown_ <= 0.0f) {
        if (throwableBodyLimit_ != 0u
            && physicsWorld_->stats().residentBodies
                >= throwableBodyLimit_) {
            break;
        }
        if (spawnThrowable(
                shape, origin, direction, camera_->worldSector())) {
            LOG_INFO("Threw {}", physics::PhysicsWorld::throwableShapeName(shape));
        }
        throwableCooldown_ += throwInterval;
    }
}

void Application::handleKeyboardShortcuts() {
    if (!input_) return;

    // Escape - release mouse or exit
    if (input_->wasKeyPressed(Key::Escape)) {
        if (input_->isMouseCaptured()) {
            input_->releaseMouse();
        } else {
#if !defined(VOXY_WASM)
            requestExit();
#endif
        }
    }

    // F1 - toggle debug overlay
    if (input_->wasKeyPressed(Key::F1)) {
        getDebugOverlay().toggle();
    }

    // F2 - toggle wireframe mode (triangle path)
    if (input_->wasKeyPressed(Key::F2)) {
        toggleWireframe();
    }

    // F3 - toggle render path
    if (input_->wasKeyPressed(Key::F3)) {
        toggleRenderPath();
    }

    // F4 - toggle depth visualization
    if (input_->wasKeyPressed(Key::F4)) {
        if (debugVisMode_ == DebugVisMode::Depth) {
            setDebugVisMode(DebugVisMode::Off);
        } else {
            setDebugVisMode(DebugVisMode::Depth);
        }
    }

    // F5 - toggle normal visualization
    if (input_->wasKeyPressed(Key::F5)) {
        if (debugVisMode_ == DebugVisMode::Normals) {
            setDebugVisMode(DebugVisMode::Off);
        } else {
            setDebugVisMode(DebugVisMode::Normals);
        }
    }

    // F6 - toggle mip level heat map (raycast path)
    if (input_->wasKeyPressed(Key::F6)) {
        if (debugVisMode_ == DebugVisMode::MipLevels) {
            setDebugVisMode(DebugVisMode::Off);
        } else {
            setDebugVisMode(DebugVisMode::MipLevels);
        }
    }

    // F7 - toggle benchmark mode
    if (!wreckwaterClientState_ && !motoSession_
        && input_->wasKeyPressed(Key::F7)) {
        toggleBenchmark();
    }
    
    // F8 - toggle controller mode (free-fly / character)
    if (!wreckwaterClientState_ && !motoSession_
        && input_->wasKeyPressed(Key::F8)) {
        toggleControllerMode();
    }

    // F9 - toggle uncapped FPS mode
    if (input_->wasKeyPressed(Key::F9)) {
        toggleUncappedFPS();
    }

    // K - toggle Lego Mode (F10 is reserved by browser)
    if (input_->wasKeyPressed(Key::K)) {
        toggleLegoMode();
    }

    // Free-fly recording and teleport shortcuts must not move an
    // authority-following multiplayer camera.
    if (wreckwaterClientState_
        || (motoSession_ && motoSession_->isInitialized())) return;

    // R - Record camera position
    if (input_->wasKeyPressed(Key::R)) {
        if (camera_) {
            CameraState state;
            state.position = camera_->position();
            state.yaw = camera_->yaw();
            state.pitch = camera_->pitch();
            state.sector = camera_->worldSector();

            recordedPositions_.push_back(state);
            LOG_INFO("Recorded state [{}] : Pos({:.2f}, {:.2f}, {:.2f}) Yaw({:.2f}) Pitch({:.2f})",
                     recordedPositions_.size() - 1, state.position.x, state.position.y, state.position.z,
                     state.yaw, state.pitch);

            LOG_INFO("All recorded positions:");
            LOG_INFO("teleportTargets_ = {{");
            for (const auto& s : recordedPositions_) {
                LOG_INFO("    {{ {{ {:.2f}f, {:.2f}f, {:.2f}f }}, {:.4f}f, {:.4f}f }},",
                         s.position.x, s.position.y, s.position.z, s.yaw, s.pitch);
            }
            LOG_INFO("}};");
        }
    }

    // 1-9 - Teleport
    auto checkTeleport = [&](Key key, size_t index) {
        if (input_->wasKeyPressed(key)) {
            if (index < teleportTargets_.size()) {
                if (camera_) {
                    const auto& target = teleportTargets_[index];
                    camera_->setWorldPosition(target.sector, target.position);
                    camera_->setYaw(target.yaw);
                    camera_->setPitch(target.pitch);
                    LOG_INFO("Teleported to position {}: {:.2f}, {:.2f}, {:.2f}",
                             index + 1, target.position.x, target.position.y, target.position.z);
                }
            } else {
                LOG_WARN("No teleport target for index {} (defined: {})", index + 1, teleportTargets_.size());
            }
        }
    };

    checkTeleport(Key::Num1, 0);
    checkTeleport(Key::Num2, 1);
    checkTeleport(Key::Num3, 2);
    checkTeleport(Key::Num4, 3);
    checkTeleport(Key::Num5, 4);
    checkTeleport(Key::Num6, 5);
    checkTeleport(Key::Num7, 6);
    checkTeleport(Key::Num8, 7);
    checkTeleport(Key::Num9, 8);
}

void Application::captureScreenshot(const std::string& filepath) {
#if defined(VOXY_WASM)
    static_cast<void>(filepath);
    LOG_WARN("Screenshots are not supported on WebAssembly builds");
    return;
#else
    if (!gpuContext_) return;

    LOG_INFO("Capturing screenshot to: {}", filepath);

    WGPUDevice device = gpuContext_->getDevice();
    WGPUQueue queue = gpuContext_->getQueue();
    WGPUTexture sourceTexture = config_.benchmarkOnStartup
        ? benchmarkTargetTexture_ : gpuContext_->getCurrentTexture();

    if (!sourceTexture) {
        LOG_ERROR("No current texture to capture");
        return;
    }

    uint32_t width = gpuContext_->getSwapchainWidth();
    uint32_t height = gpuContext_->getSwapchainHeight();

    const WGPUTextureFormat format = gpuContext_->getSwapchainFormat();
    const bool bgra = format == WGPUTextureFormat_BGRA8Unorm
                   || format == WGPUTextureFormat_BGRA8UnormSrgb;
    const bool rgba = format == WGPUTextureFormat_RGBA8Unorm
                   || format == WGPUTextureFormat_RGBA8UnormSrgb;
    if (!bgra && !rgba) {
        LOG_ERROR("Screenshot capture does not support texture format {}",
                  gpu::textureFormatToString(format));
        return;
    }
    if (width == 0u || height == 0u
        || width > static_cast<uint32_t>(std::numeric_limits<int>::max())
        || height > static_cast<uint32_t>(std::numeric_limits<int>::max())) {
        LOG_ERROR("Invalid screenshot dimensions: {}x{}", width, height);
        return;
    }

    // Bytes per row must be a multiple of 256.
    constexpr uint64_t bytesPerPixel = 4u;
    constexpr uint64_t rowAlignment = 256u;
    const uint64_t unalignedBytesPerRow =
        static_cast<uint64_t>(width) * bytesPerPixel;
    const uint64_t alignedBytesPerRow =
        (unalignedBytesPerRow + rowAlignment - 1u)
        & ~(rowAlignment - 1u);
    const uint64_t bufferSize =
        alignedBytesPerRow * static_cast<uint64_t>(height);
    const uint64_t pixelBytes =
        static_cast<uint64_t>(width) * height * bytesPerPixel;
    if (alignedBytesPerRow > std::numeric_limits<uint32_t>::max()
        || bufferSize > std::numeric_limits<size_t>::max()
        || pixelBytes > std::numeric_limits<size_t>::max()) {
        LOG_ERROR("Screenshot dimensions overflow the readback layout");
        return;
    }
    const uint32_t bytesPerRow =
        static_cast<uint32_t>(alignedBytesPerRow);
    const size_t mappedSize = static_cast<size_t>(bufferSize);

    WGPUBufferDescriptor bufferDesc = {};
    bufferDesc.label = "screenshot_buffer";
    bufferDesc.size = bufferSize;
    bufferDesc.usage = WGPUBufferUsage_CopyDst | WGPUBufferUsage_MapRead;
    WGPUBuffer buffer = wgpuDeviceCreateBuffer(device, &bufferDesc);
    if (!buffer) {
        LOG_ERROR("Failed to create screenshot readback buffer");
        return;
    }

    WGPUCommandEncoderDescriptor encoderDesc = {};
    encoderDesc.label = "screenshot_encoder";
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(device, &encoderDesc);
    if (!encoder) {
        LOG_ERROR("Failed to create screenshot command encoder");
        wgpuBufferDestroy(buffer);
        wgpuBufferRelease(buffer);
        return;
    }

    WGPUImageCopyTexture src = {};
    src.texture = sourceTexture;
    src.origin = {0, 0, 0};

    WGPUImageCopyBuffer dst = {};
    dst.buffer = buffer;
    dst.layout.offset = 0;
    dst.layout.bytesPerRow = bytesPerRow;
    dst.layout.rowsPerImage = height;

    WGPUExtent3D extent = {width, height, 1};

    wgpuCommandEncoderCopyTextureToBuffer(encoder, &src, &dst, &extent);

    WGPUCommandBufferDescriptor cmdDesc = {};
    WGPUCommandBuffer cmd = wgpuCommandEncoderFinish(encoder, &cmdDesc);
    wgpuCommandEncoderRelease(encoder);
    if (!cmd) {
        LOG_ERROR("Failed to finish screenshot command buffer");
        wgpuBufferDestroy(buffer);
        wgpuBufferRelease(buffer);
        return;
    }

#if defined(VOXY_USE_DAWN)
    wgpuQueueSubmit(queue, 1, &cmd);
    WGPUWrappedSubmissionIndex submission = {queue, 0}; // unused for Dawn path
#else
    // Capture submission index so we can explicitly wait for completion.
    WGPUSubmissionIndex submissionIndex = wgpuQueueSubmitForIndex(queue, 1, &cmd);
    WGPUWrappedSubmissionIndex submission = {queue, submissionIndex};
#endif
    wgpuCommandBufferRelease(cmd);

    auto* mapState = new ScreenshotMapState;
    wgpuBufferMapAsync(
        buffer, WGPUMapMode_Read, 0, mappedSize,
        onScreenshotBufferMapped, mapState);

    // Wait for mapping
    // Process events to allow callbacks to fire
    // wgpu-native callbacks are typically synchronous, but Dawn requires polling
    constexpr int maxPollAttempts = 1000;
    int pollAttempt = 0;
    while (!mapState->done.load(std::memory_order_acquire)
           && pollAttempt < maxPollAttempts) {
#if defined(VOXY_USE_DAWN)
        WGPUInstance instance = gpuContext_->getInstance();
        wgpuInstanceProcessEvents(instance);
    #if defined(_WIN32)
        Sleep(1);
    #else
        usleep(1000);
    #endif
#else
        // Pump the wgpu-native device, waiting on our submission.
        wgpuDevicePoll(device, /*wait=*/true, &submission);
#endif

        pollAttempt++;
    }
    
    const bool mappingDone =
        mapState->done.load(std::memory_order_acquire);
    const bool mappingSucceeded = mappingDone
        && mapState->succeeded.load(std::memory_order_relaxed);
    releaseScreenshotMapState(mapState);
    if (!mappingDone) {
        LOG_ERROR("Buffer mapping timed out after {} poll attempts", maxPollAttempts);
        wgpuBufferDestroy(buffer);
        wgpuBufferRelease(buffer);
        return;
    }
    if (!mappingSucceeded) {
        wgpuBufferDestroy(buffer);
        wgpuBufferRelease(buffer);
        return;
    }

    // Read data
    const uint8_t* data = static_cast<const uint8_t*>(
        wgpuBufferGetConstMappedRange(buffer, 0, mappedSize));
    if (!data) {
        LOG_ERROR("Failed to map buffer range");
        wgpuBufferUnmap(buffer);
        wgpuBufferDestroy(buffer);
        wgpuBufferRelease(buffer);
        return;
    }

    // Convert to RGBA and remove row padding.
    std::vector<uint8_t> pngData(static_cast<size_t>(pixelBytes));
    for (uint32_t y = 0; y < height; ++y) {
        for (uint32_t x = 0; x < width; ++x) {
            const size_t srcIndex = static_cast<size_t>(y) * bytesPerRow
                                  + static_cast<size_t>(x) * 4u;
            const size_t dstIndex =
                (static_cast<size_t>(y) * width + x) * 4u;
            pngData[dstIndex + 0] = data[srcIndex + (bgra ? 2u : 0u)];
            pngData[dstIndex + 1] = data[srcIndex + 1]; // G
            pngData[dstIndex + 2] = data[srcIndex + (bgra ? 0u : 2u)];
            pngData[dstIndex + 3] = data[srcIndex + 3]; // A
        }
    }

    wgpuBufferUnmap(buffer);
    wgpuBufferDestroy(buffer);
    wgpuBufferRelease(buffer);

    if (stbi_write_png(filepath.c_str(), static_cast<int>(width), static_cast<int>(height), 4, pngData.data(), static_cast<int>(width) * 4)) {
        LOG_INFO("Saved screenshot to: {}", filepath);
    } else {
        LOG_ERROR("Failed to save screenshot to: {}", filepath);
    }
#endif
}

} // namespace voxy
