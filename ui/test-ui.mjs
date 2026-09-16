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
test('invalid runtime state fails closed; unsaved builds keep unload protection',()=>{
    const f=fixture();let published;const b=createBridge(f.engine,f.w,s=>published=s);
    const event=new f.w.Event('beforeunload',{cancelable:true});f.w.dispatchEvent(event);assert.equal(event.defaultPrevented,true);
    f.state={...f.state,creative:false};b.refresh();assert.equal(published.failed,true);assert.equal(b.action(2,8),false);
    const failedEvent=new f.w.Event('beforeunload',{cancelable:true});f.w.dispatchEvent(failedEvent);assert.equal(failedEvent.defaultPrevented,true);
    b.cleanup();f.close();
});
test('minimal HUD cycles real pieces, opens colour only on demand, and cleans up',async()=>{
    const f=fixture();f.state={...f.state,dirty:false};f.w.eval(bundle);const app=f.w.VoxyBuildUI.install(f.engine,f.w);await wait();
    const doc=f.w.document;assert.equal(doc.querySelector('.bb-top-actions button').textContent,'Save');assert.equal(doc.querySelectorAll('.bb-piece').length,15);
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
test('modal uses authoritative intents, traps focus, and does not block click after row focus',async()=>{
    const f=fixture();f.state={...f.state,mode:'pause',menuSelected:0,rows:[{label:'Return to building',enabled:true,intent:65},{label:'Save build',enabled:true,intent:66}]};
    f.w.eval(bundle);const app=f.w.VoxyBuildUI.install(f.engine,f.w);await wait();
    const doc=f.w.document,second=doc.querySelector('[data-row="1"]');second.focus();second.click();await wait();
    assert.ok(f.calls.some(([id,v])=>id===26&&v===66));assert.ok(f.calls.some(([id,v])=>id===10&&v===66));
    f.advance();await new Promise(r=>setTimeout(r,140));
    const last=doc.querySelector('.bb-new-world');last.focus();last.dispatchEvent(new f.w.KeyboardEvent('keydown',{key:'Tab',bubbles:true,cancelable:true}));assert.equal(doc.activeElement,doc.querySelector('[aria-label="Close menu"]'));
    app.cleanup();f.close();
});
