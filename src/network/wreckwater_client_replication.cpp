#include "network/wreckwater_client_replication.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace voxy::network {
namespace {

[[nodiscard]] bool validPolicy(
    WreckwaterAuthoritativeSamplePolicy policy) noexcept {
    return policy == WreckwaterAuthoritativeSamplePolicy::OlderSnapshot
        || policy == WreckwaterAuthoritativeSamplePolicy::NewerSnapshot;
}

void incrementSaturated(uint64_t& value) noexcept {
    if (value != std::numeric_limits<uint64_t>::max()) ++value;
}

[[nodiscard]] WreckwaterClientReplicationIdentity snapshotIdentity(
    const WreckwaterCertifiedSnapshot& snapshot) noexcept {
    return {
        .sessionId = snapshot.sessionId,
        .matchId = snapshot.matchId,
        .worldId = snapshot.worldId,
        .worldEpoch = snapshot.worldEpoch,
        .authorityEpoch = snapshot.authorityEpoch,
    };
}

[[nodiscard]] WreckwaterAuthoritativeSampleState authoritativeState(
    const WreckwaterCertifiedSnapshot& snapshot) noexcept {
    return {
        .schemaVersion = snapshot.schemaVersion,
        .flags = snapshot.flags,
        .identity = snapshotIdentity(snapshot),
        .snapshotSequence = snapshot.snapshotSequence,
        .applicationTick = snapshot.applicationTick,
        .physicsEvidenceTick = snapshot.physicsEvidenceTick,
        .phase = snapshot.phase,
        .crewOneScore = snapshot.crewOneScore,
        .crewTwoScore = snapshot.crewTwoScore,
        .outcome = snapshot.outcome,
        .winner = snapshot.winner,
        .matchStateHash = snapshot.matchStateHash,
        .eventStreamHash = snapshot.eventStreamHash,
        .serializedByteHash = snapshot.serializedByteHash,
    };
}

[[nodiscard]] WreckwaterVisualPose poseOf(
    const WreckwaterEntityState& entity) noexcept {
    return {
        .sector = entity.sector,
        .localPosition = entity.localPosition,
        .orientation = entity.orientation,
        .linearVelocity = entity.linearVelocity,
        .angularVelocity = entity.angularVelocity,
    };
}

[[nodiscard]] WreckwaterVisualCharacterPose poseOf(
    const WreckwaterCharacterState& character) noexcept {
    return {
        .sector = character.sector,
        .localFeetPosition = character.localFeetPosition,
        .worldVelocity = character.worldVelocity,
    };
}

[[nodiscard]] bool sameLogicalIdentity(
    const WreckwaterEntityState& lhs,
    const WreckwaterEntityState& rhs) noexcept {
    if (lhs.kind != rhs.kind) return false;
    if (lhs.kind == WreckwaterEntityKind::Cargo) {
        return lhs.cargo.cargoId == rhs.cargo.cargoId
            && lhs.cargo.generation == rhs.cargo.generation;
    }
    return lhs.skiff.skiffId == rhs.skiff.skiffId
        && lhs.skiff.generation == rhs.skiff.generation;
}

[[nodiscard]] bool continuousEntity(
    const WreckwaterEntityState& older,
    const WreckwaterEntityState& newer) noexcept {
    return older.netEntityId == newer.netEntityId
        && older.netGeneration == newer.netGeneration
        && sameLogicalIdentity(older, newer);
}

[[nodiscard]] const WreckwaterEntityState* findByNetId(
    const WreckwaterCertifiedSnapshot& snapshot,
    NetEntityId id) noexcept {
    const auto found = std::lower_bound(
        snapshot.entities.begin(), snapshot.entities.end(), id,
        [](const WreckwaterEntityState& entity, NetEntityId candidate) {
            return entity.netEntityId < candidate;
        });
    return found != snapshot.entities.end() && found->netEntityId == id
        ? &*found
        : nullptr;
}

[[nodiscard]] const WreckwaterCharacterState* findCharacterByHandle(
    const WreckwaterCertifiedSnapshot& snapshot,
    uint32_t characterHandle) noexcept {
    const auto found = std::find_if(
        snapshot.characters.begin(), snapshot.characters.end(),
        [characterHandle](const WreckwaterCharacterState& character) {
            return character.characterHandle == characterHandle;
        });
    return found == snapshot.characters.end() ? nullptr : &*found;
}

[[nodiscard]] bool continuousCharacter(
    const WreckwaterCharacterState& older,
    const WreckwaterCharacterState& newer) noexcept {
    if (older.characterHandle != newer.characterHandle
        || older.playerId != newer.playerId
        || older.connectionGeneration != newer.connectionGeneration
        || older.stateFlags != newer.stateFlags) {
        return false;
    }
    const auto mode = static_cast<WreckwaterCharacterMode>(
        older.stateFlags & 0x3u);
    return mode != WreckwaterCharacterMode::OnSkiff
        || (older.skiffId == newer.skiffId
            && older.skiffGeneration == newer.skiffGeneration);
}

[[nodiscard]] WreckwaterCharacterMode characterMode(
    const WreckwaterCharacterState& character) noexcept {
    return static_cast<WreckwaterCharacterMode>(
        character.stateFlags & 0x3u);
}

[[nodiscard]] bool characterConnected(
    const WreckwaterCharacterState& character) noexcept {
    return (character.stateFlags
            & kWreckwaterCharacterStateConnectedFlag)
        != 0u;
}

[[nodiscard]] WreckwaterVec3 lerpVector(
    const WreckwaterVec3& a, const WreckwaterVec3& b,
    double alpha) noexcept {
    const auto lerp = [alpha](float lhs, float rhs) {
        const double value = static_cast<double>(lhs)
            + (static_cast<double>(rhs) - static_cast<double>(lhs))
                * alpha;
        return canonicalWreckwaterFloat(static_cast<float>(value));
    };
    return {
        .x = lerp(a.x, b.x),
        .y = lerp(a.y, b.y),
        .z = lerp(a.z, b.z),
    };
}

[[nodiscard]] bool canonicalPositionFromRelative(
    const std::array<int32_t, 3>& baseSector,
    const WreckwaterVec3& baseLocal,
    const std::array<double, 3>& relativeMeters,
    std::array<int32_t, 3>& sector,
    WreckwaterVec3& local) noexcept {
    std::array<float*, 3> localComponents{&local.x, &local.y, &local.z};
    const std::array<float, 3> baseComponents{
        baseLocal.x, baseLocal.y, baseLocal.z};
    for (size_t axis = 0u; axis < 3u; ++axis) {
        double value =
            static_cast<double>(baseComponents[axis])
            + relativeMeters[axis];
        if (!std::isfinite(value)) return false;

        const double shiftDouble = std::floor(
            (value - static_cast<double>(kWreckwaterSectorLocalMinimum))
            / kWreckwaterSectorSizeMeters);
        const int64_t minimumShift =
            -int64_t{kWreckwaterMaximumSectorMagnitude}
            - int64_t{baseSector[axis]};
        const int64_t maximumShift =
            int64_t{kWreckwaterMaximumSectorMagnitude}
            - int64_t{baseSector[axis]};
        if (!std::isfinite(shiftDouble)
            || shiftDouble < static_cast<double>(minimumShift)
            || shiftDouble > static_cast<double>(maximumShift)) {
            return false;
        }
        const int64_t shift = static_cast<int64_t>(shiftDouble);
        const int64_t candidateSector =
            int64_t{baseSector[axis]} + shift;
        if (candidateSector < -kWreckwaterMaximumSectorMagnitude
            || candidateSector > kWreckwaterMaximumSectorMagnitude) {
            return false;
        }
        value -= static_cast<double>(shift)
            * kWreckwaterSectorSizeMeters;
        float localValue = canonicalWreckwaterFloat(
            static_cast<float>(value));

        int64_t adjustedSector = candidateSector;
        if (localValue >= kWreckwaterSectorLocalMaximum) {
            if (adjustedSector
                == kWreckwaterMaximumSectorMagnitude) {
                return false;
            }
            ++adjustedSector;
            localValue = canonicalWreckwaterFloat(
                localValue
                - static_cast<float>(kWreckwaterSectorSizeMeters));
        } else if (localValue < kWreckwaterSectorLocalMinimum) {
            if (adjustedSector
                == -kWreckwaterMaximumSectorMagnitude) {
                return false;
            }
            --adjustedSector;
            localValue = canonicalWreckwaterFloat(
                localValue
                + static_cast<float>(kWreckwaterSectorSizeMeters));
        }
        if (!isCanonicalWreckwaterFloat(localValue)
            || localValue < kWreckwaterSectorLocalMinimum
            || localValue >= kWreckwaterSectorLocalMaximum) {
            return false;
        }
        sector[axis] = static_cast<int32_t>(adjustedSector);
        *localComponents[axis] = localValue;
    }
    return true;
}

[[nodiscard]] std::array<double, 3> relativePosition(
    const WreckwaterEntityState& origin,
    const WreckwaterEntityState& target) noexcept {
    const std::array<float, 3> originLocal{
        origin.localPosition.x,
        origin.localPosition.y,
        origin.localPosition.z,
    };
    const std::array<float, 3> targetLocal{
        target.localPosition.x,
        target.localPosition.y,
        target.localPosition.z,
    };
    std::array<double, 3> result{};
    for (size_t axis = 0u; axis < 3u; ++axis) {
        const int64_t sectorDelta =
            int64_t{target.sector[axis]} - int64_t{origin.sector[axis]};
        result[axis] =
            static_cast<double>(sectorDelta)
                * kWreckwaterSectorSizeMeters
            + static_cast<double>(targetLocal[axis])
            - static_cast<double>(originLocal[axis]);
    }
    return result;
}

[[nodiscard]] std::array<double, 3> relativePosition(
    const WreckwaterCharacterState& origin,
    const WreckwaterCharacterState& target) noexcept {
    const std::array<float, 3> originLocal{
        origin.localFeetPosition.x,
        origin.localFeetPosition.y,
        origin.localFeetPosition.z,
    };
    const std::array<float, 3> targetLocal{
        target.localFeetPosition.x,
        target.localFeetPosition.y,
        target.localFeetPosition.z,
    };
    std::array<double, 3> result{};
    for (size_t axis = 0u; axis < 3u; ++axis) {
        const int64_t sectorDelta =
            int64_t{target.sector[axis]} - int64_t{origin.sector[axis]};
        result[axis] =
            static_cast<double>(sectorDelta)
                * kWreckwaterSectorSizeMeters
            + static_cast<double>(targetLocal[axis])
            - static_cast<double>(originLocal[axis]);
    }
    return result;
}

[[nodiscard]] bool canonicalizeVisualQuaternion(
    WreckwaterQuaternion& quaternion) noexcept {
    // At an exact half turn, different libm implementations can leave a tiny
    // residual in w with either sign. Protocol canonicalization would then
    // choose opposite (but equivalent) quaternion hemispheres. Remove only
    // float-roundoff-sized residuals before applying the shared sign rule.
    constexpr float zeroTolerance =
        8.0f * std::numeric_limits<float>::epsilon();
    std::array<float*, 4> components{
        &quaternion.x,
        &quaternion.y,
        &quaternion.z,
        &quaternion.w,
    };
    for (float* component : components) {
        if (std::abs(*component) <= zeroTolerance) {
            *component = 0.0f;
        }
    }
    return canonicalizeWreckwaterQuaternion(quaternion);
}

[[nodiscard]] WreckwaterQuaternion shortestSlerp(
    const WreckwaterQuaternion& from,
    const WreckwaterQuaternion& to,
    double alpha) noexcept {
    std::array<double, 4> a{
        static_cast<double>(from.x),
        static_cast<double>(from.y),
        static_cast<double>(from.z),
        static_cast<double>(from.w),
    };
    std::array<double, 4> b{
        static_cast<double>(to.x),
        static_cast<double>(to.y),
        static_cast<double>(to.z),
        static_cast<double>(to.w),
    };
    double dot = 0.0;
    for (size_t index = 0u; index < a.size(); ++index) {
        dot += a[index] * b[index];
    }
    if (dot < 0.0) {
        for (double& component : b) component = -component;
        dot = -dot;
    }
    dot = std::clamp(dot, 0.0, 1.0);

    std::array<double, 4> value{};
    if (dot > 0.9995) {
        for (size_t index = 0u; index < value.size(); ++index) {
            value[index] = a[index] + (b[index] - a[index]) * alpha;
        }
    } else {
        const double theta = std::acos(dot);
        const double sine = std::sin(theta);
        const double fromWeight =
            std::sin((1.0 - alpha) * theta) / sine;
        const double toWeight = std::sin(alpha * theta) / sine;
        for (size_t index = 0u; index < value.size(); ++index) {
            value[index] =
                a[index] * fromWeight + b[index] * toWeight;
        }
    }
    WreckwaterQuaternion result{
        .x = static_cast<float>(value[0]),
        .y = static_cast<float>(value[1]),
        .z = static_cast<float>(value[2]),
        .w = static_cast<float>(value[3]),
    };
    if (!canonicalizeVisualQuaternion(result)) {
        return from;
    }
    return result;
}

[[nodiscard]] WreckwaterQuaternion extrapolateOrientation(
    const WreckwaterQuaternion& orientation,
    const WreckwaterVec3& angularVelocity,
    double seconds) noexcept {
    const double wx = static_cast<double>(angularVelocity.x);
    const double wy = static_cast<double>(angularVelocity.y);
    const double wz = static_cast<double>(angularVelocity.z);
    const double speed = std::sqrt(wx * wx + wy * wy + wz * wz);
    if (!(speed > 0.0) || seconds == 0.0) return orientation;

    const double halfAngle = speed * seconds * 0.5;
    const double scale = std::sin(halfAngle) / speed;
    const std::array<double, 4> delta{
        wx * scale, wy * scale, wz * scale, std::cos(halfAngle)};
    const std::array<double, 4> source{
        static_cast<double>(orientation.x),
        static_cast<double>(orientation.y),
        static_cast<double>(orientation.z),
        static_cast<double>(orientation.w),
    };

    // Angular velocity is interpreted in world space for visual extrapolation,
    // so the incremental quaternion left-multiplies the certified orientation.
    WreckwaterQuaternion result{
        .x = static_cast<float>(
            delta[3] * source[0] + delta[0] * source[3]
            + delta[1] * source[2] - delta[2] * source[1]),
        .y = static_cast<float>(
            delta[3] * source[1] - delta[0] * source[2]
            + delta[1] * source[3] + delta[2] * source[0]),
        .z = static_cast<float>(
            delta[3] * source[2] + delta[0] * source[1]
            - delta[1] * source[0] + delta[2] * source[3]),
        .w = static_cast<float>(
            delta[3] * source[3] - delta[0] * source[0]
            - delta[1] * source[1] - delta[2] * source[2]),
    };
    if (!canonicalizeVisualQuaternion(result)) {
        return orientation;
    }
    return result;
}

[[nodiscard]] bool interpolatePose(
    const WreckwaterEntityState& older,
    const WreckwaterEntityState& newer,
    double alpha,
    double intervalTicks,
    WreckwaterVisualPose& pose) noexcept {
    const std::array<double, 3> endpoint =
        relativePosition(older, newer);
    const double seconds =
        intervalTicks / static_cast<double>(kWreckwaterPhysicsTickRateHz);
    const double alphaSquared = alpha * alpha;
    const double alphaCubed = alphaSquared * alpha;
    const double h10 =
        alphaCubed - 2.0 * alphaSquared + alpha;
    const double h01 =
        -2.0 * alphaCubed + 3.0 * alphaSquared;
    const double h11 = alphaCubed - alphaSquared;
    const std::array<float, 3> olderVelocity{
        older.linearVelocity.x,
        older.linearVelocity.y,
        older.linearVelocity.z,
    };
    const std::array<float, 3> newerVelocity{
        newer.linearVelocity.x,
        newer.linearVelocity.y,
        newer.linearVelocity.z,
    };
    std::array<double, 3> relative{};
    for (size_t axis = 0u; axis < 3u; ++axis) {
        relative[axis] =
            h10 * static_cast<double>(olderVelocity[axis]) * seconds
            + h01 * endpoint[axis]
            + h11 * static_cast<double>(newerVelocity[axis]) * seconds;
    }
    if (!canonicalPositionFromRelative(
            older.sector, older.localPosition, relative,
            pose.sector, pose.localPosition)) {
        return false;
    }
    pose.orientation =
        shortestSlerp(older.orientation, newer.orientation, alpha);
    pose.linearVelocity =
        lerpVector(older.linearVelocity, newer.linearVelocity, alpha);
    pose.angularVelocity =
        lerpVector(older.angularVelocity, newer.angularVelocity, alpha);
    return true;
}

[[nodiscard]] bool extrapolatePose(
    const WreckwaterEntityState& source,
    double extrapolationTicks,
    WreckwaterVisualPose& pose) noexcept {
    const double seconds =
        extrapolationTicks
        / static_cast<double>(kWreckwaterPhysicsTickRateHz);
    const std::array<double, 3> relative{
        static_cast<double>(source.linearVelocity.x) * seconds,
        static_cast<double>(source.linearVelocity.y) * seconds,
        static_cast<double>(source.linearVelocity.z) * seconds,
    };
    if (!canonicalPositionFromRelative(
            source.sector, source.localPosition, relative,
            pose.sector, pose.localPosition)) {
        return false;
    }
    pose.orientation = extrapolateOrientation(
        source.orientation, source.angularVelocity, seconds);
    pose.linearVelocity = source.linearVelocity;
    pose.angularVelocity = source.angularVelocity;
    return true;
}

[[nodiscard]] const WreckwaterSampledEntity* findSampledSkiff(
    const WreckwaterClientSample& sample,
    uint32_t skiffId, uint32_t skiffGeneration) noexcept {
    for (uint32_t index = 0u; index < sample.entityCount; ++index) {
        const WreckwaterSampledEntity& entity = sample.entities[index];
        if (entity.authoritativeState.kind
                == WreckwaterEntityKind::Skiff
            && entity.authoritativeState.skiff.skiffId == skiffId
            && entity.authoritativeState.skiff.generation
                == skiffGeneration) {
            return &entity;
        }
    }
    return nullptr;
}

[[nodiscard]] WreckwaterVec3 rotateVector(
    const WreckwaterQuaternion& quaternion,
    const WreckwaterVec3& vector) noexcept {
    const double qx = static_cast<double>(quaternion.x);
    const double qy = static_cast<double>(quaternion.y);
    const double qz = static_cast<double>(quaternion.z);
    const double qw = static_cast<double>(quaternion.w);
    const double vx = static_cast<double>(vector.x);
    const double vy = static_cast<double>(vector.y);
    const double vz = static_cast<double>(vector.z);
    const double tx = 2.0 * (qy * vz - qz * vy);
    const double ty = 2.0 * (qz * vx - qx * vz);
    const double tz = 2.0 * (qx * vy - qy * vx);
    return {
        canonicalWreckwaterFloat(static_cast<float>(
            vx + qw * tx + (qy * tz - qz * ty))),
        canonicalWreckwaterFloat(static_cast<float>(
            vy + qw * ty + (qz * tx - qx * tz))),
        canonicalWreckwaterFloat(static_cast<float>(
            vz + qw * tz + (qx * ty - qy * tx))),
    };
}

[[nodiscard]] WreckwaterVec3 crossVector(
    const WreckwaterVec3& lhs,
    const WreckwaterVec3& rhs) noexcept {
    return {
        canonicalWreckwaterFloat(
            lhs.y * rhs.z - lhs.z * rhs.y),
        canonicalWreckwaterFloat(
            lhs.z * rhs.x - lhs.x * rhs.z),
        canonicalWreckwaterFloat(
            lhs.x * rhs.y - lhs.y * rhs.x),
    };
}

[[nodiscard]] bool composeAttachedCharacterPose(
    const WreckwaterSampledEntity& sampledSkiff,
    const WreckwaterVec3& localFeet,
    const WreckwaterVec3& localVelocity,
    WreckwaterVisualCharacterPose& pose) noexcept {
    const WreckwaterVec3 worldLever = rotateVector(
        sampledSkiff.visualPose.orientation, localFeet);
    const std::array<double, 3> relative{
        static_cast<double>(worldLever.x),
        static_cast<double>(worldLever.y),
        static_cast<double>(worldLever.z),
    };
    if (!canonicalPositionFromRelative(
            sampledSkiff.visualPose.sector,
            sampledSkiff.visualPose.localPosition,
            relative, pose.sector, pose.localFeetPosition)) {
        return false;
    }
    const WreckwaterVec3 walkingVelocity = rotateVector(
        sampledSkiff.visualPose.orientation, localVelocity);
    const WreckwaterVec3 tangentialVelocity = crossVector(
        sampledSkiff.visualPose.angularVelocity, worldLever);
    pose.worldVelocity = {
        canonicalWreckwaterFloat(
            sampledSkiff.visualPose.linearVelocity.x
            + tangentialVelocity.x + walkingVelocity.x),
        canonicalWreckwaterFloat(
            sampledSkiff.visualPose.linearVelocity.y
            + tangentialVelocity.y + walkingVelocity.y),
        canonicalWreckwaterFloat(
            sampledSkiff.visualPose.linearVelocity.z
            + tangentialVelocity.z + walkingVelocity.z),
    };
    return std::isfinite(pose.worldVelocity.x)
        && std::isfinite(pose.worldVelocity.y)
        && std::isfinite(pose.worldVelocity.z);
}

[[nodiscard]] WreckwaterVec3 hermiteLocalVector(
    const WreckwaterVec3& olderPosition,
    const WreckwaterVec3& newerPosition,
    const WreckwaterVec3& olderVelocity,
    const WreckwaterVec3& newerVelocity,
    double alpha, double seconds) noexcept {
    const double alphaSquared = alpha * alpha;
    const double alphaCubed = alphaSquared * alpha;
    const double h00 =
        2.0 * alphaCubed - 3.0 * alphaSquared + 1.0;
    const double h10 =
        alphaCubed - 2.0 * alphaSquared + alpha;
    const double h01 =
        -2.0 * alphaCubed + 3.0 * alphaSquared;
    const double h11 = alphaCubed - alphaSquared;
    const auto component = [
        h00, h10, h01, h11, seconds
    ](float oldPosition, float newPosition,
      float oldVelocity, float newVelocity) {
        return canonicalWreckwaterFloat(static_cast<float>(
            h00 * static_cast<double>(oldPosition)
            + h10 * static_cast<double>(oldVelocity) * seconds
            + h01 * static_cast<double>(newPosition)
            + h11 * static_cast<double>(newVelocity) * seconds));
    };
    return {
        component(
            olderPosition.x, newerPosition.x,
            olderVelocity.x, newerVelocity.x),
        component(
            olderPosition.y, newerPosition.y,
            olderVelocity.y, newerVelocity.y),
        component(
            olderPosition.z, newerPosition.z,
            olderVelocity.z, newerVelocity.z),
    };
}

[[nodiscard]] bool interpolateAttachedCharacterPose(
    const WreckwaterCharacterState& older,
    const WreckwaterCharacterState& newer,
    const WreckwaterSampledEntity& sampledSkiff,
    double alpha, double intervalTicks,
    WreckwaterVisualCharacterPose& pose) noexcept {
    const double seconds =
        intervalTicks
        / static_cast<double>(kWreckwaterPhysicsTickRateHz);
    const WreckwaterVec3 localFeet = hermiteLocalVector(
        older.skiffLocalFeetPosition,
        newer.skiffLocalFeetPosition,
        older.skiffLocalVelocity,
        newer.skiffLocalVelocity,
        alpha, seconds);
    const WreckwaterVec3 localVelocity = lerpVector(
        older.skiffLocalVelocity,
        newer.skiffLocalVelocity, alpha);
    return composeAttachedCharacterPose(
        sampledSkiff, localFeet, localVelocity, pose);
}

[[nodiscard]] bool extrapolateAttachedCharacterPose(
    const WreckwaterCharacterState& source,
    const WreckwaterSampledEntity& sampledSkiff,
    double extrapolationTicks,
    WreckwaterVisualCharacterPose& pose) noexcept {
    const double seconds =
        extrapolationTicks
        / static_cast<double>(kWreckwaterPhysicsTickRateHz);
    const WreckwaterVec3 localFeet{
        canonicalWreckwaterFloat(static_cast<float>(
            static_cast<double>(source.skiffLocalFeetPosition.x)
            + static_cast<double>(source.skiffLocalVelocity.x)
                * seconds)),
        source.skiffLocalFeetPosition.y,
        canonicalWreckwaterFloat(static_cast<float>(
            static_cast<double>(source.skiffLocalFeetPosition.z)
            + static_cast<double>(source.skiffLocalVelocity.z)
                * seconds)),
    };
    return composeAttachedCharacterPose(
        sampledSkiff, localFeet,
        source.skiffLocalVelocity, pose);
}

[[nodiscard]] bool interpolateCharacterPose(
    const WreckwaterCharacterState& older,
    const WreckwaterCharacterState& newer,
    double alpha,
    double intervalTicks,
    WreckwaterVisualCharacterPose& pose) noexcept {
    const std::array<double, 3> endpoint =
        relativePosition(older, newer);
    const double seconds =
        intervalTicks / static_cast<double>(kWreckwaterPhysicsTickRateHz);
    const double alphaSquared = alpha * alpha;
    const double alphaCubed = alphaSquared * alpha;
    const double h10 =
        alphaCubed - 2.0 * alphaSquared + alpha;
    const double h01 =
        -2.0 * alphaCubed + 3.0 * alphaSquared;
    const double h11 = alphaCubed - alphaSquared;
    const std::array<float, 3> olderVelocity{
        older.worldVelocity.x,
        older.worldVelocity.y,
        older.worldVelocity.z,
    };
    const std::array<float, 3> newerVelocity{
        newer.worldVelocity.x,
        newer.worldVelocity.y,
        newer.worldVelocity.z,
    };
    std::array<double, 3> relative{};
    for (size_t axis = 0u; axis < 3u; ++axis) {
        relative[axis] =
            h10 * static_cast<double>(olderVelocity[axis]) * seconds
            + h01 * endpoint[axis]
            + h11 * static_cast<double>(newerVelocity[axis]) * seconds;
    }
    if (!canonicalPositionFromRelative(
            older.sector, older.localFeetPosition, relative,
            pose.sector, pose.localFeetPosition)) {
        return false;
    }
    pose.worldVelocity =
        lerpVector(older.worldVelocity, newer.worldVelocity, alpha);
    return true;
}

[[nodiscard]] bool extrapolateCharacterPose(
    const WreckwaterCharacterState& source,
    double extrapolationTicks,
    WreckwaterVisualCharacterPose& pose) noexcept {
    const double seconds =
        extrapolationTicks
        / static_cast<double>(kWreckwaterPhysicsTickRateHz);
    const std::array<double, 3> relative{
        static_cast<double>(source.worldVelocity.x) * seconds,
        static_cast<double>(source.worldVelocity.y) * seconds,
        static_cast<double>(source.worldVelocity.z) * seconds,
    };
    if (!canonicalPositionFromRelative(
            source.sector, source.localFeetPosition, relative,
            pose.sector, pose.localFeetPosition)) {
        return false;
    }
    pose.worldVelocity = source.worldVelocity;
    return true;
}

void copySnapshotScalars(
    WreckwaterCertifiedSnapshot& destination,
    const WreckwaterCertifiedSnapshot& source) noexcept {
    destination.schemaVersion = source.schemaVersion;
    destination.flags = source.flags;
    destination.sessionId = source.sessionId;
    destination.matchId = source.matchId;
    destination.worldId = source.worldId;
    destination.worldEpoch = source.worldEpoch;
    destination.authorityEpoch = source.authorityEpoch;
    destination.snapshotSequence = source.snapshotSequence;
    destination.applicationTick = source.applicationTick;
    destination.physicsEvidenceTick = source.physicsEvidenceTick;
    destination.phase = source.phase;
    destination.crewOneScore = source.crewOneScore;
    destination.crewTwoScore = source.crewTwoScore;
    destination.outcome = source.outcome;
    destination.winner = source.winner;
    destination.matchStateHash = source.matchStateHash;
    destination.eventStreamHash = source.eventStreamHash;
    destination.reserved = source.reserved;
    destination.serializedByteHash = source.serializedByteHash;
}

} // namespace

const char* wreckwaterClientReplicationErrorName(
    WreckwaterClientReplicationError error) noexcept {
    switch (error) {
        case WreckwaterClientReplicationError::None: return "none";
        case WreckwaterClientReplicationError::InvalidConfiguration:
            return "invalid configuration";
        case WreckwaterClientReplicationError::EmptyHistory:
            return "empty history";
        case WreckwaterClientReplicationError::MalformedSnapshot:
            return "malformed snapshot";
        case WreckwaterClientReplicationError::EntityCapacityExceeded:
            return "entity capacity exceeded";
        case WreckwaterClientReplicationError::IdentityMismatch:
            return "identity mismatch";
        case WreckwaterClientReplicationError::NonMonotonicSequence:
            return "nonmonotonic sequence";
        case WreckwaterClientReplicationError::RegressingApplicationTick:
            return "regressing application tick";
        case WreckwaterClientReplicationError::RegressingEvidenceTick:
            return "regressing evidence tick";
        case WreckwaterClientReplicationError::InvalidEntityGeneration:
            return "invalid entity generation";
        case WreckwaterClientReplicationError::InvalidCharacterGeneration:
            return "invalid character generation";
        case WreckwaterClientReplicationError::
                IdentityRegistryCapacityExceeded:
            return "identity registry capacity exceeded";
        case WreckwaterClientReplicationError::InvalidRenderTick:
            return "invalid render tick";
        case WreckwaterClientReplicationError::PositionOverflow:
            return "position overflow";
    }
    return "unknown";
}

WreckwaterClientSnapshotBuffer::WreckwaterClientSnapshotBuffer()
    : WreckwaterClientSnapshotBuffer(Config{}) {}

WreckwaterClientSnapshotBuffer::WreckwaterClientSnapshotBuffer(
    Config config)
    : config_(config) {
    if (config_.historySnapshots < 2u
        || config_.historySnapshots
            > kWreckwaterClientMaximumHistorySnapshots
        || config_.maximumEntities == 0u
        || config_.maximumEntities > kWreckwaterMaximumSnapshotEntities
        || config_.maximumExtrapolationTicks
            > kWreckwaterClientMaximumExtrapolationTicks
        || config_.maximumCharacterInterpolationGapTicks == 0u
        || config_.maximumCharacterInterpolationGapTicks
            > kWreckwaterClientMaximumCharacterInterpolationGapTicks
        || !validPolicy(config_.authoritativePolicy)) {
        return;
    }
    slots_.resize(config_.historySnapshots);
    for (Slot& slot : slots_) {
        slot.snapshot.entities.reserve(config_.maximumEntities);
        slot.snapshot.characters.reserve(
            kWreckwaterMaximumSnapshotCharacters);
    }
    initialized_ = true;
}

void WreckwaterClientSnapshotBuffer::reset() noexcept {
    oldest_ = 0u;
    count_ = 0u;
    identityPinned_ = false;
    identity_ = {};
    latestSequence_ = 0u;
    latestApplicationTick_ = 0u;
    latestEvidenceTick_ = 0u;
    networkLifetimeCount_ = 0u;
    logicalLifetimeCount_ = 0u;
    attachmentLifetimeCount_ = 0u;
    characterLifetimeCount_ = 0u;
    networkLifetimes_ = {};
    logicalLifetimes_ = {};
    attachmentLifetimes_ = {};
    characterLifetimes_ = {};
    telemetry_ = {};
    for (Slot& slot : slots_) {
        slot.snapshot.entities.clear();
        slot.snapshot.characters.clear();
    }
}

const WreckwaterCertifiedSnapshot& WreckwaterClientSnapshotBuffer::at(
    uint32_t chronologicalIndex) const noexcept {
    const uint32_t slot =
        (oldest_ + chronologicalIndex) % config_.historySnapshots;
    return slots_[slot].snapshot;
}

WreckwaterCertifiedSnapshot&
WreckwaterClientSnapshotBuffer::slotForAppend(bool& evicted) noexcept {
    evicted = count_ == config_.historySnapshots;
    if (evicted) {
        const uint32_t slot = oldest_;
        oldest_ = (oldest_ + 1u) % config_.historySnapshots;
        return slots_[slot].snapshot;
    }
    const uint32_t slot =
        (oldest_ + count_) % config_.historySnapshots;
    ++count_;
    return slots_[slot].snapshot;
}

void WreckwaterClientSnapshotBuffer::copyInto(
    WreckwaterCertifiedSnapshot& destination,
    const WreckwaterCertifiedSnapshot& source) {
    copySnapshotScalars(destination, source);
    destination.entities.assign(
        source.entities.begin(), source.entities.end());
    destination.characters.assign(
        source.characters.begin(), source.characters.end());
}

WreckwaterClientReplicationError
WreckwaterClientSnapshotBuffer::validateGenerations(
    const WreckwaterCertifiedSnapshot& snapshot) const noexcept {
    uint32_t newNetworkIds = 0u;
    uint32_t newLogicalIds = 0u;
    uint32_t newAttachmentIds = 0u;
    uint32_t newCharacterIds = 0u;
    for (const WreckwaterEntityState& incoming : snapshot.entities) {
        const uint32_t logicalId =
            incoming.kind == WreckwaterEntityKind::Cargo
            ? incoming.cargo.cargoId
            : incoming.skiff.skiffId;
        const uint32_t logicalGeneration =
            incoming.kind == WreckwaterEntityKind::Cargo
            ? incoming.cargo.generation
            : incoming.skiff.generation;

        const auto network = std::find_if(
            networkLifetimes_.begin(),
            networkLifetimes_.begin()
                + static_cast<std::ptrdiff_t>(networkLifetimeCount_),
            [&incoming](const NetworkLifetime& lifetime) {
                return lifetime.id == incoming.netEntityId;
            });
        if (network
            == networkLifetimes_.begin()
                + static_cast<std::ptrdiff_t>(networkLifetimeCount_)) {
            ++newNetworkIds;
        } else if (
            incoming.netGeneration < network->generation
            || (incoming.netGeneration == network->generation
                && (incoming.kind != network->kind
                    || logicalId != network->logicalId
                    || logicalGeneration
                        != network->logicalGeneration))) {
            return WreckwaterClientReplicationError::
                InvalidEntityGeneration;
        }

        const auto logical = std::find_if(
            logicalLifetimes_.begin(),
            logicalLifetimes_.begin()
                + static_cast<std::ptrdiff_t>(logicalLifetimeCount_),
            [&incoming, logicalId](const LogicalLifetime& lifetime) {
                return lifetime.kind == incoming.kind
                    && lifetime.id == logicalId;
            });
        if (logical
            == logicalLifetimes_.begin()
                + static_cast<std::ptrdiff_t>(logicalLifetimeCount_)) {
            ++newLogicalIds;
        } else if (logicalGeneration < logical->generation) {
            return WreckwaterClientReplicationError::
                InvalidEntityGeneration;
        }

        if (incoming.attachment.attachmentId != 0u) {
            const auto attachment = std::find_if(
                attachmentLifetimes_.begin(),
                attachmentLifetimes_.begin()
                    + static_cast<std::ptrdiff_t>(
                        attachmentLifetimeCount_),
                [&incoming](const AttachmentLifetime& lifetime) {
                    return lifetime.id
                        == incoming.attachment.attachmentId;
                });
            if (attachment
                == attachmentLifetimes_.begin()
                    + static_cast<std::ptrdiff_t>(
                        attachmentLifetimeCount_)) {
                ++newAttachmentIds;
            } else if (
                incoming.attachment.generation
                    < attachment->generation
                || (incoming.attachment.generation
                        == attachment->generation
                    && (incoming.netEntityId
                            != attachment->ownerNetEntityId
                        || incoming.netGeneration
                            != attachment->ownerNetGeneration
                        || incoming.cargo.cargoId
                            != attachment->cargoId
                        || incoming.cargo.generation
                            != attachment->cargoGeneration))) {
                return WreckwaterClientReplicationError::
                    InvalidEntityGeneration;
            }
        }
    }

    const auto characterEnd =
        characterLifetimes_.begin()
        + static_cast<std::ptrdiff_t>(characterLifetimeCount_);
    for (const WreckwaterCharacterState& incoming :
         snapshot.characters) {
        const auto byHandle = std::find_if(
            characterLifetimes_.begin(), characterEnd,
            [&incoming](const CharacterLifetime& lifetime) {
                return lifetime.handle == incoming.characterHandle;
            });
        const auto byPlayer = std::find_if(
            characterLifetimes_.begin(), characterEnd,
            [&incoming](const CharacterLifetime& lifetime) {
                return lifetime.playerId == incoming.playerId;
            });
        const bool handleKnown = byHandle != characterEnd;
        const bool playerKnown = byPlayer != characterEnd;
        if (handleKnown != playerKnown
            || (handleKnown && byHandle != byPlayer)) {
            return WreckwaterClientReplicationError::
                InvalidCharacterGeneration;
        }
        if (!handleKnown) {
            ++newCharacterIds;
            continue;
        }
        if (incoming.connectionGeneration
            < byHandle->connectionGeneration) {
            return WreckwaterClientReplicationError::
                InvalidCharacterGeneration;
        }
        if (incoming.connectionGeneration
            == byHandle->connectionGeneration) {
            if (incoming.lastAppliedCharacterInputSequence
                    < byHandle
                          ->lastAppliedCharacterInputSequence
                || (!byHandle->connected
                    && characterConnected(incoming))) {
                return WreckwaterClientReplicationError::
                    InvalidCharacterGeneration;
            }
        }
    }

    if (newNetworkIds
            > kWreckwaterMaximumSnapshotEntities - networkLifetimeCount_
        || newLogicalIds
            > kWreckwaterMaximumSnapshotEntities - logicalLifetimeCount_
        || newAttachmentIds
            > kWreckwaterMaximumSnapshotEntities
                - attachmentLifetimeCount_
        || newCharacterIds
            > kWreckwaterMaximumSnapshotCharacters
                - characterLifetimeCount_) {
        return WreckwaterClientReplicationError::
            IdentityRegistryCapacityExceeded;
    }
    return WreckwaterClientReplicationError::None;
}

void WreckwaterClientSnapshotBuffer::updateGenerationRegistries(
    const WreckwaterCertifiedSnapshot& snapshot) noexcept {
    for (const WreckwaterEntityState& incoming : snapshot.entities) {
        const uint32_t logicalId =
            incoming.kind == WreckwaterEntityKind::Cargo
            ? incoming.cargo.cargoId
            : incoming.skiff.skiffId;
        const uint32_t logicalGeneration =
            incoming.kind == WreckwaterEntityKind::Cargo
            ? incoming.cargo.generation
            : incoming.skiff.generation;

        auto network = std::find_if(
            networkLifetimes_.begin(),
            networkLifetimes_.begin()
                + static_cast<std::ptrdiff_t>(networkLifetimeCount_),
            [&incoming](const NetworkLifetime& lifetime) {
                return lifetime.id == incoming.netEntityId;
            });
        if (network
            == networkLifetimes_.begin()
                + static_cast<std::ptrdiff_t>(networkLifetimeCount_)) {
            network = networkLifetimes_.begin()
                + static_cast<std::ptrdiff_t>(networkLifetimeCount_);
            ++networkLifetimeCount_;
        }
        if (incoming.netGeneration >= network->generation) {
            *network = {
                .id = incoming.netEntityId,
                .generation = incoming.netGeneration,
                .kind = incoming.kind,
                .logicalId = logicalId,
                .logicalGeneration = logicalGeneration,
            };
        }

        auto logical = std::find_if(
            logicalLifetimes_.begin(),
            logicalLifetimes_.begin()
                + static_cast<std::ptrdiff_t>(logicalLifetimeCount_),
            [&incoming, logicalId](const LogicalLifetime& lifetime) {
                return lifetime.kind == incoming.kind
                    && lifetime.id == logicalId;
            });
        if (logical
            == logicalLifetimes_.begin()
                + static_cast<std::ptrdiff_t>(logicalLifetimeCount_)) {
            logical = logicalLifetimes_.begin()
                + static_cast<std::ptrdiff_t>(logicalLifetimeCount_);
            ++logicalLifetimeCount_;
        }
        logical->kind = incoming.kind;
        logical->id = logicalId;
        logical->generation =
            std::max(logical->generation, logicalGeneration);

        if (incoming.attachment.attachmentId == 0u) continue;
        auto attachment = std::find_if(
            attachmentLifetimes_.begin(),
            attachmentLifetimes_.begin()
                + static_cast<std::ptrdiff_t>(attachmentLifetimeCount_),
            [&incoming](const AttachmentLifetime& lifetime) {
                return lifetime.id
                    == incoming.attachment.attachmentId;
            });
        if (attachment
            == attachmentLifetimes_.begin()
                + static_cast<std::ptrdiff_t>(
                    attachmentLifetimeCount_)) {
            attachment = attachmentLifetimes_.begin()
                + static_cast<std::ptrdiff_t>(
                    attachmentLifetimeCount_);
            ++attachmentLifetimeCount_;
        }
        if (incoming.attachment.generation >= attachment->generation) {
            *attachment = {
                .id = incoming.attachment.attachmentId,
                .generation = incoming.attachment.generation,
                .ownerNetEntityId = incoming.netEntityId,
                .ownerNetGeneration = incoming.netGeneration,
                .cargoId = incoming.cargo.cargoId,
                .cargoGeneration = incoming.cargo.generation,
            };
        }
    }
    for (const WreckwaterCharacterState& incoming :
         snapshot.characters) {
        auto lifetime = std::find_if(
            characterLifetimes_.begin(),
            characterLifetimes_.begin()
                + static_cast<std::ptrdiff_t>(
                    characterLifetimeCount_),
            [&incoming](const CharacterLifetime& candidate) {
                return candidate.handle
                    == incoming.characterHandle;
            });
        if (lifetime
            == characterLifetimes_.begin()
                + static_cast<std::ptrdiff_t>(
                    characterLifetimeCount_)) {
            lifetime = characterLifetimes_.begin()
                + static_cast<std::ptrdiff_t>(
                    characterLifetimeCount_);
            ++characterLifetimeCount_;
        }
        *lifetime = {
            .handle = incoming.characterHandle,
            .playerId = incoming.playerId,
            .connectionGeneration =
                incoming.connectionGeneration,
            .lastAppliedCharacterInputSequence =
                incoming.lastAppliedCharacterInputSequence,
            .connected = characterConnected(incoming),
        };
    }
}

WreckwaterClientIngestResult WreckwaterClientSnapshotBuffer::ingest(
    const WreckwaterCertifiedSnapshot& snapshot) {
    WreckwaterClientIngestResult result;
    const auto reject =
        [this, &result](WreckwaterClientReplicationError error) {
            result.error = error;
            incrementSaturated(telemetry_.rejectedSnapshots);
        };

    if (!initialized_) {
        reject(WreckwaterClientReplicationError::InvalidConfiguration);
        return result;
    }
    if (!isCanonicalWreckwaterSnapshot(snapshot)) {
        reject(WreckwaterClientReplicationError::MalformedSnapshot);
        return result;
    }
    if (snapshot.entities.size() > config_.maximumEntities) {
        reject(WreckwaterClientReplicationError::EntityCapacityExceeded);
        return result;
    }

    const WreckwaterClientReplicationIdentity incomingIdentity =
        snapshotIdentity(snapshot);
    if (identityPinned_ && incomingIdentity != identity_) {
        reject(WreckwaterClientReplicationError::IdentityMismatch);
        return result;
    }
    if (identityPinned_ && snapshot.snapshotSequence <= latestSequence_) {
        reject(WreckwaterClientReplicationError::NonMonotonicSequence);
        return result;
    }
    if (identityPinned_
        && snapshot.applicationTick < latestApplicationTick_) {
        reject(
            WreckwaterClientReplicationError::RegressingApplicationTick);
        return result;
    }
    if (identityPinned_
        && snapshot.physicsEvidenceTick < latestEvidenceTick_) {
        reject(WreckwaterClientReplicationError::RegressingEvidenceTick);
        return result;
    }
    const WreckwaterClientReplicationError generationError =
        validateGenerations(snapshot);
    if (generationError != WreckwaterClientReplicationError::None) {
        reject(generationError);
        return result;
    }

    if (identityPinned_
        && snapshot.physicsEvidenceTick == latestEvidenceTick_) {
        WreckwaterCertifiedSnapshot& latest =
            slots_[(oldest_ + count_ - 1u)
                   % config_.historySnapshots]
                .snapshot;
        copyInto(latest, snapshot);
        result.replacedSameEvidenceTick = true;
        incrementSaturated(
            telemetry_.replacedSameEvidenceTickSnapshots);
    } else {
        WreckwaterCertifiedSnapshot& destination =
            slotForAppend(result.evictedOldest);
        copyInto(destination, snapshot);
        if (result.evictedOldest) {
            incrementSaturated(telemetry_.evictedSnapshots);
        }
        telemetry_.historyHighWater =
            std::max(telemetry_.historyHighWater, count_);
    }

    updateGenerationRegistries(snapshot);
    identityPinned_ = true;
    identity_ = incomingIdentity;
    latestSequence_ = snapshot.snapshotSequence;
    latestApplicationTick_ = snapshot.applicationTick;
    latestEvidenceTick_ = snapshot.physicsEvidenceTick;
    incrementSaturated(telemetry_.acceptedSnapshots);
    return result;
}

WreckwaterClientSampleResult WreckwaterClientSnapshotBuffer::sample(
    WreckwaterPhysicsRenderTick physicsRenderTick) const noexcept {
    WreckwaterClientSampleResult result;
    result.sample.requestedPhysicsTick = physicsRenderTick;
    if (!initialized_) {
        result.error =
            WreckwaterClientReplicationError::InvalidConfiguration;
        return result;
    }
    if (count_ == 0u) {
        result.error = WreckwaterClientReplicationError::EmptyHistory;
        return result;
    }
    if (!isCanonicalWreckwaterFloat(physicsRenderTick.fraction)
        || physicsRenderTick.fraction < 0.0f
        || physicsRenderTick.fraction >= 1.0f) {
        result.error =
            WreckwaterClientReplicationError::InvalidRenderTick;
        return result;
    }

    const WreckwaterCertifiedSnapshot& oldest = at(0u);
    const WreckwaterCertifiedSnapshot& latest = at(count_ - 1u);
    const WreckwaterCertifiedSnapshot* selected = nullptr;
    const WreckwaterCertifiedSnapshot* older = nullptr;
    const WreckwaterCertifiedSnapshot* newer = nullptr;
    double alpha = 0.0;
    double extrapolationTicks = 0.0;
    WreckwaterVisualMotionMode globalMotion =
        WreckwaterVisualMotionMode::Snapped;

    if (physicsRenderTick.whole < oldest.physicsEvidenceTick) {
        selected = &oldest;
        result.sample.evaluatedPhysicsTick = {
            .whole = oldest.physicsEvidenceTick,
            .fraction = 0.0f,
        };
        result.sample.clampedToOldest = true;
    } else {
        for (uint32_t index = 0u; index < count_; ++index) {
            const WreckwaterCertifiedSnapshot& candidate = at(index);
            if (physicsRenderTick.whole
                    == candidate.physicsEvidenceTick
                && physicsRenderTick.fraction == 0.0f) {
                selected = &candidate;
                result.sample.evaluatedPhysicsTick =
                    physicsRenderTick;
                break;
            }
            if (physicsRenderTick.whole
                < candidate.physicsEvidenceTick) {
                newer = &candidate;
                older = &at(index - 1u);
                break;
            }
        }
    }

    if (selected == nullptr && newer != nullptr) {
        const uint64_t interval =
            newer->physicsEvidenceTick
            - older->physicsEvidenceTick;
        const uint64_t elapsedWhole =
            physicsRenderTick.whole
            - older->physicsEvidenceTick;
        alpha =
            (static_cast<double>(elapsedWhole)
             + static_cast<double>(physicsRenderTick.fraction))
            / static_cast<double>(interval);
        if (!std::isfinite(alpha) || alpha < 0.0 || alpha > 1.0) {
            result.error =
                WreckwaterClientReplicationError::InvalidRenderTick;
            return result;
        }
        selected =
            config_.authoritativePolicy
                == WreckwaterAuthoritativeSamplePolicy::OlderSnapshot
            ? older
            : newer;
        result.sample.evaluatedPhysicsTick = physicsRenderTick;
        globalMotion = WreckwaterVisualMotionMode::Interpolated;
    } else if (selected == nullptr) {
        selected = &latest;
        if (physicsRenderTick.whole
            < latest.physicsEvidenceTick) {
            result.error =
                WreckwaterClientReplicationError::InvalidRenderTick;
            return result;
        }

        const uint64_t maximumWhole =
            latest.physicsEvidenceTick
            + uint64_t{config_.maximumExtrapolationTicks};
        const bool beyondMaximum =
            physicsRenderTick.whole > maximumWhole
            || (physicsRenderTick.whole == maximumWhole
                && physicsRenderTick.fraction > 0.0f);
        if (beyondMaximum) {
            extrapolationTicks =
                static_cast<double>(
                    config_.maximumExtrapolationTicks);
            result.sample.evaluatedPhysicsTick = {
                .whole = maximumWhole,
                .fraction = 0.0f,
            };
            result.sample.stale = true;
            globalMotion = WreckwaterVisualMotionMode::FrozenStale;
            incrementSaturated(telemetry_.staleSamples);
        } else {
            const uint64_t wholeDelta =
                physicsRenderTick.whole
                - latest.physicsEvidenceTick;
            extrapolationTicks =
                static_cast<double>(wholeDelta)
                + static_cast<double>(physicsRenderTick.fraction);
            result.sample.evaluatedPhysicsTick =
                physicsRenderTick;
            if (extrapolationTicks > 0.0) {
                globalMotion =
                    WreckwaterVisualMotionMode::Extrapolated;
            }
        }
    }

    result.sample.authoritative = authoritativeState(*selected);
    result.sample.entityCount =
        static_cast<uint32_t>(selected->entities.size());
    result.sample.characterCount =
        static_cast<uint32_t>(selected->characters.size());
    for (uint32_t index = 0u;
         index < result.sample.entityCount; ++index) {
        const WreckwaterEntityState& authoritative =
            selected->entities[index];
        WreckwaterSampledEntity& output = result.sample.entities[index];
        output.netEntityId = authoritative.netEntityId;
        output.netGeneration = authoritative.netGeneration;
        output.authoritativeState = authoritative;
        output.visualPose = poseOf(authoritative);
        output.motionMode = WreckwaterVisualMotionMode::Snapped;

        if (globalMotion == WreckwaterVisualMotionMode::Interpolated) {
            const WreckwaterEntityState* olderEntity =
                findByNetId(*older, authoritative.netEntityId);
            const WreckwaterEntityState* newerEntity =
                findByNetId(*newer, authoritative.netEntityId);
            if (olderEntity != nullptr && newerEntity != nullptr
                && continuousEntity(*olderEntity, *newerEntity)) {
                const double intervalTicks =
                    static_cast<double>(
                        newer->physicsEvidenceTick
                        - older->physicsEvidenceTick);
                if (!interpolatePose(
                        *olderEntity, *newerEntity, alpha,
                        intervalTicks, output.visualPose)) {
                    result.error =
                        WreckwaterClientReplicationError::PositionOverflow;
                    return result;
                }
                output.motionMode =
                    WreckwaterVisualMotionMode::Interpolated;
            }
        } else if (
            globalMotion == WreckwaterVisualMotionMode::Extrapolated
            || globalMotion == WreckwaterVisualMotionMode::FrozenStale) {
            if (!extrapolatePose(
                    authoritative, extrapolationTicks,
                    output.visualPose)) {
                result.error =
                    WreckwaterClientReplicationError::PositionOverflow;
                return result;
            }
            output.motionMode = globalMotion;
        }
    }
    for (uint32_t index = 0u;
         index < result.sample.characterCount; ++index) {
        const WreckwaterCharacterState& authoritative =
            selected->characters[index];
        WreckwaterSampledCharacter& output =
            result.sample.characters[index];
        output.characterHandle = authoritative.characterHandle;
        output.authoritativeState = authoritative;
        output.visualPose = poseOf(authoritative);
        output.motionMode = WreckwaterVisualMotionMode::Snapped;

        if (globalMotion == WreckwaterVisualMotionMode::Interpolated) {
            const WreckwaterCharacterState* olderCharacter =
                findCharacterByHandle(
                    *older, authoritative.characterHandle);
            const WreckwaterCharacterState* newerCharacter =
                findCharacterByHandle(
                    *newer, authoritative.characterHandle);
            if (olderCharacter != nullptr
                && newerCharacter != nullptr) {
                const double intervalTicks =
                    static_cast<double>(
                        newer->physicsEvidenceTick
                        - older->physicsEvidenceTick);
                const bool continuous = continuousCharacter(
                    *olderCharacter, *newerCharacter);
                const bool shortGap =
                    newer->physicsEvidenceTick
                        - older->physicsEvidenceTick
                    <= config_
                           .maximumCharacterInterpolationGapTicks;
                bool interpolated = false;
                if (continuous && shortGap
                    && characterMode(*olderCharacter)
                        == WreckwaterCharacterMode::OnSkiff) {
                    const WreckwaterSampledEntity* sampledSkiff =
                        findSampledSkiff(
                            result.sample,
                            olderCharacter->skiffId,
                            olderCharacter->skiffGeneration);
                    if (sampledSkiff != nullptr) {
                        interpolated =
                            interpolateAttachedCharacterPose(
                                *olderCharacter,
                                *newerCharacter,
                                *sampledSkiff,
                                alpha, intervalTicks,
                                output.visualPose);
                    }
                } else if (continuous && shortGap) {
                    if (!interpolateCharacterPose(
                            *olderCharacter, *newerCharacter,
                            alpha, intervalTicks,
                            output.visualPose)) {
                        result.error =
                            WreckwaterClientReplicationError::
                                PositionOverflow;
                        return result;
                    }
                    interpolated = true;
                }
                if (interpolated) {
                    output.motionMode =
                        WreckwaterVisualMotionMode::Interpolated;
                } else {
                    // A lifecycle/mode/platform discontinuity or long
                    // snapshot gap becomes visible only at its certified
                    // newer evidence tick.
                    output.visualPose = poseOf(*olderCharacter);
                }
            }
        } else if (
            globalMotion == WreckwaterVisualMotionMode::Extrapolated
            || globalMotion == WreckwaterVisualMotionMode::FrozenStale) {
            bool extrapolated = false;
            if (characterConnected(authoritative)
                && characterMode(authoritative)
                    == WreckwaterCharacterMode::OnSkiff) {
                const WreckwaterSampledEntity* sampledSkiff =
                    findSampledSkiff(
                        result.sample,
                        authoritative.skiffId,
                        authoritative.skiffGeneration);
                if (sampledSkiff != nullptr) {
                    extrapolated =
                        extrapolateAttachedCharacterPose(
                            authoritative, *sampledSkiff,
                            extrapolationTicks,
                            output.visualPose);
                }
            } else if (characterConnected(authoritative)) {
                if (!extrapolateCharacterPose(
                        authoritative, extrapolationTicks,
                        output.visualPose)) {
                    result.error =
                        WreckwaterClientReplicationError::
                            PositionOverflow;
                    return result;
                }
                extrapolated = true;
            }
            if (extrapolated) {
                output.motionMode = globalMotion;
            }
        }
    }
    return result;
}

} // namespace voxy::network
