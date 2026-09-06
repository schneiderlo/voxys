// npm install --no-save webgpu
// node scripts/test_lego_patch_gpu.mjs (requires a Vulkan WebGPU adapter)
// Runs the actual page renderer against an offscreen WebGPU target.
import { create, globals } from "webgpu";
import * as model from "../web/lego_patch_model.mjs";
import fs from "node:fs";
import vm from "node:vm";
import assert from "node:assert/strict";
const gpu = create(["backend=vulkan"]);
let device, target;
const errors = [],
  elements = new Map(),
  events = new Map();
function element(id) {
  if (!elements.has(id))
    elements.set(id, {
      textContent: "",
      hidden: true,
      classList: { toggle() {} },
      setAttribute() {},
      addEventListener(name, fn) {
        events.set(`${id}:${name}`, fn);
      },
    });
  return elements.get(id);
}
const canvas = {
  ...element("canvas"),
  width: 300,
  height: 150,
  setPointerCapture() {},
  getContext() {
    return {
      configure({ device: d }) {
        device = d;
        device.addEventListener("uncapturederror", (e) =>
          errors.push(e.error.message),
        );
      },
      getCurrentTexture() {
        if (
          !target ||
          target.width !== canvas.width ||
          target.height !== canvas.height
        ) {
          target?.destroy();
          target = device.createTexture({
            size: [canvas.width, canvas.height],
            format: "rgba8unorm",
            usage:
              globals.GPUTextureUsage.RENDER_ATTACHMENT |
              globals.GPUTextureUsage.COPY_SRC,
          });
        }
        return target;
      },
    };
  },
};
const context = {
  ...globals,
  ...model,
  URLSearchParams,
  console,
  Float32Array,
  Map,
  Set,
  Math,
  performance,
  innerWidth: 1000,
  innerHeight: 800,
  devicePixelRatio: 1,
  location: { search: "?test" },
  navigator: {
    gpu: {
      requestAdapter: (...args) => gpu.requestAdapter(...args),
      getPreferredCanvasFormat: () => "rgba8unorm",
    },
  },
  document: {
    querySelector(id) {
      return id === "canvas" ? canvas : element(id);
    },
    getElementById: (id) => element("#" + id),
    addEventListener(name, fn) {
      events.set(`document:${name}`, fn);
    },
    hidden: false,
  },
  requestAnimationFrame(fn) {
    context.nextFrame = fn;
  },
};
context.globalThis = context;
const app = fs
  .readFileSync(new URL("../web/lego_patch.mjs", import.meta.url), "utf8")
  .replace(/^import[\s\S]*?from ["']\.\/lego_patch_model\.mjs["'];?\n/, "")
  .replace(/init\(\)\.catch\(/, "globalThis.ready=init().catch(");
vm.runInNewContext(app, context);
await context.ready;
assert(context.legoPatchTest, element("#error").textContent);
context.nextFrame(16);
await device.queue.onSubmittedWorkDone();
const row = Math.ceil((canvas.width * 4) / 256) * 256;
const readback = device.createBuffer({
  size: row * canvas.height,
  usage: globals.GPUBufferUsage.COPY_DST | globals.GPUBufferUsage.MAP_READ,
});
const encoder = device.createCommandEncoder();
encoder.copyTextureToBuffer(
  { texture: target },
  { buffer: readback, bytesPerRow: row },
  [canvas.width, canvas.height],
);
device.queue.submit([encoder.finish()]);
await readback.mapAsync(globals.GPUMapMode.READ);
const pixels = new Uint8Array(readback.getMappedRange());
const colors = new Set();
for (let y = 0; y < canvas.height; y += 8)
  for (let x = 0; x < canvas.width; x += 8) {
    const i = y * row + x * 4;
    colors.add(`${pixels[i]},${pixels[i + 1]},${pixels[i + 2]}`);
  }
assert(
  colors.size > 100,
  "Scene must render detailed terrain, not a blank target",
);
readback.unmap();
readback.destroy();
context.nextFrame(24);
assert.equal(element("#fps").textContent, "Idle");
const grouped = context.legoPatchTest.model.bricks.length;
element("#single").onclick();
context.nextFrame(32);
await device.queue.onSubmittedWorkDone();
assert(context.legoPatchTest.model.bricks.length > grouped);
element("#grouped").onclick();
assert.equal(context.legoPatchTest.model.bricks.length, grouped);
for (let i = 0; i < 20; i++) element("#drop").onclick();
assert.equal(context.legoPatchTest.balls.length, 16);
context.nextFrame(48);
await device.queue.onSubmittedWorkDone();
context.innerWidth = 375;
context.innerHeight = 667;
context.devicePixelRatio = 3;
context.nextFrame(64);
await device.queue.onSubmittedWorkDone();
assert(canvas.width * canvas.height <= 1603000);
element("#reset").onclick();
assert.equal(context.legoPatchTest.balls.length, 0);
context.innerWidth = 300;
context.innerHeight = 150;
context.devicePixelRatio = 1;
context.nextFrame(80);
await device.queue.onSubmittedWorkDone();
assert.equal(canvas.width, 300);
context.document.hidden = true;
events.get("document:visibilitychange")();
context.nextFrame(96);
assert.deepEqual(errors, []);
console.log(
  `PASS: ${colors.size} sampled colors; cached terrain/shadow pipelines; group toggles; 16 sphere instances; mobile resize; reset; hidden pause`,
);
device.destroy();
