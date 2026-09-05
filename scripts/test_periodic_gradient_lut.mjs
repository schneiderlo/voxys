#!/usr/bin/env node
// Real WebGPU differential test. Software adapters are allowed for correctness,
// never for an engine-FPS claim. Requires Node >=22 and a WebGPU Chrome binary.
import assert from 'node:assert/strict';
import {readFile, mkdtemp, rm, writeFile} from 'node:fs/promises';
import {tmpdir} from 'node:os';
import path from 'node:path';
import http from 'node:http';
import {spawn} from 'node:child_process';
import {fileURLToPath} from 'node:url';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
function functionText(source, name) {
    const start = source.indexOf(`fn ${name}(`);
    assert(start >= 0, `missing ${name}`);
    let depth = 0;
    for (let i = source.indexOf('{', start); i < source.length; ++i) {
        if (source[i] === '{') ++depth;
        if (source[i] === '}' && --depth === 0) return source.slice(start, i + 1);
    }
    throw new Error(`unterminated ${name}`);
}
const ray = await readFile(path.join(root, 'shaders/ray_blit.wgsl'), 'utf8');
const water = await readFile(path.join(root, 'shaders/water_clipmap.wgsl'), 'utf8');
const header = await readFile(path.join(root, 'src/render/periodic_gradient_lut.hpp'), 'utf8');
const bake = header.match(/R"wgsl\(([\s\S]*?)\)wgsl"/)[1];
const noise = functionText(ray, 'periodicGradientHash') + '\n'
    + functionText(ray, 'periodicGradientNoise');
const probe = `
@group(0) @binding(19) var periodicGradientLut : texture_2d<f32>;
override USE_PERIODIC_GRADIENT_LUT : bool = true;
@group(1) @binding(0) var<storage, read> points : array<vec2<f32>>;
@group(1) @binding(1) var<storage, read_write> output : array<vec4<f32>>;
${noise}
@compute @workgroup_size(64)
fn check(@builtin(global_invocation_id) id : vec3<u32>) {
    if (id.x >= arrayLength(&points)) { return; }
    let p = points[id.x];
    output[id.x] = vec4<f32>(periodicGradientHash(floor(p)),
        periodicGradientNoise(p),
        periodicGradientNoise(p.yx * 1.37 + vec2<f32>(7.1, 11.3)));
}
@compute @workgroup_size(64)
fn benchmark(@builtin(global_invocation_id) id : vec3<u32>) {
    if (id.x >= arrayLength(&points)) { return; }
    var p = points[id.x];
    var sum = 0.0;
    for (var octave = 0u; octave < 12u; octave += 1u) {
        let n = periodicGradientNoise(p);
        sum += n;
        p = p.yx * 1.37 + vec2<f32>(n + 7.1, 11.3);
    }
    output[id.x] = vec4<f32>(sum, p, 1.0);
}
@vertex
fn testVertex(@builtin(vertex_index) index : u32) -> @builtin(position) vec4<f32> {
    let x = f32((index << 1u) & 2u);
    let y = f32(index & 2u);
    return vec4<f32>(x * 2.0 - 1.0, y * 2.0 - 1.0, 0.0, 1.0);
}
@fragment
fn fragmentCheck(@builtin(position) position : vec4<f32>) -> @location(0) vec4<f32> {
    let index = u32(position.y) * 257u + u32(position.x);
    if (index >= arrayLength(&points)) { return vec4<f32>(0.0); }
    let p = points[index];
    return vec4<f32>(periodicGradientHash(floor(p)), periodicGradientNoise(p),
        periodicGradientNoise(p.yx * 1.37 + vec2<f32>(7.1, 11.3)));
}`;

async function gpuTest({bake, probe, ray, water}) {
    if (!navigator.gpu) throw new Error('WebGPU unavailable (not a pass)');
    const adapter = await navigator.gpu.requestAdapter()
        ?? await navigator.gpu.requestAdapter({forceFallbackAdapter: true});
    if (!adapter) throw new Error('No WebGPU adapter (not a pass)');
    const timestamps = adapter.features.has('timestamp-query');
    const device = await adapter.requestDevice({
        requiredFeatures: timestamps ? ['timestamp-query'] : [],
    });
    const errors = [];
    device.addEventListener('uncapturederror', e => errors.push(e.error.message));
    device.pushErrorScope('validation');
    async function shader(label, code) {
        const module = device.createShaderModule({label, code});
        const info = await module.getCompilationInfo();
        const failed = info.messages.filter(m => m.type === 'error');
        if (failed.length) throw new Error(`${label}: ${failed.map(m => m.message).join('\n')}`);
        return module;
    }
    const modules = await Promise.all([
        shader('production terrain', ray), shader('production water', water),
        shader('GPU gradient bake', bake), shader('differential probe', probe),
    ]);
    // Compile the actual entry points too, not just the WGSL front end.
    // Both specializations must satisfy portable resource limits.
    for (const enabled of [0, 1]) {
        const constants = {USE_PERIODIC_GRADIENT_LUT: enabled};
        await device.createRenderPipelineAsync({
            layout: 'auto', vertex: {module: modules[0], entryPoint: 'vs'},
            fragment: {module: modules[0], entryPoint: 'fsBackground',
                constants, targets: [{format: 'rgba16float'}]},
        });
        await device.createRenderPipelineAsync({
            layout: 'auto', vertex: {module: modules[0], entryPoint: 'vs'},
            fragment: {module: modules[0], entryPoint: 'fsCachedOpaqueColor',
                constants, targets: [{format: 'bgra8unorm'}]},
        });
        await device.createRenderPipelineAsync({
            layout: 'auto', vertex: {module: modules[1], entryPoint: 'vs',
                buffers: [{arrayStride: 12, attributes: [
                    {shaderLocation: 0, offset: 0, format: 'float32x3'}]}]},
            fragment: {module: modules[1], entryPoint: 'fsColor',
                constants, targets: [{format: 'bgra8unorm'}]},
        });
    }
    const texture = device.createTexture({
        size: [32, 16], format: 'rgba32float',
        usage: GPUTextureUsage.STORAGE_BINDING | GPUTextureUsage.TEXTURE_BINDING
             | GPUTextureUsage.COPY_SRC,
    });
    const bakePipeline = await device.createComputePipelineAsync({
        layout: 'auto', compute: {module: modules[2], entryPoint: 'main'},
    });
    const bakeGroup = device.createBindGroup({
        layout: bakePipeline.getBindGroupLayout(0),
        entries: [{binding: 0, resource: texture.createView()}],
    });
    let encoder = device.createCommandEncoder();
    let pass = encoder.beginComputePass();
    pass.setPipeline(bakePipeline); pass.setBindGroup(0, bakeGroup);
    pass.dispatchWorkgroups(2, 2); pass.end();
    device.queue.submit([encoder.finish()]); // intentionally no intervening wait

    // Cover all 256 lattice cells, negative wrapping, exact/both sides of
    // boundaries, large coordinates, and an odd random batch tail.
    const values = [];
    for (let y = -32; y <= 32; ++y) for (let x = -32; x <= 32; ++x) {
        for (const delta of [-0.0001, 0, 0.0001, 0.375, 0.9999]) {
            values.push(x + delta, y - delta);
        }
    }
    for (const n of [-8388607, -65536, -16, 0, 16, 65536, 8388607]) {
        values.push(n, n, n + 0.5, -n);
    }
    let seed = 0x12345678;
    function random() {
        seed = (Math.imul(seed, 1664525) + 1013904223) >>> 0;
        return (seed / 4294967296 - 0.5) * 32768;
    }
    while (values.length < 65539 * 2) values.push(random(), random());
    const points = new Float32Array(values);
    const count = points.length / 2;
    const input = device.createBuffer({size: points.byteLength,
        usage: GPUBufferUsage.STORAGE | GPUBufferUsage.COPY_DST});
    device.queue.writeBuffer(input, 0, points);
    const outputSize = count * 16;
    const outputs = [0, 1].map(() => device.createBuffer({size: outputSize,
        usage: GPUBufferUsage.STORAGE | GPUBufferUsage.COPY_SRC}));
    const readback = device.createBuffer({size: outputSize * 2,
        usage: GPUBufferUsage.COPY_DST | GPUBufferUsage.MAP_READ});
    const textureLayout = device.createBindGroupLayout({entries: [{
        binding: 19, visibility: GPUShaderStage.COMPUTE | GPUShaderStage.FRAGMENT,
        texture: {sampleType: 'unfilterable-float'},
    }]});
    const buffersLayout = device.createBindGroupLayout({entries: [
        {binding: 0, visibility: GPUShaderStage.COMPUTE | GPUShaderStage.FRAGMENT, buffer: {type: 'read-only-storage'}},
        {binding: 1, visibility: GPUShaderStage.COMPUTE, buffer: {type: 'storage'}},
    ]});
    const layout = device.createPipelineLayout({bindGroupLayouts: [textureLayout, buffersLayout]});
    const textureGroup = device.createBindGroup({layout: textureLayout,
        entries: [{binding: 19, resource: texture.createView()}]});
    const bufferGroups = outputs.map(buffer => device.createBindGroup({layout: buffersLayout,
        entries: [{binding: 0, resource: {buffer: input}},
                  {binding: 1, resource: {buffer}}]}));
    async function pipelines(entryPoint) {
        return Promise.all([0, 1].map(enabled => device.createComputePipelineAsync({
            layout, compute: {module: modules[3], entryPoint,
                constants: {USE_PERIODIC_GRADIENT_LUT: enabled}},
        })));
    }
    const checks = await pipelines('check');
    function dispatch(encoder, pipeline, index, descriptor = {}) {
        const pass = encoder.beginComputePass(descriptor);
        pass.setPipeline(pipeline); pass.setBindGroup(0, textureGroup);
        pass.setBindGroup(1, bufferGroups[index]);
        pass.dispatchWorkgroups(Math.ceil(count / 64)); pass.end();
    }
    encoder = device.createCommandEncoder();
    for (let i = 0; i < 2; ++i) {
        dispatch(encoder, checks[i], i);
        encoder.copyBufferToBuffer(outputs[i], 0, readback, i * outputSize, outputSize);
    }
    device.queue.submit([encoder.finish()]);
    await readback.mapAsync(GPUMapMode.READ);
    const result = new Float32Array(readback.getMappedRange().slice(0));
    const words = outputSize / 4;
    let maximumError = 0, differentWords = 0;
    for (let i = 0; i < words; ++i) {
        const error = Math.abs(result[i] - result[words + i]);
        if (!Number.isFinite(error)) throw new Error(`nonfinite output at word ${i}`);
        maximumError = Math.max(maximumError, error);
        if (error !== 0) ++differentWords;
    }
    readback.unmap();
    if (maximumError > 1e-6) throw new Error(`LUT differential failed: max error ${maximumError}`);

    // Compare fragment execution too: compute-only agreement is insufficient
    // when the production consumer is a material fragment shader.
    const width = 257, height = Math.ceil(count / width);
    const rowBytes = Math.ceil(width * 16 / 256) * 256;
    const imageBytes = rowBytes * height;
    const images = [0, 1].map(() => device.createTexture({
        size: [width, height], format: 'rgba32float',
        usage: GPUTextureUsage.RENDER_ATTACHMENT | GPUTextureUsage.COPY_SRC,
    }));
    const imageReadback = device.createBuffer({size: imageBytes * 2,
        usage: GPUBufferUsage.COPY_DST | GPUBufferUsage.MAP_READ});
    const fragments = await Promise.all([0, 1].map(enabled =>
        device.createRenderPipelineAsync({layout,
            vertex: {module: modules[3], entryPoint: 'testVertex'},
            fragment: {module: modules[3], entryPoint: 'fragmentCheck',
                constants: {USE_PERIODIC_GRADIENT_LUT: enabled},
                targets: [{format: 'rgba32float'}]},
            primitive: {topology: 'triangle-list'},
        })));
    encoder = device.createCommandEncoder();
    for (let i = 0; i < 2; ++i) {
        pass = encoder.beginRenderPass({colorAttachments: [{view: images[i].createView(),
            loadOp: 'clear', storeOp: 'store', clearValue: [0, 0, 0, 0]}]});
        pass.setPipeline(fragments[i]); pass.setBindGroup(0, textureGroup);
        pass.setBindGroup(1, bufferGroups[i]); pass.draw(3); pass.end();
        encoder.copyTextureToBuffer({texture: images[i]},
            {buffer: imageReadback, offset: i * imageBytes, bytesPerRow: rowBytes},
            {width, height});
    }
    device.queue.submit([encoder.finish()]);
    await imageReadback.mapAsync(GPUMapMode.READ);
    const pixels = new Float32Array(imageReadback.getMappedRange().slice(0));
    let fragmentMaximumError = 0, fragmentDifferentWords = 0;
    for (let i = 0; i < count; ++i) for (let component = 0; component < 4; ++component) {
        const at = Math.floor(i / width) * rowBytes / 4 + (i % width) * 4 + component;
        const error = Math.abs(pixels[at] - pixels[imageBytes / 4 + at]);
        if (!Number.isFinite(error)) throw new Error(`nonfinite fragment at ${i}`);
        fragmentMaximumError = Math.max(fragmentMaximumError, error);
        if (error !== 0) ++fragmentDifferentWords;
    }
    imageReadback.unmap(); imageReadback.destroy(); images.forEach(image => image.destroy());
    if (fragmentMaximumError > 1e-6) throw new Error(`fragment differential failed: ${fragmentMaximumError}`);

    // Same-session AB/BA microbenchmark, never an engine or display-FPS gate.
    const timings = [];
    if (timestamps) {
        const benches = await pipelines('benchmark');
        const querySet = device.createQuerySet({type: 'timestamp', count: 4});
        const resolved = device.createBuffer({size: 32,
            usage: GPUBufferUsage.QUERY_RESOLVE | GPUBufferUsage.COPY_SRC});
        const timingReadback = device.createBuffer({size: 32,
            usage: GPUBufferUsage.COPY_DST | GPUBufferUsage.MAP_READ});
        for (let run = -1; run < 6; ++run) {
            encoder = device.createCommandEncoder();
            const order = run % 2 === 0 ? [0, 1] : [1, 0];
            for (const index of order) dispatch(encoder, benches[index], index, {
                timestampWrites: {querySet, beginningOfPassWriteIndex: index * 2,
                    endOfPassWriteIndex: index * 2 + 1},
            });
            encoder.resolveQuerySet(querySet, 0, 4, resolved, 0);
            encoder.copyBufferToBuffer(resolved, 0, timingReadback, 0, 32);
            device.queue.submit([encoder.finish()]);
            await timingReadback.mapAsync(GPUMapMode.READ);
            const times = new BigUint64Array(timingReadback.getMappedRange().slice(0));
            if (run >= 0) timings.push({reference_ms: Number(times[1] - times[0]) / 1e6,
                                      lut_ms: Number(times[3] - times[2]) / 1e6});
            timingReadback.unmap();
        }
        timingReadback.destroy(); resolved.destroy(); querySet.destroy();
    }
    await device.queue.onSubmittedWorkDone();
    const validation = await device.popErrorScope();
    if (validation) throw new Error(validation.message);
    if (errors.length) throw new Error(errors.join('\n'));
    const info = adapter.info;
    const report = {status: 'passed', adapter: {
        vendor: info.vendor, architecture: info.architecture,
        description: info.description, fallback: info.isFallbackAdapter,
    }, compiled_modules: 4, compiled_render_pipelines: 6, tested_points: count, compared_float_words: words,
       maximum_absolute_error: maximumError, different_float_words: differentWords,
       fragment_maximum_absolute_error: fragmentMaximumError,
       fragment_different_float_words: fragmentDifferentWords,
       microbenchmark: {kind: 'noise-only, not engine FPS', noise_calls_per_invocation: 12, timings}};
    device.destroy();
    return report;
}

const directory = await mkdtemp(path.join(tmpdir(), 'voxys-gradient-test-'));
const server = http.createServer((request, response) => {
    response.setHeader('Content-Type', 'text/html');
    response.end('<!doctype html><title>Voxys gradient correctness</title>');
});
await new Promise(resolve => server.listen(0, '127.0.0.1', resolve));
const chrome = spawn(process.env.VOXY_TEST_CHROME || 'google-chrome', [
    '--headless=new', '--no-sandbox', '--enable-unsafe-webgpu',
    '--enable-unsafe-swiftshader', '--use-angle=swiftshader',
    '--remote-debugging-port=0', `--user-data-dir=${directory}`, 'about:blank',
], {stdio: ['ignore', 'ignore', 'pipe']});
let chromeError = null, chromeLog = '';
chrome.on('error', error => {chromeError = error;});
chrome.stderr.on('data', data => {chromeLog = (chromeLog + data).slice(-10000);});
let socket;
const watchdog = setTimeout(() => {chrome.kill('SIGKILL');}, 180000);
try {
    let port;
    for (let i = 0; i < 200 && !port; ++i) {
        if (chromeError) throw chromeError;
        if (chrome.exitCode !== null) throw new Error(`Chrome exited: ${chromeLog}`);
        try {port = Number((await readFile(path.join(directory, 'DevToolsActivePort'), 'utf8')).split('\n')[0]);}
        catch {await new Promise(resolve => setTimeout(resolve, 50));}
    }
    if (!port) throw new Error(`Chrome debugging unavailable: ${chromeLog}`);
    const target = await (await fetch(`http://127.0.0.1:${port}/json/new?http://127.0.0.1:${server.address().port}/`, {method: 'PUT'})).json();
    socket = new WebSocket(target.webSocketDebuggerUrl);
    const waiting = new Map(); let sequence = 0;
    socket.addEventListener('message', event => {
        const message = JSON.parse(event.data);
        const call = waiting.get(message.id);
        if (call) {waiting.delete(message.id); message.error ? call.reject(new Error(JSON.stringify(message.error))) : call.resolve(message.result);}
    });
    socket.addEventListener('close', () => {
        for (const call of waiting.values()) call.reject(new Error('Chrome debugging closed'));
        waiting.clear();
    });
    await new Promise((resolve, reject) => {socket.addEventListener('open', resolve, {once: true}); socket.addEventListener('error', reject, {once: true});});
    const command = (method, params = {}) => new Promise((resolve, reject) => {
        const id = ++sequence; waiting.set(id, {resolve, reject});
        socket.send(JSON.stringify({id, method, params}));
    });
    await command('Runtime.enable');
    let ready = false;
    for (let i = 0; i < 100 && !ready; ++i) {
        const result = await command('Runtime.evaluate', {expression: 'isSecureContext && !!navigator.gpu', returnByValue: true});
        ready = result.result?.value === true;
        if (!ready) await new Promise(resolve => setTimeout(resolve, 50));
    }
    if (!ready) throw new Error('Secure WebGPU page unavailable (not a pass)');
    const result = await command('Runtime.evaluate', {
        expression: `(${gpuTest.toString()})(${JSON.stringify({bake, probe, ray, water})})`,
        awaitPromise: true, returnByValue: true,
    });
    if (result.exceptionDetails) throw new Error(JSON.stringify(result.exceptionDetails));
    const report = result.result.value;
    assert.equal(report.status, 'passed');
    await writeFile(path.resolve(process.env.VOXY_LUT_REPORT || 'periodic-gradient-lut-report.json'), JSON.stringify(report, null, 2) + '\n');
    console.log(JSON.stringify(report, null, 2));
} finally {
    clearTimeout(watchdog);
    socket?.close();
    // The profile is still being written until Chrome and its stdio close.
    // Waiting first avoids ENOTEMPTY after otherwise successful GPU tests.
    if (chrome.exitCode === null && chrome.signalCode === null && !chromeError) {
        const closed = new Promise(resolve => chrome.once('close', resolve));
        chrome.kill('SIGKILL');
        await closed;
    }
    await new Promise(resolve => server.close(resolve));
    await rm(directory, {recursive: true, force: true, maxRetries: 8, retryDelay: 100});
}
