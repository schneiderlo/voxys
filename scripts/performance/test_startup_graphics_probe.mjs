import assert from 'node:assert/strict';
import {test} from 'node:test';
import {installStartupGraphicsProbe} from './startup_graphics_probe.mjs';

test('startup measurement preserves pipeline descriptors and restores gameplay APIs', async () => {
    class Device {
        createShaderModule(descriptor) { return descriptor; }
        async createComputePipelineAsync(descriptor) { return descriptor; }
        async createRenderPipelineAsync(descriptor) { return descriptor; }
    }
    globalThis.GPUDevice = Device;
    const create = Device.prototype.createComputePipelineAsync;
    const shader = Device.prototype.createShaderModule;
    installStartupGraphicsProbe({expected: 'baseline', code: 'candidate'});
    const device = new Device();
    const unrelated = {label: 'water', code: 'water source'};
    assert.equal(device.createShaderModule(unrelated), unrelated);
    assert.equal(device.createShaderModule({label: 'physics_narrow_phase.wgsl', code: 'baseline'}).code, 'candidate');
    const descriptor = {label: 'collision', layout: {label: 'collision layout'},
        compute: {module: {label: 'collision source'}, entryPoint: 'main', constants: {PASS: 1}}};
    assert.equal(await device.createComputePipelineAsync(descriptor), descriptor);
    descriptor.compute.constants.PASS = 2;
    const result = voxyFinishGraphicsProbe();
    assert.equal(result.replacements, 1);
    assert.equal(result.pipelines.length, 1);
    assert.deepEqual(result.pipelines[0].constants, {PASS: 1});
    assert.deepEqual(result.pipelines[0].descriptor.layout, {label: 'collision layout'});
    assert.deepEqual(result.pipelines[0].descriptor.compute.module, {label: 'collision source'});
    assert.ok(result.graphics_span_ms >= 0);
    assert.equal(Device.prototype.createComputePipelineAsync, create);
    assert.equal(Device.prototype.createShaderModule, shader);
    await device.createComputePipelineAsync(descriptor);
    assert.equal(result.pipelines.length, 1, 'gameplay is no longer instrumented');
});

test('a mismatched deployed shader cannot produce a valid comparison', () => {
    installStartupGraphicsProbe({expected: 'baseline', code: 'candidate'});
    assert.throws(() => new GPUDevice().createShaderModule({label: 'physics_narrow_phase.wgsl', code: 'new release'}), /differs/);
    assert.throws(() => voxyFinishGraphicsProbe(), /differs/);
});

test('an absent substitution or unfinished compilation is not a completed startup', async () => {
    installStartupGraphicsProbe({expected: 'baseline', code: 'candidate'});
    await new GPUDevice().createRenderPipelineAsync({label: 'other'});
    assert.throws(() => voxyFinishGraphicsProbe(), /Incomplete/);
    installStartupGraphicsProbe(null);
    const pending = new GPUDevice().createComputePipelineAsync({label: 'pending'});
    assert.throws(() => voxyFinishGraphicsProbe(), /Incomplete/);
    await pending;
});
