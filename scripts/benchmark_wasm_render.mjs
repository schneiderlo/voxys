import fs from "node:fs";

const options = {
    port: 0,
    expectedWidth: 0,
    expectedHeight: 0,
    warmupFrames: 128,
    measuredFrames: 1500,
    batchFrames: 64,
    repeats: 3,
    minimumFps: 700,
    timeoutMs: 180_000,
    outputPath: "",
};

const usage = () => console.error(
    "usage: node scripts/benchmark_wasm_render.mjs --port PORT "
    + "--expected-width PX --expected-height PX "
    + "[--warmup-frames N] [--measured-frames N] [--batch-frames N] "
    + "[--repeats N] [--minimum-fps N] [--timeout-ms N] "
    + "[--output FILE]",
);

const readInteger = (argument, value, allowZero = false) => {
    const result = Number.parseInt(value, 10);
    if (!Number.isInteger(result) || (allowZero ? result < 0 : result <= 0)) {
        throw new Error(
            `${argument} requires a ${allowZero ? "non-negative" : "positive"}`
            + " integer",
        );
    }
    return result;
};

const readNumber = (argument, value) => {
    const result = Number.parseFloat(value);
    if (!Number.isFinite(result) || result <= 0) {
        throw new Error(`${argument} requires a positive number`);
    }
    return result;
};

for (let index = 2; index < process.argv.length; ++index) {
    const argument = process.argv[index];
    const value = () => process.argv[++index] ?? "";
    if (argument === "--port") {
        options.port = readInteger(argument, value());
    } else if (argument === "--expected-width") {
        options.expectedWidth = readInteger(argument, value());
    } else if (argument === "--expected-height") {
        options.expectedHeight = readInteger(argument, value());
    } else if (argument === "--warmup-frames") {
        options.warmupFrames = readInteger(argument, value(), true);
    } else if (argument === "--measured-frames") {
        options.measuredFrames = readInteger(argument, value());
    } else if (argument === "--batch-frames") {
        options.batchFrames = readInteger(argument, value());
    } else if (argument === "--repeats") {
        options.repeats = readInteger(argument, value());
    } else if (argument === "--minimum-fps") {
        options.minimumFps = readNumber(argument, value());
    } else if (argument === "--timeout-ms") {
        options.timeoutMs = readInteger(argument, value());
    } else if (argument === "--output") {
        options.outputPath = value();
    } else if (argument === "--help" || argument === "-h") {
        usage();
        process.exit(0);
    } else {
        usage();
        throw new Error(`unknown argument: ${argument}`);
    }
}

if (options.port <= 0 || options.expectedWidth <= 0
    || options.expectedHeight <= 0
    || options.batchFrames > options.measuredFrames) {
    usage();
    process.exit(2);
}

const delay = (milliseconds) => new Promise(
    (resolve) => setTimeout(resolve, milliseconds),
);
const deadline = () => Date.now() + options.timeoutMs;

const findPage = async () => {
    const end = deadline();
    while (Date.now() < end) {
        try {
            const response = await fetch(
                `http://127.0.0.1:${options.port}/json/list`,
            );
            const targets = await response.json();
            const page = targets.find((target) =>
                target.type === "page" && target.url.includes("index.html"));
            if (page) return page;
        } catch {
            // Chrome may still be starting.
        }
        await delay(100);
    }
    throw new Error("WASM render benchmark browser target did not start");
};

const page = await findPage();
const socket = new WebSocket(page.webSocketDebuggerUrl);
await new Promise((resolve, reject) => {
    socket.addEventListener("open", resolve, { once: true });
    socket.addEventListener("error", reject, { once: true });
});

let nextId = 1;
const pending = new Map();
const diagnostics = [];
socket.addEventListener("message", (event) => {
    const message = JSON.parse(event.data);
    if (message.method === "Runtime.exceptionThrown") {
        const details = message.params.exceptionDetails;
        diagnostics.push(details.exception?.description ?? details.text);
    } else if (message.method === "Log.entryAdded"
               && message.params.entry.level === "error") {
        diagnostics.push(message.params.entry.text);
    }
    const request = pending.get(message.id);
    if (!request) return;
    pending.delete(message.id);
    if (message.error) request.reject(new Error(message.error.message));
    else request.resolve(message.result);
});

const command = (method, params = {}) => new Promise((resolve, reject) => {
    const id = nextId++;
    pending.set(id, { resolve, reject });
    socket.send(JSON.stringify({ id, method, params }));
});

const evaluate = async (expression) => {
    const response = await command("Runtime.evaluate", {
        expression,
        returnByValue: true,
        awaitPromise: true,
    });
    if (response.exceptionDetails) {
        throw new Error(
            response.exceptionDetails.exception?.description
            ?? response.exceptionDetails.text,
        );
    }
    return response.result?.value;
};

await command("Runtime.enable");
await command("Log.enable");
const browser = await command("Browser.getVersion");

const sampleState = () => evaluate(`(() => {
    const moduleReady = typeof voxyModule !== "undefined"
        && voxyModule !== null;
    const initialized = moduleReady
        && voxyModule._voxy_is_initialized?.() === 1;
    let telemetry = null;
    if (initialized) {
        const pointer = voxyModule._voxy_get_telemetry_json();
        if (pointer) {
            telemetry = JSON.parse(voxyModule.UTF8ToString(pointer));
        }
    }
    const canvas = document.getElementById("voxy-canvas");
    const error = document.getElementById("error");
    return {
        initialized,
        href: location.href,
        throughputMode: new URLSearchParams(location.search)
            .get("renderThroughput") === "1",
        width: canvas?.width ?? 0,
        height: canvas?.height ?? 0,
        device: globalThis.voxyDeviceProfile ?? null,
        gpuErrors: [...(globalThis.voxyUncapturedGpuErrors ?? [])],
        deviceLost: globalThis.voxyDeviceLost ?? null,
        telemetry,
        errorVisible: error
            ? getComputedStyle(error).display !== "none" : false,
        errorText: error?.textContent ?? "",
    };
})()`);

let initial;
const initializationDeadline = deadline();
while (Date.now() < initializationDeadline) {
    initial = await sampleState();
    if (initial.errorVisible) {
        throw new Error(`WASM application failed: ${initial.errorText}`);
    }
    if (initial.deviceLost || initial.gpuErrors.length !== 0) {
        throw new Error(
            `WebGPU initialization error: ${JSON.stringify({
                deviceLost: initial.deviceLost,
                errors: initial.gpuErrors,
            })}`,
        );
    }
    if (initial.initialized
        && initial.width === options.expectedWidth
        && initial.height === options.expectedHeight) break;
    await delay(100);
}
if (!initial?.initialized
    || initial.width !== options.expectedWidth
    || initial.height !== options.expectedHeight) {
    throw new Error(
        "WASM render benchmark initialization/resize timed out: "
        + JSON.stringify(initial),
    );
}

const adapter = initial.device?.adapter;
const render = initial.telemetry?.render;
const throughput = initial.telemetry?.render_throughput;
if (!initial.throughputMode || !throughput?.full_quality) {
    throw new Error(
        "page is not in the full-quality renderThroughput=1 mode",
    );
}
if (initial.width !== options.expectedWidth
    || initial.height !== options.expectedHeight
    || throughput.width !== options.expectedWidth
    || throughput.height !== options.expectedHeight) {
    throw new Error(
        `unexpected framebuffer ${initial.width}x${initial.height}; expected `
        + `${options.expectedWidth}x${options.expectedHeight}`,
    );
}
const adapterIdentity = `${adapter?.vendor ?? ""} `
    + `${adapter?.architecture ?? ""} ${adapter?.device ?? ""} `
    + `${adapter?.description ?? ""}`;
const softwareAdapter = /swiftshader|software|llvmpipe/i.test(adapterIdentity)
    || adapter?.vendor?.toLowerCase() === "google";
if (!adapter || adapter.fallback !== false || softwareAdapter) {
    throw new Error(
        `hardware WebGPU adapter required: ${JSON.stringify(adapter)}`,
    );
}
if (render?.path !== "raycast" || render.terrain_width !== 8192
    || render.terrain_height !== 8192 || render.terrain_mips !== 14) {
    throw new Error(`unexpected render workload: ${JSON.stringify(render)}`);
}
if (initial.telemetry?.physics?.backend !== "webgpu_soft") {
    throw new Error(
        `unexpected physics backend: ${initial.telemetry?.physics?.backend}`,
    );
}

const runs = [];
for (let repeat = 0; repeat < options.repeats; ++repeat) {
    const started = await evaluate(
        `voxyModule._voxy_start_render_throughput_benchmark(`
        + `${options.warmupFrames}, ${options.measuredFrames}, `
        + `${options.batchFrames})`,
    );
    if (started !== 1) {
        throw new Error(`render benchmark run ${repeat + 1} did not start`);
    }

    let state;
    const runDeadline = deadline();
    while (Date.now() < runDeadline) {
        state = await sampleState();
        if (state.deviceLost || state.gpuErrors.length !== 0) {
            throw new Error(
                `WebGPU render error: ${JSON.stringify({
                    deviceLost: state.deviceLost,
                    errors: state.gpuErrors,
                })}`,
            );
        }
        const status = state.telemetry?.render_throughput?.status;
        if (status === 4 || status === -1) break;
        await delay(20);
    }
    const result = state?.telemetry?.render_throughput;
    if (result?.status !== 4) {
        throw new Error(
            `render benchmark run ${repeat + 1} failed or timed out: `
            + JSON.stringify(result),
        );
    }
    if (!result.full_quality
        || result.width !== options.expectedWidth
        || result.height !== options.expectedHeight
        || result.submitted_frames !== options.measuredFrames
        || result.completed_frames !== options.measuredFrames
        || result.batch_frames !== options.batchFrames
        || result.static_cache_frames !== options.measuredFrames
        || result.geometry_water_frames !== options.measuredFrames
        || result.terrain_cache_refreshes !== 5
        || !(result.encoding_ms >= 0)
        || !(result.elapsed_ms > 0) || !(result.fps > 0)) {
        throw new Error(
            `invalid render benchmark run ${repeat + 1}: `
            + JSON.stringify(result),
        );
    }
    runs.push({
        repeat: repeat + 1,
        frames: result.completed_frames,
        elapsed_ms: result.elapsed_ms,
        encoding_ms: result.encoding_ms,
        terrain_cache_refreshes: result.terrain_cache_refreshes,
        static_cache_frames: result.static_cache_frames,
        geometry_water_frames: result.geometry_water_frames,
        fps: result.fps,
    });
    console.log(
        `run ${repeat + 1}/${options.repeats}: `
        + `${result.fps.toFixed(2)} FPS, `
        + `${result.completed_frames} completed frames, `
        + `${result.elapsed_ms.toFixed(2)} ms`,
    );
}

const fpsValues = runs.map((run) => run.fps).sort((a, b) => a - b);
const minimumFps = fpsValues[0];
const medianFps = fpsValues[Math.floor(fpsValues.length / 2)];
const totalFrames = runs.reduce((sum, run) => sum + run.frames, 0);
const totalElapsedMs = runs.reduce((sum, run) => sum + run.elapsed_ms, 0);
const aggregateFps = totalFrames * 1000 / totalElapsedMs;
const report = {
    schema_version: 1,
    browser: browser.product,
    user_agent: browser.userAgent,
    adapter,
    device_profile: initial.device,
    framebuffer: {
        width: options.expectedWidth,
        height: options.expectedHeight,
    },
    quality: {
        full_quality: true,
        render_path: render.path,
        terrain_width: render.terrain_width,
        terrain_height: render.terrain_height,
        terrain_mips: render.terrain_mips,
        physics_backend: initial.telemetry.physics.backend,
    },
    benchmark: {
        warmup_frames: options.warmupFrames,
        measured_frames_per_run: options.measuredFrames,
        batch_frames: options.batchFrames,
        repeats: options.repeats,
        completion_verified: true,
        minimum_required_fps: options.minimumFps,
    },
    runs,
    minimum_fps: minimumFps,
    median_fps: medianFps,
    aggregate_fps: aggregateFps,
    passed: minimumFps >= options.minimumFps,
    diagnostics,
};

if (options.outputPath) {
    fs.writeFileSync(options.outputPath, `${JSON.stringify(report, null, 2)}\n`);
}
console.log(
    `queue-retired full-quality result: min ${minimumFps.toFixed(2)}, `
    + `median ${medianFps.toFixed(2)}, aggregate `
    + `${aggregateFps.toFixed(2)} FPS at `
    + `${options.expectedWidth}x${options.expectedHeight}`,
);

socket.close();
if (!report.passed) {
    throw new Error(
        `minimum ${minimumFps.toFixed(2)} FPS is below `
        + `${options.minimumFps.toFixed(2)} FPS`,
    );
}
