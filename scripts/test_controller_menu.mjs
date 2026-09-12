import assert from 'node:assert/strict';
import {createRequire} from 'node:module';
import {testDOM} from './cove_ui_test_dom.mjs';
const require=createRequire(import.meta.url),{install}=require('../web/controller_menu.js'),library=require('../web/design_library.js');
let cases=0;
function fixture(){
    const f=testDOM();f.environment.VoxyDesignLibrary=library;
    f.canvas=f.add('canvas','voxy-canvas');f.panel=f.add('section','salvage-preview');f.add('p','salvage-controller-help',f.panel);
    f.first=f.add('button','first',f.panel);f.disabled=f.add('button','disabled',f.panel);f.disabled.disabled=true;
    f.drawer=f.add('details','drawer',f.panel);f.summary=f.add('summary','summary',f.drawer);f.inside=f.add('button','inside',f.drawer);
    f.name=f.add('input','design-name',f.panel);f.name.type='text';f.name.value='My boat';
    f.list=f.add('select','design-list',f.panel);
    for(const [id,label]of [['one','First'],['two','Second']]){const option=f.add('option',null,f.list);option.value=id;option.textContent=label;}f.list.value='one';
    f.last=f.add('button','last',f.panel);f.state={active:true,ready:true,world:'world-a',session:{admissionOpen:true},workshop:{open:true}};
    f.menu=install(f.environment,f.panel);f.menu.tick(f.state);f.input=events=>f.environment.voxyControllerMenuInput(events);
    f.active=()=>f.environment.voxyControllerMenuActive();f.modal=()=>f.document.querySelector('dialog');
    f.button=label=>f.modal().querySelectorAll('button').find(button=>button.textContent===label);
    return f;
}
{
    const f=fixture();let used=0;f.first.addEventListener('click',()=>++used);
    assert.equal(f.input({confirm:true}),false);assert.equal(used,0);
    assert.equal(f.input({menu:true,confirm:true}),true);assert(f.active());assert.equal(used,0,'enter menu does not also confirm');
    assert.equal(f.document.activeElement,f.first);f.input({down:true});assert.equal(f.document.activeElement,f.summary,'skip disabled and closed drawer');
    f.input({right:true});assert(f.drawer.open);f.input({down:true});assert.equal(f.document.activeElement,f.inside);
    f.input({back:true});assert(!f.drawer.open);assert.equal(f.document.activeElement,f.summary);
    f.input({back:true});assert(!f.active());assert.equal(f.document.activeElement,f.canvas);
    f.menu.cleanup();++cases;
}
{
    const f=fixture();f.first.style.display='none';f.summary.hidden=true;f.input({menu:true});assert.equal(f.document.activeElement,f.name);
    f.list.focus();let changes=0;f.list.addEventListener('change',()=>++changes);
    f.input({right:true});assert.equal(f.list.value,'two');f.input({left:true});assert.equal(f.list.value,'one');assert.equal(changes,2);
    f.first.style.display='block';f.first.focus();f.menu.tick(f.state);f.menu.tick(f.state);assert.equal(f.document.activeElement,f.first,'refresh retains valid focus');
    f.first.disabled=true;f.menu.tick(f.state);assert.equal(f.document.activeElement,f.name,'disabled focused control recovers to an available target');
    f.menu.cleanup();++cases;
}
{
    const f=fixture();f.input({menu:true});let calls=0;
    f.first.addEventListener('click',()=>{++calls;f.canvas.focus();});f.input({confirm:true});
    assert.equal(calls,1);assert.equal(f.document.activeElement,f.first,'a brush handler cannot steal controller focus');
    f.input({menu:true,confirm:true});assert(!f.active());assert.equal(calls,1,'closing edge is consumed');
    f.menu.cleanup();++cases;
}
{
    const f=fixture();f.name.focus();assert(f.active(),'physical text focus claims engine input');
    f.input({confirm:true});assert(f.modal().open);f.button('B').click();f.button('O').click();f.button('A').click();f.button('T').click();
    f.button('Use name').focus();f.input({confirm:true});assert.equal(f.name.value,'BOAT');assert.equal(f.modal(),null);assert.equal(f.document.activeElement,f.name);
    f.input({back:true});assert(!f.active());f.menu.cleanup();++cases;
}
{
    const f=fixture();f.name.focus();const before=f.name.value;const done=f.menu.editName(f.name,library.validName);
    f.button('Z').click();f.input({back:true,confirm:true});assert.equal(await done,false);assert.equal(f.name.value,before,'cancel never edits the original name');
    f.menu.cleanup();++cases;
}
{
    const f=fixture();f.input({menu:true});const done=f.menu.editName(f.name,library.validName);
    f.button('Clear').click();f.button('Use name').click();assert(f.modal(),'empty name keeps dialog open');
    f.button('Aa').click();f.button('a').click();f.button('Space').click();f.button('b').click();f.button('Delete').click();f.button('c').click();
    f.button('Use name').click();assert.equal(await done,true);assert.equal(f.name.value,'a c');f.menu.cleanup();++cases;
}
{
    const f=fixture();const done=f.menu.editName(f.name,library.validName),value=f.modal().querySelector('input');
    value.value='é'.repeat(48);value.dispatchEvent(new f.Event('input'));f.button('A').click();assert.equal(value.value,'é'.repeat(48),'UTF-8 byte cap stops keyboard growth');
    f.button('Use name').click();assert.equal(await done,true);assert.equal(f.name.value,'é'.repeat(48));f.menu.cleanup();++cases;
}
{
    const f=fixture();const done=f.menu.editName(f.name,library.validName),value=f.modal().querySelector('input');
    value.value='bad\nname';value.dispatchEvent(new f.Event('input'));f.button('Use name').click();assert(f.modal(),'physical input also uses strict name validation');
    const event=new f.Event('keydown',{key:'Escape',bubbles:true});value.dispatchEvent(event);
    assert(event.stopped&&event.defaultPrevented);assert.equal(await done,false);f.menu.cleanup();++cases;
}
{
    const f=fixture();f.input({menu:true});f.last.focus();const result=f.menu.confirm({title:'Remove?',message:'Only the saved design.',confirmLabel:'Remove'});
    assert.equal(f.document.activeElement.textContent,'Cancel','destructive modal defaults to Cancel');f.input({confirm:true});assert.equal(await result,false);assert.equal(f.document.activeElement,f.last);
    const accepted=f.menu.confirm({title:'Remove?',message:'Only the saved design.',confirmLabel:'Remove'});f.input({right:true});f.input({confirm:true});assert.equal(await accepted,true);
    f.menu.cleanup();++cases;
}
for(const transition of ['world','closed','pending','revoked','blur','hidden','cleanup']){
    const f=fixture();f.input({menu:true});const result=f.menu.editName(f.name,library.validName);f.button('Z').click();
    if(transition==='world')f.menu.tick({...f.state,world:'world-b'});
    if(transition==='closed')f.menu.tick({...f.state,workshop:{open:false}});
    if(transition==='pending')f.menu.tick({...f.state,workshop:{open:true,pending:true}});
    if(transition==='revoked')f.menu.tick({...f.state,session:{admissionOpen:false}});
    if(transition==='blur'){f.document.focused=false;f.environment.emit('blur');}
    if(transition==='hidden'){f.document.hidden=true;f.document.dispatchEvent(new f.Event('visibilitychange'));}
    if(transition==='cleanup')f.menu.cleanup();
    assert.equal(await result,false,transition);assert.equal(f.name.value,'My boat',transition);assert.equal(f.modal(),null,transition);
    if(transition==='blur'||transition==='hidden')assert.equal(f.input({menu:true,confirm:true}),false);
    f.menu.cleanup();++cases;
}
{
    const f=fixture();f.name.focus();f.panel.hidden=true;assert(!f.active(),'hidden text field cannot trap gameplay input');
    const before=f.environment.voxyControllerMenuInput;f.menu.cleanup();assert.equal(f.environment.voxyControllerMenuInput,undefined);assert.equal(before({menu:true}),false);
    assert([...f.environment.events.values()].every(listeners=>listeners.size===0));++cases;
}
for(const navigation of [null,'controller','mouse']){
    const f=fixture();f.input({menu:true});f.first.focus();const finish=f.menu.pinFocus(f.first);
    let used=0;f.last.addEventListener('click',()=>++used);
    f.first.disabled=true;f.document.body.focus();f.menu.tick(f.state);
    assert.equal(f.document.activeElement,f.document.body,'busy control does not move to an unrelated action');
    f.input({confirm:true});assert.equal(used,0);assert.equal(f.document.activeElement,f.document.body);
    if(navigation==='controller')f.input({down:true});
    if(navigation==='mouse')f.last.focus();
    const deliberate=f.document.activeElement;f.first.disabled=false;finish(true);
    assert.equal(f.document.activeElement,navigation?deliberate:f.first,'restore only when the player did not navigate away');
    f.menu.cleanup();++cases;
}
{
    const f=fixture(),help=f.document.getElementById('salvage-controller-help');assert(help.hidden);
    f.menu.tick({...f.state,gamepad:{connected:true,armed:true,menuOwner:false}});
    assert(!help.hidden&&!f.active(),'entry instructions are visible before claiming controller focus');
    f.menu.tick({...f.state,gamepad:{connected:false,armed:false,menuOwner:false}});assert(help.hidden);
    f.menu.cleanup();++cases;
}
{
    const f=fixture(),camera=f.add('details','salvage-camera',f.panel),summary=f.add('summary',null,camera);
    const near=f.add('button','camera-near',camera),far=f.add('button','camera-far',camera);let calls=0;
    near.addEventListener('click',()=>++calls);f.state.workshop.open=false;f.state.characterCamera={available:true};f.menu.tick(f.state);
    assert(f.menu.openSection(camera));assert(f.active()&&camera.open);assert.equal(f.document.activeElement,near);
    f.input({confirm:true});assert.equal(calls,1);f.input({down:true});assert.equal(f.document.activeElement,far);
    f.input({down:true});assert.equal(f.document.activeElement,summary,'section navigation cannot reach Leave or Workshop');
    f.input({back:true,confirm:true});assert(!f.active()&&!camera.open);assert.equal(calls,1);assert.equal(f.document.activeElement,f.canvas);
    assert(f.menu.openSection(camera));f.input({menu:true,confirm:true});assert(!f.active()&&!camera.open);assert.equal(calls,1);
    f.menu.cleanup();assert(!f.menu.openSection(camera));++cases;
}
for(const transition of ['unavailable','workshop','hidden','closed','world','blur','cleanup']){
    const f=fixture(),camera=f.add('details','salvage-camera',f.panel);f.add('summary',null,camera);f.add('button',null,camera);
    f.state.workshop.open=false;f.state.characterCamera={available:true};f.menu.tick(f.state);assert(f.menu.openSection(camera));
    if(transition==='unavailable')f.state.characterCamera.available=false;
    if(transition==='workshop')f.state.workshop.open=true;
    if(transition==='hidden')camera.hidden=true;
    if(transition==='closed')camera.open=false;
    if(transition==='world')f.state.world='world-b';
    if(transition==='blur')f.environment.emit('blur');
    if(transition==='cleanup')f.menu.cleanup();else f.menu.tick(f.state);
    assert(!f.menu.active()&&!camera.open,transition);if(transition!=='cleanup')assert.equal(f.input({confirm:true}),false,transition);f.menu.cleanup();++cases;
}
console.log(`Controller menu ownership, navigation, naming and lifecycle: ${cases} cases passed`);
