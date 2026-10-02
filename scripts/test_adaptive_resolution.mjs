import {test} from 'node:test';
import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
import vm from 'node:vm';
await import('../web/adaptive_resolution.js');
const {Controller, preference, install} = globalThis.VoxyAdaptiveResolution;
const storage = settings => ({getItem: () => JSON.stringify({settings})});
const packet = (frame, ms, refreshes = frame, extras = {}) => ({
    frame: {count: frame + 32}, render: {terrain_cache_refreshes: refreshes},
    render_gpu: {available: true, frame_interval_available: true, frame,
        gpu_frame_ms: ms, render_width: 3440, render_height: 1440, ...extras},
});
function trace(controller, costs, {start = 2000, stationary = false, dimensions = [3440,1440]} = {}) {
    let frame = Math.max(0, controller.lastFrame), now = start;
    const changes = [];
    for (const ms of costs) {
        frame += 30;
        const change = controller.observe(packet(frame, ms, stationary ? 0 : frame), ...dimensions, now);
        if (change) changes.push({...change, at: now});
        now += 500;
    }
    return changes;
}
test('new gameplay defaults to adaptation; legacy fixed quality, explicit mode, benchmarks and other scenes are respected', () => {
    assert.equal(preference(null), true);
    assert.equal(preference(storage({'browser.resolutionScale': .76})), false);
    assert.equal(preference(storage({'browser.resolutionScale': .76, 'browser.adaptiveResolution': 1})), true);
    assert.equal(preference(storage({'browser.adaptiveResolution': 0})), false);
    assert.equal(preference(null, '?experience=salvage'), false);
    assert.equal(preference(null, '?adaptiveResolution=0'), false);
    assert.equal(preference(null, '?adaptiveResolution=1&renderThroughput=1'), false);
    assert.equal(preference(null, '?adaptiveResolution=1&browserBenchmarkRun=0'), false);
    assert.equal(preference({getItem(){throw Error('denied');}}), true);
});
test('one hitch, alternating overload and repeated asynchronous packets do not cause resizes', () => {
    const controller = new Controller();
    assert.deepEqual(trace(controller, [8,8,100,8,8,8,16,8,16,8,16]), []);
    const repeated = packet(1000, 20);
    for (let now = 10000; now < 20000; now += 250)
        assert.equal(controller.observe(repeated, 3440,1440,now), null);
    assert.equal(controller.resizes, 0);
});
test('sustained moving GPU overload converges with bounded scale and at least 3 seconds between changes', () => {
    const controller = new Controller({ceiling: 1.014});
    const changes = trace(controller, Array(70).fill(25));
    assert.ok(changes.length > 0 && changes.length <= 4);
    assert.equal(controller.scale, controller.floor);
    assert.ok(controller.scale >= .35);
    assert.ok(changes.every((change, i) => !i || change.at - changes[i-1].at >= 3000));
    assert.ok(changes[0].scale >= 1.014 * .8);
});
test('GPU work at a smaller viewport stays sharp; CPU/wall time alone never reduces quality', () => {
    const controller = new Controller();
    assert.deepEqual(trace(controller, Array(100).fill(8)), []);
    const snapshot = packet(10000, 8);
    snapshot.frame.cpu_ms = 60; snapshot.frame.wall_ms = 80;
    assert.equal(controller.observe(snapshot,3440,1440,60000),null);
    assert.equal(controller.scale,1);
});
test('rejects stale, missing, invalid and old-size packets without counting them as fresh evidence', () => {
    for (const extras of [{available:false}, {frame_interval_available:false}, {gpu_frame_ms:0},
        {gpu_frame_ms:NaN}, {frame:0}, {frame:9999}, {render_width:2578}, {render_height:1079}]) {
        const controller = new Controller();
        trace(controller, [18,18]);
        assert.equal(controller.observe(packet(120,18,120,extras),3440,1440,3000),null);
        assert.equal(controller.resizes,0);
    }
    const controller = new Controller();
    const stale = packet(30,20); stale.frame.count = 500;
    assert.equal(controller.observe(stale,3440,1440,4000),null);
});
test('stationary recovery restores the selected ceiling, with no repeated movement/idle oscillation', () => {
    const controller = new Controller({ceiling: 1.014});
    trace(controller, [16,16,16,16]);
    const reduced = controller.scale;
    assert.ok(reduced < controller.ceiling);
    // Brief pauses and alternating cached/uncached frames do not recover.
    assert.deepEqual(trace(controller, Array(30).fill(6), {start:4000}), []);
    assert.equal(controller.scale,reduced);
    const changes = trace(controller, Array(100).fill(6), {start:20000, stationary:true});
    assert.ok(changes.length <= 4);
    assert.equal(controller.scale,controller.ceiling);
});
test('explicit user changes reset history and fixed mode restores its selected quality immediately', () => {
    const controller = new Controller();
    trace(controller, Array(10).fill(20));
    controller.configure(.76,false,8000);
    assert.equal(controller.scale,.76);
    assert.deepEqual(trace(controller, Array(40).fill(30), {start:10000}), []);
    controller.configure(.5,true,30000);
    trace(controller,Array(100).fill(30),{start:32000});
    assert.equal(controller.scale,.35);
});
test('host skips hidden/uninitialized frames, handles malformed telemetry and cleans up its timer', () => {
    let callback, cleared, reads=0, now=0, initialized=0, json='invalid';
    const environment = {performance:{now:()=>now}, document:{hidden:false}, location:{search:''},
        setInterval(fn,ms){callback=fn; assert.equal(ms,250);return 7;}, clearInterval(id){cleared=id;}};
    const module = {_voxy_is_initialized:()=>initialized, _voxy_get_telemetry_json(){reads++;return 1;},UTF8ToString:()=>json};
    const changes=[], canvas={width:3440,height:1440};
    const host = install({module,canvas,ceiling:1,enabled:true,environment,
        applyScale:scale=>{changes.push(scale);canvas.width=Math.floor(3440*scale);canvas.height=Math.floor(1440*scale);}});
    callback();assert.equal(reads,0);
    initialized=1;callback(); assert.equal(reads,1);assert.deepEqual(changes,[]);
    environment.document.hidden=true;callback();assert.equal(reads,1);
    environment.document.hidden=false;
    for (let i=0;i<10;i++) {now=2000+i*500;json=JSON.stringify(packet(i*30,16*(canvas.width/3440)**2,i*30,{render_width:canvas.width,render_height:canvas.height}));callback();}
    assert.equal(changes.length,1);
    host.configure(.76,false);assert.equal(changes.at(-1),.76);
    const before=reads;callback();assert.equal(reads,before);
    host.dispose();assert.equal(cleared,7);callback();assert.equal(reads,before);
});
test('host enforces fixed benchmark resolution even when UI explicitly requests adaptation', () => {
    const environment={performance:{now:()=>0},location:{search:'?browserBenchmarkRun=1'},setInterval:()=>1,clearInterval(){}};
    const host=install({environment,module:{},canvas:{},ceiling:1,enabled:true,applyScale(){}});
    host.configure(1,true);assert.equal(host.controller.enabled,false);host.dispose();
});
test('browser integration loads the controller before preferences and keeps all inline scripts parseable', () => {
    const html=readFileSync(new URL('../web/index.html',import.meta.url),'utf8');
    assert.ok(html.indexOf('src="adaptive_resolution.js') < html.indexOf('src="renderer_inspector.js'));
    assert.match(html,/onAdaptiveResolution\(enabled, commit\)/);
    assert.match(html,/adaptiveResolution\?\.resized\(\)/);
    for (const match of html.matchAll(/<script(?:\s[^>]*)?>([\s\S]*?)<\/script>/g)) new vm.Script(match[1]);
});

// DOM fixture verifies the real preference UI bridge without a graphics device.
test('inspector startup and user toggle preserve fixed legacy quality and honor URL mode', async () => {
    const {JSDOM} = await import('../ui/node_modules/jsdom/lib/api.js');
    const source=readFileSync(new URL('../web/renderer_inspector.js',import.meta.url),'utf8');
    for (const [search, expected] of [['',0],['?adaptiveResolution=1',1],['?browserBenchmarkRun=1&adaptiveResolution=1',0]]) {
        const dom=new JSDOM('<body><canvas width="1920" height="1080"></canvas></body>',
            {url:'http://localhost/'+search,runScripts:'outside-only'});
        const {window}=dom;
        window.localStorage.setItem('voxy.renderer-inspector.v1',JSON.stringify({settings:{'browser.resolutionScale':.76,'browser.adaptiveResolution':0}}));
        window.VoxyAdaptiveResolution=globalThis.VoxyAdaptiveResolution;
        window.setTimeout=()=>1; // Disable UI persistence/poll timers in this fixture.
        window.eval(source);
        const changes=[], scales=[];
        const inspector=window.VoxyRendererInspector.mount({
            canvas:window.document.querySelector('canvas'),
            module:{ccall:()=>1},onRenderScale:(scale,commit)=>{if(commit)scales.push(scale);},
            onAdaptiveResolution:(value,commit)=>{if(commit)changes.push(value);},
        });
        assert.equal(inspector.values.get('browser.adaptiveResolution'),expected);
        assert.deepEqual(changes,[]); // Unchanged startup defaults do not resize.
        assert.deepEqual(scales,[]);
        const toggle=window.document.querySelector('[aria-label="Smooth movement"]');
        toggle.click();assert.equal(changes.at(-1),1-expected);
        assert.equal(inspector.values.get('browser.resolutionScale'),.76);
        assert.deepEqual(scales,[]);
        dom.window.close();
    }
});
