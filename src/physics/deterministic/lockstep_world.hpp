#pragma once

#include "physics/deterministic/lockstep_types.hpp"

#include <cstdint>
#include <span>
#include <vector>

namespace voxy::physics::deterministic {

[[nodiscard]] int32_t lockstepSaturatingAdd(int32_t lhs,
                                             int32_t rhs) noexcept;
[[nodiscard]] int32_t lockstepSaturatingSub(int32_t lhs,
                                             int32_t rhs) noexcept;
[[nodiscard]] int32_t lockstepMultiplyShift(int32_t lhs, int32_t rhs,
                                             uint32_t shift) noexcept;
[[nodiscard]] uint32_t lockstepIntegerSquareRoot(uint64_t value) noexcept;
[[nodiscard]] uint32_t lockstepHashWord(uint32_t hash,
                                        uint32_t word) noexcept;

class LockstepWorld {
public:
    struct Config {
        uint32_t bodyCapacity = 1'024;
        uint32_t contactCapacity = 4'096;
        uint32_t tickRateHz = kLockstepDefaultTickRateHz;
        uint32_t substeps = kLockstepDefaultSubsteps;
        uint32_t solverIterations = 4;
        int32_t gravityPerSubstepQ16 =
            kLockstepDefaultGravityPerSubstepQ16;
    };

    [[nodiscard]] bool initialize(const Config& config);
    void clear();
    [[nodiscard]] bool setBodies(std::span<const LockstepBody> bodies);
    [[nodiscard]] LockstepTelemetry step(uint32_t tick);

    [[nodiscard]] std::span<const LockstepBody> bodies() const noexcept {
        return bodies_;
    }
    [[nodiscard]] std::span<LockstepBody> bodies() noexcept { return bodies_; }
    [[nodiscard]] std::span<const LockstepContact> contacts() const noexcept {
        return contacts_;
    }
    [[nodiscard]] std::span<const uint32_t> islandRoots() const noexcept {
        return roots_;
    }
    [[nodiscard]] const Config& config() const noexcept { return config_; }

    [[nodiscard]] static LockstepHashes computeHashes(
        std::span<const LockstepBody> bodies,
        std::span<const LockstepContact> contacts,
        std::span<const uint32_t> roots, uint32_t tick,
        uint32_t contactCapacity);

private:
    struct BroadPhaseProxy {
        std::array<int64_t, 3> center{};
        int64_t radius = 0;
        uint32_t body = 0;
    };

    [[nodiscard]] bool contactFor(uint32_t bodyA, uint32_t bodyB,
                                  LockstepContact& output) const noexcept;
    void buildContacts(LockstepTelemetry& telemetry);
    void buildIslands();
    void solveContacts();
    void integrateBodies();

    Config config_{};
    std::vector<LockstepBody> bodies_;
    std::vector<LockstepContact> contacts_;
    std::vector<uint32_t> roots_;
    std::vector<BroadPhaseProxy> broadPhaseProxies_;
    std::vector<uint32_t> broadPhaseActive_;
    bool initialized_ = false;
};

} // namespace voxy::physics::deterministic
