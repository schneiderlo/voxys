// Optional browser work instrumentation; restore before clean timing.
export function installGameplayCounters() {
        const counts=globalThis.voxyWorkCounts={};const restore=[];
        globalThis.voxyStopWorkCounts=()=>{for(const undo of restore.reverse())undo();};
        const add=(key,n=1)=>counts[key]=(counts[key]||0)+n;
        const wrap=(type,name,observe)=>{const original=type.prototype[name];restore.push(()=>{type.prototype[name]=original;});type.prototype[name]=function(...args){observe.call(this,...args);return original.apply(this,args);};};
        const pipelines=new WeakMap();
        wrap(GPUComputePassEncoder,'setPipeline',function(p){pipelines.set(this,p.label);});
        wrap(GPUComputePassEncoder,'dispatchWorkgroups',function(x,y=1,z=1){add('dispatch:'+pipelines.get(this));add('groups:'+pipelines.get(this),x*y*z);});
        wrap(GPUComputePassEncoder,'dispatchWorkgroupsIndirect',function(){add('indirect:'+pipelines.get(this));});
        wrap(GPUCommandEncoder,'beginRenderPass',function(d){add('renderPass:'+d.label);});
        // Emscripten may omit query-set labels; resolve-buffer labels survive.
        wrap(GPUCommandEncoder,'resolveQuerySet',function(q,first,count,destination){add('resolve:'+q.label);add('resolveInto:'+destination.label);});
        wrap(GPUBuffer,'mapAsync',function(){add('map:'+this.label);});
        wrap(GPUBuffer,'getMappedRange',function(offset=0,size){add('mappedBytes:'+this.label,size===undefined?this.size-offset:size);});
        wrap(GPUCommandEncoder,'copyBufferToBuffer',function(a,b,c,d,size){add('copyBytes:'+c.label,size);});
        wrap(GPUQueue,'writeBuffer',function(buffer,offset,data,dataOffset=0,size){add('write:'+buffer.label);add('writeBytes:'+buffer.label,size===undefined?data.byteLength-dataOffset*(data.BYTES_PER_ELEMENT||1):size*(data.BYTES_PER_ELEMENT||1));});
        wrap(GPURenderPassEncoder,'drawIndexed',function(indices,instances=1){add('drawIndexed');add('indices',indices*instances);});
}
