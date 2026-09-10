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
