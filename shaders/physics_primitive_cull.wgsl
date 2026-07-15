const SHAPE_COUNT : u32 = 5u;
const WORKGROUP_SIZE : u32 = 256u;
const BODY_ALIVE : u32 = 1u << 20u;
const WORLD_SECTOR_SIZE : f32 = 256.0;
const INVALID_SECTOR_DELTA : i32 = 2147483647;

struct BodyPose {
    position_invMass : vec4<f32>,
    orientation : vec4<f32>,
};

struct BodyShape {
    dimensions_type : vec4<f32>,
    invInertia_material : vec4<f32>,
};

struct IndirectDrawArgs {
    indexCount : u32,
    instanceCount : u32,
    firstIndex : u32,
    baseVertex : i32,
    firstInstance : u32,
};

struct CullUniforms {
    planes : array<vec4<f32>, 6>,
    counts : vec4<u32>,
    cameraSector : vec4<i32>,
};

@group(0) @binding(0) var<storage, read> poses : array<BodyPose>;
@group(0) @binding(1) var<storage, read> shapes : array<BodyShape>;
@group(0) @binding(2) var<storage, read_write> visibility : array<u32>;
@group(0) @binding(3) var<storage, read_write> localOffsets : array<u32>;
@group(0) @binding(4) var<storage, read_write> blockSums : array<u32>;
@group(0) @binding(5) var<storage, read_write> blockPrefix : array<u32>;
@group(0) @binding(6) var<storage, read_write> visibleBodyIds : array<u32>;
@group(0) @binding(7) var<storage, read_write> indirectArgs : array<IndirectDrawArgs>;
@group(0) @binding(8) var<uniform> cull : CullUniforms;
@group(0) @binding(9) var<storage, read> metadata : array<vec4<i32>>;
@group(0) @binding(10) var<storage, read_write> cameraRelativePoses : array<BodyPose>;

fn bounded_sector_delta(reference : i32, other : i32,
                        maximum : u32) -> i32 {
    if (other >= reference) {
        let wide = bitcast<u32>(other) - bitcast<u32>(reference);
        if (wide > maximum) { return INVALID_SECTOR_DELTA; }
        return i32(wide);
    }
    let wide = bitcast<u32>(reference) - bitcast<u32>(other);
    if (wide > maximum) { return INVALID_SECTOR_DELTA; }
    return -i32(wide);
}

@compute @workgroup_size(256)
fn rebase_poses(@builtin(global_invocation_id) gid : vec3<u32>) {
    let body = gid.x;
    if (body >= cull.counts.x) { return; }
    let source = poses[body];
    let bodyMetadata = metadata[body];
    let maximum = u32(max(cull.cameraSector.w, 0));
    let delta = vec3<i32>(
        bounded_sector_delta(cull.cameraSector.x, bodyMetadata.x, maximum),
        bounded_sector_delta(cull.cameraSector.y, bodyMetadata.y, maximum),
        bounded_sector_delta(cull.cameraSector.z, bodyMetadata.z, maximum));
    let valid = all(delta != vec3<i32>(INVALID_SECTOR_DELTA))
        && (u32(bodyMetadata.w) & BODY_ALIVE) != 0u;
    var outputPose = source;
    if (valid) {
        outputPose.position_invMass = vec4<f32>(
            source.position_invMass.xyz
                + vec3<f32>(delta) * WORLD_SECTOR_SIZE,
            source.position_invMass.w);
    } else {
        outputPose.position_invMass.w = -1.0;
    }
    cameraRelativePoses[body] = outputPose;
}

fn visible_shape(body : u32) -> u32 {
    if (body >= cull.counts.x) { return 0u; }
    let pose = poses[body];
    if (pose.position_invMass.w < 0.0) { return 0u; }
    let shape = shapes[body];
    let dimensions = abs(shape.dimensions_type.xyz);
    if (max(dimensions.x, max(dimensions.y, dimensions.z)) <= 0.0) {
        return 0u;
    }
    let shapeIndex = u32(clamp(shape.dimensions_type.w, 0.0,
                               f32(SHAPE_COUNT - 1u)));
    let radius = 0.5 * length(dimensions);
    for (var plane = 0u; plane < 6u; plane = plane + 1u) {
        let equation = cull.planes[plane];
        let distance = dot(equation.xyz, pose.position_invMass.xyz) + equation.w;
        if (distance < -radius - 0.001) { return 0u; }
    }
    return shapeIndex + 1u;
}

var<workgroup> scanScratchFirstThree : array<u32, 256>;
var<workgroup> scanScratchLastTwo : array<u32, 256>;

@compute @workgroup_size(256)
fn cull_blocks(@builtin(global_invocation_id) gid : vec3<u32>,
               @builtin(local_invocation_id) lid : vec3<u32>,
               @builtin(workgroup_id) group : vec3<u32>) {
    let body = gid.x;
    let encodedShape = visible_shape(body);
    if (body < cull.counts.x) { visibility[body] = encodedShape; }

    // Nine bits hold every possible per-shape count in this 256-lane group.
    // Packed integer addition therefore scans all five one-hot predicates
    // without carries crossing a field boundary.
    scanScratchFirstThree[lid.x] =
          select(0u, 1u, encodedShape == 1u)
        | select(0u, 1u << 9u, encodedShape == 2u)
        | select(0u, 1u << 18u, encodedShape == 3u);
    scanScratchLastTwo[lid.x] =
          select(0u, 1u, encodedShape == 4u)
        | select(0u, 1u << 9u, encodedShape == 5u);
    workgroupBarrier();
    var offset = 1u;
    while (offset < WORKGROUP_SIZE) {
        var addendFirstThree = 0u;
        var addendLastTwo = 0u;
        if (lid.x >= offset) {
            addendFirstThree = scanScratchFirstThree[lid.x - offset];
            addendLastTwo = scanScratchLastTwo[lid.x - offset];
        }
        workgroupBarrier();
        scanScratchFirstThree[lid.x] += addendFirstThree;
        scanScratchLastTwo[lid.x] += addendLastTwo;
        workgroupBarrier();
        offset = offset << 1u;
    }
    if (encodedShape != 0u && body < cull.counts.x) {
        var prefix = 0u;
        if (encodedShape <= 3u) {
            prefix = (scanScratchFirstThree[lid.x]
                >> ((encodedShape - 1u) * 9u)) & 0x1ffu;
        } else {
            prefix = (scanScratchLastTwo[lid.x]
                >> ((encodedShape - 4u) * 9u)) & 0x1ffu;
        }
        localOffsets[body] = prefix - 1u;
    }
    if (lid.x == WORKGROUP_SIZE - 1u) {
        for (var shape = 0u; shape < 3u; shape += 1u) {
            blockSums[group.x * SHAPE_COUNT + shape] =
                (scanScratchFirstThree[lid.x] >> (shape * 9u)) & 0x1ffu;
        }
        for (var shape = 3u; shape < SHAPE_COUNT; shape += 1u) {
            blockSums[group.x * SHAPE_COUNT + shape] =
                (scanScratchLastTwo[lid.x] >> ((shape - 3u) * 9u)) & 0x1ffu;
        }
    }
}

@compute @workgroup_size(1)
fn scan_blocks(@builtin(global_invocation_id) gid : vec3<u32>) {
    if (gid.x != 0u) { return; }
    for (var shape = 0u; shape < SHAPE_COUNT; shape = shape + 1u) {
        var sum = 0u;
        for (var block = 0u; block < cull.counts.y; block = block + 1u) {
            let index = block * SHAPE_COUNT + shape;
            blockPrefix[index] = sum;
            sum += blockSums[index];
        }
        indirectArgs[shape].instanceCount = sum;
    }
}

@compute @workgroup_size(256)
fn scatter_visible(@builtin(global_invocation_id) gid : vec3<u32>,
                   @builtin(workgroup_id) group : vec3<u32>) {
    let body = gid.x;
    if (body >= cull.counts.x) { return; }
    let encodedShape = visibility[body];
    if (encodedShape == 0u) { return; }
    let shape = encodedShape - 1u;
    let output = shape * cull.counts.z
        + blockPrefix[group.x * SHAPE_COUNT + shape]
        + localOffsets[body];
    visibleBodyIds[output] = body;
}
