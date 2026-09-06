// Deterministic heightmap -> exposed grouped bricks. No per-frame generation.
export const SIZE = 32;
export const PLATE = 0.32;
export const STUD_RADIUS = 0.3;
export const STUD_HEIGHT = 0.18;
export const hash = (x, z) => {
  let h = Math.imul(x + 173, 374761393) ^ Math.imul(z + 419, 668265263);
  h = Math.imul(h ^ (h >>> 13), 1274126177);
  return (h ^ (h >>> 16)) >>> 0;
};
export function makeHeightmap() {
  const heights = new Uint8Array(SIZE * SIZE);
  for (let z = 0; z < SIZE; z++)
    for (let x = 0; x < SIZE; x++) {
      const px = (x - 15.5) / 14,
        pz = (z - 15.5) / 14;
      const radius = Math.sqrt(px * px + pz * pz);
      const hill =
        8.6 * Math.exp(-((px + 0.2) ** 2 * 2.5 + (pz - 0.15) ** 2 * 3.5));
      const coast = 1 - radius + 0.055 * Math.sin(px * 9 + pz * 5);
      heights[z * SIZE + x] =
        coast > 0 ? Math.max(1, Math.round(hill * Math.min(1, coast * 5))) : 0;
    }
  return heights;
}
export function groupHeightmap(heights, grouped = true) {
  const owners = new Int32Array(SIZE * SIZE).fill(-1),
    bricks = [];
  for (let z = 0; z < SIZE; z++)
    for (let x = 0; x < SIZE; x++) {
      const index = z * SIZE + x,
        level = heights[index];
      if (!level || owners[index] >= 0) continue;
      const rotated = (hash(x >> 2, z >> 1) & 1) !== 0;
      const shapes = grouped
        ? rotated
          ? [
              [2, 4],
              [4, 2],
              [2, 2],
              [1, 2],
              [2, 1],
              [1, 1],
            ]
          : [
              [4, 2],
              [2, 4],
              [2, 2],
              [2, 1],
              [1, 2],
              [1, 1],
            ]
        : [[1, 1]];
      for (const [w, d] of shapes) {
        if (x + w > SIZE || z + d > SIZE) continue;
        let valid = true;
        for (let dz = 0; dz < d; dz++)
          for (let dx = 0; dx < w; dx++) {
            const i = (z + dz) * SIZE + x + dx;
            if (heights[i] !== level || owners[i] >= 0) valid = false;
          }
        if (!valid) continue;
        const id = bricks.length;
        bricks.push({ x, z, w, d, level, id });
        for (let dz = 0; dz < d; dz++)
          for (let dx = 0; dx < w; dx++) owners[(z + dz) * SIZE + x + dx] = id;
        break;
      }
    }
  return { heights, owners, bricks };
}
export function heightAt(model, x, z, studs = true) {
  const ix = Math.floor(x + SIZE / 2),
    iz = Math.floor(z + SIZE / 2);
  if (ix < 0 || iz < 0 || ix >= SIZE || iz >= SIZE) return 0;
  const level = model.heights[iz * SIZE + ix];
  if (!level) return 0;
  const dx = x + SIZE / 2 - ix - 0.5,
    dz = z + SIZE / 2 - iz - 0.5;
  return (
    level * PLATE +
    (studs && dx * dx + dz * dz <= STUD_RADIUS ** 2 ? STUD_HEIGHT : 0)
  );
}
function contact(ball, closest) {
  const delta = ball.p.map((v, i) => v - closest[i]);
  const distance = Math.hypot(...delta);
  if (distance >= ball.r || distance < 1e-9) return;
  const n = delta.map((v) => v / distance),
    penetration = ball.r - distance;
  for (let i = 0; i < 3; i++) ball.p[i] += n[i] * (penetration + 1e-5);
  const vn = ball.v.reduce((sum, v, i) => sum + v * n[i], 0);
  if (vn < 0) {
    const restitution = Math.abs(vn) > 0.8 ? 0.48 : 0;
    for (let i = 0; i < 3; i++) ball.v[i] -= (1 + restitution) * vn * n[i];
    if (n[1] > 0.5) {
      ball.v[0] *= 0.985;
      ball.v[2] *= 0.985;
      ball.grounded = true;
    }
  }
}
const clamp = (x, a, b) => Math.max(a, Math.min(b, x));
export function stepBall(model, ball, dt) {
  if (ball.sleeping) return;
  ball.v[1] -= 9.81 * dt;
  for (let i = 0; i < 3; i++) ball.p[i] += ball.v[i] * dt;
  ball.grounded = false;
  for (let iteration = 0; iteration < 3; iteration++) {
    if (ball.p[1] < ball.r) {
      ball.p[1] = ball.r;
      ball.v[1] = Math.abs(ball.v[1]) > 0.8 ? Math.abs(ball.v[1]) * 0.4 : 0;
      ball.grounded = true;
    }
    const ix = Math.floor(ball.p[0] + SIZE / 2),
      iz = Math.floor(ball.p[2] + SIZE / 2);
    const seen = new Set();
    for (let z = Math.max(0, iz - 1); z <= Math.min(SIZE - 1, iz + 1); z++)
      for (let x = Math.max(0, ix - 1); x <= Math.min(SIZE - 1, ix + 1); x++) {
        const id = model.owners[z * SIZE + x];
        if (id < 0) continue;
        const b = model.bricks[id],
          top = b.level * PLATE;
        if (!seen.has(id)) {
          seen.add(id);
          const min = [b.x - SIZE / 2, 0, b.z - SIZE / 2],
            max = [min[0] + b.w, top, min[2] + b.d];
          contact(
            ball,
            ball.p.map((v, i) => clamp(v, min[i], max[i])),
          );
        }
        const cx = x - SIZE / 2 + 0.5,
          cz = z - SIZE / 2 + 0.5;
        const dx = ball.p[0] - cx,
          dz = ball.p[2] - cz,
          len = Math.hypot(dx, dz);
        const ratio = Math.min(1, STUD_RADIUS / Math.max(len, 1e-9));
        contact(ball, [
          cx + dx * ratio,
          clamp(ball.p[1], top, top + STUD_HEIGHT),
          cz + dz * ratio,
        ]);
      }
  }
  if (ball.grounded && Math.hypot(...ball.v) < 0.08)
    ball.quiet = (ball.quiet || 0) + dt;
  else ball.quiet = 0;
  if (ball.quiet > 0.6) {
    ball.sleeping = true;
    ball.v.fill(0);
  }
}

// The toy interaction is deliberately capped at 16 spheres in the page.
// Terrain contacts remain local; at most 120 sphere pairs need testing.
export function stepBalls(model, balls, dt) {
  for (const ball of balls) stepBall(model, ball, dt);
  for (let i = 0; i < balls.length; i++)
    for (let j = i + 1; j < balls.length; j++) {
      const a = balls[i],
        b = balls[j];
      if (a.sleeping && b.sleeping) continue;
      const delta = b.p.map((v, axis) => v - a.p[axis]);
      const distance = Math.hypot(...delta),
        radius = a.r + b.r;
      if (distance >= radius) continue;
      const normal =
        distance > 1e-8 ? delta.map((v) => v / distance) : [1, 0, 0];
      const speed = b.v.reduce(
        (sum, v, axis) => sum + (v - a.v[axis]) * normal[axis],
        0,
      );
      for (let axis = 0; axis < 3; axis++) {
        const correction = normal[axis] * (radius - distance + 1e-5) * 0.5;
        a.p[axis] -= correction;
        b.p[axis] += correction;
        if (speed < 0) {
          const impulse = -speed * 0.7 * normal[axis];
          a.v[axis] -= impulse;
          b.v[axis] += impulse;
        }
      }
      a.sleeping = false;
      b.sleeping = false;
      a.quiet = 0;
      b.quiet = 0;
    }
}
