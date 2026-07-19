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
const MIN_WATER_DEPTH : f32 = 0.5;
const SHORE_DEPTH : f32 = 7.5;
const WATER_SURFACE_AMPLITUDE : f32 = 1.0;
const WATER_TAU : f32 = 6.283185307179586;
const WATER_GRAVITY : f32 = 9.81;
const WATER_INCOMING_DIRECTION : vec2<f32> = vec2<f32>(0.9100, 0.4146);

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

struct CoastalWave {
    wave : vec4<f32>,
    blend : f32,
    exposure : f32,
};

struct WaterSurfaceSample {
    height : f32,
    geometrySlope : vec2<f32>,
    shadingWave : vec4<f32>,
};

fn coastFieldUv(worldXZ : vec2<f32>) -> vec2<f32> {
    let cells = max(camera.terrainSize - vec2<f32>(1.0), vec2<f32>(1.0));
    let extent = cells * camera.metrics.y;
    let rawUv = (worldXZ + extent * 0.5) / extent;
    let dims = vec2<f32>(textureDimensions(waterCoastFieldTex));
    let halfTexel = 0.5 / dims;
    return clamp(rawUv, halfTexel, vec2<f32>(1.0) - halfTexel);
}

fn coastalWaveField(worldXZ : vec2<f32>) -> CoastalWave {
    let coast = textureSampleLevel(waterCoastFieldTex,
        waterDisplacementSampler, coastFieldUv(worldXZ), 0.0);
    let coastDistance = max(coast.z, 0.0);
    let waterDepth = max(coast.w, 0.0);
    let rawOnshore = coast.xy;
    let directionalExposure = clamp(length(rawOnshore), 0.0, 1.0);
    var onshore = WATER_INCOMING_DIRECTION;
    if (dot(rawOnshore, rawOnshore) > 0.01) {
        onshore = normalize(rawOnshore);
    }

    let turn = 1.0 - smoothstep(55.0, 420.0, coastDistance);
    let wet = smoothstep(0.55, 2.8, waterDepth);
    let facing = smoothstep(-0.20, 0.55,
                            dot(WATER_INCOMING_DIRECTION, onshore));
    let coastResponse = directionalExposure * mix(0.12, 1.0, facing);
    let blend = turn * wet * coastResponse;
    let shelterInfluence = 1.0 - smoothstep(220.0, 850.0, coastDistance);
    let waveExposure = mix(1.0, max(0.16, directionalExposure),
                           shelterInfluence);
    if (blend == 0.0) {
        return CoastalWave(vec4<f32>(0.0), 0.0, waveExposure);
    }
    let shallow = 1.0 - smoothstep(4.0, 28.0, waterDepth);
    let wavelengthCompression = mix(1.0, 1.58, shallow);
    let offshoreCoordinate = -dot(worldXZ, WATER_INCOMING_DIRECTION);
    let phaseCoordinate = mix(offshoreCoordinate, coastDistance, turn) +
                          coastDistance * (wavelengthCompression - 1.0) * turn;
    let phaseGradient = normalize(mix(-WATER_INCOMING_DIRECTION,
                                      -onshore * wavelengthCompression, turn));
    let tangent = vec2<f32>(-onshore.y, onshore.x);
    let alongshore = dot(worldXZ, tangent);

    let k0 = WATER_TAU / 27.0;
    let k1 = WATER_TAU / 12.5;
    let phase0 = k0 * phaseCoordinate + sqrt(WATER_GRAVITY * k0) *
                 camera.waterMotion.x + 0.20 * sin(alongshore * 0.031);
    let phase1 = k1 * phaseCoordinate + sqrt(WATER_GRAVITY * k1) *
                 camera.waterMotion.x + 1.7 + 0.12 * sin(alongshore * 0.067);
    let shoaling = mix(1.0, 1.42, shallow);
    let amplitude0 = 0.62 * shoaling * wet;
    let amplitude1 = 0.19 * mix(1.0, 1.20, shallow) * wet;
    let height = sin(phase0) * amplitude0 + sin(phase1) * amplitude1;
    let derivative = cos(phase0) * amplitude0 * k0 +
                     cos(phase1) * amplitude1 * k1;
    let slope = phaseGradient * derivative;
    let crest = smoothstep(0.58, 0.94, sin(phase0) * 0.5 + 0.5);
    let breaking = blend * (1.0 - smoothstep(6.0, 16.0, waterDepth)) * crest;
    return CoastalWave(vec4<f32>(height, slope.x, slope.y, breaking),
                       blend, waveExposure);
}

fn waterSurfaceOffset(worldXZ : vec2<f32>) -> f32 {
    let strength = clamp(camera.waterParams.z, 0.0, 1.0);
    let shortWaves = textureSampleLevel(waterDisplacementTex,
        waterDisplacementSampler, worldXZ / 96.0, 0, 0.0).x;
    let mediumWaves = textureSampleLevel(waterDisplacementTex,
        waterDisplacementSampler, worldXZ / 384.0, 1, 0.0).x;
    let longWaves = textureSampleLevel(waterDisplacementTex,
        waterDisplacementSampler, worldXZ / 1536.0, 2, 0.0).x;
    let fftHeight = shortWaves + mediumWaves + longWaves;
    let coast = coastalWaveField(worldXZ);
    return mix(fftHeight * coast.exposure, coast.wave.x, coast.blend) *
           WATER_SURFACE_AMPLITUDE * strength;
}

fn sampleWaterSurface(worldXZ : vec2<f32>) -> WaterSurfaceSample {
    let strength = clamp(camera.waterParams.z, 0.0, 1.0);
    let cameraDistance = length(worldXZ - camera.cameraPos.xz);
    let shortWeight = 1.0 - smoothstep(520.0, 1450.0, cameraDistance);
    let mediumWeight = 1.0 - smoothstep(1900.0, 5200.0, cameraDistance);
    var shortWaves = vec4<f32>(0.0);
    var mediumWaves = vec4<f32>(0.0);
    if (shortWeight > 0.0) {
        shortWaves = textureSampleLevel(waterDisplacementTex,
            waterDisplacementSampler, worldXZ / 96.0, 0, 0.0);
    }
    if (mediumWeight > 0.0) {
        mediumWaves = textureSampleLevel(waterDisplacementTex,
            waterDisplacementSampler, worldXZ / 384.0, 1, 0.0);
    }
    let longWaves = textureSampleLevel(waterDisplacementTex,
        waterDisplacementSampler, worldXZ / 1536.0, 2, 0.0);
    let coast = coastalWaveField(worldXZ);

    // Geometry and lighting share one analytic band-limit. Sub-pixel short
    // waves otherwise move a hit without contributing visible detail.
    let fftHeight = shortWaves.x * shortWeight +
                    mediumWaves.x * mediumWeight + longWaves.x;
    let height = mix(fftHeight * coast.exposure, coast.wave.x, coast.blend) *
                 WATER_SURFACE_AMPLITUDE * strength;
    let fftGeometrySlope = shortWaves.yz * shortWeight +
                           mediumWaves.yz * mediumWeight + longWaves.yz;
    let geometrySlope = mix(fftGeometrySlope * coast.exposure,
                            coast.wave.yz, coast.blend) *
                        WATER_SURFACE_AMPLITUDE * strength;

    let summed = shortWaves * shortWeight +
                 mediumWaves * mediumWeight + longWaves;
    let compression = max(max(shortWaves.w * shortWeight,
                              mediumWaves.w * mediumWeight), longWaves.w);
    let refracted = mix(summed.xyz * coast.exposure,
                        coast.wave.xyz, coast.blend) * strength;
    let shadingWave = vec4<f32>(
        refracted, max(compression * coast.exposure, coast.wave.w));
    return WaterSurfaceSample(height, geometrySlope, shadingWave);
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
    let range = intersectAabb(origin, dir, boundsMin, boundsMax);
    if (range.y < 0.0 || range.x > range.y) {
        textureStore(outDepth, vec2<i32>(gid.xy),
                     vec4<f32>(-1.0, 0.0, 0.0, 0.0));
        return;
    }

    var t = textureLoad(terrainDepth, vec2<i32>(gid.xy), 0).x;
    var waterDepthUnder = 0.0;
    var shoreInfluence = 0.0;
    var waterShadingWave = vec4<f32>(0.0);
    var material = select(MATERIAL_SKY, MATERIAL_TERRAIN, t > 0.0);

    let waterEnabled = camera.waterParams.y > 0.5;
    let waterHeight = camera.waterParams.x;
    if (waterEnabled && abs(dir.y) > 1e-5) {
        var tWater = (waterHeight - origin.y) / dir.y;
        var surface = WaterSurfaceSample(
            0.0, vec2<f32>(0.0), vec4<f32>(0.0));
        var sampledSurface = false;
        if (abs(dir.y) > 0.02) {
            // One slope-aware Newton step converges more accurately than two
            // fixed-point updates. Its result lies on this sampled tangent
            // plane, so a second FFT/coast sample would be redundant.
            let p = origin + dir * tWater;
            surface = sampleWaterSurface(p.xz);
            sampledSurface = true;
            let residual = origin.y + dir.y * tWater -
                           waterHeight - surface.height;
            let derivative = dir.y -
                dot(surface.geometrySlope, dir.xz);
            if (abs(derivative) > 1e-4) {
                tWater -= residual / derivative;
            } else {
                tWater = (waterHeight + surface.height - origin.y) /
                         dir.y;
            }
            surface.height = origin.y + dir.y * tWater - waterHeight;
        }
        if (t > -1.5 && tWater > max(range.x, 0.0) && tWater < range.y &&
            (t <= 0.0 || tWater < t)) {
            let waterPos = origin + dir * tWater;
            if (!sampledSurface) {
                surface = sampleWaterSurface(waterPos.xz);
            }
            let surfaceHeight = waterHeight + surface.height;
            let waterCoord = (waterPos.xz + terrainOrigin) / cellScale;
            let waterCell = vec2<i32>(floor(waterCoord));
            let baseW = i32(camera.terrainSize.x);
            let baseH = i32(camera.terrainSize.y);
            let baseSize = vec2<i32>(baseW, baseH);
            if (waterCell.x >= 0 && waterCell.y >= 0 &&
                waterCell.x < baseW && waterCell.y < baseH) {
                let terrainHeightRaw =
                    f32(textureLoad(heightTex, waterCell, 0).x);
                let terrainHeightWorld =
                    heightmapToWorldHeight(terrainHeightRaw);
                if (terrainHeightWorld < surfaceHeight - MIN_WATER_DEPTH) {
                    t = tWater;
                    material = MATERIAL_WATER;
                    waterShadingWave = surface.shadingWave;
                    let realDepth = surfaceHeight - terrainHeightWorld;
                    shoreInfluence = nearbyShoreInfluence(
                        waterCell, baseSize, surfaceHeight);
                    waterDepthUnder = mix(
                        realDepth, min(realDepth, SHORE_DEPTH), shoreInfluence);
                }
            }
        }
    }

    // Terrain's exact baked shadow is camera-static and was cached alongside
    // its depth. Only displaced water needs a fresh lookup at its moving hit.
    var shadowFactor = textureLoad(
        terrainShadow, vec2<i32>(gid.xy), 0).x;
    if (t > 0.0 && material == MATERIAL_WATER) {
        shadowFactor = sampleBakedShadow(
            origin + dir * t, terrainOrigin, cellScale);
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
