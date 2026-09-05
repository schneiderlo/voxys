#!/usr/bin/env node
// Full 256x256 x 2-cascade ocean update, not a full rendered frame.
// Requires Node 22+, Chrome WebGPU and the pinned reference Git history.
import assert from 'node:assert/strict';
import {readFile, mkdtemp, rm, writeFile} from 'node:fs/promises';
import {tmpdir} from 'node:os';
import path from 'node:path';
import http from 'node:http';
import {spawn, execFileSync} from 'node:child_process';
import {fileURLToPath} from 'node:url';
const root=path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const reference=process.env.VOXY_FFT_REFERENCE || '077c1e97d9889de8584206b7cce6e24dd7e7972a';
const old = p=>execFileSync('git',['show',`${reference}:${p}`],{cwd:root,encoding:'utf8',maxBuffer:8*1024*1024});
const payload={before:old('shaders/water_fft.wgsl'),finalBefore:old('shaders/water_finalize.wgsl'),
 after:await readFile(path.join(root,'shaders/water_fft.wgsl'),'utf8'),
 finalAfter:await readFile(path.join(root,'shaders/water_finalize.wgsl'),'utf8'),reference};
async function gpuTest({before,after,finalBefore,finalAfter,reference}){
 if(!navigator.gpu)throw Error('WebGPU unavailable');
 const adapter=await navigator.gpu.requestAdapter();if(!adapter)throw Error('No adapter');
 const timestamps=adapter.features.has('timestamp-query');
 const device=await adapter.requestDevice({requiredFeatures:timestamps?['timestamp-query']:[]});
 let lost;device.lost.then(x=>{if(x.reason!=='destroyed')lost=x.message;});
 const errors=[];device.addEventListener('uncapturederror',e=>errors.push(e.error.message));
 const B=GPUBufferUsage,T=GPUTextureUsage,S=GPUShaderStage;
 const n=256,count=n*n*2,bytes=count*32;
 const report={reference,kind:'full-resolution ocean simulation; NOT full-frame rendering',
  dimensions:[n,n],cascades:2,adapter:{vendor:adapter.info.vendor,architecture:adapter.info.architecture,
  description:adapter.info.description,fallback:adapter.info.isFallbackAdapter},checks:[],timings:[]};
 const buffer=(size,usage)=>device.createBuffer({size,usage});
 async function module(label,code){const m=device.createShaderModule({label,code});
  const info=await m.getCompilationInfo();const bad=info.messages.filter(x=>x.type==='error');
  if(bad.length)throw Error(label+': '+bad.map(x=>x.message).join('; '));return m;}
 const [m0,m1,f0,f1]=await Promise.all([module('baseline FFT',before),module('fused FFT',after),
  module('baseline finalize',finalBefore),module('candidate finalize',finalAfter)]);
 const layout=device.createBindGroupLayout({entries:[{binding:0,visibility:S.COMPUTE,buffer:{type:'uniform'}},
  {binding:1,visibility:S.COMPUTE,buffer:{type:'read-only-storage'}},
  {binding:2,visibility:S.COMPUTE,buffer:{type:'storage'}},
  {binding:3,visibility:S.COMPUTE,buffer:{type:'read-only-storage'}}]});
 const pl=device.createPipelineLayout({bindGroupLayouts:[layout]});
 const pipe=async(m,entryPoint)=>device.createComputePipelineAsync({layout:pl,compute:{module:m,entryPoint}});
 const [evolve,axis,rows,columns]=await Promise.all([pipe(m0,'evolve'),pipe(m0,'fftAxis'),
  pipe(m1,'evolveRows'),pipe(m1,'fftColumns')]);
 const fl=device.createBindGroupLayout({entries:[{binding:0,visibility:S.COMPUTE,buffer:{type:'read-only-storage'}},
  {binding:1,visibility:S.COMPUTE,storageTexture:{format:'rgba16float',access:'write-only',viewDimension:'2d-array'}},
  {binding:2,visibility:S.COMPUTE,buffer:{type:'uniform'}}]});
 const fp=device.createPipelineLayout({bindGroupLayouts:[fl]});
 const [finish0,finish1]=await Promise.all([f0,f1].map(module=>device.createComputePipelineAsync({
  layout:fp,compute:{module,entryPoint:'main'}})));
 const spectrum=buffer(bytes,B.STORAGE|B.COPY_DST),twiddle=buffer(255*8,B.STORAGE|B.COPY_DST);
 const live=buffer(48,B.UNIFORM|B.COPY_DST),ax0=buffer(48,B.UNIFORM|B.COPY_DST),ax1=buffer(48,B.UNIFORM|B.COPY_DST);
 const tw=new Float32Array(255*2);
 for(let stage=0;stage<8;++stage){const half=1<<stage;for(let j=0;j<half;++j){
  tw[(half-1+j)*2]=Math.cos(2*Math.PI*j/(half*2));tw[(half-1+j)*2+1]=Math.sin(2*Math.PI*j/(half*2));}}
 device.queue.writeBuffer(twiddle,0,tw);
 const params=(time,sine,axis=0)=>{const a=new ArrayBuffer(48),u=new Uint32Array(a),f=new Float32Array(a);
  f[0]=time;u[2]=axis;u[3]=n;f[4]=1949;f[5]=326;f[6]=.08;f[7]=.05;f[8]=.9;f[9]=sine;return a;};
 device.queue.writeBuffer(ax0,0,params(0,0,0));device.queue.writeBuffer(ax1,0,params(0,0,1));
 const outputs=[0,1].map(()=>buffer(bytes,B.STORAGE|B.COPY_SRC|B.COPY_DST));
 const textures=[0,1].map(()=>device.createTexture({size:[n,n,4],format:'rgba16float',usage:T.STORAGE_BINDING|T.COPY_SRC}));
 const group=(out,uniform)=>device.createBindGroup({layout,entries:[{binding:0,resource:{buffer:uniform}},
  {binding:1,resource:{buffer:spectrum}},{binding:2,resource:{buffer:out}},{binding:3,resource:{buffer:twiddle}}]});
 const groups=outputs.map(o=>[group(o,live),group(o,ax0),group(o,ax1)]);
 const finishGroups=outputs.map((o,i)=>device.createBindGroup({layout:fl,entries:[{binding:0,resource:{buffer:o}},
  {binding:1,resource:textures[i].createView({dimension:'2d-array'})},{binding:2,resource:{buffer:live}}]}));
 const dispatch=(pass,pipeline,bind,x,y=1,z=1)=>{pass.setPipeline(pipeline);pass.setBindGroup(0,bind);pass.dispatchWorkgroups(x,y,z);};
 function encode(encoder,index,fused,candidateFinalize,stopRows=false,query=null){
  const pass=encoder.beginComputePass({label:fused?'fused ocean':'baseline ocean',...(query?{
   timestampWrites:{querySet:query,beginningOfPassWriteIndex:index*2,endOfPassWriteIndex:index*2+1}}:{})});
  if(fused)dispatch(pass,rows,groups[index][0],n*2);
  else{dispatch(pass,evolve,groups[index][0],count/256);dispatch(pass,axis,groups[index][1],n*2);}
  if(!stopRows){dispatch(pass,fused?columns:axis,groups[index][2],n*2);
   dispatch(pass,candidateFinalize?finish1:finish0,finishGroups[index],n/8,n/8,2);}
  pass.end();
 }
 const stateReads=[0,1].map(()=>buffer(bytes,B.COPY_DST|B.MAP_READ));
 const texBytes=n*n*4*8,texReads=[0,1].map(()=>buffer(texBytes,B.COPY_DST|B.MAP_READ));
 async function readPair(buffers,Constructor){const data=[];for(const b of buffers){await b.mapAsync(GPUMapMode.READ);
  data.push(new Constructor(b.getMappedRange().slice(0)));b.unmap();}return data;}
 function compare(a,b,name,tolerance){let maximum=0,different=0,worst=0;
  for(let i=0;i<a.length;++i){if(!Number.isFinite(a[i])||!Number.isFinite(b[i]))throw Error(name+': nonfinite '+i);
   const d=Math.abs(a[i]-b[i]);if(d>maximum){maximum=d;worst=i;}if(d!==0)++different;
   if(d>tolerance*(1+Math.abs(a[i])))throw Error(`${name}: index ${i}: ${a[i]} != ${b[i]}`);}
  report.checks.push({name,words:a.length,different,maximum_absolute_error:maximum,worst_index:worst});}
 let rng=0x5eeda11;const random=()=>{rng^=rng<<13;rng^=rng>>>17;rng^=rng<<5;return (rng>>>0)/4294967296;};
 const initial=new Float32Array(count*8);
 for(const kind of ['zero','impulse','random','nyquist']){
  initial.fill(0);
  if(kind==='random')for(let i=0;i<count;++i){const a=i*8;for(let j=0;j<4;++j)initial[a+j]=(random()-.5)*.003;
   const angle=random()*Math.PI*2;initial[a+4]=Math.cos(angle);initial[a+5]=Math.sin(angle);initial[a+6]=random()*13;}
  if(kind==='impulse'||kind==='nyquist')for(let c=0;c<2;++c){const a=(c*n*n+(kind==='impulse'?17*n+3:n*n-1))*8;
   initial[a]=.1;initial[a+1]=-.04;initial[a+2]=.03;initial[a+3]=.02;initial[a+4]=.6;initial[a+5]=.8;initial[a+6]=2.3;}
  device.queue.writeBuffer(spectrum,0,initial);
  for(const [time,sine] of [[0,0],[3.125,1.5],[4095.875,.35]]){
   device.queue.writeBuffer(live,0,params(time,sine));
   for(const stopRows of [true,false]){
    const encoder=device.createCommandEncoder();encode(encoder,0,false,false,stopRows);encode(encoder,1,true,true,stopRows);
    for(let i=0;i<2;++i){encoder.copyBufferToBuffer(outputs[i],0,stateReads[i],0,bytes);
     if(!stopRows)encoder.copyTextureToBuffer({texture:textures[i]},
      {buffer:texReads[i],bytesPerRow:n*8,rowsPerImage:n},[n,n,4]);}
    device.queue.submit([encoder.finish()]);
    const [a,b]=await readPair(stateReads,Float32Array);
    compare(a,b,`${kind} t=${time} sine=${sine} ${stopRows?'rows':'2D FFT'}`,3e-6);
    if(!stopRows){const [ta,tb]=await readPair(texReads,Uint16Array);
     // Exact half-float output: includes all displacement, normal, and compression layers.
     compare(ta,tb,`${kind} t=${time} final texture bits`,0);}
   }
  }
 }
 // Nonzero evolving workload, not repeated zero input or a skipped 120Hz update.
 for(let i=0;i<count;++i){const a=i*8;for(let j=0;j<4;++j)initial[a+j]=(random()-.5)*.003;
  initial[a+4]=.6;initial[a+5]=.8;initial[a+6]=random()*13;}
 device.queue.writeBuffer(spectrum,0,initial);
 const median=a=>{const b=[...a].sort((x,y)=>x-y);return (b[(b.length-1)>>1]+b[b.length>>1])/2;};
 if(timestamps){
  const query=device.createQuerySet({type:'timestamp',count:4}),resolved=buffer(32,B.QUERY_RESOLVE|B.COPY_SRC),read=buffer(32,B.COPY_DST|B.MAP_READ);
  for(const [name,fused,candidateFinalize] of [['production fused FFT',true,true]]){
   const samples=[];
   for(let trial=0;trial<16;++trial){device.queue.writeBuffer(live,0,params(1+trial/120,.35));
    const enc=device.createCommandEncoder();
    for(const i of (trial%2?[1,0]:[0,1]))encode(enc,i,i?fused:false,i?candidateFinalize:false,false,query);
    enc.resolveQuerySet(query,0,4,resolved,0);enc.copyBufferToBuffer(resolved,0,read,0,32);device.queue.submit([enc.finish()]);
    await read.mapAsync(GPUMapMode.READ);const t=new BigUint64Array(read.getMappedRange().slice(0));read.unmap();
    if(trial>=4)samples.push({baseline_ms:Number(t[1]-t[0])/1e6,candidate_ms:Number(t[3]-t[2])/1e6});
   }
   const base=median(samples.map(x=>x.baseline_ms)),candidate=median(samples.map(x=>x.candidate_ms));
   report.timings.push({name,baseline_median_ms:base,candidate_median_ms:candidate,
    percent_time_reduction:100*(1-candidate/base),samples});
  }
  query.destroy();resolved.destroy();read.destroy();
 }
 await device.queue.onSubmittedWorkDone();if(lost||errors.length)throw Error(lost||errors.join('; '));
 report.status='passed';device.destroy();return report;
}

const directory = await mkdtemp(path.join(tmpdir(), 'voxys-fft-test-'));
const server = http.createServer((request, response) => {
    response.setHeader('Content-Type', 'text/html');
    response.end('<!doctype html><title>Voxys ocean FFT correctness</title>');
});
await new Promise(resolve => server.listen(0, '127.0.0.1', resolve));
const chrome = spawn(process.env.VOXY_TEST_CHROME || 'google-chrome', [
    '--headless=new', '--no-sandbox', '--enable-unsafe-webgpu',
    '--no-first-run', '--no-default-browser-check', '--disable-background-networking',
    '--enable-unsafe-swiftshader', '--use-angle=swiftshader',
    '--remote-debugging-port=0', `--user-data-dir=${directory}`, 'about:blank',
], {stdio: ['ignore', 'ignore', 'pipe']});
let chromeError = null, chromeLog = '';
chrome.on('error', error => {chromeError = error;});
chrome.stderr.on('data', data => {chromeLog = (chromeLog + data).slice(-10000);});
let socket;
const watchdog = setTimeout(() => {chrome.kill('SIGKILL');}, 180000);
try {
    let port;
    for (let i = 0; i < 1200 && !port; ++i) {
        if (chromeError) throw chromeError;
        if (chrome.exitCode !== null) throw new Error(`Chrome exited: ${chromeLog}`);
        try {port = Number((await readFile(path.join(directory, 'DevToolsActivePort'), 'utf8')).split('\n')[0]);}
        catch {await new Promise(resolve => setTimeout(resolve, 50));}
    }
    if (!port) throw new Error(`Chrome debugging unavailable: ${chromeLog}`);
    const target = await (await fetch(`http://127.0.0.1:${port}/json/new?http://127.0.0.1:${server.address().port}/`, {method: 'PUT'})).json();
    socket = new WebSocket(target.webSocketDebuggerUrl);
    const waiting = new Map(); let sequence = 0;
    socket.addEventListener('message', event => {
        const message = JSON.parse(event.data);
        const call = waiting.get(message.id);
        if (call) {waiting.delete(message.id); message.error ? call.reject(new Error(JSON.stringify(message.error))) : call.resolve(message.result);}
    });
    socket.addEventListener('close', () => {
        for (const call of waiting.values()) call.reject(new Error('Chrome debugging closed'));
        waiting.clear();
    });
    await new Promise((resolve, reject) => {socket.addEventListener('open', resolve, {once: true}); socket.addEventListener('error', reject, {once: true});});
    const command = (method, params = {}) => new Promise((resolve, reject) => {
        const id = ++sequence; waiting.set(id, {resolve, reject});
        socket.send(JSON.stringify({id, method, params}));
    });
    await command('Runtime.enable');
    let ready = false;
    for (let i = 0; i < 100 && !ready; ++i) {
        const result = await command('Runtime.evaluate', {expression: 'isSecureContext && !!navigator.gpu', returnByValue: true});
        ready = result.result?.value === true;
        if (!ready) await new Promise(resolve => setTimeout(resolve, 50));
    }
    if (!ready) throw new Error('Secure WebGPU page unavailable (not a pass)');
    const result = await command('Runtime.evaluate', {
        expression: `(${gpuTest.toString()})(${JSON.stringify(payload)})`,
        awaitPromise: true, returnByValue: true,
    });
    if (result.exceptionDetails) throw new Error(JSON.stringify(result.exceptionDetails));
    const report = result.result.value;
    assert.equal(report.status, 'passed');
    await writeFile(path.resolve(process.env.VOXY_FFT_REPORT || 'water-fft-fusion-report.json'), JSON.stringify(report, null, 2) + '\n');
    console.log(JSON.stringify(report, null, 2));
} finally {
    clearTimeout(watchdog);
    socket?.close();
    // The profile is still being written until Chrome and its stdio close.
    // Waiting first avoids ENOTEMPTY after otherwise successful GPU tests.
    if (chrome.exitCode === null && chrome.signalCode === null && !chromeError) {
        const closed = new Promise(resolve => chrome.once('close', resolve));
        chrome.kill('SIGKILL');
        await closed;
    }
    await new Promise(resolve => server.close(resolve));
    await rm(directory, {recursive: true, force: true, maxRetries: 8, retryDelay: 100});
}
