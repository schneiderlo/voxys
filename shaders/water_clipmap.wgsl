// Camera-following displaced ocean clipmap.
// Geometry carries the live two-cascade FFT and four long swells. The
// fragment stage performs exact terrain rejection and the complete optical
// material before writing final color and linear depth together.

struct CameraUniforms {
    viewProj : mat4x4<f32>,
    invViewProj : mat4x4<f32>,
    invView : mat4x4<f32>,
    terrainSize : vec2<f32>,
    invTerrainSize : vec2<f32>,
    metrics : vec4<f32>,
    cameraPos : vec4<f32>,
    invProjParams : vec4<f32>,
    lightDirVS : vec4<f32>,
    frustumPlanes : array<vec4<f32>, 6>,
    lightDirWS : vec4<f32>,
    waterParams : vec4<f32>,
    waterColorA : vec4<f32>,
    waterColorB : vec4<f32>,
    waterMotion : vec4<f32>,
    lightingColor : vec4<f32>,
    ambientExposure : vec4<f32>,
    fogColor : vec4<f32>,
    waterOptics : vec4<f32>,
    waterFoam : vec4<f32>,
    waterSpectrum : vec4<f32>,
};

struct DebugUniforms {
    mode : u32,
    maxDepth : f32,
    padding0 : f32,
    padding1 : f32,
};

@group(0) @binding(0) var<uniform> camera : CameraUniforms;
// The immutable terrain depth distinguishes bed refraction from nearer objects.
@group(0) @binding(1) var terrainDepthTexture : texture_2d<f32>;
@group(0) @binding(6) var sceneSampler : sampler;
@group(0) @binding(7) var<uniform> debug : DebugUniforms;
@group(0) @binding(8) var skyLut : texture_2d<f32>;
@group(0) @binding(9) var foamTexture : texture_2d<f32>;
@group(0) @binding(10) var foamSampler : sampler;
@group(0) @binding(11) var sceneColorTexture : texture_2d<f32>;
@group(0) @binding(12) var sceneDepthTexture : texture_2d<f32>;
@group(0) @binding(13) var heightTexture : texture_2d<u32>;
@group(0) @binding(14) var shadowHeightTexture : texture_2d<u32>;
@group(0) @binding(15) var displacementTexture : texture_2d_array<f32>;
@group(0) @binding(16) var displacementSampler : sampler;

// Baked on this device with the original hash; no filtering or quantization.
@group(0) @binding(19) var periodicGradientLut : texture_2d<f32>;
// Test pipelines may specialize this to false for an unchanged reference.
override USE_PERIODIC_GRADIENT_LUT : bool = true;
// The authored opaque scene uses the air/water interface Fresnel even over a
// shallow bed. Legacy routes retain their historical shoreline presentation.
override OPAQUE_SCENE_WATER : bool = false;

const TAU : f32 = 6.283185307179586;
const WATER_RESOLUTION : f32 = 256.0;
const NORMAL_LAYER : i32 = 2;
const SKY_LUT_WIDTH : f32 = 1774.0;
const SKY_LUT_HEIGHT : f32 = 887.0;

const OCEAN_BASE_ABSORPTION : vec3<f32> =
    vec3<f32>(0.0580, 0.0290, 0.0120);
const OCEAN_SKY_BRIGHTNESS : f32 = 0.62;
const OCEAN_REFLECTION_ROUGHNESS_STRENGTH : f32 = 0.50;
const OCEAN_FILM_GRAIN : f32 = 0.015;
const OCEAN_VIGNETTE : f32 = 0.25;
const OCEAN_VIGNETTE_SMOOTHNESS : f32 = 0.85;
const OCEAN_UNDERWATER_DISTORTION : f32 = 0.015;
const OCEAN_UNDERWATER_SCALE : f32 = 4.0;
const OCEAN_UNDERWATER_SPEED : f32 = 1.2;
const OCEAN_PROCEDURAL_SEABED_DEPTH : f32 = 100.0;

fn oceanAbsorption() -> vec3<f32> {
    return OCEAN_BASE_ABSORPTION * camera.waterOptics.z;
}
fn oceanSurfaceColor() -> vec3<f32> { return camera.waterColorA.rgb; }
fn oceanScatterColor() -> vec3<f32> {
    return camera.waterColorB.rgb * camera.waterOptics.w;
}
fn oceanFogColor() -> vec3<f32> { return camera.fogColor.rgb; }
fn oceanSunIntensity() -> f32 { return 2.5 * camera.lightingColor.w; }
fn oceanIor() -> f32 { return camera.waterOptics.x; }
fn oceanDistortion() -> f32 { return camera.waterOptics.y; }
fn oceanReflectionDistance() -> f32 { return camera.waterFoam.w; }
fn oceanMinimumRoughness() -> f32 { return camera.waterParams.w; }
fn oceanFoamSize() -> f32 { return camera.waterFoam.x; }
fn oceanFoamOpacity() -> f32 { return camera.waterFoam.y; }
fn oceanFoamCoverage() -> f32 { return camera.waterFoam.z; }
fn atmosphericFog(distanceToCamera : f32) -> f32 {
    return clamp(1.0 - exp(-max(camera.metrics.w, 0.0) * distanceToCamera),
                 0.0, 0.98);
}

struct VertexOutput {
    @builtin(position) position : vec4<f32>,
    @location(0) worldPosition : vec3<f32>,
    @location(1) waterCoordinates : vec2<f32>,
    @location(2) lowFrequencyNormal : vec3<f32>,
    @location(3) crestCompression : f32,
};

struct LongWave {
    displacement : vec3<f32>,
    normalVector : vec3<f32>,
};

fn cascadeUv(position : vec2<f32>, scale : f32) -> vec2<f32> {
    return position / scale + vec2<f32>(0.5 + 0.5 / WATER_RESOLUTION);
}

fn sampleDisplacement(position : vec2<f32>, scale : f32,
                      layer : i32) -> vec4<f32> {
    return textureSampleLevel(displacementTexture, displacementSampler,
                              cascadeUv(position, scale), layer, 0.0);
}

fn oneLongWave(position : vec2<f32>, wave : vec4<f32>,
               dynamics : vec3<f32>, strength : f32) -> LongWave {
    let waveNumber = TAU / (wave.w + 1.0e-4);
    let phase = waveNumber * dot(wave.xy, position) -
                dynamics.z * camera.waterMotion.x + dynamics.y;
    let sine = sin(phase);
    let cosine = cos(phase);
    let amplitude = wave.z * strength;
    let ka = waveNumber * amplitude;
    return LongWave(
        vec3<f32>(-dynamics.x * amplitude * wave.x * sine,
                  amplitude * cosine,
                  -dynamics.x * amplitude * wave.y * sine),
        vec3<f32>(wave.x * ka * sine,
                  -dynamics.x * ka * cosine,
                  wave.y * ka * sine));
}

fn longWaves(position : vec2<f32>, strength : f32) -> LongWave {
    let a = oneLongWave(
        position, vec4<f32>(0.923059017, 0.384658357, 5.1541, 440.298507),
        vec3<f32>(1.0, 0.000000000, 0.374291312), strength);
    let b = oneLongWave(
        position, vec4<f32>(0.700400636, 0.713749921, 5.1541, 701.258144),
        vec3<f32>(1.0, 5.553108549, 0.296825282), strength);
    let c = oneLongWave(
        position, vec4<f32>(0.367164395, 0.930156066, 5.1541, 1116.885424),
        vec3<f32>(1.0, 4.823031791, 0.234699061), strength);
    let d = oneLongWave(
        position, vec4<f32>(-0.024039031, 0.999711021, 5.1541, 1778.85),
        vec3<f32>(1.0, 4.092955033, 0.186378666), strength);
    return LongWave(
        a.displacement + b.displacement + c.displacement + d.displacement,
        vec3<f32>(0.0, 1.0, 0.0) +
            a.normalVector + b.normalVector + c.normalVector + d.normalVector);
}

@vertex
fn vs(@location(0) localPosition : vec3<f32>) -> VertexOutput {
    // Follow the outer regular level's 200-unit grid so every nested ring and
    // the radially stretched far ring remain continuous while the camera moves.
    let outerSpacing = (800.0 * 16.0) / 64.0;
    let center = floor(camera.cameraPos.xz / outerSpacing) * outerSpacing;
    let base = vec3<f32>(localPosition.x + center.x,
                         camera.waterParams.x,
                         localPosition.z + center.y);
    let strength = clamp(camera.waterParams.z, 0.0, 2.0);
    let spectral =
        sampleDisplacement(base.xz, camera.waterSpectrum.x, 0).xyz +
        sampleDisplacement(base.xz, camera.waterSpectrum.y, 1).xyz;
    let swell = longWaves(base.xz, strength);
    let world = base + spectral * strength + swell.displacement;

    var output : VertexOutput;
    output.position = camera.viewProj * vec4<f32>(world, 1.0);
    output.worldPosition = world;
    output.waterCoordinates = base.xz;
    let broadWave = sampleDisplacement(
        base.xz, camera.waterSpectrum.x, NORMAL_LAYER);
    output.lowFrequencyNormal = broadWave.xyz +
        vec3<f32>(swell.normalVector.x,
                  swell.normalVector.y - 1.0,
                  swell.normalVector.z);
    output.crestCompression = broadWave.w;
    return output;
}

fn heightmapWorldHeight(encoded : u32) -> f32 {
    return ((f32(encoded) / 65535.0) * 2.0 - 1.0) * camera.metrics.x;
}

fn terrainSampleCoordinate(worldXZ : vec2<f32>) -> vec2<f32> {
    let cellCounts = max(camera.terrainSize - vec2<f32>(1.0), vec2<f32>(1.0));
    let origin = 0.5 * cellCounts * camera.metrics.y;
    return (worldXZ + origin) / camera.metrics.y;
}

fn bilinearTerrainHeight(sampleCoordinate : vec2<f32>) -> f32 {
    let dimensions = vec2<i32>(textureDimensions(heightTexture));
    let maximum = dimensions - vec2<i32>(1);
    let base = clamp(
        vec2<i32>(floor(sampleCoordinate)),
        vec2<i32>(0), maximum);
    let next = min(base + vec2<i32>(1), maximum);
    let fraction = clamp(fract(sampleCoordinate), vec2<f32>(0.0),
                         vec2<f32>(1.0));
    let h00 = heightmapWorldHeight(
        textureLoad(heightTexture, base, 0).x);
    let h10 = heightmapWorldHeight(
        textureLoad(heightTexture, vec2<i32>(next.x, base.y), 0).x);
    let h01 = heightmapWorldHeight(
        textureLoad(heightTexture, vec2<i32>(base.x, next.y), 0).x);
    let h11 = heightmapWorldHeight(
        textureLoad(heightTexture, next, 0).x);
    return mix(
        mix(h00, h10, fraction.x),
        mix(h01, h11, fraction.x),
        fraction.y);
}

fn bakedShadow(worldPosition : vec3<f32>) -> f32 {
    let terrainExtent = max(camera.terrainSize - vec2<f32>(1.0),
                            vec2<f32>(1.0));
    let origin = 0.5 * terrainExtent * camera.metrics.y;
    let dimensions = vec2<i32>(textureDimensions(shadowHeightTexture));
    let heightCell = (worldPosition.xz + origin) / camera.metrics.y;
    let scale = vec2<f32>(dimensions) / camera.terrainSize;
    let cell = clamp(vec2<i32>(floor(heightCell * scale)), vec2<i32>(0),
                     dimensions - vec2<i32>(1));
    let boundary = f32(textureLoad(shadowHeightTexture, cell, 0).x);
    let encodedY = ((worldPosition.y / camera.metrics.x) * 0.5 + 0.5) *
                   65535.0;
    let softness = max((1.5 / camera.metrics.x) * 0.5 * 65535.0, 1.0);
    return smoothstep(-softness, softness, encodedY - boundary);
}

fn environmentUv(directionInput : vec3<f32>) -> vec2<f32> {
    let direction = normalize(directionInput);
    let longitude = atan2(direction.z, direction.x);
    let latitude = atan2(clamp(direction.y, -1.0, 1.0),
                         length(direction.xz));
    let raw = vec2<f32>(longitude / TAU + 0.5,
                        0.5 - latitude / 3.141592653589793);
    let halfTexel = vec2<f32>(0.5 / SKY_LUT_WIDTH,
                              0.5 / SKY_LUT_HEIGHT);
    return clamp(raw, halfTexel, vec2<f32>(1.0) - halfTexel);
}

fn sampleEnvironment(direction : vec3<f32>, roughness : f32) -> vec3<f32> {
    let lod = log2(max(roughness * 0.035 * SKY_LUT_WIDTH, 1.0));
    return textureSampleLevel(skyLut, sceneSampler,
                              environmentUv(direction), lod).rgb *
           OCEAN_SKY_BRIGHTNESS;
}

fn dielectricFresnel(cosine : f32, eta : f32) -> f32 {
    let enteringFromBelow = cosine < 0.0;
    let incident = abs(cosine);
    let ratio = select(eta, 1.0 / eta, enteringFromBelow);
    let transmittedSquared =
        (1.0 - incident * incident) / (ratio * ratio);
    if (transmittedSquared >= 1.0) {
        return 1.0;
    }
    let transmitted = sqrt(max(0.0, 1.0 - transmittedSquared));
    let a = ratio * incident;
    let b = ratio * transmitted;
    let parallel = (a - transmitted) / max(a + transmitted, 1.0e-5);
    let perpendicular = (incident - b) / max(incident + b, 1.0e-5);
    return 0.5 * (parallel * parallel + perpendicular * perpendicular);
}

fn acesFilmic(inputColor : vec3<f32>) -> vec3<f32> {
    let inputMatrix = mat3x3<f32>(
        vec3<f32>(0.59719, 0.07600, 0.02840),
        vec3<f32>(0.35458, 0.90834, 0.13383),
        vec3<f32>(0.04823, 0.01566, 0.83777));
    let outputMatrix = mat3x3<f32>(
        vec3<f32>(1.60475, -0.10208, -0.00327),
        vec3<f32>(-0.53108, 1.10813, -0.07276),
        vec3<f32>(-0.07367, -0.00605, 1.07602));
    let color = inputMatrix * (inputColor / 0.6);
    let a = color * (color + vec3<f32>(0.0245786)) -
            vec3<f32>(0.000090537);
    let b = color * ((color + vec3<f32>(0.432951)) * 0.983729) +
            vec3<f32>(0.238081);
    return clamp(outputMatrix * (a / b), vec3<f32>(0.0), vec3<f32>(1.0));
}

fn linearToSrgb(linear : vec3<f32>) -> vec3<f32> {
    let low = linear * 12.92;
    let high = 1.055 * pow(max(linear, vec3<f32>(0.0)),
                           vec3<f32>(1.0 / 2.4)) - vec3<f32>(0.055);
    return select(high, low, linear <= vec3<f32>(0.0031308));
}

fn presentColor(colorInput : vec3<f32>, uv : vec2<f32>,
                dimensions : vec2<u32>) -> vec3<f32> {
    let pixel = uv * vec2<f32>(f32(max(dimensions.x, 1u)),
                               f32(max(dimensions.y, 1u)));
    let grain = fract(52.9829189 *
        fract(dot(pixel + vec2<f32>(camera.waterMotion.x),
                  vec2<f32>(0.06711056, 0.00583715))));
    var color = acesFilmic(colorInput * camera.ambientExposure.w) *
        (1.0 + OCEAN_FILM_GRAIN * clamp(grain + 0.1, 0.0, 1.0));
    let radius = length(uv - vec2<f32>(0.5)) * 2.0;
    let vignette = smoothstep(1.0 - OCEAN_VIGNETTE_SMOOTHNESS,
                              1.0, radius);
    color *= 1.0 - vignette * OCEAN_VIGNETTE;
    return linearToSrgb(color);
}

fn periodicGradientHash(cellInput : vec2<f32>) -> vec2<f32> {
    let cell = cellInput - floor(cellInput / 16.0) * 16.0;
    if (USE_PERIODIC_GRADIENT_LUT) {
        let coordinate = vec2<i32>(i32(cell.x) * 2, i32(cell.y));
        return textureLoad(periodicGradientLut, coordinate, 0).xy;
    }
    let phase = vec2<f32>(dot(cell, vec2<f32>(127.1, 311.7)),
                          dot(cell, vec2<f32>(269.5, 183.3)));
    return fract(sin(phase) * 43758.5453123) * 2.0 - vec2<f32>(1.0);
}

fn periodicGradientNoise(point : vec2<f32>) -> f32 {
    let cell = floor(point);
    let local = fract(point);
    let fade = local * local * local *
               (local * (local * 6.0 - vec2<f32>(15.0)) + vec2<f32>(10.0));
    if (USE_PERIODIC_GRADIENT_LUT) {
        let wrapped = cell - floor(cell / 16.0) * 16.0;
        let coordinate = vec2<i32>(i32(wrapped.x) * 2, i32(wrapped.y));
        let ab = textureLoad(periodicGradientLut, coordinate, 0);
        let cd = textureLoad(periodicGradientLut, coordinate + vec2<i32>(1, 0), 0);
        let a = dot(ab.xy, local);
        let b = dot(ab.zw, local - vec2<f32>(1.0, 0.0));
        let c = dot(cd.xy, local - vec2<f32>(0.0, 1.0));
        let d = dot(cd.zw, local - vec2<f32>(1.0));
        return mix(mix(a, b, fade.x), mix(c, d, fade.x), fade.y);
    }
    let a = dot(periodicGradientHash(cell), local);
    let b = dot(periodicGradientHash(cell + vec2<f32>(1.0, 0.0)),
                local - vec2<f32>(1.0, 0.0));
    let c = dot(periodicGradientHash(cell + vec2<f32>(0.0, 1.0)),
                local - vec2<f32>(0.0, 1.0));
    let d = dot(periodicGradientHash(cell + vec2<f32>(1.0)),
                local - vec2<f32>(1.0));
    return mix(mix(a, b, fade.x), mix(c, d, fade.x), fade.y);
}

fn underwaterDistortionUv(uv : vec2<f32>) -> vec2<f32> {
    let motion = camera.waterMotion.x * OCEAN_UNDERWATER_SPEED;
    let first = vec2<f32>(uv.x * OCEAN_UNDERWATER_SCALE + motion,
                          uv.y * OCEAN_UNDERWATER_SCALE + motion * 0.7);
    let second = vec2<f32>(uv.x * OCEAN_UNDERWATER_SCALE + motion * 0.8,
                           uv.y * OCEAN_UNDERWATER_SCALE - motion * 0.5);
    let edgeFade = smoothstep(0.0, 0.1, uv.x) *
                   (1.0 - smoothstep(0.9, 1.0, uv.x)) *
                   smoothstep(0.0, 0.1, uv.y) *
                   (1.0 - smoothstep(0.9, 1.0, uv.y));
    let offset = vec2<f32>(periodicGradientNoise(first),
                           periodicGradientNoise(second));
    return clamp(uv + offset * OCEAN_UNDERWATER_DISTORTION * edgeFade,
                 vec2<f32>(0.001), vec2<f32>(0.999));
}

fn proceduralSeabed(worldXZ : vec2<f32>, pathLength : f32,
                     worldPerPixel : f32) -> vec3<f32> {
    let rotated = vec2<f32>(worldXZ.x * 0.82 + worldXZ.y * 0.57,
                            worldXZ.y * 0.82 - worldXZ.x * 0.57);
    let materialLod = clamp(log2(max(
        worldPerPixel * 1024.0 / 200.0, 1.0)), 0.0, 10.0);
    let material = textureSampleLevel(
        foamTexture, foamSampler, rotated / 200.0, materialLod);
    let albedo = material.gba;
    // Beyond the existing fade endpoint this sample contributes exactly zero.
    if (pathLength >= 360.0) { return albedo * 0.86; }
    let causticScale = 46.0;
    let causticLod = clamp(log2(max(
        worldPerPixel * 1024.0 / causticScale, 1.0)), 0.0, 10.0);
    let motion = camera.waterMotion.x * vec2<f32>(0.017, -0.011);
    let causticPattern = textureSampleLevel(
        foamTexture, foamSampler, rotated / causticScale + motion,
        causticLod).r;
    let distanceFade = 1.0 - smoothstep(95.0, 360.0, pathLength);
    let caustic = smoothstep(0.53, 0.78, causticPattern) * distanceFade;
    return albedo * (0.86 + caustic * 0.78) +
           vec3<f32>(0.09, 0.18, 0.14) * caustic;
}

fn coastalFoamStrength(
    position : vec3<f32>,
    normal : vec3<f32>,
    waterDepth : f32,
    crestCompression : f32,
    distanceToCamera : f32,
    worldPerPixel : f32) -> f32 {
    let crestEnergy = max(
        smoothstep(0.045, 0.20, crestCompression),
        smoothstep(0.018, 0.16, 1.0 - normal.y));
    // Above this depth all shoreline terms are exactly zero (including the
    // wreck-contact term in the legacy path). Preserve compressed open foam.
    if (waterDepth >= 0.92 && crestEnergy == 0.0) { return 0.0; }

    let foamLod = log2(max(
        worldPerPixel * 1024.0 / oceanFoamSize(), 1.0));
    let drift = vec2<f32>(
        camera.waterMotion.x * 0.0017,
        -camera.waterMotion.x * 0.0011);
    let foamUv = position.xz / oceanFoamSize() + drift;
    let pattern = textureSampleLevel(
        foamTexture, foamSampler, foamUv, foamLod).r;
    if (waterDepth >= 0.92) {
        let threshold = 1.0 - oceanFoamCoverage();
        let openCoverage = pattern *
            smoothstep(threshold, threshold + 0.15, pattern);
        return clamp(openCoverage * oceanFoamOpacity() * crestEnergy * 0.55,
                     0.0, 1.0);
    }
    let detail = textureSampleLevel(
        foamTexture, foamSampler,
        foamUv * 2.37 + vec2<f32>(0.31, 0.67) - drift * 0.7,
        foamLod + 0.8).r;
    let depthWindow =
        smoothstep(0.07, 0.24, waterDepth) *
        (1.0 - smoothstep(0.48, 0.92, waterDepth));
    let proximity =
        1.0 - smoothstep(0.72, 1.75, waterDepth);
    let breakingPhase = 0.5 + 0.5 * sin(
        waterDepth * 1.17 -
        camera.waterMotion.x * 1.43 +
        dot(position.xz, vec2<f32>(0.052, 0.023)) +
        pattern * 3.2);
    let pulse = smoothstep(0.34, 0.76, breakingPhase);
    let spatialBreakup =
        smoothstep(0.56, 0.79, pattern) *
        mix(0.06, 1.0, smoothstep(0.50, 0.82, detail));
    let breakerExposure =
        smoothstep(0.08, 0.58, crestEnergy);
    let shoreBreaker =
        proximity * depthWindow *
        breakerExposure *
        mix(0.24, 1.0, pulse) * spatialBreakup;
    let swashResidue =
        proximity *
        (1.0 - smoothstep(0.16, 0.62, waterDepth)) *
        spatialBreakup * breakerExposure *
        mix(0.05, 0.25, pulse);
    let threshold = 1.0 - oceanFoamCoverage();
    let openCoverage = pattern *
        smoothstep(threshold, threshold + 0.15, pattern);
    // Open-water foam needs real compression. A coverage-only floor turned
    // bright sky reflections into a continuous ice-like sheet.
    let openFoam = openCoverage * oceanFoamOpacity() *
                   crestEnergy * 0.55;
    let shoreOpacity =
        max(oceanFoamOpacity(), 0.68);
    return clamp(
        max(openFoam,
            max(shoreBreaker, swashResidue) * shoreOpacity),
        0.0, 1.0);
}

fn debugColor(color : vec3<f32>, depth : f32,
              normal : vec3<f32>) -> vec3<f32> {
    switch (debug.mode) {
        case 1u: {
            let maximum = select(5000.0, debug.maxDepth,
                                 debug.maxDepth > 0.0);
            let d = clamp(depth / maximum, 0.0, 1.0);
            return vec3<f32>(1.0 - d * 0.5,
                             max(1.0 - d, 0.0),
                             max(1.0 - d * 1.5, 0.0));
        }
        case 2u: { return normal * 0.5 + 0.5; }
        case 3u: {
            let d = clamp(depth / 1000.0, 0.0, 1.0);
            return vec3<f32>(d, 1.0 - d, 1.0);
        }
        default: { return color; }
    }
}

struct FragmentOutput {
    @location(0) color : vec4<f32>,
    @location(1) linearDepth : f32,
};

fn shadeWaterFragment(input : VertexOutput) -> FragmentOutput {
    if (camera.waterParams.y <= 0.5) {
        discard;
    }

    let dimensions = textureDimensions(sceneDepthTexture, 0);
    let dimensionsF = vec2<f32>(dimensions);
    let maxPixel = vec2<i32>(i32(dimensions.x) - 1,
                             i32(dimensions.y) - 1);
    let pixel = clamp(vec2<i32>(floor(input.position.xy)),
                      vec2<i32>(0), maxPixel);
    let screenUv = (vec2<f32>(pixel) + vec2<f32>(0.5)) / dimensionsF;
    let toCamera = camera.cameraPos.xyz - input.worldPosition;
    let squaredDistance = max(dot(toCamera, toCamera), 1.0e-12);
    let inverseDistance = inverseSqrt(squaredDistance);
    let distanceToCamera = squaredDistance * inverseDistance;
    let view = toCamera * inverseDistance;
    let opaqueDepth = textureLoad(sceneDepthTexture, pixel, 0).x;
    if (opaqueDepth > 0.0 && opaqueDepth + 1.0e-3 < distanceToCamera) {
        discard;
    }

    let baseDimensions = vec2<i32>(textureDimensions(heightTexture));
    let heightCoordinate =
        terrainSampleCoordinate(input.worldPosition.xz);
    let insideTerrain =
        all(heightCoordinate >= vec2<f32>(0.0)) &&
        all(heightCoordinate <=
            vec2<f32>(baseDimensions - vec2<i32>(1)));
    var waterDepth = 500.0;
    var shadow = 1.0;
    if (insideTerrain) {
        let terrainHeight =
            bilinearTerrainHeight(heightCoordinate);
        if (terrainHeight >= input.worldPosition.y - 0.05) {
            discard;
        }
        waterDepth = clamp(input.worldPosition.y - terrainHeight,
                           0.05, 500.0);
        shadow = bakedShadow(input.worldPosition);
    }

    let detailWeight = 1.0 - smoothstep(
        900.0, 3500.0, distanceToCamera);
    var detailWave = vec4<f32>(0.0, 1.0, 0.0, 0.0);
    if (detailWeight > 0.0) {
        detailWave = sampleDisplacement(
            input.waterCoordinates, camera.waterSpectrum.y,
            NORMAL_LAYER + 1);
    }
    var normal = input.lowFrequencyNormal +
                 vec3<f32>(detailWave.x, detailWave.y - 1.0,
                           detailWave.z) * detailWeight * 0.48;
    normal = vec3<f32>(normal.x * 0.72, normal.y, normal.z * 0.72);
    if (dot(normal, normal) > 1.0e-12) {
        normal = normalize(normal);
    } else {
        normal = vec3<f32>(0.0, 1.0, 0.0);
    }

    let cameraUnderwater = camera.waterMotion.z > 0.5;
    var transmissionDirection = vec3<f32>(0.0);
    var totalInternalReflection = false;
    if (cameraUnderwater) {
        transmissionDirection = refract(-view, -normal, oceanIor());
        totalInternalReflection = dot(transmissionDirection, transmissionDirection) < 0.001;
    }
    let light = normalize(camera.lightDirWS.xyz);
    let fresnel = dielectricFresnel(dot(normal, view), oceanIor());
    let reflected = reflect(-view, normal);
    let reflectionRoughness = max(oceanMinimumRoughness(), 0.18) +
        clamp(distanceToCamera / oceanReflectionDistance(),
              0.0, 1.0) * OCEAN_REFLECTION_ROUGHNESS_STRENGTH;
    var environment = vec3<f32>(0.0);
    if (!totalInternalReflection) {
        environment = sampleEnvironment(reflected, reflectionRoughness) *
                      camera.waterColorA.w;
    }

    let halfway = normalize(light + view);
    let sunSpecular = pow(max(dot(normal, halfway), 0.0), 420.0) *
                      oceanSunIntensity() * 1.8;

    let worldPerPixel = distanceToCamera * 2.0 *
        max(camera.invProjParams.x / f32(max(dimensions.x, 1u)),
            camera.invProjParams.y / f32(max(dimensions.y, 1u)));

    var hasOpaqueRefraction = false;
    var thickness = 0.0;
    var refracted = vec3<f32>(0.0);
    // A totally reflected underwater ray cannot use scene refraction. Avoid
    // its depth reads, UV distortion and fallback material evaluation entirely.
    if (!totalInternalReflection) {
        let distortionCoverage = smoothstep(0.04, 0.80, waterDepth);
        var distortedUv = screenUv;
        var selectedRefractionDepth = opaqueDepth;
        let sceneThickness = select(
            500.0, max(opaqueDepth - distanceToCamera, 0.0), opaqueDepth > 0.0);
        if (cameraUnderwater || opaqueDepth > 0.0) {
            let distortionDistance = min(sceneThickness, 80.0) *
                                     oceanDistortion() * distortionCoverage;
            let distortedWorld = input.worldPosition +
                                 normal * distortionDistance;
            let distortedClip = camera.viewProj *
                                vec4<f32>(distortedWorld, 1.0);
            if (distortedClip.w > 1.0e-5) {
                let ndc = distortedClip.xy / distortedClip.w;
                let candidateUv = clamp(
                    vec2<f32>(ndc.x * 0.5 + 0.5,
                              0.5 - ndc.y * 0.5),
                    vec2<f32>(0.001), vec2<f32>(0.999));
                let candidatePixel = clamp(
                    vec2<i32>(floor(candidateUv * dimensionsF)),
                    vec2<i32>(0), maxPixel);
                let candidateDepth = textureLoad(
                    sceneDepthTexture, candidatePixel, 0).x;
                let candidateWaterDepth = distance(
                    camera.cameraPos.xyz, distortedWorld);
                if (candidateDepth < 0.0 ||
                    candidateDepth + 1.0e-3 >= candidateWaterDepth) {
                    distortedUv = candidateUv;
                    selectedRefractionDepth = candidateDepth;
                }
            }
        }

        var refractionUv = distortedUv;
        if (cameraUnderwater) {
            refractionUv = clamp(
                distortedUv + underwaterDistortionUv(screenUv) - screenUv,
                vec2<f32>(0.001), vec2<f32>(0.999));
            let refractionPixel = clamp(
                vec2<i32>(floor(refractionUv * dimensionsF)),
                vec2<i32>(0), maxPixel);
            selectedRefractionDepth = textureLoad(
                sceneDepthTexture, refractionPixel, 0).x;
        }
        let incomingRay = -view;
        var refractedRay = refract(incomingRay, normal, 1.0 / oceanIor());
        if (dot(refractedRay, refractedRay) < 1.0e-6) {
            refractedRay = incomingRay;
        } else {
            refractedRay = normalize(refractedRay);
        }
        hasOpaqueRefraction = selectedRefractionDepth > 0.0;
        // Use the continuous heightfield depth wherever the surface lies inside
        // the terrain domain. Whether a distorted screen ray lands on an opaque
        // texel must not change the optical thickness; that created hard turquoise
        // islands at refraction silhouettes.
        let bedDepth = select(OCEAN_PROCEDURAL_SEABED_DEPTH, waterDepth,
                              insideTerrain);
        let bedTravel = bedDepth / max(-refractedRay.y, 0.12);
        thickness = clamp(bedTravel, 0.0, 500.0);
        if (hasOpaqueRefraction) {
            // Terrain retains the continuous bed thickness above. An authored
            // object in front of that bed bounds the water path at its surface.
            // Reconstruct the selected radial-depth sample with its own ray;
            // subtracting distances from different camera rays is not a length.
            let refractionPixel = clamp(vec2<i32>(refractionUv * dimensionsF), vec2<i32>(0), maxPixel);
            let terrainDepth = textureLoad(terrainDepthTexture, refractionPixel, 0).x;
            if (!cameraUnderwater && (terrainDepth < 0.0 || selectedRefractionDepth + 0.02 < terrainDepth)) {
                let selectedUv = (vec2<f32>(refractionPixel) + vec2<f32>(0.5)) / dimensionsF;
                let ndc = vec2<f32>(selectedUv.x * 2.0 - 1.0, 1.0 - selectedUv.y * 2.0);
                let viewRay = normalize(vec3<f32>(ndc * camera.invProjParams.xy, 1.0));
                let objectPosition = (camera.invView * vec4<f32>(viewRay * selectedRefractionDepth, 1.0)).xyz;
                thickness = min(thickness, distance(input.worldPosition, objectPosition));
            }
            refracted = textureSampleLevel(
                sceneColorTexture, sceneSampler, refractionUv, 0.0).rgb;
        } else {
            // The fallback was previously evaluated and then overwritten.
            refracted = proceduralSeabed(
                (input.worldPosition + refractedRay * bedTravel).xz,
                bedTravel, worldPerPixel) *
                (0.34 + 0.66 * max(light.y, 0.0)) * shadow;
        }

    }

    let crestCompression = max(
        input.crestCompression,
        detailWave.w * detailWeight);
    let foamStrength = coastalFoamStrength(
        input.worldPosition, normal, waterDepth,
        crestCompression, distanceToCamera, worldPerPixel) *
        smoothstep(0.04, 0.18, waterDepth);

    var color : vec3<f32>;
    if (cameraUnderwater) {
        var underside : vec3<f32>;
        if (totalInternalReflection) {
            underside =
                oceanScatterColor() * 0.82 +
                proceduralSeabed(
                    input.worldPosition.xz + reflected.xz * 18.0,
                    max(distanceToCamera, 1.0), worldPerPixel) * 0.18;
        } else {
            let transmittedEnvironment = sampleEnvironment(
                normalize(transmissionDirection), reflectionRoughness);
            var transmitted = select(
                transmittedEnvironment,
                mix(transmittedEnvironment, refracted, 0.75),
                hasOpaqueRefraction);
            let interfacePath = distanceToCamera * 1.55 + 8.0;
            let interfaceTransmittance =
                exp(-oceanAbsorption() * interfacePath);
            transmitted =
                transmitted * interfaceTransmittance +
                oceanScatterColor() *
                (vec3<f32>(1.0) - interfaceTransmittance);
            underside = mix(transmitted, environment * 0.72, fresnel);
        }
        underside += camera.lightingColor.rgb * sunSpecular;
        underside = mix(underside, vec3<f32>(0.94, 0.98, 1.0),
                        foamStrength * 0.22);
        color = mix(oceanScatterColor(), underside,
                    exp(-oceanAbsorption() *
                        distanceToCamera * 1.35));
    } else {
        let scatterLobe =
            pow(clamp((dot(view, -light) + 0.5) / 1.5, 0.0, 1.0) *
                clamp(dot(normal, -light) + 0.3, 0.0, 1.0), 0.85) *
            oceanSunIntensity() * 0.35 *
            (1.0 - smoothstep(100.0, 6400.0, distanceToCamera));
        let body = mix(oceanScatterColor(), oceanSurfaceColor(),
                       clamp(scatterLobe, 0.0, 1.0));
        let forwardScatter = pow(max(dot(view, -light), 0.0), 3.0) *
                             max(1.0 - normal.y, 0.0);
        let transmittance = exp(-oceanAbsorption() * thickness);
        let clearRefracted = refracted * transmittance +
                             body * (vec3<f32>(1.0) - transmittance);
        let coastalTurbidity =
            smoothstep(0.62, 1.70, waterDepth) *
            (1.0 - smoothstep(6.5, 11.0, waterDepth)) * 0.08;
        let refractedWater =
            mix(clearRefracted, body, coastalTurbidity);
        let reflectedWater = environment +
            camera.lightingColor.rgb * sunSpecular +
            oceanScatterColor() * camera.lightingColor.rgb *
            forwardScatter * oceanSunIntensity();

        let opticalCoverage = select(smoothstep(0.12, 12.0, waterDepth),
                                     1.0, OPAQUE_SCENE_WATER);
        color = mix(refractedWater, reflectedWater,
                    fresnel * opticalCoverage *
                    clamp(1.0 - foamStrength * 2.0, 0.0, 1.0));
        color = mix(color, vec3<f32>(0.94, 0.98, 1.0), foamStrength);
        color = mix(color, oceanFogColor(),
                    atmosphericFog(distanceToCamera));
    }

    var presented = presentColor(color, screenUv, dimensions);
    if (debug.mode != 0u) {
        presented = debugColor(color, distanceToCamera, normal);
    }
    var output : FragmentOutput;
    output.color = vec4<f32>(presented, 1.0);
    output.linearDepth = distanceToCamera;
    return output;
}

@fragment
fn fs(input : VertexOutput) -> FragmentOutput {
    return shadeWaterFragment(input);
}

// When no later primitive or underwater-particle pass consumes linear depth,
// use the identical material result without exporting a redundant 20 MB R32
// surface at full-window resolution.
@fragment
fn fsColor(input : VertexOutput) -> @location(0) vec4<f32> {
    return shadeWaterFragment(input).color;
}
