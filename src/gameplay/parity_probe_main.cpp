#include "gameplay/platform_parity.hpp"

#include <cstdio>

int main() {
    const voxy::gameplay::PlatformParityResult result =
        voxy::gameplay::runPlatformParityCorpus();
    std::printf(
        "platform_parity version=%u integer=%u demolition=%u "
        "deadweight=%u wreckwater=%u aggregate=%u\n",
        voxy::gameplay::kPlatformParityCorpusVersion,
        result.integerHash, result.demolitionHash, result.deadweightHash,
        result.wreckwaterHash, result.aggregateHash);
    if (!result.success) {
        std::fputs("platform parity corpus failed to execute\n", stderr);
        return 2;
    }
    if (result.aggregateHash != voxy::gameplay::kExpectedPlatformParityHash) {
        std::fprintf(stderr, "expected aggregate=%u\n",
                     voxy::gameplay::kExpectedPlatformParityHash);
        return 1;
    }
    return 0;
}
