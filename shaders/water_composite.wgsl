// Composites animated water over cached static terrain depth. This is the
// post-traversal portion of terrain_raycast.wgsl for the non-Lego path.

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

@group(0) @binding(0) var<uniform> camera : CameraUniforms;
@group(0) @binding(1) var heightTex : texture_2d<u32>;
@group(0) @binding(2) var terrainDepth : texture_2d<f32>;
@group(0) @binding(3) var terrainShadow : texture_2d<f32>;
@group(0) @binding(4) var outDepth : texture_storage_2d<r32float, write>;
@group(0) @binding(5) var outShadow : texture_storage_2d<r32float, write>;
@group(0) @binding(6) var outMaterial : texture_storage_2d<rgba16float, write>;
@group(0) @binding(7) var shadowHeightTex : texture_2d<u32>;
@group(0) @binding(8) var waterDisplacementTex : texture_2d_array<f32>;
@group(0) @binding(9) var waterDisplacementSampler : sampler;
@group(0) @binding(10) var waterCoastFieldTex : texture_2d<f32>;

const MATERIAL_SKY : u32 = 0u;
const MATERIAL_TERRAIN : u32 = 1u;
const MATERIAL_WATER : u32 = 2u;
const MIN_WATER_DEPTH : f32 = 0.05;
const SHORE_DEPTH : f32 = 7.5;
const SHORE_SURFACE_OVERLAP : f32 = 2.0;
const WATER_TAU : f32 = 6.283185307179586;
const WATER_RESOLUTION : f32 = 256.0;
const WATER_NORMAL_LAYER : i32 = 2;

fn toHeightmapCoordinate(height : f32) -> f32 {
    let normalized = (height / camera.metrics.x) * 0.5 + 0.5;
    return normalized * 65535.0;
}

fn toHeightmapScale(height : f32) -> f32 {
    let normalized = (height / camera.metrics.x) * 0.5;
    return normalized * 65535.0;
}

fn heightmapToWorldHeight(height : f32) -> f32 {
    return ((height / 65535.0) * 2.0 - 1.0) * camera.metrics.x;
}

fn maxTraversalMip() -> u32 {
    let terrainWidth = max(u32(camera.terrainSize.x), 1u);
    let terrainHeight = max(u32(camera.terrainSize.y), 1u);
    var level = min(7u, textureNumLevels(heightTex) - 1u);
    loop {
        if (level == 0u ||
            ((terrainWidth >> level) > 0u && (terrainHeight >> level) > 0u)) {
            break;
        }
        level--;
    }
    return level;
}

struct WaterSurfaceSample {
    height : f32,
    geometrySlope : vec2<f32>,
    shadingWave : vec4<f32>,
};

struct WaterRayHit {
    distance : f32,
    surface : WaterSurfaceSample,
};

struct SpectralSurface {
    displacement : vec3<f32>,
    normalVector : vec3<f32>,
    compression : f32,
};

struct LongWaveSurface {
    displacement : vec3<f32>,
    normalVector : vec3<f32>,
    fold : f32,
};

fn cascadeUv(position : vec2<f32>, scale : f32) -> vec2<f32> {
    return position / scale +
           vec2<f32>(0.5 + 0.5 / WATER_RESOLUTION);
}

fn spectralSurface(worldXZ : vec2<f32>, strength : f32,
                   distance : f32) -> SpectralSurface {
    let detailWeight = 1.0 - smoothstep(900.0, 3500.0, max(distance, 0.0));
    let broadFirst = textureSampleLevel(
        waterDisplacementTex, waterDisplacementSampler,
        cascadeUv(worldXZ, camera.waterSpectrum.x), 0, 0.0);
    var detailFirst = vec4<f32>(0.0);
    if (detailWeight > 0.0) {
        detailFirst = textureSampleLevel(
            waterDisplacementTex, waterDisplacementSampler,
            cascadeUv(worldXZ, camera.waterSpectrum.y), 1, 0.0) * detailWeight;
    }
    let baseXZ = worldXZ - (broadFirst.xz + detailFirst.xz) * strength;
    let broad = textureSampleLevel(
        waterDisplacementTex, waterDisplacementSampler,
        cascadeUv(baseXZ, camera.waterSpectrum.x), 0, 0.0);
    let broadNormal = textureSampleLevel(
        waterDisplacementTex, waterDisplacementSampler,
        cascadeUv(baseXZ, camera.waterSpectrum.x), WATER_NORMAL_LAYER, 0.0).xyz;
    var detail = vec4<f32>(0.0);
    var detailNormal = vec3<f32>(0.0, 1.0, 0.0);
    if (detailWeight > 0.0) {
        detail = textureSampleLevel(
            waterDisplacementTex, waterDisplacementSampler,
            cascadeUv(baseXZ, camera.waterSpectrum.y), 1, 0.0) * detailWeight;
        detailNormal = textureSampleLevel(
            waterDisplacementTex, waterDisplacementSampler,
            cascadeUv(baseXZ, camera.waterSpectrum.y),
            WATER_NORMAL_LAYER + 1, 0.0).xyz;
    }
    let up = vec3<f32>(0.0, 1.0, 0.0);
    return SpectralSurface(
        (broad.xyz + detail.xyz) * strength,
        up + ((broadNormal - up) +
              (detailNormal - up) * detailWeight) * strength,
        max(broad.w, detail.w) * strength);
}

fn oneLongWave(position : vec2<f32>, wave : vec4<f32>,
               dynamics : vec3<f32>, strength : f32) -> LongWaveSurface {
    let waveNumber = WATER_TAU / (wave.w + 1.0e-4);
    let phase = waveNumber * dot(wave.xy, position) -
                dynamics.z * camera.waterMotion.x + dynamics.y;
    let sine = sin(phase);
    let cosine = cos(phase);
    let amplitude = wave.z * strength;
    let ka = waveNumber * amplitude;
    return LongWaveSurface(
        vec3<f32>(-dynamics.x * amplitude * wave.x * sine,
                  amplitude * cosine,
                  -dynamics.x * amplitude * wave.y * sine),
        vec3<f32>(wave.x * ka * sine,
                  -dynamics.x * ka * cosine,
                  wave.y * ka * sine),
        dynamics.x * ka * sine);
}

fn longWaveSurface(position : vec2<f32>, strength : f32) -> LongWaveSurface {
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
    return LongWaveSurface(
        a.displacement + b.displacement + c.displacement + d.displacement,
        vec3<f32>(0.0, 1.0, 0.0) +
            a.normalVector + b.normalVector + c.normalVector + d.normalVector,
        a.fold + b.fold + c.fold + d.fold);
}

fn sampleWaterSurface(worldXZ : vec2<f32>, distance : f32) -> WaterSurfaceSample {
    if (distance >= 6500.0) {
        return WaterSurfaceSample(
            0.0, vec2<f32>(0.0), vec4<f32>(0.0));
    }
    let strength = clamp(camera.waterParams.z, 0.0, 2.0);
    let spectral = spectralSurface(worldXZ, strength, distance);
    let longWaves = longWaveSurface(
        worldXZ - spectral.displacement.xz, strength);
    var normal = spectral.normalVector +
                 longWaves.normalVector - vec3<f32>(0.0, 1.0, 0.0);
    if (dot(normal, normal) > 1.0e-8) {
        normal = normalize(normal);
    } else {
        normal = vec3<f32>(0.0, 1.0, 0.0);
    }
    let normalY = max(normal.y, 0.08);
    let geometrySlope = -normal.xz / normalY;
    let displacement = spectral.displacement + longWaves.displacement;
    let compression = max(spectral.compression,
                          clamp(longWaves.fold, 0.0, 2.0));
    return WaterSurfaceSample(
        displacement.y, geometrySlope,
        vec4<f32>(displacement.y, normal.x, normal.z, compression));
}

fn filterWaterSurfaceForHorizon(surfaceIn : WaterSurfaceSample,
                                distance : f32) -> WaterSurfaceSample {
    var surface = surfaceIn;
    let detail = 1.0 - smoothstep(1800.0, 6500.0, max(distance, 0.0));
    surface.height *= detail;
    surface.geometrySlope *= detail;
    surface.shadingWave = vec4<f32>(
        surface.shadingWave.x * detail,
        surface.shadingWave.yz * detail,
        surface.shadingWave.w * detail);
    return surface;
}

fn intersectWaterSurface(origin : vec3<f32>, dir : vec3<f32>,
                         waterHeight : f32) -> WaterRayHit {
    let maximumWaveHeight = 48.0 * clamp(camera.waterParams.z, 0.0, 2.0);
    let envelopeA =
        (waterHeight - maximumWaveHeight - origin.y) / dir.y;
    let envelopeB =
        (waterHeight + maximumWaveHeight - origin.y) / dir.y;
    let minimumDistance = max(min(envelopeA, envelopeB), 1.0e-3);
    let maximumDistance = max(max(envelopeA, envelopeB), minimumDistance);
    var distance = (waterHeight - origin.y) / dir.y;
    distance = clamp(distance, minimumDistance, maximumDistance);
    var position = origin + dir * distance;
    var surface = filterWaterSurfaceForHorizon(
        sampleWaterSurface(position.xz, distance), distance);
    var residual = position.y - waterHeight - surface.height;
    var derivative = dir.y - dot(surface.geometrySlope, dir.xz);
    if (abs(derivative) > 1.0e-4) {
        let maximumStep = 32.0 / max(abs(dir.y), 0.002);
        distance = clamp(
            distance - clamp(residual / derivative,
                             -maximumStep, maximumStep),
            minimumDistance, maximumDistance);
    }
    if (abs(dir.y) < 0.065) {
        position = origin + dir * distance;
        surface = filterWaterSurfaceForHorizon(
            sampleWaterSurface(position.xz, distance), distance);
        residual = position.y - waterHeight - surface.height;
        derivative = dir.y - dot(surface.geometrySlope, dir.xz);
        if (abs(derivative) > 1.0e-4) {
            let maximumStep = 16.0 / max(abs(dir.y), 0.002);
            distance = clamp(
                distance - clamp(residual / derivative,
                                 -maximumStep, maximumStep),
                minimumDistance, maximumDistance);
        }
    }
    surface.height = origin.y + dir.y * distance - waterHeight;
    return WaterRayHit(distance, surface);
}

fn nearbyShoreInfluence(cell : vec2<i32>, baseSize : vec2<i32>,
                        waterHeight : f32) -> f32 {
    let shoreMip = min(3u, maxTraversalMip());
    let blockSize = 1u << shoreMip;
    let levelSize = max(
        vec2<i32>(baseSize.x >> shoreMip, baseSize.y >> shoreMip),
        vec2<i32>(1, 1));
    let base = vec2<i32>(floor(
        vec2<f32>(cell) / f32(blockSize) - vec2<f32>(0.5, 0.5)));

    var maxNearbyHeight = -1.0e20;
    for (var dz = 0; dz < 2; dz++) {
        for (var dx = 0; dx < 2; dx++) {
            let c = clamp(base + vec2<i32>(dx, dz), vec2<i32>(0, 0),
                          levelSize - vec2<i32>(1, 1));
            let h = heightmapToWorldHeight(
                f32(textureLoad(heightTex, c, i32(shoreMip)).x));
            maxNearbyHeight = max(maxNearbyHeight, h);
        }
    }

    return smoothstep(waterHeight - 36.0,
                      waterHeight - MIN_WATER_DEPTH, maxNearbyHeight);
}

fn intersectAabb(origin : vec3<f32>, dir : vec3<f32>,
                 bmin : vec3<f32>, bmax : vec3<f32>) -> vec2<f32> {
    let invDir = 1.0 / (dir + sign(dir) * 1e-20 + vec3<f32>(1e-20));
    let t0 = (bmin - origin) * invDir;
    let t1 = (bmax - origin) * invDir;
    let tMin = max(max(min(t0.x, t1.x), min(t0.y, t1.y)), min(t0.z, t1.z));
    let tMax = min(min(max(t0.x, t1.x), max(t0.y, t1.y)), max(t0.z, t1.z));
    return vec2<f32>(tMin, tMax);
}

fn rayDirFromPixel(pixel : vec2<u32>, dims : vec2<u32>) -> vec3<f32> {
    let dimf = vec2<f32>(f32(dims.x), f32(dims.y));
    let pixelF = vec2<f32>(f32(pixel.x), f32(pixel.y));
    let uv = (pixelF + vec2<f32>(0.5, 0.5)) / dimf;
    let ndc = vec2<f32>(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0);
    let viewDir = vec3<f32>(ndc * camera.invProjParams.xy, 1.0);
    return normalize((camera.invView * vec4<f32>(viewDir, 0.0)).xyz);
}

fn sampleBakedShadow(worldPos : vec3<f32>, terrainOrigin : vec2<f32>,
                     cellScale : f32) -> f32 {
    if (camera.lightDirWS.w > 0.5) {
        let light = normalize(camera.lightDirWS.xyz);
        if (light.y <= 0.0) { return 1.0; }
        var distance = cellScale;
        var visibility = 1.0;
        for (var stepIndex = 0u; stepIndex < 24u; stepIndex += 1u) {
            let point = worldPos + light * distance;
            let cell = (point.xz + terrainOrigin) / cellScale;
            if (any(cell < vec2<f32>(0.0)) || any(cell >= camera.terrainSize)) { break; }
            let height = heightmapToWorldHeight(f32(textureLoad(heightTex, vec2<i32>(cell), 0).x));
            visibility = min(visibility, smoothstep(-0.4, 0.6, point.y - height));
            if (visibility < 0.01) { break; }
            distance = distance * 1.35 + cellScale;
        }
        return visibility;
    }
    let dims = vec2<i32>(textureDimensions(shadowHeightTex));
    let cellF = (worldPos.xz + terrainOrigin) / cellScale;
    let scale = vec2<f32>(dims) / camera.terrainSize;
    let cell = clamp(vec2<i32>(floor(cellF * scale)), vec2<i32>(0, 0),
                     dims - vec2<i32>(1, 1));
    let boundary = f32(textureLoad(shadowHeightTex, cell, 0).x);
    let posY = toHeightmapCoordinate(worldPos.y);
    let soft = max(toHeightmapScale(1.5), 1.0);
    return smoothstep(-soft, soft, posY - boundary);
}

@compute @workgroup_size(8, 8, 1)
fn main(@builtin(global_invocation_id) gid : vec3<u32>) {
    let dims = textureDimensions(outDepth);
    if (gid.x >= dims.x || gid.y >= dims.y) { return; }

    let origin = camera.cameraPos.xyz;
    let dir = rayDirFromPixel(gid.xy, dims);
    let terrainSize = vec2<f32>(camera.terrainSize);
    let cellScale = camera.metrics.y;
    let terrainOrigin = 0.5 * (terrainSize - vec2<f32>(1.0, 1.0)) * cellScale;
    let borderMargin = cellScale;
    let boundsMin = vec3<f32>(
        -terrainOrigin.x + borderMargin,
        -camera.metrics.x,
        -terrainOrigin.y + borderMargin);
    let boundsMax = vec3<f32>(
        terrainSize.x * cellScale - terrainOrigin.x - borderMargin,
        camera.metrics.x,
        terrainSize.y * cellScale - terrainOrigin.y - borderMargin);
    let waterEnabled = camera.waterParams.y > 0.5;
    let waterHeight = camera.waterParams.x;
    var waterHit = WaterRayHit(
        -1.0, WaterSurfaceSample(0.0, vec2<f32>(0.0), vec4<f32>(0.0)));
    if (waterEnabled && abs(dir.y) > 1e-5) {
        let flatWaterDistance = (waterHeight - origin.y) / dir.y;
        if (flatWaterDistance > 0.0 && flatWaterDistance < 50000.0) {
            waterHit = intersectWaterSurface(origin, dir, waterHeight);
        }
    }

    let range = intersectAabb(origin, dir, boundsMin, boundsMax);
    if (range.y < 0.0 || range.x > range.y) {
        if (waterHit.distance > 0.0 && waterHit.distance < 50000.0) {
            textureStore(outDepth, vec2<i32>(gid.xy),
                         vec4<f32>(waterHit.distance, 0.0, 0.0, 0.0));
            textureStore(outShadow, vec2<i32>(gid.xy),
                         vec4<f32>(501.0, 0.0, 0.0, 0.0));
            textureStore(outMaterial, vec2<i32>(gid.xy),
                         vec4<f32>(waterHit.surface.shadingWave.y,
                                   waterHit.surface.shadingWave.z,
                                   waterHit.surface.shadingWave.w, 0.0));
            return;
        }
        textureStore(outDepth, vec2<i32>(gid.xy),
                     vec4<f32>(-1.0, 0.0, 0.0, 0.0));
        return;
    }

    var t = textureLoad(terrainDepth, vec2<i32>(gid.xy), 0).x;
    var waterDepthUnder = 0.0;
    var waterHasTerrainBed = false;
    var shoreInfluence = 0.0;
    var waterShadingWave = vec4<f32>(0.0);
    var material = select(MATERIAL_SKY, MATERIAL_TERRAIN, t > 0.0);

    if (waterHit.distance > 0.0 && waterHit.distance < 50000.0) {
        let tWater = waterHit.distance;
        let surface = waterHit.surface;
        if (t > -1.5 && (t <= 0.0 || tWater < t)) {
            let waterPos = origin + dir * tWater;
            let surfaceHeight = waterHeight + surface.height;
            let waterCoord = (waterPos.xz + terrainOrigin) / cellScale;
            let waterCell = vec2<i32>(floor(waterCoord));
            let baseW = i32(camera.terrainSize.x);
            let baseH = i32(camera.terrainSize.y);
            let baseSize = vec2<i32>(baseW, baseH);
            if (waterCell.x >= 1 && waterCell.y >= 1 &&
                waterCell.x < baseW - 1 && waterCell.y < baseH - 1) {
                let terrainHeightRaw =
                    f32(textureLoad(heightTex, waterCell, 0).x);
                let terrainHeightWorld =
                    heightmapToWorldHeight(terrainHeightRaw);
                if (terrainHeightWorld <
                    max(surfaceHeight,
                        waterHeight + SHORE_SURFACE_OVERLAP) -
                        MIN_WATER_DEPTH) {
                    t = tWater;
                    material = MATERIAL_WATER;
                    waterHasTerrainBed = true;
                    waterShadingWave = surface.shadingWave;
                    let realDepth = max(
                        surfaceHeight - terrainHeightWorld, MIN_WATER_DEPTH);
                    shoreInfluence = nearbyShoreInfluence(
                        waterCell, baseSize, surfaceHeight);
                    waterDepthUnder = mix(
                        realDepth, min(realDepth, SHORE_DEPTH), shoreInfluence);
                }
            } else {
                t = tWater;
                material = MATERIAL_WATER;
                waterShadingWave = surface.shadingWave;
                waterDepthUnder = 500.0;
            }
        }
    }

    // Terrain's exact baked shadow is camera-static and was cached alongside
    // its depth. Only displaced water needs a fresh lookup at its moving hit.
    var shadowFactor = textureLoad(
        terrainShadow, vec2<i32>(gid.xy), 0).x;
    if (t > 0.0 && material == MATERIAL_WATER && waterHasTerrainBed) {
        shadowFactor = sampleBakedShadow(
            origin + dir * t, terrainOrigin, cellScale);
    } else if (t > 0.0 && material == MATERIAL_WATER) {
        shadowFactor = 1.0;
    }

    var packedShadow = shadowFactor;
    if (material == MATERIAL_WATER) {
        packedShadow = select(-(waterDepthUnder + 1.0),
                              waterDepthUnder + 1.0, shadowFactor > 0.5);
    }
    textureStore(outDepth, vec2<i32>(gid.xy),
                 vec4<f32>(t, 0.0, 0.0, 0.0));
    textureStore(outShadow, vec2<i32>(gid.xy),
                 vec4<f32>(packedShadow, 0.0, 0.0, 0.0));
    if (material == MATERIAL_WATER) {
        textureStore(outMaterial, vec2<i32>(gid.xy),
                     vec4<f32>(waterShadingWave.y, waterShadingWave.z,
                               waterShadingWave.w, shoreInfluence));
    }
}
