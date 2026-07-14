const debugPort = Number.parseInt(process.argv[2] ?? "0", 10);
if (!Number.isInteger(debugPort) || debugPort <= 0) {
    throw new Error("usage: wait_for_wasm_startup.mjs <devtools-port>");
}

const timeoutMilliseconds = Number.parseInt(
    process.env.VOXY_WASM_STARTUP_TIMEOUT_MS ?? "90000",
    10,
);
if (!Number.isInteger(timeoutMilliseconds) || timeoutMilliseconds <= 0) {
    throw new Error("VOXY_WASM_STARTUP_TIMEOUT_MS must be a positive integer");
}
const deadline = Date.now() + timeoutMilliseconds;
const delay = (milliseconds) => new Promise(
    (resolve) => setTimeout(resolve, milliseconds),
);

let page;
while (Date.now() < deadline) {
    try {
        const response = await fetch(`http://127.0.0.1:${debugPort}/json/list`);
        const targets = await response.json();
        page = targets.find((target) =>
            target.type === "page" && target.url.includes("index.html"));
        if (page) break;
    } catch {
        // Chrome is still starting.
    }
    await delay(100);
}
if (!page) throw new Error("WASM startup browser target did not start");

const socket = new WebSocket(page.webSocketDebuggerUrl);
await new Promise((resolve, reject) => {
    socket.addEventListener("open", resolve, { once: true });
    socket.addEventListener("error", reject, { once: true });
});

let nextId = 1;
const pending = new Map();
const diagnostics = [];
let lastState = null;
socket.addEventListener("message", (event) => {
    const message = JSON.parse(event.data);
    if (message.method === "Runtime.exceptionThrown") {
        diagnostics.push(message.params.exceptionDetails.text);
    } else if (message.method === "Log.entryAdded"
               && message.params.entry.level === "error") {
        diagnostics.push(message.params.entry.text);
    } else if (message.method === "Runtime.consoleAPICalled"
               && message.params.type === "error") {
        diagnostics.push(message.params.args
            .map((argument) => argument.value ?? argument.description ?? "")
            .join(" "));
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

await command("Runtime.enable");
await command("Log.enable");
while (Date.now() < deadline) {
    const evaluated = await command("Runtime.evaluate", {
        expression: `(() => {
            const errorElement = document.getElementById("error");
            const moduleReady = typeof voxyModule !== "undefined"
                && voxyModule !== null;
            const initialized = moduleReady
                && typeof voxyModule._voxy_is_initialized === "function"
                && voxyModule._voxy_is_initialized() === 1;
            const physicsBackend = moduleReady
                && typeof voxyModule._voxy_get_physics_backend === "function"
                ? voxyModule._voxy_get_physics_backend() : -1;
            const physicsSelfTestStatus = moduleReady
                && typeof voxyModule._voxy_get_physics_self_test_status
                    === "function"
                ? voxyModule._voxy_get_physics_self_test_status() : -100;
            return JSON.stringify({
                initialized,
                physicsBackend,
                physicsSelfTestStatus,
                physicsSelfTestTick: moduleReady
                    && typeof voxyModule._voxy_get_physics_self_test_tick
                        === "function"
                    ? voxyModule._voxy_get_physics_self_test_tick() : 0,
                errorVisible: errorElement !== null
                    && getComputedStyle(errorElement).display !== "none",
                errorText: errorElement?.textContent ?? "",
                profile: globalThis.voxyDeviceProfile ?? null,
            });
        })()`,
        returnByValue: true,
    });
    if (typeof evaluated.result?.value !== "string") {
        const detail = evaluated.exceptionDetails?.exception?.description
            ?? evaluated.exceptionDetails?.text;
        if (detail) diagnostics.push(detail);
        await delay(100);
        continue;
    }
    const value = JSON.parse(evaluated.result.value);
    lastState = value;
    if (value.initialized && value.physicsBackend === 2
        && value.physicsSelfTestStatus === 2) {
        console.log(
            `WASM GPU physics self-test passed at tick ${
                value.physicsSelfTestTick
            } (${value.profile?.name ?? "unknown"})`,
        );
        socket.close();
        process.exit(0);
    }
    if (value.errorVisible) {
        socket.close();
        throw new Error(`WASM application reported failure: ${value.errorText}`);
    }
    if (value.initialized && value.profile?.name === "cpu-fallback") {
        socket.close();
        throw new Error(
            `WASM device exposes ${
                value.profile.maxStorageBuffersPerShaderStage
            } storage buffers per stage; GPU physics requires ${
                value.profile.requiredStorageBuffers
            }`,
        );
    }
    if (value.initialized && value.physicsSelfTestStatus < 0) {
        socket.close();
        throw new Error(
            `WASM GPU physics self-test failed with status ${
                value.physicsSelfTestStatus
            }${
                diagnostics.length === 0
                    ? "" : `: ${diagnostics.slice(-10).join(" | ")}`
            }`,
        );
    }
    await delay(100);
}

socket.close();
throw new Error(
    `WASM application startup timed out${
        diagnostics.length === 0 ? "" : `: ${diagnostics.slice(-10).join(" | ")}`
    }; last state: ${JSON.stringify(lastState)}`,
);
