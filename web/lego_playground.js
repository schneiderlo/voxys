// The UI sends construction actions; the shared GPU physics world owns motion.
window.installLegoPlayground = function (module) {
    const panel = document.getElementById('lego-build-panel');
    const enter = document.getElementById('lego-build-enter');
    const status = document.getElementById('lego-build-status');
    const detail = document.getElementById('lego-build-detail');
    let audio, lastClicks = 0, lastPhase = 0;
    const unlock = () => {
        const Context = window.AudioContext || window.webkitAudioContext;
        if (!Context) return;
        audio ||= new Context();
        if (audio.state === 'suspended') audio.resume().catch(() => {});
    };
    const click = (success) => {
        if (!audio || audio.state !== 'running') return;
        const now = audio.currentTime;
        for (let i = 0; i < (success ? 3 : 1); i++) {
            const oscillator = audio.createOscillator(), gain = audio.createGain();
            oscillator.type = 'sine';
            oscillator.frequency.setValueAtTime(success ? [523, 659, 784][i] : 740, now + i * .08);
            gain.gain.setValueAtTime(0, now + i * .08);
            gain.gain.linearRampToValueAtTime(.035, now + i * .08 + .003);
            gain.gain.exponentialRampToValueAtTime(.0001, now + i * .08 + (success ? .12 : .035));
            oscillator.connect(gain); gain.connect(audio.destination);
            oscillator.start(now + i * .08); oscillator.stop(now + i * .08 + .15);
            oscillator.onended = () => { oscillator.disconnect(); gain.disconnect(); };
        }
    };
    const act = action => {
        unlock();
        return module._voxy_lego_action(action) === 1;
    };
    enter.addEventListener('click', () => act(0));
    panel.querySelectorAll('[data-lego-action]').forEach(button => {
        button.addEventListener('click', () => {
            if (act(Number(button.dataset.legoAction)) && button.dataset.legoShape) {
                panel.querySelectorAll('[data-lego-shape]').forEach(b => b.setAttribute('aria-pressed', String(b === button)));
            }
        });
    });
    document.addEventListener('pointerdown', unlock, { once: true });
    document.addEventListener('keydown', unlock, { once: true });
    setInterval(() => {
        if (module._voxy_is_initialized?.() !== 1) return;
        const state = JSON.parse(module.UTF8ToString(module._voxy_get_lego_hud_json()));
        panel.hidden = !state.active;
        enter.hidden = state.active;
        document.body.classList.toggle('lego-building', state.active);
        const success = state.phase === 2 && lastPhase !== 2;
        if (state.clicks > lastClicks) click(success);
        lastClicks = state.clicks; lastPhase = state.phase;
        status.textContent = ['Build a three-level tower', 'Target ready — knock it down', 'Target down!'][state.phase];
        detail.textContent = state.phase === 2 ? 'Nice shot. Reset to play again.'
            : state.phase === 1 ? 'Aim at the tower. Throw a ball with B.'
            : state.awake ? 'Let the bricks settle, then add the next level.'
            : 'Point at the pad or a brick. Green means it fits.';
        document.getElementById('lego-levels').textContent = `${Math.min(state.levels, 3)} / 3 levels`;
        document.getElementById('lego-brick-count').textContent = `${state.bricks} / 48 bricks`;
        panel.dataset.success = String(state.phase === 2);
        document.getElementById('lego-place').disabled = !state.valid;
        document.getElementById('lego-aim').hidden = state.phase !== 1;
    }, 100);
};
