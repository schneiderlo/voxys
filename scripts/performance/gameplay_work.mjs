// Deterministic work ceilings for the quiet creative scene. Wall time remains
// a separate hardware measurement; these checks prevent known work returning.
export function gameplayWork(scenario) {
    const difference = key => (scenario.workAfter[key] || 0) - (scenario.workBefore[key] || 0);
    const ticks = scenario.after.physics.tick - scenario.before.physics.tick;
    const terrainPasses = difference('renderPass:scene_terrain_current_sun');
    const mappedBytes = difference('mappedBytes:physics_debug_readback');
    if (!(ticks > 0 && terrainPasses > 0 && mappedBytes > 0)) {
        throw Error('Missing gameplay work counters or inactive scene');
    }
    return {
        ticks,
        terrainPasses,
        seedPasses: difference('renderPass:opaque_scene_seed'),
        mappedBytesPerTick: mappedBytes / ticks,
        physicsTimingSamplesPerTick: 'resolveInto:physics_stage_timestamp_resolve' in scenario.workAfter
            ? difference('resolveInto:physics_stage_timestamp_resolve') / ticks
            : 'resolve:physics_stage_timestamps' in scenario.workAfter
                ? difference('resolve:physics_stage_timestamps') / ticks : null,
    };
}

export function checkGameplayWork(scenario) {
    const work = gameplayWork(scenario);
    if (work.seedPasses !== 0) throw Error('Redundant opaque scene seed returned');
    if (['idle', 'orbit', 'walk'].includes(scenario.name)) {
        // Event bursts during throwing are deliberately outside this bound.
        // Quiet play used to copy 2.55 MB/tick. Leave room for real small events.
        if (work.mappedBytesPerTick > 64 * 1024) throw Error('Quiet gameplay readback exceeded 64 KiB/tick');
        if (work.physicsTimingSamplesPerTick === null) throw Error('Physics timing counters unavailable');
        if (work.physicsTimingSamplesPerTick > 0.1) throw Error('Routine movement bypassed profiling cadence');
    }
    return work;
}
