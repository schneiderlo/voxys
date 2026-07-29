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
};

@group(0) @binding(0) var<uniform> uniforms : PrimitiveUniforms;
@group(0) @binding(1) var<storage, read> poses : array<BodyPose>;
@group(0) @binding(2) var<storage, read> shapes : array<BodyShape>;
@group(0) @binding(3) var<storage, read> visibleBodyIds : array<u32>;
@group(0) @binding(4) var rayDepth : texture_2d<f32>;

struct VSIn {
    @location(0) position : vec3<f32>,
    @location(1) normal : vec3<f32>,
    @builtin(instance_index) instanceIndex : u32,
};

struct VSOut {
    @builtin(position) position : vec4<f32>,
    @location(0) worldPosition : vec3<f32>,
    @location(1) worldNormal : vec3<f32>,
    @location(2) color : vec3<f32>,
};

fn rotate_by_quaternion(qInput : vec4<f32>, value : vec3<f32>) -> vec3<f32> {
    let qLengthSquared = dot(qInput, qInput);
    let q = select(vec4<f32>(0.0, 0.0, 0.0, 1.0),
                   qInput * inverseSqrt(qLengthSquared),
                   qLengthSquared > 1e-12);
    let twiceCross = 2.0 * cross(q.xyz, value);
    return value + q.w * twiceCross + cross(q.xyz, twiceCross);
}

fn shape_color(shape : u32) -> vec3<f32> {
    switch shape {
        case 0u: { return vec3<f32>(0.95, 0.28, 0.18); }
        case 1u: { return vec3<f32>(0.20, 0.62, 0.95); }
        case 2u: { return vec3<f32>(0.96, 0.70, 0.16); }
        case 3u: { return vec3<f32>(0.42, 0.85, 0.36); }
        case 4u: { return vec3<f32>(0.68, 0.38, 0.92); }
        default: { return vec3<f32>(1.0); }
    }
}

@vertex
fn vs(input : VSIn) -> VSOut {
    let bodyId = visibleBodyIds[input.instanceIndex];
    let pose = poses[bodyId];
    let shape = shapes[bodyId];
    let dimensions = max(abs(shape.dimensions_type.xyz), vec3<f32>(1e-7));
    let worldPosition = pose.position_invMass.xyz
        + rotate_by_quaternion(pose.orientation, input.position * dimensions);
    let worldNormal = normalize(rotate_by_quaternion(
        pose.orientation, input.normal / dimensions));
    let shapeIndex = u32(clamp(shape.dimensions_type.w, 0.0, 4.0));

    var output : VSOut;
    output.position = uniforms.viewProj * vec4<f32>(worldPosition, 1.0);
    output.worldPosition = worldPosition;
    output.worldNormal = worldNormal;
    output.color = shape_color(shapeIndex);
    return output;
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

    let normal = normalize(input.worldNormal);
    let lightDir = uniforms.lightDirAndRayDepth.xyz;
    let diffuse = max(dot(normal, lightDir), 0.0);
    let viewDir = cameraDelta / max(distanceToCamera, 1e-20);
    let halfVector = normalize(lightDir + viewDir);
    let specular = pow(max(dot(normal, halfVector), 0.0), 32.0);
    let ambientMaximum = max(max(uniforms.ambientColor.r,
                                 uniforms.ambientColor.g),
                             max(uniforms.ambientColor.b, 0.001));
    let ambientTint = uniforms.ambientColor.rgb / ambientMaximum;
    let sunRadiance = uniforms.lightingColor.rgb * uniforms.lightingColor.w;
    let illumination = ambientTint * max(uniforms.ambientColor.w, 0.05) +
                       sunRadiance * diffuse;
    var lit = input.color * illumination + sunRadiance * (0.22 * specular);
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
