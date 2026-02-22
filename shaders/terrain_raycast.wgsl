// ═══════════════════════════════════════════════════════════════════════════════
// terrain_raycast.wgsl - Compute Ray-Caster Shader
// ═══════════════════════════════════════════════════════════════════════════════
// Performs hierarchical DDA ray-casting against the heightfield mip pyramid,
// outputting linear ray distance per pixel.
// Features:
//   - Ray generation from pixel coordinates using inverse view-projection
//   - AABB intersection for early ray clipping
//   - Hierarchical DDA traversal with mip level transitions
//   - Distance-based LOD termination
//   - Max-height mip pyramid for efficient empty-space skipping
//   - Shadow ray traversal
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
    invProjParams : vec4<f32>,    // Inverse projection params (.xy used) - .z is Lego Mode
    lightDirVS : vec4<f32>,       // View-space light direction (.xyz)
    frustumPlanes : array<vec4<f32>, 6>, // Frustum planes
    lightDirWS : vec4<f32>,       // World-space light direction (.xyz)
};

// ─────────────────────────────────────────────────────────────────────────────
// Bindings
// ─────────────────────────────────────────────────────────────────────────────

@group(0) @binding(0) var<uniform> camera : CameraUniforms;
@group(0) @binding(1) var heightTex : texture_2d<u32>;
@group(0) @binding(2) var outDepth : texture_storage_2d<rg32float, write>;

// ─────────────────────────────────────────────────────────────────────────────
// Coordinate Space Conversion
// ─────────────────────────────────────────────────────────────────────────────
// The ray-caster operates in heightmap coordinate space for Y comparisons
// (0-65535 range) to avoid per-sample normalization.

/// Convert world-space height to heightmap coordinate space
fn toHeightmapCoordinate(height : f32) -> f32 {
    let normalized = (height / camera.metrics.x) * 0.5 + 0.5;
    return normalized * 65535.0;
}

/// Convert world-space height delta to heightmap scale
fn toHeightmapScale(height : f32) -> f32 {
    let normalized = (height / camera.metrics.x) * 0.5;
    return normalized * 65535.0;
}

// ─────────────────────────────────────────────────────────────────────────────
// AABB Intersection
// ─────────────────────────────────────────────────────────────────────────────

/// Ray-AABB intersection returning (tMin, tMax)
/// Returns (tMin, tMax) where tMin > tMax indicates no intersection
fn intersectAabb(origin : vec3<f32>, dir : vec3<f32>, 
                 bmin : vec3<f32>, bmax : vec3<f32>) -> vec2<f32> {
    // Avoid division by zero for axis-aligned rays by adding a tiny epsilon
    let invDir = 1.0 / (dir + sign(dir) * 1e-20 + vec3<f32>(1e-20)); 

    let t0 = (bmin - origin) * invDir;
    let t1 = (bmax - origin) * invDir;
    let tMin = max(max(min(t0.x, t1.x), min(t0.y, t1.y)), min(t0.z, t1.z));
    let tMax = min(min(max(t0.x, t1.x), max(t0.y, t1.y)), max(t0.z, t1.z));
    return vec2<f32>(tMin, tMax);
}

// ─────────────────────────────────────────────────────────────────────────────
// Ray Generation
// ─────────────────────────────────────────────────────────────────────────────

/// Generate ray direction from pixel coordinates using inverse view-projection
fn rayDirFromPixel(invVP : mat4x4<f32>, pixel : vec2<u32>, 
                   dims : vec2<u32>, origin : vec3<f32>) -> vec3<f32> {
    let dimf = vec2<f32>(f32(dims.x), f32(dims.y));
    let pixelF = vec2<f32>(f32(pixel.x), f32(pixel.y));
    let uv = (pixelF + vec2<f32>(0.5, 0.5)) / dimf;
    let ndc = vec2<f32>(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0);
    let near4 = invVP * vec4<f32>(ndc, 0.0, 1.0);
    let far4 = invVP * vec4<f32>(ndc, 1.0, 1.0);
    let nearPos = near4.xyz / near4.w;
    let farPos = far4.xyz / far4.w;
    return normalize(farPos - origin);
}

// ─────────────────────────────────────────────────────────────────────────────
// Lego Logic
// ─────────────────────────────────────────────────────────────────────────────

fn intersectStud(origin: vec3<f32>, dir: vec3<f32>,
                 studBase: vec3<f32>, studH: f32, studR: f32) -> f32 {
    // 1. Intersect with cylinder cap (top) at y = studBase.y + studH
    // Avoid divide by zero for horizontal rays
    var tHit = 1e30;
    if (abs(dir.y) > 1e-6) {
        let tCap = (studBase.y + studH - origin.y) / dir.y;
        if (tCap > 1e-3) {
            let pCap = origin + dir * tCap;
            let dx = pCap.x - studBase.x;
            let dz = pCap.z - studBase.z;
            if (dx*dx + dz*dz <= studR*studR) {
                tHit = tCap;
            }
        }
    }

    // 2. Intersect with cylinder wall
    // (O.x + D.x*t - C.x)^2 + (O.z + D.z*t - C.z)^2 = R^2
    let ocX = origin.x - studBase.x;
    let ocZ = origin.z - studBase.z;
    let A = dir.x*dir.x + dir.z*dir.z;

    // Safety check for vertical rays
    if (A > 1e-6) {
        let B = 2.0 * (ocX*dir.x + ocZ*dir.z);
        let C = ocX*ocX + ocZ*ocZ - studR*studR;
        let disc = B*B - 4.0*A*C;

        if (disc >= 0.0) {
            let sqrtDisc = sqrt(disc);
            let t1 = (-B - sqrtDisc) / (2.0*A);
            let t2 = (-B + sqrtDisc) / (2.0*A);

            var tCyl = -1.0;
            if (t1 > 1e-3) { tCyl = t1; }
            else if (t2 > 1e-3 && (tCyl < 0.0 || t2 < tCyl)) { tCyl = t2; }

            if (tCyl > 1e-3) {
                let pCyl = origin + dir * tCyl;
                // Check height bounds
                if (pCyl.y >= studBase.y && pCyl.y <= studBase.y + studH) {
                    if (tCyl < tHit) {
                        tHit = tCyl;
                    }
                }
            }
        }
    }

    if (tHit < 1e29) { return tHit; }
    return -1.0;
}

// ─────────────────────────────────────────────────────────────────────────────
// Shadow Ray Traversal
// ─────────────────────────────────────────────────────────────────────────────

/// Optimized shadow ray traversal
/// Returns 0.0 if shadowed, 1.0 if lit
fn intersectShadow(origin : vec3<f32>, dir : vec3<f32>) -> f32 {
    let legoMode = camera.invProjParams.z > 0.5;

    // Basic terrain setup (reused from main)
    let terrainSize = vec2<f32>(camera.terrainSize);
    let cellScale = camera.metrics.y;
    let terrainOrigin = 0.5 * (terrainSize - vec2<f32>(1.0, 1.0)) * cellScale;
    let borderMargin = cellScale * 1.0;
    let boundsMin = vec3<f32>(
        -terrainOrigin.x + borderMargin,
        -camera.metrics.x,
        -terrainOrigin.y + borderMargin
    );
    let boundsMax = vec3<f32>(
        terrainSize.x * cellScale - terrainOrigin.x - borderMargin,
        camera.metrics.x,
        terrainSize.y * cellScale - terrainOrigin.y - borderMargin
    );

    // Intersect AABB
    let range = intersectAabb(origin, dir, boundsMin, boundsMax);
    if (range.y < 0.0 || range.x > range.y) {
        return 1.0; // Lit (escapes bounds)
    }

    var t = max(range.x, 0.0) + 1e-4;

    // Check if we are already out of bounds
    if (t > range.y) { return 1.0; }

    // Start at mip level 0 for accuracy near surface, but ascend later for speed
    var mipLevel : u32 = 0u;

    // Standard DDA Setup (packed coordinates: Sebbbi's approach)
    var pos = origin + dir * t;
    let cellScaleMip = cellScale * f32(1u << mipLevel);
    let sizeI = vec2<i32>(i32(camera.terrainSize.x) >> mipLevel,
                          i32(camera.terrainSize.y) >> mipLevel);

    let cellX_init = clamp(i32(floor((pos.x + terrainOrigin.x) / cellScaleMip)), 0, sizeI.x - 1);
    let cellZ_init = clamp(i32(floor((pos.z + terrainOrigin.y) / cellScaleMip)), 0, sizeI.y - 1);
    var cellPacked = (cellZ_init << 16) | cellX_init;

    let stepX = select(-1, 1, dir.x >= 0.0);
    let stepZ = i32(select(-1, 1, dir.z >= 0.0)) << 16;
    let offsetX = select(0, 1, dir.x >= 0.0);
    let offsetZ = select(0, 1, dir.z >= 0.0) << 16;
    let offsetPacked = offsetZ | offsetX;

    let nextBoundaryX = (f32(cellX_init + offsetX) * cellScaleMip) - terrainOrigin.x;
    let nextBoundaryZ = (f32(cellZ_init + (offsetZ >> 16)) * cellScaleMip) - terrainOrigin.y;
    var tMaxX = select(1e30, (nextBoundaryX - pos.x) / dir.x + t, dir.x != 0.0);
    var tMaxZ = select(1e30, (nextBoundaryZ - pos.z) / dir.z + t, dir.z != 0.0);
    var tDeltaX = select(1e30, cellScaleMip / abs(dir.x), dir.x != 0.0);
    var tDeltaZ = select(1e30, cellScaleMip / abs(dir.z), dir.z != 0.0);

    let slopeY = toHeightmapScale(dir.y);
    let originY = toHeightmapCoordinate(origin.y);

    var loopCount = 0u;

    // Loop Limit: Increased for mip 0 traversal
    loop {
        loopCount++;
        if (loopCount > 2000u) { break; } // Safety break

        var tNext = min(tMaxX, tMaxZ);
        var yEnter = slopeY * t;
        var yExit = slopeY * tNext;

        // Sample height (unpack coordinates)
        var h = -1.0e30;
        let levelW = i32(camera.terrainSize.x) >> mipLevel;
        let levelH = i32(camera.terrainSize.y) >> mipLevel;
        let cellX = cellPacked & 0xffff;
        let cellZ = cellPacked >> 16;

        if (cellX >= 0 && cellX < levelW && cellZ >= 0 && cellZ < levelH) {
             h = f32(textureLoad(heightTex, vec2<i32>(cellX, cellZ), i32(mipLevel)).x) - originY;
        }

        // Intersection Check (Any Hit)
        // If ray enters below terrain height, it's blocked.
        // For shadows, we are strict: if any part of the segment is below height, shadow.
        // However, since we traverse empty space, we only care if the ray *starts* below,
        // or enters below.

        // Lego Mode: Studs protrude above h. We need to check against h + studHeight
        // to avoid skipping cells where the ray hits the stud but is above the brick surface.
        // Stud is ~0.2 units high.
        let hCheck = select(h, h + toHeightmapScale(cellScale * 0.25), legoMode && mipLevel == 0u);

        if (min(yEnter, yExit) <= hCheck) {
             // Potential hit - check if we need to descend or confirm hit
             
             if (mipLevel == 0u) {
                 if (legoMode) {
                     // 1. Calculate absolute World Y of the brick surface
                     // h is relative to originY in 0-65535 space
                     let h_map = (h + originY) / 65535.0;
                     let brickY = (h_map * 2.0 - 1.0) * camera.metrics.x;

                     // 2. Check Stud Intersection
                     let studHeightWorld = cellScale * 0.2;
                     let studRadiusWorld = cellScale * 0.35;
                     let cellCenterXZ = (vec2<f32>(f32(cellX)+0.5, f32(cellZ)+0.5)) * cellScale - terrainOrigin;
                     let studBasePos = vec3<f32>(cellCenterXZ.x, brickY, cellCenterXZ.y);

                     let tStud = intersectStud(origin, dir, studBasePos, studHeightWorld, studRadiusWorld);
                     if (tStud > 0.0 && tStud < tNext) {
                         return 0.0; // Hit stud
                     }

                     // 3. Check Brick Body Intersection
                     // Brick is defined by y <= brickY within this cell.
                     // The ray is in this cell between t and tNext.
                     // We just need to know if the ray segment [t, tNext] overlaps y <= brickY.
                     // yEnter = relative height at t, yExit = relative height at tNext
                     // brick height relative = h
                     if (min(yEnter, yExit) <= h) {
                         return 0.0; // Hit brick
                     }

                     // If we are here, we hit the expanded bounds (hCheck) but missed stud and brick.
                     // Fall through to "Next Cell".
                 } else {
                    return 0.0; // Hit standard terrain
                 }
             } else {
                 // Descend to Finer Mip Level (Sebbbi's packed approach)
                 mipLevel--;
                 tDeltaX *= 0.5;
                 tDeltaZ *= 0.5;
                 cellPacked = (cellPacked << 1) + offsetPacked;

                 // Relative boundary adjustment
                 if (t < tMaxX - tDeltaX) {
                     tMaxX -= tDeltaX;
                     cellPacked -= stepX;
                 }
                 if (t < tMaxZ - tDeltaZ) {
                     tMaxZ -= tDeltaZ;
                     cellPacked -= stepZ;
                 }
                 continue;
             }
        }

        // Next cell
        t = tNext;
        if (t > range.y) { break; } // Escaped terrain

        if (tMaxX < tMaxZ) {
            tMaxX += tDeltaX;
            cellPacked += stepX;
        } else {
            tMaxZ += tDeltaZ;
            cellPacked += stepZ;
        }

        // Level-Up Check - Ascend to Coarser Mip
        // When ray is far above terrain surface (relative to current mip height), ascend
        // Use stricter threshold for shadows to avoid missing thin occluders?
        // Use standard threshold from main loop
        let levelUpHeight = select(65535.0, f32(128u << mipLevel), mipLevel < 7u);
        if (yExit - levelUpHeight > h) {
            mipLevel++;
            if ((cellPacked & 1) != offsetX) { tMaxX += tDeltaX; }
            if ((cellPacked & 65536) != offsetZ) { tMaxZ += tDeltaZ; }
            tDeltaX *= 2.0;
            tDeltaZ *= 2.0;
            cellPacked = i32((u32(cellPacked) & 0xfffefffe) >> 1);
        }
    }

    return 1.0; // Lit
}

// ─────────────────────────────────────────────────────────────────────────────
// Main Compute Shader
// ─────────────────────────────────────────────────────────────────────────────
// Hierarchical DDA traversal algorithm:
//   1. Start at coarse mip level (7 for 8192×8192 = 64×64 cells)
//   2. Sample max-height at current cell and mip level
//   3. If ray potentially intersects cell's max height:
//      a. If at LOD-appropriate level for distance, accept hit
//      b. Otherwise descend to finer mip level
//   4. If ray clearly misses, step to next cell
//   5. Ascend to coarser mip when far from terrain surface (level-up)

@compute @workgroup_size(8, 8, 1)
fn main(@builtin(global_invocation_id) gid : vec3<u32>) {
    let dims = textureDimensions(outDepth);
    if (gid.x >= dims.x || gid.y >= dims.y) { return; }
    
    let origin = camera.cameraPos.xyz;
    let dir = rayDirFromPixel(camera.invViewProj, gid.xy, dims, origin);
    
    // ─────────────────────────────────────────────────────────────────────────
    // Terrain Bounds
    // ─────────────────────────────────────────────────────────────────────────
    let terrainSize = vec2<f32>(camera.terrainSize);
    let cellScale = camera.metrics.y;
    let terrainOrigin = 0.5 * (terrainSize - vec2<f32>(1.0, 1.0)) * cellScale;
    let borderMargin = cellScale * 1.0;
    let boundsMin = vec3<f32>(
        -terrainOrigin.x + borderMargin,
        -camera.metrics.x,
        -terrainOrigin.y + borderMargin
    );
    let boundsMax = vec3<f32>(
        terrainSize.x * cellScale - terrainOrigin.x - borderMargin,
        camera.metrics.x,
        terrainSize.y * cellScale - terrainOrigin.y - borderMargin
    );
    
    // ─────────────────────────────────────────────────────────────────────────
    // AABB Intersection
    // ─────────────────────────────────────────────────────────────────────────
    let range = intersectAabb(origin, dir, boundsMin, boundsMax);
    if (range.y < 0.0 || range.x > range.y) {
        // No hit, sky is fully lit (shadow = 1.0)
        textureStore(outDepth, vec2<i32>(gid.xy), vec4<f32>(-1.0, 1.0, 0.0, 0.0));
        return;
    }
    
    // ─────────────────────────────────────────────────────────────────────────
    // Initialize DDA at mip level 7
    // ─────────────────────────────────────────────────────────────────────────
    var t = max(range.x, 0.0) + 1e-4;
    var mipLevel : u32 = 7u;
    var pos = origin + dir * t;
    
    let cellScaleMip = cellScale * f32(1u << mipLevel);
    let sizeI = vec2<i32>(i32(camera.terrainSize.x) >> mipLevel, 
                          i32(camera.terrainSize.y) >> mipLevel);
    
    // Initialize cell position (packed: X in low 16 bits, Z in high 16 bits)
    let cellX_init = clamp(i32(floor((pos.x + terrainOrigin.x) / cellScaleMip)), 0, sizeI.x - 1);
    let cellZ_init = clamp(i32(floor((pos.z + terrainOrigin.y) / cellScaleMip)), 0, sizeI.y - 1);
    var cellPacked = (cellZ_init << 16) | cellX_init;
    
    // DDA Setup (packed coordinates: Sebbbi's approach)
    // ─────────────────────────────────────────────────────────────────────────
    let stepX = select(-1, 1, dir.x >= 0.0);
    let stepZ = i32(select(-1, 1, dir.z >= 0.0)) << 16;  // Packed Z step in high bits
    let offsetX = select(0, 1, dir.x >= 0.0);
    let offsetZ = select(0, 1, dir.z >= 0.0) << 16;       // Packed Z offset in high bits
    let offsetPacked = offsetZ | offsetX;
    // Initialize tMax (time to next boundary) and tDelta (time to cross one cell)
    let nextBoundaryX = (f32(cellX_init + offsetX) * cellScaleMip) - terrainOrigin.x;
    let nextBoundaryZ = (f32(cellZ_init + (offsetZ >> 16)) * cellScaleMip) - terrainOrigin.y;
    var tMaxX = select(1e30, (nextBoundaryX - pos.x) / dir.x + t, dir.x != 0.0);
    var tMaxZ = select(1e30, (nextBoundaryZ - pos.z) / dir.z + t, dir.z != 0.0);
    var tDeltaX = select(1e30, cellScaleMip / abs(dir.x), dir.x != 0.0);
    var tDeltaZ = select(1e30, cellScaleMip / abs(dir.z), dir.z != 0.0);
    
    // Convert ray Y to heightmap coordinate space for efficient comparisons
    let slopeY = toHeightmapScale(dir.y);
    let originY = toHeightmapCoordinate(origin.y);
    
    // ─────────────────────────────────────────────────────────────────────────
    // DDA Loop - Hierarchical Traversal
    // ─────────────────────────────────────────────────────────────────────────
    let legoMode = camera.invProjParams.z > 0.5;

    var loopCount = 0u;
    loop {
        loopCount++;
        if (loopCount > 2000u) {
            t = -2.0;
            break;
        }

        var tNext = min(tMaxX, tMaxZ);
        
        // Compute ray Y in heightmap space at entry and exit of current segment
        var yEnter = slopeY * t;
        var yExit = slopeY * tNext;
        
        // Sample max height from mip pyramid (already in heightmap space 0-65535)
        // Subtract originY to make comparison relative to ray origin
        // Unpack coordinates: X = low 16 bits, Z = high 16 bits
        var h = -1.0e30;
        let levelW = i32(camera.terrainSize.x) >> mipLevel;
        let levelH = i32(camera.terrainSize.y) >> mipLevel;
        let cellX = cellPacked & 0xffff;
        let cellZ = cellPacked >> 16;

        if (cellX >= 0 && cellX < levelW && cellZ >= 0 && cellZ < levelH) {
            h = f32(textureLoad(heightTex, vec2<i32>(cellX, cellZ), i32(mipLevel)).x) - originY;
        }
        
        // ─────────────────────────────────────────────────────────────────────
        // Potential Intersection Check
        // ─────────────────────────────────────────────────────────────────────
        // If ray Y (at either entry or exit) is below the max height, potential hit
        // Lego Mode: Expand height check to include studs
        let hCheck = select(h, h + toHeightmapScale(cellScale * 0.25), legoMode && mipLevel == 0u);

        if (min(yEnter, yExit) <= hCheck) {
            // Side-hit adjustment: if descending ray enters above terrain,
            // compute exact t where ray crosses terrain height

            var hitFound = false;

            if (legoMode) {
                if (mipLevel == 0u) {
                    // Logic for Lego intersection
                    // If we hit the stud or the brick, we count it as a hit.
                    // If we miss both, we do NOT set hitFound, and we do NOT descend (already at 0).
                    // We let the loop continue to next cell.

                    // But wait, the standard logic assumes if we are at mip 0, we hit.
                    // So we must manually check geometry here.

                    var hitLego = false;

                    // 1. Brick Body (y <= h)
                    if (min(yEnter, yExit) <= h) {
                        // Standard hit logic for the blocky part
                        if (slopeY < 0.0 && yEnter > h) {
                            // Hit top of brick
                            t = h / slopeY;
                        }
                        // Else hit side (t is already correct)
                        hitLego = true;
                    }

                    // 2. Stud Intersection
                    let studHeightWorld = cellScale * 0.2;
                    let studRadiusWorld = cellScale * 0.35;
                    // Reconstruct world Y of brick surface
                    let h_map = (h + originY) / 65535.0;
                    let brickY = (h_map * 2.0 - 1.0) * camera.metrics.x;
                    let cellCenterXZ = (vec2<f32>(f32(cellX)+0.5, f32(cellZ)+0.5)) * cellScale - terrainOrigin;
                    let studBasePos = vec3<f32>(cellCenterXZ.x, brickY, cellCenterXZ.y);

                    let tStud = intersectStud(origin, dir, studBasePos, studHeightWorld, studRadiusWorld);

                    if (tStud > 1e-3 && tStud < tNext) {
                        // If we hit stud closer than brick (or if we didn't hit brick)
                        if (!hitLego || tStud < t) {
                            t = tStud;
                            hitLego = true;
                        }
                    }

                    if (hitLego) {
                        hitFound = true;
                    }
                } else {
                    // Force descent if Lego mode enabled (handled by standard descend logic below)
                    // Just ensure we don't set hitFound = true prematurely for LODs
                }
            } else {
                if (slopeY < 0.0 && yEnter > h) {
                    t = h / slopeY;
                }

                // LOD check: stop descending if mip level is fine enough for distance.
                let lodBias = 10.0;
                if (f32(mipLevel) <= max(log(t) - 6.0, 0.0)) { hitFound = true; }
            }
            
            if (hitFound) { break; }
            
            // ─────────────────────────────────────────────────────────────────
            // Descend to Finer Mip Level (Sebbbi's packed approach)
            // ─────────────────────────────────────────────────────────────────
            // Only descend if we are not at mip 0
            if (mipLevel > 0u) {
                mipLevel--;
                tDeltaX *= 0.5;
                tDeltaZ *= 0.5;
                // Refine cell coordinates: shift left doubles both X and Z, add offset
                cellPacked = (cellPacked << 1) + offsetPacked;

                // Adjust cell if t is before the mid-boundary (relative approach)
                if (t < tMaxX - tDeltaX) {
                    tMaxX -= tDeltaX;
                    cellPacked -= stepX;
                }
                if (t < tMaxZ - tDeltaZ) {
                    tMaxZ -= tDeltaZ;
                    cellPacked -= stepZ;
                }
                continue;
            }
            // If mipLevel == 0 and we didn't find a hit (missed lego geometry),
            // we fall through to Next Cell logic.
        }
        
        // ─────────────────────────────────────────────────────────────────────
        // Step to Next Cell
        // ─────────────────────────────────────────────────────────────────────
        t = tNext;
        
        // Check if we've exited the terrain bounds
        if (t > range.y) { t = -1.0; break; }
        
        // Advance along the shorter axis (standard 2D DDA)
        if (tMaxX < tMaxZ) {
            tMaxX += tDeltaX;
            cellPacked += stepX;
        } else {
            tMaxZ += tDeltaZ;
            cellPacked += stepZ;
        }
        
        // ─────────────────────────────────────────────────────────────────────
        // Level-Up Check - Ascend to Coarser Mip (Sebbbi's packed approach)
        // ─────────────────────────────────────────────────────────────────────
        // When ray is far above terrain surface, ascend to coarser mip for faster traversal
        let levelUpHeight = select(65535.0, f32(128u << mipLevel), mipLevel < 7u);
        if (yExit - levelUpHeight > h) {
            mipLevel++;
            // Adjust tMax if we're not at a coarser-level cell boundary
            if ((cellPacked & 1) != offsetX) { tMaxX += tDeltaX; }
            if ((cellPacked & 65536) != offsetZ) { tMaxZ += tDeltaZ; }
            tDeltaX *= 2.0;
            tDeltaZ *= 2.0;
            // Coarsen cell coordinates: mask off low bits then shift right
            cellPacked = i32((u32(cellPacked) & 0xfffefffe) >> 1);
        }
    }
    
    // ─────────────────────────────────────────────────────────────────────────
    // Shadow Calculation & Output
    // ─────────────────────────────────────────────────────────────────────────
    var shadowFactor = 1.0;

    if (t > 0.0) {
        // Hit terrain! Trace shadow ray
        let hitPos = origin + dir * t;
        let lightDir = camera.lightDirWS.xyz;

        // Bias: offset origin towards light to avoid self-intersection
        // Larger bias needed for small cell scales to prevent shadow acne
        // For Lego, use a smaller bias relative to cell scale
        let shadowBias = select(1.5, camera.metrics.y * 0.2, legoMode);
        let shadowOrigin = hitPos + lightDir * shadowBias;

        shadowFactor = intersectShadow(shadowOrigin, lightDir);
    }

    // Output: R=depth, G=shadow
    textureStore(outDepth, vec2<i32>(gid.xy), vec4<f32>(t, shadowFactor, 0.0, 0.0));
}
