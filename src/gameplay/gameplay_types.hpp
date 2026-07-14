#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>

namespace voxy::gameplay {

inline constexpr uint32_t kGameplaySchemaVersion = 1;
inline constexpr int32_t kPositionOne = 1 << 12;
inline constexpr int32_t kScalarOne = 1 << 16;
inline constexpr uint32_t kGameplayTickRateHz = 60;
inline constexpr int32_t kGravityQ16 = 642'908;
inline constexpr int32_t kMaximumNodeMassQ16 = 64 * kScalarOne;
inline constexpr int32_t kMaximumNodeVolumeQ16 = 128 * kScalarOne;
inline constexpr int32_t kMaximumLocalPositionQ12 = 4'096 * kPositionOne;
inline constexpr uint32_t kMaximumAssemblyNodes = 256;
inline constexpr uint32_t kMaximumAssemblyEdges = 1'024;
inline constexpr int32_t kMaximumImpactQ16 = 64 * kScalarOne;

[[nodiscard]] constexpr int32_t saturateI32(int64_t value) noexcept {
    return static_cast<int32_t>(std::clamp(
        value, int64_t{std::numeric_limits<int32_t>::min()},
        int64_t{std::numeric_limits<int32_t>::max()}));
}

// Round halves away from zero. All authoritative divisions use this helper so
// native and WebAssembly cannot disagree through implementation-defined shifts.
[[nodiscard]] constexpr int64_t roundedDivide(
    int64_t numerator, int64_t denominator) noexcept {
    if (denominator == 0) {
        return numerator < 0 ? std::numeric_limits<int64_t>::min()
                             : std::numeric_limits<int64_t>::max();
    }
    const bool negative = (numerator < 0) != (denominator < 0);
    const uint64_t numeratorMagnitude = numerator < 0
        ? static_cast<uint64_t>(-(numerator + 1)) + 1u
        : static_cast<uint64_t>(numerator);
    const uint64_t denominatorMagnitude = denominator < 0
        ? static_cast<uint64_t>(-(denominator + 1)) + 1u
        : static_cast<uint64_t>(denominator);
    const uint64_t quotient = numeratorMagnitude / denominatorMagnitude;
    const uint64_t remainder = numeratorMagnitude % denominatorMagnitude;
    const uint64_t rounded = quotient
        + static_cast<uint64_t>(remainder >=
            (denominatorMagnitude / 2u + denominatorMagnitude % 2u));
    if (negative) {
        if (rounded >= (uint64_t{1} << 63u))
            return std::numeric_limits<int64_t>::min();
        return -static_cast<int64_t>(rounded);
    }
    if (rounded > static_cast<uint64_t>(std::numeric_limits<int64_t>::max()))
        return std::numeric_limits<int64_t>::max();
    return static_cast<int64_t>(rounded);
}

[[nodiscard]] constexpr int32_t multiplyQ16(
    int32_t lhs, int32_t rhs) noexcept {
    return saturateI32(roundedDivide(
        int64_t{lhs} * int64_t{rhs}, int64_t{kScalarOne}));
}

[[nodiscard]] constexpr int32_t ratioQ16(
    int64_t numerator, int64_t denominator) noexcept {
    if (denominator <= 0) return 0;
    if (numerator > std::numeric_limits<int64_t>::max() / kScalarOne)
        return std::numeric_limits<int32_t>::max();
    if (numerator < std::numeric_limits<int64_t>::min() / kScalarOne)
        return std::numeric_limits<int32_t>::min();
    return saturateI32(roundedDivide(
        numerator * int64_t{kScalarOne}, denominator));
}

[[nodiscard]] constexpr uint32_t gameplayHashWord(
    uint32_t hash, uint32_t word) noexcept {
    constexpr uint32_t prime = 16'777'619u;
    for (uint32_t byte = 0; byte < 4u; ++byte) {
        hash = (hash ^ ((word >> (byte * 8u)) & 0xffu)) * prime;
    }
    return hash;
}

[[nodiscard]] constexpr uint32_t gameplayHashI32(
    uint32_t hash, int32_t word) noexcept {
    return gameplayHashWord(hash, static_cast<uint32_t>(word));
}

inline constexpr uint32_t kGameplayHashOffset = 2'166'136'261u;

} // namespace voxy::gameplay
