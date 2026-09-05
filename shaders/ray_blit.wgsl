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
// Bounded PBR terrain pack: sand, soil, grass, and exposed rock.
// Detail stores tangent-space NormalGL XYZ and perceptual roughness in A.
@group(0) @binding(17) var terrainMaterialAlbedo : texture_2d_array<f32>;
@group(0) @binding(18) var terrainMaterialNormalRoughness :
    texture_2d_array<f32>;

// Baked on this device with the original hash; no filtering or quantization.
@group(0) @binding(19) var periodicGradientLut : texture_2d<f32>;
// Test pipelines may specialize this to false for an unchanged reference.
override USE_PERIODIC_GRADIENT_LUT : bool = true;

const MATERIAL_SKY : u32 = 0u;
const MATERIAL_TERRAIN : u32 = 1u;
const MATERIAL_WATER : u32 = 2u;
// invProjParams.w selects the opt-in course; zero preserves the terrain demo.
fn authoredCourseEnabled() -> bool { return camera.invProjParams.w > 0.5; }


// Ocean colors are linear sRGB. Absorption is a Beer-Lambert coefficient,
// not a display tint.
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

fn worldNormalToView(worldNormal : vec3<f32>) -> vec3<f32> {
    // invView's first three columns are the view basis in world space.
    return normalize(vec3<f32>(
        dot(worldNormal, camera.invView[0].xyz),
        dot(worldNormal, camera.invView[1].xyz),
        dot(worldNormal, camera.invView[2].xyz)));
}

fn filterUpFacingTerrainNormal(worldNormal : vec3<f32>) -> vec3<f32> {
    // Terrain Diffusion preserves useful macro relief but also leaves a
    // one-cell directional corrugation. Treat that frequency like geometric
    // normal-map detail on soil/grass, while leaving real rock faces intact.
    let filterWeight =
        smoothstep(0.70, 0.94, worldNormal.y) * 0.90;
    return normalize(mix(
        worldNormal, vec3<f32>(0.0, 1.0, 0.0), filterWeight));
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
    // The FFT normal layers contain sub-pixel slopes that are valid for close
    // highlights but alias into repeated Fresnel pits after reconstruction.
    // Keep geometry displacement intact and band-limit only the shading slope.
    let horizontal = wave.yz * 0.72;
    let horizontalSquared = dot(horizontal, horizontal);
    let vertical = sqrt(max(1.0 - horizontalSquared, 0.0));
    return normalize(vec3<f32>(horizontal.x, vertical, horizontal.y));
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

// ─────────────────────────────────────────────────────────────────────────────
// Shoreline terrain material
// ─────────────────────────────────────────────────────────────────────────────

const TERRAIN_LAYER_SAND : i32 = 0;
const TERRAIN_LAYER_SOIL : i32 = 1;
const TERRAIN_LAYER_GRASS : i32 = 2;
const TERRAIN_LAYER_ROCK : i32 = 3;

struct TerrainLayerSample {
    albedo : vec3<f32>,
    tangentNormal : vec3<f32>,
    roughness : f32,
};

struct TerrainSurface {
    albedo : vec3<f32>,
    normal : vec3<f32>,
    roughness : f32,
    wetness : f32,
};

struct CovePropHit {
    distance : f32,
    normal : vec3<f32>,
    grainCoordinate : f32,
    materialCue : f32,
};

fn missedCoveProp() -> CovePropHit {
    return CovePropHit(
        -1.0, vec3<f32>(0.0, 1.0, 0.0), 0.0, 0.0);
}

fn intersectCovePropSphere(
    rayOrigin : vec3<f32>, rayDirection : vec3<f32>,
    center : vec3<f32>, radius : f32,
    maximumDistance : f32, grainCoordinate : f32) -> CovePropHit {
    let offset = rayOrigin - center;
    let projected = dot(rayDirection, offset);
    let constant = dot(offset, offset) - radius * radius;
    let discriminant = projected * projected - constant;
    if (discriminant < 0.0) {
        return missedCoveProp();
    }
    let distance = -projected - sqrt(discriminant);
    if (distance <= 0.001 || distance >= maximumDistance) {
        return missedCoveProp();
    }
    let normal = normalize(
        rayOrigin + rayDirection * distance - center);
    return CovePropHit(distance, normal, grainCoordinate, 0.0);
}

fn intersectCovePropCapsule(
    rayOrigin : vec3<f32>, rayDirection : vec3<f32>,
    endpointA : vec3<f32>, endpointB : vec3<f32>,
    radius : f32, maximumDistance : f32) -> CovePropHit {
    let axis = endpointB - endpointA;
    let offset = rayOrigin - endpointA;
    let axisLengthSquared = dot(axis, axis);
    let axisRay = dot(axis, rayDirection);
    let axisOffset = dot(axis, offset);
    let rayOffset = dot(rayDirection, offset);
    let offsetSquared = dot(offset, offset);
    let quadratic =
        axisLengthSquared - axisRay * axisRay;
    let linear =
        axisLengthSquared * rayOffset - axisOffset * axisRay;
    let constant =
        axisLengthSquared * offsetSquared -
        axisOffset * axisOffset -
        radius * radius * axisLengthSquared;
    var best = missedCoveProp();
    let discriminant =
        linear * linear - quadratic * constant;
    if (quadratic > 1.0e-6 && discriminant >= 0.0) {
        let distance =
            (-linear - sqrt(discriminant)) / quadratic;
        let axisPosition = axisOffset + distance * axisRay;
        if (distance > 0.001 && distance < maximumDistance &&
            axisPosition >= 0.0 &&
            axisPosition <= axisLengthSquared) {
            let radial =
                offset + rayDirection * distance -
                axis * (axisPosition / axisLengthSquared);
            best = CovePropHit(
                distance, normalize(radial),
                axisPosition / sqrt(axisLengthSquared), 0.0);
        }
    }

    let axisLength = sqrt(axisLengthSquared);
    let firstCap = intersectCovePropSphere(
        rayOrigin, rayDirection, endpointA, radius,
        select(maximumDistance, best.distance, best.distance > 0.0),
        0.0);
    if (firstCap.distance > 0.0) {
        best = firstCap;
    }
    let secondCap = intersectCovePropSphere(
        rayOrigin, rayDirection, endpointB, radius,
        select(maximumDistance, best.distance, best.distance > 0.0),
        axisLength);
    if (secondCap.distance > 0.0) {
        best = secondCap;
    }
    return best;
}

fn intersectCovePropCapsuleCue(
    rayOrigin : vec3<f32>, rayDirection : vec3<f32>,
    endpointA : vec3<f32>, endpointB : vec3<f32>,
    radius : f32, materialCue : f32,
    maximumDistance : f32) -> CovePropHit {
    var hit = intersectCovePropCapsule(
        rayOrigin, rayDirection,
        endpointA, endpointB, radius, maximumDistance);
    if (hit.distance > 0.0) {
        hit.materialCue = materialCue;
    }
    return hit;
}

fn intersectCovePropBox(
    rayOrigin : vec3<f32>, rayDirection : vec3<f32>,
    center : vec3<f32>, halfExtents : vec3<f32>,
    materialCue : f32, maximumDistance : f32) -> CovePropHit {
    let inlandAxis = vec3<f32>(-0.2730, 0.0, 0.9620);
    let alongshoreAxis = vec3<f32>(-0.9620, 0.0, -0.2730);
    let offset = rayOrigin - center;
    let localOrigin = vec3<f32>(
        dot(offset, inlandAxis),
        offset.y,
        dot(offset, alongshoreAxis));
    let localDirection = vec3<f32>(
        dot(rayDirection, inlandAxis),
        rayDirection.y,
        dot(rayDirection, alongshoreAxis));
    let safeDirection = select(
        vec3<f32>(1.0e-6), localDirection,
        abs(localDirection) > vec3<f32>(1.0e-6));
    let first = (-halfExtents - localOrigin) / safeDirection;
    let second = (halfExtents - localOrigin) / safeDirection;
    let nearPlane = min(first, second);
    let farPlane = max(first, second);
    let distance = max(
        max(nearPlane.x, nearPlane.y), nearPlane.z);
    let farDistance = min(
        min(farPlane.x, farPlane.y), farPlane.z);
    if (distance <= 0.001 || distance >= maximumDistance ||
        farDistance < distance) {
        return missedCoveProp();
    }

    let localHit = localOrigin + localDirection * distance;
    let face = abs(localHit) / max(halfExtents, vec3<f32>(1.0e-5));
    var localNormal = vec3<f32>(0.0);
    if (face.x > face.y && face.x > face.z) {
        localNormal.x = sign(localHit.x);
    } else if (face.y > face.z) {
        localNormal.y = sign(localHit.y);
    } else {
        localNormal.z = sign(localHit.z);
    }
    let worldNormal =
        inlandAxis * localNormal.x +
        vec3<f32>(0.0, 1.0, 0.0) * localNormal.y +
        alongshoreAxis * localNormal.z;
    return CovePropHit(
        distance, worldNormal, localHit.z, materialCue);
}

fn intersectCovePropHull(
    rayOrigin : vec3<f32>, rayDirection : vec3<f32>,
    center : vec3<f32>, radii : vec3<f32>,
    minimumAlongshore : f32, maximumAlongshore : f32,
    openTop : f32, maximumDistance : f32) -> CovePropHit {
    let inlandAxis = vec3<f32>(-0.2730, 0.0, 0.9620);
    let alongshoreAxis = vec3<f32>(-0.9620, 0.0, -0.2730);
    let offset = rayOrigin - center;
    let localOrigin = vec3<f32>(
        dot(offset, inlandAxis),
        offset.y,
        dot(offset, alongshoreAxis));
    let localDirection = vec3<f32>(
        dot(rayDirection, inlandAxis),
        rayDirection.y,
        dot(rayDirection, alongshoreAxis));
    let normalizedOrigin = localOrigin / radii;
    let normalizedDirection = localDirection / radii;
    let quadratic = dot(normalizedDirection, normalizedDirection);
    let linear = dot(normalizedOrigin, normalizedDirection);
    let constant = dot(normalizedOrigin, normalizedOrigin) - 1.0;
    let discriminant = linear * linear - quadratic * constant;
    if (discriminant < 0.0 || quadratic <= 1.0e-7) {
        return missedCoveProp();
    }

    let root = sqrt(discriminant);
    let nearDistance = (-linear - root) / quadratic;
    let farDistance = (-linear + root) / quadratic;
    var best = missedCoveProp();

    let nearHit = localOrigin + localDirection * nearDistance;
    if (nearDistance > 0.001 && nearDistance < maximumDistance &&
        nearHit.z >= minimumAlongshore &&
        nearHit.z <= maximumAlongshore &&
        nearHit.y <= openTop) {
        let localNormal = normalize(nearHit / (radii * radii));
        let worldNormal =
            inlandAxis * localNormal.x +
            vec3<f32>(0.0, 1.0, 0.0) * localNormal.y +
            alongshoreAxis * localNormal.z;
        best = CovePropHit(
            nearDistance, normalize(worldNormal), nearHit.z, 0.30);
    }

    let farHit = localOrigin + localDirection * farDistance;
    if (best.distance <= 0.0 &&
        farDistance > 0.001 && farDistance < maximumDistance &&
        farHit.z >= minimumAlongshore &&
        farHit.z <= maximumAlongshore &&
        farHit.y <= openTop) {
        let localNormal = normalize(farHit / (radii * radii));
        let worldNormal =
            inlandAxis * localNormal.x +
            vec3<f32>(0.0, 1.0, 0.0) * localNormal.y +
            alongshoreAxis * localNormal.z;
        best = CovePropHit(
            farDistance, normalize(worldNormal), farHit.z, 0.30);
    }
    return best;
}

fn closestCoveProp(
    first : CovePropHit, second : CovePropHit) -> CovePropHit {
    if (second.distance > 0.0 &&
        (first.distance <= 0.0 || second.distance < first.distance)) {
        return second;
    }
    return first;
}

fn coveHullPoint(
    inland : f32, alongshore : f32, height : f32) -> vec3<f32> {
    // Broadside to the authored inlet. Keeping the hull in cove-local
    // coordinates makes its keel, planking, and contact footprint agree.
    return vec3<f32>(-650.0, height - 2.60, 3471.0) +
           vec3<f32>(-0.2730, 0.0, 0.9620) * inland +
           vec3<f32>(-0.9620, 0.0, -0.2730) * alongshore;
}

fn authoredCovePropHit(
    rayOrigin : vec3<f32>, rayDirection : vec3<f32>,
    maximumDistance : f32) -> CovePropHit {
    if (authoredCourseEnabled()) { return missedCoveProp(); }
    // The wreck is evaluated only inside this conservative local sphere.
    // Its tallest broken spar still fits, while ordinary cove pixels pay one
    // quadratic and return.
    let boundsOffset =
        rayOrigin - coveHullPoint(0.0, 0.0, -191.5);
    let boundsProjection = dot(rayDirection, boundsOffset);
    let boundsDiscriminant =
        boundsProjection * boundsProjection -
        (dot(boundsOffset, boundsOffset) - 24.0 * 24.0);
    if (boundsDiscriminant < 0.0) {
        return missedCoveProp();
    }
    let boundsRoot = sqrt(boundsDiscriminant);
    if (-boundsProjection + boundsRoot <= 0.001 ||
        -boundsProjection - boundsRoot >= maximumDistance) {
        return missedCoveProp();
    }

    var best = missedCoveProp();

    // A twenty-eight-metre lower hull supplies one uninterrupted silhouette.
    // Its deeper vertical radius puts the keel below the local sand while
    // retaining 3–5 metres of readable side above the surf.
    best = closestCoveProp(best, intersectCovePropHull(
        rayOrigin, rayDirection,
        coveHullPoint(0.0, 0.2, -194.40),
        vec3<f32>(4.60, 6.20, 14.60),
        -14.1, 14.4, 0.60, maximumDistance));

    // Raised, unequal stems keep the ellipsoid ends from reading as a canoe.
    best = closestCoveProp(best, intersectCovePropCapsuleCue(
        rayOrigin, rayDirection,
        coveHullPoint(0.0, -13.5, -200.0),
        coveHullPoint(0.15, -15.0, -194.1),
        0.55, 0.08, maximumDistance));
    best = closestCoveProp(best, intersectCovePropCapsuleCue(
        rayOrigin, rayDirection,
        coveHullPoint(0.15, -15.0, -194.1),
        coveHullPoint(0.0, -14.25, -190.7),
        0.48, 0.08, maximumDistance));
    best = closestCoveProp(best, intersectCovePropCapsuleCue(
        rayOrigin, rayDirection,
        coveHullPoint(0.0, 13.8, -200.1),
        coveHullPoint(0.10, 15.1, -193.6),
        0.58, 0.08, maximumDistance));
    best = closestCoveProp(best, intersectCovePropCapsuleCue(
        rayOrigin, rayDirection,
        coveHullPoint(0.10, 15.1, -193.6),
        coveHullPoint(0.0, 14.15, -189.9),
        0.45, 0.08, maximumDistance));

    // Nine bent near-side frames. Each is two segments rather than one
    // telephone-pole primitive: keel-to-bilge, then bilge-to-gunwale.
    best = closestCoveProp(best, intersectCovePropCapsuleCue(
        rayOrigin, rayDirection,
        coveHullPoint(0.0, -11.7, -199.1),
        coveHullPoint(3.45, -11.7, -195.9),
        0.30, 0.78, maximumDistance));
    best = closestCoveProp(best, intersectCovePropCapsuleCue(
        rayOrigin, rayDirection,
        coveHullPoint(3.45, -11.7, -195.9),
        coveHullPoint(4.35, -11.7, -191.4),
        0.27, 0.78, maximumDistance));
    best = closestCoveProp(best, intersectCovePropCapsuleCue(
        rayOrigin, rayDirection,
        coveHullPoint(0.0, -8.8, -199.3),
        coveHullPoint(3.75, -8.8, -195.8),
        0.31, 0.84, maximumDistance));
    best = closestCoveProp(best, intersectCovePropCapsuleCue(
        rayOrigin, rayDirection,
        coveHullPoint(3.75, -8.8, -195.8),
        coveHullPoint(4.65, -8.8, -190.9),
        0.28, 0.84, maximumDistance));
    best = closestCoveProp(best, intersectCovePropCapsuleCue(
        rayOrigin, rayDirection,
        coveHullPoint(0.0, -5.7, -199.4),
        coveHullPoint(3.90, -5.7, -195.7),
        0.30, 0.90, maximumDistance));
    best = closestCoveProp(best, intersectCovePropCapsuleCue(
        rayOrigin, rayDirection,
        coveHullPoint(3.90, -5.7, -195.7),
        coveHullPoint(4.75, -5.7, -190.6),
        0.27, 0.90, maximumDistance));
    best = closestCoveProp(best, intersectCovePropCapsuleCue(
        rayOrigin, rayDirection,
        coveHullPoint(0.0, -2.4, -199.5),
        coveHullPoint(3.98, -2.4, -195.7),
        0.32, 0.82, maximumDistance));
    best = closestCoveProp(best, intersectCovePropCapsuleCue(
        rayOrigin, rayDirection,
        coveHullPoint(3.98, -2.4, -195.7),
        coveHullPoint(4.82, -2.4, -190.4),
        0.29, 0.82, maximumDistance));
    best = closestCoveProp(best, intersectCovePropCapsuleCue(
        rayOrigin, rayDirection,
        coveHullPoint(0.0, 1.0, -199.5),
        coveHullPoint(4.05, 1.0, -195.6),
        0.31, 0.88, maximumDistance));
    best = closestCoveProp(best, intersectCovePropCapsuleCue(
        rayOrigin, rayDirection,
        coveHullPoint(4.05, 1.0, -195.6),
        coveHullPoint(4.88, 1.0, -190.5),
        0.28, 0.88, maximumDistance));
    best = closestCoveProp(best, intersectCovePropCapsuleCue(
        rayOrigin, rayDirection,
        coveHullPoint(0.0, 4.3, -199.5),
        coveHullPoint(3.95, 4.3, -195.7),
        0.30, 0.80, maximumDistance));
    best = closestCoveProp(best, intersectCovePropCapsuleCue(
        rayOrigin, rayDirection,
        coveHullPoint(3.95, 4.3, -195.7),
        coveHullPoint(4.80, 4.3, -190.7),
        0.27, 0.80, maximumDistance));
    best = closestCoveProp(best, intersectCovePropCapsuleCue(
        rayOrigin, rayDirection,
        coveHullPoint(0.0, 7.5, -199.4),
        coveHullPoint(3.80, 7.5, -195.8),
        0.32, 0.86, maximumDistance));
    best = closestCoveProp(best, intersectCovePropCapsuleCue(
        rayOrigin, rayDirection,
        coveHullPoint(3.80, 7.5, -195.8),
        coveHullPoint(4.65, 7.5, -190.9),
        0.29, 0.86, maximumDistance));
    best = closestCoveProp(best, intersectCovePropCapsuleCue(
        rayOrigin, rayDirection,
        coveHullPoint(0.0, 10.3, -199.2),
        coveHullPoint(3.55, 10.3, -195.9),
        0.30, 0.92, maximumDistance));
    best = closestCoveProp(best, intersectCovePropCapsuleCue(
        rayOrigin, rayDirection,
        coveHullPoint(3.55, 10.3, -195.9),
        coveHullPoint(4.40, 10.3, -191.2),
        0.27, 0.92, maximumDistance));
    best = closestCoveProp(best, intersectCovePropCapsuleCue(
        rayOrigin, rayDirection,
        coveHullPoint(0.0, 12.7, -198.9),
        coveHullPoint(3.15, 12.7, -195.8),
        0.29, 0.82, maximumDistance));
    best = closestCoveProp(best, intersectCovePropCapsuleCue(
        rayOrigin, rayDirection,
        coveHullPoint(3.15, 12.7, -195.8),
        coveHullPoint(3.95, 12.7, -191.5),
        0.26, 0.82, maximumDistance));

    // Sparse far-side frames make the opened upper hull read as a cavity.
    best = closestCoveProp(best, intersectCovePropCapsuleCue(
        rayOrigin, rayDirection,
        coveHullPoint(-3.65, -9.7, -195.8),
        coveHullPoint(-4.45, -9.7, -191.1),
        0.24, 0.98, maximumDistance));
    best = closestCoveProp(best, intersectCovePropCapsuleCue(
        rayOrigin, rayDirection,
        coveHullPoint(-3.95, -5.0, -195.7),
        coveHullPoint(-4.72, -5.0, -190.8),
        0.24, 0.98, maximumDistance));
    best = closestCoveProp(best, intersectCovePropCapsuleCue(
        rayOrigin, rayDirection,
        coveHullPoint(-4.05, 0.0, -195.6),
        coveHullPoint(-4.82, 0.0, -190.7),
        0.25, 0.98, maximumDistance));
    best = closestCoveProp(best, intersectCovePropCapsuleCue(
        rayOrigin, rayDirection,
        coveHullPoint(-3.92, 5.1, -195.7),
        coveHullPoint(-4.68, 5.1, -190.9),
        0.24, 0.98, maximumDistance));
    best = closestCoveProp(best, intersectCovePropCapsuleCue(
        rayOrigin, rayDirection,
        coveHullPoint(-3.58, 9.6, -195.9),
        coveHullPoint(-4.38, 9.6, -191.3),
        0.24, 0.98, maximumDistance));

    // Thin, irregular surviving plank courses. Their short unequal runs leave
    // intentional holes through which the frame chains remain visible.
    best = closestCoveProp(best, intersectCovePropBox(
        rayOrigin, rayDirection,
        coveHullPoint(4.30, -9.0, -194.65),
        vec3<f32>(0.28, 0.24, 4.2),
        0.05, maximumDistance));
    best = closestCoveProp(best, intersectCovePropBox(
        rayOrigin, rayDirection,
        coveHullPoint(4.45, 0.2, -194.55),
        vec3<f32>(0.27, 0.22, 3.2),
        0.05, maximumDistance));
    best = closestCoveProp(best, intersectCovePropBox(
        rayOrigin, rayDirection,
        coveHullPoint(4.18, 9.3, -194.30),
        vec3<f32>(0.28, 0.23, 4.0),
        0.05, maximumDistance));
    best = closestCoveProp(best, intersectCovePropBox(
        rayOrigin, rayDirection,
        coveHullPoint(4.62, -10.2, -192.95),
        vec3<f32>(0.25, 0.20, 2.9),
        0.04, maximumDistance));
    best = closestCoveProp(best, intersectCovePropBox(
        rayOrigin, rayDirection,
        coveHullPoint(4.78, -2.8, -192.80),
        vec3<f32>(0.24, 0.19, 2.4),
        0.04, maximumDistance));
    best = closestCoveProp(best, intersectCovePropBox(
        rayOrigin, rayDirection,
        coveHullPoint(4.70, 4.6, -192.72),
        vec3<f32>(0.25, 0.20, 3.0),
        0.04, maximumDistance));
    best = closestCoveProp(best, intersectCovePropBox(
        rayOrigin, rayDirection,
        coveHullPoint(4.43, 11.2, -192.60),
        vec3<f32>(0.24, 0.19, 1.7),
        0.04, maximumDistance));
    best = closestCoveProp(best, intersectCovePropBox(
        rayOrigin, rayDirection,
        coveHullPoint(4.55, -8.2, -191.20),
        vec3<f32>(0.23, 0.18, 3.6),
        0.03, maximumDistance));
    best = closestCoveProp(best, intersectCovePropBox(
        rayOrigin, rayDirection,
        coveHullPoint(4.82, 0.0, -191.00),
        vec3<f32>(0.22, 0.18, 2.2),
        0.03, maximumDistance));
    best = closestCoveProp(best, intersectCovePropBox(
        rayOrigin, rayDirection,
        coveHullPoint(4.40, 8.5, -190.80),
        vec3<f32>(0.23, 0.18, 3.7),
        0.03, maximumDistance));

    // A few far-side plank remnants deepen the opened cavity.
    best = closestCoveProp(best, intersectCovePropBox(
        rayOrigin, rayDirection,
        coveHullPoint(-4.55, -7.2, -190.75),
        vec3<f32>(0.22, 0.16, 3.1),
        0.96, maximumDistance));
    best = closestCoveProp(best, intersectCovePropBox(
        rayOrigin, rayDirection,
        coveHullPoint(-4.72, 1.4, -190.55),
        vec3<f32>(0.22, 0.16, 2.6),
        0.96, maximumDistance));
    best = closestCoveProp(best, intersectCovePropBox(
        rayOrigin, rayDirection,
        coveHullPoint(-4.35, 9.2, -190.55),
        vec3<f32>(0.22, 0.16, 2.8),
        0.96, maximumDistance));

    // Seventeen-metre broken mast and its surviving yard dominate the skyline.
    best = closestCoveProp(best, intersectCovePropCapsuleCue(
        rayOrigin, rayDirection,
        coveHullPoint(0.25, -1.2, -197.6),
        coveHullPoint(0.10, -5.8, -180.8),
        0.54, 0.06, maximumDistance));
    best = closestCoveProp(best, intersectCovePropCapsuleCue(
        rayOrigin, rayDirection,
        coveHullPoint(0.10, -12.7, -183.0),
        coveHullPoint(0.10, -4.8, -180.9),
        0.30, 0.08, maximumDistance));
    return best;
}

fn distanceToCoveSegment(
    point : vec2<f32>, endpointA : vec2<f32>,
    endpointB : vec2<f32>) -> f32 {
    let axis = endpointB - endpointA;
    let axisLengthSquared = max(dot(axis, axis), 1.0e-5);
    let fraction = clamp(
        dot(point - endpointA, axis) / axisLengthSquared,
        0.0, 1.0);
    return length(point - (endpointA + axis * fraction));
}

fn coveDriftwoodContact(worldPosition : vec2<f32>) -> f32 {
    if (authoredCourseEnabled()) { return 1.0; }
    let offset = worldPosition - vec2<f32>(-650.0, 3471.0);
    let hullInland = dot(offset, vec2<f32>(-0.2730, 0.9620));
    let hullAlongshore = dot(offset, vec2<f32>(-0.9620, -0.2730));
    let footprint = length(vec2<f32>(
        hullInland / 4.8, hullAlongshore / 14.4));
    let hullContact =
        1.0 - smoothstep(0.70, 1.18, footprint);
    return mix(1.0, 0.92, hullContact);
}

fn terrainMaterialScale(layer : i32) -> f32 {
    switch (layer) {
        case TERRAIN_LAYER_SAND: { return 7.5; }
        case TERRAIN_LAYER_SOIL: { return 8.5; }
        case TERRAIN_LAYER_GRASS: { return 6.5; }
        default: { return 10.0; }
    }
}

fn rotateTerrainMaterialUv(value : vec2<f32>, layer : i32) -> vec2<f32> {
    // Fixed rotations prevent the four source families from sharing axes.
    switch (layer) {
        case TERRAIN_LAYER_SAND: {
            return vec2<f32>(
                value.x * 0.819152 + value.y * 0.573576,
                value.y * 0.819152 - value.x * 0.573576);
        }
        case TERRAIN_LAYER_SOIL: {
            return vec2<f32>(
                value.x * 0.956305 - value.y * 0.292372,
                value.x * 0.292372 + value.y * 0.956305);
        }
        case TERRAIN_LAYER_GRASS: {
            return vec2<f32>(
                value.x * 0.731354 + value.y * 0.681998,
                value.y * 0.731354 - value.x * 0.681998);
        }
        default: {
            return vec2<f32>(
                value.x * 0.887011 - value.y * 0.461749,
                value.x * 0.461749 + value.y * 0.887011);
        }
    }
}

fn terrainMaterialUv(projectedWorld : vec2<f32>, layer : i32) -> vec2<f32> {
    let phase = f32(layer) * 1.713;
    let scale = terrainMaterialScale(layer);
    let rotated = rotateTerrainMaterialUv(projectedWorld / scale, layer);
    // Cross-coupled, axis-independent warp. The previous sum of diagonal dot
    // products produced long parallel bands at grazing angles. Evaluating this
    // same function for neighbor positions keeps explicit mip gradients exact.
    let warp = vec2<f32>(
        sin(projectedWorld.x * 0.0143 +
            sin(projectedWorld.y * 0.0091 + phase) * 1.31),
        sin(projectedWorld.y * 0.0167 +
            sin(projectedWorld.x * 0.0107 - phase) * 1.17));
    let broad = sin(projectedWorld.x * 0.0037 + phase) *
                sin(projectedWorld.y * 0.0049 - phase * 0.63);
    let noisePoint =
        projectedWorld * 0.031 +
        vec2<f32>(phase * 3.17, phase * -2.31);
    let stochasticWarp = vec2<f32>(
        periodicGradientNoise(noisePoint),
        periodicGradientNoise(
            noisePoint.yx * 1.37 + vec2<f32>(7.1, 11.3)));
    return rotated + warp * 0.105 + stochasticWarp * 0.38 +
           vec2<f32>(broad, -broad) * 0.026;
}

fn sampleTerrainLayer(
    layer : i32, projectedWorld : vec2<f32>,
    projectedX : vec2<f32>, projectedY : vec2<f32>) -> TerrainLayerSample {
    let uv = terrainMaterialUv(projectedWorld, layer);
    let uvX = terrainMaterialUv(projectedX, layer);
    let uvY = terrainMaterialUv(projectedY, layer);
    let gradientX = uvX - uv;
    let gradientY = uvY - uv;
    // A second incommensurate, quarter-turned lookup removes the repeated
    // lawnmower bands that a single world projection exposes at grazing
    // shoreline angles. Rotate its tangent-space normal back before blending.
    let secondaryUv =
        vec2<f32>(-uv.y, uv.x) * 1.618 +
        vec2<f32>(0.173, 0.619) * (f32(layer) + 1.0);
    let secondaryGradientX =
        vec2<f32>(-gradientX.y, gradientX.x) * 1.618;
    let secondaryGradientY =
        vec2<f32>(-gradientY.y, gradientY.x) * 1.618;
    let antiTileBlend = clamp(
        0.50 + 0.18 *
        sin(projectedWorld.x * 0.0181 + f32(layer) * 0.73) *
        sin(projectedWorld.y * 0.0147 - f32(layer) * 0.51),
        0.28, 0.72);
    // The sRGB array view performs the transfer to linear light in hardware.
    let primaryAlbedo = textureSampleGrad(
        terrainMaterialAlbedo, oceanFoamSampler,
        uv, layer, gradientX, gradientY).rgb;
    let secondaryAlbedo = textureSampleGrad(
        terrainMaterialAlbedo, oceanFoamSampler,
        secondaryUv, layer,
        secondaryGradientX, secondaryGradientY).rgb;
    var albedo = mix(primaryAlbedo, secondaryAlbedo, antiTileBlend);
    // A heavily filtered, incommensurate lookup carries only broad color
    // variation. It masks the source tile's repeat without magnifying its
    // texels or introducing another high-frequency normal field.
    let macroUv =
        rotateTerrainMaterialUv(
            projectedWorld /
                (terrainMaterialScale(layer) * 11.3),
            layer) +
        vec2<f32>(0.193, 0.617) * (f32(layer) + 1.0);
    let macroAlbedo = textureSampleLevel(
        terrainMaterialAlbedo, oceanFoamSampler,
        macroUv, layer, 5.5).rgb;
    let macroLuminance = dot(
        macroAlbedo, vec3<f32>(0.2126, 0.7152, 0.0722));
    let macroGain = clamp(
        0.92 + (macroLuminance - 0.25) * 0.45,
        0.84, 1.12);
    let macroTint =
        mix(vec3<f32>(macroGain),
            macroAlbedo / max(macroLuminance, 0.05) * macroGain,
            0.10);
    let broadNoise = periodicGradientNoise(
        projectedWorld * 0.018 +
        vec2<f32>(f32(layer) * 4.7, f32(layer) * -3.1));
    let mesoNoise = periodicGradientNoise(
        projectedWorld.yx * 0.057 +
        vec2<f32>(f32(layer) * 8.3, f32(layer) * 5.9));
    let proceduralGain = clamp(
        1.0 + broadNoise * 0.10 + mesoNoise * 0.035,
        0.90, 1.10);
    albedo *= macroTint * proceduralGain;
    let primaryDetail = textureSampleGrad(
        terrainMaterialNormalRoughness, oceanFoamSampler,
        uv, layer, gradientX, gradientY);
    let secondaryDetail = textureSampleGrad(
        terrainMaterialNormalRoughness, oceanFoamSampler,
        secondaryUv, layer,
        secondaryGradientX, secondaryGradientY);
    let primaryNormal =
        primaryDetail.rgb * 2.0 - vec3<f32>(1.0);
    let sampledSecondaryNormal =
        secondaryDetail.rgb * 2.0 - vec3<f32>(1.0);
    let secondaryNormal = vec3<f32>(
        sampledSecondaryNormal.y,
        -sampledSecondaryNormal.x,
        sampledSecondaryNormal.z);
    let unpackedNormal = mix(
        primaryNormal, secondaryNormal, antiTileBlend);
    var normalStrength = 0.68;
    switch (layer) {
        case TERRAIN_LAYER_SAND: { normalStrength = 0.18; }
        case TERRAIN_LAYER_SOIL: { normalStrength = 0.20; }
        case TERRAIN_LAYER_GRASS: { normalStrength = 0.17; }
        default: { normalStrength = 0.42; }
    }
    let scaledNormal = vec3<f32>(
        unpackedNormal.xy * normalStrength,
        max(unpackedNormal.z, 0.18));
    let normalLength = length(scaledNormal);
    let tangentNormal = select(
        vec3<f32>(0.0, 0.0, 1.0),
        scaledNormal / normalLength,
        normalLength > 1.0e-5);
    return TerrainLayerSample(
        albedo,
        tangentNormal,
        clamp(mix(
            primaryDetail.a, secondaryDetail.a, antiTileBlend),
            0.045, 1.0));
}

fn topProjectionNormal(
    tangentNormal : vec3<f32>,
    geometryNormal : vec3<f32>) -> vec3<f32> {
    // Align the top-projected NormalGL map to the real heightfield tangent
    // plane. On flat ground this basis is +X, -Z, +Y.
    let primary = vec3<f32>(1.0, 0.0, 0.0) -
                  geometryNormal * geometryNormal.x;
    let fallback = vec3<f32>(0.0, 0.0, -1.0) +
                   geometryNormal * geometryNormal.z;
    let tangentRaw = select(
        fallback, primary, dot(primary, primary) > 1.0e-5);
    let tangent = normalize(tangentRaw);
    let bitangent = normalize(cross(geometryNormal, tangent));
    return normalize(
        tangent * tangentNormal.x +
        bitangent * tangentNormal.y +
        geometryNormal * tangentNormal.z);
}

fn terrainMaterialWeights(
    worldPosition : vec3<f32>,
    geometryNormal : vec3<f32>) -> vec4<f32> {
    let relativeElevation = worldPosition.y - camera.waterParams.x;
    // Broad two-dimensional variation; no linear diagonal carrier.
    let macroVariation =
        sin(worldPosition.x * 0.0061 +
            sin(worldPosition.z * 0.0103) * 1.4) * 0.85 +
        sin(worldPosition.z * 0.0087 +
            sin(worldPosition.x * 0.0043) * 1.1) * 0.55;
    var elevation = relativeElevation + macroVariation;
    let up = clamp(geometryNormal.y, 0.0, 1.0);
    let steepness = 1.0 - up;

    let coveOffset = worldPosition.xz - vec2<f32>(-650.0, 3450.0);
    let coveInland =
        dot(coveOffset, vec2<f32>(-0.2730, 0.9620));
    let coveAlongshore =
        dot(coveOffset, vec2<f32>(-0.9620, -0.2730));
    let coveZone = select(1.0 - smoothstep(
        56.0, 80.0,
        length(vec2<f32>(coveInland, coveAlongshore))),
        0.0, authoredCourseEnabled());
    if (coveZone > 0.0) {
        elevation += coveZone * (
            0.55 * sin(coveAlongshore * 0.143 +
                       sin(coveInland * 0.071) * 1.3) +
            0.24 * sin(coveAlongshore * 0.337 -
                       sin(coveInland * 0.113)));
    }
    var authoredRock = 0.0;
    if (!authoredCourseEnabled() && length(vec2<f32>(coveInland, coveAlongshore)) < 78.0) {
        // The four explicit subtidal boulders have shallow tops, so slope alone
        // cannot identify their material. Keep their footprint compact.
        let boulder0 = exp(-dot(
            (vec2<f32>(coveInland, coveAlongshore) -
             vec2<f32>(-63.0, 8.0)) / 4.0,
            (vec2<f32>(coveInland, coveAlongshore) -
             vec2<f32>(-63.0, 8.0)) / 4.0));
        let boulder1 = exp(-dot(
            (vec2<f32>(coveInland, coveAlongshore) -
             vec2<f32>(-58.0, -11.0)) / 4.6,
            (vec2<f32>(coveInland, coveAlongshore) -
             vec2<f32>(-58.0, -11.0)) / 4.6));
        let boulder2 = exp(-dot(
            (vec2<f32>(coveInland, coveAlongshore) -
             vec2<f32>(-51.0, 16.0)) / 3.3,
            (vec2<f32>(coveInland, coveAlongshore) -
             vec2<f32>(-51.0, 16.0)) / 3.3));
        let boulder3 = exp(-dot(
            (vec2<f32>(coveInland, coveAlongshore) -
             vec2<f32>(-47.0, -21.0)) / 2.7,
            (vec2<f32>(coveInland, coveAlongshore) -
             vec2<f32>(-47.0, -21.0)) / 2.7));
        let boulderMask =
            max(max(boulder0, boulder1), max(boulder2, boulder3));
        let landBoulder0Offset =
            (vec2<f32>(coveInland, coveAlongshore) -
             vec2<f32>(11.0, -30.0)) / vec2<f32>(4.6, 3.0);
        let landBoulder1Offset =
            (vec2<f32>(coveInland, coveAlongshore) -
             vec2<f32>(15.5, -24.5)) / vec2<f32>(3.2, 2.2);
        let landBoulder2Offset =
            (vec2<f32>(coveInland, coveAlongshore) -
             vec2<f32>(27.0, -34.0)) / vec2<f32>(4.0, 2.5);
        let landBoulder3Offset =
            (vec2<f32>(coveInland, coveAlongshore) -
             vec2<f32>(19.0, 27.0)) / vec2<f32>(4.8, 2.8);
        let landBoulder4Offset =
            (vec2<f32>(coveInland, coveAlongshore) -
             vec2<f32>(25.5, 32.5)) / vec2<f32>(3.1, 2.0);
        let landBoulder5Offset =
            (vec2<f32>(coveInland, coveAlongshore) -
             vec2<f32>(34.0, 23.0)) / vec2<f32>(3.7, 2.4);
        let landBoulderMask = max(
            max(
                exp(-dot(landBoulder0Offset, landBoulder0Offset)),
                exp(-dot(landBoulder1Offset, landBoulder1Offset))),
            max(
                max(
                    exp(-dot(landBoulder2Offset, landBoulder2Offset)),
                    exp(-dot(landBoulder3Offset, landBoulder3Offset))),
                max(
                    exp(-dot(landBoulder4Offset, landBoulder4Offset)),
                    exp(-dot(landBoulder5Offset, landBoulder5Offset)))));
        let outcrop0Offset =
            (vec2<f32>(coveInland, coveAlongshore) -
             vec2<f32>(18.0, -47.0)) / vec2<f32>(27.0, 17.0);
        let outcrop1Offset =
            (vec2<f32>(coveInland, coveAlongshore) -
             vec2<f32>(19.0, 47.0)) / vec2<f32>(24.0, 13.0);
        let outcropMask = max(
            exp(-dot(outcrop0Offset, outcrop0Offset)),
            exp(-dot(outcrop1Offset, outcrop1Offset)));
        authoredRock = max(
            max(
                smoothstep(0.14, 0.62, boulderMask) * 0.92,
                smoothstep(0.18, 0.66, landBoulderMask) * 0.72),
            smoothstep(0.28, 0.74, outcropMask) * 0.52);
    }

    let slopeRock = smoothstep(0.12, 0.43, steepness);
    let highRock = smoothstep(220.0, 410.0, elevation) *
                   smoothstep(0.04, 0.34, steepness) * 0.55;
    let rock = clamp(
        max(max(slopeRock, highRock), authoredRock), 0.0, 1.0);

    // A fully rock-covered pixel has no sand/soil/grass contribution.
    if (rock == 1.0) { return vec4<f32>(0.0, 0.0, 0.0, 1.0); }

    let beachElevation = 1.0 - smoothstep(2.8, 5.2, elevation);
    let beachFlatness = smoothstep(0.48, 0.82, up);
    var sand = beachElevation * beachFlatness * (1.0 - rock);
    var transitionSignal = 0.0;
    if (coveZone > 0.0) {
        let beachMottle =
            0.5 + 0.5 * periodicGradientNoise(
                vec2<f32>(coveInland, coveAlongshore) * 0.091 +
                vec2<f32>(4.3, 12.7));
        let mixedUpperBeach =
            coveZone *
            smoothstep(0.65, 2.2, elevation) *
            (1.0 - smoothstep(4.2, 5.6, elevation));
        sand *= mix(
            1.0,
            mix(0.54, 1.0, smoothstep(0.24, 0.76, beachMottle)),
            mixedUpperBeach);
        transitionSignal =
            0.5 + 0.5 *
            sin(coveAlongshore * 0.19 +
                sin(coveInland * 0.071) * 1.7) *
            sin(coveInland * 0.23 -
                sin(coveAlongshore * 0.053) * 1.4);
        let sparseBackshore =
            coveZone *
            smoothstep(4.5, 7.5, elevation) *
            (1.0 - smoothstep(18.0, 27.0, elevation)) *
            smoothstep(0.20, 0.86, transitionSignal);
        sand = clamp(
            sand + sparseBackshore * (1.0 - rock) * 0.32,
            0.0, 1.0 - rock);
    }
    let upland = max(1.0 - rock - sand, 0.0);
    let grassVariation = 0.5 + 0.5 *
        sin(worldPosition.x * 0.017 +
            sin(worldPosition.z * 0.011) * 1.6) *
        sin(worldPosition.z * 0.019 -
            sin(worldPosition.x * 0.007) * 1.2);
    let grassSuitability =
        smoothstep(0.78, 0.91, up) *
        smoothstep(8.0, 14.0, elevation) *
        (1.0 - smoothstep(250.0, 390.0, elevation));
    var erosionPatch = 0.0;
    if (coveZone > 0.0) {
        let washCenter =
            7.0 +
            4.1 * sin((coveInland - 12.0) * 0.086) +
            1.2 * sin((coveInland + 9.0) * 0.217);
        let washLength =
            smoothstep(9.0, 18.0, coveInland) *
            (1.0 - smoothstep(57.0, 68.0, coveInland));
        let washCore =
            exp(-pow(
                (coveAlongshore - washCenter) /
                (4.8 + 0.75 * sin(coveInland * 0.19)),
                2.0)) *
            washLength * coveZone;
        erosionPatch = max(
            coveZone *
            smoothstep(7.0, 11.0, elevation) *
            (1.0 - smoothstep(34.0, 48.0, elevation)) *
            smoothstep(0.34, 0.88, 1.0 - transitionSignal),
            washCore * 0.55);
    }
    var grass = upland * grassSuitability *
                mix(0.20, 1.0, grassVariation) *
                mix(1.0, 0.30, erosionPatch);
    var soil = max(upland - grass, 0.0);
    if (authoredCourseEnabled()) {
        let authoredSoil = smoothstep(0.035, 0.72,
            textureSampleLevel(terrainTex, terrainSampler,
                terrainUV(worldPosition), 0.0).a) * (1.0 - rock);
        sand *= 1.0 - authoredSoil;
        grass *= 1.0 - authoredSoil;
        soil = max(soil, authoredSoil);
    }

    let weights = vec4<f32>(sand, soil, grass, rock);
    return weights / max(dot(weights, vec4<f32>(1.0)), 1.0e-5);
}

fn sampleRockTriplanar(
    worldPosition : vec3<f32>,
    worldX : vec3<f32>, worldY : vec3<f32>,
    geometryNormal : vec3<f32>) -> TerrainSurface {
    let signAxis = select(
        vec3<f32>(-1.0), vec3<f32>(1.0),
        geometryNormal >= vec3<f32>(0.0));
    var weights = pow(abs(geometryNormal), vec3<f32>(4.0));
    weights /= max(dot(weights, vec3<f32>(1.0)), 1.0e-5);

    let projectionX = vec2<f32>(
        worldPosition.z, -signAxis.x * worldPosition.y);
    let projectionXX = vec2<f32>(
        worldX.z, -signAxis.x * worldX.y);
    let projectionXY = vec2<f32>(
        worldY.z, -signAxis.x * worldY.y);
    let projectionY = vec2<f32>(
        worldPosition.x, -signAxis.y * worldPosition.z);
    let projectionYX = vec2<f32>(
        worldX.x, -signAxis.y * worldX.z);
    let projectionYY = vec2<f32>(
        worldY.x, -signAxis.y * worldY.z);
    let projectionZ = vec2<f32>(
        worldPosition.x, signAxis.z * worldPosition.y);
    let projectionZX = vec2<f32>(
        worldX.x, signAxis.z * worldX.y);
    let projectionZY = vec2<f32>(
        worldY.x, signAxis.z * worldY.y);

    var albedo = vec3<f32>(0.0);
    var worldNormal = vec3<f32>(0.0);
    var roughness = 0.0;
    if (weights.x > 0.0005) {
        let layer = sampleTerrainLayer(
            TERRAIN_LAYER_ROCK, projectionX, projectionXX, projectionXY);
        let mapped = vec3<f32>(
            signAxis.x * layer.tangentNormal.z,
            -signAxis.x * layer.tangentNormal.y,
            layer.tangentNormal.x);
        albedo += layer.albedo * weights.x;
        worldNormal += mapped * weights.x;
        roughness += layer.roughness * weights.x;
    }
    if (weights.y > 0.0005) {
        let layer = sampleTerrainLayer(
            TERRAIN_LAYER_ROCK, projectionY, projectionYX, projectionYY);
        let mapped = vec3<f32>(
            layer.tangentNormal.x,
            signAxis.y * layer.tangentNormal.z,
            -signAxis.y * layer.tangentNormal.y);
        albedo += layer.albedo * weights.y;
        worldNormal += mapped * weights.y;
        roughness += layer.roughness * weights.y;
    }
    if (weights.z > 0.0005) {
        let layer = sampleTerrainLayer(
            TERRAIN_LAYER_ROCK, projectionZ, projectionZX, projectionZY);
        let mapped = vec3<f32>(
            layer.tangentNormal.x,
            signAxis.z * layer.tangentNormal.y,
            signAxis.z * layer.tangentNormal.z);
        albedo += layer.albedo * weights.z;
        worldNormal += mapped * weights.z;
        roughness += layer.roughness * weights.z;
    }
    return TerrainSurface(
        albedo, normalize(worldNormal), roughness, 0.0);
}

fn sampleTerrainSurface(
    worldPosition : vec3<f32>,
    worldX : vec3<f32>, worldY : vec3<f32>,
    geometryNormalIn : vec3<f32>) -> TerrainSurface {
    let geometryNormal = normalize(geometryNormalIn);
    let shadingGeometryNormal =
        filterUpFacingTerrainNormal(geometryNormal);
    let weights = terrainMaterialWeights(worldPosition, geometryNormal);
    let projected = vec2<f32>(worldPosition.x, -worldPosition.z);
    let projectedX = vec2<f32>(worldX.x, -worldX.z);
    let projectedY = vec2<f32>(worldY.x, -worldY.z);

    var albedo = vec3<f32>(0.0);
    var worldNormal = vec3<f32>(0.0);
    var roughness = 0.0;
    if (weights.x > 0.0005) {
        let layer = sampleTerrainLayer(
            TERRAIN_LAYER_SAND, projected, projectedX, projectedY);
        albedo += layer.albedo * weights.x;
        worldNormal += topProjectionNormal(
            layer.tangentNormal, shadingGeometryNormal) * weights.x;
        roughness += layer.roughness * weights.x;
    }
    if (weights.y > 0.0005) {
        let layer = sampleTerrainLayer(
            TERRAIN_LAYER_SOIL, projected, projectedX, projectedY);
        albedo += layer.albedo * weights.y;
        worldNormal += topProjectionNormal(
            layer.tangentNormal, shadingGeometryNormal) * weights.y;
        roughness += layer.roughness * weights.y;
    }
    if (weights.z > 0.0005) {
        let layer = sampleTerrainLayer(
            TERRAIN_LAYER_GRASS, projected, projectedX, projectedY);
        albedo += layer.albedo * weights.z;
        worldNormal += topProjectionNormal(
            layer.tangentNormal, shadingGeometryNormal) * weights.z;
        roughness += layer.roughness * weights.z;
    }
    if (weights.w > 0.0005) {
        let layer = sampleRockTriplanar(
            worldPosition, worldX, worldY, geometryNormal);
        albedo += layer.albedo *
                  vec3<f32>(1.42, 1.34, 1.22) * weights.w;
        worldNormal += layer.normal * weights.w;
        roughness += layer.roughness * weights.w;
    }

    if (authoredCourseEnabled()) {
        let courseSoil = textureSampleLevel(
            terrainTex, terrainSampler, terrainUV(worldPosition), 0.0).a;
        albedo *= mix(vec3<f32>(1.0), vec3<f32>(0.30, 0.20, 0.12),
            smoothstep(0.08, 0.78, courseSoil) * 0.85);
    }
    let relativeElevation = worldPosition.y - camera.waterParams.x;
    let waterEnabled = select(0.0, 1.0, camera.waterParams.y > 0.5);
    // Noise dot products are bounded by 2; the run-up limit is below 0.30.
    // Away from this narrow wet transition, the original smoothstep is exact
    // zero/one, so neither procedural field needs to be evaluated.
    // Static run-up history: the top of the wet band sits above the current
    // water plane and fades over a narrow irregular band. This remains stable,
    // unlike wetness derived from one instantaneous crest.
    var wetness = 0.0;
    if (waterEnabled > 0.0 && relativeElevation < 0.30) {
        wetness = 1.0;
        if (relativeElevation > 0.015) {
            let runupBreakup = periodicGradientNoise(
                worldPosition.xz * 0.054 + vec2<f32>(3.7, 9.1));
            let runupDetail = periodicGradientNoise(
                worldPosition.zx * 0.13 + vec2<f32>(12.4, 2.8));
            let runupLimit =
                0.10 + runupBreakup * 0.065 + runupDetail * 0.020;
            wetness =
                (1.0 - smoothstep(0.015, max(runupLimit, 0.04),
                                  relativeElevation)) *
                waterEnabled;
        }
    }
    let submerged =
        (1.0 - smoothstep(-0.8, -0.02, relativeElevation)) *
        waterEnabled;
    let wetResponse = wetness *
        (0.92 * weights.x + 0.08 * weights.y);
    albedo *= mix(
        vec3<f32>(1.0),
        vec3<f32>(0.48, 0.42, 0.32),
        wetResponse * 0.52);
    albedo *= mix(
        vec3<f32>(1.0),
        vec3<f32>(0.76, 0.82, 0.74),
        submerged * 0.22);
    let macroBreakup =
        0.97 + 0.045 *
        sin(worldPosition.x * 0.031 +
            sin(worldPosition.z * 0.017) * 1.3) *
        sin(worldPosition.z * 0.037 -
            sin(worldPosition.x * 0.013) * 1.1);
    let coveOffset =
        worldPosition.xz - vec2<f32>(-650.0, 3450.0);
    let coveInland =
        dot(coveOffset, vec2<f32>(-0.2730, 0.9620));
    let coveAlongshore =
        dot(coveOffset, vec2<f32>(-0.9620, -0.2730));
    let coveMask = select(
        1.0 - smoothstep(50.0, 78.0,
            length(vec2<f32>(coveInland, coveAlongshore))),
        0.0, authoredCourseEnabled());
    if (coveMask > 0.0) {
        let organicPatch =
            0.5 + 0.5 *
            sin(coveInland * 0.097 +
                sin(coveAlongshore * 0.041) * 1.8) *
            sin(coveAlongshore * 0.123 -
                sin(coveInland * 0.063) * 1.4);
        let backshorePatch =
            coveMask *
            smoothstep(5.5, 9.0, relativeElevation) *
            (1.0 - smoothstep(31.0, 44.0, relativeElevation));
        albedo *= mix(
            vec3<f32>(1.0),
            mix(
                vec3<f32>(0.72, 0.88, 0.68),
                vec3<f32>(1.10, 0.84, 0.66),
                organicPatch),
            backshorePatch * 0.12);
        let washCenter =
            7.0 +
            4.1 * sin((coveInland - 12.0) * 0.086) +
            1.2 * sin((coveInland + 9.0) * 0.217);
        let washMask =
            exp(-pow(
                (coveAlongshore - washCenter) /
                (4.8 + 0.75 * sin(coveInland * 0.19)),
                2.0)) *
            smoothstep(9.0, 18.0, coveInland) *
            (1.0 - smoothstep(57.0, 68.0, coveInland)) *
            coveMask;
        albedo *= mix(
            vec3<f32>(1.0),
            vec3<f32>(0.70, 0.65, 0.55),
            washMask * 0.20);
        let wrackElevation =
            1.45 +
            0.22 * sin(
                coveAlongshore * 0.23 +
                sin(coveInland * 0.08));
        let wrackBreakup =
            smoothstep(
                0.35, 0.82,
                0.5 + 0.5 *
                sin(coveAlongshore * 0.71) *
                sin(coveAlongshore * 0.19 + 1.3));
        let wrack =
            coveMask * weights.x * wrackBreakup *
            (1.0 - smoothstep(
                0.10, 0.34,
                abs(relativeElevation - wrackElevation)));
        albedo *= mix(
            vec3<f32>(1.0),
            vec3<f32>(0.39, 0.31, 0.22),
            wrack * 0.16);
    }
    albedo *= macroBreakup *
              coveDriftwoodContact(worldPosition.xz);
    roughness = mix(roughness, 0.18, wetResponse);
    let detailStrength = mix(0.82, 0.38, wetResponse);
    worldNormal = normalize(mix(
        geometryNormal, normalize(worldNormal), detailStrength));
    // Fine shore-parallel sand ripples live in the shading normal only. Their
    // phase bends slowly along the cove so the highlights do not become a
    // ruler-straight comb, and they naturally disappear above the run-up zone.
    let rippleMask =
        coveMask * weights.x *
        (1.0 - smoothstep(1.1, 3.1, relativeElevation));
    var rippleSlope = 0.0;
    if (rippleMask > 0.0) {
        let ripplePhase =
            coveInland * 2.15 +
            sin(coveAlongshore * 0.17) * 0.78;
        rippleSlope =
            (cos(ripplePhase) * 0.065 +
             cos(ripplePhase * 2.17 + 1.4) * 0.024) *
            rippleMask;
    }
    worldNormal = normalize(
        worldNormal -
        vec3<f32>(-0.2730, 0.0, 0.9620) * rippleSlope);
    // Low sediment mottling breaks the smoothed shelf without adding a new
    // ruler-straight carrier frequency to the seabed.
    let subtidalZone =
        coveMask * weights.x *
        smoothstep(-42.0, -31.0, relativeElevation) *
        (1.0 - smoothstep(-4.0, -2.5, relativeElevation));
    if (subtidalZone > 0.0) {
        let sedimentPattern =
            sin(coveInland * 0.73 +
                sin(coveAlongshore * 0.41) * 1.3) *
            sin(coveAlongshore * 0.57 -
                sin(coveInland * 0.29) * 1.1);
        albedo *= mix(
            vec3<f32>(1.0),
            vec3<f32>(0.94 + sedimentPattern * 0.075),
            subtidalZone * 0.48);
    }
    return TerrainSurface(
        max(albedo, vec3<f32>(0.0)),
        worldNormal,
        clamp(roughness, 0.08, 1.0),
        wetness);
}

fn terrainSpecular(
    normal : vec3<f32>, view : vec3<f32>, light : vec3<f32>,
    perceptualRoughness : f32, wetness : f32) -> vec3<f32> {
    let halfVector = view + light;
    let halfLength = length(halfVector);
    let halfway = select(normal, halfVector / halfLength, halfLength > 1.0e-5);
    let nDotV = max(dot(normal, view), 1.0e-4);
    let nDotL = max(dot(normal, light), 0.0);
    if (nDotL == 0.0) { return vec3<f32>(0.0); }
    let nDotH = max(dot(normal, halfway), 0.0);
    let vDotH = max(dot(view, halfway), 0.0);
    let alpha = max(
        perceptualRoughness * perceptualRoughness, 0.0025);
    let alphaSquared = alpha * alpha;
    let denominator =
        nDotH * nDotH * (alphaSquared - 1.0) + 1.0;
    let distribution = alphaSquared /
        max(3.14159265 * denominator * denominator, 1.0e-5);
    let visibilityV = 2.0 * nDotV /
        max(nDotV + sqrt(
            alphaSquared + (1.0 - alphaSquared) * nDotV * nDotV),
            1.0e-5);
    let visibilityL = 2.0 * nDotL /
        max(nDotL + sqrt(
            alphaSquared + (1.0 - alphaSquared) * nDotL * nDotL),
            1.0e-5);
    let fresnelBase = mix(0.04, 0.0204, wetness);
    let fresnel = fresnelBase +
        (1.0 - fresnelBase) * pow(1.0 - vDotH, 5.0);
    let brdf = distribution * visibilityV * visibilityL * fresnel /
        max(4.0 * nDotV * nDotL, 1.0e-5);
    return vec3<f32>(brdf * nDotL);
}

fn shadeCoveProp(
    hit : CovePropHit, rayDirection : vec3<f32>) -> vec3<f32> {
    let worldPosition =
        camera.cameraPos.xyz + rayDirection * hit.distance;
    var normal = normalize(hit.normal);
    let barkNormal = vec3<f32>(
        sin(worldPosition.z * 2.1 + hit.grainCoordinate * 1.7),
        sin(worldPosition.x * 1.8 - hit.grainCoordinate * 1.3),
        sin(worldPosition.y * 2.4 + hit.grainCoordinate * 1.1));
    normal = normalize(normal + barkNormal * 0.022);
    let light = normalize(camera.lightDirWS.xyz);
    let view = normalize(-rayDirection);
    let diffuse = max(dot(normal, light), 0.0);
    let halfway = normalize(light + view);
    let interiorMask =
        smoothstep(0.72, 0.95, hit.materialCue);
    let hullSkin =
        smoothstep(0.12, 0.26, hit.materialCue) *
        (1.0 - interiorMask);
    let specular =
        pow(max(dot(normal, halfway), 0.0), 54.0) *
        mix(0.045, 0.006, interiorMask);
    let grain =
        0.96 + 0.035 *
        sin(hit.grainCoordinate * 2.7 +
            sin(hit.grainCoordinate * 0.83) * 1.2);
    let weather =
        0.94 + 0.06 *
        periodicGradientNoise(
            worldPosition.xz * 0.23 +
            vec2<f32>(hit.grainCoordinate * 0.07, 9.3));
    let sideDarkening =
        mix(0.82, 1.0, smoothstep(-0.10, 0.72, normal.y));
    let dryWood =
        smoothstep(-204.0, -198.3, worldPosition.y);
    let saltAge =
        0.5 + 0.5 *
        periodicGradientNoise(
            worldPosition.xz * 0.085 +
            vec2<f32>(2.7, hit.grainCoordinate * 0.031));
    let dryWoodColor = mix(
        vec3<f32>(0.18, 0.095, 0.046),
        vec3<f32>(0.46, 0.36, 0.25),
        smoothstep(0.24, 0.78, saltAge));
    let woodColor = mix(
        vec3<f32>(0.075, 0.043, 0.025),
        dryWoodColor,
        dryWood);
    let plankSeam =
        pow(
            1.0 - abs(sin(
                worldPosition.y * 4.15 +
                hit.grainCoordinate * 0.013)),
            18.0) * hullSkin;
    let exteriorAlbedo =
        woodColor *
        grain * weather * sideDarkening;
    let interiorAlbedo =
        vec3<f32>(0.030, 0.018, 0.010) *
        mix(0.82, 1.08, weather);
    let albedo = mix(
        exteriorAlbedo * (1.0 - plankSeam * 0.18),
        interiorAlbedo, interiorMask);
    let ambient = max(camera.lightDirVS.w, 0.05);
    let lit =
        albedo *
        (ambient * ambientTint() + diffuse * sunRadiance()) +
        sunRadiance() * specular;
    if (camera.waterParams.y > 0.5 &&
        camera.waterMotion.z > 0.5 &&
        worldPosition.y <= camera.waterParams.x + 0.5) {
        let caustic = underwaterTerrainCaustic(
            worldPosition, normal, hit.distance);
        let submergedLit =
            lit * (1.0 + caustic * 0.48) +
            vec3<f32>(0.012, 0.030, 0.024) * caustic;
        return applyUnderwaterMedium(
            submergedLit,
            worldPosition - camera.cameraPos.xyz,
            hit.distance);
    }
    let fog = atmosphericFog(hit.distance);
    return mix(lit, oceanFogColor(), fog);
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
               (local * (local * 6.0 - vec2<f32>(15.0)) +
                vec2<f32>(10.0));
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

fn underwaterTerrainCaustic(
    worldPosition : vec3<f32>,
    geometryNormal : vec3<f32>,
    pathLength : f32) -> f32 {
    let depthBelowSurface =
        max(camera.waterParams.x - worldPosition.y, 0.0);
    let depthFade =
        smoothstep(0.25, 1.2, depthBelowSurface) *
        (1.0 - smoothstep(28.0, 52.0, depthBelowSurface));
    let distanceFade =
        1.0 - smoothstep(55.0, 145.0, pathLength);
    let receiver = smoothstep(0.22, 0.82, geometryNormal.y);
    if (depthFade == 0.0 || distanceFade == 0.0 || receiver == 0.0) {
        return 0.0;
    }
    let motion = vec2<f32>(
        camera.waterMotion.x * 0.021,
        -camera.waterMotion.x * 0.016);
    let first = textureSampleLevel(
        oceanFoamTex, oceanFoamSampler,
        worldPosition.xz / 0.72 + motion, 0.0).r;
    let second = textureSampleLevel(
        oceanFoamTex, oceanFoamSampler,
        worldPosition.xz / 1.13 -
        motion * 0.73 + vec2<f32>(0.37, 0.61), 0.0).r;
    let textureFocus = smoothstep(
        0.54, 0.79, first * 0.56 + second * 0.44);
    let breakup =
        smoothstep(
            0.28, 0.78,
            0.5 + 0.5 *
            sin(worldPosition.x * 1.37 +
                sin(worldPosition.z * 0.91 +
                    camera.waterMotion.x * 0.29)));
    let focused = textureFocus * mix(0.30, 1.0, breakup);
    return focused * depthFade * distanceFade * receiver * 0.90;
}

// Infinite-ocean fallback for rays beyond the finite heightfield. It reuses the
// same warped CC0 sand layer as the finite shoreline, so the fallback cannot
// reveal the old square procedural-material repeat.
fn proceduralSeabed(worldXZ : vec2<f32>, pathLength : f32) -> vec3<f32> {
    let rotated = vec2<f32>(worldXZ.x * 0.82 + worldXZ.y * 0.57,
                            worldXZ.y * 0.82 - worldXZ.x * 0.57);
    let materialLod = clamp(log2(max(pathLength / 180.0, 1.0)), 0.0, 9.0);
    let sandUv = terrainMaterialUv(worldXZ, TERRAIN_LAYER_SAND);
    let albedo = textureSampleLevel(
        terrainMaterialAlbedo, oceanFoamSampler,
        sandUv, TERRAIN_LAYER_SAND, materialLod).rgb *
        vec3<f32>(0.72, 0.79, 0.74);
    // Beyond the existing fade endpoint this sample contributes exactly zero.
    if (pathLength >= 360.0) { return albedo * 0.88; }
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

fn coastalFoamStrength(
    position : vec3<f32>,
    normal : vec3<f32>,
    waterDepth : f32,
    crestCompression : f32,
    shoreInfluence : f32,
    distanceToCamera : f32,
    dims : vec2<u32>) -> f32 {
    let crestEnergy = max(
        smoothstep(0.045, 0.20, crestCompression),
        smoothstep(0.018, 0.16, 1.0 - normal.y));
    // Above this depth all shoreline terms are exactly zero (including the
    // wreck-contact term in the legacy path). Preserve compressed open foam.
    if (waterDepth >= 2.35 && crestEnergy == 0.0) { return 0.0; }

    let worldPerPixel = distanceToCamera * 2.0 *
        max(camera.invProjParams.x / f32(max(dims.x, 1u)),
            camera.invProjParams.y / f32(max(dims.y, 1u)));
    let foamLod = log2(max(
        worldPerPixel * 1024.0 / oceanFoamSize(), 1.0));
    let drift = vec2<f32>(
        camera.waterMotion.x * 0.0017,
        -camera.waterMotion.x * 0.0011);
    let foamUv = position.xz / oceanFoamSize() + drift;
    let pattern = textureSampleLevel(
        oceanFoamTex, oceanFoamSampler, foamUv, foamLod).r;
    if (waterDepth >= 2.35) {
        let threshold = 1.0 - oceanFoamCoverage();
        let openCoverage = pattern *
            smoothstep(threshold, threshold + 0.15, pattern);
        return clamp(openCoverage * oceanFoamOpacity() * crestEnergy * 0.55,
                     0.0, 1.0);
    }
    let detail = textureSampleLevel(
        oceanFoamTex, oceanFoamSampler,
        foamUv * 2.37 + vec2<f32>(0.31, 0.67) - drift * 0.7,
        foamLod + 0.8).r;

    let depthWindow =
        smoothstep(0.07, 0.24, waterDepth) *
        (1.0 - smoothstep(0.48, 0.92, waterDepth));
    let proximity = max(
        clamp(shoreInfluence, 0.0, 1.0) * 0.52,
        1.0 - smoothstep(0.72, 1.75, waterDepth));
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

    // Surf wraps around the grounded hull's finite footprint. This remains a
    // broken, depth-gated contact response—not a painted shoreline stripe.
    let wreckOffset = position.xz - vec2<f32>(-650.0, 3471.0);
    let wreckInland =
        dot(wreckOffset, vec2<f32>(-0.2730, 0.9620));
    let wreckAlongshore =
        dot(wreckOffset, vec2<f32>(-0.9620, -0.2730));
    let wreckEllipse = length(vec2<f32>(
        wreckInland / 5.4, wreckAlongshore / 15.8));
    let wreckContactBand =
        smoothstep(0.72, 0.94, wreckEllipse) *
        (1.0 - smoothstep(1.02, 1.30, wreckEllipse));
    let wreckSeaward =
        1.0 - smoothstep(-0.4, 3.2, wreckInland);
    let wreckDepthWindow =
        smoothstep(0.035, 0.18, waterDepth) *
        (1.0 - smoothstep(1.10, 2.35, waterDepth));
    let wreckBreakup =
        mix(0.32, 1.0,
            smoothstep(0.42, 0.76, max(pattern, detail)));
    let wreckContactFoam =
        wreckContactBand * wreckSeaward * wreckDepthWindow *
        wreckBreakup * mix(0.42, 1.0, pulse) * 0.82;

    let threshold = 1.0 - oceanFoamCoverage();
    let openCoverage =
        pattern * smoothstep(
            threshold, threshold + 0.15, pattern);
    // Open-water foam needs real compression. A coverage-only floor turned
    // bright sky reflections into a continuous ice-like sheet.
    let openFoam = openCoverage * oceanFoamOpacity() *
                   crestEnergy * 0.55;
    let shoreOpacity =
        max(oceanFoamOpacity(), 0.68);
    return clamp(
        max(
            max(openFoam, wreckContactFoam),
            max(shoreBreaker, swashResidue) * shoreOpacity),
        0.0, 1.0);
}

// Refraction reconstructs the distorted bed lookup from terrain albedo,
// light visibility, and the packed vertical water depth.
fn shadeOcean(posWorld : vec3<f32>, posView : vec3<f32>,
                 view : vec3<f32>, normal : vec3<f32>, waterDepth : f32,
                 shadowVisibility : f32, crestCompression : f32,
                 shoreInfluence : f32,
                 sceneRefraction : vec3<f32>, sceneThickness : f32,
                 hasSceneRefraction : bool, sceneHasOpaque : bool,
                 dims : vec2<u32>) -> vec3<f32> {
    let distanceToCamera = max(length(posView), 1.0e-6);
    let light = normalize(camera.lightDirWS.xyz);
    let viewFacing = dot(normal, view);
    let fresnel = dielectricFresnel(viewFacing, oceanIor());

    let reflected = reflect(-view, normal);
    let reflectionRoughness = max(oceanMinimumRoughness(), 0.18) +
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
                      oceanSunIntensity() * 1.8;

    let incomingRay = -view;
    let refractedRay = refract(incomingRay, normal, 1.0 / oceanIor());
    let refractedTravel = waterDepth / max(-refractedRay.y, 0.12);
    let opticalCoverage = smoothstep(0.12, 12.0, waterDepth);
    let distortionCoverage = smoothstep(0.04, 0.80, waterDepth);
    var thickness = clamp(refractedTravel, 0.0, 500.0);
    var refracted : vec3<f32>;
    let proceduralFloor = waterDepth >= 499.0 && camera.waterMotion.z < 0.5;
    if (proceduralFloor) {
        let floorTravel = OCEAN_PROCEDURAL_SEABED_DEPTH /
                          max(-refractedRay.y, 0.12);
        let bedWorld = posWorld + refractedRay * floorTravel;
        refracted = proceduralSeabed(bedWorld.xz, floorTravel) *
                    (0.34 + 0.66 * max(light.y, 0.0));
        thickness = clamp(floorTravel, 0.0, 500.0);
    } else if (hasSceneRefraction && sceneHasOpaque) {
        refracted = sceneRefraction;
        thickness = clamp(sceneThickness, 0.0, 500.0);
    } else {
        var bedWorld = posWorld + refractedRay * refractedTravel;

        // Project a normal-offset point, then intersect that camera ray with the
        // packed bed plane. This retains magnification without an opaque
        // scene-color target.
        let distortionDistance = min(thickness, 80.0) *
                                 oceanDistortion() * distortionCoverage;
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
        refracted = proceduralSeabed(bedWorld.xz, refractedTravel) *
                        (0.25 + 0.75 * bedLight * shadowVisibility);
    }
    let transmittance = exp(-oceanAbsorption() * thickness);
    let clearRefracted = refracted * transmittance +
                         body * (vec3<f32>(1.0) - transmittance);
    // Fine suspended sand gives the breaker zone a restrained teal body.
    // Pure clear-water refraction made the bright bed read as a sheet of ice.
    let coastalTurbidity =
        smoothstep(0.62, 1.70, waterDepth) *
        (1.0 - smoothstep(6.5, 11.0, waterDepth)) * 0.08;
    let refractedWater =
        mix(clearRefracted, body, coastalTurbidity);
    let reflectedWater = environment +
        camera.lightingColor.rgb * sunSpecular +
        oceanScatterColor() * camera.lightingColor.rgb *
        forwardScatter * oceanSunIntensity();

    let foamStrength = coastalFoamStrength(
        posWorld, normal, waterDepth, crestCompression,
        shoreInfluence, distanceToCamera, dims) *
        smoothstep(0.04, 0.18, waterDepth);

    if (camera.waterMotion.z > 0.5) {
        let undersideNormal = -normal;
        var transmissionDirection =
            refract(-view, undersideNormal, oceanIor());
        let totalInternalReflection =
            dot(transmissionDirection, transmissionDirection) < 0.001;
        var underside : vec3<f32>;
        if (totalInternalReflection) {
            underside =
                oceanScatterColor() * 0.82 +
                proceduralSeabed(
                    posWorld.xz + reflected.xz * 18.0,
                    max(distanceToCamera, 1.0)) * 0.18;
        } else {
            let transmittedEnvironment = sampleWaterEnvironment(
                normalize(transmissionDirection), reflectionRoughness);
            var transmitted = transmittedEnvironment;
            if (sceneHasOpaque) {
                transmitted = mix(transmittedEnvironment, refracted, 0.75);
            }
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
        // At a distant grazing intersection, sub-pixel heightfield and water
        // coverage can alternate along one scanline. Converge both sides to
        // the same medium color before that mismatch becomes black dashes.
        let grazingGuard =
            (1.0 - smoothstep(
                0.012, 0.055, abs(dot(normal, view)))) *
            smoothstep(90.0, 280.0, distanceToCamera);
        underside = mix(
            underside, oceanScatterColor() * 1.04, grazingGuard);
        let cameraTransmittance =
            exp(-oceanAbsorption() * distanceToCamera * 1.35);
        return mix(oceanScatterColor(), underside,
                   cameraTransmittance);
    }

    // Foam suppresses Fresnel before its own color is composited.
    var color = mix(refractedWater, reflectedWater,
                    fresnel * opticalCoverage *
                    clamp(1.0 - foamStrength * 2.0, 0.0, 1.0));
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
    let propRay = normalize(
        posCenterWorld - camera.cameraPos.xyz);
    let coveProp = authoredCovePropHit(
        camera.cameraPos.xyz, propRay, depthCenter);
    if (coveProp.distance > 0.0) {
        return shadeCoveProp(coveProp, propRay);
    }

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
    var geometryNormal = normalize(
        (camera.invView * vec4<f32>(normal, 0.0)).xyz);
    let exactTerrainNormal = textureLoad(materialTex, pixel, 0).xyz;
    if (dot(exactTerrainNormal, exactTerrainNormal) > 0.5) {
        geometryNormal = normalize(exactTerrainNormal);
    }

    let terrainUv = terrainUV(posCenterWorld);
    let posXWorld = viewToWorld(camera.invView, posX);
    let posYWorld = viewToWorld(camera.invView, posY);
    let terrainUvX = terrainUV(posXWorld);
    let terrainUvY = terrainUV(posYWorld);
    let terrainUvDx = select(
        terrainUvX - terrainUv, terrainUv - terrainUvX, useNegativeX);
    let terrainUvDy = select(
        terrainUvY - terrainUv, terrainUv - terrainUvY, useNegativeY);
    let materialWorldX = select(
        posXWorld, posCenterWorld * 2.0 - posXWorld, useNegativeX);
    let materialWorldY = select(
        posYWorld, posCenterWorld * 2.0 - posYWorld, useNegativeY);
    let underwaterTerrain = camera.waterParams.y > 0.5 &&
        camera.waterMotion.z > 0.5 &&
        posCenterWorld.y <= camera.waterParams.x + 0.5;
    var surface = TerrainSurface(
        vec3<f32>(0.0), geometryNormal, 0.6, 0.0);
    if (camera.invProjParams.z > 0.5) {
        // Lego mode intentionally keeps the existing world-baked colors and
        // smooth plastic response instead of introducing natural detail.
        surface.albedo = srgbToLinear(textureSampleGrad(
            terrainTex, terrainSampler, terrainUv,
            terrainUvDx, terrainUvDy).rgb);
        surface.roughness = 0.2;
    } else {
        surface = sampleTerrainSurface(
            posCenterWorld, materialWorldX, materialWorldY,
            geometryNormal);
    }
    normal = worldNormalToView(surface.normal);
    let lightVisibility = textureSampleLevel(
        lightmapTex, terrainSampler, terrainUv, 0.0).x;
    let shadow = textureLoad(shadowTex, pixel, 0).x;
    let light = camera.lightDirVS.xyz;
    let diffuse = max(dot(normal, light), 0.0) * shadow;
    let ambient = max(camera.lightDirVS.w, 0.05);
    let view = normalize(-posCenterView);
    let specular = terrainSpecular(
        normal, view, light, surface.roughness, surface.wetness) *
        lightVisibility * shadow;
    let warmLight = vec3<f32>(1.10, 0.96, 0.84);
    let coolShadow = vec3<f32>(0.90, 0.96, 1.02);
    let grade = mix(coolShadow, warmLight,
        clamp(diffuse * lightVisibility + 0.35, 0.0, 1.0));
    let lit = surface.albedo *
        (diffuse * lightVisibility * sunRadiance() + ambient * ambientTint()) *
        grade + specular * sunRadiance();
    let distanceToCamera = length(posCenterView);
    if (underwaterTerrain) {
        // The settled-camera path owns review captures and normal play most of
        // the time. Keep its submerged receiver lighting identical to the
        // direct path instead of dropping caustics after the cache refresh.
        let caustic = underwaterTerrainCaustic(
            posCenterWorld, geometryNormal, distanceToCamera);
        let submergedLit = lit *
            (1.0 + caustic * 0.62) +
            vec3<f32>(0.018, 0.045, 0.035) * caustic;
        return applyUnderwaterMedium(
            submergedLit,
            posCenterWorld - camera.cameraPos.xyz,
            distanceToCamera);
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
    let propRay = normalize(
        posCWorld - camera.cameraPos.xyz);
    let coveProp = authoredCovePropHit(
        camera.cameraPos.xyz, propRay, depthCenter);
    if (coveProp.distance > 0.0) {
        let propColor = shadeCoveProp(coveProp, propRay);
        return vec4<f32>(
            presentColor(propColor, i.uv, dims), 1.0);
    }
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
                                       waterData.z, waterData.w,
                                       vec3<f32>(0.0), 0.0,
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
    var geometryNormal = normalize(
        (camera.invView * vec4<f32>(normal, 0.0)).xyz);
    let exactTerrainNormal = textureLoad(materialTex, pixelI, 0).xyz;
    if (dot(exactTerrainNormal, exactTerrainNormal) > 0.5) {
        geometryNormal = normalize(exactTerrainNormal);
    }

    // ─────────────────────────────────────────────────────────────────────────
    // Texture Sampling
    // ─────────────────────────────────────────────────────────────────────────
    let uvTerrain = terrainUV(posCWorld);
    let posXWorld = viewToWorld(camera.invView, posXView);
    let posYWorld = viewToWorld(camera.invView, posYView);
    let uvTerrainX = terrainUV(posXWorld);
    let uvTerrainY = terrainUV(posYWorld);
    let uvTerrainDx = select(
        uvTerrainX - uvTerrain, uvTerrain - uvTerrainX, useNegX);
    let uvTerrainDy = select(
        uvTerrainY - uvTerrain, uvTerrain - uvTerrainY, useNegY);
    let materialWorldX = select(
        posXWorld, posCWorld * 2.0 - posXWorld, useNegX);
    let materialWorldY = select(
        posYWorld, posCWorld * 2.0 - posYWorld, useNegY);
    var surface = TerrainSurface(
        vec3<f32>(0.0), geometryNormal, 0.6, 0.0);
    if (camera.invProjParams.z > 0.5) {
        surface.albedo = srgbToLinear(textureSampleGrad(
            terrainTex, terrainSampler, uvTerrain,
            uvTerrainDx, uvTerrainDy).rgb);
        surface.roughness = 0.2;
    } else {
        surface = sampleTerrainSurface(
            posCWorld, materialWorldX, materialWorldY, geometryNormal);
    }
    normal = worldNormalToView(surface.normal);
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
    
    // Energy-normalized GGX direct specular. Perceptual roughness comes from
    // the material mip chain and wet sand uses an air/water dielectric F0.
    let viewDir = normalize(-posCView);
    let specular = terrainSpecular(
        normal, viewDir, lightDir, surface.roughness, surface.wetness) *
        lightVisibility * packedShadow;
    
    // Combine lighting components
    let warmLight = vec3<f32>(1.10, 0.96, 0.84);
    let coolShadow = vec3<f32>(0.90, 0.96, 1.02);
    let grade = mix(coolShadow, warmLight, clamp(finalDiffuse * lightVisibility + 0.35, 0.0, 1.0));
    let litColor = surface.albedo *
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
            let caustic = underwaterTerrainCaustic(
                posCWorld, geometryNormal, dist);
            let submergedLit = litColor *
                (1.0 + caustic * 0.62) +
                vec3<f32>(0.018, 0.045, 0.035) * caustic;
            finalColor = applyUnderwaterMedium(
                submergedLit,
                posCWorld - camera.cameraPos.xyz,
                dist);
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
        waterShadow, waterData.z, waterData.w,
        sceneRefraction, sceneThickness,
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
        up + ((broadNormal - up) * 0.82 +
              (detailNormal - up) * detailWeight * 0.48) * strength,
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
                    waterWave.x = shoreInfluence;
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
        waterShadow, scene.wave.w, scene.wave.x,
        sceneRefraction, sceneThickness,
        true, refractionDepth > 0.0, dims);
    var presented = presentColor(waterColor, i.uv, dims);
    if (debug.mode != 0u) {
        presented = applyDebugVisualization(
            waterColor, scene.depth, waterNormal);
    }
    output.color = vec4<f32>(presented, 1.0);
    return output;
}
