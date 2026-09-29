// Minimal browser instrumentation. Canvas acquisition is renderer throughput,
// not proof that a monitor displayed the frame. No extra GPU fences are added.
export function installCanvasFpsProbe(shaderOverrides = {}) {
    const probe = globalThis.voxyCanvasFps = {
        acquisitions: 0, uniqueTextures: 0, duplicates: 0, submissions: 0, commands: 0,
        measuring: false, times: [], uniqueTimes: [], sizes: [],
        devices: [], lost: [], errors: [], shaders: {},
    };
    const textures = new WeakSet();
    const acquire = GPUCanvasContext.prototype.getCurrentTexture;
    GPUCanvasContext.prototype.getCurrentTexture = function (...args) {
        const texture = acquire.apply(this, args);
        if (this.canvas !== document.getElementById('voxy-canvas')) return texture;
        ++probe.acquisitions;
        const unique = !textures.has(texture);
        if (unique) {textures.add(texture); ++probe.uniqueTextures;} else ++probe.duplicates;
        if (probe.measuring) {
            const now = performance.now();
            probe.times.push(now);
            if (unique) probe.uniqueTimes.push(now);
            const previous = probe.sizes.at(-1);
            if (!previous || previous.width !== texture.width || previous.height !== texture.height) {
                probe.sizes.push({width: texture.width, height: texture.height});
            }
        }
        return texture;
    };
    const submit = GPUQueue.prototype.submit;
    GPUQueue.prototype.submit = function (commands) {
        const result = submit.call(this, commands);
        ++probe.submissions; probe.commands += commands.length;
        return result;
    };
    const requestDevice = GPUAdapter.prototype.requestDevice;
    GPUAdapter.prototype.requestDevice = async function (...args) {
        const device = await requestDevice.apply(this, args);
        const info = this.info;
        probe.devices.push({
            fallback: this.isFallbackAdapter ?? null,
            adapter: info ? {vendor: info.vendor, architecture: info.architecture,
                device: info.device, description: info.description,
                isFallbackAdapter: info.isFallbackAdapter ?? null} : null,
            features: [...device.features].sort(),
        });
        device.lost.then(info => probe.lost.push({reason: info.reason, message: info.message}));
        device.addEventListener('uncapturederror', event => probe.errors.push(event.error.message));
        return device;
    };
    if (Object.keys(shaderOverrides).length) {
        const createShader = GPUDevice.prototype.createShaderModule;
        GPUDevice.prototype.createShaderModule = function (descriptor) {
            const replacement = shaderOverrides[descriptor.label];
            if (replacement !== undefined) {
                // Exact source equality is stronger than the expected SHA-256
                // recorded by Node, and remains synchronous like this API.
                if (descriptor.code !== replacement.expectedCode) {
                    const error = `Shader source differs from expected SHA-256 ${replacement.expectedSha256}: ${descriptor.label}`;
                    probe.errors.push(error); throw Error(error);
                }
                probe.shaders[descriptor.label] = (probe.shaders[descriptor.label] ?? 0) + 1;
                return createShader.call(this, {...descriptor, code: replacement.code});
            }
            return createShader.call(this, descriptor);
        };
    }
}
