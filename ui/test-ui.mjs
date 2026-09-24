import {test} from 'node:test';
import assert from 'node:assert/strict';
import {readFile} from 'node:fs/promises';
import {JSDOM} from 'jsdom';
import {createBridge} from './src/bridge.js';
const bundle=await readFile(new URL('../web/build_ui.js',import.meta.url),'utf8');
const wait=()=>new Promise(resolve=>setTimeout(resolve,70));
function fixture(){
    const dom=new JSDOM('<!doctype html><canvas id="voxy-canvas" tabindex="0"></canvas>',{url:'http://localhost/',runScripts:'outside-only',pretendToBeVisual:true});
    const w=dom.window;w.requestAnimationFrame=fn=>w.setTimeout(fn,0);w.cancelAnimationFrame=id=>w.clearTimeout(id);
    let state={creative:true,ready:true,mode:'build',piece:10,menuToken:1,observation:'1',rows:[],parts:0,paint:0,colourAvailable:true,canUndo:false,canRemove:false,valid:true,status:'Ready',dirty:true,saveStatus:'Unsaved build'};
    const calls=[];const engine={_get_adventure_state_json:()=>1,UTF8ToString:()=>JSON.stringify(state),_adventure_action:(id,value)=>calls.push([id,value])};
    return {w,engine,calls,get state(){return state;},set state(value){state=value;},advance(changes={}){state={...state,...changes,observation:String(Number(state.observation)+1)};},close(){dom.window.close();}};
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
test('invalid runtime state fails closed; unsaved builds keep unload protection',()=>{
    const f=fixture();let published;const b=createBridge(f.engine,f.w,s=>published=s);
    const event=new f.w.Event('beforeunload',{cancelable:true});f.w.dispatchEvent(event);assert.equal(event.defaultPrevented,true);
    f.state={...f.state,creative:false};b.refresh();assert.equal(published.failed,true);assert.equal(b.action(2,8),false);
    const failedEvent=new f.w.Event('beforeunload',{cancelable:true});f.w.dispatchEvent(failedEvent);assert.equal(failedEvent.defaultPrevented,true);
    b.cleanup();f.close();
});
test('minimal HUD cycles real pieces, opens colour only on demand, and cleans up',async()=>{
    const f=fixture();f.state={...f.state,dirty:false};f.w.eval(bundle);const app=f.w.VoxyBuildUI.install(f.engine,f.w);await wait();
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
    const f=fixture();f.state={...f.state,swimming:true};f.w.eval(bundle);const app=f.w.VoxyBuildUI.install(f.engine,f.w);await wait();
    const doc=f.w.document;assert.match(doc.querySelector('.bb-hints').textContent,/Space · riseX · diveRight-drag · steer/);
    f.advance({mode:'explore'});await new Promise(r=>setTimeout(r,140));
    assert.match(doc.querySelector('.bb-walk-dock').textContent,/Space · rise/);assert.match(doc.querySelector('.bb-walk-dock').textContent,/X · dive/);
    f.advance({swimming:false});await new Promise(r=>setTimeout(r,140));
    assert.doesNotMatch(doc.querySelector('.bb-walk-dock').textContent,/X · dive/);
    app.cleanup();f.close();
});
test('modal uses authoritative intents, traps focus, and does not block click after row focus',async()=>{
    const f=fixture();f.state={...f.state,mode:'pause',menuSelected:0,rows:[{label:'Return to building',enabled:true,intent:65},{label:'Save build',enabled:true,intent:66}]};
    f.w.eval(bundle);const app=f.w.VoxyBuildUI.install(f.engine,f.w);await wait();
    const doc=f.w.document,second=doc.querySelector('[data-row="1"]');second.focus();second.click();await wait();
    assert.ok(f.calls.some(([id,v])=>id===26&&v===66));assert.ok(f.calls.some(([id,v])=>id===10&&v===66));
    f.advance();await new Promise(r=>setTimeout(r,140));
    const last=doc.querySelector('.bb-new-world');last.focus();last.dispatchEvent(new f.w.KeyboardEvent('keydown',{key:'Tab',bubbles:true,cancelable:true}));assert.equal(doc.activeElement,doc.querySelector('[aria-label="Close menu"]'));
    app.cleanup();f.close();
});

test('motorbike button routes mount and mounted HUD replaces building controls',async()=>{
    const f=fixture();f.w.eval(bundle);const app=f.w.VoxyBuildUI.install(f.engine,f.w);await wait();
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
    f.w.eval(bundle);const app=f.w.VoxyBuildUI.install(f.engine,f.w);await wait();
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
    f.w.eval(bundle);const app=f.w.VoxyBuildUI.install(f.engine,f.w);await wait();
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
    f.w.eval(bundle);const app=f.w.VoxyBuildUI.install(f.engine,f.w);await wait();
    const message='Walk to the red cannon, then press C.';
    f.advance({status:message,statusEvent:'1'});await new Promise(r=>setTimeout(r,140));
    assert.equal(f.w.document.querySelector('.bb-toast').textContent,message);
    assert.equal(toastTimers,1);expire();await wait();
    assert.equal(f.w.document.querySelector('.bb-toast').textContent,'');
    f.advance();await new Promise(r=>setTimeout(r,140));
    assert.equal(toastTimers,1);assert.equal(f.w.document.querySelector('.bb-toast').textContent,'');
    f.advance({statusEvent:'2'});await new Promise(r=>setTimeout(r,140));
    assert.equal(f.w.document.querySelector('.bb-toast').textContent,message);
    assert.equal(toastTimers,2);app.cleanup();f.close();
});

test('distant cannon offers explicit travel while wall inspection explains aiming controls',async()=>{
    const f=fixture();f.state={...f.state,mode:'explore',cannon:{available:true,active:false,nearby:false,distanceStuds:23.2}};
    f.w.eval(bundle);const app=f.w.VoxyBuildUI.install(f.engine,f.w);await wait();
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
