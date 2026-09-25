import fs from 'node:fs';
import path from 'node:path';
import http from 'node:http';
import {spawn} from 'node:child_process';
import {createHash} from 'node:crypto';

// Hardware differential check of the actual production kernels and layout.
// Compile time, uploads and readbacks are excluded from GPU timestamp samples.
// node scripts/performance/compare_narrow_runtime.mjs --chrome=PATH \
//   --before=SHADER --after=SHADER --output=DIR [--pairs=1024] [--seed=1899408273] [--workgroup=128]
const args = Object.fromEntries(process.argv.slice(2).map(arg => {
    const at = arg.indexOf('=');
    if (!arg.startsWith('--') || at < 0) throw Error('Use --name=value arguments');
    return [arg.slice(2, at), arg.slice(at + 1)];
}));
for (const key of ['chrome', 'before', 'after', 'output']) {
    if (!args[key]) throw Error('--' + key + ' is required');
}
const pairs = Number(args.pairs || 1024);
if (!Number.isInteger(pairs) || pairs < 128 || pairs > 16384) throw Error('Invalid pair count');
const seed = Number(args.seed ?? 0x7136ab91);
if (!Number.isInteger(seed) || seed < 1 || seed > 0xffffffff) throw Error('Invalid random seed');
const workgroup = Number(args.workgroup ?? 128);
if (![64, 128, 256].includes(workgroup)) throw Error('Invalid production workgroup size');
const sources = Object.fromEntries(['before', 'after'].map(key => [key, fs.readFileSync(args[key], 'utf8')]));
const hashes = Object.fromEntries(Object.entries(sources).map(([key, code]) =>
    [key, createHash('sha256').update(code).digest('hex')]));
const dir = path.resolve(args.output);
fs.mkdirSync(dir, {recursive: true});
if (fs.existsSync(path.join(dir, 'report.json'))) throw Error('Refusing to overwrite an existing report');
const profile = fs.mkdtempSync(path.join(dir, 'profile-'));
const server = http.createServer((req, res) => {
    res.setHeader('Content-Type', req.url === '/inputs.json' ? 'application/json' : 'text/html');
    res.end(req.url === '/inputs.json' ? JSON.stringify({sources, pairs, seed, workgroup})
        : '<!doctype html><title>Production collision runtime comparison</title>');
});
await new Promise(resolve => server.listen(0, '127.0.0.1', resolve));
const chrome = spawn(args.chrome, ['--headless=new', '--remote-debugging-port=0',
    '--user-data-dir=' + profile, '--no-first-run', '--no-default-browser-check',
    '--disable-background-timer-throttling', '--disable-renderer-backgrounding', 'about:blank']);
let stderr = '', spawnError, socket;
chrome.on('error', error => { spawnError = error; });
chrome.stderr.on('data', data => { stderr += data; });
const delay = ms => new Promise(resolve => setTimeout(resolve, ms));
const pending = new Map();
let messageId = 0;

async function run() {
    const {sources, pairs, seed, workgroup} = await (await fetch('/inputs.json')).json();
    const report = globalThis.report = {pairs, seed, workgroup, kernels: [], errors: [], lost: null};
    const adapter = await navigator.gpu.requestAdapter({powerPreference: 'high-performance'});
    const identity = adapter && [adapter.info.vendor, adapter.info.architecture, adapter.info.description].join(' ');
    if (!adapter || adapter.isFallbackAdapter || adapter.info.isFallbackAdapter || /swiftshader|llvmpipe/i.test(identity)) {
        throw Error('Hardware WebGPU adapter required');
    }
    report.adapter = {vendor: adapter.info.vendor, architecture: adapter.info.architecture,
        device: adapter.info.device, description: adapter.info.description};
    if (!adapter.features.has('timestamp-query')) throw Error('GPU timestamps required');
    const device = await adapter.requestDevice({requiredFeatures: ['timestamp-query'],
        requiredLimits: {maxStorageBuffersPerShaderStage: 8}});
    device.lost.then(info => { report.lost = {reason: info.reason, message: info.message}; });
    device.addEventListener('uncapturederror', event => report.errors.push(event.error.message));
    const entries = [[0, true], [1, true], [4, false], [6, true], [7, false],
        [8, true], [9, false], [15, true]].map(([binding, readOnly]) => ({binding,
        visibility: GPUShaderStage.COMPUTE, buffer: {type: readOnly ? 'read-only-storage' : 'storage'}}));
    entries.push({binding: 10, visibility: GPUShaderStage.COMPUTE, buffer: {type: 'uniform', minBindingSize: 48}},
        {binding: 16, visibility: GPUShaderStage.COMPUTE, buffer: {type: 'uniform', minBindingSize: 128}});
    const bindLayout = device.createBindGroupLayout({entries});
    const layout = device.createPipelineLayout({bindGroupLayouts: [bindLayout]});
    const modules = Object.fromEntries(Object.entries(sources).map(([key, code]) =>
        [key, device.createShaderModule({label: key, code})]));
    function buffer(data, usage = GPUBufferUsage.STORAGE) {
        const size = typeof data === 'number' ? data : data.byteLength;
        const result = device.createBuffer({size, usage: usage | GPUBufferUsage.COPY_DST | GPUBufferUsage.COPY_SRC});
        if (typeof data !== 'number') device.queue.writeBuffer(result, 0, data);
        return result;
    }
    const median = values => [...values].sort((a, b) => a - b)[Math.floor(values.length / 2)];
    // Authored heap format 1: one 2 m box, six exterior faces and one BVH leaf.
    // The native contact suite separately covers concave, multi-cell geometry.
    function authoredBoxHeap() {
        const rows = new ArrayBuffer(39 * 16), u = new Uint32Array(rows), f = new Float32Array(rows);
        u.set([1, 1, 16, 19], 0); u.set([37, 39, 1, 6], 4);
        u.set([1, 1, 0, 1], 9 * 4); u.set([0, 6, 0, 1], 10 * 4);
        f.set([0, 0, 0, 1], 11 * 4); f.set([0, 0, 0, 1], 12 * 4);
        f.set([1, 1, 1, Math.sqrt(3)], 13 * 4);
        u.set([-50, -50, -50, 0], 14 * 4); u.set([50, 50, 50, 0], 15 * 4);
        u.set([-50, -50, -50, 7], 16 * 4); u.set([50, 50, 50, 0], 17 * 4);
        u.set([6, 0, 0, 0], 18 * 4);
        for (let axis = 0; axis < 3; ++axis) for (let side = 0; side < 2; ++side) {
            const row = 19 + 3 * (2 * axis + side), lower = [-50, -50, -50, 7], upper = [50, 50, 50, 0];
            lower[axis] = upper[axis] = side ? 50 : -50;
            u.set(lower, row * 4); u.set(upper, (row + 1) * 4); u.set([axis, side ? 1 : -1, 0, 0], (row + 2) * 4);
        }
        u.set([-50, -50, -50, 0], 37 * 4); u.set([50, 50, 50, 1], 38 * 4);
        return rows;
    }
    const fixtures = [
        {name: 'sphere_sphere', pairClass: 0, shapeType: 0}, // Unchanged control kernel.
        {name: 'box_box', pairClass: 5, shapeType: 2},
        {name: 'cylinder_cylinder', pairClass: 9, shapeType: 4},
        {name: 'sphere_box', pairClass: 3, shapeType: 0, authored: true},
        {name: 'capsule_box', pairClass: 4, shapeType: 3, authored: true},
        {name: 'box_box', pairClass: 5, shapeType: 2, authored: true},
    ];
    for (const {name, pairClass, shapeType, authored = false} of fixtures) {
        const record = {name, authored, samples: [], compile_ms: {}};
        report.kernels.push(record);
        const pipelines = {};
        device.pushErrorScope('validation');
        for (const key of ['before', 'after']) {
            const start = performance.now();
            pipelines[key] = await device.createComputePipelineAsync({layout,
                compute: {module: modules[key], entryPoint: 'narrow_' + name + '_' + workgroup,
                    ...(pairClass === 5 || authored ? {constants: {AUTHORED_PAIR_PASS: Number(authored)}} : {})}});
            record.compile_ms[key] = performance.now() - start;
        }
        const bodyCount = 2 * pairs + 1;
        const poses = new Float32Array(bodyCount * 8);
        const shapes = new Float32Array(bodyCount * 16);
        const metadata = new Int32Array(bodyCount * 4);
        const pairData = new Uint32Array(pairs * 4);
        let seed = report.seed;
        function random() { seed ^= seed << 13; seed ^= seed >>> 17; seed ^= seed << 5; return (seed >>> 0) / 4294967296; }
        for (let i = 0; i < pairs; ++i) {
            const a = 2 * i + 1, b = a + 1;
            pairData.set([a, b, pairClass, i], 4 * i);
            for (const body of [a, b]) {
                const angle = i % 4 === 0 ? 0 : (random() - .5) * 1.5;
                const axis = [random() - .5, random() - .5, random() - .5];
                const scale = Math.sin(angle * .5) / Math.hypot(...axis);
                poses.set([body === a ? 0 : (i % 5 === 0 ? 4 : .4 + random() * .8),
                    (random() - .5) * .2, (random() - .5) * .2, 1,
                    ...axis.map(x => x * scale), Math.cos(angle * .5)], body * 8);
                shapes.set([1 + random() * .5, 1 + random() * .7, 1 + random() * .5,
                    shapeType, 1, 1, 1, 0, -1, -1, -1, 1], body * 16);
                if (authored) {
                    if (pairClass === 5 || body === (i % 2 ? a : b)) {
                        shapes.set([2, 2, 2, 2], body * 16);
                        new Uint32Array(shapes.buffer).set([1, 1, 1, 0], body * 16 + 12);
                    } else {
                        shapes.set(pairClass === 3 ? [.8, .8, .8, 0] : [.6, 1.4, .6, 3], body * 16);
                    }
                }
                metadata[body * 4 + 3] = 1 | (1 << 20) | (1 << 21);
            }
        }
        const params = new ArrayBuffer(48);
        new Uint32Array(params).set([bodyCount, pairs, pairs, workgroup]);
        new Float32Array(params).set([.005, .04, .08, 0], 4);
        new Uint32Array(params).set([pairs, 1, pairs, 0], 8);
        const classes = new Uint32Array(32);
        classes[pairClass] = pairs;
        const outputBytes = pairs * 384;
        const buffers = {0: buffer(poses), 1: buffer(shapes), 4: buffer(pairData), 6: buffer(384),
            7: buffer(outputBytes), 8: buffer(metadata), 9: buffer(128), 15: buffer(authored ? authoredBoxHeap() : 16),
            10: buffer(params, GPUBufferUsage.UNIFORM), 16: buffer(classes, GPUBufferUsage.UNIFORM)};
        const group = device.createBindGroup({layout: bindLayout, entries: Object.entries(buffers)
            .map(([binding, buffer]) => ({binding: Number(binding), resource: {buffer}}))});
        function dispatch(encoder, key, timestampWrites) {
            encoder.clearBuffer(buffers[9]);
            const pass = encoder.beginComputePass(timestampWrites ? {timestampWrites} : {});
            pass.setPipeline(pipelines[key]); pass.setBindGroup(0, group);
            pass.dispatchWorkgroups(Math.ceil(pairs / workgroup)); pass.end();
        }
        async function snapshot(key) {
            const readback = device.createBuffer({size: outputBytes, usage: GPUBufferUsage.MAP_READ | GPUBufferUsage.COPY_DST});
            const encoder = device.createCommandEncoder();
            encoder.clearBuffer(buffers[7]); dispatch(encoder, key);
            encoder.copyBufferToBuffer(buffers[7], 0, readback, 0, outputBytes);
            device.queue.submit([encoder.finish()]);
            await readback.mapAsync(GPUMapMode.READ);
            const data = readback.getMappedRange().slice(0); readback.unmap(); readback.destroy();
            return data;
        }
        const before = await snapshot('before'), after = await snapshot('after');
        const bu = new Uint32Array(before), au = new Uint32Array(after);
        const bf = new Float32Array(before), af = new Float32Array(after);
        const comparison = {bitwise_equal: true, integer_mismatches: 0, float_mismatches: 0,
            max_absolute_error: 0, contacts: 0, misses: 0, contact_points: 0};
        for (let i = 0; i < bu.length; ++i) {
            const word = i % 96;
            if (bu[i] !== au[i]) comparison.bitwise_equal = false;
            const integer = word < 8 || (word >= 32 && (word - 32) % 16 >= 8 && (word - 32) % 16 < 12);
            if (integer) { if (bu[i] !== au[i]) ++comparison.integer_mismatches; }
            else {
                const error = Math.abs(bf[i] - af[i]);
                if (!Number.isFinite(bf[i]) || !Number.isFinite(af[i]) || error > 2e-5) ++comparison.float_mismatches;
                comparison.max_absolute_error = Math.max(comparison.max_absolute_error, error);
            }
            if (word === 4) {
                if (bu[i] > 4 || au[i] > 4) throw Error('Invalid manifold point count');
                if (bu[i]) ++comparison.contacts; else ++comparison.misses;
                comparison.contact_points += bu[i];
            }
        }
        record.comparison = comparison;
        if (!comparison.contacts || !comparison.misses || comparison.integer_mismatches || comparison.float_mismatches) {
            throw Error('Contact differential failed: ' + name);
        }
        const warm = device.createCommandEncoder();
        for (let i = 0; i < 8; ++i) for (const key of ['before', 'after']) dispatch(warm, key);
        device.queue.submit([warm.finish()]); await device.queue.onSubmittedWorkDone();
        const count = 20;
        const queries = device.createQuerySet({type: 'timestamp', count: count * 2});
        const resolved = device.createBuffer({size: count * 16, usage: GPUBufferUsage.QUERY_RESOLVE | GPUBufferUsage.COPY_SRC});
        const readback = device.createBuffer({size: count * 16, usage: GPUBufferUsage.COPY_DST | GPUBufferUsage.MAP_READ});
        for (let round = 0; round < 4; ++round) {
            for (const key of round % 2 ? ['after', 'before'] : ['before', 'after']) {
                const encoder = device.createCommandEncoder();
                for (let i = 0; i < count; ++i) dispatch(encoder, key, {querySet: queries,
                    beginningOfPassWriteIndex: 2 * i, endOfPassWriteIndex: 2 * i + 1});
                encoder.resolveQuerySet(queries, 0, count * 2, resolved, 0);
                encoder.copyBufferToBuffer(resolved, 0, readback, 0, count * 16);
                device.queue.submit([encoder.finish()]);
                await readback.mapAsync(GPUMapMode.READ);
                const times = new BigUint64Array(readback.getMappedRange());
                const milliseconds = Array.from({length: count}, (_, i) => Number(times[2 * i + 1] - times[2 * i]) / 1e6);
                record.samples.push({round, variant: key, median_ms: median(milliseconds), milliseconds});
                readback.unmap();
            }
        }
        record.median_ms = Object.fromEntries(['before', 'after'].map(key =>
            [key, median(record.samples.filter(s => s.variant === key).flatMap(s => s.milliseconds))]));
        if (Object.values(record.median_ms).some(ms => !Number.isFinite(ms) || ms <= 0)) {
            throw Error('Insufficient GPU timestamp resolution: increase --pairs');
        }
        const error = await device.popErrorScope();
        if (error) report.errors.push(error.message);
        if (report.errors.length || report.lost) throw Error('GPU error: ' + name);
        for (const b of Object.values(buffers)) b.destroy();
        queries.destroy(); resolved.destroy(); readback.destroy();
    }
    report.complete = true;
}

try {
    let port;
    for (let n = 0; n < 200; ++n) {
        if (spawnError) throw spawnError;
        port = stderr.match(/DevTools listening on ws:\/\/127.0.0.1:(\d+)/)?.[1];
        if (port) break; await delay(100);
    }
    if (!port) throw Error(stderr);
    const target = await (await fetch(`http://127.0.0.1:${port}/json/new?about:blank`, {method: 'PUT'})).json();
    socket = new WebSocket(target.webSocketDebuggerUrl);
    await new Promise((resolve, reject) => { socket.onopen = resolve; socket.onerror = reject; });
    socket.onmessage = event => {
        const message = JSON.parse(event.data), request = pending.get(message.id);
        if (!request) return;
        pending.delete(message.id); clearTimeout(request.timer);
        if (message.error) request.reject(Error(JSON.stringify(message.error))); else request.resolve(message.result);
    };
    const call = (method, params = {}) => new Promise((resolve, reject) => {
        const id = ++messageId;
        const timer = setTimeout(() => { pending.delete(id); reject(Error('Timeout: ' + method)); }, 900000);
        pending.set(id, {resolve, reject, timer}); socket.send(JSON.stringify({id, method, params}));
    });
    await call('Page.enable');
    await call('Page.navigate', {url: `http://127.0.0.1:${server.address().port}/`});
    await delay(500);
    await call('Runtime.evaluate', {expression: `(${run.toString()})().catch(error=>{globalThis.report||={};report.error=String(error);report.complete=true;});true`});
    let result;
    for (let n = 0; n < 450; ++n) {
        await delay(2000);
        const response = await call('Runtime.evaluate', {expression: 'globalThis.report', returnByValue: true});
        result = response.result?.value;
        if (n % 10 === 0) console.log(JSON.stringify(result?.kernels?.map(k => ({name: k.name, samples: k.samples.length}))));
        if (result?.complete) break;
    }
    result ||= {};
    result.shader_sha256 = hashes; result.browser = await call('Browser.getVersion');
    fs.writeFileSync(path.join(dir, 'report.json'), JSON.stringify(result, null, 2));
    console.log(JSON.stringify(result.kernels?.map(k => ({name: k.name, comparison: k.comparison, median_ms: k.median_ms}))));
    if (!result.complete || result.error || result.errors?.length || result.lost) process.exitCode = 1;
    await call('Browser.close');
} finally {
    for (const request of pending.values()) clearTimeout(request.timer);
    socket?.close(); chrome.kill(); server.close();
    fs.writeFileSync(path.join(dir, 'chrome.log'), stderr);
}
