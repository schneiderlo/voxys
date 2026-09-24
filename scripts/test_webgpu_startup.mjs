import assert from 'node:assert/strict';
import {test} from 'node:test';
import vm from 'node:vm';
import {readFile} from 'node:fs/promises';

const source = await readFile(new URL('../web/loader.js', import.meta.url), 'utf8');
function harness(onPoll = () => {}, stepMs = 50) {
    let now = 0;
    const context = vm.createContext({
        window: {}, console, Date: {now: () => now},
        setTimeout(resolve) {
            now += stepMs;
            onPoll(context, now);
            queueMicrotask(resolve);
        },
    });
    vm.runInContext(source, context);
    return {context, wait: context.window.VoxyLoader.waitForVoxyInitialization};
}

test('waits for cold shader compilation beyond the old two-minute deadline', async () => {
    let ready = false;
    const {wait} = harness((_, time) => { ready = time >= 180000; }, 30000);
    await wait({_voxy_is_initialized: () => Number(ready)});
    assert.equal(ready, true);
});

test('device loss overrides an engine that reports initialized', async () => {
    const {context, wait} = harness();
    context.voxyDeviceLost = {reason: 'unknown'};
    await assert.rejects(wait({_voxy_is_initialized: () => 1}), /graphics device stopped/);
});

test('device loss during compilation rejects the startup wait', async () => {
    const {wait} = harness(context => { context.voxyDeviceLost = {reason: 'unknown'}; });
    await assert.rejects(wait({_voxy_is_initialized: () => 0}), /graphics device stopped/);
});

test('failed initialization exits without waiting for the timeout', async () => {
    const module = {_voxy_is_initialized: () => 0};
    const {wait} = harness(() => { module.voxyInitializationFailed = true; });
    await assert.rejects(wait(module), /could not start/);
});

test('an engine that never finishes has a bounded startup wait', async () => {
    const {wait} = harness();
    await assert.rejects(wait({_voxy_is_initialized: () => 0}, {timeoutMs: 100}), /too long/);
});

test('main-route lighting defaults survive slow initialization and apply once', async () => {
    let ready = false;
    const calls = [];
    const {context, wait} = harness((_, time) => { ready = time >= 180000; }, 30000);
    context.location = {search: '?experience=lego-world'};
    context.URLSearchParams = URLSearchParams;
    const module = {
        _voxy_is_initialized: () => Number(ready),
        ccall(name, result, types, args) { calls.push([...args]); return 1; },
    };
    await wait(module);
    await wait(module);
    assert.deepEqual(calls, [
        ['lighting.sunIntensity', 1.7, 1],
        ['lighting.exposure', 1.15, 1],
    ]);
});
