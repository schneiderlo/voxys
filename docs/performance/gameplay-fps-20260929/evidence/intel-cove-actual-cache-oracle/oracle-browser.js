// Actual Cove background/cache-copy whole-fragment correctness. No FPS timing.
const digest=async bytes=>[...new Uint8Array(await crypto.subtle.digest('SHA-256',bytes))].map(x=>x.toString(16).padStart(2,'0')).join('');
const fetchBytes=async name=>{const response=await fetch(name);if(!response.ok)throw Error(`Missing fixture ${name}: ${response.status}`);return response.arrayBuffer();};
const camel=name=>name.replace(/_([a-z])/g,(_,letter)=>letter.toUpperCase());
async function compile(device,code,label){const module=device.createShaderModule({code,label});const info=await module.getCompilationInfo();if(info.messages.some(x=>x.type==='error'))throw Error(JSON.stringify(info.messages));return module;}
async function readTexture(device,source,size,bpp){
    const [width,height,layers]=size,rowBytes=width*bpp,pitch=Math.ceil(rowBytes/256)*256;
    const buffer=device.createBuffer({size:pitch*height*layers,usage:GPUBufferUsage.COPY_DST|GPUBufferUsage.MAP_READ});
    const encoder=device.createCommandEncoder();encoder.copyTextureToBuffer(source,{buffer,bytesPerRow:pitch,rowsPerImage:height},size);device.queue.submit([encoder.finish()]);
    await buffer.mapAsync(GPUMapMode.READ);const mapped=new Uint8Array(buffer.getMappedRange()),actual=new Uint8Array(rowBytes*height*layers);
    for(let row=0;row<height*layers;++row)actual.set(mapped.subarray(row*pitch,row*pitch+rowBytes),row*rowBytes);
    buffer.unmap();buffer.destroy();return actual;
}
function concatenate(outputs){const size=outputs.reduce((sum,bytes)=>sum+bytes.byteLength,0);const raw=new Uint8Array(size);let offset=0;for(const bytes of outputs){raw.set(bytes,offset);offset+=bytes.byteLength;}return raw;}
function componentStatistics(actual,targets){
    const view=new DataView(actual.buffer,actual.byteOffset,actual.byteLength);let offset=0,writtenColorComponents=0,writtenDepthComponents=0,nonFiniteComponents=0;
    for(const target of targets){
        const words=target.size[0]*target.size[1]*target.size[2]*(target.format==='r32float'?1:4),half=target.format==='rgba16float';let written=0;
        for(let word=0;word<words;++word){const bits=half?view.getUint16(offset,true):view.getUint32(offset,true);offset+=half?2:4;
            if(bits!==(half?0x3400:0x3e800000))++written;
            if(half?(bits&0x7c00)===0x7c00:(bits&0x7f800000)===0x7f800000)++nonFiniteComponents;
        }
        if(target.format==='r32float')writtenDepthComponents+=written;else writtenColorComponents+=written;
    }
    if(offset!==actual.byteLength)throw Error('Component accounting differs from readback size');
    return {writtenColorComponents,writtenDepthComponents,nonFiniteComponents};
}
async function preserve(render,actual){
    const sha256=await digest(actual);const response=await fetch(`/results/render-${render.id}.bin`,{method:'POST',body:actual});
    if(!response.ok)throw Error('Could not save exact image output');const saved=await response.json();if(saved.sha256!==sha256)throw Error('Saved image differs from GPU readback');return sha256;
}
async function replay(device,fixture,render,cache){
    const refs=new Map(),keys=new Map(),owned=[],outputs=[];
    const resolve=value=>{
        if(Array.isArray(value))return value.map(resolve);
        if(value&&typeof value==='object'){
            if(value.$ref){if(!refs.has(value.$ref))throw Error('Unknown GPU reference '+value.$ref);return refs.get(value.$ref);}
            return Object.fromEntries(Object.entries(value).filter(([,v])=>v!==null).map(([key,v])=>[camel(key),resolve(v)]));
        }return value;
    };
    const semanticKey=value=>{
        if(Array.isArray(value))return value.map(semanticKey);
        if(value&&typeof value==='object')return value.$ref?keys.get(value.$ref):Object.fromEntries(Object.entries(value).map(([key,v])=>[key,semanticKey(v)]));
        return value;
    };
    const blob=async value=>{
        const name=value.$blob;if(!cache.blobs.has(name)){
            const data=await fetchBytes(name);if(await digest(data)!==fixture.blobs[name].sha256||data.byteLength!==fixture.blobs[name].bytes)throw Error('Fixture blob identity differs '+name);
            cache.blobs.set(name,data);
        }return cache.blobs.get(name);
    };
    device.pushErrorScope('validation');
    let current;
    try{
        for(const operation of render.operations){
            current=operation;const descriptor=operation.descriptor?resolve(operation.descriptor):{};let result;
            switch(operation.op){
                case 'createBuffer':{
                    result=device.createBuffer({...descriptor,label:`oracle-${render.id}-${operation.id}`});owned.push(result);
                    const data=await blob(operation.data);new Uint8Array(result.getMappedRange()).set(new Uint8Array(data));result.unmap();break;
                }
                case 'createTexture':result=device.createTexture({...descriptor,label:`oracle-${render.id}-${operation.id}`});owned.push(result);break;
                case 'createTextureView':result=resolve(operation.texture).createView(descriptor);break;
                case 'createSampler':result=device.createSampler(descriptor);break;
                case 'writeTexture':device.queue.writeTexture(resolve(operation.destination),await blob(operation.data),resolve(operation.layout),operation.size);break;
                case 'createShaderModule':{
                    const key=operation.code.$blob;keys.set(operation.id,key);
                    if(!cache.modules.has(key))cache.modules.set(key,await compile(device,new TextDecoder().decode(await blob(operation.code)),key));result=cache.modules.get(key);break;
                }
                case 'createRenderPipeline':{
                    const key=JSON.stringify(semanticKey(operation.descriptor));keys.set(operation.id,key);
                    if(!cache.pipelines.has(key))cache.pipelines.set(key,await device.createRenderPipelineAsync(descriptor));result=cache.pipelines.get(key);break;
                }
                case 'getBindGroupLayout':result=resolve(operation.pipeline).getBindGroupLayout(operation.index);break;
                case 'createBindGroup':result=device.createBindGroup(descriptor);break;
                case 'createCommandEncoder':result=device.createCommandEncoder(descriptor);break;
                case 'beginRenderPass':result=resolve(operation.encoder).beginRenderPass(descriptor);break;
                case 'setPipeline':resolve(operation.pass_).setPipeline(resolve(operation.pipeline));break;
                case 'setBindGroup':resolve(operation.pass_).setBindGroup(operation.index,resolve(operation.group));break;
                case 'setScissorRect':resolve(operation.pass_).setScissorRect(...operation.arguments);break;
                case 'copyTextureToTexture':resolve(operation.encoder).copyTextureToTexture(resolve(operation.source),resolve(operation.destination),operation.size);break;
                case 'draw':resolve(operation.pass_).draw(...operation.arguments);break;
                case 'endRenderPass':resolve(operation.pass_).end();break;
                case 'finishEncoder':result=resolve(operation.encoder).finish();break;
                case 'submit':device.queue.submit(resolve(operation.commands));break;
                case 'readTexture':outputs.push(await readTexture(device,resolve(operation.source),operation.size,operation.bytesPerPixel));break;
                default:throw Error('Unhandled recorded operation '+operation.op);
            }
            if(operation.id)refs.set(operation.id,result);
        }
        await device.queue.onSubmittedWorkDone();const validation=await device.popErrorScope();if(validation)throw Error(validation.message);
    }catch(error){
        try{const validation=await device.popErrorScope();if(validation)error=Error(String(error)+'; '+validation.message);}catch{}
        throw Error(`Render ${render.id} ${render.shader}/${render.entry}/${render.case.name}, operation ${current?.op}: ${error}`);
    }finally{for(const resource of owned)resource.destroy();}
    const actual=concatenate(outputs);if(actual.byteLength!==render.expectedBytes)throw Error('Unexpected image byte count');
    const targets=render.operations.filter(x=>x.op==='readTexture').map(x=>({format:x.format,size:x.size,bytesPerPixel:x.bytesPerPixel}));
    const statistics=componentStatistics(actual,targets);
    if(render.case.name!=='disabled'&&statistics.writtenColorComponents===0)throw Error('Active fixture did not write color: '+render.id);
    return {bytes:actual,sha256:await preserve(render,actual),targets,...statistics};
}
window.oracle=(async()=>{
    const [raw,summaryRaw]=await Promise.all([fetchBytes('fixtures.json'),fetchBytes('export-result.json')]);
    const summary=JSON.parse(new TextDecoder().decode(summaryRaw));if(await digest(raw)!==summary.fixtureSha256)throw Error('Fixture JSON identity differs');
    const fixture=JSON.parse(new TextDecoder().decode(raw));
    const adapter=await navigator.gpu.requestAdapter({powerPreference:'low-power'});if(!adapter)throw Error('No hardware WebGPU adapter');
    const info=adapter.info,adapterInfo={vendor:info.vendor,architecture:info.architecture,device:info.device,description:info.description,isFallbackAdapter:adapter.isFallbackAdapter??info.isFallbackAdapter??null};
    if(adapterInfo.isFallbackAdapter||/swiftshader|llvmpipe|lavapipe|software|microsoft basic/i.test(JSON.stringify(adapterInfo)))throw Error('Software adapter is invalid');
    if(!/intel/i.test(JSON.stringify(adapterInfo)))throw Error('Expected Intel hardware adapter');
    const device=await adapter.requestDevice();const errors=[],lost=[];
    device.addEventListener('uncapturederror',event=>errors.push(event.error.message));device.lost.then(info=>lost.push({reason:info.reason,message:info.message}));
    const cache={blobs:new Map(),modules:new Map(),pipelines:new Map()},actuals=new Map(),renders=[];
    for(const render of fixture.renders){
        const actual=await replay(device,fixture,render,cache);actuals.set(render.id,actual);
        renders.push({id:render.id,kind:render.kind,shader:render.shader,entry:render.entry,case:render.case.name,bytes:actual.bytes.byteLength,sha256:actual.sha256,targets:actual.targets,writtenColorComponents:actual.writtenColorComponents,writtenDepthComponents:actual.writtenDepthComponents,nonFiniteComponents:actual.nonFiniteComponents,uploadSequenceSha256:render.uploadSequenceSha256});
        if(render.id%8===0)console.log('Image renders checked',render.id+1,'of',fixture.renders.length);
    }
    const comparisons=fixture.comparisons.map(comparison=>{
        const before=actuals.get(comparison.baselineRender),after=actuals.get(comparison.candidateRender);let differentBytes=0;const firstDifferences=[];const beforeOffset=fixture.renders[comparison.baselineRender].compareByteOffset,afterOffset=fixture.renders[comparison.candidateRender].compareByteOffset;if(beforeOffset!==afterOffset)throw Error('Live comparison offsets differ');
        if(before.bytes.byteLength!==after.bytes.byteLength)throw Error('Baseline and candidate sizes differ');
        for(let i=beforeOffset;i<before.bytes.byteLength;++i)if(before.bytes[i]!==after.bytes[i]){++differentBytes;if(firstDifferences.length<8)firstDifferences.push({offset:i,baseline:before.bytes[i],candidate:after.bytes[i]});}
        return {...comparison,bytes:before.bytes.byteLength-beforeOffset,compareByteOffset:beforeOffset,baselineSha256:before.sha256,candidateSha256:after.sha256,differentBytes,firstDifferences};
    });
    const stageControls=fixture.comparisons.map(pair=>{
        const before=actuals.get(pair.baselineRender),after=actuals.get(pair.candidateRender);
        const render=fixture.renders[pair.candidateRender];
        const colorBytes=16*16*8,cache=after.bytes.subarray(0,colorBytes),live=after.bytes.subarray(render.compareByteOffset,render.compareByteOffset+colorBytes);
        const different=(a,b)=>{let count=0;for(let i=0;i<a.length;++i)count+=a[i]!==b[i];return count;};
        const cacheToLiveDifferentBytes=different(cache,live);
        const newToLegacyCacheDifferentBytes=different(before.bytes.subarray(0,colorBytes),cache);
        const required=render.case.name==='unshadowed-lego';
        const accepted=!required||(render.actualCache&&cacheToLiveDifferentBytes===0&&newToLegacyCacheDifferentBytes>0);
        return {render:render.id,case:render.case.name,actualCache:render.actualCache,cacheToLiveDifferentBytes,newToLegacyCacheDifferentBytes,required,accepted};
    });
    await device.queue.onSubmittedWorkDone();const report={adapter:adapterInfo,features:[...device.features].sort(),fixtureSha256:summary.fixtureSha256,renderCount:renders.length,comparisons,stageControls,renders,errors,lost,compiledModules:cache.modules.size,compiledPipelines:cache.pipelines.size,
        ok:comparisons.every(x=>x.differentBytes===0)&&stageControls.every(x=>x.accepted)&&renders.every(x=>x.nonFiniteComponents===0)&&errors.length===0&&lost.length===0};
    console.log(JSON.stringify({adapter:report.adapter,renderCount:report.renderCount,comparisons:comparisons.length,failedStageControls:stageControls.filter(x=>!x.accepted),differences:comparisons.filter(x=>x.differentBytes),errors,lost,ok:report.ok}));return report;
})();
