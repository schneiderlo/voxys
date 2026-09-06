import {
  SIZE,
  PLATE,
  STUD_RADIUS,
  STUD_HEIGHT,
  hash,
  makeHeightmap,
  groupHeightmap,
  heightAt,
  stepBalls,
} from "./lego_patch_model.mjs";
const canvas = document.querySelector("canvas"),
  status = document.querySelector("#stats");
const vec = {
  sub: (a, b) => a.map((v, i) => v - b[i]),
  dot: (a, b) => a.reduce((s, v, i) => s + v * b[i], 0),
  cross: (a, b) => [
    a[1] * b[2] - a[2] * b[1],
    a[2] * b[0] - a[0] * b[2],
    a[0] * b[1] - a[1] * b[0],
  ],
};
const unit = (a) => {
  const n = Math.hypot(...a);
  return a.map((v) => v / n);
};
function lookAt(eye, target) {
  const z = unit(vec.sub(eye, target)),
    x = unit(vec.cross([0, 1, 0], z)),
    y = vec.cross(z, x);
  return [
    x[0],
    y[0],
    z[0],
    0,
    x[1],
    y[1],
    z[1],
    0,
    x[2],
    y[2],
    z[2],
    0,
    -vec.dot(x, eye),
    -vec.dot(y, eye),
    -vec.dot(z, eye),
    1,
  ];
}
function multiply(a, b) {
  const c = new Float32Array(16);
  for (let col = 0; col < 4; col++)
    for (let row = 0; row < 4; row++)
      for (let k = 0; k < 4; k++)
        c[col * 4 + row] += a[k * 4 + row] * b[col * 4 + k];
  return c;
}
function perspective(aspect) {
  const f = 1 / Math.tan(0.68 / 2),
    near = 0.1,
    far = 150;
  return [
    f / aspect,
    0,
    0,
    0,
    0,
    f,
    0,
    0,
    0,
    0,
    far / (near - far),
    -1,
    0,
    0,
    (far * near) / (near - far),
    0,
  ];
}
function ortho() {
  const r = 23,
    n = 1,
    f = 85;
  return [
    1 / r,
    0,
    0,
    0,
    0,
    1 / r,
    0,
    0,
    0,
    0,
    1 / (n - f),
    0,
    0,
    0,
    n / (n - f),
    1,
  ];
}
const light = unit([-0.7, 1.4, -0.6]);
const shadowMatrix = multiply(
  ortho(),
  lookAt(
    light.map((v) => v * 42),
    [0, 0, 0],
  ),
);
const params = new URLSearchParams(location.search);
let model = groupHeightmap(makeHeightmap()),
  yaw = 0.65,
  pitch = 0.82,
  distance = 48,
  balls = [],
  accumulator = 0;
let gpu,
  device,
  context,
  pipeline,
  shadowPipeline,
  staticBuffer,
  staticCount,
  ballBuffer,
  instanceBuffer,
  sphereCount = 0,
  uniformBuffer,
  bindGroup,
  shadowBindGroup,
  depthTexture,
  msaaTexture,
  shadowTexture,
  shadowDirty = true;
const u = new Float32Array(40);
let sceneDirty = true;
let last = 0,
  frameCount = 0,
  fpsStart = 0,
  hidden = false,
  deviceFailed = false,
  appReady = false,
  eye = [0, 0, 0];
const shader = `
struct Uniforms { vp:mat4x4<f32>, lightVP:mat4x4<f32>, eye:vec4<f32>, sun:vec4<f32> };
@group(0) @binding(0) var<uniform> u:Uniforms;
@group(0) @binding(1) var shadowTex:texture_depth_2d;
@group(0) @binding(2) var shadowSampler:sampler_comparison;
struct Input {@location(0) p:vec3<f32>,@location(1) n:vec3<f32>,@location(2) c:vec3<f32>,@location(3) bounds:vec4<f32>,@location(4) shape:vec2<f32>,@location(5) instance:vec4<f32>};
struct Vertex {@builtin(position) p:vec4<f32>,@location(0) world:vec3<f32>,@location(1) n:vec3<f32>,@location(2) c:vec3<f32>,@location(3) @interpolate(flat) bounds:vec4<f32>,@location(4) @interpolate(flat) shape:vec2<f32>};
@vertex fn vs(v:Input)->Vertex {var o:Vertex;let world=v.p*v.instance.w+v.instance.xyz;o.p=u.vp*vec4<f32>(world,1);o.world=world;o.n=v.n;o.c=v.c;o.bounds=v.bounds;o.shape=v.shape;return o;}
@vertex fn shadowVS(v:Input)->@builtin(position) vec4<f32>{return u.lightVP*vec4<f32>(v.p,1);}
@fragment fn fs(v:Vertex)->@location(0) vec4<f32>{
 let normal=normalize(v.n); var n=normal;
 let pixelSize=max(length(dpdx(v.world)),length(dpdy(v.world)));
 let detail=1.0-smoothstep(.012,.08,pixelSize);
 var ao=1.0;var roughness=.34;
 if(v.shape.y<1.5 && detail>0.0){
  var tilt=vec3<f32>(0.0);
  if(v.shape.y>.5){
   let radial=v.world.xz-v.bounds.xy;let radius=length(radial);
   if(normal.y>.5){let rim=1.0-smoothstep(0.0,.035,.30-radius);tilt=vec3<f32>(radial.x,0,radial.y)*rim/max(radius,.0001);}
   else {tilt.y=1.0-smoothstep(0.0,.035,v.shape.x-v.world.y);}
  }else{
   let local=v.world.xz-v.bounds.xy;let distances=min(local,v.bounds.zw-local);let edge=vec2<f32>(1)-smoothstep(vec2<f32>(0),vec2<f32>(.055),distances);
   let signEdge=sign(local-v.bounds.zw*.5);
   tilt=vec3<f32>(signEdge.x*edge.x*(1-abs(normal.x)),0,signEdge.y*edge.y*(1-abs(normal.z)));
   if(normal.y<.5){tilt.y=1-smoothstep(0,.055,v.shape.x-v.world.y);}
   ao=1-.12*(1-smoothstep(0,.025,min(distances.x,distances.y)))*detail;
  }
  tilt/=max(1.0,length(tilt));n=normalize(normal+tilt*detail);
 }
 let sun=u.sun.xyz;let view=normalize(u.eye.xyz-v.world);let halfway=normalize(sun+view);
 let ndl=max(dot(n,sun),0);let ndh=max(dot(n,halfway),0);
 roughness=mix(roughness,.58,smoothstep(.06,.8,pixelSize));
 let alpha=roughness*roughness;let a2=alpha*alpha;let denom=ndh*ndh*(a2-1)+1;
 let distribution=a2/max(3.14159*denom*denom,.0001);
 let spec=min(1.5,distribution*.028)*ndl;
 let sp=u.lightVP*vec4<f32>(v.world+normal*.015,1);let suv=sp.xy/sp.w*vec2<f32>(.5,-.5)+.5;
 let shadow=textureSampleCompare(shadowTex,shadowSampler,suv,sp.z/sp.w-.0004);
 let sky=mix(vec3<f32>(.23,.29,.28),vec3<f32>(.52,.64,.70),n.y*.5+.5);
 var color=v.c*(sky*.50+vec3<f32>(1.04,.97,.83)*ndl*shadow*.92)*ao+spec*shadow;
 if(v.shape.y>2.5){color=mix(vec3<f32>(.22,.47,.56),vec3<f32>(.37,.65,.71),clamp(v.world.z/45+.5,0,1));color*=.82+.18*shadow;}
 color=color/(color+vec3<f32>(.65));color=pow(color,vec3<f32>(1.0/2.2));
 return vec4<f32>(color,1);
}`;
function meshBuilder() {
  const data = [];
  function vertex(p, n, c, bounds, shape) {
    data.push(...p, ...n, ...c, ...bounds, ...shape);
  }
  function triangle(a, b, c, n, color, bounds, shape) {
    vertex(a, n, color, bounds, shape);
    vertex(b, n, color, bounds, shape);
    vertex(c, n, color, bounds, shape);
  }
  function quad(a, b, c, d, n, color, bounds, shape) {
    triangle(a, b, c, n, color, bounds, shape);
    triangle(a, c, d, n, color, bounds, shape);
  }
  return { data, vertex, triangle, quad };
}
function islandMesh() {
  const mesh = meshBuilder(),
    { quad, triangle } = mesh;
  // A single seabed plane gives the island a quiet background and receives shadows.
  quad(
    [-100, -0.02, -100],
    [-100, -0.02, 100],
    [100, -0.02, 100],
    [100, -0.02, -100],
    [0, 1, 0],
    [0.2, 0.5, 0.6],
    [0, 0, 1, 1],
    [0, 3],
  );
  for (const b of model.bricks) {
    const x = b.x - SIZE / 2,
      z = b.z - SIZE / 2,
      y = b.level * PLATE,
      bounds = [x, z, b.w, b.d],
      shape = [y, 0];
    const variation = 0.93 + (hash(b.x, b.z) % 100) / 900;
    const palette =
      b.level <= 2
        ? [0.72, 0.55, 0.29]
        : b.level >= 7
          ? [0.18, 0.34, 0.24]
          : [0.29, 0.45, 0.23];
    const color = palette.map((v) => v * variation);
    quad(
      [x, y, z],
      [x, y, z + b.d],
      [x + b.w, y, z + b.d],
      [x + b.w, y, z],
      [0, 1, 0],
      color,
      bounds,
      shape,
    );
    function neighbor(ix, iz) {
      return ix < 0 || iz < 0 || ix >= SIZE || iz >= SIZE
        ? 0
        : model.heights[iz * SIZE + ix] * PLATE;
    }
    // Split only exposed borders at height changes; no buried internal walls.
    for (let k = 0; k < b.w; k++) {
      let base = neighbor(b.x + k, b.z - 1);
      if (base < y)
        quad(
          [x + k, base, z],
          [x + k, y, z],
          [x + k + 1, y, z],
          [x + k + 1, base, z],
          [0, 0, -1],
          color,
          bounds,
          shape,
        );
      base = neighbor(b.x + k, b.z + b.d);
      if (base < y)
        quad(
          [x + k, base, z + b.d],
          [x + k + 1, base, z + b.d],
          [x + k + 1, y, z + b.d],
          [x + k, y, z + b.d],
          [0, 0, 1],
          color,
          bounds,
          shape,
        );
    }
    for (let k = 0; k < b.d; k++) {
      let base = neighbor(b.x - 1, b.z + k);
      if (base < y)
        quad(
          [x, base, z + k],
          [x, base, z + k + 1],
          [x, y, z + k + 1],
          [x, y, z + k],
          [-1, 0, 0],
          color,
          bounds,
          shape,
        );
      base = neighbor(b.x + b.w, b.z + k);
      if (base < y)
        quad(
          [x + b.w, base, z + k],
          [x + b.w, y, z + k],
          [x + b.w, y, z + k + 1],
          [x + b.w, base, z + k + 1],
          [1, 0, 0],
          color,
          bounds,
          shape,
        );
    }
    for (let dz = 0; dz < b.d; dz++)
      for (let dx = 0; dx < b.w; dx++) {
        const cx = x + dx + 0.5,
          cz = z + dz + 0.5,
          top = y + STUD_HEIGHT,
          sb = [cx, cz, STUD_RADIUS, STUD_RADIUS],
          ss = [top, 1],
          segments = 16;
        for (let j = 0; j < segments; j++) {
          const a = (j * Math.PI * 2) / segments,
            c = ((j + 1) * Math.PI * 2) / segments;
          const nx = Math.cos(a),
            nz = Math.sin(a),
            mx = Math.cos(c),
            mz = Math.sin(c);
          const p0 = [cx + nx * STUD_RADIUS, y, cz + nz * STUD_RADIUS],
            p1 = [p0[0], top, p0[2]],
            p2 = [cx + mx * STUD_RADIUS, top, cz + mz * STUD_RADIUS],
            p3 = [p2[0], y, p2[2]];
          triangle([cx, top, cz], p2, p1, [0, 1, 0], color, sb, ss);
          for (const [p, n] of [
            [p0, [nx, 0, nz]],
            [p1, [nx, 0, nz]],
            [p2, [mx, 0, mz]],
            [p0, [nx, 0, nz]],
            [p2, [mx, 0, mz]],
            [p3, [mx, 0, mz]],
          ])
            mesh.vertex(p, n, color, sb, ss);
        }
      }
  }
  return new Float32Array(mesh.data);
}
function sphereMesh() {
  const m = meshBuilder();
  const ball = { p: [0, 0, 0], r: 1 };
  {
    const c = [0.93, 0.23, 0.055];
    for (let y = 0; y < 10; y++)
      for (let x = 0; x < 16; x++) {
        const sample = (ix, iy) => {
          const p = (iy * Math.PI) / 10,
            t = (ix * Math.PI * 2) / 16;
          const n = [
            Math.sin(p) * Math.cos(t),
            Math.cos(p),
            Math.sin(p) * Math.sin(t),
          ];
          return [n.map((v, i) => ball.p[i] + v * ball.r), n];
        };
        for (const [ix, iy] of [
          [x, y],
          [x + 1, y + 1],
          [x + 1, y],
          [x, y],
          [x, y + 1],
          [x + 1, y + 1],
        ]) {
          const [p, n] = sample(ix, iy);
          m.vertex(p, n, c, [0, 0, 1, 1], [0, 2]);
        }
      }
  }
  return new Float32Array(m.data);
}
function rebuild(grouped) {
  model = groupHeightmap(model.heights, grouped);
  const vertices = islandMesh();
  staticBuffer?.destroy();
  staticBuffer = device.createBuffer({
    size: vertices.byteLength,
    usage: GPUBufferUsage.VERTEX | GPUBufferUsage.COPY_DST,
  });
  device.queue.writeBuffer(staticBuffer, 0, vertices);
  staticCount = vertices.length / 15;
  shadowDirty = true;
  sceneDirty = true;
  status.textContent = `${model.bricks.length} bricks · ${model.heights.filter((v) => v > 0).length} studs · one little island`;
  for (const id of ["grouped", "single"]) {
    const active = (id === "grouped") === grouped;
    document.getElementById(id).classList.toggle("active", active);
    document.getElementById(id).setAttribute("aria-pressed", String(active));
  }
}
function resize() {
  const scale = Math.min(
    devicePixelRatio,
    1.5,
    Math.sqrt(1600000 / (innerWidth * innerHeight)),
  );
  const w = Math.max(1, Math.round(innerWidth * scale)),
    h = Math.max(1, Math.round(innerHeight * scale));
  if (canvas.width === w && canvas.height === h && depthTexture && msaaTexture)
    return;
  sceneDirty = true;
  canvas.width = w;
  canvas.height = h;
  depthTexture?.destroy();
  msaaTexture?.destroy();
  depthTexture = device.createTexture({
    size: [w, h],
    format: "depth24plus",
    sampleCount: 4,
    usage: GPUTextureUsage.RENDER_ATTACHMENT,
  });
  msaaTexture = device.createTexture({
    size: [w, h],
    format: navigator.gpu.getPreferredCanvasFormat(),
    sampleCount: 4,
    usage: GPUTextureUsage.RENDER_ATTACHMENT,
  });
}
function drop(x = 0, z = 0) {
  sceneDirty = true;
  if (balls.length >= 16) balls.shift();
  balls.push({
    p: [x, heightAt(model, x, z) + 3.5, z],
    v: [0.25, 0, 0.12],
    r: 0.26,
    quiet: 0,
    sleeping: false,
  });
}
function frame(now) {
  if (hidden || deviceFailed) {
    last = 0;
    requestAnimationFrame(frame);
    return;
  }
  resize();
  if (!sceneDirty && !balls.some((ball) => !ball.sleeping)) {
    last = now;
    accumulator = 0;
    document.querySelector("#fps").textContent = "Idle";
    fpsStart = now;
    frameCount = 0;
    requestAnimationFrame(frame);
    return;
  }
  const dt = Math.min(last ? (now - last) / 1000 : 0, 0.05);
  last = now;
  accumulator += dt;
  while (accumulator >= 1 / 120) {
    stepBalls(model, balls, 1 / 120);
    accumulator -= 1 / 120;
  }
  eye = [
    Math.sin(yaw) * Math.cos(pitch) * distance,
    Math.sin(pitch) * distance,
    Math.cos(yaw) * Math.cos(pitch) * distance,
  ];
  u.set(
    multiply(perspective(canvas.width / canvas.height), lookAt(eye, [0, 1, 0])),
    0,
  );
  u.set(shadowMatrix, 16);
  u.set([...eye, 1], 32);
  u.set([...light, 0], 36);
  device.queue.writeBuffer(uniformBuffer, 0, u);
  const encoder = device.createCommandEncoder();
  if (shadowDirty) {
    const p = encoder.beginRenderPass({
      colorAttachments: [],
      depthStencilAttachment: {
        view: shadowTexture.createView(),
        depthClearValue: 1,
        depthLoadOp: "clear",
        depthStoreOp: "store",
      },
    });
    p.setPipeline(shadowPipeline);
    p.setBindGroup(0, shadowBindGroup);
    p.setVertexBuffer(0, staticBuffer);
    p.setVertexBuffer(1, instanceBuffer, 0, 16);
    p.draw(staticCount);
    p.end();
    shadowDirty = false;
  }
  if (balls.length) {
    const instances = new Float32Array(balls.length * 4);
    balls.forEach((b, i) => instances.set([...b.p, b.r], i * 4));
    device.queue.writeBuffer(instanceBuffer, 16, instances);
  }
  const p = encoder.beginRenderPass({
    colorAttachments: [
      {
        view: msaaTexture.createView(),
        resolveTarget: context.getCurrentTexture().createView(),
        clearValue: { r: 0.55, g: 0.7, b: 0.74, a: 1 },
        loadOp: "clear",
        storeOp: "discard",
      },
    ],
    depthStencilAttachment: {
      view: depthTexture.createView(),
      depthClearValue: 1,
      depthLoadOp: "clear",
      depthStoreOp: "discard",
    },
  });
  p.setPipeline(pipeline);
  p.setBindGroup(0, bindGroup);
  p.setVertexBuffer(0, staticBuffer);
  p.setVertexBuffer(1, instanceBuffer, 0, 16);
  p.draw(staticCount);
  if (balls.length) {
    p.setVertexBuffer(0, ballBuffer);
    p.setVertexBuffer(1, instanceBuffer, 16, balls.length * 16);
    p.draw(sphereCount, balls.length);
  }
  p.end();
  device.queue.submit([encoder.finish()]);
  sceneDirty = false;
  frameCount++;
  if (now - fpsStart > 750) {
    document.querySelector("#fps").textContent =
      `${Math.round((frameCount * 1000) / (now - fpsStart))} FPS`;
    fpsStart = now;
    frameCount = 0;
  }
  requestAnimationFrame(frame);
}
async function init() {
  if (!navigator.gpu)
    throw new Error(
      "This terrain study needs a browser with WebGPU. Open Voxys in a WebGPU-enabled browser.",
    );
  gpu = await navigator.gpu.requestAdapter();
  if (!gpu) throw new Error("No WebGPU adapter is available.");
  device = await gpu.requestDevice();
  device.lost.then((info) => {
    document.querySelector("#error").hidden = false;
    document.querySelector("#error").textContent =
      "The graphics device was lost. Reload to restart the island.";
    deviceFailed = true;
  });
  device.addEventListener("uncapturederror", (e) => console.error(e.error));
  context = canvas.getContext("webgpu");
  context.configure({
    device,
    format: navigator.gpu.getPreferredCanvasFormat(),
    alphaMode: "opaque",
  });
  const module = device.createShaderModule({ code: shader });
  const info = await module.getCompilationInfo();
  if (info.messages.some((m) => m.type === "error"))
    throw new Error(info.messages.map((m) => m.message).join("\n"));
  const instanceLayout = {
    arrayStride: 16,
    stepMode: "instance",
    attributes: [{ shaderLocation: 5, offset: 0, format: "float32x4" }],
  };
  const vertex = {
    arrayStride: 60,
    attributes: [
      { shaderLocation: 0, offset: 0, format: "float32x3" },
      { shaderLocation: 1, offset: 12, format: "float32x3" },
      { shaderLocation: 2, offset: 24, format: "float32x3" },
      { shaderLocation: 3, offset: 36, format: "float32x4" },
      { shaderLocation: 4, offset: 52, format: "float32x2" },
    ],
  };
  pipeline = device.createRenderPipeline({
    layout: "auto",
    vertex: { module, entryPoint: "vs", buffers: [vertex, instanceLayout] },
    fragment: {
      module,
      entryPoint: "fs",
      targets: [{ format: navigator.gpu.getPreferredCanvasFormat() }],
    },
    primitive: { topology: "triangle-list", cullMode: "none" },
    depthStencil: {
      format: "depth24plus",
      depthWriteEnabled: true,
      depthCompare: "less",
    },
    multisample: { count: 4 },
  });
  shadowPipeline = device.createRenderPipeline({
    layout: "auto",
    vertex: {
      module,
      entryPoint: "shadowVS",
      buffers: [vertex, instanceLayout],
    },
    primitive: { topology: "triangle-list", cullMode: "none" },
    depthStencil: {
      format: "depth32float",
      depthWriteEnabled: true,
      depthCompare: "less",
      depthBias: 2,
      depthBiasSlopeScale: 2,
    },
  });
  uniformBuffer = device.createBuffer({
    size: 160,
    usage: GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST,
  });
  shadowTexture = device.createTexture({
    size: [1024, 1024],
    format: "depth32float",
    usage: GPUTextureUsage.RENDER_ATTACHMENT | GPUTextureUsage.TEXTURE_BINDING,
  });
  bindGroup = device.createBindGroup({
    layout: pipeline.getBindGroupLayout(0),
    entries: [
      { binding: 0, resource: { buffer: uniformBuffer } },
      { binding: 1, resource: shadowTexture.createView() },
      {
        binding: 2,
        resource: device.createSampler({
          compare: "less-equal",
          minFilter: "linear",
          magFilter: "linear",
        }),
      },
    ],
  });
  shadowBindGroup = device.createBindGroup({
    layout: shadowPipeline.getBindGroupLayout(0),
    entries: [{ binding: 0, resource: { buffer: uniformBuffer } }],
  });
  instanceBuffer = device.createBuffer({
    size: 17 * 16,
    usage: GPUBufferUsage.VERTEX | GPUBufferUsage.COPY_DST,
  });
  device.queue.writeBuffer(instanceBuffer, 0, new Float32Array([0, 0, 0, 1]));
  const sphere = sphereMesh();
  sphereCount = sphere.length / 15;
  ballBuffer = device.createBuffer({
    size: sphere.byteLength,
    usage: GPUBufferUsage.VERTEX | GPUBufferUsage.COPY_DST,
  });
  device.queue.writeBuffer(ballBuffer, 0, sphere);
  rebuild(true);
  resize();
  appReady = true;
  requestAnimationFrame(frame);
  if (params.has("test"))
    globalThis.legoPatchTest = {
      get model() {
        return model;
      },
      get balls() {
        return balls;
      },
      drop,
      rebuild,
      get vertexCount() {
        return staticCount;
      },
      get device() {
        return device;
      },
    };
}
const pointers = new Map();
let dragMoved = 0,
  hadMultiple = false;
canvas.addEventListener("pointerdown", (e) => {
  canvas.setPointerCapture(e.pointerId);
  pointers.set(e.pointerId, [e.clientX, e.clientY]);
  dragMoved = 0;
  if (pointers.size > 1) hadMultiple = true;
});
canvas.addEventListener("pointermove", (e) => {
  if (!pointers.has(e.pointerId)) return;
  sceneDirty = true;
  const old = pointers.get(e.pointerId),
    dx = e.clientX - old[0],
    dy = e.clientY - old[1];
  dragMoved += Math.abs(dx) + Math.abs(dy);
  if (pointers.size === 1) {
    yaw -= dx * 0.006;
    pitch = Math.max(0.2, Math.min(1.42, pitch + dy * 0.004));
  } else {
    const other = [...pointers.entries()].find(([id]) => id !== e.pointerId)[1];
    const before = Math.hypot(old[0] - other[0], old[1] - other[1]);
    const after = Math.hypot(e.clientX - other[0], e.clientY - other[1]);
    distance = Math.max(
      12,
      Math.min(75, (distance * before) / Math.max(after, 1)),
    );
  }
  pointers.set(e.pointerId, [e.clientX, e.clientY]);
});
function pick(clientX, clientY) {
  const right = unit(vec.cross([0, 1, 0], unit(vec.sub(eye, [0, 1, 0])))),
    forward = unit(vec.sub([0, 1, 0], eye)),
    up = vec.cross(right, forward),
    f = Math.tan(0.68 / 2);
  const nx =
      ((((clientX / innerWidth) * 2 - 1) * innerWidth) / innerHeight) * f,
    ny = (1 - (clientY / innerHeight) * 2) * f;
  const ray = unit(forward.map((v, i) => v + right[i] * nx + up[i] * ny));
  for (let t = 1; t < 100; t += 0.08) {
    const x = eye[0] + ray[0] * t,
      y = eye[1] + ray[1] * t,
      z = eye[2] + ray[2] * t;
    if (
      y <= heightAt(model, x, z) &&
      Math.abs(x) < SIZE / 2 &&
      Math.abs(z) < SIZE / 2
    ) {
      drop(x, z);
      return;
    }
  }
}
canvas.addEventListener("pointerup", (e) => {
  if (dragMoved < 5 && pointers.size === 1 && !hadMultiple && appReady)
    pick(e.clientX, e.clientY);
  pointers.delete(e.pointerId);
  if (!pointers.size) hadMultiple = false;
});
canvas.addEventListener("pointercancel", (e) => {
  pointers.delete(e.pointerId);
  if (!pointers.size) hadMultiple = false;
});
canvas.addEventListener(
  "wheel",
  (e) => {
    e.preventDefault();
    sceneDirty = true;
    distance = Math.max(
      12,
      Math.min(75, distance * Math.exp(e.deltaY * 0.001)),
    );
  },
  { passive: false },
);
document.querySelector("#grouped").onclick = () => appReady && rebuild(true);
document.querySelector("#single").onclick = () => appReady && rebuild(false);
document.querySelector("#drop").onclick = () => appReady && drop();
document.querySelector("#reset").onclick = () => {
  sceneDirty = true;
  balls = [];
  yaw = 0.65;
  pitch = 0.82;
  distance = 48;
};
document.addEventListener("visibilitychange", () => {
  hidden = document.hidden;
  last = 0;
});
init().catch((e) => {
  console.error(e);
  document.querySelector("#error").hidden = false;
  document.querySelector("#error").textContent = e.message;
});
