import assert from 'node:assert/strict';
import {test} from 'node:test';
import '../web/gpu_startup.js';

const {install} = globalThis.VoxyGpuStartup;
const manifest = {'voxy_wasm.wasm': {sha256: 'a'.repeat(64)}, 'voxy_wasm.data': {sha256: 'b'.repeat(64)}};
const deferred = () => { let resolve, reject; const promise = new Promise((r, j) => {resolve = r; reject = j;}); return {promise, resolve, reject}; };
const tick = () => new Promise(resolve => setImmediate(resolve));
function fixture({cache = new Map(), blocked = false, stored = null} = {}) {
    const lost = deferred(), calls = [], pending = [];
    const device = {lost: lost.promise, pushErrorScope() {}, popErrorScope: async () => null};
    for (const kind of ['createShaderModule', 'createBindGroupLayout', 'createPipelineLayout']) {
        device[kind] = descriptor => {const object = {kind, descriptor}; calls.push(object); return object;};
    }
    for (const kind of ['createComputePipelineAsync', 'createRenderPipelineAsync']) {
        device[kind] = descriptor => {
            const object = {kind, descriptor}; calls.push(object);
            if (!blocked) return Promise.resolve(object);
            const task = deferred(); pending.push({...task, object}); return task.promise;
        };
    }
    const environment = {performance, Response, fetch: async () => new Response(JSON.stringify(stored)),
        caches: {open: async () => ({
            match: async key => cache.has(key) ? new Response(cache.get(key)) : null,
            put: async (key, value) => {cache.set(key, await value.text());},
            keys: async () => [...cache.keys()].map(key => ({url: new URL(key, 'https://test/').href})),
            delete: async request => cache.delete(new URL(request.url).pathname.slice(1) + new URL(request.url).search),
        })}};
    return {device, environment, calls, pending, lost, cache};
}
function descriptor(device, code = '@compute @workgroup_size(1) fn main() {}') {
    const module = device.createShaderModule({label: 'shader', code});
    const group = device.createBindGroupLayout({entries: [{binding: 0, visibility: 4, buffer: {type: 'storage'}}]});
    const layout = device.createPipelineLayout({bindGroupLayouts: [group]});
    return {label: 'compute', layout, compute: {module, entryPoint: 'main', constants: {COUNT: 1}}};
}

test('identical descriptors coalesce pending work across independently created modules/layouts', async () => {
    const f = fixture({blocked: true}); const api = install(f.device, {environment: f.environment});
    const first = f.device.createComputePipelineAsync(descriptor(f.device));
    const second = f.device.createComputePipelineAsync({...descriptor(f.device), label: 'another label'});
    assert.equal(f.pending.length, 1);
    f.pending[0].resolve(f.pending[0].object);
    assert.equal(await first, await second);
    const recipe = api.snapshot();
    assert.equal(recipe.resources.length, 3); assert.equal(recipe.pipelines.length, 1);
    await api.finish(); assert.equal(api.stats.hits, 1);
});

test('code, layout, entry point and constants distinguish compute programs', async () => {
    const f = fixture(); install(f.device, {environment: f.environment});
    const d = descriptor(f.device);
    await f.device.createComputePipelineAsync(d);
    await f.device.createComputePipelineAsync({...d, compute: {...d.compute, constants: {COUNT: 2}}});
    await f.device.createComputePipelineAsync({...d, compute: {...d.compute, entryPoint: 'other'}});
    await f.device.createComputePipelineAsync({...d, layout: 'auto'});
    await f.device.createComputePipelineAsync(descriptor(f.device, 'changed shader'));
    assert.equal(f.calls.filter(row => row.kind === 'createComputePipelineAsync').length, 5);
});

test('render targets, vertex attributes, depth, blend and fragment constants distinguish programs', async () => {
    const f = fixture(); install(f.device, {environment: f.environment});
    const {layout, compute: {module}} = descriptor(f.device);
    const d = {layout, vertex: {module, entryPoint: 'vs', buffers: []},
        fragment: {module, entryPoint: 'fs', constants: {label: 1}, targets: [{format: 'bgra8unorm'}]}};
    await f.device.createRenderPipelineAsync(d);
    const variants = [
        {...d, fragment: {...d.fragment, targets: [{format: 'rgba8unorm'}]}},
        {...d, fragment: {...d.fragment, constants: {label: 2}}},
        {...d, fragment: {...d.fragment, targets: [{format: 'bgra8unorm', blend: {color: {srcFactor: 'one', dstFactor: 'one'}}}]}},
        {...d, vertex: {...d.vertex, buffers: [{arrayStride: 4, attributes: [{shaderLocation: 0, offset: 0, format: 'float32'}]}]}},
        {...d, depthStencil: {format: 'depth32float', depthWriteEnabled: true, depthCompare: 'less'}},
    ];
    for (const v of variants) await f.device.createRenderPipelineAsync(v);
    assert.equal(f.calls.filter(row => row.kind === 'createRenderPipelineAsync').length, 6);
});

test('mutating a descriptor after submission does not mutate the stored recipe', async () => {
    const f = fixture(); const api = install(f.device, {environment: f.environment});
    const d = descriptor(f.device); await f.device.createComputePipelineAsync(d);
    d.compute.constants.COUNT = 99;
    assert.equal(api.snapshot().pipelines[0].descriptor.compute.constants.COUNT, 1);
});

test('a later page recreates GPU objects early and engine requests reuse them', async () => {
    const first = fixture(); const a = install(first.device, {manifest, environment: first.environment});
    await first.device.createComputePipelineAsync(descriptor(first.device)); await a.finish();
    const next = fixture({cache: first.cache, blocked: true});
    const b = install(next.device, {manifest, environment: next.environment});
    await tick();
    assert.equal(next.pending.length, 1, 'compilation starts before the engine asks for any assets/programs');
    const requested = next.device.createComputePipelineAsync(descriptor(next.device));
    assert.equal(next.pending.length, 1, 'engine joins early compilation');
    next.pending[0].resolve(next.pending[0].object);
    await requested; await b.finish();
    assert.equal(b.stats.earlyHits, 1); assert.equal(b.stats.recipeSource, 'saved');
    assert.notEqual(first.calls[0], next.calls[0], 'GPU objects never cross devices');
});

test('a release recipe enables early compilation on a first visit', async () => {
    const first = fixture(); const a = install(first.device, {manifest, environment: first.environment});
    await first.device.createComputePipelineAsync(descriptor(first.device)); await a.finish();
    const next = fixture({stored: a.recipe});
    const b = install(next.device, {manifest: {...manifest, 'voxy_graphics.json': {sha256: 'c'.repeat(64), size: 2000}}, environment: next.environment});
    await b.warmup;
    assert.equal(b.stats.earlySubmitted, 1); assert.equal(b.stats.recipeSource, 'release');
    await next.device.createComputePipelineAsync(descriptor(next.device)); await b.finish();
    assert.equal(b.stats.earlyHits, 1);
});

test('changed releases and different experiences do not replay the old recipe', async () => {
    const first = fixture(); const a = install(first.device, {manifest, environment: first.environment});
    await first.device.createComputePipelineAsync(descriptor(first.device)); await a.finish();
    for (const options of [{manifest: {...manifest, 'voxy_wasm.data': {sha256: 'd'.repeat(64)}}}, {manifest, experience: 'terrain'}, {manifest, configuration: 'physicsBackend=jolt'}]) {
        const next = fixture({cache: first.cache}); const b = install(next.device, {...options, environment: next.environment});
        await b.warmup; assert.equal(next.calls.length, 0);
    }
});

test('backend/capacity selections separate saved setup while world IDs and telemetry do not', () => {
    const config = globalThis.VoxyGpuStartup.configurationFor;
    assert.equal(config('?telemetry=0&new=1&world=abc&experience=build&physicsBackend=webgpu'), '');
    assert.equal(config('?physicsMaxBodies=2000&physicsBackend=jolt'), 'physicsBackend=jolt&physicsMaxBodies=2000');
});

test('failed early compilation retries through the engine and does not poison reuse', async () => {
    const first = fixture(); const a = install(first.device, {manifest, environment: first.environment});
    await first.device.createComputePipelineAsync(descriptor(first.device)); await a.finish();
    const next = fixture({cache: first.cache, blocked: true}); const b = install(next.device, {manifest, environment: next.environment});
    await tick();
    const requested = next.device.createComputePipelineAsync(descriptor(next.device));
    next.pending[0].reject(Error('speculative failure')); await tick();
    assert.equal(next.pending.length, 2);
    next.pending[1].resolve(next.pending[1].object); await requested; await b.finish();
});

test('storage failures are optional; unknown objects pass through unchanged', async () => {
    const f = fixture(); f.environment.caches.open = async () => {throw Error('blocked');};
    const original = f.device.createComputePipelineAsync;
    const api = install(f.device, {manifest, environment: f.environment});
    class ForeignLayout {}
    const d = {...descriptor(f.device), layout: new ForeignLayout()};
    const result = await f.device.createComputePipelineAsync(d);
    assert.equal(result.descriptor, d); await api.finish();
    assert.equal(f.device.createComputePipelineAsync, original);
});

test('finish waits for in-flight speculative work and stops queued work before play', async () => {
    const first = fixture(); const a = install(first.device, {manifest, environment: first.environment});
    for (let i = 0; i < 8; ++i) await first.device.createComputePipelineAsync(descriptor(first.device, `shader ${i}`));
    await a.finish();
    const next = fixture({cache: first.cache, blocked: true}); const b = install(next.device, {manifest, environment: next.environment});
    await tick(); assert.equal(next.pending.length, 4);
    let finished = false; const completion = b.finish().then(() => {finished = true;});
    await tick(); assert.equal(finished, false);
    for (const p of next.pending) p.resolve(p.object);
    await completion; assert.equal(next.pending.length, 4); assert.equal(finished, true);
});

test('software adapters reuse requested programs without speculative compilation', async () => {
    for (const adapter of [{architecture: 'SwiftShader'}, {fallback: true}]) {
        const profile = {adapter};
        const first = fixture(); const a = install(first.device, {manifest, profile, environment: first.environment});
        for (let i = 0; i < 6; ++i) await first.device.createComputePipelineAsync(descriptor(first.device, `shader ${i}`));
        await a.finish();
        for (const cache of [first.cache, new Map()]) {
            const next = fixture({cache, stored: a.recipe, blocked: true});
            let fetches = 0;
            next.environment.fetch = async () => { ++fetches; return new Response(JSON.stringify(a.recipe)); };
            const b = install(next.device, {manifest: {...manifest,
                'voxy_graphics.json': {sha256: 'c'.repeat(64), size: 2000}}, profile, environment: next.environment});
            await b.warmup; assert.equal(next.pending.length, 0);
            assert.equal(fetches, 0);
            assert.equal(b.stats.earlyDisabled, 'software-adapter');
            const needed = next.device.createComputePipelineAsync(descriptor(next.device, 'needed now'));
            const repeated = next.device.createComputePipelineAsync(descriptor(next.device, 'needed now'));
            assert.equal(next.pending.length, 1, 'duplicate engine requests still share compilation');
            for (const p of next.pending) p.resolve(p.object);
            assert.equal(await needed, await repeated);
            await b.finish();
            assert.equal(b.stats.earlySubmitted, 0);
            assert.equal(b.stats.hits, 1);
        }
    }
});

test('corrupt recipes and device loss never prevent normal failure handling', async () => {
    const first = fixture(); const a = install(first.device, {manifest, environment: first.environment});
    await first.device.createComputePipelineAsync(descriptor(first.device)); await a.finish();
    for (const key of first.cache.keys()) first.cache.set(key, '{broken json');
    const next = fixture({cache: first.cache}); const b = install(next.device, {manifest, environment: next.environment});
    await b.warmup;
    await next.device.createComputePipelineAsync(descriptor(next.device));
    next.lost.resolve({reason: 'destroyed'}); await tick();
    await b.finish();
    assert.equal(b.stats.recipeFailures, 1);
    assert.equal(b.stats.earlySubmitted, 0);
});

test('finish aborts an optional recipe download that is still waiting', async () => {
    const f = fixture();
    f.environment.fetch = (_, {signal}) => new Promise((resolve, reject) => {
        signal.addEventListener('abort', () => reject(Error('aborted')), {once: true});
    });
    const api = install(f.device, {manifest: {...manifest, 'voxy_graphics.json': {sha256: 'c'.repeat(64), size: 20}}, environment: f.environment});
    await tick();
    await f.device.createComputePipelineAsync(descriptor(f.device));
    await api.finish();
    assert.equal(api.stats.requests, 1);
});

test('warmup prioritizes measured expensive programs and drains other workers after a bad entry', async () => {
    const first = fixture(); const a = install(first.device, {manifest, environment: first.environment});
    for (let i = 0; i < 6; ++i) {
        const d = descriptor(first.device); d.compute.constants.COUNT = i;
        await first.device.createComputePipelineAsync(d);
    }
    await a.finish();
    const recipe = a.recipe;
    recipe.pipelines.forEach((row, i) => {row.costMs = i;});
    recipe.pipelines[4].kind = 'invalid';
    for (const key of first.cache.keys()) first.cache.set(key, JSON.stringify(recipe));
    const next = fixture({cache: first.cache, blocked: true});
    const b = install(next.device, {manifest, environment: next.environment});
    await tick();
    assert.equal(next.pending[0].object.descriptor.compute.constants.COUNT, 5);
    assert.equal(b.stats.recipeFailures, 1);
    let done = false; const finish = b.finish().then(() => {done = true;});
    await tick(); assert.equal(done, false);
    for (const p of next.pending) p.resolve(p.object);
    await finish; assert.equal(done, true);
});
