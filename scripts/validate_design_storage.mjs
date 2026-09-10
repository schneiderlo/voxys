// Real IndexedDB transactions in an isolated database. Deliberate storage
// faults are separate from the actual gameplay-input journey.
export async function validateDesignStorage(call) {
    const response=await call('Runtime.evaluate',{awaitPromise:true,returnByValue:true,expression:`(async()=>{
        const assert=(value,message)=>{if(!value)throw Error(message);};
        const database='voxys-blueprints-test-'+crypto.randomUUID();let failAfter=0;
        const nativePut=IDBObjectStore.prototype.put;
        IDBObjectStore.prototype.put=function(...args){
            if(this.transaction.db.name===database)assert(this.transaction.durability==='strict','writes must request strict durability');
            if(this.transaction.db.name===database&&failAfter&&!--failAfter)throw new DOMException('Injected quota failure','QuotaExceededError');
            return nativePut.apply(this,args);
        };
        const env={indexedDB:{open:(_name,version)=>indexedDB.open(database,version)}};
        const valid='00'.repeat(48),store=VoxyDesignLibrary.createStore(env,hex=>hex===valid),other=VoxyDesignLibrary.createStore(env,hex=>hex===valid);
        const checks=[];
        const reject=async(work,message)=>{let rejected=false;try{await work();}catch{rejected=true;}assert(rejected,message);};
        try {
            const first=await store.write('test-id','First',valid);assert(first.revision==='1','first revision');
            const changed=await store.write('test-id','Second',valid,'1');assert(changed.revision==='2','update revision');checks.push('successful transaction');
            failAfter=2;await reject(()=>store.write('test-id','Must not save',valid,'2'),'quota must reject after backup put');
            assert((await store.read('test-id')).name==='Second','failed transaction must keep current design');
            const restored=await store.write('test-id','','','2',true);assert(restored.name==='First','failed transaction must also keep original backup');checks.push('quota rollback keeps current and backup');
            await reject(()=>other.write('test-id','Stale tab',valid,'2'),'stale revision must reject');assert((await store.read('test-id')).revision==='3','stale writer cannot advance revision');checks.push('two-tab revision conflict');
            await reject(()=>store.write('test-id','Bad bytes','ff'.repeat(48),'3'),'invalid bytes must reject');assert((await store.read('test-id')).name==='First','invalid write preserves prior record');checks.push('validation before write');
            await reject(()=>store.write('second-id','First',valid),'duplicate name rejects');assert((await store.list()).length===1,'duplicate name adds no row');checks.push('duplicate name keeps library');
            for(let i=1;i<32;++i)await store.write('entry-'+i,'Design '+i,valid);
            await reject(()=>store.write('overflow','Overflow',valid),'library bound');assert((await store.list()).length===32,'no over-capacity row');checks.push('32 design cap');
            // Corrupt an existing stored payload, as can happen independently of
            // the UI. The current row must reject while its last good backup can
            // still be restored through the normal storage API.
            const db=await new Promise((resolve,reject)=>{const r=indexedDB.open(database,1);r.onsuccess=()=>resolve(r.result);r.onerror=()=>reject(r.error);});
            await new Promise((resolve,reject)=>{const tx=db.transaction('designs','readwrite',{durability:'strict'});tx.objectStore('designs').put({id:'test-id',name:'Broken',blueprint:'bad',revision:'3'});tx.oncomplete=resolve;tx.onabort=()=>reject(tx.error);});db.close();
            await reject(()=>store.read('test-id'),'stored corruption refuses load');const recovered=await store.write('test-id','','','3',true);assert(recovered.name==='Second','normal restore uses checked backup');checks.push('stored corruption and backup recovery');
            await store.remove('entry-1','1');assert((await store.list()).length===31,'explicit removal frees capacity');checks.push('explicit removal');
            return {status:'passed',checks,kind:'Isolated real IndexedDB; injected quota exception after backup write, no game mutation'};
        }finally {
            failAfter=0;IDBObjectStore.prototype.put=nativePut;store.close();other.close();
            await new Promise((resolve,reject)=>{const r=indexedDB.deleteDatabase(database);r.onsuccess=resolve;r.onerror=()=>reject(r.error);r.onblocked=()=>reject(Error('test database cleanup blocked'));});
        }
    })()`});
    if(response.exceptionDetails)throw Error(JSON.stringify(response.exceptionDetails));return response.result.value;
}
