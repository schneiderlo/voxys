#pragma once

#include "game/adventure/adventure_session.hpp"
#include "game/adventure/spatial_queries.hpp"

namespace voxy::game::adventure {

// Accumulating allowance for ONE field-home readiness evaluation, including
// every candidate bed and both furniture kinds. Pass the same object to every
// call; the helper never resets it. Exhaustion conservatively means unproved.
struct FieldHomeAccessWork {
    static constexpr size_t maximumBedChecks=8,maximumFurnitureChecks=32;
    static constexpr size_t maximumApproaches=24,maximumSampledColumns=1024;
    static constexpr size_t maximumControllerTicks=8192,maximumNavigationSteps=512;
    size_t bedChecks=0,furnitureChecks=0,approaches=0,sampledColumns=0;
    size_t controllerTicks=0,navigationSteps=0;
    bool exhausted=false;
};

// Uses the owned usable state.registeredBed in this structure as its recovery
// anchor. The caller can select a candidate bed in a private state copy without
// registering it in the save. Chest and bench must use that SAME anchor.
//
// Success requires the ordinary 3.2 m furniture interaction from either that
// safe recovery pose or a pose reached by the actual AdventurePlayer solver.
// Search is local: a snapped 16 x 16 m area around the bed, from .64 m below
// to 3.84 m above its floor. No jumping, swimming or teleport-based proof.
// Unsupported/blocked/over-capacity columns and exhausted budgets fail closed;
// this deliberately does not promise to recognize every large usable house.
// Accepted state/geometry must correspond. No caller state or queries change.
// Cache the result with accepted geometry rather than calling every HUD frame.
[[nodiscard]] bool fieldFurnitureAccessible(const AdventureState&,
    const AdventureSpatialQueries&,uint64_t structureId,FurnitureKind,
    FieldHomeAccessWork* work=nullptr);

} // namespace voxy::game::adventure
