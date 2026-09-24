import {createBridge} from './bridge.js';

// The GPU draws every visible control on both platforms. These transparent,
// non-pointer peers give the browser the same hit targets as focusable semantic
// buttons, without a second visual layout or another route for mouse input.
export function installShared(engine,environment=globalThis){
    if(!engine?._adventure_action||!environment.document)return;
    const doc=environment.document,canvas=doc.getElementById('voxy-canvas');
    if(!canvas)return;
    const host=doc.createElement('div');host.id='shared-hud-accessibility';
    host.setAttribute('aria-label','Game controls');host.setAttribute('role','group');
    const live=doc.createElement('div');live.className='shared-hud-sr';
    live.setAttribute('role','status');live.setAttribute('aria-live','polite');
    const help=doc.createElement('p');help.id='shared-hud-help';help.className='shared-hud-sr';
    help.textContent='1 to 6 choose the visible hotbar slots. B switches building and exploring. Tab opens pieces while building. P opens colours. Escape opens the menu. Tab through controls; Enter selects.';
    const originalDescription=canvas.getAttribute('aria-describedby');
    canvas.setAttribute('aria-describedby',[originalDescription,help.id].filter(Boolean).join(' '));
    doc.body.append(host,live,help);
    const hadAdventure=doc.body.classList.contains('voxy-adventure'),hadBuild=doc.body.classList.contains('voxy-build');
    doc.body.classList.add('voxy-adventure','voxy-build');
    let bridge,state,signature='',lastAnnouncement='',announcementState=null,previousMode=null,stopped=false,requestedFocus=null;
    const peers=new Map();
    const modal=()=>state&&!['build','explore'].includes(state.mode);
    const syncBounds=()=>{
        const b=canvas.getBoundingClientRect();
        Object.assign(host.style,{left:`${b.left}px`,top:`${b.top}px`,width:`${b.width}px`,height:`${b.height}px`});
    };
    const announce=message=>{if(message!==lastAnnouncement){live.textContent=message;lastAnnouncement=message;}};
    const publish=next=>{
        if(stopped)return;
        state=next;host.dataset.mode=next.mode||'unavailable';
        if(next.failed){host.replaceChildren();peers.clear();signature='';announcementState=null;announce('Game controls are unavailable. Reload to try again. Saved builds are kept.');return;}
        host.setAttribute('role',modal()?'dialog':'group');
        host.setAttribute('aria-label',modal()?(next.menuTitle||'Game menu'):'Game controls');
        if(modal())host.setAttribute('aria-modal','true');else host.removeAttribute('aria-modal');
        const hud=next.hud,controls=Array.isArray(hud?.controls)?hud.controls.slice(0,96):[];
        const valid=Number.isFinite(hud?.width)&&hud.width>0&&Number.isFinite(hud?.height)&&hud.height>0;
        const nextSignature=JSON.stringify([valid?hud:null,next.menuToken,next.ready,next.piece,next.paint,next.quickSlot,next.catalogCategory,next.mode]);
        if(nextSignature!==signature){
            signature=nextSignature;const keep=new Set(),occurrences=new Map();
            for(let index=0;valid&&index<controls.length;index++){
                const c=controls[index];
                if(![c.x,c.y,c.width,c.height,c.action,c.value].every(Number.isFinite)||c.width<=0||c.height<=0||!c.label)continue;
                // A selected radial piece moves from a petal to the centre.
                // Its semantic identity stays stable across that visual reorder.
                // Repeated actions may still have distinct on-screen peers.
                const identity=`${c.action}:${c.intent||0}:${c.value}:${c.row??''}`;
                const occurrence=occurrences.get(identity)||0;occurrences.set(identity,occurrence+1);
                const key=`${identity}:${occurrence}`;keep.add(key);
                let button=peers.get(key);
                if(!button){
                    button=doc.createElement('button');button.type='button';button.className='shared-hud-peer';
                    button.addEventListener('focus',()=>{bridge?.own(true);const target=button.hudControl;if(target.intent&&Number.isInteger(target.row)&&target.row>=0)bridge?.action(26,target.intent,button.hudToken);});
                    button.addEventListener('click',()=>{
                        const target=button.hudControl;
                        const opensMenu=[9,13,23,24,29,31,38].includes(target.action);
                        const keepMenuFocus=modal()||opensMenu;
                        if(keepMenuFocus)requestedFocus={mode:null,reverse:false,afterObservation:state.observation};
                        if(bridge?.action(target.action,target.action===10?target.intent:target.value,button.hudToken)){
                            // Menu navigation retains focus until the accepted state
                            // chooses the next control; gameplay returns to canvas.
                            if(!keepMenuFocus)bridge.worldFocus();
                        }else requestedFocus=null;
                    });
                    peers.set(key,button);host.append(button);
                }
                button.hudControl=c;button.hudToken=next.menuToken;
                button.setAttribute('aria-label',String(c.label));button.disabled=c.enabled===false||next.ready===false;
                if(!button.disabled&&c.shortcutKey>=49&&c.shortcutKey<=54)
                    button.setAttribute('aria-keyshortcuts',String.fromCharCode(c.shortcutKey));
                else button.removeAttribute('aria-keyshortcuts');
                const pressed=c.action===39?c.value===next.quickSlot:c.action===2?c.value===next.piece:c.action===13?c.value===next.catalogCategory+1:
                    next.mode==='colours'&&c.action===10?c.value===(next.paint||0):null;
                if(pressed===null)button.removeAttribute('aria-pressed');else button.setAttribute('aria-pressed',String(pressed));
                if(c.action===38)button.setAttribute('aria-expanded',String(next.mode==='colours'));else button.removeAttribute('aria-expanded');
                button.dataset.action=String(c.action);button.dataset.value=String(c.value);
                if(Number.isInteger(c.row)&&c.row>=0)button.dataset.row=String(c.row);else delete button.dataset.row;
                Object.assign(button.style,{left:`${c.x/hud.width*100}%`,top:`${c.y/hud.height*100}%`,width:`${c.width/hud.width*100}%`,height:`${c.height/hud.height*100}%`});
            }
            let removedFocus=false;
            for(const [key,button] of peers)if(!keep.has(key)){removedFocus||=doc.activeElement===button;button.remove();peers.delete(key);}
            if(removedFocus){
                if(modal()){
                    // Resolve after reconciliation, against the published
                    // selection. Focusing row zero here would send action26
                    // and overwrite that selection while controls are replaced.
                    requestedFocus??={mode:next.mode,reverse:false};
                }
                else bridge?.worldFocus();
            }
            syncBounds();
        }
        if(previousMode!==next.mode){
            // Do not move focus away from pointer or controller play. A browser
            // keyboard user who enters a menu retains access to semantic peers.
            if(previousMode!==null&&!modal()&&host.contains(doc.activeElement))bridge?.worldFocus();
            previousMode=next.mode;
        }
        // Idle engine observations are not published. A click can therefore
        // begin from an older displayed observation; wait for action completion
        // as well as a new observation before choosing the accepted controls.
        if(requestedFocus&&!next.pending&&requestedFocus.afterObservation!==next.observation&&(!requestedFocus.mode||requestedFocus.mode===next.mode)){
            const buttons=[...host.querySelectorAll('button:not(:disabled)')];
            if(!modal()){
                requestedFocus=null;bridge?.worldFocus();
            }else if(buttons.length){
                const request=requestedFocus;requestedFocus=null;
                const selected=buttons.find(button=>button.dataset.row===String(next.menuSelected));
                (request.reverse?buttons.at(-1):selected||buttons[0]).focus({preventScroll:true});
            }
        }
        const currentAnnouncement={failure:next.saveFailure||'',save:next.saveStatus||'',status:next.status||''};
        const previousAnnouncement=announcementState;announcementState=currentAnnouncement;
        if(currentAnnouncement.failure)announce(currentAnnouncement.failure);
        else if(currentAnnouncement.save!==previousAnnouncement?.save||previousAnnouncement?.failure)
            announce(currentAnnouncement.save||currentAnnouncement.status);
        else if(currentAnnouncement.status!==previousAnnouncement?.status)announce(currentAnnouncement.status);
    };
    const enterControls=event=>{
        if(event.key!=='Tab'||!state||state.failed)return;
        // Emscripten consumes Tab at document level. Enter the semantic peers
        // here first, retaining the game's Tab-to-pieces shortcut while building.
        event.preventDefault();event.stopPropagation();
        if(event.repeat)return;
        if(state.mode==='build'&&!event.shiftKey){
            requestedFocus={mode:'catalog',reverse:false};
            if(!bridge?.action(23))requestedFocus=null;
        }else{
            const buttons=[...host.querySelectorAll('button:not(:disabled)')];
            (event.shiftKey?buttons.at(-1):buttons[0])?.focus({preventScroll:true});
        }
    };
    const focusOut=event=>{if(!host.contains(event.relatedTarget))bridge?.own(false);};
    const keyDown=event=>{
        event.stopPropagation();
        if(event.key==='Escape'){
            event.preventDefault();if(event.repeat)return;
            if(bridge?.action(modal()?20:31))bridge.worldFocus();return;
        }
        const buttons=[...host.querySelectorAll('button:not(:disabled)')];
        if(!buttons.length)return;
        if(['ArrowLeft','ArrowRight','ArrowUp','ArrowDown','Home','End'].includes(event.key)){
            event.preventDefault();const current=buttons.indexOf(doc.activeElement);
            const index=event.key==='Home'?0:event.key==='End'?buttons.length-1:
                (current+(['ArrowLeft','ArrowUp'].includes(event.key)?buttons.length-1:1))%buttons.length;
            buttons[index].focus({preventScroll:true});
        }else if(event.key==='Tab'&&modal()){
            const first=buttons[0],last=buttons.at(-1);
            if(event.shiftKey&&doc.activeElement===first){event.preventDefault();last.focus();}
            else if(!event.shiftKey&&doc.activeElement===last){event.preventDefault();first.focus();}
        }else if(event.repeat&&(event.key==='Enter'||event.key===' '))event.preventDefault();
    };
    const keyUp=event=>event.stopPropagation();
    host.addEventListener('focusout',focusOut);host.addEventListener('keydown',keyDown);host.addEventListener('keyup',keyUp);
    canvas.addEventListener('keydown',enterControls);
    const resize=environment.ResizeObserver?new environment.ResizeObserver(syncBounds):null;resize?.observe(canvas);
    environment.addEventListener('resize',syncBounds);
    bridge=createBridge(engine,environment,publish);syncBounds();
    return {refresh:()=>bridge.refresh(),cleanup(){
        if(stopped)return;stopped=true;bridge.cleanup();resize?.disconnect();environment.removeEventListener('resize',syncBounds);canvas.removeEventListener('keydown',enterControls);
        host.remove();live.remove();help.remove();
        if(originalDescription===null)canvas.removeAttribute('aria-describedby');else canvas.setAttribute('aria-describedby',originalDescription);
        if(!hadAdventure)doc.body.classList.remove('voxy-adventure');if(!hadBuild)doc.body.classList.remove('voxy-build');
    }};
}
