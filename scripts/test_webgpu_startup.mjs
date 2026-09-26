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

const pageSource = await readFile(new URL('../web/index.html', import.meta.url), 'utf8');
const failureViewSource = pageSource.slice(pageSource.indexOf('function showInitializationFailure('), pageSource.indexOf('let roller = null;'));
function failureView(message, canStartExpedition, href) {
    const fields = new Map(['h2','p','#error-actions','#error-retry','#error-save-note','#error-new-expedition']
        .map(key => [key, {hidden: true, textContent: '', href: ''}]));
    const help = [{hidden: false}, {hidden: false}];
    const error = {style: {}, querySelector: key => fields.get(key), querySelectorAll: () => help};
    const context = vm.createContext({URL});
    vm.runInContext(failureViewSource, context);
    context.showInitializationFailure(error, message, canStartExpedition, href);
    return {error, fields, help};
}

test('Frontier save failure offers a separate expedition without replacing the bookmarked world', () => {
    const href = 'https://example.test/game/index.html?experience=frontier&world=saved-world&debug=1#view';
    const {error, fields, help} = failureView('The saved world is incompatible.', true, href);
    assert.equal(error.style.display, 'flex');
    assert.equal(fields.get('h2').textContent, 'Expedition could not load');
    assert.equal(fields.get('p').textContent, 'The saved world is incompatible.');
    assert.equal(fields.get('#error-retry').href, href, 'Retry retains the exact original world URL');
    assert.equal(fields.get('#error-actions').hidden, false);
    assert.equal(fields.get('#error-save-note').hidden, false);
    assert.equal(fields.get('#error-new-expedition').hidden, false);
    const fresh = new URL(fields.get('#error-new-expedition').href);
    assert.equal(fresh.origin, 'https://example.test');
    assert.equal(fresh.pathname, '/game/index.html');
    assert.equal(fresh.searchParams.get('experience'), 'frontier');
    assert.equal(fresh.searchParams.get('world'), null);
    assert.equal(fresh.searchParams.get('new'), '1');
    assert.equal(fresh.searchParams.get('debug'), '1');
    assert.equal(fresh.hash, '#view');
    assert(help.every(item => item.hidden), 'Save failures do not show unrelated browser support instructions');
});

test('default Frontier bookmarks can start fresh while other startup failures only offer Retry', () => {
    const href = 'https://example.test/index.html?world=old-world';
    const frontier = failureView('The world could not start.', true, href);
    const fresh = new URL(frontier.fields.get('#error-new-expedition').href);
    assert.equal(fresh.searchParams.get('new'), '1');
    assert.equal(fresh.searchParams.has('world'), false);
    assert.equal(fresh.searchParams.has('experience'), false, 'The default profile remains unchanged');
    const graphics = failureView('No graphics device.', false, href);
    assert.equal(graphics.fields.get('h2').textContent, 'Initialization Failed');
    assert.equal(graphics.fields.get('#error-retry').href, href);
    assert.equal(graphics.fields.get('#error-new-expedition').hidden, true);
    assert.equal(graphics.fields.get('#error-save-note').hidden, true);
    const starting = failureView('The renderer could not start.', true, 'https://example.test/index.html?experience=frontier&new=1');
    assert.equal(starting.fields.get('h2').textContent, 'Initialization Failed');
    assert.equal(starting.fields.get('#error-new-expedition').hidden, true, 'A fresh-start failure does not offer the same fresh-start URL');
    assert.equal(starting.fields.get('#error-save-note').hidden, true);
    assert.equal(starting.fields.get('#error-retry').href, 'https://example.test/index.html?experience=frontier&new=1');
});
