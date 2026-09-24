import {build} from 'esbuild';
import {readFile, writeFile, copyFile} from 'node:fs/promises';
import {createHash} from 'node:crypto';
await build({entryPoints:['src/main.jsx'],bundle:true,minify:true,format:'iife',globalName:'VoxyBuildUI',
    outfile:'../web/build_ui.js',jsx:'automatic',jsxImportSource:'preact',target:['es2020'],external:['dm-sans-latin.woff2'],legalComments:'eof'});
const packages=['preact','lucide-preact'];
let notices='Voxys building UI — third-party notices\n\n';
for(const name of packages){
    const pkg=JSON.parse(await readFile(`node_modules/${name}/package.json`,'utf8'));
    notices+=`${name} ${pkg.version}\n${await readFile(`node_modules/${name}/LICENSE`,'utf8')}\n\n`;
}
notices+=`DM Sans — Latin subset, variable weight and optical size\n${await readFile('assets/DM-Sans-OFL.txt','utf8')}\n`;
await copyFile('assets/dm-sans-latin.woff2','../web/dm-sans-latin.woff2');
await writeFile('../web/build_ui_licenses.txt',notices);

const hashes=async paths=>Object.fromEntries(await Promise.all(paths.map(async path=>[path,createHash('sha256').update(await readFile(`../${path}`)).digest('hex')])));
await writeFile('../web/build_ui_manifest.json',JSON.stringify({
    sources:await hashes(['ui/package.json','ui/package-lock.json','ui/build.mjs','ui/src/main.jsx','ui/src/bridge.js','ui/src/shared.js','ui/src/style.css','ui/assets/dm-sans-latin.woff2','ui/assets/DM-Sans-OFL.txt']),
    outputs:await hashes(['web/build_ui.js','web/build_ui.css','web/build_ui_licenses.txt','web/dm-sans-latin.woff2'])
},null,2)+'\n');
