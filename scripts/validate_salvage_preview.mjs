// Visible application journey, called by smoke_integrated_wasm. These are
// behavioral checks and screenshots, not timing or performance measurements.
import assert from 'node:assert/strict';
import { mkdir, writeFile } from 'node:fs/promises';

export async function validateSalvagePreview(call, directory) {
    await mkdir(directory, { recursive: true });
    const evaluate = async expression => {
        const result = await call('Runtime.evaluate', { expression, returnByValue: true, awaitPromise: true });
        if (result.exceptionDetails) throw Error(JSON.stringify(result.exceptionDetails));
        return result.result?.value;
    };
    const delay = milliseconds => new Promise(resolve => setTimeout(resolve, milliseconds));
    const waitFor = async (expression, label, timeout = 30000) => {
        const deadline = Date.now() + timeout;
        let last, lastError;
        do {
            try { last = await evaluate(expression); if (last) return last; }
            catch (error) { lastError = String(error); } // Navigation destroys its old execution context.
            await delay(100);
        } while (Date.now() < deadline);
        throw Error(`${label} timed out; last=${JSON.stringify(last)} error=${lastError || 'none'}`);
    };
    const stateExpression = 'JSON.parse(voxyModule.UTF8ToString(voxyModule._voxy_get_salvage_preview_json()))';
    const read = () => evaluate(`(() => {
        const state = ${stateExpression};
        const lego = JSON.parse(voxyModule.UTF8ToString(voxyModule._voxy_get_lego_hud_json()));
        return {...state, resident: voxyModule._voxy_get_physics_resident_bodies(), grouped: lego.grouped,
            playground: lego.active, pointerLocked: document.pointerLockElement === document.getElementById('voxy-canvas'),
            lost: globalThis.voxyDeviceLost || null, errors: globalThis.voxyUncapturedGpuErrors || []};
    })()`);
    const healthy = state => {
        assert.equal(state.failed, false, 'preview failed');
        assert.equal(state.lost, null, 'WebGPU device lost');
        assert.deepEqual(state.errors, [], 'GPU validation errors');
    };
    const absolute = camera => camera.local.map((value, axis) => value + camera.sector[axis] * 256);
    const separation = (a, b) => Math.hypot(...a.map((value, axis) => value - b[axis]));
    const sendKey = (code, down) => {
        const functionKey = code >= 112 && code <= 123 ? `F${code - 111}` : null;
        return call('Input.dispatchKeyEvent', { type: down ? 'keyDown' : 'keyUp',
            key: functionKey || String.fromCharCode(code).toLowerCase(),
            code: functionKey || `Key${String.fromCharCode(code)}`,
            windowsVirtualKeyCode: code, nativeVirtualKeyCode: code });
    };
    const key = async code => {
        await sendKey(code, true);
        try { await delay(100); } finally { await sendKey(code, false); }
    };
    const click = async selector => {
        // C++ can be ready before the main page reveals its control panel.
        // A visible child must also have layout and win the actual hit test;
        // otherwise a hidden ancestor gives us a spurious click at (0, 0).
        const point = await waitFor(`(() => {
            const element = document.querySelector(${JSON.stringify(selector)});
            if (!element || element.hidden || element.disabled) return null;
            const r = element.getBoundingClientRect();
            if (r.width <= 0 || r.height <= 0) return null;
            const point = {x: r.left + r.width / 2, y: r.top + r.height / 2};
            const hit = document.elementFromPoint(point.x, point.y);
            return hit && (hit === element || element.contains(hit)) ? point : null;
        })()`, `visible and hittable ${selector}`);
        await call('Input.dispatchMouseEvent', { type: 'mouseMoved', ...point });
        await call('Input.dispatchMouseEvent', { type: 'mousePressed', ...point, button: 'left', clickCount: 1 });
        await call('Input.dispatchMouseEvent', { type: 'mouseReleased', ...point, button: 'left', clickCount: 1 });
    };
    const capture = async name => {
        const result = await call('Page.captureScreenshot', { format: 'png' });
        await writeFile(`${directory}/${name}.png`, Buffer.from(result.data, 'base64'));
    };
    const report = { note: 'Real browser controls and completed application state; no performance claim.', stages: [] };
    const record = async name => {
        const state = await read(); healthy(state);
        report.stages.push({ name, state }); return state;
    };
    try {
        await waitFor(`typeof voxyModule !== 'undefined' && voxyModule?._voxy_is_initialized?.() === 1 && (${stateExpression}).ready`, 'cove startup');
        await delay(700); // Let the character reach its first supported pose.
        const initial = await record('initial');
        report.device = await evaluate('window.voxyDeviceProfile');
        report.url = await evaluate('location.href');
        assert.equal(initial.bodies, 32);
        assert.equal(initial.resets, 0);
        assert.equal(initial.controller, 'character');
        assert.equal(initial.playground, false);
        assert.equal(initial.grouped, true);
        assert(Array.isArray(initial.origin) && initial.origin.length === 3 && initial.origin.every(Number.isFinite));
        await capture('01-cove');

        await sendKey(87, true);
        try {
            await waitFor(`(() => {const p=${stateExpression}.camera;
                const start=${JSON.stringify(absolute(initial.camera))};
                return Math.hypot(...p.local.map((v,i)=>v+p.sector[i]*256-start[i])) > .35;})()`, 'walking changes camera', 7000);
        } finally { await sendKey(87, false); }
        const moved = await record('walked');
        assert(separation(absolute(initial.camera), absolute(moved.camera)) > .3);
        await capture('02-walked');

        const assertResetView = state => {
            healthy(state); assert.equal(state.bodies, 32);
            assert.equal(state.resident, initial.resident, 'reset leaked world bodies');
            assert.equal(state.controller, 'character');
            assert(separation(absolute(state.camera), absolute(initial.camera)) < .15, 'reset did not restore authored camera');
            assert(Math.abs(state.camera.yaw - initial.camera.yaw) < .005);
            assert(Math.abs(state.camera.pitch - initial.camera.pitch) < .005);
            assert.equal(state.mouseCaptured, false); assert.equal(state.pointerLocked, false);
        };
        await click('#salvage-reset');
        await waitFor(`(${stateExpression}).ready && (${stateExpression}).resets === 1`, 'Reset button acknowledgement');
        await delay(500);
        const reset = await record('reset-button'); assertResetView(reset);
        await capture('03-reset');

        // Exercise actual pointer lock where the browser grants the gesture.
        await click('#voxy-canvas'); await delay(150);
        report.pointerLockGranted = (await read()).pointerLocked;
        await sendKey(87, true); await delay(300);
        // R while W remains held also checks reset discards held movement input.
        try { await key(82); } finally { await sendKey(87, false); }
        await waitFor(`(${stateExpression}).ready && (${stateExpression}).resets === 2`, 'R key acknowledgement');
        await delay(700);
        const keyboardReset = await record('reset-keyboard'); assertResetView(keyboardReset);

        // Old playground, throwable, presentation and benchmark controls must
        // neither spawn bodies nor steal the preview's character camera.
        for (const code of [80, 66, 75, 118, 119]) await key(code); // P B K F7 F8
        const legacyActions = await evaluate('[0,1,2,3,8].map(action => voxyModule._voxy_lego_action(action))');
        assert.deepEqual(legacyActions, [0, 0, 0, 0, 0]);
        await delay(500);
        const isolated = await record('legacy-controls-isolated');
        assertResetView(isolated); assert.equal(isolated.resets, 2);
        assert.equal(isolated.playground, false); assert.equal(isolated.grouped, true);

        // Persist only observed pre-navigation state for this same-origin test.
        // The real UI still decides when to navigate after C++ acknowledges Leave.
        const exitKey = `voxy.preview.exit.${Date.now()}`;
        await evaluate(`(() => {
            const key=${JSON.stringify(exitKey)};
            sessionStorage.removeItem(key);
            addEventListener('pagehide', () => {
                const state=${stateExpression};
                sessionStorage.setItem(key, JSON.stringify({state,
                    resident:voxyModule._voxy_get_physics_resident_bodies(),
                    panelHidden:document.getElementById('salvage-preview').hidden,
                    pointerLocked:Boolean(document.pointerLockElement)}));
            }, {once:true});
        })()`);
        await click('#salvage-leave');
        await waitFor(`new URLSearchParams(location.search).get('experience') === 'lego-world'
            && typeof voxyModule !== 'undefined' && voxyModule?._voxy_is_initialized?.() === 1
            && !document.getElementById('lego-shore-controls').hidden`, 'Leave navigates to the legacy world', 90000);
        const exit = await evaluate(`JSON.parse(sessionStorage.getItem(${JSON.stringify(exitKey)}))`);
        assert(exit, 'missing observed Leave acknowledgement');
        assert.equal(exit.state.active, false); assert.equal(exit.state.bodies, 0);
        assert.equal(exit.state.failed, false); assert.equal(exit.state.mouseCaptured, false);
        assert.equal(exit.resident, initial.resident - 32, 'Leave removed an unowned body or retained fixtures');
        assert.equal(exit.panelHidden, true); assert.equal(exit.pointerLocked, false);
        report.exit = exit;
        await evaluate(`sessionStorage.removeItem(${JSON.stringify(exitKey)})`);
        const legacy = await record('legacy-world-after-leave');
        assert.equal(legacy.active, false); assert.equal(legacy.playground, false);
        assert.equal(await evaluate('document.getElementById("salvage-preview").hidden'), true);
        await capture('04-legacy-world');

        await call('Page.navigate', { url: report.url });
        await waitFor(`new URLSearchParams(location.search).get('experience') === 'salvage'
            && typeof voxyModule !== 'undefined' && voxyModule?._voxy_is_initialized?.() === 1
            && (${stateExpression}).ready`, 'fresh cove re-entry', 90000);
        await delay(700);
        const reentered = await record('reentered');
        assert.equal(reentered.resets, 0); assertResetView(reentered);
        await click('#salvage-reset');
        await waitFor(`(${stateExpression}).ready && (${stateExpression}).resets === 1`, 'single action after re-entry');
        await delay(300);
        assert.equal((await record('single-reset-after-reentry')).resets, 1);
        await capture('05-reentered');

        // This separate page is a terrain study, not the expedition engine.
        const studyUrl = new URL('lego_patch.html?test', report.url).href;
        await call('Page.navigate', { url: studyUrl });
        await waitFor('globalThis.legoPatchTest?.vertexCount > 0 && document.getElementById("error").hidden', 'standalone terrain study', 30000);
        await evaluate('new Promise(resolve => requestAnimationFrame(() => requestAnimationFrame(resolve))).then(() => legoPatchTest.device.queue.onSubmittedWorkDone())');
        const groupedBricks = await evaluate('legoPatchTest.model.bricks.length');
        await capture('06-standalone-study');
        await click('#single');
        const singleBricks = await evaluate('legoPatchTest.model.bricks.length');
        assert(singleBricks > groupedBricks);
        await click('#grouped');
        assert.equal(await evaluate('legoPatchTest.model.bricks.length'), groupedBricks);
        await click('#drop');
        assert.equal(await evaluate('legoPatchTest.balls.length'), 1);
        await click('#reset');
        assert.equal(await evaluate('legoPatchTest.balls.length'), 0);
        assert.equal(await evaluate('document.getElementById("error").hidden'), true);
        assert.deepEqual(await evaluate('globalThis.voxyUncapturedGpuErrors || []'), []);
        assert.equal(await evaluate('Boolean(globalThis.voxyDeviceLost)'), false);
        report.standaloneStudy = { url: studyUrl, groupedBricks, singleBricks,
            dropAndReset: 'passed', note: 'Actual separately loaded terrain study, no expedition gameplay claim.' };
        report.status = 'passed';
        await writeFile(`${directory}/summary.json`, JSON.stringify(report, null, 2));
        return report;
    } catch (error) {
        report.status = 'failed'; report.error = String(error);
        try { report.final = await read(); } catch { /* the standalone page has no engine module */ }
        try { await capture('failure'); } catch { /* navigation may be incomplete */ }
        await writeFile(`${directory}/summary.json`, JSON.stringify(report, null, 2));
        throw error;
    }
}
