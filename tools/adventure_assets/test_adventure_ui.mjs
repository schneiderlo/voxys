import test from 'node:test';
import assert from 'node:assert/strict';
import {createRequire} from 'node:module';
import {testDOM} from '../../scripts/cove_ui_test_dom.mjs';
const require=createRequire(import.meta.url),api=require('../../web/adventure_ui.js');

function fixture(beforeInstall){
    const f=testDOM();let serial=0;
    f.timers=new Map();
    f.environment.setInterval=(callback,milliseconds)=>{assert.equal(milliseconds,250);f.timers.set(++serial,callback);return serial;};
    f.environment.clearInterval=id=>f.timers.delete(id);
    f.state={ready:true,build:false,piece:1,status:'Choose a home site.',valid:false,
        wood:90,stone:80,scrap:16,menu:'',rows:[],saveStatus:'Unsaved changes',dirty:true};
    f.actions=[];f.refuse=false;f.readError=false;
    f.engine={_get_adventure_state_json(){if(f.readError)throw Error('unavailable');return JSON.stringify(f.state);},
        UTF8ToString:value=>value,_adventure_action(action,value){f.actions.push([action,value]);return f.refuse?0:1;}};
    beforeInstall?.(f);f.ui=api.install(f.engine,f.environment);
    f.el=id=>f.document.getElementById(id);f.tick=()=>f.ui.refresh();
    return f;
}

test('read-only polling presents actual supplies without invoking commands',()=>{
    const f=fixture();assert.equal(f.timers.size,1);
    assert.equal(f.el('adventure-stock').textContent,'Wood 90 · Stone 80 · Scrap 16');
    assert(f.el('adventure-builder').hidden);
    f.tick();f.tick();assert.deepEqual(f.actions,[]);
    f.ui.cleanup();assert.equal(f.timers.size,0);assert.equal(f.el('adventure-ui'),null);
});
test('building controls send only bounded piece/placement intents and require valid preview',()=>{
    const f=fixture();f.el('adventure-place').click();assert.deepEqual(f.actions,[]);
    f.state.build=true;f.state.valid=true;f.state.previewReason='Ready to place.';f.tick();
    f.el('adventure-piece').value='12';f.el('adventure-piece').dispatchEvent(new f.Event('change'));
    f.el('adventure-place').click();f.el('adventure-lower').click();f.el('adventure-raise').click();
    assert.deepEqual(f.actions,[[2,12],[4,0],[12,-1],[12,1]]);
    f.el('adventure-piece').value='999';f.el('adventure-piece').dispatchEvent(new f.Event('change'));
    assert.equal(f.actions.length,4);f.ui.cleanup();
});
test('refused commands cannot alter displayed authority or inventory',()=>{
    const f=fixture();f.refuse=true;f.el('adventure-build').click();
    assert(f.el('adventure-builder').hidden);
    assert.equal(f.el('adventure-stock').textContent,'Wood 90 · Stone 80 · Scrap 16');
    assert.equal(f.state.wood,90);f.ui.cleanup();
});
test('starter room is a separate paid-layout intent with actual cost and validity',()=>{
    const f=fixture();f.el('adventure-starter').click();assert.deepEqual(f.actions,[[14,0]]);
    assert(f.el('adventure-builder').hidden,'request alone cannot activate the preview');
    f.state.build=true;f.state.piece=0;f.state.selected='Starter room';
    f.state.costText='70 wood · 16 stone · 8 scrap';f.state.previewReason='Move out of the room preview.';f.tick();
    assert(!f.el('adventure-builder').hidden);assert(f.el('adventure-piece').hidden);
    assert.equal(f.el('adventure-piece').options.length,14,'no fake catalog piece added');
    assert.equal(f.el('adventure-starter').getAttribute('aria-pressed'),'true');
    assert.equal(f.el('adventure-cost').textContent,f.state.costText);
    assert.equal(f.el('adventure-place').textContent,'Place room');
    f.el('adventure-place').click();assert.deepEqual(f.actions,[[14,0]]);
    f.state.valid=true;f.tick();f.el('adventure-place').click();assert.deepEqual(f.actions,[[14,0],[4,0]]);
    assert.equal(f.state.wood,90,'only core acceptance can spend supplies');f.ui.cleanup();
});
test('starter room can return to real catalog pieces without sending piece zero',()=>{
    const f=fixture();f.state.build=true;f.state.piece=0;f.state.selected='Starter room';f.tick();
    f.el('adventure-individual').click();assert.deepEqual(f.actions,[[13,0]]);
    f.state.piece=3;f.state.selected='Wall';f.tick();
    assert(!f.el('adventure-piece').hidden);assert(f.el('adventure-individual').hidden);
    assert.equal(f.el('adventure-piece').value,'3');assert.equal(f.el('adventure-place').textContent,'Place');
    f.el('adventure-piece').value='5';f.el('adventure-piece').dispatchEvent(new f.Event('change'));
    assert.deepEqual(f.actions,[[13,0],[2,5]]);f.ui.cleanup();
});
test('interaction rows preserve order, disabled eligibility and unchanged focus',()=>{
    const f=fixture();f.state.menu='Chest';f.state.rows=[{label:'Store wood',enabled:true},{label:'Take stone',enabled:false}];f.tick();
    const rows=f.el('adventure-menu-rows').children,first=rows[0],focusCount=first.focusCount;
    first.click();rows[1].click();assert.deepEqual(f.actions,[[10,0]]);
    f.tick();f.tick();assert.equal(f.el('adventure-menu-rows').children[0],first);
    assert.equal(first.focusCount,focusCount);assert.equal(f.document.activeElement,first);
    f.state.rows[1].enabled=true;f.tick();f.el('adventure-menu-rows').children[1].click();
    assert.deepEqual(f.actions,[[10,0],[10,1]]);f.ui.cleanup();
});
test('save errors and actual dirty state remain visible until confirmed state changes',()=>{
    const f=fixture();f.state.saveStatus='Storage is full. Your changes are still here.';f.tick();
    assert.equal(f.el('adventure-save-status').textContent,f.state.saveStatus);
    f.el('adventure-save').click();assert.deepEqual(f.actions,[[8,0]]);
    assert.equal(f.el('adventure-save-status').textContent,f.state.saveStatus);
    const e=new f.Event('beforeunload');for(const fn of f.environment.events.get('beforeunload'))fn(e);
    assert(e.defaultPrevented);
    f.state.dirty=false;f.state.saveStatus='Home and progress saved.';f.tick();
    const clean=new f.Event('beforeunload');for(const fn of f.environment.events.get('beforeunload'))fn(clean);
    assert(!clean.defaultPrevented);f.ui.cleanup();assert.equal(f.environment.events.get('beforeunload').size,0);
});
test('a full backpack and chest expose all 64 transfer choices',()=>{
    const f=fixture();f.state.menu='Chest';
    f.state.rows=Array.from({length:64},(_,i)=>({label:`${i<32?'Store':'Take'} Wood (${i+1})`,enabled:true}));
    f.tick();const rows=f.el('adventure-menu-rows').children;
    assert.equal(rows.length,64);rows[63].click();assert.deepEqual(f.actions,[[10,63]]);f.ui.cleanup();
});
test('unavailable state disables actions and recovery needs a new authoritative read',()=>{
    const f=fixture();f.readError=true;f.tick();f.el('adventure-save').click();f.el('adventure-build').click();
    assert.deepEqual(f.actions,[]);assert.match(f.el('adventure-status').textContent,/unavailable/);
    f.readError=false;f.tick();f.el('adventure-save').click();assert.deepEqual(f.actions,[[8,0]]);f.ui.cleanup();
});
test('an empty inactive runtime cannot leave actionable stale controls',()=>{
    const f=fixture();f.state={};f.tick();f.el('adventure-save').click();f.el('adventure-build').click();
    assert.deepEqual(f.actions,[]);assert.match(f.el('adventure-status').textContent,/unavailable/);
    assert(f.el('adventure-piece').disabled||f.el('adventure-build').disabled);f.ui.cleanup();
});
test('full 1024-part observations keep controls available within the explicit bound',()=>{
    const f=fixture();
    f.state.structures=Array.from({length:4},(_,s)=>({id:String(1+s),parts:Array.from({length:256},(_,p)=>({
        id:String(18446744073709000000n+BigInt(s*256+p)),kind:10,x:4095.1234567890123,y:599.1234567890123,z:-4095.1234567890123,yaw:3}))}));
    f.state.components=Array.from({length:32},(_,c)=>({id:String(9000+c),part:String(18446744073709000000n+BigInt(c)),kind:2,wood:31968,stone:31968,scrap:31968}));
    f.state.camera={viewProjection:Array(16).fill(1.1234567890123456),origin:[4096,0,-4096],width:1280,height:800};
    const size=JSON.stringify(f.state).length;
    assert(size>65536&&size<api.maximumStateCharacters);
    f.tick();assert(!f.el('adventure-save').disabled);f.el('adventure-save').click();
    assert.deepEqual(f.actions,[[8,0]]);f.ui.cleanup();
});
test('oversized observations fail closed without forwarding an action',()=>{
    const f=fixture();f.state.status='x'.repeat(api.maximumStateCharacters);f.tick();
    f.el('adventure-save').click();assert.deepEqual(f.actions,[]);
    assert(f.el('adventure-save').disabled);assert.match(f.el('adventure-status').textContent,/unavailable/);f.ui.cleanup();
});
test('new/continue links retain distinct profile routes and text is never interpreted as HTML',()=>{
    const f=fixture();assert.equal(f.el('adventure-new').href,'?experience=adventure&new=1');
    assert.equal(f.el('adventure-continue').href,'?experience=adventure');
    assert.equal(f.el('adventure-prototypes').href,'?experience=lego-world');
    f.state.status='<img src=x onerror=alert(1)>';f.tick();
    assert.equal(f.el('adventure-status').textContent,f.state.status);assert.equal(f.el('adventure-status').children.length,0);
    f.ui.cleanup();
});
test('confirmed Cove shortcut is reused unchanged and restored on cleanup',()=>{
    let original,home;
    const f=fixture(f=>{home=f.add('nav','old-worlds');original=f.add('a','cove-continue',home);
        original.href='http://127.0.0.1/index.html?experience=salvage-cove&world=0123456789abcdef0123456789abcdef';
        original.hidden=false;original.textContent='Continue saved Cove';});
    assert.equal(f.el('cove-continue'),original);assert(f.el('adventure-more').contains(original));
    assert(!home.contains(original));assert.match(original.href,/world=0123456789abcdef0123456789abcdef$/);
    assert.deepEqual(f.actions,[]);f.ui.cleanup();assert(home.contains(original));
});
test('an unavailable Cove shortcut is not exposed as a saved adventure',()=>{
    const f=fixture(f=>{const link=f.add('a','cove-continue');link.hidden=true;});
    assert(!f.el('adventure-more').contains(f.el('cove-continue')));assert(f.el('cove-continue').hidden);f.ui.cleanup();
});
test('text scale, contrast and collapse preserve ordinary controls without commands',()=>{
    const f=fixture();f.state.textScale=1.5;f.state.highContrast=true;f.tick();
    assert.equal(f.el('adventure-ui').style['--adventure-text-scale'],'1.5');
    assert.equal(f.el('adventure-ui').dataset.contrast,'true');
    f.el('adventure-collapse').click();assert(f.el('adventure-controls').hidden);
    f.el('adventure-collapse').click();assert(!f.el('adventure-controls').hidden);
    assert.deepEqual(f.actions,[]);f.ui.cleanup();
});
