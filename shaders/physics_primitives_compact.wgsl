// BEGIN GENERATED LEGO SURFACE
// Canonical LEGO geometry. Generated into standalone shader modules by
// scripts/sync_lego_surface.py; no runtime shader preprocessor is required.
fn legoPlateCount(heightScale: f32, cellScale: f32) -> u32 {
    return u32(clamp(floor(2.0 * heightScale / (0.32 * cellScale) + 0.5), 1.0, 65535.0));
}
fn legoPlateLevel(raw: u32, count: u32) -> u32 {
    return (raw * count + 32767u) / 65535u;
}
fn legoPlateTop(raw: u32, heightScale: f32, cellScale: f32) -> f32 {
    let count = legoPlateCount(heightScale, cellScale);
    return -heightScale + f32(legoPlateLevel(raw, count)) * (2.0 * heightScale / f32(count));
}
struct LegoContact {
    normal: vec3<f32>, distance: f32,
    point: vec3<f32>, feature: u32,
};
fn legoConsiderContact(best: LegoContact, center: vec3<f32>, point: vec3<f32>,
                       interior: f32, radius: f32, feature: u32) -> LegoContact {
    let delta = center - point;
    let len = length(delta);
    let distance = select(len, interior, interior < 0.0) - radius;
    if (distance >= best.distance) { return best; }
    let normal = select(delta / max(len, 1e-7), vec3<f32>(0.0,1.0,0.0),
                         interior < 0.0 || len < 1e-7);
    return LegoContact(normal, distance, point, feature);
}
// Exterior distance to the union, with a one-cell halo around the footprint.
// The columns are solid downwards. Decorative brick seams have no collision.
fn legoSphereContact(field: texture_2d<u32>, params: vec4<f32>, size: vec2<u32>,
                      center: vec3<f32>, radius: f32) -> LegoContact {
    var best = LegoContact(vec3<f32>(0.0,1.0,0.0), 1e30, center, 0u);
    let cellScale = params.z;
    let p = (center.xz + params.xy) / cellScale;
    let reach = (radius + cellScale) / cellScale;
    let minimum = max(vec2<i32>(0), vec2<i32>(floor(p - vec2<f32>(reach))));
    let maximum = min(vec2<i32>(size) - vec2<i32>(2), vec2<i32>(floor(p + vec2<f32>(reach))));
    for (var z = minimum.y; z <= maximum.y; z += 1) {
        for (var x = minimum.x; x <= maximum.x; x += 1) {
            let low = vec2<f32>(f32(x), f32(z)) * cellScale - params.xy;
            let high = low + vec2<f32>(cellScale);
            let top = legoPlateTop(textureLoad(field, vec2<i32>(x,z), 0).x, params.w, cellScale);
            let closest = clamp(center.xz, low, high);
            let inside = all(center.xz >= low) && all(center.xz <= high) && center.y < top;
            let point = vec3<f32>(closest.x, select(min(center.y,top),top,inside), closest.y);
            let feature = (u32(z) * size.x + u32(x)) * 8u;
            best = legoConsiderContact(best, center, point, select(0.0,center.y-top,inside), radius, feature+1u);
            let studCenter = low + vec2<f32>(0.5 * cellScale);
            let d = center.xz - studCenter;
            let len = length(d);
            let r = 0.30 * cellScale;
            let cap = top + 0.18 * cellScale;
            let radial = studCenter + d * min(1.0, r / max(len,1e-7));
            let insideStud = len < r && center.y >= top && center.y < cap;
            let studPoint = vec3<f32>(radial.x, select(clamp(center.y,top,cap),cap,insideStud), radial.y);
            best = legoConsiderContact(best, center, studPoint, select(0.0,center.y-cap,insideStud), radius, feature+2u);
        }
    }
    return best;
}

// Dynamic bricks use material tag B; tag A remains the existing plastic PBR
// material. Dimensions bound the complete body, including the stud caps.
fn legoIsBrick(material: u32) -> bool { return (material & 0xf0000000u) == 0xb0000000u; }
fn legoBrickParts(dimensions: vec3<f32>, material: u32) -> u32 {
    if (!legoIsBrick(material)) { return 1u; }
    return 1u + min(u32(round(dimensions.x))*u32(round(dimensions.z)),8u);
}
fn legoBrickPartSize(dimensions: vec3<f32>, part: u32) -> vec3<f32> {
    if (part == 0u) { return vec3<f32>(dimensions.x,dimensions.y-0.18,dimensions.z); }
    return vec3<f32>(0.6,0.18,0.6);
}
fn legoBrickPartOffset(dimensions: vec3<f32>, part: u32) -> vec3<f32> {
    if (part == 0u) { return vec3<f32>(0.0,-0.09,0.0); }
    let width=max(u32(round(dimensions.x)),1u);
    return vec3<f32>(f32((part-1u)%width)+0.5-f32(width)*0.5,
        dimensions.y*0.5-0.09,f32((part-1u)/width)+0.5-f32(u32(round(dimensions.z)))*0.5);
}
// END GENERATED LEGO SURFACE

struct PrimitiveUniforms {
    viewProj : mat4x4<f32>,
    cameraPos : vec4<f32>,
    lightDirAndRayDepth : vec4<f32>,
    viewport : vec4<f32>,
    lightingColor : vec4<f32>,
    ambientColor : vec4<f32>,
    fogColorExposure : vec4<f32>,
};

struct BodyPose {
    position_invMass : vec4<f32>,
    orientation : vec4<f32>,
};

struct BodyShape {
    dimensions_type : vec4<f32>,
    invInertia_material : vec4<f32>,
    material_coefficients : vec4<f32>,
    authored_shape : vec4<u32>,
};

@group(0) @binding(0) var<uniform> uniforms : PrimitiveUniforms;
@group(0) @binding(1) var<storage, read> poses : array<BodyPose>;
@group(0) @binding(2) var<storage, read> shapes : array<BodyShape>;
@group(0) @binding(3) var<storage, read> visibleBodyIds : array<u32>;
@group(0) @binding(4) var rayDepth : texture_2d<f32>;

struct VSIn {
    @location(0) position : vec3<f32>,
    @location(1) normal : vec3<f32>,
    @location(2) part : f32,
    @builtin(instance_index) instanceIndex : u32,
};

struct VSOut {
    @builtin(position) position : vec4<f32>,
    @location(0) worldPosition : vec3<f32>,
    @location(1) worldNormal : vec3<f32>,
    @location(2) color : vec3<f32>,
    @location(3) material : vec3<f32>,
    @location(4) localPosition : vec3<f32>,
    @location(5) @interpolate(flat) bevelShape : vec4<f32>,
    @location(6) @interpolate(flat) orientation : vec4<f32>,
};

struct SurfaceMaterial {
    baseColor : vec3<f32>,
    roughness : f32,
    metallic : f32,
    clearcoat : f32,
};

fn rotate_by_quaternion(qInput : vec4<f32>, value : vec3<f32>) -> vec3<f32> {
    let qLengthSquared = dot(qInput, qInput);
    let q = select(vec4<f32>(0.0, 0.0, 0.0, 1.0),
                   qInput * inverseSqrt(qLengthSquared),
                   qLengthSquared > 1e-12);
    let twiceCross = 2.0 * cross(q.xyz, value);
    return value + q.w * twiceCross + cross(q.xyz, twiceCross);
}

fn fallback_material(shape : u32) -> SurfaceMaterial {
    var color = vec3<f32>(1.0);
    switch shape {
        case 0u: { color = vec3<f32>(0.95, 0.28, 0.18); }
        case 1u: { color = vec3<f32>(0.20, 0.62, 0.95); }
        case 2u: { color = vec3<f32>(0.96, 0.70, 0.16); }
        case 3u: { color = vec3<f32>(0.42, 0.85, 0.36); }
        case 4u: { color = vec3<f32>(0.68, 0.38, 0.92); }
        default: {}
    }
    return SurfaceMaterial(color, 0.48, 0.0, 0.0);
}

fn decode_material(shape : BodyShape, shapeIndex : u32) -> SurfaceMaterial {
    let packed = bitcast<u32>(shape.invInertia_material.w);
    if ((packed & 0xf0000000u) != 0xa0000000u && !legoIsBrick(packed)) {
        return fallback_material(shapeIndex);
    }
    let color = vec3<f32>(
        f32(packed & 31u) / 31.0,
        f32((packed >> 5u) & 63u) / 63.0,
        f32((packed >> 11u) & 31u) / 31.0);
    return SurfaceMaterial(
        color,
        max(f32((packed >> 16u) & 63u) / 63.0, 0.045),
        f32((packed >> 22u) & 31u) / 31.0,
        f32((packed >> 27u) & 1u));
}

fn vertex_output(input:VSIn, lego:bool)->VSOut {
    let bodyId=visibleBodyIds[input.instanceIndex];let pose=poses[bodyId];let shape=shapes[bodyId];
    var dimensions=max(abs(shape.dimensions_type.xyz),vec3<f32>(1e-7));
    let packed=bitcast<u32>(shape.invInertia_material.w);
    var offset=vec3<f32>(0);var hidden=legoIsBrick(packed);
    if(lego){
        let part=u32(input.part);
        hidden=!legoIsBrick(packed)||part>=legoBrickParts(dimensions,packed);
        offset=legoBrickPartOffset(dimensions,part);
        dimensions=legoBrickPartSize(dimensions,part);
    }
    let worldPosition=pose.position_invMass.xyz+rotate_by_quaternion(pose.orientation,input.position*dimensions+offset);
    let material=decode_material(shape,u32(clamp(shape.dimensions_type.w,0.0,4.0)));
    var output:VSOut;
    output.position=uniforms.viewProj*vec4<f32>(worldPosition,1);
    if(hidden || any(shape.authored_shape != vec4<u32>(0u))){output.position=vec4<f32>(0,0,2,1);}
    output.worldPosition=worldPosition;
    output.worldNormal=normalize(rotate_by_quaternion(pose.orientation,input.normal/dimensions));
    output.color=material.baseColor;output.material=vec3<f32>(material.roughness,material.metallic,material.clearcoat);
    output.localPosition=input.position*dimensions;
    output.bevelShape=vec4<f32>(dimensions,select(0.0,1.0,lego));
    if(lego && input.part>0.0){output.bevelShape.w=2.0;}
    output.orientation=pose.orientation;
    return output;
}
@vertex fn vs(input:VSIn)->VSOut{return vertex_output(input,false);}
@vertex fn vs_lego(input:VSIn)->VSOut{return vertex_output(input,true);}

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

fn distributionGgx(noh : f32, roughness : f32) -> f32 {
    let alpha = roughness * roughness;
    let alphaSquared = alpha * alpha;
    let denominator = noh * noh * (alphaSquared - 1.0) + 1.0;
    return alphaSquared /
        max(3.14159265359 * denominator * denominator, 1e-6);
}

fn visibilitySmithGgxCorrelated(
    nov : f32, nol : f32, roughness : f32) -> f32 {
    let alpha = roughness * roughness;
    let alphaSquared = alpha * alpha;
    let lambdaV = nol * sqrt(max(
        nov * nov * (1.0 - alphaSquared) + alphaSquared, 1e-7));
    let lambdaL = nov * sqrt(max(
        nol * nol * (1.0 - alphaSquared) + alphaSquared, 1e-7));
    return 0.5 / max(lambdaV + lambdaL, 1e-6);
}

fn fresnelSchlick(f0 : vec3<f32>, voh : f32) -> vec3<f32> {
    let factor = pow(clamp(1.0 - voh, 0.0, 1.0), 5.0);
    return f0 + (vec3<f32>(1.0) - f0) * factor;
}

fn plastic_normal(input:VSOut)->vec3<f32> {
    let pixel=max(length(dpdx(input.localPosition)),length(dpdy(input.localPosition)));
    let visibility=1.0-smoothstep(.01,.05,pixel);
    if(input.bevelShape.w==0.0){return normalize(input.worldNormal);}
    let half=input.bevelShape.xyz*.5;
    var rounded=sign(input.localPosition)*max(abs(input.localPosition)-half+vec3<f32>(.02),vec3<f32>(0));
    if(input.bevelShape.w>1.5){
        let r=max(length(input.localPosition.xz),.0001);
        let band=max(vec2<f32>(r,abs(input.localPosition.y))-vec2<f32>(half.x,half.y)+.012,vec2<f32>(0));
        rounded=vec3<f32>(input.localPosition.x/r*band.x,sign(input.localPosition.y)*band.y,input.localPosition.z/r*band.x);
    }
    let n=rotate_by_quaternion(input.orientation,normalize(rounded+vec3<f32>(0,1e-8,0)));
    return normalize(mix(input.worldNormal,n,visibility));
}

@fragment
fn fs(input : VSOut) -> @location(0) vec4<f32> {
    let cameraDelta = uniforms.cameraPos.xyz - input.worldPosition;
    let distanceToCamera = length(cameraDelta);
    if (uniforms.lightDirAndRayDepth.w > 0.5) {
        let dims = textureDimensions(rayDepth, 0);
        let pixel = clamp(vec2<i32>(input.position.xy), vec2<i32>(0),
                          vec2<i32>(dims) - vec2<i32>(1));
        let terrainDistance = textureLoad(rayDepth, pixel, 0).x;
        if (terrainDistance >= 0.0
            && distanceToCamera > terrainDistance + 0.02) {
            discard;
        }
    }

    let normal = plastic_normal(input);
    let lightDir = uniforms.lightDirAndRayDepth.xyz;
    let viewDir = cameraDelta / max(distanceToCamera, 1e-20);
    let halfVector = normalize(lightDir + viewDir);
    let nol = max(dot(normal, lightDir), 0.0);
    let nov = max(dot(normal, viewDir), 1e-4);
    let noh = max(dot(normal, halfVector), 0.0);
    let voh = max(dot(viewDir, halfVector), 0.0);
    let roughness = clamp(input.material.x, 0.045, 1.0);
    let metallic = clamp(input.material.y, 0.0, 1.0);
    let f0 = mix(vec3<f32>(0.04), input.color, metallic);
    let fresnel = fresnelSchlick(f0, voh);
    let distribution = distributionGgx(noh, roughness);
    let visibility =
        visibilitySmithGgxCorrelated(nov, nol, roughness);
    let specular = fresnel * distribution * visibility;
    let diffuse = (vec3<f32>(1.0) - fresnel)
        * (1.0 - metallic) * input.color * (1.0 / 3.14159265359);

    let coatRoughness = mix(0.18, 0.10, input.material.z);
    let coatFresnel = fresnelSchlick(vec3<f32>(0.04), voh);
    let clearcoat = input.material.z * coatFresnel
        * distributionGgx(noh, coatRoughness)
        * visibilitySmithGgxCorrelated(nov, nol, coatRoughness) * 0.25;
    let ambientMaximum = max(max(uniforms.ambientColor.r,
                                 uniforms.ambientColor.g),
                             max(uniforms.ambientColor.b, 0.001));
    let ambientTint = uniforms.ambientColor.rgb / ambientMaximum;
    let sunRadiance = uniforms.lightingColor.rgb * uniforms.lightingColor.w;
    let ambient = input.color * ambientTint
        * max(uniforms.ambientColor.w, 0.05)
        * mix(1.0, 0.35, metallic);
    var lit = ambient + sunRadiance * nol
        * (diffuse + specular + clearcoat) * 3.14159265359;
    let maximumFog = select(0.7, 1.0,
                            uniforms.lightDirAndRayDepth.w > 0.5);
    let fog = clamp(1.0 - exp(-max(uniforms.viewport.z, 0.0) *
                              distanceToCamera), 0.0, maximumFog);
    lit = mix(lit, uniforms.fogColorExposure.rgb, fog);
    var presented = lit * max(uniforms.fogColorExposure.w, 0.0);
    if (uniforms.lightDirAndRayDepth.w > 0.5) {
        presented = linearToSrgb(acesFilmic(presented));
    }
    return vec4<f32>(presented, 1.0);
}
