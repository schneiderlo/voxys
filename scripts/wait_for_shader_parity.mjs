const debugPort = Number.parseInt(process.argv[2] ?? "0", 10);
if (!Number.isInteger(debugPort) || debugPort <= 0) {
    throw new Error("usage: wait_for_shader_parity.mjs <devtools-port>");
}

const deadline = Date.now() + 30_000;
const delay = (milliseconds) => new Promise(
    (resolve) => setTimeout(resolve, milliseconds),
);

let page;
while (Date.now() < deadline) {
    try {
        const response = await fetch(`http://127.0.0.1:${debugPort}/json/list`);
        const targets = await response.json();
        page = targets.find((target) =>
            target.type === "page" && target.url.includes("shader_parity.html"));
        if (page) break;
    } catch {
        // Chrome is still starting.
    }
    await delay(100);
}
if (!page) throw new Error("shader parity browser target did not start");

const socket = new WebSocket(page.webSocketDebuggerUrl);
await new Promise((resolve, reject) => {
    socket.addEventListener("open", resolve, { once: true });
    socket.addEventListener("error", reject, { once: true });
});

let nextId = 1;
const pending = new Map();
socket.addEventListener("message", (event) => {
    const message = JSON.parse(event.data);
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
while (Date.now() < deadline) {
    const evaluated = await command("Runtime.evaluate", {
        expression: `JSON.stringify({
            status: document.getElementById("result")?.dataset.status,
            text: document.getElementById("result")?.textContent,
        })`,
        returnByValue: true,
    });
    const value = JSON.parse(evaluated.result.value);
    if (value.status === "pass") {
        console.log(value.text);
        socket.close();
        process.exit(0);
    }
    if (value.status === "fail") {
        console.error(value.text);
        socket.close();
        process.exit(1);
    }
    await delay(100);
}

socket.close();
throw new Error("WebGPU shader compilation timed out");
