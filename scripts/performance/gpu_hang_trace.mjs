// Local diagnostic instrumentation. Extra queue fences can affect scheduling;
// use the same instrumentation for controls, then confirm without it.
export function installGpuHangTrace(options = {}) {
    const trace = globalThis.voxyGpuTrace = {submitted:0,completed:0,records:[],devices:[],lost:[],errors:[],options};
    const encoders=new WeakMap(),passes=new WeakMap(),commands=new WeakMap();
    const skip=options.skip?new RegExp(options.skip):null;
    const wrap=(type,name,fn)=>{const original=type.prototype[name];type.prototype[name]=function(...args){return fn.call(this,original,args);};};
    wrap(GPUAdapter,'requestDevice',async function(original,args){
        const device=await original.apply(this,args);
        trace.devices.push({features:[...device.features],adapter:{vendor:this.info.vendor,architecture:this.info.architecture}});
        device.lost.then(info=>trace.lost.push({reason:info.reason,message:info.message,at:performance.now(),submitted:trace.submitted,completed:trace.completed}));
        device.addEventListener('uncapturederror',event=>trace.errors.push(event.error.message));
        return device;
    });
    wrap(GPUDevice,'createCommandEncoder',function(original,args){
        const encoder=original.apply(this,args);
        encoders.set(encoder,{label:args[0]?.label||'',operations:{},order:[]});return encoder;
    });
    const add=(encoder,key,groups)=>{
        if(!encoder)return;
        if(!encoder.operations[key]){encoder.operations[key]={count:0};encoder.order.push(key);}
        const op=encoder.operations[key];op.count++;
        if(groups)op.maxGroups=Math.max(op.maxGroups||0,groups);
    };
    for(const [name,type] of [['beginComputePass','compute'],['beginRenderPass','render']]){
        wrap(GPUCommandEncoder,name,function(original,args){
            const pass=original.apply(this,args);passes.set(pass,{encoder:encoders.get(this),type,label:args[0]?.label||'',pipeline:''});return pass;
        });
    }
    for(const type of [GPUComputePassEncoder,GPURenderPassEncoder])wrap(type,'setPipeline',function(original,args){
        const pass=passes.get(this);if(pass)pass.pipeline=args[0].label;
        return original.apply(this,args);
    });
    for(const [type,names] of [[GPUComputePassEncoder,['dispatchWorkgroups','dispatchWorkgroupsIndirect']],[GPURenderPassEncoder,['draw','drawIndexed','drawIndirect','drawIndexedIndirect']]]){
        for(const name of names)wrap(type,name,function(original,args){
            const pass=passes.get(this),key=pass?`${pass.type}:${pass.label}:${pass.pipeline}:${name}`:name;
            const omitted=skip?.test(key);
            add(pass?.encoder,(omitted?'SKIPPED:':'')+key,name==='dispatchWorkgroups'?args[0]*(args[1]??1)*(args[2]??1):undefined);
            if(!omitted)return original.apply(this,args);
        });
    }
    for(const name of ['copyBufferToBuffer','copyTextureToBuffer','copyBufferToTexture','copyTextureToTexture','resolveQuerySet','clearBuffer']){
        wrap(GPUCommandEncoder,name,function(original,args){add(encoders.get(this),name);return original.apply(this,args);});
    }
    wrap(GPUCommandEncoder,'finish',function(original,args){const command=original.apply(this,args);commands.set(command,encoders.get(this));return command;});
    wrap(GPUQueue,'submit',function(original,args){
        const record={id:++trace.submitted,start:performance.now(),commands:Array.from(args[0],command=>commands.get(command)||{unknown:true})};
        trace.records.push(record);
        if(trace.records.length>256){
            const completed=trace.records.findIndex(item=>item.end!==undefined);
            // Preserve the first pending submissions if a stalled queue fills
            // the trace, as well as the most recent submissions.
            trace.records.splice(completed>=0?completed:128,1);
        }
        const result=original.apply(this,args);
        this.onSubmittedWorkDone().then(()=>{
            // Dawn can settle outstanding completion promises after loss. That
            // is not evidence that the queued GPU commands executed correctly.
            if(trace.lost.length){record.settledAfterLoss=performance.now();return;}
            record.end=performance.now();trace.completed=Math.max(trace.completed,record.id);
        },error=>{record.error=String(error);});
        return result;
    });
}

// Diagnostic only: turn each completed pass into its own command buffer and
// submit separately. Must be installed AFTER installGpuHangTrace, so each real
// encoder and each segment receives its own completion record. This changes
// scheduling; a pass is not a production-performance result.
export function splitGpuPasses() {
    const batches=new WeakMap();
    const create=GPUDevice.prototype.createCommandEncoder;
    const submit=GPUQueue.prototype.submit;
    GPUDevice.prototype.createCommandEncoder=function(descriptor){
        const device=this;
        let active=create.call(device,descriptor);
        const segments=[];
        const overrides=new Map();
        return new Proxy(active,{
            get(target,key){
                if(overrides.has(key))return overrides.get(key);
                if(key==='finish')return descriptor=>{
                    const last=active.finish(descriptor);segments.push(last);
                    batches.set(last,segments);return last;
                };
                if(key==='beginComputePass'||key==='beginRenderPass')return descriptor=>{
                    const pass=active[key](descriptor),end=pass.end.bind(pass);
                    Object.defineProperty(pass,'end',{value:()=>{
                        end();segments.push(active.finish());
                        active=create.call(device,{label:'diagnostic pass continuation'});
                    }});
                    return pass;
                };
                const value=active[key];return typeof value==='function'?value.bind(active):value;
            },
            // The page wraps begin*Pass on individual encoders for timestamp
            // compatibility. Keep that wrapper on the facade; writing it onto
            // the underlying encoder would make its forwarding call recurse.
            set(target,key,value){
                if(key==='label')active.label=value;else overrides.set(key,value);
                return true;
            }
        });
    };
    GPUQueue.prototype.submit=function(commands){
        for(const command of commands){
            const segments=batches.get(command)||[command];
            for(const segment of segments)submit.call(this,[segment]);
        }
    };
}
