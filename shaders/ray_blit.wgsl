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
@group(0) @binding(2) var terrainTex : texture_2d<f32>;
@group(0) @binding(3) var lightmapTex : texture_2d<f32>;
@group(0) @binding(4) var terrainSampler : sampler;
@group(0) @binding(5) var<uniform> debug : DebugUniforms;

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
fn sampleDepth(coords : vec2<i32>) -> f32 {
    let dims = textureDimensions(depthTex, 0);
    let clamped = clamp(coords, vec2<i32>(0, 0), vec2<i32>(i32(dims.x) - 1, i32(dims.y) - 1));
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

// ─────────────────────────────────────────────────────────────────────────────
// Fragment Shader
// ─────────────────────────────────────────────────────────────────────────────

@fragment
fn fs(i : VSOut) -> @location(0) vec4<f32> {
    // Get texture dimensions and compute pixel coordinates
    let dims = textureDimensions(depthTex, 0);
    let dimsF = vec2<f32>(f32(dims.x), f32(dims.y));
    let pixF = i.uv * dimsF;
    let pixelI = clamp(vec2<i32>(floor(pixF)), vec2<i32>(0, 0), 
                       vec2<i32>(dims) - vec2<i32>(1, 1));
    let ndcCenter = ndcFromPixel(pixelI, dimsF);
    // Sample RG32Float texture (R=Depth, G=Shadow)
    let depthSample = textureLoad(depthTex, clamp(pixelI, vec2<i32>(0, 0), vec2<i32>(i32(dims.x) - 1, i32(dims.y) - 1)), 0);
    let depthCenter = depthSample.x;
    let shadowFactor = depthSample.y;

    let invProjParams = camera.invProjParams.xy;
    
    // ─────────────────────────────────────────────────────────────────────────
    // Sky Rendering - Atmospheric Scattering
    // ─────────────────────────────────────────────────────────────────────────
    // If depth < 0, this pixel shows sky (no terrain intersection)
    if (depthCenter < 0.0) {
        // Check for raycast failure sentinel (-2.0)
        if (depthCenter < -1.5) {
            return vec4<f32>(1.0, 0.0, 0.5, 1.0); // Hot Pink
        }

        let viewDir = rayDirFromPixel(invProjParams, ndcCenter);
        let worldDir = normalize((camera.invView * vec4<f32>(viewDir, 0.0)).xyz);
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
        let horizonColor = vec3<f32>(0.8, 0.85, 0.95);
        let zenithColor = vec3<f32>(0.15, 0.35, 0.65);
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
        // Procedural Clouds (FBM noise)
        // ─────────────────────────────────────────────────────────────────────
        if (worldDir.y > 0.0) {
            // Project ray onto cloud plane at fixed height
            let cloudHeight = 800.0;
            let t = cloudHeight / max(worldDir.y, 0.001);
            let cloudPos = worldDir.xz * t * 0.0008; // Scale for cloud size
            
            // Animated cloud position (use fog density as time proxy, or static)
            let cloudOffset = vec2<f32>(camera.metrics.w * 1000.0, 0.0);
            let animatedPos = cloudPos + cloudOffset;
            
            // FBM noise for clouds
            var cloudNoise = 0.0;
            var amplitude = 0.5;
            var frequency = 1.0;
            var noisePos = animatedPos;
            
            for (var i = 0; i < 5; i++) {
                cloudNoise += amplitude * simplexNoise2D(noisePos * frequency);
                amplitude *= 0.5;
                frequency *= 2.0;
                noisePos = noisePos * 1.02 + vec2<f32>(0.1, 0.05); // Slight rotation
            }
            
            // Shape clouds
            let cloudDensity = smoothstep(0.1, 0.6, cloudNoise * 0.5 + 0.5);
            let cloudFade = smoothstep(0.0, 0.3, worldDir.y); // Fade near horizon
            let distanceFade = exp(-t * 0.0003); // Fade with distance
            
            // Cloud lighting (simple sun-facing)
            let cloudBrightness = mix(0.7, 1.0, max(0.0, sunDot * 0.5 + 0.5));
            let cloudColor = vec3<f32>(1.0, 0.98, 0.95) * cloudBrightness;
            
            // Blend clouds
            let cloudAlpha = cloudDensity * cloudFade * distanceFade * 0.8;
            skyColor = mix(skyColor, cloudColor, cloudAlpha);
        }
        
        // Tone mapping (simple Reinhard)
        skyColor = skyColor / (skyColor + vec3<f32>(1.0));
        
        // Gamma correction
        skyColor = pow(skyColor, vec3<f32>(1.0 / 2.2));
        
        return vec4<f32>(skyColor, 1.0);
    }
    
    // ─────────────────────────────────────────────────────────────────────────
    // Position Reconstruction
    // ─────────────────────────────────────────────────────────────────────────
    let posCView = viewPosFromDepth(invProjParams, ndcCenter, depthCenter);
    let posCWorld = viewToWorld(camera.invView, posCView);
    
    // ─────────────────────────────────────────────────────────────────────────
    // Screen-Space Normal Reconstruction
    // ─────────────────────────────────────────────────────────────────────────
    // Sample neighbor depths to reconstruct surface normal
    let offsetX = vec2<i32>(1, 0);
    let offsetY = vec2<i32>(0, 1);
    let depthNegX = sampleDepth(pixelI - offsetX);
    let depthPosX = sampleDepth(pixelI + offsetX);
    let depthNegY = sampleDepth(pixelI - offsetY);
    let depthPosY = sampleDepth(pixelI + offsetY);
    
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
    var normal = normalize(cross(dx, dy));

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
    let litColor = albedo * (finalDiffuse * lightVisibility + ambient) + specular;
    
    // ─────────────────────────────────────────────────────────────────────────
    // Fog
    // ─────────────────────────────────────────────────────────────────────────
    // Exponential fog capped at 0.7 to maintain visibility at distance
    let fogDensity = camera.metrics.w;
    let fogColor = vec3<f32>(0.6, 0.68, 0.76);  // Hardcoded fog color
    let dist = length(posCView);
    let fogFactor = clamp(1.0 - exp(-fogDensity * dist), 0.0, 0.7);
    let finalColor = mix(litColor, fogColor, fogFactor);
    
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

