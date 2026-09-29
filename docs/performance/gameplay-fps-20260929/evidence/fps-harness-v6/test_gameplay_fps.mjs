import {test} from 'node:test';
import assert from 'node:assert/strict';
import vm from 'node:vm';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import {createHash} from 'node:crypto';
import {spawnSync} from 'node:child_process';
import {fileURLToPath} from 'node:url';
import {installCanvasFpsProbe} from './canvas_fps_probe.mjs';
import {distribution, summarizeGameplayFps, validateGameplayFps, compareGameplayFps, makeAdventurePlayerTickGuard, statisticalMedian, gameplayLightingSettingNames} from './gameplay_fps_metrics.mjs';
import {summarizeGameplayFpsWork, checkFreshFrontierHudWork} from './gameplay_fps_work.mjs';
import {inspectGameplaySite, readGameplayShaderOverrides, readGameplaySymbolMap, scriptedOrbitMouseX} from './gameplay_fps_inputs.mjs';

function fixture() {
    const state = (count, tick) => ({acquisitions: count, uniqueTextures: count / 2, submissions: count,
        commands: count, visibility: 'visible', uncapped: true,
        canvas: {width: 1280, height: 720}, adventure: {health: 100, maxHealth: 100, mode: 'explore', menu: '', menuTitle: '', menuText: '', menuStatus: '', player: {x: 1, y: 2, z: 3, yaw: .4, tick: count === 100 ? '1000' : '1600'},
            camera: {yaw: .4, distance: 18, requestedDistance: 18, eye: [10, 11, 12]}},
        telemetry: {frame: {count, pacing_skips: 0, gpu_queue: 2, gpu_queue_limit: 3}, render: {surface_acquired_frames: count,
            path: 'raycast', terrain_width: 8192, terrain_height: 8192, terrain_cache_refreshes: count},
            physics: {tick, backend: 'webgpu_soft', bodies: {current: 3, overflow: false},
                arithmetic: 'fast_float', substeps: 4, fixed_tick_seconds: 1 / 60}}});
    return {before: state(100, 20), after: state(200, 80), elapsedMs: 1000,
        acquisitionTimes: Array.from({length: 100}, (_, i) => i * 10), textureSizes: [{width: 1280, height: 720}],
        cpu: [1, 2, 3], gpuSamples: [], devices: [{adapter: {vendor: 'intel', description: 'Intel Iris Xe'}, fallback: false}],
        lost: [], gpuErrors: [], pageErrors: [], loadErrors: [], appliedShaders: {},
        travel: {complete: true, clock: 'AdventurePlayer', requestedTicks: 600, observedTicks: 600,
            driverPollMs: 8, playerStallTimeoutMs: 5000, playerTickMethod: 'readonly-scalar'}};
}
const config = {width: 1280, height: 720};
test('canvas frame times use acquired textures independently of loop and RAF counts', () => {
    const sample = fixture(); sample.after.telemetry.frame.count = 400;
    const result = validateGameplayFps(sample, config);
    assert.equal(result.ok, true);
    assert.equal(result.summary.acquiredFps, 100);
    assert.equal(result.summary.loopFps, 300);
    assert.equal(result.summary.acquisitionIntervalMs.p95, 10);
    assert.equal(result.summary.gpuMs.count, 0);
});
test('nearest rank percentiles retain tail stalls', () => {
    const values = Array.from({length: 100}, (_, i) => i < 94 ? 1 : 20);
    assert.deepEqual(distribution(values), {count: 100, p50: 1, p95: 20, p99: 20, maximum: 20});
});
test('every invalid hardware or workload condition fails independently', () => {
    const mutate = [
        sample => {sample.devices = [];},
        sample => {sample.devices[0].fallback = true;},
        sample => {sample.devices[0].adapter.architecture = 'swiftshader';},
        sample => {sample.devices[0].adapter.description = 'llvmpipe';},
        sample => {sample.deviceProfile = {adapter: {fallback: true}};},
        sample => {sample.lost.push({reason: 'unknown'});},
        sample => {sample.gpuErrors.push('bad shader');},
        sample => {sample.pageErrors.push('exception');},
        sample => {sample.loadErrors.push('404');},
        sample => {sample.after.canvas.width = 640;},
        sample => {sample.textureSizes[0].height = 360;},
        sample => {sample.textureSizes = [];},
        sample => {sample.after.visibility = 'hidden';},
        sample => {sample.after.uncapped = false;},
        sample => {sample.after.telemetry.render.terrain_width = 1024;},
        sample => {sample.after.telemetry.physics.backend = 'jolt_legacy';},
        sample => {sample.after.telemetry.physics.tick = 20;},
        sample => {sample.after.telemetry.physics.bodies.overflow = true;},
        sample => {sample.after.telemetry.physics.invalid_manifolds = 1;},
        sample => {sample.after.telemetry.physics.islands = {root_errors: 1};},
        sample => {sample.travel.complete = false;},
        sample => {sample.after.adventure.player.tick = sample.before.adventure.player.tick;},
        sample => {sample.travel.observedTicks = 599;},
        sample => {sample.travel.requestedTicks = 601;},
        sample => {sample.travel.clock = 'physics';},
        sample => {sample.after.adventure.camera.eye[0] = NaN;},
        sample => {sample.after.adventure.camera.distance = 0;},
        sample => {sample.after.adventure.player.yaw = undefined;},
        sample => {sample.after.adventure.health = 0;},
        sample => {sample.after.adventure.health = NaN;},
        sample => {sample.after.adventure.mode = 'pause';},
        sample => {sample.after.adventure.menu = 'Main';},
        sample => {sample.after.adventure.menuTitle = 'Paused';},
        sample => {sample.after.submissions = sample.before.submissions;},
        sample => {sample.after.commands = sample.before.commands;},
        sample => {sample.after.telemetry.frame.gpu_queue = 4;},
        sample => {sample.acquisitionTimes.pop();},
        sample => {sample.acquisitionTimes[20] = sample.acquisitionTimes[19];},
        sample => {delete sample.after.telemetry.render.surface_acquired_frames;},
    ];
    for (const change of mutate) {
        const sample = fixture(); change(sample);
        assert.equal(validateGameplayFps(sample, config).ok, false, change.toString());
    }
});
test('missing shader overrides cannot produce a passing A/B result', () => {
    assert.equal(validateGameplayFps(fixture(), {...config, shaderLabels: ['terrain_blit']}).ok, false);
    const sample = fixture(); sample.appliedShaders.terrain_blit = 1;
    assert.equal(validateGameplayFps(sample, {...config, shaderLabels: ['terrain_blit']}).ok, true);
});

function row(variant = 'baseline') {
    const sample = fixture();
    Object.assign(sample, {variant, experience: 'frontier', scenario: 'idle', requestedTicks: 600,
        travelVersion: 1, clockHour: 16, dayNight: false, uncapped: true});
    sample.validation = validateGameplayFps(sample, config);
    return sample;
}
function lightingRow(variant = 'baseline', moving = false, hour = 16) {
    const sample = row(variant);
    sample.travelVersion = 5; sample.dayNight = moving; sample.clockHour = hour;
    sample.requireZeroSun = false;
    for (const state of [sample.before, sample.after]) {
        state.rendererLighting = Object.fromEntries(gameplayLightingSettingNames.map(key => [key, 1]));
        Object.assign(state.rendererLighting, {'lighting.dayNightEnabled':1, 'lighting.dayNightPaused':moving ? 0 : 1,
            'lighting.dayHour':hour % 24, 'lighting.dayCycleMinutes':120, 'lighting.sunIntensity':1.7});
    }
    if (moving) sample.after.rendererLighting['lighting.dayHour'] = (hour + .01) % 24;
    return sample;
}
test('actual fixed lighting must confirm accepted controls and stable endpoints', () => {
    assert.equal(validateGameplayFps(lightingRow(), config).ok, true);
    for (const mutate of [
        sample => {delete sample.before.rendererLighting;},
        sample => {sample.before.rendererLighting['lighting.sunIntensity'] = NaN;},
        sample => {sample.after.rendererLighting['lighting.dayNightEnabled'] = 0;},
        sample => {sample.before.rendererLighting['lighting.dayNightPaused'] = 0;},
        sample => {sample.after.rendererLighting['lighting.dayHour'] = 0;},
        sample => {sample.after.rendererLighting['lighting.dayCycleMinutes'] = 0;},
        sample => {sample.after.rendererLighting['lighting.sunIntensity'] = -1;},
        sample => {sample.after.rendererLighting['lighting.sunElevation'] += .1;},
    ]) {
        const sample = lightingRow(); mutate(sample);
        assert.equal(validateGameplayFps(sample, config).ok, false, mutate.toString());
    }
});
test('fixed hour wraps midnight and moving lighting must actually advance', () => {
    assert.equal(validateGameplayFps(lightingRow('baseline', false, 24), config).ok, true);
    const moving = lightingRow('baseline', true, 23.995);
    moving.after.rendererLighting['lighting.sunIntensity'] = .28;
    assert.equal(validateGameplayFps(moving, config).ok, true);
    moving.after.rendererLighting['lighting.dayHour'] = moving.before.rendererLighting['lighting.dayHour'];
    assert.equal(validateGameplayFps(moving, config).ok, false);
});
test('ordinary moonlit midnight does not satisfy an exact zero-sun guard', () => {
    const sample = lightingRow('baseline', false, 0); sample.requireZeroSun = true;
    for (const state of [sample.before, sample.after]) state.rendererLighting['lighting.sunIntensity'] = .28;
    assert.match(validateGameplayFps(sample, config).errors.join(';'), /zero-direct-light path is inactive/);
    for (const state of [sample.before, sample.after]) state.rendererLighting['lighting.sunIntensity'] = 0;
    assert.equal(validateGameplayFps(sample, config).ok, true);
    sample.before.rendererLighting['lighting.sunIntensity'] = Number.MIN_VALUE;
    assert.equal(validateGameplayFps(sample, config).ok, false);
});
test('fixed renderer lighting parity is required between otherwise identical variants', () => {
    const baseline = lightingRow(), candidate = lightingRow('candidate');
    assert.deepEqual(compareGameplayFps([baseline, candidate])[0].failures, []);
    for (const state of [candidate.before, candidate.after]) state.rendererLighting['lighting.sunColor.r'] = .9;
    assert.equal(validateGameplayFps(candidate, config).ok, true);
    assert.match(compareGameplayFps([baseline, candidate])[0].failures.join(';'), /Mismatched .* renderer lighting.sunColor.r/);
});
test('Chrome profile root cannot escape the selected output directory', () => {
    const temp = fs.mkdtempSync(path.join(os.tmpdir(), 'voxys-profile-boundary-test-'));
    try {
        const output = path.join(temp, 'output'), outside = path.join(temp, 'outside');
        const result = spawnSync(process.execPath, [fileURLToPath(new URL('benchmark_gameplay_fps.mjs', import.meta.url)),
            '--chrome=unused', '--site=unused', `--output=${output}`, `--profile-root=${outside}`], {encoding:'utf8'});
        assert.notEqual(result.status, 0);
        assert.match(result.stderr, /--profile-root must be inside the output directory/);
        assert.equal(fs.existsSync(outside), false);
    } finally {fs.rmSync(temp, {recursive:true, force:true});}
});
test('moving lighting retains actual phase while requiring matched cycle settings', () => {
    const baseline = lightingRow('baseline', true), candidate = lightingRow('candidate', true);
    assert.deepEqual(compareGameplayFps([baseline, candidate])[0].failures, []);
    for (const state of [candidate.before, candidate.after]) state.rendererLighting['lighting.dayCycleMinutes'] = 60;
    assert.match(compareGameplayFps([baseline, candidate])[0].failures.join(';'), /Mismatched .* renderer lighting.dayCycleMinutes/);
});
test('balanced repeated A/B results accept a parity-preserving speedup', () => {
    const baseline = row(), candidate = row('candidate'); candidate.elapsedMs = 800;
    candidate.acquisitionTimes = candidate.acquisitionTimes.map(time => time * .8);
    candidate.validation = validateGameplayFps(candidate, config);
    const result = compareGameplayFps([baseline, candidate, candidate, baseline])[0];
    assert.deepEqual(result.failures, []);
    assert.equal(result.acquiredFps.changePercent, 25);
});
test('comparison rejects missing repeats, changed physics and changed scene pose', () => {
    assert.ok(compareGameplayFps([row()])[0].failures.length);
    for (const mutate of [
        sample => {sample.requestedTicks = 300;},
        sample => {sample.before.telemetry.physics.substeps = 2;},
        sample => {sample.after.adventure.player.x += 1;},
        sample => {sample.after.adventure.camera.yaw += .1;},
        sample => {sample.before.adventure.camera.yaw += .01;},
        sample => {sample.before.adventure.camera.distance += .01;},
        sample => {sample.before.adventure.camera.eye[0] += .01;},
        sample => {sample.after.adventure.camera.eye[0] += 1;},
        sample => {sample.after.adventure.camera.requestedDistance += 1;},
        sample => {sample.after.adventure.player.yaw += .1;},
        sample => {sample.after.telemetry.physics.bodies.current += 1;},
        sample => {sample.travel.driverPollMs = 100;},
        sample => {sample.travel.playerTickMethod = 'full-state-json-diagnostic';},
        sample => {sample.travel.playerStallTimeoutMs = 6000;},
        sample => {sample.after.adventure.health -= 1;},
        sample => {sample.before.adventure.maxHealth += 1;},
    ]) {
        const candidate = row('candidate'); mutate(candidate);
        assert.ok(compareGameplayFps([row(), candidate])[0].failures.length, mutate.toString());
    }
});
test('authoritative player clock stalls fail even while another clock advances', () => {
    const check = makeAdventurePlayerTickGuard(100, 0);
    assert.equal(check(100, 4999), 0);
    assert.throws(() => check(100, 5000), /Player clock stalled/);
    const progressing = makeAdventurePlayerTickGuard(100, 0);
    assert.equal(progressing(101, 4999), 1);
    assert.equal(progressing(101, 9998), 1);
    assert.equal(progressing(105, 9999), 5);
    assert.throws(() => progressing(104, 10000), /moved backward/);
    assert.throws(() => makeAdventurePlayerTickGuard(NaN, 0), /Invalid/);
    assert.throws(() => makeAdventurePlayerTickGuard(100, 0)(NaN, 1), /unavailable/);
});
test('bounded orbit returns to the starting coordinate and holds the final player ticks', () => {
    assert.equal(scriptedOrbitMouseX(0, 1280), 640);
    for (let step = 0; step < 120; ++step) {
        assert.ok(Math.abs(scriptedOrbitMouseX(step, 1280) - 640) <= 100);
        if (step >= 110) assert.equal(scriptedOrbitMouseX(step, 1280), 640);
    }
});
test('comparison rejects FPS and tail-latency regressions independently', () => {
    for (const metric of ['acquiredFps', 'intervalP95', 'intervalP99']) {
        const candidate = row('candidate');
        if (metric === 'acquiredFps') candidate.elapsedMs = 1200;
        else {
            for (let i = 94; i < 100; ++i) candidate.acquisitionTimes[i] += (i - 93) * 20;
            candidate.elapsedMs += 120;
        }
        candidate.validation = validateGameplayFps(candidate, config);
        assert.ok(compareGameplayFps([row(), candidate])[0].failures.length);
    }
});
test('comparison recomputes raw counter validity instead of trusting a stale summary', () => {
    const candidate = row('candidate');
    delete candidate.after.telemetry.render.surface_acquired_frames;
    assert.ok(compareGameplayFps([row(), candidate])[0].failures.includes('Invalid workload cannot support a performance result'));
    for (const value of [-1, 100, Infinity, NaN]) assert.throws(() => compareGameplayFps([], value));
});
test('work report separates label-specific HUD uploads and scene draws', () => {
    const sample = fixture(); sample.validation = {summary: summarizeGameplayFps(sample)};
    sample.workBefore = {'write:cove_hud_quads': 10, 'writeBytes:cove_hud_quads': 100};
    sample.workAfter = {'write:cove_hud_quads': 12, 'writeBytes:cove_hud_quads': 900,
        'write:adventure_hud_triangles': 4, 'writeBytes:adventure_hud_triangles': 1000,
        'drawIndexed:forest': 100, 'dispatch:terrain': 10};
    const work = summarizeGameplayFpsWork(sample);
    assert.equal(work.hud[0].writes, 2); assert.equal(work.hud[0].bytes, 800);
    assert.equal(work.hud[1].writes, 4); assert.equal(work.indexedDraws[0].perFrame, 1);
    assert.equal(work.dispatches[0].count, 10);
});
test('fresh Frontier idle guard rejects recurring HUD work and missing instrumentation', () => {
    const sample = row(); sample.freshWorldIdle = true;
    sample.workBefore = {'write:cove_hud_quads': 10, 'writeBytes:cove_hud_quads': 100,
        'write:adventure_hud_triangles': 10, 'writeBytes:adventure_hud_triangles': 100,
        'bufferCreated:cove_hud_quads': 2, 'bufferCreated:adventure_hud_triangles': 1};
    sample.workAfter = {...sample.workBefore};
    assert.equal(checkFreshFrontierHudWork(sample).uploads.cove_hud_quads.writes, 0);
    for (const label of ['cove_hud_quads', 'adventure_hud_triangles']) {
        const failure = structuredClone(sample); failure.workAfter[`write:${label}`] += 1;
        assert.throws(() => checkFreshFrontierHudWork(failure), /rebuilt/);
    }
    const emptyTriangles = structuredClone(sample);
    for (const counts of [emptyTriangles.workBefore, emptyTriangles.workAfter]) {
        delete counts['write:adventure_hud_triangles']; delete counts['writeBytes:adventure_hud_triangles'];
    }
    assert.equal(checkFreshFrontierHudWork(emptyTriangles).uploads.adventure_hud_triangles.writes, 0);
    delete emptyTriangles.workBefore['bufferCreated:adventure_hud_triangles'];
    assert.throws(() => checkFreshFrontierHudWork(emptyTriangles), /buffer instrumentation/);
    const missing = structuredClone(sample); missing.workBefore = {}; missing.workAfter = {};
    assert.throws(() => checkFreshFrontierHudWork(missing), /Missing initial/);
    const moved = structuredClone(sample); moved.after.adventure.player.x += 1;
    assert.throws(() => checkFreshFrontierHudWork(moved), /stationary/);
});
test('probe counts all canvas acquisition calls and records unique texture turnover separately', async () => {
    const canvas = {}, texture = {width: 1280, height: 720};
    class Canvas {
        constructor(element) {this.canvas = element;}
        getCurrentTexture() {return texture;}
    }
    class Device {createShaderModule(value) {return value;}}
    class Queue {submit() {}}
    class Adapter {
        constructor() {this.info = {vendor: 'intel', architecture: 'xe', description: 'Intel'};}
        async requestDevice() {return {features: new Set(), lost: new Promise(() => {}), addEventListener() {}};}
    }
    let time = 0;
    const context = vm.createContext({GPUCanvasContext: Canvas, GPUAdapter: Adapter, GPUDevice: Device, GPUQueue: Queue,
        document: {getElementById: () => canvas}, performance: {now: () => ++time}});
    vm.runInContext(`(${installCanvasFpsProbe.toString()})({'test': {code: 'replacement', expectedCode: 'old', expectedSha256: 'hash'}})`, context);
    const probe = context.voxyCanvasFps;
    probe.measuring = true;
    const main = new Canvas(canvas), other = new Canvas({});
    assert.equal(main.getCurrentTexture(), texture); assert.equal(main.getCurrentTexture(), texture);
    other.getCurrentTexture();
    assert.equal(probe.acquisitions, 2); assert.equal(probe.uniqueTextures, 1); assert.equal(probe.duplicates, 1);
    assert.equal(probe.times.length, 2); assert.equal(probe.uniqueTimes.length, 1);
    new Queue().submit([{}, {}]); assert.equal(probe.submissions, 1); assert.equal(probe.commands, 2);
    assert.equal(new Device().createShaderModule({label: 'test', code: 'old'}).code, 'replacement');
    assert.equal(new Device().createShaderModule({label: 'other', code: 'old'}).code, 'old');
    assert.equal(probe.shaders.test, 1);
    assert.throws(() => new Device().createShaderModule({label: 'test', code: 'different'}), /expected SHA-256/);
    assert.equal(probe.shaders.test, 1);
    await new Adapter().requestDevice(); assert.equal(probe.devices.length, 1);
});
test('site inputs accept both complete binary names and reject missing data or ambiguous builds', () => {
    const directory = fs.mkdtempSync(path.join(os.tmpdir(), 'voxys-fps-input-test-'));
    try {
        fs.writeFileSync(path.join(directory, 'index.html'), 'local page');
        for (const stem of ['voxy_wasm', 'voxy_wasm_cc']) {
            fs.writeFileSync(path.join(directory, stem + '.js'), stem + '.data');
            fs.writeFileSync(path.join(directory, stem + '.wasm'), 'wasm');
            assert.throws(() => inspectGameplaySite(directory), /Missing required staged data/);
            fs.writeFileSync(path.join(directory, stem + '.data'), 'data');
            assert.equal(inspectGameplaySite(directory).stem, stem);
            for (const suffix of ['.js', '.wasm', '.data']) fs.unlinkSync(path.join(directory, stem + suffix));
        }
        for (const stem of ['voxy_wasm', 'voxy_wasm_cc']) for (const suffix of ['.js', '.wasm', '.data']) fs.writeFileSync(path.join(directory, stem + suffix), 'file');
        assert.throws(() => inspectGameplaySite(directory), /Expected one complete/);
    } finally {fs.rmSync(directory, {recursive: true, force: true});}
});
test('shader manifests require a frozen expected source and verify its recorded hash', () => {
    const directory = fs.mkdtempSync(path.join(os.tmpdir(), 'voxys-fps-shader-test-'));
    try {
        fs.writeFileSync(path.join(directory, 'candidate.wgsl'), 'candidate');
        fs.writeFileSync(path.join(directory, 'original.wgsl'), 'original');
        const manifest = path.join(directory, 'manifest.json');
        fs.writeFileSync(manifest, JSON.stringify({'ray_blit.wgsl': 'candidate.wgsl'}));
        assert.throws(() => readGameplayShaderOverrides(manifest), /requires source and expectedSource/);
        const entry = {source: 'candidate.wgsl', expectedSource: 'original.wgsl', expectedSha256: 'wrong'};
        fs.writeFileSync(manifest, JSON.stringify({'ray_blit.wgsl': entry}));
        assert.throws(() => readGameplayShaderOverrides(manifest), /hash differs/);
        entry.expectedSha256 = createHash('sha256').update('original').digest('hex');
        fs.writeFileSync(manifest, JSON.stringify({'ray_blit.wgsl': entry}));
        const result = readGameplayShaderOverrides(manifest);
        assert.equal(result.overrides['ray_blit.wgsl'].expectedCode, 'original');
        assert.equal(result.identities['ray_blit.wgsl'].expectedSha256, entry.expectedSha256);
    } finally {fs.rmSync(directory, {recursive: true, force: true});}
});
test('separate-capture CLI rejects browser drift and instrumented timing comparisons', () => {
    const directory = fs.mkdtempSync(path.join(os.tmpdir(), 'voxys-fps-compare-test-'));
    try {
        const capture = () => ({complete: true, configuration: {width: 1280, height: 720, repeats: 1,
            ticks: 600, warmupTicks: 600, experiences: ['frontier'], scenarios: ['idle'],
            uncapped: true, instrumented: false, profiling: false, clockHour: 16, dayNight: false},
            browser: {product: 'Chrome/test'}, chromeArguments: ['--headless=new'],
            system: {gpu: {devices: [{deviceString: 'Intel', driverVersion: 'test'}]}}, rows: [row()]});
        const baseline = path.join(directory, 'baseline.json'), candidate = path.join(directory, 'candidate.json');
        const output = path.join(directory, 'comparison.json');
        fs.writeFileSync(baseline, JSON.stringify(capture()));
        const script = fileURLToPath(new URL('./compare_gameplay_fps.mjs', import.meta.url));
        const run = value => {
            fs.writeFileSync(candidate, JSON.stringify(value));
            return spawnSync(process.execPath, [script, `--baseline=${baseline}`, `--candidate=${candidate}`, `--output=${output}`], {encoding: 'utf8'});
        };
        assert.equal(run(capture()).status, 0);
        const drift = capture(); drift.browser.product = 'Chrome/other';
        assert.equal(run(drift).status, 2);
        assert.ok(JSON.parse(fs.readFileSync(output, 'utf8')).failures.includes('Mismatched Chrome version'));
        const diagnostic = capture(); diagnostic.configuration.instrumented = true;
        assert.equal(run(diagnostic).status, 2);
        assert.match(fs.readFileSync(output, 'utf8'), /clean timing required/);
    } finally {fs.rmSync(directory, {recursive: true, force: true});}
});
test('CPU profile symbol metadata requires the exact captured WASM hash', () => {
    const directory = fs.mkdtempSync(path.join(os.tmpdir(), 'voxys-fps-symbol-test-'));
    try {
        const filename = path.join(directory, 'symbols.map'); fs.writeFileSync(filename, '123:some_function');
        assert.equal(readGameplaySymbolMap(undefined, undefined, 'wasm-hash').matched, false);
        assert.throws(() => readGameplaySymbolMap(filename, 'old-wasm-hash', 'wasm-hash'), /matching captured WASM/);
        const result = readGameplaySymbolMap(filename, 'wasm-hash', 'wasm-hash');
        assert.equal(result.matched, true); assert.equal(result.wasmSha256, 'wasm-hash');
        assert.equal(result.sha256, createHash('sha256').update('123:some_function').digest('hex'));
    } finally {fs.rmSync(directory, {recursive: true, force: true});}
});

test('run aggregates use midpoint medians while interval percentiles keep nearest ranks', () => {
    assert.equal(statisticalMedian([10, 20]), 15);
    assert.equal(statisticalMedian([10, 20, 30]), 20);
    assert.equal(statisticalMedian([1, 2, 3, 4, 5, 6]), 3.5);
    assert.equal(statisticalMedian([]), null);
    assert.equal(distribution([10, 20]).p50, 10);
    const baselineSlow = row(); baselineSlow.elapsedMs = 2000;
    const result = compareGameplayFps([row(), baselineSlow, row('candidate'), row('candidate')])[0];
    assert.equal(result.acquiredFps.baseline, 75);
    assert.equal(result.acquiredFps.candidate, 100);
});

test('aggregate comparison rejects changed or missing recorded Windows power status', () => {
    const baseline=row(); baseline.powerLineStatus='Online';
    const candidate=row('candidate'); candidate.powerLineStatus='Online';
    assert.deepEqual(compareGameplayFps([baseline,candidate])[0].failures,[]);
    for (const status of ['Offline', undefined, 'Unknown']) {
        candidate.powerLineStatus=status;
        assert.ok(compareGameplayFps([baseline,candidate])[0].failures.includes('Mismatched or missing Windows power-line status'));
    }
});
