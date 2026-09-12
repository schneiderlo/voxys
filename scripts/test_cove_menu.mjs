import test from 'node:test';
import assert from 'node:assert/strict';
import {createRequire} from 'node:module';
import {testDOM} from './cove_ui_test_dom.mjs';
const require=createRequire(import.meta.url),api=require('../web/cove_menu.js'),controller=require('../web/controller_menu.js');
function fixture(){
    const f=testDOM();f.environment.VoxyControllerMenu=controller;
    f.canvas=f.add('canvas','voxy-canvas');f.panel=f.add('section','salvage-preview');f.panel.dataset.scene='cove';
    f.more=f.add('details','salvage-more-controls',f.panel);f.add('summary','menu-summary',f.more);
    f.nav=f.add('nav','cove-menu-nav',f.more);
    for(const page of ['pause','job','map','inventory','settings','help','tools']){
        const b=f.add('button',`tab-${page}`,f.nav);b.setAttribute('data-cove-page',page);
        const section=f.add('section',`cove-page-${page}`,f.more);section.setAttribute('data-cove-section',page);
    }
    for(const id of ['cove-menu-back','cove-menu-state','cove-checkpoint-state','cove-result','cove-tutorial-progress','cove-tutorial-title','cove-tutorial-text','cove-tutorial-action','cove-tutorial-restart','cove-tutorial-toggle','cove-help-prev','cove-help-next','cove-help-title','cove-help-steps','cove-tutorial','cove-map','cove-landmark-list','cove-inventory','cove-practice-status'])f.add(id==='cove-map'?'svg':id.includes('action')||id.endsWith('back')||id.endsWith('prev')||id.endsWith('next')||id.endsWith('restart')||id.endsWith('toggle')?'button':'p',id,f.more);
    for(const id of ['salvage-save','salvage-pause','salvage-reset','salvage-interact','salvage-hook','salvage-workshop-toggle','workshop-test','cove-practice-return','cove-tutorial-open'])f.add('button',id,f.panel);
    f.add('section','cove-practice',f.panel);
    for(const id of ['salvage-job','salvage-field-tools']){const d=f.add('details',id,f.more);f.add('summary',null,d);}
    const resume=f.add('button','cove-menu-resume',f.more);resume.setAttribute('data-cove-forward','salvage-pause');
    f.forward=f.add('button','forward-save',f.more);f.forward.setAttribute('data-cove-forward','salvage-save');
    f.state={active:true,ready:true,world:'one',session:{admissionOpen:true,inventory:{salvageMaterial:'48',specialMachinery:'0'}},pause:{phase:'running'},workshop:{open:false,storedParts:2},boat:{parts:11,massKg:1035},player:{feet:[6,1,-49]},gamepad:{connected:true},ui:{tutorialsEnabled:true,tutorial:{title:'Walk the dock',text:'Take a few steps.',action:90,actionLabel:'Pause',number:1,total:7,enabled:true}}};
    f.actions=[];f.engine={_voxy_get_salvage_preview_json:()=>JSON.stringify(f.state),UTF8ToString:value=>value,_voxy_salvage_preview_action:id=>{f.actions.push(id);return 1;}};
    f.controller=controller.install(f.environment,f.panel);f.controller.tick(f.state);
    f.menu=api.install(f.engine,f.environment,f.controller,{status:()=>({message:'Saved checkpoint confirmed.'})});
    f.tick=()=>{f.controller.tick(f.state);f.menu.tick(f.state);};f.tick();
    f.el=id=>f.document.getElementById(id);f.finish=()=>{f.menu.cleanup();f.controller.cleanup();};return f;
}
test('one page at a time, controller Back returns overview then closes without resuming',()=>{
    const f=fixture();assert(f.environment.voxyCoveMenu('inventory'));
    assert.equal(f.el('cove-page-inventory').hidden,false);assert(f.el('cove-page-job').hidden);
    assert.match(f.el('cove-inventory').children.map(e=>e.textContent).join(' '),/Material 48 Machinery 0 Fitted parts 11 Stored parts 2 Boat mass 1035 kg/);
    f.environment.voxyControllerMenuInput({back:true,confirm:true});assert.equal(f.menu.page(),'pause');assert(f.more.open);assert.deepEqual(f.actions,[]);
    f.environment.voxyControllerMenuInput({back:true});assert(!f.more.open);assert(!f.controller.active());assert.deepEqual(f.actions,[]);f.finish();
});
test('an owned modal refuses a menu handoff without changing its page or focus',async()=>{
    const f=fixture();f.menu.open('job');const pending=f.controller.confirm({title:'Leave?',message:'Unsaved',confirmLabel:'Leave'}),focused=f.document.activeElement;
    assert.equal(f.menu.open('settings'),false);assert.equal(f.menu.page(),'job');assert.equal(f.document.activeElement,focused);
    f.environment.voxyControllerMenuInput({back:true});assert.equal(await pending,false);f.finish();
});
test('menu actions use actual button eligibility; revoked admission cannot forward or start tutorials',()=>{
    const f=fixture();let saves=0;f.el('salvage-save').addEventListener('click',()=>++saves);f.forward.click();assert.equal(saves,1);
    f.el('salvage-save').disabled=true;f.tick();f.forward.click();assert.equal(saves,1);
    f.state.session.admissionOpen=false;f.el('cove-tutorial-action').click();assert.deepEqual(f.actions,[]);f.tick();assert(!f.menu.open('help'));assert(!f.more.open);f.finish();
});
test('tutorial action reads fresh state and exact availability rather than an old visible step',()=>{
    const f=fixture();f.menu.open('help');assert.match(f.el('cove-tutorial-progress').textContent,/Step 1 \/ 7/);
    f.state.ui.tutorial.action=350;f.state.ui.tutorial.enabled=false;f.el('cove-tutorial-action').click();assert.deepEqual(f.actions,[]);
    f.state.ui.tutorial.enabled=true;f.el('cove-tutorial-action').click();assert.deepEqual(f.actions,[350]);
    f.state.ui.tutorialsEnabled=false;f.tick();assert(f.el('cove-tutorial-open').hidden&&f.el('cove-tutorial-text').hidden);assert(!f.el('cove-tutorial-toggle').disabled);f.finish();
});
test('map plots only actual admitted finite locations and lists matching distances',()=>{
    const f=fixture();f.state.landmarks=[{id:'dock',label:'Dock',position:[6,1,-51]},{id:'cargo',label:'Generator',position:[12,0,-49]},{id:'bad',label:'Bad',position:[NaN,0,0]}];
    f.menu.open('map');const list=f.el('cove-landmark-list').children.map(e=>e.textContent);
    assert.deepEqual(list,['1. Dock · 2.0 m','2. Generator · 6.0 m','3. You · 0.0 m']);assert.equal(f.el('cove-map').children.length,6);
    const first=f.el('cove-map').children[0];f.tick();assert.equal(f.el('cove-map').children[0],first,'unchanged observations preserve DOM');f.finish();
});
test('help and primary labels use current controller/key metadata',()=>{
    const f=fixture();f.state.ui.actions=[{id:'interact',label:'Interact',keyLabel:'O',padLabel:'Y'}];f.el('salvage-interact').textContent='Board boat · E';f.menu.open('help');
    assert.match(f.el('cove-help-steps').children.map(e=>e.textContent).join(' '),/Use Y/);assert.equal(f.el('salvage-interact').textContent,'Board boat · Y');
    f.state.gamepad.connected=false;f.tick();f.menu.open('help');assert.equal(f.el('salvage-interact').textContent,'Board boat · O');f.finish();
});
test('practice buttons require current canBegin/canReturn and expose the real temporary state',()=>{
    const f=fixture();f.state.practice={active:false,canBegin:true,canReturn:false};f.tick();f.el('workshop-test').click();assert.deepEqual(f.actions,[340]);
    f.state.practice={active:true,canBegin:false,canReturn:false,message:'Entering test'};f.el('workshop-test').click();f.tick();assert(!f.el('cove-practice').hidden);assert(f.el('cove-practice-return').disabled);
    f.state.practice.canReturn=true;f.tick();f.el('cove-practice-return').click();assert.deepEqual(f.actions,[340,341]);f.finish();
});
test('world change closes the menu and cleanup removes its callbacks',()=>{
    const f=fixture();f.menu.open('map');f.state.world='two';f.tick();assert(!f.more.open);const old=f.environment.voxyCoveMenu;f.finish();assert.equal(f.environment.voxyCoveMenu,undefined);assert.equal(old('help'),false);
});
function landing(stored=null){
    const f=testDOM();f.environment.VoxyControllerMenu=controller;f.add('canvas','voxy-canvas');f.panel=f.add('section','cove-landing');
    f.new=f.add('a','cove-new',f.panel);f.new.href='?experience=salvage-cove';f.saved=f.add('a','cove-continue',f.panel);f.saved.hidden=true;
    for(const id of ['cove-landing-help','cove-landing-close'])f.add('button',id,f.panel);
    f.help=f.add('section','cove-landing-instructions',f.panel);f.help.hidden=true;f.add('button','cove-landing-back',f.help);f.add('button','cove-landing-open');
    f.environment.localStorage={getItem:()=>stored};f.menu=api.installLanding(f.environment);return f;
}
test('landing controller navigates visible original anchors and consumes combined menu edges',()=>{
    const f=landing();let opened=0;f.new.addEventListener('click',()=>++opened);f.new.focus();f.environment.voxyCoveLandingMenuInput({confirm:true});assert.equal(opened,1);
    f.environment.voxyCoveLandingMenuInput({menu:true,confirm:true});assert.equal(opened,1);assert(f.panel.hidden);
    f.environment.voxyCoveLandingMenuInput({menu:true,confirm:true});assert(!f.panel.hidden);assert.equal(opened,1);
    f.environment.voxyCoveLandingMenuInput({down:true});assert.equal(f.document.activeElement.id,'cove-landing-help');f.environment.voxyCoveLandingMenuInput({confirm:true});assert(!f.help.hidden);
    f.environment.voxyCoveLandingMenuInput({back:true});assert(f.help.hidden);assert.equal(f.document.activeElement.id,'cove-landing-help');f.menu.cleanup();
});
test('landing blur/hidden edge cannot confirm and reacquisition consumes its first confirm',()=>{
    const f=landing();let opened=0;f.new.addEventListener('click',()=>++opened);f.new.focus();f.environment.emit('blur');
    f.environment.voxyCoveLandingMenuInput({confirm:true});assert.equal(opened,0);f.environment.voxyCoveLandingMenuInput({confirm:true});assert.equal(opened,1);
    f.document.hidden=true;f.document.dispatchEvent(new f.Event('visibilitychange'));assert(!f.environment.voxyCoveLandingMenuActive());assert(!f.environment.voxyCoveLandingMenuInput({confirm:true}));
    f.document.hidden=false;f.environment.voxyCoveLandingMenuInput({confirm:true});assert.equal(opened,1);f.menu.cleanup();assert.equal(f.environment.voxyCoveLandingMenuInput,undefined);
});

test('event captions are a separate optional live status and never replace save messages',()=>{
    const f=fixture(),caption=f.add('p','cove-event-caption',f.panel);f.state.ui.caption='Boarded boat';f.tick();assert.equal(caption.textContent,'Boarded boat');assert(!caption.hidden);
    assert.equal(f.el('cove-checkpoint-state').textContent,'Saved checkpoint confirmed.');f.state.ui.captionsEnabled=false;f.tick();assert(caption.hidden);assert.equal(caption.textContent,'');f.finish();
});

test('owned general menu uses current Pause/Save chords and leaves text/rebinding input alone',()=>{
    const f=fixture();f.menu.open('pause');f.state.pause.phase='paused';let saves=0;f.environment.voxyCoveSave=()=>{++saves;return true;};
    f.engine.ccall=()=>JSON.stringify({bindings:[{action:'pause',key:79,modifiers:0,alternate:0,alternateModifiers:0},{action:'save',key:299,modifiers:0,alternate:0,alternateModifiers:0}]});
    const save=new f.Event('keydown',{code:'F10',key:'F10',bubbles:true});f.el('tab-pause').dispatchEvent(save);assert.equal(saves,1);assert(save.stopped&&save.defaultPrevented);
    const input=f.add('input',null,f.more);input.type='text';input.dispatchEvent(new f.Event('keydown',{code:'KeyO',key:'o',bubbles:true}));assert.deepEqual(f.actions,[]);
    const resume=new f.Event('keydown',{code:'KeyO',key:'o',bubbles:true});f.el('tab-pause').dispatchEvent(resume);assert.deepEqual(f.actions,[91]);assert(!f.more.open);assert(resume.stopped&&resume.defaultPrevented);f.finish();
});

test('landing Back closes the current saved-world list before leaving the title',()=>{
    const f=landing(),drawer=f.add('details','cove-saved-worlds',f.panel),summary=f.add('summary',null,drawer),link=f.add('a',null,drawer);link.href='?experience=salvage-cove&world=valid';drawer.open=true;link.focus();
    f.environment.voxyCoveLandingMenuInput({back:true});assert(!drawer.open);assert(!f.panel.hidden);assert.equal(f.document.activeElement,summary);f.menu.cleanup();
});

test('landing applies bounded optional presentation preferences without game calls',()=>{
    const f=landing(JSON.stringify({version:1,textScale:1.5,highContrast:true}));assert.equal(f.document.body.style['--cove-ui-scale'],'1.5');assert.equal(f.document.body.getAttribute('data-cove-contrast'),'true');f.menu.cleanup();
    for(const value of ['x'.repeat(32769),'not json',JSON.stringify({version:1,textScale:99,highContrast:true}),JSON.stringify({version:1,textScale:1.5,highContrast:'true'})]){
        const bad=landing(value);assert.equal(bad.document.body.style['--cove-ui-scale'],undefined);bad.menu.cleanup();
    }
});

test('paused Job has an explicit Resume that keeps the current Job page available',()=>{
    const f=fixture();f.state.pause.phase='paused';f.el('salvage-pause').addEventListener('click',()=>{f.state.pause.phase='running';f.tick();});f.menu.open('job');
    const resume=f.el('cove-menu-resume');assert(!resume.hidden&&!resume.disabled);resume.click();assert.equal(f.state.pause.phase,'running');assert.equal(f.menu.page(),'job');assert(f.more.open);assert(resume.hidden);f.finish();
});
