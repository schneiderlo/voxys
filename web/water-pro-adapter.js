/*
 * Voxy Water Pro adapter.
 *
 * This file deliberately does not vendor the licensed threejs-water-pro package.
 * It gives the web build a stable integration point: copy the purchased package
 * into web/vendor/threejs-water-pro and enable it with ?waterpro=1.
 */
(function (global) {
    "use strict";

    const DEFAULT_VENDOR_MODULE = "threejs-water-pro";
    const DEFAULT_NORMALS_URL = "./vendor/threejs-water-pro/textures/waternormals.jpg";

    const state = {
        config: null,
        modulePromise: null,
        module: null,
        waterConstructor: null,
        error: null,
        status: "unconfigured"
    };

    function query() {
        try {
            return new URLSearchParams(global.location ? global.location.search : "");
        } catch (_err) {
            return new URLSearchParams();
        }
    }

    function optionBool(value, fallback) {
        if (value === undefined || value === null || value === "") return fallback;
        if (typeof value === "boolean") return value;
        const normalized = String(value).toLowerCase();
        return normalized === "1" || normalized === "true" || normalized === "yes" || normalized === "on";
    }

    function configure(options = {}) {
        const params = query();
        const enabled = optionBool(params.get("waterpro"), optionBool(options.enabled, false));
        const moduleSpecifier = params.get("waterpro-module") || options.moduleSpecifier || DEFAULT_VENDOR_MODULE;
        const normalsUrl = params.get("waterpro-normals") || options.normalsUrl || DEFAULT_NORMALS_URL;

        state.config = {
            enabled,
            moduleSpecifier,
            normalsUrl,
            fallback: optionBool(options.fallback, true),
            renderer: options.renderer || "voxy-wgsl",
            seaLevel: Number.isFinite(options.seaLevel) ? options.seaLevel : 0.0,
            preset: {
                waterColor: options.waterColor || 0x2e9fc8,
                sunColor: options.sunColor || 0xffffff,
                distortionScale: Number.isFinite(options.distortionScale) ? options.distortionScale : 1.15,
                size: Number.isFinite(options.size) ? options.size : 12000
            }
        };
        state.status = enabled ? "configured" : "disabled";
        state.error = null;
        return getStatus();
    }

    function getWaterConstructor(moduleObject) {
        if (!moduleObject) return null;
        if (typeof moduleObject.Water === "function") return moduleObject.Water;
        if (moduleObject.default && typeof moduleObject.default.Water === "function") return moduleObject.default.Water;
        if (typeof moduleObject.default === "function") return moduleObject.default;
        return null;
    }

    async function load() {
        if (!state.config) configure();
        if (!state.config.enabled) {
            state.status = "disabled";
            return null;
        }
        if (state.module) return state.module;
        if (!state.modulePromise) {
            state.status = "loading";
            state.modulePromise = import(state.config.moduleSpecifier)
                .then((moduleObject) => {
                    const Water = getWaterConstructor(moduleObject);
                    if (!Water) {
                        throw new Error("threejs-water-pro module did not export Water");
                    }
                    state.module = moduleObject;
                    state.waterConstructor = Water;
                    state.status = "ready";
                    return moduleObject;
                })
                .catch((err) => {
                    state.error = err;
                    state.status = "missing";
                    state.modulePromise = null;
                    return null;
                });
        }
        return state.modulePromise;
    }

    function toColor(THREE, value, fallback) {
        if (value && value.isColor) return value;
        return new THREE.Color(value === undefined ? fallback : value);
    }

    function toVector3(THREE, value, fallback) {
        if (value && value.isVector3) return value;
        const source = Array.isArray(value) ? value : fallback;
        return new THREE.Vector3(source[0], source[1], source[2]).normalize();
    }

    function makeFallbackMaterial(THREE, options) {
        const uniforms = {
            time: { value: 0.0 },
            waterColor: { value: toColor(THREE, options.waterColor, 0x2e9fc8) },
            deepColor: { value: toColor(THREE, options.deepColor, 0x063d5c) },
            sunDirection: { value: toVector3(THREE, options.sunDirection, [0.35, 0.82, 0.42]) },
            opacity: { value: Number.isFinite(options.opacity) ? options.opacity : 0.78 },
            distortionScale: { value: Number.isFinite(options.distortionScale) ? options.distortionScale : 1.15 }
        };

        return new THREE.ShaderMaterial({
            uniforms,
            transparent: true,
            depthWrite: false,
            vertexShader: `
                uniform float time;
                uniform float distortionScale;
                varying vec3 vWorldPosition;
                varying vec3 vNormal;

                float wave(vec2 p, vec2 dir, float speed, float scale) {
                    return sin(dot(p, normalize(dir)) * scale + time * speed);
                }

                void main() {
                    vec3 p = position;
                    float h = 0.0;
                    h += wave(p.xz, vec2(1.0, 0.35), 0.62, 0.045) * 2.4;
                    h += wave(p.xz, vec2(-0.25, 1.0), 0.41, 0.071) * 1.1;
                    h += wave(p.xz, vec2(0.72, -0.61), 0.88, 0.118) * 0.42;
                    p.y += h * distortionScale;

                    vec4 world = modelMatrix * vec4(p, 1.0);
                    vWorldPosition = world.xyz;
                    vNormal = normalize(normalMatrix * normal);
                    gl_Position = projectionMatrix * viewMatrix * world;
                }
            `,
            fragmentShader: `
                uniform float time;
                uniform vec3 waterColor;
                uniform vec3 deepColor;
                uniform vec3 sunDirection;
                uniform float opacity;
                varying vec3 vWorldPosition;
                varying vec3 vNormal;

                float wave(vec2 p, vec2 dir, float speed, float scale) {
                    return sin(dot(p, normalize(dir)) * scale + time * speed);
                }

                void main() {
                    vec2 p = vWorldPosition.xz;
                    vec3 n = normalize(vec3(
                        wave(p, vec2(1.0, 0.35), 0.62, 0.045) * 0.17 +
                        wave(p, vec2(0.72, -0.61), 0.88, 0.118) * 0.08,
                        1.0,
                        wave(p, vec2(-0.25, 1.0), 0.41, 0.071) * 0.15
                    ));
                    vec3 viewDir = normalize(cameraPosition - vWorldPosition);
                    vec3 halfDir = normalize(sunDirection + viewDir);
                    float fresnel = pow(1.0 - clamp(dot(n, viewDir), 0.0, 1.0), 5.0);
                    float specular = pow(max(dot(n, halfDir), 0.0), 96.0);
                    float crest = smoothstep(0.72, 1.0, wave(p, vec2(1.0, 0.35), 0.62, 0.045) * 0.5 + 0.5);
                    vec3 base = mix(deepColor, waterColor, 0.65 + crest * 0.25);
                    vec3 color = base + vec3(0.65, 0.84, 0.95) * specular + vec3(0.18, 0.28, 0.32) * fresnel;
                    gl_FragColor = vec4(color, opacity);
                }
            `
        });
    }

    function createFallbackSurface(args) {
        const THREE = args.THREE;
        if (!THREE) throw new Error("createFallbackSurface requires a THREE namespace");

        const options = Object.assign({}, state.config ? state.config.preset : {}, args.options || {});
        const size = Number.isFinite(options.size) ? options.size : 12000;
        const geometry = args.geometry || new THREE.PlaneGeometry(size, size, 128, 128);
        const mesh = args.mesh || new THREE.Mesh(geometry, makeFallbackMaterial(THREE, options));

        if (!args.mesh) {
            mesh.rotation.x = -Math.PI * 0.5;
            mesh.position.y = Number.isFinite(options.seaLevel) ? options.seaLevel : 0.0;
        }

        const tick = (elapsedSeconds) => {
            const material = mesh.material;
            if (material && material.uniforms && material.uniforms.time) {
                material.uniforms.time.value = elapsedSeconds;
            }
        };

        mesh.userData.voxyWater = {
            source: "fallback-threejs-shader",
            tick
        };

        if (args.scene) args.scene.add(mesh);
        return { mesh, material: mesh.material, tick, source: "fallback-threejs-shader" };
    }

    async function createSurface(args = {}) {
        if (!state.config) configure();
        const THREE = args.THREE;
        const options = Object.assign({}, state.config.preset, args.options || {});

        if (state.config.enabled) {
            await load();
            if (state.waterConstructor) {
                const geometry = args.geometry || (THREE ? new THREE.PlaneGeometry(options.size, options.size, 256, 256) : null);
                const sourceMesh = args.mesh || (THREE && geometry ? new THREE.Mesh(geometry) : null);
                if (!sourceMesh) {
                    throw new Error("Water Pro surface requires args.mesh or THREE plus geometry support");
                }
                const water = new state.waterConstructor(sourceMesh, Object.assign({
                    waterNormals: options.waterNormals || state.config.normalsUrl,
                    sunPosition: options.sunPosition || options.sunDirection || [0.35, 0.82, 0.42],
                    waterColor: options.waterColor,
                    sunColor: options.sunColor,
                    distortionScale: options.distortionScale
                }, options.waterPro || {}));

                if (args.scene) args.scene.add(water);
                return { mesh: water, source: "threejs-water-pro", module: state.module };
            }
        }

        if (state.config.fallback) {
            return createFallbackSurface(args);
        }
        throw state.error || new Error("threejs-water-pro is not available");
    }

    function getStatus() {
        return {
            enabled: !!(state.config && state.config.enabled),
            status: state.status,
            renderer: state.config ? state.config.renderer : "unconfigured",
            moduleSpecifier: state.config ? state.config.moduleSpecifier : DEFAULT_VENDOR_MODULE,
            normalsUrl: state.config ? state.config.normalsUrl : DEFAULT_NORMALS_URL,
            error: state.error ? state.error.message : null
        };
    }

    global.VoxyWaterPro = {
        DEFAULT_VENDOR_MODULE,
        DEFAULT_NORMALS_URL,
        configure,
        load,
        createSurface,
        createFallbackSurface,
        status: getStatus
    };
})(window);
