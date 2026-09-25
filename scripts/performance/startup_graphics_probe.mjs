// Installed only by the measurement harness, before application code runs.
// The optional source substitution compares shaders in the identical release.
export function installStartupGraphicsProbe(replacement) {
    const report = globalThis.voxyGraphicsStartup = {pipelines: [], replacements: 0};
    const restore = [];
    if (replacement) {
        const original = GPUDevice.prototype.createShaderModule;
        GPUDevice.prototype.createShaderModule = function(descriptor) {
            if (descriptor.label === (replacement.label || 'physics_narrow_phase.wgsl')) {
                if (descriptor.code !== replacement.expected) {
                    report.error = 'Release collision shader differs from the expected baseline';
                    throw Error(report.error);
                }
                descriptor = {...descriptor, code: replacement.code};
                report.replacements++;
            }
            return original.call(this, descriptor);
        };
        restore.push(() => { GPUDevice.prototype.createShaderModule = original; });
    }
    for (const name of ['createComputePipelineAsync', 'createRenderPipelineAsync']) {
        const original = GPUDevice.prototype[name];
        GPUDevice.prototype[name] = function(descriptor) {
            // GPU objects have no enumerable state. Use their labels to compare
            // pipeline settings across builds without retaining live resources.
            const snapshot = JSON.parse(JSON.stringify(descriptor, (key, value) =>
                (key === 'module' || key === 'layout') && value && typeof value === 'object'
                    ? {label: value.label} : value));
            const record = {name, label: descriptor.label,
                entry: descriptor.compute?.entryPoint ?? descriptor.fragment?.entryPoint,
                constants: snapshot.compute?.constants, descriptor: snapshot,
                start_ms: performance.now()};
            report.pipelines.push(record);
            return original.call(this, descriptor).then(pipeline => {
                record.end_ms = performance.now();
                return pipeline;
            }, error => {
                record.error = String(error);
                throw error;
            });
        };
        restore.push(() => { GPUDevice.prototype[name] = original; });
    }
    globalThis.voxyFinishGraphicsProbe = () => {
        for (const undo of restore) undo();
        report.playable_observed_ms = performance.now();
        const starts = report.pipelines.map(p => p.start_ms);
        const ends = report.pipelines.map(p => p.end_ms);
        report.graphics_span_ms = Math.max(...ends) - Math.min(...starts);
        if (report.error || !starts.length || ends.some(end => end === undefined)
            || (replacement && report.replacements !== 1)) {
            throw Error(report.error || 'Incomplete graphics startup probe');
        }
        return report;
    };
}
