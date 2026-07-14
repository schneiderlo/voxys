#pragma once

#include <algorithm>
#include <cmath>
#include <compare>
#include <cstdint>
#include <limits>

namespace voxy::physics::deterministic {

// Signed saturating fixed point. Conversion from float is tooling-only;
// authoritative simulation uses raw values and integer operations.
template <uint32_t FractionBits>
class Fixed32 {
    static_assert(FractionBits > 0 && FractionBits < 31);

public:
    constexpr Fixed32() = default;

    [[nodiscard]] static constexpr Fixed32 fromRaw(int32_t value) noexcept {
        Fixed32 result;
        result.raw_ = value;
        return result;
    }

    [[nodiscard]] static constexpr Fixed32 fromInteger(int32_t value) noexcept {
        const int64_t scaled = int64_t{value}
            * (int64_t{1} << FractionBits);
        return fromRaw(saturate(scaled));
    }

    [[nodiscard]] static Fixed32 fromDouble(double value) noexcept {
        if (!std::isfinite(value)) {
            return fromRaw(value < 0.0
                ? std::numeric_limits<int32_t>::min()
                : std::numeric_limits<int32_t>::max());
        }
        const long double scaled = std::round(
            static_cast<long double>(value)
            * static_cast<long double>(uint64_t{1} << FractionBits));
        if (scaled >= std::numeric_limits<int32_t>::max())
            return fromRaw(std::numeric_limits<int32_t>::max());
        if (scaled <= std::numeric_limits<int32_t>::min())
            return fromRaw(std::numeric_limits<int32_t>::min());
        return fromRaw(static_cast<int32_t>(scaled));
    }

    [[nodiscard]] constexpr int32_t raw() const noexcept { return raw_; }
    [[nodiscard]] constexpr double toDouble() const noexcept {
        return static_cast<double>(raw_)
            / static_cast<double>(uint64_t{1} << FractionBits);
    }

    [[nodiscard]] friend constexpr Fixed32 operator+(
        Fixed32 lhs, Fixed32 rhs) noexcept {
        return fromRaw(saturate(int64_t{lhs.raw_} + rhs.raw_));
    }
    [[nodiscard]] friend constexpr Fixed32 operator-(
        Fixed32 lhs, Fixed32 rhs) noexcept {
        return fromRaw(saturate(int64_t{lhs.raw_} - rhs.raw_));
    }
    [[nodiscard]] friend constexpr Fixed32 operator-(Fixed32 value) noexcept {
        return fromRaw(value.raw_ == std::numeric_limits<int32_t>::min()
            ? std::numeric_limits<int32_t>::max() : -value.raw_);
    }
    [[nodiscard]] friend constexpr Fixed32 operator*(
        Fixed32 lhs, Fixed32 rhs) noexcept {
        const int64_t product = int64_t{lhs.raw_} * rhs.raw_;
        const uint64_t magnitude = unsignedMagnitude(product);
        const uint64_t rounded = magnitude
            + (uint64_t{1} << (FractionBits - 1u));
        const uint64_t shifted = rounded >> FractionBits;
        if (product < 0) {
            if (shifted >= uint64_t{1} << 31u)
                return fromRaw(std::numeric_limits<int32_t>::min());
            return fromRaw(-static_cast<int32_t>(shifted));
        }
        if (shifted > uint64_t{std::numeric_limits<int32_t>::max()})
            return fromRaw(std::numeric_limits<int32_t>::max());
        return fromRaw(static_cast<int32_t>(shifted));
    }
    [[nodiscard]] friend constexpr Fixed32 operator/(
        Fixed32 lhs, Fixed32 rhs) noexcept {
        if (rhs.raw_ == 0) {
            return fromRaw(lhs.raw_ < 0
                ? std::numeric_limits<int32_t>::min()
                : std::numeric_limits<int32_t>::max());
        }
        const int64_t numerator = int64_t{lhs.raw_}
            * (int64_t{1} << FractionBits);
        const int64_t quotient = numerator / rhs.raw_;
        const int64_t remainder = numerator % rhs.raw_;
        const uint64_t twiceRemainder = unsignedMagnitude(remainder) * 2u;
        const uint64_t denominator = unsignedMagnitude(rhs.raw_);
        int64_t rounded = quotient;
        if (twiceRemainder >= denominator) {
            rounded += ((lhs.raw_ < 0) != (rhs.raw_ < 0)) ? -1 : 1;
        }
        return fromRaw(saturate(rounded));
    }

    constexpr Fixed32& operator+=(Fixed32 rhs) noexcept {
        return *this = *this + rhs;
    }
    constexpr Fixed32& operator-=(Fixed32 rhs) noexcept {
        return *this = *this - rhs;
    }
    constexpr Fixed32& operator*=(Fixed32 rhs) noexcept {
        return *this = *this * rhs;
    }
    constexpr Fixed32& operator/=(Fixed32 rhs) noexcept {
        return *this = *this / rhs;
    }

    [[nodiscard]] constexpr auto operator<=>(const Fixed32&) const = default;

private:
    [[nodiscard]] static constexpr uint64_t unsignedMagnitude(
        int64_t value) noexcept {
        return value < 0
            ? static_cast<uint64_t>(-(value + 1)) + 1u
            : static_cast<uint64_t>(value);
    }

    [[nodiscard]] static constexpr int32_t saturate(int64_t value) noexcept {
        return static_cast<int32_t>(std::clamp(
            value, int64_t{std::numeric_limits<int32_t>::min()},
            int64_t{std::numeric_limits<int32_t>::max()}));
    }

    int32_t raw_ = 0;
};

using Position = Fixed32<12>;
using LinearVelocity = Fixed32<16>;
using AngularVelocity = Fixed32<20>;
using Unit = Fixed32<30>;
using Material = Fixed32<28>;

} // namespace voxy::physics::deterministic
