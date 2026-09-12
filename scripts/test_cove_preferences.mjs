import test from 'node:test';
import assert from 'node:assert/strict';
import {createRequire} from 'node:module';
import {testDOM} from './cove_ui_test_dom.mjs';
const require=createRequire(import.meta.url),api=require('../web/cove_preferences.js');
const defaults=()=>({version:1,mouseSensitivity:1,padSensitivity:1,moveDeadzone:.2,lookDeadzone:.2,invertX:false,invertY:false,reelToggle:false,orbitToggle:false,textScale:1,highContrast:false,tutorialsEnabled:true,captionsEnabled:true,locale:'en',bindings:['interact','camera_menu'].map((action,i)=>({action,key:i?291:69,modifiers:0,alternate:0,alternateModifiers:0,pad:i?3:0}))});
function fixture(stored=null){
    const f=testDOM();f.panel=f.add('div','cove-preferences');f.value=defaults();f.calls=[];f.writes=[];
    f.environment.localStorage={getItem:()=>stored,setItem:(...args)=>f.writes.push(args)};
    f.engine={ccall(name,_r,_t,args){assert.equal(name,'voxy_cove_preferences_action');const [kind,text]=args;f.calls.push({kind,text});
        if(kind===1)return JSON.stringify(f.value);if(kind===3){f.value=defaults();return 'ok';}
        let next;try{next=JSON.parse(text);}catch{return 'Invalid settings';}
        if(f.refuse||next.version!==1||!Array.isArray(next.bindings))return 'Controls conflict. Current settings unchanged.';
        f.value=next;return 'ok';}};
    f.state={ready:true,world:'one',session:{admissionOpen:true},ui:{actions:[{id:'interact',label:'Interact'},{id:'camera_menu',label:'Camera'}]}};
    f.ui=api.install(f.engine,f.environment,f.panel);f.ui.tick(f.state);f.el=id=>f.document.getElementById(id);
    f.change=(id,value)=>{const e=f.el(id);if(e.type==='checkbox')e.checked=value;else e.value=String(value);e.dispatchEvent(new f.Event('change'));};return f;
}
test('settings apply sends full canonical JSON and persists only an accepted result',()=>{
    const f=fixture();f.change('cove-pref-textScale',1.5);f.change('cove-binding-key',79);f.el('cove-preferences-apply').click();
    assert.equal(f.value.textScale,1.5);assert.equal(f.value.bindings[0].key,79);assert.equal(f.value.bindings.length,2);assert.equal(f.writes.length,1);
    assert.equal(f.document.body.style['--cove-ui-scale'],'1.5');assert.match(f.el('cove-preferences-status').textContent,/applied and remembered/);f.ui.cleanup();
});
test('refused binding preserves core, stored preferences and editable draft',()=>{
    const f=fixture();f.refuse=true;const before=structuredClone(f.value);f.change('cove-binding-key',87);f.el('cove-preferences-apply').click();
    assert.deepEqual(f.value,before);assert.deepEqual(f.writes,[]);assert.equal(f.el('cove-binding-key').value,'87');assert.match(f.el('cove-preferences-status').textContent,/conflict/);f.ui.cleanup();
});
test('an external toggle cannot be overwritten by an unrelated unfinished settings edit',()=>{
    const f=fixture();f.change('cove-pref-padSensitivity',1.5);f.el('cove-pref-padSensitivity').focus();const focus=f.document.activeElement;
    f.value.tutorialsEnabled=false;f.state.ui.tutorialsEnabled=false;f.ui.tick(f.state);assert.equal(f.document.activeElement,focus);assert.equal(f.el('cove-pref-padSensitivity').value,'1.5');
    f.el('cove-preferences-apply').click();assert.equal(f.value.tutorialsEnabled,false);assert.equal(f.value.padSensitivity,1.5);f.ui.cleanup();
});
test('binding edits survive switching the action selector and apply together',()=>{
    const f=fixture();f.change('cove-binding-key',79);f.change('cove-binding-action','camera_menu');f.change('cove-binding-pad',9);f.change('cove-binding-action','interact');
    assert.equal(f.el('cove-binding-key').value,'79');f.el('cove-preferences-apply').click();assert.equal(f.value.bindings[0].key,79);assert.equal(f.value.bindings[1].pad,9);f.ui.cleanup();
});
test('core pad indices and reserved keys match the shared picker contract',()=>{
    const f=fixture(),pads=f.el('cove-binding-pad').options;
    for(const [index,label]of [[6,'Left trigger'],[7,'Right trigger'],[8,'View'],[15,'D-pad right']])assert.equal(pads.find(row=>row.value===String(index)).textContent,label);
    assert(!pads.some(row=>row.value==='16'));assert(!f.el('cove-binding-key').options.some(row=>['256','298'].includes(row.value)));
    assert(f.el('cove-binding-key').options.find(row=>row.value==='291').disabled);f.change('cove-binding-action','camera_menu');assert(!f.el('cove-binding-key').options.find(row=>row.value==='291').disabled);f.ui.cleanup();
});
test('storage denial is optional and a direct Reset restores defaults without another approval',()=>{
    const f=fixture();f.environment.localStorage.setItem=()=>{throw Error('quota');};f.change('cove-pref-highContrast',true);f.el('cove-preferences-apply').click();assert.equal(f.value.highContrast,true);
    assert.match(f.el('cove-preferences-status').textContent,/for this visit/);f.el('cove-preferences-reset').click();assert.equal(f.value.highContrast,false);assert.equal(f.calls.filter(row=>row.kind===3).length,1);f.ui.cleanup();
});
test('bad or oversized stored settings never bypass strict core validation',()=>{
    const bad=fixture('not json');assert.match(bad.el('cove-preferences-status').textContent,/refused/);assert.deepEqual(bad.value,defaults());bad.ui.cleanup();
    const large=fixture('x'.repeat(32769));assert(!large.calls.some(row=>row.kind===2));large.ui.cleanup();
});
test('revocation disables all controls and cleanup removes every mutation listener',()=>{
    const f=fixture();f.state.session.admissionOpen=false;f.ui.tick(f.state);assert(f.el('cove-preferences-apply').disabled);const before=f.calls.length;f.el('cove-preferences-apply').click();assert.equal(f.calls.length,before);
    f.ui.cleanup();f.el('cove-preferences-reset').disabled=false;f.el('cove-preferences-reset').click();assert.equal(f.calls.length,before);
});
