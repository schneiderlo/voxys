import assert from 'node:assert/strict';
import {readFile,writeFile} from 'node:fs/promises';
import {createHash} from 'node:crypto';
import {fileURLToPath,pathToFileURL} from 'node:url';
import {resolve,dirname} from 'node:path';

// Isolated real-DOM regression. This never loads the game, supplies inventory,
// or claims a player journey. Browser default keyboard activation is the thing
// under test; recorded engine calls are an explicit UI-boundary test double.
const args=process.argv.slice(2),option=name=>args[args.indexOf(name)+1];
assert(args.includes('--playwright')&&args.includes('--browser')&&args.includes('--output'),
    'Pass --playwright <module> --browser <executable> --output <report.json>');
const imported=await import(pathToFileURL(resolve(option('--playwright'))).href);
const playwright=imported.chromium?imported:imported.default;
const root=resolve(dirname(fileURLToPath(import.meta.url)),'../..');
const script=resolve(root,'web/adventure_ui.js'),style=resolve(root,'web/adventure_ui.css');
const digest=async path=>createHash('sha256').update(await readFile(path)).digest('hex');
const browser=await playwright.chromium.launch({headless:true,executablePath:option('--browser'),
    args:['--disable-gpu','--disable-software-rasterizer','--disable-dev-shm-usage']});
const stages=[];
try{
    const page=await browser.newPage({viewport:{width:1280,height:800}});
    await page.setContent('<!doctype html><html><body><canvas id="game" tabindex="0" width="400" height="300"></canvas>'
        +'<p class="controls-hint">Legacy controls</p><div id="physics-backend-switcher">Legacy backend</div></body></html>');
    await page.addScriptTag({path:script});await page.addStyleTag({path:style});
    await page.evaluate(()=>{
        window.calls=[];window.documentKeys=[];
        window.uiState={build:false,piece:1,menu:'',rows:[],wood:80,stone:60,scrap:20,valid:false,dirty:false,status:'Ready'};
        // Match the document-level, bubbling WASM registration. A leaked Enter
        // produces a second command and fails the test, regardless of order.
        for(const type of ['keydown','keyup'])document.addEventListener(type,event=>{
            documentKeys.push([type,event.code]);
            if(type==='keydown'&&event.code==='Enter')calls.push([10,0,'document']);
        });
        const engine={_get_adventure_state_json:()=>JSON.stringify(uiState),UTF8ToString:value=>value,
            _adventure_action(action,value){calls.push([action,value,'ui']);if(action===9)uiState.menu='';}};
        window.ui=VoxyAdventureUI.install(engine,window);
    });
    assert(await page.locator('#adventure-ui').getAttribute('data-voxy-ui'));
    assert.equal(await page.locator('.controls-hint').isVisible(),false);
    assert.equal(await page.locator('#physics-backend-switcher').isVisible(),false);
    await page.evaluate(()=>{uiState.menu='Chest';uiState.rows=[{label:'Store wood',enabled:true},{label:'Take stone',enabled:true}];ui.refresh();});
    assert.equal(await page.locator('#adventure-menu-rows button').first().evaluate(element=>element===document.activeElement),true);
    await page.keyboard.press('Enter');
    let result=await page.evaluate(()=>({calls,documentKeys}));
    assert.deepEqual(result.calls,[[15,1,'ui'],[10,0,'ui']]);
    assert.deepEqual(result.documentKeys,[]);
    stages.push({name:'Enter default activation sends one row intent; no document key leakage',...result});

    await page.keyboard.press('Tab');
    assert.equal(await page.locator('#adventure-menu-rows button').nth(1).evaluate(element=>element===document.activeElement),true);
    await page.keyboard.press('Space');
    result=await page.evaluate(()=>({calls,documentKeys}));
    assert.deepEqual(result.calls,[[15,1,'ui'],[10,0,'ui'],[10,1,'ui']]);
    assert.deepEqual(result.documentKeys,[]);
    stages.push({name:'Tab keeps normal focus navigation; Space activates once; ownership stays inside panel',...result});

    await page.keyboard.press('Escape');
    result=await page.evaluate(()=>({calls,menu:uiState.menu,active:document.activeElement.tagName,documentKeys}));
    assert.equal(result.menu,'');assert.equal(result.active,'BODY');
    assert.deepEqual(result.calls.slice(-2),[[15,0,'ui'],[9,0,'ui']]);
    stages.push({name:'Escape releases focus and closes an actually open menu once',...result});

    await page.evaluate(()=>{calls.length=0;documentKeys.length=0;uiState.build=true;uiState.menu='';ui.refresh();});
    await page.locator('#adventure-build').focus();await page.keyboard.press('Escape');
    result=await page.evaluate(()=>({calls,build:uiState.build,menu:uiState.menu,active:document.activeElement.tagName}));
    assert.deepEqual(result.calls,[[15,1,'ui'],[15,0,'ui']]);
    assert.equal(result.build,true);assert.equal(result.menu,'');assert.equal(result.active,'BODY');
    stages.push({name:'Escape in world building only releases UI focus; does not toggle the game menu',...result});

    await page.evaluate(()=>{calls.length=0;});
    await page.locator('#adventure-save').focus();await page.locator('#game').click({position:{x:50,y:50}});
    result=await page.evaluate(()=>({calls,active:document.activeElement.id}));
    assert.deepEqual(result.calls,[[15,1,'ui'],[15,0,'ui']]);assert.equal(result.active,'game');
    await page.locator('#adventure-save').focus();await page.evaluate(()=>ui.cleanup());
    assert.equal(await page.locator('.controls-hint').isVisible(),true);
    assert.equal(await page.locator('#physics-backend-switcher').isVisible(),true);
    assert.equal(await page.locator('#adventure-ui').count(),0);
    assert.deepEqual(await page.evaluate(()=>calls.slice(-2)),[[15,1,'ui'],[15,0,'ui']]);
    stages.push({name:'Canvas click and cleanup release ownership; adventure-only legacy hiding is removed',...result});
    await writeFile(option('--output'),JSON.stringify({status:'passed',scope:'Isolated real Chromium DOM keyboard/focus regression; no game or GPU rendering.',
        browser:browser.version(),source_sha256:await digest(script),style_sha256:await digest(style),stages},null,2)+'\n');
    process.stdout.write(`Passed ${stages.length} real DOM input stages.\n`);
}finally{
    await browser.close();
}
