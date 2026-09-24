import {test,afterEach} from 'node:test';
import assert from 'node:assert/strict';
import {readFile} from 'node:fs/promises';
import {JSDOM} from 'jsdom';
import {createBridge} from './src/bridge.js';
const bundle=await readFile(new URL('../web/build_ui.js',import.meta.url),'utf8');
const openFixtures=new Set();
afterEach(()=>{for(const dom of openFixtures)dom.window.close();openFixtures.clear();});
async function waitFor(predicate){const end=Date.now()+5000;while(!predicate()){if(Date.now()>end)throw Error('UI did not reach the expected state');await new Promise(r=>setTimeout(r,10));}}
const wait=()=>new Promise(resolve=>setTimeout(resolve,70));
function fixture(){
    const dom=new JSDOM('<!doctype html><canvas id="voxy-canvas" tabindex="0"></canvas>',{url:'http://localhost/',runScripts:'outside-only',pretendToBeVisual:true});
    openFixtures.add(dom);const w=dom.window;w.requestAnimationFrame=fn=>w.setTimeout(fn,0);w.cancelAnimationFrame=id=>w.clearTimeout(id);
    let state={creative:true,ready:true,mode:'build',piece:10,menuToken:1,observation:'1',rows:[],parts:0,paint:0,colourAvailable:true,canUndo:false,canRemove:false,valid:true,status:'Ready',dirty:true,saveStatus:'Unsaved build'};
    const calls=[];const engine={_get_adventure_state_json:()=>1,UTF8ToString:()=>JSON.stringify(state),_adventure_action:(id,value)=>calls.push([id,value])};
    return {w,engine,calls,get state(){return state;},set state(value){state=value;},advance(changes={}){state={...state,...changes,observation:String(Number(state.observation)+1)};},ready(){return waitFor(()=>w.document.querySelector('#build-ui')?.dataset.mode===state.mode);},close(){dom.window.close();openFixtures.delete(dom);}};
}
test('bridge rejects stale menu events and guards pending actions without blocking focus metadata',()=>{
    const f=fixture();let published;const b=createBridge(f.engine,f.w,s=>published=s);
    assert.equal(b.action(26,100,1),true);assert.equal(published.pending,false);
    assert.equal(b.action(2,8),true);assert.equal(b.action(2,9),false);
    f.advance({piece:8});b.refresh();assert.equal(published.pending,false);
    f.advance({menuToken:2});assert.equal(b.action(10,100,1),false);
    assert.equal(f.calls.filter(([id])=>id===10).length,0);
    f.advance({valid:false});b.refresh();assert.equal(b.action(4),false);
    b.own(true);f.w.document.querySelector('canvas').dispatchEvent(new f.w.Event('pointerdown',{bubbles:true}));
    assert.deepEqual(f.calls.at(-1),[15,0]);
    b.cleanup();assert.deepEqual(f.calls.at(-1),[19,0]);assert.equal(b.action(8),false);f.close();
});
test('bridge publishes only changed snapshots so an idle poll does not re-render',()=>{
    const f=fixture();const published=[];const b=createBridge(f.engine,f.w,s=>published.push(s));
    assert.equal(published.length,1);b.refresh();b.refresh();assert.equal(published.length,1);
    f.advance({piece:8});b.refresh();assert.equal(published.length,2);assert.equal(published.at(-1).piece,8);
    assert.equal(b.action(2,9),true);assert.equal(published.at(-1).pending,true);
    const count=published.length;b.refresh();assert.equal(published.length,count);
    f.advance({piece:9});b.refresh();assert.equal(published.at(-1).pending,false);assert.equal(published.at(-1).piece,9);
    f.state={...f.state,creative:false};b.refresh();b.refresh();assert.equal(published.at(-1).failed,true);
    f.state={...f.state,creative:true};b.refresh();assert.equal(published.at(-1).failed,undefined);
    b.cleanup();f.close();
});
test('engine ticks do not publish unchanged controls, but pending completion and preferences stay current',()=>{
    const f=fixture(),published=[],preferenceTicks=[];
    f.w.VoxyAdventurePreferences={install:()=>({tick:state=>preferenceTicks.push(state.preferencesRevision),cleanup(){}})};
    f.state={...f.state,preferencesRevision:'1',cannon:{available:true,nearby:false,ready:true,shots:0}};
    const b=createBridge(f.engine,f.w,state=>published.push(state));
    for(let tick=2;tick<=101;tick++){
        f.advance({player:{tick:String(tick),x:tick},camera:{yaw:tick/10},forest:{selectionMs:tick/100},
            cannon:{...f.state.cannon,shots:tick,yaw:tick/10},preferencesRevision:String(tick)});
        b.refresh();
    }
    assert.equal(published.length,1,'Frame diagnostics must not invalidate the controls');
    assert.equal(preferenceTicks.at(-1),'101','Preference transport must still see fresh accepted revisions');
    assert.equal(b.action(3),true);assert.equal(published.at(-1).pending,true);
    assert.equal(published.at(-1).observation,f.state.observation,'Admission uses the latest observation');
    assert.equal(b.action(3),false,'Pending guard still blocks repeated input');
    f.advance();b.refresh();assert.equal(published.at(-1).pending,false);
    assert.equal(published.length,3,'An accepted tick must release pending controls even without a visual change');
    f.advance({cannon:{...f.state.cannon,nearby:true}});b.refresh();
    assert.equal(published.length,4);assert.equal(published.at(-1).cannon.nearby,true);
    f.state={...f.state,menuToken:2};assert.equal(b.action(10,65,1),false,'Stale menu input is still rejected');
    b.cleanup();f.close();
});
test('pending input publishes the next accepted frame without idle animation work',()=>{
    const f=fixture(),frames=new Map(),published=[];let serial=0;
    f.w.requestAnimationFrame=callback=>{frames.set(++serial,callback);return serial;};
    f.w.cancelAnimationFrame=id=>frames.delete(id);
    const frame=()=>{const [id,callback]=frames.entries().next().value;frames.delete(id);callback();};
    const b=createBridge(f.engine,f.w,state=>published.push(state));
    b.refresh();assert.equal(frames.size,0,'Idle controls must not create animation callbacks');
    assert(b.action(26,65,1));assert.equal(frames.size,0,'Focus metadata needs no pending frame');
    assert(b.action(2,8));assert.equal(published.at(-1).pending,true);assert.equal(frames.size,1);
    b.refresh();b.refresh();assert.equal(frames.size,1,'Only one follow-up may be queued');
    frame();assert.equal(frames.size,1,'An unaccepted observation waits one more frame');
    f.advance({piece:8});frame();
    assert.equal(published.at(-1).piece,8);assert.equal(published.at(-1).pending,false);
    assert.equal(frames.size,0,'Completion ends animation work without an interval tick');
    const beforeStall=serial;assert(b.action(3));
    for(let attempt=0;attempt<8;attempt++)frame();
    assert.equal(serial-beforeStall,8);assert.equal(frames.size,0,'A stalled engine gets only eight fast follow-ups');
    b.refresh();assert.equal(frames.size,0,'Ordinary polls cannot restart the exhausted action budget');
    f.advance();b.refresh();assert.equal(published.at(-1).pending,false,'The interval still resolves a stalled action');
    assert(b.action(3));assert.equal(frames.size,1);
    f.advance();b.refresh();assert.equal(frames.size,0,'The hidden-tab interval can resolve and cancel the frame');
    assert(b.action(3));assert.equal(frames.size,1);
    b.cleanup();assert.equal(frames.size,0,'Cleanup cancels queued input work');f.close();
});
test('invalid runtime state fails closed; unsaved builds keep unload protection',()=>{
    const f=fixture();let published;const b=createBridge(f.engine,f.w,s=>published=s);
    const event=new f.w.Event('beforeunload',{cancelable:true});f.w.dispatchEvent(event);assert.equal(event.defaultPrevented,true);
    f.state={...f.state,creative:false};b.refresh();assert.equal(published.failed,true);assert.equal(b.action(2,8),false);
    const failedEvent=new f.w.Event('beforeunload',{cancelable:true});f.w.dispatchEvent(failedEvent);assert.equal(failedEvent.defaultPrevented,true);
    b.cleanup();f.close();
});
test('minimal HUD cycles real pieces, opens colour only on demand, and cleans up',async()=>{
    const f=fixture();f.state={...f.state,dirty:false};f.w.eval(bundle);const app=f.w.VoxyBuildUI.install(f.engine,f.w);await f.ready();
    const doc=f.w.document;assert.equal(doc.querySelector('[aria-label="Save build"]').textContent,'Save');assert.equal(doc.querySelectorAll('.bb-piece').length,15);
    assert.equal(doc.querySelector('.bb-popup'),null);assert.equal(doc.querySelector('[aria-label="Brick 2 × 4"]').getAttribute('aria-pressed'),'true');
    const wheel=new f.w.WheelEvent('wheel',{deltaY:100,bubbles:true,cancelable:true});doc.querySelector('.bb-hotbar').dispatchEvent(wheel);assert.equal(wheel.defaultPrevented,true);
    assert.deepEqual(f.calls.filter(([id])=>id===2).at(-1),[2,2]);
    f.advance({piece:2});await new Promise(r=>setTimeout(r,140));
    doc.querySelector('[data-popup-toggle="colour"]').click();await wait();assert.ok(doc.querySelector('#bb-colour-popup'));
    doc.querySelector('[aria-label="Sage colour"]').click();await wait();assert.deepEqual(f.calls.filter(([id])=>id===30).at(-1),[30,0x86a789]);assert.equal(doc.querySelector('#bb-colour-popup'),null);
    f.advance({paint:0x86a789});await new Promise(r=>setTimeout(r,140));
    doc.querySelector('[aria-label="Open menu"]').click();await wait();assert.deepEqual(f.calls.filter(([id])=>id===31).at(-1),[31,0]);
    assert.equal(doc.querySelector('.bb-tray'),null);assert.equal(doc.querySelector('#adventure-quest'),null);
    app.cleanup();assert.equal(doc.querySelector('#build-ui'),null);assert.deepEqual(f.calls.at(-1),[19,0]);f.close();
});
test('swimming hints follow water state in building and explore modes',async()=>{
    const f=fixture();f.state={...f.state,swimming:true};f.w.eval(bundle);const app=f.w.VoxyBuildUI.install(f.engine,f.w);await f.ready();
    const doc=f.w.document;assert.match(doc.querySelector('.bb-hints').textContent,/Space · riseX · diveRight-drag · steer/);
    f.advance({mode:'explore'});await new Promise(r=>setTimeout(r,140));
    assert.match(doc.querySelector('.bb-walk-dock').textContent,/Space · rise/);assert.match(doc.querySelector('.bb-walk-dock').textContent,/X · dive/);
    f.advance({swimming:false});await new Promise(r=>setTimeout(r,140));
    assert.doesNotMatch(doc.querySelector('.bb-walk-dock').textContent,/X · dive/);
    app.cleanup();f.close();
});
test('modal uses authoritative intents, traps focus, and does not block click after row focus',async()=>{
    const f=fixture();f.state={...f.state,mode:'pause',menuSelected:0,rows:[{label:'Return to building',enabled:true,intent:65},{label:'Save build',enabled:true,intent:66}]};
    f.w.eval(bundle);const app=f.w.VoxyBuildUI.install(f.engine,f.w);await f.ready();
    const doc=f.w.document,second=doc.querySelector('[data-row="1"]');second.focus();second.click();await wait();
    assert.ok(f.calls.some(([id,v])=>id===26&&v===66));assert.ok(f.calls.some(([id,v])=>id===10&&v===66));
    f.advance();await new Promise(r=>setTimeout(r,140));
    const last=doc.querySelector('.bb-new-world');last.focus();last.dispatchEvent(new f.w.KeyboardEvent('keydown',{key:'Tab',bubbles:true,cancelable:true}));assert.equal(doc.activeElement,doc.querySelector('[aria-label="Close menu"]'));
    app.cleanup();f.close();
});

test('motorbike button routes mount and mounted HUD replaces building controls',async()=>{
    const f=fixture();f.w.eval(bundle);const app=f.w.VoxyBuildUI.install(f.engine,f.w);await f.ready();
    const button=[...f.w.document.querySelectorAll('button')].find(b=>b.textContent==='MotorbikeM');
    assert.ok(button);button.click();await wait();assert.deepEqual(f.calls.filter(([id])=>id===32).at(-1),[32,0]);
    f.advance({riding:true,mode:'explore',build:false});await new Promise(r=>setTimeout(r,140));
    assert.equal(f.w.document.querySelector('.bb-hotbar'),null);
    assert.ok(f.w.document.querySelector('[aria-label="Motorbike controls"]'));
    assert.ok([...f.w.document.querySelectorAll('button')].some(b=>b.textContent==='Get offM'));
    app.cleanup();f.close();
});

test('cannon mode owns firing controls and disables fire until physical collision is ready',async()=>{
    const f=fixture();f.state={...f.state,mode:'explore',cannon:{available:true,active:true,ready:false}};
    f.w.eval(bundle);const app=f.w.VoxyBuildUI.install(f.engine,f.w);await f.ready();
    const doc=f.w.document,controls=doc.querySelector('[aria-label="Cannon controls"]');assert.ok(controls);
    assert.equal(doc.querySelector('.bb-hotbar'),null);
    assert.equal([...doc.querySelectorAll('button')].some(b=>b.textContent==='MotorbikeM'),false);
    assert.equal(controls.querySelector('button').disabled,true);
    assert.match(controls.textContent,/Preparing cannon/);
    f.advance({cannon:{available:true,active:true,ready:true}});await new Promise(r=>setTimeout(r,140));
    doc.querySelector('[aria-label="Cannon controls"] button').click();await wait();
    assert.deepEqual(f.calls.filter(([id])=>id===34).at(-1),[34,0]);
    f.advance();await new Promise(r=>setTimeout(r,140));
    [...doc.querySelectorAll('button')].find(b=>b.textContent==='Leave cannonC').click();await wait();
    assert.deepEqual(f.calls.filter(([id])=>id===33).at(-1),[33,0]);
    app.cleanup();f.close();
});


test('cannon impact controls show shot and damage state without exposing manual test actions',async()=>{
    const f=fixture();f.state={...f.state,mode:'explore',cannon:{available:true,active:true,ready:true,wallReady:true,wallReleased:false,wallBusy:false,impacts:0}};
    f.w.eval(bundle);const app=f.w.VoxyBuildUI.install(f.engine,f.w);await f.ready();
    const find=text=>[...f.w.document.querySelectorAll('button')].find(b=>b.textContent===text);
    const controls=()=>f.w.document.querySelector('[aria-label="Cannon controls"]');
    assert.equal(find('Release wall'),undefined);assert.equal(find('Remove support brick'),undefined);
    assert.equal(find('Rebuild wall').disabled,true);assert.equal(find('FireSpace').disabled,false);
    f.advance({cannon:{...f.state.cannon,awaitingHit:true}});await new Promise(r=>setTimeout(r,140));
    assert.equal(find('FireSpace').disabled,true);assert.equal(find('Leave cannonC').disabled,true);assert.match(controls().textContent,/Shot away…/);
    // A miss/expired projectile returns to aiming without claiming breakage.
    f.advance({cannon:{...f.state.cannon,awaitingHit:false}});await new Promise(r=>setTimeout(r,140));
    assert.equal(find('FireSpace').disabled,false);assert.equal(find('Leave cannonC').disabled,false);assert.doesNotMatch(controls().textContent,/bricks freed/);
    f.advance({cannon:{available:true,active:true,ready:false,wallReady:false,wallReleased:true,wallBusy:true,impacts:1}});await new Promise(r=>setTimeout(r,140));
    assert.equal(find('FireSpace').disabled,true);assert.equal(find('Leave cannonC').disabled,true);
    assert.match(controls().textContent,/Let the pieces settle/);assert.match(controls().textContent,/Damage lasts this session · Rebuild to save/);
    assert.equal(find('Rebuild wall').disabled,false);
    f.advance({cannon:{...f.state.cannon,ready:true,wallReady:true,wallBusy:false}});await new Promise(r=>setTimeout(r,140));
    assert.match(controls().textContent,/Hit · bricks freed/);assert.equal(find('Leave cannonC').disabled,false);
    assert.equal(find('FireSpace').disabled,false); // Certified settled roots can be hit again.
    find('Rebuild wall').click();await wait();assert.deepEqual(f.calls.filter(([id])=>id===36).at(-1),[36,0]);
    f.advance({cannon:{available:true,active:true,ready:true,wallReady:true,wallReleased:false,wallBusy:false,impacts:1}});await new Promise(r=>setTimeout(r,140));
    assert.equal(find('FireSpace').disabled,false);assert.equal(find('Rebuild wall').disabled,true);
    assert.doesNotMatch(controls().textContent,/bricks freed|Damage lasts this session/);
    f.advance({cannon:{available:true,active:true,ready:false,wallReady:false,wallReleased:false,wallBusy:true,wallFailed:true,wallMessage:'Pieces haven’t settled; rebuild the wall.'}});await new Promise(r=>setTimeout(r,140));
    assert.equal(find('Rebuild wall').disabled,false);assert.match(controls().textContent,/Pieces haven’t settled; rebuild the wall/);
    assert.doesNotMatch(controls().textContent,/Let the pieces settle|bricks freed/);
    f.advance({cannon:{...f.state.cannon,wallMessage:'Physics stopped safely. Reload the world to restore the house.'}});await new Promise(r=>setTimeout(r,140));
    assert.match(controls().textContent,/Reload the world/);
    f.advance({cannon:{...f.state.cannon,wallMessage:'Impact detection stopped safely. Reload the world.'}});await new Promise(r=>setTimeout(r,140));
    assert.match(controls().textContent,/Impact detection stopped safely. Reload the world/);
    assert.doesNotMatch(controls().textContent,/Let the pieces settle/);
    assert.equal(find('FireSpace').disabled,true);
    // A rebuilt, ready wall does not repair a failed hit-event stream.
    f.advance({cannon:{...f.state.cannon,wallBusy:false,wallReleased:false,wallReady:true,wallFailed:true,ready:false}});await new Promise(r=>setTimeout(r,140));
    assert.match(controls().textContent,/Impact detection stopped safely. Reload the world/);
    assert.equal(find('FireSpace').disabled,true);
    f.advance({cannon:{...f.state.cannon,wallMessage:'Wall collision could not be installed. Rebuild the wall.'}});await new Promise(r=>setTimeout(r,140));
    assert.match(controls().textContent,/Wall collision could not be installed. Rebuild the wall/);
    assert.equal(find('Rebuild wall').disabled,false);
    assert.equal(f.calls.some(([id])=>id===35||id===37),false);
    app.cleanup();f.close();
});


test('repeated cannon refusals show feedback again without extending it on ordinary frames',async()=>{
    const f=fixture();f.state={...f.state,statusEvent:'0',mode:'explore'};
    const nativeTimeout=f.w.setTimeout.bind(f.w),nativeClear=f.w.clearTimeout.bind(f.w);
    let expire,toastTimers=0;
    f.w.setTimeout=(callback,delay,...args)=>{
        if(delay===3000){expire=callback;return -(++toastTimers);}
        return nativeTimeout(callback,delay,...args);
    };
    f.w.clearTimeout=id=>{if(id>=0)nativeClear(id);};
    f.w.eval(bundle);const app=f.w.VoxyBuildUI.install(f.engine,f.w);await f.ready();
    // Let the initial status effect establish its baseline before the event.
    await wait();
    const message='Walk to the red cannon, then press C.';
    f.advance({status:message,statusEvent:'1'});await waitFor(()=>f.w.document.querySelector('.bb-toast').textContent===message&&toastTimers===1);
    assert.equal(f.w.document.querySelector('.bb-toast').textContent,message);
    assert.equal(toastTimers,1);expire();await waitFor(()=>f.w.document.querySelector('.bb-toast').textContent==='');
    assert.equal(f.w.document.querySelector('.bb-toast').textContent,'');
    f.advance();await new Promise(r=>setTimeout(r,140));
    assert.equal(toastTimers,1);assert.equal(f.w.document.querySelector('.bb-toast').textContent,'');
    f.advance({statusEvent:'2'});await waitFor(()=>f.w.document.querySelector('.bb-toast').textContent===message&&toastTimers===2);
    assert.equal(f.w.document.querySelector('.bb-toast').textContent,message);
    assert.equal(toastTimers,2);app.cleanup();f.close();
});

test('distant cannon offers explicit travel while wall inspection explains aiming controls',async()=>{
    const f=fixture();f.state={...f.state,mode:'explore',cannon:{available:true,active:false,nearby:false,distanceStuds:23.2}};
    f.w.eval(bundle);const app=f.w.VoxyBuildUI.install(f.engine,f.w);await f.ready();
    const visit=f.w.document.querySelector('button[aria-label="Visit cannon"]');
    assert.ok(visit);visit.click();await wait();
    assert.deepEqual(f.calls.filter(([id])=>id===33).at(-1),[33,0]);
    f.advance({cannon:{available:true,active:false,nearby:true,distanceStuds:8}});await new Promise(r=>setTimeout(r,140));
    assert.ok(f.w.document.querySelector('button[aria-label="Cannon"]'));
    f.advance({cannon:{available:true,active:true,ready:true,wallReady:true,nearby:true,inspectingWall:true}});await new Promise(r=>setTimeout(r,140));
    assert.ok(f.w.document.querySelector('button[aria-label="Leave cannon"]'));
    assert.match(f.w.document.querySelector('[aria-label="Cannon controls"]').textContent,/Wall close-up · A\/D or W\/S to return to aiming/);
    app.cleanup();f.close();
});

test('catalog search retains original row intent and exposes an empty state',async()=>{
    const f=fixture();f.state={...f.state,mode:'catalog',catalogCategory:1,menuSelected:0,
        rows:[{label:'Brick 1 × 2',pieceKind:8,enabled:true,intent:81},{label:'Brick 2 × 4',pieceKind:10,enabled:true,intent:94}]};
    f.w.eval(bundle);const app=f.w.VoxyBuildUI.install(f.engine,f.w);await f.ready();const doc=f.w.document;
    assert.equal([...doc.querySelectorAll('.bb-filters button')].find(b=>b.getAttribute('aria-pressed')==='true').textContent,'Bricks');
    const input=doc.querySelector('input[type=search]');input.focus();input.value='2 × 4';input.dispatchEvent(new f.w.Event('input',{bubbles:true}));await wait();
    assert.equal(doc.querySelectorAll('.bb-catalog-piece').length,1);assert.equal(doc.querySelector('.bb-catalog-piece').dataset.row,'1');
    assert.equal(doc.activeElement,input);
    doc.querySelector('.bb-catalog-piece').click();await wait();assert.deepEqual(f.calls.filter(([id])=>id===10).at(-1),[10,94]);
    f.advance();await new Promise(r=>setTimeout(r,140));input.value='no such piece';input.dispatchEvent(new f.w.Event('input',{bubbles:true}));await wait();
    assert.match(doc.querySelector('.bb-empty').textContent,/No pieces found/);doc.querySelector('.bb-empty button').click();await wait();
    assert.equal(doc.querySelectorAll('.bb-catalog-piece').length,2);
    app.cleanup();f.close();
});

test('palette keyboard dismissal restores its trigger and keeps gameplay input owned',async()=>{
    const f=fixture();f.w.eval(bundle);const app=f.w.VoxyBuildUI.install(f.engine,f.w);await f.ready();const doc=f.w.document;
    const trigger=doc.querySelector('[data-popup-toggle="colour"]');trigger.click();await wait();
    assert.equal(doc.activeElement.getAttribute('aria-label'),'Close colour palette');
    doc.activeElement.dispatchEvent(new f.w.KeyboardEvent('keydown',{key:'Escape',bubbles:true,cancelable:true}));await wait();
    assert.equal(doc.querySelector('.bb-popup'),null);assert.equal(doc.activeElement,trigger);assert.equal(trigger.getAttribute('aria-expanded'),'false');

    app.cleanup();f.close();
});

test('quick tools dispatch existing commands and accessibility preferences reach the root',async()=>{
    const f=fixture();f.state={...f.state,canUndo:false,textScale:1.5,highContrast:true,reducedMotion:true};
    f.w.eval(bundle);const app=f.w.VoxyBuildUI.install(f.engine,f.w);await f.ready();const doc=f.w.document;
    assert.equal(doc.querySelector('[aria-label="Undo last piece"]').disabled,true);
    assert.equal(doc.querySelector('#build-ui').dataset.contrast,'true');assert.equal(doc.querySelector('#build-ui').dataset.motion,'reduced');
    assert.equal(doc.querySelector('#build-ui').style.getPropertyValue('--bb-scale'),'1.5');
    doc.querySelector('[aria-label="Rotate piece"]').click();await wait();assert.deepEqual(f.calls.filter(([id])=>id===3).at(-1),[3,0]);
    f.advance({canUndo:true});await new Promise(r=>setTimeout(r,140));doc.querySelector('[aria-label="Undo last piece"]').click();await wait();
    assert.deepEqual(f.calls.filter(([id])=>id===6).at(-1),[6,0]);
    app.cleanup();f.close();
});
