import assert from 'node:assert/strict';
import {createRequire} from 'node:module';
import {testDOM} from './cove_ui_test_dom.mjs';
const require=createRequire(import.meta.url),{install}=require('../web/design_library.js');
const bytes='01'.repeat(48),settle=()=>new Promise(resolve=>setImmediate(resolve));
// Storage behavior itself has separate real-IndexedDB coverage. This model
// lets UI tests deliberately finish reads after an owner/focus transition.
function storage(){
    const records={designs:new Map([['saved',{id:'saved',name:'Saved boat',blueprint:bytes,revision:'1'}]]),backups:new Map()};
    let delayed=false,held=[],closed=false;
    const db={close(){closed=true;},transaction(){
        let pending=0,finished=false;const copies=Object.fromEntries(Object.entries(records).map(([key,value])=>[key,new Map(value)]));
        const tx={objectStore:name=>({
            getAll(){return request(()=>[...copies[name].values()].map(row=>({...row})));},
            get(id){return request(()=>copies[name].has(id)?{...copies[name].get(id)}:undefined,true);},
            put(row){copies[name].set(row.id,{...row});},delete(id){copies[name].delete(id);}
        }),abort(){finished=true;queueMicrotask(()=>tx.onabort?.());}};
        const complete=()=>queueMicrotask(()=>{if(!pending&&!finished){finished=true;for(const key of Object.keys(records))records[key]=copies[key];tx.oncomplete?.();}});
        function request(read,hold=false){
            ++pending;const result={};const work=()=>{if(finished)return;result.result=read();result.onsuccess?.();--pending;complete();};
            if(hold&&delayed)held.push(work);else queueMicrotask(work);return result;
        }
        return tx;
    }};
    return {records,indexedDB:{open(){const request={};queueMicrotask(()=>{request.result=db;request.onsuccess?.();});return request;}},
        delay(){delayed=true;},release(){delayed=false;const work=held;held=[];for(const step of work)queueMicrotask(step);},closed:()=>closed};
}
async function fixture(){
    const f=testDOM(),s=storage();Object.assign(f.environment,{indexedDB:s.indexedDB,crypto:{randomUUID:()=>`new-${s.records.designs.size}`}});
    f.panel=f.add('details','workshop-designs');f.panel.open=true;
    f.name=f.add('input','design-name',f.panel);f.name.value='New boat';
    f.list=f.add('select','design-list',f.panel);f.status=f.add('p','design-status',f.panel);f.note=f.add('p','design-load-note',f.panel);
    f.file=f.add('input','design-file',f.panel);f.file.type='file';f.file.hidden=true;
    f.buttons={};for(const action of ['save-new','update','load','rename','duplicate','restore','export','import','refresh','remove','keyboard']){
        const button=f.add('button','design-'+action,f.panel);button.dataset.designAction=action;f.buttons[action]=button;
    }
    f.calls=[];f.engine={ccall(_name,_return,_types,[action,text]){f.calls.push([action,text]);return action===1?bytes:'ok';}};
    f.keyboard=[];f.confirmations=[];
    f.menu={editName(target,validate){f.keyboard.push({target,validate});return Promise.resolve(false);},confirm(options){f.confirmations.push(options);return new Promise(resolve=>{f.answer=resolve;});}};
    f.state={active:true,ready:true,world:'world-a',session:{admissionOpen:true},workshop:{open:true,changed:false,brickTool:false,canLoadBlueprint:true,revision:'1'}};
    f.library=install(f.engine,f.environment,f.menu);f.tick=update=>{f.state={...f.state,...update};f.library.tick(f.state);};
    f.tick({});await settle();f.storage=s;return f;
}
let cases=0;
{
    const f=await fixture();assert(!f.buttons.load.disabled);f.buttons.load.click();await settle();
    assert.deepEqual(f.calls.filter(([id])=>id===2),[[2,bytes]]);assert.equal(f.name.value,'Saved boat');assert.match(f.status.textContent,/Check the price, then Launch/);
    f.buttons.keyboard.click();assert.equal(f.keyboard.length,1);assert.equal(f.keyboard[0].target,f.name);assert(!f.keyboard[0].validate(''));
    f.library.cleanup();++cases;
}
{
    const f=await fixture();
    for(const changed of [{changed:true,canLoadBlueprint:false},{changed:true,brickTool:true,canLoadBlueprint:false},{canLoadBlueprint:undefined}]){
        f.tick({workshop:{...f.state.workshop,...changed}});assert(f.buttons.load.disabled);f.buttons.load.click();
    }
    assert.equal(f.calls.filter(([id])=>id===2).length,0);assert.match(f.note.textContent,/unused brick preview/);
    assert(!f.buttons['save-new'].disabled,'kept design can save while an unused brush is armed');
    f.library.cleanup();++cases;
}
{
    const f=await fixture();f.buttons['save-new'].click();await settle();
    const saved=[...f.storage.records.designs.values()].find(row=>row.name==='New boat');assert(saved);assert.equal(saved.blueprint,bytes);
    assert.match(f.status.textContent,/Saved/);assert(f.calls.every(([id])=>[1,3].includes(id)),'naming and saving do not call load or Launch');
    f.name.value='Copy';f.buttons.duplicate.click();await settle();assert.equal(f.storage.records.designs.size,3);
    assert.equal([...f.storage.records.designs.values()].find(row=>row.name==='Copy').blueprint,bytes);
    f.library.cleanup();++cases;
}
{
    const f=await fixture();f.buttons.remove.click();assert.equal(f.confirmations.length,1);assert.match(f.confirmations[0].message,/owned parts stay unchanged/);
    assert(f.buttons.remove.disabled&&f.name.disabled,'modal operation owns the library');f.answer(false);await settle();assert(f.storage.records.designs.has('saved'));
    f.buttons.remove.click();f.answer(true);await settle();assert(!f.storage.records.designs.has('saved'));assert.match(f.status.textContent,/removed/);
    assert.equal(f.calls.length,0,'deleting a named blueprint does not mutate a boat');f.library.cleanup();++cases;
}
for(const kind of ['world','closed','pending','revoked','cleanup']){
    const f=await fixture();f.storage.delay();f.buttons.load.click();await settle();
    if(kind==='world')f.tick({world:'world-b'});
    if(kind==='closed')f.tick({workshop:{...f.state.workshop,open:false}});
    if(kind==='pending')f.tick({workshop:{...f.state.workshop,pending:true}});
    if(kind==='revoked')f.tick({session:{admissionOpen:false}});
    if(kind==='cleanup')f.library.cleanup();
    f.storage.release();await settle();assert.equal(f.calls.filter(([id])=>id===2).length,0,`${kind}: stale read cannot load a boat`);
    f.library.cleanup();++cases;
}
{
    const f=await fixture();f.buttons.remove.click();f.tick({world:'world-b'});f.answer(true);await settle();
    assert(f.storage.records.designs.has('saved'),'stale confirmation cannot delete a different owner’s library selection');
    f.library.cleanup();++cases;
}
{
    const f=await fixture();f.tick({session:{admissionOpen:false}});for(const button of Object.values(f.buttons)){assert(button.disabled);button.click();}
    assert.equal(f.calls.length,0);f.library.cleanup();assert(f.storage.closed());
    assert(Object.values(f.buttons).every(button=>[...button.listeners.values()].every(set=>set.size===0)));++cases;
}

{
    const f=await fixture();f.storage.delay();f.buttons.load.click();await settle();f.tick({practice:{active:true}});
    for(const button of Object.values(f.buttons))assert(button.disabled);assert(f.name.disabled&&f.file.disabled);
    f.storage.release();await settle();assert.equal(f.calls.filter(([id])=>id===2).length,0,'test transition refuses a pending normal design load');f.library.cleanup();++cases;
}
console.log(`Design library UI permissions, naming, owned confirmation and stale async replies: ${cases} cases passed`);
