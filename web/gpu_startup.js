// Compile from a small, release-matched recipe while the asset pack downloads.
// Recipes contain WGSL and descriptors, never driver binaries. GPU objects and
// in-flight promises are reused only on the device that created them.
(function(global) {
    'use strict';
    const schema = 1;
    const cacheName = 'voxys-gpu-startup-v1';
    const recipeFile = 'voxy_graphics.json';
    const resourceKinds = ['createShaderModule', 'createBindGroupLayout', 'createPipelineLayout'];
    const pipelineKinds = ['createComputePipelineAsync', 'createRenderPipelineAsync'];
    const maxBytes = 4 * 1024 * 1024;
    const installed = new WeakMap();
    const keyOf = descriptor => JSON.stringify({...descriptor, label: undefined});

    function configurationFor(search) {
        const params = new URLSearchParams(search);
        for (const name of ['experience', 'new', 'world', 'telemetry', 'debug',
            'browserBenchmarkRun', 'physicsProfile', 'renderProfile']) params.delete(name);
        if (params.get('physicsBackend')?.toLowerCase() === 'webgpu') params.delete('physicsBackend');
        params.sort();
        return params.toString();
    }

    function releaseKey(manifest) {
        const wasm = manifest?.['voxy_wasm.wasm']?.sha256;
        const data = manifest?.['voxy_wasm.data']?.sha256;
        return /^[0-9a-f]{64}$/.test(wasm) && /^[0-9a-f]{64}$/.test(data)
            ? `${wasm}:${data}` : null;
    }

    function install(device, {manifest, experience = 'build', profile = {}, configuration = '', environment = global} = {}) {
        if (installed.has(device)) return installed.get(device);
        const release = releaseKey(manifest);
        const resources = [], resourceKeys = new Map(), objects = new WeakMap();
        const pipelines = new Map(), used = new Map();
        const originals = new Map();
        const ownMethods = new Map();
        const fetchAbort = new AbortController();
        let stopped = false, lost = false, finished = false;
        // Software compilers contend with rendering for CPU time and memory.
        // Keep exact-request reuse there, without speculative compilation.
        const software = profile.adapter?.architecture?.toLowerCase() === 'swiftshader'
            || profile.adapter?.fallback === true;
        const now = () => environment.performance?.now() ?? Date.now();
        const stats = {requests: 0, hits: 0, submitted: 0, earlySubmitted: 0,
            earlyHits: 0, earlyDisabled: software ? 'software-adapter' : null,
            recipeSource: 'none', recipeFailures: 0, startedMs: now()};

        // Unknown GPU objects and non-JSON descriptors use the original API.
        // This prevents partial keys from silently aliasing different pipelines.
        function encode(value) {
            if (value === null || ['string', 'boolean'].includes(typeof value)) return value;
            if (typeof value === 'number' && Number.isFinite(value)) return value;
            if (typeof value !== 'object') throw Error('Uncacheable descriptor');
            const reference = objects.get(value);
            if (reference !== undefined) return {$gpu: reference};
            if (Array.isArray(value)) return value.map(encode);
            const proto = Object.getPrototypeOf(value);
            if (proto !== Object.prototype && proto !== null) throw Error('Unknown GPU object');
            const copy = {};
            for (const key of Object.keys(value).sort()) {
                if (value[key] !== undefined) copy[key] = encode(value[key]);
            }
            return copy;
        }
        function decode(value, refs) {
            if (value === null || typeof value !== 'object') return value;
            if (Array.isArray(value)) return value.map(item => decode(item, refs));
            if ('$gpu' in value) {
                if (Object.keys(value).length !== 1 || !Number.isInteger(value.$gpu)
                    || !refs[value.$gpu]) throw Error('Invalid GPU reference');
                return refs[value.$gpu];
            }
            return Object.fromEntries(Object.entries(value).map(([key, item]) => [key, decode(item, refs)]));
        }
        function resource(kind, descriptor) {
            const encoded = encode(descriptor), key = kind + keyOf(encoded);
            const hit = resourceKeys.get(key);
            if (hit !== undefined) return resources[hit].object;
            const object = originals.get(kind).call(device, descriptor);
            const id = resources.length;
            resources.push({kind, descriptor: encoded, object});
            resourceKeys.set(key, id); objects.set(object, id);
            return object;
        }
        function pipeline(kind, descriptor, early = false) {
            const encoded = encode(descriptor), key = kind + keyOf(encoded);
            if (!early) { ++stats.requests; used.set(key, {kind, descriptor: encoded}); }
            const hit = pipelines.get(key);
            if (hit) {
                if (!early) { ++stats.hits; if (hit.early) ++stats.earlyHits; }
                // A failed speculative compile must not prevent a normal retry.
                return hit.promise.catch(error => {
                    if (!early && hit.early && !lost) return pipeline(kind, descriptor);
                    throw error;
                });
            }
            ++stats.submitted;
            if (early) {
                ++stats.earlySubmitted;
                stats.firstEarlyMs ??= now();
            }
            const entry = {early};
            const started = now();
            entry.promise = originals.get(kind).call(device, descriptor).then(result => {
                entry.costMs = now() - started;
                return result;
            }, error => {
                if (pipelines.get(key) === entry) pipelines.delete(key);
                throw error;
            });
            pipelines.set(key, entry);
            return entry.promise;
        }

        for (const kind of [...resourceKinds, ...pipelineKinds]) {
            originals.set(kind, device[kind]);
            ownMethods.set(kind, Object.getOwnPropertyDescriptor(device, kind));
            device[kind] = function(descriptor) {
                if (this !== device || lost || finished) return originals.get(kind).call(this, descriptor);
                // Only serialization failures fall back. Never hide an error
                // from WebGPU itself, or submit a failed descriptor twice.
                try { encode(descriptor); } catch { return originals.get(kind).call(this, descriptor); }
                return resourceKinds.includes(kind) ? resource(kind, descriptor) : pipeline(kind, descriptor);
            };
        }

        function snapshot() {
            // Keep only resources reachable from pipelines the engine requested.
            const selected = [], remap = new Map();
            function copy(value) {
                if (value === null || typeof value !== 'object') return value;
                if (Array.isArray(value)) return value.map(copy);
                if ('$gpu' in value) {
                    const old = value.$gpu;
                    if (!remap.has(old)) {
                        const row = resources[old];
                        const descriptor = copy(row.descriptor);
                        remap.set(old, selected.length);
                        selected.push({kind: row.kind, descriptor});
                    }
                    return {$gpu: remap.get(old)};
                }
                return Object.fromEntries(Object.entries(value).map(([key, item]) => [key, copy(item)]));
            }
            const requests = [...used].map(([key, row]) => ({kind: row.kind,
                descriptor: copy(row.descriptor), costMs: pipelines.get(key)?.costMs ?? 0}));
            return {schema, release, experience, configuration, resources: selected, pipelines: requests};
        }

        async function replay(recipe) {
            if (!recipe || recipe.schema !== schema || recipe.release !== release
                || recipe.experience !== experience || (recipe.configuration ?? '') !== configuration
                || !Array.isArray(recipe.resources)
                || !Array.isArray(recipe.pipelines) || recipe.resources.length > 1024
                || recipe.pipelines.length > 1024) throw Error('Graphics recipe does not match this load');
            if (stopped || lost) return;
            const refs = [];
            // Resource validation is speculative. Pop before yielding so an
            // engine error scope cannot be nested in this scope accidentally.
            device.pushErrorScope('validation');
            let importError;
            try {
                for (const row of recipe.resources) {
                    if (!resourceKinds.includes(row.kind)) throw Error('Invalid resource kind');
                    refs.push(resource(row.kind, decode(row.descriptor, refs)));
                }
            } catch (error) { importError = error; }
            const validation = device.popErrorScope();
            if (await validation || importError) throw Error('Invalid graphics recipe resources');
            let cursor = 0;
            // Start expensive programs first. These are measured preparation
            // costs, never shader settings or runtime simulation parameters.
            const cost = row => Number.isFinite(row.costMs) ? row.costMs : 0;
            const queue = [...recipe.pipelines].sort((a, b) => cost(b) - cost(a));
            // Bound compiler concurrency and let real engine requests take
            // priority over recipes still waiting to start.
            await Promise.all(Array.from({length: 4}, async () => {
                while (!stopped && !lost && cursor < queue.length) {
                    const row = queue[cursor++];
                    try {
                        if (!pipelineKinds.includes(row?.kind)) throw Error('Invalid pipeline kind');
                        await pipeline(row.kind, decode(row.descriptor, refs), true);
                    }
                    catch { ++stats.recipeFailures; }
                }
            }));
        }

        const cachePromise = release && environment.caches
            ? environment.caches.open(cacheName).catch(() => null) : Promise.resolve(null);
        const variant = encodeURIComponent(JSON.stringify({profile, configuration}));
        const cacheUrl = release ? `voxy_graphics_saved.json?release=${release}&experience=${encodeURIComponent(experience)}&device=${variant}` : null;
        async function read(response) {
            if (!response?.ok) return null;
            const text = await response.text();
            if (text.length > maxBytes) throw Error('Graphics recipe is too large');
            return JSON.parse(text);
        }
        async function load() {
            if (!release || software) return;
            const cache = await cachePromise;
            try {
                const saved = cache && await read(await cache.match(cacheUrl));
                if (saved) { stats.recipeSource = 'saved'; await replay(saved); return; }
            } catch { ++stats.recipeFailures; }
            const entry = manifest?.[recipeFile];
            // The published recipe is captured from the default experience.
            // Other modes learn their own recipe after a successful startup.
            if (!entry || experience !== 'build' || configuration || profile.name === 'cpu-fallback'
                || entry.size > maxBytes || stopped || lost) return;
            try {
                const recipe = await read(await environment.fetch(`${recipeFile}?h=${entry.sha256}`, {
                    credentials: 'same-origin', signal: fetchAbort.signal,
                }));
                stats.recipeSource = 'release'; await replay(recipe);
            } catch { ++stats.recipeFailures; }
        }
        const api = {
            stats, snapshot,
            warmup: load().catch(() => { ++stats.recipeFailures; }),
            async finish() {
                if (api.completion) return api.completion;
                stopped = true;
                fetchAbort.abort();
                api.completion = (async () => {
                    // No speculative compilation may spill into gameplay.
                    await api.warmup;
                    const recipe = snapshot();
                    stats.finishedMs = now();
                    stats.uniquePipelines = recipe.pipelines.length;
                    finished = true;
                    for (const [kind, property] of ownMethods) {
                        if (property) Object.defineProperty(device, kind, property);
                        else delete device[kind];
                    }
                    const cache = await cachePromise;
                    if (cache && !lost && recipe.pipelines.length) {
                        try {
                            const text = JSON.stringify(recipe);
                            if (text.length <= maxBytes) {
                                await cache.put(cacheUrl, new environment.Response(text, {headers: {'Content-Type': 'application/json'}}));
                                for (const request of await cache.keys()) {
                                    if (new URL(request.url).searchParams.get('release') !== release) await cache.delete(request);
                                }
                            }
                        } catch { /* Storage is optional; normal startup succeeded. */ }
                    }
                    api.recipe = recipe;
                    // Engine handles own the used objects. Release extra warmup
                    // objects and descriptor strings before the first frame.
                    resources.length = 0; resourceKeys.clear(); pipelines.clear(); used.clear();
                    return stats;
                })();
                return api.completion;
            },
        };
        installed.set(device, api);
        device.lost.then(() => { lost = true; stopped = true; });
        return api;
    }
    global.VoxyGpuStartup = {install, releaseKey, configurationFor};
})(globalThis);
