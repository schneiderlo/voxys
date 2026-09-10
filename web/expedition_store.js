(function(root,factory){
    const api=factory();if(typeof module==='object'&&module.exports)module.exports=api;else root.VoxyExpeditionStore=api;
})(globalThis,function(){
    'use strict';
    const maximumPayloadBytes=16*1024*1024,maximumWorlds=8,maximumGeneration=18446744073709551615n;
    class StoreError extends Error {
        constructor(code,message){super(message);this.name='VoxyStoreError';this.code=code;}
    }
    const error=(code,message)=>new StoreError(code,message);
    function counter(value,zero=false){
        if(typeof value==='string'&&/^(0|[1-9][0-9]{0,19})$/.test(value))value=BigInt(value);
        if(typeof value!=='bigint'||value<(zero?0n:1n)||value>maximumGeneration)throw error('InvalidData','The save generation is invalid.');
        return value;
    }
    function worldBytes(world){
        if(typeof world!=='string'||!/^[0-9a-f]{32}$/.test(world)||/^0+$/.test(world))throw error('InvalidData','The world identity is invalid.');
        return Uint8Array.from(world.match(/../g),pair=>parseInt(pair,16));
    }
    function copyBytes(value,maximum){
        if(value instanceof ArrayBuffer)value=new Uint8Array(value);
        if(!(value instanceof Uint8Array)||!(value.buffer instanceof ArrayBuffer)||value.byteLength>maximum)
            throw error('Capacity','The save data is invalid or exceeds its size limit.');
        return value.slice();
    }
    const equal=(a,b)=>a.length===b.length&&a.every((value,index)=>value===b[index]);
    async function encodeGeneration(world,generation,payload,crypto=globalThis.crypto){
        const identity=worldBytes(world),number=counter(generation),data=copyBytes(payload,maximumPayloadBytes);
        if(!data.length)throw error('InvalidData','An empty save cannot be published.');
        const bytes=new Uint8Array(data.length+72),view=new DataView(bytes.buffer);
        bytes.set([83,86,83,71]);view.setUint32(4,1,true);bytes.set(identity,8);
        view.setBigUint64(24,number,true);view.setBigUint64(32,BigInt(data.length),true);bytes.set(data,40);
        bytes.set(new Uint8Array(await crypto.subtle.digest('SHA-256',bytes.subarray(0,bytes.length-32))),bytes.length-32);
        return bytes;
    }
    async function decodeGeneration(input,world,crypto=globalThis.crypto){
        const identity=worldBytes(world),bytes=copyBytes(input,maximumPayloadBytes+72);
        if(bytes.length<=72||!equal(bytes.subarray(0,4),new Uint8Array([83,86,83,71])))throw error('InvalidData','The save file is damaged.');
        const view=new DataView(bytes.buffer);
        if(view.getUint32(4,true)!==1)throw error('UnsupportedSchema','This save needs a different game version.');
        const generation=counter(view.getBigUint64(24,true));
        if(view.getBigUint64(32,true)!==BigInt(bytes.length-72)||!equal(bytes.subarray(8,24),identity))throw error('InvalidData','The save belongs to another world or is damaged.');
        const hash=new Uint8Array(await crypto.subtle.digest('SHA-256',bytes.subarray(0,bytes.length-32)));
        if(!equal(hash,bytes.subarray(bytes.length-32)))throw error('InvalidData','The save file is damaged.');
        return {generation,payload:bytes.slice(40,-32),needsRepair:false};
    }
    function classify(value){
        if(value instanceof StoreError)return value;
        if(value?.name==='QuotaExceededError')return error('NoSpace','Browser storage is full. The previous save is unchanged.');
        if(value?.name==='VersionError')return error('UnsupportedSchema','This save database needs a different game version.');
        if(value?.name==='AbortError')return error('Interrupted','The save transaction was interrupted.');
        if(value?.name==='SecurityError'||value?.name==='NotAllowedError')return error('Permission','Browser storage access is unavailable.');
        return error('Io',String(value?.message||value||'Browser storage failed.'));
    }
    // This deliberately requires an archive validator supplied by the host.
    // Outer checksums alone cannot attest content, ownership or physical state.
    async function openStore(world,validatePayload,options={}){
        worldBytes(world);
        if(typeof validatePayload!=='function')throw error('InvalidData','A game archive validator is required.');
        const environment=options.environment||globalThis,database=options.databaseName||'voxys-expeditions-v1';
        if(!/^[a-zA-Z0-9-]{1,96}$/.test(database))throw error('InvalidData','The save database name is invalid.');
        if(!environment.indexedDB||!environment.crypto?.subtle||!environment.navigator?.locks?.request)
            throw error('UnsupportedPlatform','This browser cannot provide protected world saves.');
        let release,db=null,lockLost=false,closing=false,closePromise=null,busy=false,active=null,activeTransaction=null;
        const held=new Promise(resolve=>{release=resolve;});let acquired,rejected;
        const acquisition=new Promise((resolve,reject)=>{acquired=resolve;rejected=reject;});
        const lockTask=environment.navigator.locks.request(database+':world:'+world,{mode:'exclusive',ifAvailable:true},lock=>{
            if(!lock){rejected(error('Busy','This world is already open in another tab.'));return;}
            acquired();return held;
        });
        lockTask.catch(reason=>{lockLost=true;rejected(reason);if(db)void close();});
        try {
            await acquisition;
            db=await new Promise((resolve,reject)=>{
                let abandoned=false;const request=environment.indexedDB.open(database,1);
                const refuse=value=>{abandoned=true;reject(classify(value));};
                request.onupgradeneeded=()=>{request.result.createObjectStore('current');request.result.createObjectStore('mirror');};
                request.onerror=()=>refuse(request.error);
                request.onblocked=()=>refuse(error('Busy','Close an older game tab before opening this save.'));
                request.onsuccess=()=>{
                    if(abandoned){request.result.close();return;}
                    if(Array.from(request.result.objectStoreNames).sort().join(',')!=='current,mirror'){
                        request.result.close();refuse(error('UnsupportedSchema','The save database layout is incompatible.'));return;
                    }
                    resolve(request.result);
                };
            });
            if(lockLost)throw error('Closed','This tab no longer owns the world save.');
        }catch(value){db?.close();release();await lockTask.catch(()=>{});throw classify(value);}
        function transaction(mode,work){
            if(closing)return Promise.reject(error('Closed','The world save is closed.'));
            return new Promise((resolve,reject)=>{
                let tx,result,failure;
                const abort=value=>{failure=classify(value);try{tx.abort();}catch{reject(failure);}};
                try {
                    tx=db.transaction(['current','mirror'],mode,{durability:mode==='readwrite'?'strict':'default'});activeTransaction=tx;
                    const clear=()=>{if(activeTransaction===tx)activeTransaction=null;};
                    tx.oncomplete=()=>{clear();resolve(result);};
                    tx.onerror=event=>{failure ||= classify(event.target.error||tx.error);};
                    tx.onabort=()=>{clear();reject(failure||classify(tx.error||new DOMException('Save interrupted','AbortError')));};
                    if(mode==='readwrite'&&tx.durability!=='strict'){abort(error('UnsupportedPlatform','This browser cannot confirm that world progress was saved.'));return;}
                    work(tx,value=>{result=value;},abort);
                }catch(value){if(tx)abort(value);else reject(classify(value));}
            });
        }
        const readRows=()=>transaction('readonly',(tx,done)=>{
            const current=tx.objectStore('current'),mirror=tx.objectStore('mirror'),rows={};
            for(const [key,request]of [['current',current.get(world)],['mirror',mirror.get(world)],
                ['currentPresent',current.count(world)],['mirrorPresent',mirror.count(world)]])
                request.onsuccess=()=>{rows[key]=key.endsWith('Present')?request.result===1:request.result;};
            done(rows);
        });
        function rowShape(row){
            if(!row||Object.keys(row).sort().join(',')!=='bytes,generation,version'||row.version!==1)
                throw error('UnsupportedSchema','The stored save record has an incompatible layout.');
            if(typeof row.generation!=='string'||!(row.bytes instanceof Uint8Array)||row.bytes.byteLength>maximumPayloadBytes+72)
                throw error('InvalidData','The saved record is damaged.');
            counter(row.generation);
        }
        async function candidate(row,present){
            if(!present)return null;
            if(row===undefined)throw error('InvalidData','The saved record is damaged.');
            rowShape(row); // Unknown/malformed record metadata is not a repairable payload.
            try {
                const value=await decodeGeneration(row.bytes,world,environment.crypto);
                if(value.generation!==BigInt(row.generation))throw error('InvalidData','The save generation does not match its record.');
                return value;
            }catch(value){if(value?.code==='InvalidData')return null;throw value;}
        }
        async function readValidated(){
            const rows=await readRows(),a=await candidate(rows.current,rows.currentPresent),b=await candidate(rows.mirror,rows.mirrorPresent);
            if(!a&&!b){
                if(rows.currentPresent||rows.mirrorPresent)throw error('InvalidData','Both save copies are damaged.');
                return {rows,value:{generation:0n,payload:new Uint8Array(),needsRepair:false}};
            }
            if(a&&b&&a.generation===b.generation&&!equal(a.payload,b.payload))throw error('Conflict','Save copies disagree about this generation.');
            const value=!a||(b&&b.generation>a.generation)?b:a;value.needsRepair=!a||!b||a.generation!==b.generation;
            if(!await validatePayload(value.payload.slice()))throw error('InvalidData','The game archive is damaged or incompatible.');
            return {rows,value};
        }
        function sameRow(a,b){
            if(a===undefined||b===undefined)return a===b;
            return Object.keys(a).sort().join(',')==='bytes,generation,version'&&Object.keys(b).sort().join(',')==='bytes,generation,version'
                &&a.version===b.version&&a.generation===b.generation&&a.bytes instanceof Uint8Array&&b.bytes instanceof Uint8Array&&equal(a.bytes,b.bytes);
        }
        function run(work){
            if(closing)return Promise.reject(error('Closed','The world save is closed.'));
            if(busy)return Promise.reject(error('Busy','A save operation is already running.'));
            busy=true;
            const pending=(async()=>{try{return await work();}catch(value){throw classify(value);}finally{busy=false;}})();
            active=pending;const clear=()=>{if(active===pending)active=null;};pending.then(clear,clear);return pending;
        }
        async function close(){
            if(closePromise)return closePromise;
            closing=true;
            closePromise=(async()=>{
                try{activeTransaction?.abort();}catch{} // Completion may already be in progress; drain its actual result.
                await active?.catch(()=>{});db.close();release();await lockTask.catch(()=>{});
            })();return closePromise;
        }
        db.onversionchange=()=>{void close();};db.onclose=()=>{void close();};
        return {
            load:()=>run(async()=>{const {value}=await readValidated();return value;}),
            publish:(expectedGeneration,payload)=>run(async()=>{
                // Snapshot before the first await: caller mutation cannot change a pending save.
                const data=copyBytes(payload,maximumPayloadBytes),expected=counter(expectedGeneration,true);
                const previous=await readValidated();
                if(previous.value.generation!==expected)throw error('Conflict','The stored world changed. Reload it before saving.');
                if(expected===maximumGeneration)throw error('Capacity','This save cannot be updated further.');
                if(!data.length||!await validatePayload(data.slice()))throw error('InvalidData','The game archive is invalid.');
                const next=expected+1n,bytes=await encodeGeneration(world,next,data,environment.crypto);
                return transaction('readwrite',(tx,done,abort)=>{
                    const current=tx.objectStore('current'),mirror=tx.objectStore('mirror');
                    const requests=[current.get(world),mirror.get(world),current.getAllKeys(undefined,maximumWorlds+1),mirror.getAllKeys(undefined,maximumWorlds+1)];
                    let received=0;
                    for(const request of requests)request.onsuccess=()=>{
                        if(++received!==requests.length)return;
                        try {
                            if(requests[2].result.includes(world)!==previous.rows.currentPresent||requests[3].result.includes(world)!==previous.rows.mirrorPresent
                                ||!sameRow(requests[0].result,previous.rows.current)||!sameRow(requests[1].result,previous.rows.mirror))throw error('Conflict','The stored world changed during save preparation.');
                            const keys=new Set([...requests[2].result,...requests[3].result]);
                            for(const key of keys)worldBytes(key);
                            if(keys.size>maximumWorlds||(!keys.has(world)&&keys.size>=maximumWorlds))throw error('Capacity','The browser world library is full (8 worlds).');
                            const row={version:1,generation:String(next),bytes};
                            current.put(row,world);mirror.put(row,world);done({generation:next});
                        }catch(value){abort(value);}
                    };
                });
            }),
            close,
            get closed(){return closing;},
            get busy(){return busy;}
        };
    }
    return {StoreError,maximumPayloadBytes,maximumWorlds,encodeGeneration,decodeGeneration,openStore};
});
