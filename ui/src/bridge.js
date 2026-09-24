// All accepted game state comes from C++. The UI owns presentation only.
const modes=new Set(['build','explore','catalog','pause','settings','controls','combat-binding','binding-choice','guide']);
// Only presentation data invalidates the DOM. The engine's observation, player
// tick, camera and physics diagnostics change even while every control is idle.
// Keep this projection in step with the fields read by main.jsx;
// the full, fresh snapshot is still retained for action admission and delivery.
const presentationFields=['ready','failed','message','mode','piece','paint','parts','rows','menuToken','menuSelected',
    'menuTitle','menuText','catalogCategory','status','statusEvent','saveStatus','saveFailure','dirty',
    'preferencesStatus','textScale','highContrast','reducedMotion','colourAvailable',
    'canUndo','canRemove','riding','swimming','interaction'];
const cannonFields=['available','nearby','active','ready','awaitingHit','wallBusy','wallReleased','wallFailed',
    'wallMessage','error','inspectingWall','impacts'];
const presentationSignature=state=>JSON.stringify([
    ...presentationFields.map(key=>state[key]),state.cannon&&cannonFields.map(key=>state.cannon[key]),
]);
export function createBridge(engine,environment,notify){
    let stopped=false,owned=false,latest=null,pending=null,preferences=null,dirty=false,pendingFrame=null,pendingFramesRemaining=0;
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
    let previousRaw=null,previousSignature=null,publishedSignature=null,publishedPending=null;
    const cancelPendingFrame=()=>{
        pendingFramesRemaining=0;
        if(pendingFrame!==null){environment.cancelAnimationFrame?.(pendingFrame);pendingFrame=null;}
    };
    const followPending=()=>{
        if(pendingFrame!==null||pendingFramesRemaining===0||typeof environment.requestAnimationFrame!=='function')return;
        // Input waits for the next accepted engine frame, not the next 100ms
        // poll. Eight attempts bound work when the engine is stalled; the
        // interval covers that case and hidden tabs. Idle play schedules none.
        --pendingFramesRemaining;
        pendingFrame=environment.requestAnimationFrame(()=>{pendingFrame=null;refresh();});
    };
    function refresh(){
        if(stopped)return;
        try{
            const raw=readRaw();
            const unchanged=raw===previousRaw&&latest;
            const state=unchanged?latest:parse(raw);latest=state;previousRaw=raw;dirty=state.dirty===true;
            if(pending!==null&&pending!==state.observation)pending=null;
            const isPending=pending!==null;
            if(isPending)followPending();else cancelPendingFrame();
            // Preference persistence is driven by its own accepted revision,
            // including revisions that do not change a visible label.
            preferences?.tick(state);
            const signature=unchanged?previousSignature:presentationSignature(state);previousSignature=signature;
            if(signature===publishedSignature&&isPending===publishedPending)return;
            publishedSignature=signature;publishedPending=isPending;
            notify({...state,pending:isPending});
        }catch(error){cancelPendingFrame();latest=null;previousRaw=null;publishedSignature=null;notify({failed:true,message:String(error.message)});}
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
            if(id!==26){pending=current.observation;pendingFramesRemaining=8;}dispatch(id,value);refresh();return true;
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
        if(stopped)return;own(false);stopped=true;cancelPendingFrame();dispatch(19,0);preferences?.cleanup();
        canvas?.removeEventListener('pointerdown',focusWorld,true);
        environment.clearInterval(timer);environment.removeEventListener('beforeunload',beforeUnload);
    }};
}
