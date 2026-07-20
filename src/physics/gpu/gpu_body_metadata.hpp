#pragma once

#include <cstdint>

namespace voxy::physics {

// GPU body metadata packs a 20-bit generation and runtime flags into the
// fourth lane. The first three lanes are signed world-sector coordinates.
inline constexpr uint32_t kGpuBodyGenerationMask = 0x000f'ffffu;
inline constexpr uint32_t kGpuBodyAliveFlag = 1u << 20u;
inline constexpr uint32_t kGpuBodyAwakeFlag = 1u << 21u;
inline constexpr uint32_t kGpuBodyBulletFlag = 1u << 22u;
inline constexpr uint32_t kGpuBodyCcdHitFlag = 1u << 23u;
inline constexpr uint32_t kGpuBodyCcdFailureFlag = 1u << 24u;
inline constexpr uint32_t kGpuBodyTerrainMipRejectedFlag = 1u << 25u;
inline constexpr uint32_t kGpuBodySubmergedFlag = 1u << 26u;
inline constexpr uint32_t kGpuBodyTerrainContactShift = 27u;
inline constexpr uint32_t kGpuBodyTerrainContactMask =
    0x0fu << kGpuBodyTerrainContactShift;
inline constexpr uint32_t kGpuBodyKinematicFlag = 1u << 31u;

[[nodiscard]] constexpr uint32_t packGpuBodyMetadata(
    uint32_t generation, uint32_t flags) noexcept {
    return (generation & kGpuBodyGenerationMask)
         | (flags & ~kGpuBodyGenerationMask);
}

} // namespace voxy::physics
