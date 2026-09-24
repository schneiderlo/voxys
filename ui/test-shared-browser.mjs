// Browser input/accessibility verification for the GPU HUD's semantic peers.
// A synthetic host publishes hit boxes; native GPU appearance is tested separately.
import assert from 'node:assert/strict';
import fs from 'node:fs/promises';
import http from 'node:http';
import path from 'node:path';
import {fileURLToPath,pathToFileURL} from 'node:url';
const args=Object.fromEntries(process.argv.slice(2).map(arg=>{const i=arg.indexOf('=');return[arg.slice(2,i),arg.slice(i+1)];}));
if(!args.playwright||!args.browser)throw Error('Required: --playwright=MODULE --browser=CHROME');
const {chromium}=await import(pathToFileURL(path.resolve(args.playwright)));
const web=fileURLToPath(new URL('../web/',import.meta.url));
const html='<!doctype html><html><head><link rel="stylesheet" href="/build_ui.css"></head><body style="margin:0"><canvas id="voxy-canvas" tabindex="0" style="position:fixed;inset:0;width:100%;height:100%"></canvas><script src="/build_ui.js"></script></body></html>';
const server=http.createServer(async(q,s)=>{
    const name=new URL(q.url,'http://localhost').pathname;
    if(name==='/'){s.setHeader('Content-Type','text/html');s.end(html);return;}
    const file=path.resolve(web,name.slice(1));if(!file.startsWith(web)){s.writeHead(403);s.end();return;}
    try{s.setHeader('Content-Type',name.endsWith('.js')?'text/javascript':name.endsWith('.css')?'text/css':'application/octet-stream');s.end(await fs.readFile(file));}catch{s.writeHead(404);s.end();}
});
await new Promise(r=>server.listen(0,'127.0.0.1',r));
let browser;
try{
    browser=await chromium.launch({executablePath:args.browser,headless:true,args:['--no-sandbox']});
    const page=await browser.newPage({viewport:{width:1280,height:720}}),errors=[];page.on('pageerror',e=>errors.push(String(e)));
    await page.goto(`http://127.0.0.1:${server.address().port}/`);
    await page.evaluate(()=>{
        const control=(label,action,value,x=440,extra={})=>({label,action,value,x,y:600,width:100,height:60,intent:0,row:-1,enabled:true,...extra});
        window.fixture={creative:true,ready:true,mode:'build',piece:10,menuToken:1,observation:'1',rows:[],dirty:false,hud:{width:1280,height:720,controls:[control('Brick 2 × 4',2,10),control('Colours',38,0,550)]}};
        window.calls=[];window.canvasClicks=0;window.documentTabs=0;
        window.update=changes=>{fixture={...fixture,...changes,observation:String(Number(fixture.observation)+1)};};
        document.querySelector('canvas').addEventListener('click',()=>canvasClicks++);
        document.addEventListener('keydown',e=>{if(e.key==='Tab'){documentTabs++;e.preventDefault();}});
        const engine={_get_adventure_state_json:()=>1,UTF8ToString:()=>JSON.stringify(fixture),_adventure_action:(action,value)=>{
            calls.push([action,value]);
            if(action===23)update({mode:'catalog',menuSelected:0,hud:{...fixture.hud,controls:[control('Brick 1 × 2',10,8,440,{intent:65,row:0}),control('Close',20,0,550)]}});
            if(action===20)update({mode:'build'});
        }};
        window.app=VoxyBuildUI.installShared(engine,window);
    });
    await page.waitForSelector('.shared-hud-peer');
    assert.equal(await page.locator('#build-ui').count(),0);
    assert.equal(await page.locator('.shared-hud-peer').first().evaluate(el=>getComputedStyle(el).pointerEvents),'none');
    await page.mouse.click(480,630);assert.equal(await page.evaluate(()=>canvasClicks),1);
    assert.equal(await page.evaluate(()=>calls.some(([id])=>id===2)),false,'DOM peers must not dispatch pointer actions as well as native hit tests');
    await page.locator('canvas').focus();await page.keyboard.press('Tab');
    await page.waitForFunction(()=>document.activeElement.getAttribute('aria-label')==='Brick 1 × 2');
    assert.equal(await page.evaluate(()=>documentTabs),0,'Canvas Tab must reach a11y before Emscripten eats it');
    assert.equal(await page.locator('.shared-hud-peer').first().evaluate(el=>getComputedStyle(el).outlineStyle),'solid');
    await page.keyboard.press('Tab');assert.equal(await page.evaluate(()=>document.activeElement.getAttribute('aria-label')),'Close');
    await page.keyboard.press('Tab');assert.equal(await page.evaluate(()=>document.activeElement.getAttribute('aria-label')),'Brick 1 × 2');
    await page.keyboard.press('Escape');await page.waitForFunction(()=>fixture.mode==='build');
    assert.equal(await page.evaluate(()=>document.activeElement.id),'voxy-canvas');
    await page.setViewportSize({width:640,height:360});await page.waitForTimeout(100);
    const b=await page.locator('.shared-hud-peer').first().boundingBox();
    assert.equal(b.x,220);assert.equal(b.y,300);assert.equal(b.width,50);assert.equal(b.height,30);
    await page.evaluate(()=>app.cleanup());assert.equal(await page.locator('#shared-hud-accessibility').count(),0);
    assert.deepEqual(errors,[]);
    console.log('Shared HUD browser checks passed: pointer ownership, keyboard entry, focus, modal Tab, Escape, resize and cleanup.');
}finally{await browser?.close();server.close();}
