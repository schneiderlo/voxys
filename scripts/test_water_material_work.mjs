#!/usr/bin/env node
// Differential WebGPU execution of production functions and fragment paths.
// Software adapters prove correctness only. Timings are synthetic, NOT game FPS.
import assert from 'node:assert/strict';
import {readFile, mkdtemp, rm, writeFile} from 'node:fs/promises';
import {execFileSync, spawn} from 'node:child_process';
import {tmpdir} from 'node:os';
import path from 'node:path';
import http from 'node:http';
import {fileURLToPath} from 'node:url';
const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const reference = process.env.VOXY_REFERENCE_REF || 'd9a6a399a9cafdb986be992413de8322ac879b8c';
assert(/^(?:[a-f0-9]{7,40}|HEAD)$/.test(reference), 'reference must be a commit or HEAD');
const paths = ['shaders/ray_blit.wgsl','shaders/water_clipmap.wgsl'];
const before = paths.map(p => execFileSync('git',['show',`${reference}:${p}`],{cwd:root,encoding:'utf8'}));
const after = await Promise.all(paths.map(p => readFile(path.join(root,p),'utf8')));
function fn(source, name) {
    const start=source.indexOf(`fn ${name}(`);
    assert(start>=0, `missing ${name}`);
    let depth=0;
    for (let i=source.indexOf('{',start); i<source.length; ++i) {
        if (source[i]==='{') ++depth;
        if (source[i]==='}' && --depth===0) return source.slice(start,i+1);
    }
    throw new Error(`unterminated ${name}`);
}
const caseDeclaration=`
struct Case { p : vec4<f32>, n : vec4<f32>, x : vec4<f32>, y : vec4<f32> };
@group(1) @binding(0) var<storage, read> cases : array<Case>;
@group(1) @binding(1) var<storage, read_write> result : array<vec4<f32>>;
@vertex fn fixtureVertex(@builtin(vertex_index) i : u32) -> @builtin(position) vec4<f32> {
    return vec4<f32>(f32((i << 1u) & 2u)*2.0-1.0, f32(i & 2u)*2.0-1.0,0.0,1.0);
}`;
const surfaceFixture=`${caseDeclaration}
struct SurfaceOutput { @location(0) albedoRoughness : vec4<f32>, @location(1) normalWetness : vec4<f32> };
@fragment fn surfaceFixture(@builtin(position) pixel : vec4<f32>) -> SurfaceOutput {
    let c=cases[u32(pixel.y)*64u+u32(pixel.x)];
    let s=sampleTerrainSurface(c.p.xyz,c.x.xyz,c.y.xyz,normalize(c.n.xyz));
    return SurfaceOutput(vec4<f32>(s.albedo,s.roughness), vec4<f32>(s.normal,s.wetness));
}
@fragment fn legacyWaterFixture(@builtin(position) pixel : vec4<f32>) -> @location(0) vec4<f32> {
    let index=u32(pixel.y)*64u+u32(pixel.x);
    let c=cases[index]; let delta=camera.cameraPos.xyz-c.p.xyz;
    let view=normalize(delta); let distance=length(delta);
    return vec4<f32>(shadeOcean(c.p.xyz,vec3<f32>(0.0,0.0,distance),view,
        normalize(c.n.xyz),c.x.w,1.0,c.y.w,c.n.w,vec3<f32>(0.2,0.3,0.4),
        4.0,(index & 1u)==0u,(index & 2u)==0u,vec2<u32>(64u)),1.0);
}
@compute @workgroup_size(64) fn foamFixture(@builtin(global_invocation_id) id : vec3<u32>) {
    if(id.x>=arrayLength(&cases)){return;}
    let c=cases[id.x];
    result[id.x]=vec4<f32>(coastalFoamStrength(c.p.xyz,normalize(c.n.xyz),c.x.w,c.y.w,c.n.w,30.0,vec2<u32>(64u)),0.0,0.0,1.0);
}`;
const waterFixture=`${caseDeclaration}
@fragment fn waterFixture(@builtin(position) pixel : vec4<f32>) -> FragmentOutput {
    let c=cases[u32(pixel.y)*64u+u32(pixel.x)];
    var v : VertexOutput;
    v.position=pixel; v.worldPosition=c.p.xyz; v.waterCoordinates=c.p.xz;
    v.lowFrequencyNormal=normalize(c.n.xyz); v.crestCompression=c.y.w;
    return shadeWaterFragment(v);
}
@compute @workgroup_size(64) fn foamFixture(@builtin(global_invocation_id) id : vec3<u32>) {
    if(id.x>=arrayLength(&cases)){return;}
    let c=cases[id.x];
    result[id.x]=vec4<f32>(coastalFoamStrength(c.p.xyz,normalize(c.n.xyz),c.x.w,c.y.w,30.0,0.05),0.0,0.0,1.0);
}`;
function mathProbe(source, candidate) {
    const common=['periodicGradientHash','periodicGradientNoise','terrainMaterialScale',
        'rotateTerrainMaterialUv','terrainMaterialUv','terrainMaterialWeights'];
    const extra=['periodicNoiseFromCorners','periodicNoiseFootprint','terrainMaterialUvWithNoise','terrainMaterialUvFootprint'];
    const helpers=[...common,...(candidate?extra:[])].map(n=>fn(source,n)).join('\n');
    const camera=source.slice(source.indexOf('struct CameraUniforms'),source.indexOf('// Debug visualization'));
    return `${camera}\n@group(0) @binding(0) var<uniform> camera : CameraUniforms;
const TERRAIN_LAYER_SAND : i32=0; const TERRAIN_LAYER_SOIL : i32=1;
const TERRAIN_LAYER_GRASS : i32=2; const TERRAIN_LAYER_ROCK : i32=3;
${helpers}\n${caseDeclaration}
@compute @workgroup_size(64) fn mathFixture(@builtin(global_invocation_id) id : vec3<u32>) {
    if(id.x>=arrayLength(&cases)){return;}
    let c=cases[id.x]; let layer=i32(id.x & 3u);
    let uv=${candidate?'terrainMaterialUvFootprint(c.p.xz,c.x.xz,c.y.xz,layer)':
        'mat3x2<f32>(terrainMaterialUv(c.p.xz,layer),terrainMaterialUv(c.x.xz,layer),terrainMaterialUv(c.y.xz,layer))'};
    result[id.x*3u]=vec4<f32>(uv[0],uv[1]);
    result[id.x*3u+1u]=vec4<f32>(uv[2],0.0,1.0);
    result[id.x*3u+2u]=terrainMaterialWeights(c.p.xyz,normalize(c.n.xyz));
}`;
}
const payload={reference, before, after,
    math:[mathProbe(before[0],false),mathProbe(after[0],true)],surfaceFixture,waterFixture};

async function gpuTest({reference,before,after,math,surfaceFixture,waterFixture}) {
    if(!navigator.gpu) throw new Error('WebGPU unavailable (not a pass)');
    const adapter=await navigator.gpu.requestAdapter();
    if(!adapter) throw new Error('Adapter unavailable (not a pass)');
    const timestamps=adapter.features.has('timestamp-query');
    const device=await adapter.requestDevice({requiredFeatures:timestamps?['timestamp-query']:[]});
    const errors=[]; device.addEventListener('uncapturederror',e=>errors.push(e.error.message));
    device.pushErrorScope('validation');
    const B=GPUBufferUsage,T=GPUTextureUsage,S=GPUShaderStage;
    const visibility=S.VERTEX|S.FRAGMENT|S.COMPUTE;
    const report={reference,adapter:{vendor:adapter.info.vendor,architecture:adapter.info.architecture,
        fallback:adapter.info.isFallbackAdapter},checks:[],timing_kind:'synthetic shader fixtures, not engine/display FPS'};
    const makeBuffer=(size,usage)=>device.createBuffer({size,usage});
    async function module(code,label){
        const m=device.createShaderModule({code,label});
        const info=await m.getCompilationInfo();
        const e=info.messages.filter(m=>m.type==='error');
        if(e.length) throw new Error(label+': '+e.map(m=>`${m.lineNum}: ${m.message}`).join('\n'));
        return m;
    }
    const shaders=await Promise.all([before[0]+surfaceFixture,after[0]+surfaceFixture,
        before[1]+waterFixture,after[1]+waterFixture,...math].map((s,i)=>module(s,`fixture ${i}`)));
    // Isolate the sharing tradeoff without changing any production file.
    const sharedBlock=`    let footprint = terrainMaterialUvFootprint(
        projectedWorld, projectedX, projectedY, layer);
    let uv = footprint[0];
    let uvX = footprint[1];
    let uvY = footprint[2];`;
    const independentBlock=`    let uv = terrainMaterialUv(projectedWorld, layer);
    let uvX = terrainMaterialUv(projectedX, layer);
    let uvY = terrainMaterialUv(projectedY, layer);`;
    if(!after[0].includes(sharedBlock))throw new Error('sharing variant mismatch');
    const independent=await module(after[0].replace(sharedBlock,independentBlock)+surfaceFixture,
        'independent-footprint diagnostic variant');
    // Actual production entry points, not merely front-end compilation.
    for(let i=0;i<4;++i){
        const m=shaders[i],water=i>=2;
        const vertex=water?{module:m,entryPoint:'vs',buffers:[{arrayStride:12,attributes:[
            {shaderLocation:0,offset:0,format:'float32x3'}]}]}:{module:m,entryPoint:'vs'};
        const entries=water?[['fsColor',['bgra8unorm']],['fs',['bgra8unorm','r32float']]]:
            [['fsBackground',['rgba16float']],['fsCachedOpaqueColor',['bgra8unorm']],['fs',['bgra8unorm']]];
        for(const [entryPoint,formats] of entries) await device.createRenderPipelineAsync({
            layout:'auto',vertex,fragment:{module:m,entryPoint,targets:formats.map(format=>({format}))}});
    }
    report.compiled_production_pipelines=10;
    const uniform=makeBuffer(544,B.UNIFORM|B.COPY_DST),debug=makeBuffer(16,B.UNIFORM|B.COPY_DST);
    const u=new Float32Array(136);
    for(const offset of [0,16,32]) for(const i of [0,5,10,15]) u[offset+i]=1;
    u.set([32,32,1/32,1/32],48); u.set([600,1,1,.0001],52);
    u.set([0,12,0,1],56);u.set([.57735,.57735,0,0],60);
    const light=[.317999,.847998,.423999];
    u.set([...light,.38],64); u.set([...light,0],92);
    u.set([0,1,1,.1],96);u.set([.12,.46,.5,.42],100);u.set([0,.28,.42,30],104);
    u.set([3.5,0,0,0],108);u.set([1,.95,.9,1],112);u.set([.1,.12,.15,1],116);
    u.set([.36,.58,.64,0],120);u.set([1.33,.2,1,1],124);u.set([261,.3,.21,1500],128);
    u.set([1949,326,0,0],132);device.queue.writeBuffer(uniform,0,u);
    const entries=[],resources=[];
    const sampler=device.createSampler({minFilter:'linear',magFilter:'linear',mipmapFilter:'linear',
        addressModeU:'repeat',addressModeV:'repeat',maxAnisotropy:8});
    for(let binding=0;binding<=18;++binding){
        if(binding===0||binding===7){entries.push({binding,visibility,buffer:{type:'uniform'}});
            resources.push({binding,resource:{buffer:binding===0?uniform:debug}});continue;}
        if([6,10,16].includes(binding)){entries.push({binding,visibility,sampler:{type:'filtering'}});
            resources.push({binding,resource:sampler});continue;}
        const unfiltered=[1,2,12].includes(binding),uint=[13,14].includes(binding),array=[15,17,18].includes(binding);
        const format=uint?'r32uint':unfiltered?'r32float':binding===17?'rgba8unorm-srgb':'rgba8unorm';
        const layers=array?4:1,mips=unfiltered||uint?1:6;
        const texture=device.createTexture({size:[32,32,layers],format,mipLevelCount:mips,
            usage:T.TEXTURE_BINDING|T.COPY_DST});
        for(let mip=0;mip<mips;++mip){const size=32>>mip;
            const data=uint?new Uint32Array(size*size*layers):unfiltered?new Float32Array(size*size*layers):new Uint8Array(size*size*layers*4);
            for(let l=0;l<layers;++l)for(let y=0;y<size;++y)for(let x=0;x<size;++x){const p=(l*size+y)*size+x;
                if(uint)data[p]=binding===13?32000+(x+y)*10:32000;
                else if(unfiltered)data[p]=binding===2?1:(x%3===0?-1:50);
                else if(binding===18)data.set([120+x%17,120+y%17,255,80+(x+y+mip)%100],p*4);
                else if(binding===15)data.set([0,255,0,Math.round((l%2)*.1*255)],p*4);
                else data.set([(x*17+y*13+l*37+mip*11)%256,(x*9+y*19+101)%256,(x*3+y*7+57)%256,255],p*4);
            }
            device.queue.writeTexture({texture,mipLevel:mip},data,{bytesPerRow:size*4,rowsPerImage:size},[size,size,layers]);
        }
        entries.push({binding,visibility,texture:{sampleType:uint?'uint':unfiltered?'unfilterable-float':'float',viewDimension:array?'2d-array':'2d'}});
        resources.push({binding,resource:texture.createView({dimension:array?'2d-array':'2d'})});
    }
    const layout0=device.createBindGroupLayout({entries});
    const group0=device.createBindGroup({layout:layout0,entries:resources});
    const layout1=device.createBindGroupLayout({entries:[
        {binding:0,visibility:S.COMPUTE|S.FRAGMENT,buffer:{type:'read-only-storage'}},
        {binding:1,visibility:S.COMPUTE,buffer:{type:'storage'}}]});
    const layout=device.createPipelineLayout({bindGroupLayouts:[layout0,layout1]});
    let state=0x519ba;
    const rand=()=>{state^=state<<13;state^=state>>>17;state^=state<<5;return (state>>>0)/4294967296;};
    function cases(count,water=false){
        const a=new Float32Array(count*16),depths=[.034,.035,.07,.16,.24,.48,.62,.72,.919999,.92,1.75,2.34999,2.35,20,500];
        for(let i=0;i<count;++i){
            const x=water?(i%64-32)*1.25:(i%2?-650+rand()*160-80:rand()*8192-4096);
            const z=water?(Math.floor(i/64)-32)*1.25:(i%2?3450+rand()*160-80:rand()*8192-4096);
            const y=water?0:[-.81,-.02,0,.014999,.015,.1,.26999,.3,5,10,200,450][i%12];
            const d=[0,.001,.05,.5,3,32,512][i%7];
            a.set([x,y,z,0,rand()*1.2-.6,.15+rand()*.85,rand()*1.2-.6,rand(),
                x+d,y,z,depths[i%depths.length],x,y,z+d,[0,.018,.045,.08,.20,1][i%6]],i*16);
        }
        return a;
    }
    function compare(a,b,label,tolerance=2e-5){
        if(a.length!==b.length)throw new Error(label+' size mismatch');
        let max=0,different=0;
        for(let i=0;i<a.length;++i){const e=Math.abs(a[i]-b[i]);
            if(!Number.isFinite(e))throw new Error(label+' nonfinite at '+i);
            max=Math.max(max,e);if(e!==0)++different;
            if(e>tolerance*Math.max(1,Math.abs(a[i])))throw new Error(`${label}: word ${i}, ${a[i]} vs ${b[i]}, error ${e}`);
        }
        report.checks.push({label,compared_words:a.length,maximum_absolute_error:max,different_words:different});
    }
    async function computePair(modules,entryPoint,data,wordsPerCase,label){
        const count=data.length/16,bytes=count*wordsPerCase*4;
        const input=makeBuffer(data.byteLength,B.STORAGE|B.COPY_DST);device.queue.writeBuffer(input,0,data);
        const staging=makeBuffer(bytes*2,B.MAP_READ|B.COPY_DST);const encoder=device.createCommandEncoder();
        const outputs=[];
        for(let i=0;i<2;++i){
            const output=makeBuffer(bytes,B.STORAGE|B.COPY_SRC);outputs.push(output);
            const pipeline=await device.createComputePipelineAsync({layout,compute:{module:modules[i],entryPoint}});
            const group1=device.createBindGroup({layout:layout1,entries:[{binding:0,resource:{buffer:input}},{binding:1,resource:{buffer:output}}]});
            const pass=encoder.beginComputePass();pass.setPipeline(pipeline);pass.setBindGroup(0,group0);pass.setBindGroup(1,group1);
            pass.dispatchWorkgroups(Math.ceil(count/64));pass.end();encoder.copyBufferToBuffer(output,0,staging,bytes*i,bytes);
        }
        device.queue.submit([encoder.finish()]);await staging.mapAsync(GPUMapMode.READ);
        const values=new Float32Array(staging.getMappedRange().slice(0));compare(values.subarray(0,bytes/4),values.subarray(bytes/4),label);
        staging.unmap();staging.destroy();input.destroy();outputs.forEach(b=>b.destroy());
    }
    const mathCases=cases(65539);
    // Exact lattice/wrap and large fp32 inputs also exercise reference fallback.
    for(let i=0;i<96;++i){const x=[-16777216,-1024,-16,-1,0,1,16,1024,16777216][i%9];
        mathCases[i*16]=x;mathCases[i*16+2]=x;mathCases[i*16+8]=x+.25;mathCases[i*16+14]=x-.25;}
    await computePair(shaders.slice(4,6),'mathFixture',mathCases,12,'UV footprints and material weights');
    const samples=cases(4096),wetSamples=cases(4096,true);
    await computePair(shaders.slice(0,2),'foamFixture',samples,4,'legacy foam incl. wreck contact');
    await computePair(shaders.slice(2,4),'foamFixture',wetSamples,4,'clipmap foam');
    async function fragmentPair(modules,entryPoint,formats,data,label){
        const input=makeBuffer(data.byteLength,B.STORAGE|B.COPY_DST);device.queue.writeBuffer(input,0,data);
        const unused=makeBuffer(16,B.STORAGE);
        const group1=device.createBindGroup({layout:layout1,entries:[{binding:0,resource:{buffer:input}},{binding:1,resource:{buffer:unused}}]});
        const rowBytes=formats.map(f=>f==='r32float'?256:1024),sizes=rowBytes.map(r=>r*64),total=sizes.reduce((a,b)=>a+b,0);
        const staging=makeBuffer(total*2,B.COPY_DST|B.MAP_READ);const encoder=device.createCommandEncoder();
        const textures=[],pipelines=[],targetSets=[];
        const query=timestamps?device.createQuerySet({type:'timestamp',count:4}):null;
        const resolved=timestamps?makeBuffer(32,B.QUERY_RESOLVE|B.COPY_SRC):null;
        const timeRead=timestamps?makeBuffer(32,B.MAP_READ|B.COPY_DST):null;
        for(let i=0;i<2;++i){
            const targets=formats.map(format=>{const t=device.createTexture({size:[64,64],format,usage:T.RENDER_ATTACHMENT|T.COPY_SRC});textures.push(t);return t;});
            const pipeline=await device.createRenderPipelineAsync({layout,vertex:{module:modules[i],entryPoint:'fixtureVertex'},
                fragment:{module:modules[i],entryPoint,targets:formats.map(format=>({format}))}});
            pipelines.push(pipeline);targetSets.push(targets);
            const pass=encoder.beginRenderPass({colorAttachments:targets.map(t=>({view:t.createView(),loadOp:'clear',storeOp:'store',
                clearValue:{r:.125,g:.125,b:.125,a:.125}})),...(query?{timestampWrites:{querySet:query,beginningOfPassWriteIndex:i*2,endOfPassWriteIndex:i*2+1}}:{})});
            pass.setPipeline(pipeline);pass.setBindGroup(0,group0);pass.setBindGroup(1,group1);pass.draw(3);pass.end();
            let offset=total*i;for(let k=0;k<targets.length;++k){encoder.copyTextureToBuffer({texture:targets[k]},
                {buffer:staging,offset,bytesPerRow:rowBytes[k]},[64,64]);offset+=sizes[k];}
        }
        if(query){encoder.resolveQuerySet(query,0,4,resolved,0);encoder.copyBufferToBuffer(resolved,0,timeRead,0,32);}
        device.queue.submit([encoder.finish()]);await staging.mapAsync(GPUMapMode.READ);
        const values=new Float32Array(staging.getMappedRange().slice(0));compare(values.subarray(0,total/4),values.subarray(total/4),label);
        staging.unmap();staging.destroy();
        const check=report.checks.at(-1);
        if(query){await timeRead.mapAsync(GPUMapMode.READ);
            const t=new BigUint64Array(timeRead.getMappedRange().slice(0));
            check.first_pass_gpu_ms={reference:Number(t[1]-t[0])/1e6,
                candidate:Number(t[3]-t[2])/1e6};timeRead.unmap();}
        // Cold draws may include software-driver JIT. Keep them separate from
        // warmed, alternating-order samples; neither is a game-FPS benchmark.
        const warmed=[];
        for(let repeat=0;repeat<9;++repeat){
            const batch=device.createCommandEncoder();
            const order=repeat%2?[1,0]:[0,1];
            for(const i of order){
                const pass=batch.beginRenderPass({colorAttachments:targetSets[i].map(t=>({
                    view:t.createView(),loadOp:'clear',storeOp:'store',
                    clearValue:{r:.125,g:.125,b:.125,a:.125}})),
                    ...(query?{timestampWrites:{querySet:query,
                        beginningOfPassWriteIndex:i*2,endOfPassWriteIndex:i*2+1}}:{})});
                pass.setPipeline(pipelines[i]);pass.setBindGroup(0,group0);
                pass.setBindGroup(1,group1);pass.draw(3);pass.end();
            }
            if(query){batch.resolveQuerySet(query,0,4,resolved,0);
                batch.copyBufferToBuffer(resolved,0,timeRead,0,32);}
            device.queue.submit([batch.finish()]);
            if(query){await timeRead.mapAsync(GPUMapMode.READ);
                const t=new BigUint64Array(timeRead.getMappedRange().slice(0));
                if(repeat>=3)warmed.push({reference:Number(t[1]-t[0])/1e6,
                    candidate:Number(t[3]-t[2])/1e6});
                timeRead.unmap();
            }else{await device.queue.onSubmittedWorkDone();}
        }
        check.warmed_paired_gpu_ms=warmed;
        input.destroy();unused.destroy();textures.forEach(t=>t.destroy());
        if(query){timeRead.destroy();resolved.destroy();query.destroy();}
    }
    await fragmentPair(shaders.slice(0,2),'surfaceFixture',['rgba32float','rgba32float'],samples,'terrain surface: dry/shore/subtidal/rock');
    await fragmentPair([shaders[0],independent],'surfaceFixture',['rgba32float','rgba32float'],samples,
        'diagnostic independent footprint: mixed terrain');
    for(const [kind,ox,oz,height,nx,ny,nz] of [
        ['shore',-650,3450,.1,.05,1,.02],['upland',0,-256,100,.1,1,.1],['rock',0,-256,250,.7,.3,.6]]){
        const coherent=new Float32Array(4096*16);
        for(let i=0;i<4096;++i){const x=ox+(i%64)*.08,z=oz+Math.floor(i/64)*.08;
            coherent.set([x,height,z,0,nx,ny,nz,0,x+.08,height,z,0,x,height,z+.08,0],i*16);}
        await fragmentPair(shaders.slice(0,2),'surfaceFixture',['rgba32float','rgba32float'],coherent,
            'coherent '+kind+': shared footprint');
        await fragmentPair([shaders[0],independent],'surfaceFixture',['rgba32float','rgba32float'],coherent,
            'diagnostic coherent '+kind+': independent footprint');
    }
    for(const underwater of [false,true]){
        u[57]=underwater?-12:12;u[110]=underwater?1:0;device.queue.writeBuffer(uniform,0,u);
        await fragmentPair(shaders.slice(0,2),'legacyWaterFixture',['rgba32float'],wetSamples,
            underwater?'legacy water below: refraction/TIR':'legacy water above: refraction precedence');
        await fragmentPair(shaders.slice(2,4),'waterFixture',['rgba32float','r32float'],wetSamples,
            underwater?'water below: transmission/TIR/discard':'water above: refraction/fallback/discard');
    }
    await device.queue.onSubmittedWorkDone();const validation=await device.popErrorScope();
    if(validation)throw new Error(validation.message);if(errors.length)throw new Error(errors.join('\n'));
    report.status='passed';device.destroy();return report;
}

const directory = await mkdtemp(path.join(tmpdir(), 'voxys-water-material-test-'));
const server = http.createServer((request, response) => {
    response.setHeader('Content-Type', 'text/html');
    response.end('<!doctype html><title>Voxys gradient correctness</title>');
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
const watchdog = setTimeout(() => {chrome.kill('SIGKILL');}, 300000);
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
    await writeFile(path.resolve(process.env.VOXY_WORK_REPORT || 'water-material-work-report.json'), JSON.stringify(report, null, 2) + '\n');
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
