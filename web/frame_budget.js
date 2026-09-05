// Reports completed GPU frame intervals. Does not alter camera, scene,
// resolution, simulation, scheduler, or profile settings. CPU/display FPS is
// deliberately a separate metric from the GPU execution budget.
(function(root) {
    'use strict';
    function summarize(samples, targetMs = 1, minimumSamples = 30) {
        if (!Number.isFinite(targetMs) || targetMs <= 0 ||
            !Number.isSafeInteger(minimumSamples) || minimumSamples < 2) {
            throw new RangeError('Invalid budget or minimum sample count');
        }
        const unique = new Map();
        for (const sample of samples) {
            if (!sample?.frame_interval_available || !Number.isFinite(sample.gpu_frame_ms) ||
                sample.gpu_frame_ms <= 0 || !Number.isSafeInteger(sample.frame) || sample.frame < 0 ||
                !Number.isSafeInteger(sample.render_width) || sample.render_width < 1 ||
                !Number.isSafeInteger(sample.render_height) || sample.render_height < 1) continue;
            unique.set(sample.frame, {...sample});
        }
        const values = [...unique.values()].sort((a,b) => a.frame-b.frame);
        const dimensions = new Set(values.map(s=>`${s.render_width}x${s.render_height}`));
        const times = values.map(s=>s.gpu_frame_ms).sort((a,b)=>a-b);
        const percentile = p => times.length ? times[Math.ceil(times.length*p)-1] : null;
        const p95 = percentile(.95);
        let status = values.length < minimumSamples ? 'insufficient_samples' :
            dimensions.size !== 1 ? 'mixed_resolution' :
            p95 <= targetMs ? 'sampled_budget_met' : 'sampled_budget_exceeded';
        if (dimensions.size > 1) status = 'mixed_resolution';
        return {status, target_ms:targetMs, sample_count:times.length, minimum_samples:minimumSamples,
            render_resolutions:[...dimensions], p50_ms:percentile(.50), p95_ms:p95,
            p99_ms:percentile(.99), max_ms:times.length?times[times.length-1]:null,
            over_budget:times.filter(t=>t>targetMs).length, first_sample_frame:values[0]?.frame??null,
            last_sample_frame:values.at(-1)?.frame??null,
            scope:'GPU frame command stream, including GPU physics; excluding CPU, queue uploads, presentation and query readback',
            sampling:'asynchronous profiling samples, not every displayed frame', samples:values};
    }
    async function measure(options = {}) {
        const targetMs = options.targetMs ?? 1, count = options.samples ?? 30;
        const duration = options.maximumDurationMs ?? 120000;
        if (!Number.isSafeInteger(count) || count < 2 || count > 1000 ||
            !Number.isFinite(duration) || duration < 100 || duration > 600000) {
            throw new RangeError('Invalid sampling options');
        }
        summarize([],targetMs,count); // Validate target before starting a poll.
        const module = options.module ?? (typeof voxyModule !== 'undefined' ? voxyModule : root.voxyModule);
        if (!module?._voxy_get_telemetry_json || !module.UTF8ToString) {
            throw new Error('Voxys is not initialized');
        }
        const read = () => JSON.parse(module.UTF8ToString(module._voxy_get_telemetry_json()));
        const initial = read(), firstFrame = initial.frame.count;
        const start = performance.now(), samples = new Map();
        let aborted = false;
        while (performance.now()-start < duration && samples.size < count) {
            if (options.signal?.aborted) {aborted=true;break;}
            const snapshot = read(), timing = snapshot.render_gpu;
            if (timing?.frame_interval_available && timing.frame >= firstFrame &&
                Number.isFinite(timing.gpu_frame_ms) && timing.gpu_frame_ms > 0) {
                samples.set(timing.frame,{...timing});
            }
            if (samples.size < count) await new Promise(r=>setTimeout(r,25));
        }
        const report = summarize([...samples.values()],targetMs,count);
        const adapter = root.voxyDeviceProfile?.adapter ?? null;
        report.adapter=adapter; report.build=root.voxyBuildId??null;
        // Some browser profiles omit/misreport isFallbackAdapter even when
        // the returned architecture explicitly identifies a software renderer.
        const adapterName = [adapter?.architecture, adapter?.description].filter(Boolean).join(' ');
        report.software_adapter = adapter?.fallback === true ||
            /swiftshader|llvmpipe|lavapipe/i.test(adapterName) ? true :
            adapter?.fallback === false ? false : null;
        report.hardware_acceptance_established=false; // A scene sample is not full workload acceptance.
        report.elapsed_ms=performance.now()-start; report.aborted=aborted;
        report.initial_cpu_ms=initial.frame.cpu_ms;
        if(aborted)report.status='aborted';
        return report;
    }
    root.VoxyFrameBudget = Object.freeze({summarize, measure});
    root.voxyMeasureGpuBudget = measure;
})(globalThis);
