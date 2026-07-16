const options = {
    port: 0,
    clicks: 3,
    clickIntervalMs: 500,
    durationMs: 15_000,
    settleMs: 2_000,
    warmupTick: 300,
    timeoutMs: 120_000,
    expectedWidth: 1236,
    expectedHeight: 777,
    profile: false,
    trace: false,
};

for (let index = 2; index < process.argv.length; ++index) {
    const argument = process.argv[index];
    const readInteger = (name) => {
        const value = Number.parseInt(process.argv[++index] ?? "", 10);
        if (!Number.isInteger(value) || value < 0) {
            throw new Error(`${name} requires a non-negative integer`);
        }
        return value;
    };
    switch (argument) {
        case "--port": options.port = readInteger(argument); break;
        case "--clicks": options.clicks = readInteger(argument); break;
        case "--click-interval-ms":
            options.clickIntervalMs = readInteger(argument);
            break;
        case "--duration-ms": options.durationMs = readInteger(argument); break;
        case "--settle-ms": options.settleMs = readInteger(argument); break;
        case "--warmup-tick": options.warmupTick = readInteger(argument); break;
        case "--timeout-ms": options.timeoutMs = readInteger(argument); break;
        case "--expected-width":
            options.expectedWidth = readInteger(argument);
            break;
        case "--expected-height":
            options.expectedHeight = readInteger(argument);
            break;
        case "--profile": options.profile = true; break;
        case "--trace": options.trace = true; break;
        default: throw new Error(`unknown argument: ${argument}`);
    }
}

if (options.port <= 0 || options.durationMs <= 0 || options.timeoutMs <= 0) {
    throw new Error(
        "usage: profile_wasm_clicks.mjs --port PORT "
        + "[--clicks N] [--click-interval-ms MS] [--duration-ms MS] "
        + "[--settle-ms MS] [--warmup-tick TICK] [--timeout-ms MS] "
        + "[--profile] [--trace]",
    );
}

const delay = (milliseconds) => new Promise(
    (resolve) => setTimeout(resolve, milliseconds),
);
const deadline = Date.now() + options.timeoutMs;

let page;
while (Date.now() < deadline) {
    try {
        const response = await fetch(
            `http://127.0.0.1:${options.port}/json/list`,
        );
        const targets = await response.json();
        page = targets.find((target) =>
            target.type === "page" && target.url.includes("index.html"));
        if (page) break;
    } catch {
        // Chrome is still starting.
    }
    await delay(100);
}
if (!page) throw new Error("WASM benchmark browser target did not start");

const socket = new WebSocket(page.webSocketDebuggerUrl);
await new Promise((resolve, reject) => {
    socket.addEventListener("open", resolve, { once: true });
    socket.addEventListener("error", reject, { once: true });
});

let nextId = 1;
const pending = new Map();
const diagnostics = [];
const ignoredDiagnostics = [];
const traceEvents = [];
let finishTrace;
const traceComplete = new Promise((resolve) => { finishTrace = resolve; });
const network = {
    requests: 0,
    requestBytes: 0,
    responseBytes: 0,
};
socket.addEventListener("message", (event) => {
    const message = JSON.parse(event.data);
    if (message.method === "Runtime.exceptionThrown") {
        const details = message.params.exceptionDetails;
        const description = details.exception?.description ?? details.text;
        if (description.includes("WrongDocumentError")
            && description.includes("pointer lock")) {
            // Headless Chrome has no valid top-level document for Pointer
            // Lock. The engine still records its internal capture state, which
            // is all deterministic input injection needs.
            ignoredDiagnostics.push(description);
        } else {
            diagnostics.push(description);
        }
    } else if (message.method === "Log.entryAdded"
               && message.params.entry.level === "error") {
        diagnostics.push(message.params.entry.text);
    } else if (message.method === "Network.requestWillBeSent") {
        ++network.requests;
        network.requestBytes += message.params.request.postData?.length ?? 0;
    } else if (message.method === "Network.loadingFinished") {
        network.responseBytes += message.params.encodedDataLength ?? 0;
    } else if (message.method === "Tracing.dataCollected") {
        traceEvents.push(...message.params.value);
    } else if (message.method === "Tracing.tracingComplete") {
        finishTrace();
    }
    if (!message.id || !pending.has(message.id)) return;
    const { resolve, reject } = pending.get(message.id);
    pending.delete(message.id);
    if (message.error) reject(new Error(message.error.message));
    else resolve(message.result);
});

const command = (method, params = {}) => new Promise((resolve, reject) => {
    const id = nextId++;
    pending.set(id, { resolve, reject });
    socket.send(JSON.stringify({ id, method, params }));
});

const evaluate = async (expression) => {
    const result = await command("Runtime.evaluate", {
        expression,
        returnByValue: true,
        awaitPromise: true,
    });
    if (result.exceptionDetails) {
        throw new Error(
            result.exceptionDetails.exception?.description
            ?? result.exceptionDetails.text,
        );
    }
    return result.result?.value;
};

await command("Runtime.enable");
await command("Log.enable");
await command("Network.enable");
await command("Performance.enable");
const browserVersion = await command("Browser.getVersion");

let readyState;
while (Date.now() < deadline) {
    readyState = await evaluate(`(() => {
        const initialized = typeof voxyModule !== "undefined"
            && voxyModule?._voxy_is_initialized?.() === 1;
        let sample = null;
        if (initialized) {
            const pointer = voxyModule._voxy_get_telemetry_json();
            if (pointer) {
                sample = JSON.parse(voxyModule.UTF8ToString(pointer));
            }
        }
        const canvas = document.getElementById("voxy-canvas");
        const error = document.getElementById("error");
        const bounds = canvas?.getBoundingClientRect();
        return {
            initialized,
            tick: sample?.physics?.tick ?? 0,
            backend: sample?.physics?.backend ?? null,
            arithmetic: sample?.physics?.arithmetic ?? null,
            render: sample?.render ?? null,
            camera: sample?.camera ?? null,
            device: globalThis.voxyDeviceProfile ?? null,
            canvasWidth: canvas?.width ?? 0,
            canvasHeight: canvas?.height ?? 0,
            cssWidth: bounds?.width ?? 0,
            cssHeight: bounds?.height ?? 0,
            viewportWidth: innerWidth,
            viewportHeight: innerHeight,
            devicePixelRatio,
            buildId: globalThis.voxyBuildId ?? null,
            profilingEnabled: new URLSearchParams(location.search)
                .get("physicsProfile") !== "0",
            renderProfilingEnabled: new URLSearchParams(location.search)
                .get("renderProfile") === "1",
            errorVisible: error
                ? getComputedStyle(error).display !== "none"
                : false,
            errorText: error?.textContent ?? "",
        };
    })()`);
    if (readyState.errorVisible) {
        throw new Error(`WASM application failed: ${readyState.errorText}`);
    }
    if (readyState.initialized && readyState.tick >= options.warmupTick) break;
    await delay(250);
}
if (!readyState?.initialized || readyState.tick < options.warmupTick) {
    throw new Error(
        `WASM workload warmup timed out: ${JSON.stringify(readyState)}`,
    );
}
if (readyState.canvasWidth !== options.expectedWidth
    || readyState.canvasHeight !== options.expectedHeight) {
    throw new Error(
        `unexpected canvas ${readyState.canvasWidth}x${readyState.canvasHeight}; `
        + `expected ${options.expectedWidth}x${options.expectedHeight}`,
    );
}
if (readyState.backend !== "webgpu_soft"
    || readyState.arithmetic !== "fast_float"
    || readyState.render?.path !== "raycast"
    || readyState.render?.terrain_width !== 8192
    || readyState.render?.terrain_height !== 8192
    || readyState.render?.terrain_mips !== 14) {
    throw new Error(`unexpected workload: ${JSON.stringify(readyState)}`);
}

// Freeze the view before spawning. The browser defaults to a gravity-driven
// character, so wall-clock A/B differences would otherwise change every body
// origin and direction. F8 selects free-fly and Num1 applies a fixed camera.
const cameraSetupFrame = await evaluate(
    "voxyModule._voxy_get_frame_count()",
);
await evaluate(`(() => {
    voxyModule._voxy_key_event(119, 1);
    voxyModule._voxy_key_event(119, 0);
    voxyModule._voxy_key_event(49, 1);
    voxyModule._voxy_key_event(49, 0);
})()`);
while (Date.now() < deadline) {
    const frame = await evaluate("voxyModule._voxy_get_frame_count()");
    if (frame > cameraSetupFrame) break;
    await delay(25);
}
const benchmarkCamera = await evaluate(`(() => {
    const pointer = voxyModule._voxy_get_telemetry_json();
    return JSON.parse(voxyModule.UTF8ToString(pointer)).camera;
})()`);
const expectedCamera = [202.53, 120.92, -27.16];
const benchmarkWorldPosition = benchmarkCamera?.position.map(
    (value, axis) => value + 256 * benchmarkCamera.sector[axis],
);
if (!benchmarkCamera || expectedCamera.some(
    (value, axis) => Math.abs(benchmarkWorldPosition[axis] - value) > 0.001
) || Math.abs(benchmarkCamera.yaw - 1.4578) > 0.0001
    || Math.abs(benchmarkCamera.pitch + 0.0944) > 0.0001) {
    throw new Error(
        `deterministic camera setup failed: ${JSON.stringify(benchmarkCamera)}`,
    );
}

await evaluate(`(() => {
    const capture = {
        active: true,
        startedMs: performance.now(),
        lastFrameMs: null,
        frameMs: [],
        frameSamples: [],
        stageSamples: [],
        renderStageSamples: [],
        lastSubmittedAtMs: null,
        lastSubmittedFrame: voxyModule._voxy_get_frame_count(),
        stageTickFloor: voxyModule._voxy_get_physics_stage_tick(),
        stageTickCeiling: 0,
        frameCountStart: voxyModule._voxy_get_frame_count(),
        frameCountEnd: 0,
        recordFrames: true,
        peakJsHeapBytes: performance.memory?.usedJSHeapSize ?? 0,
        wasmBytes: voxyModule.HEAPU8?.buffer?.byteLength
            ?? voxyModule.wasmMemory?.buffer?.byteLength ?? 0,
    };
    globalThis.__voxyGoalCapture = capture;
    // Benchmark input is already captured by the engine. Prevent the page's
    // anonymous click listener from making a second Pointer Lock request,
    // which headless Chrome rejects as an unhandled promise.
    document.getElementById("voxy-canvas").addEventListener(
        "click", (event) => event.stopImmediatePropagation(), true,
    );
    const onFrame = (now) => {
        if (!capture.active) return;
        capture.peakJsHeapBytes = Math.max(
            capture.peakJsHeapBytes,
            performance.memory?.usedJSHeapSize ?? 0,
        );
        const submittedFrame = voxyModule._voxy_get_frame_count();
        if (submittedFrame !== capture.lastSubmittedFrame) {
            if (capture.recordFrames && capture.lastSubmittedAtMs !== null) {
                capture.frameMs.push(now - capture.lastSubmittedAtMs);
            }
            if (capture.recordFrames) capture.lastSubmittedAtMs = now;
            capture.lastSubmittedFrame = submittedFrame;
            for (;;) {
                const timingTick =
                    voxyModule._voxy_poll_physics_stage_timing();
                if (timingTick <= 0) break;
                const stages = [];
                for (let stage = 0; stage < 14; ++stage) {
                    stages.push(
                        voxyModule._voxy_get_polled_physics_stage_ms(stage),
                    );
                }
                capture.stageSamples.push({
                    tick: timingTick,
                    atMs: performance.now() - capture.startedMs,
                    totalMs: stages.every((value) => value >= 0)
                        ? stages.reduce((sum, value) => sum + value, 0) : -1,
                    stages,
                });
            }
            for (;;) {
                const timingFrame =
                    voxyModule._voxy_poll_render_stage_timing?.() ?? 0;
                if (timingFrame <= 0) break;
                const stages = [];
                for (let stage = 0; stage < 4; ++stage) {
                    stages.push(
                        voxyModule._voxy_get_polled_render_stage_ms(stage),
                    );
                }
                capture.renderStageSamples.push({
                    frame: timingFrame,
                    atMs: performance.now() - capture.startedMs,
                    totalMs: stages.every((value) => value >= 0)
                        ? stages.reduce((sum, value) => sum + value, 0) : -1,
                    stages,
                });
            }
            if (capture.recordFrames) {
                capture.frameSamples.push({
                    atMs: performance.now() - capture.startedMs,
                    cpuMs: voxyModule._voxy_get_last_frame_cpu_ms(),
                    queue: voxyModule._voxy_get_gpu_frames_in_flight(),
                    pacingSkips: voxyModule._voxy_get_gpu_pacing_skips(),
                    substeps: voxyModule._voxy_get_physics_substeps(),
                    bodies: voxyModule._voxy_get_physics_resident_bodies(),
                });
            }
        }
        requestAnimationFrame(onFrame);
    };
    requestAnimationFrame(onFrame);
    return true;
})()`);

const canvasCenter = await evaluate(`(() => {
    const bounds = document.getElementById("voxy-canvas")
        .getBoundingClientRect();
    return {
        x: bounds.left + bounds.width * 0.5,
        y: bounds.top + bounds.height * 0.5,
    };
})()`);
const clickInputs = [];
await evaluate("voxyModule._voxy_mouse_move(0, 0)");
for (let click = 0; click < options.clicks; ++click) {
    clickInputs.push(await evaluate(`(() => {
        const pointer = voxyModule._voxy_get_telemetry_json();
        const sample = JSON.parse(voxyModule.UTF8ToString(pointer));
        return {
            camera: sample.camera,
            encodedTick: voxyModule._voxy_get_physics_encoded_tick(),
            frame: voxyModule._voxy_get_frame_count(),
        };
    })()`));
    await command("Input.dispatchMouseEvent", {
        type: "mousePressed",
        x: canvasCenter.x,
        y: canvasCenter.y,
        button: "right",
        buttons: 2,
        clickCount: 1,
    });
    await command("Input.dispatchMouseEvent", {
        type: "mouseReleased",
        x: canvasCenter.x,
        y: canvasCenter.y,
        button: "right",
        buttons: 0,
        clickCount: 1,
    });
    const clickBodyCount = (click + 1) * 128;
    let clickProcessed = false;
    while (Date.now() < deadline) {
        const bodies = await evaluate(
            "voxyModule._voxy_get_physics_resident_bodies()",
        );
        if (bodies === clickBodyCount) {
            clickProcessed = true;
            break;
        }
        await delay(50);
    }
    if (!clickProcessed) {
        throw new Error(`body count did not reach ${clickBodyCount}`);
    }
    if (click + 1 < options.clicks) await delay(options.clickIntervalMs);
}
const expectedBodies = options.clicks * 128;
await delay(options.settleMs);

let cpuProfileStarted = false;
let heapProfileStarted = false;
if (options.profile) {
    await command("Profiler.enable");
    await command("Profiler.setSamplingInterval", { interval: 1000 });
    await command("Profiler.start");
    cpuProfileStarted = true;
    try {
        await command("HeapProfiler.enable");
        await command("HeapProfiler.startSampling", {
            samplingInterval: 32_768,
            includeObjectsCollectedByMajorGC: true,
            includeObjectsCollectedByMinorGC: true,
        });
        heapProfileStarted = true;
    } catch (error) {
        diagnostics.push(`heap sampling unavailable: ${error.message}`);
    }
}
if (options.trace) {
    await command("Tracing.start", {
        categories: "gpu,disabled-by-default-gpu.dawn,devtools.timeline",
        options: "record-as-much-as-possible",
        transferMode: "ReportEvents",
    });
}

await evaluate(`(() => {
    const capture = globalThis.__voxyGoalCapture;
    while (voxyModule._voxy_poll_physics_stage_timing() > 0) {}
    while ((voxyModule._voxy_poll_render_stage_timing?.() ?? 0) > 0) {}
    capture.startedMs = performance.now();
    capture.frameMs.length = 0;
    capture.frameSamples.length = 0;
    capture.stageSamples.length = 0;
    capture.renderStageSamples.length = 0;
    capture.lastSubmittedAtMs = null;
    capture.lastSubmittedFrame = voxyModule._voxy_get_frame_count();
    capture.frameCountStart = capture.lastSubmittedFrame;
    capture.stageTickFloor = voxyModule._voxy_get_physics_encoded_tick();
    capture.recordFrames = true;
    capture.peakJsHeapBytes = performance.memory?.usedJSHeapSize ?? 0;
})()`);
network.requests = 0;
network.requestBytes = 0;
network.responseBytes = 0;

await delay(options.durationMs);

const measurementWindow = await evaluate(`(() => {
    const capture = globalThis.__voxyGoalCapture;
    capture.recordFrames = false;
    capture.frameCountEnd = voxyModule._voxy_get_frame_count();
    capture.stageTickCeiling = voxyModule._voxy_get_physics_encoded_tick();
    return {
        elapsedMs: performance.now() - capture.startedMs,
        stageTickCeiling: capture.stageTickCeiling,
    };
})()`);
if (readyState.profilingEnabled) {
    while (Date.now() < deadline) {
        const latestStageTick = await evaluate(
            "voxyModule._voxy_get_physics_stage_tick()",
        );
        if (latestStageTick >= measurementWindow.stageTickCeiling) break;
        await delay(50);
    }
}
const capture = await evaluate(`(() => {
    const capture = globalThis.__voxyGoalCapture;
    capture.active = false;
    return {
        elapsedMs: ${JSON.stringify(0)},
        frameMs: capture.frameMs,
        frameSamples: capture.frameSamples,
        stageSamples: capture.stageSamples,
        renderStageSamples: capture.renderStageSamples,
        peakJsHeapBytes: capture.peakJsHeapBytes,
        wasmBytes: capture.wasmBytes,
        stageTickFloor: capture.stageTickFloor,
        stageTickCeiling: capture.stageTickCeiling,
        frameCountStart: capture.frameCountStart,
        frameCountEnd: capture.frameCountEnd,
    };
})()`);
capture.elapsedMs = measurementWindow.elapsedMs;
if (options.trace) {
    await command("Tracing.end");
    await traceComplete;
}
const telemetry = await evaluate(`(() => {
    const pointer = voxyModule._voxy_get_telemetry_json();
    return JSON.parse(voxyModule.UTF8ToString(pointer));
})()`);

const performanceMetrics = Object.fromEntries(
    (await command("Performance.getMetrics")).metrics.map(
        (metric) => [metric.name, metric.value],
    ),
);

let cpuProfile = null;
if (cpuProfileStarted) {
    cpuProfile = (await command("Profiler.stop")).profile;
}
let heapProfile = null;
if (heapProfileStarted) {
    heapProfile = (await command("HeapProfiler.stopSampling")).profile;
}

const percentile = (samples, fraction) => {
    if (samples.length === 0) return 0;
    const sorted = [...samples].sort((left, right) => left - right);
    const rank = Math.max(1, Math.ceil(fraction * sorted.length));
    return sorted[Math.min(rank - 1, sorted.length - 1)];
};
const summarize = (samples) => ({
    count: samples.length,
    p50: percentile(samples, 0.50),
    p95: percentile(samples, 0.95),
    p99: percentile(samples, 0.99),
    maximum: samples.length === 0 ? 0 : Math.max(...samples),
});

const cpuHotspotBySite = new Map();
if (cpuProfile) {
    const totalSamples = cpuProfile.nodes.reduce(
        (sum, node) => sum + (node.hitCount ?? 0), 0,
    );
    for (const node of cpuProfile.nodes) {
        const samples = node.hitCount ?? 0;
        if (samples === 0) continue;
        const frame = node.callFrame;
        const key = `${frame.functionName}\n${frame.url}\n${frame.lineNumber}`;
        const current = cpuHotspotBySite.get(key) ?? {
            function: frame.functionName,
            url: frame.url,
            line: frame.lineNumber + 1,
            samples: 0,
            percent: 0,
        };
        current.samples += samples;
        current.percent = totalSamples === 0
            ? 0 : current.samples * 100 / totalSamples;
        cpuHotspotBySite.set(key, current);
    }
}
const cpuHotspots = [...cpuHotspotBySite.values()]
    .sort((left, right) => right.samples - left.samples);

const allocationBySite = new Map();
const visitAllocationNode = (node) => {
    const bytes = node.selfSize ?? 0;
    if (bytes !== 0) {
        const frame = node.callFrame;
        const key = `${frame.functionName}\n${frame.url}\n${frame.lineNumber}`;
        const current = allocationBySite.get(key) ?? {
            function: frame.functionName,
            url: frame.url,
            line: frame.lineNumber + 1,
            bytes: 0,
        };
        current.bytes += bytes;
        allocationBySite.set(key, current);
    }
    for (const child of node.children ?? []) visitAllocationNode(child);
};
if (heapProfile?.head) visitAllocationNode(heapProfile.head);
const allocationHotspots = [...allocationBySite.values()]
    .sort((left, right) => right.bytes - left.bytes)
    .slice(0, 20);

const validStageSamples = [...new Map(capture.stageSamples
    .filter((sample) => sample.totalMs >= 0
        && sample.tick > capture.stageTickFloor
        && sample.tick <= capture.stageTickCeiling)
    .map((sample) => [sample.tick, sample])).values()];
const validRenderStageSamples = [...new Map(capture.renderStageSamples
    .filter((sample) => sample.totalMs >= 0
        && sample.frame > capture.frameCountStart
        && sample.frame <= capture.frameCountEnd)
    .map((sample) => [sample.frame, sample])).values()];
const stageNames = [
    "commands_active_compaction", "ccd", "forces_water",
    "broad_index_build", "broad_index_sort_ranges", "broad_pair_count",
    "broad_pair_scatter", "broad_pair_sort_unique", "broad_lifecycle",
    "narrow_phase", "dynamic_solver", "static_contacts",
    "islands_sleeping", "tick_finalize",
];
const gpuStages = Object.fromEntries(stageNames.map((name, stage) => [
    name,
    summarize(validStageSamples.map((sample) => sample.stages[stage])),
]));
const renderStageNames = [
    "water_simulation", "terrain_raycast", "lighting_blit", "primitives",
];
const renderGpuStages = Object.fromEntries(renderStageNames.map(
    (name, stage) => [
        name,
        summarize(validRenderStageSamples.map(
            (sample) => sample.stages[stage])),
    ],
));
const summedGpuMilliseconds = validStageSamples.reduce(
    (sum, sample) => sum + sample.totalMs, 0,
);
const gpuStageShares = Object.fromEntries(stageNames.map((name, stage) => {
    const milliseconds = validStageSamples.reduce(
        (sum, sample) => sum + sample.stages[stage], 0,
    );
    return [name, {
        milliseconds,
        percent: summedGpuMilliseconds > 0
            ? milliseconds * 100 / summedGpuMilliseconds : 0,
    }];
}));
const traceDurationBySite = new Map();
for (const event of traceEvents) {
    if (event.ph !== "X" || !(event.dur > 0)) continue;
    const category = event.cat ?? "";
    if (!category.toLowerCase().includes("gpu")
        && !category.toLowerCase().includes("dawn")) continue;
    const key = `${category}\n${event.name}`;
    const current = traceDurationBySite.get(key) ?? {
        category,
        name: event.name,
        count: 0,
        durationUs: 0,
    };
    ++current.count;
    current.durationUs += event.dur;
    traceDurationBySite.set(key, current);
}
const traceGpuDurationUs = [...traceDurationBySite.values()]
    .reduce((sum, site) => sum + site.durationUs, 0);
const traceHotspots = [...traceDurationBySite.values()]
    .map((site) => ({
        ...site,
        percent: traceGpuDurationUs > 0
            ? site.durationUs * 100 / traceGpuDurationUs : 0,
    }))
    .sort((left, right) => right.durationUs - left.durationUs)
    .slice(0, 30);
const submittedFrames = capture.frameCountEnd - capture.frameCountStart;
const gpuSamplesPassed = !readyState.profilingEnabled
    || validStageSamples.length >= 10;
const renderGpuSamplesPassed = !readyState.renderProfilingEnabled
    || validRenderStageSamples.length >= 10;
const baseInvariantsPassed = telemetry.physics.backend === "webgpu_soft"
    && telemetry.physics.arithmetic === "fast_float"
    && telemetry.physics.bodies.current === options.clicks * 128
    && !telemetry.physics.candidate_pairs.overflow
    && !telemetry.physics.pairs.overflow
    && !telemetry.physics.contacts.overflow
    && !telemetry.physics.solver_overflow.overflow
    && telemetry.physics.invalid_manifolds === 0
    && telemetry.physics.color_conflicts === 0
    && telemetry.physics.islands.root_errors === 0
    && telemetry.physics.ccd_failures === 0;

const result = {
    schema: "voxys.wasm_click_profile.v1",
    capturedAt: new Date().toISOString(),
    options,
    browser: {
        target: page.url,
        version: browserVersion,
        device: readyState.device,
        canvasWidth: readyState.canvasWidth,
        canvasHeight: readyState.canvasHeight,
        cssWidth: readyState.cssWidth,
        cssHeight: readyState.cssHeight,
        viewportWidth: readyState.viewportWidth,
        viewportHeight: readyState.viewportHeight,
        devicePixelRatio: readyState.devicePixelRatio,
        buildId: readyState.buildId,
        physicsProfiling: readyState.profilingEnabled,
        renderProfiling: readyState.renderProfilingEnabled,
        camera: telemetry.camera,
    },
    workload: {
        requestedClicks: options.clicks,
        expectedBodies: options.clicks * 128,
        observedBodies: telemetry.physics.bodies.current,
        clickInputs,
        benchmarkCamera,
        tick: telemetry.physics.tick,
        candidates: telemetry.physics.candidate_pairs.current,
        contacts: telemetry.physics.contacts.current,
        solverMode: telemetry.physics.solver_mode,
        substeps: telemetry.physics.scheduled_substeps,
    },
    frame: {
        ...summarize(capture.frameMs),
        elapsedMs: capture.elapsedMs,
        submittedFrames,
        throughputFps: capture.elapsedMs > 0
            ? submittedFrames * 1000 / capture.elapsedMs : 0,
    },
    gpu: {
        total: summarize(validStageSamples.map((sample) => sample.totalMs)),
        stages: gpuStages,
        render: {
            total: summarize(validRenderStageSamples.map(
                (sample) => sample.totalMs)),
            stages: renderGpuStages,
        },
        stageShares: gpuStageShares,
        queue: summarize(capture.frameSamples.map((sample) => sample.queue)),
        pacingSkipsStart: capture.frameSamples[0]?.pacingSkips ?? 0,
        pacingSkipsEnd: capture.frameSamples.at(-1)?.pacingSkips ?? 0,
        tickFloor: capture.stageTickFloor,
        tickCeiling: capture.stageTickCeiling,
        samplesPassed: gpuSamplesPassed,
        trace: {
            eventCount: traceEvents.length,
            summedDurationUs: traceGpuDurationUs,
            hotspots: traceHotspots,
        },
        io: telemetry.physics.io,
    },
    cpu: {
        frameMs: summarize(capture.frameSamples.map((sample) => sample.cpuMs)),
        hotspots: cpuHotspots.slice(0, 20),
    },
    memory: {
        peakJsHeapBytes: capture.peakJsHeapBytes,
        wasmBytes: capture.wasmBytes,
        physics: telemetry.physics.memory,
        performanceMetrics,
        allocationHotspots,
    },
    network,
    invariants: {
        backend: telemetry.physics.backend,
        arithmetic: telemetry.physics.arithmetic,
        bodyCountPassed:
            telemetry.physics.bodies.current === options.clicks * 128,
        candidateOverflow: telemetry.physics.candidate_pairs.overflow,
        pairOverflow: telemetry.physics.pairs.overflow,
        contactOverflow: telemetry.physics.contacts.overflow,
        solverOverflow: telemetry.physics.solver_overflow.overflow,
        invalidManifolds: telemetry.physics.invalid_manifolds,
        colorConflicts: telemetry.physics.color_conflicts,
        rootErrors: telemetry.physics.islands.root_errors,
        ccdFailures: telemetry.physics.ccd_failures,
        gpuSamplesPassed,
        renderGpuSamplesPassed,
        overallPassed: baseInvariantsPassed && gpuSamplesPassed
            && renderGpuSamplesPassed
            && diagnostics.length === 0,
    },
    raw: {
        frameMs: capture.frameMs,
        frameSamples: capture.frameSamples,
        gpuStageSamples: validStageSamples,
        renderGpuStageSamples: validRenderStageSamples,
    },
    diagnostics,
    ignoredDiagnostics,
};

console.log(JSON.stringify(result, null, 2));
if (!result.invariants.overallPassed) process.exitCode = 2;
socket.close();
