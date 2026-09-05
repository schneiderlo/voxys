from pathlib import Path
import re, subprocess
MAIN='3f06f364ce65a01da2abedc6a880c908903eee3a'
MOTO='19134562d032945072ddd463b021f2d93d886737'
def show(ref,path):
    return subprocess.check_output(['git','show',f'{ref}:{path}'],text=True)
def change(s,a,b):
    assert s.count(a)==1,(a[:120],s.count(a))
    return s.replace(a,b)
def edit(path,a,b):
    p=Path(path);p.write_text(change(p.read_text(),a,b))
def block(s,start):
    start=s.index(start);a=s.index('{',start);d=0
    for i in range(a,len(s)):
        if s[i]=='{': d+=1
        if s[i]=='}':
            d-=1
            if not d:return s[start:i+1]
    raise ValueError(start)
for file in ['BUILD','CMakeLists.txt']:
    p=Path(file);s,n=re.subn(r'<<<<<<< HEAD\n(.*?)=======\n.*?>>>>>>> [^\n]+\n',lambda m:m[1],p.read_text(),flags=re.S)
    assert n==1,(file,n);p.write_text(s)
Path('ridgebreak.cfg').write_text(show(MOTO,'voxy.cfg'))
Path('voxy.cfg').write_text(show(MAIN,'voxy.cfg'))
p=Path('shaders/ray_blit.wgsl')
s=subprocess.check_output(['git','show',':2:shaders/ray_blit.wgsl'],text=True)
s=change(s,'const MATERIAL_WATER : u32 = 2u;','''const MATERIAL_WATER : u32 = 2u;
// invProjParams.w selects the opt-in course; zero preserves the terrain demo.
fn authoredCourseEnabled() -> bool { return camera.invProjParams.w > 0.5; }
''')
s=change(s,'''    let coveZone = 1.0 - smoothstep(
        56.0, 80.0,
        length(vec2<f32>(coveInland, coveAlongshore)));''','''    let coveZone = select(1.0 - smoothstep(
        56.0, 80.0,
        length(vec2<f32>(coveInland, coveAlongshore))),
        0.0, authoredCourseEnabled());''')
s=change(s,'if (length(vec2<f32>(coveInland, coveAlongshore)) < 78.0)','if (!authoredCourseEnabled() && length(vec2<f32>(coveInland, coveAlongshore)) < 78.0)')
s=change(s,'    let grass = upland * grassSuitability *','    var grass = upland * grassSuitability *')
s=change(s,'''    let soil = max(upland - grass, 0.0);

    let weights''','''    var soil = max(upland - grass, 0.0);
    if (authoredCourseEnabled()) {
        let authoredSoil = smoothstep(0.035, 0.72,
            textureSampleLevel(terrainTex, terrainSampler,
                terrainUV(worldPosition), 0.0).a) * (1.0 - rock);
        sand *= 1.0 - authoredSoil;
        grass *= 1.0 - authoredSoil;
        soil = max(soil, authoredSoil);
    }

    let weights''')
f=block(s,'fn sampleTerrainSurface(')
f=change(f,'    let relativeElevation = worldPosition.y - camera.waterParams.x;','''    if (authoredCourseEnabled()) {
        let courseSoil = textureSampleLevel(
            terrainTex, terrainSampler, terrainUV(worldPosition), 0.0).a;
        albedo *= mix(vec3<f32>(1.0), vec3<f32>(0.30, 0.20, 0.12),
            smoothstep(0.08, 0.78, courseSoil) * 0.85);
    }
    let relativeElevation = worldPosition.y - camera.waterParams.x;''')
s=s.replace(block(s,'fn sampleTerrainSurface('),f)
s=change(s,'''    let coveMask =
        1.0 - smoothstep(
            50.0, 78.0,
            length(vec2<f32>(coveInland, coveAlongshore)));''','''    let coveMask = select(
        1.0 - smoothstep(50.0, 78.0,
            length(vec2<f32>(coveInland, coveAlongshore))),
        0.0, authoredCourseEnabled());''')
s=change(s,'''fn authoredCovePropHit(
    rayOrigin : vec3<f32>, rayDirection : vec3<f32>,
    maximumDistance : f32) -> CovePropHit {''','''fn authoredCovePropHit(
    rayOrigin : vec3<f32>, rayDirection : vec3<f32>,
    maximumDistance : f32) -> CovePropHit {
    if (authoredCourseEnabled()) { return missedCoveProp(); }''')
s=change(s,'fn coveDriftwoodContact(worldPosition : vec2<f32>) -> f32 {','''fn coveDriftwoodContact(worldPosition : vec2<f32>) -> f32 {
    if (authoredCourseEnabled()) { return 1.0; }''')
p.write_text(s)
edit('src/app/application.hpp','    std::string windowTitle = "RIDGEBREAK";','''    std::string windowTitle = "voxy";
    bool motoEnabled = false; // Opt-in prototype, never replaces the terrain demo.''')
p=Path('src/app/application.cpp');s=p.read_text()
s=change(s,'    if (config_.wreckwaterClient || config_.benchmarkOnStartup','    if (!config_.motoEnabled || config_.wreckwaterClient || config_.benchmarkOnStartup')
s=change(s,'    uniforms.setLegoMode(legoMode_);','''    uniforms.setLegoMode(legoMode_);
    uniforms.invProjParams.w = config_.motoEnabled ? 1.0f : 0.0f;''')
original=show(MAIN,'src/app/application.cpp');of=block(original,'bool Application::initTerrain()');cur=block(s,'bool Application::initTerrain()')
oldelse=block(of,'else {');newelse=block(cur,'else {')
cur=cur.replace(newelse,newelse.replace('else {','else if (config_.motoEnabled) {',1)+' '+oldelse)
s=s.replace(block(s,'bool Application::initTerrain()'),cur)
s=s.replace('LOG_INFO("  RIDGEBREAK - Motocross Freeride");','LOG_INFO("  {}", config_.motoEnabled ? "RIDGEBREAK - Motocross Freeride" : "Voxy - Terrain Renderer");')
p.write_text(s)
p=Path('src/engine/platform/native/entry.cpp');s=show(MAIN,str(p));s=change(s,'    voxy::ApplicationConfig appConfig;','''    voxy::ApplicationConfig appConfig;
    appConfig.motoEnabled = config.window.title == "RIDGEBREAK";''')
s=change(s,'''        appConfig.heightmapWidth = 256;
        appConfig.heightmapHeight = 256;''','''        appConfig.heightmapWidth = appConfig.motoEnabled ? 2048u : 256u;
        appConfig.heightmapHeight = appConfig.heightmapWidth;''');p.write_text(s)
p=Path('src/engine/platform/wasm/entry.cpp');s=p.read_text();s=change(s,'    voxy::ApplicationConfig appConfig;','''    voxy::ApplicationConfig appConfig;
    appConfig.motoEnabled = config.window.title == "RIDGEBREAK";''')
s=change(s,'''        appConfig.heightmapWidth = 2048;
        appConfig.heightmapHeight = 2048;''','''        appConfig.heightmapWidth = appConfig.motoEnabled ? 2048u : 256u;
        appConfig.heightmapHeight = appConfig.heightmapWidth;''');p.write_text(s)
p=Path('web/index.html');s=p.read_text();s=change(s,'<title>RIDGEBREAK</title>','<title>voxy — WebGPU Terrain Renderer</title>')
a=s.index('                    arguments: (() => {');b=s.index('                    })(),',a)+len('                    })(),')
s=s[:a]+'''                    arguments: (() => {
                        const params = new URLSearchParams(location.search);
                        const args = params.get('experience') === 'ridgebreak'
                            ? ['--config', 'ridgebreak.cfg'] : [];
                        const value = Number(params.get('physicsMaxBodies'));
                        if (Number.isSafeInteger(value) && value >= 2 && value <= 1000000) {
                            args.push('--physics-max-bodies', String(value));
                        }
                        return args;
                    })(),'''+s[b:]
orig=show(MAIN,'web/index.html');ma=re.search(r'        <div class="controls-hint">.*?</div>',orig,re.S);mc=re.search(r'        <div class="controls-hint">.*?</div>',s,re.S)
assert ma and mc
s=s[:mc.start()]+ma[0]+s[mc.end():]
s=change(s,"                        document.body.classList.add('ridgebreak-active');",'''                        document.body.classList.add('ridgebreak-active');
                        document.title = 'RIDGEBREAK';
                        document.querySelector('.controls-hint').textContent =
                            'W/S throttle/brake · A/D steer · Q/E lean · R remount · C circuit';''')
p.write_text(s)
edit('BUILD','        "--preload-file", "voxy.cfg@/voxy.cfg",','''        "--preload-file", "voxy.cfg@/voxy.cfg",
        "--preload-file", "ridgebreak.cfg@/ridgebreak.cfg",''')
p=Path('BUILD');s=p.read_text();assert s.count('        "voxy.cfg",')==3;p.write_text(s.replace('        "voxy.cfg",','        "voxy.cfg",\n        "ridgebreak.cfg",'))
edit('CMakeLists.txt','        foreach(VOXY_WASM_DATA_FILE\n','        foreach(VOXY_WASM_DATA_FILE\n            ridgebreak.cfg\n')
edit('tools/test_water_material_work.py',"        self.assertNotIn('@binding(19)',RAY)","        self.assertIn('@binding(19) var periodicGradientLut',RAY)")
edit('tools/test_periodic_gradient_lut.py',"        self.assertIn('timestampWrites.endOfPassWriteIndex = beginTimestampOnly', source)","        self.assertIn('(beginTimestampOnly || deferTimestampEnd)', source)\n        self.assertIn('particleTimestamps.endOfPassWriteIndex = timestampEnd', source)")
p=Path('scripts/test_water_material_work.mjs');s=p.read_text()
s=change(s,"const payload={reference, before, after,",'''const bakeHeader = await readFile(path.join(root,'src/render/periodic_gradient_lut.hpp'),'utf8');
const bake = bakeHeader.match(/R"wgsl\(([\s\S]*?)\)wgsl"/)[1];
const payload={reference, before, after, bake,''')
s=change(s,'async function gpuTest({reference,before,after,math,surfaceFixture,waterFixture}) {','async function gpuTest({reference,before,after,bake,math,surfaceFixture,waterFixture}) {')
s=change(s,"    const helpers=common.map(n=>fn(source,n)).join('\\n');",'''    if(source.includes('fn authoredCourseEnabled(')) common.push('authoredCourseEnabled','terrainUV');
    const helpers=common.map(n=>fn(source,n)).join('\\n');
    const lut=source.includes('override USE_PERIODIC_GRADIENT_LUT') ? `
@group(0) @binding(19) var periodicGradientLut : texture_2d<f32>;
override USE_PERIODIC_GRADIENT_LUT : bool = true;` : '';''')
s=change(s,'${helpers}\\n${caseDeclaration}', '''@group(0) @binding(4) var terrainTex : texture_2d<f32>;
@group(0) @binding(6) var terrainSampler : sampler;
${lut}
${helpers}\\n${caseDeclaration}''')
s=change(s,'    const layout0=device.createBindGroupLayout({entries});','''    // Bind the actual GPU-generated table in combined material/water fixtures.
    const gradients=device.createTexture({size:[32,16],format:'rgba32float',
        usage:T.TEXTURE_BINDING|T.STORAGE_BINDING});
    const bakePipeline=await device.createComputePipelineAsync({layout:'auto',
        compute:{module:await module(bake,'production gradient bake'),entryPoint:'main'}});
    const bakeGroup=device.createBindGroup({layout:bakePipeline.getBindGroupLayout(0),
        entries:[{binding:0,resource:gradients.createView()}]});
    const bakeEncoder=device.createCommandEncoder();const bakePass=bakeEncoder.beginComputePass();
    bakePass.setPipeline(bakePipeline);bakePass.setBindGroup(0,bakeGroup);
    bakePass.dispatchWorkgroups(2,2);bakePass.end();device.queue.submit([bakeEncoder.finish()]);
    entries.push({binding:19,visibility,texture:{sampleType:'unfilterable-float'}});
    resources.push({binding:19,resource:gradients.createView()});
    const layout0=device.createBindGroupLayout({entries});''')
p.write_text(s)
