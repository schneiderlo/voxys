#include "gameplay/water_dynamics.hpp"

#include <algorithm>
#include <limits>
#include <numeric>

namespace voxy::gameplay {
namespace {

int64_t floorModulo(int64_t value, int64_t modulus) noexcept {
    const int64_t remainder = value % modulus;
    return remainder < 0 ? remainder + modulus : remainder;
}

int32_t triangleWave(
    int64_t phaseQ12, int32_t wavelengthQ12,
    int32_t amplitudeQ12) noexcept {
    const int64_t period = wavelengthQ12;
    const int64_t phase = floorModulo(phaseQ12, period);
    const int64_t distance = std::min(phase, period - phase);
    const int64_t offset = -int64_t{amplitudeQ12}
        + roundedDivide(4 * int64_t{amplitudeQ12} * distance, period);
    return saturateI32(offset);
}

int64_t periodicAdvance(
    uint64_t tick, int32_t advanceQ12, int32_t wavelengthQ12) noexcept {
    if (advanceQ12 == 0) return 0;
    const int64_t advanceMagnitude = advanceQ12 < 0
        ? -int64_t{advanceQ12} : int64_t{advanceQ12};
    const int64_t divisor = std::gcd(
        advanceMagnitude, int64_t{wavelengthQ12});
    const uint64_t cycle = static_cast<uint64_t>(
        int64_t{wavelengthQ12} / divisor);
    return static_cast<int64_t>(tick % cycle) * advanceQ12;
}

int64_t saturatingAdd(int64_t lhs, int64_t rhs) noexcept {
    if (rhs > 0 && lhs > std::numeric_limits<int64_t>::max() - rhs)
        return std::numeric_limits<int64_t>::max();
    if (rhs < 0 && lhs < std::numeric_limits<int64_t>::min() - rhs)
        return std::numeric_limits<int64_t>::min();
    return lhs + rhs;
}

} // namespace

DeterministicWaterField::DeterministicWaterField()
    : DeterministicWaterField(WaterFieldConfig{}) {}

DeterministicWaterField::DeterministicWaterField(WaterFieldConfig config)
    : config_(config) {}

bool DeterministicWaterField::valid() const noexcept {
    return config_.primaryAmplitudeQ12 >= 0
        && config_.secondaryAmplitudeQ12 >= 0
        && config_.primaryAmplitudeQ12 <= kMaximumLocalPositionQ12
        && config_.secondaryAmplitudeQ12 <= kMaximumLocalPositionQ12
        && config_.primaryWavelengthQ12 >= 4
        && config_.secondaryWavelengthQ12 >= 4
        && config_.baseHeightQ12 >= -kMaximumLocalPositionQ12
        && config_.baseHeightQ12 <= kMaximumLocalPositionQ12;
}

int32_t DeterministicWaterField::height(
    int32_t xQ12, int32_t zQ12, uint64_t tick) const noexcept {
    if (!valid()) return config_.baseHeightQ12;
    const int64_t primaryPhase = int64_t{xQ12}
        + roundedDivide(zQ12, 2)
        + periodicAdvance(tick, config_.primaryAdvanceQ12,
                          config_.primaryWavelengthQ12);
    const int64_t secondaryPhase = -roundedDivide(xQ12, 2)
        + int64_t{zQ12}
        + periodicAdvance(tick, config_.secondaryAdvanceQ12,
                          config_.secondaryWavelengthQ12);
    return saturateI32(int64_t{config_.baseHeightQ12}
        + triangleWave(primaryPhase, config_.primaryWavelengthQ12,
                       config_.primaryAmplitudeQ12)
        + triangleWave(secondaryPhase, config_.secondaryWavelengthQ12,
                       config_.secondaryAmplitudeQ12));
}

WaterSurfaceSampleQ DeterministicWaterField::sample(
    int32_t xQ12, int32_t zQ12, uint64_t tick) const noexcept {
    const int32_t current = height(xQ12, zQ12, tick);
    const int32_t next = tick == std::numeric_limits<uint64_t>::max()
        ? current : height(xQ12, zQ12, tick + 1u);
    // Q12 metres/tick -> Q16 metres/second.
    const int64_t velocity = (int64_t{next} - current)
        * int64_t{kGameplayTickRateHz} * 16;
    return WaterSurfaceSampleQ{
        .heightQ12 = current,
        .verticalVelocityQ16 = saturateI32(velocity),
    };
}

uint32_t DeterministicWaterField::configHash() const noexcept {
    uint32_t hash = gameplayHashWord(kGameplayHashOffset,
                                     kGameplaySchemaVersion);
    hash = gameplayHashI32(hash, config_.baseHeightQ12);
    hash = gameplayHashI32(hash, config_.primaryAmplitudeQ12);
    hash = gameplayHashI32(hash, config_.secondaryAmplitudeQ12);
    hash = gameplayHashI32(hash, config_.primaryWavelengthQ12);
    hash = gameplayHashI32(hash, config_.secondaryWavelengthQ12);
    hash = gameplayHashI32(hash, config_.primaryAdvanceQ12);
    return gameplayHashI32(hash, config_.secondaryAdvanceQ12);
}

BuoyancyResultQ evaluateBuoyancy(
    std::span<const BuoyancyPointQ> points,
    const BuoyancyInputQ& input,
    const DeterministicWaterField& water,
    uint64_t tick) noexcept {
    BuoyancyResultQ result;
    if (input.massQ16 <= 0 || points.empty() || !water.valid()) return result;

    int64_t effectiveVolume = 0;
    int64_t submergedVolume = 0;
    int64_t weightedCenter = 0;
    int64_t weightedWaterVelocity = 0;
    for (const auto& point : points) {
        if (point.displacedVolumeQ16 <= 0) continue;
        const int32_t dryFraction = kScalarOne - std::clamp(
            point.floodedFractionQ16, 0, kScalarOne);
        const int32_t pointVolume = multiplyQ16(
            point.displacedVolumeQ16, dryFraction);
        if (pointVolume <= 0) continue;
        effectiveVolume = saturatingAdd(effectiveVolume, pointVolume);
        const auto surface = water.sample(
            point.localPositionQ12[0], point.localPositionQ12[2], tick);
        const int32_t worldY = saturateI32(
            int64_t{input.worldVerticalOffsetQ12}
            + point.localPositionQ12[1]);
        const int32_t depthQ12 = saturateI32(
            int64_t{surface.heightQ12} - worldY);
        const int32_t submergedFraction = std::clamp(
            saturateI32(roundedDivide(
                int64_t{depthQ12} * kScalarOne, kPositionOne)),
            0, kScalarOne);
        const int32_t submerged = multiplyQ16(
            pointVolume, submergedFraction);
        submergedVolume = saturatingAdd(submergedVolume, submerged);
        weightedCenter = saturatingAdd(
            weightedCenter, int64_t{submerged} * worldY);
        weightedWaterVelocity = saturatingAdd(
            weightedWaterVelocity,
            int64_t{submerged} * surface.verticalVelocityQ16);
    }
    result.effectiveVolumeQ16 = saturateI32(effectiveVolume);
    result.submergedVolumeQ16 = saturateI32(submergedVolume);
    if (submergedVolume <= 0 || effectiveVolume <= 0) return result;

    result.submergedFractionQ16 = std::clamp(
        ratioQ16(submergedVolume, effectiveVolume), 0, kScalarOne);
    result.centerOfBuoyancyYQ12 = saturateI32(
        roundedDivide(weightedCenter, submergedVolume));
    result.waterVerticalVelocityQ16 = saturateI32(
        roundedDivide(weightedWaterVelocity, submergedVolume));

    const int32_t volumeToMassQ16 = std::clamp(
        ratioQ16(submergedVolume, input.massQ16), 0, 3 * kScalarOne);
    const int32_t upwardAcceleration = multiplyQ16(
        kGravityQ16, volumeToMassQ16);
    const int32_t relativeVelocity = saturateI32(
        int64_t{input.verticalVelocityQ16}
        - result.waterVerticalVelocityQ16);
    const int32_t drag = multiplyQ16(
        multiplyQ16(relativeVelocity, std::clamp(
            input.dragCoefficientQ16, 0, 4 * kScalarOne)),
        result.submergedFractionQ16);
    result.accelerationQ16 = saturateI32(
        int64_t{upwardAcceleration} - kGravityQ16 - drag);
    return result;
}

} // namespace voxy::gameplay
