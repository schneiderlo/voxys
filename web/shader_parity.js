"use strict";

async function validateShaders() {
    const result = document.getElementById("result");
    // Headless Chrome's --dump-dom waits on virtual timers, not bare WebGPU
    // promises. Keep one timer live until compilation has completed.
    const keepAlive = setInterval(() => {}, 50);
    try {
        if (!navigator.gpu) {
            throw new Error("navigator.gpu is unavailable");
        }
        const adapter = await navigator.gpu.requestAdapter();
        if (!adapter) {
            throw new Error("WebGPU adapter request failed");
        }
        const device = await adapter.requestDevice();
        const failures = [];
        for (const filename of VOXY_SHADER_PARITY_FILES) {
            const response = await fetch(`/shaders/${filename}`);
            if (!response.ok) {
                failures.push(`${filename}: HTTP ${response.status}`);
                continue;
            }
            const module = device.createShaderModule({
                label: `parity:${filename}`,
                code: await response.text(),
            });
            const compilation = await module.getCompilationInfo();
            for (const message of compilation.messages) {
                if (message.type === "error") {
                    failures.push(
                        `${filename}:${message.lineNum}:${message.linePos}: ${message.message}`,
                    );
                }
            }
        }
        if (failures.length !== 0) {
            throw new Error(failures.join(" | "));
        }
        result.dataset.status = "pass";
        result.textContent = `pass:${VOXY_SHADER_PARITY_FILES.length}`;
    } catch (error) {
        result.dataset.status = "fail";
        result.textContent = `fail:${error instanceof Error ? error.message : String(error)}`;
    } finally {
        clearInterval(keepAlive);
    }
}

void validateShaders();
