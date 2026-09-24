import assert from 'node:assert/strict';

export function startupBudget(env = process.env) {
    const softwareGpu = String(env.VOXY_SMOKE_GPU || '').startsWith('swiftshader');
    // The page allows ten minutes for cold compilation. Software GPU checks
    // also need time to render and retire frames after that initialization.
    const startupMs = softwareGpu ? 660000 : 180000;
    const totalMs = Number(env.VOXY_SMOKE_TIMEOUT_MS || (softwareGpu ? 780000 : 240000));
    assert(Number.isInteger(totalMs) && totalMs >= startupMs + 60000 && totalMs <= 1800000,
        'smoke timeout must leave at least 60 seconds after startup and be at most 1800 seconds');
    return {startupMs, totalMs};
}
