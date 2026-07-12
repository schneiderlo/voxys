# GPU Shader Optimization Tricks

Reference document for shader optimization techniques. Learned from analyzing [sebbbi's LimitedDetail](https://sebbbi.github.io/LimitedDetail/) hierarchical DDA terrain raycast implementation.

---

## 1. Bit-Packed Coordinates

**Problem:** Tracking 2D grid coordinates requires two variables and separate operations.

**Solution:** Pack X and Z into a single 32-bit integer.

```wgsl
// Pack: X in low 16 bits, Z in high 16 bits
var cellPacked = (cellZ << 16) | cellX;

// Unpack when needed
let cellX = cellPacked & 0xffff;
let cellZ = cellPacked >> 16;
```

**Benefits:**
- Single register instead of two
- Mip level transitions become single operations (see below)
- Step operations become single adds

---

## 2. Packed Step/Offset Constants

**Problem:** DDA stepping requires separate X and Z step operations.

**Solution:** Pre-shift Z constants to match the packed format.

```wgsl
let stepX = select(-1, 1, dir.x >= 0.0);
let stepZ = i32(select(-1, 1, dir.z >= 0.0)) << 16;  // Pre-shifted!

// Single add steps both dimensions
cellPacked += select(stepX, stepZ, tMaxZ < tMaxX);
```

---

## 3. Elegant Mip Level Transitions

### Descent (to finer mip)

**Naive approach:** Recalculate cell coordinates from world position.

**Sebbbi's approach:** Bit-shift doubles both coordinates simultaneously.

```wgsl
// Double both X and Z indices, add offset for direction
cellPacked = (cellPacked << 1) + offsetPacked;

// Relative boundary check (no world-space math!)
if (t < tMaxX - tDeltaX) {
    tMaxX -= tDeltaX;
    cellPacked -= stepX;
}
```

### Ascent (to coarser mip)

```wgsl
// Mask clears low bits of both X and Z, then shift halves both
cellPacked = i32((u32(cellPacked) & 0xfffefffe) >> 1);
```

**Key insight:** `0xfffefffe` = `1111...1110 1111...1110` clears bit 0 and bit 16 simultaneously.

---

## 4. Relative vs Absolute Boundary Calculation

**Naive approach:** Recalculate world-space boundary positions every mip transition.

```wgsl
// SLOW: World-space division every transition
let boundaryX = (f32(cellX & ~1) + 1.0) * cellScale - terrainOrigin.x;
let tMidX = (boundaryX - origin.x) / dir.x;
```

**Sebbbi's approach:** Use relative tMax deltas.

```wgsl
// FAST: Just subtract the delta you already have
if (t < tMaxX - tDeltaX) {
    tMaxX -= tDeltaX;
}
```

**Why it works:** After halving `tDeltaX` for the finer mip, `tMaxX - tDeltaX` gives the midpoint boundary.

---

## 5. Heightmap Coordinate Space

**Problem:** Normalizing height samples every iteration is expensive.

**Solution:** Convert ray parameters to heightmap space once, compare directly.

```wgsl
// Convert ONCE at setup
let slopeY = toHeightmapScale(dir.y);      // Ray slope in [0, 65535] space
let originY = toHeightmapCoordinate(origin.y);

// In loop: direct comparison, no normalization
let yEnter = slopeY * t;
let h = f32(textureLoad(heightTex, coord, mip).x) - originY;
if (yEnter <= h) { /* hit */ }
```

---

## 6. LOD Termination Based on Distance

**Problem:** Raymarching to pixel-perfect detail everywhere is wasteful.

**Solution:** Stop descending mip levels when detail would be sub-pixel.

```wgsl
// Accept hit at current mip if it's fine enough for this distance
if (f32(mipLevel) <= max(log(t) - 6.0, 0.0)) { break; }
```

**Interpretation:**
| Distance (t) | Min acceptable mip |
|--------------|-------------------|
| ~400         | 0 (finest)        |
| ~1100        | 1                 |
| ~2980        | 2                 |
| ~8100        | 3                 |

---

## 7. Hierarchical Empty-Space Skipping

**Pattern:** Start coarse, descend on potential hit, ascend when far from surface.

```wgsl
// Level-up when ray is far above terrain
let levelUpHeight = f32(128u << mipLevel);  // Threshold scales with mip
if (yExit - levelUpHeight > h) {
    mipLevel++;
    // Adjust tMax for coarser grid...
}
```

**Key insight:** The `128 << mipLevel` threshold ensures ray only ascends when there's no chance of missing geometry.

---

## 8. Avoid Division in Hot Loops

**Problem:** Division is expensive on GPUs.

**Solution:** Compute inverse once, multiply instead.

```wgsl
// ONCE at setup
let invDir = 1.0 / (dir + sign(dir) * 1e-20);  // Safe inverse

// In loop: multiply instead of divide
let t0 = (bmin - origin) * invDir;
```

---

## 9. Use `select()` Instead of Branches

**Problem:** GPU branches can cause warp divergence.

**Solution:** Use `select()` for simple conditionals.

```wgsl
// Instead of: if (dir.x >= 0.0) { step = 1; } else { step = -1; }
let step = select(-1, 1, dir.x >= 0.0);
```

---

## Summary: DDA Optimization Checklist

- [ ] Pack 2D coordinates into single integer
- [ ] Pre-shift Z constants for packed format
- [ ] Use bit-shifts for mip transitions
- [ ] Use relative tMax calculations (subtract deltas)
- [ ] Convert to heightmap space once at setup
- [ ] Add distance-based LOD termination
- [ ] Add level-up logic for empty-space skipping
- [ ] Precompute inverse direction
- [ ] Replace simple branches with `select()`
