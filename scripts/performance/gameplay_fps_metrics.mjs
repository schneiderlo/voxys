const software = /swiftshader|llvmpipe|lavapipe|software rasterizer|microsoft basic render/i;

// Read at scene boundaries only. Requested clock controls do not prove that
// the renderer actually uses the day/night path or zero direct light.
export const gameplayLightingSettingNames = ['lighting.dayNightEnabled', 'lighting.dayNightPaused',
    'lighting.dayHour', 'lighting.dayCycleMinutes', 'lighting.sunAzimuth', 'lighting.sunElevation',
    'lighting.sunColor.r', 'lighting.sunColor.g', 'lighting.sunColor.b', 'lighting.sunIntensity',
    'lighting.ambientColor.r', 'lighting.ambientColor.g', 'lighting.ambientColor.b', 'lighting.ambientIntensity',
    'lighting.fogColor.r', 'lighting.fogColor.g', 'lighting.fogColor.b', 'lighting.exposure'];

const hourDistance = (a, b) => Math.min((a - b + 24) % 24, (b - a + 24) % 24);

// This guard follows the player clock directly. A healthy physics queue cannot
// hide a paused player simulation or a wrong benchmark clock.
export function makeAdventurePlayerTickGuard(initialTick, initialTime, stallTimeoutMs = 5000) {
    if (!Number.isSafeInteger(initialTick) || initialTick < 0 || !Number.isFinite(initialTime)
        || !(Number.isFinite(stallTimeoutMs) && stallTimeoutMs > 0)) throw Error('Invalid AdventurePlayer clock guard');
    let lastTick = initialTick, lastProgressTime = initialTime;
    return (tick, now) => {
        if (!Number.isSafeInteger(tick) || tick < lastTick) throw Error('Player tick is unavailable or moved backward');
        if (!Number.isFinite(now) || now < lastProgressTime) throw Error('Invalid player clock observation time');
        if (tick > lastTick) {lastTick = tick; lastProgressTime = now;}
        else if (now - lastProgressTime >= stallTimeoutMs) throw Error('AdventurePlayer clock stalled');
        return tick - initialTick;
    };
}

export function distribution(values) {
    const samples = values.filter(Number.isFinite).sort((a, b) => a - b);
    const at = fraction => samples.length ? samples[Math.max(0, Math.ceil(samples.length * fraction) - 1)] : null;
    return {count: samples.length, p50: at(.5), p95: at(.95), p99: at(.99),
        maximum: samples.at(-1) ?? null};
}

export function statisticalMedian(values) {
    const sorted = values.filter(Number.isFinite).sort((a, b) => a - b);
    if (!sorted.length) return null;
    const middle = Math.floor(sorted.length / 2);
    return sorted.length % 2 ? sorted[middle] : sorted[middle - 1] / 2 + sorted[middle] / 2;
}

export function summarizeGameplayFps(sample) {
    const acquiredFrames = sample.after.acquisitions - sample.before.acquisitions;
    const surfaceFrames = sample.after.telemetry.render.surface_acquired_frames
        - sample.before.telemetry.render.surface_acquired_frames;
    const loopFrames = sample.after.telemetry.frame.count - sample.before.telemetry.frame.count;
    const physicsTicks = sample.after.telemetry.physics.tick - sample.before.telemetry.physics.tick;
    const intervals = sample.acquisitionTimes.slice(1).map((time, index) => time - sample.acquisitionTimes[index]);
    return {acquiredFrames, surfaceFrames, loopFrames, physicsTicks,
        uniqueSurfaceTextures: sample.after.uniqueTextures - sample.before.uniqueTextures,
        uniqueSurfaceTurnoverFps: (sample.after.uniqueTextures - sample.before.uniqueTextures) * 1000 / sample.elapsedMs,
        queueSubmissions: sample.after.submissions - sample.before.submissions,
        submittedCommands: sample.after.commands - sample.before.commands,
        acquiredFps: acquiredFrames * 1000 / sample.elapsedMs,
        loopFps: loopFrames * 1000 / sample.elapsedMs,
        acquisitionIntervalMs: distribution(intervals), cpuMs: distribution(sample.cpu),
        gpuMs: distribution(sample.gpuSamples.filter(s => s.frame_interval_available).map(s => s.gpu_frame_ms)),
        terrainRefreshes: sample.after.telemetry.render.terrain_cache_refreshes
            - sample.before.telemetry.render.terrain_cache_refreshes,
        pacingSkips: sample.after.telemetry.frame.pacing_skips - sample.before.telemetry.frame.pacing_skips};
}

export function validateGameplayFps(sample, {width, height, uncapped = true, shaderLabels = []}) {
    const errors = [];
    const summary = summarizeGameplayFps(sample);
    if (!(Number.isFinite(sample.elapsedMs) && sample.elapsedMs > 0)) errors.push('Invalid measurement duration');
    if (!(summary.acquiredFrames >= 30 && summary.acquisitionIntervalMs.count >= 29)) errors.push('Too few acquired canvas frames');
    if (summary.acquiredFrames !== sample.acquisitionTimes.length) errors.push('Acquisition timestamps are incomplete');
    if (summary.surfaceFrames !== summary.acquiredFrames) errors.push('Canvas and engine surface counters disagree');
    if (!Number.isInteger(summary.surfaceFrames) || !Number.isInteger(summary.loopFrames)) errors.push('Missing engine frame counters');
    if (!(summary.physicsTicks > 0)) errors.push('Physics did not advance');
    if (!(summary.queueSubmissions > 0 && summary.submittedCommands >= summary.acquiredFrames)) errors.push('Canvas acquisitions lack GPU submissions');
    if (sample.acquisitionTimes.some((time, i) => !Number.isFinite(time) || (i > 0 && time <= sample.acquisitionTimes[i - 1]))) errors.push('Invalid acquisition clock');
    if (sample.acquisitionTimes.at(-1) - sample.acquisitionTimes[0] > sample.elapsedMs) errors.push('Acquisition timestamps exceed measurement duration');
    for (const state of [sample.before, sample.after]) {
        if (sample.travelVersion >= 5) {
            const lighting = state.rendererLighting;
            if (!gameplayLightingSettingNames.every(key => Number.isFinite(lighting?.[key]))) errors.push('Actual renderer lighting is missing or invalid');
            if (lighting?.['lighting.dayNightEnabled'] !== 1) errors.push('Actual day/night cycle is disabled');
            if (lighting?.['lighting.dayNightPaused'] !== (sample.dayNight ? 0 : 1)) errors.push('Actual day/night pause state differs');
            if (!(lighting?.['lighting.dayHour'] >= 0 && lighting['lighting.dayHour'] < 24)
                || !(lighting?.['lighting.dayCycleMinutes'] > 0) || !(lighting?.['lighting.sunIntensity'] >= 0)) errors.push('Actual day/night values are invalid');
            if (!sample.dayNight && !(hourDistance(lighting?.['lighting.dayHour'], ((sample.clockHour % 24) + 24) % 24) <= 1e-5)) errors.push('Actual fixed hour differs from requested hour');
            if (sample.requireZeroSun && lighting?.['lighting.sunIntensity'] !== 0) errors.push('Requested zero-direct-light path is inactive');
        }
        const adventure = state.adventure;
        if (!Number.isFinite(adventure?.health) || !(adventure.health > 0)
            || !Number.isFinite(adventure?.maxHealth) || adventure.health > adventure.maxHealth) errors.push('Game health is missing or invalid');
        if (adventure?.mode !== 'explore' || adventure?.menu !== ''
            || ['menuTitle', 'menuText', 'menuStatus'].some(key => adventure?.[key] !== '')) errors.push('Gameplay exploration is inactive or a game menu is open');
        const player = state.adventure?.player, camera = state.adventure?.camera;
        if (!['x', 'y', 'z', 'yaw'].every(key => Number.isFinite(player?.[key]))) errors.push('Player pose is missing or invalid');
        if (!Number.isFinite(camera?.yaw) || !(camera?.distance > 0) || !(camera?.requestedDistance > 0)
            || camera?.eye?.length !== 3 || !camera.eye.every(Number.isFinite)) errors.push('Camera pose is missing or invalid');
        if (state.visibility !== 'visible') errors.push('Page is hidden');
        if (state.uncapped !== uncapped) errors.push('Engine loop mode differs from requested mode');
        if (!(state.telemetry.frame.gpu_queue >= 0 && state.telemetry.frame.gpu_queue <= state.telemetry.frame.gpu_queue_limit)) errors.push('GPU queue limit is missing or exceeded');
        if (state.canvas?.width !== width || state.canvas?.height !== height) errors.push('Canvas resolution changed');
        if (state.telemetry?.render?.path !== 'raycast'
            || state.telemetry?.render?.terrain_width !== 8192
            || state.telemetry?.render?.terrain_height !== 8192) errors.push('Production terrain renderer is missing');
        if (!/^webgpu/.test(state.telemetry?.physics?.backend ?? '')) errors.push('WebGPU physics is missing');
        for (const [key, value] of Object.entries(state.telemetry?.physics ?? {})) {
            if (value && typeof value === 'object' && value.overflow) errors.push(`Physics ${key} overflowed`);
        }
        for (const key of ['invalid_manifolds', 'color_conflicts', 'ccd_failures']) {
            if (state.telemetry?.physics?.[key] > 0) errors.push(`Physics ${key} is nonzero`);
        }
        if (state.telemetry?.physics?.islands?.root_errors > 0) errors.push('Physics island root errors');
    }
    if (sample.travelVersion >= 5) {
        const before = sample.before.rendererLighting, after = sample.after.rendererLighting;
        if (!sample.dayNight && gameplayLightingSettingNames.some(key => before?.[key] !== after?.[key])) errors.push('Fixed renderer lighting changed within scene');
        if (sample.dayNight && !(((after?.['lighting.dayHour'] - before?.['lighting.dayHour'] + 24) % 24) > 0)) errors.push('Actual moving day/night hour did not advance');
    }
    if (sample.queueSamples?.some(frame => !(frame.gpu_queue >= 0 && frame.gpu_queue <= frame.gpu_queue_limit))) errors.push('GPU queue bound exceeded during gameplay');
    if (!sample.textureSizes.length || sample.textureSizes.some(size => size.width !== width || size.height !== height)) errors.push('Acquired texture resolution differs');
    if (!sample.devices.length) errors.push('GPU device instrumentation is missing');
    if (sample.devices.some(device => device.fallback || device.adapter?.isFallbackAdapter
        || software.test(JSON.stringify(device)))) errors.push('Software or fallback GPU selected');
    if (sample.deviceProfile?.adapter?.fallback || software.test(JSON.stringify(sample.deviceProfile))) errors.push('Engine reports software GPU');
    if (sample.lost.length || sample.gpuErrors.length || sample.engineHealth?.lost || sample.engineHealth?.errors?.length) errors.push('GPU device loss or uncaptured error');
    if (sample.pageErrors?.length || sample.loadErrors?.length) errors.push('Page or first-party load errors');
    for (const label of shaderLabels) if (!(sample.appliedShaders[label] > 0)) errors.push(`Shader override not applied: ${label}`);
    if (summary.acquiredFps < 2 && sample.after.telemetry.frame.gpu_queue === 0) errors.push('Empty GPU queue with throttled renderer');
    if (!sample.travel?.complete) errors.push('Scripted journey did not complete');
    const playerTicks = Number(sample.after.adventure?.player?.tick) - Number(sample.before.adventure?.player?.tick);
    if (!(Number.isSafeInteger(playerTicks) && playerTicks > 0)) errors.push('AdventurePlayer did not advance');
    if (sample.travel?.clock !== 'AdventurePlayer' || sample.travel?.observedTicks !== playerTicks
        || !(sample.travel?.requestedTicks > 0 && playerTicks >= sample.travel.requestedTicks)) errors.push('Authoritative player journey is incomplete or inconsistent');
    return {ok: errors.length === 0, errors: [...new Set(errors)], summary};
}

// ABBA balances slow drift. This is an experience comparison, not a bit-exact
// physics equivalence oracle: preserve endpoints and correctness separately.
export function compareGameplayFps(rows, regressionPercent = 7.5) {
    if (!(Number.isFinite(regressionPercent) && regressionPercent >= 0 && regressionPercent < 100)) throw Error('Regression allowance must be between 0 and 100 percent');
    const groups = new Map();
    for (const original of rows) {
        const row = {...original, validation: validateGameplayFps(original, {
            width: original.before.canvas.width, height: original.before.canvas.height,
            uncapped: original.uncapped, shaderLabels: original.expectedShaderLabels ?? [],
        })};
        const key = `${row.experience}/${row.scenario}`;
        if (!groups.has(key)) groups.set(key, {baseline: [], candidate: []});
        groups.get(key)[row.variant].push(row);
    }
    return [...groups].map(([workload, variants]) => {
        const median = (rows, metric) => statisticalMedian(rows.map(row => metric(row.validation.summary)));
        const result = {workload, baselineRuns: variants.baseline.length, candidateRuns: variants.candidate.length,
            failures: []};
        if (!variants.baseline.length || variants.baseline.length !== variants.candidate.length) {
            result.failures.push('Incomplete A/B workload'); return result;
        }
        if ([...variants.baseline, ...variants.candidate].some(row => !row.validation.ok)) result.failures.push('Invalid workload cannot support a performance result');
        if ([...variants.baseline, ...variants.candidate].some(row => row.captureInstrumented
            || row.travel?.playerTickMethod !== 'readonly-scalar')) result.failures.push('Diagnostic capture cannot support a clean performance result');
        const powerStatuses = [...variants.baseline, ...variants.candidate].map(row => row.powerLineStatus);
        if (powerStatuses.some(status => status !== undefined) &&
            (powerStatuses.some(status => !['Online', 'Offline'].includes(status)) || new Set(powerStatuses).size !== 1)) {
            result.failures.push('Mismatched or missing Windows power-line status');
        }
        const expected = variants.baseline[0];
        for (const row of [...variants.baseline, ...variants.candidate]) {
            for (const key of ['requestedTicks', 'travelVersion', 'clockHour', 'dayNight', 'uncapped', 'requireZeroSun']) {
                if (row[key] !== expected[key]) result.failures.push(`Mismatched ${key}`);
            }
            for (const key of ['backend', 'arithmetic', 'substeps', 'fixed_tick_seconds', 'maximum_catch_up_ticks', 'broad_phase_cell_size', 'solver_workgroup_size']) {
                if (row.before.telemetry.physics[key] !== expected.before.telemetry.physics[key]) result.failures.push(`Mismatched physics ${key}`);
            }
            if (JSON.stringify(row.devices) !== JSON.stringify(expected.devices)) result.failures.push('Mismatched GPU devices or required features');
            for (const key of ['width', 'height']) {
                if (row.before.canvas[key] !== expected.before.canvas[key] || row.after.canvas[key] !== expected.after.canvas[key]) result.failures.push(`Mismatched canvas ${key}`);
            }
            // Tick polling can cross a boundary. Record the exact path and reject
            // a visible workload mismatch rather than normalizing it away.
            for (const key of ['x', 'y', 'z']) {
                if (Math.abs(row.before.adventure.player[key] - expected.before.adventure.player[key]) > .001) result.failures.push(`Mismatched starting player ${key}`);
                if (Math.abs(row.after.adventure.player[key] - expected.after.adventure.player[key]) > .5) result.failures.push(`Mismatched final player ${key}`);
            }
            if (row.travel.clock !== expected.travel.clock || row.travel.driverPollMs !== expected.travel.driverPollMs) result.failures.push('Mismatched authoritative journey clock or polling cadence');
            if (row.travel.playerTickMethod !== expected.travel.playerTickMethod) result.failures.push('Mismatched player tick reader cost');
            if (row.travel.playerStallTimeoutMs !== expected.travel.playerStallTimeoutMs) result.failures.push('Mismatched player stall guard');
            for (const key of ['yaw', 'distance']) {
                if (Math.abs(row.before.adventure.camera[key] - expected.before.adventure.camera[key]) > .001) result.failures.push(`Mismatched starting camera ${key}`);
            }
            if (Math.abs(row.after.adventure.camera.yaw - expected.after.adventure.camera.yaw) > .01) result.failures.push('Mismatched final camera yaw');
            if (Math.abs(row.after.adventure.camera.distance - expected.after.adventure.camera.distance) > .5) result.failures.push('Mismatched final camera distance');
            if (Math.abs(row.after.adventure.player.yaw - expected.after.adventure.player.yaw) > .01) result.failures.push('Mismatched final player yaw');
            for (const endpoint of ['before', 'after']) {
                if (row[endpoint].adventure.camera.requestedDistance !== expected[endpoint].adventure.camera.requestedDistance) result.failures.push('Mismatched requested camera distance');
                for (let axis = 0; axis < 3; ++axis) {
                    if (Math.abs(row[endpoint].adventure.camera.eye?.[axis] - expected[endpoint].adventure.camera.eye?.[axis]) > (endpoint === 'before' ? .001 : .5)) result.failures.push(`Mismatched ${endpoint === 'before' ? 'starting' : 'final'} camera eye`);
                }
            }
            for (const endpoint of ['before', 'after']) {
                if (row.travelVersion >= 5) {
                    const lightingKeys = row.dayNight
                        ? ['lighting.dayNightEnabled', 'lighting.dayNightPaused', 'lighting.dayCycleMinutes'] : gameplayLightingSettingNames;
                    for (const key of lightingKeys) if (row[endpoint].rendererLighting?.[key] !== expected[endpoint].rendererLighting?.[key]) result.failures.push(`Mismatched ${endpoint} renderer ${key}`);
                }
                for (const key of ['health', 'maxHealth', 'mode', 'menu', 'menuTitle', 'menuText', 'menuStatus']) {
                    if (row[endpoint].adventure[key] !== expected[endpoint].adventure[key]) result.failures.push(`Mismatched ${endpoint} game ${key}`);
                }
            }
            if (row.after.telemetry.physics.bodies.current !== expected.after.telemetry.physics.bodies.current) result.failures.push('Mismatched resident body counts');
        }
        const metrics = {acquiredFps: summary => summary.acquiredFps,
            intervalP95Ms: summary => summary.acquisitionIntervalMs.p95,
            intervalP99Ms: summary => summary.acquisitionIntervalMs.p99};
        for (const [name, metric] of Object.entries(metrics)) {
            const baseline = median(variants.baseline, metric), candidate = median(variants.candidate, metric);
            result[name] = {baseline, candidate, changePercent: (candidate / baseline - 1) * 100};
            if (!(baseline > 0 && candidate > 0)) result.failures.push(`Invalid ${name}`);
            else if (name === 'acquiredFps' ? candidate < baseline * (1 - regressionPercent / 100)
                : candidate > baseline * (1 + regressionPercent / 100)) result.failures.push(`${name} regressed beyond ${regressionPercent}%`);
        }
        result.failures = [...new Set(result.failures)];
        return result;
    });
}
