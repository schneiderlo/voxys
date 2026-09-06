// node scripts/test_lego_patch.mjs
import assert from "node:assert/strict";
import {
  SIZE,
  PLATE,
  STUD_HEIGHT,
  makeHeightmap,
  groupHeightmap,
  heightAt,
  stepBall,
  stepBalls,
} from "../web/lego_patch_model.mjs";
const heights = makeHeightmap(),
  model = groupHeightmap(heights),
  single = groupHeightmap(heights, false);
assert.deepEqual(
  model,
  groupHeightmap(makeHeightmap()),
  "Layout must be deterministic",
);
assert(
  model.bricks.length < single.bricks.length * 0.65,
  "Grouping must substantially reduce top faces",
);
assert(model.bricks.some((b) => b.w * b.d === 8));
for (let z = 0; z < SIZE; z++)
  for (let x = 0; x < SIZE; x++) {
    const level = heights[z * SIZE + x],
      id = model.owners[z * SIZE + x];
    if (!level) {
      assert.equal(id, -1);
      continue;
    }
    const b = model.bricks[id];
    assert(b && x >= b.x && x < b.x + b.w && z >= b.z && z < b.z + b.d);
    assert.equal(b.level, level);
    assert.equal(
      heightAt(model, x - SIZE / 2 + 0.05, z - SIZE / 2 + 0.05),
      level * PLATE,
    );
    assert.equal(
      heightAt(model, x - SIZE / 2 + 0.5, z - SIZE / 2 + 0.5),
      level * PLATE + STUD_HEIGHT,
    );
    assert.equal(
      heightAt(model, x - SIZE / 2 + 0.5, z - SIZE / 2 + 0.5),
      heightAt(single, x - SIZE / 2 + 0.5, z - SIZE / 2 + 0.5),
    );
  }
const area = model.bricks.reduce((n, b) => n + b.w * b.d, 0);
assert.equal(
  area,
  heights.filter((h) => h > 0).length,
  "Every active cell must be covered exactly once",
);
for (const [x, z] of [
  [0.5, 0.5],
  [0.05, 0.05],
  [-5.5, 2.5],
  [10.5, 3.5],
]) {
  const floor = heightAt(model, x, z),
    ball = { p: [x, floor + 3, z], v: [0, 0, 0], r: 0.26 };
  for (let step = 0; step < 2400; step++) stepBall(model, ball, 1 / 120);
  assert(ball.p.every(Number.isFinite) && ball.v.every(Number.isFinite));
  assert(
    Math.abs(ball.p[1] - heightAt(model, ball.p[0], ball.p[2]) - ball.r) <
      0.015,
    `Ball must rest on visible surface at ${x},${z}: ${ball.p[1]} vs ${floor + ball.r}`,
  );
  assert(ball.sleeping, "Settled balls must sleep");
}
console.log(
  `PASS: deterministic coverage, ${single.bricks.length} cells -> ${model.bricks.length} bricks, shared surface heights, stud/top collisions, sleeping`,
);

const empty = groupHeightmap(new Uint8Array(SIZE * SIZE));
const pair = [
  { p: [-0.2, 0.26, 0], v: [1, 0, 0], r: 0.26 },
  { p: [0.2, 0.26, 0], v: [-1, 0, 0], r: 0.26 },
];
stepBalls(empty, pair, 1 / 120);
assert(pair[1].p[0] - pair[0].p[0] >= 0.52, "Spheres must not overlap");
assert(pair[0].v[0] < 0 && pair[1].v[0] > 0, "Head-on spheres must separate");
console.log("PASS: bounded sphere-pair collision");
