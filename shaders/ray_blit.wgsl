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
    lightingColor : vec4<f32>,
    ambientExposure : vec4<f32>,
    fogColor : vec4<f32>,
    waterOptics : vec4<f32>,
    waterFoam : vec4<f32>,
    waterSpectrum : vec4<f32>,
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
// Water-only auxiliary output: accepted normal XZ and crest/shore data.
@group(0) @binding(3) var materialTex : texture_2d<f32>;
@group(0) @binding(4) var terrainTex : texture_2d<f32>;
@group(0) @binding(5) var lightmapTex : texture_2d<f32>;
@group(0) @binding(6) var terrainSampler : sampler;
@group(0) @binding(7) var<uniform> debug : DebugUniforms;
// Full-sphere equirectangular static environment, baked once
// by sky_lut.wgsl. See that file for the mapping.
@group(0) @binding(8) var skyLUT : texture_2d<f32>;
// Asset-free surface-foam network generated deterministically by BlitPath.
// It is a scalar linear mask with a complete mip chain and repeat addressing.
@group(0) @binding(9) var oceanFoamTex : texture_2d<f32>;
@group(0) @binding(10) var oceanFoamSampler : sampler;
// Exact terrain/sky color for a settled camera. Only fsCached references it;
// the direct and background-refresh pipelines retain their original layout.
@group(0) @binding(11) var backgroundTex : texture_2d<f32>;
// Opaque-scene ray distance paired with the linear background color.
@group(0) @binding(12) var backgroundDepthTex : texture_2d<f32>;
// Geometry inputs used only by fsFused. The final water material and linear
// depth are emitted together, avoiding a full-resolution intermediate G-buffer.
@group(0) @binding(13) var fusedHeightTex : texture_2d<u32>;
@group(0) @binding(14) var fusedShadowHeightTex : texture_2d<u32>;
@group(0) @binding(15) var fusedWaterDisplacementTex : texture_2d_array<f32>;
@group(0) @binding(16) var fusedWaterDisplacementSampler : sampler;

const MATERIAL_SKY : u32 = 0u;
const MATERIAL_TERRAIN : u32 = 1u;
const MATERIAL_WATER : u32 = 2u;

// Ocean colors are linear sRGB. Absorption is a Beer-Lambert coefficient,
// not a display tint.
const OCEAN_BASE_ABSORPTION : vec3<f32> =
    vec3<f32>(0.015208514, 0.009134059, 0.008568126);
const OCEAN_SKY_BRIGHTNESS : f32 = 0.9;
const OCEAN_REFLECTION_ROUGHNESS_STRENGTH : f32 = 0.50;
const OCEAN_FILM_GRAIN : f32 = 0.06;
const OCEAN_VIGNETTE : f32 = 0.25;
const OCEAN_VIGNETTE_SMOOTHNESS : f32 = 0.85;
const OCEAN_UNDERWATER_DISTORTION : f32 = 0.015;
const OCEAN_UNDERWATER_SCALE : f32 = 4.0;
const OCEAN_UNDERWATER_SPEED : f32 = 1.2;
const OCEAN_SUN_SHAFT_INTENSITY : f32 = 0.20;
const OCEAN_PROCEDURAL_SEABED_DEPTH : f32 = 100.0;
const SKY_LUT_WIDTH : f32 = 1774.0;
const SKY_LUT_HEIGHT : f32 = 887.0;

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
fn ambientTint() -> vec3<f32> {
    let maximum = max(max(camera.ambientExposure.r,
                          camera.ambientExposure.g),
                      max(camera.ambientExposure.b, 0.001));
    return camera.ambientExposure.rgb / maximum;
}
fn sunRadiance() -> vec3<f32> {
    return camera.lightingColor.rgb * camera.lightingColor.w;
}

// ─────────────────────────────────────────────────────────────────────────────
// Vertex Shader (Fullscreen Triangle)
// ─────────────────────────────────────────────────────────────────────────────
// Uses a single oversized triangle to cover the screen. This technique avoids
// the diagonal seam that would be visible with a quad made of two triangles.

struct VSOut {
    @builtin(position) pos : vec4<f32>,
    @location(0) uv : vec2<f32>,
};

struct CachedOpaqueOutput {
    @location(0) color : vec4<f32>,
    @location(1) linearDepth : f32,
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

fn environmentUv(directionIn : vec3<f32>) -> vec2<f32> {
    let direction = normalize(directionIn);
    let longitude = atan2(direction.z, direction.x);
    let latitude = atan2(clamp(direction.y, -1.0, 1.0),
                         length(direction.xz));
    let raw = vec2<f32>(longitude / 6.283185307179586 + 0.5,
                        0.5 - latitude / 3.141592653589793);
    // The bake evaluates the periodic seam at texel centers. Staying inside
    // those centers gives continuous clamped filtering without requiring a
    // sampler whose repeat mode would also affect terrain textures.
    let halfTexel = vec2<f32>(0.5 / SKY_LUT_WIDTH,
                              0.5 / SKY_LUT_HEIGHT);
    return clamp(raw, halfTexel, vec2<f32>(1.0) - halfTexel);
}

/// One filtered sample of the baked environment.
fn sampleSkyLUT(dir : vec3<f32>) -> vec3<f32> {
    let uv = environmentUv(dir);
    return textureSampleLevel(skyLUT, terrainSampler, uv, 0.0).rgb *
           OCEAN_SKY_BRIGHTNESS;
}

fn visibleSkyRadiance(dir : vec3<f32>) -> vec3<f32> {
    // The visible enclosure receives the preset's hemispherical illumination;
    // water reflections sample the raw environment, as the material does.
    let ambientSky = vec3<f32>(0.837, 0.998, 1.231);
    let ambientGround = vec3<f32>(0.529, 0.969, 1.154);
    let hemisphere = mix(ambientGround, ambientSky,
                         clamp(dir.y * 0.5 + 0.5, 0.0, 1.0));
    return sampleSkyLUT(dir) * hemisphere;
}


// ─────────────────────────────────────────────────────────────────────────────
// Ocean material and underwater composite
// ─────────────────────────────────────────────────────────────────────────────

fn waterWaveNormal(wave : vec4<f32>) -> vec3<f32> {
    let horizontalSquared = dot(wave.yz, wave.yz);
    let vertical = sqrt(max(1.0 - horizontalSquared, 0.0));
    return normalize(vec3<f32>(wave.y, vertical, wave.z));
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
        applyPostEffects(
            acesFilmic(color * camera.ambientExposure.w), uv, dims));
}

fn sampleWaterEnvironment(directionIn : vec3<f32>, roughness : f32) -> vec3<f32> {
    let direction = normalize(directionIn);
    let uv = environmentUv(direction);
    // Same angular footprint as the reference material, adjusted to this
    // generated 1024px equirectangular environment.
    let lod = log2(max(roughness * 0.035 * SKY_LUT_WIDTH, 1.0));
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
    let transmittance = exp(-oceanAbsorption() * pathLength);
    var submerged = mix(oceanScatterColor(), color, transmittance);
    let sunAlignment = max(dot(normalize(toFragment),
                               normalize(camera.lightDirWS.xyz)), 0.0);
    var shaft = pow(sunAlignment, 12.0) * OCEAN_SUN_SHAFT_INTENSITY;
    shaft *= 1.0 - exp(-pathLength * 0.015);
    submerged += vec3<f32>(1.0, 0.95, 0.85) * shaft;
    return submerged;
}

// Infinite-ocean fallback for rays beyond the finite heightfield. A broad
// repeatable field and directional sand ripples replace external floor maps
// while keeping the transmitted water from collapsing to reflected sky.
fn proceduralSeabed(worldXZ : vec2<f32>, pathLength : f32) -> vec3<f32> {
    let rotated = vec2<f32>(worldXZ.x * 0.82 + worldXZ.y * 0.57,
                            worldXZ.y * 0.82 - worldXZ.x * 0.57);
    let materialLod = clamp(log2(max(pathLength / 180.0, 1.0)), 0.0, 9.0);
    let material = textureSampleLevel(
        oceanFoamTex, oceanFoamSampler,
        rotated / 200.0, materialLod);
    let albedo = material.gba;
    let motion = camera.waterMotion.x;
    let causticUv = rotated / 46.0 +
        vec2<f32>(motion * 0.017, -motion * 0.011);
    let causticPattern = textureSampleLevel(
        oceanFoamTex, oceanFoamSampler, causticUv,
        max(materialLod - 1.5, 0.0)).r;
    let distanceFade = 1.0 - smoothstep(95.0, 360.0, pathLength);
    let caustic = smoothstep(0.60, 0.82, causticPattern) * distanceFade;
    return albedo * (0.88 + caustic * 0.62) +
           vec3<f32>(0.08, 0.15, 0.12) * caustic;
}

// Refraction reconstructs the distorted bed lookup from terrain albedo,
// light visibility, and the packed vertical water depth.
fn shadeOcean(posWorld : vec3<f32>, posView : vec3<f32>,
                 view : vec3<f32>, normal : vec3<f32>, waterDepth : f32,
                 shadowVisibility : f32, crestCompression : f32,
                 sceneRefraction : vec3<f32>, sceneThickness : f32,
                 hasSceneRefraction : bool, sceneHasOpaque : bool,
                 dims : vec2<u32>) -> vec3<f32> {
    let distanceToCamera = max(length(posView), 1.0e-6);
    let light = normalize(camera.lightDirWS.xyz);
    let viewFacing = dot(normal, view);
    let fresnel = dielectricFresnel(viewFacing, oceanIor());

    let reflected = reflect(-view, normal);
    let reflectionRoughness = oceanMinimumRoughness() +
        clamp(distanceToCamera / oceanReflectionDistance(),
              0.0, 1.0) * OCEAN_REFLECTION_ROUGHNESS_STRENGTH;
    let environment = sampleWaterEnvironment(reflected, reflectionRoughness) *
                      camera.waterColorA.w;

    let scatterLobe =
        pow(clamp((dot(view, -light) + 0.5) / 1.5, 0.0, 1.0) *
            clamp(dot(normal, -light) + 0.3, 0.0, 1.0), 0.85) *
        oceanSunIntensity() * 0.35 *
        (1.0 - smoothstep(100.0, 6400.0, distanceToCamera));
    let body = mix(oceanScatterColor(), oceanSurfaceColor(),
                   clamp(scatterLobe, 0.0, 1.0));
    let forwardScatter = pow(max(dot(view, -light), 0.0), 3.0) *
                         max(1.0 - normal.y, 0.0);

    let halfway = normalize(light + view);
    let sunSpecular = pow(max(dot(normal, halfway), 0.0), 420.0) *
                      oceanSunIntensity() * 4.0;

    let incomingRay = -view;
    let refractedRay = refract(incomingRay, normal, 1.0 / oceanIor());
    let refractedTravel = waterDepth / max(-refractedRay.y, 0.12);
    var thickness = clamp(refractedTravel, 0.0, 500.0);
    var bedWorld = posWorld + refractedRay * refractedTravel;

    // Project a normal-offset point, then intersect that camera ray with the
    // packed bed plane. This retains magnification without an opaque
    // scene-color target.
    let distortionDistance = min(thickness, 80.0) *
                             oceanDistortion();
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
    let bedLight = textureSampleLevel(lightmapTex, terrainSampler,
                                      bedUv, 0.0).x;
    var refracted = proceduralSeabed(bedWorld.xz, refractedTravel) *
                    (0.25 + 0.75 * bedLight * shadowVisibility);
    let proceduralFloor = waterDepth >= 499.0 &&
                          camera.waterMotion.z < 0.5;
    if (proceduralFloor) {
        let floorTravel = OCEAN_PROCEDURAL_SEABED_DEPTH /
                          max(-refractedRay.y, 0.12);
        bedWorld = posWorld + refractedRay * floorTravel;
        refracted = proceduralSeabed(bedWorld.xz, floorTravel) *
                    (0.34 + 0.66 * max(light.y, 0.0));
        thickness = clamp(floorTravel, 0.0, 500.0);
    }
    let transmittance = exp(-oceanAbsorption() * thickness);
    let refractedWater = refracted * transmittance +
                         body * (vec3<f32>(1.0) - transmittance);
    let reflectedWater = environment +
        camera.lightingColor.rgb * sunSpecular +
        oceanScatterColor() * camera.lightingColor.rgb *
        forwardScatter * oceanSunIntensity();

    let foamUv = posWorld.xz / oceanFoamSize();
    let worldPerPixel = distanceToCamera * 2.0 *
        max(camera.invProjParams.x / f32(max(dims.x, 1u)),
            camera.invProjParams.y / f32(max(dims.y, 1u)));
    let foamLod = log2(max(worldPerPixel * 1024.0 / oceanFoamSize(),
                           1.0));
    let foamPattern = textureSampleLevel(oceanFoamTex,
        oceanFoamSampler, foamUv, foamLod).r;
    let threshold = 1.0 - oceanFoamCoverage();
    let coverage = foamPattern *
        smoothstep(threshold, threshold + 0.15, foamPattern);
    let foamStrength = clamp(coverage * oceanFoamOpacity(), 0.0, 1.0);

    if (camera.waterMotion.z > 0.5) {
        let undersideNormal = -normal;
        var transmissionDirection =
            refract(-view, undersideNormal, oceanIor());
        let totalInternalReflection =
            dot(transmissionDirection, transmissionDirection) < 0.001;
        if (totalInternalReflection) {
            transmissionDirection = vec3<f32>(0.0, 1.0, 0.0);
        } else {
            transmissionDirection = normalize(transmissionDirection);
        }
        let transmittedEnvironment = sampleWaterEnvironment(
            transmissionDirection, reflectionRoughness);
        var transmitted = transmittedEnvironment;
        if (sceneHasOpaque) {
            transmitted = mix(transmittedEnvironment, refracted, 0.75);
        }
        var underside = mix(transmitted, environment, fresnel);
        underside += camera.lightingColor.rgb * sunSpecular;
        underside = mix(underside, vec3<f32>(0.94, 0.98, 1.0),
                        foamStrength * 0.35);
        let cameraTransmittance =
            exp(-oceanAbsorption() * distanceToCamera);
        return mix(oceanScatterColor(), underside,
                   cameraTransmittance);
    }

    // Foam suppresses Fresnel before its own color is composited.
    var color = mix(refractedWater, reflectedWater,
                    fresnel * clamp(1.0 - foamStrength * 2.0, 0.0, 1.0));
    color = mix(color, vec3<f32>(0.94, 0.98, 1.0), foamStrength);
    let fog = atmosphericFog(distanceToCamera);
    return mix(color, oceanFogColor(), fog);
}

fn backgroundSky(pixel : vec2<i32>, dims : vec2<u32>) -> vec3<f32> {
    let dimsF = vec2<f32>(f32(dims.x), f32(dims.y));
    let ndc = ndcFromPixel(pixel, dimsF);
    let viewRay = rayDirFromPixel(camera.invProjParams.xy, ndc);
    let worldRay = normalize(
        (camera.invView * vec4<f32>(viewRay, 0.0)).xyz);
    var color = visibleSkyRadiance(worldRay);
    if (camera.waterParams.y > 0.5 && camera.waterMotion.z > 0.5) {
        // Downward rays outside the finite terrain meet an analytic sandy
        // floor. Upward rays retain the full-sphere transmitted environment.
        if (worldRay.y < -0.015) {
            let floorHeight = camera.waterParams.x -
                              OCEAN_PROCEDURAL_SEABED_DEPTH;
            let floorTravel = (floorHeight - camera.cameraPos.y) /
                              worldRay.y;
            if (floorTravel > 0.0 && floorTravel < 500.0) {
                let floorPosition = camera.cameraPos.xyz +
                                    worldRay * floorTravel;
                let directLight = 0.34 + 0.66 *
                                  max(camera.lightDirWS.y, 0.0);
                let floorColor = proceduralSeabed(
                    floorPosition.xz, floorTravel) * directLight;
                let submergedFloor = applyUnderwaterMedium(
                    floorColor, worldRay, floorTravel);
                let distantWater = applyUnderwaterMedium(
                    color, worldRay, 500.0);
                return mix(submergedFloor, distantWater,
                           smoothstep(330.0, 495.0, floorTravel));
            }
        }
        color = applyUnderwaterMedium(color, worldRay, 500.0);
    }
    return color;
}

fn backgroundTerrain(pixel : vec2<i32>, dims : vec2<u32>,
                     depthCenter : f32) -> vec3<f32> {
    let dimsF = vec2<f32>(f32(dims.x), f32(dims.y));
    let maxCoord = vec2<i32>(i32(dims.x) - 1, i32(dims.y) - 1);
    let ndcCenter = ndcFromPixel(pixel, dimsF);
    let posCenterView = viewPosFromDepth(
        camera.invProjParams.xy, ndcCenter, depthCenter);
    let posCenterWorld = viewToWorld(camera.invView, posCenterView);

    let negativeX = sampleDepth(pixel - vec2<i32>(1, 0), maxCoord);
    let positiveX = sampleDepth(pixel + vec2<i32>(1, 0), maxCoord);
    let negativeY = sampleDepth(pixel - vec2<i32>(0, 1), maxCoord);
    let positiveY = sampleDepth(pixel + vec2<i32>(0, 1), maxCoord);
    let useNegativeX = pixel.x > 0 &&
        (pixel.x >= i32(dims.x) - 1 ||
         abs(negativeX - depthCenter) < abs(positiveX - depthCenter));
    let useNegativeY = pixel.y > 0 &&
        (pixel.y >= i32(dims.y) - 1 ||
         abs(negativeY - depthCenter) < abs(positiveY - depthCenter));
    let depthX = select(positiveX, negativeX, useNegativeX);
    let depthY = select(positiveY, negativeY, useNegativeY);
    let pixelX = select(pixel + vec2<i32>(1, 0),
                        pixel - vec2<i32>(1, 0), useNegativeX);
    let pixelY = select(pixel + vec2<i32>(0, 1),
                        pixel - vec2<i32>(0, 1), useNegativeY);
    let posX = viewPosFromDepth(camera.invProjParams.xy,
                                ndcFromPixel(pixelX, dimsF), depthX);
    let posY = viewPosFromDepth(camera.invProjParams.xy,
                                ndcFromPixel(pixelY, dimsF), depthY);
    var tangentX = posX - posCenterView;
    var tangentY = posY - posCenterView;
    if (useNegativeX) { tangentX = -tangentX; }
    if (useNegativeY) { tangentY = -tangentY; }
    var normal = cross(tangentX, tangentY);
    if (dot(normal, normal) > 1.0e-12) {
        normal = normalize(normal);
    } else {
        normal = vec3<f32>(0.0, 1.0, 0.0);
    }

    let terrainUv = terrainUV(posCenterWorld);
    let underwaterTerrain = camera.waterParams.y > 0.5 &&
        camera.waterMotion.z > 0.5 &&
        posCenterWorld.y <= camera.waterParams.x + 0.5;
    var albedo = srgbToLinear(textureSampleLevel(terrainTex,
        terrainSampler, terrainUv, 0.0).rgb);
    if (underwaterTerrain) {
        albedo = proceduralSeabed(
            posCenterWorld.xz, length(posCenterView));
    }
    let lightVisibility = textureSampleLevel(
        lightmapTex, terrainSampler, terrainUv, 0.0).x;
    let shadow = textureLoad(shadowTex, pixel, 0).x;
    let light = camera.lightDirVS.xyz;
    let diffuse = max(dot(normal, light), 0.0) * shadow;
    let ambient = max(camera.lightDirVS.w, 0.05);
    let view = normalize(-posCenterView);
    let halfway = normalize(light + view);
    let roughness = 0.6;
    let specular = mix(0.04, 0.25, 1.0 - roughness) *
        pow(max(dot(normal, halfway), 0.0),
            max((1.0 - roughness) * 160.0, 8.0)) *
        lightVisibility * shadow;
    let warmLight = vec3<f32>(1.10, 0.96, 0.84);
    let coolShadow = vec3<f32>(0.90, 0.96, 1.02);
    let grade = mix(coolShadow, warmLight,
        clamp(diffuse * lightVisibility + 0.35, 0.0, 1.0));
    let lit = albedo *
        (diffuse * lightVisibility * sunRadiance() + ambient * ambientTint()) *
        grade + specular * sunRadiance();
    let distanceToCamera = length(posCenterView);
    if (underwaterTerrain) {
        return applyUnderwaterMedium(
            lit, posCenterWorld - camera.cameraPos.xyz, distanceToCamera);
    }
    let fog = atmosphericFog(distanceToCamera);
    return mix(lit, oceanFogColor(), fog);
}

// Linear-HDR opaque scene used by the water pass for exact
// screen-space refraction. Presentation is intentionally deferred.
@fragment
fn fsBackground(i : VSOut) -> @location(0) vec4<f32> {
    let dims = textureDimensions(depthTex, 0);
    let dimsF = vec2<f32>(f32(dims.x), f32(dims.y));
    let maxCoord = vec2<i32>(i32(dims.x) - 1, i32(dims.y) - 1);
    let pixel = clamp(vec2<i32>(floor(i.uv * dimsF)),
                      vec2<i32>(0), maxCoord);
    let depth = textureLoad(depthTex, pixel, 0).x;
    if (depth < 0.0) {
        return vec4<f32>(backgroundSky(pixel, dims), 1.0);
    }
    return vec4<f32>(backgroundTerrain(pixel, dims, depth), 1.0);
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
                           camera.waterMotion.z > 0.5;
    let pixelI = outputPixelI;
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
        var skyColor = visibleSkyRadiance(worldDir);

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
                                       waterData.z, vec3<f32>(0.0), 0.0,
                                       false, false, dims);
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
    let litColor = albedo *
        (finalDiffuse * lightVisibility * sunRadiance() +
         ambient * ambientTint()) * grade + specular * sunRadiance();
    
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
        let fog = atmosphericFog(dist);
        finalColor = mix(litColor, oceanFogColor(), fog);
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

// Terrain-cache specialization. Terrain and sky are copied from the exact
// full-resolution background refresh; animated ocean shading remains live,
// and submerged views apply screen-space distortion.
@fragment
fn fsCached(i : VSOut) -> @location(0) vec4<f32> {
    let dims = textureDimensions(depthTex, 0);
    let dimsF = vec2<f32>(f32(dims.x), f32(dims.y));
    let maxCoord = vec2<i32>(i32(dims.x) - 1, i32(dims.y) - 1);
    let outputPixelI = clamp(
        vec2<i32>(floor(i.uv * dimsF)), vec2<i32>(0, 0), maxCoord);
    let cameraUnderwater = camera.waterParams.y > 0.5 &&
                           camera.waterMotion.z > 0.5;
    let pixelI = outputPixelI;
    let depthCenter = textureLoad(depthTex, pixelI, 0).x;

    // Check depth first because sky pixels intentionally leave the other
    // raycast outputs untouched.
    if (depthCenter < 0.0) {
        let hdr = textureLoad(backgroundTex, pixelI, 0).rgb;
        return vec4<f32>(presentColor(hdr, i.uv, dims), 1.0);
    }

    let packedShadow = textureLoad(shadowTex, pixelI, 0).x;
    if (abs(packedShadow) <= 1.0) {
        let hdr = textureLoad(backgroundTex, pixelI, 0).rgb;
        return vec4<f32>(presentColor(hdr, i.uv, dims), 1.0);
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

    let screenUv = (vec2<f32>(pixelI) + vec2<f32>(0.5)) / dimsF;
    var distortedUv = screenUv;
    let opaqueDepth = textureLoad(backgroundDepthTex, pixelI, 0).x;
    let sceneThickness = select(
        500.0, max(opaqueDepth - depthCenter, 0.0), opaqueDepth > 0.0);
    let distortionDistance = min(sceneThickness, 80.0) *
                             oceanDistortion();
    let distortedClip = camera.viewProj *
        vec4<f32>(posCWorld + waterNormal * distortionDistance, 1.0);
    if (distortedClip.w > 1.0e-5) {
        let distortedNdc = distortedClip.xy / distortedClip.w;
        let candidateUv = clamp(
            vec2<f32>(distortedNdc.x * 0.5 + 0.5,
                      0.5 - distortedNdc.y * 0.5),
            vec2<f32>(0.001), vec2<f32>(0.999));
        let candidatePixel = clamp(
            vec2<i32>(floor(candidateUv * dimsF)),
            vec2<i32>(0), maxCoord);
        let candidateDepth = textureLoad(
            backgroundDepthTex, candidatePixel, 0).x;
        // Do not smear an opaque silhouette which lies in front of the water.
        if (candidateDepth < 0.0 || candidateDepth + 1.0e-4 >= depthCenter) {
            distortedUv = candidateUv;
        }
    }
    var refractionUv = distortedUv;
    if (cameraUnderwater) {
        refractionUv = clamp(
            distortedUv + underwaterDistortionUv(screenUv) - screenUv,
            vec2<f32>(0.001), vec2<f32>(0.999));
    }
    let refractionPixel = clamp(
        vec2<i32>(floor(refractionUv * dimsF)),
        vec2<i32>(0), maxCoord);
    let refractionDepth = textureLoad(
        backgroundDepthTex, refractionPixel, 0).x;
    // The opaque scene is a linear-HDR color target. Bilinear sampling here
    // matches normal image sampling and removes the blocky refraction steps
    // that nearest texel loads produced at silhouettes.
    let sceneRefraction = textureSampleLevel(
        backgroundTex, terrainSampler, refractionUv, 0.0).rgb;
    let waterColor = shadeOcean(
        posCWorld, posCView, viewDirWS, waterNormal, waterDepth,
        waterShadow, waterData.z, sceneRefraction, sceneThickness,
        true, refractionDepth > 0.0, dims);
    var outputColor = presentColor(waterColor, i.uv, dims);
    if (debug.mode != 0u) {
        outputColor = applyDebugVisualization(
            waterColor, depthCenter, waterNormal);
    }
    return vec4<f32>(outputColor, 1.0);
}

// ─────────────────────────────────────────────────────────────────────────────
// Present the exact cached opaque scene while preserving the ray-caster's
// linear scene depth for later depth-aware passes.
@fragment
fn fsCachedOpaque(i : VSOut) -> CachedOpaqueOutput {
    let dims = textureDimensions(backgroundDepthTex, 0);
    let dimsF = vec2<f32>(dims);
    let maxCoord = vec2<i32>(i32(dims.x) - 1, i32(dims.y) - 1);
    let pixel = clamp(vec2<i32>(floor(i.uv * dimsF)),
                      vec2<i32>(0), maxCoord);
    let hdr = textureLoad(backgroundTex, pixel, 0).rgb;
    let linearDepth = textureLoad(backgroundDepthTex, pixel, 0).x;
    var output : CachedOpaqueOutput;
    output.color = vec4<f32>(presentColor(hdr, i.uv, dims), 1.0);
    output.linearDepth = linearDepth;
    return output;
}

@fragment
fn fsCachedOpaqueColor(i : VSOut) -> @location(0) vec4<f32> {
    let dims = textureDimensions(backgroundDepthTex, 0);
    let dimsF = vec2<f32>(dims);
    let maxCoord = vec2<i32>(i32(dims.x) - 1, i32(dims.y) - 1);
    let pixel = clamp(vec2<i32>(floor(i.uv * dimsF)),
                      vec2<i32>(0), maxCoord);
    let hdr = textureLoad(backgroundTex, pixel, 0).rgb;
    return vec4<f32>(presentColor(hdr, i.uv, dims), 1.0);
}

// Fused animated-water intersection and material
// ─────────────────────────────────────────────────────────────────────────────

const FUSED_MIN_WATER_DEPTH : f32 = 0.05;
const FUSED_SHORE_DEPTH : f32 = 7.5;
const FUSED_SHORE_SURFACE_OVERLAP : f32 = 2.0;
const FUSED_WATER_TAU : f32 = 6.283185307179586;
const FUSED_WATER_RESOLUTION : f32 = 256.0;
const FUSED_WATER_NORMAL_LAYER : i32 = 2;

struct FusedWaterSurfaceSample {
    height : f32,
    geometrySlope : vec2<f32>,
    shadingWave : vec4<f32>,
};

struct FusedWaterRayHit {
    distance : f32,
    surface : FusedWaterSurfaceSample,
};

struct FusedSpectralSurface {
    displacement : vec3<f32>,
    normalVector : vec3<f32>,
    compression : f32,
};

struct FusedLongWaveSurface {
    displacement : vec3<f32>,
    normalVector : vec3<f32>,
    fold : f32,
};

struct FusedSceneSample {
    depth : f32,
    packedShadow : f32,
    wave : vec4<f32>,
    material : u32,
};

struct FusedFragmentOutput {
    @location(0) color : vec4<f32>,
    @location(1) linearDepth : f32,
};

fn fusedHeightmapCoordinate(height : f32) -> f32 {
    let normalized = (height / camera.metrics.x) * 0.5 + 0.5;
    return normalized * 65535.0;
}

fn fusedHeightmapScale(height : f32) -> f32 {
    return (height / camera.metrics.x) * 0.5 * 65535.0;
}

fn fusedWorldHeight(height : f32) -> f32 {
    return ((height / 65535.0) * 2.0 - 1.0) * camera.metrics.x;
}

fn fusedMaxTraversalMip() -> u32 {
    let terrainWidth = max(u32(camera.terrainSize.x), 1u);
    let terrainHeight = max(u32(camera.terrainSize.y), 1u);
    var level = min(7u, textureNumLevels(fusedHeightTex) - 1u);
    loop {
        if (level == 0u ||
            ((terrainWidth >> level) > 0u && (terrainHeight >> level) > 0u)) {
            break;
        }
        level--;
    }
    return level;
}

fn fusedCascadeUv(position : vec2<f32>, scale : f32) -> vec2<f32> {
    return position / scale +
           vec2<f32>(0.5 + 0.5 / FUSED_WATER_RESOLUTION);
}

fn fusedSpectralSurface(worldXZ : vec2<f32>, strength : f32,
                        distance : f32) -> FusedSpectralSurface {
    let detailWeight = 1.0 - smoothstep(900.0, 3500.0, max(distance, 0.0));
    let broadFirst = textureSampleLevel(
        fusedWaterDisplacementTex, fusedWaterDisplacementSampler,
        fusedCascadeUv(worldXZ, camera.waterSpectrum.x), 0, 0.0);
    var detailFirst = vec4<f32>(0.0);
    if (detailWeight > 0.0) {
        detailFirst = textureSampleLevel(
            fusedWaterDisplacementTex, fusedWaterDisplacementSampler,
            fusedCascadeUv(worldXZ, camera.waterSpectrum.y), 1, 0.0) *
            detailWeight;
    }
    let baseXZ = worldXZ - (broadFirst.xz + detailFirst.xz) * strength;
    let broad = textureSampleLevel(
        fusedWaterDisplacementTex, fusedWaterDisplacementSampler,
        fusedCascadeUv(baseXZ, camera.waterSpectrum.x), 0, 0.0);
    let broadNormal = textureSampleLevel(
        fusedWaterDisplacementTex, fusedWaterDisplacementSampler,
        fusedCascadeUv(baseXZ, camera.waterSpectrum.x),
        FUSED_WATER_NORMAL_LAYER, 0.0).xyz;
    var detail = vec4<f32>(0.0);
    var detailNormal = vec3<f32>(0.0, 1.0, 0.0);
    if (detailWeight > 0.0) {
        detail = textureSampleLevel(
            fusedWaterDisplacementTex, fusedWaterDisplacementSampler,
            fusedCascadeUv(baseXZ, camera.waterSpectrum.y), 1, 0.0) *
            detailWeight;
        detailNormal = textureSampleLevel(
            fusedWaterDisplacementTex, fusedWaterDisplacementSampler,
            fusedCascadeUv(baseXZ, camera.waterSpectrum.y),
            FUSED_WATER_NORMAL_LAYER + 1, 0.0).xyz;
    }
    let up = vec3<f32>(0.0, 1.0, 0.0);
    return FusedSpectralSurface(
        (broad.xyz + detail.xyz) * strength,
        up + ((broadNormal - up) +
              (detailNormal - up) * detailWeight) * strength,
        max(broad.w, detail.w) * strength);
}

fn fusedOneLongWave(position : vec2<f32>, wave : vec4<f32>,
                    dynamics : vec3<f32>, strength : f32)
                    -> FusedLongWaveSurface {
    let waveNumber = FUSED_WATER_TAU / (wave.w + 1.0e-4);
    let phase = waveNumber * dot(wave.xy, position) -
                dynamics.z * camera.waterMotion.x + dynamics.y;
    let sine = sin(phase);
    let cosine = cos(phase);
    let amplitude = wave.z * strength;
    let ka = waveNumber * amplitude;
    return FusedLongWaveSurface(
        vec3<f32>(-dynamics.x * amplitude * wave.x * sine,
                  amplitude * cosine,
                  -dynamics.x * amplitude * wave.y * sine),
        vec3<f32>(wave.x * ka * sine,
                  -dynamics.x * ka * cosine,
                  wave.y * ka * sine),
        dynamics.x * ka * sine);
}

fn fusedLongWaveSurface(position : vec2<f32>, strength : f32)
                        -> FusedLongWaveSurface {
    let a = fusedOneLongWave(
        position, vec4<f32>(0.923059017, 0.384658357, 5.1541, 440.298507),
        vec3<f32>(1.0, 0.000000000, 0.374291312), strength);
    let b = fusedOneLongWave(
        position, vec4<f32>(0.700400636, 0.713749921, 5.1541, 701.258144),
        vec3<f32>(1.0, 5.553108549, 0.296825282), strength);
    let c = fusedOneLongWave(
        position, vec4<f32>(0.367164395, 0.930156066, 5.1541, 1116.885424),
        vec3<f32>(1.0, 4.823031791, 0.234699061), strength);
    let d = fusedOneLongWave(
        position, vec4<f32>(-0.024039031, 0.999711021, 5.1541, 1778.85),
        vec3<f32>(1.0, 4.092955033, 0.186378666), strength);
    return FusedLongWaveSurface(
        a.displacement + b.displacement + c.displacement + d.displacement,
        vec3<f32>(0.0, 1.0, 0.0) +
            a.normalVector + b.normalVector + c.normalVector + d.normalVector,
        a.fold + b.fold + c.fold + d.fold);
}

fn fusedSampleWaterSurface(worldXZ : vec2<f32>, distance : f32)
                           -> FusedWaterSurfaceSample {
    if (distance >= 6500.0) {
        return FusedWaterSurfaceSample(
            0.0, vec2<f32>(0.0), vec4<f32>(0.0));
    }
    let strength = clamp(camera.waterParams.z, 0.0, 2.0);
    let spectral = fusedSpectralSurface(worldXZ, strength, distance);
    let longWaves = fusedLongWaveSurface(
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
    return FusedWaterSurfaceSample(
        displacement.y, geometrySlope,
        vec4<f32>(displacement.y, normal.x, normal.z, compression));
}

fn fusedFilterWaterHorizon(surfaceIn : FusedWaterSurfaceSample,
                           distance : f32) -> FusedWaterSurfaceSample {
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

fn fusedIntersectWater(origin : vec3<f32>, dir : vec3<f32>,
                       waterHeight : f32) -> FusedWaterRayHit {
    let maximumWaveHeight = 48.0 * clamp(camera.waterParams.z, 0.0, 2.0);
    let envelopeA =
        (waterHeight - maximumWaveHeight - origin.y) / dir.y;
    let envelopeB =
        (waterHeight + maximumWaveHeight - origin.y) / dir.y;
    let minimumDistance = max(min(envelopeA, envelopeB), 1.0e-3);
    let maximumDistance = max(max(envelopeA, envelopeB), minimumDistance);
    var distance = clamp(
        (waterHeight - origin.y) / dir.y,
        minimumDistance, maximumDistance);
    var position = origin + dir * distance;
    var surface = fusedFilterWaterHorizon(
        fusedSampleWaterSurface(position.xz, distance), distance);
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
        surface = fusedFilterWaterHorizon(
            fusedSampleWaterSurface(position.xz, distance), distance);
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
    return FusedWaterRayHit(distance, surface);
}

fn fusedNearbyShore(cell : vec2<i32>, baseSize : vec2<i32>,
                    waterHeight : f32) -> f32 {
    let shoreMip = min(3u, textureNumLevels(fusedHeightTex) - 1u);
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
            let h = fusedWorldHeight(
                f32(textureLoad(fusedHeightTex, c, i32(shoreMip)).x));
            maxNearbyHeight = max(maxNearbyHeight, h);
        }
    }
    return smoothstep(waterHeight - 36.0,
                      waterHeight - FUSED_MIN_WATER_DEPTH,
                      maxNearbyHeight);
}

fn fusedIntersectAabb(origin : vec3<f32>, dir : vec3<f32>,
                      bmin : vec3<f32>, bmax : vec3<f32>) -> vec2<f32> {
    let invDir = 1.0 / (dir + sign(dir) * 1e-20 + vec3<f32>(1e-20));
    let t0 = (bmin - origin) * invDir;
    let t1 = (bmax - origin) * invDir;
    let tMin = max(max(min(t0.x, t1.x), min(t0.y, t1.y)), min(t0.z, t1.z));
    let tMax = min(min(max(t0.x, t1.x), max(t0.y, t1.y)), max(t0.z, t1.z));
    return vec2<f32>(tMin, tMax);
}

fn fusedBakedShadow(worldPos : vec3<f32>, terrainOrigin : vec2<f32>,
                    cellScale : f32) -> f32 {
    let dims = vec2<i32>(textureDimensions(fusedShadowHeightTex));
    let cellF = (worldPos.xz + terrainOrigin) / cellScale;
    let scale = vec2<f32>(dims) / camera.terrainSize;
    let cell = clamp(vec2<i32>(floor(cellF * scale)), vec2<i32>(0, 0),
                     dims - vec2<i32>(1, 1));
    let boundary = f32(textureLoad(fusedShadowHeightTex, cell, 0).x);
    let posY = fusedHeightmapCoordinate(worldPos.y);
    let soft = max(fusedHeightmapScale(1.5), 1.0);
    return smoothstep(-soft, soft, posY - boundary);
}

fn fusedWorldRay(pixel : vec2<i32>, dims : vec2<u32>) -> vec3<f32> {
    let ndc = ndcFromPixel(pixel, vec2<f32>(dims));
    let viewRay = rayDirFromPixel(camera.invProjParams.xy, ndc);
    // invView's upper 3x3 is an orthonormal camera rotation, so it preserves
    // the normalization already performed by rayDirFromPixel.
    return (camera.invView * vec4<f32>(viewRay, 0.0)).xyz;
}

fn fusedSceneAt(pixel : vec2<i32>, dims : vec2<u32>) -> FusedSceneSample {
    let origin = camera.cameraPos.xyz;
    let dir = fusedWorldRay(pixel, dims);
    let terrainSize = vec2<f32>(camera.terrainSize);
    let cellScale = camera.metrics.y;
    let terrainOrigin =
        0.5 * (terrainSize - vec2<f32>(1.0, 1.0)) * cellScale;
    let waterEnabled = camera.waterParams.y > 0.5;
    let waterHeight = camera.waterParams.x;
    var waterHit = FusedWaterRayHit(
        -1.0,
        FusedWaterSurfaceSample(0.0, vec2<f32>(0.0), vec4<f32>(0.0)));
    if (waterEnabled && abs(dir.y) > 1.0e-5) {
        let flatWaterDistance = (waterHeight - origin.y) / dir.y;
        if (flatWaterDistance > 0.0 && flatWaterDistance < 50000.0) {
            waterHit = fusedIntersectWater(origin, dir, waterHeight);
        }
    }

    var depth = textureLoad(backgroundDepthTex, pixel, 0).x;
    var waterDepthUnder = 0.0;
    var waterHasTerrainBed = false;
    var waterWave = vec4<f32>(0.0);
    var material = select(MATERIAL_SKY, MATERIAL_TERRAIN, depth > 0.0);
    if (waterHit.distance > 0.0 && waterHit.distance < 50000.0) {
        let waterDistance = waterHit.distance;
        let surface = waterHit.surface;
        if (depth > -1.5 && (depth <= 0.0 || waterDistance < depth)) {
            let waterPos = origin + dir * waterDistance;
            let surfaceHeight = waterHeight + surface.height;
            let waterCoord = (waterPos.xz + terrainOrigin) / cellScale;
            let waterCell = vec2<i32>(floor(waterCoord));
            let baseSize = vec2<i32>(
                i32(camera.terrainSize.x), i32(camera.terrainSize.y));
            if (waterCell.x >= 1 && waterCell.y >= 1 &&
                waterCell.x < baseSize.x - 1 &&
                waterCell.y < baseSize.y - 1) {
                let terrainHeight = fusedWorldHeight(f32(textureLoad(
                    fusedHeightTex, waterCell, 0).x));
                if (terrainHeight <
                    max(surfaceHeight,
                        waterHeight + FUSED_SHORE_SURFACE_OVERLAP) -
                        FUSED_MIN_WATER_DEPTH) {
                    depth = waterDistance;
                    material = MATERIAL_WATER;
                    waterHasTerrainBed = true;
                    waterWave = surface.shadingWave;
                    let realDepth = max(
                        surfaceHeight - terrainHeight, FUSED_MIN_WATER_DEPTH);
                    let shoreInfluence = fusedNearbyShore(
                        waterCell, baseSize, surfaceHeight);
                    waterDepthUnder = mix(
                        realDepth, min(realDepth, FUSED_SHORE_DEPTH),
                        shoreInfluence);
                }
            } else {
                depth = waterDistance;
                material = MATERIAL_WATER;
                waterWave = surface.shadingWave;
                waterDepthUnder = 500.0;
            }
        }
    }

    var shadow = textureLoad(shadowTex, pixel, 0).x;
    if (depth > 0.0 && material == MATERIAL_WATER && waterHasTerrainBed) {
        shadow = fusedBakedShadow(
            origin + dir * depth, terrainOrigin, cellScale);
    } else if (depth > 0.0 && material == MATERIAL_WATER) {
        shadow = 1.0;
    }
    var packedShadow = shadow;
    if (material == MATERIAL_WATER) {
        packedShadow = select(-(waterDepthUnder + 1.0),
                              waterDepthUnder + 1.0, shadow > 0.5);
    }
    return FusedSceneSample(depth, packedShadow, waterWave, material);
}

@fragment
fn fsFused(i : VSOut) -> FusedFragmentOutput {
    let dims = textureDimensions(backgroundDepthTex, 0);
    let dimsF = vec2<f32>(dims);
    let maxCoord = vec2<i32>(i32(dims.x) - 1, i32(dims.y) - 1);
    let pixel = clamp(vec2<i32>(floor(i.uv * dimsF)),
                      vec2<i32>(0), maxCoord);
    let scene = fusedSceneAt(pixel, dims);
    var output : FusedFragmentOutput;
    output.linearDepth = scene.depth;

    if (scene.material != MATERIAL_WATER || scene.depth < 0.0) {
        let hdr = textureLoad(backgroundTex, pixel, 0).rgb;
        output.color = vec4<f32>(presentColor(hdr, i.uv, dims), 1.0);
        return output;
    }

    let ndcCenter = ndcFromPixel(pixel, dimsF);
    let posView = viewPosFromDepth(
        camera.invProjParams.xy, ndcCenter, scene.depth);
    let posWorld = viewToWorld(camera.invView, posView);
    let viewDirWS = normalize(camera.cameraPos.xyz - posWorld);
    let waterDepth = max(abs(scene.packedShadow) - 1.0, 0.0);
    let waterShadow = select(0.0, 1.0, scene.packedShadow >= 0.0);
    let waterNormal = waterWaveNormal(
        vec4<f32>(0.0, scene.wave.y, scene.wave.z, scene.wave.w));

    let screenUv = (vec2<f32>(pixel) + vec2<f32>(0.5)) / dimsF;
    var distortedUv = screenUv;
    let opaqueDepth = textureLoad(backgroundDepthTex, pixel, 0).x;
    let sceneThickness = select(
        500.0, max(opaqueDepth - scene.depth, 0.0), opaqueDepth > 0.0);
    let distortionDistance = min(sceneThickness, 80.0) *
                             oceanDistortion();
    let distortedClip = camera.viewProj *
        vec4<f32>(posWorld + waterNormal * distortionDistance, 1.0);
    if (distortedClip.w > 1.0e-5) {
        let distortedNdc = distortedClip.xy / distortedClip.w;
        let candidateUv = clamp(
            vec2<f32>(distortedNdc.x * 0.5 + 0.5,
                      0.5 - distortedNdc.y * 0.5),
            vec2<f32>(0.001), vec2<f32>(0.999));
        let candidatePixel = clamp(
            vec2<i32>(floor(candidateUv * dimsF)),
            vec2<i32>(0), maxCoord);
        let candidateDepth = textureLoad(
            backgroundDepthTex, candidatePixel, 0).x;
        if (candidateDepth < 0.0 ||
            candidateDepth + 1.0e-4 >= scene.depth) {
            distortedUv = candidateUv;
        }
    }
    let cameraUnderwater = camera.waterParams.y > 0.5 &&
                           camera.waterMotion.z > 0.5;
    var refractionUv = distortedUv;
    if (cameraUnderwater) {
        refractionUv = clamp(
            distortedUv + underwaterDistortionUv(screenUv) - screenUv,
            vec2<f32>(0.001), vec2<f32>(0.999));
    }
    let refractionPixel = clamp(
        vec2<i32>(floor(refractionUv * dimsF)),
        vec2<i32>(0), maxCoord);
    let refractionDepth = textureLoad(
        backgroundDepthTex, refractionPixel, 0).x;
    let sceneRefraction = textureSampleLevel(
        backgroundTex, terrainSampler, refractionUv, 0.0).rgb;
    let waterColor = shadeOcean(
        posWorld, posView, viewDirWS, waterNormal, waterDepth,
        waterShadow, scene.wave.w, sceneRefraction, sceneThickness,
        true, refractionDepth > 0.0, dims);
    var presented = presentColor(waterColor, i.uv, dims);
    if (debug.mode != 0u) {
        presented = applyDebugVisualization(
            waterColor, scene.depth, waterNormal);
    }
    output.color = vec4<f32>(presented, 1.0);
    return output;
}
