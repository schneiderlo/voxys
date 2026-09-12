// BEGIN GENERATED SCENE SUN SHADOW
struct SunShadowUniforms {
    viewProj: mat4x4<f32>,
    // enabled, world metres per texel, inverse depth range, reserved
    params: vec4<f32>,
    // Absolute origin of the camera-sector frame used by mesh casters.
    worldOrigin: vec4<f32>,
};
@group(2) @binding(0) var<uniform> sunShadow: SunShadowUniforms;
@group(2) @binding(1) var sunDepth: texture_depth_2d;
@group(2) @binding(2) var sunSampler: sampler_comparison;

fn sunVisibility(position: vec3<f32>, geometricNormal: vec3<f32>, light: vec3<f32>) -> f32 {
    if (sunShadow.params.x < 0.5) { return 1.0; }
    // Use geometric normals for bias: normal-map grain must not move shadows.
    let slope = 1.0 - abs(dot(geometricNormal, light));
    let biased = position + geometricNormal * sunShadow.params.y * (0.2 + 0.65 * slope);
    let clip = sunShadow.viewProj * vec4<f32>(biased, 1);
    let uv = clip.xy * vec2<f32>(0.5, -0.5) + vec2<f32>(0.5);
    if (any(uv <= vec2<f32>(0)) || any(uv >= vec2<f32>(1)) || clip.z <= 0.0 || clip.z >= 1.0) {
        return 1.0;
    }
    let texel = 1.0 / vec2<f32>(textureDimensions(sunDepth));
    let reference = clip.z - 0.003 * sunShadow.params.z;
    var visibility = 0.0;
    for (var y = -1; y <= 1; y += 1) {
        for (var x = -1; x <= 1; x += 1) {
            visibility += textureSampleCompareLevel(sunDepth, sunSampler,
                uv + vec2<f32>(f32(x), f32(y)) * texel, reference);
        }
    }
    // A local map fades at its border instead of following the camera as a hard edge.
    let edge = max(abs(clip.x), abs(clip.y));
    let fade = smoothstep(0.80, 0.98, edge);
    return mix(visibility / 9.0, 1.0, fade);
}

fn sceneSunVisibility(worldPosition: vec3<f32>, geometricNormal: vec3<f32>, light: vec3<f32>) -> f32 {
    return sunVisibility(worldPosition - sunShadow.worldOrigin.xyz, geometricNormal, light);
}
// END GENERATED SCENE SUN SHADOW

// BEGIN GENERATED AUTHORED GEOMETRY
// Authored shape heap format 1. The including pipeline declares
// var<storage, read> authored_shape_heap: array<vec4<u32>> at its chosen binding.
// One binding holds the header, seven-row descriptors, three-row cells/faces
// and two-row preorder BVH nodes. All geometric coordinates are root-local
// integer ticks at .02 m. Every index inside a shape is local to that resource.

struct AuthoredShapeView {
    valid: bool,
    cell_base: u32,
    cell_count: u32,
    face_base: u32,
    face_count: u32,
    node_base: u32,
    node_count: u32,
    center_inverse_mass: vec4<f32>,
    root_from_body: vec4<f32>,
    inverse_inertia_radius: vec4<f32>,
    minimum: vec3<f32>,
    maximum: vec3<f32>,
};

fn authored_shape(index: u32, generation: u32) -> AuthoredShapeView {
    var result: AuthoredShapeView;
    let rows = arrayLength(&authored_shape_heap);
    if (rows < 2u || index == 0u || generation == 0u) { return result; }
    let sections = authored_shape_heap[0];
    let capacity = authored_shape_heap[1];
    if (sections.x != 1u || sections.y == 0u || sections.y > 512u || index > sections.y) { return result; }
    if (sections.z != 2u + 7u * (sections.y + 1u) || sections.w < sections.z || capacity.x < sections.w
        || capacity.y < capacity.x || capacity.y > rows) { return result; }
    if ((sections.w-sections.z)%3u != 0u || (capacity.x-sections.w)%3u != 0u || (capacity.y-capacity.x)%2u != 0u) { return result; }
    let cell_capacity = (sections.w-sections.z)/3u;
    let face_capacity = (capacity.x-sections.w)/3u;
    let node_capacity = (capacity.y-capacity.x)/2u;
    if (capacity.z != cell_capacity || capacity.w != face_capacity) { return result; }
    let descriptor = 2u + 7u*index;
    // sections.z bounds the descriptor area, and the area is inside the binding.
    let identity = authored_shape_heap[descriptor];
    let ranges = authored_shape_heap[descriptor+1u];
    if (identity.x != generation || identity.y != 1u || identity.w == 0u
        || ranges.y == 0u || ranges.w == 0u) { return result; }
    if (identity.z > cell_capacity || identity.w > cell_capacity-identity.z
        || ranges.x > face_capacity || ranges.y > face_capacity-ranges.x
        || ranges.z > node_capacity || ranges.w > node_capacity-ranges.z) { return result; }
    result.cell_base = sections.z + 3u*identity.z;
    result.cell_count = identity.w;
    result.face_base = sections.w + 3u*ranges.x;
    result.face_count = ranges.y;
    result.node_base = capacity.x + 2u*ranges.z;
    result.node_count = ranges.w;
    result.center_inverse_mass = bitcast<vec4<f32>>(authored_shape_heap[descriptor+2u]);
    result.root_from_body = bitcast<vec4<f32>>(authored_shape_heap[descriptor+3u]);
    result.inverse_inertia_radius = bitcast<vec4<f32>>(authored_shape_heap[descriptor+4u]);
    result.minimum = vec3<f32>(bitcast<vec4<i32>>(authored_shape_heap[descriptor+5u]).xyz)*0.02;
    result.maximum = vec3<f32>(bitcast<vec4<i32>>(authored_shape_heap[descriptor+6u]).xyz)*0.02;
    result.valid = true;
    return result;
}

struct AuthoredShapeCell {
    valid: bool,
    minimum: vec3<f32>,
    maximum: vec3<f32>,
    source: u32,
    first_face: u32,
    face_count: u32,
};

fn authored_shape_ref(reference: vec4<u32>) -> AuthoredShapeView {
    if (reference.z != 1u || reference.w != 0u) {
        var invalid: AuthoredShapeView;
        return invalid;
    }
    return authored_shape(reference.x, reference.y);
}
fn authored_cell(shape: AuthoredShapeView, index: u32) -> AuthoredShapeCell {
    var result: AuthoredShapeCell;
    if (!shape.valid || index >= shape.cell_count) { return result; }
    let row = shape.cell_base + 3u*index;
    let lower = authored_shape_heap[row];
    let upper = authored_shape_heap[row+1u];
    let detail = authored_shape_heap[row+2u];
    if (upper.w > shape.face_count || detail.x > shape.face_count-upper.w || lower.w == 0u) { return result; }
    result.minimum = vec3<f32>(bitcast<vec3<i32>>(lower.xyz))*0.02;
    result.maximum = vec3<f32>(bitcast<vec3<i32>>(upper.xyz))*0.02;
    result.source = lower.w;
    result.first_face = upper.w;
    result.face_count = detail.x;
    // Zero exterior patches is valid for a completely enclosed union cell.
    result.valid = true;
    return result;
}

struct AuthoredShapeFace {
    valid: bool,
    minimum: vec3<f32>,
    maximum: vec3<f32>,
    source: u32,
    cell: u32,
    axis: u32,
    sign: i32,
};
fn authored_face(shape: AuthoredShapeView, index: u32) -> AuthoredShapeFace {
    var result: AuthoredShapeFace;
    if (!shape.valid || index >= shape.face_count) { return result; }
    let row = shape.face_base + 3u*index;
    let lower = authored_shape_heap[row];
    let upper = authored_shape_heap[row+1u];
    let detail = authored_shape_heap[row+2u];
    let sign = bitcast<i32>(detail.y);
    if (lower.w == 0u || upper.w >= shape.cell_count || detail.x > 2u || (sign != -1 && sign != 1)) { return result; }
    result.minimum = vec3<f32>(bitcast<vec3<i32>>(lower.xyz))*0.02;
    result.maximum = vec3<f32>(bitcast<vec3<i32>>(upper.xyz))*0.02;
    result.source = lower.w;
    result.cell = upper.w;
    result.axis = detail.x;
    result.sign = sign;
    result.valid = true;
    return result;
}

struct AuthoredShapeNode {
    valid: bool,
    minimum: vec3<f32>,
    maximum: vec3<f32>,
    cell: u32,
    escape: u32,
};
fn authored_node(shape: AuthoredShapeView, index: u32) -> AuthoredShapeNode {
    var result: AuthoredShapeNode;
    if (!shape.valid || index >= shape.node_count) { return result; }
    let row = shape.node_base + 2u*index;
    let lower = authored_shape_heap[row];
    let upper = authored_shape_heap[row+1u];
    if ((lower.w != 0xffffffffu && lower.w >= shape.cell_count) || upper.w <= index || upper.w > shape.node_count) { return result; }
    result.minimum = vec3<f32>(bitcast<vec3<i32>>(lower.xyz))*0.02;
    result.maximum = vec3<f32>(bitcast<vec3<i32>>(upper.xyz))*0.02;
    result.cell = lower.w;
    result.escape = upper.w;
    result.valid = true;
    return result;
}

fn authored_quat_conjugate(q: vec4<f32>) -> vec4<f32> { return vec4<f32>(-q.xyz,q.w); }
fn authored_quat_product(a: vec4<f32>, b: vec4<f32>) -> vec4<f32> {
    return vec4<f32>(a.w*b.xyz+b.w*a.xyz+cross(a.xyz,b.xyz),a.w*b.w-dot(a.xyz,b.xyz));
}
fn authored_quat_rotate(q: vec4<f32>, v: vec3<f32>) -> vec3<f32> {
    let t=2.0*cross(q.xyz,v);
    return v+q.w*t+cross(q.xyz,t);
}
fn authored_root_vector(shape: AuthoredShapeView, body_vector: vec3<f32>) -> vec3<f32> {
    return authored_quat_rotate(shape.root_from_body,body_vector);
}
fn authored_body_vector(shape: AuthoredShapeView, root_vector: vec3<f32>) -> vec3<f32> {
    return authored_quat_rotate(authored_quat_conjugate(shape.root_from_body),root_vector);
}
fn authored_root_point(shape: AuthoredShapeView, body_point: vec3<f32>) -> vec3<f32> {
    return shape.center_inverse_mass.xyz+authored_root_vector(shape,body_point);
}
fn authored_body_point(shape: AuthoredShapeView, root_point: vec3<f32>) -> vec3<f32> {
    return authored_body_vector(shape,root_point-shape.center_inverse_mass.xyz);
}
fn authored_body_orientation(shape: AuthoredShapeView, world_root_orientation: vec4<f32>) -> vec4<f32> {
    return authored_quat_product(world_root_orientation,shape.root_from_body);
}
fn authored_root_orientation(shape: AuthoredShapeView, world_body_orientation: vec4<f32>) -> vec4<f32> {
    return authored_quat_product(world_body_orientation,authored_quat_conjugate(shape.root_from_body));
}
fn authored_com_position(shape: AuthoredShapeView, root_position: vec3<f32>, root_orientation: vec4<f32>) -> vec3<f32> {
    return root_position+authored_quat_rotate(root_orientation,shape.center_inverse_mass.xyz);
}
fn authored_root_position(shape: AuthoredShapeView, com_position: vec3<f32>, body_orientation: vec4<f32>) -> vec3<f32> {
    return com_position-authored_quat_rotate(authored_root_orientation(shape,body_orientation),shape.center_inverse_mass.xyz);
}
fn authored_com_velocity(shape: AuthoredShapeView, root_velocity: vec3<f32>, world_omega: vec3<f32>, root_orientation: vec4<f32>) -> vec3<f32> {
    return root_velocity+cross(world_omega,authored_quat_rotate(root_orientation,shape.center_inverse_mass.xyz));
}
fn authored_world_inverse_inertia(shape: AuthoredShapeView, body_orientation: vec4<f32>, world_impulse: vec3<f32>) -> vec3<f32> {
    let body_impulse=authored_quat_rotate(authored_quat_conjugate(body_orientation),world_impulse);
    return authored_quat_rotate(body_orientation,body_impulse*shape.inverse_inertia_radius.xyz);
}
struct AuthoredImpulseDelta { linear: vec3<f32>, angular: vec3<f32>, };
fn authored_impulse_at_root_point(shape: AuthoredShapeView, body_orientation: vec4<f32>, world_impulse: vec3<f32>, root_point: vec3<f32>) -> AuthoredImpulseDelta {
    let world_lever=authored_quat_rotate(body_orientation,authored_body_point(shape,root_point));
    return AuthoredImpulseDelta(world_impulse*shape.center_inverse_mass.w,
        authored_world_inverse_inertia(shape,body_orientation,cross(world_lever,world_impulse)));
}
// END GENERATED AUTHORED GEOMETRY

struct MeshUniforms {
    viewProj : mat4x4<f32>,
    cameraPosition : vec4<f32>,
    lightDirectionFogDensity : vec4<f32>,
    sunColorIntensity : vec4<f32>,
    ambientColorIntensity : vec4<f32>,
    fogColorExposure : vec4<f32>,
    environmentParams : vec4<f32>,
};

struct GpuDrawInstance {
    modelMatrix : mat4x4<f32>,
    tintColor : vec4<f32>,
    emissiveBoost : f32,
    materialIndex : u32,
    padding : vec2<u32>,
};

struct GpuMaterial {
    baseColorFactor : vec4<f32>,
    emissiveFactorAlphaCutoff : vec4<f32>,
    metallicRoughnessNormalOcclusion : vec4<f32>,
    flags : vec4<u32>,
};

@group(0) @binding(0) var<uniform> uniforms : MeshUniforms;
@group(0) @binding(1) var<storage, read> instances : array<GpuDrawInstance>;
@group(0) @binding(2) var<storage, read> materials : array<GpuMaterial>;
@group(0) @binding(3) var environmentTexture : texture_2d<f32>;
@group(0) @binding(4) var environmentSampler : sampler;
@group(0) @binding(5) var rayDepth : texture_2d<f32>;
@group(0) @binding(6) var baseColorTexture : texture_2d<f32>;
@group(0) @binding(7) var normalTexture : texture_2d<f32>;
@group(0) @binding(8) var metallicRoughnessTexture : texture_2d<f32>;
@group(0) @binding(9) var emissiveTexture : texture_2d<f32>;
@group(0) @binding(10) var materialSampler : sampler;
@group(0) @binding(11) var filteredSpecular : texture_cube<f32>;
@group(0) @binding(12) var filteredDiffuse : texture_cube<f32>;
@group(0) @binding(13) var environmentBrdf : texture_2d<f32>;
@group(0) @binding(14) var filteredSampler : sampler;

struct LiveBodyPose { position_mass: vec4<f32>, orientation: vec4<f32> };
struct LiveBodyCamera { sector: vec4<i32>, local: vec4<f32> };
@group(1) @binding(0) var<storage,read> live_poses: array<LiveBodyPose>;
@group(1) @binding(1) var<storage,read> live_metadata: array<vec4<i32>>;
@group(1) @binding(2) var<storage,read> live_shapes: array<vec4<u32>>;
@group(1) @binding(3) var<storage,read> authored_shape_heap: array<vec4<u32>>;
@group(1) @binding(4) var<uniform> live_camera: LiveBodyCamera;


fn live_root_matrix(body: vec2<u32>) -> mat4x4<f32> {
    var invalid: mat4x4<f32>;
    if(body.x==0u || body.x>=arrayLength(&live_poses) || body.x>=arrayLength(&live_metadata)
        || body.x>=arrayLength(&live_shapes)/4u) { return invalid; }
    let bodyMeta=live_metadata[body.x];
    if((bitcast<u32>(bodyMeta.w)&0x000fffffu)!=body.y || (bitcast<u32>(bodyMeta.w)&0x00100000u)==0u) { return invalid; }
    let shape=authored_shape_ref(live_shapes[body.x*4u+3u]);
    if(!shape.valid) { return invalid; }
    var delta=vec3<f32>(0.0);
    for(var axis=0u;axis<3u;axis+=1u) {
        let positive=bodyMeta[axis]>=live_camera.sector[axis];
        let difference=select(bitcast<u32>(live_camera.sector[axis])-bitcast<u32>(bodyMeta[axis]),
            bitcast<u32>(bodyMeta[axis])-bitcast<u32>(live_camera.sector[axis]),positive);
        if(difference>4096u) { return invalid; }
        delta[axis]=select(-1.0,1.0,positive)*f32(difference)*256.0;
    }
    let pose=live_poses[body.x];
    let q=authored_root_orientation(shape,pose.orientation);
    let position=authored_root_position(shape,pose.position_mass.xyz,pose.orientation)+delta;
    return mat4x4<f32>(vec4<f32>(authored_quat_rotate(q,vec3<f32>(1,0,0)),0),
        vec4<f32>(authored_quat_rotate(q,vec3<f32>(0,1,0)),0),
        vec4<f32>(authored_quat_rotate(q,vec3<f32>(0,0,1)),0),vec4<f32>(position,1));
}

fn safeNormalize(value : vec3<f32>, fallback : vec3<f32>) -> vec3<f32> {
    let lengthSquared = dot(value, value);
    let normalized = value * inverseSqrt(max(lengthSquared, 1.0e-12));
    return select(fallback, normalized, lengthSquared > 1.0e-12);
}

struct VertexInput {
    @location(0) position : vec3<f32>,
    @location(1) normal : vec3<f32>,
    @location(2) tangent : vec4<f32>,
    @location(3) texCoord : vec2<f32>,
    @location(4) joints : vec4<u32>,
    @location(5) weights : vec4<f32>,
    @builtin(instance_index) instanceIndex : u32,
};

struct VertexOutput {
    @builtin(position) position : vec4<f32>,
    @location(0) worldPosition : vec3<f32>,
    @location(1) worldNormal : vec3<f32>,
    @location(2) tintColor : vec4<f32>,
    @location(3) @interpolate(flat) materialIndex : u32,
    @location(4) @interpolate(flat) emissiveBoost : f32,
    @location(5) texCoord : vec2<f32>,
    @location(6) worldTangent : vec4<f32>,
};

fn meshVertex(input : VertexInput, shadowPass: bool) -> VertexOutput {
    let instance = instances[input.instanceIndex];
    var model=instance.modelMatrix;
    if(instance.padding.x!=0u) {
        let root=live_root_matrix(instance.padding);
        if(root[3].w==0.0) { var hidden: VertexOutput; hidden.position=vec4<f32>(2,2,2,1); return hidden; }
        model=root*model;
    }
    let worldPosition = model * vec4<f32>(input.position, 1.0);

    let modelX = model[0].xyz;
    let modelY = model[1].xyz;
    let modelZ = model[2].xyz;
    let cofactorX = cross(modelY, modelZ);
    let cofactorY = cross(modelZ, modelX);
    let cofactorZ = cross(modelX, modelY);
    let determinantSign = select(-1.0, 1.0, dot(modelX, cofactorX) >= 0.0);
    let normalMatrix = mat3x3<f32>(cofactorX, cofactorY, cofactorZ);

    var output : VertexOutput;
    output.position = uniforms.viewProj * worldPosition;
    if (shadowPass) { output.position = sunShadow.viewProj * worldPosition; }
    output.worldPosition = worldPosition.xyz;
    output.worldNormal = safeNormalize(
        normalMatrix * input.normal * determinantSign, vec3<f32>(0.0, 1.0, 0.0));
    output.tintColor = instance.tintColor;
    output.materialIndex = instance.materialIndex;
    output.emissiveBoost = instance.emissiveBoost;
    output.texCoord = input.texCoord;
    let tangentDirection = safeNormalize(
        (model * vec4<f32>(input.tangent.xyz, 0.0)).xyz,
        vec3<f32>(1.0, 0.0, 0.0));
    output.worldTangent = vec4<f32>(
        tangentDirection, input.tangent.w * determinantSign);
    return output;
}

@vertex fn vs(input: VertexInput) -> VertexOutput { return meshVertex(input, false); }

// Resolve exactly the same live root, stale-generation rejection and local
// hierarchy as the color pass. No readback or delayed CPU shadow pose.
@vertex fn vsSunShadow(input: VertexInput) -> VertexOutput {
    return meshVertex(input, true);
}

@fragment fn fsSunShadow(input: VertexOutput, @builtin(front_facing) frontFacing: bool) {
    let dx = dpdx(input.texCoord);
    let dy = dpdy(input.texCoord);
    let material = materials[input.materialIndex];
    if (!frontFacing && material.flags.y == 0u) { discard; }
    if (material.flags.x == 1u) {
        var alpha = material.baseColorFactor.a * input.tintColor.a;
        if ((material.flags.w & 1u) != 0u) {
            alpha *= textureSampleGrad(baseColorTexture, materialSampler, input.texCoord, dx, dy).a;
        }
        if (alpha < material.emissiveFactorAlphaCutoff.w) { discard; }
    }
}


fn distributionGGX(normal : vec3<f32>, halfVector : vec3<f32>,
                   roughness : f32) -> f32 {
    let alpha = roughness * roughness;
    let alphaSquared = alpha * alpha;
    let normalDotHalf = max(dot(normal, halfVector), 0.0);
    let denominatorTerm = normalDotHalf * normalDotHalf
        * (alphaSquared - 1.0) + 1.0;
    return alphaSquared
        / max(3.141592653589793 * denominatorTerm * denominatorTerm, 1.0e-6);
}

fn geometrySchlickGGX(normalDotDirection : f32, roughness : f32) -> f32 {
    let shifted = roughness + 1.0;
    let k = shifted * shifted * 0.125;
    return normalDotDirection
        / max(normalDotDirection * (1.0 - k) + k, 1.0e-6);
}

fn geometrySmith(normal : vec3<f32>, viewDirection : vec3<f32>,
                 lightDirection : vec3<f32>, roughness : f32) -> f32 {
    return geometrySchlickGGX(max(dot(normal, viewDirection), 0.0), roughness)
        * geometrySchlickGGX(max(dot(normal, lightDirection), 0.0), roughness);
}

fn fresnelSchlick(cosine : f32, f0 : vec3<f32>) -> vec3<f32> {
    return f0 + (vec3<f32>(1.0) - f0)
        * pow(clamp(1.0 - cosine, 0.0, 1.0), 5.0);
}

fn fresnelSchlickRoughness(cosine : f32, f0 : vec3<f32>,
                          roughness : f32) -> vec3<f32> {
    return f0 + (max(vec3<f32>(1.0 - roughness), f0) - f0)
        * pow(clamp(1.0 - cosine, 0.0, 1.0), 5.0);
}

fn directionToEquirectangular(directionInput : vec3<f32>) -> vec2<f32> {
    let direction = safeNormalize(directionInput, vec3<f32>(0.0, 1.0, 0.0));
    let longitude = atan2(direction.z, direction.x);
    let latitude = atan2(clamp(direction.y, -1.0, 1.0),
                         max(length(direction.xz), 1.0e-6));
    return vec2<f32>(longitude * 0.15915494309189535 + 0.5,
                     0.5 - latitude * 0.3183098861837907);
}

fn sampleEnvironment(direction : vec3<f32>, lod : f32) -> vec3<f32> {
    return textureSampleLevel(environmentTexture, environmentSampler,
                              directionToEquirectangular(direction), lod).rgb;
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
    let numerator = color * (color + vec3<f32>(0.0245786))
        - vec3<f32>(0.000090537);
    let denominator = color * ((color + vec3<f32>(0.432951)) * 0.983729)
        + vec3<f32>(0.238081);
    return clamp(outputMatrix * (numerator / denominator),
                 vec3<f32>(0.0), vec3<f32>(1.0));
}

fn linearToSrgb(linear : vec3<f32>) -> vec3<f32> {
    let low = linear * 12.92;
    let high = 1.055 * pow(max(linear, vec3<f32>(0.0)),
                           vec3<f32>(1.0 / 2.4)) - vec3<f32>(0.055);
    return select(high, low, linear <= vec3<f32>(0.0031308));
}

fn shadeLinear(input : VertexOutput, frontFacing : bool) -> vec4<f32> {
    // Material selection is flat per primitive but non-uniform across a quad.
    // Compute derivatives before any material-dependent branch, then use the
    // explicit-gradient sampling operations inside those branches. Plain
    // textureSample is invalid in non-uniform control flow on browser WebGPU.
    let texCoordDx = dpdx(input.texCoord);
    let texCoordDy = dpdy(input.texCoord);
    let material = materials[input.materialIndex];
    if (!frontFacing && material.flags.y == 0u) {
        discard;
    }
    let textureMask = material.flags.w;
    var baseColorSample = vec4<f32>(1.0);
    if ((textureMask & 1u) != 0u) {
        baseColorSample = textureSampleGrad(
            baseColorTexture, materialSampler, input.texCoord,
            texCoordDx, texCoordDy);
    }
    let baseColor = material.baseColorFactor * input.tintColor
        * baseColorSample;
    let alpha = clamp(baseColor.a, 0.0, 1.0);
    if (material.flags.x == 1u
        && alpha < material.emissiveFactorAlphaCutoff.w) {
        discard;
    }

    let cameraDelta = uniforms.cameraPosition.xyz - input.worldPosition;
    let distanceToCamera = length(cameraDelta);
    if (uniforms.cameraPosition.w > 0.5) {
        let dimensions = textureDimensions(rayDepth, 0);
        let pixel = clamp(vec2<i32>(input.position.xy), vec2<i32>(0),
                          vec2<i32>(dimensions) - vec2<i32>(1));
        let terrainDistance = textureLoad(rayDepth, pixel, 0).x;
        if (terrainDistance >= 0.0
            && distanceToCamera > terrainDistance + 0.02) {
            discard;
        }
    }

    var normal = safeNormalize(input.worldNormal, vec3<f32>(0.0, 1.0, 0.0));
    if (!frontFacing && material.flags.y != 0u) {
        normal = -normal;
    }
    let geometricNormal = normal;
    if ((textureMask & 2u) != 0u) {
        let tangent = safeNormalize(
            input.worldTangent.xyz - normal * dot(normal, input.worldTangent.xyz),
            vec3<f32>(1.0, 0.0, 0.0));
        let bitangent = safeNormalize(
            cross(normal, tangent) * input.worldTangent.w,
            vec3<f32>(0.0, 0.0, 1.0));
        let encodedNormal = textureSampleGrad(
            normalTexture, materialSampler, input.texCoord,
            texCoordDx, texCoordDy).xyz;
        let tangentNormal = safeNormalize(
            vec3<f32>((encodedNormal.xy * 2.0 - vec2<f32>(1.0))
                      * material.metallicRoughnessNormalOcclusion.z,
                      encodedNormal.z * 2.0 - 1.0),
            vec3<f32>(0.0, 0.0, 1.0));
        normal = safeNormalize(
            mat3x3<f32>(tangent, bitangent, normal) * tangentNormal,
            normal);
    }
    let viewDirection = safeNormalize(
        cameraDelta, vec3<f32>(0.0, 0.0, 1.0));
    let lightDirection = safeNormalize(
        uniforms.lightDirectionFogDensity.xyz, vec3<f32>(0.0, 1.0, 0.0));
    let halfVector = safeNormalize(
        viewDirection + lightDirection, normal);
    let normalDotLight = max(dot(normal, lightDirection), 0.0);
    let normalDotView = max(dot(normal, viewDirection), 0.0);

    let albedo = max(baseColor.rgb, vec3<f32>(0.0));
    var metallicRoughnessSample = vec4<f32>(1.0);
    if ((textureMask & 4u) != 0u) {
        metallicRoughnessSample = textureSampleGrad(
            metallicRoughnessTexture, materialSampler, input.texCoord,
            texCoordDx, texCoordDy);
    }
    let metallic = clamp(material.metallicRoughnessNormalOcclusion.x
                         * metallicRoughnessSample.b, 0.0, 1.0);
    let roughness = clamp(material.metallicRoughnessNormalOcclusion.y
                          * metallicRoughnessSample.g, 0.045, 1.0);
    let f0 = mix(vec3<f32>(0.04), albedo, metallic);
    let fresnel = fresnelSchlick(max(dot(halfVector, viewDirection), 0.0), f0);
    let distribution = distributionGGX(normal, halfVector, roughness);
    let geometry = geometrySmith(normal, viewDirection, lightDirection, roughness);
    let specular = distribution * geometry * fresnel
        / max(4.0 * normalDotView * normalDotLight, 1.0e-5);
    let diffuseWeight = (vec3<f32>(1.0) - fresnel) * (1.0 - metallic);
    let sunRadiance = uniforms.sunColorIntensity.rgb
        * max(uniforms.sunColorIntensity.w, 0.0);
    let direct = (diffuseWeight * albedo * 0.3183098861837907 + specular)
        * sunRadiance * normalDotLight * sunVisibility(input.worldPosition, geometricNormal, lightDirection);

    let reflectionDirection = reflect(-viewDirection, normal);
    let ambientScale = uniforms.ambientColorIntensity.rgb
        * max(uniforms.ambientColorIntensity.w, 0.0);
    var ambient = vec3<f32>(0.0);
    if (uniforms.environmentParams.x > 0.5) {
        let filteredLod = roughness * f32(textureNumLevels(filteredSpecular) - 1u);
        let reflected = textureSampleLevel(filteredSpecular, filteredSampler, reflectionDirection, filteredLod).rgb;
        let irradianceOverPi = textureSampleLevel(filteredDiffuse, filteredSampler, normal, 0.0).rgb;
        // The clamp sampler keeps grazing/normal incidence at the LUT edges.
        let brdf = textureSampleLevel(environmentBrdf, filteredSampler,
            vec2<f32>(normalDotView, roughness), 0.0).rg;
        // Single-scattering split sum; use the same integrated Fresnel energy
        // for the diffuse remainder. No extra /pi: the diffuse cube stores E/pi.
        let specularEnergy = clamp(f0 * brdf.x + vec3<f32>(brdf.y), vec3<f32>(0.0), vec3<f32>(1.0));
        let diffuseEnergy = (vec3<f32>(1.0) - specularEnergy) * (1.0 - metallic);
        ambient = (diffuseEnergy * albedo * irradianceOverPi + specularEnergy * reflected) * ambientScale;
    } else {
        let mipCount = textureNumLevels(environmentTexture);
        let maximumLod = f32(max(mipCount, 1u) - 1u);
        let diffuseEnvironment = sampleEnvironment(normal, maximumLod);
        let specularEnvironment = sampleEnvironment(
            reflectionDirection, roughness * maximumLod);
        let environmentFresnel = fresnelSchlickRoughness(
            normalDotView, f0, roughness);
        let environmentDiffuseWeight = (vec3<f32>(1.0) - environmentFresnel)
            * (1.0 - metallic);
        ambient = (environmentDiffuseWeight * albedo * diffuseEnvironment
            + environmentFresnel * specularEnvironment) * ambientScale;
    }

    var emissiveSample = vec3<f32>(1.0);
    if ((textureMask & 8u) != 0u) {
        emissiveSample = textureSampleGrad(
            emissiveTexture, materialSampler, input.texCoord,
            texCoordDx, texCoordDy).rgb;
    }
    let emissive = max(material.emissiveFactorAlphaCutoff.rgb, vec3<f32>(0.0))
        * max(input.tintColor.rgb, vec3<f32>(0.0)) * emissiveSample
        + vec3<f32>(max(input.emissiveBoost, 0.0));
    let unlit = albedo + emissive;
    let pbr = direct + ambient + emissive;
    var color = select(pbr, unlit, material.flags.z != 0u);

    let fogAmount = clamp(
        1.0 - exp(-max(uniforms.lightDirectionFogDensity.w, 0.0)
                  * distanceToCamera), 0.0, 1.0);
    color = mix(color, max(uniforms.fogColorExposure.rgb, vec3<f32>(0.0)),
                fogAmount);
    let outputAlpha = select(1.0, alpha, material.flags.x == 2u);
    return vec4<f32>(color, outputAlpha);
}

@fragment fn fs(input : VertexOutput,
    @builtin(front_facing) frontFacing : bool) -> @location(0) vec4<f32> {
    let linear = shadeLinear(input, frontFacing);
    return vec4<f32>(linearToSrgb(acesFilmic(
        linear.rgb * max(uniforms.fogColorExposure.w, 0.0))), linear.a);
}

struct OpaqueOutput {
    @location(0) color : vec4<f32>,
    @location(1) radialDepth : f32,
};
@fragment fn fsOpaqueHdr(input : VertexOutput,
    @builtin(front_facing) frontFacing : bool) -> OpaqueOutput {
    return OpaqueOutput(shadeLinear(input, frontFacing),
        distance(uniforms.cameraPosition.xyz, input.worldPosition));
}
