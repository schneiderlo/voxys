import assert from 'node:assert/strict';
import { createRequire } from 'node:module';
const require = createRequire(import.meta.url);
const { install } = require('../web/salvage_preview.js');
function fixture(asset = false, workshop = false) {
    class Element {
        hidden = true; disabled = false; textContent = ''; listeners = new Map();
        addEventListener(name, callback) { this.listeners.set(name, callback); }
        removeEventListener(name, callback) { if (this.listeners.get(name) === callback) this.listeners.delete(name); }
        click() { this.listeners.get('click')?.(); }
        attributes = new Map(); dataset = {};
        setAttribute(name, value) { this.attributes.set(name, value); }
    }
    const elements = Object.fromEntries(['salvage-preview', 'salvage-status', 'salvage-reset', 'salvage-pause', 'salvage-leave', 'salvage-interact','salvage-towing','salvage-tow-status','salvage-hook','salvage-reel','salvage-payout','salvage-hold','salvage-job','salvage-job-status','salvage-job-accept','salvage-job-deliver','salvage-harbor','salvage-harbor-status',...['install','attach','raise','lower','stop','release'].map(n=>'salvage-harbor-'+n)].map(id => [id, new Element()]));
    const lodButtons = asset ? [0, 1, 2, 3].map(value => Object.assign(new Element(), {dataset: {salvageLod: String(value)}})) : [];
    const guideButtons = asset ? [0, 1, 2].map(value => Object.assign(new Element(), {dataset: {salvageGuide: String(value)}})) : [];
    if (asset) {
        for (const id of ['salvage-lods', 'salvage-title', 'salvage-help', 'salvage-guides', 'salvage-guide-note']) elements[id] = new Element();
        elements['salvage-lods'].querySelectorAll = () => lodButtons;
        elements['salvage-guides'].querySelectorAll = () => guideButtons;
    }
    const workshopButtons=workshop?[61,62,63,64,65,66,67,68,69,70,71,72,73,74,75,76,77,78,79,80,81,82,83,84,85,86,87,88,89,92,93,94,97,98]
        .map(action=>Object.assign(new Element(),{dataset:{workshopAction:String(action)}})):[];
    if(workshop) {
        for(const name of ['Brick 1 x 2','Brick 2 x 2','Brick 2 x 4'])workshopButtons.push(
            Object.assign(new Element(),{dataset:{workshopAction:'-1',workshopBrick:name}}));
        for(const id of ['salvage-workshop','salvage-workshop-toggle','salvage-workshop-part','salvage-workshop-status','salvage-scope','salvage-workshop-stock','workshop-catalog-name','workshop-settings-note','workshop-recovery-status'])elements[id]=new Element();
        elements['salvage-workshop'].querySelectorAll=()=>workshopButtons;
    }
    let state = { active: true, ready: true, busy: false, failed: false, resets: 0 };
    if (asset) state.assetFixture = {forcedLod: '0', guides: 0, availableLods: ['0','1','2','3']};
    let tick, cleared = 0;const events=new Map();
    const actions = [], navigations = [];
    const environment = {
        document: { getElementById: id => elements[id] },
        location: { assign: url => navigations.push(url) },
        setInterval: callback => { tick = callback; return 17; },
        clearInterval: id => { assert.equal(id, 17); ++cleared; },
        addEventListener: (name, callback) => { assert(!events.has(name));events.set(name,callback); },
        removeEventListener: (name, callback) => {assert.equal(events.get(name),callback);events.delete(name);},
    };
    const engine = {
        _voxy_is_initialized: () => 1,
        _voxy_salvage_preview_action: action => { actions.push(action); return 1; },
        _voxy_get_salvage_preview_json: () => JSON.stringify(state), UTF8ToString: s => s,
    };
    const cleanup = install(engine, environment);
    return { elements, lodButtons, guideButtons, workshopButtons, actions, navigations, engine, cleanup, tick: () => tick(),
        state: update => Object.assign(state, update), pagehide: () => events.get('pagehide')?.(), event:name=>events.get(name)?.(), events, cleared: () => cleared };
}
{
    const f=fixture(true,true);
    const w={open:true,canOpen:true,selected:9,name:'Winch',valid:false,changed:true,undo:1,
        massKg:1035,parts:12,message:'Blocked.',catalogName:'Pontoon',partCost:'24',partMachinery:'0',
        canAdd:false,materials:'48',machinery:'0',charge:'0',refund:'0',affordable:true};
    f.state({workshop:w});f.tick();
    const focus=f.workshopButtons.find(b=>b.dataset.workshopAction==='97');
    const frame=f.workshopButtons.find(b=>b.dataset.workshopAction==='98');
    assert(!focus.disabled&&!frame.disabled,'camera remains usable while diagnosing an invalid ghost');
    focus.click();frame.click();assert.deepEqual(f.actions,[97,98]);
    let stopped=0;const onKey=f.elements['salvage-preview'].listeners.get('keydown');
    onKey({code:'ShiftLeft',stopPropagation:()=>++stopped});assert.equal(stopped,0);
    onKey({code:'KeyG',stopPropagation:()=>++stopped});assert.equal(stopped,1,'text/button actions stay in the UI');
    f.state({workshop:{...w,pending:true}});f.tick();focus.click();assert.deepEqual(f.actions,[97,98]);
    f.cleanup();assert.equal(focus.listeners.size,0);
}
{
    const f = fixture();
    f.elements['salvage-reset'].click(); f.elements['salvage-reset'].click();
    assert.deepEqual(f.actions, [1]);
    f.tick(); // An old Ready snapshot is not reset completion.
    assert.equal(f.elements['salvage-reset'].disabled, true);
    f.state({ ready: false, busy: true }); f.tick();
    f.state({ ready: true, busy: false, resets: 1 }); f.tick();
    assert.equal(f.elements['salvage-reset'].disabled, false);
    f.cleanup(); f.cleanup();
    assert.equal(f.cleared(), 1);
    assert.equal(f.elements['salvage-reset'].listeners.size, 0);
}
{
    const f = fixture();
    f.elements['salvage-reset'].click(); f.elements['salvage-leave'].click();
    f.elements['salvage-leave'].click(); f.tick();
    assert.deepEqual(f.actions, [1, 2]); assert.deepEqual(f.navigations, []);
    f.state({ active: false, ready: false }); f.tick();
    assert.deepEqual(f.navigations, ['?experience=lego-world']);
    assert.equal(f.cleared(), 1); assert.equal(f.elements['salvage-preview'].hidden, true);
    f.tick(); assert.equal(f.navigations.length, 1);
}
{
    const f = fixture();
    f.pagehide(); f.elements['salvage-reset'].click(); f.tick();
    assert.deepEqual(f.actions, []); assert.equal(f.cleared(), 1);
}
{
    const f = fixture();
    f.elements['salvage-leave'].click(); f.state({ failed: true, active: false }); f.tick();
    assert.deepEqual(f.navigations, []);
    assert.match(f.elements['salvage-status'].textContent, /stopped/);
    assert.equal(f.elements['salvage-reset'].disabled, true);
    f.cleanup();
}
{
    const f = fixture(true);
    assert.equal(f.elements['salvage-lods'].hidden, false);
    assert.equal(f.lodButtons[0].attributes.get('aria-pressed'), 'true');
    f.lodButtons[2].click(); assert.deepEqual(f.actions, [12]);
    f.state({assetFixture: {forcedLod: '2', availableLods: ['0','1','2','3']}}); f.tick();
    assert.equal(f.lodButtons[2].attributes.get('aria-pressed'), 'true');
    f.elements['salvage-reset'].click(); f.tick(); f.lodButtons[1].click();
    assert.deepEqual(f.actions, [12, 1], 'pending Reset must suppress detail controls');
    f.state({resets: 1}); f.tick(); f.tick();
    f.lodButtons[3].click(); assert.deepEqual(f.actions, [12, 1, 13]);
    f.state({failed: true}); f.tick(); f.lodButtons[0].click();
    assert.deepEqual(f.actions, [12, 1, 13], 'failure must suppress detail controls');
    f.cleanup();
    assert(f.lodButtons.every(button => button.listeners.size === 0));
}
{
    const f = fixture(true);
    assert.equal(f.elements['salvage-guides'].hidden, false);
    assert.equal(f.guideButtons[0].attributes.get('aria-pressed'), 'true');
    f.guideButtons[2].click(); assert.deepEqual(f.actions, [22]);
    f.state({assetFixture: {forcedLod: '0', guides: 2, availableLods: ['0','1','2','3']}}); f.tick();
    assert.equal(f.guideButtons[2].attributes.get('aria-pressed'), 'true');
    assert.match(f.elements['salvage-guide-note'].textContent, /clearance/);
    f.elements['salvage-reset'].click(); f.guideButtons[1].click();
    assert.deepEqual(f.actions, [22, 1]);
    f.state({resets: 1}); f.tick(); f.tick();
    f.guideButtons[1].click(); assert.deepEqual(f.actions, [22, 1, 21]);
    f.state({failed: true}); f.tick(); f.guideButtons[0].click();
    assert.deepEqual(f.actions, [22, 1, 21]);
    f.cleanup(); assert(f.guideButtons.every(button => button.listeners.size === 0));
}
{
    const f = fixture(true);
    f.state({assetFixture: {forcedLod: '0', guides: 0, availableLods: ['0','1']}}); f.tick();
    assert.deepEqual(f.lodButtons.map(button => button.disabled), [false,false,true,true]);
    f.lodButtons[2].click(); f.lodButtons[3].click(); assert.deepEqual(f.actions, []);
    f.lodButtons[1].click(); assert.deepEqual(f.actions, [11]);
    f.state({assetFixture: {forcedLod: '0', guides: 0}}); f.tick();
    assert(f.lodButtons.every(button => button.disabled), 'missing availability must fail closed');
    f.cleanup();
}
{
    const f = fixture(true);
    f.state({player: {mode: 'walking', interaction: 'none', onBoat: false}}); f.tick();
    const action = f.elements['salvage-interact'];
    assert.equal(action.hidden, false); assert.equal(action.disabled, true);
    action.click(); assert.deepEqual(f.actions, []);
    f.state({player: {mode: 'walking', interaction: 'board', onBoat: false}}); f.tick();
    assert.equal(action.disabled, false); assert.match(action.textContent, /Board/);
    action.click(); assert.deepEqual(f.actions, [30]);
    f.elements['salvage-reset'].click(); action.click();
    assert.deepEqual(f.actions, [30, 1], 'pending reset must suppress interaction');
    f.state({resets: 1, player: {mode: 'helm', interaction: 'leave-helm', onBoat: true}}); f.tick(); f.tick();
    assert.match(f.elements['salvage-status'].textContent, /throttle/);
    f.state({failed: true}); f.tick(); action.click();
    assert.deepEqual(f.actions, [30, 1]);
    f.cleanup(); assert.equal(action.listeners.size, 0);
}
{
    const f=fixture(true);
    const tow={name:'Salvage generator',massKg:420,distance:6,operable:false,confirmed:true,inRange:true,attached:false,motor:0};
    f.state({tow});f.tick();
    const hook=f.elements['salvage-hook'],reel=f.elements['salvage-reel'];
    assert(hook.disabled);assert(reel.disabled);
    tow.operable=true;f.tick();hook.click();assert.deepEqual(f.actions,[40]);
    tow.attached=true;f.tick();assert.match(hook.textContent,/Release/);
    reel.click();assert.deepEqual(f.actions,[40,41]);
    tow.confirmed=false;f.tick();assert(hook.disabled);assert(reel.disabled);
    tow.confirmed=true;f.tick();f.elements['salvage-leave'].click();reel.click();
    assert.deepEqual(f.actions,[40,41,2]);
    f.cleanup();assert.equal(hook.listeners.size,0);assert.equal(reel.listeners.size,0);
}
{
    const f=fixture(true), accept=f.elements['salvage-job-accept'], deliver=f.elements['salvage-job-deliver'];
    const job={phase:'available',pending:false,canDeliver:false,harborDistance:8};
    f.state({job}); f.tick();
    assert(!accept.hidden && !accept.disabled); assert(deliver.hidden);
    accept.click(); assert.deepEqual(f.actions,[50]);
    job.phase='accepted'; f.tick();
    assert(accept.hidden); assert(!deliver.hidden && deliver.disabled);
    deliver.click(); assert.deepEqual(f.actions,[50],'distant cargo cannot be delivered');
    job.canDeliver=true; f.tick(); deliver.click(); assert.deepEqual(f.actions,[50,51]);
    job.pending=true; f.tick(); deliver.click(); f.elements['salvage-reset'].click();
    assert.deepEqual(f.actions,[50,51],'physical hand-off must suppress duplicate delivery and reset');
    job.phase='completed'; job.pending=false; job.canDeliver=false; f.tick();
    assert(deliver.hidden); assert.doesNotMatch(f.elements['salvage-job-status'].textContent,/\+60/);
    job.savePending=true;f.tick();
    assert.match(f.elements['salvage-job-status'].textContent,/Saving/);
    assert(f.elements['salvage-leave'].disabled);
    job.savePending=false;job.durable=true;f.tick();
    assert.match(f.elements['salvage-job-status'].textContent,/delivered and saved.*\+60/);
    f.cleanup(); assert.equal(accept.listeners.size,0); assert.equal(deliver.listeners.size,0);
}
{
    const f=fixture(true,true);
    const w={open:true,canOpen:true,selected:9,name:'Winch',valid:false,changed:true,undo:0,
        revision:'0',massKg:0,parts:11,message:'Not connected.'};
    f.state({workshop:w});f.tick();
    const keep=f.workshopButtons.find(b=>b.dataset.workshopAction==='71');
    assert(keep.disabled);keep.click();assert.deepEqual(f.actions,[]);
    let stopped=false;f.elements['salvage-preview'].listeners.get('keydown')({stopPropagation(){stopped=true;}});
    assert(stopped,'focused workshop keys must remain in UI');
    f.state({workshop:{...w,valid:true,massKg:1035}});f.tick();keep.click();assert.deepEqual(f.actions,[71]);
    f.engine._voxy_salvage_preview_action=()=>{f.state({workshop:{...w,open:false,canOpen:true}});return 1;};
    f.elements['salvage-workshop-toggle'].click();
    assert.equal(f.elements['salvage-preview'].dataset.workshop,'false','close must release keyboard ownership immediately');
    stopped=false;f.elements['salvage-preview'].listeners.get('keydown')({stopPropagation(){stopped=true;}});
    assert.equal(stopped,false);
    f.state({workshop:{...w,open:false,canOpen:false}});f.tick();
    assert(f.elements['salvage-workshop-toggle'].disabled);
    f.cleanup();assert.equal(f.elements['salvage-preview'].listeners.has('keydown'),false);
    assert(f.workshopButtons.every(b=>b.listeners.size===0));
}
{
    const f=fixture(true,true);
    const w={open:true,canOpen:true,selected:9,name:'Winch',valid:true,changed:false,undo:1,
        revision:'1',massKg:945,parts:10,message:'Design kept.',canLaunch:true};
    f.state({workshop:w});f.tick();
    const launch=f.workshopButtons.find(b=>b.dataset.workshopAction==='79');
    const undo=f.workshopButtons.find(b=>b.dataset.workshopAction==='80');
    const redo=f.workshopButtons.find(b=>b.dataset.workshopAction==='81');
    assert(!launch.disabled);assert(undo.disabled&&redo.disabled);launch.click();assert.deepEqual(f.actions,[79]);
    f.state({workshop:{...w,pending:true,launchMessage:'Preparing boat…'}});f.tick();
    assert(f.workshopButtons.every(b=>b.disabled));assert(f.elements['salvage-workshop-toggle'].disabled);
    launch.click();assert.deepEqual(f.actions,[79],'pending launch cannot submit duplicate input');
    assert.match(f.elements['salvage-workshop-status'].textContent,/Preparing boat/);
    f.state({workshop:{...w,canLaunch:false,canUndoLaunch:true}});f.tick();
    assert(launch.disabled);assert(!undo.disabled);undo.click();assert.deepEqual(f.actions,[79,80]);
    f.state({workshop:{...w,canLaunch:false,canRedoLaunch:true}});f.tick();
    assert(undo.disabled);assert(!redo.disabled);redo.click();assert.deepEqual(f.actions,[79,80,81]);
    f.cleanup();assert(f.workshopButtons.every(b=>b.listeners.size===0));
}
{
    const f=fixture(true,true);
    const w={open:true,canOpen:true,selected:9,name:'Winch',valid:true,changed:false,undo:0,
        revision:'0',massKg:1035,parts:11,message:'Ready.',catalogName:'Pontoon',partCost:'24',partMachinery:'0',
        canAdd:true,materials:'48',machinery:'0',charge:'24',refund:'0',affordable:true,canLaunch:true};
    f.state({workshop:w});f.tick();
    assert.match(f.elements['workshop-catalog-name'].textContent,/Pontoon.*24 material/);
    assert.match(f.elements['salvage-workshop-stock'].textContent,/Stock: 48.*Launch: 24 material/);
    const add=f.workshopButtons.find(b=>b.dataset.workshopAction==='84');add.click();assert.deepEqual(f.actions,[84]);
    f.state({workshop:{...w,canAdd:false,changed:true}});f.tick();add.click();assert.deepEqual(f.actions,[84]);
    f.state({workshop:{...w,materials:'0',affordable:false,canLaunch:false}});f.tick();
    assert.match(f.elements['salvage-workshop-stock'].textContent,/More stock needed/);
    const launch=f.workshopButtons.find(b=>b.dataset.workshopAction==='79');assert(launch.disabled);launch.click();assert.deepEqual(f.actions,[84]);
    f.state({workshop:{...w,charge:'18446744073709551615',refund:'18446744073709551614'}});f.tick();
    assert.match(f.elements['salvage-workshop-stock'].textContent,/Launch: 1 material/,'prices must not lose uint64 precision');
    f.cleanup();
}
{
    const f=fixture(true,true);
    const w={open:true,canOpen:true,selected:9,name:'Winch',valid:true,changed:false,undo:0,
        revision:'0',massKg:1035,parts:11,message:'Ready.',canAdd:true,catalogIndex:4,pointerPlacement:true,
        catalog:[{index:4,name:'Brick 1 x 2',cost:'2'},{index:2,name:'Brick 2 x 2',cost:'3'},{index:6,name:'Brick 2 x 4',cost:'5'}]};
    f.state({workshop:w});f.tick();const bricks=f.workshopButtons.filter(b=>b.dataset.workshopBrick);
    assert.deepEqual(bricks.map(b=>b.dataset.workshopAction),['104','102','106'],'palette follows admitted indices');
    assert.equal(bricks[0].attributes.get('aria-pressed'),'true');
    for(const b of bricks)b.click();assert.deepEqual(f.actions,[104,102,106]);
    f.state({workshop:{...w,canAdd:false}});f.tick();for(const b of bricks){assert(b.disabled);b.click();}
    assert.deepEqual(f.actions,[104,102,106],'unfinished placement cannot add another ghost');
    f.state({workshop:{...w,catalog:[]}});f.tick();assert(bricks.every(b=>b.hidden&&b.disabled));f.cleanup();
}
{
    const f=fixture(true,true);
    const w={open:true,canOpen:true,selected:7,name:'Propeller',valid:true,changed:false,undo:0,
        revision:'0',massKg:1035,parts:11,message:'Ready.',configurable:true,hasOutputLimit:true,canReverse:true,
        settings:{enabled:true,limitPercent:75,reversed:false}};
    const controls=[85,86,87].map(a=>f.workshopButtons.find(b=>b.dataset.workshopAction===String(a)));
    f.state({workshop:w});f.tick();assert.match(f.elements['workshop-settings-note'].textContent,/On.*75%.*Forward/);
    for(const b of controls){assert(!b.hidden&&!b.disabled);b.click();}assert.deepEqual(f.actions,[85,86,87]);
    f.state({workshop:{...w,name:'Winch',hasOutputLimit:false,canReverse:false,settings:{enabled:false}}});f.tick();
    assert.match(controls[0].textContent,/Turn on/);assert(!controls[0].hidden);assert(controls[1].hidden&&controls[2].hidden);
    controls[1].click();controls[2].click();assert.deepEqual(f.actions,[85,86,87]);
    f.state({workshop:{...w,pending:true}});f.tick();assert(controls.every(b=>b.disabled));
    f.state({workshop:{...w,configurable:false,hasOutputLimit:false,canReverse:false}});f.tick();
    assert(controls.every(b=>b.hidden&&b.disabled));f.cleanup();
}
{
    const f=fixture();const pause=f.elements['salvage-pause'];assert(pause.hidden);
    f.state({player:{mode:'helm',interaction:'leave-helm'},pause:{phase:'running',canPause:false}});f.tick();
    assert(!pause.hidden&&pause.disabled);
    f.state({pause:{phase:'running',canPause:true}});f.tick();pause.click();assert.deepEqual(f.actions,[90]);
    f.state({pause:{phase:'requested',canPause:false}});f.tick();assert(pause.disabled);
    assert(f.elements['salvage-reset'].disabled);assert(f.elements['salvage-interact'].disabled);
    pause.click();f.elements['salvage-reset'].click();f.elements['salvage-interact'].click();assert.deepEqual(f.actions,[90]);
    f.state({pause:{phase:'draining',canPause:false}});f.tick();assert(pause.disabled);
    f.state({pause:{phase:'paused',canPause:false}});f.tick();assert(!pause.disabled);
    assert.match(pause.textContent,/Resume/);assert.equal(pause.attributes.get('aria-pressed'),'true');
    assert.match(f.elements['salvage-status'].textContent,/Expedition paused/);
    pause.click();assert.deepEqual(f.actions,[90,91]);
    f.state({pause:{phase:'running',canPause:true}});f.tick();assert(!f.elements['salvage-reset'].disabled);
    assert.equal(pause.attributes.get('aria-pressed'),'false');
    f.state({failed:true});f.tick();assert(pause.disabled);f.cleanup();assert.equal(pause.listeners.size,0);
}
{
    const f=fixture();f.state({pause:{phase:'paused',canPause:false}});f.tick();
    f.elements['salvage-leave'].click();f.tick();assert.deepEqual(f.actions,[2]);
    assert(f.elements['salvage-pause'].disabled);assert.deepEqual(f.navigations,[]);
    f.state({active:false,ready:false});f.tick();assert.deepEqual(f.navigations,['?experience=lego-world']);
    f.elements['salvage-pause'].click();assert.deepEqual(f.actions,[2]);assert.equal(f.cleared(),1);
}
console.log('salvage preview UI lifecycle tests: 18 passed');

{
    const f=fixture(),button=name=>f.elements['salvage-harbor-'+name];
    const lift={installed:true,durable:true,pending:false,atDock:true,compatible:true,attached:true,
        brokenMask:0,canOperate:true,canRelease:true,motor:0,lengths:[4,4,4,4]};
    f.state({job:{phase:'completed',durable:true},pause:{phase:'running'},harbor:lift});f.tick();
    const event={button:0,preventDefault(){}};
    button('raise').listeners.get('pointerdown')(event);assert.deepEqual(f.actions,[54]);
    // The control can disable while its GPU command is pending. Releasing
    // outside the button still stops the motor exactly once.
    f.state({harbor:{...lift,canOperate:false,motor:.5}});f.tick();
    assert(button('raise').disabled);f.event('pointerup');f.event('pointercancel');assert.deepEqual(f.actions,[54,56]);
    f.state({harbor:lift});f.tick();button('lower').listeners.get('pointerdown')(event);f.event('blur');
    assert.deepEqual(f.actions,[54,56,55,56]);
    button('raise').listeners.get('pointerdown')(event);f.cleanup();assert.deepEqual(f.actions,[54,56,55,56,54,56]);
    assert.equal(f.events.size,0);assert.equal(button('raise').listeners.size,0);
}
{
    const f=fixture();f.state({job:{phase:'completed',durable:true},pause:{phase:'paused'},
        harbor:{installed:true,pending:true,durable:false,atDock:true,compatible:true,lengths:[0,0,0,0]}});f.tick();
    assert(f.elements['salvage-pause'].disabled&&f.elements['salvage-leave'].disabled);
    assert.match(f.elements['salvage-harbor-status'].textContent,/Saving the powered harbor lift/);f.cleanup();
}
console.log('Harbor hold/release/focus/cleanup and installation freeze: 2 cases passed');

{
    const f=fixture();f.state({job:{phase:'completed',durable:true},pause:{phase:'running'},
        harbor:{installed:true,durable:true,pending:false,attachmentPending:true,atDock:true,compatible:true,
            canAttach:false,canOperate:false,canRelease:true,motor:0,lengths:[0,0,0,0]}});f.tick();
    assert(f.elements['salvage-harbor-attach'].disabled);
    assert(!f.elements['salvage-harbor-stop'].disabled);
    assert.match(f.elements['salvage-harbor-status'].textContent,/Waiting for safe alignment/);
    f.event('pointerup');assert.deepEqual(f.actions,[]);
    f.event('blur');assert.deepEqual(f.actions,[56]);
    f.state({harbor:{installed:true,durable:true,atDock:true,compatible:true,attachmentPending:false,
        message:'Attachment timed out. Reposition the boat and try again.',attachmentIssue:'Align the whole boat.'}});f.tick();
    assert.match(f.elements['salvage-harbor-status'].textContent,/Attachment timed out/);
    f.cleanup();assert.equal(f.events.size,0);
}
console.log('Pending harbor attachment: visible cancellation and blur: 1 case passed');

{
    const f=fixture();f.state({rescue:{phase:'idle',pending:false,completed:'0'},pause:{phase:'running',canPause:true}});f.tick();
    assert.equal(f.elements['salvage-reset'].textContent,'Rescue · R');
    f.elements['salvage-reset'].click();assert.deepEqual(f.actions,[1]);
    f.state({rescue:{phase:'releasing',pending:true,completed:'0'},pause:{phase:'paused'}});f.tick();
    assert(f.elements['salvage-pause'].disabled);assert(f.elements['salvage-reset'].disabled);assert(f.elements['salvage-leave'].disabled);
    f.state({rescue:{phase:'saving',pending:true,savePending:true,completed:'0'},job:{secured:false,savePending:true}});f.tick();
    assert(f.elements['salvage-pause'].disabled);f.elements['salvage-pause'].click();assert.deepEqual(f.actions,[1]);
    f.state({rescue:{phase:'idle',pending:false,completed:'1'},job:{phase:'available',secured:false,savePending:false}});f.tick();f.tick();
    assert(!f.elements['salvage-pause'].disabled);assert(f.elements['salvage-reset'].disabled);
    f.elements['salvage-pause'].click();assert.deepEqual(f.actions,[1,91]);f.cleanup();
}
console.log('Rescue controls: saved acknowledgment gates Resume: 1 case passed');

{
    const f=fixture(true,true);
    const w={open:true,canOpen:true,selected:9,name:'Winch',valid:true,changed:false,undo:0,
        revision:'0',massKg:1035,parts:11,message:'Ready.',recoveryDesigns:2,recoverySelected:1,
        canLoadRecovery:true,canRemoveRecovery:false};
    const load=f.workshopButtons.find(b=>b.dataset.workshopAction==='89');
    const remove=f.workshopButtons.find(b=>b.dataset.workshopAction==='94');
    f.state({workshop:w});f.tick();
    assert.match(f.elements['workshop-recovery-status'].textContent,/Recovered design 2 of 2/);
    assert(!load.disabled&&remove.disabled);load.click();remove.click();assert.deepEqual(f.actions,[89]);
    f.state({workshop:{...w,canRemoveRecovery:true}});f.tick();remove.click();assert.deepEqual(f.actions,[89,94]);
    f.state({workshop:{...w,pending:true}});f.tick();assert(load.disabled&&remove.disabled);
    f.state({workshop:{...w,recoveryDesigns:0,canLoadRecovery:false,canRemoveRecovery:false}});f.tick();
    assert(load.disabled&&remove.disabled);assert.match(f.elements['workshop-recovery-status'].textContent,/four custom boat designs/);
    f.cleanup();
}
