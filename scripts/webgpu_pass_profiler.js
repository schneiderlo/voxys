// Per-pass GPU timing for a WebGPU page, injected before the page's scripts
// (see scripts/profile_free_build.mjs). Every render/compute pass without its
// own timestampWrites gets begin/end timestamps; buffer copies outside passes
// are counted by bytes. A pass is named by the pipelines it binds
// ("label|entryPoint@shader-module-label").
(() => {
  const P = globalThis.__passProf = {enabled: false, stats: new Map(), copies: {count: 0, bytes: 0}, errors: 0};
  const pipelineName = new WeakMap(), encoderDevice = new WeakMap(), encoderState = new WeakMap(),
    passRecord = new WeakMap(), bufferReadback = new WeakMap();
  const MAX = 64;
  const nameOf = desc => {
    const stage = desc.fragment || desc.compute || {};
    return `${desc.label || '?'}|${stage.entryPoint || 'main'}@${stage.module?.label || '?'}`;
  };
  const requestDevice = GPUAdapter.prototype.requestDevice;
  GPUAdapter.prototype.requestDevice = function (desc = {}) {
    const features = [...(desc.requiredFeatures || [])];
    if (this.features.has('timestamp-query') && !features.includes('timestamp-query')) features.push('timestamp-query');
    return requestDevice.call(this, {...desc, requiredFeatures: features});
  };
  for (const kind of ['createRenderPipeline', 'createComputePipeline']) {
    const sync = GPUDevice.prototype[kind], async = GPUDevice.prototype[kind + 'Async'];
    GPUDevice.prototype[kind] = function (desc) { const p = sync.call(this, desc); pipelineName.set(p, nameOf(desc)); return p; };
    GPUDevice.prototype[kind + 'Async'] = function (desc) {
      return async.call(this, desc).then(p => { pipelineName.set(p, nameOf(desc)); return p; });
    };
  }
  const createEncoder = GPUDevice.prototype.createCommandEncoder;
  GPUDevice.prototype.createCommandEncoder = function (...args) {
    const encoder = createEncoder.apply(this, args); encoderDevice.set(encoder, this); return encoder;
  };
  const stateOf = encoder => {
    let state = encoderState.get(encoder);
    if (!state) { state = {list: [], timed: 0}; encoderState.set(encoder, state); }
    return state;
  };
  for (const [kind, method] of [['render', 'beginRenderPass'], ['compute', 'beginComputePass']]) {
    const begin = GPUCommandEncoder.prototype[method];
    GPUCommandEncoder.prototype[method] = function (desc = {}) {
      const device = encoderDevice.get(this);
      const record = {kind, pipelines: new Set(), draws: 0, untimed: true};
      let passDesc = desc;
      if (P.enabled) {
        const state = stateOf(this);
        state.list.push(record);
        if (device?.features.has('timestamp-query') && !desc.timestampWrites && state.timed < MAX) {
          state.querySet ||= device.createQuerySet({type: 'timestamp', count: MAX * 2});
          record.index = state.timed++; record.untimed = false;
          passDesc = {...desc, timestampWrites: {querySet: state.querySet,
            beginningOfPassWriteIndex: record.index * 2, endOfPassWriteIndex: record.index * 2 + 1}};
        }
      }
      const pass = begin.call(this, passDesc);
      passRecord.set(pass, record);
      return pass;
    };
  }
  const copy = GPUCommandEncoder.prototype.copyBufferToBuffer;
  GPUCommandEncoder.prototype.copyBufferToBuffer = function (...args) {
    if (P.enabled) { P.copies.count++; P.copies.bytes += Number(args.length === 5 ? args[4] : args[2] ?? 0); }
    return copy.apply(this, args);
  };
  const writeBuffer = GPUQueue.prototype.writeBuffer;
  GPUQueue.prototype.writeBuffer = function (buffer, offset, data, dataOffset = 0, size) {
    if (P.enabled) {
      const bytes = size !== undefined ? size * (data.BYTES_PER_ELEMENT || 1) : data.byteLength - dataOffset * (data.BYTES_PER_ELEMENT || 1);
      P.uploads = P.uploads || {count: 0, bytes: 0}; P.uploads.count++; P.uploads.bytes += bytes;
    }
    return writeBuffer.call(this, buffer, offset, data, dataOffset, size);
  };
  for (const proto of [GPURenderPassEncoder.prototype, GPUComputePassEncoder.prototype]) {
    const setPipeline = proto.setPipeline;
    proto.setPipeline = function (p) { passRecord.get(this)?.pipelines.add(pipelineName.get(p) || '?'); return setPipeline.call(this, p); };
    for (const name of ['draw', 'drawIndexed', 'drawIndirect', 'drawIndexedIndirect', 'dispatchWorkgroups', 'dispatchWorkgroupsIndirect']) {
      if (!proto[name]) continue;
      const original = proto[name];
      proto[name] = function (...args) { const r = passRecord.get(this); if (r) r.draws++; return original.apply(this, args); };
    }
  }
  const finish = GPUCommandEncoder.prototype.finish;
  GPUCommandEncoder.prototype.finish = function (...args) {
    const state = encoderState.get(this);
    let readback = null;
    if (state?.querySet) {
      const device = encoderDevice.get(this), bytes = MAX * 2 * 8;
      const resolve = device.createBuffer({size: bytes, usage: GPUBufferUsage.QUERY_RESOLVE | GPUBufferUsage.COPY_SRC});
      const read = device.createBuffer({size: bytes, usage: GPUBufferUsage.MAP_READ | GPUBufferUsage.COPY_DST});
      this.resolveQuerySet(state.querySet, 0, state.timed * 2, resolve, 0);
      copy.call(this, resolve, 0, read, 0, bytes);
      readback = {read, resolve, querySet: state.querySet, list: state.list};
    }
    const buffer = finish.apply(this, args);
    if (readback) bufferReadback.set(buffer, readback);
    return buffer;
  };
  const key = r => `${r.kind}: ${[...r.pipelines].join(' + ') || '(no pipeline)'}`;
  const submit = GPUQueue.prototype.submit;
  GPUQueue.prototype.submit = function (buffers) {
    const result = submit.call(this, buffers);
    for (const b of buffers) {
      const rb = bufferReadback.get(b);
      if (!rb) continue;
      rb.read.mapAsync(GPUMapMode.READ).then(() => {
        const t = new BigInt64Array(rb.read.getMappedRange());
        for (const r of rb.list) {
          if (r.untimed) continue;
          const ms = Number(t[r.index * 2 + 1] - t[r.index * 2]) / 1e6;
          if (!(ms >= 0 && ms < 1000)) continue;
          const s = P.stats.get(key(r)) || {ms: 0, n: 0, draws: 0};
          s.ms += ms; s.n++; s.draws += r.draws; P.stats.set(key(r), s);
        }
        rb.read.unmap(); rb.read.destroy(); rb.resolve.destroy(); rb.querySet.destroy();
      }).catch(() => { P.errors++; });
    }
    return result;
  };
  P.reset = () => { P.stats.clear(); P.copies = {count: 0, bytes: 0}; P.uploads = {count: 0, bytes: 0}; P.errors = 0; };
  // Normalized per presented frame (frames = canvas textures acquired).
  P.report = frames => ({frames, errors: P.errors,
    copiesPerFrame: P.copies.count / frames, copyMBPerFrame: P.copies.bytes / frames / 1e6,
    uploadsPerFrame: (P.uploads?.count || 0) / frames, uploadMBPerFrame: (P.uploads?.bytes || 0) / frames / 1e6,
    rows: [...P.stats].map(([pass, s]) => ({pass, msPerFrame: s.ms / frames, perFrame: s.n / frames, drawsPerPass: s.draws / s.n}))
      .sort((a, b) => b.msPerFrame - a.msPerFrame)});
})();
