// Isolated real IndexedDB/Web Locks fixtures. No GameSession or game commands.
(function(){
    const api=VoxyExpeditionStore,world='73746f72652d666978747572652d3031';
    const assert=(value,message)=>{if(!value)throw Error(message);};
    const rejects=async(work,code)=>{try{await work();}catch(e){assert(e.code===code,'expected '+code+', got '+e.code+': '+e.message);return;}throw Error('Expected rejection: '+code);};
    const openDb=name=>new Promise((resolve,reject)=>{const r=indexedDB.open(name,1);r.onsuccess=()=>resolve(r.result);r.onerror=()=>reject(r.error);});
    async function raw(name,work){const db=await openDb(name);try{return await new Promise((resolve,reject)=>{
        const tx=db.transaction(['current','mirror'],'readwrite',{durability:'strict'});let result;
        tx.oncomplete=()=>resolve(result);tx.onabort=()=>reject(tx.error);work(tx,value=>{result=value;});
    });}finally{db.close();}}
    const row=async(generation,payload)=>({version:1,generation:String(generation),bytes:await api.encodeGeneration(world,generation,payload)});
    const both=(database,value)=>raw(database,tx=>{tx.objectStore('current').put(value,world);tx.objectStore('mirror').put(value,world);});
    const removeDatabase=name=>new Promise((resolve,reject)=>{const r=indexedDB.deleteDatabase(name);r.onsuccess=resolve;r.onerror=()=>reject(r.error);r.onblocked=()=>reject(Error('Database cleanup blocked'));});
    globalThis.runExpeditionStorageCases=async database=>{
        const checks=[];let store,failPut=0,abortNext=false,closeNext=false,closeResult,gate=null,completed=0,strictWrites=0;
        const nativePut=IDBObjectStore.prototype.put,nativeTransaction=IDBDatabase.prototype.transaction;
        IDBObjectStore.prototype.put=function(...args){
            if(this.transaction.db.name===database&&failPut&&!--failPut)throw new DOMException('Injected quota failure','QuotaExceededError');
            return nativePut.apply(this,args);
        };
        IDBDatabase.prototype.transaction=function(...args){
            const tx=nativeTransaction.apply(this,args);
            if(this.name===database&&args[1]==='readwrite'){
                assert(tx.durability==='strict','strict transaction durability');strictWrites++;tx.addEventListener('complete',()=>completed++);
                if(abortNext){abortNext=false;queueMicrotask(()=>tx.abort());}
                if(closeNext){closeNext=false;queueMicrotask(()=>{closeResult=store.close();});}
            }
            return tx;
        };
        const validate=async data=>{
            if(gate&&data[1]===44){gate.reached();await gate.wait;}
            return data[0]===1&&data.length<=256;
        };
        try{
            store=await api.openStore(world,validate,{databaseName:database});
            assert((await store.load()).generation===0n,'empty slot');
            const before=completed;assert((await store.publish(0n,new Uint8Array([1,10]))).generation===1n,'initial generation');
            assert(completed===before+1,'acknowledgment follows transaction complete');
            let loaded=await store.load();assert(loaded.payload[1]===10&&!loaded.needsRepair,'identical copies');checks.push('strict atomic publication acknowledged after complete');
            await rejects(()=>api.openStore(world,validate,{databaseName:database}),'Busy');checks.push('same-world exclusive Web Lock');
            const input=new Uint8Array([1,11]),pending=store.publish(1n,input);input.fill(0);
            await rejects(()=>store.publish(1n,new Uint8Array([1,12])),'Busy');await pending;
            assert((await store.load()).payload[1]===11,'pending write owns input');checks.push('one pending operation and owned payload');
            await rejects(()=>store.publish(1n,new Uint8Array([1,12])),'Conflict');
            await rejects(()=>store.publish(2n,new Uint8Array([0,12])),'InvalidData');assert((await store.load()).generation===2n,'invalid requests preserve generation');checks.push('stale requests and invalid archives preserve save');
            failPut=2;const oldComplete=completed;await rejects(()=>store.publish(2n,new Uint8Array([1,13])),'NoSpace');
            loaded=await store.load();assert(loaded.generation===2n&&loaded.payload[1]===11&&!loaded.needsRepair,'quota rollback restores both copies');
            assert(completed===oldComplete,'aborted save is not acknowledged');checks.push('quota after first put rolls back both copies');
            abortNext=true;await rejects(()=>store.publish(2n,new Uint8Array([1,14])),'Interrupted');assert((await store.load()).generation===2n,'aborted transaction preserved');checks.push('actual IndexedDB abort preserves prior generation');
            await raw(database,tx=>{const request=tx.objectStore('current').get(world);request.onsuccess=()=>{const value=request.result;value.bytes[40]^=255;tx.objectStore('current').put(value,world);};});
            loaded=await store.load();assert(loaded.generation===2n&&loaded.payload[1]===11&&loaded.needsRepair,'corrupt current recovers checked mirror');
            await store.publish(2n,new Uint8Array([1,15]));assert(!(await store.load()).needsRepair,'repair publishes both copies');checks.push('payload corruption and redundant-copy repair');
            await raw(database,tx=>tx.objectStore('mirror').delete(world));assert((await store.load()).needsRepair,'missing copy flagged');
            await store.publish(3n,new Uint8Array([1,16]));checks.push('missing copy recovery');
            const conflicting=await row(4n,new Uint8Array([1,99]));await raw(database,tx=>tx.objectStore('mirror').put(conflicting,world));
            await rejects(()=>store.load(),'Conflict');await rejects(()=>store.publish(4n,new Uint8Array([1,17])),'Conflict');
            await both(database,await row(4n,new Uint8Array([1,16])));checks.push('matching generation with differing payload refuses');
            const future=await row(4n,new Uint8Array([1,16]));future.bytes[4]=2;await raw(database,tx=>tx.objectStore('current').put(future,world));
            await rejects(()=>store.load(),'UnsupportedSchema');await both(database,await row(4n,new Uint8Array([1,16])));checks.push('unknown format never silently falls back');
            const broken=await row(4n,new Uint8Array([1,16]));broken.bytes=broken.bytes.slice(0,40);await both(database,broken);
            await rejects(()=>store.load(),'InvalidData');await rejects(()=>store.publish(0n,new Uint8Array([1,17])),'InvalidData');
            await both(database,await row(4n,new Uint8Array([1,16])));checks.push('two damaged copies cannot become a new world');
            const malformed=await row(4n,new Uint8Array([1,16]));malformed.generation=4;
            await raw(database,tx=>tx.objectStore('current').put(malformed,world));
            await rejects(()=>store.load(),'InvalidData');await both(database,await row(4n,new Uint8Array([1,16])));
            checks.push('malformed record metadata refuses before repair');
            await both(database,undefined); // IndexedDB permits this value while the key still exists.
            await rejects(()=>store.load(),'InvalidData');await rejects(()=>store.publish(0n,new Uint8Array([1,17])),'InvalidData');
            await both(database,await row(4n,new Uint8Array([1,16])));checks.push('present undefined records are damaged, never empty worlds');
            let reached,release;const entered=new Promise(resolve=>{reached=resolve;});gate={reached,wait:new Promise(resolve=>{release=resolve;}),release:()=>release()};
            const racing=store.publish(4n,new Uint8Array([1,44]));await entered;
            await both(database,await row(5n,new Uint8Array([1,45])));release();await rejects(()=>racing,'Conflict');gate=null;
            assert((await store.load()).payload[1]===45,'CAS preserves external change');checks.push('transaction rechecks bytes after async validation');
            closeNext=true;await rejects(()=>store.publish(5n,new Uint8Array([1,46])),'Interrupted');await closeResult;
            await rejects(()=>store.load(),'Closed');store=await api.openStore(world,validate,{databaseName:database});
            assert((await store.load()).generation===5n,'close drained abort before new owner');checks.push('close aborts and drains before releasing ownership');
            for(let i=1;i<8;i++){
                const otherWorld=world.slice(0,-2)+(32+i).toString(16);const other=await api.openStore(otherWorld,validate,{databaseName:database});
                try{await other.publish(0n,new Uint8Array([1,i]));}finally{await other.close();}
            }
            const overflow=await api.openStore(world.slice(0,-2)+'40',validate,{databaseName:database});
            try{await rejects(()=>overflow.publish(0n,new Uint8Array([1,0])),'Capacity');}finally{await overflow.close();}
            checks.push('eight-world bound counts both stores');
            const maximum=18446744073709551615n;await both(database,await row(maximum-1n,new Uint8Array([1,90])));
            assert((await store.publish(maximum-1n,new Uint8Array([1,91]))).generation===maximum,'lossless 64-bit generation');
            await rejects(()=>store.publish(maximum,new Uint8Array([1,92])),'Capacity');assert((await store.load()).generation===maximum,'overflow preserved');checks.push('full-width generation and exhaustion');
            await navigator.locks.request(database+':world:'+world,{mode:'exclusive',steal:true},async()=>{
                for(let wait=0;wait<100&&!store.closed;wait++)await new Promise(resolve=>setTimeout(resolve,1));
                assert(store.closed,'revoked owner must close');await store.close();
            });
            await rejects(()=>store.publish(maximum,new Uint8Array([1,92])),'Closed');
            store=await api.openStore(world,validate,{databaseName:database});assert((await store.load()).generation===maximum,'new owner keeps saved data');
            checks.push('revoked Web Lock closes and fences old owner');
            await new Promise((resolve,reject)=>{const request=indexedDB.open(database,2);request.onsuccess=()=>{request.result.close();resolve();};request.onerror=()=>reject(request.error);});
            assert(store.closed,'version change closes old owner');
            await rejects(()=>api.openStore(world,validate,{databaseName:database}),'UnsupportedSchema');
            checks.push('database upgrade closes owner and rejects older version');
            return {status:'passed',checks,strictWrites,kind:'Isolated real IndexedDB/Web Locks; synthetic archive validator and deliberate storage faults; no game mutation'};
        }finally{
            gate?.release?.();gate=null;failPut=0;abortNext=false;closeNext=false;IDBObjectStore.prototype.put=nativePut;IDBDatabase.prototype.transaction=nativeTransaction;
            await store?.close();await removeDatabase(database);
        }
    };
    globalThis.openJourneyStore=async database=>{
        globalThis.journeyStore=await api.openStore(world,data=>data[0]===1,{databaseName:database});
        const state=await journeyStore.load();return {generation:String(state.generation),payload:Array.from(state.payload),needsRepair:state.needsRepair};
    };
    // Debugger pauses are restricted to this test page. The runner kills its
    // private browser at the pause, before the transaction or after its commit.
    globalThis.installSaveCrashCut=(database,point)=>{
        if(point==='first-put'){
            const original=IDBObjectStore.prototype.put;let seen=0;
            IDBObjectStore.prototype.put=function(...args){const request=original.apply(this,args);
                if(this.transaction.db.name===database&&++seen===1){debugger;}return request;};
        }else if(point==='committed'){
            const original=IDBDatabase.prototype.transaction;
            IDBDatabase.prototype.transaction=function(...args){const tx=original.apply(this,args);
                if(this.name===database&&args[1]==='readwrite')tx.addEventListener('complete',()=>{debugger;},{once:true});return tx;};
        }else throw Error('Unknown crash cut');
    };
})();
