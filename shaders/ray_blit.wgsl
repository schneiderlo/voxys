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
//   - Terrain normal map sampling
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
    invProjParams : vec4<f32>,    // Inverse projection params (.xy), lego flag (.z), time seconds (.w)
    lightDirVS : vec4<f32>,       // View-space light direction (.xyz)
    frustumPlanes : array<vec4<f32>, 6>, // Frustum planes
    lightDirWS : vec4<f32>,       // World-space light direction (.xyz)
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
@group(0) @binding(3) var terrainTex : texture_2d<f32>;
@group(0) @binding(4) var lightmapTex : texture_2d<f32>;
@group(0) @binding(5) var terrainSampler : sampler;
@group(0) @binding(6) var normalTex : texture_2d<f32>;
@group(0) @binding(7) var<uniform> debug : DebugUniforms;

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

fn sampleShadow(coords : vec2<i32>, maxCoord : vec2<i32>) -> f32 {
    let clamped = clamp(coords, vec2<i32>(0, 0), maxCoord);
    return textureLoad(shadowTex, clamped, 0).x;
}

fn filteredShadow(coords : vec2<i32>, maxCoord : vec2<i32>) -> f32 {
    let visibility = sampleShadow(coords, maxCoord);
    return max(visibility, 0.60);
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
/// Maps world XZ position to terrain texture space based on terrain dimensions.
fn terrainCoord(worldPos : vec3<f32>) -> vec2<f32> {
    let terrainSize = vec2<f32>(camera.terrainSize);
    let cellCounts = max(terrainSize - vec2<f32>(1.0, 1.0), vec2<f32>(1.0, 1.0));
    let cellScale = max(camera.metrics.y, 0.0001);
    let origin = 0.5 * cellCounts * cellScale;
    let coord = (worldPos.xz + origin) / cellScale;
    return coord / cellCounts;
}

/// Compute clamped terrain UV coordinates from world-space position.
fn terrainUV(worldPos : vec3<f32>) -> vec2<f32> {
    return clamp(terrainCoord(worldPos), vec2<f32>(0.0, 0.0), vec2<f32>(1.0, 1.0));
}

// ─────────────────────────────────────────────────────────────────────────────
// Noise Functions for Procedural Clouds
// ─────────────────────────────────────────────────────────────────────────────

/// Hash function for noise
fn hash2D(p: vec2<f32>) -> f32 {
    var p3 = fract(vec3<f32>(p.x, p.y, p.x) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

/// 2D gradient for simplex noise
fn grad2D(hash: f32) -> vec2<f32> {
    let angle = hash * 6.28318530718;
    return vec2<f32>(cos(angle), sin(angle));
}

/// 2D Simplex noise implementation
fn simplexNoise2D(p: vec2<f32>) -> f32 {
    // Skew factors for 2D simplex grid
    let F2 = 0.366025403784; // (sqrt(3) - 1) / 2
    let G2 = 0.211324865405; // (3 - sqrt(3)) / 6
    
    // Skew input space to determine simplex cell
    let s = (p.x + p.y) * F2;
    let i = floor(p.x + s);
    let j = floor(p.y + s);
    
    // Unskew to get first corner in (x,y) space
    let t = (i + j) * G2;
    let X0 = i - t;
    let Y0 = j - t;
    let x0 = p.x - X0;
    let y0 = p.y - Y0;
    
    // Determine which simplex we're in
    var i1: f32;
    var j1: f32;
    if (x0 > y0) {
        i1 = 1.0; j1 = 0.0;
    } else {
        i1 = 0.0; j1 = 1.0;
    }
    
    // Offsets for middle and last corners
    let x1 = x0 - i1 + G2;
    let y1 = y0 - j1 + G2;
    let x2 = x0 - 1.0 + 2.0 * G2;
    let y2 = y0 - 1.0 + 2.0 * G2;
    
    // Calculate contributions from three corners
    var n0 = 0.0;
    var n1 = 0.0;
    var n2 = 0.0;
    
    var t0 = 0.5 - x0*x0 - y0*y0;
    if (t0 >= 0.0) {
        t0 = t0 * t0;
        let g0 = grad2D(hash2D(vec2<f32>(i, j)));
        n0 = t0 * t0 * dot(g0, vec2<f32>(x0, y0));
    }
    
    var t1 = 0.5 - x1*x1 - y1*y1;
    if (t1 >= 0.0) {
        t1 = t1 * t1;
        let g1 = grad2D(hash2D(vec2<f32>(i + i1, j + j1)));
        n1 = t1 * t1 * dot(g1, vec2<f32>(x1, y1));
    }
    
    var t2 = 0.5 - x2*x2 - y2*y2;
    if (t2 >= 0.0) {
        t2 = t2 * t2;
        let g2 = grad2D(hash2D(vec2<f32>(i + 1.0, j + 1.0)));
        n2 = t2 * t2 * dot(g2, vec2<f32>(x2, y2));
    }
    
    // Scale to [-1, 1] range
    return 70.0 * (n0 + n1 + n2);
}

fn valueNoise2D(p: vec2<f32>) -> f32 {
    let cell = floor(p);
    let f = fract(p);
    let u = f * f * (3.0 - 2.0 * f);
    let a = hash2D(cell);
    let b = hash2D(cell + vec2<f32>(1.0, 0.0));
    let c = hash2D(cell + vec2<f32>(0.0, 1.0));
    let d = hash2D(cell + vec2<f32>(1.0, 1.0));
    return mix(mix(a, b, u.x), mix(c, d, u.x), u.y);
}

fn cloudField(p: vec2<f32>) -> f32 {
    let broad = valueNoise2D(p * 0.72);
    let body = valueNoise2D(p * 1.37 + vec2<f32>(11.7, -4.3));
    let lace = valueNoise2D(p * 2.83 + vec2<f32>(-19.1, 8.6));
    let erosion = valueNoise2D(p * 5.20 + vec2<f32>(3.4, 25.2));
    let field = broad * 0.50 + body * 0.33 + lace * 0.17;
    return clamp(field - max(erosion - 0.68, 0.0) * 0.42, 0.0, 1.0);
}

fn cloudCoverageAt(cloudPos: vec2<f32>, time: f32) -> f32 {
    let wind = vec2<f32>(time * 0.010, -time * 0.004);
    let p = cloudPos + wind;
    let banks = smoothstep(0.50, 0.76, cloudField(p));
    let wisps = smoothstep(0.62, 0.86, valueNoise2D(p * 4.30 + vec2<f32>(time * 0.018, 0.0)));
    return clamp(banks * 0.88 + wisps * 0.12, 0.0, 1.0);
}

fn cloudShadowAtWorld(worldPos: vec3<f32>, time: f32) -> f32 {
    let cloudPos = worldPos.xz * 0.00042;
    let cover = cloudCoverageAt(cloudPos, time);
    return mix(1.0, 0.78, cover);
}

fn waterMaskFromAlbedo(color : vec3<f32>) -> f32 {
    return smoothstep(0.04, 0.22, color.b - max(color.r, color.g) * 0.72);
}

fn terrainDetailColor(worldPos : vec3<f32>,
                      normal : vec3<f32>,
                      color : vec3<f32>,
                      waterMask : f32) -> vec3<f32> {
    let macroNoise = valueNoise2D(worldPos.xz * 0.018 + vec2<f32>(7.0, -13.0));
    let medium = valueNoise2D(worldPos.xz * 0.090 + vec2<f32>(31.0, 5.0));
    let grain = hash2D(floor(worldPos.xz * 0.85 + vec2<f32>(19.0, -23.0)));
    let dryMask = 1.0 - waterMask;
    let slopeMask = smoothstep(0.30, 0.82, 1.0 - clamp(normal.y, 0.0, 1.0)) * dryMask;
    let greenMask = smoothstep(0.04, 0.22, color.g - max(color.r, color.b) * 0.72) * dryMask;

    var detailed = color * (0.955 + macroNoise * 0.060 + medium * 0.030 + grain * 0.018);
    detailed = mix(detailed, detailed * vec3<f32>(0.86, 0.87, 0.84), slopeMask * 0.20);
    detailed = mix(detailed, detailed * vec3<f32>(0.84, 1.06, 0.82), greenMask * (0.04 + 0.08 * macroNoise));
    return clamp(detailed, vec3<f32>(0.0), vec3<f32>(1.0));
}

struct WaterSpectrum {
    normal : vec3<f32>,
    crest : f32,
    whitecap : f32,
    detail : f32,
};

fn waterSpectrum(waterXZ : vec2<f32>, time : f32, farFlatten : f32) -> WaterSpectrum {
    let dirA = vec2<f32>(0.940, 0.341);
    let dirB = vec2<f32>(-0.270, 0.963);
    let dirC = vec2<f32>(0.763, -0.647);

    let waveNumberA = 0.018480;
    let waveNumberB = 0.041888;
    let waveNumberC = 0.089760;

    let amplitudeA = 3.60;
    let amplitudeB = 1.45;
    let amplitudeC = 0.56;

    let steepnessA = 0.74;
    let steepnessB = 0.56;
    let steepnessC = 0.36;

    let phaseA = dot(waterXZ, dirA) * waveNumberA - time * 0.21 + 0.40;
    let phaseB = dot(waterXZ, dirB) * waveNumberB - time * 0.35 + 1.70;
    let phaseC = dot(waterXZ, dirC) * waveNumberC - time * 0.61 + 3.10;

    let waveA = sin(phaseA);
    let waveB = sin(phaseB);
    let waveC = sin(phaseC);
    let chopA = cos(phaseA);
    let chopB = cos(phaseB);
    let chopC = cos(phaseC);

    let farDamp = 1.0 - farFlatten * 0.70;
    let waveAmplitudeA = waveNumberA * amplitudeA;
    let waveAmplitudeB = waveNumberB * amplitudeB;
    let waveAmplitudeC = waveNumberC * amplitudeC;
    let detail = clamp(0.50 + waveB * 0.24 + chopC * 0.18 + waveA * 0.08, 0.0, 1.0);

    let slope = (dirA * chopA * waveAmplitudeA +
                 dirB * chopB * waveAmplitudeB +
                 dirC * chopC * waveAmplitudeC +
                 vec2<f32>(detail - 0.5, waveC * 0.5) * 0.014) * farDamp;

    let gerstnerFold = steepnessA * waveAmplitudeA * waveA +
                       steepnessB * waveAmplitudeB * waveB +
                       steepnessC * waveAmplitudeC * waveC;
    let vertical = max(1.0 - gerstnerFold * farDamp, 0.24);
    let heightSignal = (waveA * amplitudeA +
                        waveB * amplitudeB +
                        waveC * amplitudeC) /
                       (amplitudeA + amplitudeB + amplitudeC);
    let foldSignal = clamp(1.0 - vertical, 0.0, 1.0) * 1.85 +
                     abs(chopC) * 0.12 +
                     detail * 0.15;
    let crestSignal = heightSignal * 0.62 + foldSignal * 0.54 + detail * 0.16;

    var spectrum : WaterSpectrum;
    spectrum.normal = normalize(vec3<f32>(-slope.x, vertical, -slope.y));
    spectrum.crest = smoothstep(0.42, 0.84, crestSignal) * farDamp;
    spectrum.whitecap = smoothstep(1.02, 1.36, crestSignal + foldSignal * 0.34) * farDamp;
    spectrum.detail = detail;
    return spectrum;
}

fn waterWaveNormal(waterXZ : vec2<f32>, time : f32) -> vec3<f32> {
    return waterSpectrum(waterXZ, time, 0.0).normal;
}

fn waterCrest(waterXZ : vec2<f32>, time : f32) -> f32 {
    return waterSpectrum(waterXZ, time, 0.0).crest;
}

fn waterCaustics(waterXZ : vec2<f32>, waterDepth : f32, time : f32) -> f32 {
    let p = waterXZ * 0.062;
    let causticA = sin(p.x + p.y * 1.73 + time * 0.68);
    let causticB = sin(p.x * -1.47 + p.y * 0.82 - time * 0.51);
    let lattice = smoothstep(0.46, 0.96, causticA * causticB * 0.50 + 0.50);
    let depthFade = 1.0 - smoothstep(1.0, 22.0, waterDepth);
    return lattice * depthFade;
}

fn waterSurfaceColor(waterPos : vec3<f32>,
                     terrainColor : vec3<f32>,
                     terrainDistance : f32,
                     waterDistance : f32,
                     planeMask : f32,
                     time : f32) -> vec3<f32> {
    let waterDepth = max(terrainDistance - waterDistance, 0.0);
    let shallow = 1.0 - smoothstep(2.5, 24.0, waterDepth);
    let nearShore = 1.0 - smoothstep(0.8, 9.0, waterDepth);
    let deepFade = smoothstep(14.0, 70.0, waterDepth);
    let farFlatten = smoothstep(520.0, 2400.0, waterDistance);
    let wave = waterSpectrum(waterPos.xz, time, farFlatten);
    let rippleNoise = wave.detail;
    let waterNormal = normalize(mix(wave.normal,
                                   vec3<f32>(0.0, 1.0, 0.0),
                                   farFlatten * 0.58));

    let lightDir = normalize(camera.lightDirWS.xyz);
    let viewDir = normalize(camera.cameraPos.xyz - waterPos);
    let halfDir = normalize(lightDir + viewDir);
    let cloudShadow = 0.96 + farFlatten * 0.04;
    let diffuse = (0.60 + 0.38 * max(dot(waterNormal, lightDir), 0.0)) * mix(0.88, 1.0, cloudShadow);
    let glintPower = mix(68.0, 164.0, deepFade);
    let glint = pow(max(dot(waterNormal, halfDir), 0.0), glintPower) *
                mix(0.22, 0.54, deepFade) * cloudShadow * (1.0 - farFlatten * 0.35);
    let fresnel = 0.035 + 0.965 * pow(1.0 - clamp(dot(waterNormal, viewDir), 0.0, 1.0), 5.0);
    let caustics = waterCaustics(waterPos.xz, waterDepth, time);

    let deepColor = vec3<f32>(0.035, 0.180, 0.340);
    let midColor = vec3<f32>(0.070, 0.330, 0.460);
    let shallowColor = vec3<f32>(0.180, 0.540, 0.620);
    let absorption = 1.0 / (vec3<f32>(1.0) + waterDepth * vec3<f32>(0.085, 0.040, 0.024));
    let absorbedFloor = terrainColor * absorption;
    let transmission = mix(absorbedFloor * vec3<f32>(0.70, 0.92, 0.90), shallowColor, 0.62);
    var waterColor = mix(mix(transmission, midColor, 1.0 - shallow), deepColor, deepFade);
    waterColor *= diffuse + 0.25;

    let reflectionDir = reflect(-viewDir, waterNormal);
    let reflectionLift = clamp(reflectionDir.y * 0.5 + 0.5, 0.0, 1.0);
    let skyReflect = mix(vec3<f32>(0.62, 0.78, 0.90),
                         vec3<f32>(0.20, 0.46, 0.70),
                         reflectionLift * (1.22 - 0.22 * reflectionLift));
    let reflectionColor = skyReflect;

    waterColor = mix(waterColor, reflectionColor, fresnel * mix(0.32, 0.58, deepFade) * (1.0 - farFlatten * 0.32));
    let windStreakBase = 0.5 + 0.5 * sin(dot(waterPos.xz, vec2<f32>(0.94, 0.34)) * 0.18 +
                                         waterDistance * 0.034 +
                                         time * 0.62);
    let windStreakBreakup = 0.58 + wave.detail * 0.42;
    let windStreak = smoothstep(0.76, 0.99, windStreakBase * windStreakBreakup) *
                     (1.0 - farFlatten * 0.84);
    waterColor += vec3<f32>(1.00, 0.93, 0.78) * glint;
    waterColor += vec3<f32>(0.016, 0.032, 0.046) * windStreak;
    waterColor += vec3<f32>(0.20, 0.32, 0.24) * caustics * shallow * cloudShadow;
    waterColor *= 0.995 + (rippleNoise - 0.5) * (0.095 * (1.0 - farFlatten * 0.75));

    let shoreFoam = smoothstep(0.25, 0.90, planeMask) *
                    nearShore *
                    (0.58 + 0.42 * wave.detail);
    let ambientFoam = smoothstep(0.98, 1.0, wave.detail + wave.crest * 0.08) *
                      deepFade *
                      (1.0 - farFlatten * 0.90);
    let whitecaps = wave.whitecap * mix(nearShore * 0.24, deepFade * 0.035, smoothstep(18.0, 80.0, waterDepth)) *
                    (1.0 - farFlatten * 0.72);
    let foamMask = clamp(shoreFoam * 0.58 + whitecaps + ambientFoam * 0.05, 0.0, 0.62);
    let foamColor = mix(vec3<f32>(0.78, 0.90, 0.86), vec3<f32>(0.95, 0.98, 0.92), cloudShadow);
    waterColor = mix(waterColor, foamColor, foamMask);

    let distanceHaze = farFlatten * 0.34;
    waterColor = mix(waterColor, vec3<f32>(0.40, 0.60, 0.72), distanceHaze);

    let opacity = smoothstep(0.20, 0.72, planeMask) *
                  mix(0.42, 0.94, smoothstep(5.0, 38.0, waterDepth));
    return mix(terrainColor, waterColor, opacity);
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
    let shadowFactor = filteredShadow(pixelI, maxCoord);

    let invProjParams = camera.invProjParams.xy;
    let viewRay = rayDirFromPixel(invProjParams, ndcCenter);
    let worldRay = normalize((camera.invView * vec4<f32>(viewRay, 0.0)).xyz);
    
    // ─────────────────────────────────────────────────────────────────────────
    // Sky Rendering - Atmospheric Scattering
    // ─────────────────────────────────────────────────────────────────────────
    // If depth < 0, this pixel shows sky (no terrain intersection)
    if (depthCenter < 0.0) {
        // Check for raycast failure sentinel (-2.0)
        if (depthCenter < -1.5) {
            return vec4<f32>(1.0, 0.0, 0.5, 1.0); // Hot Pink
        }

        let worldDir = worldRay;
        let sunDir = normalize(camera.lightDirWS.xyz);
        
        // ─────────────────────────────────────────────────────────────────────
        // Atmospheric Scattering (simplified Rayleigh + Mie)
        // ─────────────────────────────────────────────────────────────────────
        let sunDot = dot(worldDir, sunDir);
        
        // Rayleigh scattering - stronger for shorter wavelengths (blue)
        // Phase function: (3/16π)(1 + cos²θ)
        let rayleighPhase = 0.75 * (1.0 + sunDot * sunDot);
        let rayleighCoeff = vec3<f32>(0.0058, 0.0135, 0.0331); // λ^-4 approximation
        
        // Mie scattering - forward scattering (haze around sun)
        // Henyey-Greenstein phase function approximation
        let g = 0.76; // Asymmetry factor (forward scattering)
        let g2 = g * g;
        let miePhase = (1.0 - g2) / pow(1.0 + g2 - 2.0 * g * sunDot, 1.5) * 0.25;
        let mieCoeff = vec3<f32>(0.004, 0.004, 0.004);
        
        // Optical depth increases near horizon (longer path through atmosphere)
        let zenithAngle = max(worldDir.y, 0.001);
        let opticalDepth = 1.0 / (zenithAngle + 0.15 * pow(93.885 - degrees(acos(zenithAngle)), -1.253));
        
        // Combine scattering
        let rayleigh = rayleighCoeff * rayleighPhase * opticalDepth;
        let mie = mieCoeff * miePhase * opticalDepth;
        
        // Sky color from scattering
        let sunIntensity = 22.0;
        let skyScatter = (rayleigh + mie) * sunIntensity;
        
        // Base sky gradient (zenith to horizon)
        let horizonColor = vec3<f32>(0.62, 0.76, 0.92);
        let zenithColor = vec3<f32>(0.08, 0.28, 0.58);
        let heightGradient = pow(max(worldDir.y, 0.0), 0.4);
        let baseSky = mix(horizonColor, zenithColor, heightGradient);
        
        // Combine base sky with scattering
        var skyColor = baseSky + skyScatter;
        
        // ─────────────────────────────────────────────────────────────────────
        // Sun Disc with Bloom
        // ─────────────────────────────────────────────────────────────────────
        let sunAngularRadius = 0.0087; // ~0.5 degrees in radians
        let sunAngle = acos(clamp(sunDot, -1.0, 1.0));
        
        // Sharp sun disc
        let sunDisc = smoothstep(sunAngularRadius * 1.2, sunAngularRadius * 0.8, sunAngle);
        let sunColor = vec3<f32>(1.0, 0.95, 0.85) * 5.0;
        
        // Multi-layer bloom around sun
        let bloom1 = exp(-sunAngle * 8.0) * 0.5;   // Tight bloom
        let bloom2 = exp(-sunAngle * 3.0) * 0.25;  // Medium bloom
        let bloom3 = exp(-sunAngle * 1.0) * 0.1;   // Wide glow
        let bloomColor = vec3<f32>(1.0, 0.9, 0.7);
        
        skyColor += sunColor * sunDisc;
        skyColor += bloomColor * (bloom1 + bloom2 + bloom3);
        
        // ─────────────────────────────────────────────────────────────────────
        // Procedural Clouds
        // ─────────────────────────────────────────────────────────────────────
        if (worldDir.y > 0.0) {
            let cloudHeight = 800.0;
            let t = cloudHeight / max(worldDir.y, 0.001);
            let cloudPos = worldDir.xz * t * 0.00072;
            let cloudDensity = cloudCoverageAt(cloudPos, camera.invProjParams.w);
            let cloudFade = smoothstep(0.02, 0.26, worldDir.y) *
                            (1.0 - smoothstep(0.86, 1.0, worldDir.y) * 0.18);
            let distanceFade = exp(-t * 0.00024);

            let sunEdge = pow(max(sunDot, 0.0), 6.0);
            let cloudBrightness = mix(0.64, 1.08, max(0.0, sunDot * 0.5 + 0.5));
            let cloudBase = vec3<f32>(0.78, 0.82, 0.86);
            let cloudLit = vec3<f32>(1.0, 0.96, 0.88) * cloudBrightness +
                           vec3<f32>(0.18, 0.14, 0.08) * sunEdge;
            let cloudColor = mix(cloudBase, cloudLit, 0.72);

            let cloudAlpha = cloudDensity * cloudFade * distanceFade * 0.74;
            skyColor = mix(skyColor, cloudColor, cloudAlpha);
        }
        
        // Tone mapping (simple Reinhard)
        skyColor = skyColor / (skyColor + vec3<f32>(1.0));
        
        // Gamma correction
        skyColor = pow(skyColor, vec3<f32>(1.0 / 2.2));

        // Sky pixels can still see the sea-level plane beyond the heightfield hit
        // distance. Composite ocean here so distant water keeps a flat horizon.
        let seaLevel = 0.0;
        if (abs(worldRay.y) > 0.0001) {
            let waterDistance = (seaLevel - camera.cameraPos.y) / worldRay.y;
            if (waterDistance > 0.0) {
                let waterPos = camera.cameraPos.xyz + worldRay * waterDistance;
                let waterUv = terrainCoord(waterPos);
                if (waterUv.x >= 0.0 && waterUv.x <= 1.0 && waterUv.y >= 0.0 && waterUv.y <= 1.0) {
                    let waterAlbedo = textureSampleLevel(terrainTex, terrainSampler, waterUv, 0.0).xyz;
                    let planeMask = waterMaskFromAlbedo(waterAlbedo);
                    let oceanMask = smoothstep(0.18, 0.55, planeMask);
                    if (oceanMask > 0.01) {
                        let deepOceanDistance = waterDistance + mix(42.0, 140.0, planeMask);
                        let skyWater = waterSurfaceColor(waterPos, skyColor, deepOceanDistance, waterDistance, 1.0, camera.invProjParams.w);
                        return vec4<f32>(mix(skyColor, skyWater, oceanMask), 1.0);
                    }
                }
            }
        }
        
        return vec4<f32>(skyColor, 1.0);
    }
    
    // ─────────────────────────────────────────────────────────────────────────
    // Position Reconstruction
    // ─────────────────────────────────────────────────────────────────────────
    let posCView = viewPosFromDepth(invProjParams, ndcCenter, depthCenter);
    let posCWorld = viewToWorld(camera.invView, posCView);
    
    // ─────────────────────────────────────────────────────────────────────────
    // Texture Sampling
    // ─────────────────────────────────────────────────────────────────────────
    let uvTerrain = terrainUV(posCWorld);
    let normal = normalize(textureSampleLevel(normalTex, terrainSampler, uvTerrain, 0.0).xyz * 2.0 - vec3<f32>(1.0, 1.0, 1.0));
    let rawAlbedo = textureSampleLevel(terrainTex, terrainSampler, uvTerrain, 0.0).xyz;
    let waterMask = waterMaskFromAlbedo(rawAlbedo);
    let albedo = terrainDetailColor(posCWorld, normal, rawAlbedo, waterMask);
    let lightVisibility = textureSampleLevel(lightmapTex, terrainSampler, uvTerrain, 0.0).x;
    
    // ─────────────────────────────────────────────────────────────────────────
    // Lighting
    // ─────────────────────────────────────────────────────────────────────────
    let lightDir = normalize(camera.lightDirWS.xyz);
    
    // Diffuse (Lambertian)
    let lambert = max(dot(normal, lightDir), 0.0);
    let diffuse = lambert * 0.76 + 0.24;
    
    // Apply Shadow from Ray Tracing
    let terrainCloudShadow = cloudShadowAtWorld(posCWorld, camera.invProjParams.w);
    let finalDiffuse = diffuse * shadowFactor * terrainCloudShadow;

    // Fixed ambient intensity
    let ambient = max(camera.lightDirVS.w, 0.05);
    let ambientCloud = ambient * mix(0.90, 1.0, terrainCloudShadow);
    
    // Specular (roughness-based Blinn-Phong)
    let viewDir = normalize(camera.cameraPos.xyz - posCWorld);
    let halfVec = normalize(lightDir + viewDir);

    var roughness = mix(0.72, 0.18, waterMask);
    if (camera.invProjParams.z > 0.5) {
        roughness = 0.2; // Shiny plastic for Lego
    }

    let specPower = max((1.0 - roughness) * 160.0, 8.0);  // ~64 for roughness 0.6
    let specStrength = mix(0.04, 0.25, 1.0 - roughness);  // ~0.124 for roughness 0.6
    let specularTerm = pow(max(dot(normal, halfVec), 0.0), specPower);
    // Apply shadow to specular as well
    let specular = specStrength * specularTerm * lightVisibility * shadowFactor * terrainCloudShadow;
    
    // Combine lighting components
    let warmSunTint = vec3<f32>(1.12, 0.99, 0.82);
    let coolAmbientTint = vec3<f32>(0.68, 0.78, 0.94);
    let litColor = albedo * (finalDiffuse * lightVisibility * warmSunTint +
                             ambientCloud * coolAmbientTint) +
                   specular * warmSunTint;
    var shadedColor = litColor;
    var fogDistance = length(posCView);

    // Minecraft fills terrain below sea level with water. The heightfield itself
    // remains the ocean floor, so composite a sea-level plane wherever the baked
    // biome texture says the underlying surface is water.
    let seaLevel = 0.0;
    if (abs(worldRay.y) > 0.0001) {
        let waterDistance = (seaLevel - camera.cameraPos.y) / worldRay.y;
        if (waterDistance > 0.0 && waterDistance < depthCenter) {
            let waterPos = camera.cameraPos.xyz + worldRay * waterDistance;
            let waterUv = terrainCoord(waterPos);
            if (waterUv.x >= 0.0 && waterUv.x <= 1.0 && waterUv.y >= 0.0 && waterUv.y <= 1.0) {
                var planeMask = 0.0;
                if (waterMask > 0.18 && posCWorld.y <= seaLevel + 1.0) {
                    planeMask = waterMask;
                } else {
                    let waterAlbedo = textureSampleLevel(terrainTex, terrainSampler, waterUv, 0.0).xyz;
                    planeMask = waterMaskFromAlbedo(waterAlbedo);
                }
                if (planeMask > 0.18) {
                    shadedColor = waterSurfaceColor(waterPos, shadedColor, depthCenter, waterDistance, planeMask, camera.invProjParams.w);
                    fogDistance = waterDistance;
                }
            }
        }
    }
    
    // ─────────────────────────────────────────────────────────────────────────
    // Fog
    // ─────────────────────────────────────────────────────────────────────────
    // Exponential fog capped at 0.7 to maintain visibility at distance
    let fogDensity = camera.metrics.w;
    let fogColor = vec3<f32>(0.58, 0.70, 0.82);  // Hardcoded fog color
    let fogFactor = clamp(1.0 - exp(-fogDensity * fogDistance), 0.0, 0.7);
    let finalColor = mix(shadedColor, fogColor, fogFactor);
    
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
