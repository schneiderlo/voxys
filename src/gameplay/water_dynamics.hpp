#pragma once

#include "gameplay/gameplay_types.hpp"

#include <array>
#include <cstdint>
#include <span>

namespace voxy::gameplay {

struct WaterFieldConfig {
    int32_t baseHeightQ12 = 0;
    int32_t primaryAmplitudeQ12 = kPositionOne / 8;
    int32_t secondaryAmplitudeQ12 = kPositionOne / 16;
    int32_t primaryWavelengthQ12 = 16 * kPositionOne;
    int32_t secondaryWavelengthQ12 = 11 * kPositionOne;
    int32_t primaryAdvanceQ12 = kPositionOne / 32;
    int32_t secondaryAdvanceQ12 = -kPositionOne / 48;
};

struct WaterSurfaceSampleQ {
    int32_t heightQ12 = 0;
    int32_t verticalVelocityQ16 = 0;
};

class DeterministicWaterField {
public:
    DeterministicWaterField();
    explicit DeterministicWaterField(WaterFieldConfig config);

    [[nodiscard]] bool valid() const noexcept;
    [[nodiscard]] WaterSurfaceSampleQ sample(
        int32_t xQ12, int32_t zQ12, uint64_t tick) const noexcept;
    [[nodiscard]] uint32_t configHash() const noexcept;

private:
    [[nodiscard]] int32_t height(
        int32_t xQ12, int32_t zQ12, uint64_t tick) const noexcept;

    WaterFieldConfig config_{};
};

struct BuoyancyPointQ {
    std::array<int32_t, 3> localPositionQ12{};
    int32_t displacedVolumeQ16 = 0;
    int32_t floodedFractionQ16 = 0;
};

struct BuoyancyInputQ {
    int32_t worldVerticalOffsetQ12 = 0;
    int32_t verticalVelocityQ16 = 0;
    int32_t massQ16 = 0;
    int32_t dragCoefficientQ16 = kScalarOne / 2;
};

struct BuoyancyResultQ {
    int32_t accelerationQ16 = -kGravityQ16;
    int32_t submergedVolumeQ16 = 0;
    int32_t effectiveVolumeQ16 = 0;
    int32_t submergedFractionQ16 = 0;
    int32_t centerOfBuoyancyYQ12 = 0;
    int32_t waterVerticalVelocityQ16 = 0;
};

[[nodiscard]] BuoyancyResultQ evaluateBuoyancy(
    std::span<const BuoyancyPointQ> points,
    const BuoyancyInputQ& input,
    const DeterministicWaterField& water,
    uint64_t tick) noexcept;

} // namespace voxy::gameplay
