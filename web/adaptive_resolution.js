// Uses already completed GPU timestamp packets; never waits on the GPU or
// changes the user's saved resolution. Missing profiling keeps fixed quality.
(function(root) {
    'use strict';
    const storageKey = 'voxy.renderer-inspector.v1';
    const clamp = (value, lo, hi) => Math.min(hi, Math.max(lo, value));
    function preference(storage, search = '') {
        const params = new URLSearchParams(search);
        if (params.has('browserBenchmarkRun') || params.get('renderThroughput') === '1') return false;
        const override = params.get('adaptiveResolution');
        if (override === '0' || override === '1') return override === '1';
        try {
            const settings = JSON.parse(storage?.getItem(storageKey))?.settings;
            const auto = settings?.['browser.adaptiveResolution'];
            if (auto === 0 || auto === 1) return auto === 1;
            // A legacy saved resolution is an explicit fixed-quality choice.
            if (Number.isFinite(settings?.['browser.resolutionScale'])) return false;
        } catch (_) { /* Storage denial must not stop the renderer. */ }
        return ['frontier', 'build', 'adventure', 'lego-world'].includes(params.get('experience') || 'frontier');
    }

    class Controller {
        constructor({ceiling = 1, enabled = true, now = 0} = {}) {
            this.configure(ceiling, enabled, now);
        }
        configure(ceiling, enabled, now) {
            this.ceiling = clamp(Number.isFinite(ceiling) ? ceiling : 1, .35, 1.5);
            this.floor = Math.max(.35, this.ceiling * .65);
            this.scale = this.ceiling;
            this.enabled = Boolean(enabled);
            this.lastChange = now;
            this.lastMotion = now;
            this.lastFrame = -1;
            this.refreshes = null;
            this.samples = [];
            this.resizes = 0;
        }
        resetSamples(now) {
            this.samples = [];
            this.lastChange = now;
            this.lastMotion = now;
        }
        observe(snapshot, width, height, now) {
            if (!this.enabled || !snapshot || !Number.isFinite(now)) return null;
            const refreshes = snapshot.render?.terrain_cache_refreshes;
            if (Number.isSafeInteger(refreshes)) {
                if (this.refreshes !== null && refreshes !== this.refreshes) this.lastMotion = now;
                this.refreshes = refreshes;
            } else this.lastMotion = now; // No stationary evidence: do not restore aggressively.
            const gpu = snapshot.render_gpu;
            if (!gpu?.available || !gpu.frame_interval_available ||
                !Number.isSafeInteger(gpu.frame) || gpu.frame <= this.lastFrame ||
                !Number.isSafeInteger(snapshot.frame?.count) || gpu.frame > snapshot.frame.count ||
                snapshot.frame.count - gpu.frame > 120 ||
                gpu.render_width !== width || gpu.render_height !== height ||
                !Number.isFinite(gpu.gpu_frame_ms) || gpu.gpu_frame_ms <= 0) return null;
            this.lastFrame = gpu.frame;
            // Discard queued frames from startup or the preceding target size.
            if (now - this.lastChange < 1500) return null;
            this.samples.push({ms: gpu.gpu_frame_ms, at: now});
            this.samples = this.samples.filter(s => now - s.at <= 2000).slice(-3);
            if (this.samples.length < 3 || now - this.lastChange < 3000) return null;
            const costs = this.samples.map(s => s.ms).sort((a, b) => a - b);
            const median = costs[1];
            let next = this.scale;
            let reason;
            // 10 ms leaves CPU/presentation headroom within a 60 Hz frame.
            // Require sustained overload; one slow sample cannot trigger resize.
            if (costs[0] > 11.2) {
                next = Math.max(this.floor, this.scale * .8, this.scale * Math.sqrt(10 / median));
                reason = 'gpu-over-budget';
            } else if (now - this.lastMotion >= 8000 && now - this.lastChange >= 8000 && costs[2] < 10) {
                // Restore slowly only after terrain cache has settled. The
                // projected pixel cost must still fit the same GPU budget.
                next = Math.min(this.ceiling, this.scale + this.ceiling * .08,
                    this.scale * Math.sqrt(10 / costs[2]));
                reason = 'stationary-recovery';
            }
            next = clamp(next, this.floor, this.ceiling);
            if (Math.abs(next - this.scale) < this.ceiling * .025 &&
                !(reason === 'stationary-recovery' && next === this.ceiling && next > this.scale)) return null;
            this.scale = next;
            this.resizes += 1;
            this.resetSamples(now);
            return {scale: next, reason, gpu_ms: median};
        }
    }

    function install({module, canvas, ceiling, enabled, applyScale, environment = root}) {
        const params = new URLSearchParams(environment.location?.search || '');
        const locked = params.has('browserBenchmarkRun') || params.get('renderThroughput') === '1';
        const controller = new Controller({ceiling, enabled: enabled && !locked, now: environment.performance.now()});
        let disposed = false;
        const timer = environment.setInterval(() => {
            if (disposed || !controller.enabled || environment.document?.hidden ||
                module?._voxy_is_initialized?.() !== 1 || !module?._voxy_get_telemetry_json) return;
            try {
                const snapshot = JSON.parse(module.UTF8ToString(module._voxy_get_telemetry_json()));
                const change = controller.observe(snapshot, canvas.width, canvas.height, environment.performance.now());
                if (change) applyScale(change.scale);
            } catch (_) { /* Profiling/device loss must not break input or startup. */ }
        }, 250);
        return {
            controller,
            configure(nextCeiling, nextEnabled) {
                controller.configure(nextCeiling, nextEnabled && !locked, environment.performance.now());
                applyScale(controller.scale);
            },
            resized() { controller.resetSamples(environment.performance.now()); },
            dispose() { disposed = true; environment.clearInterval(timer); },
        };
    }
    root.VoxyAdaptiveResolution = Object.freeze({Controller, install, preference, storageKey});
})(globalThis);
