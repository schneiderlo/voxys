// ═══════════════════════════════════════════════════════════════════════════════
// ray_blit.wgsl - Fullscreen Lighting Pass Shader
// ═══════════════════════════════════════════════════════════════════════════════
// Composites the ray-caster output into a final linear-HDR image and applies
// voxys' physically based ocean material.
// Features:
//   - Fullscreen triangle vertex shader (single oversized triangle)
//   - Depth texture sampling from ray-cast pass
//   - Mip-filtered baked sky/environment rendering
//   - Position reconstruction from depth
//   - Screen-space normal reconstruction (picking closer neighbor)
//   - Exact dielectric Fresnel, Beer-Lambert refraction, foam, and TIR
//   - ACES/grain/vignette presentation
//   - Underwater distortion, absorption, and sun shafts
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
// Water-only auxiliary output: accepted surface slope and crest/shore data.
@group(0) @binding(3) var materialTex : texture_2d<f32>;
@group(0) @binding(4) var terrainTex : texture_2d<f32>;
@group(0) @binding(5) var lightmapTex : texture_2d<f32>;
@group(0) @binding(6) var terrainSampler : sampler;
@group(0) @binding(7) var<uniform> debug : DebugUniforms;
// Paraboloid-mapped static sky (scattering + gradient + clouds), baked once
// by sky_lut.wgsl. See that file for the mapping.
@group(0) @binding(8) var skyLUT : texture_2d<f32>;
// Asset-free surface-foam network generated deterministically by BlitPath.
// It is a scalar linear mask with a complete mip chain and repeat addressing.
@group(0) @binding(9) var oceanFoamTex : texture_2d<f32>;
@group(0) @binding(10) var oceanFoamSampler : sampler;
// Exact terrain/sky color for a settled camera. Only fsCached references it;
// the direct and background-refresh pipelines retain their original layout.
@group(0) @binding(11) var backgroundTex : texture_2d<f32>;

const MATERIAL_SKY : u32 = 0u;
const MATERIAL_TERRAIN : u32 = 1u;
const MATERIAL_WATER : u32 = 2u;

// Ocean colors are linear sRGB. Absorption is a Beer-Lambert coefficient,
// not a display tint.
const OCEAN_ABSORPTION : vec3<f32> =
    vec3<f32>(0.015208514, 0.009134059, 0.008568126);
const OCEAN_SURFACE_COLOR : vec3<f32> =
    vec3<f32>(0.015996293, 0.135633330, 0.090841711);
const OCEAN_SCATTER_COLOR : vec3<f32> =
    vec3<f32>(0.015996293, 0.061246054, 0.099898728);
const OCEAN_FOG_COLOR : vec3<f32> =
    vec3<f32>(0.274677312, 0.327778098, 0.366252596);
const OCEAN_SKY_BRIGHTNESS : f32 = 0.9;
const OCEAN_SUN_INTENSITY : f32 = 2.5;
const OCEAN_IOR : f32 = 1.31;
const OCEAN_DISTORTION_STRENGTH : f32 = 0.20;
const OCEAN_REFLECTION_ROUGHNESS_DISTANCE : f32 = 1500.0;
const OCEAN_REFLECTION_ROUGHNESS_STRENGTH : f32 = 0.50;
const OCEAN_MINIMUM_ROUGHNESS : f32 = 0.02;
const OCEAN_FOG_NEAR : f32 = 1000.0;
const OCEAN_FOG_FAR : f32 = 10000.0;
const OCEAN_FOAM_SIZE : f32 = 261.0;
const OCEAN_FOAM_OPACITY : f32 = 0.30;
const OCEAN_FOAM_COVERAGE : f32 = 0.21;
const OCEAN_FILM_GRAIN : f32 = 0.06;
const OCEAN_VIGNETTE : f32 = 0.25;
const OCEAN_VIGNETTE_SMOOTHNESS : f32 = 0.85;
const OCEAN_UNDERWATER_DISTORTION : f32 = 0.015;
const OCEAN_UNDERWATER_SCALE : f32 = 4.0;
const OCEAN_UNDERWATER_SPEED : f32 = 1.2;
const OCEAN_SUN_SHAFT_INTENSITY : f32 = 0.20;

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


// ─────────────────────────────────────────────────────────────────────────────
// Ocean material and underwater composite
// ─────────────────────────────────────────────────────────────────────────────

fn waterWaveNormal(wave : vec4<f32>) -> vec3<f32> {
    return normalize(vec3<f32>(-wave.y, 1.0, -wave.z));
}

// Exact unpolarized dielectric Fresnel. The sign of cosine
// distinguishes air-to-water from water-to-air and preserves total internal
// reflection on the underside.
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

fn srgbToLinear(encoded : vec3<f32>) -> vec3<f32> {
    let low = encoded / 12.92;
    let high = pow((encoded + vec3<f32>(0.055)) / 1.055,
                   vec3<f32>(2.4));
    return select(high, low, encoded <= vec3<f32>(0.04045));
}

fn applyPostEffects(colorIn : vec3<f32>, uv : vec2<f32>,
                       dims : vec2<u32>) -> vec3<f32> {
    let pixel = uv * vec2<f32>(f32(max(dims.x, 1u)),
                               f32(max(dims.y, 1u)));
    let grain = fract(52.9829189 *
        fract(dot(pixel + vec2<f32>(camera.waterMotion.x),
                  vec2<f32>(0.06711056, 0.00583715))));
    var color = colorIn *
        (1.0 + OCEAN_FILM_GRAIN * clamp(grain + 0.1, 0.0, 1.0));
    let radius = length(uv - vec2<f32>(0.5)) * 2.0;
    let vignette = smoothstep(1.0 - OCEAN_VIGNETTE_SMOOTHNESS,
                              1.0, radius);
    color *= 1.0 - vignette * OCEAN_VIGNETTE;
    return color;
}

// The swapchain uses BGRA8Unorm, so perform the final transfer explicitly.
fn presentColor(color : vec3<f32>, uv : vec2<f32>,
                   dims : vec2<u32>) -> vec3<f32> {
    return linearToSrgb(
        applyPostEffects(acesFilmic(color), uv, dims));
}

fn sampleWaterEnvironment(directionIn : vec3<f32>, roughness : f32) -> vec3<f32> {
    let direction = normalize(directionIn);
    let uv = direction.xz / (1.0 + max(direction.y, 0.0)) * 0.5 +
             vec2<f32>(0.5);
    // Same footprint-to-mip conversion as the Vulkan material, adjusted from
    // its 4K HDRI to voxys's 512px baked environment.
    let lod = log2(max(roughness * 0.035 * 512.0, 1.0));
    return textureSampleLevel(skyLUT, terrainSampler, uv, lod).rgb *
           OCEAN_SKY_BRIGHTNESS;
}

fn periodicGradientHash(cellIn : vec2<f32>) -> vec2<f32> {
    let cell = cellIn - floor(cellIn / 16.0) * 16.0;
    let phase = vec2<f32>(dot(cell, vec2<f32>(127.1, 311.7)),
                          dot(cell, vec2<f32>(269.5, 183.3)));
    return fract(sin(phase) * 43758.5453123) * 2.0 - vec2<f32>(1.0);
}

fn periodicGradientNoise(point : vec2<f32>) -> f32 {
    let cell = floor(point);
    let local = fract(point);
    let fade = local * local * local *
               (local * (local * 6.0 - vec2<f32>(15.0)) +
                vec2<f32>(10.0));
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

fn applyUnderwaterMedium(color : vec3<f32>, toFragment : vec3<f32>,
                            pathLength : f32) -> vec3<f32> {
    let transmittance = exp(-OCEAN_ABSORPTION * pathLength);
    var submerged = mix(OCEAN_SCATTER_COLOR, color, transmittance);
    let sunAlignment = max(dot(normalize(toFragment),
                               normalize(camera.lightDirWS.xyz)), 0.0);
    var shaft = pow(sunAlignment, 12.0) * OCEAN_SUN_SHAFT_INTENSITY;
    shaft *= 1.0 - exp(-pathLength * 0.015);
    submerged += vec3<f32>(1.0, 0.95, 0.85) * shaft;
    return submerged;
}

// Refraction reconstructs the distorted bed lookup from terrain albedo,
// light visibility, and the packed vertical water depth.
fn shadeOcean(posWorld : vec3<f32>, posView : vec3<f32>,
                 view : vec3<f32>, normal : vec3<f32>, waterDepth : f32,
                 shadowVisibility : f32, dims : vec2<u32>) -> vec3<f32> {
    let distanceToCamera = max(length(posView), 1.0e-6);
    let light = normalize(camera.lightDirWS.xyz);
    let viewFacing = dot(normal, view);
    let fresnel = dielectricFresnel(viewFacing, OCEAN_IOR);

    let reflected = reflect(-view, normal);
    let reflectionRoughness = OCEAN_MINIMUM_ROUGHNESS +
        clamp(distanceToCamera / OCEAN_REFLECTION_ROUGHNESS_DISTANCE,
              0.0, 1.0) * OCEAN_REFLECTION_ROUGHNESS_STRENGTH;
    let environment = sampleWaterEnvironment(reflected, reflectionRoughness);

    let scatterLobe =
        pow(clamp((dot(view, -light) + 0.5) / 1.5, 0.0, 1.0) *
            clamp(dot(normal, -light) + 0.3, 0.0, 1.0), 0.85) *
        OCEAN_SUN_INTENSITY * 0.35 *
        (1.0 - smoothstep(100.0, 6400.0, distanceToCamera));
    let body = mix(OCEAN_SCATTER_COLOR, OCEAN_SURFACE_COLOR,
                   clamp(scatterLobe, 0.0, 1.0));
    let forwardScatter = pow(max(dot(view, -light), 0.0), 3.0) *
                         max(1.0 - normal.y, 0.0);

    let halfway = normalize(light + view);
    let sunSpecular = pow(max(dot(normal, halfway), 0.0), 420.0) *
                      OCEAN_SUN_INTENSITY * 4.0;

    let incomingRay = -view;
    let thickness = clamp(
        waterDepth / max(abs(incomingRay.y), 0.12), 0.0, 500.0);
    let refractedRay = refract(incomingRay, normal, 1.0 / OCEAN_IOR);
    let refractedTravel = waterDepth / max(-refractedRay.y, 0.12);
    var bedWorld = posWorld + refractedRay * refractedTravel;

    // Project a normal-offset point, then intersect that camera ray with the
    // packed bed plane. This retains magnification without an opaque
    // scene-color target.
    let distortionDistance = min(thickness, 80.0) *
                             OCEAN_DISTORTION_STRENGTH;
    let distortedClip = camera.viewProj *
                        vec4<f32>(posWorld + normal * distortionDistance, 1.0);
    if (distortedClip.w > 1.0e-5) {
        let distortedNdc = distortedClip.xy / distortedClip.w;
        let distortedViewRay = rayDirFromPixel(camera.invProjParams.xy,
                                               distortedNdc);
        let distortedWorldRay = normalize(
            (camera.invView * vec4<f32>(distortedViewRay, 0.0)).xyz);
        let bedY = posWorld.y - waterDepth;
        let bedTravel = (bedY - camera.cameraPos.y) /
                        min(distortedWorldRay.y, -1.0e-4);
        if (bedTravel > 0.0 && bedTravel < 50000.0) {
            bedWorld = camera.cameraPos.xyz + distortedWorldRay * bedTravel;
        }
    }

    let bedUv = terrainUV(bedWorld);
    let bedEncoded = textureSampleLevel(terrainTex, terrainSampler,
                                        bedUv, 0.0).rgb;
    let bedAlbedo = srgbToLinear(bedEncoded);
    let bedLight = textureSampleLevel(lightmapTex, terrainSampler,
                                      bedUv, 0.0).x;
    let refracted = bedAlbedo *
                    (0.25 + 0.75 * bedLight * shadowVisibility);
    let transmittance = exp(-OCEAN_ABSORPTION * thickness);
    let refractedWater = refracted * transmittance +
                         body * (vec3<f32>(1.0) - transmittance);
    let reflectedWater = environment +
        vec3<f32>(1.0, 0.96, 0.82) * sunSpecular +
        OCEAN_SCATTER_COLOR * forwardScatter * OCEAN_SUN_INTENSITY;

    let foamUv = posWorld.xz / OCEAN_FOAM_SIZE;
    let worldPerPixel = distanceToCamera * 2.0 *
        max(camera.invProjParams.x / f32(max(dims.x, 1u)),
            camera.invProjParams.y / f32(max(dims.y, 1u)));
    let foamLod = log2(max(worldPerPixel * 1024.0 / OCEAN_FOAM_SIZE,
                           1.0));
    let foamPattern = textureSampleLevel(oceanFoamTex,
        oceanFoamSampler, foamUv, foamLod).r;
    let threshold = 1.0 - OCEAN_FOAM_COVERAGE;
    let coverage = foamPattern *
        smoothstep(threshold, threshold + 0.15, foamPattern);
    let foamStrength = clamp(coverage * OCEAN_FOAM_OPACITY, 0.0, 1.0);

    if (camera.cameraPos.y < camera.waterParams.x) {
        let undersideNormal = -normal;
        var transmissionDirection =
            refract(-view, undersideNormal, OCEAN_IOR);
        let totalInternalReflection =
            dot(transmissionDirection, transmissionDirection) < 0.001;
        if (totalInternalReflection) {
            transmissionDirection = vec3<f32>(0.0, 1.0, 0.0);
        } else {
            transmissionDirection = normalize(transmissionDirection);
        }
        let transmitted = sampleWaterEnvironment(transmissionDirection,
                                               reflectionRoughness);
        var underside = mix(transmitted, environment, fresnel);
        underside += vec3<f32>(1.0, 0.96, 0.82) * sunSpecular;
        underside = mix(underside, vec3<f32>(0.94, 0.98, 1.0),
                        foamStrength * 0.35);
        let cameraTransmittance =
            exp(-OCEAN_ABSORPTION * distanceToCamera);
        return mix(OCEAN_SCATTER_COLOR, underside,
                   cameraTransmittance);
    }

    // Foam suppresses Fresnel before its own color is composited.
    var color = mix(refractedWater, reflectedWater,
                    fresnel * clamp(1.0 - foamStrength * 2.0, 0.0, 1.0));
    color = mix(color, vec3<f32>(0.94, 0.98, 1.0), foamStrength);
    let fog = smoothstep(OCEAN_FOG_NEAR, OCEAN_FOG_FAR,
                         distanceToCamera);
    return mix(color, OCEAN_FOG_COLOR, fog);
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
    let outputPixelI = clamp(vec2<i32>(floor(pixF)),
                             vec2<i32>(0, 0), maxCoord);
    let cameraUnderwater = camera.waterParams.y > 0.5 &&
                           camera.cameraPos.y < camera.waterParams.x;
    var pixelI = outputPixelI;
    // Distort the resolved scene wherever the camera-to-fragment path is
    // submerged. Static-cache refreshes deliberately
    // carry time zero; fsCached applies the live offset when it reuses them.
    if (cameraUnderwater && abs(camera.waterMotion.x) > 1.0e-6) {
        let underwaterUv = underwaterDistortionUv(i.uv);
        pixelI = clamp(vec2<i32>(floor(underwaterUv * dimsF)),
                       vec2<i32>(0, 0), maxCoord);
    }
    let ndcCenter = ndcFromPixel(pixelI, dimsF);
    let depthCenter = textureLoad(depthTex, pixelI, 0).x;

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

        if (cameraUnderwater) {
            // The source's enclosure supplies a finite path for rays which do
            // not meet the seabed, preventing bright horizon cracks.
            skyColor = applyUnderwaterMedium(skyColor, worldDir, 500.0);
        }
        return vec4<f32>(presentColor(skyColor, i.uv, dims), 1.0);
    }
    
    // ─────────────────────────────────────────────────────────────────────────
    // Position Reconstruction
    // ─────────────────────────────────────────────────────────────────────────
    let posCView = viewPosFromDepth(invProjParams, ndcCenter, depthCenter);
    let posCWorld = viewToWorld(camera.invView, posCView);
    let packedShadow = textureLoad(shadowTex, pixelI, 0).x;

    // Terrain shadows are in [0, 1]. Water stores signed depth + 1 and its
    // accepted water depth is always positive, so these ranges cannot
    // overlap. Only water needs the material texture's shoreline fraction.
    if (abs(packedShadow) > 1.0) {
        let waterData = textureLoad(materialTex, pixelI, 0);
        let viewDirWS = normalize(camera.cameraPos.xyz - posCWorld);
        // Water packs depth in the magnitude and its binary shadow in the sign.
        let waterDepth = max(abs(packedShadow) - 1.0, 0.0);
        let waterShadow = select(0.0, 1.0, packedShadow >= 0.0);
        // The intersection pass already sampled this exact FFT/coastal field at
        // the accepted hit. Reuse its slope instead of evaluating the wave
        // cascades and coastal field a second time.
        let waterWave = vec4<f32>(0.0, waterData.x, waterData.y, waterData.z);
        let waterNormal = waterWaveNormal(waterWave);
        let waterColor = shadeOcean(posCWorld, posCView, viewDirWS,
                                       waterNormal, waterDepth, waterShadow,
                                       dims);
        var outputColor = presentColor(waterColor, i.uv, dims);
        if (debug.mode != 0u) {
            outputColor = applyDebugVisualization(waterColor, depthCenter,
                                                  waterNormal);
        }
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
    let albedoEncoded = textureSampleLevel(
        terrainTex, terrainSampler, uvTerrain, 0.0).xyz;
    // Terrain JPEGs are currently uploaded as UNORM. Decode them here so the
    // refracted bed, opaque scene, ACES curve, and Vulkan path share one linear
    // working space.
    let albedo = srgbToLinear(albedoEncoded);
    let lightVisibility = textureSampleLevel(lightmapTex, terrainSampler, uvTerrain, 0.0).x;
    
    // ─────────────────────────────────────────────────────────────────────────
    // Lighting
    // ─────────────────────────────────────────────────────────────────────────
    // Light direction is pre-transformed to view-space by CPU
    let lightDir = camera.lightDirVS.xyz;
    
    // Diffuse (Lambertian)
    let diffuse = max(dot(normal, lightDir), 0.0);
    
    // Apply Shadow from Ray Tracing
    let finalDiffuse = diffuse * packedShadow;

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
    let specular = specStrength * specularTerm * lightVisibility * packedShadow;
    
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
    if (cameraUnderwater) {
        // Only attenuate geometry below the current water surface; cliffs above
        // it remain in air even when the camera is submerged.
        if (posCWorld.y <= camera.waterParams.x + 0.5) {
            finalColor = applyUnderwaterMedium(
                litColor, posCWorld - camera.cameraPos.xyz, dist);
        } else {
            finalColor = litColor;
        }
    } else {
        let fog = smoothstep(OCEAN_FOG_NEAR, OCEAN_FOG_FAR, dist);
        finalColor = mix(litColor, OCEAN_FOG_COLOR, fog);
    }
    
    // ─────────────────────────────────────────────────────────────────────────
    // Apply Debug Visualization (if enabled)
    // ─────────────────────────────────────────────────────────────────────────
    var outputColor = presentColor(finalColor, i.uv, dims);
    if (debug.mode != 0u) {
        outputColor = applyDebugVisualization(finalColor, depthCenter, normal);
    }
    
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

// Settled-camera specialization. Static terrain and sky are copied from the
// exact full-resolution background refresh; animated ocean shading remains
// live, and submerged views apply screen-space distortion.
@fragment
fn fsCached(i : VSOut) -> @location(0) vec4<f32> {
    let dims = textureDimensions(depthTex, 0);
    let dimsF = vec2<f32>(f32(dims.x), f32(dims.y));
    let maxCoord = vec2<i32>(i32(dims.x) - 1, i32(dims.y) - 1);
    let outputPixelI = clamp(
        vec2<i32>(floor(i.uv * dimsF)), vec2<i32>(0, 0), maxCoord);
    let cameraUnderwater = camera.waterParams.y > 0.5 &&
                           camera.cameraPos.y < camera.waterParams.x;
    var pixelI = outputPixelI;
    if (cameraUnderwater) {
        pixelI = clamp(vec2<i32>(floor(
                           underwaterDistortionUv(i.uv) * dimsF)),
                       vec2<i32>(0, 0), maxCoord);
    }
    let depthCenter = textureLoad(depthTex, pixelI, 0).x;

    // Check depth first because sky pixels intentionally leave the other
    // raycast outputs untouched.
    if (depthCenter < 0.0) {
        return textureLoad(backgroundTex, pixelI, 0);
    }

    let packedShadow = textureLoad(shadowTex, pixelI, 0).x;
    if (abs(packedShadow) <= 1.0) {
        return textureLoad(backgroundTex, pixelI, 0);
    }

    let ndcCenter = ndcFromPixel(pixelI, dimsF);
    let posCView = viewPosFromDepth(
        camera.invProjParams.xy, ndcCenter, depthCenter);
    let posCWorld = viewToWorld(camera.invView, posCView);
    let waterData = textureLoad(materialTex, pixelI, 0);
    let viewDirWS = normalize(camera.cameraPos.xyz - posCWorld);
    let waterDepth = max(abs(packedShadow) - 1.0, 0.0);
    let waterShadow = select(0.0, 1.0, packedShadow >= 0.0);
    let waterWave = vec4<f32>(
        0.0, waterData.x, waterData.y, waterData.z);
    let waterNormal = waterWaveNormal(waterWave);
    let waterColor = shadeOcean(
        posCWorld, posCView, viewDirWS, waterNormal, waterDepth,
        waterShadow, dims);
    var outputColor = presentColor(waterColor, i.uv, dims);
    if (debug.mode != 0u) {
        outputColor = applyDebugVisualization(
            waterColor, depthCenter, waterNormal);
    }
    return vec4<f32>(outputColor, 1.0);
}
