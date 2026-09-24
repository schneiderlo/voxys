// Starts the release's WASM and data-pack downloads while the rest of the page
// and WebGPU initialize, and keeps each data pack in Cache Storage under its
// content hash. A deploy that leaves a pack unchanged then costs no download.
// The Pages build writes the release manifest into index.html; without it
// (local builds) nothing starts here and Emscripten fetches files itself.
(function(global){
    'use strict';
    const cacheName='voxys-release-files-v1';
    const fileName=/^voxy_[a-z0-9_]+\.(data|wasm)$/;
    const sha256=/^[0-9a-f]{64}$/;

    function parseManifest(text){
        if(typeof text!=='string'||!text.startsWith('{'))return null;
        try{
            const manifest=JSON.parse(text);
            if(!manifest||typeof manifest!=='object')return null;
            for(const [name,entry] of Object.entries(manifest)){
                if(!fileName.test(name)||!sha256.test(entry?.sha256)
                    ||!Number.isSafeInteger(entry?.size)||entry.size<=0)return null;
            }
            return manifest;
        }catch{return null;}
    }

    // Optional packs (see scripts/build_wasm_data_pack.py) hold content that
    // only these experiences read. Everything else is in voxy_wasm.data.
    function packsFor(experience){
        if(experience==='ridgebreak')return ['moto'];
        if(String(experience).startsWith('salvage'))return ['salvage'];
        return [];
    }

    const fileUrl=(name,entry)=>`${name}?h=${entry.sha256}`;

    async function readBody(response,size,progress){
        if(!response.body){
            const buffer=await response.arrayBuffer();
            if(buffer.byteLength!==size)throw Error(`expected ${size} bytes, received ${buffer.byteLength}`);
            progress(size);return buffer;
        }
        const reader=response.body.getReader(),bytes=new Uint8Array(size);
        let offset=0;
        for(;;){
            const {done,value}=await reader.read();
            if(done)break;
            if(offset+value.byteLength>size){reader.cancel().catch(()=>{});throw Error(`more than ${size} bytes`);}
            bytes.set(value,offset);offset+=value.byteLength;progress(offset);
        }
        if(offset!==size)throw Error(`expected ${size} bytes, received ${offset}`);
        return bytes.buffer;
    }

    async function openCache(environment){
        try{return environment.caches?await environment.caches.open(cacheName):null;}
        catch{return null;}
    }

    function start({manifest,packs,environment=global}){
        const names=['voxy_wasm.data',...packs.map(pack=>`voxy_${pack}.data`)].filter(name=>manifest?.[name]);
        const perFile=new Map(names.map(name=>[name,0])),buffers=new Map(),writes=[];
        const state={
            total:names.reduce((sum,name)=>sum+manifest[name].size,0),loaded:0,cachedFiles:0,
            onProgress:null,wasmModule:null,
            // Emscripten's getPreloadedPackage hook: hand over a finished pack once.
            take(remoteName,size){
                const name=String(remoteName).split('?')[0].split('/').pop();
                const buffer=buffers.get(name);
                if(!buffer||buffer.byteLength!==size)return null;
                buffers.delete(name);return buffer;
            },
        };
        const report=(name,bytes)=>{
            state.loaded+=bytes-perFile.get(name);perFile.set(name,bytes);
            try{state.onProgress?.(state);}catch{}
        };
        const cachePromise=openCache(environment);
        async function load(name){
            const entry=manifest[name],url=fileUrl(name,entry),cache=await cachePromise;
            if(cache){
                try{
                    const hit=await cache.match(url);
                    if(hit){
                        buffers.set(name,await readBody(hit,entry.size,bytes=>report(name,bytes)));
                        ++state.cachedFiles;return;
                    }
                }catch(error){console.warn(`Cached ${name} is unusable; downloading it again.`,error);report(name,0);}
            }
            const response=await environment.fetch(url,{credentials:'same-origin'});
            if(!response.ok)throw Error(`${response.status} while downloading ${name}`);
            // Tee to disk while reading; a failed or short read is caught by
            // the size check the next time the cached copy is used.
            if(cache)writes.push(cache.put(url,response.clone()).catch(error=>console.warn(`Could not keep ${name} for the next visit.`,error)));
            buffers.set(name,await readBody(response,entry.size,bytes=>report(name,bytes)));
        }
        const wasm=manifest?.['voxy_wasm.wasm'];
        const compile=wasm&&environment.WebAssembly?.compileStreaming
            ?environment.WebAssembly.compileStreaming(environment.fetch(fileUrl('voxy_wasm.wasm',wasm),{credentials:'same-origin'}))
                .then(module=>{state.wasmModule=module;},error=>console.warn('Early WASM compile failed; the engine will fetch it.',error))
            :Promise.resolve();
        // A failed pack is left to Emscripten's own download of the same URL.
        state.ready=Promise.all([compile,...names.map(name=>load(name).catch(error=>{
            console.warn(`Early download of ${name} failed; the engine will fetch it.`,error);report(name,0);
        }))]).then(()=>state);
        // Drop files from other releases once this release's copies are stored.
        state.ready.then(()=>Promise.all(writes)).then(async()=>{
            const cache=await cachePromise;if(!cache)return;
            const current=new Set(Object.entries(manifest).map(([name,entry])=>new URL(fileUrl(name,entry),environment.location.href).href));
            for(const request of await cache.keys())if(!current.has(request.url))await cache.delete(request);
        }).catch(()=>{});
        return state;
    }

    // Emscripten's locateFile hook: content-addressed URLs for release files,
    // the build id for everything else.
    function locate(path,prefix,manifest,buildId){
        const entry=manifest?.[path];
        return entry?`${prefix}${fileUrl(path,entry)}`:`${prefix}${path}?v=${encodeURIComponent(buildId)}`;
    }

    const api={parseManifest,packsFor,start,locate};
    global.VoxyDataPacks=api;
    const location=global.location;
    // The directory URL redirects to index.html; start on the real page only.
    if(!location||location.pathname.endsWith('/')||!global.navigator?.gpu)return;
    const manifest=parseManifest(global.voxyReleaseFiles);
    if(!manifest)return;
    const experience=new URLSearchParams(location.search).get('experience')||'build';
    global.voxyStartupDownloads=start({manifest,packs:packsFor(experience)});
})(globalThis);
