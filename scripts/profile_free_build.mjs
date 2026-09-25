#!/usr/bin/env node
// Free Build frame-rate profile on this machine's hardware GPU (headless
// Chrome, native Vulkan). Reports frames actually presented to the canvas;
// the engine's own counter also counts loop iterations that submit nothing.
//
// usage: node scripts/profile_free_build.mjs <site-dir> [scenarios] [width] [height]
//   scenarios: comma list of idle, walk, throw, cannon (default idle,walk,cannon)
//   PROFILE_PASSES=1       per-pass GPU ms, copies and uploads per presented frame
//   SHADER_OVERRIDE=l=p,.. replace the WGSL of shader module label l with file p
//   SCREENSHOT=file.raw    capture the start view's canvas pixels (+ .json size/format)
//   DAY_NIGHT=1|0          keep the day/night cycle running | disable it (default: paused at 09:00)
import http from 'node:http';
import {spawn} from 'node:child_process';
import {readFileSync, statSync, mkdtempSync, rmSync} from 'node:fs';
import path from 'node:path';
import {tmpdir} from 'node:os';

const [root, scenarioArg = 'idle,walk,cannon', w = '1920', h = '1080'] = process.argv.slice(2);
if (!root) { console.error('usage: profile_free_build.mjs <site-dir> [scenarios] [width] [height]'); process.exit(2); }
const width = Number(w), height = Number(h), scenarios = scenarioArg.split(',');
const passes = process.env.PROFILE_PASSES === '1';
const mime = {'.html': 'text/html', '.js': 'text/javascript', '.css': 'text/css', '.svg': 'image/svg+xml',
  '.png': 'image/png', '.json': 'application/json', '.wasm': 'application/wasm', '.data': 'application/octet-stream'};
const server = http.createServer((req, res) => {
  const p = new URL(req.url, 'http://x').pathname;
  const file = path.join(root, decodeURIComponent(p === '/' ? '/index.html' : p));
  try { statSync(file); } catch { res.writeHead(404); return res.end(); }
  res.writeHead(200, {'content-type': mime[path.extname(file)] || 'application/octet-stream'});
  res.end(readFileSync(file));
});
await new Promise(r => server.listen(0, '127.0.0.1', r));
const profile = mkdtempSync(path.join(tmpdir(), 'voxys-fps-'));
const chrome = spawn('google-chrome', ['--headless=new', '--no-sandbox', '--no-first-run', '--no-default-browser-check',
  '--disable-background-networking', '--enable-unsafe-webgpu', '--enable-webgpu-developer-features', '--disable-gpu-watchdog',
  '--disable-background-timer-throttling', '--disable-renderer-backgrounding', '--use-angle=vulkan', '--enable-features=Vulkan',
  '--use-vulkan=native', '--disable-vulkan-surface', '--remote-debugging-port=0', `--user-data-dir=${profile}`, 'about:blank'],
  {stdio: ['ignore', 'ignore', 'pipe']});
// Chrome keeps writing its profile until it exits; remove the directory only
// after that, or a tmpfs /tmp slowly fills with abandoned profiles.
const chromeClosed = new Promise(r => chrome.once('close', r));
const finish = async code => {
  try { chrome.kill('SIGKILL'); } catch {}
  await Promise.race([chromeClosed, new Promise(r => setTimeout(r, 5000))]);
  try { rmSync(profile, {recursive: true, force: true}); } catch {}
  process.exit(code);
};
for (const signal of ['SIGINT', 'SIGTERM']) process.on(signal, () => finish(130));
process.on('uncaughtException', error => { console.error(error); finish(1); });
process.on('unhandledRejection', error => { console.error(error); finish(1); });

const wsUrl = await new Promise((resolve, reject) => {
  let buf = '';
  chrome.stderr.on('data', d => { buf += d; const m = buf.match(/DevTools listening on (ws:\S+)/); if (m) resolve(m[1]); });
  setTimeout(() => reject(new Error('chrome did not start')), 30000);
});
const ws = new WebSocket(wsUrl);
await new Promise(r => ws.addEventListener('open', r));
let id = 0; const pending = new Map();
ws.addEventListener('message', e => {
  const m = JSON.parse(e.data);
  if (m.id && pending.has(m.id)) { pending.get(m.id)(m); pending.delete(m.id); }
  else if (m.method === 'Runtime.consoleAPICalled' && String(m.params.args[0]?.value).includes('EXP ')) console.log(String(m.params.args[0].value).slice(0, 240));
  else if (m.method === 'Runtime.exceptionThrown') console.log('page exception:', JSON.stringify(m.params.exceptionDetails).slice(0, 300));
});
const send = (method, params = {}, sessionId) => new Promise(r => { const n = ++id; pending.set(n, r); ws.send(JSON.stringify({id: n, method, params, sessionId})); });
const {result: {targetId}} = await send('Target.createTarget', {url: 'about:blank'});
const {result: {sessionId}} = await send('Target.attachToTarget', {targetId, flatten: true});
const s = (m, p) => send(m, p, sessionId);
const evaluate = async (expression, timeout = 120000) => {
  const r = await Promise.race([s('Runtime.evaluate', {expression, awaitPromise: true, returnByValue: true}),
    new Promise(res => setTimeout(() => res({timeout: true}), timeout))]);
  if (r.timeout) throw new Error('evaluate timeout');
  if (r.result?.exceptionDetails) throw new Error(JSON.stringify(r.result.exceptionDetails).slice(0, 500));
  return r.result?.result?.value;
};
await s('Page.enable'); await s('Runtime.enable');
const inject = source => s('Page.addScriptToEvaluateOnNewDocument', {source});
await inject(`(()=>{globalThis.__presented=0;const o=GPUCanvasContext.prototype.getCurrentTexture;
  GPUCanvasContext.prototype.getCurrentTexture=function(...a){globalThis.__presented++;const t=o.apply(this,a);globalThis.__canvasTexture=t;return t;};
  // Headless Vulkan canvases are not composited into page screenshots; copy
  // the frame's own texture right after the engine submits it instead.
  const configure=GPUCanvasContext.prototype.configure;
  GPUCanvasContext.prototype.configure=function(c){globalThis.__canvasDevice=c.device;globalThis.__canvasFormat=c.format;
    return configure.call(this,{...c,usage:(c.usage??GPUTextureUsage.RENDER_ATTACHMENT)|GPUTextureUsage.COPY_SRC});};
  const submit=GPUQueue.prototype.submit;
  GPUQueue.prototype.submit=function(b){const r=submit.call(this,b);const want=globalThis.__captureRequest;const t=globalThis.__canvasTexture;
    if(want&&t&&globalThis.__canvasDevice){globalThis.__captureRequest=null;const d=globalThis.__canvasDevice,w=t.width,h=t.height,row=Math.ceil(w*4/256)*256;
      const buf=d.createBuffer({size:row*h,usage:GPUBufferUsage.COPY_DST|GPUBufferUsage.MAP_READ});const e=d.createCommandEncoder();
      e.copyTextureToBuffer({texture:t},{buffer:buf,bytesPerRow:row},[w,h]);submit.call(this,[e.finish()]);
      buf.mapAsync(GPUMapMode.READ).then(()=>{const src=new Uint8Array(buf.getMappedRange()),out=new Uint8Array(w*h*4);
        for(let y=0;y<h;y++)out.set(src.subarray(y*row,y*row+w*4),y*w*4);buf.unmap();
        let bin='';for(let i=0;i<out.length;i+=65536)bin+=String.fromCharCode.apply(null,out.subarray(i,i+65536));
        want({width:w,height:h,format:globalThis.__canvasFormat,data:btoa(bin)});});}
    return r;};})();`);
if (passes) await inject(readFileSync(new URL('./webgpu_pass_profiler.js', import.meta.url), 'utf8'));
if (process.env.SHADER_OVERRIDE) {
  const files = Object.fromEntries(process.env.SHADER_OVERRIDE.split(',').map(e => { const [label, file] = e.split('='); return [label, readFileSync(file, 'utf8')]; }));
  await inject(`(()=>{const files=${JSON.stringify(files)};const o=GPUDevice.prototype.createShaderModule;
    GPUDevice.prototype.createShaderModule=function(d){const code=files[d.label];return o.call(this,code?{...d,code}:d);};})();`);
}
await s('Emulation.setDeviceMetricsOverride', {width, height, deviceScaleFactor: 1, mobile: false});
// Engine stage profiling would claim the timestamp writes the pass profiler uses.
await s('Page.navigate', {url: `http://127.0.0.1:${server.address().port}/index.html?experience=build${passes ? '&renderProfile=0&physicsProfile=0' : ''}`});
await s('Page.bringToFront');
await s('Emulation.setFocusEmulationEnabled', {enabled: true});
const t0 = Date.now();
await evaluate(`(async()=>{for(;;){try{
  const t=JSON.parse(voxyModule.UTF8ToString(voxyModule._voxy_get_telemetry_json()));
  const loading=getComputedStyle(document.getElementById('loading')).display!=='none';
  if(voxyModule._voxy_is_initialized()===1&&t.frame.count>=60&&!loading&&voxyModule._get_adventure_state_json())return;
}catch{}await new Promise(r=>setTimeout(r,250));}})()`, 300000);
const setting = (k, v) => evaluate(`voxyModule.ccall('voxy_renderer_set_number','number',['string','number','number'],['${k}',${v},1])`);
await evaluate(`voxyModule._voxy_set_uncapped_fps(1)`);
if (process.env.DAY_NIGHT === '0') await setting('lighting.dayNightEnabled', 0);
else if (process.env.DAY_NIGHT !== '1') { await setting('lighting.dayHour', 9); await setting('lighting.dayNightPaused', 1); }
await new Promise(r => setTimeout(r, 3000));
const adapter = await evaluate(`window.voxyDeviceProfile?.adapter?.description`);
console.log(`started in ${((Date.now() - t0) / 1000).toFixed(1)} s on ${adapter}; canvas ${await evaluate(`document.getElementById('voxy-canvas').width+'x'+document.getElementById('voxy-canvas').height`)}`);

if (process.env.SCREENSHOT) {
  // Raw RGBA/BGRA bytes plus a JSON sidecar with size and canvas format.
  const shot = await evaluate(`new Promise(r=>{globalThis.__captureRequest=r;})`, 30000);
  const fs = await import('node:fs');
  fs.writeFileSync(process.env.SCREENSHOT, Buffer.from(shot.data, 'base64'));
  fs.writeFileSync(process.env.SCREENSHOT + '.json', JSON.stringify({width: shot.width, height: shot.height, format: shot.format}));
  console.log(`screenshot ${process.env.SCREENSHOT} ${shot.width}x${shot.height} ${shot.format}`);
}
const state = () => evaluate(`JSON.parse(voxyModule.UTF8ToString(voxyModule._get_adventure_state_json()))`);
const measure = (ms, drive = 'async()=>{}') => evaluate(`(async()=>{
  const read=()=>JSON.parse(voxyModule.UTF8ToString(voxyModule._voxy_get_telemetry_json()));
  const drive=${drive},st={};
  if(globalThis.__passProf){__passProf.reset();__passProf.enabled=true;}
  const start=performance.now(),first=read(),p0=__presented,cpu=[],gpu=new Map();
  while(performance.now()-start<${ms}){
    await drive(performance.now()-start,st);
    const t=read();
    if(Number.isFinite(t.frame?.cpu_ms))cpu.push(t.frame.cpu_ms);
    if(t.render_gpu?.frame_interval_available)gpu.set(t.render_gpu.frame,t.render_gpu.gpu_frame_ms);
    await new Promise(r=>setTimeout(r,16));
  }
  await drive(-1,st);
  const end=performance.now(),last=read(),presented=__presented-p0;
  let passes=null;
  if(globalThis.__passProf){__passProf.enabled=false;await new Promise(r=>setTimeout(r,500));passes=__passProf.report(presented);}
  const pct=(v,p)=>{const x=[...v].sort((a,b)=>a-b);return x.length?x[Math.min(x.length-1,Math.ceil(x.length*p)-1)]:null;};
  return {presentedFps:1000*presented/(end-start),loopFps:1000*(last.frame.count-first.frame.count)/(end-start),
    cpuP50:pct(cpu,.5),cpuP95:pct(cpu,.95),gpuP50:pct(gpu.values(),.5),gpuP95:pct(gpu.values(),.95),
    bodies:last.physics?.bodies?.current,passes};
})()`, ms + 60000);
const walk = `async(t,s)=>{const key=(k,d)=>voxyModule._voxy_key_event(k,d);
  if(t<0){for(const k of [87,83,37,39])key(k,0);return;}
  const phase=Math.floor(t/2500)%4;
  if(phase!==s.phase){for(const k of [87,83,37,39])key(k,0);key([87,39,83,37][phase],1);s.phase=phase;}}`;
const click = async button => {
  for (const type of ['mousePressed', 'mouseReleased'])
    await s('Input.dispatchMouseEvent', {type, x: width / 2, y: height / 2, button, clickCount: 1});
};
const f = v => v == null ? '   -  ' : v.toFixed(2).padStart(6);
const print = (name, r) => {
  console.log(`\n== ${name} @ ${width}x${height}: PRESENTED ${r.presentedFps.toFixed(1)} fps (loop ${r.loopFps.toFixed(1)})  cpu p50/p95 ${f(r.cpuP50)} ${f(r.cpuP95)} ms  gpu p50/p95 ${f(r.gpuP50)} ${f(r.gpuP95)} ms  bodies ${r.bodies}`);
  if (!r.passes) return;
  const total = r.passes.rows.reduce((a, x) => a + x.msPerFrame, 0);
  console.log(`   timed passes ${total.toFixed(2)} ms/presented frame; copies ${r.passes.copiesPerFrame.toFixed(1)}/frame ${r.passes.copyMBPerFrame.toFixed(2)} MB/frame; uploads ${r.passes.uploadsPerFrame.toFixed(1)}/frame ${r.passes.uploadMBPerFrame.toFixed(2)} MB/frame`);
  for (const x of r.passes.rows.slice(0, 14))
    console.log(`   ${x.msPerFrame.toFixed(3).padStart(7)} ms  x${x.perFrame.toFixed(2)}  draws ${x.drawsPerPass.toFixed(0).padStart(5)}  ${x.pass.slice(0, 150)}`);
};
for (const scenario of scenarios) {
  if (scenario === 'idle') print('idle', await measure(8000));
  else if (scenario === 'walk') print('walk', await measure(15000, walk));
  else if (scenario === 'throw') {
    await click('left');
    if ((await state()).mode === 'build') await evaluate(`voxyModule._adventure_action(1,0)`);
    await new Promise(r => setTimeout(r, 500));
    const throws = (async () => { for (let i = 0; i < 5; i++) { await click('right'); await new Promise(r => setTimeout(r, 1500)); } })();
    print('throw 5x100 bricks', await measure(10000));
    await throws;
  } else if (scenario === 'cannon') {
    await evaluate(`voxyModule._adventure_action(33,0)`);
    await new Promise(r => setTimeout(r, 3000));
    print('cannon impact', await measure(12000, `async(t,s)=>{if(t>=0&&(s.n||0)<2&&t>(s.n||0)*6000){voxyModule._adventure_action(34,0);s.n=(s.n||0)+1;}}`));
    console.log(`   status: ${(await state()).status}`);
  }
}
server.close(); await finish(0);
