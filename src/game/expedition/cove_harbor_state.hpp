#pragma once
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace voxy::game::expedition {
inline constexpr uint32_t kCoveHarborLiftProfile=1;
inline constexpr size_t kCoveHarborLiftLines=4;
inline constexpr float kCoveHarborLiftMinimumLength=2.5f;
inline constexpr float kCoveHarborLiftMaximumLength=12.f;
inline constexpr float kCoveHarborLiftReelSpeed=.5f;
inline constexpr float kCoveHarborLiftLowerSpeed=.25f;
inline constexpr float kCoveHarborLiftLineForce=30000.f;
inline constexpr float kCoveHarborLiftBreakForce=45000.f;
inline constexpr float kCoveHarborLiftCompliance=5e-6f; // 200 kN/m axial sling stiffness.

enum class CoveHarborLiftMode:uint8_t { Detached,Attached,Broken };
struct CoveHarborLiftState {
    // Zero preserves the exact pre-hoist world. Profile 1 installs the fixed
    // service even when detached/unpowered; installation needs host migration.
    uint32_t profile=0;
    CoveHarborLiftMode mode=CoveHarborLiftMode::Detached;
    // Canonical zero when detached. Persist actual per-line lengths, never a
    // commanded ideal length, motor speed, backend handle or adjustable force.
    std::array<float,kCoveHarborLiftLines> lengths{};
    uint8_t brokenMask=0; // One bit per line; nonzero only in Broken mode.
    [[nodiscard]] bool operator==(const CoveHarborLiftState&) const = default;
};
[[nodiscard]] inline bool validCoveHarborLiftState(const CoveHarborLiftState& state) noexcept {
    if(state.profile>kCoveHarborLiftProfile||(state.profile==0&&state.mode!=CoveHarborLiftMode::Detached)
        ||state.mode>CoveHarborLiftMode::Broken||state.brokenMask>15
        ||(state.mode==CoveHarborLiftMode::Broken)!=(state.brokenMask!=0))return false;
    for(float length:state.lengths)if(!std::isfinite(length)
        || (state.mode==CoveHarborLiftMode::Detached?length!=0:
            length<kCoveHarborLiftMinimumLength||length>kCoveHarborLiftMaximumLength))return false;
    return true;
}

} // namespace voxy::game::expedition
