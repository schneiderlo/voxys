#include "client/wreckwater_application_client.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

#include <glm/geometric.hpp>

#include "physics/terrain_topology.hpp"

namespace voxy::client {
namespace {

[[nodiscard]] bool finiteVector(const glm::vec3& value) noexcept {
    return std::isfinite(value.x) && std::isfinite(value.y)
        && std::isfinite(value.z);
}

[[nodiscard]] bool validTerrainView(
    const WreckwaterCameraTerrainView& terrain) noexcept {
    if (terrain.width < 2u || terrain.height < 2u
        || !std::isfinite(terrain.heightScale)
        || terrain.heightScale <= 0.0f
        || !std::isfinite(terrain.cellScale)
        || terrain.cellScale < 1.0e-4f
        || terrain.maximumIntervals == 0u
        || terrain.maximumVisitedCells == 0u) {
        return false;
    }
    const uint64_t sampleCount =
        static_cast<uint64_t>(terrain.width)
        * static_cast<uint64_t>(terrain.height);
    return sampleCount <= std::numeric_limits<size_t>::max()
        && terrain.samples.size()
            >= static_cast<size_t>(sampleCount);
}

constexpr double kTerrainProbeIntervalMeters = 0.25;

[[nodiscard]] bool finiteNonNegative(float value) noexcept {
    return std::isfinite(value) && value >= 0.0f;
}

[[nodiscard]] uint64_t intervalCount(
    double pathDistance) noexcept {
    if (!std::isfinite(pathDistance)
        || pathDistance <= 0.0) {
        return 0u;
    }
    const double count =
        std::ceil(pathDistance / kTerrainProbeIntervalMeters);
    if (!std::isfinite(count)
        || count
            > static_cast<double>(
                std::numeric_limits<uint64_t>::max())) {
        return std::numeric_limits<uint64_t>::max();
    }
    return std::max<uint64_t>(
        1u, static_cast<uint64_t>(count));
}

[[nodiscard]] bool validProbeRequest(
    const WreckwaterCameraObstructionProbeRequest& request)
    noexcept {
    return request.identity.sequence != 0u
        && request.identity.playerId != 0u
        && request.identity.characterHandle != 0u
        && request.identity.connectionGeneration != 0u
        && request.physicsQuery.type
            == physics::PhysicsQueryType::SphereCast
        && request.physicsQuery.requestId
            == static_cast<uint32_t>(request.identity.sequence)
        && finiteNonNegative(request.physicsQuery.radius)
        && std::isfinite(request.pathDistance)
        && request.pathDistance > 0.0f
        && physics::isValidWorldPosition(request.worldStart)
        && physics::isValidWorldPosition(request.worldEnd);
}

[[nodiscard]] float worldHeight(
    uint16_t sample, float heightScale) noexcept {
    return physics::terrain_topology::worldHeight(
        static_cast<float>(sample), heightScale);
}

[[nodiscard]] bool finiteHit(
    const physics::PhysicsQueryHit& hit,
    uint32_t requestId,
    float maximumDistance) noexcept {
    const float tolerance =
        std::max(0.001f, maximumDistance * 0.001f);
    return hit.requestId == requestId
        && hit.type == physics::PhysicsQueryType::SphereCast
        && std::isfinite(hit.fraction)
        && std::isfinite(hit.distance)
        && hit.fraction >= 0.0f
        && hit.fraction <= 1.0f + 0.001f
        && hit.distance >= 0.0f
        && hit.distance <= maximumDistance + tolerance
        && finiteVector(hit.point)
        && finiteVector(hit.normal);
}

} // namespace

bool mapWreckwaterCameraRelativeControls(
    const WreckwaterApplicationDigitalControls& digital,
    const glm::vec3& cameraForward,
    const glm::vec3& cameraRight,
    WreckwaterGraphicalClientControls& output) noexcept {
    output = {
        .jumpDown = digital.jumpDown,
        .boardDown = digital.boardDown,
    };
    if (!finiteVector(cameraForward)
        || !finiteVector(cameraRight)) {
        return false;
    }

    glm::vec2 forward(cameraForward.x, cameraForward.z);
    glm::vec2 right(cameraRight.x, cameraRight.z);
    float forwardLengthSquared = glm::dot(forward, forward);
    const float rightLengthSquared = glm::dot(right, right);
    if (!std::isfinite(forwardLengthSquared)
        || !std::isfinite(rightLengthSquared)
        || (forwardLengthSquared < 1.0e-8f
            && rightLengthSquared < 1.0e-8f)) {
        return false;
    }
    if (forwardLengthSquared < 1.0e-8f) {
        right *= 1.0f / std::sqrt(rightLengthSquared);
        forward = {right.y, -right.x};
        forwardLengthSquared = 1.0f;
    }
    forward *= 1.0f / std::sqrt(forwardLengthSquared);

    right -= forward * glm::dot(right, forward);
    const float orthogonalRightLengthSquared =
        glm::dot(right, right);
    if (!std::isfinite(orthogonalRightLengthSquared)
        || orthogonalRightLengthSquared < 1.0e-8f) {
        right = {-forward.y, forward.x};
    } else {
        right *= 1.0f / std::sqrt(
            orthogonalRightLengthSquared);
    }

    const float lateral =
        (digital.right ? 1.0f : 0.0f)
        - (digital.left ? 1.0f : 0.0f);
    const float longitudinal =
        (digital.forward ? 1.0f : 0.0f)
        - (digital.backward ? 1.0f : 0.0f);
    glm::vec2 movement =
        right * lateral + forward * longitudinal;
    const float movementLengthSquared =
        glm::dot(movement, movement);
    if (!std::isfinite(movementLengthSquared)) return false;
    if (movementLengthSquared > 1.0f) {
        movement *= 1.0f / std::sqrt(movementLengthSquared);
    }
    if (movement.x == 0.0f) movement.x = 0.0f;
    if (movement.y == 0.0f) movement.y = 0.0f;
    output.movement = movement;
    return true;
}

bool wreckwaterCameraTerrainViewSupports(
    const WreckwaterCameraTerrainView& terrain,
    float maximumPathDistance,
    float sphereRadius) noexcept {
    if (!validTerrainView(terrain)
        || !std::isfinite(maximumPathDistance)
        || maximumPathDistance <= 0.0f
        || !finiteNonNegative(sphereRadius)) {
        return false;
    }
    const uint64_t intervals =
        intervalCount(maximumPathDistance);
    if (intervals == 0u
        || intervals > terrain.maximumIntervals) {
        return false;
    }
    const double intervalDistance =
        static_cast<double>(maximumPathDistance)
        / static_cast<double>(intervals);
    const double spanSamples =
        (intervalDistance
         + 2.0 * static_cast<double>(sphereRadius))
        / static_cast<double>(terrain.cellScale);
    if (!std::isfinite(spanSamples)) return false;
    const double side = std::ceil(spanSamples) + 4.0;
    if (!std::isfinite(side)
        || side
            > std::sqrt(static_cast<double>(
                std::numeric_limits<uint64_t>::max()))) {
        return false;
    }
    const uint64_t cellsPerInterval =
        static_cast<uint64_t>(side)
        * static_cast<uint64_t>(side);
    return cellsPerInterval == 0u
        || intervals
            <= terrain.maximumVisitedCells
                / cellsPerInterval;
}

WreckwaterCameraTerrainCastResult
wreckwaterCameraTerrainSphereCast(
    const WreckwaterCameraObstructionProbeRequest& request,
    const WreckwaterCameraTerrainView& terrain) noexcept {
    WreckwaterCameraTerrainCastResult result;
    if (!validProbeRequest(request)) {
        result.status =
            WreckwaterCameraTerrainCastStatus::InvalidRequest;
        return result;
    }
    if (!validTerrainView(terrain)) {
        result.status =
            WreckwaterCameraTerrainCastStatus::InvalidTerrain;
        return result;
    }

    const glm::dvec3 start =
        physics::worldPositionToAbsolute(request.worldStart);
    const glm::dvec3 end =
        physics::worldPositionToAbsolute(request.worldEnd);
    const glm::dvec3 path = end - start;
    const double pathLength = glm::length(path);
    const double tolerance = std::max(
        0.001,
        static_cast<double>(request.pathDistance) * 0.001);
    if (!std::isfinite(pathLength)
        || std::abs(
            pathLength
            - static_cast<double>(request.pathDistance))
            > tolerance) {
        result.status =
            WreckwaterCameraTerrainCastStatus::InvalidRequest;
        return result;
    }

    const uint64_t intervals = intervalCount(pathLength);
    if (intervals == 0u
        || intervals > terrain.maximumIntervals) {
        result.status =
            WreckwaterCameraTerrainCastStatus::
                WorkBudgetExceeded;
        return result;
    }
    const glm::dvec2 origin =
        glm::dvec2(
            physics::terrain_topology::centeredOrigin(
                terrain.width, terrain.height,
                terrain.cellScale));
    const double inverseCellScale =
        1.0 / static_cast<double>(terrain.cellScale);
    const double radius =
        static_cast<double>(request.physicsQuery.radius);
    uint64_t visitedCells = 0u;

    for (uint64_t interval = 0u;
         interval < intervals; ++interval) {
        const double t0 =
            static_cast<double>(interval)
            / static_cast<double>(intervals);
        const double t1 =
            static_cast<double>(interval + 1u)
            / static_cast<double>(intervals);
        const glm::dvec3 first = start + path * t0;
        const glm::dvec3 second = start + path * t1;
        const double minimumWorldX =
            std::min(first.x, second.x) - radius;
        const double maximumWorldX =
            std::max(first.x, second.x) + radius;
        const double minimumWorldZ =
            std::min(first.z, second.z) - radius;
        const double maximumWorldZ =
            std::max(first.z, second.z) + radius;
        const double minimumSampleX =
            (minimumWorldX + origin.x) * inverseCellScale;
        const double maximumSampleX =
            (maximumWorldX + origin.x) * inverseCellScale;
        const double minimumSampleZ =
            (minimumWorldZ + origin.y) * inverseCellScale;
        const double maximumSampleZ =
            (maximumWorldZ + origin.y) * inverseCellScale;
        const double maximumTerrainX =
            static_cast<double>(terrain.width - 1u);
        const double maximumTerrainZ =
            static_cast<double>(terrain.height - 1u);
        if (maximumSampleX < 0.0
            || minimumSampleX > maximumTerrainX
            || maximumSampleZ < 0.0
            || minimumSampleZ > maximumTerrainZ) {
            continue;
        }

        const double clampedMinimumX =
            std::clamp(minimumSampleX, 0.0, maximumTerrainX);
        const double clampedMaximumX =
            std::clamp(maximumSampleX, 0.0, maximumTerrainX);
        const double clampedMinimumZ =
            std::clamp(minimumSampleZ, 0.0, maximumTerrainZ);
        const double clampedMaximumZ =
            std::clamp(maximumSampleZ, 0.0, maximumTerrainZ);
        const uint32_t minimumCellX = static_cast<uint32_t>(
            std::max(
                0.0, std::floor(clampedMinimumX) - 1.0));
        const uint32_t maximumCellX = static_cast<uint32_t>(
            std::min(
                static_cast<double>(terrain.width - 2u),
                std::floor(clampedMaximumX) + 1.0));
        const uint32_t minimumCellZ = static_cast<uint32_t>(
            std::max(
                0.0, std::floor(clampedMinimumZ) - 1.0));
        const uint32_t maximumCellZ = static_cast<uint32_t>(
            std::min(
                static_cast<double>(terrain.height - 2u),
                std::floor(clampedMaximumZ) + 1.0));
        if (maximumCellX < minimumCellX
            || maximumCellZ < minimumCellZ) {
            continue;
        }
        const uint64_t intervalCells =
            (static_cast<uint64_t>(maximumCellX)
             - minimumCellX + 1u)
            * (static_cast<uint64_t>(maximumCellZ)
               - minimumCellZ + 1u);
        if (intervalCells
                > terrain.maximumVisitedCells - visitedCells) {
            result.status =
                WreckwaterCameraTerrainCastStatus::
                    WorkBudgetExceeded;
            return result;
        }
        visitedCells += intervalCells;

        uint16_t maximumRawHeight = 0u;
        for (uint32_t z = minimumCellZ;
             z <= maximumCellZ; ++z) {
            for (uint32_t x = minimumCellX;
                 x <= maximumCellX; ++x) {
                const size_t row0 =
                    static_cast<size_t>(z) * terrain.width;
                const size_t row1 =
                    static_cast<size_t>(z + 1u)
                    * terrain.width;
                maximumRawHeight = std::max(
                    {maximumRawHeight,
                     terrain.samples[row0 + x],
                     terrain.samples[row0 + x + 1u],
                     terrain.samples[row1 + x],
                     terrain.samples[row1 + x + 1u]});
            }
        }
        const float maximumHeight =
            worldHeight(maximumRawHeight, terrain.heightScale);
        const double minimumCenterY =
            std::min(first.y, second.y);
        if (static_cast<double>(maximumHeight)
            >= minimumCenterY - radius) {
            result.status =
                WreckwaterCameraTerrainCastStatus::Accepted;
            result.hit = true;
            result.hitDistance = static_cast<float>(
                t0 * pathLength);
            return result;
        }
    }

    result.status =
        WreckwaterCameraTerrainCastStatus::Accepted;
    return result;
}

WreckwaterCameraObstructionProbeResult
mergeWreckwaterCameraObstructionProbeResult(
    const WreckwaterCameraObstructionProbeRequest& request,
    const WreckwaterCameraTerrainCastResult& terrain,
    const physics::PhysicsQueryBatch& rigidBodies) noexcept {
    WreckwaterCameraObstructionProbeResult result;
    result.identity = request.identity;
    if (!validProbeRequest(request) || !terrain
        || (terrain.hit
            && (!finiteNonNegative(terrain.hitDistance)
                || terrain.hitDistance
                    > request.pathDistance))) {
        result.overflow = true;
        return result;
    }
    if (rigidBodies.outputs.size() != 1u) {
        result.overflow = true;
        return result;
    }
    const physics::PhysicsQueryOutput& output =
        rigidBodies.outputs.front();
    if (output.requestId != request.physicsQuery.requestId
        || output.type != physics::PhysicsQueryType::SphereCast
        || output.overflow) {
        result.overflow = true;
        return result;
    }

    float closest = terrain.hit
        ? terrain.hitDistance
        : std::numeric_limits<float>::infinity();
    bool hitFound = terrain.hit;
    for (const auto& hit : output.hits) {
        if (!finiteHit(
                hit, request.physicsQuery.requestId,
                request.pathDistance)) {
            result.overflow = true;
            return result;
        }
        closest = std::min(closest, hit.distance);
        hitFound = true;
    }
    result.hit = hitFound;
    result.hitDistance = hitFound ? closest : 0.0f;
    result.completeScene = true;
    return result;
}

WreckwaterApplicationClient::~WreckwaterApplicationClient() {
    close();
}

bool WreckwaterApplicationClient::initialize(
    const Config& config,
    std::unique_ptr<network::IMultiplayerTransport> transport) {
    close();
    if (transport == nullptr) return false;

    auto runtime =
        std::make_unique<network::WreckwaterClientRuntime>(
            config.runtime, std::move(transport));
    WreckwaterGraphicalClientLoop graphical;
    WreckwaterThirdPersonCameraRig camera;
    if (!runtime->initialized()
        || !graphical.initialize(config.graphical, *runtime)
        || !camera.initialize(config.camera)) {
        runtime->close();
        return false;
    }

    runtime_ = std::move(runtime);
    graphical_ = graphical;
    camera_ = camera;
    initialized_ = true;
    return true;
}

void WreckwaterApplicationClient::close() noexcept {
    if (runtime_) runtime_->close();
    graphical_ = {};
    camera_ = {};
    runtime_.reset();
    initialized_ = false;
}

WreckwaterApplicationClientFrameResult
WreckwaterApplicationClient::frame(
    const WreckwaterApplicationClientFrameInput& input) {
    WreckwaterApplicationClientFrameResult result;
    if (!initialized_ || runtime_ == nullptr) return result;

    result.graphical = graphical_.frame({
        .elapsedNanoseconds = input.elapsedNanoseconds,
        .cameraSector = input.cameraSector,
        .controls = input.controls,
    });

    const float elapsedSeconds = static_cast<float>(
        static_cast<double>(input.elapsedNanoseconds)
        / static_cast<double>(
            kWreckwaterGraphicalClientNanosecondsPerSecond));
    result.camera = camera_.update(
        graphical_.presentation().thirdPersonCameraTarget(),
        input.camera,
        elapsedSeconds);
    result.cameraUpdated =
        result.camera.status
            == WreckwaterThirdPersonCameraStatus::Accepted
        && camera_.pose().valid;
    if (result.cameraUpdated
        && camera_.pose().cameraPosition.sector
            != input.cameraSector) {
        result.presentationRebaseStatus =
            graphical_.presentation().rebaseCameraSector(
                camera_.pose().cameraPosition.sector);
        result.presentationRebased =
            result.presentationRebaseStatus
                == WreckwaterCharacterPresentationStatus::Accepted;
        if (!result.presentationRebased) {
            result.cameraUpdated = false;
        }
    }
    return result;
}

} // namespace voxy::client
