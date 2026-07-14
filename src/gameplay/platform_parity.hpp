#pragma once

#include <cstdint>

namespace voxy::gameplay {

// Increment only when a deliberately reviewed authoritative gameplay change
// updates the frozen corpus below.
inline constexpr uint32_t kPlatformParityCorpusVersion = 1;
inline constexpr uint32_t kExpectedPlatformParityHash = 714'369'668u;

struct PlatformParityResult {
    uint32_t integerHash = 0;
    uint32_t demolitionHash = 0;
    uint32_t deadweightHash = 0;
    uint32_t wreckwaterHash = 0;
    uint32_t aggregateHash = 0;
    bool success = false;
};

[[nodiscard]] PlatformParityResult runPlatformParityCorpus();

} // namespace voxy::gameplay
