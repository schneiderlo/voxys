#include "gameplay/platform_parity.hpp"

#include "gameplay/gameplay_types.hpp"
#include "gameplay/vertical_slices.hpp"

#include <array>

namespace voxy::gameplay {

PlatformParityResult runPlatformParityCorpus() {
    PlatformParityResult result;
    uint32_t integerHash = gameplayHashWord(
        kGameplayHashOffset, kPlatformParityCorpusVersion);
    for (const auto [numerator, denominator] :
         std::array<std::array<int32_t, 2>, 8>{{
             {{5, 2}}, {{-5, 2}}, {{5, -2}}, {{-5, -2}},
             {{1'000'003, 97}}, {{-1'000'003, 97}},
             {{32'767, 60}}, {{-32'768, 60}},
         }}) {
        integerHash = gameplayHashI32(integerHash, saturateI32(
            roundedDivide(numerator, denominator)));
    }
    result.integerHash = integerHash;

    DemolitionLeagueSlice demolition(DemolitionLeagueSlice::Config{
        .roundTicks = 240,
        .significantMassQ16 = 2 * kScalarOne,
    });
    if (!demolition.initialize()
        || !demolition.submitImpact(AssemblyDamageCommand{
            .tick = 1,
            .sequence = 1,
            .source = 1,
            .edgeId = 101,
            .damageQ16 = 4 * kScalarOne,
        })) return result;
    for (uint32_t tick = 0; tick < 180u; ++tick) {
        if (!demolition.step()) return result;
    }
    result.demolitionHash = demolition.stateHash();

    DeadweightSlice deadweight;
    if (!deadweight.initialize()) return result;
    uint64_t sequence = 1;
    while (deadweight.state().outcome == TransportOutcome::Running
           && deadweight.state().tick < 1'800u) {
        const uint64_t tick = deadweight.state().tick + 1u;
        int32_t correctionX = 0;
        if (deadweight.state().cargoPositionQ12[0] > kPositionOne / 8)
            correctionX = -kScalarOne / 4;
        else if (deadweight.state().cargoPositionQ12[0] < -kPositionOne / 8)
            correctionX = kScalarOne / 4;
        if (!deadweight.submitInput(DeadweightInput{
                .tick = tick,
                .sequence = sequence++,
                .clientId = 1,
                .pullXQ16 = correctionX,
                .pullZQ16 = kScalarOne,
            })
            || !deadweight.submitInput(DeadweightInput{
                .tick = tick,
                .sequence = sequence++,
                .clientId = 2,
                .pullXQ16 = correctionX,
                .pullZQ16 = kScalarOne,
            })
            || !deadweight.step()) return result;
    }
    if (deadweight.state().outcome != TransportOutcome::Delivered)
        return result;
    result.deadweightHash = deadweight.state().stateHash;

    WreckwaterSlice wreckwater;
    if (!wreckwater.initialize()) return result;
    const std::array<AssemblyDamageCommand, 4> impacts{{
        {1u, 1u, 1u, 12u, 3 * kScalarOne},
        {1u, 2u, 1u, 14u, 2 * kScalarOne},
        {1u, 3u, 1u, 15u, 2 * kScalarOne},
        {1u, 4u, 1u, 18u, kScalarOne},
    }};
    for (const auto& impact : impacts) {
        if (!wreckwater.submitImpact(impact)) return result;
    }
    for (uint32_t tick = 0; tick < 300u; ++tick) {
        if (!wreckwater.step()) return result;
    }
    result.wreckwaterHash = wreckwater.stateHash();

    uint32_t aggregate = gameplayHashWord(
        kGameplayHashOffset, kPlatformParityCorpusVersion);
    aggregate = gameplayHashWord(aggregate, result.integerHash);
    aggregate = gameplayHashWord(aggregate, result.demolitionHash);
    aggregate = gameplayHashWord(aggregate, result.deadweightHash);
    aggregate = gameplayHashWord(aggregate, result.wreckwaterHash);
    result.aggregateHash = aggregate;
    result.success = true;
    return result;
}

} // namespace voxy::gameplay
