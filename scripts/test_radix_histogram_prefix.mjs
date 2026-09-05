#!/usr/bin/env node
// Exact GPU oracle for radix histogram offsets and digit bases.
// Requires Node 22+, Chrome WebGPU and the pinned reference Git history.
import assert from 'node:assert/strict';
import { readFile, mkdtemp, rm, writeFile } from 'node:fs/promises';
import { tmpdir } from 'node:os';
import path from 'node:path';
import http from 'node:http';
import { spawn, execFileSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';
const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const reference = process.env.VOXY_RADIX_REFERENCE || '6701a85';
const payload = {
    before: execFileSync(
        'git',
        ['show', reference + ':shaders/physics_deterministic_primitives.wgsl'],
        { cwd: root, encoding: 'utf8' },
    ),
    after: await readFile(
        path.join(root, 'shaders/physics_deterministic_primitives.wgsl'),
        'utf8',
    ),
    reference,
};
async function gpuTest({ before, after, reference }) {
    const adapter = await navigator.gpu.requestAdapter();
    if (!adapter) throw Error('No WebGPU adapter');
    const device = await adapter.requestDevice();
    const B = GPUBufferUsage,
        S = GPUShaderStage;
    const errors = [];
    device.addEventListener('uncapturederror', (e) =>
        errors.push(e.error.message),
    );
    const report = {
        reference,
        kind: 'exact histogram-prefix GPU oracle',
        adapter: {
            vendor: adapter.info.vendor,
            architecture: adapter.info.architecture,
            description: adapter.info.description,
            fallback: adapter.info.isFallbackAdapter,
        },
        checks: [],
    };
    async function module(label, code) {
        const m = device.createShaderModule({ label, code });
        const info = await m.getCompilationInfo();
        const bad = info.messages.filter((x) => x.type === 'error');
        if (bad.length)
            throw Error(label + ': ' + bad.map((x) => x.message).join('; '));
        return m;
    }
    const [old, current] = await Promise.all([
        module('original', before),
        module('parallel', after),
    ]);
    const layout = device.createBindGroupLayout({
        entries: [
            { binding: 4, visibility: S.COMPUTE, buffer: { type: 'uniform' } },
            ...[20, 21, 22, 23, 24, 25].map((binding) => ({
                binding,
                visibility: S.COMPUTE,
                buffer: {
                    type: [20, 25].includes(binding)
                        ? 'read-only-storage'
                        : 'storage',
                },
            })),
        ],
    });
    const pl = device.createPipelineLayout({ bindGroupLayouts: [layout] });
    const pipe = (module, entryPoint) =>
        device.createComputePipelineAsync({
            layout: pl,
            compute: { module, entryPoint },
        });
    const [p0, p1, p2] = await Promise.all([
        pipe(old, 'radix_prefix'),
        pipe(current, 'radix_block_prefix'),
        pipe(current, 'radix_prefix'),
    ]);
    const maxBlocks = 4097,
        words = maxBlocks * 256,
        bytes = words * 4;
    const buffer = (size, usage) => device.createBuffer({ size, usage });
    const hist = buffer(bytes, B.STORAGE | B.COPY_DST),
        params = buffer(16, B.UNIFORM | B.COPY_DST),
        counts = buffer(16, B.STORAGE | B.COPY_DST);
    const dummyInput = buffer(16, B.STORAGE),
        dummyOutput = buffer(16, B.STORAGE);
    const offsets = [0, 1].map(() =>
        buffer(bytes, B.STORAGE | B.COPY_SRC | B.COPY_DST),
    );
    const bases = [0, 1].map(() =>
        buffer(1024, B.STORAGE | B.COPY_SRC | B.COPY_DST),
    );
    const reads = [0, 1].map(() =>
        buffer(bytes + 1024, B.MAP_READ | B.COPY_DST),
    );
    const groups = offsets.map((o, i) =>
        device.createBindGroup({
            layout,
            entries: [
                { binding: 4, resource: { buffer: params } },
                { binding: 20, resource: { buffer: dummyInput } },
                { binding: 21, resource: { buffer: dummyOutput } },
                { binding: 22, resource: { buffer: hist } },
                { binding: 23, resource: { buffer: o } },
                { binding: 24, resource: { buffer: bases[i] } },
                { binding: 25, resource: { buffer: counts } },
            ],
        }),
    );
    let rng = 0x51a71234;
    const random = () => {
        rng ^= rng << 13;
        rng ^= rng >>> 17;
        rng ^= rng << 5;
        return rng >>> 0;
    };
    const initial = new Uint32Array(words).fill(0xa55aa55a),
        baseInitial = new Uint32Array(256).fill(0xa55aa55a);
    for (const blocks of [0, 1, 31, 32, 33, 127, 128, 129, 255, 256, 257, 1024, 4097])
        for (const pattern of ['uniform', 'skew', 'random']) {
            const histogram = new Uint32Array(words);
            for (let b = 0; b < blocks; ++b) {
                if (pattern === 'uniform')
                    histogram.fill(1, b * 256, (b + 1) * 256);
                else if (pattern === 'skew')
                    histogram[b * 256 + (b % 2 ? 255 : 0)] = 256;
                else
                    for (let k = 0; k < 256; ++k)
                        histogram[b * 256 + (random() % 256)]++;
            }
            device.queue.writeBuffer(hist, 0, histogram);
            device.queue.writeBuffer(
                params,
                0,
                new Uint32Array([maxBlocks * 256, 2, 256, 0]),
            );
            device.queue.writeBuffer(
                counts,
                0,
                new Uint32Array([0, 0, blocks * 256, 0]),
            );
            for (let i = 0; i < 2; ++i) {
                device.queue.writeBuffer(offsets[i], 0, initial);
                device.queue.writeBuffer(bases[i], 0, baseInitial);
            }
            const expected = initial.slice(),
                expectedBases = new Uint32Array(256);
            let total = 0;
            for (let d = 0; d < 256; ++d) {
                let sum = 0;
                expectedBases[d] = total;
                for (let b = 0; b < blocks; ++b) {
                    const index = b * 256 + d;
                    expected[index] = sum;
                    sum += histogram[index];
                }
                total += sum;
            }
            const enc = device.createCommandEncoder(),
                pass = enc.beginComputePass();
            pass.setBindGroup(0, groups[0]);
            pass.setPipeline(p0);
            pass.dispatchWorkgroups(1);
            pass.setBindGroup(0, groups[1]);
            pass.setPipeline(p1);
            pass.dispatchWorkgroups(64);
            pass.setPipeline(p2);
            pass.dispatchWorkgroups(1);
            pass.end();
            for (let i = 0; i < 2; ++i) {
                enc.copyBufferToBuffer(offsets[i], 0, reads[i], 0, bytes);
                enc.copyBufferToBuffer(bases[i], 0, reads[i], bytes, 1024);
            }
            device.queue.submit([enc.finish()]);
            for (let i = 0; i < 2; ++i) {
                await reads[i].mapAsync(GPUMapMode.READ);
                const got = new Uint32Array(reads[i].getMappedRange());
                for (let j = 0; j < words; ++j)
                    if (got[j] !== expected[j])
                        throw Error(
                            `${pattern} blocks=${blocks} variant=${i} offset[${j}] ${got[j]} != ${expected[j]}`,
                        );
                for (let j = 0; j < 256; ++j)
                    if (got[words + j] !== expectedBases[j])
                        throw Error(
                            `${pattern} blocks=${blocks} variant=${i} base[${j}]`,
                        );
                reads[i].unmap();
            }
            report.checks.push({
                blocks,
                pattern,
                compared_words: 2 * (words + 256),
                different_words: 0,
            });
        }
    await device.queue.onSubmittedWorkDone();
    if (errors.length) throw Error(errors.join('; '));
    report.status = 'passed';
    device.destroy();
    return report;
}

const directory = await mkdtemp(path.join(tmpdir(), 'voxys-radix-test-'));
const server = http.createServer((request, response) => {
    response.setHeader('Content-Type', 'text/html');
    response.end(
        '<!doctype html><title>Voxys radix prefix correctness</title>',
    );
});
await new Promise((resolve) => server.listen(0, '127.0.0.1', resolve));
const chrome = spawn(
    process.env.VOXY_TEST_CHROME || 'google-chrome',
    [
        '--headless=new',
        '--no-sandbox',
        '--enable-unsafe-webgpu',
        '--no-first-run',
        '--no-default-browser-check',
        '--disable-background-networking',
        '--enable-unsafe-swiftshader',
        '--use-angle=swiftshader',
        '--remote-debugging-port=0',
        `--user-data-dir=${directory}`,
        'about:blank',
    ],
    { stdio: ['ignore', 'ignore', 'pipe'] },
);
let chromeError = null,
    chromeLog = '';
chrome.on('error', (error) => {
    chromeError = error;
});
chrome.stderr.on('data', (data) => {
    chromeLog = (chromeLog + data).slice(-10000);
});
let socket;
const watchdog = setTimeout(() => {
    chrome.kill('SIGKILL');
}, 180000);
try {
    let port;
    for (let i = 0; i < 1200 && !port; ++i) {
        if (chromeError) throw chromeError;
        if (chrome.exitCode !== null)
            throw new Error(`Chrome exited: ${chromeLog}`);
        try {
            port = Number(
                (
                    await readFile(
                        path.join(directory, 'DevToolsActivePort'),
                        'utf8',
                    )
                ).split('\n')[0],
            );
        } catch {
            await new Promise((resolve) => setTimeout(resolve, 50));
        }
    }
    if (!port) throw new Error(`Chrome debugging unavailable: ${chromeLog}`);
    const target = await (
        await fetch(
            `http://127.0.0.1:${port}/json/new?http://127.0.0.1:${server.address().port}/`,
            { method: 'PUT' },
        )
    ).json();
    socket = new WebSocket(target.webSocketDebuggerUrl);
    const waiting = new Map();
    let sequence = 0;
    socket.addEventListener('message', (event) => {
        const message = JSON.parse(event.data);
        const call = waiting.get(message.id);
        if (call) {
            waiting.delete(message.id);
            message.error
                ? call.reject(new Error(JSON.stringify(message.error)))
                : call.resolve(message.result);
        }
    });
    socket.addEventListener('close', () => {
        for (const call of waiting.values())
            call.reject(new Error('Chrome debugging closed'));
        waiting.clear();
    });
    await new Promise((resolve, reject) => {
        socket.addEventListener('open', resolve, { once: true });
        socket.addEventListener('error', reject, { once: true });
    });
    const command = (method, params = {}) =>
        new Promise((resolve, reject) => {
            const id = ++sequence;
            waiting.set(id, { resolve, reject });
            socket.send(JSON.stringify({ id, method, params }));
        });
    await command('Runtime.enable');
    let ready = false;
    for (let i = 0; i < 100 && !ready; ++i) {
        const result = await command('Runtime.evaluate', {
            expression: 'isSecureContext && !!navigator.gpu',
            returnByValue: true,
        });
        ready = result.result?.value === true;
        if (!ready) await new Promise((resolve) => setTimeout(resolve, 50));
    }
    if (!ready) throw new Error('Secure WebGPU page unavailable (not a pass)');
    const result = await command('Runtime.evaluate', {
        expression: `(${gpuTest.toString()})(${JSON.stringify(payload)})`,
        awaitPromise: true,
        returnByValue: true,
    });
    if (result.exceptionDetails)
        throw new Error(JSON.stringify(result.exceptionDetails));
    const report = result.result.value;
    assert.equal(report.status, 'passed');
    await writeFile(
        path.resolve(
            process.env.VOXY_RADIX_REPORT ||
                'radix-histogram-prefix-report.json',
        ),
        JSON.stringify(report, null, 2) + '\n',
    );
    console.log(JSON.stringify(report, null, 2));
} finally {
    clearTimeout(watchdog);
    socket?.close();
    // The profile is still being written until Chrome and its stdio close.
    // Waiting first avoids ENOTEMPTY after otherwise successful GPU tests.
    if (
        chrome.exitCode === null &&
        chrome.signalCode === null &&
        !chromeError
    ) {
        const closed = new Promise((resolve) => chrome.once('close', resolve));
        chrome.kill('SIGKILL');
        await closed;
    }
    await new Promise((resolve) => server.close(resolve));
    await rm(directory, {
        recursive: true,
        force: true,
        maxRetries: 8,
        retryDelay: 100,
    });
}
