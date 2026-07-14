const debugPort = Number.parseInt(process.argv[2] ?? "0", 10);
if (!Number.isInteger(debugPort) || debugPort <= 0) {
    throw new Error("usage: wait_for_wasm_startup.mjs <devtools-port>");
}

const deadline = Date.now() + 90_000;
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
            return JSON.stringify({
                initialized: moduleReady
                    && typeof voxyModule._voxy_is_initialized === "function"
                    && voxyModule._voxy_is_initialized() === 1,
                physicsBackend: moduleReady
                    && typeof voxyModule._voxy_get_physics_backend === "function"
                    ? voxyModule._voxy_get_physics_backend() : -1,
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
    if (value.initialized && value.physicsBackend === 2) {
        console.log(`WASM application initialized (${value.profile?.name ?? "unknown"})`);
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
    await delay(100);
}

socket.close();
throw new Error(
    `WASM application startup timed out${
        diagnostics.length === 0 ? "" : `: ${diagnostics.slice(-10).join(" | ")}`
    }`,
);
