// Real Chromium layout/input checks using an explicitly synthetic engine fixture.
// Pass the local Playwright module and Chromium binary; no browser is downloaded.
import assert from 'node:assert/strict';
import fs from 'node:fs/promises';
import http from 'node:http';
import path from 'node:path';
import {fileURLToPath,pathToFileURL} from 'node:url';
const args=Object.fromEntries(process.argv.slice(2).map(arg=>{const i=arg.indexOf('=');return[arg.slice(2,i),arg.slice(i+1)];}));
if(!args.playwright||!args.browser||!args.output)throw Error('Required: --playwright=MODULE --browser=CHROME --output=DIRECTORY');
const {chromium}=await import(pathToFileURL(path.resolve(args.playwright)));
const web=fileURLToPath(new URL('../web/',import.meta.url)),output=path.resolve(args.output);
await fs.mkdir(output,{recursive:true});
const html=`<!doctype html><html><head><meta name="viewport" content="width=device-width,initial-scale=1"><link rel="stylesheet" href="/style.css"><link rel="stylesheet" href="/build_ui.css"><link rel="stylesheet" href="/adventure_ui.css"></head><body style="margin:0;background:linear-gradient(150deg,#a6cad6,#709da9 35%,#6c907b 65%,#a8b9a1);height:100vh"><canvas id="voxy-canvas" tabindex="0" style="position:fixed;inset:0;width:100%;height:100%;outline:none"></canvas><script src="/build_ui.js"></script></body></html>`;
const server=http.createServer(async(q,s)=>{
    const name=decodeURIComponent(new URL(q.url,'http://localhost').pathname);
    if(name==='/'){s.setHeader('Content-Type','text/html');s.end(html);return;}
    const file=path.resolve(web,name.slice(1));
    if(!file.startsWith(web)){s.writeHead(403);s.end();return;}
    try{const bytes=await fs.readFile(file);s.setHeader('Content-Type',({'.js':'text/javascript','.css':'text/css','.svg':'image/svg+xml','.woff2':'font/woff2'})[path.extname(file)]||'application/octet-stream');s.end(bytes);}
    catch{s.writeHead(404);s.end();}
});
await new Promise(r=>server.listen(0,'127.0.0.1',r));
let browser;const errors=[],checks=[];
try{
    browser=await chromium.launch({executablePath:args.browser,headless:true,args:['--no-sandbox']});
    const page=await browser.newPage({viewport:{width:1440,height:900},reducedMotion:'reduce'});
    page.on('pageerror',e=>errors.push(String(e)));
    await page.goto(`http://127.0.0.1:${server.address().port}/`);
    await page.evaluate(()=>{
        window.fixture={creative:true,ready:true,mode:'build',piece:10,menuToken:1,observation:'1',rows:[],parts:28,paint:0,colourAvailable:true,canUndo:true,canRemove:true,valid:true,status:'Ready',dirty:true,saveStatus:'Unsaved build',cannon:{available:true,nearby:false}};
        window.calls=[];window.updateFixture=changes=>{Object.assign(fixture,changes);fixture.observation=String(Number(fixture.observation)+1);};
        const engine={_get_adventure_state_json:()=>1,UTF8ToString:()=>JSON.stringify(fixture),_adventure_action:(id,value)=>{
            calls.push([id,value]);if(id===2)updateFixture({piece:value});if(id===30)updateFixture({paint:value});
            if(id===20)updateFixture({mode:'build',rows:[]});if(id===8)updateFixture({dirty:false,saveStatus:'Saved build'});
        }};
        window.fixtureApp=VoxyBuildUI.install(engine,window);
    });
    await page.waitForSelector('.bb-dock');await page.evaluate(()=>document.fonts.ready);
    const screenshot=async name=>{await page.screenshot({path:path.join(output,name+'.png')});checks.push(name);};
    const contained=async selector=>{
        const result=await page.locator(selector).evaluate(el=>{const b=el.getBoundingClientRect();return{left:b.left,right:b.right,top:b.top,bottom:b.bottom,width:innerWidth,height:innerHeight,overflow:el.scrollWidth-el.clientWidth};});
        assert(result.left>=-1&&result.right<=result.width+1&&result.top>=-1&&result.bottom<=result.height+1,`${selector} escapes viewport: ${JSON.stringify(result)}`);
        assert(result.overflow<=1,`${selector} overflows horizontally: ${JSON.stringify(result)}`);
    };
    await contained('.bb-dock');
    const dockBounds=await page.locator('.bb-dock').boundingBox();
    assert(dockBounds.height<=115&&dockBounds.width<=680,'Gameplay hotbar must stay compact');
    await screenshot('desktop-build');
    await page.locator('[data-popup-toggle="colour"]').click();await page.waitForSelector('#bb-colour-popup');
    await contained('.bb-popup');await screenshot('desktop-colour');await page.locator('.bb-colours').screenshot({path:path.join(output,'palette-detail.png')});
    assert.equal(await page.locator('.bb-swatch').count(),7);
    for(const swatch of await page.locator('.bb-swatch').all()){
        assert(await swatch.evaluate(el=>{const b=el.getBoundingClientRect();return b.width>=44&&b.height>=44&&document.elementFromPoint(b.x+b.width/2,b.y+b.height/2)?.closest('button')===el;}),'Each paint brick must have an unobstructed touch target');
    }
    await page.getByRole('button',{name:'Sunflower colour',exact:true}).hover();
    await page.waitForFunction(()=>document.querySelector('.bb-colour-caption strong').textContent==='Sunflower');
    await page.getByRole('button',{name:'Sage colour',exact:true}).click();
    await page.waitForFunction(()=>fixture.paint===0x86a789&&!document.querySelector('.bb-popup'));
    await page.locator('[data-popup-toggle="colour"]').click();await page.waitForSelector('.bb-popup');
    assert.equal(await page.getByRole('button',{name:'Sage colour',exact:true}).getAttribute('aria-pressed'),'true');
    await page.keyboard.press('Escape');await page.waitForSelector('.bb-popup',{state:'detached'});
    assert.equal(await page.locator('[data-popup-toggle="colour"]').evaluate(el=>el===document.activeElement),true);
    await page.getByRole('button',{name:'Brick 2 × 4',exact:true}).focus();await page.keyboard.press('ArrowRight');await page.keyboard.press('Enter');
    await page.waitForFunction(()=>fixture.piece===2);assert.equal(await page.evaluate(()=>calls.filter(c=>c[0]===2).at(-1)[1]),2);
    await page.getByRole('button',{name:'Save build',exact:true}).click();await page.waitForFunction(()=>document.querySelector('[aria-label="Save build"]').textContent==='Saved');
    await page.evaluate(()=>updateFixture({mode:'catalog',menuToken:2,menuSelected:0,catalogCategory:1,rows:[{label:'Brick 1 × 2',pieceKind:8,intent:81,enabled:true},{label:'Brick 2 × 2',pieceKind:9,intent:82,enabled:true},{label:'Brick 2 × 4',pieceKind:10,intent:83,enabled:true}]}));
    await page.waitForSelector('.bb-catalog');await screenshot('desktop-catalog');
    await page.getByRole('searchbox').fill('2 × 4');await page.waitForFunction(()=>document.querySelectorAll('.bb-catalog-piece').length===1);
    assert.equal(await page.locator('.bb-catalog-piece').getAttribute('data-row'),'2');
    assert.equal(await page.getByRole('searchbox').evaluate(el=>el===document.activeElement),true);
    assert.equal(await page.locator('.bb-top-actions').evaluate(el=>el.inert),true);
    await page.getByRole('searchbox').fill('nothing');await page.waitForSelector('.bb-empty');await screenshot('catalog-empty');
    await page.getByRole('button',{name:'Clear search'}).click();
    await page.setViewportSize({width:390,height:844});await contained('.bb-dialog');await screenshot('mobile-catalog');
    await page.keyboard.press('Escape');await page.waitForSelector('.bb-dock');await contained('.bb-dock');await contained('.bb-top-actions');await screenshot('mobile-build');
    await page.locator('[data-popup-toggle="colour"]').click();await page.waitForSelector('.bb-popup');await contained('.bb-popup');await screenshot('mobile-colour');await page.keyboard.press('Escape');
    await page.setViewportSize({width:320,height:740});
    await page.evaluate(()=>updateFixture({textScale:1.5,highContrast:true,reducedMotion:true}));
    await page.waitForFunction(()=>document.querySelector('#build-ui').style.getPropertyValue('--bb-scale')==='1.5');
    await contained('.bb-dock');await contained('.bb-top-actions');await screenshot('large-text-contrast');
    await page.locator('[data-popup-toggle="tools"]').click();await page.waitForSelector('.bb-tool-menu');await contained('.bb-popup');await screenshot('large-text-tools');await page.keyboard.press('Escape');
    await page.locator('[data-popup-toggle="colour"]').click();await page.waitForSelector('.bb-colours');await contained('.bb-colours');await screenshot('large-text-colour');await page.keyboard.press('Escape');
    await page.evaluate(()=>updateFixture({mode:'catalog',menuToken:3,menuSelected:0,catalogCategory:0,rows:['Foundation','Floor','Wall','Open doorway','Flat roof','Stairs','Beam','Pier','Hinged door'].map((label,i)=>({label,pieceKind:[1,2,3,4,5,6,7,14,15][i],intent:100+i,enabled:true}))}));
    await page.waitForSelector('.bb-catalog');await contained('.bb-catalog');await screenshot('large-text-catalog');
    for(const tile of await page.locator('.bb-catalog-piece').all())assert(await tile.evaluate(el=>el.scrollWidth<=el.clientWidth+1),'Piece label must fit at enlarged text size');
    await page.setViewportSize({width:1440,height:900});await page.evaluate(()=>updateFixture({textScale:1,highContrast:false}));
    await page.waitForFunction(()=>document.querySelector('#build-ui').style.getPropertyValue('--bb-scale')==='1');
    await contained('.bb-catalog');await screenshot('desktop-structure');await page.locator('.bb-catalog').screenshot({path:path.join(output,'catalog-detail.png')});await page.keyboard.press('Escape');await page.waitForSelector('.bb-dock');
    await page.setViewportSize({width:844,height:390});await page.evaluate(()=>updateFixture({textScale:1,highContrast:false}));await page.waitForTimeout(150);
    await contained('.bb-dock');await page.locator('[data-popup-toggle="tools"]').click();await page.waitForSelector('.bb-popup');await contained('.bb-popup');await screenshot('landscape-tools');await page.keyboard.press('Escape');
    await page.locator('[data-popup-toggle="colour"]').click();await page.waitForSelector('.bb-colours');await contained('.bb-colours');await screenshot('landscape-colour');
    assert.deepEqual(errors,[]);
    await fs.writeFile(path.join(output,'report.json'),JSON.stringify({fixture:'synthetic engine; real Chromium layout and input',checks,pageErrors:errors},null,2));
    console.log(`${checks.length} browser layouts and keyboard/action checks passed.`);
}finally{await browser?.close();server.close();}
