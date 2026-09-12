import assert from 'node:assert/strict';
import { createRequire } from 'node:module';
const require = createRequire(import.meta.url);
const { install } = require('../web/salvage_preview.js');
function fixture(asset = false, workshop = false, cove = false, cameraController = false) {
    class Element {
        hidden = true; disabled = false; textContent = ''; listeners = new Map();
        addEventListener(name, callback) { this.listeners.set(name, callback); }
        removeEventListener(name, callback) { if (this.listeners.get(name) === callback) this.listeners.delete(name); }
        click() { this.listeners.get('click')?.(); }
        attributes = new Map(); dataset = {};
        focused = 0; focus() { ++this.focused; }
        setAttribute(name, value) { this.attributes.set(name, value); }
    }
    const elements = Object.fromEntries(['salvage-preview', 'salvage-status', 'salvage-reset', 'salvage-pause', 'salvage-leave', 'salvage-interact','salvage-towing','salvage-tow-status','salvage-hook','salvage-reel','salvage-payout','salvage-hold','salvage-job','salvage-job-status','salvage-job-accept','salvage-job-deliver','salvage-harbor','salvage-harbor-status',...['install','attach','raise','lower','stop','release'].map(n=>'salvage-harbor-'+n)].map(id => [id, new Element()]));
    for(const id of ['salvage-objective','salvage-objective-title','salvage-objective-detail','salvage-objective-action','salvage-field-tools','salvage-more-controls'])elements[id]=new Element();
    elements['salvage-field-tools'].hidden=false;
    if(cove)elements['salvage-preview'].dataset.scene='cove';
    for(const [id,label] of Object.entries({'salvage-job-accept':'Recover the generator · J','salvage-job-deliver':'Deliver generator · H',
        'salvage-harbor-install':'Power harbor lift · K','salvage-reel':'Reel in · Q','salvage-payout':'Pay out · Z','salvage-hold':'Stop winch'}))elements[id].textContent=label;
    for(const id of ['salvage-hook','salvage-reel','salvage-payout','salvage-hold'])elements[id].hidden=false;
    const lodButtons = asset ? [0, 1, 2, 3].map(value => Object.assign(new Element(), {dataset: {salvageLod: String(value)}})) : [];
    const guideButtons = asset ? [0, 1, 2].map(value => Object.assign(new Element(), {dataset: {salvageGuide: String(value)}})) : [];
    if (asset) {
        for (const id of ['salvage-lods', 'salvage-title', 'salvage-help', 'salvage-guides', 'salvage-guide-note']) elements[id] = new Element();
        elements['salvage-lods'].querySelectorAll = () => lodButtons;
        elements['salvage-guides'].querySelectorAll = () => guideButtons;
    }
    const cameraButtons=[320,321,322,323,324,325].map(action=>Object.assign(new Element(),{hidden:false,dataset:{cameraAction:String(action)}}));
    elements['salvage-camera']=new Element();elements['salvage-camera'].querySelectorAll=()=>cameraButtons;
    const workshopButtons=workshop?[61,62,63,64,65,66,67,68,69,70,71,72,73,74,75,76,77,78,79,80,81,82,83,84,85,86,87,88,89,92,93,94,96,97,98,300,301,302,303,304,305,306,307,308,309]
        .map(action=>Object.assign(new Element(),{dataset:{workshopAction:String(action)}})):[];
    if(workshop) {
        for(const name of ['Brick 1 x 2','Brick 2 x 2','Brick 2 x 4'])workshopButtons.push(
            Object.assign(new Element(),{dataset:{workshopAction:'-1',workshopBrick:name}}));
        for(let index=0;index<8;++index)workshopButtons.push(
            Object.assign(new Element(),{dataset:{workshopAction:String(200+index),workshopPaint:String(index)}}));
        for(const id of ['voxy-canvas','salvage-workshop','salvage-workshop-toggle','salvage-workshop-part','salvage-workshop-status','salvage-workshop-tool','salvage-scope','salvage-workshop-stock','workshop-catalog-name','workshop-settings-note','workshop-recovery-status','workshop-paint-name','workshop-paint-note','workshop-selection-note'])elements[id]=new Element();
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
    const priorCameraMenu=()=> 'prior',cameraOpened=[];
    environment['voxyCoveCameraMenu']=priorCameraMenu;
    if(cameraController)environment.VoxyControllerMenu={install:()=>({tick(){},cleanup(){},openSection(element){
        assert(elements['salvage-more-controls'].open,'camera ancestor must be disclosed before focus ownership');
        if(environment.refuseCamera)return false;
        cameraOpened.push(element);return true;
    }})};
    const engine = {
        _voxy_is_initialized: () => 1,
        _voxy_salvage_preview_action: action => { actions.push(action); return 1; },
        _voxy_get_salvage_preview_json: () => JSON.stringify(state), UTF8ToString: s => s,
    };
    const cleanup = install(engine, environment);
    return { elements, lodButtons, guideButtons, workshopButtons, cameraButtons, cameraOpened, environment, priorCameraMenu, actions, navigations, engine, cleanup, tick: () => tick(),
        state: update => Object.assign(state, update), pagehide: () => events.get('pagehide')?.(), event:name=>events.get(name)?.(), events, cleared: () => cleared };
}
{
    const f=fixture(true,true),button=action=>f.workshopButtons.find(b=>b.dataset.workshopAction===String(action));
    const w={open:true,canOpen:true,name:'Brick 2 x 4',selected:10,selectedCount:3,selectedParts:[10,12,14],
        changed:false,valid:true,undo:2,redoCount:1,parts:14,massKg:1060,catalogName:'Brick 2 x 4',canPaint:true,paintName:'Mixed',paintIndex:null};
    f.state({workshop:w});f.tick();
    assert.match(f.elements['salvage-workshop-part'].textContent,/3 parts selected.*Primary: Brick 2 x 4/);
    assert.match(f.elements['workshop-selection-note'].textContent,/Group changes are kept and undone together/);
    assert.match(f.elements['workshop-paint-note'].textContent,/selected bricks/);
    assert(!button(305).disabled,'replacement applies atomically to the selection');
    for(const action of [300,301,302,303,304,305,306,307,308,309]){assert(!button(action).disabled,String(action));button(action).click();}
    assert.deepEqual(f.actions,[300,301,302,303,304,305,306,307,308,309]);
    f.state({workshop:{...w,selectedCount:1,redoCount:0}});f.tick();
    assert(button(301).disabled&&button(306).disabled);button(305).click();assert.equal(f.actions.at(-1),305);
    f.state({workshop:{...w,changed:true,valid:false,problemCode:'clearance-blocked',message:'Leave room around the connector.'}});f.tick();
    for(const action of [61,62,300,301,302,303,304,305,306,309])assert(button(action).disabled,String(action));
    assert(!button(307).disabled&&!button(308).disabled,'invalid preview remains editable');
    assert.equal(f.elements['salvage-workshop-status'].dataset.problem,'clearance-blocked');
    assert.match(f.elements['salvage-workshop-status'].textContent,/Leave room around the connector/);
    for(const update of [{workshop:{...w,pending:true}},{workshop:w,session:{admissionOpen:false}}]){
        f.state(update);f.tick();for(let action=300;action<=309;++action)assert(button(action).disabled,String(action));
    }
    f.cleanup();console.log('Workshop group actions, redo, replacement, typed reason and permission gating: 1 case passed');
}
{
    const f=fixture(true,true);
    const w={open:true,canOpen:true,name:'Brick 2 x 4',selected:27,valid:true,changed:true,undo:2,
        catalogName:'Brick 2 x 4',massKg:1060,parts:13,placedParts:12,placedBricks:2,
        brickTool:true,canPaint:true,paintIndex:3,paintName:'Blue',paint:[50,108,190,255],
        brushPaint:[50,108,190,255],materials:'48',charge:'10'};
    const paints=f.workshopButtons.filter(b=>b.dataset.workshopPaint!==undefined);
    f.state({workshop:w});f.tick();
    assert.equal(paints.length,8);assert(paints.every(b=>!b.disabled));
    assert.deepEqual(paints.map(b=>b.attributes.get('aria-pressed')),['false','false','false','true','false','false','false','false']);
    assert.equal(f.elements['workshop-paint-name'].textContent,'Paint · Blue');
    assert.match(f.elements['workshop-paint-note'].textContent,/Paint is free.*Next bricks use this color/);
    for(const button of paints)button.click();
    assert.deepEqual(f.actions,[200,201,202,203,204,205,206,207]);
    assert.equal(f.elements['voxy-canvas'].focused,8,'each brush paint choice returns rotation keys to the canvas');
    assert.match(f.elements['salvage-workshop-stock'].textContent,/Launch: 10 material/,'painting does not add a browser-side charge');
    f.state({workshop:{...w,brushPaint:undefined,paintIndex:null,paintName:'Custom',paint:[15,90,117,255]}});f.tick();
    assert(paints.every(b=>b.attributes.get('aria-pressed')==='false'),'custom stored colors must not select Original');
    assert.equal(f.elements['workshop-paint-name'].textContent,'Paint · Custom');
    assert.match(f.elements['workshop-paint-note'].textContent,/Choose a color for new bricks/);
    f.cleanup();assert(paints.every(b=>b.listeners.size===0));
    console.log('Brick paint brush: eight actions, exact selection, custom colors and keyboard focus: 1 case passed');
}
{
    const f=fixture(true,true);
    const w={open:true,canOpen:true,name:'Brick 1 x 2',selected:25,valid:true,changed:false,undo:0,
        massKg:1040,parts:11,brickTool:false,canPaint:true,paintIndex:0,paintName:'Original',paint:[255,255,255,255]};
    const paints=f.workshopButtons.filter(b=>b.dataset.workshopPaint!==undefined);
    const keep=f.workshopButtons.find(b=>b.dataset.workshopAction==='71');
    f.state({workshop:w});f.tick();
    assert.equal(paints[0].attributes.get('aria-pressed'),'true');
    assert.match(f.elements['workshop-paint-note'].textContent,/Paint is free.*then Keep/);
    paints[2].click();assert.deepEqual(f.actions,[202]);assert(keep.disabled);
    assert.equal(f.elements['voxy-canvas'].focused,0,'painting an existing brick leaves focus among edit controls');
    f.state({workshop:{...w,changed:true,paintIndex:2,paintName:'Teal'}});f.tick();
    assert(!keep.disabled);keep.click();assert.deepEqual(f.actions,[202,71]);
    for(const blocked of [{canPaint:false},{canPaint:undefined},{pending:true}]){
        f.state({workshop:{...w,...blocked}});f.tick();
        assert(paints.every(b=>b.disabled));for(const button of paints)button.click();
        if(!blocked.pending)assert(paints.every(b=>b.attributes.get('aria-pressed')==='false'));
    }
    assert.deepEqual(f.actions,[202,71],'unsupported parts, missing capability and pending launch cannot submit paint');
    f.state({workshop:w});f.tick();
    f.engine._voxy_salvage_preview_action=()=>0;paints[5].click();
    assert.equal(paints[0].attributes.get('aria-pressed'),'true','refused paint does not optimistically change selected color');
    f.state({workshop:{...w,canPaint:false}});f.tick();
    assert.match(f.elements['workshop-paint-note'].textContent,/Choose a brick to paint/);
    f.cleanup();assert(paints.every(b=>b.listeners.size===0));
    console.log('Brick paint editing: Keep, permission gating, refusal and cleanup: 1 case passed');
}
{
    const f=fixture(true,true);
    const w={open:true,canOpen:true,selected:28,name:'Brick 1 x 2',catalogName:'Brick 1 x 2',catalogIndex:4,
        valid:false,changed:true,undo:3,massKg:0,parts:14,placedParts:13,placedBricks:3,message:'Blocked by another part.',
        brickTool:true,pointerPlacement:true,canChooseBrick:true,canAdd:false,canLaunch:true,materials:'48',charge:'8',
        catalog:[{index:4,name:'Brick 1 x 2',cost:'1'},{index:2,name:'Brick 2 x 2',cost:'3'},{index:6,name:'Brick 2 x 4',cost:'6'}]};
    f.state({workshop:w});f.tick();
    const select=f.workshopButtons.find(b=>b.dataset.workshopAction==='96');
    const launch=f.workshopButtons.find(b=>b.dataset.workshopAction==='79');
    const bricks=f.workshopButtons.filter(b=>b.dataset.workshopBrick);
    assert(bricks.every(b=>!b.disabled),'an unplaced preview must not block choosing another brick');
    assert(!select.disabled&&!launch.disabled,'placed bricks can launch despite an invalid unused preview');
    assert.match(f.elements['salvage-workshop-part'].textContent,/3 bricks placed.*13 boat parts/);
    assert.match(f.elements['salvage-workshop-tool'].textContent,/preview is not charged/);
    assert.match(f.elements['salvage-workshop-stock'].textContent,/Launch: 8 material/);
    bricks[1].click();select.click();launch.click();assert.deepEqual(f.actions,[102,96,79]);
    assert.equal(f.elements['voxy-canvas'].focused,2,'brick selection and Select hand keyboard input to the canvas');
    f.state({workshop:{...w,brickTool:false,pointerPlacement:false,changed:false,parts:13}});f.tick();
    assert(select.disabled);assert.equal(select.attributes.get('aria-pressed'),'true');
    f.state({workshop:{...w,pending:true}});f.tick();
    assert(select.disabled&&launch.disabled&&bricks.every(b=>b.disabled));
    f.cleanup();assert.equal(select.listeners.size,0);
    console.log('Continuous brick controls: switching, placed counts, launch, selection and focus: 1 case passed');
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
    f.state({workshop:{...w,changed:true,pointerTarget:false}});f.tick();
    assert.match(f.elements['salvage-workshop-status'].textContent,/Point at a part/);
    assert(!f.workshopButtons.find(b=>b.dataset.workshopAction==='71').disabled,'Keep preserves a valid position without a pointer target');
    f.state({workshop:{...w,changed:true,pointerTarget:false,valid:false,message:'Parts overlap.'}});f.tick();
    assert.match(f.elements['salvage-workshop-status'].textContent,/Parts overlap/);
    assert(f.workshopButtons.find(b=>b.dataset.workshopAction==='71').disabled);
    f.state({workshop:w});f.tick();
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

// D42: one promoted existing action, with read-only guidance and exact controls.
const objectiveState=()=>({
    player:{onBoat:false,mode:'walking',interaction:'none'},pause:{phase:'running',canPause:true},
    workshop:{open:false,canOpen:true},job:{phase:'available',pending:false,savePending:false,durable:false,canDeliver:false,harborDistance:8},
    tow:{name:'Generator',massKg:420,distance:6,hasWinch:true,operable:false,confirmed:true,attached:false,inRange:true,motor:0},
    harbor:{installed:false,durable:false,pending:false,canInstall:false,lengths:[]}
});
const objectiveFixture=()=>{const f=fixture(true,true,true);f.state(objectiveState());f.tick();return f;};
const card=f=>f.elements['salvage-objective'];
const next=f=>f.elements['salvage-objective-action'];
{
    const f=objectiveFixture();
    assert(!card(f).hidden&&!next(f).hidden&&!next(f).disabled);
    assert.equal(card(f).dataset.step,'accept');
    assert.equal(next(f).textContent,f.elements['salvage-job-accept'].textContent);
    next(f).click();assert.deepEqual(f.actions,[50],'one click forwards the existing accept handler once');
    f.state({job:{...objectiveState().job,phase:'accepted'},player:{onBoat:false,interaction:'none'}});f.tick();
    assert.equal(card(f).dataset.step,'board');assert(next(f).hidden&&next(f).disabled);
    next(f).click();assert.deepEqual(f.actions,[50],'no invented interaction without a real prompt');
    f.state({player:{onBoat:false,interaction:'board'}});f.tick();
    assert.equal(next(f).textContent,'Board boat · E');next(f).click();assert.deepEqual(f.actions,[50,30]);
    f.cleanup();
}
{
    const f=objectiveFixture();
    // A state transition between paint and click must never dispatch the old action.
    f.state({job:{...objectiveState().job,phase:'accepted'},player:{onBoat:false,interaction:'board'}});
    next(f).click();assert.deepEqual(f.actions,[]);assert.equal(card(f).dataset.step,'board');
    f.elements['salvage-interact'].hidden=true; // Existing tick restores actual current visibility.
    f.state({player:{onBoat:false,interaction:'none'}});next(f).click();assert.deepEqual(f.actions,[]);
    f.state({job:{...objectiveState().job,phase:'available',pending:true}});f.tick();
    assert.equal(card(f).dataset.step,'securing');assert(next(f).hidden);next(f).click();assert.deepEqual(f.actions,[]);
    f.cleanup();
}
{
    const f=objectiveFixture(),s=objectiveState();
    f.state({job:{...s.job,phase:'accepted'},player:{onBoat:true,interaction:'helm'},tow:{...s.tow,operable:true,confirmed:false}});f.tick();
    assert.equal(card(f).dataset.step,'waiting-tow');assert(next(f).hidden);
    f.state({tow:{...s.tow,operable:true,confirmed:true}});f.tick();
    assert.equal(card(f).dataset.step,'hook');next(f).click();assert.deepEqual(f.actions,[40]);
    f.state({tow:{...s.tow,operable:true,attached:true,ropeLength:4}});f.tick();
    assert.equal(card(f).dataset.step,'return');assert.equal(next(f).textContent,'Open winch controls');
    next(f).click();assert.equal(f.elements['salvage-field-tools'].open,true);
    assert.equal(f.elements['salvage-more-controls'].open,true,'promoted winch action reveals its enclosing controls');
    assert.equal(f.elements['salvage-reel'].focused,1);assert.deepEqual(f.actions,[40],'revealing controls must not start the winch');
    f.state({tow:{...s.tow,operable:true,attached:true,confirmed:false,ropeLength:4}});f.tick();
    assert(next(f).hidden);next(f).click();assert.deepEqual(f.actions,[40]);f.cleanup();
}
{
    const f=objectiveFixture(),s=objectiveState();
    f.state({job:{...s.job,phase:'accepted',canDeliver:true},tow:{...s.tow,hasWinch:false,confirmed:false}});f.tick();
    assert.equal(card(f).dataset.step,'deliver','authoritative delivery readiness precedes optional winch/boarding steps');
    assert(!f.elements['salvage-job-deliver'].hidden&&!f.elements['salvage-job-deliver'].disabled);
    next(f).click();assert.deepEqual(f.actions,[51]);
    f.state({job:{...s.job,phase:'accepted',canDeliver:false,harborDistance:0}});f.tick();
    assert.notEqual(card(f).dataset.step,'deliver','geometry never grants delivery permission');
    assert.equal(card(f).dataset.step,'winch');next(f).click();assert.deepEqual(f.actions,[51,60]);f.cleanup();
}
{
    const f=objectiveFixture(),s=objectiveState();
    for(const change of [
        {job:{...s.job,phase:'accepted',canDeliver:true,pending:true}},
        {job:{...s.job,phase:'completed',durable:false,savePending:true}},
        {job:{...s.job,phase:'completed',durable:true},harbor:{...s.harbor,pending:true,canInstall:true}},
        {rescue:{pending:true,phase:'saving',completed:'0'}},
        {pause:{phase:'draining'}},{pause:{phase:'paused'}},{ready:false},{busy:true},
    ]){
        f.state({...objectiveState(),rescue:undefined,ready:true,busy:false,...change});f.tick();
        assert(next(f).hidden&&next(f).disabled);next(f).click();assert.deepEqual(f.actions,[]);
    }
    f.state({...objectiveState(),rescue:undefined,ready:true,busy:false});f.tick();
    assert.equal(card(f).dataset.step,'accept');f.elements['salvage-reset'].click();next(f).click();
    assert.deepEqual(f.actions,[1],'local pending Reset must also suppress an old promoted action');f.cleanup();
}
{
    const f=objectiveFixture(),s=objectiveState();
    f.state({job:{...s.job,phase:'completed',durable:false},harbor:{...s.harbor,canInstall:true}});f.tick();
    assert.equal(card(f).dataset.step,'saving');assert(next(f).hidden);
    f.state({job:{...s.job,phase:'completed',durable:true},player:{onBoat:true,interaction:'dock'},harbor:s.harbor});f.tick();
    assert.match(card(f).dataset.step,/dock/);next(f).click();assert.deepEqual(f.actions,[30]);
    f.state({player:{onBoat:false,interaction:'none'},harbor:{...s.harbor,canInstall:true}});f.tick();
    assert.equal(card(f).dataset.step,'power');next(f).click();assert.deepEqual(f.actions,[30,52]);
    f.state({harbor:{...s.harbor,installed:true,durable:false}});f.tick();assert.notEqual(card(f).dataset.step,'powered');assert(next(f).hidden);
    f.state({harbor:{...s.harbor,installed:true,durable:true}});f.tick();assert.equal(card(f).dataset.step,'powered');assert(next(f).hidden);
    f.state({harbor:undefined});f.tick();assert.equal(card(f).dataset.step,'delivered');assert(next(f).hidden);f.cleanup();
}
{
    const f=objectiveFixture();
    f.state({workshop:{open:true,canOpen:true,name:'Winch',selected:9,parts:11,massKg:1035,message:'Ready',valid:true}});f.tick();
    assert(card(f).hidden&&next(f).hidden);next(f).click();assert.deepEqual(f.actions,[]);
    f.state({workshop:{open:false,canOpen:true}});f.tick();assert(!card(f).hidden);
    f.state({failed:true});f.tick();assert(next(f).hidden&&next(f).disabled);next(f).click();assert.deepEqual(f.actions,[]);
    assert.equal(f.elements['salvage-objective-title'].textContent,'Cove unavailable');f.cleanup();
    assert(card(f).hidden);assert.equal(next(f).listeners.size,0);assert.equal(f.events.size,0);next(f).click();assert.deepEqual(f.actions,[]);
    const inspection=fixture(true,true);assert(card(inspection).hidden);inspection.cleanup();
}
{
    const f=objectiveFixture();
    f.elements['salvage-leave'].click();next(f).click();assert.deepEqual(f.actions,[2]);assert(next(f).hidden);
    f.state({active:false,ready:false});f.tick();assert.equal(f.cleared(),1);assert(card(f).hidden);
    assert.deepEqual(f.navigations,['?experience=lego-world']);next(f).click();assert.deepEqual(f.actions,[2]);
}
{
    const f=objectiveFixture(),button=next(f),detail=f.elements['salvage-objective-detail'];
    button.focus();const transitions=[];
    for(const field of ['hidden','disabled']){
        let value=button[field];
        Object.defineProperty(button,field,{get:()=>value,set:next=>{
            if(next!==value){transitions.push([field,next]);if(next)button.focused=0;}value=next;
        }});
    }
    let text=detail.textContent,announcements=0;
    Object.defineProperty(detail,'textContent',{get:()=>text,set:value=>{text=value;++announcements;}});
    f.tick();f.tick();
    assert.deepEqual(transitions,[],'unchanged refresh must not temporarily disable or hide a focused action');
    assert.equal(button.focused,1);assert.equal(announcements,0,'unchanged objective must not mutate its live region');
    f.state({job:{...objectiveState().job,pending:true}});f.tick();
    assert(button.hidden&&button.disabled);assert.equal(button.focused,0,'real unavailability clears the action');
    f.cleanup();
}
{
    const f=objectiveFixture(),s=objectiveState();
    for(const change of [
        {},
        {job:{...s.job,phase:'accepted'},player:{onBoat:true,interaction:'helm'},tow:{...s.tow,operable:true}},
        {job:{...s.job,phase:'completed',durable:true},harbor:{...s.harbor,canInstall:true}},
        {pause:{phase:'paused',canPause:false}},
    ]){
        f.state({...objectiveState(),...change,session:{admissionOpen:false}});f.tick();
        assert.equal(card(f).dataset.step,'unavailable');assert(next(f).hidden&&next(f).disabled);
        assert.equal(f.elements['salvage-objective-title'].textContent,'Expedition unavailable');
        next(f).click();assert.deepEqual(f.actions,[]);
    }
    f.state({...objectiveState(),session:{admissionOpen:true}});f.tick();assert.equal(card(f).dataset.step,'accept');
    f.state({session:{admissionOpen:false}});next(f).click();assert.deepEqual(f.actions,[],'revoked admission invalidates an already displayed action');
    assert(!f.elements['salvage-job-accept'].disabled,'the legacy enabled button alone is insufficient after revocation');
    f.state({session:{}});f.tick();assert.equal(card(f).dataset.step,'accept','missing optional observation preserves older engine compatibility');
    f.cleanup();
}
console.log('Browser next objective: permissions, focus, stale clicks, forwarding, drawer focus, save priority and cleanup: 10 cases passed');

{
    const f=fixture(false,false,true,true),panel=f.elements['salvage-camera'];
    const camera={available:true,mode:'chase',distance:4.8,reducedMotion:false,frameLoad:false};
    f.state({player:{mode:'walking'},characterCamera:camera,pause:{phase:'paused'}});f.tick();
    assert(!panel.hidden&&f.cameraButtons.every(button=>!button.disabled),'paused camera controls remain available');
    assert.equal(f.elements['salvage-more-controls'].open,false,'camera is initially behind More controls');
    f.environment.refuseCamera=true;
    assert(!f.environment['voxyCoveCameraMenu']());
    assert.equal(f.elements['salvage-more-controls'].open,false,'refused modal handoff does not expand the HUD');
    f.environment.refuseCamera=false;
    assert(f.environment['voxyCoveCameraMenu']());assert.deepEqual(f.cameraOpened,[panel]);
    for(const button of f.cameraButtons)button.click();assert.deepEqual(f.actions,[320,321,322,323,324,325]);
    f.state({characterCamera:{...camera,mode:'orbit',distance:1.5,reducedMotion:true,frameLoad:true}});f.tick();
    assert.equal(f.cameraButtons[0].textContent,'View: Orbit');assert.equal(f.cameraButtons[2].textContent,'Frame load: On');
    assert.equal(f.cameraButtons[3].textContent,'Reduced motion: On');assert(f.cameraButtons[4].disabled&&!f.cameraButtons[5].disabled);
    f.state({characterCamera:{...camera,distance:12}});f.tick();assert(!f.cameraButtons[4].disabled&&f.cameraButtons[5].disabled);
    for(const update of [{characterCamera:{...camera,available:false}},{characterCamera:camera,session:{admissionOpen:false}},
        {session:{admissionOpen:true},workshop:{open:true}},{workshop:{open:false},characterCamera:undefined}]){
        f.elements['salvage-more-controls'].open=false;
        f.state(update);f.cameraButtons[0].click();assert.equal(f.actions.length,6,'stale click must re-read permissions');
        assert(!f.environment['voxyCoveCameraMenu']());
        assert(!f.elements['salvage-more-controls'].open,'unavailable camera cannot reveal extra controls');
    }
    f.state({characterCamera:camera,workshop:{open:false}});f.tick();
    let swallowed=0;panel.listeners.get('keydown')({type:'keydown',key:'ArrowUp',stopPropagation(){++swallowed;}});
    panel.listeners.get('keyup')({type:'keyup',key:'ArrowUp',stopPropagation(){++swallowed;}});
    assert.equal(swallowed,1,'mouse-opened drawer lets old held keys release');
    const opener=f.environment['voxyCoveCameraMenu'];f.cleanup();assert.equal(f.environment['voxyCoveCameraMenu'],f.priorCameraMenu);
    assert(!opener()&&panel.hidden&&!panel.open);assert(f.cameraButtons.every(button=>button.listeners.size===0));
    assert(!f.elements['salvage-more-controls'].open);
    assert.equal(panel.listeners.size,0);
    console.log('Outside camera actions, paused permissions, bounds, stale events and global cleanup: 1 case passed');
}
{
    const f=fixture(true,true,true),more=f.elements['salvage-more-controls'];
    assert.equal(more.open,false,'Cove starts compact');
    more.open=true;f.tick();f.tick();assert.equal(more.open,true,'unchanged refresh preserves the user disclosure choice');
    f.state({workshop:{open:true,canOpen:true,name:'Brick 2 x 4',selected:0,parts:11,massKg:1035}});f.tick();
    assert(!more.open);assert(!f.elements['salvage-workshop'].hidden);
    more.open=true;f.tick();assert(more.open,'extra controls can still be reached from the workshop');
    f.state({workshop:{open:false,canOpen:true}});f.tick();assert(!more.open);
    assert.equal(f.elements['salvage-preview'].dataset.workshop,'false');
    f.elements['salvage-workshop-toggle'].click();assert.equal(f.actions.at(-1),60,'compact Workshop keeps its real handler');
    f.cleanup();
    const inspection=fixture(true);assert(inspection.elements['salvage-more-controls'].open,'inspection controls remain directly available');
    inspection.cleanup();
    console.log('Compact Cove disclosure, workshop transitions and unchanged inspection controls: 1 case passed');
}

{
    const f=fixture(true,true,true);f.state({world:'test',pause:{phase:'paused',canPause:false},session:{admissionOpen:true},workshop:{open:false},practice:{active:true,canReturn:false,message:'Returning to your workshop…'},job:{phase:'available',canAccept:true}});f.tick();
    assert.equal(f.elements['salvage-objective'].dataset.step,'practice');assert.equal(f.elements['salvage-objective-title'].textContent,'Free boat test');assert.match(f.elements['salvage-objective-detail'].textContent,/Returning/);assert(f.elements['salvage-objective-action'].hidden);f.cleanup();
    console.log('Practice objective priority over paused job/reward guidance: 1 case passed');
}
