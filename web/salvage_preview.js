(function (root, factory) {
    const api = factory();
    if (typeof module === 'object' && module.exports) module.exports = api;
    else root.VoxySalvagePreview = api;
})(globalThis, function () {
    'use strict';
    function install(engine, environment = globalThis) {
        const document = environment.document;
        const panel = document.getElementById('salvage-preview');
        const status = document.getElementById('salvage-status');
        const reset = document.getElementById('salvage-reset');
        const leave = document.getElementById('salvage-leave');
        let stopped = false, pending = null, resetCount = 0, requestedReset = 0;
        let timer;
        const cleanup = () => {
            if (stopped) return;
            stopped = true;
            environment.clearInterval(timer);
            reset.removeEventListener('click', onReset);
            leave.removeEventListener('click', onLeave);
            environment.removeEventListener('pagehide', cleanup);
            panel.hidden = true;
        };
        const fail = () => {
            status.textContent = 'Preview stopped. Reload the page to try again.';
            reset.disabled = leave.disabled = true;
            pending = null;
        };
        const act = action => {
            if (stopped || engine._voxy_is_initialized?.() !== 1) return false;
            try { return engine._voxy_salvage_preview_action(action) === 1; }
            catch { fail(); return false; }
        };
        const onReset = () => {
            if (pending || reset.disabled) return;
            if (act(1)) {
                pending = 'reset'; requestedReset = resetCount;
                reset.disabled = true;
                status.textContent = 'Resetting the cove…';
            }
        };
        const onLeave = () => {
            if (pending === 'leave' || leave.disabled) return;
            if (act(2)) {
                pending = 'leave';
                reset.disabled = leave.disabled = true;
                status.textContent = 'Leaving the cove…';
            }
        };
        const tick = () => {
            if (stopped) return;
            let state;
            try {
                state = JSON.parse(engine.UTF8ToString(engine._voxy_get_salvage_preview_json()));
            } catch { fail(); return; }
            if (state.failed) { fail(); return; }
            if (pending === 'leave' && state.active === false) {
                cleanup();
                // Navigation follows the C++ frame-boundary removal acknowledgement.
                environment.location.assign('?experience=lego-world');
                return;
            }
            if (engine._voxy_is_initialized?.() !== 1) { fail(); return; }
            resetCount = state.resets;
            if (pending === 'reset' && state.ready && resetCount > requestedReset) pending = null;
            if (pending) return;
            reset.disabled = !state.ready;
            leave.disabled = !state.active;
            status.textContent = state.busy ? 'Resetting the cove…' : 'Explore the dock and wreck.';
        };
        reset.addEventListener('click', onReset);
        leave.addEventListener('click', onLeave);
        environment.addEventListener('pagehide', cleanup);
        panel.hidden = false;
        timer = environment.setInterval(tick, 100);
        tick();
        return cleanup;
    }
    return { install };
});
