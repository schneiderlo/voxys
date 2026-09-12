// Cove cosmetic pool: same live water displacement and output transform.
// Color blends after presentation; this does not claim linear HDR compositing.
struct CameraUniforms {
    viewProj : mat4x4<f32>,
    invViewProj : mat4x4<f32>,
    invView : mat4x4<f32>,
    terrainSize : vec2<f32>,
    invTerrainSize : vec2<f32>,
    metrics : vec4<f32>,
    cameraPos : vec4<f32>,
    invProjParams : vec4<f32>,
    lightDirVS : vec4<f32>,
    frustumPlanes : array<vec4<f32>, 6>,
    lightDirWS : vec4<f32>,
    waterParams : vec4<f32>,
    waterColorA : vec4<f32>,
    waterColorB : vec4<f32>,
    waterMotion : vec4<f32>,
    lightingColor : vec4<f32>,
    ambientExposure : vec4<f32>,
    fogColor : vec4<f32>,
    waterOptics : vec4<f32>,
    waterFoam : vec4<f32>,
    waterSpectrum : vec4<f32>,
};
struct EffectsUniforms { camera: CameraUniforms, sceneOrigin: vec4<f32>, };
struct Instance { positionKind:vec4<f32>, velocityAge:vec4<f32>, sizeLife:vec4<f32>, color:vec4<f32>, };
@group(0) @binding(0) var<uniform> frame: EffectsUniforms;
@group(0) @binding(1) var<storage,read> instances:array<Instance>;
@group(0) @binding(2) var visibleDepth:texture_2d<f32>;
@group(0) @binding(3) var displacementTexture:texture_2d_array<f32>;
@group(0) @binding(4) var displacementSampler:sampler;
const TAU:f32=6.283185307179586;
const WATER_RESOLUTION:f32=256.0;
struct LongWave {
    displacement : vec3<f32>,
    normalVector : vec3<f32>,
};

fn cascadeUv(position : vec2<f32>, scale : f32) -> vec2<f32> {
    return position / scale + vec2<f32>(0.5 + 0.5 / WATER_RESOLUTION);
}

fn sampleDisplacement(position : vec2<f32>, scale : f32,
                      layer : i32) -> vec4<f32> {
    return textureSampleLevel(displacementTexture, displacementSampler,
                              cascadeUv(position, scale), layer, 0.0);
}

fn oneLongWave(position : vec2<f32>, wave : vec4<f32>,
               dynamics : vec3<f32>, strength : f32) -> LongWave {
    let waveNumber = TAU / (wave.w + 1.0e-4);
    let phase = waveNumber * dot(wave.xy, position) -
                dynamics.z * frame.camera.waterMotion.x + dynamics.y;
    let sine = sin(phase);
    let cosine = cos(phase);
    let amplitude = wave.z * strength;
    let ka = waveNumber * amplitude;
    return LongWave(
        vec3<f32>(-dynamics.x * amplitude * wave.x * sine,
                  amplitude * cosine,
                  -dynamics.x * amplitude * wave.y * sine),
        vec3<f32>(wave.x * ka * sine,
                  -dynamics.x * ka * cosine,
                  wave.y * ka * sine));
}

fn longWaves(position : vec2<f32>, strength : f32) -> LongWave {
    let a = oneLongWave(
        position, vec4<f32>(0.923059017, 0.384658357, 5.1541, 440.298507),
        vec3<f32>(1.0, 0.000000000, 0.374291312), strength);
    let b = oneLongWave(
        position, vec4<f32>(0.700400636, 0.713749921, 5.1541, 701.258144),
        vec3<f32>(1.0, 5.553108549, 0.296825282), strength);
    let c = oneLongWave(
        position, vec4<f32>(0.367164395, 0.930156066, 5.1541, 1116.885424),
        vec3<f32>(1.0, 4.823031791, 0.234699061), strength);
    let d = oneLongWave(
        position, vec4<f32>(-0.024039031, 0.999711021, 5.1541, 1778.85),
        vec3<f32>(1.0, 4.092955033, 0.186378666), strength);
    return LongWave(
        a.displacement + b.displacement + c.displacement + d.displacement,
        vec3<f32>(0.0, 1.0, 0.0) +
            a.normalVector + b.normalVector + c.normalVector + d.normalVector);
}

fn displacement(base:vec2<f32>)->vec3<f32> {
    let strength=clamp(frame.camera.waterParams.z,0.0,2.0);
    return (sampleDisplacement(base,frame.camera.waterSpectrum.x,0).xyz
        +sampleDisplacement(base,frame.camera.waterSpectrum.y,1).xyz)*strength
        +longWaves(base,strength).displacement;
}
fn surfaceAt(world:vec2<f32>)->vec3<f32> {
    // Bounded inverse horizontal displacement. Height follows the live GPU
    // field; the CPU's 24-mode estimate only decides cosmetic emission.
    var base=world;
    for(var i=0u;i<3u;i+=1u){base=world-displacement(base).xz;}
    let d=displacement(base);
    return vec3<f32>(base.x,frame.camera.waterParams.x,base.y)+d;
}
fn acesFilmic(inputColor : vec3<f32>) -> vec3<f32> {
    let inputMatrix = mat3x3<f32>(
        vec3<f32>(0.59719, 0.07600, 0.02840),
        vec3<f32>(0.35458, 0.90834, 0.13383),
        vec3<f32>(0.04823, 0.01566, 0.83777));
    let outputMatrix = mat3x3<f32>(
        vec3<f32>(1.60475, -0.10208, -0.00327),
        vec3<f32>(-0.53108, 1.10813, -0.07276),
        vec3<f32>(-0.07367, -0.00605, 1.07602));
    let color = inputMatrix * (inputColor / 0.6);
    let a = color * (color + vec3<f32>(0.0245786)) -
            vec3<f32>(0.000090537);
    let b = color * ((color + vec3<f32>(0.432951)) * 0.983729) +
            vec3<f32>(0.238081);
    return clamp(outputMatrix * (a / b), vec3<f32>(0.0), vec3<f32>(1.0));
}

fn linearToSrgb(linear : vec3<f32>) -> vec3<f32> {
    let low = linear * 12.92;
    let high = 1.055 * pow(max(linear, vec3<f32>(0.0)),
                           vec3<f32>(1.0 / 2.4)) - vec3<f32>(0.055);
    return select(high, low, linear <= vec3<f32>(0.0031308));
}

fn presentColor(colorInput:vec3<f32>)->vec3<f32> {
    // Cove's optical finish omits grain/vignette in both water and opaque.
    return linearToSrgb(acesFilmic(colorInput*frame.camera.ambientExposure.w));
}

struct Vertex {
    @builtin(position) position:vec4<f32>,
    @location(0) uv:vec2<f32>,
    @location(1) world:vec3<f32>,
    @location(2) color:vec4<f32>,
    @location(3) @interpolate(flat) kind:u32,
    @location(4) @interpolate(flat) age:f32,
};
fn corner(index:u32)->vec2<f32> {
    switch index {
        case 0u:{return vec2<f32>(-.5,-.5);}
        case 1u:{return vec2<f32>(.5,-.5);}
        case 2u:{return vec2<f32>(.5,.5);}
        case 3u:{return vec2<f32>(-.5,-.5);}
        case 4u:{return vec2<f32>(.5,.5);}
        default:{return vec2<f32>(-.5,.5);}
    }
}
@vertex
fn vs(@builtin(vertex_index) vertex:u32,@builtin(instance_index) instance:u32)->Vertex {
    let source=instances[instance];let kind=u32(source.positionKind.w);
    let age=clamp(source.velocityAge.w/source.sizeLife.z,0.0,1.0);
    let c=corner(vertex);let dimensions=source.sizeLife.xy*(1.0+age*select(.3,1.1,kind<2u));
    let center=source.positionKind.xyz+frame.sceneOrigin.xyz;
    var world=center;
    if(kind<2u){
        let angle=source.sizeLife.w;let cosine=cos(angle);let sine=sin(angle);
        let local=c*dimensions;
        let offset=vec2<f32>(cosine*local.x+sine*local.y,-sine*local.x+cosine*local.y);
        world=surfaceAt(center.xz+offset)+vec3<f32>(0,.025,0);
    }else{
        let right=frame.camera.invView[0].xyz;let up=frame.camera.invView[1].xyz;
        world+=right*c.x*dimensions.x+up*c.y*dimensions.y;
    }
    var output:Vertex;output.position=frame.camera.viewProj*vec4<f32>(world,1);
    output.uv=c+vec2<f32>(.5);output.world=world;output.color=source.color;
    output.kind=kind;output.age=age;return output;
}
fn hash(p:vec2<f32>)->f32 {return fract(sin(dot(p,vec2<f32>(127.1,311.7)))*43758.5453);}
@fragment
fn fs(input:Vertex)->@location(0) vec4<f32> {
    let dimensions=textureDimensions(visibleDepth);
    let pixel=clamp(vec2<i32>(input.position.xy),vec2<i32>(0),vec2<i32>(dimensions)-vec2<i32>(1));
    let depth=textureLoad(visibleDepth,pixel,0).x;
    let distance=length(input.world-frame.camera.cameraPos.xyz);
    // Immutable visible water/opaque depth, never the current color target.
    if(depth>0.0&&distance>depth+.025){discard;}
    if(input.kind>=2u&&input.world.y<surfaceAt(input.world.xz).y-.02){discard;}
    let p=(input.uv-vec2<f32>(.5))*2.0;
    let edge=1.0-smoothstep(.5,1.0,dot(p,p));
    var shape=edge;
    if(input.kind<2u){
        // Small broken cells read as foam instead of solid white rectangles.
        let cells=input.uv*vec2<f32>(10,7);
        let bubble=length(fract(cells)-vec2<f32>(.5));
        let porous=mix(.27,1.0,smoothstep(.24,.40,bubble));
        shape*=porous*(.7+.3*hash(floor(cells)));
    }
    let soft=select(1.0,clamp((depth-distance+.04)/.12,0.0,1.0),depth>0.0&&input.kind>=2u);
    let alpha=input.color.a*shape*(1.0-input.age)*(1.0-input.age)*soft;
    if(alpha<.003){discard;}
    let normal=select(normalize(frame.camera.cameraPos.xyz-input.world),vec3<f32>(0,1,0),input.kind<2u);
    let lighting=frame.camera.ambientExposure.xyz+frame.camera.lightingColor.rgb*frame.camera.lightingColor.w
        *max(0.0,dot(normal,normalize(frame.camera.lightDirWS.xyz)));
    let fog=1.0-exp(-max(frame.camera.metrics.w,0.0)*distance);
    var radiance=mix(input.color.rgb*lighting,frame.camera.fogColor.rgb,fog);
    if(frame.camera.waterMotion.z>.5){
        // Bounded homogeneous underwater attenuation. Cross-surface refraction
        // remains the water compositor's responsibility, not an overlay claim.
        let transmission=exp(-vec3<f32>(.09,.045,.025)*distance*frame.camera.waterOptics.z);
        radiance=radiance*transmission+frame.camera.waterColorB.rgb*(vec3<f32>(1)-transmission);
    }
    return vec4<f32>(presentColor(radiance)*alpha,alpha);
}
