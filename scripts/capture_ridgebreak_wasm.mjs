import fs from "node:fs";
import path from "node:path";

const debugPort = Number.parseInt(process.argv[2] ?? "0", 10);
const outputPath = path.resolve(process.argv[3] ?? "ridgebreak-wasm.png");
if (!Number.isInteger(debugPort) || debugPort <= 0) {
    throw new Error(
        "usage: capture_ridgebreak_wasm.mjs <devtools-port> [output.png]",
    );
}

const timeoutMs = Number.parseInt(
    process.env.RIDGEBREAK_CAPTURE_TIMEOUT_MS ?? "120000", 10,
);
const commandTimeoutMs = Number.parseInt(
    process.env.RIDGEBREAK_CAPTURE_COMMAND_TIMEOUT_MS ?? "15000", 10,
);
if (!Number.isInteger(timeoutMs) || timeoutMs <= 0
    || !Number.isInteger(commandTimeoutMs) || commandTimeoutMs <= 0) {
    throw new Error(
        "RIDGEBREAK capture timeouts must be positive integer milliseconds",
    );
}
const driveMs = Number.parseInt(
    process.env.RIDGEBREAK_CAPTURE_DRIVE_MS ?? "2500", 10,
);
const settleMs = Number.parseInt(
    process.env.RIDGEBREAK_CAPTURE_SETTLE_MS ?? "750", 10,
);
const toggleRenderPath =
    process.env.RIDGEBREAK_CAPTURE_TOGGLE_PATH === "1";
const deadline = Date.now() + timeoutMs;
const delay = milliseconds => new Promise(
    resolve => setTimeout(resolve, milliseconds),
);

let target;
while (Date.now() < deadline) {
    try {
        const response = await fetch(
            `http://127.0.0.1:${debugPort}/json/list`,
        );
        const targets = await response.json();
        target = targets.find(candidate =>
            candidate.type === "page"
            && candidate.url.includes("index.html"));
        if (target) break;
    } catch {
        // Browser startup is asynchronous.
    }
    await delay(100);
}
if (!target) throw new Error("RIDGEBREAK browser target did not start");

const socket = new WebSocket(target.webSocketDebuggerUrl);
await new Promise((resolve, reject) => {
    socket.addEventListener("open", resolve, {once: true});
    socket.addEventListener("error", reject, {once: true});
});

let nextId = 1;
const pending = new Map();
const diagnostics = [];
socket.addEventListener("message", event => {
    const message = JSON.parse(event.data);
    if (message.method === "Runtime.exceptionThrown") {
        diagnostics.push(message.params.exceptionDetails.text);
    } else if (message.method === "Runtime.consoleAPICalled"
               && message.params.type === "error") {
        diagnostics.push(message.params.args
            .map(argument => argument.value ?? argument.description ?? "")
            .join(" "));
    }
    const callback = pending.get(message.id);
    if (!callback) return;
    pending.delete(message.id);
    clearTimeout(callback.timer);
    if (message.error) callback.reject(new Error(message.error.message));
    else callback.resolve(message.result);
});

const command = (method, params = {}) => new Promise((resolve, reject) => {
    const id = nextId++;
    const remainingMs = Math.max(1, deadline - Date.now());
    const budgetMs = Math.min(commandTimeoutMs, remainingMs);
    const timer = setTimeout(() => {
        pending.delete(id);
        reject(new Error(
            `RIDGEBREAK DevTools command timed out after ${budgetMs} ms: `
            + method,
        ));
    }, budgetMs);
    pending.set(id, {resolve, reject, timer});
    try {
        socket.send(JSON.stringify({id, method, params}));
    } catch (error) {
        clearTimeout(timer);
        pending.delete(id);
        reject(error);
    }
});

const evaluate = async expression => {
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

await command("Page.enable");
await command("Runtime.enable");
await command("Page.bringToFront");

let state = null;
while (Date.now() < deadline) {
    state = await evaluate(`(() => ({
        initialized: typeof voxyModule !== "undefined"
            && voxyModule?._voxy_is_initialized?.() === 1,
        frames: voxyModule?._voxy_get_frame_count?.() ?? 0,
        loading: getComputedStyle(document.getElementById("loading")).display,
        progress: document.getElementById("loading-progress-text")
            ?.textContent ?? "",
        technical: document.getElementById("loading-technical")
            ?.textContent?.trim() ?? "",
        error: document.getElementById("error")?.textContent ?? "",
        errorVisible: getComputedStyle(document.getElementById("error")).display
            !== "none",
    }))()`);
    if (state.errorVisible) {
        throw new Error(`RIDGEBREAK startup failed: ${state.error}`);
    }
    if (state.initialized && state.frames >= 30 && state.loading === "none") {
        break;
    }
    await delay(100);
}
if (!state?.initialized || state.frames < 30 || state.loading !== "none") {
    throw new Error(
        `RIDGEBREAK capture timed out: ${JSON.stringify(state)}; `
        + diagnostics.slice(-8).join(" | "),
    );
}

if (toggleRenderPath) {
    await evaluate("voxyModule._voxy_key_event(114, 1)");
    await evaluate("voxyModule._voxy_key_event(114, 0)");
    await delay(500);
}

if (driveMs > 0) {
    await evaluate("voxyModule._voxy_key_event(87, 1)");
    await delay(driveMs);
    await evaluate("voxyModule._voxy_key_event(87, 0)");
}
await delay(Math.max(0, settleMs));

const capture = await command("Page.captureScreenshot", {
    format: "png",
    fromSurface: true,
    captureBeyondViewport: false,
});
fs.mkdirSync(path.dirname(outputPath), {recursive: true});
fs.writeFileSync(outputPath, Buffer.from(capture.data, "base64"));

const finalState = await evaluate(`({
    frames: voxyModule?._voxy_get_frame_count?.() ?? 0,
    fps: voxyModule?._voxy_get_fps?.() ?? 0,
    canvas: [
        document.getElementById("voxy-canvas")?.width ?? 0,
        document.getElementById("voxy-canvas")?.height ?? 0,
    ],
    gpuErrorCount: globalThis.voxyUncapturedGpuErrorCount ?? 0,
    gpuErrors: (globalThis.voxyUncapturedGpuErrors ?? []).slice(-8),
    deviceLost: globalThis.voxyDeviceLost ?? null,
    telemetry: (() => {
        const pointer = voxyModule?._voxy_get_telemetry_json?.() ?? 0;
        return pointer ? JSON.parse(voxyModule.UTF8ToString(pointer)) : null;
    })(),
})`);
socket.close();
if (finalState.deviceLost || finalState.gpuErrorCount > 0) {
    throw new Error(
        `RIDGEBREAK WebGPU validation failed: ${JSON.stringify({
            deviceLost: finalState.deviceLost,
            gpuErrorCount: finalState.gpuErrorCount,
            gpuErrors: finalState.gpuErrors,
        })}`,
    );
}
console.log(JSON.stringify({outputPath, ...finalState}));
