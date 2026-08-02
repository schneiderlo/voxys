(function rendererInspectorBootstrap(global) {
    'use strict';

    const STORAGE_KEY = 'voxy.renderer-inspector.v1';
    const UI_STORAGE_KEY = 'voxy.renderer-inspector-ui.v1';
    const PRESET_STORAGE_KEY = 'voxy.renderer-presets.v1';
    const NUMBER_TYPES = ['number', 'select', 'boolean'];
    // KeyboardEvent.code names physical key positions. These are WASD on a
    // QWERTY keyboard and ZQSD on an AZERTY keyboard.
    const MOVEMENT_KEY_CODES = new Map([
        ['KeyW', 87],
        ['KeyA', 65],
        ['KeyS', 83],
        ['KeyD', 68],
    ]);

    const groups = [
        {
            id: 'render', label: 'Render', icon: '▣', open: true,
            controls: [
                {
                    key: 'browser.resolutionScale', label: 'Resolution',
                    type: 'number', min: 0.35, max: 1.5, step: 0.01,
                    default: 1, unit: '×', browser: true,
                    description: 'Internal pixel density. Render targets resize when released.',
                    cost: 'resize',
                },
                {
                    key: 'render.path', label: 'Render Path', type: 'select',
                    default: 1,
                    options: [{value: 1, label: 'Raycast'}, {value: 0, label: 'Triangles'}],
                    description: 'Switch between the primary raycast and reference triangle path.',
                },
            ],
        },
        {
            id: 'lighting', label: 'Lighting', icon: '☀', open: true,
            controls: [
                number('lighting.sunAzimuth', 'Sun Azimuth', -180, 180, 0.1, '°',
                    'Horizontal sun angle.', 'rebuild'),
                number('lighting.sunElevation', 'Sun Elevation', 1, 89, 0.1, '°',
                    'Sun height above the horizon.', 'rebuild'),
                color('lighting.sunColor', 'Sun Colour', 'Direct-light tint.'),
                number('lighting.sunIntensity', 'Sun Strength', 0, 10, 0.01, '',
                    'Direct lighting and ocean highlights.'),
                color('lighting.ambientColor', 'Ambient Colour', 'Sky-fill tint.'),
                number('lighting.ambientIntensity', 'Ambient Strength', 0, 4, 0.01, '',
                    'Light reaching surfaces outside direct sun.'),
                color('lighting.fogColor', 'Fog Colour', 'Atmospheric and horizon colour.'),
                number('lighting.fogDensity', 'Fog Density', 0, 0.003, 0.000001, '',
                    'Exponential atmosphere density.'),
                number('lighting.exposure', 'Exposure', 0.05, 8, 0.01, '×',
                    'Scene-linear exposure before tone mapping.'),
            ],
        },
        {
            id: 'water', label: 'Water Surface', icon: '≋', open: true,
            controls: [
                toggle('water.enabled', 'Enabled', 'Render water and enable buoyancy.'),
                number('water.height', 'Sea Level', -1000, 1000, 0.1, 'm',
                    'Visual and physical water plane.', 'rebuild'),
                color('water.shallowColor', 'Surface Colour', 'Colour of shallow, lit water.'),
                color('water.deepColor', 'Deep Colour', 'Subsurface scattering colour.'),
                number('water.roughness', 'Roughness', 0.02, 1, 0.001, '',
                    'Minimum reflection roughness.'),
                number('water.reflectionStrength', 'Reflection', 0, 1, 0.001, '',
                    'Environment reflection strength.'),
                number('water.reflectionDistance', 'Reflection Falloff', 10, 20000, 1, 'm',
                    'Distance over which reflections become rough.'),
                number('water.ior', 'Index of Refraction', 1, 2, 0.001, '',
                    'Fresnel and refraction response. Water is about 1.33.'),
                number('water.distortion', 'Refraction', 0, 1, 0.001, '',
                    'Screen-space refraction displacement.'),
                number('water.absorptionScale', 'Absorption', 0, 5, 0.001, '×',
                    'Beer–Lambert absorption multiplier.'),
                number('water.scatterStrength', 'Scattering', 0, 5, 0.001, '×',
                    'Deep-water body-light multiplier.'),
                number('water.shoreFade', 'Shore Fade', 0.01, 500, 0.01, 'm',
                    'Depth used to blend shallow and deep colours.'),
                number('water.foamSize', 'Foam Scale', 8, 2000, 1, 'm',
                    'World-space foam pattern size.'),
                number('water.foamOpacity', 'Foam Opacity', 0, 1, 0.001, '',
                    'Whitecap visibility.'),
                number('water.foamCoverage', 'Foam Coverage', 0, 1, 0.001, '',
                    'Fraction of the foam pattern that becomes visible.'),
            ],
        },
        {
            id: 'waves', label: 'Wave Spectrum', icon: '∿', open: true,
            controls: [
                number('water.waveStrength', 'Master Strength', 0, 2, 0.001, '×',
                    'Final displacement multiplier shared by rendering and physics.'),
                number('water.spectrum.significantHeight', 'Wave Height', 0.1, 100, 0.1, 'm',
                    'Significant wave height used to generate the spectrum.', 'rebuild'),
                number('water.spectrum.direction', 'Wind Direction', -180, 180, 0.1, '°',
                    'Dominant spectral direction.', 'rebuild'),
                number('water.spectrum.choppiness', 'Choppiness', 0, 5, 0.001, '',
                    'Horizontal displacement and crest sharpness.'),
                number('water.spectrum.peakEnhancement', 'Peak Shape', 0.05, 10, 0.001, '',
                    'Energy concentrated around the dominant wavelength.', 'rebuild'),
                number('water.spectrum.windAlignment', 'Directionality', 0, 1, 0.001, '',
                    'How strongly waves align with wind.', 'rebuild'),
                number('water.spectrum.speed', 'Animation Speed', 0, 5, 0.001, '×',
                    'Spectral phase speed.'),
                number('water.spectrum.largePatch', 'Swell Patch', 64, 8192, 1, 'm',
                    'Repeating world size of the broad cascade.', 'rebuild'),
                number('water.spectrum.detailPatch', 'Detail Patch', 16, 2048, 1, 'm',
                    'Repeating world size of the detail cascade.', 'rebuild'),
                number('water.spectrum.largeAmplitude', 'Swell Amplitude', 0, 2, 0.001, '×',
                    'Broad cascade output multiplier.'),
                number('water.spectrum.detailAmplitude', 'Detail Amplitude', 0, 2, 0.001, '×',
                    'Detail cascade output multiplier.'),
                number('water.spectrum.directionalSine', 'Directional Phase', 0, 1.5, 0.001, '',
                    'Directional quadrature component used during spectrum evolution.'),
            ],
        },
        {
            id: 'camera', label: 'Camera', icon: '◉', open: false,
            controls: [
                number('camera.fov', 'Field of View', 20, 120, 0.1, '°',
                    'Vertical perspective field of view.'),
                number('camera.near', 'Near Clip', 0.01, 10, 0.001, 'm',
                    'Closest visible distance.'),
                number('camera.far', 'Far Clip', 100, 100000, 1, 'm',
                    'Farthest visible distance.'),
                number('camera.moveSpeed', 'Move Speed', 0.1, 1000, 0.1, 'm/s',
                    'Walk and free-flight base speed.'),
                number('camera.mouseSensitivity', 'Look Sensitivity', 0.00001, 0.02,
                    0.00001, '', 'Mouse radians per pixel.'),
                number('camera.eyeHeight', 'Eye Height', 0.5, 10, 0.01, 'm',
                    'Character camera height above the ground.'),
            ],
        },
    ];

    function number(key, label, min, max, step, unit, description, cost) {
        return {key, label, type: 'number', min, max, step, unit, description, cost};
    }

    function color(key, label, description) {
        return {key, label, type: 'color', description};
    }

    function toggle(key, label, description) {
        return {key, label, type: 'boolean', description};
    }

    function safeParse(storageKey, fallback) {
        try {
            const parsed = JSON.parse(localStorage.getItem(storageKey));
            return parsed && typeof parsed === 'object' ? parsed : fallback;
        } catch (_) {
            return fallback;
        }
    }

    function decimals(step) {
        if (!Number.isFinite(step) || step >= 1) return 0;
        return Math.min(8, Math.max(0, Math.ceil(-Math.log10(step))));
    }

    function clamp(value, min, max) {
        return Math.min(max, Math.max(min, value));
    }

    function valuesEqual(a, b) {
        if (Array.isArray(a) && Array.isArray(b)) {
            return a.length === b.length && a.every((value, index) =>
                Math.abs(value - b[index]) < 1e-7);
        }
        return Math.abs(Number(a) - Number(b)) < 1e-7;
    }

    function clone(value) {
        return JSON.parse(JSON.stringify(value));
    }

    function colorToHex(rgb) {
        const channel = value => Math.round(clamp(value, 0, 1) * 255)
            .toString(16).padStart(2, '0');
        return `#${channel(rgb[0])}${channel(rgb[1])}${channel(rgb[2])}`;
    }

    function hexToColor(value) {
        const match = /^#?([0-9a-f]{6})$/i.exec(value);
        if (!match) return null;
        const packed = Number.parseInt(match[1], 16);
        return [((packed >> 16) & 255) / 255,
            ((packed >> 8) & 255) / 255, (packed & 255) / 255];
    }

    function isTextEntry(target) {
        if (!(target instanceof HTMLElement)) return false;
        if (target.isContentEditable || target instanceof HTMLTextAreaElement) {
            return true;
        }
        if (!(target instanceof HTMLInputElement)) return false;
        return ['email', 'password', 'search', 'tel', 'text', 'url']
            .includes(target.type);
    }

    class RendererInspector {
        constructor(options) {
            this.module = options.module;
            this.canvas = options.canvas;
            this.onRenderScale = options.onRenderScale;
            this.values = new Map();
            this.defaults = new Map();
            this.rows = new Map();
            this.connected = false;
            this.history = [];
            this.future = [];
            this.transactionBefore = null;
            this.forwardedMovementKeys = new Set();
            this.persistTimer = 0;
            this.uiState = safeParse(UI_STORAGE_KEY, {});
            this.customPresets = safeParse(PRESET_STORAGE_KEY, {});
            this.root = null;
            this.panel = null;
            this.search = null;
            this.status = null;
            this.presetSelect = null;
            this.build();
            this.installGlobalEvents();
            this.connectWhenReady();
        }

        build() {
            const root = document.createElement('div');
            root.id = 'renderer-inspector';
            root.dataset.voxyUi = 'true';
            root.innerHTML = `
                <button class="ri-launcher" type="button" aria-label="Toggle renderer inspector"
                    title="Renderer inspector (\`)" aria-expanded="false">
                    <span class="ri-launcher-icon">◫</span><span>Renderer</span>
                </button>
                <aside class="ri-panel" aria-label="Renderer inspector" aria-hidden="true">
                    <div class="ri-resize" title="Drag to resize"></div>
                    <header class="ri-header">
                        <div class="ri-title-block">
                            <span class="ri-eyebrow">RIDGEBREAK</span>
                            <h2>Renderer</h2>
                        </div>
                        <div class="ri-header-actions">
                            <button data-action="undo" title="Undo (Ctrl Z)" disabled>↶</button>
                            <button data-action="redo" title="Redo (Ctrl Shift Z)" disabled>↷</button>
                            <button data-action="close" title="Close inspector">×</button>
                        </div>
                    </header>
                    <div class="ri-toolbar">
                        <label class="ri-search-wrap">
                            <span>⌕</span>
                            <input class="ri-search" type="search" placeholder="Search settings" autocomplete="off">
                            <kbd>/</kbd>
                        </label>
                        <div class="ri-preset-row">
                            <select class="ri-presets" aria-label="Renderer preset"></select>
                            <button data-action="save-preset" title="Save current preset">＋</button>
                            <button data-action="delete-preset" title="Delete selected custom preset">−</button>
                            <button data-action="copy" title="Copy settings as JSON">⧉</button>
                            <button data-action="paste" title="Paste settings JSON">⇩</button>
                        </div>
                    </div>
                    <div class="ri-scroll" tabindex="-1"></div>
                    <footer class="ri-footer">
                        <span class="ri-live-dot"></span>
                        <span class="ri-status">Waiting for renderer…</span>
                        <span class="ri-resolution">—</span>
                    </footer>
                    <div class="ri-toast" role="status" aria-live="polite"></div>
                </aside>`;
            document.body.appendChild(root);
            this.root = root;
            this.panel = root.querySelector('.ri-panel');
            this.search = root.querySelector('.ri-search');
            this.status = root.querySelector('.ri-status');
            this.presetSelect = root.querySelector('.ri-presets');

            const width = clamp(Number(this.uiState.width) || 382, 320, 680);
            this.panel.style.setProperty('--ri-width', `${width}px`);
            this.renderGroups(root.querySelector('.ri-scroll'));
            this.refreshPresetOptions();
            this.bindShellEvents();
            this.setOpen(Boolean(this.uiState.open), false);
        }

        renderGroups(container) {
            for (const group of groups) {
                const section = document.createElement('section');
                section.className = 'ri-group';
                section.dataset.group = group.id;
                const collapsed = this.uiState.collapsed?.[group.id] ?? !group.open;
                section.classList.toggle('is-collapsed', collapsed);

                const heading = document.createElement('button');
                heading.type = 'button';
                heading.className = 'ri-group-heading';
                heading.innerHTML = `<span class="ri-chevron">⌄</span>
                    <span class="ri-group-icon">${group.icon}</span>
                    <span>${group.label}</span>
                    <span class="ri-group-count">${group.controls.length}</span>`;
                heading.setAttribute('aria-expanded', String(!collapsed));
                heading.addEventListener('click', () => {
                    section.classList.toggle('is-collapsed');
                    const open = !section.classList.contains('is-collapsed');
                    heading.setAttribute('aria-expanded', String(open));
                    this.uiState.collapsed = this.uiState.collapsed || {};
                    this.uiState.collapsed[group.id] = !open;
                    this.persistUi();
                });
                section.appendChild(heading);

                const body = document.createElement('div');
                body.className = 'ri-group-body';
                for (const control of group.controls) {
                    const row = this.renderControl(control);
                    body.appendChild(row);
                    this.rows.set(control.key, row);
                }
                section.appendChild(body);
                container.appendChild(section);
            }
        }

        renderControl(control) {
            const row = document.createElement('div');
            row.className = `ri-row ri-${control.type}`;
            row.dataset.key = control.key;
            row.dataset.search = `${control.label} ${control.key} ${control.description || ''}`
                .toLowerCase();
            row.title = control.description || '';

            const label = document.createElement('div');
            label.className = 'ri-label';
            label.innerHTML = `<span class="ri-label-text">${control.label}</span>`;
            if (control.cost) {
                const badge = document.createElement('span');
                badge.className = `ri-cost ri-cost-${control.cost}`;
                badge.textContent = control.cost === 'resize' ? 'RESIZE' : 'BUILD';
                badge.title = control.cost === 'resize'
                    ? 'Render targets resize when committed'
                    : 'A dependent GPU/CPU resource rebuilds when committed';
                label.appendChild(badge);
            }
            row.appendChild(label);

            const editor = document.createElement('div');
            editor.className = 'ri-editor';
            if (control.type === 'number') this.renderNumber(control, editor, label);
            else if (control.type === 'boolean') this.renderBoolean(control, editor);
            else if (control.type === 'color') this.renderColor(control, editor);
            else if (control.type === 'select') this.renderSelect(control, editor);
            row.appendChild(editor);

            const reset = document.createElement('button');
            reset.type = 'button';
            reset.className = 'ri-reset';
            reset.title = 'Reset this value';
            reset.setAttribute('aria-label', `Reset ${control.label}`);
            reset.textContent = '•';
            reset.addEventListener('click', () => {
                this.beginTransaction();
                this.setControlValue(control, clone(this.defaults.get(control.key)), true);
            });
            row.appendChild(reset);
            return row;
        }

        renderNumber(control, editor, label) {
            label.classList.add('ri-scrubbable');
            label.title = `${control.description || ''}\nDrag label to scrub. Shift = fine.`;
            const slider = document.createElement('input');
            slider.type = 'range';
            slider.min = String(control.min);
            slider.max = String(control.max);
            slider.step = String(control.step);
            slider.value = String(control.default ?? control.min);
            slider.setAttribute('aria-label', control.label);

            const field = document.createElement('div');
            field.className = 'ri-number-field';
            const input = document.createElement('input');
            input.type = 'number';
            input.min = String(control.min);
            input.max = String(control.max);
            input.step = String(control.step);
            input.value = String(control.default ?? control.min);
            input.setAttribute('aria-label', `${control.label} value`);
            const unit = document.createElement('span');
            unit.textContent = control.unit || '';
            field.append(input, unit);
            editor.append(slider, field);
            editor._slider = slider;
            editor._input = input;

            slider.addEventListener('focus', () => this.beginTransaction());
            slider.addEventListener('pointerdown', () => this.beginTransaction());
            slider.addEventListener('input', () => {
                this.setControlValue(control, Number(slider.value), false, slider);
            });
            slider.addEventListener('change', () => {
                this.setControlValue(control, Number(slider.value), true, slider);
            });
            input.addEventListener('focus', () => this.beginTransaction());
            input.addEventListener('input', () => {
                if (input.value !== '' && Number.isFinite(input.valueAsNumber)) {
                    this.setControlValue(control, input.valueAsNumber, false, input);
                }
            });
            input.addEventListener('change', () => {
                const value = Number.isFinite(input.valueAsNumber)
                    ? input.valueAsNumber : this.values.get(control.key);
                this.setControlValue(control, value, true, input);
            });

            let scrub = null;
            label.addEventListener('pointerdown', event => {
                if (event.button !== 0) return;
                event.preventDefault();
                this.beginTransaction();
                scrub = {x: event.clientX, value: Number(this.values.get(control.key))};
                label.setPointerCapture(event.pointerId);
                label.classList.add('is-scrubbing');
            });
            label.addEventListener('pointermove', event => {
                if (!scrub) return;
                const range = control.max - control.min;
                const precision = event.shiftKey ? 0.1 : 1;
                const delta = (event.clientX - scrub.x) / 220 * range * precision;
                const value = Math.round((scrub.value + delta) / control.step) * control.step;
                this.setControlValue(control, value, false, label);
            });
            const endScrub = event => {
                if (!scrub) return;
                scrub = null;
                label.classList.remove('is-scrubbing');
                if (label.hasPointerCapture(event.pointerId)) {
                    label.releasePointerCapture(event.pointerId);
                }
                this.setControlValue(control, this.values.get(control.key), true, label);
            };
            label.addEventListener('pointerup', endScrub);
            label.addEventListener('pointercancel', endScrub);
        }

        renderBoolean(control, editor) {
            const button = document.createElement('button');
            button.type = 'button';
            button.className = 'ri-switch';
            button.setAttribute('role', 'switch');
            button.setAttribute('aria-label', control.label);
            button.setAttribute('aria-checked', 'false');
            button.innerHTML = '<span></span>';
            button.addEventListener('click', () => {
                this.beginTransaction();
                this.setControlValue(control, this.values.get(control.key) ? 0 : 1, true);
            });
            editor.appendChild(button);
            editor._button = button;
        }

        renderColor(control, editor) {
            const wrapper = document.createElement('div');
            wrapper.className = 'ri-color-field';
            const swatch = document.createElement('input');
            swatch.type = 'color';
            swatch.value = '#ffffff';
            swatch.setAttribute('aria-label', control.label);
            const hex = document.createElement('input');
            hex.type = 'text';
            hex.maxLength = 7;
            hex.spellcheck = false;
            hex.value = '#ffffff';
            hex.setAttribute('aria-label', `${control.label} hex value`);
            wrapper.append(swatch, hex);
            editor.appendChild(wrapper);
            editor._swatch = swatch;
            editor._hex = hex;

            swatch.addEventListener('pointerdown', () => this.beginTransaction());
            swatch.addEventListener('focus', () => this.beginTransaction());
            swatch.addEventListener('input', () => {
                this.setControlValue(control, hexToColor(swatch.value), false, swatch);
            });
            swatch.addEventListener('change', () => {
                this.setControlValue(control, hexToColor(swatch.value), true, swatch);
            });
            hex.addEventListener('focus', () => this.beginTransaction());
            hex.addEventListener('input', () => {
                const value = hexToColor(hex.value);
                if (value) this.setControlValue(control, value, false, hex);
            });
            hex.addEventListener('change', () => {
                const value = hexToColor(hex.value) || this.values.get(control.key);
                this.setControlValue(control, value, true, hex);
            });
        }

        renderSelect(control, editor) {
            const select = document.createElement('select');
            select.setAttribute('aria-label', control.label);
            for (const option of control.options) {
                const element = document.createElement('option');
                element.value = String(option.value);
                element.textContent = option.label;
                select.appendChild(element);
            }
            select.addEventListener('focus', () => this.beginTransaction());
            select.addEventListener('change', () => {
                this.setControlValue(control, Number(select.value), true, select);
            });
            editor.appendChild(select);
            editor._select = select;
        }

        bindShellEvents() {
            const launcher = this.root.querySelector('.ri-launcher');
            launcher.addEventListener('click', () => this.setOpen(!this.isOpen()));
            this.root.querySelector('[data-action="close"]')
                .addEventListener('click', () => this.setOpen(false));
            this.root.querySelector('[data-action="undo"]')
                .addEventListener('click', () => this.undo());
            this.root.querySelector('[data-action="redo"]')
                .addEventListener('click', () => this.redo());
            this.root.querySelector('[data-action="copy"]')
                .addEventListener('click', () => this.copySettings());
            this.root.querySelector('[data-action="paste"]')
                .addEventListener('click', () => this.pasteSettings());
            this.root.querySelector('[data-action="save-preset"]')
                .addEventListener('click', () => this.savePreset());
            this.root.querySelector('[data-action="delete-preset"]')
                .addEventListener('click', () => this.deletePreset());

            this.search.addEventListener('input', () => this.filter(this.search.value));
            this.presetSelect.addEventListener('change', () => {
                if (this.presetSelect.value) this.applyPreset(this.presetSelect.value);
            });

            const handle = this.root.querySelector('.ri-resize');
            let resize = null;
            handle.addEventListener('pointerdown', event => {
                event.preventDefault();
                resize = {x: event.clientX, width: this.panel.getBoundingClientRect().width};
                handle.setPointerCapture(event.pointerId);
                this.panel.classList.add('is-resizing');
            });
            handle.addEventListener('pointermove', event => {
                if (!resize) return;
                const width = clamp(resize.width + resize.x - event.clientX, 320,
                    Math.min(680, window.innerWidth - 40));
                this.panel.style.setProperty('--ri-width', `${width}px`);
                this.uiState.width = width;
            });
            const endResize = event => {
                if (!resize) return;
                resize = null;
                this.panel.classList.remove('is-resizing');
                if (handle.hasPointerCapture(event.pointerId)) {
                    handle.releasePointerCapture(event.pointerId);
                }
                this.persistUi();
            };
            handle.addEventListener('pointerup', endResize);
            handle.addEventListener('pointercancel', endResize);
        }

        installGlobalEvents() {
            const releaseForwardedMovementKeys = () => {
                for (const code of this.forwardedMovementKeys) {
                    const movementKey = MOVEMENT_KEY_CODES.get(code);
                    if (movementKey !== undefined) {
                        this.module?._voxy_key_event?.(movementKey, 0);
                    }
                }
                this.forwardedMovementKeys.clear();
            };
            window.addEventListener('blur', releaseForwardedMovementKeys);
            document.addEventListener('visibilitychange', () => {
                if (document.hidden) releaseForwardedMovementKeys();
            });
            document.addEventListener('keydown', event => {
                const inside = this.root.contains(event.target);
                if (event.code === 'Backquote' && !event.repeat) {
                    event.preventDefault();
                    event.stopImmediatePropagation();
                    this.setOpen(!this.isOpen());
                    return;
                }
                if (!this.isOpen()) return;
                if (event.key === 'Escape') {
                    event.preventDefault();
                    event.stopImmediatePropagation();
                    this.setOpen(false);
                    return;
                }
                if (event.key === '/' && !inside) {
                    event.preventDefault();
                    event.stopImmediatePropagation();
                    this.search.focus();
                    return;
                }
                if ((event.ctrlKey || event.metaKey) && event.key.toLowerCase() === 'z') {
                    event.preventDefault();
                    event.stopImmediatePropagation();
                    if (event.shiftKey) this.redo();
                    else this.undo();
                    return;
                }
                if ((event.ctrlKey || event.metaKey) && event.key.toLowerCase() === 'y') {
                    event.preventDefault();
                    event.stopImmediatePropagation();
                    this.redo();
                    return;
                }
                const movementKey = MOVEMENT_KEY_CODES.get(event.code);
                if (inside && movementKey !== undefined && !event.ctrlKey
                    && !event.metaKey && !event.altKey
                    && !isTextEntry(event.target)) {
                    // The inspector owns events from its controls, so forward
                    // movement explicitly using the physical key position.
                    // This avoids layout-dependent keyCode values on AZERTY.
                    if (!this.forwardedMovementKeys.has(event.code)) {
                        this.forwardedMovementKeys.add(event.code);
                        this.module?._voxy_key_event?.(movementKey, 1);
                    }
                    event.preventDefault();
                    event.stopImmediatePropagation();
                    return;
                }
                if (inside) event.stopImmediatePropagation();
            }, true);
            document.addEventListener('keyup', event => {
                const movementKey = MOVEMENT_KEY_CODES.get(event.code);
                const inside = this.isOpen() && this.root.contains(event.target);
                const wasForwarded = this.forwardedMovementKeys.delete(event.code);
                if (movementKey !== undefined && (wasForwarded || inside)) {
                    // Always release a movement key that ends inside the panel.
                    // Its keydown may have happened over the canvas before the
                    // user clicked a slider; swallowing only keyup would leave
                    // the engine key stuck down.
                    this.module?._voxy_key_event?.(movementKey, 0);
                    event.preventDefault();
                    event.stopImmediatePropagation();
                    return;
                }
                if (inside) {
                    event.stopImmediatePropagation();
                }
            }, true);
        }

        async connectWhenReady() {
            for (let attempt = 0; attempt < 240; attempt += 1) {
                const probe = this.getEngineNumber('lighting.sunElevation');
                if (Number.isFinite(probe)) {
                    this.connect();
                    return;
                }
                await new Promise(resolve => setTimeout(resolve, 50));
            }
            this.status.textContent = 'Renderer bridge unavailable';
            this.root.classList.add('has-error');
        }

        connect() {
            for (const group of groups) {
                for (const control of group.controls) {
                    let value;
                    if (control.browser) {
                        value = RendererInspector.getStoredRenderScale();
                    } else if (control.type === 'color') {
                        value = ['r', 'g', 'b'].map(channel =>
                            this.getEngineNumber(`${control.key}.${channel}`));
                    } else {
                        value = this.getEngineNumber(control.key);
                    }
                    if (!Array.isArray(value) && !Number.isFinite(value)) {
                        value = control.default ?? 0;
                    }
                    if (Array.isArray(value) && value.some(item => !Number.isFinite(item))) {
                        value = [1, 1, 1];
                    }
                    this.values.set(control.key, clone(value));
                    this.defaults.set(control.key, clone(value));
                    this.updateRow(control);
                }
            }
            this.connected = true;
            this.root.classList.add('is-connected');
            this.status.textContent = 'Live';
            this.refreshPresetOptions();
            this.restorePersistedSettings();
            this.updateResolutionLabel();
            this.pollStatus();
        }

        getEngineNumber(key) {
            if (!this.module?.ccall) return Number.NaN;
            try {
                return this.module.ccall('voxy_renderer_get_number', 'number',
                    ['string'], [key]);
            } catch (_) {
                return Number.NaN;
            }
        }

        setEngineNumber(key, value, commit) {
            if (!this.connected && key !== 'browser.resolutionScale') return false;
            if (!this.module?.ccall) return false;
            return Boolean(this.module.ccall('voxy_renderer_set_number', 'number',
                ['string', 'number', 'number'], [key, value, commit ? 1 : 0]));
        }

        beginTransaction() {
            if (!this.transactionBefore) this.transactionBefore = this.snapshot();
        }

        finishTransaction() {
            if (!this.transactionBefore) return;
            const before = this.transactionBefore;
            this.transactionBefore = null;
            const after = this.snapshot();
            if (JSON.stringify(before) === JSON.stringify(after)) return;
            this.history.push(before);
            if (this.history.length > 80) this.history.shift();
            this.future.length = 0;
            this.updateHistoryButtons();
        }

        setControlValue(control, rawValue, commit, source) {
            if (!control || rawValue == null) return;
            let value = rawValue;
            if (control.type === 'number') {
                value = clamp(Number(value), control.min, control.max);
                if (!Number.isFinite(value)) return;
            } else if (control.type === 'boolean') {
                value = Number(Boolean(Number(value)));
            } else if (control.type === 'color') {
                if (!Array.isArray(value) || value.length !== 3) return;
                value = value.map(Number);
                if (value.some(channel => !Number.isFinite(channel))) return;
                value = value.map(channel => clamp(channel, 0, 1));
            } else if (control.type === 'select') {
                value = Number(value);
                if (!control.options.some(option => option.value === value)) return;
            }
            this.values.set(control.key, clone(value));
            this.sendControl(control, value, commit);
            this.updateRow(control, source);
            if (commit) {
                this.finishTransaction();
                this.schedulePersist();
                if (control.cost) this.toast(
                    control.cost === 'resize' ? 'Resizing render targets…' : 'Rebuilding dependency…');
            }
        }

        sendControl(control, value, commit) {
            if (control.browser) {
                this.onRenderScale?.(value, commit);
                this.updateResolutionLabel();
                return;
            }
            if (control.type === 'color') {
                ['r', 'g', 'b'].forEach((channel, index) =>
                    this.setEngineNumber(`${control.key}.${channel}`, value[index], commit));
            } else {
                this.setEngineNumber(control.key, Number(value), commit);
            }
        }

        updateRow(control, source) {
            const row = this.rows.get(control.key);
            if (!row) return;
            const value = this.values.get(control.key);
            const editor = row.querySelector('.ri-editor');
            if (control.type === 'number') {
                if (source !== editor._slider) editor._slider.value = String(value);
                if (source !== editor._input) {
                    editor._input.value = Number(value).toFixed(decimals(control.step));
                }
                const progress = (Number(value) - control.min) / (control.max - control.min);
                editor._slider.style.setProperty('--ri-progress', `${clamp(progress, 0, 1) * 100}%`);
            } else if (control.type === 'boolean') {
                const checked = Boolean(Number(value));
                editor._button.setAttribute('aria-checked', String(checked));
                editor._button.classList.toggle('is-on', checked);
            } else if (control.type === 'color') {
                const hex = colorToHex(value);
                if (source !== editor._swatch) editor._swatch.value = hex;
                if (source !== editor._hex) editor._hex.value = hex.toUpperCase();
            } else if (control.type === 'select') {
                if (source !== editor._select) editor._select.value = String(value);
            }
            row.classList.toggle('is-modified',
                !valuesEqual(value, this.defaults.get(control.key)));
        }

        snapshot() {
            const settings = {};
            for (const [key, value] of this.values) settings[key] = clone(value);
            return settings;
        }

        applySnapshot(snapshot, options = {}) {
            if (!snapshot || typeof snapshot !== 'object') return;
            if (options.recordHistory !== false) this.beginTransaction();
            const controls = groups.flatMap(group => group.controls);
            const touched = [];
            for (const control of controls) {
                if (!(control.key in snapshot)) continue;
                let value = snapshot[control.key];
                if (control.type === 'number') value = clamp(Number(value), control.min, control.max);
                if (control.type === 'boolean') value = Number(Boolean(Number(value)));
                if (control.type === 'color') {
                    if (!Array.isArray(value) || value.length !== 3) continue;
                    value = value.map(Number);
                    if (value.some(channel => !Number.isFinite(channel))) continue;
                    value = value.map(channel => clamp(channel, 0, 1));
                }
                if (control.type === 'select') {
                    value = Number(value);
                    if (!control.options.some(option => option.value === value)) continue;
                }
                if (NUMBER_TYPES.includes(control.type) && !Number.isFinite(Number(value))) continue;
                if (valuesEqual(value, this.values.get(control.key))) continue;
                this.values.set(control.key, clone(value));
                this.sendControl(control, value, false);
                this.updateRow(control);
                touched.push(control);
            }
            // All commits land in one browser task. C++ coalesces their dirty
            // flags and performs at most one spectrum/shadow/coast rebuild.
            for (const control of touched) {
                this.sendControl(control, this.values.get(control.key), true);
            }
            if (options.recordHistory !== false) this.finishTransaction();
            if (options.persist !== false) this.schedulePersist();
            this.updateResolutionLabel();
        }

        undo() {
            if (!this.history.length) return;
            const previous = this.history.pop();
            this.future.push(this.snapshot());
            this.applySnapshot(previous, {recordHistory: false});
            this.updateHistoryButtons();
            this.toast('Undid renderer edit');
        }

        redo() {
            if (!this.future.length) return;
            const next = this.future.pop();
            this.history.push(this.snapshot());
            this.applySnapshot(next, {recordHistory: false});
            this.updateHistoryButtons();
            this.toast('Redid renderer edit');
        }

        updateHistoryButtons() {
            this.root.querySelector('[data-action="undo"]').disabled = !this.history.length;
            this.root.querySelector('[data-action="redo"]').disabled = !this.future.length;
        }

        builtInPresets() {
            const defaults = Object.fromEntries(this.defaults);
            return {
                default: {label: 'Startup Default', values: defaults},
                calm: {label: 'Calm Water', values: {
                    'water.waveStrength': 0.45,
                    'water.spectrum.significantHeight': 8,
                    'water.spectrum.choppiness': 0.8,
                    'water.spectrum.detailAmplitude': 0.035,
                    'water.foamOpacity': 0.12,
                }},
                storm: {label: 'Heavy Storm', values: {
                    'water.waveStrength': 1.55,
                    'water.spectrum.significantHeight': 42,
                    'water.spectrum.choppiness': 3.6,
                    'water.spectrum.windAlignment': 0.78,
                    'water.spectrum.largeAmplitude': 0.48,
                    'water.spectrum.detailAmplitude': 0.13,
                    'water.foamOpacity': 0.62,
                    'water.foamCoverage': 0.42,
                    'lighting.fogDensity': 0.00028,
                    'lighting.exposure': 0.82,
                }},
                sunset: {label: 'Low Golden Sun', values: {
                    'lighting.sunElevation': 8,
                    'lighting.sunAzimuth': -38,
                    'lighting.sunColor': [1, 0.43, 0.16],
                    'lighting.sunIntensity': 1.7,
                    'lighting.ambientColor': [0.18, 0.23, 0.42],
                    'lighting.ambientIntensity': 0.34,
                    'lighting.fogColor': [0.48, 0.25, 0.20],
                    'lighting.exposure': 1.15,
                }},
            };
        }

        refreshPresetOptions() {
            const selected = this.presetSelect?.value || '';
            this.presetSelect.innerHTML = '<option value="">Presets</option>';
            const builtinGroup = document.createElement('optgroup');
            builtinGroup.label = 'Built-in';
            for (const [id, preset] of Object.entries(this.builtInPresets())) {
                const option = document.createElement('option');
                option.value = `builtin:${id}`;
                option.textContent = preset.label;
                builtinGroup.appendChild(option);
            }
            this.presetSelect.appendChild(builtinGroup);
            const names = Object.keys(this.customPresets).sort((a, b) => a.localeCompare(b));
            if (names.length) {
                const customGroup = document.createElement('optgroup');
                customGroup.label = 'Custom';
                for (const name of names) {
                    const option = document.createElement('option');
                    option.value = `custom:${name}`;
                    option.textContent = name;
                    customGroup.appendChild(option);
                }
                this.presetSelect.appendChild(customGroup);
            }
            if ([...this.presetSelect.options].some(option => option.value === selected)) {
                this.presetSelect.value = selected;
            }
        }

        applyPreset(id) {
            let values;
            if (id.startsWith('builtin:')) values = this.builtInPresets()[id.slice(8)]?.values;
            if (id.startsWith('custom:')) values = this.customPresets[id.slice(7)];
            if (!values) return;
            this.applySnapshot(values);
            this.toast(`Applied ${this.presetSelect.selectedOptions[0]?.textContent || 'preset'}`);
        }

        savePreset() {
            const name = global.prompt('Preset name');
            if (!name?.trim()) return;
            this.customPresets[name.trim()] = this.snapshot();
            localStorage.setItem(PRESET_STORAGE_KEY, JSON.stringify(this.customPresets));
            this.refreshPresetOptions();
            this.presetSelect.value = `custom:${name.trim()}`;
            this.toast(`Saved preset “${name.trim()}”`);
        }

        deletePreset() {
            const id = this.presetSelect.value;
            if (!id.startsWith('custom:')) {
                this.toast('Choose a custom preset to delete', true);
                return;
            }
            const name = id.slice(7);
            delete this.customPresets[name];
            localStorage.setItem(PRESET_STORAGE_KEY, JSON.stringify(this.customPresets));
            this.refreshPresetOptions();
            this.toast(`Deleted preset “${name}”`);
        }

        async copySettings() {
            const text = JSON.stringify({version: 1, settings: this.snapshot()}, null, 2);
            try {
                await navigator.clipboard.writeText(text);
                this.toast('Renderer settings copied');
            } catch (_) {
                global.prompt('Copy renderer settings', text);
            }
        }

        async pasteSettings() {
            let text = '';
            try {
                text = await navigator.clipboard.readText();
            } catch (_) {
                text = global.prompt('Paste renderer settings JSON') || '';
            }
            if (!text) return;
            try {
                const parsed = JSON.parse(text);
                this.applySnapshot(parsed.settings || parsed);
                this.toast('Renderer settings imported');
            } catch (_) {
                this.toast('Invalid settings JSON', true);
            }
        }

        restorePersistedSettings() {
            const persisted = safeParse(STORAGE_KEY, null);
            if (persisted?.settings) {
                this.applySnapshot(persisted.settings,
                    {recordHistory: false, persist: false});
            }
        }

        schedulePersist() {
            clearTimeout(this.persistTimer);
            this.persistTimer = setTimeout(() => {
                localStorage.setItem(STORAGE_KEY, JSON.stringify({
                    version: 1,
                    settings: this.snapshot(),
                }));
            }, 150);
        }

        persistUi() {
            localStorage.setItem(UI_STORAGE_KEY, JSON.stringify(this.uiState));
        }

        filter(query) {
            const needle = query.trim().toLowerCase();
            for (const section of this.root.querySelectorAll('.ri-group')) {
                let matches = 0;
                for (const row of section.querySelectorAll('.ri-row')) {
                    const visible = !needle || row.dataset.search.includes(needle);
                    row.hidden = !visible;
                    if (visible) matches += 1;
                }
                section.hidden = matches === 0;
                section.classList.toggle('is-searching', Boolean(needle));
            }
        }

        isOpen() {
            return this.root.classList.contains('is-open');
        }

        setOpen(open, persist = true) {
            if (!open && this.root.contains(document.activeElement)) {
                // Blurring a numeric/color field emits its change event, which
                // commits any resource-building preview before the panel hides.
                document.activeElement.blur();
            }
            this.root.classList.toggle('is-open', open);
            this.panel.setAttribute('aria-hidden', String(!open));
            this.root.querySelector('.ri-launcher').setAttribute('aria-expanded', String(open));
            if (open && document.pointerLockElement) document.exitPointerLock?.();
            this.uiState.open = open;
            if (persist) this.persistUi();
        }

        updateResolutionLabel() {
            const label = this.root.querySelector('.ri-resolution');
            if (!label || !this.canvas) return;
            label.textContent = `${this.canvas.width} × ${this.canvas.height}`;
        }

        pollStatus() {
            const poll = () => {
                if (!this.root.isConnected) return;
                const revision = this.module?._voxy_renderer_get_revision?.() ?? 0;
                const applied = this.module?._voxy_renderer_get_applied_revision?.() ?? 0;
                const pending = applied < revision;
                this.root.classList.toggle('is-applying', pending);
                this.status.textContent = pending ? 'Applying…' : 'Live';
                this.updateResolutionLabel();
                setTimeout(poll, 250);
            };
            poll();
        }

        toast(message, error = false) {
            const element = this.root.querySelector('.ri-toast');
            element.textContent = message;
            element.classList.toggle('is-error', error);
            element.classList.add('is-visible');
            clearTimeout(this.toastTimer);
            this.toastTimer = setTimeout(() => element.classList.remove('is-visible'), 1800);
        }

        static getStoredRenderScale() {
            const persisted = safeParse(STORAGE_KEY, null);
            const value = Number(persisted?.settings?.['browser.resolutionScale']);
            return Number.isFinite(value) ? clamp(value, 0.35, 1.5) : 1;
        }
    }

    global.VoxyRendererInspector = {
        instance: null,
        mount(options) {
            if (this.instance) return this.instance;
            this.instance = new RendererInspector(options);
            return this.instance;
        },
        getStoredRenderScale: RendererInspector.getStoredRenderScale,
    };
})(window);
