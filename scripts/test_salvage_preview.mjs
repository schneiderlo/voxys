import assert from 'node:assert/strict';
import { createRequire } from 'node:module';
const require = createRequire(import.meta.url);
const { install } = require('../web/salvage_preview.js');
function fixture() {
    class Element {
        hidden = true; disabled = false; textContent = ''; listeners = new Map();
        addEventListener(name, callback) { this.listeners.set(name, callback); }
        removeEventListener(name, callback) { if (this.listeners.get(name) === callback) this.listeners.delete(name); }
        click() { this.listeners.get('click')?.(); }
    }
    const elements = Object.fromEntries(['salvage-preview', 'salvage-status', 'salvage-reset', 'salvage-leave'].map(id => [id, new Element()]));
    let state = { active: true, ready: true, busy: false, failed: false, resets: 0 };
    let tick, pagehide, cleared = 0;
    const actions = [], navigations = [];
    const environment = {
        document: { getElementById: id => elements[id] },
        location: { assign: url => navigations.push(url) },
        setInterval: callback => { tick = callback; return 17; },
        clearInterval: id => { assert.equal(id, 17); ++cleared; },
        addEventListener: (name, callback) => { assert.equal(name, 'pagehide'); pagehide = callback; },
        removeEventListener: (name, callback) => { assert.equal(callback, pagehide); pagehide = undefined; },
    };
    const engine = {
        _voxy_is_initialized: () => 1,
        _voxy_salvage_preview_action: action => { actions.push(action); return 1; },
        _voxy_get_salvage_preview_json: () => JSON.stringify(state), UTF8ToString: s => s,
    };
    const cleanup = install(engine, environment);
    return { elements, actions, navigations, engine, cleanup, tick: () => tick(),
        state: update => Object.assign(state, update), pagehide: () => pagehide?.(), cleared: () => cleared };
}
{
    const f = fixture();
    f.elements['salvage-reset'].click(); f.elements['salvage-reset'].click();
    assert.deepEqual(f.actions, [1]);
    f.tick(); // An old Ready snapshot is not reset completion.
    assert.equal(f.elements['salvage-reset'].disabled, true);
    f.state({ ready: false, busy: true }); f.tick();
    f.state({ ready: true, busy: false, resets: 1 }); f.tick();
    assert.equal(f.elements['salvage-reset'].disabled, false);
    f.cleanup(); f.cleanup();
    assert.equal(f.cleared(), 1);
    assert.equal(f.elements['salvage-reset'].listeners.size, 0);
}
{
    const f = fixture();
    f.elements['salvage-reset'].click(); f.elements['salvage-leave'].click();
    f.elements['salvage-leave'].click(); f.tick();
    assert.deepEqual(f.actions, [1, 2]); assert.deepEqual(f.navigations, []);
    f.state({ active: false, ready: false }); f.tick();
    assert.deepEqual(f.navigations, ['?experience=lego-world']);
    assert.equal(f.cleared(), 1); assert.equal(f.elements['salvage-preview'].hidden, true);
    f.tick(); assert.equal(f.navigations.length, 1);
}
{
    const f = fixture();
    f.pagehide(); f.elements['salvage-reset'].click(); f.tick();
    assert.deepEqual(f.actions, []); assert.equal(f.cleared(), 1);
}
{
    const f = fixture();
    f.elements['salvage-leave'].click(); f.state({ failed: true, active: false }); f.tick();
    assert.deepEqual(f.navigations, []);
    assert.match(f.elements['salvage-status'].textContent, /stopped/);
    assert.equal(f.elements['salvage-reset'].disabled, true);
    f.cleanup();
}
console.log('salvage preview UI lifecycle tests: 4 passed');
