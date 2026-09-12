/**
 * ═══════════════════════════════════════════════════════════════════════════════
 * voxy WASM Loader
 * ═══════════════════════════════════════════════════════════════════════════════
 * Utility functions for loading and initializing the voxy WASM module.
 */

/**
 * Check if WebGPU is supported in the current browser
 * @returns {boolean}
 */
function isWebGPUSupported() {
    return 'gpu' in navigator;
}

/**
 * Get information about WebGPU support and capabilities
 * @returns {Promise<Object>}
 */
async function getWebGPUInfo() {
    if (!isWebGPUSupported()) {
        return {
            supported: false,
            reason: 'WebGPU API not available'
        };
    }
    
    try {
        const adapter = await navigator.gpu.requestAdapter();
        if (!adapter) {
            return {
                supported: false,
                reason: 'No suitable GPU adapter found'
            };
        }
        
        const info = await adapter.requestAdapterInfo();
        const limits = adapter.limits;
        
        return {
            supported: true,
            adapter: {
                vendor: info.vendor || 'Unknown',
                architecture: info.architecture || 'Unknown',
                device: info.device || 'Unknown',
                description: info.description || 'Unknown'
            },
            limits: {
                maxTextureDimension2D: limits.maxTextureDimension2D,
                maxBufferSize: limits.maxBufferSize,
                maxComputeWorkgroupsPerDimension: limits.maxComputeWorkgroupsPerDimension
            }
        };
    } catch (err) {
        return {
            supported: false,
            reason: err.message
        };
    }
}

/**
 * Create a device with the limit used by the real Voxys physics layouts.
 * Compiling a shader module does not validate bind-group limits. Adapters
 * below this limit still receive a normal device so the explicit CPU fallback
 * can initialize.
 */
// The pinned emdawnwebgpu bridge forwards the C UINT32_MAX sentinel as
// a numeric JS query index. WebGPU needs the optional property omitted.
// Scope this adapter to renderer-owned devices; never patch global GPU
// prototypes or suppress other invalid indices/validation failures.
const timestampCompatibleDevices = new WeakSet();
function normalizeTimestampDescriptor(descriptor) {
    const writes = descriptor?.timestampWrites;
    const undefinedIndex = 0xffffffff;
    if (!writes || (writes.beginningOfPassWriteIndex !== undefinedIndex
        && writes.endOfPassWriteIndex !== undefinedIndex)) return descriptor;
    const normalized = {...writes};
    for (const key of ['beginningOfPassWriteIndex', 'endOfPassWriteIndex']) {
        if (normalized[key] === undefinedIndex) delete normalized[key];
    }
    // Preserve invalid no-endpoint requests so WebGPU still rejects them.
    return {...descriptor, timestampWrites: normalized};
}
function installTimestampCompatibility(device) {
    if (typeof device?.createCommandEncoder !== 'function'
        || timestampCompatibleDevices.has(device)) return;
    const create = device.createCommandEncoder;
    device.createCommandEncoder = function(...args) {
        const encoder = Reflect.apply(create, this, args);
        for (const name of ['beginRenderPass', 'beginComputePass']) {
            const begin = encoder[name];
            encoder[name] = function(descriptor) {
                return Reflect.apply(begin, this,
                    [normalizeTimestampDescriptor(descriptor)]);
            };
        }
        return encoder;
    };
    timestampCompatibleDevices.add(device);
}

async function requestVoxyDevice(adapter, { enableTimestamps = false } = {}) {
    const requiredStorageBuffers = 8;
    const supportedStorageBuffers =
        adapter.limits.maxStorageBuffersPerShaderStage;
    const requiredLimits = {};
    if (supportedStorageBuffers >= requiredStorageBuffers) {
        requiredLimits.maxStorageBuffersPerShaderStage =
            requiredStorageBuffers;
    }
    const requiredFeatures = [];
    if (enableTimestamps && adapter.features.has('timestamp-query')) {
        requiredFeatures.push('timestamp-query');
    }
    const device = await adapter.requestDevice({
        requiredFeatures,
        requiredLimits,
    });
    installTimestampCompatibility(device);
    let adapterInfo = adapter.info;
    if (!adapterInfo && typeof adapter.requestAdapterInfo === 'function') {
        try {
            adapterInfo = await adapter.requestAdapterInfo();
        } catch {
            adapterInfo = {};
        }
    }
    adapterInfo ||= {};
    const profile = Object.freeze({
        name: supportedStorageBuffers >= requiredStorageBuffers
            ? 'webgpu-physics' : 'cpu-fallback',
        adapter: Object.freeze({
            vendor: adapterInfo.vendor || 'Unknown',
            architecture: adapterInfo.architecture || 'Unknown',
            device: adapterInfo.device || 'Unknown',
            description: adapterInfo.description || 'Unknown',
            fallback: Boolean(adapter.isFallbackAdapter),
        }),
        requiredStorageBuffers,
        maxStorageBuffersPerShaderStage:
            device.limits.maxStorageBuffersPerShaderStage,
        maxStorageBufferBindingSize:
            device.limits.maxStorageBufferBindingSize,
        maxBufferSize: device.limits.maxBufferSize,
        maxComputeWorkgroupsPerDimension:
            device.limits.maxComputeWorkgroupsPerDimension,
        timestampQuery: device.features.has('timestamp-query'),
    });
    return { device, profile };
}

/**
 * Request pointer lock on an element with cross-browser support
 * @param {HTMLElement} element
 */
function requestPointerLock(element) {
    const request = element.requestPointerLock ||
                   element.mozRequestPointerLock ||
                   element.webkitRequestPointerLock;
    if (request) {
        request.call(element);
    }
}

/**
 * Exit pointer lock with cross-browser support
 */
function exitPointerLock() {
    const exit = document.exitPointerLock ||
                document.mozExitPointerLock ||
                document.webkitExitPointerLock;
    if (exit) {
        exit.call(document);
    }
}

/**
 * Check if pointer is currently locked
 * @returns {boolean}
 */
function isPointerLocked() {
    return !!(document.pointerLockElement ||
             document.mozPointerLockElement ||
             document.webkitPointerLockElement);
}

/**
 * Toggle fullscreen mode for an element
 */
async function toggleFullscreen(element) {
    if (!document.fullscreenElement) {
        try {
            await element.requestFullscreen();
        } catch (err) {
            console.warn('Fullscreen request failed:', err);
        }
    } else {
        await document.exitFullscreen();
    }
}

/**
 * Get the device pixel ratio, clamped to reasonable values
 * @returns {number}
 */
function getDevicePixelRatio() {
    return Math.min(Math.max(window.devicePixelRatio || 1, 1), 3);
}

/**
 * Calculate canvas size based on window size and DPR
 * @param {number} maxDpr - Maximum DPR to use (for performance)
 * @param {number} renderScale - User-selected internal resolution multiplier
 * @returns {{width: number, height: number, dpr: number}}
 */
function calculateCanvasSize(maxDpr = 2, renderScale = 1) {
    const scale = Math.min(Math.max(Number(renderScale) || 1, 0.35), 1.5);
    const dpr = Math.min(getDevicePixelRatio(), maxDpr) * scale;
    return {
        width: Math.floor(window.innerWidth * dpr),
        height: Math.floor(window.innerHeight * dpr),
        dpr: dpr
    };
}

// Main LEGO World uses a product default distinct from the renderer inspector's
// cross-route fallback. Keep route-independent settings untouched.
const MAIN_RENDERER_SCALE = 1.014;
const MAIN_RUNTIME_RENDERER_DEFAULTS = Object.freeze({
    'lighting.sunIntensity': 1.7,
    'lighting.exposure': 1.15,
});
const PREVIOUS_MAIN_RENDERER_DEFAULT = Object.freeze({
    'browser.resolutionScale': 1,
    'render.path': 1,
    'lighting.sunAzimuth': -45,
    'lighting.sunElevation': 54.7356,
    'lighting.sunColor': [1, 0.96, 0.88],
    'lighting.sunIntensity': 1,
    'lighting.ambientColor': [0.17, 0.20, 0.23],
    'lighting.ambientIntensity': 0.55,
    'lighting.fogColor': [0.66, 0.77, 0.80],
    'lighting.fogDensity': 0.0001,
    'lighting.exposure': 1,
    'water.enabled': 1,
    'water.height': -200,
    'water.shallowColor': [0.12, 0.43, 0.46],
    'water.deepColor': [0.025, 0.14, 0.20],
    'water.roughness': 0.24,
    'water.reflectionStrength': 0.22,
    'water.reflectionDistance': 1500,
    'water.ior': 1.31,
    'water.distortion': 0.04,
    'water.absorptionScale': 1,
    'water.scatterStrength': 1,
    'water.shoreFade': 8,
    'water.foamSize': 96,
    'water.foamOpacity': 0.07,
    'water.foamCoverage': 0.14,
    'water.waveStrength': 0.10,
    'water.spectrum.significantHeight': 25.9,
    'water.spectrum.direction': 57,
    'water.spectrum.choppiness': 2.24,
    'water.spectrum.peakEnhancement': 0.65,
    'water.spectrum.windAlignment': 0.32,
    'water.spectrum.speed': 2,
    'water.spectrum.largePatch': 1949,
    'water.spectrum.detailPatch': 326,
    'water.spectrum.largeAmplitude': 0.33,
    'water.spectrum.detailAmplitude': 0.07,
    'water.spectrum.directionalSine': 0.68,
    'camera.fov': 60,
    'camera.near': 0.1,
    'camera.far': 10000,
    'camera.moveSpeed': 10,
    'camera.mouseSensitivity': 0.0002,
    'camera.eyeHeight': 1.8,
});
const mainRendererDefaultsApplied = new WeakSet();

function isMainLegoWorldRoute() {
    if (typeof location === 'undefined') return false;
    const experience = new URLSearchParams(location.search).get('experience') || 'lego-world';
    return experience === 'lego-world';
}

function storedRendererScale() {
    try {
        const parsed = JSON.parse(localStorage.getItem('voxy.renderer-inspector.v1'));
        const value = Number(parsed?.settings?.['browser.resolutionScale']);
        return Number.isFinite(value)
            ? Math.min(Math.max(value, 0.35), 1.5) : null;
    } catch (_) {
        return null;
    }
}

function applyMainRuntimeRendererDefaults(module) {
    if (!isMainLegoWorldRoute() || !module?.ccall
        || module?._voxy_is_initialized?.() !== 1) return false;
    if (mainRendererDefaultsApplied.has(module)) return true;
    for (const [key, value] of Object.entries(MAIN_RUNTIME_RENDERER_DEFAULTS)) {
        const applied = module.ccall('voxy_renderer_set_number', 'number',
            ['string', 'number', 'number'], [key, value, 1]);
        if (!applied) return false;
    }
    mainRendererDefaultsApplied.add(module);
    return true;
}

function patchRendererInspectorApi(api) {
    if (!api || api.__voxyMainDefaultsBridge) return api;
    const originalScale = api.getStoredRenderScale?.bind(api);
    api.getStoredRenderScale = () => {
        const stored = storedRendererScale();
        if (stored !== null) return stored;
        if (isMainLegoWorldRoute()) return MAIN_RENDERER_SCALE;
        return originalScale ? originalScale() : 1;
    };
    if (typeof api.mount === 'function') {
        const originalMount = api.mount.bind(api);
        api.mount = options => {
            applyMainRuntimeRendererDefaults(options?.module);
            const instance = originalMount(options);
            if (isMainLegoWorldRoute() && instance
                && !instance.__voxyPreviousMainDefaultPreset
                && typeof instance.builtInPresets === 'function') {
                const originalPresets = instance.builtInPresets.bind(instance);
                instance.builtInPresets = () => ({
                    ...originalPresets(),
                    previousMain20260911: {
                        label: 'Previous Main Default (2026-09-11)',
                        values: JSON.parse(JSON.stringify(PREVIOUS_MAIN_RENDERER_DEFAULT)),
                    },
                });
                Object.defineProperty(instance, '__voxyPreviousMainDefaultPreset', {
                    value: true,
                });
                instance.refreshPresetOptions?.();
            }
            return instance;
        };
    }
    Object.defineProperty(api, '__voxyMainDefaultsBridge', {value: true});
    return api;
}

function installRendererInspectorBridge() {
    if (globalThis.VoxyRendererInspector) {
        patchRendererInspectorApi(globalThis.VoxyRendererInspector);
        return;
    }
    Object.defineProperty(globalThis, 'VoxyRendererInspector', {
        configurable: true,
        enumerable: true,
        get() { return undefined; },
        set(value) {
            const patched = patchRendererInspectorApi(value);
            Object.defineProperty(globalThis, 'VoxyRendererInspector', {
                configurable: true,
                enumerable: true,
                writable: true,
                value: patched,
            });
        },
    });
}

function applyMainRuntimeRendererDefaultsWhenReady() {
    if (!isMainLegoWorldRoute()) return;
    let attempts = 0;
    const poll = () => {
        let module = globalThis.voxyModule;
        try {
            if (typeof voxyModule !== 'undefined') module = voxyModule;
        } catch (_) {}
        if (applyMainRuntimeRendererDefaults(module)) return;
        attempts += 1;
        if (attempts < 240) setTimeout(poll, 50);
    };
    setTimeout(poll, 0);
}

installRendererInspectorBridge();
applyMainRuntimeRendererDefaultsWhenReady();

// Export for use in main script
window.VoxyLoader = {
    isWebGPUSupported,
    getWebGPUInfo,
    requestVoxyDevice,
    requestPointerLock,
    exitPointerLock,
    isPointerLocked,
    toggleFullscreen,
    getDevicePixelRatio,
    calculateCanvasSize
};
