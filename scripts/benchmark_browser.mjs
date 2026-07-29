#!/usr/bin/env node

import fs from "node:fs";
import http from "node:http";
import os from "node:os";
import path from "node:path";
import process from "node:process";
import { spawn, spawnSync } from "node:child_process";
import { fileURLToPath } from "node:url";

const scriptDirectory = path.dirname(fileURLToPath(import.meta.url));
const repositoryRoot = path.resolve(scriptDirectory, "..");
const deployedUrl = "https://schneiderlo.github.io/voxys/index.html";
const physicsStageNames = [
    "commands_active_compaction",
    "ccd",
    "forces_water",
    "broad_index_build",
    "broad_index_sort_ranges",
    "broad_pair_count",
    "broad_pair_scatter",
    "broad_pair_sort_unique",
    "broad_lifecycle",
    "narrow_phase_bucketing",
    "narrow_phase_collision",
    "dynamic_solver_coloring",
    "dynamic_solver_graph",
    "dynamic_solver_solve",
    "static_contacts",
    "islands_sleeping",
    "tick_finalize",
];
const renderStageNames = [
    "water_simulation",
    "terrain_raycast",
    "lighting_blit",
    "primitives",
];

const options = {
    target: "deployed",
    url: "",
    artifactDirectory: "",
    buildLocal: false,
    bodies: [100, 1_000, 5_000, 10_000],
    modes: ["score", "headroom"],
    runs: 3,
    width: 1920,
    height: 1080,
    devicePixelRatio: 1,
    warmupTicks: 120,
    impactTicks: 300,
    settleTicks: 180,
    bodiesPerVolley: 128,
    ticksPerVolley: 4,
    layout: "pile",
    broadPhaseCellSize: 4,
    pairCapacity: 65_536,
    candidatePairCapacity: 262_144,
    solverWorkgroupSize: 256,
    timeoutMs: 180_000,
    chrome: "",
    displayMode: "auto",
    output: "",
    baseline: "",
    maximumRegressionPercent: 7.5,
    expectedBackend: "webgpu_soft",
    expectedBuild: "",
    keepProfile: false,
    chromeArguments: [],
};

const usage = () => {
    console.log(`Usage:
  node scripts/benchmark_browser.mjs [options]

Targets:
  --target deployed|local       Deployed Pages is the default
  --url URL                     Benchmark an explicit production page
  --artifact-dir DIR            Local voxy_wasm.js/.wasm/.data directory
  --build-local                 Build the Bazel WASM target before local serving

Workload:
  --bodies 100,1000,5000,10000 Exact final body counts
  --modes score,headroom        Add diagnose for GPU timestamp stages
  --runs N                      Repetitions per mode/count (default: 3)
  --warmup-ticks N              Default: 120
  --impact-ticks N              Default: 300
  --settle-ticks N              Default: 180
  --bodies-per-volley N         1..128, default: real 128-body volley
  --ticks-per-volley N          Physics ticks between volleys (default: 4)
  --layout pile|sweep           Fixed player pile (default) or terrain sweep
  --broad-cell-size N           Broad-phase grid size (default: 4)
  --pair-capacity N             Filtered pair capacity (default: 65536)
  --candidate-capacity N        Candidate pair capacity (default: 262144)
  --solver-workgroup N          128 or 256 (default: 256)
  --quick                       One short correctness run

Browser:
  --resolution WIDTHxHEIGHT     CSS viewport size (default: 1920x1080)
  --dpr N                       Device pixel ratio; page caps it at 1.5
  --chrome PATH                 Chrome/Chromium executable
  --headed | --headless         Auto-selects from DISPLAY by default
  --chrome-arg ARG              Repeat for additional Chrome arguments
  --timeout-ms N                Per workload timeout

Results:
  --output FILE                 JSON output (default: timestamped /tmp file)
  --baseline FILE               Compare against an earlier result
  --max-regression-percent N    Baseline failure threshold (default: 7.5)
  --expected-backend NAME       Default: webgpu_soft
  --expect-build SHA            Reject a different deployed build
  --keep-profile                Keep the temporary isolated Chrome profile
  --help

Examples:
  node scripts/benchmark_browser.mjs
  node scripts/benchmark_browser.mjs --modes score,headroom,diagnose --runs 5
  node scripts/benchmark_browser.mjs --target local --build-local --quick
`);
};

const readInteger = (name, value, { allowZero = false } = {}) => {
    const text = String(value ?? "");
    const parsed = /^\d+$/.test(text) ? Number(text) : Number.NaN;
    if (!Number.isSafeInteger(parsed) || parsed < (allowZero ? 0 : 1)) {
        throw new Error(`${name} requires ${allowZero ? "a non-negative" : "a positive"} integer`);
    }
    return parsed;
};

const readNumber = (name, value) => {
    const parsed = Number(value);
    if (!Number.isFinite(parsed) || parsed <= 0) {
        throw new Error(`${name} requires a positive number`);
    }
    return parsed;
};

for (let index = 2; index < process.argv.length; ++index) {
    const argument = process.argv[index];
    const value = () => process.argv[++index] ?? "";
    switch (argument) {
        case "--target":
            options.target = value();
            break;
        case "--url":
            options.url = value();
            break;
        case "--artifact-dir":
            options.artifactDirectory = path.resolve(value());
            break;
        case "--build-local":
            options.buildLocal = true;
            break;
        case "--bodies":
            options.bodies = value().split(",").map((bodyCount) =>
                readInteger(argument, bodyCount));
            break;
        case "--modes":
        case "--mode":
            options.modes = value().split(",").filter(Boolean);
            break;
        case "--runs":
            options.runs = readInteger(argument, value());
            break;
        case "--resolution": {
            const match = /^(\d+)x(\d+)$/i.exec(value());
            if (!match) throw new Error("--resolution requires WIDTHxHEIGHT");
            options.width = readInteger(argument, match[1]);
            options.height = readInteger(argument, match[2]);
            break;
        }
        case "--dpr":
            options.devicePixelRatio = readNumber(argument, value());
            break;
        case "--warmup-ticks":
            options.warmupTicks = readInteger(
                argument, value(), { allowZero: true });
            break;
        case "--impact-ticks":
            options.impactTicks = readInteger(argument, value());
            break;
        case "--settle-ticks":
            options.settleTicks = readInteger(argument, value());
            break;
        case "--bodies-per-volley":
            options.bodiesPerVolley = readInteger(argument, value());
            break;
        case "--ticks-per-volley":
            options.ticksPerVolley = readInteger(argument, value());
            break;
        case "--layout":
            options.layout = value();
            break;
        case "--broad-cell-size":
            options.broadPhaseCellSize = readNumber(argument, value());
            break;
        case "--pair-capacity":
            options.pairCapacity = readInteger(argument, value());
            break;
        case "--candidate-capacity":
            options.candidatePairCapacity = readInteger(argument, value());
            break;
        case "--solver-workgroup":
            options.solverWorkgroupSize = readInteger(argument, value());
            break;
        case "--timeout-ms":
            options.timeoutMs = readInteger(argument, value());
            break;
        case "--chrome":
            options.chrome = path.resolve(value());
            break;
        case "--headed":
            options.displayMode = "headed";
            break;
        case "--headless":
            options.displayMode = "headless";
            break;
        case "--chrome-arg":
            options.chromeArguments.push(value());
            break;
        case "--output":
            options.output = path.resolve(value());
            break;
        case "--baseline":
            options.baseline = path.resolve(value());
            break;
        case "--max-regression-percent":
            options.maximumRegressionPercent = readNumber(argument, value());
            break;
        case "--expected-backend":
            options.expectedBackend = value();
            break;
        case "--expect-build":
            options.expectedBuild = value();
            break;
        case "--keep-profile":
            options.keepProfile = true;
            break;
        case "--quick":
            options.runs = 1;
            options.warmupTicks = 30;
            options.impactTicks = 60;
            options.settleTicks = 30;
            options.modes = ["score"];
            break;
        case "--help":
        case "-h":
            usage();
            process.exit(0);
            break;
        default:
            throw new Error(`unknown argument: ${argument}`);
    }
}

if (!["deployed", "local"].includes(options.target)) {
    throw new Error("--target must be deployed or local");
}
if (options.bodies.length === 0
    || options.bodies.some((bodyCount) => bodyCount > 131_072)) {
    throw new Error("--bodies must contain values from 1 through 131072");
}
if (new Set(options.bodies).size !== options.bodies.length) {
    throw new Error("--bodies cannot contain duplicates");
}
if (options.modes.length === 0
    || options.modes.some((mode) =>
        !["score", "headroom", "diagnose"].includes(mode))) {
    throw new Error("--modes accepts score, headroom, and diagnose");
}
if (new Set(options.modes).size !== options.modes.length) {
    throw new Error("--modes cannot contain duplicates");
}
if (options.bodiesPerVolley > 128) {
    throw new Error("--bodies-per-volley cannot exceed the real 128-body volley");
}
if (options.ticksPerVolley > 3_600) {
    throw new Error("--ticks-per-volley cannot exceed 3600");
}
if (!["pile", "sweep"].includes(options.layout)) {
    throw new Error("--layout must be pile or sweep");
}
const cellsPerSector = 256 / options.broadPhaseCellSize;
if (!Number.isInteger(cellsPerSector)
    || cellsPerSector < 1 || cellsPerSector > 2_097_152) {
    throw new Error("--broad-cell-size must evenly divide 256");
}
if (options.candidatePairCapacity < options.pairCapacity) {
    throw new Error("--candidate-capacity cannot be smaller than --pair-capacity");
}
if (![128, 256].includes(options.solverWorkgroupSize)) {
    throw new Error("--solver-workgroup must be 128 or 256");
}
if (options.width > 16_384 || options.height > 16_384
    || options.devicePixelRatio > 4) {
    throw new Error("requested browser dimensions are unreasonable");
}
const effectiveRenderDpr = Math.min(options.devicePixelRatio, 1.5);
const expectedCanvasWidth = Math.floor(options.width * effectiveRenderDpr);
const expectedCanvasHeight = Math.floor(options.height * effectiveRenderDpr);

const delay = (milliseconds) => new Promise(
    (resolve) => setTimeout(resolve, milliseconds),
);

const percentile = (samples, fraction) => {
    if (samples.length === 0) return 0;
    const sorted = [...samples].sort((left, right) => left - right);
    const rank = Math.max(1, Math.ceil(fraction * sorted.length));
    return sorted[Math.min(rank - 1, sorted.length - 1)];
};

const median = (samples) => samples.length === 0
    ? null : percentile(samples, 0.5);

const summarize = (samples) => ({
    count: samples.length,
    p50: percentile(samples, 0.50),
    p95: percentile(samples, 0.95),
    p99: percentile(samples, 0.99),
    maximum: samples.length === 0 ? 0 : Math.max(...samples),
});

const summarizeGpuStages = (samples, names) => {
    const stages = {};
    for (let stage = 0; stage < names.length; ++stage) {
        stages[names[stage]] = summarize(samples.map(
            (sample) => sample.stages[stage],
        ).filter((value) => Number.isFinite(value) && value >= 0));
    }
    const totals = samples.map((sample) =>
        sample.stages.every((value) => Number.isFinite(value) && value >= 0)
            ? sample.stages.reduce((sum, value) => sum + value, 0)
            : -1,
    ).filter((value) => value >= 0);
    return { total: summarize(totals), stages };
};

const executableFromPath = (name) => {
    for (const directory of (process.env.PATH ?? "").split(path.delimiter)) {
        if (!directory) continue;
        const candidate = path.join(directory, name);
        try {
            fs.accessSync(candidate, fs.constants.X_OK);
            return candidate;
        } catch {
            // Keep searching.
        }
    }
    return "";
};

const findChrome = () => {
    const explicit = options.chrome || process.env.VOXYS_CHROME || "";
    if (explicit) {
        fs.accessSync(explicit, fs.constants.X_OK);
        return explicit;
    }
    const names = process.platform === "win32"
        ? ["chrome.exe", "msedge.exe"]
        : ["google-chrome", "google-chrome-stable", "chromium", "chromium-browser"];
    for (const name of names) {
        const candidate = executableFromPath(name);
        if (candidate) return candidate;
    }
    const fixed = process.platform === "darwin"
        ? ["/Applications/Google Chrome.app/Contents/MacOS/Google Chrome"]
        : ["/usr/bin/google-chrome", "/usr/bin/chromium"];
    for (const candidate of fixed) {
        try {
            fs.accessSync(candidate, fs.constants.X_OK);
            return candidate;
        } catch {
            // Keep searching.
        }
    }
    throw new Error("Chrome was not found; pass --chrome or set VOXYS_CHROME");
};

const gitBuildId = () => {
    const result = spawnSync("git", ["rev-parse", "HEAD"], {
        cwd: repositoryRoot,
        encoding: "utf8",
    });
    if (result.status !== 0) return "local";
    const dirty = spawnSync("git", ["status", "--porcelain"], {
        cwd: repositoryRoot,
        encoding: "utf8",
    }).stdout.trim();
    return `${result.stdout.trim()}${dirty ? "-dirty" : ""}`;
};

const mimeType = (filename) => {
    switch (path.extname(filename)) {
        case ".html": return "text/html; charset=utf-8";
        case ".js":
        case ".mjs": return "text/javascript; charset=utf-8";
        case ".css": return "text/css; charset=utf-8";
        case ".wasm": return "application/wasm";
        case ".data": return "application/octet-stream";
        case ".json": return "application/json";
        case ".png": return "image/png";
        case ".jpg":
        case ".jpeg": return "image/jpeg";
        default: return "application/octet-stream";
    }
};

const findArtifactDirectory = () => {
    if (options.artifactDirectory) return options.artifactDirectory;
    for (const candidate of [
        path.join(repositoryRoot, "bazel-bin", "voxy_wasm"),
        path.join(repositoryRoot, "build-wasm", "bin"),
    ]) {
        if (fs.existsSync(path.join(candidate, "voxy_wasm.js"))
            || fs.existsSync(path.join(candidate, "voxy_wasm_cc.js"))) {
            return candidate;
        }
    }
    throw new Error(
        "local voxy_wasm.js was not found; pass --build-local or --artifact-dir",
    );
};

const buildLocalArtifact = () => {
    const result = spawnSync(
        "nix",
        ["develop", "-c", "bazel", "build", "--config=wasm", "//:voxy_wasm"],
        { cwd: repositoryRoot, stdio: "inherit" },
    );
    if (result.status !== 0) {
        throw new Error(`local WASM build failed with status ${result.status}`);
    }
};

const findBazelWasmArtifactDirectory = () => {
    const queryArguments = [
        "cquery",
        "--config=wasm",
        "//:voxy_wasm",
        "--output=files",
    ];
    const invocations = [
        ["bazel", queryArguments],
        ["nix", ["develop", "-c", "bazel", ...queryArguments]],
    ];
    for (const [executable, arguments_] of invocations) {
        const result = spawnSync(executable, arguments_, {
            cwd: repositoryRoot,
            encoding: "utf8",
        });
        if (result.status !== 0) continue;
        const artifact = result.stdout.split(/\r?\n/).map(
            (line) => line.trim(),
        ).find((line) =>
            /(?:^|\/)voxy_wasm\/voxy_wasm(?:_cc)?\.js$/.test(line));
        if (!artifact) continue;
        const directory = path.dirname(path.resolve(repositoryRoot, artifact));
        if (fs.existsSync(path.join(directory, path.basename(artifact)))) {
            return directory;
        }
    }
    return "";
};

const startLocalServer = async (artifactDirectory) => {
    const webDirectory = path.join(repositoryRoot, "web");
    const localBuildId = gitBuildId();
    const artifactBase = fs.existsSync(
        path.join(artifactDirectory, "voxy_wasm.js"))
        ? "voxy_wasm" : "voxy_wasm_cc";
    const artifactNames = new Set([
        "voxy_wasm.js",
        "voxy_wasm.wasm",
        "voxy_wasm.data",
        "voxy_wasm.worker.js",
        "voxy_wasm_cc.js",
        "voxy_wasm_cc.wasm",
        "voxy_wasm_cc.data",
        "voxy_wasm_cc.worker.js",
    ]);
    const server = http.createServer((request, response) => {
        const requestUrl = new URL(request.url ?? "/", "http://127.0.0.1");
        let relative;
        try {
            relative = decodeURIComponent(requestUrl.pathname)
                .replace(/^\/+/, "");
        } catch {
            response.writeHead(400);
            response.end();
            return;
        }
        if (!relative) relative = "index.html";
        if (relative.includes("..") || path.isAbsolute(relative)) {
            response.writeHead(403);
            response.end();
            return;
        }

        const basename = path.basename(relative);
        const artifactFilename = basename.startsWith("voxy_wasm_cc.")
            ? basename
            : basename.replace("voxy_wasm", artifactBase);
        const candidates = artifactNames.has(basename)
            ? [path.join(artifactDirectory, artifactFilename)]
            : [
                path.join(webDirectory, relative),
                path.join(repositoryRoot, relative),
            ];
        const filename = candidates.find((candidate) => {
            try {
                return fs.statSync(candidate).isFile();
            } catch {
                return false;
            }
        });
        if (!filename) {
            response.writeHead(404);
            response.end("Not found");
            return;
        }

        const headers = {
            "Content-Type": mimeType(filename),
            "Cache-Control": "no-store",
        };
        if (relative === "index.html") {
            const contents = fs.readFileSync(filename, "utf8")
                .replaceAll("__VOXY_BUILD_ID__", localBuildId);
            response.writeHead(200, {
                ...headers,
                "Content-Length": Buffer.byteLength(contents),
            });
            if (request.method !== "HEAD") response.end(contents);
            else response.end();
            return;
        }

        const stat = fs.statSync(filename);
        response.writeHead(200, {
            ...headers,
            "Content-Length": stat.size,
        });
        if (request.method === "HEAD") response.end();
        else fs.createReadStream(filename).pipe(response);
    });
    await new Promise((resolve, reject) => {
        server.once("error", reject);
        server.listen(0, "127.0.0.1", resolve);
    });
    const address = server.address();
    return {
        url: `http://127.0.0.1:${address.port}/index.html`,
        close: () => new Promise((resolve) => server.close(resolve)),
    };
};

class CdpSession {
    constructor(webSocketUrl) {
        this.socket = new WebSocket(webSocketUrl);
        this.nextId = 1;
        this.pending = new Map();
        this.listeners = new Set();
    }

    async open() {
        await new Promise((resolve, reject) => {
            this.socket.addEventListener("open", resolve, { once: true });
            this.socket.addEventListener("error", reject, { once: true });
        });
        this.socket.addEventListener("message", (event) => {
            const message = JSON.parse(event.data);
            if (message.method) {
                for (const listener of this.listeners) listener(message);
            }
            const pending = this.pending.get(message.id);
            if (!pending) return;
            this.pending.delete(message.id);
            if (message.error) {
                pending.reject(new Error(message.error.message));
            } else {
                pending.resolve(message.result);
            }
        });
        this.socket.addEventListener("close", () => {
            for (const pending of this.pending.values()) {
                pending.reject(new Error("Chrome debugging connection closed"));
            }
            this.pending.clear();
        });
    }

    onEvent(listener) {
        this.listeners.add(listener);
        return () => this.listeners.delete(listener);
    }

    command(method, params = {}) {
        return new Promise((resolve, reject) => {
            const id = this.nextId++;
            this.pending.set(id, { resolve, reject });
            this.socket.send(JSON.stringify({ id, method, params }));
        });
    }

    async evaluate(expression) {
        const result = await this.command("Runtime.evaluate", {
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
    }

    close() {
        this.socket.close();
    }
}

const launchChrome = async () => {
    const executable = findChrome();
    const profileDirectory = fs.mkdtempSync(
        path.join(os.tmpdir(), "voxys-browser-benchmark-"),
    );
    const hasDisplay = Boolean(
        process.env.DISPLAY || process.env.WAYLAND_DISPLAY,
    );
    const headless = options.displayMode === "headless"
        || (options.displayMode === "auto" && !hasDisplay);
    const arguments_ = [
        "--remote-debugging-port=0",
        `--user-data-dir=${profileDirectory}`,
        "--no-first-run",
        "--no-default-browser-check",
        "--disable-background-networking",
        `--window-size=${options.width},${options.height}`,
        `--force-device-scale-factor=${options.devicePixelRatio}`,
    ];
    if (headless) arguments_.push("--headless=new");
    if (process.platform === "linux") {
        arguments_.push(
            "--enable-unsafe-webgpu",
            "--enable-features=Vulkan",
            "--use-angle=vulkan",
        );
    }
    arguments_.push(...options.chromeArguments, "about:blank");

    const child = spawn(executable, arguments_, {
        cwd: repositoryRoot,
        stdio: ["ignore", "pipe", "pipe"],
    });
    const browserLog = [];
    const captureLog = (chunk) => {
        browserLog.push(...String(chunk).split("\n").filter(Boolean));
        if (browserLog.length > 200) {
            browserLog.splice(0, browserLog.length - 200);
        }
    };
    child.stdout.on("data", captureLog);
    child.stderr.on("data", captureLog);

    const activePortFile = path.join(profileDirectory, "DevToolsActivePort");
    const deadline = Date.now() + 30_000;
    let port = 0;
    while (Date.now() < deadline && child.exitCode === null) {
        try {
            port = Number.parseInt(
                fs.readFileSync(activePortFile, "utf8").split("\n")[0],
                10,
            );
            if (port > 0) break;
        } catch {
            // Chrome has not written the endpoint yet.
        }
        await delay(50);
    }
    if (!(port > 0)) {
        child.kill("SIGTERM");
        throw new Error(
            `Chrome did not expose a debugging port:\n${browserLog.join("\n")}`,
        );
    }

    let page;
    while (Date.now() < deadline) {
        try {
            const targets = await (
                await fetch(`http://127.0.0.1:${port}/json/list`)
            ).json();
            page = targets.find((target) => target.type === "page");
            if (page) break;
        } catch {
            // DevTools HTTP endpoint is still starting.
        }
        await delay(50);
    }
    if (!page) {
        child.kill("SIGTERM");
        throw new Error("Chrome did not create a page target");
    }

    const cdp = new CdpSession(page.webSocketDebuggerUrl);
    await cdp.open();
    return {
        cdp,
        child,
        executable,
        arguments: arguments_,
        profileDirectory,
        port,
        headless,
        browserLog,
    };
};

const stopChrome = async (browser) => {
    const waitForExit = async (timeoutMs) => {
        if (browser.child.exitCode !== null) return true;
        return Promise.race([
            new Promise((resolve) => {
                browser.child.once("exit", () => resolve(true));
            }),
            delay(timeoutMs).then(() => false),
        ]);
    };

    let exited = browser.child.exitCode !== null;
    if (!exited) {
        try {
            await browser.cdp.command("Browser.close");
        } catch {
            // Closing the browser can close CDP before its response arrives.
        }
        exited = await waitForExit(3_000);
    }
    if (!exited) {
        browser.child.kill("SIGTERM");
        exited = await waitForExit(3_000);
    }
    if (!exited) {
        browser.child.kill("SIGKILL");
        await waitForExit(3_000);
    }
    browser.cdp.close();

    if (!options.keepProfile) {
        let lastError;
        for (let attempt = 0; attempt < 20; ++attempt) {
            try {
                fs.rmSync(browser.profileDirectory, {
                    recursive: true,
                    force: true,
                    maxRetries: 3,
                    retryDelay: 100,
                });
                return;
            } catch (error) {
                lastError = error;
                await delay(100);
            }
        }
        console.warn(
            `warning: could not remove Chrome profile `
            + `${browser.profileDirectory}: ${lastError?.message}`,
        );
    }
};

const safeOrigin = (value) => {
    try {
        return new URL(value).origin;
    } catch {
        return "";
    }
};

const createDiagnostics = (targetUrl) => ({
    targetOrigin: safeOrigin(targetUrl),
    requests: new Map(),
    firstPartyFailures: [],
    exceptions: [],
    consoleErrors: [],
    ignored: [],
    requestCount: 0,
    responseBytes: 0,
});

const handleDiagnosticEvent = (diagnostics, message) => {
    if (message.method === "Network.requestWillBeSent") {
        const request = message.params.request;
        diagnostics.requests.set(message.params.requestId, request.url);
        ++diagnostics.requestCount;
    } else if (message.method === "Network.responseReceived") {
        const response = message.params.response;
        if (response.status >= 400
            && safeOrigin(response.url) === diagnostics.targetOrigin) {
            diagnostics.firstPartyFailures.push({
                url: response.url,
                error: `HTTP ${response.status}`,
                canceled: false,
            });
        }
    } else if (message.method === "Network.loadingFinished") {
        diagnostics.responseBytes += message.params.encodedDataLength ?? 0;
    } else if (message.method === "Network.loadingFailed") {
        const url = diagnostics.requests.get(message.params.requestId) ?? "";
        const entry = {
            url,
            error: message.params.errorText,
            canceled: Boolean(message.params.canceled),
        };
        if (!entry.canceled && safeOrigin(url) === diagnostics.targetOrigin) {
            diagnostics.firstPartyFailures.push(entry);
        } else {
            diagnostics.ignored.push(entry);
        }
    } else if (message.method === "Runtime.exceptionThrown") {
        const details = message.params.exceptionDetails;
        diagnostics.exceptions.push(
            details.exception?.description ?? details.text,
        );
    } else if (message.method === "Runtime.consoleAPICalled"
               && message.params.type === "error") {
        diagnostics.consoleErrors.push(message.params.args.map(
            (argument) => argument.value ?? argument.description ?? "",
        ).join(" "));
    } else if (message.method === "Log.entryAdded"
               && message.params.entry.level === "error") {
        const entry = message.params.entry;
        const sameOrigin = entry.url
            && safeOrigin(entry.url) === diagnostics.targetOrigin;
        if (sameOrigin || !entry.text.startsWith("Failed to load resource")) {
            diagnostics.consoleErrors.push(entry.text);
        } else {
            diagnostics.ignored.push({
                url: entry.url ?? "",
                error: entry.text,
            });
        }
    }
};

const readReadyStateExpression = `(() => {
    const moduleReady = typeof voxyModule !== "undefined"
        && voxyModule?._voxy_is_initialized?.() === 1;
    let telemetry = null;
    if (moduleReady) {
        const pointer = voxyModule._voxy_get_telemetry_json?.();
        if (pointer) {
            telemetry = JSON.parse(voxyModule.UTF8ToString(pointer));
        }
    }
    const canvas = document.getElementById("voxy-canvas");
    const bounds = canvas?.getBoundingClientRect();
    const error = document.getElementById("error");
    return {
        href: location.href,
        initialized: moduleReady,
        journeyApi: typeof voxyModule?._voxy_start_browser_journey_benchmark
            === "function",
        frame: moduleReady ? voxyModule._voxy_get_frame_count() : 0,
        tick: telemetry?.physics?.tick ?? 0,
        backend: telemetry?.physics?.backend ?? null,
        arithmetic: telemetry?.physics?.arithmetic ?? null,
        physicsConfiguration: {
            broadPhaseCellSize:
                telemetry?.physics?.broad_phase_cell_size ?? null,
            pairCapacity: telemetry?.physics?.pairs?.capacity ?? null,
            candidatePairCapacity:
                telemetry?.physics?.candidate_pairs?.capacity ?? null,
            solverWorkgroupSize:
                telemetry?.physics?.solver_workgroup_size ?? null,
        },
        render: telemetry?.render ?? null,
        canvasWidth: canvas?.width ?? 0,
        canvasHeight: canvas?.height ?? 0,
        cssWidth: bounds?.width ?? 0,
        cssHeight: bounds?.height ?? 0,
        viewportWidth: innerWidth,
        viewportHeight: innerHeight,
        devicePixelRatio,
        visibility: document.visibilityState,
        device: globalThis.voxyDeviceProfile ?? null,
        buildId: globalThis.voxyBuildId ?? null,
        errorVisible: error
            ? getComputedStyle(error).display !== "none"
            : false,
        errorText: error?.textContent ?? "",
    };
})()`;

const waitForReady = async (cdp, deadline) => {
    let state = null;
    let stableCanvasPolls = 0;
    while (Date.now() < deadline) {
        try {
            state = await cdp.evaluate(readReadyStateExpression);
            if (state.errorVisible) {
                throw new Error(`WASM application failed: ${state.errorText}`);
            }
            const exactCanvas = state.canvasWidth === expectedCanvasWidth
                && state.canvasHeight === expectedCanvasHeight
                && state.viewportWidth === options.width
                && state.viewportHeight === options.height
                && Math.abs(state.devicePixelRatio
                            - options.devicePixelRatio) < 1.0e-6;
            stableCanvasPolls = exactCanvas ? stableCanvasPolls + 1 : 0;
            if (state.initialized && stableCanvasPolls >= 3) {
                if (!state.journeyApi) {
                    throw new Error(
                        "WASM artifact lacks the browser journey API; "
                        + "rebuild it with --build-local",
                    );
                }
                if (state.tick >= 30) return state;
            }
        } catch (error) {
            if (String(error).includes("WASM application failed")
                || String(error).includes("lacks the browser journey API")) {
                throw error;
            }
            // Navigation replaces the execution context.
        }
        await delay(100);
    }
    throw new Error(`browser readiness timed out: ${JSON.stringify(state)}`);
};

const drainGpuExpression = `(() => {
    const physics = [];
    if (typeof voxyModule?._voxy_poll_physics_stage_timing === "function") {
        for (;;) {
            const tick = voxyModule._voxy_poll_physics_stage_timing();
            if (!(tick > 0)) break;
            const stages = [];
            for (let stage = 0; stage < ${physicsStageNames.length}; ++stage) {
                stages.push(
                    voxyModule._voxy_get_polled_physics_stage_ms(stage));
            }
            physics.push({tick, stages});
        }
    }
    const render = [];
    if (typeof voxyModule?._voxy_poll_render_stage_timing === "function") {
        for (;;) {
            const frame = voxyModule._voxy_poll_render_stage_timing();
            if (!(frame > 0)) break;
            const stages = [];
            for (let stage = 0; stage < ${renderStageNames.length}; ++stage) {
                stages.push(
                    voxyModule._voxy_get_polled_render_stage_ms(stage));
            }
            render.push({frame, stages});
        }
    }
    return {
        status: voxyModule._voxy_get_browser_journey_benchmark_status(),
        frame: voxyModule._voxy_get_frame_count(),
        tick: voxyModule._voxy_get_physics_encoded_tick(),
        queue: voxyModule._voxy_get_gpu_frames_in_flight(),
        pacingSkips: voxyModule._voxy_get_gpu_pacing_skips(),
        physics,
        render,
    };
})()`;

const readTelemetryExpression = `(() => {
    const pointer = voxyModule._voxy_get_telemetry_json();
    return JSON.parse(voxyModule.UTF8ToString(pointer));
})()`;

const readJourneyExpression = `(() => {
    const pointer = voxyModule._voxy_get_browser_journey_benchmark_json();
    return pointer
        ? JSON.parse(voxyModule.UTF8ToString(pointer))
        : null;
})()`;

const runWorkload = async (
    browser, targetUrl, mode, bodyCount, iteration, sequence,
) => {
    const { cdp } = browser;
    const profileEnabled = mode === "diagnose";
    const uncapped = mode === "headroom";
    const runId = `${Date.now()}-${mode}-${bodyCount}-${iteration}`;
    const url = new URL(targetUrl);
    url.searchParams.set("physicsBackend", "webgpu");
    url.searchParams.set("telemetry", "0");
    url.searchParams.set("browserBenchmarkRun", runId);
    url.searchParams.set(
        "broadPhaseCellSize", String(options.broadPhaseCellSize));
    url.searchParams.set("physicsPairCapacity", String(options.pairCapacity));
    url.searchParams.set(
        "physicsCandidatePairCapacity",
        String(options.candidatePairCapacity));
    url.searchParams.set(
        "physicsSolverWorkgroup", String(options.solverWorkgroupSize));
    url.searchParams.delete("benchmarkBodies");
    url.searchParams.delete("renderThroughput");
    if (profileEnabled) {
        url.searchParams.set("physicsProfile", "1");
        url.searchParams.set("renderProfile", "1");
    } else {
        url.searchParams.delete("physicsProfile");
        url.searchParams.delete("renderProfile");
    }

    const diagnostics = createDiagnostics(url.href);
    const removeListener = cdp.onEvent((message) =>
        handleDiagnosticEvent(diagnostics, message));
    const deadline = Date.now() + options.timeoutMs;
    try {
        await cdp.command("Page.navigate", { url: url.href });
        const ready = await waitForReady(cdp, deadline);
        if (ready.backend !== options.expectedBackend
            || ready.arithmetic !== "fast_float"
            || ready.render?.path !== "raycast"
            || ready.render?.terrain_width !== 8192
            || ready.render?.terrain_height !== 8192
            || ready.render?.terrain_mips !== 14) {
            throw new Error(`unexpected production workload: ${JSON.stringify(ready)}`);
        }
        const appliedPhysics = ready.physicsConfiguration;
        const cellSizeMatches = Number.isFinite(
            appliedPhysics?.broadPhaseCellSize)
            && Math.abs(
                appliedPhysics.broadPhaseCellSize
                    - options.broadPhaseCellSize)
                <= Math.max(1e-6, options.broadPhaseCellSize * 1e-6);
        if (!cellSizeMatches
            || appliedPhysics?.pairCapacity !== options.pairCapacity
            || appliedPhysics?.candidatePairCapacity
                !== options.candidatePairCapacity
            || appliedPhysics?.solverWorkgroupSize
                !== options.solverWorkgroupSize) {
            throw new Error(
                "browser did not apply the requested physics configuration: "
                + JSON.stringify(appliedPhysics),
            );
        }
        if (ready.visibility !== "visible") {
            throw new Error(`benchmark page is ${ready.visibility}, not visible`);
        }
        if (ready.device?.adapter?.fallback) {
            throw new Error("Chrome selected a fallback WebGPU adapter");
        }
        if (options.expectedBuild
            && ready.buildId !== options.expectedBuild) {
            throw new Error(
                `deployed build ${ready.buildId} does not match ${options.expectedBuild}`,
            );
        }

        const initialUncapped = await cdp.evaluate(
            "voxyModule._voxy_get_uncapped_fps() === 1",
        );
        const loopModeRequestFrame = await cdp.evaluate(
            "voxyModule._voxy_get_frame_count()",
        );
        await cdp.evaluate(
            `voxyModule._voxy_set_uncapped_fps(${uncapped ? 1 : 0})`,
        );
        let loopModeReady = false;
        while (Date.now() < deadline) {
            loopModeReady = await cdp.evaluate(
                `voxyModule._voxy_get_uncapped_fps() === ${uncapped ? 1 : 0}`
                + ` && voxyModule._voxy_get_frame_count()`
                + ` > ${loopModeRequestFrame}`,
            );
            if (loopModeReady) break;
            await delay(25);
        }
        if (!loopModeReady) throw new Error("browser loop mode did not change");

        const start = await cdp.evaluate(`({
            frame: voxyModule._voxy_get_frame_count(),
            tick: voxyModule._voxy_get_physics_encoded_tick(),
            queue: voxyModule._voxy_get_gpu_frames_in_flight(),
            pacingSkips: voxyModule._voxy_get_gpu_pacing_skips(),
        })`);
        const started = await cdp.evaluate(
            `voxyModule._voxy_start_browser_journey_benchmark(`
            + `${bodyCount},${options.warmupTicks},${options.impactTicks},`
            + `${options.settleTicks},${options.bodiesPerVolley},`
            + `${options.ticksPerVolley},`
            + `${options.layout === "pile" ? 0 : 1})`,
        );
        if (started !== 1) {
            throw new Error("engine rejected the browser journey");
        }

        const physicsGpuSamples = [];
        const renderGpuSamples = [];
        let progress = null;
        while (Date.now() < deadline) {
            progress = await cdp.evaluate(drainGpuExpression);
            if (profileEnabled) {
                physicsGpuSamples.push(...progress.physics);
                renderGpuSamples.push(...progress.render);
            }
            if (progress.status === 5 || progress.status === -1) break;
            await delay(profileEnabled || uncapped ? 25 : 100);
        }
        if (!progress || ![5, -1].includes(progress.status)) {
            throw new Error(`journey timed out: ${JSON.stringify(progress)}`);
        }

        const journey = await cdp.evaluate(readJourneyExpression);
        if (!journey) throw new Error("engine returned no journey result");

        // Let final telemetry and timestamp readbacks retire before validating.
        const telemetryDeadline = Math.min(deadline, Date.now() + 5_000);
        let telemetry = null;
        while (Date.now() < telemetryDeadline) {
            const drained = await cdp.evaluate(drainGpuExpression);
            if (profileEnabled) {
                physicsGpuSamples.push(...drained.physics);
                renderGpuSamples.push(...drained.render);
            }
            telemetry = await cdp.evaluate(readTelemetryExpression);
            if (telemetry.physics.tick >= journey.clock.final_physics_tick) break;
            await delay(50);
        }
        const end = await cdp.evaluate(`({
            frame: voxyModule._voxy_get_frame_count(),
            tick: voxyModule._voxy_get_physics_encoded_tick(),
            queue: voxyModule._voxy_get_gpu_frames_in_flight(),
            pacingSkips: voxyModule._voxy_get_gpu_pacing_skips(),
            visibility: document.visibilityState,
            canvas: {
                width: document.getElementById("voxy-canvas")?.width ?? 0,
                height: document.getElementById("voxy-canvas")?.height ?? 0,
                cssWidth: document.getElementById("voxy-canvas")
                    ?.getBoundingClientRect().width ?? 0,
                cssHeight: document.getElementById("voxy-canvas")
                    ?.getBoundingClientRect().height ?? 0,
            },
            jsHeapBytes: performance.memory?.usedJSHeapSize ?? null,
            wasmBytes: voxyModule.HEAPU8?.buffer?.byteLength ?? null,
        })`);
        const performanceMetrics = Object.fromEntries(
            (await cdp.command("Performance.getMetrics")).metrics.map(
                (metric) => [metric.name, metric.value],
            ),
        );

        const physics = telemetry.physics;
        const finalCellSizeMatches = Number.isFinite(
            physics.broad_phase_cell_size)
            && Math.abs(
                physics.broad_phase_cell_size
                    - options.broadPhaseCellSize)
                <= Math.max(1e-6, options.broadPhaseCellSize * 1e-6);
        const invariants = {
            journeyPassed: journey.passed,
            layoutPassed: journey.config?.layout === options.layout,
            physicsConfigurationPassed:
                finalCellSizeMatches
                && physics.pairs.capacity === options.pairCapacity
                && physics.candidate_pairs.capacity
                    === options.candidatePairCapacity
                && physics.solver_workgroup_size
                    === options.solverWorkgroupSize,
            bodyCountPassed:
                physics.bodies.current
                === journey.counts.expected_final_bodies,
            renderBodyRangePassed:
                journey.peaks.submitted_primitives
                >= journey.counts.expected_final_bodies,
            terrainContactObserved:
                journey.peaks.terrain_contact_bodies > 0,
            observedCapacityOverflowMask:
                journey.peaks.capacity_overflow_mask,
            observedPhysicsErrorMask:
                journey.peaks.physics_error_mask,
            candidateOverflow: physics.candidate_pairs.overflow,
            pairOverflow: physics.pairs.overflow,
            contactOverflow: physics.contacts.overflow,
            solverOverflow: physics.solver_overflow.overflow,
            invalidManifolds: physics.invalid_manifolds,
            colorConflicts: physics.color_conflicts,
            rootErrors: physics.islands.root_errors,
            ccdFailures: physics.ccd_failures,
            pageVisibleAtEnd: end.visibility === "visible",
            canvasPassed: end.canvas.width === expectedCanvasWidth
                && end.canvas.height === expectedCanvasHeight,
            initialUncappedPassed: initialUncapped,
            firstPartyFailures: diagnostics.firstPartyFailures.length,
            exceptions: diagnostics.exceptions.length,
            consoleErrors: diagnostics.consoleErrors.length,
            gpuSamplesPassed: !profileEnabled
                || !ready.device?.timestampQuery
                || physicsGpuSamples.length > 0,
            renderGpuSamplesPassed: !profileEnabled
                || !ready.device?.timestampQuery
                || renderGpuSamples.length > 0,
        };
        invariants.overallPassed = invariants.journeyPassed
            && invariants.layoutPassed
            && invariants.physicsConfigurationPassed
            && invariants.bodyCountPassed
            && invariants.renderBodyRangePassed
            && invariants.terrainContactObserved
            && invariants.observedCapacityOverflowMask === 0
            && invariants.observedPhysicsErrorMask === 0
            && !invariants.candidateOverflow
            && !invariants.pairOverflow
            && !invariants.contactOverflow
            && !invariants.solverOverflow
            && invariants.invalidManifolds === 0
            && invariants.colorConflicts === 0
            && invariants.rootErrors === 0
            && invariants.ccdFailures === 0
            && invariants.pageVisibleAtEnd
            && invariants.canvasPassed
            && invariants.initialUncappedPassed
            && invariants.firstPartyFailures === 0
            && invariants.exceptions === 0
            && invariants.consoleErrors === 0
            && invariants.gpuSamplesPassed
            && invariants.renderGpuSamplesPassed;

        return {
            schema: "voxys.browser_benchmark_run.v1",
            mode,
            bodyCount,
            iteration,
            sequence,
            capturedAt: new Date().toISOString(),
            passed: invariants.overallPassed,
            browser: {
                target: url.href,
                buildId: ready.buildId,
                device: ready.device,
                version: await cdp.command("Browser.getVersion"),
                executable: browser.executable,
                arguments: browser.arguments,
                headless: browser.headless,
                initialUncapped,
                authoritativeExperienceScore: !browser.headless
                    && mode === "score",
                canvas: end.canvas,
                viewport: {
                    width: ready.viewportWidth,
                    height: ready.viewportHeight,
                    devicePixelRatio: ready.devicePixelRatio,
                },
            },
            journey,
            gpu: profileEnabled ? {
                physics: summarizeGpuStages(
                    physicsGpuSamples, physicsStageNames),
                render: summarizeGpuStages(
                    renderGpuSamples, renderStageNames),
                rawPhysics: physicsGpuSamples,
                rawRender: renderGpuSamples,
            } : null,
            pacing: {
                frameStart: start.frame,
                frameEnd: end.frame,
                physicsTickStart: start.tick,
                physicsTickEnd: end.tick,
                queueStart: start.queue,
                queueEnd: end.queue,
                skipsStart: start.pacingSkips,
                skipsEnd: end.pacingSkips,
                skips: end.pacingSkips - start.pacingSkips,
            },
            physics: {
                backend: physics.backend,
                arithmetic: physics.arithmetic,
                configuration: {
                    broadPhaseCellSize: physics.broad_phase_cell_size,
                    pairCapacity: physics.pairs.capacity,
                    candidatePairCapacity:
                        physics.candidate_pairs.capacity,
                    solverWorkgroupSize:
                        physics.solver_workgroup_size,
                },
                candidates: physics.candidate_pairs.current,
                pairs: physics.pairs.current,
                contacts: physics.contacts.current,
                manifolds: physics.manifolds.current,
                terrainContacts: physics.terrain_contacts.current,
                terrainContactBodies: physics.terrain_contact_bodies,
                narrowPairClasses: physics.narrow_pair_classes,
                narrowCollisionPairClasses:
                    physics.narrow_collision_pair_classes,
                graphColors: physics.graph_colors,
                solverOverflowContacts:
                    physics.solver_overflow.current,
                maximumBodyDegree: physics.maximum_body_degree,
                sleepingBodies: physics.sleeping_bodies,
                memory: physics.memory,
                io: physics.io,
            },
            render: {
                submittedPrimitives: telemetry.render.submitted_primitives,
            },
            memory: {
                jsHeapBytes: end.jsHeapBytes,
                wasmBytes: end.wasmBytes,
                performanceMetrics,
            },
            network: {
                requests: diagnostics.requestCount,
                responseBytes: diagnostics.responseBytes,
            },
            invariants,
            diagnostics: {
                firstPartyFailures: diagnostics.firstPartyFailures,
                exceptions: diagnostics.exceptions,
                consoleErrors: diagnostics.consoleErrors,
                ignored: diagnostics.ignored,
            },
        };
    } finally {
        removeListener();
    }
};

const summarizeRuns = (runs) => {
    const groups = new Map();
    for (const run of runs) {
        const key = `${run.mode}:${run.bodyCount}`;
        const group = groups.get(key) ?? [];
        group.push(run);
        groups.set(key, group);
    }
    return [...groups.entries()].map(([key, group]) => {
        const [mode, bodyText] = key.split(":");
        const passedRuns = group.filter((run) => run.passed);
        const measuredRuns = group.filter((run) =>
            run.journey?.passed
            && Number.isFinite(run.journey?.frame?.fps));
        const values = (read) => measuredRuns.map(read);
        const gpuValues = (read) => measuredRuns
            .map(read).filter((value) => Number.isFinite(value));
        return {
            mode,
            bodies: Number(bodyText),
            runs: group.length,
            passedRuns: passedRuns.length,
            measuredRuns: measuredRuns.length,
            passed: passedRuns.length === group.length,
            medianFps: median(values((run) => run.journey.frame.fps)),
            medianLowOnePercentFps: median(values(
                (run) => run.journey.frame.low_1_percent_fps)),
            medianP95Ms: median(values(
                (run) => run.journey.frame.p95_ms)),
            medianP99Ms: median(values(
                (run) => run.journey.frame.p99_ms)),
            medianMaximumMs: median(values(
                (run) => run.journey.frame.max_ms)),
            medianMissed60Hz: median(values(
                (run) => run.journey.frame.missed_deadlines_60hz)),
            medianCpuP95Ms: median(values(
                (run) => run.journey.cpu.p95_ms)),
            medianPacingSkips: median(values(
                (run) => run.pacing.skips)),
            medianCandidatePairsPeak: median(values(
                (run) => run.journey.peaks.candidate_pairs)),
            medianTerrainContactBodiesPeak: median(values(
                (run) => run.journey.peaks.terrain_contact_bodies)),
            medianSubmittedPrimitivesPeak: median(values(
                (run) => run.journey.peaks.submitted_primitives)),
            medianPhysicsGpuP50Ms: median(gpuValues(
                (run) => run.gpu?.physics?.total?.p50)),
            medianRenderGpuP50Ms: median(gpuValues(
                (run) => run.gpu?.render?.total?.p50)),
        };
    }).sort((left, right) =>
        options.modes.indexOf(left.mode) - options.modes.indexOf(right.mode)
        || left.bodies - right.bodies);
};

const compareBaseline = (summary, baseline, runs) => {
    const compatibilityErrors = [];
    if (baseline.schema !== "voxys.browser_benchmark.v1") {
        compatibilityErrors.push("baseline schema differs");
    }
    for (const field of ["bodies", "modes"]) {
        if (JSON.stringify(baseline.options?.[field])
            !== JSON.stringify(options[field])) {
            compatibilityErrors.push(`option ${field} differs`);
        }
    }
    for (const [field, currentValue] of Object.entries({
        runs: options.runs,
        width: options.width,
        height: options.height,
        devicePixelRatio: options.devicePixelRatio,
        expectedCanvasWidth,
        expectedCanvasHeight,
        warmupTicks: options.warmupTicks,
        impactTicks: options.impactTicks,
        settleTicks: options.settleTicks,
        bodiesPerVolley: options.bodiesPerVolley,
        ticksPerVolley: options.ticksPerVolley,
        layout: options.layout,
        broadPhaseCellSize: options.broadPhaseCellSize,
        pairCapacity: options.pairCapacity,
        candidatePairCapacity: options.candidatePairCapacity,
        solverWorkgroupSize: options.solverWorkgroupSize,
        expectedBackend: options.expectedBackend,
    })) {
        if (baseline.options?.[field] !== currentValue) {
            compatibilityErrors.push(`option ${field} differs`);
        }
    }

    const baselineRun = baseline.runs?.[0];
    const currentRun = runs[0];
    if (!baselineRun || !currentRun) {
        compatibilityErrors.push("browser/GPU identity is missing");
    } else {
        const identities = {
            headless: [
                baselineRun.browser?.headless,
                currentRun.browser?.headless,
            ],
            browser: [
                baselineRun.browser?.version?.product,
                currentRun.browser?.version?.product,
            ],
            executable: [
                baselineRun.browser?.executable,
                currentRun.browser?.executable,
            ],
            gpu: [
                baselineRun.browser?.device?.adapter,
                currentRun.browser?.device?.adapter,
            ],
            canvas: [
                baselineRun.browser?.canvas,
                currentRun.browser?.canvas,
            ],
        };
        for (const [name, [previous, current]] of Object.entries(identities)) {
            if (JSON.stringify(previous) !== JSON.stringify(current)) {
                compatibilityErrors.push(`${name} differs`);
            }
        }
        const normalizeArguments = (arguments_) => (arguments_ ?? [])
            .filter((argument) => !argument.startsWith("--user-data-dir="));
        if (JSON.stringify(normalizeArguments(
                baselineRun.browser?.arguments))
            !== JSON.stringify(normalizeArguments(
                currentRun.browser?.arguments))) {
            compatibilityErrors.push("browser arguments differ");
        }
    }

    const baselineRows = new Map((baseline.summary ?? []).map(
        (row) => [`${row.mode}:${row.bodies}`, row],
    ));
    const rows = [];
    const regressions = [];
    for (const current of summary) {
        const previous = baselineRows.get(`${current.mode}:${current.bodies}`);
        if (!previous) {
            compatibilityErrors.push(
                `baseline row ${current.mode}:${current.bodies} is missing`,
            );
            continue;
        }
        const percent = (currentValue, previousValue) =>
            Number.isFinite(currentValue)
                && Number.isFinite(previousValue)
                && previousValue !== 0
                ? (currentValue - previousValue) * 100 / previousValue
                : null;
        const comparison = {
            mode: current.mode,
            bodies: current.bodies,
            fpsPercent: percent(
                current.medianFps, previous.medianFps),
            p95Percent: percent(
                current.medianP95Ms, previous.medianP95Ms),
            p99Percent: percent(
                current.medianP99Ms, previous.medianP99Ms),
            cpuP95Percent: percent(
                current.medianCpuP95Ms, previous.medianCpuP95Ms),
        };
        rows.push(comparison);
        if (comparison.fpsPercent !== null
            && comparison.fpsPercent
                < -options.maximumRegressionPercent) {
            regressions.push({
                ...comparison,
                metric: "fps",
                regressionPercent: -comparison.fpsPercent,
            });
        }
        if (comparison.p95Percent !== null
            && comparison.p95Percent
                > options.maximumRegressionPercent) {
            regressions.push({
                ...comparison,
                metric: "p95_ms",
                regressionPercent: comparison.p95Percent,
            });
        }
    }
    return {
        source: options.baseline,
        maximumRegressionPercent: options.maximumRegressionPercent,
        rows,
        compatibilityErrors,
        regressions,
        passed: compatibilityErrors.length === 0
            && regressions.length === 0,
    };
};

const formatNumber = (value, digits = 2) =>
    Number.isFinite(value) ? value.toFixed(digits) : "-";

const describeFailure = (run) => {
    const reasons = [];
    if (!run.journey.passed) {
        reasons.push(run.journey.failure ?? "journey");
    }
    if (!run.invariants.layoutPassed) reasons.push("layout");
    if (!run.invariants.physicsConfigurationPassed) {
        reasons.push("physics-configuration");
    }
    if (!run.invariants.bodyCountPassed) reasons.push("body-count");
    if (!run.invariants.renderBodyRangePassed) reasons.push("render-range");
    if (!run.invariants.terrainContactObserved) reasons.push("no-terrain-contact");
    const overflowMask = run.invariants.observedCapacityOverflowMask;
    if ((overflowMask & 1) !== 0) reasons.push("candidate-overflow");
    if ((overflowMask & 2) !== 0) reasons.push("pair-overflow");
    if ((overflowMask & 4) !== 0) reasons.push("contact-overflow");
    if ((overflowMask & 8) !== 0) reasons.push("solver-overflow");
    if (run.invariants.candidateOverflow && (overflowMask & 1) === 0) {
        reasons.push("candidate-overflow");
    }
    if (run.invariants.pairOverflow && (overflowMask & 2) === 0) {
        reasons.push("pair-overflow");
    }
    if (run.invariants.contactOverflow && (overflowMask & 4) === 0) {
        reasons.push("contact-overflow");
    }
    if (run.invariants.solverOverflow && (overflowMask & 8) === 0) {
        reasons.push("solver-overflow");
    }
    if (run.invariants.observedPhysicsErrorMask !== 0) {
        reasons.push(
            `physics-errors=0x${run.invariants.observedPhysicsErrorMask.toString(16)}`,
        );
    }
    if (run.invariants.invalidManifolds !== 0
        || run.invariants.colorConflicts !== 0
        || run.invariants.rootErrors !== 0
        || run.invariants.ccdFailures !== 0) {
        reasons.push("physics-correctness");
    }
    if (run.invariants.firstPartyFailures !== 0) reasons.push("network");
    if (run.invariants.exceptions !== 0) reasons.push("exception");
    if (run.invariants.consoleErrors !== 0) reasons.push("console");
    if (!run.invariants.canvasPassed) reasons.push("canvas");
    if (!run.invariants.initialUncappedPassed) {
        reasons.push("initial-loop-capped");
    }
    if (!run.invariants.pageVisibleAtEnd) reasons.push("page-hidden");
    if (!run.invariants.gpuSamplesPassed
        || !run.invariants.renderGpuSamplesPassed) {
        reasons.push("gpu-timestamps");
    }
    return reasons.join(", ") || "invariant";
};

const printSummary = (summary) => {
    console.log("");
    console.log(
        "mode       bodies  pass  fps       1% low    p95 ms   p99 ms   "
        + "miss60  cpu95   terrain  physicsGPU  renderGPU",
    );
    for (const row of summary) {
        console.log(
            `${row.mode.padEnd(10)} `
            + `${String(row.bodies).padStart(6)}  `
            + `${`${row.passedRuns}/${row.runs}`.padStart(4)}  `
            + `${formatNumber(row.medianFps).padStart(8)}  `
            + `${formatNumber(row.medianLowOnePercentFps).padStart(8)}  `
            + `${formatNumber(row.medianP95Ms).padStart(7)}  `
            + `${formatNumber(row.medianP99Ms).padStart(7)}  `
            + `${formatNumber(row.medianMissed60Hz, 0).padStart(6)}  `
            + `${formatNumber(row.medianCpuP95Ms).padStart(6)}  `
            + `${formatNumber(
                row.medianTerrainContactBodiesPeak, 0).padStart(7)}  `
            + `${formatNumber(row.medianPhysicsGpuP50Ms).padStart(10)}  `
            + `${formatNumber(row.medianRenderGpuP50Ms).padStart(9)}`,
        );
    }
};

let localServer = null;
let browser = null;
try {
    if (options.target === "local" && options.buildLocal) {
        buildLocalArtifact();
    }
    if (options.target === "local" && !options.url) {
        const artifactDirectory = options.artifactDirectory
            ? findArtifactDirectory()
            : findBazelWasmArtifactDirectory() || findArtifactDirectory();
        localServer = await startLocalServer(artifactDirectory);
    }
    const targetUrl = options.url
        || localServer?.url
        || deployedUrl;
    new URL(targetUrl);

    browser = await launchChrome();
    const { cdp } = browser;
    await cdp.command("Runtime.enable");
    await cdp.command("Log.enable");
    await cdp.command("Network.enable");
    await cdp.command("Performance.enable");
    await cdp.command("Page.enable");
    await cdp.command("Network.setCacheDisabled", { cacheDisabled: false });
    await cdp.command("Emulation.setDeviceMetricsOverride", {
        width: options.width,
        height: options.height,
        deviceScaleFactor: options.devicePixelRatio,
        mobile: false,
        screenWidth: options.width,
        screenHeight: options.height,
    });
    await cdp.command("Page.addScriptToEvaluateOnNewDocument", {
        source: `(() => {
            try {
                localStorage.clear();
                sessionStorage.clear();
            } catch {
                // about:blank and opaque origins may reject storage access.
            }
            globalThis.__voxyBrowserBenchmark = true;
        })();`,
    });

    console.log(`target: ${targetUrl}`);
    console.log(
        `browser: ${browser.executable} (${browser.headless ? "headless" : "headed"})`,
    );
    console.log(
        `viewport: ${options.width}x${options.height} @ DPR `
        + `${options.devicePixelRatio}; canvas: `
        + `${expectedCanvasWidth}x${expectedCanvasHeight}`,
    );

    const runs = [];
    let sequence = 0;
    for (const mode of options.modes) {
        for (let iteration = 0; iteration < options.runs; ++iteration) {
            const bodyOrder = iteration % 2 === 0
                ? options.bodies
                : [...options.bodies].reverse();
            for (const bodyCount of bodyOrder) {
                ++sequence;
                process.stdout.write(
                    `[${sequence}/${options.modes.length
                        * options.runs * options.bodies.length}] `
                    + `${mode} ${bodyCount} bodies... `,
                );
                const run = await runWorkload(
                    browser, targetUrl, mode, bodyCount, iteration, sequence,
                );
                runs.push(run);
                console.log(
                    `${run.passed ? "PASS" : "FAIL"} `
                    + `${formatNumber(run.journey.frame.fps)} FPS, `
                    + `p95 ${formatNumber(run.journey.frame.p95_ms)} ms`
                    + `${run.passed ? "" : ` [${describeFailure(run)}]`}`,
                );
            }
        }
    }

    const summary = summarizeRuns(runs);
    const baseline = options.baseline
        ? JSON.parse(fs.readFileSync(options.baseline, "utf8"))
        : null;
    const comparison = baseline
        ? compareBaseline(summary, baseline, runs) : null;
    const result = {
        schema: "voxys.browser_benchmark.v1",
        capturedAt: new Date().toISOString(),
        target: targetUrl,
        options: {
            bodies: options.bodies,
            modes: options.modes,
            runs: options.runs,
            width: options.width,
            height: options.height,
            devicePixelRatio: options.devicePixelRatio,
            expectedCanvasWidth,
            expectedCanvasHeight,
            warmupTicks: options.warmupTicks,
            impactTicks: options.impactTicks,
            settleTicks: options.settleTicks,
            bodiesPerVolley: options.bodiesPerVolley,
            ticksPerVolley: options.ticksPerVolley,
            layout: options.layout,
            broadPhaseCellSize: options.broadPhaseCellSize,
            pairCapacity: options.pairCapacity,
            candidatePairCapacity: options.candidatePairCapacity,
            solverWorkgroupSize: options.solverWorkgroupSize,
            expectedBackend: options.expectedBackend,
            expectedBuild: options.expectedBuild || null,
        },
        browser: {
            executable: browser.executable,
            arguments: browser.arguments,
            headless: browser.headless,
            profileDirectory: options.keepProfile
                ? browser.profileDirectory : null,
        },
        overallPassed: runs.every((run) => run.passed)
            && (!comparison || comparison.passed),
        summary,
        comparison,
        runs,
    };

    if (!options.output) {
        const stamp = new Date().toISOString().replaceAll(":", "-");
        options.output = path.join(
            os.tmpdir(), `voxys-browser-benchmark-${stamp}.json`);
    }
    fs.mkdirSync(path.dirname(options.output), { recursive: true });
    fs.writeFileSync(options.output, `${JSON.stringify(result, null, 2)}\n`);
    printSummary(summary);
    if (comparison) {
        console.log("");
        if (comparison.compatibilityErrors.length !== 0) {
            console.log(
                "baseline comparison: INCOMPATIBLE — "
                + comparison.compatibilityErrors.join(", "),
            );
        } else {
            console.log(
                comparison.passed
                    ? "baseline comparison: PASS"
                    : `baseline comparison: `
                        + `${comparison.regressions.length} regression(s)`,
            );
        }
    }
    console.log(`\nresult: ${options.output}`);
    if (!result.overallPassed) process.exitCode = 2;
} catch (error) {
    console.error(`browser benchmark failed: ${error.stack ?? error}`);
    if (browser?.browserLog?.length) {
        console.error("recent Chrome log:");
        console.error(browser.browserLog.slice(-40).join("\n"));
    }
    process.exitCode = 1;
} finally {
    if (browser) await stopChrome(browser);
    if (localServer) await localServer.close();
}
