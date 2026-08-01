#include "client/wreckwater_character_presentation.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>

#include <glm/geometric.hpp>

namespace voxy::client {
namespace {

void incrementSaturated(uint64_t& value) noexcept {
    if (value != std::numeric_limits<uint64_t>::max()) ++value;
}

[[nodiscard]] bool finiteFloat(float value) noexcept {
    return std::isfinite(value);
}

[[nodiscard]] bool finiteVector(const glm::vec3& value) noexcept {
    return finiteFloat(value.x)
        && finiteFloat(value.y)
        && finiteFloat(value.z);
}

[[nodiscard]] bool finitePositive(float value) noexcept {
    return finiteFloat(value) && value > 0.0f;
}

[[nodiscard]] bool finiteNonNegative(float value) noexcept {
    return finiteFloat(value) && value >= 0.0f;
}

[[nodiscard]] bool validIdentity(
    const network::WreckwaterClientReplicationIdentity&
        identity) noexcept {
    return identity.sessionId != 0u
        && identity.matchId != 0u
        && identity.worldId != 0u
        && identity.worldEpoch != 0u
        && identity.authorityEpoch != 0u;
}

[[nodiscard]] bool modeFromNetwork(
    const network::WreckwaterCharacterState& source,
    game::WreckwaterCharacterMode& mode) noexcept {
    switch (static_cast<network::WreckwaterCharacterMode>(
        source.stateFlags & 0x3u)) {
        case network::WreckwaterCharacterMode::Airborne:
            mode = game::WreckwaterCharacterMode::Airborne;
            return true;
        case network::WreckwaterCharacterMode::OnSkiff:
            mode = game::WreckwaterCharacterMode::OnSkiff;
            return true;
        case network::WreckwaterCharacterMode::Swimming:
            mode = game::WreckwaterCharacterMode::Swimming;
            return true;
    }
    return false;
}

[[nodiscard]] physics::WorldPosition visualWorldPosition(
    const network::WreckwaterVisualCharacterPose& pose) noexcept {
    return {
        .sector = {
            pose.sector[0], pose.sector[1], pose.sector[2]},
        .local = {
            pose.localFeetPosition.x,
            pose.localFeetPosition.y,
            pose.localFeetPosition.z,
        },
    };
}

[[nodiscard]] glm::vec3 visualVelocity(
    const network::WreckwaterVisualCharacterPose& pose) noexcept {
    return {
        pose.worldVelocity.x,
        pose.worldVelocity.y,
        pose.worldVelocity.z,
    };
}

[[nodiscard]] bool cameraSectorRelative(
    const physics::WorldPosition& position,
    const glm::ivec3& cameraSector,
    uint32_t maximumSectorDelta,
    glm::vec3& output) noexcept {
    if (!physics::isValidWorldPosition(position)) return false;
    const std::array<int32_t, 3> positionSectors{
        position.sector.x,
        position.sector.y,
        position.sector.z,
    };
    const std::array<int32_t, 3> cameraSectors{
        cameraSector.x, cameraSector.y, cameraSector.z};
    const std::array<float, 3> locals{
        position.local.x, position.local.y, position.local.z};
    std::array<float, 3> relative{};
    for (size_t axis = 0u; axis < relative.size(); ++axis) {
        const int64_t delta =
            static_cast<int64_t>(positionSectors[axis])
            - static_cast<int64_t>(cameraSectors[axis]);
        if (delta
                < -static_cast<int64_t>(maximumSectorDelta)
            || delta
                > static_cast<int64_t>(maximumSectorDelta)) {
            return false;
        }
        const double value =
            static_cast<double>(delta)
                * static_cast<double>(physics::kWorldSectorSize)
            + static_cast<double>(locals[axis]);
        if (!std::isfinite(value)
            || value
                < -static_cast<double>(
                    std::numeric_limits<float>::max())
            || value
                > static_cast<double>(
                    std::numeric_limits<float>::max())) {
            return false;
        }
        relative[axis] = static_cast<float>(value);
        if (!finiteFloat(relative[axis])) return false;
    }
    output = {relative[0], relative[1], relative[2]};
    return true;
}

[[nodiscard]] bool translatedWorldPosition(
    const physics::WorldPosition& source,
    const glm::dvec3& offset,
    physics::WorldPosition& output) noexcept {
    if (!physics::isValidWorldPosition(source)
        || !std::isfinite(offset.x)
        || !std::isfinite(offset.y)
        || !std::isfinite(offset.z)) {
        return false;
    }
    const glm::dvec3 absolute =
        physics::worldPositionToAbsolute(source) + offset;
    constexpr double minimum =
        static_cast<double>(std::numeric_limits<int32_t>::min())
            * static_cast<double>(physics::kWorldSectorSize)
        - static_cast<double>(physics::kWorldSectorHalf);
    constexpr double maximum =
        static_cast<double>(std::numeric_limits<int32_t>::max())
            * static_cast<double>(physics::kWorldSectorSize)
        + static_cast<double>(physics::kWorldSectorHalf);
    if (!std::isfinite(absolute.x)
        || !std::isfinite(absolute.y)
        || !std::isfinite(absolute.z)
        || absolute.x < minimum || absolute.x >= maximum
        || absolute.y < minimum || absolute.y >= maximum
        || absolute.z < minimum || absolute.z >= maximum) {
        return false;
    }
    output = physics::worldPositionFromAbsolute(absolute);
    return physics::isValidWorldPosition(output);
}

void canonicalizeZero(glm::vec3& value) noexcept {
    if (value.x == 0.0f) value.x = 0.0f;
    if (value.y == 0.0f) value.y = 0.0f;
    if (value.z == 0.0f) value.z = 0.0f;
}

} // namespace

const char* wreckwaterCharacterPresentationStatusName(
    WreckwaterCharacterPresentationStatus status) noexcept {
    switch (status) {
        case WreckwaterCharacterPresentationStatus::Accepted:
            return "accepted";
        case WreckwaterCharacterPresentationStatus::NotInitialized:
            return "not initialized";
        case WreckwaterCharacterPresentationStatus::
                InvalidConfiguration:
            return "invalid configuration";
        case WreckwaterCharacterPresentationStatus::InvalidSample:
            return "invalid sample";
        case WreckwaterCharacterPresentationStatus::
                SnapshotIdentityMismatch:
            return "snapshot identity mismatch";
        case WreckwaterCharacterPresentationStatus::StaleSample:
            return "stale sample";
        case WreckwaterCharacterPresentationStatus::
                LocalSampleMissing:
            return "local sample missing";
        case WreckwaterCharacterPresentationStatus::
                LocalIdentityMismatch:
            return "local identity mismatch";
        case WreckwaterCharacterPresentationStatus::
                LocalPoseUnavailable:
            return "local pose unavailable";
        case WreckwaterCharacterPresentationStatus::PositionOverflow:
            return "position overflow";
    }
    return "unknown";
}

bool WreckwaterCharacterPresentation::initialize(
    const Config& config) noexcept {
    initialized_ = false;
    if (config.localPlayerId == 0u
        || !finiteNonNegative(config.facingSpeedThreshold)
        || !finiteNonNegative(config.teleportDistance)
        || config.maximumCameraSectorDelta == 0u
        || config.maximumCameraSectorDelta
            > static_cast<uint32_t>(
                std::numeric_limits<int32_t>::max())
        || !finiteNonNegative(config.cameraTargetHeight)
        || !finiteVector(config.defaultFacing)
        || !finiteVector(config.primitiveProxyDimensions)
        || !finitePositive(config.primitiveProxyDimensions.x)
        || !finitePositive(config.primitiveProxyDimensions.y)
        || !finitePositive(config.primitiveProxyDimensions.z)) {
        return false;
    }
    glm::vec3 defaultFacing(
        config.defaultFacing.x, 0.0f,
        config.defaultFacing.z);
    const float facingLengthSquared =
        glm::dot(defaultFacing, defaultFacing);
    if (!finitePositive(facingLengthSquared)) return false;
    defaultFacing *= glm::inversesqrt(facingLengthSquared);
    canonicalizeZero(defaultFacing);

    std::array<
        WreckwaterCharacterPresentationRosterSlot,
        kWreckwaterAvatarPresentationCapacity>
        canonicalRoster{};
    std::array<
        bool, kWreckwaterAvatarPresentationCapacity>
        occupied{};
    bool localFound = false;
    for (const auto& slot : config.roster) {
        if (slot.playerId == 0u
            || slot.crewSlot >= game::kWreckwaterPlayersPerCrew
            || (slot.crew != game::CrewId::CrewOne
                && slot.crew != game::CrewId::CrewTwo)) {
            return false;
        }
        const uint32_t crewIndex =
            slot.crew == game::CrewId::CrewOne ? 0u : 1u;
        const size_t index = static_cast<size_t>(
            crewIndex * game::kWreckwaterPlayersPerCrew
            + slot.crewSlot);
        if (index >= occupied.size() || occupied[index])
            return false;
        for (const auto& prior : canonicalRoster) {
            if (prior.playerId == slot.playerId) return false;
        }
        occupied[index] = true;
        canonicalRoster[index] = slot;
        if (slot.playerId == config.localPlayerId) {
            if (localFound) return false;
            localFound = true;
        }
    }
    if (!localFound
        || !std::all_of(
            occupied.begin(), occupied.end(),
            [](bool value) { return value; })) {
        return false;
    }

    config_ = config;
    config_.defaultFacing = defaultFacing;
    roster_ = canonicalRoster;
    initialized_ = true;
    reset();
    return true;
}

void WreckwaterCharacterPresentation::reset() noexcept {
    states_ = {};
    avatars_ = {};
    cameraTarget_ = {};
    primitiveProxies_ = {};
    identity_ = {};
    latestSnapshotSequence_ = 0u;
    visibleCount_ = 0u;
    identityPinned_ = false;
    telemetry_ = {};
    for (size_t index = 0u; index < roster_.size(); ++index) {
        states_[index].facing = config_.defaultFacing;
        avatars_[index].playerId = roster_[index].playerId;
        avatars_[index].crew = roster_[index].crew;
        avatars_[index].crewSlot = roster_[index].crewSlot;
        avatars_[index].facing = config_.defaultFacing;
    }
}

size_t WreckwaterCharacterPresentation::rosterIndex(
    uint64_t playerId) const noexcept {
    for (size_t index = 0u; index < roster_.size(); ++index) {
        if (roster_[index].playerId == playerId) return index;
    }
    return roster_.size();
}

WreckwaterCharacterPresentationStatus
WreckwaterCharacterPresentation::applyVisible(
    size_t index,
    uint32_t characterHandle,
    uint32_t connectionGeneration,
    game::WreckwaterCharacterMode mode,
    const physics::WorldPosition& worldFeetPosition,
    const glm::vec3& worldVelocity,
    game::SkiffId skiffId,
    uint32_t skiffGeneration,
    uint64_t snapshotSequence,
    uint64_t poseTick,
    float poseFraction,
    bool localPredicted,
    const glm::ivec3& cameraSector,
    std::array<
        SlotState, kWreckwaterAvatarPresentationCapacity>& states,
    std::array<
        WreckwaterAvatarPresentation,
        kWreckwaterAvatarPresentationCapacity>& avatars,
    WreckwaterCharacterPresentationTelemetry& telemetry) const
    noexcept {
    if (index >= states.size()
        || characterHandle == 0u
        || connectionGeneration == 0u
        || !physics::isValidWorldPosition(worldFeetPosition)
        || !finiteVector(worldVelocity)
        || !finiteFloat(poseFraction)
        || poseFraction < 0.0f || poseFraction >= 1.0f
        || (mode != game::WreckwaterCharacterMode::Airborne
            && mode != game::WreckwaterCharacterMode::OnSkiff
            && mode != game::WreckwaterCharacterMode::Swimming)
        || (mode == game::WreckwaterCharacterMode::OnSkiff
            && (skiffId == 0u || skiffGeneration == 0u))
        || (mode != game::WreckwaterCharacterMode::OnSkiff
            && (skiffId != 0u || skiffGeneration != 0u))) {
        return WreckwaterCharacterPresentationStatus::InvalidSample;
    }

    glm::vec3 cameraRelative;
    if (!cameraSectorRelative(
            worldFeetPosition, cameraSector,
            config_.maximumCameraSectorDelta, cameraRelative)) {
        return WreckwaterCharacterPresentationStatus::
            PositionOverflow;
    }

    SlotState& state = states[index];
    const bool sameLifetime =
        state.visible
        && state.characterHandle == characterHandle
        && state.connectionGeneration == connectionGeneration;
    bool teleported = !sameLifetime;
    const bool sameBinding =
        state.mode == mode
        && (mode != game::WreckwaterCharacterMode::OnSkiff
            || (state.skiffId == skiffId
                && state.skiffGeneration == skiffGeneration));
    bool discontinuity =
        !sameLifetime
        || !sameBinding;
    if (sameLifetime) {
        const glm::dvec3 delta =
            physics::worldPositionToAbsolute(worldFeetPosition)
            - physics::worldPositionToAbsolute(
                state.worldFeetPosition);
        const double distance = glm::length(delta);
        if (!std::isfinite(distance)) {
            return WreckwaterCharacterPresentationStatus::
                PositionOverflow;
        }
        if (distance
            > static_cast<double>(config_.teleportDistance)) {
            teleported = true;
            discontinuity = true;
        }
    }

    glm::vec3 facing = config_.defaultFacing;
    const glm::vec2 horizontal(
        worldVelocity.x, worldVelocity.z);
    const float speedSquared = glm::dot(horizontal, horizontal);
    const float thresholdSquared =
        config_.facingSpeedThreshold
        * config_.facingSpeedThreshold;
    if (!finiteNonNegative(speedSquared)) {
        return WreckwaterCharacterPresentationStatus::InvalidSample;
    }
    if (speedSquared > thresholdSquared) {
        const float inverseLength = glm::inversesqrt(speedSquared);
        facing = {
            horizontal.x * inverseLength,
            0.0f,
            horizontal.y * inverseLength,
        };
        canonicalizeZero(facing);
        incrementSaturated(telemetry.facingUpdates);
    } else if (sameLifetime && state.hasFacing) {
        facing = state.facing;
        incrementSaturated(telemetry.facingRetentions);
    }

    state = {
        .characterHandle = characterHandle,
        .connectionGeneration = connectionGeneration,
        .mode = mode,
        .worldFeetPosition = worldFeetPosition,
        .facing = facing,
        .skiffId = skiffId,
        .skiffGeneration = skiffGeneration,
        .visible = true,
        .hasFacing = true,
    };
    WreckwaterAvatarPresentation& avatar = avatars[index];
    avatar.characterHandle = characterHandle;
    avatar.connectionGeneration = connectionGeneration;
    avatar.sourceSnapshotSequence = snapshotSequence;
    avatar.poseTick = poseTick;
    avatar.poseFraction = poseFraction;
    avatar.mode = mode;
    avatar.worldFeetPosition = worldFeetPosition;
    avatar.cameraSectorFeetPosition = cameraRelative;
    avatar.worldVelocity = worldVelocity;
    avatar.facing = facing;
    avatar.skiffId = skiffId;
    avatar.skiffGeneration = skiffGeneration;
    avatar.localPredicted = localPredicted;
    avatar.visible = true;
    avatar.teleported = teleported;
    avatar.discontinuity = discontinuity;
    if (teleported) incrementSaturated(telemetry.teleports);
    if (discontinuity)
        incrementSaturated(telemetry.discontinuities);
    return WreckwaterCharacterPresentationStatus::Accepted;
}

void WreckwaterCharacterPresentation::applyHidden(
    size_t index,
    uint32_t characterHandle,
    uint32_t connectionGeneration,
    std::array<
        SlotState, kWreckwaterAvatarPresentationCapacity>& states,
    std::array<
        WreckwaterAvatarPresentation,
        kWreckwaterAvatarPresentationCapacity>& avatars,
    WreckwaterCharacterPresentationTelemetry& telemetry) const
    noexcept {
    SlotState& state = states[index];
    WreckwaterAvatarPresentation& avatar = avatars[index];
    const bool purged = state.visible;
    if (characterHandle != 0u) {
        state.characterHandle = characterHandle;
        avatar.characterHandle = characterHandle;
    } else {
        avatar.characterHandle = state.characterHandle;
    }
    if (connectionGeneration != 0u) {
        state.connectionGeneration = connectionGeneration;
        avatar.connectionGeneration = connectionGeneration;
    } else {
        avatar.connectionGeneration =
            state.connectionGeneration;
    }
    state.mode = game::WreckwaterCharacterMode::Airborne;
    state.worldFeetPosition = {};
    state.facing = config_.defaultFacing;
    state.skiffId = 0u;
    state.skiffGeneration = 0u;
    state.visible = false;
    state.hasFacing = false;
    avatar.facing = config_.defaultFacing;
    avatar.visible = false;
    avatar.localPredicted = false;
    avatar.discontinuity = purged;
    if (purged) {
        incrementSaturated(telemetry.visibilityPurges);
        incrementSaturated(telemetry.discontinuities);
    }
}

WreckwaterCharacterPresentationStatus
WreckwaterCharacterPresentation::buildCameraAndPrimitiveDescriptors(
    const glm::ivec3& cameraSector,
    const std::array<
        WreckwaterAvatarPresentation,
        kWreckwaterAvatarPresentationCapacity>& avatars,
    WreckwaterThirdPersonCameraTarget& cameraTarget,
    WreckwaterAvatarPrimitiveProxyBatch& proxies) const noexcept {
    cameraTarget = {};
    proxies = {};
    for (const auto& avatar : avatars) {
        if (!avatar.visible) continue;
        if (proxies.count >= proxies.instances.size()) {
            return WreckwaterCharacterPresentationStatus::
                InvalidSample;
        }
        const float yaw =
            std::atan2(avatar.facing.x, avatar.facing.z);
        if (!finiteFloat(yaw)) {
            return WreckwaterCharacterPresentationStatus::
                InvalidSample;
        }
        physics::DynamicBodySnapshot& proxy =
            proxies.instances[proxies.count++];
        proxy.shape = physics::ThrowableShape::Capsule;
        proxy.position = avatar.cameraSectorFeetPosition
            + glm::vec3(
                0.0f,
                config_.primitiveProxyDimensions.y * 0.5f,
                0.0f);
        proxy.rotation = glm::angleAxis(
            yaw, glm::vec3(0.0f, 1.0f, 0.0f));
        proxy.dimensions = config_.primitiveProxyDimensions;
        proxy.active = true;
        proxy.sector = cameraSector;
        if (!finiteVector(proxy.position)) {
            return WreckwaterCharacterPresentationStatus::
                PositionOverflow;
        }

        if (avatar.playerId != config_.localPlayerId) continue;
        physics::WorldPosition worldTarget;
        if (!translatedWorldPosition(
                avatar.worldFeetPosition,
                {0.0,
                 static_cast<double>(
                     config_.cameraTargetHeight),
                 0.0},
                worldTarget)) {
            return WreckwaterCharacterPresentationStatus::
                PositionOverflow;
        }
        glm::vec3 cameraRelativeTarget;
        if (!cameraSectorRelative(
                worldTarget, cameraSector,
                config_.maximumCameraSectorDelta,
                cameraRelativeTarget)) {
            return WreckwaterCharacterPresentationStatus::
                PositionOverflow;
        }
        cameraTarget = {
            .playerId = avatar.playerId,
            .characterHandle = avatar.characterHandle,
            .connectionGeneration =
                avatar.connectionGeneration,
            .worldTargetPosition = worldTarget,
            .cameraSectorTargetPosition =
                cameraRelativeTarget,
            .worldVelocity = avatar.worldVelocity,
            .facing = avatar.facing,
            .mode = avatar.mode,
            .valid = true,
            .teleported = avatar.teleported,
            .discontinuity = avatar.discontinuity,
        };
    }
    return WreckwaterCharacterPresentationStatus::Accepted;
}

WreckwaterCharacterPresentationStatus
WreckwaterCharacterPresentation::update(
    const WreckwaterCharacterController& localController,
    const network::WreckwaterClientSample& sample,
    const glm::ivec3& cameraSector) noexcept {
    const auto reject =
        [this](WreckwaterCharacterPresentationStatus status) {
            incrementSaturated(telemetry_.samplesRejected);
            return status;
        };
    if (!initialized_) {
        return WreckwaterCharacterPresentationStatus::
            NotInitialized;
    }
    if (sample.authoritative.schemaVersion
            != network::kWreckwaterWireSchemaVersion
        || sample.authoritative.flags
            != network::kWreckwaterCertifiedFullSnapshotFlag
        || !validIdentity(sample.authoritative.identity)
        || sample.authoritative.snapshotSequence == 0u
        || sample.characterCount
            > kWreckwaterAvatarPresentationCapacity
        || !finiteFloat(sample.evaluatedPhysicsTick.fraction)
        || sample.evaluatedPhysicsTick.fraction < 0.0f
        || sample.evaluatedPhysicsTick.fraction >= 1.0f) {
        return reject(
            WreckwaterCharacterPresentationStatus::InvalidSample);
    }
    if (identityPinned_
        && sample.authoritative.identity != identity_) {
        return reject(
            WreckwaterCharacterPresentationStatus::
                SnapshotIdentityMismatch);
    }
    if (identityPinned_
        && sample.authoritative.snapshotSequence
            < latestSnapshotSequence_) {
        return reject(
            WreckwaterCharacterPresentationStatus::StaleSample);
    }

    auto candidateStates = states_;
    std::array<
        WreckwaterAvatarPresentation,
        kWreckwaterAvatarPresentationCapacity>
        candidateAvatars{};
    for (size_t index = 0u;
         index < candidateAvatars.size(); ++index) {
        candidateAvatars[index].playerId =
            roster_[index].playerId;
        candidateAvatars[index].crew = roster_[index].crew;
        candidateAvatars[index].crewSlot =
            roster_[index].crewSlot;
        candidateAvatars[index].characterHandle =
            candidateStates[index].characterHandle;
        candidateAvatars[index].connectionGeneration =
            candidateStates[index].connectionGeneration;
        candidateAvatars[index].facing =
            candidateStates[index].hasFacing
            ? candidateStates[index].facing
            : config_.defaultFacing;
    }
    auto candidateTelemetry = telemetry_;
    std::array<
        bool, kWreckwaterAvatarPresentationCapacity>
        seen{};
    const network::WreckwaterCharacterState*
        localAuthoritative = nullptr;

    for (uint32_t sampleIndex = 0u;
         sampleIndex < sample.characterCount; ++sampleIndex) {
        const auto& sampled = sample.characters[sampleIndex];
        const auto& authoritative = sampled.authoritativeState;
        if (!network::isCanonicalWreckwaterCharacterState(
                authoritative)
            || sampled.characterHandle
                != authoritative.characterHandle) {
            return reject(
                WreckwaterCharacterPresentationStatus::
                    InvalidSample);
        }
        const size_t index = rosterIndex(
            authoritative.playerId);
        if (index >= roster_.size() || seen[index]) {
            return reject(
                WreckwaterCharacterPresentationStatus::
                    InvalidSample);
        }
        seen[index] = true;

        game::WreckwaterCharacterMode mode;
        const physics::WorldPosition world =
            visualWorldPosition(sampled.visualPose);
        const glm::vec3 velocity =
            visualVelocity(sampled.visualPose);
        if (!modeFromNetwork(authoritative, mode)
            || !physics::isValidWorldPosition(world)
            || !finiteVector(velocity)) {
            return reject(
                WreckwaterCharacterPresentationStatus::
                    InvalidSample);
        }
        if (authoritative.playerId == config_.localPlayerId) {
            localAuthoritative = &authoritative;
            continue;
        }

        const bool connected =
            (authoritative.stateFlags
                & network::
                    kWreckwaterCharacterStateConnectedFlag)
            != 0u;
        if (!connected) {
            applyHidden(
                index, authoritative.characterHandle,
                authoritative.connectionGeneration,
                candidateStates, candidateAvatars,
                candidateTelemetry);
            continue;
        }
        const WreckwaterCharacterPresentationStatus applied =
            applyVisible(
                index,
                authoritative.characterHandle,
                authoritative.connectionGeneration,
                mode, world, velocity,
                authoritative.skiffId,
                authoritative.skiffGeneration,
                sample.authoritative.snapshotSequence,
                sample.evaluatedPhysicsTick.whole,
                sample.evaluatedPhysicsTick.fraction,
                false, cameraSector,
                candidateStates, candidateAvatars,
                candidateTelemetry);
        if (applied
            != WreckwaterCharacterPresentationStatus::Accepted) {
            return reject(applied);
        }
    }

    const size_t localIndex = rosterIndex(
        config_.localPlayerId);
    for (size_t index = 0u; index < seen.size(); ++index) {
        if (index != localIndex && !seen[index]) {
            applyHidden(
                index, 0u, 0u,
                candidateStates, candidateAvatars,
                candidateTelemetry);
        }
    }

    const bool localReady =
        localController.binding()
            == WreckwaterCharacterControllerBinding::Ready;
    if (localReady) {
        if (localAuthoritative == nullptr) {
            return reject(
                WreckwaterCharacterPresentationStatus::
                    LocalSampleMissing);
        }
        if ((localAuthoritative->stateFlags
                & network::
                    kWreckwaterCharacterStateConnectedFlag)
                == 0u
            || localController.replicationIdentity()
                != sample.authoritative.identity
            || localController.playerId()
                != config_.localPlayerId
            || localController.characterHandle()
                != localAuthoritative->characterHandle
            || localController.connectionGeneration()
                != localAuthoritative
                       ->connectionGeneration) {
            return reject(
                WreckwaterCharacterPresentationStatus::
                    LocalIdentityMismatch);
        }
        const auto localPose = localController.renderPose();
        if (!localPose) {
            return reject(
                WreckwaterCharacterPresentationStatus::
                    LocalPoseUnavailable);
        }
        const WreckwaterCharacterPresentationStatus applied =
            applyVisible(
                localIndex,
                localController.characterHandle(),
                localController.connectionGeneration(),
                localPose.pose.mode,
                localPose.pose.feetPosition,
                localPose.pose.worldVelocity,
                localPose.pose.skiffId,
                localPose.pose.skiffGeneration,
                sample.authoritative.snapshotSequence,
                localPose.pose.tick, 0.0f, true,
                cameraSector,
                candidateStates, candidateAvatars,
                candidateTelemetry);
        if (applied
            != WreckwaterCharacterPresentationStatus::Accepted) {
            return reject(applied);
        }
        incrementSaturated(
            candidateTelemetry.localPredictionOverrides);
    } else {
        const uint32_t handle =
            localAuthoritative != nullptr
            ? localAuthoritative->characterHandle
            : 0u;
        const uint32_t generation =
            localAuthoritative != nullptr
            ? localAuthoritative->connectionGeneration
            : 0u;
        applyHidden(
            localIndex, handle, generation,
            candidateStates, candidateAvatars,
            candidateTelemetry);
    }

    WreckwaterThirdPersonCameraTarget candidateCameraTarget;
    WreckwaterAvatarPrimitiveProxyBatch candidateProxies;
    const WreckwaterCharacterPresentationStatus built =
        buildCameraAndPrimitiveDescriptors(
            cameraSector, candidateAvatars,
            candidateCameraTarget, candidateProxies);
    if (built
        != WreckwaterCharacterPresentationStatus::Accepted) {
        return reject(built);
    }

    uint32_t candidateVisibleCount = 0u;
    for (const auto& avatar : candidateAvatars) {
        if (avatar.visible) ++candidateVisibleCount;
    }
    incrementSaturated(candidateTelemetry.samplesAccepted);
    candidateTelemetry.visibleHighWater = std::max(
        candidateTelemetry.visibleHighWater,
        candidateVisibleCount);
    states_ = candidateStates;
    avatars_ = candidateAvatars;
    cameraTarget_ = candidateCameraTarget;
    primitiveProxies_ = candidateProxies;
    identity_ = sample.authoritative.identity;
    identityPinned_ = true;
    latestSnapshotSequence_ =
        sample.authoritative.snapshotSequence;
    visibleCount_ = candidateVisibleCount;
    telemetry_ = candidateTelemetry;
    return WreckwaterCharacterPresentationStatus::Accepted;
}

WreckwaterCharacterPresentationStatus
WreckwaterCharacterPresentation::rebaseCameraSector(
    const glm::ivec3& cameraSector) noexcept {
    if (!initialized_) {
        return WreckwaterCharacterPresentationStatus::
            NotInitialized;
    }

    auto candidateAvatars = avatars_;
    for (auto& avatar : candidateAvatars) {
        if (!avatar.visible) continue;
        glm::vec3 relative;
        if (!cameraSectorRelative(
                avatar.worldFeetPosition, cameraSector,
                config_.maximumCameraSectorDelta, relative)) {
            return WreckwaterCharacterPresentationStatus::
                PositionOverflow;
        }
        avatar.cameraSectorFeetPosition = relative;
    }

    WreckwaterThirdPersonCameraTarget candidateCameraTarget;
    WreckwaterAvatarPrimitiveProxyBatch candidateProxies;
    const WreckwaterCharacterPresentationStatus built =
        buildCameraAndPrimitiveDescriptors(
            cameraSector, candidateAvatars,
            candidateCameraTarget, candidateProxies);
    if (built
        != WreckwaterCharacterPresentationStatus::Accepted) {
        return built;
    }

    avatars_ = candidateAvatars;
    cameraTarget_ = candidateCameraTarget;
    primitiveProxies_ = candidateProxies;
    incrementSaturated(telemetry_.cameraSectorRebases);
    return WreckwaterCharacterPresentationStatus::Accepted;
}

} // namespace voxy::client
