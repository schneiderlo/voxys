#include "game/wreckwater_vessel_damage.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>
#include <tuple>

namespace voxy::game {
namespace {

constexpr size_t kInvalidIndex = std::numeric_limits<size_t>::max();
constexpr uint64_t kFnvOffset64 = 14'695'981'039'346'656'037ull;
constexpr uint64_t kFnvPrime64 = 1'099'511'628'211ull;

[[nodiscard]] bool finiteVector(const glm::vec3& value) noexcept {
    return std::isfinite(value.x) && std::isfinite(value.y)
        && std::isfinite(value.z);
}

[[nodiscard]] bool finiteQuaternion(const glm::quat& value) noexcept {
    return std::isfinite(value.w) && std::isfinite(value.x)
        && std::isfinite(value.y) && std::isfinite(value.z);
}

[[nodiscard]] bool validImpactMaterial(
    WreckwaterImpactMaterial material) noexcept {
    return static_cast<uint32_t>(material)
        <= static_cast<uint32_t>(WreckwaterImpactMaterial::Rock);
}

[[nodiscard]] uint32_t nextGeneration(uint32_t generation) noexcept {
    return generation == std::numeric_limits<uint32_t>::max()
        ? 0u : generation + 1u;
}

[[nodiscard]] bool handleLess(
    physics::BodyHandle lhs, physics::BodyHandle rhs) noexcept {
    return std::tuple{lhs.index, lhs.generation}
        < std::tuple{rhs.index, rhs.generation};
}

[[nodiscard]] bool contactLess(
    const WreckwaterContactHitEvidence& lhs,
    const WreckwaterContactHitEvidence& rhs) noexcept {
    return std::tuple{
        lhs.bodyA.index, lhs.bodyA.generation,
        lhs.bodyB.index, lhs.bodyB.generation,
        lhs.sourceId, lhs.featureA, lhs.featureB,
        std::bit_cast<uint32_t>(lhs.normalImpulse),
        std::bit_cast<uint32_t>(lhs.impactSpeed)}
        < std::tuple{
        rhs.bodyA.index, rhs.bodyA.generation,
        rhs.bodyB.index, rhs.bodyB.generation,
        rhs.sourceId, rhs.featureA, rhs.featureB,
        std::bit_cast<uint32_t>(rhs.normalImpulse),
        std::bit_cast<uint32_t>(rhs.impactSpeed)};
}

[[nodiscard]] bool sameContactIdentity(
    const WreckwaterContactHitEvidence& lhs,
    const WreckwaterContactHitEvidence& rhs) noexcept {
    return lhs.bodyA == rhs.bodyA && lhs.bodyB == rhs.bodyB
        && lhs.sourceId == rhs.sourceId
        && lhs.featureA == rhs.featureA
        && lhs.featureB == rhs.featureB;
}

void hashU32(uint64_t& hash, uint32_t value) noexcept {
    for (uint32_t shift = 0u; shift < 32u; shift += 8u) {
        hash ^= static_cast<uint8_t>(value >> shift);
        hash *= kFnvPrime64;
    }
}

void hashU64(uint64_t& hash, uint64_t value) noexcept {
    hashU32(hash, static_cast<uint32_t>(value));
    hashU32(hash, static_cast<uint32_t>(value >> 32u));
}

void hashFloat(uint64_t& hash, float value) noexcept {
    hashU32(hash, std::bit_cast<uint32_t>(value));
}

[[nodiscard]] float materialMultiplier(
    const WreckwaterVesselDamageAuthority::Config& config,
    WreckwaterImpactMaterial material) noexcept {
    switch (material) {
        case WreckwaterImpactMaterial::Timber:
            return config.timberDamageMultiplier;
        case WreckwaterImpactMaterial::Steel:
            return config.steelDamageMultiplier;
        case WreckwaterImpactMaterial::Rock:
            return config.rockDamageMultiplier;
        case WreckwaterImpactMaterial::Unknown:
            return config.steelDamageMultiplier;
    }
    return config.steelDamageMultiplier;
}

[[nodiscard]] uint64_t saturatingMilliImpulse(
    long double impulse) noexcept {
    if (!(impulse > 0.0L)) return 0u;
    const long double rounded =
        std::floor(impulse * 1'000.0L + 0.5L);
    const long double maximum = static_cast<long double>(
        std::numeric_limits<uint64_t>::max());
    if (!std::isfinite(rounded) || rounded >= maximum) {
        return std::numeric_limits<uint64_t>::max();
    }
    return static_cast<uint64_t>(rounded);
}

} // namespace

const char* wreckwaterVesselDamageStatusName(
    WreckwaterVesselDamageStatus status) noexcept {
    switch (status) {
        case WreckwaterVesselDamageStatus::Accepted:
            return "accepted";
        case WreckwaterVesselDamageStatus::NotInitialized:
            return "not initialized";
        case WreckwaterVesselDamageStatus::InvalidConfiguration:
            return "invalid configuration";
        case WreckwaterVesselDamageStatus::InvalidInput:
            return "invalid input";
        case WreckwaterVesselDamageStatus::NonMonotonicTick:
            return "non-monotonic tick";
        case WreckwaterVesselDamageStatus::ContactCapacityExceeded:
            return "contact capacity exceeded";
        case WreckwaterVesselDamageStatus::DuplicateContactIdentity:
            return "duplicate contact identity";
        case WreckwaterVesselDamageStatus::StaleBodyIdentity:
            return "stale body identity";
        case WreckwaterVesselDamageStatus::LifecycleMismatch:
            return "lifecycle mismatch";
        case WreckwaterVesselDamageStatus::IdentityExhausted:
            return "identity exhausted";
        case WreckwaterVesselDamageStatus::OutputCapacityExceeded:
            return "output capacity exceeded";
    }
    return "unknown";
}

bool canonicalizeWreckwaterContactHit(
    WreckwaterContactHitEvidence& hit) noexcept {
    if (hit.physicsTick == 0u
        || !hit.bodyA.valid() || !hit.bodyB.valid()
        || hit.bodyA == hit.bodyB
        || !finiteVector(hit.localAnchorA)
        || !finiteVector(hit.localAnchorB)
        || !finiteVector(hit.normalAtoB)
        || !std::isfinite(hit.normalImpulse)
        || !std::isfinite(hit.impactSpeed)
        || hit.normalImpulse < 0.0f || hit.impactSpeed < 0.0f
        || !validImpactMaterial(hit.materialA)
        || !validImpactMaterial(hit.materialB)) {
        return false;
    }
    const float normalLengthSquared =
        glm::dot(hit.normalAtoB, hit.normalAtoB);
    if (!std::isfinite(normalLengthSquared)
        || normalLengthSquared <= 1.0e-12f) {
        return false;
    }
    hit.normalAtoB *= glm::inversesqrt(normalLengthSquared);
    if (handleLess(hit.bodyB, hit.bodyA)) {
        std::swap(hit.bodyA, hit.bodyB);
        std::swap(hit.featureA, hit.featureB);
        std::swap(hit.localAnchorA, hit.localAnchorB);
        std::swap(hit.materialA, hit.materialB);
        hit.normalAtoB = -hit.normalAtoB;
    }
    return true;
}

bool WreckwaterVesselDamageAuthority::validConfig(
    const Config& config) const noexcept {
    return std::isfinite(config.waterHeight)
        && std::isfinite(
            config.waterDensityKilogramsPerCubicMetre)
        && config.waterDensityKilogramsPerCubicMetre > 0.0f
        && std::isfinite(config.gravityMetresPerSecondSquared)
        && config.gravityMetresPerSecondSquared > 0.0f
        && std::isfinite(config.breachDischargeCoefficient)
        && config.breachDischargeCoefficient > 0.0f
        && config.breachDischargeCoefficient <= 1.0f
        && std::isfinite(config.breachAreaSquareMetres)
        && config.breachAreaSquareMetres > 0.0f
        && std::isfinite(config.compartmentCapacityCubicMetres)
        && config.compartmentCapacityCubicMetres > 0.0f
        && std::isfinite(config.sinkFloodedFraction)
        && config.sinkFloodedFraction > 0.0f
        && config.sinkFloodedFraction <= 1.0f
        && std::isfinite(config.minimumImpactSpeed)
        && config.minimumImpactSpeed >= 0.0f
        && std::isfinite(config.edgeBreakImpulse)
        && config.edgeBreakImpulse >= 0.0005f
        && std::isfinite(config.timberDamageMultiplier)
        && config.timberDamageMultiplier > 0.0f
        && std::isfinite(config.steelDamageMultiplier)
        && config.steelDamageMultiplier > 0.0f
        && std::isfinite(config.rockDamageMultiplier)
        && config.rockDamageMultiplier > 0.0f
        && config.respawnDelayPhysicsTicks != 0u
        && config.significantFragmentLifetimeTicks != 0u;
}

size_t WreckwaterVesselDamageAuthority::vesselIndex(
    uint32_t skiffId) const noexcept {
    for (size_t index = 0u; index < vessels_.size(); ++index) {
        if (vessels_[index].skiffId == skiffId) return index;
    }
    return kInvalidIndex;
}

size_t WreckwaterVesselDamageAuthority::vesselIndex(
    physics::BodyHandle body) const noexcept {
    for (size_t index = 0u; index < vessels_.size(); ++index) {
        if (vessels_[index].body == body) return index;
    }
    return kInvalidIndex;
}

WreckwaterVesselDamageStatus
WreckwaterVesselDamageAuthority::resetTopologyForVessel(
    size_t index, uint32_t nextSkiffGenerationValue,
    physics::BodyHandle body) noexcept {
    if (index >= vessels_.size() || nextSkiffGenerationValue == 0u
        || !body.valid() || body.generation == 0u) {
        return WreckwaterVesselDamageStatus::InvalidInput;
    }
    const size_t compartmentBase =
        index * kWreckwaterCompartmentsPerSkiff;
    const size_t edgeBase =
        index * kWreckwaterStructuralEdgesPerSkiff;
    const uint32_t skiffId = vessels_[index].skiffId;

    // Generation rollover is a transaction boundary. Preflight every slot
    // before mutating any topology so exhaustion cannot leave half a vessel
    // on the next generation.
    for (size_t local = 0u;
         local < kWreckwaterCompartmentsPerSkiff; ++local) {
        if (compartments_[compartmentBase + local]
                .handle.generation
            == std::numeric_limits<uint32_t>::max()
            || breaches_[compartmentBase + local]
                .handle.generation
            == std::numeric_limits<uint32_t>::max()) {
            return WreckwaterVesselDamageStatus::IdentityExhausted;
        }
    }
    for (size_t local = 0u;
         local < kWreckwaterStructuralEdgesPerSkiff; ++local) {
        if (edges_[edgeBase + local].handle.generation
            == std::numeric_limits<uint32_t>::max()) {
            return WreckwaterVesselDamageStatus::IdentityExhausted;
        }
    }

    constexpr std::array<glm::vec3,
        kWreckwaterCompartmentsPerSkiff> centers{{
        {-1.15f, -0.35f, 2.15f},
        {1.15f, -0.35f, 2.15f},
        {-1.15f, -0.35f, -2.15f},
        {1.15f, -0.35f, -2.15f},
    }};
    for (size_t local = 0u; local < centers.size(); ++local) {
        WreckwaterCompartmentState& compartment =
            compartments_[compartmentBase + local];
        uint32_t generation = compartment.handle.generation;
        if (generation == 0u) {
            generation = 1u;
        } else {
            generation = nextGeneration(generation);
            if (generation == 0u) {
                return WreckwaterVesselDamageStatus::
                    IdentityExhausted;
            }
        }
        compartment = {
            .handle = {
                static_cast<uint32_t>(compartmentBase + local + 1u),
                generation,
            },
            .skiffId = skiffId,
            .skiffGeneration = nextSkiffGenerationValue,
            .localCenter = centers[local],
            .capacityCubicMetres =
                config_.compartmentCapacityCubicMetres,
            .waterCubicMetres = 0.0f,
            .active = true,
        };
    }

    for (size_t local = 0u;
         local < kWreckwaterStructuralEdgesPerSkiff; ++local) {
        WreckwaterStructuralEdgeState& edge = edges_[edgeBase + local];
        uint32_t generation = edge.handle.generation;
        if (generation == 0u) {
            generation = 1u;
        } else {
            generation = nextGeneration(generation);
            if (generation == 0u) {
                return WreckwaterVesselDamageStatus::
                    IdentityExhausted;
            }
        }
        edge = {};
        edge.handle = {
            static_cast<uint32_t>(edgeBase + local + 1u),
            generation,
        };
        edge.skiffId = skiffId;
        edge.skiffGeneration = nextSkiffGenerationValue;
        edge.breakDamageMilliImpulse =
            saturatingMilliImpulse(config_.edgeBreakImpulse);
        edge.active = true;
        if (local < kWreckwaterCompartmentsPerSkiff) {
            edge.compartmentA =
                compartments_[compartmentBase + local].handle;
            edge.localCenter = centers[local];
            edge.localCenter.x +=
                edge.localCenter.x < 0.0f ? -0.85f : 0.85f;
            edge.exterior = true;
        } else if (local == 4u) {
            edge.compartmentA =
                compartments_[compartmentBase].handle;
            edge.compartmentB =
                compartments_[compartmentBase + 1u].handle;
            edge.localCenter = {0.0f, -0.20f, 2.15f};
        } else {
            edge.compartmentA =
                compartments_[compartmentBase + 2u].handle;
            edge.compartmentB =
                compartments_[compartmentBase + 3u].handle;
            edge.localCenter = {0.0f, -0.20f, -2.15f};
        }
    }

    const size_t breachBase =
        index * kWreckwaterCompartmentsPerSkiff;
    for (size_t local = 0u;
         local < kWreckwaterCompartmentsPerSkiff; ++local) {
        WreckwaterBreachState& breach =
            breaches_[breachBase + local];
        uint32_t generation = breach.handle.generation;
        if (generation == 0u) {
            generation = 1u;
        } else {
            generation = nextGeneration(generation);
            if (generation == 0u) {
                return WreckwaterVesselDamageStatus::
                    IdentityExhausted;
            }
        }
        breach = {};
        breach.handle = {
            static_cast<uint32_t>(breachBase + local + 1u),
            generation,
        };
    }

    vessels_[index].skiffGeneration = nextSkiffGenerationValue;
    vessels_[index].body = body;
    vessels_[index].lifecycle = WreckwaterVesselLifecycle::Active;
    vessels_[index].sunkAtPhysicsTick = 0u;
    vessels_[index].floodedFraction = 0.0f;
    return WreckwaterVesselDamageStatus::Accepted;
}

bool WreckwaterVesselDamageAuthority::initialize(
    const Config& config,
    std::span<const WreckwaterVesselPose> vessels) noexcept {
    if (initialized_ || !validConfig(config)
        || vessels.size() != kWreckwaterDamageSkiffCount) {
        return false;
    }
    std::array<bool, kWreckwaterDamageSkiffCount> seen{};
    for (const WreckwaterVesselPose& vessel : vessels) {
        if (vessel.skiffId == 0u
            || vessel.skiffId > kWreckwaterDamageSkiffCount
            || seen[vessel.skiffId - 1u]
            || vessel.skiffGeneration == 0u
            || !vessel.body.valid() || vessel.body.generation == 0u
            || !physics::isValidWorldPosition(vessel.position)
            || !finiteQuaternion(vessel.orientation)) {
            return false;
        }
        for (const WreckwaterVesselPose& prior : vessels) {
            if (&prior == &vessel) break;
            if (prior.body == vessel.body) return false;
        }
        seen[vessel.skiffId - 1u] = true;
    }

    config_ = config;
    vessels_ = {};
    compartments_ = {};
    edges_ = {};
    breaches_ = {};
    fragments_ = {};
    for (const WreckwaterVesselPose& vessel : vessels) {
        const size_t index = vessel.skiffId - 1u;
        vessels_[index].skiffId = vessel.skiffId;
        const WreckwaterVesselDamageStatus reset =
            resetTopologyForVessel(
                index, vessel.skiffGeneration, vessel.body);
        if (reset != WreckwaterVesselDamageStatus::Accepted) {
            vessels_ = {};
            compartments_ = {};
            edges_ = {};
            breaches_ = {};
            return false;
        }
    }
    floodForceOutputCount_ = 0u;
    lifecycleOutputCount_ = 0u;
    lastClosedPhysicsTick_ = 0u;
    initialized_ = true;
    updateStateHash();
    return true;
}

size_t WreckwaterVesselDamageAuthority::closestExteriorEdge(
    size_t vessel, const glm::vec3& localAnchor) const noexcept {
    if (vessel >= vessels_.size()) return kInvalidIndex;
    const size_t begin =
        vessel * kWreckwaterStructuralEdgesPerSkiff;
    size_t best = kInvalidIndex;
    float bestDistance = std::numeric_limits<float>::infinity();
    for (size_t local = 0u;
         local < kWreckwaterCompartmentsPerSkiff; ++local) {
        const size_t index = begin + local;
        const WreckwaterStructuralEdgeState& edge = edges_[index];
        if (!edge.active || !edge.exterior || edge.broken) continue;
        const glm::vec3 delta = localAnchor - edge.localCenter;
        const float distance = glm::dot(delta, delta);
        if (distance < bestDistance) {
            bestDistance = distance;
            best = index;
        }
    }
    return best;
}

uint64_t WreckwaterVesselDamageAuthority::quantizedDamage(
    float impulse, WreckwaterImpactMaterial material) const noexcept {
    return saturatingMilliImpulse(
        static_cast<long double>(impulse)
        * static_cast<long double>(
            materialMultiplier(config_, material)));
}

bool WreckwaterVesselDamageAuthority::appendLifecycle(
    const WreckwaterDamageLifecycleIntent& intent) noexcept {
    if (lifecycleOutputCount_ >= lifecycleOutput_.size()) return false;
    lifecycleOutput_[lifecycleOutputCount_++] = intent;
    return true;
}

bool WreckwaterVesselDamageAuthority::breakEdge(
    size_t edgeIndexValue, uint64_t physicsTick,
    WreckwaterVesselDamageTickResult& result) noexcept {
    if (edgeIndexValue >= edges_.size()) return false;
    WreckwaterStructuralEdgeState& edge = edges_[edgeIndexValue];
    if (!edge.active || edge.broken) return true;
    edge.broken = true;
    ++result.brokenEdgeCount;

    if (edge.exterior) {
        const size_t breachIndex = edgeIndexValue
            / kWreckwaterStructuralEdgesPerSkiff
                * kWreckwaterCompartmentsPerSkiff
            + edgeIndexValue % kWreckwaterStructuralEdgesPerSkiff;
        if (breachIndex >= breaches_.size()) return false;
        WreckwaterBreachState& breach = breaches_[breachIndex];
        breach.skiffId = edge.skiffId;
        breach.skiffGeneration = edge.skiffGeneration;
        breach.compartment = edge.compartmentA;
        breach.sourceEdge = edge.handle;
        breach.localCenter = edge.localCenter;
        breach.areaSquareMetres = config_.breachAreaSquareMetres;
        breach.active = true;
        ++result.openedBreachCount;
    }

    size_t fragmentIndex = kInvalidIndex;
    for (size_t index = 0u; index < fragments_.size(); ++index) {
        if (!fragments_[index].active) {
            fragmentIndex = index;
            break;
        }
    }
    if (fragmentIndex == kInvalidIndex) {
        ++result.suppressedFragmentCount;
        return true;
    }
    WreckwaterSignificantFragmentState& fragment =
        fragments_[fragmentIndex];
    uint32_t generation = fragment.handle.generation;
    generation = generation == 0u ? 1u : nextGeneration(generation);
    if (generation == 0u) return false;
    fragment = {
        .handle = {
            static_cast<uint32_t>(fragmentIndex + 1u), generation},
        .skiffId = edge.skiffId,
        .skiffGeneration = edge.skiffGeneration,
        .sourceEdge = edge.handle,
        .localCenter = edge.localCenter,
        .dimensions = {0.55f, 0.20f, 0.85f},
        .spawnedAtPhysicsTick = physicsTick,
        .active = true,
    };
    return appendLifecycle({
        .type = WreckwaterDamageLifecycleIntentType::
            SpawnSignificantFragment,
        .sourcePhysicsTick = physicsTick,
        .skiffId = edge.skiffId,
        .skiffGeneration = edge.skiffGeneration,
        .fragment = fragment.handle,
        .sourceEdge = edge.handle,
        .localCenter = fragment.localCenter,
        .dimensions = fragment.dimensions,
    });
}

void WreckwaterVesselDamageAuthority::retireExpiredFragments(
    uint64_t physicsTick,
    WreckwaterVesselDamageTickResult& result) noexcept {
    for (WreckwaterSignificantFragmentState& fragment : fragments_) {
        if (!fragment.active
            || physicsTick < fragment.spawnedAtPhysicsTick
            || physicsTick - fragment.spawnedAtPhysicsTick
                < config_.significantFragmentLifetimeTicks) {
            continue;
        }
        const WreckwaterDamageLifecycleIntent intent{
            .type = WreckwaterDamageLifecycleIntentType::
                RetireSignificantFragment,
            .sourcePhysicsTick = physicsTick,
            .skiffId = fragment.skiffId,
            .skiffGeneration = fragment.skiffGeneration,
            .fragment = fragment.handle,
            .sourceEdge = fragment.sourceEdge,
            .localCenter = fragment.localCenter,
            .dimensions = fragment.dimensions,
        };
        if (!appendLifecycle(intent)) {
            result.status =
                WreckwaterVesselDamageStatus::OutputCapacityExceeded;
            return;
        }
        fragment.active = false;
    }
}

WreckwaterVesselDamageStatus
WreckwaterVesselDamageAuthority::advanceFlooding(
    const std::array<WreckwaterVesselPose,
                     kWreckwaterDamageSkiffCount>& poses,
    uint64_t physicsTick,
    WreckwaterVesselDamageTickResult& result) noexcept {
    const float dt = 1.0f / 60.0f;
    for (const WreckwaterBreachState& breach : breaches_) {
        if (!breach.active) continue;
        const size_t vessel = vesselIndex(breach.skiffId);
        if (vessel == kInvalidIndex
            || vessels_[vessel].lifecycle
                != WreckwaterVesselLifecycle::Active) {
            continue;
        }
        const size_t compartmentIndex =
            breach.compartment.index == 0u
                ? kInvalidIndex : breach.compartment.index - 1u;
        if (compartmentIndex >= compartments_.size()) {
            return WreckwaterVesselDamageStatus::InvalidInput;
        }
        WreckwaterCompartmentState& compartment =
            compartments_[compartmentIndex];
        if (!compartment.active
            || compartment.handle != breach.compartment) {
            return WreckwaterVesselDamageStatus::StaleBodyIdentity;
        }
        const WreckwaterVesselPose& pose = poses[vessel];
        if (!pose.available) continue;
        const glm::vec3 worldOffset =
            pose.orientation * breach.localCenter;
        const double breachHeight =
            physics::worldPositionToAbsolute(pose.position).y
            + static_cast<double>(worldOffset.y);
        const float depth = std::max(
            config_.waterHeight - static_cast<float>(breachHeight),
            0.0f);
        if (depth <= 0.0f) continue;
        const float flowCubicMetresPerSecond =
            config_.breachDischargeCoefficient
            * breach.areaSquareMetres
            * std::sqrt(
                2.0f * config_.gravityMetresPerSecondSquared
                * depth);
        compartment.waterCubicMetres = std::min(
            compartment.capacityCubicMetres,
            compartment.waterCubicMetres
                + flowCubicMetresPerSecond * dt);
    }

    for (size_t vessel = 0u; vessel < vessels_.size(); ++vessel) {
        WreckwaterVesselDamageState& state = vessels_[vessel];
        const size_t base =
            vessel * kWreckwaterCompartmentsPerSkiff;
        float water = 0.0f;
        float capacity = 0.0f;
        for (size_t local = 0u;
             local < kWreckwaterCompartmentsPerSkiff; ++local) {
            const WreckwaterCompartmentState& compartment =
                compartments_[base + local];
            water += compartment.waterCubicMetres;
            capacity += compartment.capacityCubicMetres;
            if (state.lifecycle
                    != WreckwaterVesselLifecycle::Active
                || compartment.waterCubicMetres <= 0.0f) {
                continue;
            }
            if (floodForceOutputCount_
                >= floodForceOutput_.size()) {
                return WreckwaterVesselDamageStatus::
                    OutputCapacityExceeded;
            }
            const float floodedMass =
                compartment.waterCubicMetres
                * config_.waterDensityKilogramsPerCubicMetre;
            floodForceOutput_[floodForceOutputCount_++] = {
                .skiffId = state.skiffId,
                .skiffGeneration = state.skiffGeneration,
                .body = state.body,
                .compartment = compartment.handle,
                .localPoint = compartment.localCenter,
                .worldForce = {
                    0.0f,
                    -floodedMass
                        * config_.gravityMetresPerSecondSquared,
                    0.0f,
                },
            };
        }
        state.floodedFraction =
            capacity > 0.0f ? water / capacity : 0.0f;
        if (state.lifecycle == WreckwaterVesselLifecycle::Active
            && state.floodedFraction
                >= config_.sinkFloodedFraction) {
            state.lifecycle =
                WreckwaterVesselLifecycle::SunkWaitingForRespawn;
            state.sunkAtPhysicsTick = physicsTick;
            if (!appendLifecycle({
                    .type =
                        WreckwaterDamageLifecycleIntentType::
                            SkiffSunk,
                    .sourcePhysicsTick = physicsTick,
                    .skiffId = state.skiffId,
                    .skiffGeneration = state.skiffGeneration,
                })) {
                return WreckwaterVesselDamageStatus::
                    OutputCapacityExceeded;
            }
        } else if (
            state.lifecycle
                == WreckwaterVesselLifecycle::
                    SunkWaitingForRespawn
            && physicsTick >= state.sunkAtPhysicsTick
            && physicsTick - state.sunkAtPhysicsTick
                >= config_.respawnDelayPhysicsTicks) {
            const uint32_t next =
                nextGeneration(state.skiffGeneration);
            if (next == 0u) {
                return WreckwaterVesselDamageStatus::
                    IdentityExhausted;
            }
            for (WreckwaterSignificantFragmentState& fragment
                 : fragments_) {
                if (!fragment.active
                    || fragment.skiffId != state.skiffId
                    || fragment.skiffGeneration
                        != state.skiffGeneration) {
                    continue;
                }
                if (!appendLifecycle({
                        .type =
                            WreckwaterDamageLifecycleIntentType::
                                RetireSignificantFragment,
                        .sourcePhysicsTick = physicsTick,
                        .skiffId = fragment.skiffId,
                        .skiffGeneration =
                            fragment.skiffGeneration,
                        .fragment = fragment.handle,
                        .sourceEdge = fragment.sourceEdge,
                        .localCenter = fragment.localCenter,
                        .dimensions = fragment.dimensions,
                    })) {
                    return WreckwaterVesselDamageStatus::
                        OutputCapacityExceeded;
                }
                fragment.active = false;
            }
            if (!appendLifecycle({
                    .type =
                        WreckwaterDamageLifecycleIntentType::
                            SkiffRespawn,
                    .sourcePhysicsTick = physicsTick,
                    .skiffId = state.skiffId,
                    .skiffGeneration = state.skiffGeneration,
                    .nextSkiffGeneration = next,
                })) {
                return WreckwaterVesselDamageStatus::
                    OutputCapacityExceeded;
            }
            state.lifecycle =
                WreckwaterVesselLifecycle::RespawnEventQueued;
        }
    }
    result.floodForces = std::span(
        floodForceOutput_.data(), floodForceOutputCount_);
    result.lifecycleIntents = std::span(
        lifecycleOutput_.data(), lifecycleOutputCount_);
    return WreckwaterVesselDamageStatus::Accepted;
}

WreckwaterVesselDamageTickResult
WreckwaterVesselDamageAuthority::closeExactTick(
    const WreckwaterVesselDamageTickInput& input) noexcept {
    WreckwaterVesselDamageTickResult result;
    result.physicsTick = input.physicsTick;
    floodForceOutputCount_ = 0u;
    lifecycleOutputCount_ = 0u;
    if (!initialized_) return result;
    if (input.physicsTick == 0u
        || input.vessels.size() != vessels_.size()) {
        result.status = WreckwaterVesselDamageStatus::InvalidInput;
        return result;
    }
    if (input.physicsTick != lastClosedPhysicsTick_ + 1u) {
        result.status =
            WreckwaterVesselDamageStatus::NonMonotonicTick;
        return result;
    }
    if (input.contacts.size() > contactScratch_.size()) {
        result.status =
            WreckwaterVesselDamageStatus::ContactCapacityExceeded;
        return result;
    }

    std::array<WreckwaterVesselPose,
        kWreckwaterDamageSkiffCount> poses{};
    std::array<bool, kWreckwaterDamageSkiffCount> seen{};
    for (const WreckwaterVesselPose& pose : input.vessels) {
        const size_t index = vesselIndex(pose.skiffId);
        if (index == kInvalidIndex || seen[index]
            || pose.skiffGeneration
                != vessels_[index].skiffGeneration
            || pose.body != vessels_[index].body
            || !physics::isValidWorldPosition(pose.position)
            || !finiteQuaternion(pose.orientation)) {
            result.status =
                WreckwaterVesselDamageStatus::StaleBodyIdentity;
            return result;
        }
        const float orientationLength = glm::length(pose.orientation);
        if (!std::isfinite(orientationLength)
            || orientationLength <= 1.0e-6f) {
            result.status =
                WreckwaterVesselDamageStatus::InvalidInput;
            return result;
        }
        poses[index] = pose;
        poses[index].orientation /= orientationLength;
        seen[index] = true;
    }

    for (size_t index = 0u; index < input.contacts.size(); ++index) {
        WreckwaterContactHitEvidence hit = input.contacts[index];
        if (hit.physicsTick != input.physicsTick
            || !canonicalizeWreckwaterContactHit(hit)) {
            result.status =
                WreckwaterVesselDamageStatus::InvalidInput;
            return result;
        }
        contactScratch_[index] = hit;
    }
    std::sort(
        contactScratch_.begin(),
        contactScratch_.begin()
            + static_cast<std::ptrdiff_t>(input.contacts.size()),
        contactLess);
    for (size_t index = 1u; index < input.contacts.size(); ++index) {
        if (sameContactIdentity(
                contactScratch_[index - 1u],
                contactScratch_[index])) {
            result.status =
                WreckwaterVesselDamageStatus::
                    DuplicateContactIdentity;
            return result;
        }
    }

    retireExpiredFragments(input.physicsTick, result);
    if (result.status
        == WreckwaterVesselDamageStatus::OutputCapacityExceeded) {
        return result;
    }
    for (size_t index = 0u; index < input.contacts.size(); ++index) {
        const WreckwaterContactHitEvidence& hit =
            contactScratch_[index];
        const size_t vesselA = vesselIndex(hit.bodyA);
        const size_t vesselB = vesselIndex(hit.bodyB);
        const bool staleA = vesselA == kInvalidIndex
            && std::any_of(
                vessels_.begin(), vessels_.end(),
                [&hit](const WreckwaterVesselDamageState& vessel) {
                    return vessel.body.index == hit.bodyA.index;
                });
        const bool staleB = vesselB == kInvalidIndex
            && std::any_of(
                vessels_.begin(), vessels_.end(),
                [&hit](const WreckwaterVesselDamageState& vessel) {
                    return vessel.body.index == hit.bodyB.index;
                });
        if (staleA || staleB) {
            result.status =
                WreckwaterVesselDamageStatus::StaleBodyIdentity;
            return result;
        }
        if (hit.impactSpeed < config_.minimumImpactSpeed
            || hit.normalImpulse <= 0.0f) {
            continue;
        }
        if (vesselA == kInvalidIndex && vesselB == kInvalidIndex) {
            continue;
        }
        const auto applyToVessel =
            [this, &hit, &result, &input](
                size_t vessel, const glm::vec3& localAnchor,
                WreckwaterImpactMaterial impactingMaterial) {
                if (vessel == kInvalidIndex
                    || vessels_[vessel].lifecycle
                        != WreckwaterVesselLifecycle::Active) {
                    return true;
                }
                const size_t edgeIndexValue =
                    closestExteriorEdge(vessel, localAnchor);
                if (edgeIndexValue == kInvalidIndex) return true;
                WreckwaterStructuralEdgeState& edge =
                    edges_[edgeIndexValue];
                const uint64_t damage = quantizedDamage(
                    hit.normalImpulse, impactingMaterial);
                if (damage
                    > std::numeric_limits<uint64_t>::max()
                        - edge.accumulatedDamageMilliImpulse) {
                    edge.accumulatedDamageMilliImpulse =
                        std::numeric_limits<uint64_t>::max();
                } else {
                    edge.accumulatedDamageMilliImpulse += damage;
                }
                if (edge.accumulatedDamageMilliImpulse
                    >= edge.breakDamageMilliImpulse) {
                    return breakEdge(
                        edgeIndexValue, input.physicsTick, result);
                }
                return true;
            };
        if (!applyToVessel(
                vesselA, hit.localAnchorA, hit.materialB)
            || !applyToVessel(
                vesselB, hit.localAnchorB, hit.materialA)) {
            result.status =
                WreckwaterVesselDamageStatus::OutputCapacityExceeded;
            return result;
        }
        ++result.acceptedContactCount;
    }

    result.status = advanceFlooding(
        poses, input.physicsTick, result);
    if (!result) return result;
    lastClosedPhysicsTick_ = input.physicsTick;
    updateStateHash();
    return result;
}

WreckwaterVesselDamageStatus
WreckwaterVesselDamageAuthority::confirmRespawn(
    uint32_t skiffId, uint32_t priorSkiffGeneration,
    uint32_t nextSkiffGenerationValue,
    physics::BodyHandle body) noexcept {
    if (!initialized_) {
        return WreckwaterVesselDamageStatus::NotInitialized;
    }
    const size_t index = vesselIndex(skiffId);
    if (index == kInvalidIndex
        || vessels_[index].skiffGeneration
            != priorSkiffGeneration
        || vessels_[index].lifecycle
            != WreckwaterVesselLifecycle::RespawnEventQueued
        || nextSkiffGenerationValue
            != nextGeneration(priorSkiffGeneration)
        || !body.valid() || body.generation == 0u) {
        return WreckwaterVesselDamageStatus::LifecycleMismatch;
    }
    WreckwaterVesselDamageAuthority replacement = *this;
    const WreckwaterVesselDamageStatus reset =
        replacement.resetTopologyForVessel(
            index, nextSkiffGenerationValue, body);
    if (reset == WreckwaterVesselDamageStatus::Accepted) {
        vessels_ = replacement.vessels_;
        compartments_ = replacement.compartments_;
        edges_ = replacement.edges_;
        breaches_ = replacement.breaches_;
        updateStateHash();
    }
    return reset;
}

void WreckwaterVesselDamageAuthority::updateStateHash() noexcept {
    uint64_t hash = kFnvOffset64;
    hashU64(hash, lastClosedPhysicsTick_);
    for (const WreckwaterVesselDamageState& vessel : vessels_) {
        hashU32(hash, vessel.skiffId);
        hashU32(hash, vessel.skiffGeneration);
        hashU32(hash, vessel.body.index);
        hashU32(hash, vessel.body.generation);
        hashU32(hash, static_cast<uint32_t>(vessel.lifecycle));
        hashU64(hash, vessel.sunkAtPhysicsTick);
        hashFloat(hash, vessel.floodedFraction);
    }
    for (const WreckwaterCompartmentState& compartment
         : compartments_) {
        hashU32(hash, compartment.handle.index);
        hashU32(hash, compartment.handle.generation);
        hashU32(hash, compartment.skiffGeneration);
        hashFloat(hash, compartment.waterCubicMetres);
        hashU32(hash, compartment.active ? 1u : 0u);
    }
    for (const WreckwaterStructuralEdgeState& edge : edges_) {
        hashU32(hash, edge.handle.index);
        hashU32(hash, edge.handle.generation);
        hashU64(hash, edge.accumulatedDamageMilliImpulse);
        hashU32(hash, edge.broken ? 1u : 0u);
    }
    for (const WreckwaterBreachState& breach : breaches_) {
        hashU32(hash, breach.handle.index);
        hashU32(hash, breach.handle.generation);
        hashU32(hash, breach.sourceEdge.index);
        hashU32(hash, breach.sourceEdge.generation);
        hashFloat(hash, breach.areaSquareMetres);
        hashU32(hash, breach.active ? 1u : 0u);
    }
    for (const WreckwaterSignificantFragmentState& fragment
         : fragments_) {
        hashU32(hash, fragment.handle.index);
        hashU32(hash, fragment.handle.generation);
        hashU32(hash, fragment.sourceEdge.index);
        hashU32(hash, fragment.sourceEdge.generation);
        hashU64(hash, fragment.spawnedAtPhysicsTick);
        hashU32(hash, fragment.active ? 1u : 0u);
    }
    stateHash_ = hash;
}

} // namespace voxy::game
