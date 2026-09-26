import {test} from 'node:test';
import assert from 'node:assert/strict';
import {JSDOM} from 'jsdom';
import {installShared} from './src/shared.js';
import {installFrontierAudio} from './src/frontier_audio.js';

function fixture(t,soundPreference=null){
    const dom=new JSDOM('<canvas id="voxy-canvas" tabindex="0"></canvas>',{url:'http://localhost/',pretendToBeVisual:true});
    const w=dom.window,doc=w.document,canvas=doc.querySelector('canvas');
    if(soundPreference!==null)w.localStorage.setItem('voxys.frontier.audio.v1',String(soundPreference));
    canvas.getBoundingClientRect=()=>({left:10,top:20,width:640,height:360});
    const control=(label,action,value,x=20,extra={})=>({label,action,value,x,y:600,width:100,height:60,intent:0,row:-1,enabled:true,...extra});
    let state={creative:true,ready:true,mode:'build',piece:10,menuToken:1,observation:'1',rows:[],dirty:true,saveStatus:'Unsaved build',hud:{width:1280,height:720,controls:[control('Brick 2 × 4',2,10),control('Colours',38,0,130)]}};
    const calls=[],engine={_get_adventure_state_json:()=>1,UTF8ToString:()=>JSON.stringify(state),_adventure_action:(id,value)=>calls.push([id,value])};
    const app=installShared(engine,w);
    t.after(()=>{app.cleanup();dom.window.close();});
    return {w,doc,canvas,app,calls,control,get state(){return state;},set state(s){state=s;},update(changes){state={...state,...changes,observation:String(Number(state.observation)+1)};app.refresh();}};
}

test('shared browser controls mirror framebuffer geometry without a second visual HUD',t=>{
    const f=fixture(t),host=f.doc.getElementById('shared-hud-accessibility');
    assert.equal(f.doc.getElementById('build-ui'),null);
    assert.equal(host.style.left,'10px');assert.equal(host.style.width,'640px');
    const button=host.querySelector('button');
    assert.equal(button.getAttribute('aria-label'),'Brick 2 × 4');
    assert.equal(button.style.left,'1.5625%');assert.equal(button.style.width,'7.8125%');
    f.app.refresh();assert.equal(host.querySelector('button'),button,'Idle polling keeps the same focus node');
    button.focus();assert.deepEqual(f.calls.at(-1),[15,1]);
    button.click();assert(f.calls.some(([id,value])=>id===2&&value===10));
    assert.equal(f.doc.activeElement,f.canvas);
});

test('live engine ticks leave the semantic DOM untouched until a control changes',t=>{
    const f=fixture(t),host=f.doc.getElementById('shared-hud-accessibility');
    const observer=new f.w.MutationObserver(()=>{});
    observer.observe(host,{attributes:true,childList:true,subtree:true});
    for(let tick=0;tick<100;tick++)f.update({player:{tick:String(tick),x:tick},camera:{yaw:tick/10},forest:{selectionMs:tick/100},navigation:{cameraBearingDegrees:tick,playerUv:[tick/100,.5]}});
    assert.equal(observer.takeRecords().length,0,'Frame telemetry must not write semantic attributes');
    f.update({piece:8});assert(observer.takeRecords().length>0,'Visible selection changes still reconcile controls');
    observer.disconnect();
});

test('slot shortcuts follow the rendered hotbar and are removed from disabled menu peers',t=>{
    const f=fixture(t);
    f.update({quickSlot:1,hud:{...f.state.hud,controls:[f.control('Red brick',39,1,20,{shortcutKey:49})]}});
    const button=f.doc.querySelector('.shared-hud-peer');assert.equal(button.getAttribute('aria-keyshortcuts'),'1');
    assert.equal(button.getAttribute('aria-pressed'),'true');
    f.update({quickSlot:2});assert.equal(button.getAttribute('aria-pressed'),'false','Slot identity survives equal piece kinds');
    f.update({mode:'catalog',hud:{...f.state.hud,controls:[f.control('Red brick',39,1,20,{shortcutKey:49,enabled:false})]}});
    assert.equal(button.getAttribute('aria-keyshortcuts'),null);
});

test('shared menu peers refuse stale intents and carry accepted opaque identities',t=>{
    const f=fixture(t);
    f.update({mode:'colours',hud:{width:1280,height:720,controls:[f.control('Sage',10,0x86a789,20,{intent:65,row:0})]}});
    const button=f.doc.querySelector('.shared-hud-peer');button.focus();
    assert(f.calls.some(([id,value])=>id===26&&value===65));
    f.state={...f.state,menuToken:2};button.click();
    assert.equal(f.calls.filter(([id])=>id===10).length,0);
    f.update({hud:{width:1280,height:720,controls:[f.control('Cloud',10,0xf1eee4,20,{intent:129,row:0})]}});
    f.doc.querySelector('.shared-hud-peer').click();
    assert(f.calls.some(([id,value])=>id===10&&value===129));
});

test('canvas Tab enters the accepted catalogue before Emscripten consumes the key',t=>{
    const f=fixture(t);let leaked=false;f.w.addEventListener('keydown',()=>{leaked=true;});
    f.canvas.focus();
    f.canvas.dispatchEvent(new f.w.KeyboardEvent('keydown',{key:'Tab',bubbles:true,cancelable:true}));
    assert(f.calls.some(([id])=>id===23));assert.equal(leaked,false);
    assert.equal(f.doc.activeElement,f.canvas);
    f.update({mode:'catalog',menuSelected:0,hud:{width:1280,height:720,controls:[f.control('Brick',10,0,20,{intent:65,row:0})]}});
    assert.equal(f.doc.activeElement,f.doc.querySelector('.shared-hud-peer'));
    assert(f.calls.some(([id,value])=>id===15&&value===1));
});

test('opening a palette retains keyboard focus across the first-render hitbox gap',t=>{
    const f=fixture(t),colours=f.doc.querySelector('[data-action="38"]');colours.focus();colours.click();
    assert.equal(f.doc.activeElement,colours);
    f.update({mode:'colours',menuSelected:0,hud:{width:1280,height:720,controls:[]}});
    f.update({hud:{width:1280,height:720,controls:[f.control('Sage',10,0x86a789,20,{intent:65,row:0})]}});
    assert.equal(f.doc.activeElement.getAttribute('aria-label'),'Sage');
    assert.equal(f.doc.querySelector('#shared-hud-accessibility').getAttribute('role'),'dialog');
});

test('a fresher raw observation cannot consume menu focus before its queued action is accepted',t=>{
    const f=fixture(t),colours=f.doc.querySelector('[data-action="38"]');colours.focus();
    // The frame advanced after the last presentation poll. Dispatch re-reads
    // this newer observation, but the palette command still awaits a frame.
    f.state={...f.state,observation:'2'};colours.click();
    assert.equal(f.doc.activeElement,colours,'A pending refresh must retain the opening control');
    f.update({mode:'colours',menuSelected:0,hud:{width:1280,height:720,controls:[]}});
    f.update({hud:{width:1280,height:720,controls:[f.control('Sage',10,0x86a789,20,{intent:65,row:0})]}});
    assert.equal(f.doc.activeElement.getAttribute('aria-label'),'Sage');
});

test('menu focus waits for accepted controls after many unpublished engine observations',t=>{
    const f=fixture(t),colours=f.doc.querySelector('[data-action="38"]');
    for(let tick=0;tick<100;tick++)f.update({player:{tick:String(tick)}});
    colours.focus();colours.click();
    assert.equal(f.doc.activeElement,colours,'A pending action must not return focus to the canvas');
    f.update({mode:'colours',menuSelected:0,hud:{width:1280,height:720,controls:[]}});
    f.update({hud:{width:1280,height:720,controls:[f.control('Sage',10,0x86a789,20,{intent:65,row:0})]}});
    assert.equal(f.doc.activeElement.getAttribute('aria-label'),'Sage','Focus follows the accepted menu after the hitbox gap');
});

test('radial selection keeps the same focused peer when a petal moves to the centre',t=>{
    const f=fixture(t);
    const category=f.control('Structure',13,1,20,{intent:65,row:-1});
    const piece=(label,row,x)=>f.control(label,10,0,x,{intent:65+row,row});
    f.update({mode:'catalog',menuSelected:0,hud:{width:1280,height:720,controls:[
        category,piece('Floor',1,130),piece('Wall',2,240),piece('Foundation',0,350)]}});
    const wall=f.doc.querySelector('[data-row="2"]');wall.focus();
    assert.equal(f.doc.activeElement,wall);
    const before=f.calls.filter(([id])=>id===26).length;
    f.update({menuSelected:2,hud:{width:1280,height:720,controls:[
        category,piece('Foundation',0,130),piece('Floor',1,240),piece('Wall',2,350)]}});
    assert.equal(f.doc.querySelector('[data-row="2"]'),wall,'Visual order must not recreate a semantic control');
    assert.equal(f.doc.activeElement,wall,'Focus must follow the selected piece into the centre');
    assert.equal(wall.style.left,'27.34375%');
    assert.equal(f.calls.filter(([id])=>id===26).length,before,'Repositioning must not dispatch a different selection');
});

test('repeated semantic actions remain separate peers when surrounding controls reorder',t=>{
    const f=fixture(t),close=f.control('Close',20,0,20,{intent:65});
    const next=f.control('Next',25,1,130,{intent:65});
    f.update({mode:'catalog',hud:{width:1280,height:720,controls:[close,next,{...close,x:240}]}});
    const closes=[...f.doc.querySelectorAll('[data-action="20"]')];
    assert.equal(closes.length,2);closes[1].focus();
    f.update({hud:{width:1280,height:720,controls:[next,close,{...close,x:350}]}});
    assert.deepEqual([...f.doc.querySelectorAll('[data-action="20"]')],closes);
    assert.equal(f.doc.activeElement,closes[1]);
    assert.equal(closes[1].style.left,'27.34375%');
});

test('a replaced menu context restores published selection without selecting row zero first',t=>{
    const f=fixture(t);
    const rows=token=>['Text size','High contrast','Reduced motion'].map((label,row)=>
        f.control(label,10,0,20+row*110,{intent:token*64+row+1,row}));
    f.update({mode:'settings',menuSelected:2,hud:{width:1280,height:720,controls:rows(1)}});
    f.doc.querySelector('[data-row="2"]').focus();f.calls.length=0;
    f.update({menuToken:2,menuSelected:2,hud:{width:1280,height:720,controls:rows(2)}});
    assert.equal(f.doc.activeElement.getAttribute('aria-label'),'Reduced motion');
    assert.deepEqual(f.calls.filter(([id])=>id===26),[[26,131]],'Only the accepted row should receive presentation focus');
});

test('shared menu keyboard focus wraps, Escape returns to canvas, and input stays owned',t=>{
    const f=fixture(t);
    f.update({mode:'pause',hud:{width:1280,height:720,controls:[f.control('Resume',10,0,20,{intent:65,row:0}),f.control('Save',10,0,130,{intent:66,row:1})]}});
    const [first,last]=f.doc.querySelectorAll('.shared-hud-peer');last.focus();
    let leaked=false;f.w.addEventListener('keydown',()=>{leaked=true;});
    last.dispatchEvent(new f.w.KeyboardEvent('keydown',{key:'Tab',bubbles:true,cancelable:true}));
    assert.equal(f.doc.activeElement,first);assert.equal(leaked,false);
    first.dispatchEvent(new f.w.KeyboardEvent('keydown',{key:'Escape',bubbles:true,cancelable:true}));
    assert(f.calls.some(([id])=>id===20));assert.equal(f.doc.activeElement,f.canvas);
});

test('shared HUD keeps preferences, dirty-world protection, failures and cleanup',t=>{
    const f=fixture(t);
    const dirty=new f.w.Event('beforeunload',{cancelable:true});f.w.dispatchEvent(dirty);assert(dirty.defaultPrevented);
    f.update({saveFailure:'Save failed: storage is full.'});
    assert.equal(f.doc.querySelector('[role=status]').textContent,'Save failed: storage is full.');
    f.update({creative:false});assert.equal(f.doc.querySelectorAll('.shared-hud-peer').length,0);
    assert.match(f.doc.querySelector('[role=status]').textContent,/unavailable/);
    f.app.cleanup();assert.equal(f.doc.getElementById('shared-hud-accessibility'),null);
    assert.equal(f.canvas.hasAttribute('aria-describedby'),false);
    const clean=new f.w.Event('beforeunload',{cancelable:true});f.w.dispatchEvent(clean);assert.equal(clean.defaultPrevented,false);
});

test('gameplay announcements are not masked by persistent save status or repeated after priority updates',t=>{
    const f=fixture(t),live=f.doc.querySelector('[role=status]');
    assert.equal(live.textContent,'Unsaved build');
    f.update({status:'Stand on dry ground to ride.'});
    assert.equal(live.textContent,'Stand on dry ground to ride.');
    f.update({});assert.equal(live.textContent,'Stand on dry ground to ride.');
    f.update({saveStatus:'Saving...',status:'Ready to place'});
    assert.equal(live.textContent,'Saving...');
    f.update({});assert.equal(live.textContent,'Saving...','An unchanged placement message must not replay after saving starts');
    f.update({saveStatus:'Saved build'});assert.equal(live.textContent,'Saved build');
    f.update({});assert.equal(live.textContent,'Saved build');
    f.update({status:'Move closer to place this brick.'});
    assert.equal(live.textContent,'Move closer to place this brick.');
    f.update({saveFailure:'Storage is full.',status:'Ready to place'});
    assert.equal(live.textContent,'Storage is full.');
    f.update({});assert.equal(live.textContent,'Storage is full.');
    f.update({saveFailure:''});assert.equal(live.textContent,'Saved build');
});

test('frontier publishes real expedition data and announces milestones without moving control focus',t=>{
    const f=fixture(t),live=f.doc.querySelector('[role=status]');
    f.update({frontier:true,piece:17,mode:'explore',region:'Dawnreach Highlands',chapter:'THE FIRST LIGHT',
        health:73,maxHealth:100,wood:24,stone:18,scrap:7,objectiveTitle:'Rekindle the ridge beacon',
        objectiveDetail:'Build a path to the old signal tower.',objectiveDistance:'84 m'});
    assert(f.doc.body.classList.contains('voxy-frontier'));
    const readout=f.doc.getElementById('shared-hud-world-status');
    assert.match(readout.textContent,/Health 73 of 100/);assert.match(readout.textContent,/Wood 24/);
    assert.match(readout.textContent,/84 m/);assert.match(live.textContent,/Rekindle the ridge beacon/);
    const button=f.doc.querySelector('.shared-hud-peer');button.focus();
    f.update({wood:29,health:61,objectiveDistance:'68 m'});
    assert.equal(f.doc.activeElement,button);assert.match(readout.textContent,/Wood 29/);
    assert.match(readout.textContent,/Health 61 of 100/);
    assert.match(live.textContent,/Rekindle the ridge beacon/,'Distance/resource ticks do not repeat announcements');
    f.update({milestone:'BEACON REKINDLED',objectiveTitle:'Beyond the ridge',objectiveDetail:'Discover the next valley.'});
    assert.equal(live.textContent,'BEACON REKINDLED');
    f.update({});assert.equal(live.textContent,'BEACON REKINDLED');
    f.app.cleanup();assert.equal(f.doc.getElementById('shared-hud-world-status'),null);
    assert.equal(f.doc.body.classList.contains('voxy-frontier'),false);
});

test('frontier journal and station menus retain exact row intents and shared keyboard focus',t=>{
    const f=fixture(t);
    f.update({frontier:true,piece:24,mode:'explore',hud:{...f.state.hud,controls:[f.control('Journal',16,0)]}});
    const journal=f.doc.querySelector('.shared-hud-peer');journal.focus();journal.click();
    assert(f.calls.some(([action])=>action===16));
    f.update({mode:'journal',menuTitle:'Expedition journal',menuSelected:0,hud:{...f.state.hud,controls:[f.control('Track beacon',10,3,20,{intent:67,row:0})]}});
    assert.equal(f.doc.activeElement.getAttribute('aria-label'),'Track beacon');
    assert.equal(f.doc.getElementById('shared-hud-accessibility').getAttribute('aria-modal'),'true');
    for(const mode of ['bag','workbench','chest','dialogue']) {
        f.update({mode});assert.equal(f.doc.querySelectorAll('.shared-hud-peer').length,1,mode);
    }
    f.doc.querySelector('.shared-hud-peer').click();
    assert(f.calls.some(([action,intent])=>action===10&&intent===67));
});

function audioFixture(t){
    const dom=new JSDOM('<canvas/>',{pretendToBeVisual:true}),w=dom.window;
    let opened=0,closed=0,current=null,hidden=false;
    const sources=[],params=[];
    const param=()=>{const result={value:0,events:[],cancelScheduledValues(){},setTargetAtTime(value,time){this.events.push([value,time]);},
        setValueAtTime(value,time){this.events.push([value,time]);},exponentialRampToValueAtTime(value,time){this.events.push([value,time]);}};params.push(result);return result;};
    const node=()=>({connect(){},disconnect(){this.disconnected=true;}});
    const source=kind=>{const result={...node(),kind,frequency:param(),playbackRate:param(),start(time){this.startedAt=time;},stop(time){this.stoppedAt=time;}};sources.push(result);return result;};
    Object.defineProperty(w.document,'hidden',{get:()=>hidden});
    w.AudioContext=class {
        constructor(){opened++;current=this;this.state='running';this.currentTime=0;this.sampleRate=8000;this.destination={};}
        createGain(){return {...node(),gain:param()};}
        createOscillator(){return source('tone');}
        createBufferSource(){return source('noise');}
        createBuffer(_channels,length){return {getChannelData:()=>new Float32Array(length)};}
        createBiquadFilter(){return {...node(),Q:param(),frequency:param()};}
        createStereoPanner(){return {...node(),pan:param()};}
        createDynamicsCompressor(){return {...node(),threshold:param(),knee:param(),ratio:param(),attack:param(),release:param()};}
        close(){closed++;return Promise.resolve();}
    };
    const audio=installFrontierAudio(w);
    const update=(changes={})=>{state={...state,...changes};audio.update(state);};
    let state={frontier:true,audioSequence:'1',audioEvent:'gather',audioEnabled:true,audioMood:'silent',audioPace:0};
    t.after(()=>{audio.cleanup();dom.window.close();});
    return {audio,sources,params,update,get opened(){return opened;},get closed(){return closed;},
        unlock(){w.document.dispatchEvent(new w.MouseEvent('pointerdown',{bubbles:true}));},
        advance(seconds){current.currentTime+=seconds;for(const item of sources)if(!item.disconnected&&item.stoppedAt<=current.currentTime)item.onended?.();},
        visible(value){hidden=!value;w.document.dispatchEvent(new w.Event('visibilitychange'));},
        get live(){return sources.filter(item=>!item.disconnected);},
    };
}

test('frontier audio only plays fresh accepted events after a gesture and respects mute',t=>{
    const f=audioFixture(t),event=(audioSequence,audioEvent='gather',audioEnabled=true)=>f.update({audioSequence,audioEvent,audioEnabled});
    event('1');event('2');assert.equal(f.opened,0);assert.equal(f.sources.length,0);
    f.unlock();assert.equal(f.opened,1);assert.equal(f.sources.length,0,'Unlock never replays the previous event');
    event('3');const gathered=f.sources.length;assert(gathered>0);assert(f.sources.some(source=>source.kind==='noise'),'Gather has a physical impact texture');
    event('3');assert.equal(f.sources.length,gathered);
    event('4','beacon',false);assert.equal(f.sources.length,gathered);
    event('4','beacon',true);assert.equal(f.sources.length,gathered,'Unmuting does not replay a muted event');
    f.advance(.2);event('5','beacon');assert(f.sources.length>gathered);
    const beacon=f.sources.length;event('6','unsupported');assert.equal(f.sources.length,beacon);
    f.audio.cleanup();assert.equal(f.closed,1);assert.equal(f.live.length,0);event('7');assert.equal(f.sources.length,beacon);
    f.unlock();assert.equal(f.opened,1);
});

test('frontier ambience evolves on accepted observations and stops across mute, menus and hidden tabs',t=>{
    const f=audioFixture(t);f.update({audioMood:'explore'});f.unlock();f.update();
    assert(f.sources.length>0,'Existing state observations start wind after unlock');
    f.advance(.2);f.update();assert(f.sources.some(source=>source.kind==='tone'),'Exploration has a sparse tonal score');
    f.advance(8);const count=f.sources.length;f.update();assert(f.sources.length>count,'Idle observations advance the soundtrack');
    f.update({audioEnabled:false});f.advance(.2);assert.equal(f.live.length,0,'Mute retires current and future scheduled voices');
    const muted=f.sources.length;f.advance(60);f.update({audioSequence:'2',audioEvent:'beacon'});assert.equal(f.sources.length,muted);
    f.update({audioEnabled:true,audioMood:'silent'});assert.equal(f.sources.length,muted,'No missed event or ambience enters a silent menu');
    f.update({audioMood:'camp'});assert(f.sources.length>muted);f.visible(false);f.advance(1);assert.equal(f.live.length,0);
    const hidden=f.sources.length;f.advance(60);f.update({audioSequence:'3',audioEvent:'hit'});assert.equal(f.sources.length,hidden);
    f.visible(true);f.update();assert(f.sources.length-hidden<15,'Returning schedules current ambience, not a backlog');
    f.update({frontier:false});f.advance(1);assert.equal(f.live.length,0,'Leaving the expedition retires all audio');
});

test('frontier footsteps follow authoritative pace and sound work remains bounded under rapid events',t=>{
    const f=audioFixture(t);f.update({audioMood:'combat'});f.unlock();f.update();
    f.advance(.15);f.update();const still=f.sources.length;
    f.update({audioPace:1});f.advance(.2);f.update();assert(f.sources.length>still,'Grounded movement creates footsteps');
    f.update({audioPace:0});f.advance(.2);const stationary=f.sources.length;f.update();assert.equal(f.sources.length,stationary,'Stationary players have no footsteps');
    for(let i=2;i<200;i++)f.update({audioSequence:String(i),audioEvent:'beacon'});
    assert(f.live.length<=48,'Rapid input cannot accumulate an unbounded audio graph');
    f.advance(20);f.update({audioMood:'silent'});f.advance(1);assert.equal(f.live.length,0,'All generated sources have finite lifetimes');
});

test('frontier sound preference restores through an accepted engine action and persists independently of saves',t=>{
    const f=fixture(t,false);
    f.update({frontier:true,audioEnabled:true});
    assert(f.calls.some(([action,value])=>action===41&&value===0));
    assert.equal(f.w.localStorage.getItem('voxys.frontier.audio.v1'),'false');
    f.update({audioEnabled:false});
    f.update({audioEnabled:true});
    assert.equal(f.w.localStorage.getItem('voxys.frontier.audio.v1'),'true');
    assert.equal(f.calls.filter(([action])=>action===41).length,1,'Do not restore over a later accepted user choice');
});
