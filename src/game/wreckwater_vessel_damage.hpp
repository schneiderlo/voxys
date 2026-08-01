#pragma once

#include "physics/physics_types.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace voxy::game {

inline constexpr size_t kWreckwaterDamageSkiffCount = 2u;
inline constexpr size_t kWreckwaterCompartmentsPerSkiff = 4u;
inline constexpr size_t kWreckwaterStructuralEdgesPerSkiff = 6u;
inline constexpr size_t kWreckwaterMaximumCompartments =
    kWreckwaterDamageSkiffCount * kWreckwaterCompartmentsPerSkiff;
inline constexpr size_t kWreckwaterMaximumStructuralEdges =
    kWreckwaterDamageSkiffCount * kWreckwaterStructuralEdgesPerSkiff;
inline constexpr size_t kWreckwaterMaximumBreaches =
    kWreckwaterMaximumCompartments;
inline constexpr size_t kWreckwaterMaximumSignificantFragments = 8u;
inline constexpr size_t kWreckwaterMaximumContactHitsPerTick = 64u;
inline constexpr size_t kWreckwaterMaximumFloodForcesPerTick =
    kWreckwaterMaximumCompartments;
// A live fragment implies its exterior source edge is already broken.
// Therefore retired fragments plus fragments spawned from newly broken edges
// cannot exceed the eight exterior edges, even when both happen in one tick.
inline constexpr size_t kWreckwaterMaximumDamageLifecycleIntentsPerTick =
    kWreckwaterMaximumSignificantFragments
    + kWreckwaterDamageSkiffCount;

struct WreckwaterCompartmentHandle {
    uint32_t index = 0u;
    uint32_t generation = 0u;

    [[nodiscard]] constexpr bool valid() const noexcept {
        return index != 0u && generation != 0u;
    }
    [[nodiscard]] constexpr auto operator<=>(
        const WreckwaterCompartmentHandle&) const noexcept = default;
};

struct WreckwaterStructuralEdgeHandle {
    uint32_t index = 0u;
    uint32_t generation = 0u;

    [[nodiscard]] constexpr bool valid() const noexcept {
        return index != 0u && generation != 0u;
    }
    [[nodiscard]] constexpr auto operator<=>(
        const WreckwaterStructuralEdgeHandle&) const noexcept = default;
};

struct WreckwaterBreachHandle {
    uint32_t index = 0u;
    uint32_t generation = 0u;

    [[nodiscard]] constexpr bool valid() const noexcept {
        return index != 0u && generation != 0u;
    }
    [[nodiscard]] constexpr auto operator<=>(
        const WreckwaterBreachHandle&) const noexcept = default;
};

struct WreckwaterFragmentHandle {
    uint32_t index = 0u;
    uint32_t generation = 0u;

    [[nodiscard]] constexpr bool valid() const noexcept {
        return index != 0u && generation != 0u;
    }
    [[nodiscard]] constexpr auto operator<=>(
        const WreckwaterFragmentHandle&) const noexcept = default;
};

enum class WreckwaterImpactMaterial : uint32_t {
    Unknown = 0u,
    Timber,
    Steel,
    Rock,
};

enum class WreckwaterVesselLifecycle : uint32_t {
    Active = 0u,
    SunkWaitingForRespawn,
    RespawnEventQueued,
};

enum class WreckwaterDamageLifecycleIntentType : uint32_t {
    SpawnSignificantFragment = 1u,
    RetireSignificantFragment = 2u,
    SkiffSunk = 3u,
    SkiffRespawn = 4u,
};

enum class WreckwaterVesselDamageStatus : uint32_t {
    Accepted = 0u,
    NotInitialized,
    InvalidConfiguration,
    InvalidInput,
    NonMonotonicTick,
    ContactCapacityExceeded,
    DuplicateContactIdentity,
    StaleBodyIdentity,
    LifecycleMismatch,
    IdentityExhausted,
    OutputCapacityExceeded,
};

[[nodiscard]] const char* wreckwaterVesselDamageStatusName(
    WreckwaterVesselDamageStatus status) noexcept;

struct WreckwaterVesselPose {
    uint32_t skiffId = 0u;
    uint32_t skiffGeneration = 0u;
    physics::BodyHandle body{};
    physics::WorldPosition position{};
    glm::quat orientation{1.0f, 0.0f, 0.0f, 0.0f};
    bool available = true;
};

// ContactHit evidence is canonical when bodyA < bodyB by the complete
// generational handle. localAnchorA/B and featureA/B follow that same order.
// normalAtoB points from A toward B.
struct WreckwaterContactHitEvidence {
    uint64_t physicsTick = 0u;
    uint32_t sourceId = 0u;
    physics::BodyHandle bodyA{};
    physics::BodyHandle bodyB{};
    uint32_t featureA = 0u;
    uint32_t featureB = 0u;
    glm::vec3 localAnchorA{0.0f};
    glm::vec3 localAnchorB{0.0f};
    glm::vec3 normalAtoB{1.0f, 0.0f, 0.0f};
    float normalImpulse = 0.0f;
    float impactSpeed = 0.0f;
    WreckwaterImpactMaterial materialA =
        WreckwaterImpactMaterial::Unknown;
    WreckwaterImpactMaterial materialB =
        WreckwaterImpactMaterial::Unknown;

    [[nodiscard]] bool operator==(
        const WreckwaterContactHitEvidence&) const = default;
};

[[nodiscard]] bool canonicalizeWreckwaterContactHit(
    WreckwaterContactHitEvidence& hit) noexcept;

struct WreckwaterCompartmentState {
    WreckwaterCompartmentHandle handle{};
    uint32_t skiffId = 0u;
    uint32_t skiffGeneration = 0u;
    glm::vec3 localCenter{0.0f};
    float capacityCubicMetres = 0.0f;
    float waterCubicMetres = 0.0f;
    bool active = false;
};

struct WreckwaterStructuralEdgeState {
    WreckwaterStructuralEdgeHandle handle{};
    uint32_t skiffId = 0u;
    uint32_t skiffGeneration = 0u;
    WreckwaterCompartmentHandle compartmentA{};
    WreckwaterCompartmentHandle compartmentB{};
    glm::vec3 localCenter{0.0f};
    uint64_t accumulatedDamageMilliImpulse = 0u;
    uint64_t breakDamageMilliImpulse = 0u;
    bool exterior = false;
    bool broken = false;
    bool active = false;
};

struct WreckwaterBreachState {
    WreckwaterBreachHandle handle{};
    uint32_t skiffId = 0u;
    uint32_t skiffGeneration = 0u;
    WreckwaterCompartmentHandle compartment{};
    WreckwaterStructuralEdgeHandle sourceEdge{};
    glm::vec3 localCenter{0.0f};
    float areaSquareMetres = 0.0f;
    bool active = false;
};

struct WreckwaterSignificantFragmentState {
    WreckwaterFragmentHandle handle{};
    uint32_t skiffId = 0u;
    uint32_t skiffGeneration = 0u;
    WreckwaterStructuralEdgeHandle sourceEdge{};
    glm::vec3 localCenter{0.0f};
    glm::vec3 dimensions{0.0f};
    uint64_t spawnedAtPhysicsTick = 0u;
    bool active = false;
};

struct WreckwaterVesselDamageState {
    uint32_t skiffId = 0u;
    uint32_t skiffGeneration = 0u;
    physics::BodyHandle body{};
    WreckwaterVesselLifecycle lifecycle =
        WreckwaterVesselLifecycle::Active;
    uint64_t sunkAtPhysicsTick = 0u;
    float floodedFraction = 0.0f;
};

struct WreckwaterFloodForceIntent {
    uint32_t skiffId = 0u;
    uint32_t skiffGeneration = 0u;
    physics::BodyHandle body{};
    WreckwaterCompartmentHandle compartment{};
    glm::vec3 localPoint{0.0f};
    glm::vec3 worldForce{0.0f};
};

struct WreckwaterDamageLifecycleIntent {
    WreckwaterDamageLifecycleIntentType type =
        WreckwaterDamageLifecycleIntentType::SpawnSignificantFragment;
    uint64_t sourcePhysicsTick = 0u;
    uint32_t skiffId = 0u;
    uint32_t skiffGeneration = 0u;
    uint32_t nextSkiffGeneration = 0u;
    WreckwaterFragmentHandle fragment{};
    WreckwaterStructuralEdgeHandle sourceEdge{};
    glm::vec3 localCenter{0.0f};
    glm::vec3 dimensions{0.0f};
};

struct WreckwaterVesselDamageTickInput {
    uint64_t physicsTick = 0u;
    std::span<const WreckwaterVesselPose> vessels{};
    std::span<const WreckwaterContactHitEvidence> contacts{};
};

struct WreckwaterVesselDamageTickResult {
    WreckwaterVesselDamageStatus status =
        WreckwaterVesselDamageStatus::NotInitialized;
    uint64_t physicsTick = 0u;
    uint32_t acceptedContactCount = 0u;
    uint32_t brokenEdgeCount = 0u;
    uint32_t openedBreachCount = 0u;
    uint32_t suppressedFragmentCount = 0u;
    std::span<const WreckwaterFloodForceIntent> floodForces{};
    std::span<const WreckwaterDamageLifecycleIntent> lifecycleIntents{};

    [[nodiscard]] explicit operator bool() const noexcept {
        return status == WreckwaterVesselDamageStatus::Accepted;
    }
};

class WreckwaterVesselDamageAuthority {
public:
    struct Config {
        float waterHeight = 0.0f;
        float waterDensityKilogramsPerCubicMetre = 1'025.0f;
        float gravityMetresPerSecondSquared = 9.81f;
        float breachDischargeCoefficient = 0.62f;
        float breachAreaSquareMetres = 0.075f;
        float compartmentCapacityCubicMetres = 1.35f;
        float sinkFloodedFraction = 0.62f;
        float minimumImpactSpeed = 1.0f;
        float edgeBreakImpulse = 150'000.0f;
        float timberDamageMultiplier = 0.65f;
        float steelDamageMultiplier = 1.0f;
        float rockDamageMultiplier = 1.35f;
        uint64_t respawnDelayPhysicsTicks = 300u;
        uint64_t significantFragmentLifetimeTicks = 600u;
    };

    [[nodiscard]] bool initialize(
        const Config& config,
        std::span<const WreckwaterVesselPose> vessels) noexcept;

    [[nodiscard]] WreckwaterVesselDamageTickResult closeExactTick(
        const WreckwaterVesselDamageTickInput& input) noexcept;

    [[nodiscard]] WreckwaterVesselDamageStatus confirmRespawn(
        uint32_t skiffId, uint32_t priorSkiffGeneration,
        uint32_t nextSkiffGeneration, physics::BodyHandle body) noexcept;

    [[nodiscard]] bool initialized() const noexcept {
        return initialized_;
    }
    [[nodiscard]] uint64_t lastClosedPhysicsTick() const noexcept {
        return lastClosedPhysicsTick_;
    }
    [[nodiscard]] uint64_t stateHash() const noexcept {
        return stateHash_;
    }
    [[nodiscard]] std::span<const WreckwaterVesselDamageState>
    vessels() const noexcept {
        return vessels_;
    }
    [[nodiscard]] std::span<const WreckwaterCompartmentState>
    compartments() const noexcept {
        return compartments_;
    }
    [[nodiscard]] std::span<const WreckwaterStructuralEdgeState>
    structuralEdges() const noexcept {
        return edges_;
    }
    [[nodiscard]] std::span<const WreckwaterBreachState>
    breaches() const noexcept {
        return breaches_;
    }
    [[nodiscard]] std::span<const WreckwaterSignificantFragmentState>
    significantFragments() const noexcept {
        return fragments_;
    }

private:
    [[nodiscard]] bool validConfig(const Config& config) const noexcept;
    [[nodiscard]] size_t vesselIndex(uint32_t skiffId) const noexcept;
    [[nodiscard]] size_t vesselIndex(
        physics::BodyHandle body) const noexcept;
    [[nodiscard]] WreckwaterVesselDamageStatus resetTopologyForVessel(
        size_t vesselIndex, uint32_t nextSkiffGeneration,
        physics::BodyHandle body) noexcept;
    [[nodiscard]] size_t closestExteriorEdge(
        size_t vesselIndex, const glm::vec3& localAnchor) const noexcept;
    [[nodiscard]] uint64_t quantizedDamage(
        float impulse, WreckwaterImpactMaterial material) const noexcept;
    [[nodiscard]] bool appendLifecycle(
        const WreckwaterDamageLifecycleIntent& intent) noexcept;
    void retireExpiredFragments(
        uint64_t physicsTick,
        WreckwaterVesselDamageTickResult& result) noexcept;
    [[nodiscard]] bool breakEdge(
        size_t edgeIndex, uint64_t physicsTick,
        WreckwaterVesselDamageTickResult& result) noexcept;
    [[nodiscard]] WreckwaterVesselDamageStatus advanceFlooding(
        const std::array<WreckwaterVesselPose,
                         kWreckwaterDamageSkiffCount>& poses,
        uint64_t physicsTick,
        WreckwaterVesselDamageTickResult& result) noexcept;
    void updateStateHash() noexcept;

    Config config_{};
    std::array<WreckwaterVesselDamageState,
               kWreckwaterDamageSkiffCount> vessels_{};
    std::array<WreckwaterCompartmentState,
               kWreckwaterMaximumCompartments> compartments_{};
    std::array<WreckwaterStructuralEdgeState,
               kWreckwaterMaximumStructuralEdges> edges_{};
    std::array<WreckwaterBreachState,
               kWreckwaterMaximumBreaches> breaches_{};
    std::array<WreckwaterSignificantFragmentState,
               kWreckwaterMaximumSignificantFragments> fragments_{};
    std::array<WreckwaterContactHitEvidence,
               kWreckwaterMaximumContactHitsPerTick> contactScratch_{};
    std::array<WreckwaterFloodForceIntent,
               kWreckwaterMaximumFloodForcesPerTick> floodForceOutput_{};
    std::array<WreckwaterDamageLifecycleIntent,
               kWreckwaterMaximumDamageLifecycleIntentsPerTick>
        lifecycleOutput_{};
    size_t floodForceOutputCount_ = 0u;
    size_t lifecycleOutputCount_ = 0u;
    uint64_t lastClosedPhysicsTick_ = 0u;
    uint64_t stateHash_ = 0u;
    bool initialized_ = false;
};

} // namespace voxy::game
