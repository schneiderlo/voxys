// Dedicated destructive test in a fresh disposable browser process. Never run
// this on a user's tab. Actual GPUDevice.destroy(), no synthetic lost flag.
import assert from 'node:assert/strict';
import {mkdir, writeFile} from 'node:fs/promises';

export async function validateSalvageAssetLoss(call, directory) {
    await mkdir(directory, {recursive: true});
    const evaluate = async expression => {
        const result = await call('Runtime.evaluate', {expression, returnByValue: true, awaitPromise: true});
        if (result.exceptionDetails) throw Error(JSON.stringify(result.exceptionDetails));
        return result.result?.value;
    };
    const snapshot = `({initialized:voxyModule._voxy_is_initialized(),
        state:JSON.parse(voxyModule.UTF8ToString(voxyModule._voxy_get_salvage_preview_json())),
        lost:globalThis.voxyDeviceLost, errors:globalThis.voxyUncapturedGpuErrors || [],
        diagnostics:globalThis.voxyStartupDiagnostics})`;
    const report = {kind: 'Actual browser GPUDevice.destroy with in-flight application resources'};
    try {
        const deadline = Date.now() + 30000;
        do {
            report.before = await evaluate(snapshot);
            if (report.before.state.ready && report.before.state.assetFixture?.status === 2
                && BigInt(report.before.state.assetFixture.completedSerial) > 0n) break;
            await new Promise(resolve => setTimeout(resolve, 100));
        } while (Date.now() < deadline);
        assert.equal(report.before.state.assetFixture.status, 2);
        assert.equal(report.before.initialized, 1);
        assert.deepEqual(report.before.errors, []); assert.equal(report.before.lost, null);
        const previousSerial=report.before.state.assetFixture.submittedSerial;
        const hierarchy=await evaluate("new URLSearchParams(location.search).get('experience')==='salvage-hierarchy'");
        const rotations=await evaluate("['salvage-rotations-a','salvage-rotations-b'].includes(new URLSearchParams(location.search).get('experience'))");
        const experience=await evaluate("new URLSearchParams(location.search).get('experience')");
        const skiff=experience==='salvage-kit-broad'||experience==='salvage-kit-narrow';
        const socketBoxes=experience==='salvage-material-detail'?135:
            ['salvage-materials','salvage-kit-cargo'].includes(experience)?120:
            skiff?510:hierarchy?375:rotations?360:report.before.state.assetFixture.assembly?150:360;
        if(experience==='salvage-material-detail'){
            assert.equal(report.before.state.assetFixture.environmentGpuBytes,'1228944');
            assert.equal(report.before.state.assetFixture.environmentBakeCount,1);
            assert.equal(report.before.state.assetFixture.environmentReady,true);
        }
        assert.equal(await evaluate('voxyModule._voxy_salvage_preview_action(22)'),1);
        const guideDeadline=Date.now()+10000;
        do {
            report.before=await evaluate(snapshot);
            if(report.before.state.assetFixture.guideBoxes===socketBoxes
                && BigInt(report.before.state.assetFixture.completedSerial)>BigInt(previousSerial))break;
            await new Promise(resolve=>setTimeout(resolve,100));
        }while(Date.now()<guideDeadline);
        assert.equal(report.before.state.assetFixture.guideBoxes,socketBoxes);
        assert(BigInt(report.before.state.assetFixture.completedSerial)>BigInt(previousSerial));
        assert.equal(await evaluate('voxyModule.preinitializedWebGPUDevice instanceof GPUDevice'), true);
        // Return immediately after destruction; do not await a queue promise
        // that should reject on the lost device.
        await evaluate('voxyModule.preinitializedWebGPUDevice.destroy(); true');
        const end = Date.now() + 10000;
        do {
            report.after = await evaluate(snapshot);
            if (report.after.lost && report.after.initialized === 0) break;
            await new Promise(resolve => setTimeout(resolve, 100));
        } while (Date.now() < end);
        assert.equal(report.after.lost?.reason, 'destroyed', 'actual device loss must be observed');
        assert.equal(report.after.initialized, 0, 'application must finish exceptional teardown');
        assert.equal(report.after.state.active, false);
        assert.equal(report.after.state.assetFixture, undefined, 'fixture owner must be released');
        assert.deepEqual(report.after.errors, [], 'device loss must not cause uncaptured validation errors');
        assert.equal(report.after.diagnostics.destroyCalls.length, report.before.diagnostics.destroyCalls.length + 1);
        await new Promise(resolve => setTimeout(resolve, 300));
        report.settled = await evaluate(snapshot);
        assert.equal(report.settled.initialized, 0); assert.deepEqual(report.settled.errors, []);
        const screenshot = await call('Page.captureScreenshot', {format: 'png'});
        await writeFile(`${directory}/stopped.png`, Buffer.from(screenshot.data, 'base64'));
        report.status = 'passed';
        await writeFile(`${directory}/summary.json`, JSON.stringify(report, null, 2) + '\n');
        return report;
    } catch (error) {
        report.status = 'failed'; report.error = String(error);
        try {report.final = await evaluate(snapshot);} catch {}
        await writeFile(`${directory}/summary.json`, JSON.stringify(report, null, 2) + '\n');
        throw error;
    }
}
