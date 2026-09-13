(function(root,factory){
    const api=factory();if(typeof module==='object'&&module.exports)module.exports=api;else root.VoxyAdventureSaves=api;
})(globalThis,function(){
    'use strict';
    const databaseName='voxys-adventure-v1',metadataKey='voxys-adventure-current-v1';
    const maximumPayloadBytes=1024*1024;
    const validWorld=value=>typeof value==='string'&&/^[0-9a-f]{32}$/.test(value)&&!/^[0]+$/.test(value);
    const equal=(a,b)=>a.length===b.length&&a.every((v,i)=>v===b[i]);
    function confirmedWorld(environment=globalThis){
        try{const value=environment.localStorage?.getItem(metadataKey);return validWorld(value)?value:null;}catch{return null;}
    }
    function randomWorld(environment){
        if(!environment.crypto?.getRandomValues)throw Error('A new adventure identity could not be created.');
        const bytes=environment.crypto.getRandomValues(new Uint8Array(16));
        if(bytes.every(v=>v===0))throw Error('A new adventure identity could not be created.');
        return Array.from(bytes,v=>v.toString(16).padStart(2,'0')).join('');
    }
    // Bootstrap integrity gate only. Runtime must later install the complete
    // content/authority validator before any archive may be published.
    async function checkArchive(input,world,environment=globalThis){
        if(!validWorld(world)||!(input instanceof Uint8Array)||input.length<288||input.length>maximumPayloadBytes)return false;
        const bytes=input.slice(),view=new DataView(bytes.buffer);
        if(!equal(bytes.subarray(0,8),new TextEncoder().encode('VXADHOME'))||view.getUint32(8,true)!==1)return false;
        if(Array.from(bytes.subarray(12,28),v=>v.toString(16).padStart(2,'0')).join('')!==world)return false;
        const digest=new Uint8Array(await environment.crypto.subtle.digest('SHA-256',bytes.subarray(0,-32)));
        return equal(digest,bytes.subarray(-32));
    }
    async function open(options={}){
        const environment=options.environment||globalThis;
        if(options.newWorld&&options.world)throw Error('Choose either a new or a saved adventure.');
        if(options.world!==undefined&&!validWorld(options.world))throw Error('The adventure world identity is invalid.');
        const selected=options.world||(!options.newWorld?confirmedWorld(environment):null);
        const world=selected||randomWorld(environment);
        const storeApi=options.storeApi||environment.VoxyExpeditionStore;
        if(!storeApi?.openStore)throw Error('Adventure storage is unavailable.');
        let validator=null,validated=false,closed=false,busy=false,generation=0n,loaded=new Uint8Array();
        const gate=async bytes=>await checkArchive(bytes,world,environment)&&(!validator||await validator(bytes.slice(),world));
        const store=await storeApi.openStore(world,gate,{environment,databaseName});
        const close=async()=>{if(closed)return;closed=true;environment.removeEventListener?.('pagehide',onPageHide);await store.close();};
        const onPageHide=()=>{void close();};
        try {
            const result=await store.load();generation=result.generation;loaded=result.payload.slice();
            if(selected&&(!generation||!loaded.length))throw Error('The selected adventure save is missing.');
            if(loaded.length&&!await checkArchive(loaded,world,environment))throw Error('This adventure save is damaged or belongs to another world.');
            environment.addEventListener?.('pagehide',onPageHide);
        }catch(error){await close();throw error;}
        function remember(){try{environment.localStorage?.setItem(metadataKey,world);}catch{/* Optional shortcut; confirmed world bytes remain durable. */}}
        return {
            world,
            get generation(){return generation;},get busy(){return busy;},get closed(){return closed;},
            get loadedBytes(){return loaded.slice();},
            async setValidator(value){
                if(closed||busy||typeof value!=='function')throw Error('Adventure validation is unavailable.');
                if(loaded.length&&!await value(loaded.slice(),world))throw Error('The saved adventure does not match the installed world.');
                if(closed)throw Error('The adventure was closed during validation.');
                validator=value;validated=true;
            },
            async load(){
                if(closed||busy)throw Error('Adventure storage is busy or closed.');
                busy=true;
                try{const result=await store.load();if(closed)throw Error('The adventure was closed during loading.');generation=result.generation;loaded=result.payload.slice();return {...result,payload:loaded.slice()};}
                finally{busy=false;}
            },
            async publish(input){
                if(closed||busy)throw Error('Adventure storage is busy or closed.');
                if(!validated||!validator)throw Error('Wait for the adventure world to finish loading before saving.');
                if(!(input instanceof Uint8Array)||input.length>maximumPayloadBytes)throw Error('The adventure save exceeds its size limit.');
                const bytes=input.slice();busy=true;
                try {
                    if(!await gate(bytes))throw Error('The adventure save does not match the current world.');
                    if(closed)throw Error('The adventure was closed before saving.');
                    const result=await store.publish(generation,bytes);
                    generation=result.generation;loaded=bytes;
                    if(!closed)remember();
                    return {generation,world};
                }catch(error){
                    // Strict transactions normally reject before publication.
                    // Read the actual generation before permitting a retry.
                    if(!closed){try{const actual=await store.load();generation=actual.generation;loaded=actual.payload.slice();}catch{validated=false;}}
                    throw error;
                }finally{busy=false;}
            },
            close
        };
    }
    return {databaseName,metadataKey,maximumPayloadBytes,validWorld,confirmedWorld,checkArchive,open};
});
