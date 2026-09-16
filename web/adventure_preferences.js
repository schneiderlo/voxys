(function(root,factory){
    const api=factory();
    if(typeof module==='object'&&module.exports)module.exports=api;
    else root.VoxyAdventurePreferences=api;
})(globalThis,function(){
    'use strict';
    const storageKey='voxys.adventure.preferences.v1',maximumBytes=4096;
    const bounded=text=>typeof text==='string'&&text.length<=maximumBytes
        &&new TextEncoder().encode(text).length<=maximumBytes;
    const revision=value=>typeof value==='string'&&/^(0|[1-9][0-9]{0,19})$/.test(value)
        &&BigInt(value)<=18446744073709551615n?BigInt(value):null;
    function install(engine,environment=globalThis){
        if(typeof engine?.ccall!=='function')return undefined;
        let stopped=false,initialized=false,observed=null,loading=null;
        const call=(kind,text='')=>engine.ccall('adventure_preferences_action','string',['number','string'],[kind,text]);
        const notify=(kind,message='')=>{try{call(kind,message);}catch{}};
        const canonical=()=>{
            const text=call(1);
            if(!bounded(text))throw Error('Settings response exceeds the transport limit.');
            // This is a transport check, not a second preference schema. Only
            // the core parses/validates settings and returns canonical bytes.
            const value=JSON.parse(text);
            if(!value||typeof value!=='object'||Array.isArray(value))throw Error('Settings response is unavailable.');
            return text;
        };
        const persist=text=>{
            try{
                const storage=environment.localStorage;
                if(typeof storage?.setItem!=='function')throw Error('Storage is unavailable.');
                storage.setItem(storageKey,text);notify(4);
            }catch{notify(5,'Settings work for this visit. This device could not remember them.');}
        };
        const start=current=>{
            initialized=true;observed=current;
            let before;
            try{before=canonical();}catch{notify(5,'Settings are unavailable to save. Current controls still work.');return;}
            let saved;
            try{
                const storage=environment.localStorage;
                if(typeof storage?.getItem!=='function')throw Error('Storage is unavailable.');
                saved=storage.getItem(storageKey);
            }catch{notify(5,'Settings work for this visit. Saved settings could not be read.');return;}
            if(saved===null)return;
            if(!bounded(saved)){notify(5,'Saved settings were kept but could not be loaded. Current controls still work.');return;}
            try{
                if(call(2,saved)!=='ok'){
                    notify(5,'Saved settings were kept but could not be loaded. Current controls still work.');return;
                }
                loading={beforeRevision:current,canonical:null};
                const after=canonical();
                // tick's snapshot predates this synchronous apply. A changed
                // apply advances the core revision once; wait for that fresh
                // snapshot before establishing the observed baseline.
                loading=after!==before?{beforeRevision:current,canonical:after}:null;
                notify(4);
            }catch{notify(5,'Saved settings were kept. Current controls work for this visit.');}
        };
        return {
            tick(state){
                // The runtime's accepted UI snapshot omits top-level ready.
                // Match that established contract: explicit startup/failure
                // states stop transport; the canonical revision gates work.
                if(stopped||!state||state.ready===false||state.failed===true)return;
                const current=revision(state.preferencesRevision);if(current===null)return;
                if(!initialized){start(current);return;}
                if(loading){
                    if(current<loading.beforeRevision||(current===loading.beforeRevision&&loading.canonical!==null))return;
                    const loaded=loading;loading=null;observed=current;
                    try{
                        const text=canonical();
                        // Startup alone never rewrites storage. A later menu
                        // change (including explicit Save again / Reset) is a
                        // new revision and can deliberately replace old bytes.
                        if(current>loaded.beforeRevision+1n||(loaded.canonical!==null&&text!==loaded.canonical))persist(text);
                    }catch{notify(5,'Settings work for this visit. They could not be remembered.');}
                    return;
                }
                if(current<=observed)return;
                // Account before I/O: denial or an unavailable core response
                // gets one attempt per explicit revision, never a polling loop.
                observed=current;
                try{persist(canonical());}
                catch{notify(5,'Settings work for this visit. They could not be remembered.');}
            },
            cleanup(){stopped=true;loading=null;},
        };
    }
    return {install,storageKey,maximumBytes};
});
