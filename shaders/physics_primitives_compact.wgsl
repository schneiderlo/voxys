struct PrimitiveUniforms {
    viewProj : mat4x4<f32>,
    cameraPos : vec4<f32>,
    lightDirAndRayDepth : vec4<f32>,
    viewport : vec4<f32>,
};

struct BodyPose {
    position_invMass : vec4<f32>,
    orientation : vec4<f32>,
};

struct BodyShape {
    dimensions_type : vec4<f32>,
    invInertia_material : vec4<f32>,
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

@fragment
fn fs(input : VSOut) -> @location(0) vec4<f32> {
    if (uniforms.lightDirAndRayDepth.w > 0.5) {
        let dims = textureDimensions(rayDepth, 0);
        let pixel = clamp(vec2<i32>(input.position.xy), vec2<i32>(0),
                          vec2<i32>(dims) - vec2<i32>(1));
        let terrainDistance = textureLoad(rayDepth, pixel, 0).x;
        let objectDistance = length(input.worldPosition - uniforms.cameraPos.xyz);
        if (terrainDistance >= 0.0 && objectDistance > terrainDistance + 0.02) {
            discard;
        }
    }

    let lightDir = normalize(uniforms.lightDirAndRayDepth.xyz);
    let diffuse = max(dot(normalize(input.worldNormal), lightDir), 0.0);
    let viewDir = normalize(uniforms.cameraPos.xyz - input.worldPosition);
    let halfVector = normalize(lightDir + viewDir);
    let specular = pow(max(dot(normalize(input.worldNormal), halfVector), 0.0), 32.0);
    let lit = input.color * (0.22 + 0.78 * diffuse) + vec3<f32>(0.22) * specular;
    return vec4<f32>(lit, 1.0);
}
