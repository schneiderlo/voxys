// ═══════════════════════════════════════════════════════════════════════════════
// ray_blit.wgsl - Fullscreen Lighting Pass Shader
// ═══════════════════════════════════════════════════════════════════════════════
// Composites the ray-caster output into a final shaded image with lighting,
// fog, and sky rendering.
// Features:
//   - Fullscreen triangle vertex shader (single oversized triangle)
//   - Depth texture sampling from ray-cast pass
//   - Sky rendering with vertical gradient and S-curve interpolation
//   - Position reconstruction from depth
//   - Screen-space normal reconstruction (picking closer neighbor)
//   - Terrain UV computation
//   - Lighting (diffuse, ambient, roughness-based specular)
//   - Exponential fog (capped at 0.7)
// ═══════════════════════════════════════════════════════════════════════════════

// ─────────────────────────────────────────────────────────────────────────────
// Uniforms
// ─────────────────────────────────────────────────────────────────────────────

struct CameraUniforms {
    viewProj : mat4x4<f32>,       // View-projection matrix
    invViewProj : mat4x4<f32>,    // Inverse view-projection
    invView : mat4x4<f32>,        // Inverse view matrix
    terrainSize : vec2<f32>,      // Heightmap dimensions (e.g., 8192, 8192)
    invTerrainSize : vec2<f32>,   // 1.0 / terrainSize
    metrics : vec4<f32>,          // (heightScale, cellScale, step, fogDensity)
    cameraPos : vec4<f32>,        // World-space camera position (.xyz)
    invProjParams : vec4<f32>,    // Inverse projection params (.xy used)
    lightDirVS : vec4<f32>,       // View-space light direction (.xyz)
    frustumPlanes : array<vec4<f32>, 6>, // Frustum planes
    lightDirWS : vec4<f32>,       // World-space light direction (.xyz)
    waterParams : vec4<f32>,      // (height, enabled, waveStrength, roughness)
    waterColorA : vec4<f32>,      // shallow color rgb, reflection strength
    waterColorB : vec4<f32>,      // deep color rgb, shore fade distance
    waterMotion : vec4<f32>,      // simulation time, reserved...
};

// Debug visualization uniforms
struct DebugUniforms {
    mode : u32,           // 0=none, 1=depth, 2=normals, 3=mip_levels
    maxDepth : f32,       // Max depth for depth visualization (e.g., 5000.0)
    padding0 : f32,
    padding1 : f32,
};

// ─────────────────────────────────────────────────────────────────────────────
// Bindings
// ─────────────────────────────────────────────────────────────────────────────

@group(0) @binding(0) var<uniform> camera : CameraUniforms;
@group(0) @binding(1) var depthTex : texture_2d<f32>;
@group(0) @binding(2) var shadowTex : texture_2d<f32>;
@group(0) @binding(3) var materialTex : texture_2d<f32>;
@group(0) @binding(4) var terrainTex : texture_2d<f32>;
@group(0) @binding(5) var lightmapTex : texture_2d<f32>;
@group(0) @binding(6) var terrainSampler : sampler;
@group(0) @binding(7) var<uniform> debug : DebugUniforms;
// Paraboloid-mapped static sky (scattering + gradient + clouds), baked once
// by sky_lut.wgsl. See that file for the mapping.
@group(0) @binding(8) var skyLUT : texture_2d<f32>;
// Tiling water detail noise, CPU-baked once (see BlitPath::createWaterNoise):
// R,G = detail-wave gradient (dx, dz) encoded as v*4+0.5; B = flow noise,
// A = ripple noise, both stored as v*0.5+0.5. One tile = 1024 world units.
@group(0) @binding(9) var waterNoiseTex : texture_2d<f32>;
@group(0) @binding(10) var waterNoiseSampler : sampler;
@group(0) @binding(11) var waterDisplacementTex : texture_2d_array<f32>;
@group(0) @binding(12) var waterDisplacementSampler : sampler;
@group(0) @binding(13) var waterFoamTex : texture_2d<f32>;

const MATERIAL_SKY : u32 = 0u;
const MATERIAL_TERRAIN : u32 = 1u;
const MATERIAL_WATER : u32 = 2u;

// Beer-Lambert extinction per world unit of water. Red is absorbed fastest,
// so the water tends blue-green as the light path lengthens.
const WATER_EXTINCTION : vec3<f32> = vec3<f32>(0.135, 0.052, 0.033);

// ─────────────────────────────────────────────────────────────────────────────
// Vertex Shader (Fullscreen Triangle)
// ─────────────────────────────────────────────────────────────────────────────
// Uses a single oversized triangle to cover the screen. This technique avoids
// the diagonal seam that would be visible with a quad made of two triangles.

struct VSOut {
    @builtin(position) pos : vec4<f32>,
    @location(0) uv : vec2<f32>,
};

@vertex
fn vs(@builtin(vertex_index) i : u32) -> VSOut {
    // Oversized triangle vertices in NDC
    var pos = array<vec2<f32>, 3>(
        vec2<f32>(-1.0, -3.0),  // Bottom-left, extends below screen
        vec2<f32>(3.0, 1.0),    // Right, extends past screen
        vec2<f32>(-1.0, 1.0)    // Top-left
    );
    // Corresponding UV coordinates
    var uv = array<vec2<f32>, 3>(
        vec2<f32>(0.0, 2.0),    // Bottom-left
        vec2<f32>(2.0, 0.0),    // Right
        vec2<f32>(0.0, 0.0)     // Top-left
    );
    
    var o : VSOut;
    o.pos = vec4<f32>(pos[i], 0.0, 1.0);
    o.uv = uv[i];
    return o;
}

// ─────────────────────────────────────────────────────────────────────────────
// Helper Functions
// ─────────────────────────────────────────────────────────────────────────────

/// Sample depth from depth texture at given pixel coordinates
/// Clamps coordinates to texture bounds to avoid sampling outside
fn sampleDepth(coords : vec2<i32>, maxCoord : vec2<i32>) -> f32 {
    let clamped = clamp(coords, vec2<i32>(0, 0), maxCoord);
    return textureLoad(depthTex, clamped, 0).x;
}

/// Convert pixel coordinates to normalized device coordinates (NDC)
fn ndcFromPixel(coords : vec2<i32>, dims : vec2<f32>) -> vec2<f32> {
    let uv = (vec2<f32>(f32(coords.x), f32(coords.y)) + vec2<f32>(0.5, 0.5)) / dims;
    return vec2<f32>(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0);
}

/// Generate normalized view-space ray direction from NDC and inverse projection params
fn rayDirFromPixel(invProjParams : vec2<f32>, ndc : vec2<f32>) -> vec3<f32> {
    // GLM is configured for a left-handed (+Z forward) clip space, so rays should
    // extend along +Z. Use +1.0 here to match the handedness of the CPU projection.
    let dir = vec3<f32>(ndc * invProjParams, 1.0);

    return normalize(dir);
}

/// Reconstruct view-space position from depth using ray marching distance
fn viewPosFromDepth(invProjParams : vec2<f32>, ndc : vec2<f32>, depth : f32) -> vec3<f32> {
    let dir = rayDirFromPixel(invProjParams, ndc);
    return dir * depth;
}

/// Transform view-space position to world-space using inverse view matrix
fn viewToWorld(invView : mat4x4<f32>, viewPos : vec3<f32>) -> vec3<f32> {
    return (invView * vec4<f32>(viewPos, 1.0)).xyz;
}

/// Compute terrain UV coordinates from world-space position
/// Maps world XZ position to [0, 1] UV range based on terrain dimensions
fn terrainUV(worldPos : vec3<f32>) -> vec2<f32> {
    let terrainSize = vec2<f32>(camera.terrainSize);
    let cellCounts = max(terrainSize - vec2<f32>(1.0, 1.0), vec2<f32>(1.0, 1.0));
    let cellScale = max(camera.metrics.y, 0.0001);
    let origin = 0.5 * cellCounts * cellScale;
    let coord = (worldPos.xz + origin) / cellScale;
    return clamp(coord / cellCounts, vec2<f32>(0.0, 0.0), vec2<f32>(1.0, 1.0));
}

fn skyGradient(worldDir : vec3<f32>) -> vec3<f32> {
    let horizonColor = vec3<f32>(1.00, 0.70, 0.56);
    let zenithColor = vec3<f32>(0.50, 0.66, 0.84);
    let heightGradient = pow(max(worldDir.y, 0.0), 0.45);
    return mix(horizonColor, zenithColor, heightGradient);
}

/// One filtered sample of the baked sky (linear HDR, no sun disc).
fn sampleSkyLUT(dir : vec3<f32>) -> vec3<f32> {
    let uv = dir.xz / (1.0 + max(dir.y, 0.0)) * 0.5 + vec2<f32>(0.5, 0.5);
    return textureSampleLevel(skyLUT, terrainSampler, uv, 0.0).rgb;
}

/// Sum three band-limited Tessendorf cascades resolved by the GPU inverse FFT.
/// Returns (height, dh/dx, dh/dz, Jacobian compression).
fn waterWaveField(p : vec2<f32>) -> vec4<f32> {
    let strength = clamp(camera.waterParams.z, 0.0, 1.0);
    let shortWaves = textureSampleLevel(waterDisplacementTex,
        waterDisplacementSampler, p / 96.0, 0, 0.0);
    let mediumWaves = textureSampleLevel(waterDisplacementTex,
        waterDisplacementSampler, p / 384.0, 1, 0.0);
    let longWaves = textureSampleLevel(waterDisplacementTex,
        waterDisplacementSampler, p / 1536.0, 2, 0.0);
    // Manual cascade LOD. The simulation texture has no mip chain because it
    // is rewritten every frame; fading bands before they become sub-pixel
    // prevents the glittering horizon that an unfiltered slope field creates.
    let cameraDistance = length(p - camera.cameraPos.xz);
    let shortWeight = 1.0 - smoothstep(520.0, 1450.0, cameraDistance);
    let mediumWeight = 1.0 - smoothstep(1900.0, 5200.0, cameraDistance);
    let summed = shortWaves * shortWeight +
                 mediumWaves * mediumWeight + longWaves;
    return vec4<f32>(summed.xyz * strength,
                     max(max(shortWaves.w * shortWeight,
                             mediumWaves.w * mediumWeight), longWaves.w));
}

fn waterWaveNormal(wave : vec4<f32>) -> vec3<f32> {
    return normalize(vec3<f32>(-wave.y, 1.0, -wave.z));
}

/// RGB is reflected radiance; A is confidence.  A confidence channel avoids
/// the old binary sky/terrain transition at the first missing SSR sample.
fn sampleScreenTerrainReflection(origin : vec3<f32>, dir : vec3<f32>, dims : vec2<u32>) -> vec4<f32> {
    let dimsF = vec2<f32>(f32(dims.x), f32(dims.y));
    let maxCoord = vec2<i32>(i32(dims.x) - 1, i32(dims.y) - 1);

    // March in screen space: project only the two ray endpoints and step by
    // lerping their clip coordinates, instead of a mat4 projection per step.
    // Uniform screen steps also cover each on-screen region exactly once, so
    // thin walls cannot be skipped the way growing world strides could.
    let maxDist = 1400.0;
    let endPoint = origin + dir * maxDist;
    let clipA = camera.viewProj * vec4<f32>(origin, 1.0);
    let clipB = camera.viewProj * vec4<f32>(endPoint, 1.0);
    if (clipA.w <= 0.0 && clipB.w <= 0.0) {
        return vec4<f32>(0.0);
    }
    let stepCount = 24u;
    for (var step = 1u; step <= stepCount; step++) {
        let s = f32(step) / f32(stepCount);
        let clip = mix(clipA, clipB, s);
        if (clip.w <= 0.0) { continue; }
        let ndc = clip.xy / clip.w;
        if (any(ndc < vec2<f32>(-1.0, -1.0)) || any(ndc > vec2<f32>(1.0, 1.0))) {
            continue;
        }
        let uv = vec2<f32>(ndc.x * 0.5 + 0.5, 0.5 - ndc.y * 0.5);
        let pixel = clamp(vec2<i32>(floor(uv * dimsF)), vec2<i32>(0, 0), maxCoord);
        let sceneDepth = textureLoad(depthTex, pixel, 0).x;
        let sceneMaterial = u32(textureLoad(materialTex, pixel, 0).x + 0.5);

        // Projection is linear in homogeneous coordinates. Because clip is the
        // same interpolation of the projected endpoints, its world-space point
        // is the same interpolation of the ray endpoints (no second 1/w pass).
        let candidate = mix(origin, endPoint, s);
        let candidateDepth = length(candidate - camera.cameraPos.xyz);
        let tolerance = max(4.0, candidateDepth * 0.06);

        // Only accept terrain hits: sampling water pixels here would read
        // the water-surface depth and produce false self-reflections.
        if (sceneMaterial == MATERIAL_TERRAIN &&
            sceneDepth > 0.0 &&
            abs(sceneDepth - candidateDepth) < tolerance) {
            let ndcHit = ndcFromPixel(pixel, dimsF);
            let hitView = viewPosFromDepth(camera.invProjParams.xy, ndcHit, sceneDepth);
            let hitWorld = viewToWorld(camera.invView, hitView);
            let hitUV = terrainUV(hitWorld);
            // Approximate the lit look of the reflected wall: modulate the
            // albedo by the hit pixel's sun shadow and baked light so the
            // reflection carries shadow / ambient occlusion instead of
            // looking flat and unlit.
            let albedoHit = textureSampleLevel(terrainTex, terrainSampler, hitUV, 0.0).rgb;
            let hitShadow = clamp(textureLoad(shadowTex, pixel, 0).x, 0.0, 1.0);
            let hitLight = textureSampleLevel(lightmapTex, terrainSampler, hitUV, 0.0).x;
            let edge = max(abs(ndc.x), abs(ndc.y));
            let confidence = (1.0 - smoothstep(0.72, 0.98, edge)) *
                             mix(0.95, 0.25, s);
            return vec4<f32>(albedoHit * (0.35 + 0.65 * hitShadow * hitLight),
                             confidence);
        }
    }

    return vec4<f32>(0.0);
}

/// Trace the incoming sunlight through the local surface normal to the bed,
/// then measure convergence in the simulated FFT slope field. Brightness now
/// comes from the surface lens Jacobian rather than an unrelated noise grid.
fn refractedRayCaustics(posWorld : vec3<f32>, waterDepth : f32,
                        n : vec3<f32>, sunDir : vec3<f32>) -> f32 {
    if (waterDepth <= 0.0 || waterDepth > 22.0 || sunDir.y <= 0.02) {
        return 0.0;
    }
    let ray = refract(-sunDir, n, 0.7502);
    let travel = waterDepth / max(-ray.y, 0.12);
    let bedPoint = posWorld.xz + ray.xz * travel;
    let epsilon = mix(0.45, 1.5, clamp(waterDepth / 22.0, 0.0, 1.0));
    let slopeLeft = waterWaveField(bedPoint - vec2<f32>(epsilon, 0.0)).y;
    let slopeRight = waterWaveField(bedPoint + vec2<f32>(epsilon, 0.0)).y;
    let slopeDown = waterWaveField(bedPoint - vec2<f32>(0.0, epsilon)).z;
    let slopeUp = waterWaveField(bedPoint + vec2<f32>(0.0, epsilon)).z;
    let divergence = ((slopeRight - slopeLeft) + (slopeUp - slopeDown)) /
                     (2.0 * epsilon);
    let convergence = max(-divergence, 0.0);
    let focusLines = smoothstep(0.006, 0.065, convergence * (1.0 + waterDepth * 0.08));
    let breakup = textureSampleLevel(waterNoiseTex, waterNoiseSampler,
        bedPoint / 380.0, 0.0).b;
    return focusLines * mix(0.72, 1.0, breakup) *
           exp(-waterDepth * 0.105) * smoothstep(0.02, 0.35, sunDir.y);
}

fn shadeWater(posWorld : vec3<f32>, posView : vec3<f32>, viewDirWS : vec3<f32>,
              n : vec3<f32>, waveCrest : f32, waterDepth : f32, shoreMask : f32,
              shadowVisibility : f32, dims : vec2<u32>) -> vec3<f32> {
    let sunDir = normalize(camera.lightDirWS.xyz);
    let shallow = camera.waterColorA.rgb;
    let deep = camera.waterColorB.rgb;

    // ── Underwater: camera below the surface, looking up at it ───────────────
    // (The flat plane height ignores the wave offset; right at the surface the
    // two shading modes may alternate for a frame, which reads as splashing.)
    if (camera.cameraPos.y < camera.waterParams.x) {
        // Seen from below, the surface normal faces away; flip it.
        let nUp = -n;
        let cosTheta = clamp(dot(nUp, viewDirWS), 0.0, 1.0);
        let reflectionStrength = clamp(camera.waterColorA.a, 0.0, 1.0);
        let reflectance = clamp(reflectionStrength * (0.05 + 0.95 * pow(1.0 - cosTheta, 4.0)),
                                0.0, 0.9);
        // What comes through the surface is skylight, attenuated by the water
        // between the camera and the surface.
        let pathLen = length(posView);
        let transmit = exp(-WATER_EXTINCTION * pathLen);
        let skyThrough = skyGradient(vec3<f32>(0.0, 1.0, 0.0));
        let refracted = skyThrough * transmit + deep * (1.0 - transmit);
        // Internal reflection shows the water body itself, not the sky.
        let surface = mix(refracted, deep * 0.8, reflectance);
        // Inside the medium everything fades to the deep colour with distance.
        let fogFactor = clamp(1.0 - exp(-0.02 * pathLen), 0.0, 0.85);
        return mix(surface, deep, fogFactor);
    }

    // waterColorB.a ("shore_fade") is the column depth, in world units, over
    // which the water colour fades from shallow to deep.
    let shoreFade = max(camera.waterColorB.a, 0.001);
    let depth01 = clamp(waterDepth / shoreFade, 0.0, 1.0);
    let depthCurve = smoothstep(0.0, 1.0, depth01);

    // ── Refraction: the riverbed seen through the water ──────────────────────
    // Follow the eye ray through the air/water interface.  This replaces the
    // old normal-offset UV trick with Snell refraction and gives shallows a
    // convincing magnifying/wobbling motion.
    let refractedViewRay = refract(-viewDirWS, n, 0.7502);
    let refractedTravel = min(waterDepth / max(-refractedViewRay.y, 0.12), 48.0);
    let bedWorld = posWorld + vec3<f32>(refractedViewRay.x * refractedTravel,
                                       -waterDepth,
                                       refractedViewRay.z * refractedTravel);
    let bedUV = terrainUV(bedWorld);
    let bedAlbedo = textureSampleLevel(terrainTex, terrainSampler, bedUV, 0.0).rgb;
    let bedLight = textureSampleLevel(lightmapTex, terrainSampler, bedUV, 0.0).x;
    // The bed is lit by the (shadow-attenuated) sun plus ambient fill.
    let caustic = refractedRayCaustics(posWorld, waterDepth, n, sunDir) *
                   shadowVisibility;
    let bedLit = bedAlbedo * (0.25 + 0.75 * bedLight * shadowVisibility) *
                 (vec3<f32>(1.0) + vec3<f32>(1.00, 0.82, 0.48) * caustic * 0.52);

    // Beer-Lambert extinction follows the refracted path, not just vertical depth.
    let opticalDepth = min(refractedTravel, waterDepth * 3.0);
    let transmit = exp(-WATER_EXTINCTION * max(opticalDepth, 0.0));
    // Flow noise from the baked tiling texture. The texture's noise has 8
    // periods per tile, so sampling at k/8 reproduces the feature size of
    // the old simplexNoise2D(p * k).
    let flow1 = textureSampleLevel(waterNoiseTex, waterNoiseSampler,
                                   posWorld.xz * (0.014 / 8.0) + vec2<f32>(0.31, 0.17) +
                                   vec2<f32>(camera.waterMotion.x * 0.0012, 0.0),
                                   0.0).b * 2.0 - 1.0;
    let flow2 = textureSampleLevel(waterNoiseTex, waterNoiseSampler,
                                   posWorld.xz * (0.032 / 8.0) + vec2<f32>(0.67, 0.41) +
                                   vec2<f32>(0.0, -camera.waterMotion.x * 0.0017),
                                   0.0).b * 2.0 - 1.0;
    let flowBands = flow1 * 0.5 + flow2 * 0.22;
    let flowLift = clamp(flowBands * 0.5 + 0.5, 0.0, 1.0);
    // The water's own in-scattered colour. Reservoir water is a near-uniform
    // depth, so let the flow noise shift the shallow↔deep colour a little to give
    // the turquoise some mottled tonal life instead of a flat painted sheet.
    let bandMix = clamp(depthCurve + (flowLift - 0.5) * 0.55, 0.0, 1.0);
    let waterTint = mix(shallow, deep, bandMix) * mix(0.90, 1.10, flowLift) *
                    mix(0.72, 1.0, shadowVisibility);
    // Skylight scatters inside the water column, keeping it luminous even when
    // viewed from above where the Fresnel mirror term is nearly zero.
    let skyAmbient = skyGradient(vec3<f32>(0.0, 1.0, 0.0)) * 0.10;
    // Looking straight into the water: bed dimmed by the column + water scatter.
    let refracted = bedLit * transmit + (waterTint + skyAmbient) * (1.0 - transmit);

    // ── Reflection ───────────────────────────────────────────────────────────
    // Exact Schlick Fresnel for air/water (IOR 1.333).  The art control scales
    // physical reflectance but never turns normal-incidence water into a mirror.
    let cosTheta = clamp(dot(n, viewDirWS), 0.0, 1.0);
    let reflectionStrength = clamp(camera.waterColorA.a, 0.0, 1.0);
    let waterF0 = 0.02037;
    let reflectance = clamp((waterF0 + (1.0 - waterF0) *
                            pow(1.0 - cosTheta, 5.0)) * reflectionStrength,
                            0.0, 0.96);

    let reflectedDir = reflect(-viewDirWS, n);
    // The LUT is HDR, while this path writes directly to an unorm target.
    // Bound just the reflected radiance so glancing water retains hue instead
    // of becoming a featureless white sheet before presentation.
    let reflectedSky = min(sampleSkyLUT(reflectedDir), vec3<f32>(1.15));
    let warmSky = mix(reflectedSky, vec3<f32>(0.96, 0.68, 0.50), 0.22);

    // Near normal incidence the terrain reflection contributes only a few
    // percent. Keep smooth sky reflection and avoid a 48-step SSR march there.
    // Grazing water, where reflections are visible, retains the full path.
    var terrainReflection = vec4<f32>(0.0);
    if (reflectance > 0.08) {
        terrainReflection = sampleScreenTerrainReflection(posWorld + n * 0.25, reflectedDir, dims);
    }
    // Single-frame SSR is useful as a hint near cliffs, not as the base mirror.
    // Keep its weight conservative so ray misses cannot carve dark islands into
    // the ocean.  The stable sky LUT remains the reflection fallback.
    let ssrWeight = terrainReflection.a * 0.24 * smoothstep(0.08, 0.30, reflectance);
    let reflectedScene = mix(warmSky, terrainReflection.rgb, ssrWeight);

    var surface = mix(refracted, reflectedScene, reflectance);

    // ── Sun glint (specular) ─────────────────────────────────────────────────
    // GGX microfacet highlight.  Unlike the old Blinn exponent this preserves
    // an intense, compact glint while roughness broadens it naturally.
    let halfVec = normalize(sunDir + viewDirWS);
    let roughness = clamp(camera.waterParams.w, 0.02, 1.0);
    let noV = max(dot(n, viewDirWS), 0.001);
    let noL = max(dot(n, sunDir), 0.0);
    let noH = max(dot(n, halfVec), 0.0);
    let voH = max(dot(viewDirWS, halfVec), 0.0);
    let alpha = max(roughness * roughness, 0.0025);
    let alpha2 = alpha * alpha;
    let denom = noH * noH * (alpha2 - 1.0) + 1.0;
    let distribution = alpha2 / max(3.14159265 * denom * denom, 0.0001);
    let gv = noL * sqrt(noV * noV * (1.0 - alpha2) + alpha2);
    let gl = noV * sqrt(noL * noL * (1.0 - alpha2) + alpha2);
    let visibility = 0.5 / max(gv + gl, 0.0001);
    let microFresnel = waterF0 + (1.0 - waterF0) * pow(1.0 - voH, 5.0);
    let sunGlint = min(distribution * visibility * microFresnel * noL, 0.65);
    surface += vec3<f32>(1.0, 0.86, 0.58) * sunGlint * 0.18 * shadowVisibility;

    // ── Shore foam ───────────────────────────────────────────────────────────
    // Foam hugs the waterline (shallow depth). It is lit geometry, so darken it
    // by the sun shadow — otherwise it glows in the dark at the canyon bottom.
    let shoreline = shoreMask * (1.0 - smoothstep(0.05, 0.5, depth01));
    // Ripple noise (A channel, 16 periods per tile) replaces per-pixel simplex.
    let shoreRipple = textureSampleLevel(waterNoiseTex, waterNoiseSampler,
                                         posWorld.xz * (0.18 / 16.0) + vec2<f32>(0.13, 0.57) +
                                         vec2<f32>(camera.waterMotion.x * 0.003, 0.0),
                                         0.0).a;
    let persistentFoam = textureSampleLevel(
        waterFoamTex, waterDisplacementSampler, posWorld.xz / 96.0, 0.0).x;
    let whitecap = persistentFoam * smoothstep(0.025, 0.12, waveCrest);
    let foamAmount = shoreline * smoothstep(0.30, 0.85, shoreRipple) * 0.58 +
                     whitecap * mix(0.55, 0.90, shoreRipple);
    let foam = vec3<f32>(0.90, 0.88, 0.78) * foamAmount;
    let shoreShadow = mix(0.4, 1.0, shadowVisibility);
    surface += foam * shoreShadow;

    // ── Distance fog ─────────────────────────────────────────────────────────
    // Same exponential fog as the terrain pass (colour and 0.7 cap) so distant
    // water sinks into the canyon haze instead of popping against it.
    let dist = length(posView);
    let fogColor = vec3<f32>(0.78, 0.64, 0.54);
    let fogFactor = clamp(1.0 - exp(-camera.metrics.w * dist), 0.0, 0.7);
    return mix(surface, fogColor, fogFactor);
}

// ─────────────────────────────────────────────────────────────────────────────
// Fragment Shader
// ─────────────────────────────────────────────────────────────────────────────

@fragment
fn fs(i : VSOut) -> @location(0) vec4<f32> {
    // Get texture dimensions and compute pixel coordinates
    let dims = textureDimensions(depthTex, 0);
    let dimsF = vec2<f32>(f32(dims.x), f32(dims.y));
    let maxCoord = vec2<i32>(i32(dims.x) - 1, i32(dims.y) - 1);
    let pixF = i.uv * dimsF;
    let pixelI = clamp(vec2<i32>(floor(pixF)), vec2<i32>(0, 0), maxCoord);
    let ndcCenter = ndcFromPixel(pixelI, dimsF);
    let depthCenter = textureLoad(depthTex, pixelI, 0).x;
    let shadowFactor = textureLoad(shadowTex, pixelI, 0).x;
    // Material id in the integer part; shore influence (scaled by 0.49)
    // packed in the fraction by the raycast pass.
    let materialRaw = textureLoad(materialTex, pixelI, 0).x;
    let material = u32(materialRaw + 0.5);

    let invProjParams = camera.invProjParams.xy;
    
    // ─────────────────────────────────────────────────────────────────────────
    // Sky Rendering
    // ─────────────────────────────────────────────────────────────────────────
    // If depth < 0, this pixel shows sky (no terrain intersection).
    // Scattering, gradient, and clouds are static and come from the baked
    // LUT (see sky_lut.wgsl). Only the sun disc and its bloom stay analytic:
    // they are sharper than the LUT resolution.
    if (depthCenter < 0.0) {
        // Check for raycast failure sentinel (-2.0)
        if (depthCenter < -1.5) {
            return vec4<f32>(1.0, 0.0, 0.5, 1.0); // Hot Pink
        }

        let viewDir = rayDirFromPixel(invProjParams, ndcCenter);
        let worldDir = normalize((camera.invView * vec4<f32>(viewDir, 0.0)).xyz);
        let sunDir = normalize(camera.lightDirWS.xyz);

        var skyColor = sampleSkyLUT(worldDir);

        // Sun disc with multi-layer bloom
        let sunAngle = acos(clamp(dot(worldDir, sunDir), -1.0, 1.0));
        let sunAngularRadius = 0.0087; // ~0.5 degrees in radians
        let sunDisc = smoothstep(sunAngularRadius * 1.2, sunAngularRadius * 0.8, sunAngle);
        let sunColor = vec3<f32>(1.0, 0.95, 0.85) * 5.0;
        let bloom = exp(-sunAngle * 8.0) * 0.5 +
                    exp(-sunAngle * 3.0) * 0.25 +
                    exp(-sunAngle * 1.0) * 0.1;
        skyColor += sunColor * sunDisc + vec3<f32>(1.0, 0.9, 0.7) * bloom;

        // Tone mapping (simple Reinhard), gamma, warm grade
        skyColor = skyColor / (skyColor + vec3<f32>(1.0));
        skyColor = pow(skyColor, vec3<f32>(1.0 / 2.2));
        skyColor = mix(skyColor, vec3<f32>(1.00, 0.64, 0.54), 0.18);
        return vec4<f32>(skyColor, 1.0);
    }
    
    // ─────────────────────────────────────────────────────────────────────────
    // Position Reconstruction
    // ─────────────────────────────────────────────────────────────────────────
    let posCView = viewPosFromDepth(invProjParams, ndcCenter, depthCenter);
    let posCWorld = viewToWorld(camera.invView, posCView);

    if (material == MATERIAL_WATER) {
        let viewDirWS = normalize(camera.cameraPos.xyz - posCWorld);
        // Water packs depth in the magnitude and its binary shadow in the sign.
        let packedShadow = textureLoad(shadowTex, pixelI, 0).x;
        let waterDepth = max(abs(packedShadow) - 1.0, 0.0);
        let waterShadow = select(0.0, 1.0, packedShadow >= 0.0);
        // Shore proximity computed once in the raycast pass (world space),
        // unpacked from the material fraction. Replaces a 32-tap screen mask.
        let shoreMask = clamp((materialRaw - f32(material)) / 0.49, 0.0, 1.0);
        // Compute the wave normal once, here, so the debug normal view shows
        // the real surface normal instead of a flat placeholder.
        let waterWave = waterWaveField(posCWorld.xz);
        let waterNormal = waterWaveNormal(waterWave);
        let waterColor = shadeWater(posCWorld, posCView, viewDirWS, waterNormal,
                                    waterWave.w, waterDepth, shoreMask, waterShadow, dims);
        let outputColor = applyDebugVisualization(waterColor, depthCenter, waterNormal);
        return vec4<f32>(outputColor, 1.0);
    }
    
    // ─────────────────────────────────────────────────────────────────────────
    // Screen-Space Normal Reconstruction
    // ─────────────────────────────────────────────────────────────────────────
    // Sample neighbor depths to reconstruct surface normal
    let offsetX = vec2<i32>(1, 0);
    let offsetY = vec2<i32>(0, 1);
    let depthNegX = sampleDepth(pixelI - offsetX, maxCoord);
    let depthPosX = sampleDepth(pixelI + offsetX, maxCoord);
    let depthNegY = sampleDepth(pixelI - offsetY, maxCoord);
    let depthPosY = sampleDepth(pixelI + offsetY, maxCoord);
    
    // Choose closer neighbor for each axis to handle depth discontinuities
    // Logic optimization:
    // 1. If x=0 (Left Edge): (pixelI.x > 0) is false -> useNegX = false -> Forces PosX.
    // 2. If x=Max (Right Edge): (pixelI.x >= dims.x - 1) is true -> useNegX = true -> Forces NegX.
    // 3. Middle: Uses the standard depth difference comparison.
    let useNegX = (pixelI.x > 0) && ( (pixelI.x >= i32(dims.x) - 1) || (abs(depthNegX - depthCenter) < abs(depthPosX - depthCenter)) );
    let useNegY = (pixelI.y > 0) && ( (pixelI.y >= i32(dims.y) - 1) || (abs(depthNegY - depthCenter) < abs(depthPosY - depthCenter)) );
    
    var depthX = select(depthPosX, depthNegX, useNegX);
    var depthY = select(depthPosY, depthNegY, useNegY);
    var coordX = select(pixelI + offsetX, pixelI - offsetX, useNegX);
    var coordY = select(pixelI + offsetY, pixelI - offsetY, useNegY);
    
    // Reconstruct positions for neighbor pixels
    let ndcX = ndcFromPixel(coordX, dimsF);
    let ndcY = ndcFromPixel(coordY, dimsF);
    let posXView = viewPosFromDepth(invProjParams, ndcX, depthX);
    let posYView = viewPosFromDepth(invProjParams, ndcY, depthY);
    
    // Compute normal from cross product of position differences. Force dx/dy to always represent
    // the positive screen-space axis directions so the cross product remains stable regardless
    // of which neighbor sample was selected to avoid depth discontinuities.
    var dx = posXView - posCView;
    var dy = posYView - posCView;
    if (useNegX) { dx = -dx; }
    if (useNegY) { dy = -dy; }
    // On perfectly flat surfaces / distant horizons the cross product can be zero;
    // normalize() of a zero vector yields NaN and corrupts the lighting. Fall back
    // to an up-facing normal when the length is degenerate.
    let n = cross(dx, dy);
    let nLen = length(n);
    var normal = select(vec3<f32>(0.0, 1.0, 0.0), n / nLen, nLen > 1e-6);

    // ─────────────────────────────────────────────────────────────────────────
    // Texture Sampling
    // ─────────────────────────────────────────────────────────────────────────
    let uvTerrain = terrainUV(posCWorld);
    let albedo = textureSampleLevel(terrainTex, terrainSampler, uvTerrain, 0.0).xyz;
    let lightVisibility = textureSampleLevel(lightmapTex, terrainSampler, uvTerrain, 0.0).x;
    
    // ─────────────────────────────────────────────────────────────────────────
    // Lighting
    // ─────────────────────────────────────────────────────────────────────────
    // Light direction is pre-transformed to view-space by CPU
    let lightDir = camera.lightDirVS.xyz;
    
    // Diffuse (Lambertian)
    let diffuse = max(dot(normal, lightDir), 0.0);
    
    // Apply Shadow from Ray Tracing
    let finalDiffuse = diffuse * shadowFactor;

    // Fixed ambient intensity
    let ambient = max(camera.lightDirVS.w, 0.05);
    
    // Specular (roughness-based Blinn-Phong)
    let viewDir = normalize(-posCView);
    let halfVec = normalize(lightDir + viewDir);

    var roughness = 0.6;
    if (camera.invProjParams.z > 0.5) {
        roughness = 0.2; // Shiny plastic for Lego
    }

    let specPower = max((1.0 - roughness) * 160.0, 8.0);  // ~64 for roughness 0.6
    let specStrength = mix(0.04, 0.25, 1.0 - roughness);  // ~0.124 for roughness 0.6
    let specularTerm = pow(max(dot(normal, halfVec), 0.0), specPower);
    // Apply shadow to specular as well
    let specular = specStrength * specularTerm * lightVisibility * shadowFactor;
    
    // Combine lighting components
    let warmLight = vec3<f32>(1.10, 0.96, 0.84);
    let coolShadow = vec3<f32>(0.90, 0.96, 1.02);
    let grade = mix(coolShadow, warmLight, clamp(finalDiffuse * lightVisibility + 0.35, 0.0, 1.0));
    let litColor = albedo * (finalDiffuse * lightVisibility + ambient) * grade + specular * vec3<f32>(1.0, 0.88, 0.70);
    
    // ─────────────────────────────────────────────────────────────────────────
    // Fog / Underwater Medium
    // ─────────────────────────────────────────────────────────────────────────
    let dist = length(posCView);
    var finalColor : vec3<f32>;
    let cameraUnderwater = camera.waterParams.y > 0.5 &&
                           camera.cameraPos.y < camera.waterParams.x;
    if (cameraUnderwater) {
        // The medium is water, not air: attenuate the bed by the path length
        // through the water and fade toward the deep water colour.
        let transmit = exp(-WATER_EXTINCTION * dist);
        finalColor = litColor * transmit + camera.waterColorB.rgb * (1.0 - transmit);
    } else {
        // Exponential fog capped at 0.7 to maintain visibility at distance
        let fogDensity = camera.metrics.w;
        let fogColor = vec3<f32>(0.78, 0.64, 0.54);  // Warm canyon haze
        let fogFactor = clamp(1.0 - exp(-fogDensity * dist), 0.0, 0.7);
        finalColor = mix(litColor, fogColor, fogFactor);
    }
    
    // ─────────────────────────────────────────────────────────────────────────
    // Apply Debug Visualization (if enabled)
    // ─────────────────────────────────────────────────────────────────────────
    let outputColor = applyDebugVisualization(finalColor, depthCenter, normal);
    
    return vec4<f32>(outputColor, 1.0);
}

// ─────────────────────────────────────────────────────────────────────────────
// Debug Visualization Helper
// ─────────────────────────────────────────────────────────────────────────────

/// Apply debug visualization based on mode
fn applyDebugVisualization(finalColor : vec3<f32>, depth : f32, 
                           normal : vec3<f32>) -> vec3<f32> {
    switch (debug.mode) {
        case 1u: {  // Depth visualization
            let maxD = select(5000.0, debug.maxDepth, debug.maxDepth > 0.0);
            let d = clamp(depth / maxD, 0.0, 1.0);
            // Color gradient: white (near) -> yellow -> red -> dark (far)
            let r = 1.0 - d * 0.5;
            let g = 1.0 - d;
            let b = 1.0 - d * 1.5;
            return vec3<f32>(r, max(g, 0.0), max(b, 0.0));
        }
        case 2u: {  // Normal visualization
            // Map normals from [-1,1] to [0,1] for visualization
            return normal * 0.5 + 0.5;
        }
        case 3u: {  // Mip level heat map (placeholder - would need mip info from raycast)
            // For now, show depth-based gradient as mip levels correlate with distance
            let d = clamp(depth / 1000.0, 0.0, 1.0);
            let mipApprox = min(u32(d * 7.0), 7u);
            // Use switch instead of array indexing (WGSL requires constant array indices)
            switch (mipApprox) {
                case 0u: { return vec3<f32>(1.0, 0.0, 0.0); }  // Mip 0: Red
                case 1u: { return vec3<f32>(1.0, 0.5, 0.0); }  // Mip 1: Orange
                case 2u: { return vec3<f32>(1.0, 1.0, 0.0); }  // Mip 2: Yellow
                case 3u: { return vec3<f32>(0.0, 1.0, 0.0); }  // Mip 3: Green
                case 4u: { return vec3<f32>(0.0, 1.0, 1.0); }  // Mip 4: Cyan
                case 5u: { return vec3<f32>(0.0, 0.0, 1.0); }  // Mip 5: Blue
                case 6u: { return vec3<f32>(0.5, 0.0, 1.0); }  // Mip 6: Purple
                default: { return vec3<f32>(1.0, 0.0, 1.0); }  // Mip 7: Magenta
            }
        }
        default: {  // mode == 0: no debug visualization
            return finalColor;
        }
    }
}
