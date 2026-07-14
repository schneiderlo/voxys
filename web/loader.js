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
    const profile = Object.freeze({
        name: supportedStorageBuffers >= requiredStorageBuffers
            ? 'webgpu-physics' : 'cpu-fallback',
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
 * @param {HTMLElement} element
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
 * @returns {{width: number, height: number, dpr: number}}
 */
function calculateCanvasSize(maxDpr = 2) {
    const dpr = Math.min(getDevicePixelRatio(), maxDpr);
    return {
        width: Math.floor(window.innerWidth * dpr),
        height: Math.floor(window.innerHeight * dpr),
        dpr: dpr
    };
}

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
