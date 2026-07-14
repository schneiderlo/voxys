#include "physics/deterministic/lockstep_world.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <limits>

namespace voxy::physics::deterministic {
namespace {

constexpr uint32_t kFnvOffset = 2'166'136'261u;
constexpr uint32_t kFnvPrime = 16'777'619u;

uint32_t magnitude(int32_t value) noexcept {
    const uint32_t bits = static_cast<uint32_t>(value);
    return value < 0 ? 0u - bits : bits;
}

int32_t saturate(int64_t value) noexcept {
    return static_cast<int32_t>(std::clamp(
        value, int64_t{std::numeric_limits<int32_t>::min()},
        int64_t{std::numeric_limits<int32_t>::max()}));
}

int32_t normalizeLocal(int32_t& sector, int32_t local) noexcept {
    int64_t sectorDelta = local / kLockstepSectorSize;
    int64_t canonical = local % kLockstepSectorSize;
    if (canonical >= kLockstepSectorHalf) {
        ++sectorDelta;
        canonical -= kLockstepSectorSize;
    } else if (canonical < -kLockstepSectorHalf) {
        --sectorDelta;
        canonical += kLockstepSectorSize;
    }
    const int64_t candidate = int64_t{sector} + sectorDelta;
    if (candidate < std::numeric_limits<int32_t>::min()) {
        sector = std::numeric_limits<int32_t>::min();
        return -kLockstepSectorHalf;
    }
    if (candidate > std::numeric_limits<int32_t>::max()) {
        sector = std::numeric_limits<int32_t>::max();
        return kLockstepSectorHalf - 1;
    }
    sector = static_cast<int32_t>(candidate);
    return static_cast<int32_t>(canonical);
}

uint32_t unitFraction(uint32_t numerator, uint32_t denominator) noexcept {
    if (denominator == 0 || numerator >= denominator)
        return static_cast<uint32_t>(kLockstepUnitOne);
    uint64_t remainder = numerator;
    uint32_t quotient = 0;
    for (uint32_t bit = 0; bit < 30u; ++bit) {
        remainder <<= 1u;
        quotient <<= 1u;
        if (remainder >= denominator) {
            remainder -= denominator;
            quotient |= 1u;
        }
    }
    return quotient;
}

bool alive(const LockstepBody& body) noexcept {
    return (body.identity[2] & LockstepBodyAlive) != 0u;
}

bool dynamic(const LockstepBody& body) noexcept {
    return alive(body)
        && (body.identity[2] & LockstepBodyStatic) == 0u
        && body.positionInvMass[3] > 0;
}

} // namespace

int32_t lockstepSaturatingAdd(int32_t lhs, int32_t rhs) noexcept {
    return saturate(int64_t{lhs} + rhs);
}

int32_t lockstepSaturatingSub(int32_t lhs, int32_t rhs) noexcept {
    return saturate(int64_t{lhs} - rhs);
}

int32_t lockstepMultiplyShift(int32_t lhs, int32_t rhs,
                              uint32_t shift) noexcept {
    if (shift == 0 || shift >= 63) return shift == 0
        ? saturate(int64_t{lhs} * rhs) : 0;
    const bool negative = (lhs < 0) != (rhs < 0);
    const uint64_t product = uint64_t{magnitude(lhs)} * magnitude(rhs);
    const uint64_t rounded = product + (uint64_t{1} << (shift - 1u));
    const uint64_t value = rounded >> shift;
    if (negative) {
        if (value >= uint64_t{1} << 31u)
            return std::numeric_limits<int32_t>::min();
        return -static_cast<int32_t>(value);
    }
    if (value > uint64_t{std::numeric_limits<int32_t>::max()})
        return std::numeric_limits<int32_t>::max();
    return static_cast<int32_t>(value);
}

uint32_t lockstepIntegerSquareRoot(uint64_t value) noexcept {
    uint64_t remainder = 0;
    uint32_t root = 0;
    for (uint32_t iteration = 0; iteration < 32u; ++iteration) {
        root <<= 1u;
        remainder = (remainder << 2u) | (value >> 62u);
        value <<= 2u;
        const uint64_t candidate = (uint64_t{root} << 1u) | 1u;
        if (remainder >= candidate) {
            remainder -= candidate;
            root += 1u;
        }
    }
    return root;
}

uint32_t lockstepHashWord(uint32_t hash, uint32_t word) noexcept {
    return (hash ^ word) * kFnvPrime;
}

bool LockstepWorld::initialize(const Config& config) {
    clear();
    if (config.bodyCapacity == 0 || config.contactCapacity == 0
        || config.tickRateHz == 0
        || config.substeps == 0 || config.substeps > 16
        || config.solverIterations == 0 || config.solverIterations > 32) {
        return false;
    }
    const uint32_t divisorScale =
        config.substeps * kLockstepVelocityToPositionScale;
    if (config.tickRateHz
        > static_cast<uint32_t>(std::numeric_limits<int32_t>::max())
            / divisorScale)
        return false;
    config_ = config;
    bodies_.resize(config_.bodyCapacity);
    roots_.resize(config_.bodyCapacity);
    contacts_.reserve(config_.contactCapacity);
    initialized_ = true;
    return true;
}

void LockstepWorld::clear() {
    config_ = {};
    bodies_.clear();
    contacts_.clear();
    roots_.clear();
    initialized_ = false;
}

bool LockstepWorld::setBodies(std::span<const LockstepBody> bodies) {
    if (!initialized_ || bodies.size() > bodies_.size()) return false;
    std::fill(bodies_.begin(), bodies_.end(), LockstepBody{});
    std::copy(bodies.begin(), bodies.end(), bodies_.begin());
    for (uint32_t index = 0; index < bodies_.size(); ++index) {
        if (!alive(bodies_[index])) continue;
        if (bodies_[index].identity[0] != index) return false;
        if (bodies_[index].sectorRadius[3] <= 0) return false;
    }
    return true;
}

void LockstepWorld::integrateBodies() {
    const int32_t velocityToPositionDivisor = static_cast<int32_t>(
        uint64_t{config_.tickRateHz} * config_.substeps
        * kLockstepVelocityToPositionScale);
    for (auto& body : bodies_) {
        if (!dynamic(body)
            || (body.identity[2] & LockstepBodyAwake) == 0u) continue;
        body.linearVelocity[1] = lockstepSaturatingAdd(
            body.linearVelocity[1], config_.gravityPerSubstepQ16);
        for (uint32_t axis = 0; axis < 3u; ++axis) {
            const int32_t delta = body.linearVelocity[axis]
                                / velocityToPositionDivisor;
            body.positionInvMass[axis] = lockstepSaturatingAdd(
                body.positionInvMass[axis], delta);
            body.positionInvMass[axis] = normalizeLocal(
                body.sectorRadius[axis], body.positionInvMass[axis]);
        }
    }
}

bool LockstepWorld::contactFor(uint32_t bodyA, uint32_t bodyB,
                               LockstepContact& output) const noexcept {
    const auto& a = bodies_[bodyA];
    const auto& b = bodies_[bodyB];
    if (!alive(a) || !alive(b) || (!dynamic(a) && !dynamic(b))) return false;
    std::array<int32_t, 3> delta{};
    uint64_t squaredDistance = 0;
    for (uint32_t axis = 0; axis < 3u; ++axis) {
        const int64_t wideSectorDelta = int64_t{b.sectorRadius[axis]}
                                      - a.sectorRadius[axis];
        if (wideSectorDelta < -1 || wideSectorDelta > 1) return false;
        const int32_t sectorDelta = static_cast<int32_t>(wideSectorDelta);
        const int64_t globalDelta =
            int64_t{sectorDelta} * kLockstepSectorSize
            + int64_t{b.positionInvMass[axis]} - a.positionInvMass[axis];
        delta[axis] = saturate(globalDelta);
        const uint64_t component = magnitude(delta[axis]);
        squaredDistance += component * component;
    }
    const int32_t radiusSum = lockstepSaturatingAdd(
        a.sectorRadius[3], b.sectorRadius[3]);
    if (radiusSum <= 0) return false;
    const uint64_t radius = static_cast<uint32_t>(radiusSum);
    const uint64_t radiusSquared = radius * radius;
    if (squaredDistance >= radiusSquared) return false;
    const uint32_t distance = lockstepIntegerSquareRoot(squaredDistance);
    std::array<int32_t, 3> normal{};
    if (distance == 0) {
        normal[0] = bodyA < bodyB ? kLockstepUnitOne : -kLockstepUnitOne;
    } else {
        for (uint32_t axis = 0; axis < 3u; ++axis) {
            const uint32_t fraction = unitFraction(
                magnitude(delta[axis]), distance);
            normal[axis] = delta[axis] < 0
                ? -static_cast<int32_t>(fraction)
                : static_cast<int32_t>(fraction);
        }
    }
    output.ids = {bodyA, bodyB, 0u, 1u};
    output.normalPenetration = {
        normal[0], normal[1], normal[2],
        lockstepSaturatingSub(radiusSum, static_cast<int32_t>(distance))};
    return true;
}

void LockstepWorld::buildContacts(LockstepTelemetry& telemetry) {
    contacts_.clear();
    uint32_t total = 0;
    for (uint32_t bodyA = 0; bodyA < bodies_.size(); ++bodyA) {
        if (!alive(bodies_[bodyA])) continue;
        for (uint32_t bodyB = bodyA + 1u; bodyB < bodies_.size(); ++bodyB) {
            LockstepContact contact;
            if (!contactFor(bodyA, bodyB, contact)) continue;
            if (contacts_.size() < config_.contactCapacity)
                contacts_.push_back(contact);
            ++total;
        }
    }
    telemetry.contacts = static_cast<uint32_t>(contacts_.size());
    telemetry.contactOverflow = total > config_.contactCapacity;
}

void LockstepWorld::buildIslands() {
    for (uint32_t body = 0; body < bodies_.size(); ++body)
        roots_[body] = alive(bodies_[body]) ? body : std::numeric_limits<uint32_t>::max();
    for (uint32_t round = 0; round < 32u; ++round) {
        for (const auto& contact : contacts_) {
            const uint32_t bodyA = contact.ids[0];
            const uint32_t bodyB = contact.ids[1];
            const uint32_t rootA = roots_[bodyA];
            const uint32_t rootB = roots_[bodyB];
            const uint32_t lower = std::min(rootA, rootB);
            const uint32_t higher = std::max(rootA, rootB);
            if (higher < roots_.size()) roots_[higher] = lower;
        }
        for (uint32_t body = 0; body < bodies_.size(); ++body) {
            const uint32_t root = roots_[body];
            if (root < roots_.size()) roots_[body] = roots_[root];
        }
    }
}

void LockstepWorld::solveContacts() {
    for (uint32_t iteration = 0;
         iteration < config_.solverIterations; ++iteration) {
        for (auto& stored : contacts_) {
            const uint32_t bodyA = stored.ids[0];
            const uint32_t bodyB = stored.ids[1];
            LockstepContact contact;
            if (!contactFor(bodyA, bodyB, contact)) continue;
            stored = contact;
            auto& a = bodies_[bodyA];
            auto& b = bodies_[bodyB];
            const bool dynamicA = dynamic(a);
            const bool dynamicB = dynamic(b);
            const uint32_t correctionShift = dynamicA && dynamicB ? 31u : 30u;
            for (uint32_t axis = 0; axis < 3u; ++axis) {
                const int32_t correction = lockstepMultiplyShift(
                    contact.normalPenetration[3],
                    contact.normalPenetration[axis], correctionShift);
                if (dynamicA) {
                    a.positionInvMass[axis] = lockstepSaturatingSub(
                        a.positionInvMass[axis], correction);
                    a.positionInvMass[axis] = normalizeLocal(
                        a.sectorRadius[axis], a.positionInvMass[axis]);
                }
                if (dynamicB) {
                    b.positionInvMass[axis] = lockstepSaturatingAdd(
                        b.positionInvMass[axis], correction);
                    b.positionInvMass[axis] = normalizeLocal(
                        b.sectorRadius[axis], b.positionInvMass[axis]);
                }
            }
            int32_t normalVelocity = 0;
            for (uint32_t axis = 0; axis < 3u; ++axis) {
                const int32_t relative = lockstepSaturatingSub(
                    b.linearVelocity[axis], a.linearVelocity[axis]);
                normalVelocity = lockstepSaturatingAdd(
                    normalVelocity, lockstepMultiplyShift(
                        relative, contact.normalPenetration[axis], 30u));
            }
            if (normalVelocity >= 0) continue;
            const int32_t closing = normalVelocity
                == std::numeric_limits<int32_t>::min()
                ? std::numeric_limits<int32_t>::max() : -normalVelocity;
            const uint32_t velocityShift = dynamicA && dynamicB ? 31u : 30u;
            for (uint32_t axis = 0; axis < 3u; ++axis) {
                const int32_t impulse = lockstepMultiplyShift(
                    closing, contact.normalPenetration[axis], velocityShift);
                if (dynamicA) a.linearVelocity[axis] = lockstepSaturatingSub(
                    a.linearVelocity[axis], impulse);
                if (dynamicB) b.linearVelocity[axis] = lockstepSaturatingAdd(
                    b.linearVelocity[axis], impulse);
            }
        }
    }
}

LockstepTelemetry LockstepWorld::step(uint32_t tick) {
    LockstepTelemetry telemetry;
    if (!initialized_) return telemetry;
    telemetry.tick = tick;
    for (const auto& body : bodies_) telemetry.liveBodies += alive(body) ? 1u : 0u;
    for (uint32_t substep = 0; substep < config_.substeps; ++substep) {
        integrateBodies();
        buildContacts(telemetry);
        buildIslands();
        solveContacts();
    }
    buildContacts(telemetry);
    buildIslands();
    telemetry.hashes = computeHashes(
        bodies_, contacts_, roots_, tick, config_.contactCapacity);
    return telemetry;
}

LockstepHashes LockstepWorld::computeHashes(
    std::span<const LockstepBody> bodies,
    std::span<const LockstepContact> contacts,
    std::span<const uint32_t> roots, uint32_t tick,
    uint32_t contactCapacity) {
    LockstepHashes result;
    result.bodies.resize(bodies.size());
    result.contacts.resize(contactCapacity);
    result.islands.resize(bodies.size());
    result.bodyAggregate = kFnvOffset;
    for (uint32_t index = 0; index < bodies.size(); ++index) {
        uint32_t hash = kFnvOffset;
        const auto words = std::bit_cast<std::array<uint32_t, 16>>(
            bodies[index]);
        for (uint32_t word : words)
            hash = lockstepHashWord(hash, word);
        result.bodies[index] = hash;
        result.bodyAggregate = lockstepHashWord(result.bodyAggregate, hash);
    }
    result.contactAggregate = kFnvOffset;
    for (uint32_t index = 0; index < contacts.size(); ++index) {
        uint32_t hash = kFnvOffset;
        const auto words = std::bit_cast<std::array<uint32_t, 8>>(
            contacts[index]);
        for (uint32_t word : words)
            hash = lockstepHashWord(hash, word);
        result.contacts[index] = hash;
        result.contactAggregate = lockstepHashWord(
            result.contactAggregate, hash);
    }
    result.islandAggregate = kFnvOffset;
    for (uint32_t root = 0; root < bodies.size(); ++root) {
        if (root >= roots.size() || roots[root] != root || !alive(bodies[root]))
            continue;
        uint32_t hash = lockstepHashWord(kFnvOffset, root);
        for (uint32_t body = 0; body < bodies.size(); ++body) {
            if (body < roots.size() && roots[body] == root)
                hash = lockstepHashWord(hash, result.bodies[body]);
        }
        result.islands[root] = hash;
        result.islandAggregate = lockstepHashWord(result.islandAggregate, hash);
    }
    uint32_t world = lockstepHashWord(kFnvOffset, tick);
    world = lockstepHashWord(world, result.bodyAggregate);
    world = lockstepHashWord(world, result.contactAggregate);
    world = lockstepHashWord(world, result.islandAggregate);
    result.world = world;
    return result;
}

} // namespace voxy::physics::deterministic
