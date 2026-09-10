// Exercises the shipping entry point, not an extracted geometry helper. A
// helper-only dispatch missed the browser failure in the full contact solver.
export async function runAuthoredTerrainRegression(shader) {
    const check=(condition,message)=>{if(!condition)throw Error(message);};
    const adapter=await navigator.gpu.requestAdapter();
    check(adapter,'No WebGPU adapter');
    check(!adapter.info.isFallbackAdapter,'Hardware adapter required');
    const device=await adapter.requestDevice();
    const errors=[];
    device.addEventListener('uncapturederror',event=>errors.push(event.error.message));
    const report={adapter:{vendor:adapter.info.vendor,architecture:adapter.info.architecture,
        device:adapter.info.device,description:adapter.info.description},cases:[]};
    const block=rows=>{
        const bytes=new ArrayBuffer(rows*16);
        return {bytes,u:new Uint32Array(bytes),i:new Int32Array(bytes),f:new Float32Array(bytes)};
    };
    const row=(data,type,index,values)=>data[type].set(values,index*4);
    // Format-1 authored heap: one 420 kg cove cargo box, six signed exterior
    // faces, one BVH leaf. Root dimensions are 1.8 x 1.28 x 1.6 metres. The
    // principal frame is rotated 90 degrees about X, as in the compiled asset.
    const heap=block(53),q=[Math.SQRT1_2,0,0,Math.SQRT1_2];
    const inertia=[.006805313751101494,.005856721196323633,.004926108289510012];
    row(heap,'u',0,[1,3,30,33]);row(heap,'u',1,[51,53,1,6]);
    row(heap,'u',23,[1,1,0,1]);row(heap,'u',24,[0,6,0,1]);
    row(heap,'f',25,[0,0,0,1/420]);row(heap,'f',26,q);
    row(heap,'f',27,[...inertia,1.363874197]);
    const minimum=[-45,-32,-40],maximum=[45,32,40];
    row(heap,'i',28,[...minimum,0]);row(heap,'i',29,[...maximum,0]);
    row(heap,'i',30,[...minimum,1]);row(heap,'i',31,[...maximum,0]);
    row(heap,'u',32,[6,0,0,0]);
    for(let axis=0;axis<3;axis++)for(let side=0;side<2;side++){
        const low=[...minimum],high=[...maximum],face=2*axis+side;
        low[axis]=high[axis]=side?maximum[axis]:minimum[axis];
        row(heap,'i',33+3*face,[...low,1]);row(heap,'i',34+3*face,[...high,0]);
        row(heap,'i',35+3*face,[axis,side?1:-1,0,0]);
    }
    row(heap,'i',51,[...minimum,0]);row(heap,'i',52,[...maximum,1]);
    const raw=21627;
    const owned=[];
    const upload=(bytes,uniform=false)=>{
        const buffer=device.createBuffer({size:bytes.byteLength,usage:GPUBufferUsage.COPY_DST|
            GPUBufferUsage.COPY_SRC|(uniform?GPUBufferUsage.UNIFORM:GPUBufferUsage.STORAGE)});
        device.queue.writeBuffer(buffer,0,bytes);owned.push(buffer);return buffer;
    };
    try {
        const module=device.createShaderModule({code:shader});
        const pipeline=await device.createComputePipelineAsync({layout:'auto',compute:{module,entryPoint:'solve_static_contacts'}});
        const shapeHeap=upload(heap.bytes);
        const field=device.createTexture({size:[256,256],format:'r16uint',
            usage:GPUTextureUsage.TEXTURE_BINDING|GPUTextureUsage.COPY_DST});
        owned.push(field);
        device.queue.writeTexture({texture:field},new Uint16Array(256*256).fill(raw),{bytesPerRow:512},[256,256]);
        const cases=[
            {name:'recorded-cove-cargo-on-studs',y:-203.025,count:1,terrain:2,touching:true},
            {name:'penetrating-cargo-corrects-upward',y:-203.06,count:1,terrain:2,touching:true,corrects:true},
            {name:'cargo-above-studs-keeps-falling',y:-202.9,count:1,terrain:2,touching:false},
            {name:'129-active-bodies-cross-workgroup-boundary',y:-203.025,count:129,terrain:2,touching:true},
            {name:'smooth-heightfield-still-supports-cargo',y:600*(2*raw/65535-1)+.64-.005,count:1,terrain:1,touching:true},
        ];
        for(const sample of cases){
            const capacity=sample.count+3;
            const sim=block(13),poses=block(capacity*2),motions=block(capacity*2),shapes=block(capacity*4),metadata=block(capacity);
            row(sim,'f',0,[0,-9.81,0,1/60]);row(sim,'f',1,[.05,.05,500,100]);
            row(sim,'u',2,[capacity,0,1,4]);row(sim,'u',3,[1,3,capacity,0]);
            row(sim,'f',4,[127.5,127.5,1,600]);row(sim,'u',5,[256,256,1,sample.terrain]);
            row(sim,'f',6,[-200,1,1.08,.55]);row(sim,'f',7,[.08,.360555142,.38,.005]);
            row(sim,'f',8,[.02,.2,.05,.5]);row(sim,'f',9,[.360555142,.38,.2,0]);
            row(sim,'f',10,[.65,.55,.25,.01]);row(sim,'f',11,[.1,.13333334,1949,326]);
            const ids=new Uint32Array(Math.max(4,sample.count));
            for(let body=3;body<capacity;body++){
                // Non-identity active-list order; metadata is a negative Y sector.
                ids[capacity-1-body]=body;
                row(poses,'f',2*body,[-23.5,sample.y+256,-95,1/420]);
                row(poses,'f',2*body+1,[q[0],3.0938262368662706e-11,3.0938262368662706e-11,q[3]]);
                row(motions,'f',2*body,[0,-.2,0,0]);
                row(shapes,'f',4*body,[2.727748394,2.727748394,2.727748394,2]);
                row(shapes,'f',4*body+1,[...inertia,0]);row(shapes,'f',4*body+2,[.65,.25,.01,1]);
                row(shapes,'u',4*body+3,[3,1,1,0]);row(metadata,'i',body,[0,-1,0,0x300001]);
            }
            const counts=new Uint32Array(32);counts[0]=sample.count;
            const resources={0:upload(poses.bytes),1:upload(motions.bytes),2:upload(shapes.bytes),
                3:upload(metadata.bytes),6:upload(counts.buffer),8:upload(sim.bytes,true),
                9:upload(ids.buffer),15:upload(new ArrayBuffer(capacity*48)),18:shapeHeap};
            const entries=Object.entries(resources).map(([binding,buffer])=>({binding:Number(binding),resource:{buffer}}));
            entries.push({binding:14,resource:field.createView()});
            const group=device.createBindGroup({layout:pipeline.getBindGroupLayout(0),entries});
            const sizes=[capacity*32,capacity*32,128,capacity*48,capacity*16];
            const bindings=[0,1,6,15,3],offsets=[];let size=0;
            for(const bytes of sizes){offsets.push(size);size+=bytes;}
            const read=device.createBuffer({size,usage:GPUBufferUsage.COPY_DST|GPUBufferUsage.MAP_READ});owned.push(read);
            const encoder=device.createCommandEncoder(),pass=encoder.beginComputePass();
            pass.setPipeline(pipeline);pass.setBindGroup(0,group);pass.dispatchWorkgroups(Math.ceil(sample.count/128));pass.end();
            bindings.forEach((binding,index)=>encoder.copyBufferToBuffer(resources[binding],0,read,offsets[index],sizes[index]));
            device.queue.submit([encoder.finish()]);await read.mapAsync(GPUMapMode.READ);
            const bytes=read.getMappedRange().slice(0);read.unmap();
            const floats=new Float32Array(bytes),words=new Uint32Array(bytes);
            const counters=Array.from(words.slice(offsets[2]/4,offsets[2]/4+24));
            const observation={name:sample.name,bodies:sample.count,counters,motions:[],cache:[]};
            report.cases.push(observation);
            check(counters[16]===0,`${sample.name}: incomplete terrain`);
            check(counters[2]===(sample.touching?sample.count:0),`${sample.name}: supported-body count ${counters[2]}`);
            check(counters[3]===(sample.touching?4*sample.count:0),`${sample.name}: contact count ${counters[3]}`);
            if(sample.terrain===2&&sample.touching)check(counters[19]>0,`${sample.name}: reservoir did not overflow`);
            for(let body=3;body<capacity;body++){
                const motion=Array.from(floats.slice(offsets[1]/4+8*body,offsets[1]/4+8*body+8));
                const cache=Array.from(words.slice(offsets[3]/4+12*body,offsets[3]/4+12*body+12));
                if(body===3){observation.motions.push(motion);observation.cache.push(cache);}
                check(motion.every(Number.isFinite),`${sample.name}: nonfinite motion`);
                check(cache[8]===1&&cache[9]===(sample.touching?4:0)&&cache[10]===0,`${sample.name}: incomplete cache publication for body ${body}`);
                check(sample.touching?motion[1]>-.1:Math.abs(motion[1]+.2)<1e-6,`${sample.name}: incorrect support force`);
                const y=floats[8*body+1];
                check(y>=poses.f[8*body+1]-1e-5,`${sample.name}: correction pushed cargo downward`);
                if(sample.corrects)check(y>poses.f[8*body+1],`${sample.name}: no penetration correction`);
                // Contact resolution updates terrain-contact flags; the
                // generation and alive bit must survive that legitimate change.
                check((words[offsets[4]/4+body*4+3]&0x1fffff)===0x100001,`${sample.name}: body identity changed`);
            }
            check(words.slice(0,24).every(value=>value===0),'Inactive pose slots changed');
            observation.status='passed';
            for(const [binding,buffer] of Object.entries(resources))if(binding!=='18')buffer.destroy();
            read.destroy();
        }
        // A support-only assertion would allow keeping the first 16 crowded
        // contacts forever. Also exercise coverage, deepest retention and
        // duplicate replacement using the same shipping reservoir routine.
        const reservoirModule=device.createShaderModule({code:shader+`
@group(0) @binding(19) var<storage,read_write> reservoirCheck: array<vec4<u32>>;
@compute @workgroup_size(1)
fn check_reservoir() {
    var result: AuthoredTerrainResult;
    for(var i=0u;i<16u;i++) {
        authored_terrain_append(&result,AuthoredTerrainPoint(
            vec3<f32>(f32(i)*.125,0,0),-.125,vec3<f32>(0,1,0),i));
    }
    authored_terrain_append(&result,AuthoredTerrainPoint(vec3<f32>(128,0,0),-.25,vec3<f32>(0,1,0),100u));
    authored_terrain_append(&result,AuthoredTerrainPoint(vec3<f32>(0,0,128),-.25,vec3<f32>(0,1,0),101u));
    authored_terrain_append(&result,AuthoredTerrainPoint(vec3<f32>(-128,-2,0),-2.0,vec3<f32>(0,1,0),300u));
    authored_terrain_append(&result,AuthoredTerrainPoint(vec3<f32>(128,0,0),-.75,vec3<f32>(0,1,0),200u));
    authored_terrain_append(&result,AuthoredTerrainPoint(vec3<f32>(128,0,0),-.125,vec3<f32>(0,1,0),5u));
    reservoirCheck[0]=vec4<u32>(result.count,result.reductions,result.status,result.cells);
    for(var i=0u;i<result.count;i++) {
        reservoirCheck[1u+2u*i]=bitcast<vec4<u32>>(vec4<f32>(result.items[i].point,result.items[i].separation));
        reservoirCheck[2u+2u*i]=vec4<u32>(bitcast<vec3<u32>>(result.items[i].normal),result.items[i].feature);
    }
}`});
        const reservoirPipeline=await device.createComputePipelineAsync({layout:'auto',compute:{module:reservoirModule,entryPoint:'check_reservoir'}});
        const output=upload(new ArrayBuffer(33*16));
        const group=device.createBindGroup({layout:reservoirPipeline.getBindGroupLayout(0),entries:[{binding:19,resource:{buffer:output}}]});
        const read=device.createBuffer({size:33*16,usage:GPUBufferUsage.COPY_DST|GPUBufferUsage.MAP_READ});owned.push(read);
        const encoder=device.createCommandEncoder(),pass=encoder.beginComputePass();
        pass.setPipeline(reservoirPipeline);pass.setBindGroup(0,group);pass.dispatchWorkgroups(1);pass.end();
        encoder.copyBufferToBuffer(output,0,read,0,33*16);device.queue.submit([encoder.finish()]);await read.mapAsync(GPUMapMode.READ);
        const bytes=read.getMappedRange().slice(0);read.unmap();
        const words=new Uint32Array(bytes),floats=new Float32Array(bytes),points=[];
        const observation={name:'reservoir-preserves-coverage-deepest-and-stronger-duplicate',header:Array.from(words.slice(0,4)),points};
        report.cases.push(observation);
        check(words[0]===16&&words[1]===3&&words[2]===0,'Reservoir did not complete three bounded replacements');
        for(let i=0;i<16;i++){
            const point={position:Array.from(floats.slice(4+8*i,7+8*i)),separation:floats[7+8*i],feature:words[11+8*i]};
            check(point.position.every(Number.isFinite)&&Number.isFinite(point.separation),'Nonfinite reservoir point');points.push(point);
        }
        check(points.some(point=>point.feature===300&&point.separation===-2),'Deepest contact lost');
        check(points.some(point=>point.position[2]===128),'Distant support patch lost');
        const duplicate=points.filter(point=>point.position[0]===128);
        check(duplicate.length===1&&duplicate[0].feature===200&&duplicate[0].separation===-.75,'Duplicate failed to retain the stronger contact');
        observation.status='passed';
        check(errors.length===0,errors.join('\n'));
        report.status='passed';return report;
    }catch(error){report.status='failed';report.error=String(error);report.gpuErrors=errors;return report;}
    finally{for(const resource of owned)resource.destroy();device.destroy();}
}
