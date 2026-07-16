import fs from "node:fs";
import path from "node:path";
import { spawn } from "node:child_process";
import { fileURLToPath } from "node:url";

const options = {
    port: 0,
    bodies: [0, 256, 512, 1024, 2048, 4096, 8192, 10112],
    workload: "preset",
    outputDirectory: "",
    durationMs: 15_000,
    durationTicks: 300,
    settleMs: 3_000,
    timeoutMs: 180_000,
    expectedWidth: 5504,
    expectedHeight: 2161,
    profile: false,
    trace: false,
};

const usage = () => console.error(
    "usage: node scripts/profile_wasm_matrix.mjs --port PORT "
    + "--output-dir DIR [--bodies 0,256,512,1024,2048,4096,8192,10112] "
    + "[--workload preset|click-batches] [--duration-ms N] "
    + "[--duration-ticks N] [--settle-ms N] [--timeout-ms N] "
    + "[--expected-width N] [--expected-height N] [--profile] [--trace]",
);

const readInteger = (argument, value) => {
    const result = Number.parseInt(value, 10);
    if (!Number.isInteger(result) || result < 0) {
        throw new Error(`${argument} requires a non-negative integer`);
    }
    return result;
};

for (let index = 2; index < process.argv.length; ++index) {
    const argument = process.argv[index];
    const value = () => process.argv[++index] ?? "";
    if (argument === "--port") options.port = readInteger(argument, value());
    else if (argument === "--output-dir") options.outputDirectory = value();
    else if (argument === "--workload") options.workload = value();
    else if (argument === "--bodies") {
        options.bodies = value().split(",").map((bodyCount) =>
            readInteger(argument, bodyCount));
    } else if (argument === "--duration-ms") {
        options.durationMs = readInteger(argument, value());
    } else if (argument === "--duration-ticks") {
        options.durationTicks = readInteger(argument, value());
    } else if (argument === "--settle-ms") {
        options.settleMs = readInteger(argument, value());
    } else if (argument === "--timeout-ms") {
        options.timeoutMs = readInteger(argument, value());
    } else if (argument === "--expected-width") {
        options.expectedWidth = readInteger(argument, value());
    } else if (argument === "--expected-height") {
        options.expectedHeight = readInteger(argument, value());
    } else if (argument === "--profile") options.profile = true;
    else if (argument === "--trace") options.trace = true;
    else if (argument === "--help" || argument === "-h") {
        usage();
        process.exit(0);
    } else {
        usage();
        throw new Error(`unknown argument: ${argument}`);
    }
}

if (!(options.port > 0) || !options.outputDirectory
    || !["preset", "click-batches"].includes(options.workload)
    || !(options.durationMs > 0) || !(options.durationTicks > 0)
    || !(options.timeoutMs > 0)
    || options.bodies.length === 0) {
    usage();
    process.exit(2);
}
for (const bodyCount of options.bodies) {
    if (options.workload === "click-batches" && bodyCount % 128 !== 0) {
        throw new Error(
            `body count ${bodyCount} is not a multiple of the deterministic `
            + "128-body browser batch",
        );
    }
}

const delay = (milliseconds) => new Promise(
    (resolve) => setTimeout(resolve, milliseconds),
);

const findPage = async () => {
    const deadline = Date.now() + options.timeoutMs;
    while (Date.now() < deadline) {
        try {
            const targets = await (await fetch(
                `http://127.0.0.1:${options.port}/json/list`,
            )).json();
            const page = targets.find((target) =>
                target.type === "page" && target.url.includes("index.html"));
            if (page) return page;
        } catch {
            // Chrome is still starting.
        }
        await delay(100);
    }
    throw new Error("Chrome benchmark page did not start");
};

const preparePage = async (bodyCount) => {
    const page = await findPage();
    const socket = new WebSocket(page.webSocketDebuggerUrl);
    await new Promise((resolve, reject) => {
        socket.addEventListener("open", resolve, { once: true });
        socket.addEventListener("error", reject, { once: true });
    });
    let nextId = 0;
    const pending = new Map();
    socket.addEventListener("message", (event) => {
        const message = JSON.parse(event.data);
        const request = pending.get(message.id);
        if (!request) return;
        pending.delete(message.id);
        if (message.error) request.reject(new Error(message.error.message));
        else request.resolve(message.result);
    });
    const command = (method, params = {}) =>
        new Promise((resolve, reject) => {
            const id = ++nextId;
            pending.set(id, { resolve, reject });
            socket.send(JSON.stringify({ id, method, params }));
        });

    const adapter = await command("Runtime.evaluate", {
        expression: "(async () => !!(await navigator.gpu.requestAdapter(" 
            + "{powerPreference: 'high-performance'})))()",
        awaitPromise: true,
        returnByValue: true,
    });
    if (!adapter.result?.value) {
        socket.close();
        throw new Error("WebGPU adapter is unavailable");
    }
    const currentUrl = new URL(page.url);
    if (!currentUrl.searchParams.has("profileSession")) {
        currentUrl.searchParams.set(
            "profileSession", `${Date.now()}-${process.pid}`);
    }
    currentUrl.searchParams.set("matrixRun", `${Date.now()}-${bodyCount}`);
    if (options.workload === "preset") {
        currentUrl.searchParams.set("benchmarkBodies", String(bodyCount));
    } else {
        currentUrl.searchParams.delete("benchmarkBodies");
    }
    await command("Runtime.evaluate", {
        expression: `location.replace(${JSON.stringify(currentUrl.href)})`,
    });
    socket.close();
};

const scriptDirectory = path.dirname(fileURLToPath(import.meta.url));
const profilerPath = path.join(scriptDirectory, "profile_wasm_clicks.mjs");

const capture = (bodyCount) => new Promise((resolve, reject) => {
    const arguments_ = [
        profilerPath,
        "--port", String(options.port),
        "--duration-ms", String(options.durationMs),
        "--duration-ticks", String(options.durationTicks),
        "--settle-ms", String(options.settleMs),
        "--timeout-ms", String(options.timeoutMs),
        "--expected-width", String(options.expectedWidth),
        "--expected-height", String(options.expectedHeight),
    ];
    if (options.workload === "preset") {
        arguments_.push("--preset-bodies", String(bodyCount));
    } else {
        arguments_.push("--clicks", String(bodyCount / 128));
    }
    if (options.profile) arguments_.push("--profile");
    if (options.trace) arguments_.push("--trace");
    const child = spawn(process.execPath, arguments_, {
        stdio: ["ignore", "pipe", "pipe"],
    });
    let stdout = "";
    let stderr = "";
    child.stdout.setEncoding("utf8");
    child.stderr.setEncoding("utf8");
    child.stdout.on("data", (chunk) => { stdout += chunk; });
    child.stderr.on("data", (chunk) => { stderr += chunk; });
    child.on("error", reject);
    child.on("close", (status) => {
        let profile;
        try {
            profile = JSON.parse(stdout);
        } catch (error) {
            reject(new Error(
                `profile for ${bodyCount} bodies was not JSON: `
                + `${error.message}\n${stderr}`,
            ));
            return;
        }
        if (!profile.browser.physicsProfiling
            || !profile.browser.renderProfiling) {
            reject(new Error(
                "matrix URL must enable ?physicsProfile=1&renderProfile=1",
            ));
            return;
        }
        resolve({ profile, status, stderr });
    });
});

fs.mkdirSync(options.outputDirectory, { recursive: true });
const manifest = {
    schema: "voxys.wasm_profile_matrix.v1",
    capturedAt: new Date().toISOString(),
    options,
    overallPassed: true,
    profiles: [],
};

for (const bodyCount of options.bodies) {
    process.stderr.write(`profiling ${bodyCount} bodies...\n`);
    await preparePage(bodyCount);
    const result = await capture(bodyCount);
    const profile = result.profile;
    const passed = result.status === 0 && profile.invariants?.overallPassed;
    const filename = `bodies-${String(bodyCount).padStart(5, "0")}.json`;
    fs.writeFileSync(
        path.join(options.outputDirectory, filename),
        `${JSON.stringify(profile, null, 2)}\n`,
    );
    const entry = {
        bodies: bodyCount,
        file: filename,
        passed,
        throughputFps: profile.frame.throughputFps,
        frameP50Ms: profile.frame.p50,
        frameP95Ms: profile.frame.p95,
        frameP99Ms: profile.frame.p99,
        physicsGpuP50Ms: profile.gpu.total.p50,
        renderGpuP50Ms: profile.gpu.render.total.p50,
        peakJsHeapBytes: profile.memory.peakJsHeapBytes,
    };
    manifest.overallPassed &&= passed;
    manifest.profiles.push(entry);
    process.stderr.write(
        `  ${entry.throughputFps.toFixed(2)} FPS; `
        + `physics ${entry.physicsGpuP50Ms.toFixed(3)} ms; `
        + `render ${entry.renderGpuP50Ms.toFixed(3)} ms\n`,
    );
    if (!passed) {
        process.stderr.write(
            `  FAILED invariants: ${JSON.stringify(profile.invariants)}\n`,
        );
    }
}

fs.writeFileSync(
    path.join(options.outputDirectory, "manifest.json"),
    `${JSON.stringify(manifest, null, 2)}\n`,
);
console.log(JSON.stringify(manifest, null, 2));
if (!manifest.overallPassed) process.exitCode = 2;
