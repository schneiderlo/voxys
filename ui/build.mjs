import {build} from 'esbuild';
import {readFile, writeFile} from 'node:fs/promises';
import {createHash} from 'node:crypto';
await build({entryPoints:['src/main.jsx'],bundle:true,minify:true,format:'iife',globalName:'VoxyBuildUI',
    outfile:'../web/build_ui.js',jsx:'automatic',jsxImportSource:'preact',target:['es2020'],legalComments:'eof'});
const packages=['preact','lucide-preact'];
let notices='Voxys building UI — third-party notices\n\n';
for(const name of packages){
    const pkg=JSON.parse(await readFile(`node_modules/${name}/package.json`,'utf8'));
    notices+=`${name} ${pkg.version}\n${await readFile(`node_modules/${name}/LICENSE`,'utf8')}\n\n`;
}
await writeFile('../web/build_ui_licenses.txt',notices);

const hashes=async paths=>Object.fromEntries(await Promise.all(paths.map(async path=>[path,createHash('sha256').update(await readFile(`../${path}`)).digest('hex')])));
await writeFile('../web/build_ui_manifest.json',JSON.stringify({
    sources:await hashes(['ui/package.json','ui/package-lock.json','ui/build.mjs','ui/src/main.jsx','ui/src/bridge.js','ui/src/style.css']),
    outputs:await hashes(['web/build_ui.js','web/build_ui.css','web/build_ui_licenses.txt'])
},null,2)+'\n');
