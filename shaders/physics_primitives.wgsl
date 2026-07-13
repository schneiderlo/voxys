struct PrimitiveUniforms {
    viewProj : mat4x4<f32>,
    cameraPos : vec4<f32>,
    lightDirAndRayDepth : vec4<f32>,
    viewport : vec4<f32>,
};

struct InstanceData {
    model : mat4x4<f32>,
    color : vec4<f32>,
};

@group(0) @binding(0) var<uniform> uniforms : PrimitiveUniforms;
@group(0) @binding(1) var<storage, read> instances : array<InstanceData>;
@group(0) @binding(2) var rayDepth : texture_2d<f32>;

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

@vertex
fn vs(input : VSIn) -> VSOut {
    let instance = instances[input.instanceIndex];
    let worldPosition = instance.model * vec4<f32>(input.position, 1.0);

    var output : VSOut;
    output.position = uniforms.viewProj * worldPosition;
    output.worldPosition = worldPosition.xyz;
    output.worldNormal = normalize((instance.model * vec4<f32>(input.normal, 0.0)).xyz);
    output.color = instance.color.rgb;
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
