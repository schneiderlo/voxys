// Real application inspection controls, GPU completion, resize and retirement.
// This is functional evidence; captures require a separate visual review.
import assert from 'node:assert/strict';
import {mkdir, writeFile} from 'node:fs/promises';

export async function validateSalvageAssetFixture(call, directory) {
    await mkdir(directory, {recursive: true});
    const delay = ms => new Promise(resolve => setTimeout(resolve, ms));
    const expression = 'JSON.parse(voxyModule.UTF8ToString(voxyModule._voxy_get_salvage_preview_json()))';
    const evaluate = async expression => {
        const value = await call('Runtime.evaluate', {expression, returnByValue: true, awaitPromise: true});
        if (value.exceptionDetails) throw Error(JSON.stringify(value.exceptionDetails));
        return value.result?.value;
    };
    const wait = async (expression, label, timeout = 30000) => {
        const deadline = Date.now() + timeout;
        let last, error;
        do {
            try { last = await evaluate(expression); if (last) return last; }
            catch (caught) { error = String(caught); }
            await delay(100);
        } while (Date.now() < deadline);
        throw Error(`${label}: last=${JSON.stringify(last)}, error=${error || 'none'}`);
    };
    const read = () => evaluate(`({...${expression},
        resident:voxyModule._voxy_get_physics_resident_bodies(),
        lost:globalThis.voxyDeviceLost || null, errors:globalThis.voxyUncapturedGpuErrors || [],
        render:JSON.parse(voxyModule.UTF8ToString(voxyModule._voxy_get_telemetry_json())).render_gpu})`);
    const report = {kind: 'Real browser inspection journey; no art approval or performance claim', stages: []};
    let cove = false;
    const capture = async name => {
        if (cove) return; // D19: controls use state/queue evidence, not a screenshot matrix.
        const image = await call('Page.captureScreenshot', {format: 'png'});
        await writeFile(`${directory}/${name}.png`, Buffer.from(image.data, 'base64'));
    };
    const record = async name => {
        const state = await read();
        assert.equal(state.failed, false); assert.equal(state.lost, null); assert.deepEqual(state.errors, []);
        assert.deepEqual(state.session, {revision: '0', tick: '0', admissionOpen: state.active,
            builds: 0, cargo: 0, jobs: 0, inventory: {salvageMaterial: '0', specialMachinery: '0'}});
        report.stages.push({name, state});
        return state;
    };
    const click = async selector => {
        const point = await wait(`(() => {
            const element=document.querySelector(${JSON.stringify(selector)});
            if(!element || element.disabled || element.hidden) return null;
            const r=element.getBoundingClientRect(); if(!r.width || !r.height) return null;
            const p={x:r.left+r.width/2,y:r.top+r.height/2}, hit=document.elementFromPoint(p.x,p.y);
            return hit && (hit===element || element.contains(hit)) ? p : null;
        })()`, `hittable ${selector}`);
        await call('Input.dispatchMouseEvent', {type: 'mouseMoved', ...point});
        await call('Input.dispatchMouseEvent', {type: 'mousePressed', ...point, button: 'left', clickCount: 1});
        await call('Input.dispatchMouseEvent', {type: 'mouseReleased', ...point, button: 'left', clickCount: 1});
    };
    const sendKey = (letter, down) => call('Input.dispatchKeyEvent', {
        type: down ? 'keyDown' : 'keyUp', key: letter.toLowerCase(), code: `Key${letter}`,
        windowsVirtualKeyCode: letter.charCodeAt(0), nativeVirtualKeyCode: letter.charCodeAt(0),
    });
    const ready = `typeof voxyModule !== 'undefined' && voxyModule?._voxy_is_initialized?.() === 1
        && (${expression}).ready && (${expression}).assetFixture?.status === 2
        && BigInt((${expression}).assetFixture.completedSerial)>0n`;
    const absolute = camera => camera.local.map((v, i) => v + camera.sector[i] * 256);
    const distance = (a, b) => Math.hypot(...a.map((v, i) => v - b[i]));
    try {
        await wait(ready, 'authored asset startup');
        report.url = await evaluate('location.href');
        report.device = await evaluate('window.voxyDeviceProfile');
        const initial = await record('initial');
        const experience=new URL(report.url).searchParams.get('experience');
        cove = experience === 'salvage-cove';
        const assembly=experience==='salvage-assembly',hierarchy=experience==='salvage-hierarchy';
        const rotations=experience==='salvage-rotations-a'||experience==='salvage-rotations-b';
        const skiff=experience==='salvage-kit-broad'||experience==='salvage-kit-narrow';
        const cargo=experience==='salvage-kit-cargo';
        const materials=experience==='salvage-materials';
        const metric=experience==='salvage-material-detail';
        const expectedDraws=cove?36:metric?13:materials?2:skiff?11:rotations?24:hierarchy?10:assembly?6:4;
        const expectedSockets=metric?135:materials?120:skiff?510:cargo?120:hierarchy?375:assembly?150:360;
        const expectedUploads=cove?27:metric?9:materials?6:skiff?24:cargo?12:hierarchy?4:3;
        const availableLods=hierarchy?['0','1']:['0','1','2','3'];
        assert.deepEqual(initial.assetFixture.availableLods,availableLods);
        if(hierarchy){
            assert.equal(initial.assetFixture.lods.length,5);
            for(const id of ['2','3']){
                await wait(`document.querySelector('[data-salvage-lod="${id}"]').disabled`,'unavailable detail disabled');
                assert.equal(await evaluate(`voxyModule._voxy_salvage_preview_action(${10+Number(id)})`),0);
            }
            assert.equal((await read()).assetFixture.forcedLod,'0','refused detail must preserve auto');
        }
        const expectedPrototypes=assembly?1:0;
        assert.equal(initial.assetFixture.prototypeUploads,expectedPrototypes);
        if(assembly)assert.deepEqual(initial.assetFixture.assembly,{parts:6,connections:5});
        if(rotations)assert.deepEqual(initial.assetFixture.assembly,{parts:24,connections:12});
        if(skiff)assert.deepEqual(initial.assetFixture.assembly,{parts:11,connections:17});
        assert.equal(initial.controller, 'free-fly');
        assert.equal(initial.bodies, 0); assert.equal(initial.resets, 0);
        assert.equal(initial.assetFixture.uploads, expectedUploads); assert.equal(initial.assetFixture.draws, expectedDraws);
        assert.equal(initial.assetFixture.forcedLod, '0');
        assert.equal(initial.assetFixture.guides, 0); assert.equal(initial.assetFixture.guideBoxes, 0);
        assert.equal(initial.assetFixture.guideMeshGpuBytes, 1936);
        assert(BigInt(initial.assetFixture.gpuReservationBytes) <= 16n * 1024n * 1024n);
        assert.equal(initial.render.render_width, 1920); assert.equal(initial.render.render_height, 1080);
        if (cove) {
            assert(initial.assetFixture.draws > 0 && initial.assetFixture.draws <= 512);
            assert.equal(initial.assetFixture.opaqueSceneGpuBytes, String(1920*1080*12));
            assert.equal(await evaluate('voxyModule._voxy_salvage_preview_action(21)'),0);
            assert.equal(await evaluate('voxyModule._voxy_salvage_preview_action(22)'),0);
        }
        await capture('01-auto');
        for (const lod of (cove ? [] : [...availableLods.slice(1),'0'])) {
            const before = await read();
            await click(`[data-salvage-lod="${lod}"]`);
            await wait(`(() => { const s=${expression}, a=s.assetFixture;
                return s.ready && a.forcedLod===${JSON.stringify(lod)}
                    && BigInt(a.completedSerial)>BigInt(${JSON.stringify(before.assetFixture.submittedSerial)})
                    && (a.forcedLod==='0' || a.lods.every(id=>id===null||id===a.forcedLod)); })()`, `LOD ${lod} drawn and retired`);
            const state = await record(`lod-${lod}`);
            assert.equal(state.assetFixture.uploads, initial.assetFixture.uploads);
            assert.equal(state.assetFixture.generation, initial.assetFixture.generation);
            assert.equal(state.assetFixture.draws, expectedDraws);
            assert.equal(state.assetFixture.prototypeUploads,expectedPrototypes);
            await capture(`02-lod-${lod}`);
        }
        for (const [mode, boxes] of (cove ? [] : [[1,cargo?23:hierarchy?24:26],[2,expectedSockets],[0,0]])) {
            const before = await read();
            await click(`[data-salvage-guide="${mode}"]`);
            await wait(`(() => {const s=${expression},a=s.assetFixture;
                return s.ready && a.guides===${mode} && a.guideBoxes===${boxes}
                    && BigInt(a.completedSerial)>BigInt(${JSON.stringify(before.assetFixture.submittedSerial)});})()`, 'guide mode drawn and completed');
            const state = await record(`guides-${mode}`);
            assert.equal(state.assetFixture.generation, initial.assetFixture.generation);
            assert.equal(state.assetFixture.uploads, expectedUploads);
            assert.equal(state.assetFixture.gpuReservationBytes, initial.assetFixture.gpuReservationBytes);
            assert(mode ? state.assetFixture.draws > expectedDraws : state.assetFixture.draws === expectedDraws);
            await capture(`02-guides-${mode}`);
        }
        // Resizing replaces the borrowed depth/environment views. Real encoded
        // frames must resume after validation without rebuilding resident meshes.
        for (const [width, height] of [[1280, 720], [1920, 1080]]) {
            const before = await read();
            await call('Emulation.setDeviceMetricsOverride', {width, height, deviceScaleFactor: 1, mobile: false});
            await wait(`(() => {const s=${expression};
                const t=JSON.parse(voxyModule.UTF8ToString(voxyModule._voxy_get_telemetry_json())).render_gpu;
                return s.ready && s.assetFixture.status===2 && t.render_width===${width} && t.render_height===${height}
                    && BigInt(s.assetFixture.completedSerial)>BigInt(${JSON.stringify(before.assetFixture.submittedSerial)});})()`, 'resize and GPU completion');
            const resized = await record(`resized-${width}`);
            if (cove) assert.equal(resized.assetFixture.opaqueSceneGpuBytes, String(width*height*12));
            assert.equal(resized.assetFixture.generation, initial.assetFixture.generation);
            assert.equal(resized.assetFixture.uploads, expectedUploads);
            await capture(`03-resized-${width}`);
        }
        await sendKey('W', true);
        try {
            await wait(`(() => {const p=${expression}.camera, start=${JSON.stringify(absolute(initial.camera))};
                return Math.hypot(...p.local.map((v,i)=>v+p.sector[i]*256-start[i]))>.35;})()`, 'flying changes the view', 7000);
        } finally { await sendKey('W', false); }
        const moved = await record('flown');
        assert(distance(absolute(moved.camera), absolute(initial.camera)) > .3);
        await capture('04-flown');
        if (!cove) {
        const manualLod=availableLods.at(-1);
        await click(`[data-salvage-lod="${manualLod}"]`);
        await wait(`(${expression}).assetFixture.forcedLod===${JSON.stringify(manualLod)}`, 'manual detail before Reset');
        await click('[data-salvage-guide="2"]');
        await wait(`(${expression}).assetFixture.guideBoxes===${expectedSockets}`, 'socket overlay before Reset');
        }
        const beforeReset = await read();
        await click('#salvage-reset');
        await wait(`${ready} && (${expression}).resets===1
            && BigInt((${expression}).assetFixture.completedSerial)>BigInt(${JSON.stringify(beforeReset.assetFixture.submittedSerial)})`, 'Reset acknowledgement and fresh GPU completion');
        const reset = await record('reset');
        assert.equal(reset.assetFixture.forcedLod, '0');
        assert.equal(reset.assetFixture.guides, 0); assert.equal(reset.assetFixture.guideBoxes, 0);
        assert.equal(reset.assetFixture.uploads, expectedUploads); assert.equal(reset.assetFixture.generation, initial.assetFixture.generation);
        assert.equal(reset.resident, initial.resident); assert.equal(reset.controller, 'free-fly');
        assert(distance(absolute(reset.camera), absolute(initial.camera)) < .01);
        await capture('05-reset');
        if (!cove) {
            await click('[data-salvage-guide="2"]');
            await wait(`(${expression}).assetFixture.guideBoxes===${expectedSockets}`, 'active socket resources before Leave');
        }
        const exitKey = `voxy.asset.exit.${Date.now()}`;
        await evaluate(`addEventListener('pagehide',()=>sessionStorage.setItem(${JSON.stringify(exitKey)},
            JSON.stringify({state:${expression},resident:voxyModule._voxy_get_physics_resident_bodies(),
                panelHidden:document.getElementById('salvage-preview').hidden})),{once:true})`);
        await click('#salvage-leave');
        await wait(`new URLSearchParams(location.search).get('experience')==='lego-world'
            && typeof voxyModule !== 'undefined' && voxyModule?._voxy_is_initialized?.()===1
            && !document.getElementById('lego-shore-controls').hidden`, 'Leave returns to original world', 90000);
        report.exit = await evaluate(`JSON.parse(sessionStorage.getItem(${JSON.stringify(exitKey)}))`);
        assert.equal(report.exit.state.active, false); assert.equal(report.exit.state.failed, false);
        assert.equal(report.exit.state.busy, false);
        assert.equal(report.exit.state.assetFixture.status, 8, 'Leave must drain the GPU owner before navigation');
        assert.equal(report.exit.state.assetFixture.generation, '0');
        assert.equal(report.exit.state.assetFixture.gpuReservationBytes, '0');
        assert.equal(report.exit.state.assetFixture.guideMeshGpuBytes, 0);
        assert.equal(report.exit.state.assetFixture.guideBoxes, 0);
        assert.equal(report.exit.state.session.admissionOpen, false);
        assert.equal(report.exit.state.bodies, 0); assert.equal(report.exit.resident, initial.resident);
        assert.equal(report.exit.panelHidden, true);
        await capture('06-original-world');
        await evaluate(`sessionStorage.removeItem(${JSON.stringify(exitKey)})`);
        await call('Page.navigate', {url: report.url});
        await wait(`new URLSearchParams(location.search).get('experience')===${JSON.stringify(experience)} && ${ready}`, 'fresh asset re-entry', 90000);
        const reentered = await record('reentered');
        assert.equal(reentered.resets, 0); assert.equal(reentered.assetFixture.uploads, expectedUploads);
        assert.equal(reentered.resident, initial.resident);
        assert(distance(absolute(reentered.camera), absolute(initial.camera)) < .01);
        await click('#salvage-reset');
        await wait(`${ready} && (${expression}).resets===1
            && BigInt((${expression}).assetFixture.completedSerial)>BigInt(${JSON.stringify(reentered.assetFixture.submittedSerial)})`, 'single Reset and fresh GPU completion after re-entry');
        await record('reset-after-reentry'); await capture('07-reentered');
        report.status = 'passed';
        await writeFile(`${directory}/summary.json`, JSON.stringify(report, null, 2) + '\n');
        return report;
    } catch (error) {
        report.status = 'failed'; report.error = String(error);
        try {report.final = await read(); await capture('failure');} catch {}
        await writeFile(`${directory}/summary.json`, JSON.stringify(report, null, 2) + '\n');
        throw error;
    }
}
