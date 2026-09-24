// All accepted game state comes from C++. The UI owns presentation only.
const modes=new Set(['build','explore','catalog','pause','settings','controls','combat-binding','binding-choice','guide']);
export function createBridge(engine,environment,notify){
    let stopped=false,owned=false,latest=null,pending=null,preferences=null,dirty=false;
    const dispatch=(action,value=0)=>engine._adventure_action(action,value);
    const own=value=>{if(owned!==value){owned=value;dispatch(15,value?1:0);}};
    function readRaw(){
        const pointer=engine._get_adventure_state_json();
        const raw=pointer&&engine.UTF8ToString(pointer);
        if(typeof raw!=='string'||raw.length>512*1024)throw Error('Building controls are unavailable.');
        return raw;
    }
    function parse(raw){
        const state=JSON.parse(raw);
        if(!state||state.creative!==true||!modes.has(state.mode)||!Number.isInteger(state.piece)||state.piece<1||state.piece>15
            ||!Number.isInteger(state.menuToken)||!Array.isArray(state.rows))throw Error('Building controls are unavailable.');
        return state;
    }
    const read=()=>parse(readRaw());
    // The 100ms poll usually sees an unchanged snapshot. Publishing it again
    // re-renders the whole interface, so publish only when the core's bytes
    // or the pending flag differ from what the UI already shows.
    let publishedRaw=null,publishedPending=null;
    function refresh(){
        if(stopped)return;
        try{
            const raw=readRaw();
            const state=raw===publishedRaw&&latest?latest:parse(raw);latest=state;dirty=state.dirty===true;
            if(pending!==null&&pending!==state.observation)pending=null;
            const isPending=pending!==null;
            if(raw===publishedRaw&&isPending===publishedPending)return;
            publishedRaw=raw;publishedPending=isPending;
            preferences?.tick(state);notify({...state,pending:isPending});
        }catch(error){latest=null;publishedRaw=null;notify({failed:true,message:String(error.message)});}
    }
    const worldFocus=()=>{environment.document.getElementById('voxy-canvas')?.focus({preventScroll:true});own(false);};
    const action=(id,value=0,expectedToken=null)=>{
        if(stopped||!latest||latest.failed||latest.ready===false||pending!==null)return false;
        // Re-read before executing a DOM event, so an old modal cannot activate
        // a freshly repurposed row during the 100ms presentation interval.
        try{
            const current=read();
            if(expectedToken!==null&&current.menuToken!==expectedToken){refresh();return false;}
            if(id===4&&(current.mode!=='build'||current.valid!==true))return false;
            if(id!==26)pending=current.observation;dispatch(id,value);refresh();return true;
        }catch{refresh();return false;}
    };
    dispatch(19,1);
    try{preferences=environment.VoxyAdventurePreferences?.install(engine,environment);}catch{}
    const beforeUnload=event=>{if(dirty){event.preventDefault();event.returnValue='';}};
    const canvas=environment.document.getElementById('voxy-canvas');
    const focusWorld=()=>{own(false);};
    canvas?.addEventListener('pointerdown',focusWorld,true);
    environment.addEventListener('beforeunload',beforeUnload);
    refresh();const timer=environment.setInterval(refresh,100);
    return {action,own,worldFocus,refresh,cleanup(){
        if(stopped)return;own(false);stopped=true;dispatch(19,0);preferences?.cleanup();
        canvas?.removeEventListener('pointerdown',focusWorld,true);
        environment.clearInterval(timer);environment.removeEventListener('beforeunload',beforeUnload);
    }};
}
