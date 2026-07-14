const SHAPE_COUNT : u32 = 5u;
const WORKGROUP_SIZE : u32 = 256u;

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

fn visible_shape(body : u32) -> u32 {
    if (body >= cull.counts.x) { return 0u; }
    let pose = poses[body];
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

var<workgroup> scanScratch : array<u32, 256>;

@compute @workgroup_size(256)
fn cull_blocks(@builtin(global_invocation_id) gid : vec3<u32>,
               @builtin(local_invocation_id) lid : vec3<u32>,
               @builtin(workgroup_id) group : vec3<u32>) {
    let body = gid.x;
    let encodedShape = visible_shape(body);
    if (body < cull.counts.x) { visibility[body] = encodedShape; }

    for (var shape = 0u; shape < SHAPE_COUNT; shape = shape + 1u) {
        let predicate = select(0u, 1u, encodedShape == shape + 1u);
        scanScratch[lid.x] = predicate;
        workgroupBarrier();
        var offset = 1u;
        while (offset < WORKGROUP_SIZE) {
            var addend = 0u;
            if (lid.x >= offset) { addend = scanScratch[lid.x - offset]; }
            workgroupBarrier();
            scanScratch[lid.x] += addend;
            workgroupBarrier();
            offset = offset << 1u;
        }
        if (predicate != 0u && body < cull.counts.x) {
            localOffsets[body] = scanScratch[lid.x] - 1u;
        }
        if (lid.x == WORKGROUP_SIZE - 1u) {
            blockSums[group.x * SHAPE_COUNT + shape] = scanScratch[lid.x];
        }
        workgroupBarrier();
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
