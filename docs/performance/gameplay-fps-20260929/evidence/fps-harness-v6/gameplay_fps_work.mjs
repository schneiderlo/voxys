import {installGameplayCounters} from './gameplay_counters.mjs';

export {installGameplayCounters};

// Add pipeline-specific draw counts to the existing dispatch/upload probe.
export function installGameplayDrawCounters() {
    const counts = globalThis.voxyWorkCounts;
    const createBuffer = GPUDevice.prototype.createBuffer;
    GPUDevice.prototype.createBuffer = function (descriptor) {
        const buffer = createBuffer.call(this, descriptor);
        const key = `bufferCreated:${buffer.label || '(unlabeled)'}`;
        counts[key] = (counts[key] ?? 0) + 1;
        return buffer;
    };
    const pipelines = new WeakMap();
    const setPipeline = GPURenderPassEncoder.prototype.setPipeline;
    GPURenderPassEncoder.prototype.setPipeline = function (pipeline) {
        pipelines.set(this, pipeline.label || '(unlabeled)');
        return setPipeline.call(this, pipeline);
    };
    for (const method of ['draw', 'drawIndexed', 'drawIndirect', 'drawIndexedIndirect']) {
        const original = GPURenderPassEncoder.prototype[method];
        GPURenderPassEncoder.prototype[method] = function (...args) {
            const key = `${method}:${pipelines.get(this) ?? '(unbound)'}`;
            counts[key] = (counts[key] ?? 0) + 1;
            return original.apply(this, args);
        };
    }
}

export function summarizeGameplayFpsWork(sample) {
    const frames = sample.validation.summary.acquiredFrames;
    const difference = key => (sample.workAfter[key] ?? 0) - (sample.workBefore[key] ?? 0);
    const rows = prefix => Object.keys(sample.workAfter).filter(key => key.startsWith(prefix))
        .map(key => ({label: key.slice(prefix.length), count: difference(key), perFrame: difference(key) / frames}))
        .filter(row => row.count > 0).sort((a, b) => b.count - a.count);
    return {
        hud: ['cove_hud_quads', 'adventure_hud_triangles', 'hud_quads'].map(label => ({label,
            writes: difference(`write:${label}`), bytes: difference(`writeBytes:${label}`),
            writesPerFrame: difference(`write:${label}`) / frames,
            bytesPerFrame: difference(`writeBytes:${label}`) / frames})),
        renderPasses: rows('renderPass:'), dispatches: rows('dispatch:'),
        workgroups: rows('groups:'), indexedDraws: rows('drawIndexed:'),
        draws: rows('draw:'), indirectDraws: rows('drawIndirect:'),
        indexedIndirectDraws: rows('drawIndexedIndirect:'), uploads: rows('write:'),
        uploadBytes: rows('writeBytes:'), copyBytes: rows('copyBytes:'),
    };
}

// Deliberately scoped to a fresh-camp Frontier idle capture. Later play can
// legitimately change health, saves, menus, or map data while standing still.
export function checkFreshFrontierHudWork(sample) {
    if (sample.experience !== 'frontier' || sample.scenario !== 'idle' || !sample.freshWorldIdle) {
        throw Error('HUD work guard requires the first idle scenario of a fresh Frontier world');
    }
    for (const key of ['x', 'y', 'z']) {
        if (Math.abs(sample.after.adventure.player[key] - sample.before.adventure.player[key]) > .001) {
            throw Error('HUD work guard requires a stationary player');
        }
    }
    for (const key of ['mode', 'health', 'menuTitle', 'menuText', 'menuStatus']) {
        if (sample.before.adventure[key] !== sample.after.adventure[key]) throw Error(`Idle presentation changed: ${key}`);
    }
    const result = {};
    if (!['cove_hud_quads', 'adventure_hud_triangles'].some(label => sample.workBefore[`write:${label}`] > 0)) {
        throw Error('Missing initial HUD upload instrumentation');
    }
    for (const label of ['cove_hud_quads', 'adventure_hud_triangles']) {
        const key = `write:${label}`;
        // Frontier can leave its triangle backend empty from initialization.
        // Buffer creation proves the target exists even when it never uploads.
        if (!(sample.workBefore[`bufferCreated:${label}`] > 0)) throw Error(`Missing initial HUD buffer instrumentation: ${label}`);
        const writes = (sample.workAfter[key] ?? 0) - (sample.workBefore[key] ?? 0);
        const bytesKey = `writeBytes:${label}`;
        const bytes = (sample.workAfter[bytesKey] ?? 0) - (sample.workBefore[bytesKey] ?? 0);
        result[label] = {writes, bytes};
        if (writes !== 0 || bytes !== 0) throw Error(`Fresh Frontier idle rebuilt ${label}: ${writes} writes, ${bytes} bytes`);
    }
    return {scope: 'fresh-world stationary Frontier idle after feedback warmup', uploads: result};
}
