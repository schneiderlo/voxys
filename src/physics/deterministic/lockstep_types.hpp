#pragma once

#include <array>
#include <cstdint>
#include <vector>

namespace voxy::physics::deterministic {

inline constexpr uint32_t kLockstepSchemaVersion = 1;
inline constexpr int32_t kLockstepPositionOne = 1 << 12;
inline constexpr int32_t kLockstepVelocityOne = 1 << 16;
inline constexpr int32_t kLockstepUnitOne = 1 << 30;
inline constexpr int32_t kLockstepSectorSize = 256 * kLockstepPositionOne;
inline constexpr int32_t kLockstepSectorHalf = kLockstepSectorSize / 2;
inline constexpr uint32_t kLockstepDefaultTickRateHz = 60;
inline constexpr uint32_t kLockstepDefaultSubsteps = 4;
inline constexpr uint32_t kLockstepVelocityToPositionScale = 16;
inline constexpr int32_t kLockstepDefaultGravityPerSubstepQ16 = -2'679;

enum LockstepBodyFlag : uint32_t {
    LockstepBodyAlive = 1u << 0u,
    LockstepBodyAwake = 1u << 1u,
    LockstepBodyStatic = 1u << 2u,
};

struct alignas(16) LockstepBody {
    // Stable body ID, generation, flags, integer sleep ticks.
    std::array<uint32_t, 4> identity{};
    // Sector xyz; radius in Q20.12.
    std::array<int32_t, 4> sectorRadius{};
    // Sector-local xyz in Q20.12; inverse mass in Q16.16.
    std::array<int32_t, 4> positionInvMass{};
    // Linear xyz in Q16.16; reserved.
    std::array<int32_t, 4> linearVelocity{};
};

struct alignas(16) LockstepContact {
    // body A, body B, canonical feature ID, active state.
    std::array<uint32_t, 4> ids{};
    // Contact normal xyz Q1.30; penetration Q20.12.
    std::array<int32_t, 4> normalPenetration{};
};

struct LockstepHashes {
    std::vector<uint32_t> bodies;
    std::vector<uint32_t> contacts;
    std::vector<uint32_t> islands;
    uint32_t bodyAggregate = 0;
    uint32_t contactAggregate = 0;
    uint32_t islandAggregate = 0;
    uint32_t world = 0;
};

struct LockstepTelemetry {
    uint32_t liveBodies = 0;
    uint32_t contacts = 0;
    bool contactOverflow = false;
    uint32_t tick = 0;
    LockstepHashes hashes;
};

static_assert(sizeof(LockstepBody) == 64);
static_assert(sizeof(LockstepContact) == 32);

} // namespace voxy::physics::deterministic
