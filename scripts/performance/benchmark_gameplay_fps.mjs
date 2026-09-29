#!/usr/bin/env node
// Node 22+ on the same OS as Chrome. Example from WSL:
// '/mnt/c/Program Files/nodejs/node.exe' C:\\Users\\Public\\voxys-fps-20260929\\scripts\\performance\\benchmark_gameplay_fps.mjs \
//   --chrome='C:\\Program Files\\Google\\Chrome\\Application\\chrome.exe' \
//   --site='C:\\Users\\Public\\voxys-fps-20260929\\baseline' --output='C:\\Users\\Public\\voxys-fps-20260929\\results'
// Baseline-only: --repeats=3. A/B: add --candidate-site=DIR; order is ABBA.
// Shader experiments: --candidate-shaders=JSON maps shader module labels to files.
// Clean throughput and work diagnostics are separate: use --counts=1 for the latter.
import fs from 'node:fs';
import path from 'node:path';
import http from 'node:http';
import {spawn, execFileSync} from 'node:child_process';
import {installCanvasFpsProbe} from './canvas_fps_probe.mjs';
import {inspectGameplaySite, readGameplayShaderOverrides, readGameplaySymbolMap, hashGameplayInputFile, scriptedOrbitMouseX} from './gameplay_fps_inputs.mjs';
import {installGameplayCounters, installGameplayDrawCounters, summarizeGameplayFpsWork, checkFreshFrontierHudWork} from './gameplay_fps_work.mjs';
import {validateGameplayFps, compareGameplayFps, makeAdventurePlayerTickGuard, gameplayLightingSettingNames} from './gameplay_fps_metrics.mjs';

const options = Object.fromEntries(process.argv.slice(2).map(arg => {
    const split = arg.indexOf('=');
    if (!arg.startsWith('--') || split < 3) throw Error('Use --name=value');
    return [arg.slice(2, split), arg.slice(split + 1)];
}));
if (!options.chrome || !options.site || !options.output) throw Error('--chrome, --site and --output are required');
const positiveInteger = (name, fallback) => {
    const value = Number(options[name] ?? fallback);
    if (!Number.isInteger(value) || value <= 0) throw Error(`--${name} requires a positive integer`);
    return value;
};
const width = positiveInteger('width', 1280), height = positiveInteger('height', 720);
const repeats = positiveInteger('repeats', 3), ticks = positiveInteger('ticks', 600);
const warmupTicks = positiveInteger('warmup-ticks', 600), timeoutMs = positiveInteger('timeout-ms', 900000);
const driverPollMs = positiveInteger('driver-poll-ms', 8);
const playerStallTimeoutMs = 5000;
const experiences = (options.experiences ?? 'frontier,build').split(',');
const scenarios = (options.scenarios ?? 'idle,orbit,walk').split(',');
if (experiences.some(value => !['frontier', 'build'].includes(value))) throw Error('--experiences accepts frontier,build');
if (scenarios.some(value => !['idle', 'orbit', 'walk'].includes(value))) throw Error('--scenarios accepts idle,orbit,walk');
const hasCandidate = Boolean(options['candidate-site'] || options['candidate-shaders']);
const uncapped = options.uncapped !== '0', instrumented = options.counts === '1';
const profiling = options.profiling === '1';
const cpuProfile = options['cpu-profile'] === '1';
const diagnosticOnly = options['diagnostic-only'] === '1';
const dayNight = options['day-night'] === '1';
const requireZeroSun = options['require-zero-sun'] === '1';
const checkHudWork = options['check-hud-work'] === '1';
if (checkHudWork && !instrumented) throw Error('--check-hud-work=1 requires --counts=1');
const clockHour = Number(options.hour ?? 16);
if (!Number.isFinite(clockHour) || clockHour < 0 || clockHour > 24) throw Error('--hour must be between 0 and 24');
const output = path.resolve(options.output);
fs.mkdirSync(output, {recursive: true});
const profileRoot = path.resolve(options['profile-root'] ?? output);
if (profileRoot !== output && !profileRoot.startsWith(output + path.sep)) throw Error('--profile-root must be inside the output directory');
fs.mkdirSync(profileRoot, {recursive: true});
const sites = {baseline: path.resolve(options.site), candidate: path.resolve(options['candidate-site'] ?? options.site)};
const harnessIdentity = Object.fromEntries(['benchmark_gameplay_fps.mjs', 'canvas_fps_probe.mjs',
    'gameplay_fps_inputs.mjs', 'gameplay_fps_metrics.mjs', 'gameplay_fps_work.mjs', 'gameplay_counters.mjs']
    .map(filename => [filename, hashGameplayInputFile(new URL(filename, import.meta.url))]));
fs.mkdirSync(path.join(output, 'harness'), {recursive: true});
for (const filename of Object.keys(harnessIdentity)) {
    fs.copyFileSync(new URL(filename, import.meta.url), path.join(output, 'harness', filename));
}
const identities = {};
const symbolMaps = {};
for (const variant of hasCandidate ? ['baseline', 'candidate'] : ['baseline']) {
    identities[variant] = inspectGameplaySite(sites[variant]);
    symbolMaps[variant] = readGameplaySymbolMap(options[`${variant}-symbol-map`],
        options[`${variant}-symbol-map-wasm-sha256`], identities[variant].files[identities[variant].stem + '.wasm'].sha256);
}
const shaderOverrides = {}, shaderIdentities = {};
for (const variant of ['baseline', 'candidate']) {
    const inputs = readGameplayShaderOverrides(options[`${variant}-shaders`]);
    shaderOverrides[variant] = inputs.overrides; shaderIdentities[variant] = inputs.identities;
}
function captureSetupPowerLine() {
    if (process.platform !== 'win32') return {available:false, reason:'Windows power API unavailable on this OS'};
    const command = "$ErrorActionPreference='Stop'; Add-Type -AssemblyName System.Windows.Forms; $status=[System.Windows.Forms.SystemInformation]::PowerStatus; [pscustomobject]@{PowerLineStatus=$status.PowerLineStatus.ToString(); BatteryChargeStatus=$status.BatteryChargeStatus.ToString(); BatteryLifePercent=$status.BatteryLifePercent} | ConvertTo-Json -Compress";
    const raw = execFileSync(path.join(process.env.SystemRoot ?? 'C:\\Windows', 'System32', 'WindowsPowerShell', 'v1.0', 'powershell.exe'),
        ['-NoProfile', '-Command', command], {encoding:'utf8', timeout:15000, windowsHide:true});
    const result = JSON.parse(raw.trim().replace(/^\uFEFF/, ''));
    if (!['Online', 'Offline'].includes(result.PowerLineStatus)) throw Error('Windows power-line status is unavailable');
    return {available:true, capturedAtUtc:new Date().toISOString(), ...result};
}
const sleep = milliseconds => new Promise(resolve => setTimeout(resolve, milliseconds));
const connections = [];
async function connect(endpoint, event) {
    const socket = new WebSocket(endpoint);
    await new Promise((resolve, reject) => { socket.onopen = resolve; socket.onerror = reject; });
    connections.push(socket);
    let sequence = 0;
    const pending = new Map();
    socket.onmessage = message => {
        const value = JSON.parse(message.data), request = pending.get(value.id);
        if (request) {
            pending.delete(value.id); clearTimeout(request.timer);
            value.error ? request.reject(Error(JSON.stringify(value.error))) : request.resolve(value.result);
        } else if (value.method) event?.(value);
    };
    socket.onclose = () => {
        for (const request of pending.values()) {clearTimeout(request.timer); request.reject(Error('CDP connection closed'));}
        pending.clear();
    };
    const call = (method, params = {}) => new Promise((resolve, reject) => {
        if (socket.readyState !== WebSocket.OPEN) {reject(Error('CDP connection is not open')); return;}
        const id = ++sequence;
        const timer = setTimeout(() => {pending.delete(id); reject(Error(`CDP timeout: ${method}`));}, timeoutMs);
        pending.set(id, {resolve, reject, timer});
        socket.send(JSON.stringify({id, method, params}));
    });
    return {call, evaluate: async expression => {
        const response = await call('Runtime.evaluate', {expression, awaitPromise: true, returnByValue: true});
        if (response.exceptionDetails) throw Error(JSON.stringify(response.exceptionDetails));
        return response.result.value;
    }};
}

const readState = `(() => {
    const telemetry = JSON.parse(voxyModule.UTF8ToString(voxyModule._voxy_get_telemetry_json()));
    const adventure = JSON.parse(voxyModule.UTF8ToString(voxyModule._get_adventure_state_json()));
    const rendererLighting = Object.fromEntries(${JSON.stringify(gameplayLightingSettingNames)}.map(name =>
        [name, voxyModule.ccall('voxy_renderer_get_number', 'number', ['string'], [name])]));
    const canvas = document.getElementById('voxy-canvas');
    return {telemetry, adventure, rendererLighting, acquisitions: voxyCanvasFps.acquisitions, uniqueTextures: voxyCanvasFps.uniqueTextures,
        submissions: voxyCanvasFps.submissions, commands: voxyCanvasFps.commands, buildId: window.voxyBuildId ?? null,
        visibility: document.visibilityState, uncapped: voxyModule._voxy_get_uncapped_fps() === 1,
        canvas: {width: canvas.width, height: canvas.height}};
})()`;
const report = {schema: 'voxys.gameplay-fps.v1', complete: false, startedAt: new Date().toISOString(),
    measurement: 'Canvas acquisition calls and engine surface frames during uncapped headless gameplay; unique texture turnover reported separately; not monitor display FPS',
    capturePurpose: options['capture-purpose'] ?? 'current-build-gameplay-performance',
    configuration: {width, height, repeats, ticks, warmupTicks, driverPollMs, playerStallTimeoutMs, recordPowerLineStatus: process.platform === 'win32',
        travelClock: 'authoritative AdventurePlayer fixed-step tick', experiences, scenarios, uncapped,
        instrumented: instrumented || cpuProfile || diagnosticOnly, counts: instrumented, cpuProfile,
        diagnosticOnly, profiling, clockHour, dayNight, requireZeroSun, checkHudWork},
    sites, identities, shaderIdentities, symbolMaps, harnessIdentity, trialConditions: [], rows: []};
const saveReport = () => fs.writeFileSync(path.join(output, 'report.json'), JSON.stringify(report, null, 2));
let server, chrome, browser;
const profile = fs.mkdtempSync(path.join(profileRoot, 'voxys-fps-20260929-'));
try {
    server = http.createServer((request, response) => {
        const parsed = new URL(request.url, 'http://localhost');
        const segments = decodeURIComponent(parsed.pathname).split('/').filter(Boolean);
        const variant = segments.shift();
        const root = sites[variant];
        if (!root) {response.writeHead(404); response.end(); return;}
        const file = path.resolve(root, segments.join('/') || 'index.html');
        if (!file.startsWith(root + path.sep)) {response.writeHead(403); response.end(); return;}
        fs.readFile(file, (error, bytes) => {
            if (error) {response.writeHead(404); response.end(); return;}
            const mime = {'.html': 'text/html', '.js': 'text/javascript', '.mjs': 'text/javascript',
                '.css': 'text/css', '.wasm': 'application/wasm', '.json': 'application/json', '.svg': 'image/svg+xml', '.png': 'image/png'};
            response.writeHead(200, {'Content-Type': mime[path.extname(file)] ?? 'application/octet-stream', 'Cache-Control': 'no-cache'});
            response.end(bytes);
        });
    });
    await new Promise(resolve => server.listen(0, '127.0.0.1', resolve));
    const chromeArguments = ['--headless=new', `--window-size=${width},${height}`, '--remote-debugging-port=0',
        `--user-data-dir=${profile}`, '--no-first-run', '--no-default-browser-check',
        '--disable-background-timer-throttling', '--disable-renderer-backgrounding', '--disable-background-networking', 'about:blank'];
    report.chromeArguments = chromeArguments.filter(value => !value.startsWith('--user-data-dir='));
    let stderr = '';
    chrome = spawn(options.chrome, chromeArguments);
    const closed = new Promise(resolve => chrome.once('close', resolve));
    chrome.on('error', error => {stderr += String(error);});
    chrome.stderr.on('data', bytes => {
        stderr += bytes;
        fs.appendFileSync(path.join(output, 'chrome.log'), bytes);
    });
    let endpoint;
    for (let i = 0; i < 300; ++i) {
        endpoint = stderr.match(/DevTools listening on (ws:\/\/\S+)/)?.[1];
        if (endpoint) break;
        await sleep(100);
    }
    if (!endpoint) throw Error(`Chrome failed to launch: ${stderr}`);
    browser = await connect(endpoint);
    report.browser = await browser.call('Browser.getVersion');
    report.system = await browser.call('SystemInfo.getInfo');
    saveReport();
    const sequence = hasCandidate ? ['baseline', 'candidate', 'candidate', 'baseline'] : ['baseline'];
    let trial = 0;
    for (let repeat = 1; repeat <= repeats; ++repeat) for (const experience of experiences) for (const variant of sequence) {
        ++trial;
        const setupPower = captureSetupPowerLine();
        report.trialConditions.push({trial, repeat, variant, experience, power:setupPower});
        saveReport();
        const pageErrors = [], loadErrors = [], pageConsoleErrors = [];
        const firstPartyRequests = new Set();
        const target = await (await fetch(`http://127.0.0.1:${new URL(endpoint).port}/json/new?about:blank`, {method: 'PUT'})).json();
        const page = await connect(target.webSocketDebuggerUrl, value => {
            if (value.method === 'Runtime.exceptionThrown') pageErrors.push(value.params.exceptionDetails);
            if (value.method === 'Network.requestWillBeSent'
                && value.params.request.url.startsWith(`http://127.0.0.1:${server.address().port}/`)) firstPartyRequests.add(value.params.requestId);
            if (value.method === 'Network.loadingFailed' && !value.params.canceled
                && firstPartyRequests.has(value.params.requestId)) loadErrors.push(value.params);
            if (value.method === 'Runtime.consoleAPICalled') {
                const message = value.params.args.map(arg => arg.value ?? arg.description ?? '').join(' ');
                fs.appendFileSync(path.join(output, 'console.log'), `${trial} ${message}\n`);
                if (value.params.type === 'error') pageConsoleErrors.push(message);
            }
            if (value.method === 'Network.responseReceived' && value.params.response.status >= 400
                && value.params.response.url.startsWith(`http://127.0.0.1:${server.address().port}/`)
                && !new URL(value.params.response.url).pathname.endsWith('/favicon.ico')) loadErrors.push(value.params.response);
        });
        await page.call('Runtime.enable'); await page.call('Page.enable'); await page.call('Network.enable');
        await page.call('Emulation.setDeviceMetricsOverride', {width, height, deviceScaleFactor: 1, mobile: false});
        await page.call('Emulation.setFocusEmulationEnabled', {enabled: true});
        await page.call('Page.addScriptToEvaluateOnNewDocument', {source: `(${installCanvasFpsProbe.toString()})(${JSON.stringify(shaderOverrides[variant])})`});
        if (instrumented) await page.call('Page.addScriptToEvaluateOnNewDocument', {source: `(${installGameplayCounters.toString()})(); (${installGameplayDrawCounters.toString()})()`});
        const url = `http://127.0.0.1:${server.address().port}/${variant}/index.html?experience=${experience}&new=1&telemetry=0&renderProfile=${profiling ? 1 : 0}&physicsProfile=${profiling ? 1 : 0}`;
        console.log(`Trial ${trial}: ${variant} / ${experience}, repeat ${repeat}`);
        await page.call('Page.navigate', {url});
        const startWait = Date.now();
        for (;;) {
            const ready = await page.evaluate(`({ready: typeof voxyModule !== 'undefined' && voxyModule?._voxy_is_initialized?.() === 1,
                loading: document.getElementById('loading') ? getComputedStyle(document.getElementById('loading')).display !== 'none' : true,
                health: globalThis.voxyCanvasFps ? {lost: voxyCanvasFps.lost, errors: voxyCanvasFps.errors} : null})`);
            if (ready.health?.lost.length || ready.health?.errors.length) throw Error(`GPU failure during startup: ${JSON.stringify(ready.health)}`);
            if (pageErrors.length || loadErrors.length || pageConsoleErrors.length) throw Error(`Page failed during startup: ${JSON.stringify({pageErrors, loadErrors, pageConsoleErrors})}`);
            if (ready.ready && !ready.loading) break;
            if (Date.now() - startWait > timeoutMs) throw Error('Game startup timed out');
            await sleep(500);
        }
        report.lastStartupMs = Date.now() - startWait;
        const scalarPlayerTickAvailable = await page.evaluate(`typeof voxyModule._voxy_get_adventure_player_tick === 'function'`);
        if (!scalarPlayerTickAvailable && !diagnosticOnly && !instrumented && !cpuProfile && !profiling) {
            throw Error('Clean gameplay timing requires readonly scalar adventure player tick; JSON fallback is diagnostic-only');
        }
        // Use accepted public controls. Camera position remains the production
        // starting view; no renderer flags, distances or simulation budgets change.
        await page.evaluate(`(() => {
            const canvas = document.getElementById('voxy-canvas'); canvas.focus();
            canvas.width = ${width}; canvas.height = ${height}; voxyModule._voxy_resize(${width}, ${height});
            voxyModule._adventure_action(15, 0); voxyModule._adventure_action(22, 0);
            const set = (key, value) => voxyModule.ccall('voxy_renderer_set_number', 'number', ['string','number','number'], [key, value, 1]);
            if (set('lighting.dayNightEnabled', 1) !== 1 || set('lighting.dayHour', ${clockHour}) !== 1
                || set('lighting.dayNightPaused', ${dayNight ? 0 : 1}) !== 1) throw Error('Lighting controls were not accepted');
            voxyModule._voxy_set_uncapped_fps(${uncapped ? 1 : 0});
        })()`);
        await page.call('Input.dispatchMouseEvent', {type: 'mouseMoved', x: width / 2, y: height / 2});
        await page.evaluate(`(async () => {
            const playerTick = ${scalarPlayerTickAvailable}
                ? () => Number(voxyModule._voxy_get_adventure_player_tick())
                : () => Number(JSON.parse(voxyModule.UTF8ToString(voxyModule._get_adventure_state_json())).player.tick);
            const first = playerTick();
            const checkPlayerTick = (${makeAdventurePlayerTickGuard.toString()})(first, performance.now(), ${playerStallTimeoutMs});
            const deadline = performance.now() + ${timeoutMs};
            while (checkPlayerTick(playerTick(), performance.now()) < ${warmupTicks}) {
                if (voxyCanvasFps.lost.length || voxyCanvasFps.errors.length || globalThis.voxyDeviceLost
                    || globalThis.voxyUncapturedGpuErrors?.length) throw Error('GPU failure during warmup');
                if (performance.now() > deadline) throw Error('Warmup player ticks did not reach requested duration');
                await new Promise(resolve => setTimeout(resolve, ${driverPollMs}));
            }
        })()`);
        for (const [scenarioIndex, scenario] of scenarios.entries()) {
            if (scenario === 'orbit') await page.call('Input.dispatchMouseEvent', {type: 'mousePressed', button: 'right', buttons: 2, x: width / 2, y: height / 2, clickCount: 1});
            if (cpuProfile) {
                await page.call('Profiler.enable');
                await page.call('Profiler.setSamplingInterval', {interval: 1000});
                await page.call('Profiler.start');
            }
            const sample = await page.evaluate(`(async () => {
                const state = () => (${readState});
                const probe = voxyCanvasFps;
                const before = state(), workBefore = {...globalThis.voxyWorkCounts};
                const firstTick = Number(before.adventure.player.tick);
                const playerTick = ${scalarPlayerTickAvailable}
                    ? () => Number(voxyModule._voxy_get_adventure_player_tick())
                    : () => Number(JSON.parse(voxyModule.UTF8ToString(voxyModule._get_adventure_state_json())).player.tick);
                const started = performance.now(), deadline = started + ${timeoutMs};
                const checkPlayerTick = (${makeAdventurePlayerTickGuard.toString()})(firstTick, started, ${playerStallTimeoutMs});
                const cpu = [], gpu = new Map(), inputs = [], queueSamples = [];
                let phase = -1, orbitStep = -1, lastTelemetry = -1;
                let observedTicks = 0, driverReadCount = 0, driverReadMs = 0;
                probe.times = []; probe.uniqueTimes = []; probe.sizes = []; probe.measuring = true;
                try {
                    for (;;) {
                        if (probe.lost.length || probe.errors.length || globalThis.voxyDeviceLost
                            || globalThis.voxyUncapturedGpuErrors?.length) throw Error('GPU failure during scripted journey');
                        const readStart = performance.now();
                        const currentPlayerTick = playerTick();
                        driverReadMs += performance.now() - readStart; ++driverReadCount;
                        const tick = checkPlayerTick(currentPlayerTick, performance.now());
                        observedTicks = tick;
                        if (tick >= ${ticks}) break;
                        if (performance.now() > deadline) throw Error('Gameplay journey timed out');
                        if (${JSON.stringify(scenario)} === 'walk') {
                            if (phase !== 0) {
                                for (const key of [87,68,83,65]) voxyModule._voxy_key_event(key, 0);
                                voxyModule._voxy_key_event(87, 1);
                                inputs.push({tick, phase: 0, key: 'W'}); phase = 0;
                            }
                        }
                        if (${JSON.stringify(scenario)} === 'orbit') {
                            const next = Math.min(119, Math.floor(tick * 120 / ${ticks}));
                            if (next !== orbitStep) {
                                const x = (${scriptedOrbitMouseX.toString()})(next, ${width});
                                document.getElementById('voxy-canvas').dispatchEvent(new MouseEvent('mousemove', {bubbles:true, clientX:x, clientY:${height / 2}, buttons:2}));
                                inputs.push({tick, step:next, x}); orbitStep = next;
                            }
                        }
                        if (performance.now() - lastTelemetry >= 100) {
                            const telemetry = JSON.parse(voxyModule.UTF8ToString(voxyModule._voxy_get_telemetry_json()));
                            cpu.push(telemetry.frame.cpu_ms);
                            queueSamples.push({gpu_queue: telemetry.frame.gpu_queue, gpu_queue_limit: telemetry.frame.gpu_queue_limit});
                            if (telemetry.render_gpu?.available) gpu.set(telemetry.render_gpu.frame, telemetry.render_gpu);
                            lastTelemetry = performance.now();
                        }
                        await new Promise(resolve => setTimeout(resolve, ${driverPollMs}));
                    }
                } finally {
                    probe.measuring = false;
                    for (const key of [87,68,83,65]) voxyModule._voxy_key_event(key, 0);
                }
                const elapsedMs = performance.now() - started, after = state();
                return {before, after, elapsedMs, acquisitionTimes: probe.times, textureSizes: probe.sizes,
                    uniqueSurfaceTimes: probe.uniqueTimes, queueSamples,
                    cpu, gpuSamples: [...gpu.values()], devices: probe.devices, lost: probe.lost,
                    gpuErrors: probe.errors, deviceProfile: globalThis.voxyDeviceProfile,
                    appliedShaders: probe.shaders, workBefore, workAfter: {...globalThis.voxyWorkCounts},
                    engineHealth: {lost: globalThis.voxyDeviceLost, errors: globalThis.voxyUncapturedGpuErrors},
                    travel: {complete: true, clock:'AdventurePlayer', path:${JSON.stringify(scenario === 'walk' ? 'constant-W-forward' : scenario)}, requestedTicks:${ticks}, observedTicks,
                        playerTickMethod:${JSON.stringify(scalarPlayerTickAvailable ? 'readonly-scalar' : 'full-state-json-diagnostic')},
                        driverPollMs:${driverPollMs}, playerStallTimeoutMs:${playerStallTimeoutMs}, driverReadCount, driverReadMs, inputs}};
            })()`);
            if (cpuProfile) {
                const {profile: capturedProfile} = await page.call('Profiler.stop');
                await page.call('Profiler.disable');
                const filename = `${trial}-${variant}-${experience}-${scenario}.cpuprofile`;
                fs.writeFileSync(path.join(output, filename), JSON.stringify(capturedProfile));
                const metadata = {profile: filename, trial, variant, experience, scenario, samplingIntervalUs: 1000,
                    diagnostic: true, artifactIdentity: identities[variant], shaderIdentity: shaderIdentities[variant],
                    symbolMap: symbolMaps[variant], browser: report.browser, harnessIdentity,
                    interpretation: symbolMaps[variant].matched ? 'Symbols explicitly paired with captured WASM hash'
                        : 'WASM function names are unverified; no historical map applied'};
                fs.writeFileSync(path.join(output, filename + '.metadata.json'), JSON.stringify(metadata, null, 2));
                sample.cpuProfile = metadata;
            }
            if (scenario === 'orbit') await page.call('Input.dispatchMouseEvent', {type: 'mouseReleased', button: 'right', buttons: 0, x: width / 2, y: height / 2, clickCount: 1});
            Object.assign(sample, {trial, repeat, variant, experience, scenario, travelVersion: 6,
                captureInstrumented: instrumented || cpuProfile || diagnosticOnly || profiling,
                setupPower, powerLineStatus: setupPower.available ? setupPower.PowerLineStatus : undefined,
                requestedTicks: ticks, clockHour, dayNight, requireZeroSun, uncapped,
                freshWorldIdle: scenarioIndex === 0 && scenario === 'idle',
                expectedShaderLabels: Object.keys(shaderOverrides[variant]),
                pageErrors: [...pageErrors, ...pageConsoleErrors], loadErrors: [...loadErrors]});
            sample.validation = validateGameplayFps(sample, {width, height, uncapped, shaderLabels: Object.keys(shaderOverrides[variant])});
            if (experience === 'frontier' && sample.after.adventure.frontier !== true) {
                sample.validation.ok = false; sample.validation.errors.push('Frontier experience was not selected');
            }
            if (experience === 'build' && sample.after.adventure.frontier === true) {
                sample.validation.ok = false; sample.validation.errors.push('Creative experience loaded Frontier');
            }
            if (scenario === 'walk') {
                const startYaw = sample.before.adventure.camera.yaw;
                const headingChanged = [sample.before.adventure.player.yaw,
                    sample.after.adventure.player.yaw, sample.after.adventure.camera.yaw]
                    .some(yaw => Math.abs(Math.atan2(Math.sin(yaw - startYaw), Math.cos(yaw - startYaw))) > .001);
                if (headingChanged) {
                    sample.validation.ok = false;
                    sample.validation.errors.push('Constant-W walk changed player or camera heading');
                }
            }
            if (instrumented) sample.work = summarizeGameplayFpsWork(sample);
            if (checkHudWork && experience === 'frontier' && scenario === 'idle') {
                try {sample.hudWorkGuard = {ok: true, ...checkFreshFrontierHudWork(sample)};}
                catch (error) {
                    sample.hudWorkGuard = {ok: false, error: String(error)};
                    sample.validation.ok = false; sample.validation.errors.push(String(error));
                }
            }
            report.rows.push(sample); saveReport();
            if (diagnosticOnly) console.log(`${scenario}: diagnostic acquisition/surface counts ${sample.validation.summary.acquiredFrames}/${sample.validation.summary.surfaceFrames}; unique textures ${sample.validation.summary.uniqueSurfaceTextures}; ${sample.validation.ok ? 'valid counters and health' : sample.validation.errors.join('; ')}`);
            else console.log(`${scenario}: ${sample.validation.summary.acquiredFps.toFixed(2)} canvas acquisitions/s; unique textures ${sample.validation.summary.uniqueSurfaceTurnoverFps.toFixed(2)}/s; p95 ${sample.validation.summary.acquisitionIntervalMs.p95?.toFixed(2)} ms; ${sample.validation.ok ? 'valid' : sample.validation.errors.join('; ')}`);
            if (!sample.validation.ok) throw Error(`Invalid gameplay capture: ${sample.validation.errors.join('; ')}`);
            const shot = await page.call('Page.captureScreenshot', {format: 'png'});
            fs.writeFileSync(path.join(output, `${trial}-${variant}-${experience}-${scenario}.png`), Buffer.from(shot.data, 'base64'));
        }
        await browser.call('Target.closeTarget', {targetId: target.id});
        await sleep(1000);
    }
    if (hasCandidate) report.comparison = compareGameplayFps(report.rows, Number(options['max-regression-percent'] ?? 7.5));
    report.complete = true; report.finishedAt = new Date().toISOString(); saveReport();
    if (report.comparison?.some(group => group.failures.length)) process.exitCode = 2;
    await browser.call('Browser.close');
    await Promise.race([closed, sleep(5000)]);
} catch (error) {
    report.error = String(error); saveReport(); throw error;
} finally {
    if (browser) try {await browser.call('Browser.close');} catch {}
    for (const socket of connections) socket.close();
    if (chrome && !chrome.killed) chrome.kill();
    if (server) server.close();
    try {fs.rmSync(profile, {recursive: true, force: true, maxRetries: 5, retryDelay: 200});} catch {}
}
