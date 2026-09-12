(function(root,factory){
    const api=factory();if(typeof module==='object'&&module.exports)module.exports=api;else root.VoxyCoveSaves=api;
})(globalThis,function(){
    'use strict';
    const owners=new WeakMap();
    // A disposable navigation hint; IndexedDB remains the sole save authority.
    const lastConfirmedWorldKey='voxys.cove.last-confirmed-world.v1';
    const historyKey='voxys.cove.confirmed-world-history.v1';
    function history(environment){
        try {const text=environment.localStorage.getItem(historyKey);if(!text||text.length>8192)return [];
            const rows=JSON.parse(text);if(!Array.isArray(rows))return [];
            const seen=new Set();return rows.filter(row=>row&&validWorld(row.world)&&Number.isSafeInteger(row.savedAt)&&row.savedAt>0&&row.savedAt<=8640000000000000&&!seen.has(row.world)&&seen.add(row.world)).slice(0,32);
        }catch{return [];}
    }
    const validWorld=world=>typeof world==='string'&&/^[0-9a-f]{32}$/.test(world)&&!/^0+$/.test(world);
    function rememberConfirmedWorld(world,environment){
        if(!validWorld(world))return;
        try{environment.localStorage.setItem(lastConfirmedWorldKey,world);}catch{} // Saving must not depend on this hint.
        try{const rows=[{world,savedAt:Date.now()},...history(environment).filter(row=>row.world!==world)].slice(0,32);
            environment.localStorage.setItem(historyKey,JSON.stringify(rows));}catch{}
    }
    function installContinue(environment=globalThis){
        const link=environment.document.getElementById('cove-continue');
        if(!link)return;
        link.hidden=true;link.removeAttribute('href');
        try{
            const world=environment.localStorage.getItem(lastConfirmedWorldKey);
            if(!validWorld(world))return;
            const url=new URL(environment.location.href);
            if(url.protocol!=='https:'&&url.protocol!=='http:')return;
            url.search='';url.hash='';
            url.searchParams.set('experience','salvage-cove');url.searchParams.set('world',world);
            link.href=url.href;link.hidden=false;
        }catch{} // Unavailable or damaged local metadata hides only the shortcut.
    }
    function installHistory(environment=globalThis){
        const list=environment.document.getElementById('cove-saved-list');if(!list)return;list.replaceChildren();
        let base;try{base=new URL(environment.location.href);if(!['http:','https:'].includes(base.protocol))return;}catch{return;}
        const rows=history(environment);
        if(!rows.length){const item=environment.document.createElement('li');item.textContent='No saved expeditions are listed on this device yet.';list.append(item);return;}
        for(const row of rows){const item=environment.document.createElement('li'),link=environment.document.createElement('a');
            const url=new URL(base);url.search='';url.hash='';url.searchParams.set('experience','salvage-cove');url.searchParams.set('world',row.world);
            link.href=url.href;link.textContent=`Cove · ${new Date(row.savedAt).toLocaleString()}`;item.append(link);list.append(item);}
    }
    const saved=engine=>{engine._voxy_cove_save_completed?.();};
    const toHex=bytes=>Array.from(bytes,b=>b.toString(16).padStart(2,'0')).join('');
    const fromHex=text=>{
        if(!text||text.length%2||text.length>2*(8*1024*1024+4096+4*(128*1024+4))||!/^[0-9a-f]+$/.test(text))throw Error('The expedition could not be prepared.');
        return Uint8Array.from(text.match(/../g),pair=>parseInt(pair,16));
    };
    const action=(engine,kind,text='')=>engine.ccall('voxy_salvage_expedition_action','string',['number','string'],[kind,text]);
    const read=engine=>JSON.parse(engine.UTF8ToString(engine._voxy_get_salvage_preview_json()));
    async function resume(engine,world,args,environment=globalThis){
        if(!validWorld(world))throw Error('The saved expedition address is invalid.');
        let booted=false,stopped=false,store;
        const stop=()=>{stopped=true;if(booted)action(engine,5);store?.close();};
        environment.addEventListener('pagehide',stop,{once:true});
        try {
            store=await environment.VoxyExpeditionStore.openStore(world,async payload=>{
                if(stopped)return false;
                if(!booted){
                    if(engine.ccall('voxy_stage_cove_resume','number',['string','string'],[world,toHex(payload)])!==1)return false;
                    engine.callMain(args);booted=true;
                    const deadline=Date.now()+30000;
                    while(!stopped&&engine._voxy_is_initialized()!==1&&Date.now()<deadline)
                        await new Promise(resolve=>environment.setTimeout(resolve,30));
                    if(stopped||engine._voxy_is_initialized()!==1)return false;
                    const state=read(engine);
                    if(state.world!==world||state.restore?.phase!=='awaiting-storage')return false;
                }
                return !stopped&&action(engine,2,toHex(payload))==='ok';
            });
            const loaded=await store.load();
            if(stopped||loaded.generation===0n)throw Error('This saved expedition is missing. Your other saves are unchanged.');
            const next=fromHex(action(engine,3));
            const committed=await store.publish(loaded.generation,next);
            const digest=toHex(new Uint8Array(await environment.crypto.subtle.digest('SHA-256',next)));
            if(stopped||store.closed||action(engine,4,digest)!=='ok')throw Error('The expedition could not finish loading. Reload to retry.');
            owners.set(engine,{store,generation:committed.generation,world});store=null;
            rememberConfirmedWorld(world,environment);saved(engine);
        }catch(error){if(booted)action(engine,5);throw error;}
        finally {environment.removeEventListener('pagehide',stop);await store?.close();}
    }
    function install(engine,environment=globalThis){
        const button=environment.document.getElementById('salvage-save');
        const status=environment.document.getElementById('salvage-save-status');
        const help=environment.document.getElementById('salvage-save-help');
        if(!button||!status)return undefined;
        let owner=owners.get(engine),stopped=false,busy=false,world=null;
        let confirmedGeneration=owner?.generation||0n;
        const priorSave=environment['voxyCoveSave'];
        let registered=false,delivery=false,deliveryAttempted=false,harbor=false,rescue=false,workshop=false;
        let message=owner?'Loaded. Resume when ready.':'Pause to save this expedition.';
        const close=()=>{
            if(stopped)return;stopped=true;button.removeEventListener('click',save);
            environment.removeEventListener('pagehide',close);owners.delete(engine);
            if(registered||owner)action(engine,5);
            owner?.store.close();
            if(environment['voxyCoveSave']===requestSave)environment['voxyCoveSave']=priorSave;
        };
        const save=async(automatic=false)=>{
            if(stopped||busy||(automatic!==true&&button.disabled)||!world)return;
            const savingDelivery=delivery,savingHarbor=delivery&&harbor,savingRescue=delivery&&rescue,savingWorkshop=delivery&&workshop;
            if(savingDelivery)deliveryAttempted=true;
            busy=true;button.disabled=true;status.textContent=savingWorkshop?'Saving boat and owned parts…':savingRescue?'Saving your recovered boat…':savingHarbor?'Saving the powered harbor lift…':savingDelivery?'Saving the delivery…':'Saving expedition…';
            try {
                const payload=fromHex(action(engine,1));
                const digest=savingDelivery?toHex(new Uint8Array(await environment.crypto.subtle.digest('SHA-256',payload))):null;
                if(!owner){
                    const store=await environment.VoxyExpeditionStore.openStore(world,data=>action(engine,2,toHex(data))==='ok');
                    if(stopped){await store.close();return;}
                    owner={store,generation:0n,world};owners.set(engine,owner);
                    const current=await store.load();
                    if(current.generation!==0n)throw Error('This world already has a save. Reload its saved address.');
                }
                if(stopped||owner.world!==world)throw Error('The active expedition changed.');
                const committed=await owner.store.publish(owner.generation,payload);
                owner.generation=committed.generation;
                if(!stopped){
                    if(owner.store.closed || (savingDelivery&&action(engine,7,digest)!=='ok')) {
                        action(engine,5);throw Error('The saved progress could not be confirmed. Reload to recover it.');
                    }
                    const url=new URL(environment.location.href);url.searchParams.set('world',world);
                    environment.history.replaceState(null,'',url.href);
                    rememberConfirmedWorld(world,environment);
                    confirmedGeneration=committed.generation;saved(engine);
                    message=savingWorkshop?'Boat and owned parts saved. Resume when ready.':savingRescue?'Boat recovered and saved. Resume when ready.':savingHarbor?'Harbor lift powered and saved. Resume when ready.':savingDelivery?'Delivery saved. Resume when ready.':'Saved. Reload resumes this expedition.';
                }
            }catch(error){message=`Save failed. ${error.message||error}`;}
            finally {busy=false;if(!stopped)status.textContent=message;}
        };
        button.addEventListener('click',save);environment.addEventListener('pagehide',close,{once:true});
        const update=state=>{
            if(stopped)return;
            world=state.world||null;button.hidden=status.hidden=!world;
            if(help)help.hidden=!world;
            if(world&&!registered)registered=action(engine,6)==='ok';
            rescue=Boolean(state.rescue?.savePending);
            workshop=Boolean(state.workshop?.savePending);
            delivery=workshop||rescue||Boolean(state.job?.savePending&&state.job?.secured);harbor=Boolean(state.harbor?.pending);
            if(!delivery)deliveryAttempted=false;
            if(owner?.store.closed){action(engine,5);message='This expedition closed in this tab. Reload to continue.';}
            button.disabled=busy||Boolean(state.practice?.active)||Boolean(state.workshop?.open)||state.session?.admissionOpen===false||!state.ready||state.failed||state.pause?.phase!=='paused'||Boolean(state.rescue?.pending&&!rescue)||Boolean(owner?.store.closed);
            if(!busy)status.textContent=state.session?.admissionOpen===false?'This expedition is unavailable. Reload to continue.':state.practice?.active?'Test mode cannot be saved. Return to your workshop first.':state.workshop?.open?'Close the workshop, then pause to save the expedition. Save a design to keep workshop plans.':message;
            if(delivery&&!deliveryAttempted&&!button.disabled)void save(true);
        };
        const requestSave=()=>{
            if(stopped)return false;
            try{update(read(engine));}catch{return false;}
            if(stopped||busy||button.disabled||!world)return false;
            void save();return true;
        };
        environment['voxyCoveSave']=requestSave;
        return {cleanup:close,tick:update,requestSave,status:()=>({busy,message:status.textContent,
            hasConfirmedSave:Boolean(owner&&owner.world===world&&confirmedGeneration>0n),canSave:!stopped&&!busy&&!button.disabled&&Boolean(world)})};
    }
    return {resume,install,installContinue,installHistory};
});
