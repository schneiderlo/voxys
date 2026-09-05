from pathlib import Path

def change(s,a,b):
    assert s.count(a)==1,(a[:100],s.count(a));return s.replace(a,b)
def section(s,a,b):
    i=s.index(a);j=s.index(b,i);return s[i:j]
def indent(s,n=4):
    return '\n'.join((' '*n+l if l else l) for l in s.split('\n'))
p=Path('shaders/water_clipmap.wgsl');s=p.read_text()
a=s.index('fn shadeWaterFragment(');f=s[a:]
f=change(f,'    let light = normalize(camera.lightDirWS.xyz);','''    let cameraUnderwater = camera.waterMotion.z > 0.5;
    var transmissionDirection = vec3<f32>(0.0);
    var totalInternalReflection = false;
    if (cameraUnderwater) {
        transmissionDirection = refract(-view, -normal, oceanIor());
        totalInternalReflection = dot(transmissionDirection, transmissionDirection) < 0.001;
    }
    let light = normalize(camera.lightDirWS.xyz);''')
f=change(f,'''    let environment = sampleEnvironment(reflected, reflectionRoughness) *
                      camera.waterColorA.w;''','''    var environment = vec3<f32>(0.0);
    if (!totalInternalReflection) {
        environment = sampleEnvironment(reflected, reflectionRoughness) *
                      camera.waterColorA.w;
    }''')
scattering=section(f,'    let scatterLobe =','    let halfway =');f=f.replace(scattering,'')
f=change(f,'''    let cameraUnderwater = camera.waterMotion.z > 0.5;
    let opticalCoverage = smoothstep(0.12, 12.0, waterDepth);
''','')
world=section(f,'    let worldPerPixel =','    let incomingRay =');f=f.replace(world,'')
refraction=section(f,'    let distortionCoverage =','    let transmittance =')
refraction=change(refraction,'    let hasOpaqueRefraction = selectedRefractionDepth > 0.0;','    hasOpaqueRefraction = selectedRefractionDepth > 0.0;')
refraction=change(refraction,'    let thickness = clamp(bedTravel, 0.0, 500.0);','    thickness = clamp(bedTravel, 0.0, 500.0);')
refraction=change(refraction,'    var refracted : vec3<f32>;\n','')
old=section(f,'    let distortionCoverage =','    let transmittance =')
f=f.replace(old,world+'''    var hasOpaqueRefraction = false;
    var thickness = 0.0;
    var refracted = vec3<f32>(0.0);
    // A totally reflected underwater ray cannot use scene refraction. Avoid
    // its depth reads, UV distortion and fallback material evaluation entirely.
    if (!totalInternalReflection) {
'''+indent(refraction)+'''    }

''')
transmission=section(f,'    let transmittance =','    let crestCompression =');f=f.replace(transmission,'')
f=change(f,'''        let undersideNormal = -normal;
        var transmissionDirection =
            refract(-view, undersideNormal, oceanIor());
        let totalInternalReflection =
            dot(transmissionDirection, transmissionDirection) < 0.001;
''','')
f=change(f,'''    } else {
        color = mix(refractedWater, reflectedWater,''','''    } else {
'''+indent(scattering)+indent(transmission)+'''        let opticalCoverage = smoothstep(0.12, 12.0, waterDepth);
        color = mix(refractedWater, reflectedWater,''')
s=s[:a]+f;p.write_text(s)
for path,scale in [('shaders/water_clipmap.wgsl','0.86'),('shaders/ray_blit.wgsl','0.88')]:
    p=Path(path);s=p.read_text();i=s.index('fn proceduralSeabed(');j=s.index('\nfn ',i+4)
    f=s[i:j];mark='    let causticScale = 46.0;' if 'water_clipmap' in path else '    let motion = camera.waterMotion.x;'
    f=change(f,mark,'''    // Beyond the existing fade endpoint this sample contributes exactly zero.
    if (pathLength >= 360.0) { return albedo * '''+scale+'''; }
'''+mark);s=s[:i]+f+s[j:];p.write_text(s)
p=Path('shaders/ray_blit.wgsl');s=p.read_text()
s=change(s,'''    let nDotL = max(dot(normal, light), 0.0);
    let nDotH''','''    let nDotL = max(dot(normal, light), 0.0);
    if (nDotL == 0.0) { return vec3<f32>(0.0); }
    let nDotH''')
p.write_text(s)
p=Path('tools/test_water_material_work.py');s=p.read_text();s=change(s,"        self.assertIn('var refracted : vec3<f32>;\n    if (hasOpaqueRefraction)',WATER)","        self.assertIn('if (!totalInternalReflection)',WATER)\n        self.assertIn('if (hasOpaqueRefraction)',WATER)");p.write_text(s)
p=Path('scripts/test_periodic_gradient_lut.mjs');s=p.read_text()
s=change(s,'i < 200 && !port','i < 1200 && !port')
s=change(s,"    '--headless=new', '--no-sandbox', '--enable-unsafe-webgpu',", "    '--headless=new', '--no-sandbox', '--enable-unsafe-webgpu',\n    '--no-first-run', '--no-default-browser-check', '--disable-background-networking',")
p.write_text(s)
p=Path('scripts/test_water_material_work.mjs');s=p.read_text()
s=change(s,'struct SurfaceOutput {', '''@fragment fn specularFixture(@builtin(position) pixel : vec4<f32>) -> @location(0) vec4<f32> {
    let c=cases[u32(pixel.y)*64u+u32(pixel.x)];
    return vec4<f32>(terrainSpecular(normalize(c.n.xyz),normalize(vec3<f32>(0.2,0.8,0.5)),
        normalize(c.p.xyz),c.x.w,c.y.w),1.0);
}
@fragment fn seabedFixture(@builtin(position) pixel : vec4<f32>) -> @location(0) vec4<f32> {
    let c=cases[u32(pixel.y)*64u+u32(pixel.x)];
    return vec4<f32>(proceduralSeabed(c.p.xz,c.x.w),1.0);
}
struct SurfaceOutput {''')
s=change(s,'const waterFixture=`${caseDeclaration}\n','''const waterFixture=`${caseDeclaration}
@fragment fn seabedFixture(@builtin(position) pixel : vec4<f32>) -> @location(0) vec4<f32> {
    let c=cases[u32(pixel.y)*64u+u32(pixel.x)];
    return vec4<f32>(proceduralSeabed(c.p.xz,c.x.w,0.05),1.0);
}
''')
s=change(s,'    // Exercise the exact beginning/middle/end timestamp topology of blit.','''    // Cover zero-contribution boundaries and both sides of the critical angle.
    u[57]=12;u[110]=0;device.queue.writeBuffer(uniform,0,u);
    const boundary=new Float32Array(samples);
    for(let i=0;i<4096;++i)boundary[i*16+11]=[0.01,95,359.999,360,360.001,1000][i%6];
    await fragmentPair(shaders.slice(0,2),'seabedFixture',['rgba32float'],boundary,'legacy seabed: caustic fade boundary');
    await fragmentPair(shaders.slice(2,4),'seabedFixture',['rgba32float'],boundary,'clipmap seabed: caustic fade boundary');
    const specular=new Float32Array(4096*16);
    for(let i=0;i<4096;++i)specular.set([.5,[-1,-.001,0,.001,1][i%5],.5,0,
        0,1,0,0,0,0,0,[.08,.2,.6,1][i%4],0,0,0,(i%7)/6],i*16);
    await fragmentPair(shaders.slice(0,2),'specularFixture',['rgba32float'],specular,'terrain specular: backlit and grazing');
    const critical=new Float32Array(4096*16);
    const angle=Math.asin(1/1.33);
    for(let i=0;i<4096;++i){const a=angle+[-.2,-.0001,0,.0001,.2][i%5];
        critical.set([12*Math.tan(a),0,0,0,0,1,0,0,0,0,0,20,0,0,0,.04],i*16);}
    u[57]=-12;u[110]=1;device.queue.writeBuffer(uniform,0,u);
    await fragmentPair(shaders.slice(2,4),'waterFixture',['rgba32float','r32float'],critical,'water: critical reflection angles');
    if(before[0].includes('fn authoredCourseEnabled(')){
        u[63]=1;u[57]=12;u[110]=0;device.queue.writeBuffer(uniform,0,u);
        await fragmentPair(shaders.slice(0,2),'surfaceFixture',['rgba32float','rgba32float'],samples,'opt-in course: materials');
        u[63]=0;device.queue.writeBuffer(uniform,0,u);
    }
    // Exercise the exact beginning/middle/end timestamp topology of blit.''')
p.write_text(s)
p=Path('tools/test_water_material_work.py');s=p.read_text()
s=change(s,"    def test_telemetry_reports_sample_age_and_real_queue_limit(self):",'''    def test_new_shortcuts_only_remove_zero_or_unused_contributions(self):
        for distance in [360.0,360.001,1000.0]:
            self.assertEqual(1-smooth(95,360,distance),0)
        for source,gain in [(RAY,'0.88'),(WATER,'0.86')]:
            self.assertIn('if (pathLength >= 360.0) { return albedo * '+gain,source)
        water_body=WATER[WATER.index('fn shadeWaterFragment('):]
        guard=water_body.index('if (!totalInternalReflection) {',water_body.index('var hasOpaqueRefraction'))
        self.assertLess(guard,water_body.index('var distortedUv'))
        self.assertIn('if (nDotL == 0.0) { return vec3<f32>(0.0); }',RAY)

    def test_course_remains_opt_in(self):
        app=(ROOT/'src/app/application.hpp').read_text()
        self.assertIn('bool motoEnabled = false',app)
        self.assertIn('if (!config_.motoEnabled', (ROOT/'src/app/application.cpp').read_text())
        cfg=(ROOT/'voxy.cfg').read_text()
        self.assertIn('td_seed_1234_2048_albedo.jpg',cfg)
        self.assertIn('td_seed_1234_8192.ldh',cfg)
        self.assertTrue((ROOT/'ridgebreak.cfg').exists())
        self.assertIn("params.get('experience') === 'ridgebreak'", (ROOT/'web/index.html').read_text())

    def test_telemetry_reports_sample_age_and_real_queue_limit(self):''')
p.write_text(s)
